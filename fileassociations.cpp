// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only

#include "fileassociations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <QSettings>
#include <qt_windows.h>
#include <shlobj.h>
#elif defined(Q_OS_MACOS)
#include <CoreServices/CoreServices.h>
#elif defined(Q_OS_LINUX)
#include <QImage>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#endif

QString registerImageFileTypes()
{
#ifdef Q_OS_WIN
    const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString command = QStringLiteral("\"%1\" \"%2\"").arg(executable, QStringLiteral("%1"));
    const QString icon = QStringLiteral("\"%1\",0").arg(executable);
    const QString progId = QStringLiteral("ImageViewer.Image");
    const QString capabilities = QStringLiteral("ImageViewer/Capabilities");
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software"), QSettings::NativeFormat);
    const QString type = QStringLiteral("Classes/") + progId;
    registry.setValue(type + QStringLiteral("/Default"), QStringLiteral("Image Viewer image"));
    registry.setValue(type + QStringLiteral("/DefaultIcon/Default"), icon);
    registry.setValue(type + QStringLiteral("/shell/open/command/Default"), command);
    registry.setValue(capabilities + QStringLiteral("/ApplicationName"), QStringLiteral("Image Viewer"));
    registry.setValue(capabilities + QStringLiteral("/ApplicationDescription"),
                      QStringLiteral("View PNG, JPEG, JPEG 2000, WebP, HEIC, HEIF and AVIF images."));
    registry.setValue(capabilities + QStringLiteral("/ApplicationIcon"), icon);
    for (const QString &extension : {QStringLiteral(".png"), QStringLiteral(".jpg"),
             QStringLiteral(".jpeg"), QStringLiteral(".jp2"), QStringLiteral(".webp"),
             QStringLiteral(".heic"), QStringLiteral(".heif"), QStringLiteral(".avif")}) {
        registry.setValue(QStringLiteral("Classes/") + extension
                              + QStringLiteral("/OpenWithProgids/") + progId, QString());
        registry.setValue(capabilities + QStringLiteral("/FileAssociations/") + extension, progId);
    }
    registry.setValue(QStringLiteral("RegisteredApplications/Image Viewer"),
                      QStringLiteral("Software\\ImageViewer\\Capabilities"));
    registry.sync();
    if (registry.status() != QSettings::NoError)
        return QCoreApplication::translate("FileAssociations", "Could not write file registration to your user registry.");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return {};
#elif defined(Q_OS_MACOS)
    CFBundleRef bundle = CFBundleGetMainBundle();
    CFURLRef url = bundle ? CFBundleCopyBundleURL(bundle) : nullptr;
    if (!url)
        return QCoreApplication::translate("FileAssociations", "Run Image Viewer from its installed iv.app bundle.");
    const bool isApp = QCoreApplication::applicationDirPath().endsWith(QStringLiteral(".app/Contents/MacOS"));
    const OSStatus status = isApp ? LSRegisterURL(url, true) : paramErr;
    CFRelease(url);
    if (status != noErr)
        return QCoreApplication::translate("FileAssociations", "Could not register iv.app (error %1). Move it to Applications and try again.").arg(status);
    return {};
#elif defined(Q_OS_LINUX)
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString applications = data + QStringLiteral("/applications");
    const QString icons = data + QStringLiteral("/icons/hicolor/512x512/apps");
    if (data.isEmpty() || !QDir().mkpath(applications) || !QDir().mkpath(icons))
        return QCoreApplication::translate("FileAssociations", "Could not create your application registration directories.");
    QString executable = qEnvironmentVariable("APPIMAGE");
    if (executable.isEmpty())
        executable = QCoreApplication::applicationFilePath();
    executable = QFileInfo(executable).absoluteFilePath();
    if (!QFileInfo(executable).isExecutable())
        return QCoreApplication::translate("FileAssociations", "The image viewer executable is unavailable.");
    // Exec arguments have shell-like quoting followed by desktop-entry string escaping.
    executable.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    executable.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    executable.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    executable.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    executable.replace(QLatin1Char('%'), QStringLiteral("%%"));
    executable.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    executable.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    executable.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    executable.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    if (!QImage(QStringLiteral(":/iv/image-viewer.png")).save(icons + QStringLiteral("/image-viewer.png")))
        return QCoreApplication::translate("FileAssociations", "Could not install the image viewer icon.");
    const QByteArray entry = QStringLiteral(
        "[Desktop Entry]\nType=Application\nName=Image Viewer\nExec=\"%1\" %f\n"
        "Icon=image-viewer\nCategories=Graphics;Viewer;\n"
        "MimeType=image/png;image/jpeg;image/jp2;image/webp;image/heic;image/heif;image/avif;\n"
        "Terminal=false\n").arg(executable).toUtf8();
    QSaveFile file(applications + QStringLiteral("/image-viewer.desktop"));
    if (!file.open(QIODevice::WriteOnly) || file.write(entry) != entry.size() || !file.commit())
        return QCoreApplication::translate("FileAssociations", "Could not save the desktop entry: %1").arg(file.errorString());
    const QString updater = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
    if (!updater.isEmpty()) {
        QProcess process;
        process.start(updater, {applications});
        if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            process.kill();
            process.waitForFinished(1000);
            return QCoreApplication::translate("FileAssociations", "The desktop entry was saved, but refreshing the application database failed. Sign out and back in to refresh Open With.");
        }
    }
    return {};
#else
    return QCoreApplication::translate("FileAssociations", "File type registration is not supported on this platform.");
#endif
}

