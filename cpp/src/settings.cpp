#include "settings.h"

#include <QDir>
#include <QSettings>

#include "applog.h"

namespace settings {

namespace {

QString &overridden()
{
    static QString path;
    return path;
}

int readInt(const QSettings &store, const QString &key, int fallback, int low, int high)
{
    bool ok = false;
    const int value = store.value(key).toInt(&ok);
    return ok ? qBound(low, value, high) : fallback;
}

} // namespace

QString filePath()
{
    if (!overridden().isEmpty())
        return overridden();
    // The folder above the logs, which the Python build shares.
    return QDir(applog::directory()).absoluteFilePath(QStringLiteral("../picviewer-cpp.ini"));
}

void setFilePath(const QString &path)
{
    overridden() = path;
}

Preferences load()
{
    const QSettings store(filePath(), QSettings::IniFormat);
    Preferences p;
    p.cacheMb = readInt(store, QStringLiteral("cache/megabytes"), Preferences::DefaultCacheMb,
                        Preferences::MinCacheMb, Preferences::MaxCacheMb);
    p.prefetchRadius = readInt(store, QStringLiteral("cache/prefetch"), Preferences::DefaultPrefetch,
                               Preferences::MinPrefetch, Preferences::MaxPrefetch);
    p.slideshowSeconds = readInt(store, QStringLiteral("slideshow/seconds"),
                                 Preferences::DefaultSlideshowSeconds,
                                 Preferences::MinSlideshowSeconds, Preferences::MaxSlideshowSeconds);
    return p;
}

void save(const Preferences &p)
{
    QSettings store(filePath(), QSettings::IniFormat);
    store.setValue(QStringLiteral("cache/megabytes"), p.cacheMb);
    store.setValue(QStringLiteral("cache/prefetch"), p.prefetchRadius);
    store.setValue(QStringLiteral("slideshow/seconds"), p.slideshowSeconds);
}

QByteArray windowValue(const QString &key)
{
    const QSettings store(filePath(), QSettings::IniFormat);
    return store.value(QStringLiteral("window/") + key).toByteArray();
}

void setWindowValue(const QString &key, const QByteArray &value)
{
    QSettings store(filePath(), QSettings::IniFormat);
    store.setValue(QStringLiteral("window/") + key, value);
}

} // namespace settings
