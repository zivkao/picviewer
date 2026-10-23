#include "applog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QLoggingCategory>
#include <QMutex>
#include <QScreen>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>

#include <libheif/heif.h>
#include <libraw/libraw.h>

#include <windows.h>

#include <cstdio>

Q_LOGGING_CATEGORY(logEnv, "picviewer.env")

namespace applog {
namespace {

constexpr qint64 MaxBytes = 1024 * 1024;    // rotate at 1 MiB
constexpr int BackupCount = 3;

QFile g_file;
QTextStream g_stream;
QMutex g_mutex;                              // handler is called from worker threads
bool g_verbose = false;
QtMessageHandler g_previous = nullptr;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?????";
}

void rotateIfNeeded()
{
    if (!g_file.isOpen() || g_file.size() < MaxBytes)
        return;

    g_stream.flush();
    g_file.close();

    const QString base = filePath();
    QFile::remove(base + QStringLiteral(".%1").arg(BackupCount));
    for (int i = BackupCount - 1; i >= 1; --i) {
        const QString from = base + QStringLiteral(".%1").arg(i);
        if (QFile::exists(from))
            QFile::rename(from, base + QStringLiteral(".%1").arg(i + 1));
    }
    QFile::rename(base, base + QStringLiteral(".1"));

    g_file.setFileName(base);
    if (g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        g_stream.setDevice(&g_file);
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtDebugMsg && !g_verbose)
        return;

    const QMutexLocker locker(&g_mutex);
    rotateIfNeeded();
    if (!g_file.isOpen())
        return;

    g_stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             << ' ' << levelName(type) << "  "
             << QString::fromLatin1(context.category ? context.category : "default").leftJustified(24)
             << message << '\n';
    g_stream.flush();

    if (type == QtFatalMsg && g_previous)
        g_previous(type, context, message);
}

} // namespace

QString directory()
{
    // AppLocalDataLocation is %LOCALAPPDATA%\<app>; the Python port wrote to
    // the same place, so both versions share one log folder.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(QStringLiteral("PicViewer/logs"));
}

QString filePath()
{
    return QDir(directory()).filePath(QStringLiteral("picviewer-cpp.log"));
}

void install(bool verbose)
{
    g_verbose = verbose;
    QDir().mkpath(directory());

    g_file.setFileName(filePath());
    if (g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        g_stream.setDevice(&g_file);
        g_stream.setEncoding(QStringConverter::Utf8);
    }

    // Debug messages are dropped unless asked for; everything else is kept.
    // Only this application's own categories follow -v down to debug. Qt's
    // internal categories at debug level bury everything else, the way
    // Pillow's plugin-import chatter did on the Python side.
    QLoggingCategory::setFilterRules(
        verbose ? QStringLiteral("*.debug=false\npicviewer.*.debug=true\n"
                                 "picviewer.debug=true")
                : QStringLiteral("*.debug=false"));
    g_previous = qInstallMessageHandler(handler);
}

void logEnvironment()
{
    qCInfo(logEnv).noquote() << "os      :" << QSysInfo::prettyProductName()
                             << QSysInfo::currentCpuArchitecture();
    qCInfo(logEnv).noquote() << "qt      :" << qVersion()
                             << "| build" << QT_VERSION_STR
                             << "| platform" << QGuiApplication::platformName();
    qCInfo(logEnv).noquote() << "libraw  :" << LibRaw::version();
    qCInfo(logEnv).noquote() << "libheif :" << heif_get_version();
    qCInfo(logEnv).noquote() << "cores   :" << QThread::idealThreadCount();

    // Nothing in the viewer accounts for devicePixelRatio, so a scaled display
    // is the first thing to suspect when "100%" does not look like 1:1.
    for (const QScreen *screen : QGuiApplication::screens()) {
        qCInfo(logEnv).noquote().nospace()
            << "screen  : " << screen->name() << " "
            << screen->geometry().width() << "x" << screen->geometry().height()
            << " @ " << screen->logicalDotsPerInch() << " dpi, ratio "
            << screen->devicePixelRatio();
    }

    QStringList formats;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        formats << QString::fromLatin1(format);
    qCInfo(logEnv).noquote() << "qt reads:" << formats.join(QLatin1Char(' '));
}

namespace {

LONG WINAPI crashHandler(EXCEPTION_POINTERS *info)
{
    const auto *record = info && info->ExceptionRecord ? info->ExceptionRecord : nullptr;
    const unsigned long code = record ? record->ExceptionCode : 0;
    const void *address = record ? record->ExceptionAddress : nullptr;

    // Write directly rather than through the Qt handler: by this point the
    // process is unwinding and the less machinery involved, the better the
    // odds the line actually reaches the disk.
    const QMutexLocker locker(&g_mutex);
    if (g_file.isOpen()) {
        g_stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                 << " FATAL  picviewer.crash         "
                 << "unhandled exception 0x" << QString::number(code, 16)
                 << " at " << QString::number(reinterpret_cast<quintptr>(address), 16)
                 << " -- see the decode line above for the file being read\n";
        g_stream.flush();
        g_file.flush();
    }
    return EXCEPTION_CONTINUE_SEARCH;   // let Windows report it as it would have
}

} // namespace

void installCrashHandler()
{
    SetUnhandledExceptionFilter(crashHandler);
}

void shutdown()
{
    const QMutexLocker locker(&g_mutex);
    if (g_file.isOpen()) {
        g_stream.flush();
        g_file.close();
    }
}

} // namespace applog
