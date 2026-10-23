// Asynchronous decoding: worker pool, two-stage delivery, neighbour prefetch.

#pragma once

#include <QObject>
#include <QThreadPool>

#include "cache.h"
#include "decoders/decoder.h"

// Beyond this we downscale on decode. A 200MP RGB888 image is already 600MB;
// letting the truly pathological files through would starve everything else.
inline constexpr qint64 MaxDecodePixels = 200'000'000;
inline constexpr int PreviewMaxSide = 3840;

class ImageLoader : public QObject
{
    Q_OBJECT

public:
    explicit ImageLoader(QObject *parent = nullptr);
    ~ImageLoader() override;

    /// Decode *path* and deliver it with this generation stamped on it.
    void load(const QString &path, quint64 generation);

    /// Warm the cache for images the user is about to reach.
    void prefetch(const QStringList &paths);

    void shutdown();

    ImageCache &cache() { return m_cache; }

signals:
    /// Emitted on the GUI thread. May fire twice for one load: a preview
    /// first, then the full decode.
    void ready(const QString &path, const decoders::DecodedImage &image, quint64 generation);
    void failed(const QString &path, const QString &message, quint64 generation);

private:
    friend class LoadTask;

    ImageCache m_cache;
    QThreadPool m_pool;
};
