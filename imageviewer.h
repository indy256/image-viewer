#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QDir>
#include <QMainWindow>
#include <QImage>
#include <QFrame>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>

#if defined(QT_PRINTSUPPORT_LIB)
#  include <QtPrintSupport/qtprintsupportglobal.h>

#  if QT_CONFIG(printer)
#    include <QPrinter>
#  endif
#endif

QT_BEGIN_NAMESPACE
class QAction;
class QLabel;
class QMenu;
class QScrollArea;
class QScrollBar;
QT_END_NAMESPACE

class ImageViewer : public QMainWindow
{
    Q_OBJECT

public:
    ImageViewer(QWidget *parent = nullptr);
    bool loadFile(const QString &);

protected:
    virtual void resizeEvent(QResizeEvent *event) override;
    virtual void showEvent(QShowEvent *event) override;
    virtual void wheelEvent(QWheelEvent *event) override;

private slots:
    void open();
    void saveAs();
    void print();
    void copy();
    void paste();
    void nextFile();
    void prevFile();
    void about();

private:
    void createActions();
    void createMenus();
    void updateActions();
    bool saveFile(const QString &fileName);
    void setImage(const QImage &newImage);
    void zoomToFit();
    void advanceFile(int delta);

    QGraphicsView *view;
    QGraphicsScene *scene;
    QImage image;
    QDir dir;
    QString filename;

#if defined(QT_PRINTSUPPORT_LIB) && QT_CONFIG(printer)
    QPrinter printer;
#endif

    QAction *saveAsAct;
    QAction *printAct;
    QAction *copyAct;
    QAction *nextFileAct;
    QAction *prevFileAct;
};

#endif
