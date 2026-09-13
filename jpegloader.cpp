// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only
#include "jpegloader.h"

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QTransform>
#include <turbojpeg.h>

#include <climits>
#include <memory>

QImage loadJpeg(const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return {};
    }
    QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = file.errorString();
        return {};
    }
    std::unique_ptr<void, decltype(&tj3Destroy)> decoder(tj3Init(TJINIT_DECOMPRESS), tj3Destroy);
    if (!decoder) {
        error = QStringLiteral("Cannot initialize JPEG decoder");
        return {};
    }
    const auto handle = decoder.get();
    const auto bytes = reinterpret_cast<const unsigned char *>(data.constData());
    const int limit = QImageReader::allocationLimit();
    if (tj3Set(handle, TJPARAM_MAXMEMORY, limit) < 0
        || tj3Set(handle, TJPARAM_SAVEMARKERS, 4) < 0
        || tj3DecompressHeader(handle, bytes, size_t(data.size())) < 0) {
        error = QString::fromUtf8(tj3GetErrorStr(handle));
        return {};
    }
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader metadata(&buffer, "jpeg");
    metadata.setAutoTransform(true);
    const int colorspace = tj3Get(handle, TJPARAM_COLORSPACE);
    // Qt retains its handling of CMYK profiles and higher-precision samples.
    if (tj3Get(handle, TJPARAM_PRECISION) != 8
        || colorspace == TJCS_CMYK || colorspace == TJCS_YCCK) {
        QImage image = metadata.read();
        error = metadata.errorString();
        return image;
    }
    const int width = tj3Get(handle, TJPARAM_JPEGWIDTH);
    const int height = tj3Get(handle, TJPARAM_JPEGHEIGHT);
    if (width <= 0 || height <= 0 || width > INT_MAX / 4
        || (limit > 0 && quint64(width) * quint64(height) * 4 > quint64(limit) * 1024 * 1024)) {
        error = QStringLiteral("JPEG exceeds image allocation limit");
        return {};
    }
    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        error = QStringLiteral("Cannot allocate JPEG image");
        return {};
    }
    const int pixelFormat = Q_BYTE_ORDER == Q_LITTLE_ENDIAN ? TJPF_BGRA : TJPF_ARGB;
    if (tj3Decompress8(handle, bytes, size_t(data.size()), image.bits(),
                     int(image.bytesPerLine()), pixelFormat) < 0) {
        error = QString::fromUtf8(tj3GetErrorStr(handle));
        return {};
    }
    unsigned char *profile = nullptr;
    size_t profileSize = 0;
    if (tj3GetICCProfile(handle, &profile, &profileSize) == 0 && profile) {
        image.setColorSpace(QColorSpace::fromIccProfile(
            QByteArray(reinterpret_cast<const char *>(profile), qsizetype(profileSize))));
    }
    tj3Free(profile);
    const auto orientation = metadata.transformation();
    image = image.mirrored(orientation.testFlag(QImageIOHandler::TransformationMirror),
                           orientation.testFlag(QImageIOHandler::TransformationFlip));
    if (orientation.testFlag(QImageIOHandler::TransformationRotate90))
        image = image.transformed(QTransform().rotate(90));
    error.clear();
    return image;
}
