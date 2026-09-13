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
#include <future>
#include <iostream>
#include <vector>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

static QByteArray encode(const QImage &image, const char *format, int quality = 100)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    writer.setQuality(quality);
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

static bool hasTransparentPixel(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alpha() < 255)
                return true;
    return false;
}

static void checkConcurrentDecoding(const QByteArray &bytes, const char *format,
                                    const QImage &expected)
{
    std::vector<std::future<QImage>> decodes;
    for (int i = 0; i < 4; ++i)
        decodes.push_back(std::async(std::launch::async, [bytes, format] {
            return QImage::fromData(bytes, format);
        }));
    for (auto &decode : decodes)
        require(decode.get() == expected, qPrintable(QString("Concurrent %1 decoding failed").arg(format)));
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QImageReader::setAllocationLimit(1024);
    require(QImageReader::supportedImageFormats().contains("jp2"), "JP2 plugin not linked");
    require(QImageReader::supportedImageFormats().contains("webp"), "WebP plugin not linked");
    require(QImageReader::supportedImageFormats().contains("heic"), "HEIC plugin not linked");
    require(QImageReader::supportedImageFormats().contains("avif"), "AVIF plugin not linked");
    require(app.arguments().size() == 3, "Missing fixture directories");
    auto heicFixture = [&](const QString &name) {
        QFile file(QDir(app.arguments().at(2)).filePath(name));
        require(file.open(QIODevice::ReadOnly), "Cannot open HEIC fixture");
        return file.readAll();
    };
    const QByteArray heic = heicFixture("rainbow-451x461.heic");
    const QByteArray avif = heicFixture("clap_cropped.avif");
    const QImage decodedAvif = QImage::fromData(avif, "avif");
    require(decodedAvif.size() == QSize(64,64), "AVIF decoding/cropping failed");
    require(QImage::fromData(avif).size() == decodedAvif.size(), "AVIF detection failed");
    require(QImage::fromData(avif.left(64), "avif").isNull(), "Truncated AVIF should fail");
    const QImage alphaAvif = QImage::fromData(heicFixture("simple_osm_tile_alpha.avif"), "avif");
    require(!alphaAvif.isNull(), "AVIF alpha fixture failed to decode");
    require(hasTransparentPixel(alphaAvif), "AVIF transparency was lost");
    checkConcurrentDecoding(avif, "avif", decodedAvif);
    const QImage decodedHeic = QImage::fromData(heic, "heic");
    require(decodedHeic.size() == QSize(451, 461), "HEIC decoding failed");
    require(QImage::fromData(heic, "heif").size() == decodedHeic.size(), "HEIF alias failed");
    require(QImage::fromData(heic.left(64), "heic").isNull(), "Truncated HEIC should fail");
    require(QImage::fromData(heicFixture("clap_cropped.heic"), "heic").size() == QSize(64,64),
            "HEIC crop was not applied");
    const QImage alphaHeic = QImage::fromData(heicFixture("with-alpha-512x512.heic"), "heic");
    require(alphaHeic.size() == QSize(512,512), "HEIC alpha fixture failed to decode");
    require(hasTransparentPixel(alphaHeic), "HEIC transparency was lost");
    checkConcurrentDecoding(heic, "heic", decodedHeic);
    QImageReader reference(QDir(app.arguments().at(1)).filePath("red.jp2"), "jp2");
    require(reference.read().size() == QSize(64, 48), "Reference JP2 fixture failed to decode");
    require(QImage::fromData(QByteArray("invalid JP2"), "jp2").isNull(),
            "Corrupt JP2 should fail decoding");
    QImage red(64, 48, QImage::Format_RGB32);
    red.fill(Qt::red);
    QImage blue(64, 48, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    auto fixture = [&](const QString &name) {
        QFile file(QDir(app.arguments().at(1)).filePath(name));
        require(file.open(QIODevice::ReadOnly), "Cannot open JP2 fixture");
        return file.readAll();
    };
    const QByteArray redJp2 = fixture("red.jp2");
    const QByteArray blueJp2 = fixture("blue.jp2");
    const QByteArray jpeg = encode(red, "jpeg");
    const QByteArray blueWebp = encode(blue, "webp");
    const QImage lossyWebp = QImage::fromData(encode(red, "webp", 75), "webp");
    require(lossyWebp.size() == red.size() && lossyWebp.pixelColor(32,24).red() > 245
                && lossyWebp.pixelColor(32,24).green() < 10
                && lossyWebp.pixelColor(32,24).blue() < 10,
            "Lossy WebP decoding failed");
    QImage transparent(64, 48, QImage::Format_ARGB32);
    transparent.fill(QColor(200, 100, 50, 128));
    const QImage decodedWebp = QImage::fromData(encode(transparent, "webp"), "webp");
    require(decodedWebp.size() == transparent.size()
                && decodedWebp.pixelColor(32, 24) == QColor(200, 100, 50, 128),
            "Transparent lossless WebP decoding failed");
    require(QImage::fromData(blueWebp.left(20), "webp").isNull(),
            "Truncated WebP should fail decoding");
    const QImage roundTrip = QImage::fromData(redJp2, "jp2");
    require(roundTrip.size() == red.size() && roundTrip.pixelColor(32, 24) == QColor(Qt::red),
            "RGB JP2 decoding failed");
    const QImage decodedGray = QImage::fromData(fixture("gray.jp2"), "jp2");
    require(!decodedGray.isNull() && decodedGray.pixelColor(32, 24) == QColor(128, 128, 128),
            "Grayscale JP2 decoding failed");
    auto checkPixel = [&](const QString &name, QColor expected) {
        const QImage image = QImage::fromData(fixture(name), "jp2");
        require(image.size() == QSize(4,4) && image.pixelColor(2,2) == expected,
                qPrintable("Pixel conversion failed: " + name));
    };
    checkPixel("rgba.jp2", QColor(200,100,50,128));
    checkPixel("gray16.jp2", QColor(128,128,128));
    checkPixel("signed16.jp2", QColor(0,0,0));
    checkPixel("sycc420.jp2", QColor(128,128,128));
    checkPixel("cmyk.jp2", QColor(255,0,0));
    std::vector<std::future<bool>> decodes;
    for (int i = 0; i < 8; ++i) {
        decodes.push_back(std::async(std::launch::async, [redJp2, blueJp2] {
            for (int j = 0; j < 20; ++j) {
                const bool red = j % 2 == 0;
                const QImage image = QImage::fromData(red ? redJp2 : blueJp2, "jp2");
                if (image.isNull() || image.pixelColor(32,24) != QColor(red ? Qt::red : Qt::blue))
                    return false;
            }
            return true;
        }));
    }
    for (auto &decode : decodes)
        require(decode.get(), "Concurrent JP2 decoding failed");

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

    auto wheel = [&](int vertical, int horizontal = 0) {
        QWheelEvent event(QPointF(160,120), viewer.mapToGlobal(QPoint(160,120)),
                          QPoint(), QPoint(horizontal,vertical), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&viewer, &event);
    };
    wheel(60);
    require(viewer.windowTitle().contains("(2/14)"), "Partial notch moved too early");
    wheel(60);
    require(viewer.windowTitle().contains("(1/14)"), "Wheel up did not select previous file");
    wheel(120);
    require(viewer.windowTitle().contains("(1/14)"), "Wheel wrapped past first file");
    wheel(-120);
    require(viewer.windowTitle().contains("(2/14)"), "Wheel down did not select next file");
    wheel(0,120);
    require(viewer.windowTitle().contains("(2/14)"), "Horizontal wheel changed selection");
    wheel(-2400);
    require(viewer.windowTitle().contains("(14/14)"), "Wheel did not stop at last file");
    wheel(1440);
    require(viewer.windowTitle().contains("(2/14)"), "Multiple wheel notches failed");
    require(waitFor(isBlue), "Wheel navigation did not display the image");

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
    writeFile(folder.filePath("e.WEBP"), blueWebp);
    require(waitFor([&] { return viewer.windowTitle().contains("/16)"); }),
            "Added uppercase WebP file was not discovered");
    wheel(-2400);
    require(waitFor([&] { return viewer.windowTitle().contains("e.WEBP (16/16)") && isBlue(); }),
            "Navigation to WebP failed");
    writeFile(folder.filePath("e.WEBP"), encode(red, "webp"));
    require(waitFor([&] { return viewer.grab().toImage().pixelColor(160,120) == QColor(Qt::red); }),
            "Modified WebP did not invalidate its cached image");
    ImageViewer webpViewer({"iv", folder.filePath("e.WEBP")});
    webpViewer.resize(320, 240);
    webpViewer.show();
    require(waitFor([&] { return webpViewer.grab().toImage().pixelColor(160,120) == QColor(Qt::red); }),
            "Opening WebP from command line failed");
    const QString heicPath = folder.filePath(QString::fromUtf8("f-\xD1\x84\xD0\xBE\xD1\x82\xD0\xBE.HEIC"));
    writeFile(heicPath, heic);
    require(waitFor([&] { return viewer.windowTitle().contains("/17)"); }),
            "Added uppercase HEIC file was not discovered");
    wheel(-120);
    require(waitFor([&] { return viewer.windowTitle().contains("(17/17)")
        && viewer.windowTitle().contains(QString::fromUtf8("451 \xC3\x97 461")); }),
            "Navigation to HEIC failed");
    ImageViewer heicViewer({"iv", heicPath});
    heicViewer.resize(320, 240);
    heicViewer.show();
    require(waitFor([&] { return heicViewer.windowTitle().contains(QString::fromUtf8("451 \xC3\x97 461")); }),
            "Opening HEIC from command line failed");
    writeFile(heicPath, heicFixture("clap_cropped.heic"));
    require(waitFor([&] { return viewer.windowTitle().contains(QString::fromUtf8("64 \xC3\x97 64")); }),
            "Changed HEIC was not reloaded");
    const QString avifPath = folder.filePath("g.AVIF");
    writeFile(avifPath, avif);
    require(waitFor([&] { return viewer.windowTitle().contains("/18)"); }),
            "Added uppercase AVIF was not discovered");
    wheel(-120);
    require(waitFor([&] { return viewer.windowTitle().contains("g.AVIF (18/18)")
        && viewer.windowTitle().contains(QString::fromUtf8("64 \xC3\x97 64")); }),
            "Navigation to AVIF failed");
    ImageViewer avifViewer({"iv", avifPath});
    avifViewer.resize(320, 240);
    avifViewer.show();
    require(waitFor([&] { return avifViewer.windowTitle().contains(QString::fromUtf8("64 \xC3\x97 64")); }),
            "Opening AVIF from command line failed");
    writeFile(avifPath, QByteArray("invalid AVIF"));
    require(waitFor([&] { return viewer.grab().toImage().pixelColor(10,10) == QColor(28,28,28); }),
            "Changed AVIF was not reloaded");
    key(Qt::Key_Left);
    require(waitFor([&] { return viewer.windowTitle().contains("(17/18)")
        && viewer.windowTitle().contains(QString::fromUtf8("64 \xC3\x97 64")); }),
            "Navigation after corrupt AVIF failed");
    std::cout << "PASS: JP2/WebP/HEIC/AVIF decoding, navigation, preloading, live updates, corrupt file recovery\n";
}
