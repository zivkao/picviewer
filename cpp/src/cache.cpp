#include "cache.h"

#include <QFileInfo>

CacheKey CacheKey::of(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {path, 0, 0};
    return {path, info.lastModified().toMSecsSinceEpoch(), info.size()};
}

ImageCache::ImageCache(qint64 budget)
    : m_budget(budget)
{
}

std::optional<decoders::DecodedImage> ImageCache::get(const CacheKey &key)
{
    const std::lock_guard lock(m_mutex);
    const auto found = m_index.find(key);
    if (found == m_index.end())
        return std::nullopt;

    // Splice to the back: most recently used.
    m_order.splice(m_order.end(), m_order, found->second);
    return found->second->second;
}

void ImageCache::put(const CacheKey &key, const decoders::DecodedImage &item)
{
    const std::lock_guard lock(m_mutex);

    if (const auto existing = m_index.find(key); existing != m_index.end()) {
        // A full decode supersedes the preview it replaced.
        m_bytes -= existing->second->second.nbytes();
        m_order.erase(existing->second);
        m_index.erase(existing);
    }

    m_order.emplace_back(key, item);
    m_index[key] = std::prev(m_order.end());
    m_bytes += item.nbytes();
    evict();
}

void ImageCache::evict()
{
    // Always keep the most recent entry, even if it alone exceeds budget --
    // evicting it would mean never being able to show that image at all.
    while (m_bytes > m_budget && m_order.size() > 1) {
        const Entry &oldest = m_order.front();
        m_bytes -= oldest.second.nbytes();
        m_index.erase(oldest.first);
        m_order.pop_front();
    }
}

void ImageCache::clear()
{
    const std::lock_guard lock(m_mutex);
    m_order.clear();
    m_index.clear();
    m_failedOrder.clear();
    m_failed.clear();
    m_bytes = 0;
}

void ImageCache::markFailed(const CacheKey &key)
{
    const std::lock_guard lock(m_mutex);
    if (m_failed.contains(key))
        return;

    m_failedOrder.push_back(key);
    m_failed[key] = std::prev(m_failedOrder.end());
    while (m_failed.size() > MaxFailed) {
        m_failed.erase(m_failedOrder.front());
        m_failedOrder.pop_front();
    }
}

bool ImageCache::hasFailed(const CacheKey &key)
{
    const std::lock_guard lock(m_mutex);
    return m_failed.contains(key);
}
