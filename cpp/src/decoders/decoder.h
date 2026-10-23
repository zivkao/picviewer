// Decoder interface shared by every format backend.

#pragma once

#include <QImage>
#include <QMap>
#include <QMetaType>
#include <QSize>
#include <QString>

#include <optional>

#include "sniff.h"

namespace decoders {

// Confidence levels returned by Decoder::score(). Anything <= 0 means "cannot
// handle"; the registry tries candidates from highest to lowest and falls
// through to the next on failure.
inline constexpr int ScoreExact = 100;      // magic bytes identify a format this decoder owns
inline constexpr int ScoreLikely = 60;      // plausible from the container, e.g. TIFF-based RAW
inline constexpr int ScoreExtension = 40;   // only the file suffix suggests it
inline constexpr int ScoreFallback = 10;    // last resort, let the library try

/// One decode result, possibly a stand-in for a slower full decode.
struct DecodedImage {
    QImage image;
    bool isPreview = false;
    QSize fullSize;                     ///< true dimensions of the source
    QMap<QString, QString> metadata;

    qint64 nbytes() const { return qMax<qint64>(image.sizeInBytes(), 1); }
};

/// Why a decode failed, for a message the user can act on.
struct DecodeError {
    enum class Kind {
        Unsupported,    ///< no decoder could read it
        Unreadable,     ///< the bytes could not be reached at all
    };
    Kind kind = Kind::Unsupported;
    QString message;
};

class Decoder {
public:
    virtual ~Decoder() = default;

    virtual QString name() const = 0;

    /// How confident this decoder is that it can read *path*.
    virtual int score(const QString &path, sniff::Container container) const = 0;

    /// A fast, possibly degraded decode. nullopt if there is no fast path.
    virtual std::optional<DecodedImage> loadPreview(const QString &path,
                                                    sniff::Container container,
                                                    QSize maxSize) const
    {
        Q_UNUSED(path);
        Q_UNUSED(container);
        Q_UNUSED(maxSize);
        return std::nullopt;
    }

    /// The accurate decode, optionally downscaled to fit *maxSize*.
    /// Returns nullopt and sets *error* rather than throwing: these failures
    /// are routine (a corrupt file, a format this backend does not really
    /// handle) and the registry needs to try the next candidate.
    virtual std::optional<DecodedImage> loadFull(const QString &path,
                                                 sniff::Container container,
                                                 QSize maxSize,
                                                 QString *error) const = 0;
};

} // namespace decoders

// Queued signals copy their arguments through the metatype system, and the
// loader delivers decoded images from a worker thread to the GUI thread that
// way, so the type has to be registered or the connection silently does
// nothing at runtime.
Q_DECLARE_METATYPE(decoders::DecodedImage)
