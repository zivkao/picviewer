// The thumbnail strip's pictures: made small once, kept on disk, loaded only
// when they scroll into view.

#pragma once

#include <QAbstractListModel>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

namespace thumbnails {

/// Longest side, in physical pixels: the strip shows them at 96 logical
/// pixels, which on a display scaled to 200% is this.
inline constexpr int Size = 192;

/// Kept beside the logs, in %LOCALAPPDATA%\PicViewer\thumbnails.
QString directory();
/// The tests point this at a temporary folder.
void setDirectory(const QString &path);

/// The cache file for *path* as it is now: changing the file -- its time or
/// its size -- changes the key, so an edited photograph gets a new thumbnail
/// rather than the old one. Empty if the file cannot be stat'ed.
QString cacheFile(const QString &path);

/// The cached thumbnail, or a null image.
QImage loadCached(const QString &path);
void store(const QString &path, const QImage &thumbnail);

/// Decode *path* small: a RAW's embedded preview where there is one, which
/// is milliseconds against a second or more to demosaic; everything else
/// asked for at thumbnail size, which JPEG in particular does during the
/// inverse DCT. Null if it will not decode.
QImage make(const QString &path);

/// Delete the least recently used thumbnails until the folder is under
/// *maxBytes*. A cache hit touches its file, so "used" means shown.
void prune(qint64 maxBytes);
inline constexpr qint64 DefaultBudget = 200LL * 1024 * 1024;

} // namespace thumbnails

/// One row per image in the playlist; the picture comes when asked for.
class ThumbnailModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit ThumbnailModel(QObject *parent = nullptr);
    ~ThumbnailModel() override;

    void setPaths(const QStringList &paths);

    int rowCount(const QModelIndex &parent = {}) const override;
    /// The decoration is requested here, so only rows a view actually paints
    /// -- the ones in sight -- are ever decoded.
    QVariant data(const QModelIndex &index, int role) const override;

    // Counts for the tests and the log.
    bool isLoaded(int row) const;
    int fromDisk() const { return m_fromDisk; }
    int decoded() const { return m_decoded; }

private:
    void request(const QString &path) const;
    void deliver(const QString &path, const QImage &image, bool fromDisk);

    QStringList m_paths;
    QHash<QString, int> m_rowOf;
    /// Bounded by memory, not count: a folder of five thousand photographs
    /// would otherwise keep half a gigabyte of thumbnails. An evicted one
    /// comes back from disk in a millisecond when it scrolls into view again.
    mutable QCache<QString, QPixmap> m_pixmaps;
    QSet<QString> m_failed;
    mutable QSet<QString> m_pending;
    mutable int m_priority = 0;
    QPixmap m_placeholder;
    QPixmap m_broken;
    int m_fromDisk = 0;
    int m_decoded = 0;
    /// Two threads of their own, so the strip filling in never holds up the
    /// image the user is looking at.
    mutable QThreadPool m_pool;
};
