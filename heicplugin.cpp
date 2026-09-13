// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only

#include <QColorSpace>
#include <QImageIOPlugin>
#include <QImageReader>
#include <QIODevice>
#include <libheif/heif.h>
#include <cstring>
#include <memory>

namespace {
bool hasHeifSignature(QIODevice *device)
{
    if (!device || !device->isReadable())
        return false;
    const QByteArray header = device->peek(12);
    if (header.size() < 12 || header.mid(4, 4) != "ftyp")
        return false;
    // libheif's quick check omits generic HEIF and the compact 'mif3' AVIF brand.
    const QByteArray brand = header.mid(8, 4);
    return brand == "heic" || brand == "heix" || brand == "hevc" || brand == "hevx"
        || brand == "avif" || brand == "avis" || brand == "mif1" || brand == "mif2"
        || brand == "mif3" || brand == "msf1";
}

struct HeifLibrary
{
    heif_error error = heif_init(nullptr);
    ~HeifLibrary() { if (error.code == heif_error_Ok) heif_deinit(); }
};

class HeicHandler final : public QImageIOHandler
{
public:
    bool canRead() const override { return hasHeifSignature(device()); }

    bool read(QImage *output) override
    {
        static const HeifLibrary library;
        if (library.error.code != heif_error_Ok || !output || !canRead())
            return false;
        // Bound both compressed input and decoded allocations before decoding.
        const int allocationLimit = QImageReader::allocationLimit();
        const quint64 budget = quint64(allocationLimit > 0 ? allocationLimit : 1024) * 1024 * 1024;
        const QByteArray bytes = device()->read(qint64(budget + 1));
        if (bytes.isEmpty() || quint64(bytes.size()) > budget)
            return false;
        std::unique_ptr<heif_context, decltype(&heif_context_free)>
            context(heif_context_alloc(), heif_context_free);
        if (!context)
            return false;
        auto *limits = heif_context_get_security_limits(context.get());
        limits->max_image_size_pixels = budget / 8;
        limits->max_memory_block_size = budget;
        limits->max_total_memory = budget;
        heif_context_set_max_decoding_threads(context.get(), 0);
        if (heif_context_read_from_memory_without_copy(context.get(), bytes.constData(),
                size_t(bytes.size()), nullptr).code != heif_error_Ok)
            return false;
        heif_image_handle *rawHandle = nullptr;
        const auto handleError = heif_context_get_primary_image_handle(context.get(), &rawHandle);
        std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>
            handle(rawHandle, heif_image_handle_release);
        if (handleError.code != heif_error_Ok || !handle)
            return false;
        const int width = heif_image_handle_get_width(handle.get());
        const int height = heif_image_handle_get_height(handle.get());
        if (width <= 0 || height <= 0 || quint64(width) * height > budget / 8)
            return false;
        std::unique_ptr<heif_decoding_options, decltype(&heif_decoding_options_free)>
            options(heif_decoding_options_alloc(), heif_decoding_options_free);
        if (!options)
            return false;
        options->convert_hdr_to_8bit = true;
        options->num_codec_threads = 1;
        heif_image *rawImage = nullptr;
        const auto decodeError = heif_decode_image(handle.get(), &rawImage, heif_colorspace_RGB,
                                                   heif_chroma_interleaved_RGBA, options.get());
        std::unique_ptr<heif_image, decltype(&heif_image_release)>
            image(rawImage, heif_image_release);
        if (decodeError.code != heif_error_Ok || !image)
            return false;
        const QSize size(heif_image_get_width(image.get(), heif_channel_interleaved),
                         heif_image_get_height(image.get(), heif_channel_interleaved));
        QImage result;
        if (size.width() <= 0 || size.height() <= 0
            || quint64(size.width()) * size.height() > budget / 8
            || !allocateImage(size, QImage::Format_RGBA8888, &result))
            return false;
        int stride = 0;
        const auto *pixels = heif_image_get_plane_readonly(image.get(), heif_channel_interleaved, &stride);
        const size_t rowBytes = size_t(size.width()) * 4;
        if (!pixels || stride < 0 || size_t(stride) < rowBytes)
            return false;
        for (int y = 0; y < size.height(); ++y)
            std::memcpy(result.scanLine(y), pixels + size_t(y) * stride, rowBytes);
        const size_t profileSize = heif_image_get_raw_color_profile_size(image.get());
        if (profileSize > 0 && profileSize <= limits->max_color_profile_size) {
            QByteArray profile(qsizetype(profileSize), Qt::Uninitialized);
            if (heif_image_get_raw_color_profile(image.get(), profile.data()).code == heif_error_Ok) {
                const auto colorSpace = QColorSpace::fromIccProfile(profile);
                if (colorSpace.isValid()) {
                    result.setColorSpace(colorSpace);
                    result.convertToColorSpace(QColorSpace::SRgb);
                }
            }
        }
        *output = std::move(result);
        return true;
    }
};
} // namespace

class HeicPlugin final : public QImageIOPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "heicplugin.json")
public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override
    {
        if (format == "heic" || format == "heif" || format == "avif"
            || (format.isEmpty() && hasHeifSignature(device)))
            return CanRead;
        return {};
    }
    QImageIOHandler *create(QIODevice *device, const QByteArray &format = {}) const override
    {
        auto *handler = new HeicHandler;
        handler->setDevice(device);
        handler->setFormat(format);
        return handler;
    }
};

#include "heicplugin.moc"
