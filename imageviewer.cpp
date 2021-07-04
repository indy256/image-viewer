#include "imageviewer.h"

#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QColorSpace>
#include <QDebug>
#include <QDir>
#include <QWheelEvent>
#include <QFileDialog>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsView>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QTransform>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStatusBar>

#if defined(QT_PRINTSUPPORT_LIB)
#  include <QtPrintSupport/qtprintsupportglobal.h>

#  if QT_CONFIG(printdialog)
#    include <QPrintDialog>
#  endif
#endif

ImageViewer::ImageViewer(QWidget *parent) : QMainWindow(parent)
{
    createActions();

    scene = new QGraphicsScene(this);
    view = new QGraphicsView(this);
    view->setScene(scene);
    view->setBackgroundBrush(QBrush(Qt::gray, Qt::SolidPattern));

    setCentralWidget(view);

    resize(QGuiApplication::primaryScreen()->availableSize() * 8 / 10);
}

bool ImageViewer::loadFile(const QString &path)
{
    QFileInfo info(path);
    dir = info.dir();
    filename = info.fileName();

    setImage(QImage(path));
    return true;
}

void ImageViewer::setImage(const QImage &newImage)
{
    image = newImage;
    scene->clear();
    scene->addPixmap(QPixmap::fromImage(image));
    scene->setSceneRect(0, 0, image.width(), image.height());

    zoomToFit();
    updateActions();
}

void ImageViewer::zoomToFit()
{
    view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

void ImageViewer::resizeEvent(QResizeEvent*)
{
    zoomToFit();
}

void ImageViewer::showEvent(QShowEvent*)
{
    zoomToFit();
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    QPoint numDegrees = event->angleDelta();
    if (!numDegrees.isNull()) {
        if (numDegrees.y() > 0) {
            prevFile();
        }
        if (numDegrees.y() < 0) {
            nextFile();
        }
    }
    event->accept();
}

bool ImageViewer::saveFile(const QString &fileName)
{
    QImageWriter writer(fileName);

    if (!writer.write(image)) {
        QMessageBox::information(this, QGuiApplication::applicationDisplayName(),
                                 tr("Cannot write %1: %2")
                                 .arg(QDir::toNativeSeparators(fileName)), writer.errorString());
        return false;
    }
    const QString message = tr("Wrote \"%1\"").arg(QDir::toNativeSeparators(fileName));
    statusBar()->showMessage(message);
    return true;
}

static void initializeImageFileDialog(QFileDialog &dialog, QFileDialog::AcceptMode acceptMode)
{
    static bool firstDialog = true;

    if (firstDialog) {
        firstDialog = false;
        const QStringList picturesLocations = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
        dialog.setDirectory(picturesLocations.isEmpty() ? QDir::currentPath() : picturesLocations.last());
    }

    QStringList mimeTypeFilters;
    const QByteArrayList supportedMimeTypes = acceptMode == QFileDialog::AcceptOpen
        ? QImageReader::supportedMimeTypes() : QImageWriter::supportedMimeTypes();
    for (const QByteArray &mimeTypeName : supportedMimeTypes)
        mimeTypeFilters.append(mimeTypeName);
    mimeTypeFilters.sort();
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter("image/jpeg");
    dialog.setAcceptMode(acceptMode);
    if (acceptMode == QFileDialog::AcceptSave)
        dialog.setDefaultSuffix("jpg");
}

void ImageViewer::open()
{
    QFileDialog dialog(this, tr("Open File"));
    initializeImageFileDialog(dialog, QFileDialog::AcceptOpen);

    while (dialog.exec() == QDialog::Accepted && !loadFile(dialog.selectedFiles().constFirst())) {}
}

void ImageViewer::saveAs()
{
    QFileDialog dialog(this, tr("Save File As"));
    initializeImageFileDialog(dialog, QFileDialog::AcceptSave);

    while (dialog.exec() == QDialog::Accepted && !saveFile(dialog.selectedFiles().constFirst())) {}
}

void ImageViewer::print()
{
    Q_ASSERT(!image.isNull());
#if defined(QT_PRINTSUPPORT_LIB) && QT_CONFIG(printdialog)
    QPrintDialog dialog(&printer, this);
    if (dialog.exec()) {
        QPainter painter(&printer);
        QPixmap pixmap = QPixmap::fromImage(image);
        QRect rect = painter.viewport();
        QSize size = pixmap.size();
        size.scale(rect.size(), Qt::KeepAspectRatio);
        painter.setViewport(rect.x(), rect.y(), size.width(), size.height());
        painter.setWindow(pixmap.rect());
        painter.drawPixmap(0, 0, pixmap);
    }
#endif
}

void ImageViewer::copy()
{
#ifndef QT_NO_CLIPBOARD
    QGuiApplication::clipboard()->setImage(image);
#endif // !QT_NO_CLIPBOARD
}

#ifndef QT_NO_CLIPBOARD
static QImage clipboardImage()
{
    if (const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData()) {
        if (mimeData->hasImage()) {
            const QImage image = qvariant_cast<QImage>(mimeData->imageData());
            if (!image.isNull())
                return image;
        }
    }
    return QImage();
}
#endif // !QT_NO_CLIPBOARD

void ImageViewer::paste()
{
#ifndef QT_NO_CLIPBOARD
    const QImage newImage = clipboardImage();
    if (newImage.isNull()) {
        statusBar()->showMessage(tr("No image in clipboard"));
    } else {
        setImage(newImage);
        setWindowFilePath(QString());
        const QString message = tr("Obtained image from clipboard, %1x%2, Depth: %3")
            .arg(newImage.width()).arg(newImage.height()).arg(newImage.depth());
        statusBar()->showMessage(message);
    }
#endif // !QT_NO_CLIPBOARD
}

void ImageViewer::advanceFile(int delta) {
    QStringList images = dir.entryList(QStringList() << "*.jpg" << "*.JPG" << "*.png", QDir::Files);
    images.sort();

    QStringList::const_iterator newFilename;
    if (delta == 1) {
        newFilename = std::upper_bound(images.cbegin(), images.cend(), filename);
        if (newFilename == images.cend()) {
            return;
        }
    } else {
        newFilename = std::lower_bound(images.cbegin(), images.cend(), filename);
        if (newFilename == images.cbegin()) {
            return;
        }
        --newFilename;
    }

    setWindowFilePath(*newFilename);
    loadFile(dir.filePath(*newFilename));
}

void ImageViewer::nextFile()
{
    advanceFile(1);
}

void ImageViewer::prevFile()
{
    advanceFile(-1);
}

void ImageViewer::about()
{
    QMessageBox::about(this, tr("About Image Viewer"),
            tr("<p><p>The <b>Image Viewer</b></p>"
               "<p><a href='https://github.com/indy256/image-viewer'>github.com/indy256/image-viewer</a></p>"));
}

void ImageViewer::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *openAct = fileMenu->addAction(tr("&Open..."), this, &ImageViewer::open);
    openAct->setShortcut(QKeySequence::Open);

    saveAsAct = fileMenu->addAction(tr("&Save As..."), this, &ImageViewer::saveAs);
    saveAsAct->setEnabled(false);

    printAct = fileMenu->addAction(tr("&Print..."), this, &ImageViewer::print);
    printAct->setShortcut(QKeySequence::Print);
    printAct->setEnabled(false);

    fileMenu->addSeparator();

    QAction *exitAct = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAct->setShortcut(tr("Esc"));

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));

    copyAct = editMenu->addAction(tr("&Copy"), this, &ImageViewer::copy);
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setEnabled(false);

    QAction *pasteAct = editMenu->addAction(tr("&Paste"), this, &ImageViewer::paste);
    pasteAct->setShortcut(QKeySequence::Paste);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

    nextFileAct = viewMenu->addAction(tr("&Next file"), this, &ImageViewer::nextFile);
    nextFileAct->setEnabled(true);
    nextFileAct->setShortcut(QKeySequence::MoveToNextChar);

    prevFileAct = viewMenu->addAction(tr("&Previous file"), this, &ImageViewer::prevFile);
    prevFileAct->setEnabled(true);
    prevFileAct->setShortcut(QKeySequence::MoveToPreviousChar);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    helpMenu->addAction(tr("&About"), this, &ImageViewer::about);
    helpMenu->addAction(tr("About &Qt"), this, &QApplication::aboutQt);
}

void ImageViewer::updateActions()
{
    saveAsAct->setEnabled(!image.isNull());
    copyAct->setEnabled(!image.isNull());
}
