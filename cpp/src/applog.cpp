#include "applog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

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
    QLoggingCategory::setFilterRules(verbose ? QStringLiteral("*.debug=true")
                                             : QStringLiteral("*.debug=false"));
    g_previous = qInstallMessageHandler(handler);
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
