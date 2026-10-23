// Container-format detection from magic bytes.
//
// Extensions lie often enough that we sniff first and only fall back to the
// suffix when the header is inconclusive. The result is a coarse *container*
// label, not a decoder choice -- several RAW formats are TIFF containers, so
// telling NEF from TIFF is left to the decoders' own scoring.

#pragma once

#include <QByteArray>
#include <QString>

namespace sniff {

// An enum rather than the Python version's strings: the decoders compare
// these constantly, and a typo becomes a compile error instead of a format
// that silently never matches.
enum class Container {
    Unknown,
    Jpeg,
    Png,
    Gif,
    Bmp,
    Tiff,
    Webp,
    Heif,
    Avif,
    Psd,
    Ico,
    Jp2,
    Pnm,
    Cr3,
    Raf,
    Rw2,
    Orf,
    X3f,
};

inline constexpr int HeaderBytes = 32;

/// Human-readable name, for logs and error messages.
QString name(Container container);

/// First bytes of a file. Empty if it cannot be read -- an unreadable file is
/// the caller's problem to report, not something to throw from here.
QByteArray readHeader(const QString &path, int size = HeaderBytes);

/// Coarse container for a file on disk.
Container of(const QString &path);

/// Coarse container given a header already in hand. Exposed separately so the
/// tests can cover every format without touching the filesystem.
Container of(const QString &path, const QByteArray &header);

} // namespace sniff
