#include "photoinfo.h"

#include <QFile>
#include <QtEndian>

#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>

namespace photoinfo {

namespace {

const QByteArray ExifHeader("Exif\0\0", 6);

// Tag numbers rather than libexif's enum names: a few of these (LensModel
// among them) were added to the enum late, and the numbers are the standard.
constexpr int TagMake = 0x010f;
constexpr int TagModel = 0x0110;
constexpr int TagExposureTime = 0x829a;
constexpr int TagFNumber = 0x829d;
constexpr int TagIso = 0x8827;
constexpr int TagDateTimeOriginal = 0x9003;
constexpr int TagDateTimeDigitized = 0x9004;
constexpr int TagExposureBias = 0x9204;
constexpr int TagFocalLength = 0x920a;
constexpr int TagFocalLength35 = 0xa405;
constexpr int TagLensModel = 0xa434;
constexpr int TagExifPointer = 0x8769;
constexpr int TagGpsPointer = 0x8825;
// In the GPS directory's own numbering.
constexpr int TagGpsLatitudeRef = 1;
constexpr int TagGpsLatitude = 2;
constexpr int TagGpsLongitudeRef = 3;
constexpr int TagGpsLongitude = 4;
constexpr int TagGpsAltitudeRef = 5;
constexpr int TagGpsAltitude = 6;

struct ExifDataDeleter {
    void operator()(ExifData *data) const { exif_data_unref(data); }
};

ExifEntry *find(ExifData *data, ExifIfd ifd, int tag)
{
    return exif_content_get_entry(data->ifd[ifd], static_cast<ExifTag>(tag));
}

QString text(const ExifEntry *entry)
{
    if (!entry || entry->format != EXIF_FORMAT_ASCII || !entry->data || entry->size == 0)
        return {};
    // NUL-terminated by the specification, and not always in practice.
    const auto *chars = reinterpret_cast<const char *>(entry->data);
    return QString::fromUtf8(chars, qstrnlen(chars, entry->size)).simplified();
}

double number(const ExifEntry *entry, ExifByteOrder order)
{
    if (!entry || !entry->data || entry->components < 1)
        return 0;
    switch (entry->format) {
    case EXIF_FORMAT_RATIONAL:
        if (entry->size >= 8) {
            const ExifRational r = exif_get_rational(entry->data, order);
            return r.denominator ? double(r.numerator) / r.denominator : 0;
        }
        break;
    case EXIF_FORMAT_SRATIONAL:
        if (entry->size >= 8) {
            const ExifSRational r = exif_get_srational(entry->data, order);
            return r.denominator ? double(r.numerator) / r.denominator : 0;
        }
        break;
    case EXIF_FORMAT_SHORT:
        if (entry->size >= 2)
            return exif_get_short(entry->data, order);
        break;
    case EXIF_FORMAT_LONG:
        if (entry->size >= 4)
            return qMin<double>(exif_get_long(entry->data, order), INT_MAX);
        break;
    default:
        break;
    }
    return 0;
}

/// Degrees from GPS's three rationals -- degrees, minutes, seconds -- signed
/// by the matching reference tag ("S" or "W" for negative).
std::optional<double> degrees(const ExifEntry *entry, const ExifEntry *ref, ExifByteOrder order,
                              QChar negative)
{
    if (!entry || !entry->data || entry->format != EXIF_FORMAT_RATIONAL
        || entry->components < 3 || entry->size < 24)
        return {};
    double parts[3];
    for (int i = 0; i < 3; ++i) {
        const ExifRational r = exif_get_rational(entry->data + 8 * i, order);
        // 0/0 for the minutes or seconds is a writer saying "none".
        if (r.denominator == 0 && (i == 0 || r.numerator != 0))
            return {};
        parts[i] = r.denominator ? double(r.numerator) / r.denominator : 0;
    }
    const double value = parts[0] + parts[1] / 60 + parts[2] / 3600;
    return text(ref).startsWith(negative, Qt::CaseInsensitive) ? -value : value;
}

QDateTime dateTime(const ExifEntry *entry)
{
    // "2025:05:01 13:22:10". Cameras with no clock set write spaces and
    // colons in the same shape, which simply fails to parse.
    return QDateTime::fromString(text(entry), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
}

/// Up to one decimal place, without a trailing ".0".
QString trimmed(double value)
{
    QString s = QString::number(value, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}

} // namespace

bool PhotoInfo::isEmpty() const
{
    return make.isEmpty() && model.isEmpty() && lens.isEmpty() && focalLength <= 0
           && aperture <= 0 && exposureTime <= 0 && iso <= 0 && !exposureBias
           && !taken.isValid() && !latitude;
}

PhotoInfo fromExif(const QByteArray &block)
{
    QByteArray data = block;
    if (!data.startsWith(ExifHeader)) {
        if (!data.startsWith(QByteArrayView("II*\0", 4)) && !data.startsWith(QByteArrayView("MM\0*", 4)))
            return {};
        // libexif accepts a block only behind this header or inside a JPEG.
        data.prepend(ExifHeader);
    }

    const std::unique_ptr<ExifData, ExifDataDeleter> exif(exif_data_new());
    if (!exif)
        return {};
    // By default libexif "follows the specification": after loading it runs
    // exif_data_fix, which rewrites entries and adds mandatory ones the file
    // left out. This panel reports what the camera wrote, so that is off.
    // (Checked rather than assumed: with it on, the fields read below come
    // out the same for the test blocks, an undated one included. Nothing
    // fails without this line, which is why no mutation guards it.)
    exif_data_unset_option(exif.get(), EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
    exif_data_load_data(exif.get(), reinterpret_cast<const unsigned char *>(data.constData()),
                        static_cast<unsigned int>(data.size()));
    const ExifByteOrder order = exif_data_get_byte_order(exif.get());
    ExifData *d = exif.get();

    PhotoInfo info;
    info.make = text(find(d, EXIF_IFD_0, TagMake));
    info.model = text(find(d, EXIF_IFD_0, TagModel));
    info.lens = text(find(d, EXIF_IFD_EXIF, TagLensModel));
    info.focalLength = number(find(d, EXIF_IFD_EXIF, TagFocalLength), order);
    info.focalLength35 = qRound(number(find(d, EXIF_IFD_EXIF, TagFocalLength35), order));
    info.aperture = number(find(d, EXIF_IFD_EXIF, TagFNumber), order);
    info.exposureTime = number(find(d, EXIF_IFD_EXIF, TagExposureTime), order);
    info.iso = qRound(number(find(d, EXIF_IFD_EXIF, TagIso), order));
    if (const ExifEntry *bias = find(d, EXIF_IFD_EXIF, TagExposureBias))
        info.exposureBias = number(bias, order);
    info.taken = dateTime(find(d, EXIF_IFD_EXIF, TagDateTimeOriginal));
    if (!info.taken.isValid())
        info.taken = dateTime(find(d, EXIF_IFD_EXIF, TagDateTimeDigitized));

    const auto lat = degrees(find(d, EXIF_IFD_GPS, TagGpsLatitude),
                             find(d, EXIF_IFD_GPS, TagGpsLatitudeRef), order, QLatin1Char('S'));
    const auto lon = degrees(find(d, EXIF_IFD_GPS, TagGpsLongitude),
                             find(d, EXIF_IFD_GPS, TagGpsLongitudeRef), order, QLatin1Char('W'));
    setPosition(info, lat, lon);
    if (info.latitude) {
        if (const ExifEntry *alt = find(d, EXIF_IFD_GPS, TagGpsAltitude)) {
            const ExifEntry *ref = find(d, EXIF_IFD_GPS, TagGpsAltitudeRef);
            const bool below = ref && ref->data && ref->size >= 1 && ref->data[0] == 1;
            const double metres = number(alt, order);
            info.altitude = below ? -metres : metres;
        }
    }
    return info;
}

void setPosition(PhotoInfo &info, std::optional<double> latitude, std::optional<double> longitude)
{
    if (!latitude || !longitude)
        return;
    // Out of range is a damaged tag. Exactly 0,0 is what a camera writes
    // when it has no fix -- a point in the Gulf of Guinea, not a photograph.
    if (std::abs(*latitude) > 90 || std::abs(*longitude) > 180
        || (*latitude == 0 && *longitude == 0))
        return;
    info.latitude = latitude;
    info.longitude = longitude;
}

QByteArray exifFromPng(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    static const uchar signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (file.read(8) != QByteArray(reinterpret_cast<const char *>(signature), 8))
        return {};

    // Chunks are length, type, data, CRC. The specification puts eXIf before
    // the image data, and some writers put it after; walk to IEND either way.
    for (int chunk = 0; chunk < 10000; ++chunk) {
        uchar header[8];
        if (file.read(reinterpret_cast<char *>(header), 8) != 8)
            return {};
        const quint32 length = qFromBigEndian<quint32>(header);
        const QByteArray type(reinterpret_cast<const char *>(header + 4), 4);
        if (qint64(length) > file.size() - file.pos())
            return {};
        if (type == "eXIf") {
            const QByteArray data = file.read(length);
            return data.size() == qsizetype(length) ? data : QByteArray();
        }
        if (type == "IEND")
            return {};
        if (!file.seek(file.pos() + length + 4))
            return {};
    }
    return {};
}

QByteArray exifFromWebp(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray riff = file.read(12);
    if (riff.size() != 12 || !riff.startsWith("RIFF") || riff.mid(8, 4) != "WEBP")
        return {};

    // RIFF chunks: a four-character code, a little-endian size, the data,
    // and a pad byte when the size is odd.
    for (int chunk = 0; chunk < 10000; ++chunk) {
        uchar header[8];
        if (file.read(reinterpret_cast<char *>(header), 8) != 8)
            return {};
        const QByteArray code(reinterpret_cast<const char *>(header), 4);
        const quint32 size = qFromLittleEndian<quint32>(header + 4);
        if (qint64(size) > file.size() - file.pos())
            return {};
        if (code == "EXIF") {
            const QByteArray data = file.read(size);
            return data.size() == qsizetype(size) ? data : QByteArray();
        }
        if (!file.seek(file.pos() + size + (size & 1)))
            return {};
    }
    return {};
}

namespace {

/// A TIFF's bytes, read in its own byte order with every access checked.
struct Tiff {
    const uchar *data;
    qint64 size;
    bool big;

    std::optional<quint16> u16(qint64 at) const
    {
        if (at < 0 || at + 2 > size)
            return {};
        return big ? qFromBigEndian<quint16>(data + at) : qFromLittleEndian<quint16>(data + at);
    }
    std::optional<quint32> u32(qint64 at) const
    {
        if (at < 0 || at + 4 > size)
            return {};
        return big ? qFromBigEndian<quint32>(data + at) : qFromLittleEndian<quint32>(data + at);
    }
};

int typeSize(quint16 type)
{
    switch (type) {
    case 1: case 2: case 6: case 7: return 1;     // BYTE ASCII SBYTE UNDEFINED
    case 3: case 8: return 2;                      // SHORT SSHORT
    case 4: case 9: case 11: return 4;             // LONG SLONG FLOAT
    case 5: case 10: case 12: return 8;            // RATIONAL SRATIONAL DOUBLE
    default: return 0;
    }
}

struct Entry {
    quint16 tag;
    quint16 type;
    quint32 count;
    QByteArray value;       ///< raw, in the TIFF's own byte order
};

/// The entries of the directory at *offset* whose tags are wanted, their
/// values copied out. Anything that runs off the end is left out.
QList<Entry> directory(const Tiff &tiff, quint32 offset, std::initializer_list<quint16> wanted)
{
    QList<Entry> found;
    const auto count = tiff.u16(offset);
    if (!count || *count > 1000)
        return found;
    for (int i = 0; i < *count; ++i) {
        const qint64 at = qint64(offset) + 2 + 12 * i;
        const auto tag = tiff.u16(at);
        const auto type = tiff.u16(at + 2);
        const auto n = tiff.u32(at + 4);
        if (!tag || !type || !n)
            break;
        if (std::find(wanted.begin(), wanted.end(), *tag) == wanted.end())
            continue;
        const qint64 bytes = qint64(*n) * typeSize(*type);
        if (bytes <= 0 || bytes > (1 << 20))
            continue;
        qint64 from = at + 8;       // four bytes or fewer sit in the entry itself
        if (bytes > 4) {
            const auto pointed = tiff.u32(at + 8);
            if (!pointed)
                continue;
            from = *pointed;
        }
        if (from + bytes > tiff.size)
            continue;
        found << Entry{*tag, *type, *n,
                       QByteArray(reinterpret_cast<const char *>(tiff.data + from), bytes)};
    }
    return found;
}

} // namespace

QByteArray exifFromTiffBytes(const QByteArray &bytes)
{
    if (bytes.size() < 8)
        return {};
    Tiff tiff{reinterpret_cast<const uchar *>(bytes.constData()), bytes.size(), false};
    if (bytes.startsWith("MM"))
        tiff.big = true;
    else if (!bytes.startsWith("II"))
        return {};
    if (tiff.u16(2) != quint16(42))
        return {};
    const auto ifd0 = tiff.u32(4);
    if (!ifd0)
        return {};

    const auto offsetIn = [&](const Entry &e) -> std::optional<quint32> {
        if (e.value.size() != 4)
            return {};
        const auto *v = reinterpret_cast<const uchar *>(e.value.constData());
        return tiff.big ? qFromBigEndian<quint32>(v) : qFromLittleEndian<quint32>(v);
    };

    QList<Entry> first, exif, gps;
    for (const Entry &e : directory(tiff, *ifd0, {TagMake, TagModel, TagExifPointer, TagGpsPointer})) {
        if (e.tag == TagExifPointer) {
            if (const auto at = offsetIn(e))
                exif = directory(tiff, *at, {TagExposureTime, TagFNumber, TagIso,
                                             TagDateTimeOriginal, TagDateTimeDigitized,
                                             TagExposureBias, TagFocalLength, TagFocalLength35,
                                             TagLensModel});
        } else if (e.tag == TagGpsPointer) {
            if (const auto at = offsetIn(e))
                gps = directory(tiff, *at, {0, TagGpsLatitudeRef, TagGpsLatitude,
                                            TagGpsLongitudeRef, TagGpsLongitude,
                                            TagGpsAltitudeRef, TagGpsAltitude});
        } else {
            first << e;
        }
    }
    if (first.isEmpty() && exif.isEmpty() && gps.isEmpty())
        return {};

    // Written back out in the same byte order, so the values copy across
    // untouched: header, IFD0, the EXIF and GPS directories, then every value
    // too long to sit in its entry.
    const auto u16 = [&](quint16 v) {
        QByteArray b(2, '\0');
        tiff.big ? qToBigEndian(v, b.data()) : qToLittleEndian(v, b.data());
        return b;
    };
    const auto u32 = [&](quint32 v) {
        QByteArray b(4, '\0');
        tiff.big ? qToBigEndian(v, b.data()) : qToLittleEndian(v, b.data());
        return b;
    };
    const auto sizeOf = [](qsizetype entries) { return entries ? quint32(2 + 12 * entries + 4) : 0u; };

    const qsizetype firstCount = first.size() + (exif.isEmpty() ? 0 : 1) + (gps.isEmpty() ? 0 : 1);
    const quint32 firstAt = 8;
    const quint32 exifAt = firstAt + sizeOf(firstCount);
    const quint32 gpsAt = exifAt + sizeOf(exif.size());
    const quint32 overflowAt = gpsAt + sizeOf(gps.size());
    if (!exif.isEmpty())
        first << Entry{TagExifPointer, 4, 1, u32(exifAt)};
    if (!gps.isEmpty())
        first << Entry{TagGpsPointer, 4, 1, u32(gpsAt)};

    QByteArray overflow;
    const auto write = [&](const QList<Entry> &entries) {
        QByteArray out = u16(quint16(entries.size()));
        for (const Entry &e : entries) {
            out += u16(e.tag) + u16(e.type) + u32(e.count);
            if (e.value.size() <= 4) {
                out += e.value + QByteArray(4 - e.value.size(), '\0');
            } else {
                out += u32(overflowAt + quint32(overflow.size()));
                overflow += e.value;
                if (overflow.size() % 2)
                    overflow += '\0';
            }
        }
        return out + u32(0);
    };

    QByteArray out = tiff.big ? QByteArray("MM\0*", 4) : QByteArray("II*\0", 4);
    out += u32(firstAt);
    out += write(first);
    if (!exif.isEmpty())
        out += write(exif);
    if (!gps.isEmpty())
        out += write(gps);
    return out + overflow;
}

QByteArray exifFromTiff(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    if (const uchar *mapped = size > 0 ? file.map(0, size) : nullptr)
        return exifFromTiffBytes(QByteArray::fromRawData(reinterpret_cast<const char *>(mapped), size));
    return exifFromTiffBytes(file.readAll());
}

QByteArray exifFromJpeg(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    uchar marker[2];
    if (file.read(reinterpret_cast<char *>(marker), 2) != 2 || marker[0] != 0xff || marker[1] != 0xd8)
        return {};

    // EXIF sits in the first few segments if anywhere. The cap is for files
    // that are not what they claim, not for real ones.
    for (int segment = 0; segment < 64; ++segment) {
        uchar header[4];
        if (file.read(reinterpret_cast<char *>(header), 4) != 4 || header[0] != 0xff)
            return {};
        const uchar type = header[1];
        // Start of scan or end of image: the metadata segments are behind us.
        if (type == 0xda || type == 0xd9)
            return {};
        const int length = qFromBigEndian<quint16>(header + 2);
        if (length < 2)
            return {};
        const int payload = length - 2;
        if (type == 0xe1 && payload > ExifHeader.size()) {
            const QByteArray data = file.read(payload);
            if (data.size() != payload)
                return {};
            if (data.startsWith(ExifHeader))
                return data;
            continue;       // APP1 also carries XMP; EXIF may follow it
        }
        if (!file.seek(file.pos() + payload))
            return {};
    }
    return {};
}

QByteArray exifFromHeifItem(const QByteArray &item)
{
    if (item.size() < 4)
        return {};
    const quint32 offset = qFromBigEndian<quint32>(item.constData());
    if (offset > quint32(item.size() - 4))
        return {};
    return item.mid(4 + offset);
}

QString camera(const PhotoInfo &info)
{
    const QString make = info.make.simplified();
    const QString model = info.model.simplified();
    if (model.isEmpty())
        return make;
    if (make.isEmpty())
        return model;
    // Makers name themselves in full ("NIKON CORPORATION", "OLYMPUS IMAGING
    // CORP.") and then often again in the model. One word of the make is the
    // brand.
    const QString brand = make.section(QLatin1Char(' '), 0, 0);
    if (model.startsWith(brand, Qt::CaseInsensitive))
        return model;
    return brand + QLatin1Char(' ') + model;
}

QString focalLength(const PhotoInfo &info)
{
    if (info.focalLength <= 0)
        return {};
    QString s = trimmed(info.focalLength) + QStringLiteral(" mm");
    if (info.focalLength35 > 0 && info.focalLength35 != qRound(info.focalLength))
        s += QStringLiteral(" (%1 mm equivalent)").arg(info.focalLength35);
    return s;
}

QString aperture(double fNumber)
{
    return fNumber > 0 ? QStringLiteral("f/") + trimmed(fNumber) : QString();
}

QString exposureTime(double seconds)
{
    if (seconds <= 0)
        return {};
    // Cameras count fast speeds in fractions and slow ones in seconds, and
    // switch over around half a second: 1/2, then 0.6, 0.8, 1.
    if (seconds < 0.55)
        return QStringLiteral("1/%1 s").arg(qRound(1.0 / seconds));
    return trimmed(seconds) + QStringLiteral(" s");
}

QString exposureBias(double ev)
{
    if (std::abs(ev) < 0.05)
        return {};
    return (ev > 0 ? QStringLiteral("+") : QString()) + trimmed(ev) + QStringLiteral(" EV");
}

QString location(const PhotoInfo &info)
{
    if (!info.latitude || !info.longitude)
        return {};
    const QChar degree(0x00b0);
    return QStringLiteral("%1%2 %3, %4%5 %6")
        .arg(QString::number(std::abs(*info.latitude), 'f', 6)).arg(degree)
        .arg(*info.latitude < 0 ? QLatin1Char('S') : QLatin1Char('N'))
        .arg(QString::number(std::abs(*info.longitude), 'f', 6)).arg(degree)
        .arg(*info.longitude < 0 ? QLatin1Char('W') : QLatin1Char('E'));
}

QString mapUrl(const PhotoInfo &info)
{
    if (!info.latitude || !info.longitude)
        return {};
    const QString lat = QString::number(*info.latitude, 'f', 6);
    const QString lon = QString::number(*info.longitude, 'f', 6);
    return QStringLiteral("https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=16/%1/%2")
        .arg(lat, lon);
}

QList<QPair<QString, QString>> rows(const PhotoInfo &info)
{
    const QList<QPair<QString, QString>> all = {
        {QStringLiteral("Camera"), camera(info)},
        {QStringLiteral("Lens"), info.lens},
        {QStringLiteral("Focal length"), focalLength(info)},
        {QStringLiteral("Aperture"), aperture(info.aperture)},
        {QStringLiteral("Shutter"), exposureTime(info.exposureTime)},
        {QStringLiteral("ISO"), info.iso > 0 ? QString::number(info.iso) : QString()},
        {QStringLiteral("Exposure bias"), info.exposureBias ? exposureBias(*info.exposureBias)
                                                            : QString()},
        {QStringLiteral("Taken"), info.taken.isValid()
                                      ? info.taken.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                      : QString()},
        {QStringLiteral("Location"), location(info)},
        {QStringLiteral("Altitude"), info.altitude
                                         ? QStringLiteral("%1 m").arg(qRound(*info.altitude))
                                         : QString()},
    };
    QList<QPair<QString, QString>> shown;
    for (const auto &row : all) {
        if (!row.second.isEmpty())
            shown << row;
    }
    return shown;
}

} // namespace photoinfo
