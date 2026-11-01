#include "thumbnails.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPointer>
#include <QSaveFile>

#include <algorithm>

#include "applog.h"
#include "decoders/registry.h"

namespace thumbnails {

namespace {

QString &overridden()
{
    static QString path;
    return path;
}

} // namespace

QString directory()
{
    if (!overridden().isEmpty())
        return overridden();
    return QDir(applog::directory()).absoluteFilePath(QStringLiteral("../thumbnails"));
}

void setDirectory(const QString &path)
{
    overridden() = path;
}

QString cacheFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    // The size in the key too: a thumbnail made at another size is another
    // thumbnail.
    const QByteArray identity = info.absoluteFilePath().toUtf8() + '|'
                                + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '|'
                                + QByteArray::number(info.size()) + '|' + QByteArray::number(Size);
    const QByteArray hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex();
    return QDir(directory()).filePath(QString::fromLatin1(hash) + QStringLiteral(".jpg"));
}

QImage loadCached(const QString &path)
{
    const QString file = cacheFile(path);
    if (file.isEmpty())
        return {};
    QImage image(file);
    if (!image.isNull()) {
        // Touched, so pruning takes what has not been looked at in longest.
        QFile touched(file);
        if (touched.open(QIODevice::ReadWrite))
            touched.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
    return image;
}

void store(const QString &path, const QImage &thumbnail)
{
    const QString file = cacheFile(path);
    if (file.isEmpty() || thumbnail.isNull())
        return;
    QDir().mkpath(directory());
    // Written whole or not at all, so a crash mid-write cannot leave half a
    // JPEG that loads as a broken thumbnail forever.
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly) && thumbnail.save(&out, "JPEG", 85))
        out.commit();
}

QImage make(const QString &path)
{
    const auto &registry = decoders::Registry::instance();
    const QSize box(Size, Size);
    std::optional<decoders::DecodedImage> decoded = registry.loadPreview(path, box);
    if (!decoded) {
        decoders::DecodeError error;
        decoded = registry.loadFull(path, box, &error);
    }
    if (!decoded || decoded->image.isNull())
        return {};
    QImage image = decoded->image;
    if (image.width() > Size || image.height() > Size)
        image = image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // JPEG has no alpha: composite onto the strip's own grey rather than the
    // black a transparent PNG would otherwise turn.
    if (image.hasAlphaChannel()) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(QColor(58, 60, 64));
        QPainter painter(&flat);
        painter.drawImage(0, 0, image);
        image = flat;
    }
    return image.convertToFormat(QImage::Format_RGB32);
}

void prune(qint64 maxBytes)
{
    QDir dir(directory());
    QFileInfoList files = dir.entryInfoList({QStringLiteral("*.jpg")}, QDir::Files);
    qint64 total = 0;
    for (const QFileInfo &f : files)
        total += f.size();
    if (total <= maxBytes)
        return;
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });
    for (const QFileInfo &f : files) {
        if (total <= maxBytes)
            break;
        if (QFile::remove(f.absoluteFilePath()))
            total -= f.size();
    }
}

} // namespace thumbnails

// ---------------------------------------------------------------------------

namespace {

QPixmap tile(const QColor &fill, const QColor &mark)
{
    QPixmap pixmap(96, 72);
    pixmap.fill(fill);
    QPainter painter(&pixmap);
    painter.setPen(QPen(mark, 2));
    painter.drawRect(pixmap.rect().adjusted(1, 1, -2, -2));
    return pixmap;
}

} // namespace

ThumbnailModel::ThumbnailModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_pixmaps(64 * 1024)      // KiB
    , m_placeholder(tile(QColor(58, 60, 64), QColor(74, 76, 80)))
    , m_broken(tile(QColor(70, 40, 40), QColor(150, 70, 70)))
{
    m_pool.setMaxThreadCount(2);
}

ThumbnailModel::~ThumbnailModel()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void ThumbnailModel::setPaths(const QStringList &paths)
{
    beginResetModel();
    m_paths = paths;
    m_rowOf.clear();
    for (int row = 0; row < m_paths.size(); ++row)
        m_rowOf.insert(m_paths.at(row), row);
    // Queued work for the last folder is dropped; what is already running
    // finishes and fills the disk cache, which is not wasted.
    m_pool.clear();
    m_pending.clear();
    endResetModel();
}

int ThumbnailModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_paths.size());
}

QVariant ThumbnailModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_paths.size())
        return {};
    const QString &path = m_paths.at(index.row());
    switch (role) {
    case Qt::DecorationRole:
        if (const QPixmap *pixmap = m_pixmaps.object(path))
            return *pixmap;
        if (m_failed.contains(path))
            return m_broken;
        request(path);
        return m_placeholder;
    case Qt::ToolTipRole:
        return QFileInfo(path).fileName();
    default:
        return {};
    }
}

bool ThumbnailModel::isLoaded(int row) const
{
    return row >= 0 && row < m_paths.size()
           && (m_pixmaps.contains(m_paths.at(row)) || m_failed.contains(m_paths.at(row)));
}

void ThumbnailModel::request(const QString &path) const
{
    if (m_pending.contains(path))
        return;
    m_pending.insert(path);
    QPointer<ThumbnailModel> self(const_cast<ThumbnailModel *>(this));
    // Newest first: scrolling quickly past a hundred thumbnails should fill
    // in the ones now in sight, not the ones it scrolled past.
    m_pool.start([self, path] {
        QImage image = thumbnails::loadCached(path);
        const bool fromDisk = !image.isNull();
        if (!fromDisk) {
            image = thumbnails::make(path);
            thumbnails::store(path, image);
        }
        if (self)
            QMetaObject::invokeMethod(self, [self, path, image, fromDisk] {
                if (self)
                    self->deliver(path, image, fromDisk);
            }, Qt::QueuedConnection);
    }, ++m_priority);
}

void ThumbnailModel::deliver(const QString &path, const QImage &image, bool fromDisk)
{
    m_pending.remove(path);
    if (image.isNull()) {
        m_failed.insert(path);
    } else {
        m_pixmaps.insert(path, new QPixmap(QPixmap::fromImage(image)),
                         qMax<qsizetype>(1, image.sizeInBytes() / 1024));
        ++(fromDisk ? m_fromDisk : m_decoded);
    }
    if (const auto row = m_rowOf.constFind(path); row != m_rowOf.constEnd()) {
        const QModelIndex at = index(*row);
        emit dataChanged(at, at, {Qt::DecorationRole});
    }
}
