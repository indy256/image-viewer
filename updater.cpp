// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only

#include "updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Updater", text); }

QProcessEnvironment helperEnvironment()
{
    auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_LINUX
    // The AppImage mount goes away when the old viewer exits.
    for (const auto *key : {"APPIMAGE", "APPDIR", "OWD", "LD_LIBRARY_PATH",
                           "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"})
        environment.remove(QString::fromLatin1(key));
#endif
    return environment;
}

bool run(QProgressDialog &progress, const QString &program, const QStringList &arguments,
         QString &error, int timeout = 330000)
{
    QProcess process;
    process.setProcessEnvironment(helperEnvironment());
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, &loop, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    process.start(program, arguments);
    deadline.start(timeout);
    loop.exec();
    const bool timedOut = !deadline.isActive();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
    if (progress.wasCanceled())
        return false;
    if (timedOut || process.error() == QProcess::FailedToStart
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = timedOut ? tr("The operation timed out.")
            : QString::fromUtf8(process.readAllStandardError()).trimmed().left(2000);
        if (error.isEmpty())
            error = process.errorString();
        return false;
    }
    return true;
}

QByteArray digest(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return hash.result().toHex();
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size()
        && file.flush();
}
}

bool updateToLatestVersion(QWidget *parent, const QString &imagePath)
{
    const auto fail = [parent](const QString &error) {
        QMessageBox::warning(parent, tr("Update to latest version"), error);
        return false;
    };
    QString assetName;
    QString target = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    assetName = QStringLiteral("iv-windows-x64.exe");
#elif defined(Q_OS_WIN) && defined(Q_PROCESSOR_ARM_64)
    assetName = QStringLiteral("iv-windows-arm64.exe");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    assetName = QStringLiteral("iv-macos-arm64.dmg");
    QDir bundle(QCoreApplication::applicationDirPath());
    if (!bundle.cdUp() || !bundle.cdUp() || !bundle.dirName().endsWith(QStringLiteral(".app")))
        return fail(tr("Run the installed iv.app to update it."));
    target = QFileInfo(bundle.absolutePath()).canonicalFilePath();
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    assetName = QStringLiteral("iv-linux-x64.AppImage");
    target = QFileInfo(qEnvironmentVariable("APPIMAGE")).canonicalFilePath();
    if (qEnvironmentVariableIsEmpty("APPIMAGE"))
        return fail(tr("Run the AppImage release to update it automatically."));
#else
    return fail(tr("No release is available for this operating system and architecture."));
#endif
    if (target.isEmpty() || !QFileInfo::exists(target))
        return fail(tr("The installed application could not be located."));
#ifdef Q_OS_WIN
    const QString system = qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/");
    const QString curl = system + QStringLiteral("curl.exe");
    const QString helper = system + QStringLiteral("WindowsPowerShell/v1.0/powershell.exe");
#else
    const QString curl = QStandardPaths::findExecutable(QStringLiteral("curl"));
    const QString helper = QStringLiteral("/bin/sh");
#endif
    if (curl.isEmpty() || !QFileInfo::exists(curl) || !QFileInfo::exists(helper))
        return fail(tr("The updater needs curl and the system scripting shell installed."));
    QTemporaryDir staging(QFileInfo(target).absolutePath() + QStringLiteral("/.iv-update-XXXXXX"));
    if (!staging.isValid())
        return fail(tr("The application folder is not writable. Move the app to a writable folder and try again."));
    const QString directory = staging.path();
    QProgressDialog progress(tr("Checking the latest release..."), tr("Cancel"), 0, 0, parent);
    progress.setWindowTitle(tr("Update to latest version"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.show();
    QString error;
    const auto download = [&](const QString &url, const QString &path, qint64 limit) {
        return run(progress, curl,
                   {QStringLiteral("--fail"), QStringLiteral("--location"),
                    QStringLiteral("--silent"), QStringLiteral("--show-error"),
                    QStringLiteral("--proto"), QStringLiteral("=https"),
                    QStringLiteral("--proto-redir"), QStringLiteral("=https"),
                    QStringLiteral("--connect-timeout"), QStringLiteral("15"),
                    QStringLiteral("--max-time"), QStringLiteral("300"),
                    QStringLiteral("--max-filesize"), QString::number(limit),
                    QStringLiteral("--user-agent"), QStringLiteral("ImageViewer-Updater"),
                    QStringLiteral("--output"), path, url}, error);
    };
    const auto failed = [&] {
        progress.hide();
        return progress.wasCanceled() ? false : fail(error);
    };
    const QString metadataPath = directory + QStringLiteral("/release.json");
    if (!download(QStringLiteral("https://api.github.com/repos/indy256/image-viewer/releases/latest"),
                  metadataPath, 2 * 1024 * 1024))
        return failed();
    QFile metadata(metadataPath);
    if (!metadata.open(QIODevice::ReadOnly))
        return fail(tr("Could not read the release information."));
    const auto release = QJsonDocument::fromJson(metadata.readAll()).object();
    metadata.close();
    QJsonObject asset;
    for (const auto &entry : release.value(QStringLiteral("assets")).toArray()) {
        const auto candidate = entry.toObject();
        if (candidate.value(QStringLiteral("name")).toString().compare(assetName, Qt::CaseInsensitive) == 0) {
            asset = candidate;
            break;
        }
    }
    const QString checksum = asset.value(QStringLiteral("digest")).toString();
    const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
    const qint64 size = asset.value(QStringLiteral("size")).toInteger();
    if (asset.isEmpty() || release.value(QStringLiteral("draft")).toBool()
        || release.value(QStringLiteral("prerelease")).toBool())
        return fail(tr("The latest release does not contain %1. Try again after its builds finish.").arg(assetName));
    if (!QRegularExpression(QStringLiteral("^sha256:[0-9a-f]{64}$")).match(checksum).hasMatch()
        || size <= 0 || size > 1024LL * 1024 * 1024 || url.scheme() != QStringLiteral("https")
        || url.host() != QStringLiteral("github.com") || !url.userInfo().isEmpty()
        || !url.path().startsWith(QStringLiteral("/indy256/image-viewer/releases/download/")))
        return fail(tr("The release asset has invalid download or checksum information."));
#ifndef Q_OS_MACOS
    if (digest(target) == checksum.mid(7).toLatin1()) {
        progress.hide();
        QMessageBox::information(parent, tr("Update to latest version"), tr("You already have the latest version."));
        return false;
    }
#endif
    progress.setLabelText(tr("Downloading %1...").arg(release.value(QStringLiteral("tag_name")).toString()));
    QString source = directory + QStringLiteral("/download");
    if (!download(url.toString(QUrl::FullyEncoded), source, size))
        return failed();
    progress.setLabelText(tr("Verifying the download..."));
    if (QFileInfo(source).size() != size || digest(source) != checksum.mid(7).toLatin1())
        return fail(tr("The download failed verification. Your application has not been changed."));
#ifdef Q_OS_MACOS
    const QString mount = directory + QStringLiteral("/mount");
    QDir().mkpath(mount);
    const bool mounted = run(progress, QStringLiteral("/usr/bin/hdiutil"),
             {QStringLiteral("attach"), QStringLiteral("-readonly"), QStringLiteral("-nobrowse"),
              QStringLiteral("-mountpoint"), mount, source}, error, 60000);
    source = directory + QStringLiteral("/new.app");
    const bool copied = mounted && run(progress, QStringLiteral("/usr/bin/ditto"),
                            {mount + QStringLiteral("/iv.app"), source}, error, 60000);
    // Also attempt detach after cancellation during mounting. Never recursively
    // remove a staging directory that may still contain a mounted disk image.
    QProcess detach;
    detach.start(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("detach"), mount});
    if (!detach.waitForFinished(15000)) { detach.kill(); detach.waitForFinished(1000); }
    if (detach.exitStatus() != QProcess::NormalExit || detach.exitCode() != 0) {
        staging.setAutoRemove(false);
        if (mounted)
            return fail(tr("Could not detach the update disk image. Your app has not been changed. Staging folder: %1").arg(directory));
    }
    if (!copied)
        return failed();
    if (!QFileInfo(source + QStringLiteral("/Contents/MacOS/iv")).isExecutable())
        return fail(tr("The release does not contain a valid iv.app."));
#else
    if (!QFile::setPermissions(source, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                     | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther))
        return fail(tr("Could not prepare the downloaded application."));
#endif
    if (progress.wasCanceled())
        return false;
    const QString ready = directory + QStringLiteral("/ready");
    const QString commit = directory + QStringLiteral("/commit");
    const QString log = directory + QStringLiteral("/update.log");
    QStringList arguments;
#ifdef Q_OS_WIN
    const QString script = directory + QStringLiteral("/update.ps1");
    const QString planPath = directory + QStringLiteral("/plan.json");
    const QJsonObject plan{{QStringLiteral("target"), target}, {QStringLiteral("source"), source},
                          {QStringLiteral("ready"), ready},
                          {QStringLiteral("commit"), commit},
                          {QStringLiteral("log"), log}, {QStringLiteral("image"), imagePath},
                          {QStringLiteral("pid"), QCoreApplication::applicationPid()}};
    if (!QFile::copy(QStringLiteral(":/iv/update-windows.ps1"), script)
        || !writeFile(planPath, QJsonDocument(plan).toJson()))
        return fail(tr("Could not prepare the update helper."));
    arguments = {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                 QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                 QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                 QStringLiteral("-File"), script, QStringLiteral("-PlanPath"), planPath};
#else
    const QString script = directory + QStringLiteral("/update.sh");
    if (!QFile::copy(QStringLiteral(":/iv/update-unix.sh"), script))
        return fail(tr("Could not prepare the update helper."));
    arguments = {script, target, source, ready, log,
                 QString::number(QCoreApplication::applicationPid()), imagePath, commit};
#endif
    QProcess installer;
    installer.setProgram(helper);
    installer.setArguments(arguments);
    installer.setWorkingDirectory(directory);
    installer.setProcessEnvironment(helperEnvironment());
    // Keep the helper's own log separate from inherited output handles on Windows.
    installer.setProcessChannelMode(QProcess::MergedChannels);
    installer.setStandardOutputFile(directory + QStringLiteral("/helper-output.log"), QIODevice::Append);
#ifdef Q_OS_WIN
    installer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif
    if (!installer.startDetached())
        return fail(tr("Could not start the update helper. Your application has not been changed."));
    // The detached helper owns this directory now, including its diagnostic logs.
    staging.setAutoRemove(false);
    progress.setCancelButton(nullptr);
    progress.setLabelText(tr("Preparing to restart..."));
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(50);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (QFileInfo::exists(ready)) loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(15000);
    loop.exec();
    progress.hide();
    if (!QFileInfo::exists(ready))
        return fail(tr("The update helper could not start. Your app is still running. Logs: %1").arg(directory));
    if (!writeFile(commit, QByteArrayLiteral("commit")))
        return fail(tr("Could not hand off the update. Your application has not been changed."));
    return true;
}
