// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImageReader>
#include <QKeyEvent>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

class ImageViewer final : public QWidget
{
public:
    explicit ImageViewer(const QStringList &arguments)
    {
        resize(1000, 700);
        setMinimumSize(1, 1);
        setWindowTitle(tr("Image Viewer"));
        if (arguments.size() != 2) {
            message_ = tr("Usage: iv <file.jpg|file.jp2|file.webp|file.heic|file.heif|file.avif>\n\nLeft / Right: previous / next image\nF: toggle full screen\nEsc: exit");
            return;
        }

        const QFileInfo initial(arguments.at(1));
        if (!initial.isFile() || imageFormat(initial).isEmpty()) {
            message_ = tr("Not a supported image file (JPEG, JP2, WebP, HEIC/HEIF or AVIF): %1").arg(arguments.at(1));
            return;
        }

        directory_ = initial.absolutePath();
        refreshTimer_.setSingleShot(true);
        refreshTimer_.setInterval(150);
        connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
                [this] { refreshTimer_.start(); });
        connect(&watcher_, &QFileSystemWatcher::fileChanged, this,
                [this] { refreshTimer_.start(); });
        connect(&refreshTimer_, &QTimer::timeout, this, [this] { refreshDirectory(); });
        refreshDirectory(initial.absoluteFilePath());
    }

    void enterFullScreen()
    {
        if (isFullScreen())
            return;
        windowedState_ = windowState();
        windowedGeometry_ = isMaximized() ? normalGeometry() : geometry();
        fitPending_ = false;
#ifndef Q_OS_MACOS
        // macOS manages its own frame during the native fullscreen transition.
        // Set the platform flag before Qt calculates fullscreen geometry elsewhere.
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
#ifndef Q_OS_MACOS
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
#ifdef Q_OS_MACOS
            // Restore Cocoa's key window and first responder after Qt processes
            // the windowed-mode changes, without stealing focus from another app.
            QTimer::singleShot(0, this, [this] {
                if (isVisible() && !isFullScreen()
                    && QGuiApplication::applicationState() == Qt::ApplicationActive) {
                    activateWindow();
                    setFocus(Qt::OtherFocusReason);
                }
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
            painter.drawImage(target, image_);
        } else {
            painter.setPen(Qt::white);
            painter.drawText(rect().adjusted(24, 24, -24, -24),
                             Qt::AlignCenter | Qt::TextWordWrap, message_);
        }
    }

private:
    using FileStamp = QPair<qint64, QDateTime>;

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
        const QString selected = index_ >= 0 ? files_.at(index_) : preferredPath;
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
        if (files == files_ && stamps == stamps_)
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
            message_ = tr("No JPEG, JP2, WebP, HEIC/HEIF or AVIF files in this folder. Waiting for images...");
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
        if (suffix == "jpeg" || suffix == "jp2" || suffix == "webp"
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
        image_ = it == cache_.cend() ? QImage() : it->image;
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
        auto schedule = [this](qsizetype index) {
            if (pending_.size() >= maxPendingDecodes_ || !inCacheRange(index))
                return;
            const QString path = files_.at(index);
            if (cache_.contains(path) || pending_.contains(path))
                return;
            pending_.insert(path);
            const FileStamp stamp = stamps_.value(path);
            workers_.start([this, path, stamp] {
                QImageReader reader(path, imageFormat(QFileInfo(path)));
                reader.setAutoTransform(true);
                CachedImage result{reader.read(), reader.errorString()};
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
    // JPEG 2000 needs substantial temporary memory beyond the decoded pixels.
    // Keep a bounded budget; QT_IMAGEIO_MAXALLOC can override it at runtime.
    QImageReader::setAllocationLimit(1024);
    ImageViewer viewer(app.arguments());
    viewer.enterFullScreen();
    return app.exec();
}
