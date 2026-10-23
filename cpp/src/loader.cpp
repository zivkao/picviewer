#include "loader.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRunnable>
#include <QThread>

#include "decoders/registry.h"
#include "sniff.h"

Q_LOGGING_CATEGORY(logLoader, "picviewer.loader")

namespace {
constexpr int PriorityForeground = 10;
constexpr int PriorityPrefetch = 0;

QSize decodeCap()
{
    const int side = static_cast<int>(std::sqrt(static_cast<double>(MaxDecodePixels)));
    return {side, side};
}
} // namespace

/// Decode one file, emitting a preview first when the format offers one.
class LoadTask : public QRunnable
{
public:
    LoadTask(ImageLoader *loader, QString path, quint64 generation, bool cacheOnly)
        : m_loader(loader)
        , m_path(std::move(path))
        , m_generation(generation)
        , m_cacheOnly(cacheOnly)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        const CacheKey key = CacheKey::of(m_path);
        const auto &registry = decoders::Registry::instance();

        if (auto cached = m_loader->cache().get(key); cached && !cached->isPreview) {
            qCDebug(logLoader).noquote() << "cache hit" << QFileInfo(m_path).fileName();
            emitReady(*cached);
            return;
        }

        if (!m_cacheOnly)
            emitPreview(registry);

        QElapsedTimer timer;
        timer.start();

        decoders::DecodeError error;
        auto result = registry.loadFull(m_path, decodeCap(), &error);
        if (!result) {
            m_loader->cache().markFailed(key);
            // Only the first line: these messages are several lines of
            // guidance meant for the screen, and the full text per attempt
            // buries everything else in the log.
            const QString summary = error.message.section(QLatin1Char('\n'), 0, 0);
            if (m_cacheOnly) {
                qCDebug(logLoader).noquote() << "prefetch failed for"
                                   << QFileInfo(m_path).fileName() << ":" << summary;
            } else {
                const QFileInfo info(m_path);
                qCCritical(logLoader).noquote().nospace()
                    << "decode failed for " << info.fileName()
                    << " (" << info.size() << " bytes, "
                    << sniff::name(sniff::of(m_path)) << "): " << summary;
                // Each decoder's own complaint, one per line. The single-line
                // summary above is what the user sees; this is what says which
                // backend objected and to what.
                for (const QString &line : error.message.split(QLatin1Char('\n'))) {
                    if (line.contains(QLatin1Char(':')) && !line.trimmed().isEmpty()
                        && !line.startsWith(QLatin1String("  -")))
                        qCCritical(logLoader).noquote() << "   " << line.trimmed();
                }
                QMetaObject::invokeMethod(
                    m_loader, "failed", Qt::QueuedConnection,
                    Q_ARG(QString, m_path), Q_ARG(QString, error.message),
                    Q_ARG(quint64, m_generation));
            }
            return;
        }

        ImageCache &cache = m_loader->cache();
        const qint64 before = cache.nbytes();
        cache.put(key, *result);
        if (cache.nbytes() < before) {
            // Went down after an insert, so older entries were dropped. The
            // only visible sign of memory pressure there is.
            qCInfo(logLoader).nospace()
                << "cache evicted " << (before + result->nbytes() - cache.nbytes()) / 1000000
                << " MB, now " << cache.nbytes() / 1000000 << " of "
                << cache.budget() / 1000000 << " MB";
        }
        qCInfo(logLoader).nospace().noquote()
            << "decoded " << QFileInfo(m_path).fileName()
            << " via " << result->metadata.value(QStringLiteral("decoder"), QStringLiteral("?"))
            << " in " << timer.elapsed() << "ms -> "
            << result->image.width() << "x" << result->image.height()
            << " (" << QString::number(result->nbytes() / 1e6, 'f', 1) << " MB)"
            << (m_cacheOnly ? " [prefetch]" : "");
        emitReady(*result);
    }

private:
    void emitPreview(const decoders::Registry &registry)
    {
        auto preview = registry.loadPreview(m_path, {PreviewMaxSide, PreviewMaxSide});
        if (!preview)
            return;
        qCInfo(logLoader).nospace().noquote()
            << "preview for " << QFileInfo(m_path).fileName() << " -> "
            << preview->image.width() << "x" << preview->image.height();
        deliver(*preview);
    }

    void emitReady(const decoders::DecodedImage &image)
    {
        if (!m_cacheOnly)
            deliver(image);
    }

    void deliver(const decoders::DecodedImage &image)
    {
        // Queued: this runs on a pool thread, and the viewer touches QPixmap,
        // which only exists on the GUI thread.
        QMetaObject::invokeMethod(
            m_loader, "ready", Qt::QueuedConnection,
            Q_ARG(QString, m_path),
            Q_ARG(decoders::DecodedImage, image),
            Q_ARG(quint64, m_generation));
    }

    ImageLoader *m_loader;
    QString m_path;
    quint64 m_generation;
    bool m_cacheOnly;
};

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
{
    // Leave a core for the GUI thread; decoding is CPU-bound and will happily
    // saturate everything otherwise.
    m_pool.setMaxThreadCount(qMax(2, QThread::idealThreadCount() - 1));
}

ImageLoader::~ImageLoader()
{
    shutdown();
}

void ImageLoader::load(const QString &path, quint64 generation)
{
    m_pool.start(new LoadTask(this, path, generation, false), PriorityForeground);
}

void ImageLoader::prefetch(const QStringList &paths)
{
    for (const QString &path : paths) {
        const CacheKey key = CacheKey::of(path);
        if (m_cache.get(key) || m_cache.hasFailed(key))
            continue;
        m_pool.start(new LoadTask(this, path, 0, true), PriorityPrefetch);
    }
}

void ImageLoader::shutdown()
{
    m_pool.clear();
    m_pool.waitForDone(3000);
}
