// Byte-budgeted LRU cache for decoded images.
//
// Counting entries instead of bytes is the classic way to blow up an image
// viewer: a hundred thumbnails and a hundred 100-megapixel TIFFs are the same
// number, three orders of magnitude apart in memory.

#pragma once

#include <QString>

#include <list>
#include <mutex>
#include <unordered_map>

#include "decoders/decoder.h"

/// Identifies a file by path plus mtime and size, so edits invalidate it --
/// and so a file that is repaired after a failure is tried again on its own.
struct CacheKey {
    QString path;
    qint64 mtime = 0;
    qint64 size = 0;

    bool operator==(const CacheKey &other) const = default;

    static CacheKey of(const QString &path);
};

template <>
struct std::hash<CacheKey> {
    std::size_t operator()(const CacheKey &key) const noexcept
    {
        return qHash(key.path) ^ (std::hash<qint64>{}(key.mtime) << 1)
               ^ (std::hash<qint64>{}(key.size) << 2);
    }
};

class ImageCache {
public:
    static constexpr qint64 DefaultBudget = 1024LL * 1024 * 1024;  // 1 GiB of pixels
    static constexpr std::size_t MaxFailed = 2048;

    explicit ImageCache(qint64 budget = DefaultBudget);

    std::optional<decoders::DecodedImage> get(const CacheKey &key);
    void put(const CacheKey &key, const decoders::DecodedImage &item);
    void clear();

    /// Remember that a file could not be decoded. Without this, a folder of
    /// unreadable files is retried on every navigation, since nothing ever
    /// lands in the cache to stop the prefetcher.
    void markFailed(const CacheKey &key);
    bool hasFailed(const CacheKey &key);

    qint64 nbytes() const { return m_bytes; }
    qint64 budget() const { return m_budget; }

private:
    void evict();   // caller holds the lock

    using Entry = std::pair<CacheKey, decoders::DecodedImage>;

    qint64 m_budget;
    qint64 m_bytes = 0;
    std::list<Entry> m_order;                                      // front = oldest
    std::unordered_map<CacheKey, std::list<Entry>::iterator> m_index;
    std::list<CacheKey> m_failedOrder;
    std::unordered_map<CacheKey, std::list<CacheKey>::iterator> m_failed;
    mutable std::mutex m_mutex;
};
