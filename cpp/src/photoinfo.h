// What the camera recorded about a photograph: body, lens, exposure, time.
//
// Read from two places. Camera RAW comes from LibRaw, which the RAW decoder
// already has open. Everything else comes from an EXIF block parsed by
// libexif: found in the file for JPEG (its APP1 segment), PNG (an eXIf chunk),
// WebP (an EXIF chunk) and TIFF (its own directories, condensed first), or
// handed over by libheif for HEIC and AVIF.

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>

#include <optional>

namespace photoinfo {

struct PhotoInfo {
    QString make;
    QString model;
    QString lens;
    double focalLength = 0;             ///< millimetres; 0 when not recorded
    int focalLength35 = 0;              ///< the 35 mm equivalent; 0 when not recorded
    double aperture = 0;                ///< f-number
    double exposureTime = 0;            ///< seconds
    int iso = 0;
    std::optional<double> exposureBias; ///< EV
    QDateTime taken;                    ///< the camera's clock as recorded, no time zone
    std::optional<double> latitude;     ///< degrees, south negative
    std::optional<double> longitude;    ///< degrees, west negative
    std::optional<double> altitude;     ///< metres, below sea level negative

    bool isEmpty() const;
};

/// Parse an EXIF block: a TIFF header and its directories, with or without
/// the "Exif\0\0" prefix JPEG puts in front. Anything unreadable gives an
/// empty PhotoInfo rather than an error -- a photograph without EXIF is
/// ordinary, and a damaged block is not worth failing a decode over.
PhotoInfo fromExif(const QByteArray &block);

/// Record a position, unless it is out of range or exactly 0,0 -- which is
/// what a camera without a fix writes, not where anyone took a photograph.
void setPosition(PhotoInfo &info, std::optional<double> latitude,
                 std::optional<double> longitude);

/// The EXIF block from a JPEG's APP1 segment, or empty if it has none.
/// Walks the segment headers rather than reading the whole file.
QByteArray exifFromJpeg(const QString &path);

/// The EXIF block from a PNG's eXIf chunk, or empty if it has none.
QByteArray exifFromPng(const QString &path);

/// The EXIF block from a WebP's EXIF chunk, or empty if it has none.
QByteArray exifFromWebp(const QString &path);

/// A TIFF keeps EXIF in its own directories, anywhere in the file -- often
/// after the image data, past the first 64 KiB, which is as far as libexif
/// will look. This copies the entries read here (IFD0's make and model, the
/// EXIF and GPS directories) into a compact block of their own.
QByteArray exifFromTiff(const QString &path);
QByteArray exifFromTiffBytes(const QByteArray &tiff);

/// HEIF stores EXIF behind a 4-byte big-endian offset to the TIFF header
/// (ISO/IEC 23008-12, Annex A); this strips it. Empty if the offset points
/// outside the block.
QByteArray exifFromHeifItem(const QByteArray &item);

// Formatting, separate so it can be tested without any file. Each returns an
// empty string for a value that was not recorded.

/// "Canon EOS R5", not "Canon Canon EOS R5"; "NIKON Z 6", not
/// "NIKON CORPORATION NIKON Z 6".
QString camera(const PhotoInfo &info);
/// "70 mm", or "5.1 mm (26 mm equivalent)" when the two differ.
QString focalLength(const PhotoInfo &info);
/// "f/5.6", "f/8".
QString aperture(double fNumber);
/// "1/250 s" below about half a second, "0.8 s" or "2 s" above.
QString exposureTime(double seconds);
/// "+0.7 EV", "-1 EV"; empty for zero, which is what nearly every camera
/// records and is not worth a line.
QString exposureBias(double ev);

/// "25.033964° N, 121.564472° E"; empty without a position.
QString location(const PhotoInfo &info);
/// The position on OpenStreetMap, for a link the user may choose to follow.
QString mapUrl(const PhotoInfo &info);

/// Label and value for each thing recorded, in reading order.
QList<QPair<QString, QString>> rows(const PhotoInfo &info);

} // namespace photoinfo
