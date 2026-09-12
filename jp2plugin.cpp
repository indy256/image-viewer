#include <QColorSpace>
#include <QDebug>
#include <QImageIOHandler>
#include <QImageReader>
#include <QIODevice>
#include <QtPlugin>

#include <openjpeg.h>
extern "C" {
#include "color.h"
}

#include <algorithm>
#include <climits>
#include <limits>
#include <memory>

namespace {

bool hasSignature(QIODevice *device)
{
    return device && device->peek(12) == QByteArray("\0\0\0\x0cjP  \r\n\x87\n", 12);
}

OPJ_SIZE_T readBytes(void *buffer, OPJ_SIZE_T count, void *context)
{
    auto *device = static_cast<QIODevice *>(context);
    const qint64 read = device->read(static_cast<char *>(buffer),
        static_cast<qint64>(std::min<OPJ_SIZE_T>(count, std::numeric_limits<qint64>::max())));
    return read > 0 ? static_cast<OPJ_SIZE_T>(read) : OPJ_SIZE_T(-1);
}

OPJ_BOOL seekTo(OPJ_OFF_T position, void *context)
{
    auto *device = static_cast<QIODevice *>(context);
    return position >= 0 && position <= device->size() && device->seek(position);
}

OPJ_OFF_T skipBytes(OPJ_OFF_T count, void *context)
{
    auto *device = static_cast<QIODevice *>(context);
    const qint64 position = device->pos();
    if (count < -position || count > device->size() - position)
        return -1;
    return device->seek(position + count) ? count : -1;
}

void reportError(const char *message, void *)
{
    qWarning().noquote() << "OpenJPEG:" << QString::fromUtf8(message).trimmed();
}

// Reject excessive raster/component allocations before decoding sample buffers.
// OpenJPEG's internal working memory is additional to this estimate.
bool validLayout(const opj_image_t &image)
{
    if (image.x1 <= image.x0 || image.y1 <= image.y0 || !image.comps
        || image.numcomps < 1 || image.numcomps > 4)
        return false;
    const quint64 width = image.x1 - image.x0;
    const quint64 height = image.y1 - image.y0;
    if (width > INT_MAX || height > INT_MAX)
        return false;
    const int limit = QImageReader::allocationLimit();
    quint64 remaining = limit > 0 ? quint64(limit) * 1024 * 1024
                                  : quint64(std::numeric_limits<qint64>::max());
    if (width * height > remaining / 4)
        return false;
    remaining -= width * height * 4;
    for (OPJ_UINT32 i = 0; i < image.numcomps; ++i) {
        const auto &component = image.comps[i];
        if (!component.w || !component.h || !component.dx || !component.dy
            || component.prec < 1 || component.prec > 31)
            return false;
        const quint64 samples = quint64(component.w) * component.h;
        // Allow a second component buffer for color conversion.
        if (samples > remaining / 8)
            return false;
        remaining -= samples * 8;
    }
    return true;
}

int sample8(const opj_image_comp_t &component, quint64 x, quint64 y)
{
    const qint64 column = std::clamp<qint64>(qint64(x / component.dx) - component.x0,
                                            0, component.w - 1);
    const qint64 row = std::clamp<qint64>(qint64(y / component.dy) - component.y0,
                                         0, component.h - 1);
    const qint64 maximum = (qint64(1) << component.prec) - 1;
    qint64 value = component.data[quint64(row) * component.w + column];
    if (component.sgnd)
        value += qint64(1) << (component.prec - 1);
    return int((std::clamp(value, qint64(0), maximum) * 255 + maximum / 2) / maximum);
}

class OpenJpegHandler final : public QImageIOHandler
{
public:
    bool canRead() const override { return hasSignature(device()); }

    bool read(QImage *output) override
    {
        if (!output || !device() || device()->isSequential() || !device()->seek(0))
            return false;
        const std::unique_ptr<opj_codec_t, decltype(&opj_destroy_codec)>
            codec(opj_create_decompress(OPJ_CODEC_JP2), opj_destroy_codec);
        const std::unique_ptr<opj_stream_t, decltype(&opj_stream_destroy)>
            stream(opj_stream_create(64 * 1024, OPJ_TRUE), opj_stream_destroy);
        if (!codec || !stream)
            return false;
        opj_set_error_handler(codec.get(), reportError, nullptr);
        opj_dparameters_t parameters;
        opj_set_default_decoder_parameters(&parameters);
        if (!opj_setup_decoder(codec.get(), &parameters))
            return false;
        // Parallelism is managed by the viewer's two background workers.
        if (!opj_codec_set_threads(codec.get(), 1))
            return false;
        opj_stream_set_user_data(stream.get(), device(), nullptr);
        opj_stream_set_user_data_length(stream.get(), device()->size());
        opj_stream_set_read_function(stream.get(), readBytes);
        opj_stream_set_seek_function(stream.get(), seekTo);
        opj_stream_set_skip_function(stream.get(), skipBytes);
        opj_image_t *rawImage = nullptr;
        const bool headerRead = opj_read_header(stream.get(), codec.get(), &rawImage);
        const std::unique_ptr<opj_image_t, decltype(&opj_image_destroy)>
            image(rawImage, opj_image_destroy);
        if (!headerRead || !image || !validLayout(*image))
            return false;
        if (!opj_decode(codec.get(), stream.get(), image.get())
            || !opj_end_decompress(codec.get(), stream.get()))
            return false;
        for (OPJ_UINT32 i = 0; i < image->numcomps; ++i)
            if (!image->comps[i].data)
                return false;

        const bool convertColor = image->color_space == OPJ_CLRSPC_SYCC
            || image->color_space == OPJ_CLRSPC_EYCC || image->color_space == OPJ_CLRSPC_CMYK;
        if (convertColor) {
            // The reference color converters use signed integer shifts.
            for (OPJ_UINT32 i = 0; i < image->numcomps; ++i)
                if (image->comps[i].prec > 16 || image->comps[i].sgnd)
                    return false;
        }
        switch (image->color_space) {
        case OPJ_CLRSPC_SYCC: color_sycc_to_rgb(image.get()); break;
        case OPJ_CLRSPC_EYCC: color_esycc_to_rgb(image.get()); break;
        case OPJ_CLRSPC_CMYK: color_cmyk_to_rgb(image.get()); break;
        default: break;
        }
        if (convertColor && image->color_space == OPJ_CLRSPC_SRGB) {
            for (OPJ_UINT32 i = 1; i < 3; ++i) {
                image->comps[i].x0 = image->comps[0].x0;
                image->comps[i].y0 = image->comps[0].y0;
            }
        }
        if (!validLayout(*image)
            || (image->color_space != OPJ_CLRSPC_SRGB && image->color_space != OPJ_CLRSPC_GRAY
                && image->color_space != OPJ_CLRSPC_UNSPECIFIED
                && image->color_space != OPJ_CLRSPC_UNKNOWN))
            return false;
        const bool gray = image->numcomps <= 2;
        const int channels = gray ? 1 : 3;
        const bool alpha = image->numcomps == unsigned(channels + 1);
        QImage result;
        if (!allocateImage(QSize(image->x1 - image->x0, image->y1 - image->y0),
                           QImage::Format_ARGB32, &result))
            return false;
        for (int y = 0; y < result.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                const quint64 sx = quint64(image->x0) + x, sy = quint64(image->y0) + y;
                const int r = sample8(image->comps[0], sx, sy);
                const int g = gray ? r : sample8(image->comps[1], sx, sy);
                const int b = gray ? r : sample8(image->comps[2], sx, sy);
                const int a = alpha ? sample8(image->comps[channels], sx, sy) : 255;
                line[x] = alpha && image->comps[channels].alpha == 2
                    ? qUnpremultiply(qRgba(r, g, b, a)) : qRgba(r, g, b, a);
            }
        }
        if (!convertColor && image->icc_profile_buf && image->icc_profile_len > 0
            && image->icc_profile_len <= INT_MAX) {
            const QColorSpace space = QColorSpace::fromIccProfile(QByteArray(
                reinterpret_cast<const char *>(image->icc_profile_buf), image->icc_profile_len));
            if (space.isValid()) {
                result.setColorSpace(space);
                result.convertToColorSpace(QColorSpace::SRgb);
            }
        }
        *output = std::move(result);
        return true;
    }
};
} // namespace

class OpenJpegPlugin final : public QImageIOPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "jp2plugin.json")
public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override
    {
        if (format == "jp2" || (format.isEmpty() && device && device->isReadable() && hasSignature(device)))
            return CanRead;
        return {};
    }
    QImageIOHandler *create(QIODevice *device, const QByteArray &format = {}) const override
    {
        auto *handler = new OpenJpegHandler;
        handler->setDevice(device);
        handler->setFormat(format);
        return handler;
    }
};

#include "jp2plugin.moc"
