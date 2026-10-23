// Decoder dispatch: sniff the file, rank the candidates, try until one wins.

#pragma once

#include <QSet>
#include <QString>

#include <memory>
#include <vector>

#include "decoder.h"

namespace decoders {

class Registry {
public:
    Registry();

    /// Decoders that might read *path*, most confident first. Pass a
    /// container to rank without touching the disk; it is sniffed otherwise.
    std::vector<const Decoder *> candidates(
        const QString &path,
        std::optional<sniff::Container> container = std::nullopt) const;

    /// Fast stand-in decode, or nullopt when no decoder offers one.
    std::optional<DecodedImage> loadPreview(const QString &path, QSize maxSize) const;

    /// Accurate decode. Sets *error* and returns nullopt if every candidate fails.
    std::optional<DecodedImage> loadFull(const QString &path,
                                         QSize maxSize,
                                         DecodeError *error) const;

    QStringList decoderNames() const;
    QSet<QString> supportedExtensions() const;

    static Registry &instance();

private:
    std::vector<std::unique_ptr<Decoder>> m_decoders;
};

/// Establish that the bytes are reachable before blaming any decoder.
/// Otherwise an unreadable file surfaces as a decoder error such as
/// "heif: permission denied", which points at the format when the real
/// problem is the filesystem.
std::optional<DecodeError> checkReadable(const QString &path);

} // namespace decoders
