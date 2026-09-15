// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only

#include "jpegloader.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QImageReader>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

class FullscreenCloseButton final : public QAbstractButton
{
public:
    explicit FullscreenCloseButton(QWidget *parent) : QAbstractButton(parent)
    {
        setFixedSize(48, 48);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Close"));
        setAccessibleName(tr("Close"));
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), isDown() ? QColor(170, 35, 35) : QColor(40, 40, 40, 220));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(160, 160, 160), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(17, 17, 31, 31);
        painter.drawLine(31, 17, 17, 31);
    }

    void leaveEvent(QEvent *event) override
    {
        hide();
        QAbstractButton::leaveEvent(event);
    }
};

class ImageViewer final : public QWidget
{
public:
    explicit ImageViewer(const QStringList &arguments)
    {
        resize(1000, 700);
        setMinimumSize(1, 1);
        setMouseTracking(true);
        connect(&closeButton_, &QAbstractButton::clicked, this, &QWidget::close);
        setWindowTitle(tr("Image Viewer"));
        qApp->installEventFilter(this);
        refreshTimer_.setSingleShot(true);
        refreshTimer_.setInterval(150);
        connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
                [this] { refreshTimer_.start(); });
        connect(&watcher_, &QFileSystemWatcher::fileChanged, this,
                [this] { refreshTimer_.start(); });
        connect(&refreshTimer_, &QTimer::timeout, this, [this] { refreshDirectory(); });
        if (arguments.size() != 2) {
            message_ = tr("Usage: iv <file.png|file.jpg|file.jp2|file.webp|file.heic|file.heif|file.avif>\n\nLeft / Right: previous / next image\nUp / Down: increase / decrease temporary gamma\nF: toggle full screen\ns: toggle temporary mild sharpening\nS (Shift+S): toggle temporary strong sharpening\n%1: copy image\nEsc: exit")
                           .arg(QKeySequence(QKeySequence::Copy).toString(QKeySequence::NativeText));
            return;
        }

        openFile(arguments.at(1));
    }

    void openFile(const QString &path)
    {
        const QFileInfo initial(path);
        if (!initial.isFile() || imageFormat(initial).isEmpty()) {
            image_ = QImage();
            message_ = tr("Not a supported image file (PNG, JPEG, JP2, WebP, HEIC/HEIF or AVIF): %1").arg(path);
            update();
            return;
        }

        refreshTimer_.stop();
        if (directory_ != initial.absolutePath()) {
            const QStringList watchedPaths = watcher_.directories() + watcher_.files();
            if (!watchedPaths.isEmpty())
                watcher_.removePaths(watchedPaths);
            cache_.clear();
            image_ = QImage();
            files_.clear();
            stamps_.clear();
            index_ = -1;
            directory_ = initial.absolutePath();
        }
        refreshDirectory(initial.absoluteFilePath());
    }

    void enterFullScreen()
    {
        if (isFullScreen())
            return;
        windowedState_ = windowState();
        windowedGeometry_ = isMaximized() ? normalGeometry() : geometry();
        fitPending_ = false;
#ifdef Q_OS_WIN
        // Windows needs an explicit frameless hint to cover the screen edges.
        // Other platforms let the window manager handle fullscreen decorations.
        // QWindow flags preserve the native window and mouse click tracking.
        winId();
        windowHandle()->setFlag(Qt::FramelessWindowHint, true);
#endif
        showFullScreen();
        refreshNativeFrame();
    }

    void toggleFullScreen()
    {
        dragPending_ = false;
        if (isFullScreen()) {
            const QRect screenArea = screen()->availableGeometry();
            showNormal();
#ifdef Q_OS_WIN
            windowHandle()->setFlag(Qt::FramelessWindowHint, false);
#endif
            refreshNativeFrame();
            restoreWindowedGeometry();
            if (!firstWindowedSwitch_ && windowedState_.testFlag(Qt::WindowMaximized))
                showMaximized();
            fitPending_ = firstWindowedSwitch_;
            fitWindowToImage();
            if (firstWindowedSwitch_) {
                const QRect frame = frameGeometry();
                move(pos() + screenArea.center() - frame.center());
                firstWindowedSwitch_ = false;
            }
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
            // Restore focus after Qt processes the windowed-mode changes.
            QTimer::singleShot(0, this, [this] {
                if (!isVisible() || isFullScreen())
                    return;
#ifdef Q_OS_MACOS
                // Cocoa must not steal focus from another application.
                if (QGuiApplication::applicationState() != Qt::ApplicationActive)
                    return;
#elif defined(Q_OS_LINUX)
                raise();
#endif
                activateWindow();
                setFocus(Qt::OtherFocusReason);
            });
#endif
        } else {
            enterFullScreen();
        }
    }

    ~ImageViewer() override
    {
        // Workers may post results to this widget until their decoding finishes.
        workers_.waitForDone();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        closeButton_.move(width() - closeButton_.width(), 0);
        closeButton_.hide();
        QWidget::resizeEvent(event);
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::WindowStateChange)
            closeButton_.hide();
        QWidget::changeEvent(event);
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == qApp && event->type() == QEvent::FileOpen) {
            const auto *openEvent = static_cast<QFileOpenEvent *>(event);
            if (!openEvent->url().isLocalFile())
                return false;
            openFile(openEvent->url().toLocalFile());
            if (isMinimized())
                showNormal();
            raise();
            activateWindow();
            setFocus(Qt::OtherFocusReason);
            event->accept();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragPending_ = !isFullScreen();
            dragStart_ = event->position().toPoint();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        closeButton_.setVisible(isFullScreen() && event->buttons() == Qt::NoButton
                                && closeButton_.geometry().contains(event->position().toPoint()));
        if (dragPending_ && event->buttons().testFlag(Qt::LeftButton)
            && (event->position().toPoint() - dragStart_).manhattanLength()
                   >= QApplication::startDragDistance()) {
            dragPending_ = false;
            if (!isFullScreen() && windowHandle() && windowHandle()->startSystemMove()) {
                event->accept();
                return;
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            dragPending_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            toggleFullScreen();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->matches(QKeySequence::Copy)) {
            if (!event->isAutoRepeat() && !image_.isNull())
                QApplication::clipboard()->setImage(image_);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
            if (!image_.isNull()) {
                const int gammaTenths = std::clamp(
                    gammaTenths_ + (event->key() == Qt::Key_Up ? 1 : -1), 1, 40);
                if (gammaTenths != gammaTenths_) {
                    gammaTenths_ = gammaTenths;
                    adjustedImage_ = QImage();
                    update();
                }
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_S) {
            if (!event->isAutoRepeat() && !image_.isNull()) {
                const bool uppercase = event->text().isEmpty()
                    ? event->modifiers().testFlag(Qt::ShiftModifier)
                    : event->text() == QLatin1String("S");
                const Sharpening requested = uppercase ? Sharpening::Strong : Sharpening::Mild;
                sharpening_ = sharpening_ == requested ? Sharpening::Off : requested;
                adjustedImage_ = QImage();
                update();
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_F) {
            if (!event->isAutoRepeat())
                toggleFullScreen();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            event->accept();
            close();
            return;
        }
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            navigate(event->key() == Qt::Key_Right ? 1 : -1);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const int delta = event->angleDelta().y();
        if (delta == 0) {
            event->ignore();
            return;
        }
        // Accumulate high-resolution wheel events into standard 120-unit notches.
        if ((delta > 0 && wheelRemainder_ < 0) || (delta < 0 && wheelRemainder_ > 0))
            wheelRemainder_ = 0;
        wheelRemainder_ += delta;
        const qsizetype steps = wheelRemainder_ / 120;
        wheelRemainder_ %= 120;
        if (steps != 0)
            navigate(-steps);
        event->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(28, 28, 28));
        if (!image_.isNull()) {
            const QSize fitted = image_.size().scaled(size(), Qt::KeepAspectRatio);
            const QRect target(QPoint((width() - fitted.width()) / 2,
                                      (height() - fitted.height()) / 2), fitted);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            if (sharpening_ != Sharpening::Off || gammaTenths_ != 10) {
                const QSize pixels = (fitted * devicePixelRatioF()).expandedTo(QSize(1, 1));
                if (adjustedImage_.size() != pixels) {
                    adjustedImage_ = image_.scaled(
                        pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    if (sharpening_ != Sharpening::Off)
                        adjustedImage_ = sharpen(adjustedImage_, sharpening_);
                    if (gammaTenths_ != 10)
                        adjustGamma(adjustedImage_, gammaTenths_);
                }
                painter.drawImage(target, adjustedImage_.isNull() ? image_ : adjustedImage_);
            } else {
                painter.drawImage(target, image_);
            }
        } else {
            painter.setPen(Qt::white);
            painter.drawText(rect().adjusted(24, 24, -24, -24),
                             Qt::AlignCenter | Qt::TextWordWrap, message_);
        }
    }

private:
    using FileStamp = QPair<qint64, QDateTime>;
    enum class Sharpening { Off, Mild, Strong };

    static void adjustGamma(QImage &image, int gammaTenths)
    {
        // Apply output = input^(1/gamma) to straight color channels, leaving
        // alpha unchanged. A lookup table avoids a power operation per channel.
        image = image.convertToFormat(QImage::Format_ARGB32);
        std::array<int, 256> levels;
        for (int value = 0; value < 256; ++value)
            levels[value] = qRound(255.0 * std::pow(value / 255.0, 10.0 / gammaTenths));
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = row[x];
                row[x] = qRgba(levels[qRed(pixel)], levels[qGreen(pixel)],
                               levels[qBlue(pixel)], qAlpha(pixel));
            }
        }
    }

    static QImage sharpen(const QImage &image, Sharpening sharpening)
    {
        const QImage source = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage result = source.copy();
        if (source.isNull() || result.isNull())
            return {};
        // Four-neighbor unsharp mask, with replicated image edges. Strong mode
        // doubles the detail boost, always using the original display image.
        // Preserve alpha and keep premultiplied color channels within its range.
        const int strength = sharpening == Sharpening::Strong ? 2 : 1;
        for (int y = 0; y < source.height(); ++y) {
            const auto *above = reinterpret_cast<const QRgb *>(source.constScanLine(std::max(0, y - 1)));
            const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            const auto *below = reinterpret_cast<const QRgb *>(source.constScanLine(std::min(source.height() - 1, y + 1)));
            auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                const QRgb center = row[x];
                const QRgb left = row[std::max(0, x - 1)];
                const QRgb right = row[std::min(source.width() - 1, x + 1)];
                const int alpha = qAlpha(center);
                const auto channel = [&](int shift) {
                    const auto value = [shift](QRgb pixel) { return int((pixel >> shift) & 255); };
                    const int detail = 4 * value(center) - value(left) - value(right)
                        - value(above[x]) - value(below[x]);
                    return std::clamp((2 * value(center) + strength * detail + 1) / 2,
                                      0, alpha);
                };
                output[x] = qRgba(channel(16), channel(8), channel(0), alpha);
            }
        }
        return result;
    }

    void navigate(qsizetype offset)
    {
        if (index_ < 0 || files_.isEmpty())
            return;
        const qsizetype next = std::clamp(index_ + offset, qsizetype(0), files_.size() - 1);
        if (next != index_) {
            index_ = next;
            loadImage();
        }
    }

    void refreshDirectory(const QString &preferredPath = {})
    {
        const QString selected = !preferredPath.isEmpty() ? preferredPath
            : index_ >= 0 ? files_.at(index_) : QString();
        QStringList files;
        QMap<QString, FileStamp> stamps;
        const auto entries = QDir(directory_).entryInfoList(
            QDir::Files | QDir::Hidden, QDir::NoSort);
        for (const auto &entry : entries) {
            if (!imageFormat(entry).isEmpty()) {
                const QString path = entry.absoluteFilePath();
                files.append(path);
                stamps.insert(path, {entry.size(), entry.lastModified()});
            }
        }
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            const auto left = QFileInfo(a).fileName();
            const auto right = QFileInfo(b).fileName();
            const int order = QString::compare(left, right, Qt::CaseInsensitive);
            return order == 0 ? left < right : order < 0;
        });
        if (!watcher_.directories().contains(directory_) && QDir(directory_).exists())
            watcher_.addPath(directory_);
        // File watches detect ongoing writes; directory watches detect new files
        // and let us reattach watches after atomic file replacement.
        const QStringList watchedFiles = watcher_.files();
        const QSet<QString> watched(watchedFiles.begin(), watchedFiles.end());
        QStringList added, removed;
        for (const QString &path : files) {
            if (!watched.contains(path))
                added.append(path);
        }
        for (const QString &path : watchedFiles) {
            if (!stamps.contains(path))
                removed.append(path);
        }
        if (!removed.isEmpty())
            watcher_.removePaths(removed);
        if (!added.isEmpty())
            watcher_.addPaths(added);
        if (files == files_ && stamps == stamps_ && preferredPath.isEmpty())
            return;
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (!stamps.contains(it.key()) || stamps.value(it.key()) != stamps_.value(it.key()))
                it = cache_.erase(it);
            else
                ++it;
        }
        const qsizetype previousIndex = index_;
        files_ = std::move(files);
        stamps_ = std::move(stamps);
        index_ = files_.indexOf(selected);
#ifdef Q_OS_WIN
        if (index_ < 0) {
            for (qsizetype i = 0; i < files_.size(); ++i) {
                if (files_.at(i).compare(selected, Qt::CaseInsensitive) == 0) {
                    index_ = i;
                    break;
                }
            }
        }
#endif
        if (files_.isEmpty()) {
            index_ = -1;
            image_ = QImage();
            message_ = tr("No PNG, JPEG, JP2, WebP, HEIC/HEIF or AVIF files in this folder. Waiting for images...");
            setWindowTitle(tr("Image Viewer"));
            update();
            return;
        }
        if (index_ < 0)
            index_ = std::clamp(previousIndex, qsizetype(0), files_.size() - 1);
        loadImage();
    }

    void restoreWindowedGeometry()
    {
        // Restore client geometry after Windows has recalculated the frame.
        // Restoring before that subtracts the decoration size on every cycle.
        setGeometry(windowedGeometry_);
#ifdef Q_OS_WIN
        const HWND handle = reinterpret_cast<HWND>(winId());
        RECT client{}, outer{};
        GetClientRect(handle, &client);
        GetWindowRect(handle, &outer);
        const qreal scale = devicePixelRatioF();
        SetWindowPos(handle, nullptr, 0, 0,
                     outer.right - outer.left + qRound(windowedGeometry_.width() * scale)
                         - (client.right - client.left),
                     outer.bottom - outer.top + qRound(windowedGeometry_.height() * scale)
                         - (client.bottom - client.top),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
    }

    void refreshNativeFrame()
    {
#ifdef Q_OS_WIN
        // Keep the native window alive so mouse activation and click tracking
        // survive mode changes. Explicitly recalculate its non-client frame.
        const HWND handle = reinterpret_cast<HWND>(winId());
        LONG_PTR style = GetWindowLongPtr(handle, GWL_STYLE);
        if (isFullScreen())
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME);
        else
            style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
        SetWindowLongPtr(handle, GWL_STYLE, style);
        // DWM can draw a separate thin border even without WS_BORDER.
        const DWMNCRENDERINGPOLICY rendering = isFullScreen()
            ? DWMNCRP_DISABLED : DWMNCRP_USEWINDOWSTYLE;
        DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY,
                             &rendering, sizeof(rendering));
        // Windows 11 border-color attribute; older Windows ignores it.
        // Numeric constants also support the older local MinGW headers.
        constexpr DWORD borderColorAttribute = 34; // DWMWA_BORDER_COLOR
        const COLORREF borderColor = isFullScreen() ? 0xfffffffe : 0xffffffff;
        DwmSetWindowAttribute(handle, borderColorAttribute,
                             &borderColor, sizeof(borderColor));
        constexpr DWORD cornerPreferenceAttribute = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
        const DWORD cornerPreference = isFullScreen() ? 1 : 0; // DONOTROUND / DEFAULT
        DwmSetWindowAttribute(handle, cornerPreferenceAttribute,
                             &cornerPreference, sizeof(cornerPreference));
        SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                         | SWP_FRAMECHANGED);
        if (!isFullScreen()) {
            // Re-enabling DWM decorations does not refresh their active state.
            // Redraw the caption using actual focus without activating the app.
            SendMessage(handle, WM_NCACTIVATE, GetForegroundWindow() == handle, 0);
        }
#endif
    }

    void fitWindowToImage()
    {
        if (!fitPending_ || isFullScreen() || image_.isNull())
            return;
        const QRect area = screen()->availableGeometry();
        const QSize decoration = frameGeometry().size() - size();
        // Leave desktop space around the initial image-sized window.
        const QSize initialBounds(qRound(area.width() * 0.85),
                                  qRound(area.height() * 0.85));
        const QSize available = (initialBounds - decoration).expandedTo(QSize(1, 1));
        QSize desired = image_.size();
        if (desired.width() > available.width() || desired.height() > available.height())
            desired.scale(available, Qt::KeepAspectRatio);
        resize(desired.expandedTo(QSize(1, 1)));
        QRect frame = frameGeometry();
        frame.moveCenter(area.center());
        frame.moveLeft(std::clamp(frame.left(), area.left(),
                                 std::max(area.left(), area.right() - frame.width() + 1)));
        frame.moveTop(std::clamp(frame.top(), area.top(),
                                std::max(area.top(), area.bottom() - frame.height() + 1)));
        move(pos() + frame.topLeft() - frameGeometry().topLeft());
        fitPending_ = false;
    }

    static QByteArray imageFormat(const QFileInfo &file)
    {
        const QByteArray suffix = file.suffix().toLower().toLatin1();
        if (suffix == "jpg")
            return "jpeg";
        if (suffix == "png" || suffix == "jpeg" || suffix == "jp2" || suffix == "webp"
            || suffix == "heic" || suffix == "heif" || suffix == "avif")
            return suffix;
        return {};
    }

    void loadImage()
    {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (!inCacheRange(files_.indexOf(it.key())))
                it = cache_.erase(it);
            else
                ++it;
        }
        showCachedImage();
        preload();
    }

    struct CachedImage {
        QImage image;
        QString error;
    };

    bool inCacheRange(qsizetype index) const
    {
        return index >= 0 && index < files_.size()
            && index >= index_ - preloadRadius_ && index <= index_ + preloadRadius_;
    }

    void showCachedImage()
    {
        const QString &path = files_.at(index_);
        const QFileInfo file(path);
        const auto it = cache_.constFind(path);
        const QImage nextImage = it == cache_.cend() ? QImage() : it->image;
        if (image_.cacheKey() != nextImage.cacheKey()) {
            sharpening_ = Sharpening::Off;
            gammaTenths_ = 10;
            adjustedImage_ = QImage();
        }
        image_ = nextImage;
        if (it == cache_.cend())
            message_ = tr("Loading %1...").arg(file.fileName());
        else if (image_.isNull())
            message_ = tr("Could not open %1\n%2\n\nUse Left / Right to continue.")
                           .arg(file.fileName(), it->error);
        else
            message_.clear();
        const QString resolution = image_.isNull() ? QString()
            : tr(" - %1 \u00d7 %2").arg(image_.width()).arg(image_.height());
        setWindowTitle(tr("%1 (%2/%3)%4 - Image Viewer - Left / Right to navigate")
                           .arg(file.absoluteDir().dirName() + QLatin1Char('/') + file.fileName())
                           .arg(index_ + 1).arg(files_.size()).arg(resolution));
        fitWindowToImage();
        update();
    }

    void preload()
    {
        // Bound concurrent decodes. Recompute priorities after each result
        // so rapid navigation never leaves a long queue of obsolete work.
        auto schedule = [this](qsizetype candidateIndex) {
            if (pending_.size() >= maxPendingDecodes_ || !inCacheRange(candidateIndex))
                return;
            const QString path = files_.at(candidateIndex);
            if (cache_.contains(path) || pending_.contains(path))
                return;
            pending_.insert(path);
            const FileStamp stamp = stamps_.value(path);
            workers_.start([this, path, stamp] {
                CachedImage result;
                const QByteArray format = imageFormat(QFileInfo(path));
                if (format == "jpeg") {
                    result.image = loadJpeg(path, result.error);
                } else {
                    QImageReader reader(path, format);
                    reader.setAutoTransform(true);
                    result = {reader.read(), reader.errorString()};
                }
                QMetaObject::invokeMethod(this, [this, path, stamp, result = std::move(result)] {
                    pending_.remove(path);
                    const qsizetype index = files_.indexOf(path);
                    if (inCacheRange(index) && stamps_.value(path) == stamp) {
                        cache_.insert(path, result);
                        if (index == index_)
                            showCachedImage();
                    }
                    preload();
                }, Qt::QueuedConnection);
            });
        };
        schedule(index_);
        for (qsizetype distance = 1;
             distance <= preloadRadius_ && pending_.size() < maxPendingDecodes_; ++distance) {
            schedule(index_ + distance);
            schedule(index_ - distance);
        }
    }

    static constexpr qsizetype preloadRadius_ = 5;
    static constexpr qsizetype maxPendingDecodes_ = 7;

    FullscreenCloseButton closeButton_{this};
    bool dragPending_ = false;
    qint64 wheelRemainder_ = 0;
    QPoint dragStart_;
    Qt::WindowStates windowedState_ = Qt::WindowNoState;
    QRect windowedGeometry_;
    bool firstWindowedSwitch_ = true;
    bool fitPending_ = false;

    QString directory_;
    QStringList files_;
    qsizetype index_ = -1;
    QImage image_;
    QImage adjustedImage_;
    Sharpening sharpening_ = Sharpening::Off;
    int gammaTenths_ = 10;
    QString message_;
    QFileSystemWatcher watcher_;
    QTimer refreshTimer_;
    QMap<QString, FileStamp> stamps_;
    QMap<QString, CachedImage> cache_;
    QSet<QString> pending_;
    QThreadPool workers_;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/iv/image-viewer.png")));
    app.setDesktopFileName(QStringLiteral("image-viewer"));
    // JPEG 2000 needs substantial temporary memory beyond the decoded pixels.
    // Keep a bounded budget; QT_IMAGEIO_MAXALLOC can override it at runtime.
    QImageReader::setAllocationLimit(1024);
    ImageViewer viewer(app.arguments());
    viewer.enterFullScreen();
    return app.exec();
}
