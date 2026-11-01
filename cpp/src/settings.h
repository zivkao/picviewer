// What the user can change, and what the window remembers between sessions.
//
// Kept in an INI file beside the logs, picviewer-cpp.ini in
// %LOCALAPPDATA%\PicViewer, rather than the registry: one place to look, readable
// in Notepad, and deleting the PicViewer folder resets everything. The
// uninstaller leaves it, as it leaves the logs.

#pragma once

#include <QByteArray>
#include <QString>

struct Preferences {
    static constexpr int MinCacheMb = 256;
    static constexpr int MaxCacheMb = 16384;
    static constexpr int DefaultCacheMb = 1024;
    static constexpr int MinPrefetch = 0;
    static constexpr int MaxPrefetch = 5;
    static constexpr int DefaultPrefetch = 2;
    static constexpr int MinSlideshowSeconds = 1;
    static constexpr int MaxSlideshowSeconds = 60;
    static constexpr int DefaultSlideshowSeconds = 4;

    /// Decoded pixels kept in memory, so stepping back is instant.
    int cacheMb = DefaultCacheMb;
    /// Images decoded ahead on each side of the current one.
    int prefetchRadius = DefaultPrefetch;
    int slideshowSeconds = DefaultSlideshowSeconds;

    bool operator==(const Preferences &) const = default;
};

namespace settings {

QString filePath();

/// Point everything at another file. The tests do, so that running them
/// neither reads nor overwrites the settings of whoever runs them.
void setFilePath(const QString &path);

/// Values out of range, or not numbers at all, come back as the nearest
/// sensible value rather than as whatever the file says: it is a text file,
/// and anyone can edit it.
Preferences load();
void save(const Preferences &preferences);

/// Opaque blobs the window keeps for itself: geometry and dock layout.
QByteArray windowValue(const QString &key);
void setWindowValue(const QString &key, const QByteArray &value);

} // namespace settings
