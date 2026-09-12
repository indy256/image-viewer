#define main viewerMain
#include "../main.cpp"
#undef main

#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

static QByteArray encode(const QImage &image, const char *format)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    writer.setQuality(100);
    require(writer.write(image), "Image encoding failed");
    return bytes;
}

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot create test image");
    require(file.write(bytes) == bytes.size(), "Cannot write test image");
}

static bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 10000) {
        QApplication::processEvents();
        if (condition())
            return true;
        QThread::msleep(10);
    }
    return false;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QImageReader::setAllocationLimit(1024);
    require(QImageReader::supportedImageFormats().contains("jp2"), "JP2 plugin not linked");
    require(app.arguments().size() == 2, "Missing reference JP2 fixture");
    QImageReader reference(app.arguments().at(1), "jp2");
    require(reference.read().size() == QSize(498, 80), "Reference JP2 fixture failed to decode");
    require(QImage::fromData(QByteArray("invalid JP2"), "jp2").isNull(),
            "Corrupt JP2 should fail decoding");
    QImage red(64, 48, QImage::Format_RGB32);
    red.fill(Qt::red);
    QImage blue(64, 48, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    const QByteArray redJp2 = encode(red, "jp2");
    const QByteArray blueJp2 = encode(blue, "jp2");
    const QByteArray jpeg = encode(red, "jpeg");
    const QImage roundTrip = QImage::fromData(redJp2, "jp2");
    require(roundTrip.size() == red.size() && roundTrip.pixelColor(32, 24) == QColor(Qt::red),
            "RGB JP2 decoding failed");
    QImage gray(64, 48, QImage::Format_Indexed8);
    QList<QRgb> grayscale;
    for (int i = 0; i < 256; ++i)
        grayscale.append(qRgb(i, i, i));
    gray.setColorTable(grayscale);
    gray.fill(128);
    const QImage decodedGray = QImage::fromData(encode(gray, "jp2"), "jp2");
    require(!decodedGray.isNull() && decodedGray.pixelColor(32, 24) == QColor(128, 128, 128),
            "Grayscale JP2 decoding failed");

    QTemporaryDir folder;
    require(folder.isValid(), "Cannot create temporary directory");
    const QString first = folder.filePath(QString::fromUtf8("a-фото.JPG"));
    const QString second = folder.filePath("b.JP2");
    writeFile(first, jpeg);
    writeFile(second, blueJp2);
    // Enough JP2 neighbors to exercise concurrent background scheduling.
    for (int i = 0; i < 12; ++i)
        writeFile(folder.filePath(QString("d%1.jp2").arg(i)), blueJp2);

    ImageViewer viewer({"iv", second});
    viewer.resize(320, 240);
    viewer.show();
    auto isBlue = [&] {
        return viewer.grab().toImage().pixelColor(160, 120) == QColor(Qt::blue);
    };
    auto key = [&](int code) {
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
        QApplication::sendEvent(&viewer, &event);
    };
    require(waitFor(isBlue), "Opening uppercase JP2 failed");
    require(viewer.windowTitle().contains("b.JP2 (2/14)"), "Mixed image ordering failed");
    key(Qt::Key_Left);
    require(waitFor([&] { return viewer.grab().toImage().pixelColor(160,120).red() > 250; }),
            "Navigation from JP2 to JPEG failed");
    key(Qt::Key_Right);
    require(waitFor(isBlue), "Navigation back to JP2 failed");

    writeFile(folder.filePath("c.jp2"), redJp2);
    require(waitFor([&] { return viewer.windowTitle().contains("(2/15)"); }),
            "Added JP2 file was not discovered");
    key(Qt::Key_Right);
    require(waitFor([&] { return viewer.grab().toImage().pixelColor(160,120) == QColor(Qt::red); }),
            "New JP2 did not decode");
    writeFile(folder.filePath("c.jp2"), QByteArray("invalid JP2"));
    require(waitFor([&] { return viewer.grab().toImage().pixelColor(10,10) == QColor(28,28,28); }),
            "Modified JP2 did not invalidate its cached image");
    key(Qt::Key_Right);
    require(waitFor(isBlue), "Navigation after corrupt JP2 failed");
    std::cout << "PASS: JP2 decoding, mixed navigation, preloading, live updates, corrupt file recovery\n";
}
