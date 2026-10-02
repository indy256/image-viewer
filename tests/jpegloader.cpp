// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only
#include "jpegloader.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QImageReader::setAllocationLimit(1024);
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;

    // Generate a synthetic fixture so the test needs no external image files.
    QImage source(32, 32, QImage::Format_RGB888);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor(x * 7, y * 7, (x + y) * 3));
    QByteArray clean;
    QBuffer buffer(&clean);
    if (!buffer.open(QIODevice::WriteOnly) || !source.save(&buffer, "JPEG", 90))
        return 1;

    int failures = 0;
    auto decode = [&](const char *name, const QByteArray &data, bool expectSuccess) {
        const QString path = directory.filePath(QString::fromLatin1(name) + ".jpg");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
            ++failures;
            return QImage();
        }
        file.close();
        QString error = QStringLiteral("stale error");
        QImage image = loadJpeg(path, error);
        if (expectSuccess ? (image.isNull() || !error.isEmpty())
                          : (!image.isNull() || error.isEmpty())) {
            std::fprintf(stderr, "%s: unexpected decode result: %s\n", name, qPrintable(error));
            ++failures;
        }
        return image;
    };

    const QImage reference = decode("clean", clean, true);
    QByteArray headerWarning = clean;
    headerWarning.insert(2, QByteArray(379, 'x'));
    QByteArray scanWarning = clean;
    scanWarning.insert(scanWarning.size() - 2, QByteArray(379, 'x'));
    for (const QImage &image : {decode("header-warning", headerWarning, true),
                               decode("scan-warning", scanWarning, true)}) {
        if (image != reference) {
            std::fprintf(stderr, "Recovered pixels differ from clean JPEG\n");
            ++failures;
        }
    }
    decode("invalid", "not a JPEG", false);
    decode("truncated-header", clean.left(20), false);
    // Valid header followed by an invalid marker must remain a fatal error.
    const qsizetype scan = clean.indexOf(QByteArray::fromHex("ffda"));
    if (scan < 0) {
        ++failures;
    } else {
        QByteArray fatal = clean;
        fatal[scan + 1] = char(0x02);
        decode("fatal-marker", fatal, false);
    }
    std::printf("JPEG regression tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
