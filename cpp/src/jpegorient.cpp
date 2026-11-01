#include "jpegorient.h"

#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QSaveFile>
#include <QtEndian>

#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>

#include "photoinfo.h"

namespace jpegorient {

namespace {

const QByteArray ExifHeader("Exif\0\0", 6);
constexpr int TagOrientation = 0x0112;
constexpr qint64 MaxFileBytes = 512LL * 1024 * 1024;

/// Where things are in a JPEG, from its segment headers.
struct Layout {
    bool valid = false;             ///< the headers reach the image data intact
    qint64 exifMarker = -1;         ///< the EXIF APP1 segment's FF E1, if any
    qint64 exifPayload = -1;        ///< its "Exif\0\0" header
    qint64 exifLength = 0;          ///< from there to the segment's end
    qint64 insertAt = 2;            ///< where a new APP1 goes: after SOI, or after JFIF's APP0
};

Layout layoutOf(const QByteArray &jpeg)
{
    Layout l;
    const auto *d = reinterpret_cast<const uchar *>(jpeg.constData());
    const qint64 n = jpeg.size();
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8)
        return l;
    qint64 at = 2;
    for (int segment = 0; segment < 64; ++segment) {
        if (at + 4 > n || d[at] != 0xff)
            return l;
        const uchar type = d[at + 1];
        if (type == 0xda) {         // start of scan: the headers are all behind us
            l.valid = true;
            return l;
        }
        const qint64 length = qFromBigEndian<quint16>(d + at + 2);
        if (length < 2 || at + 2 + length > n)
            return l;
        // JFIF requires its APP0 to come first, so a new APP1 goes after it.
        if (type == 0xe0 && at == 2)
            l.insertAt = at + 2 + length;
        if (type == 0xe1 && l.exifMarker < 0 && length - 2 >= ExifHeader.size()
            && std::memcmp(d + at + 4, ExifHeader.constData(), ExifHeader.size()) == 0) {
            l.exifMarker = at;
            l.exifPayload = at + 4;
            l.exifLength = length - 2;
        }
        at += 2 + length;
    }
    return l;
}

/// Where in *tiff* IFD0 keeps its orientation value, if it has the tag as a
/// single SHORT; and the byte order to write it in.
std::optional<qint64> orientationValueAt(const QByteArray &tiff, bool *big)
{
    const auto *d = reinterpret_cast<const uchar *>(tiff.constData());
    const qint64 n = tiff.size();
    if (n < 8)
        return {};
    if (tiff.startsWith("MM"))
        *big = true;
    else if (tiff.startsWith("II"))
        *big = false;
    else
        return {};
    const auto u16 = [&](qint64 at) {
        return *big ? qFromBigEndian<quint16>(d + at) : qFromLittleEndian<quint16>(d + at);
    };
    const auto u32 = [&](qint64 at) {
        return *big ? qFromBigEndian<quint32>(d + at) : qFromLittleEndian<quint32>(d + at);
    };
    const qint64 ifd0 = u32(4);
    if (ifd0 < 8 || ifd0 + 2 > n)
        return {};
    const int count = u16(ifd0);
    for (int i = 0; i < count && i < 1000; ++i) {
        const qint64 entry = ifd0 + 2 + 12 * qint64(i);
        if (entry + 12 > n)
            return {};
        if (u16(entry) == TagOrientation && u16(entry + 2) == 3 && u32(entry + 4) == 1)
            return entry + 8;
    }
    return {};
}

int readOrientationAt(const QByteArray &jpeg, const Layout &l)
{
    if (!l.valid)
        return 0;
    if (l.exifPayload < 0)
        return 1;
    const QByteArray tiff = jpeg.mid(l.exifPayload + ExifHeader.size(),
                                     l.exifLength - ExifHeader.size());
    bool big = false;
    const auto at = orientationValueAt(tiff, &big);
    if (!at)
        return 1;
    const auto *v = reinterpret_cast<const uchar *>(tiff.constData() + *at);
    const int value = big ? qFromBigEndian<quint16>(v) : qFromLittleEndian<quint16>(v);
    return value >= 1 && value <= 8 ? value : 1;
}

struct ExifDataDeleter {
    void operator()(ExifData *data) const { exif_data_unref(data); }
};

/// The EXIF block with an orientation tag added, through libexif. Kept from
/// "fixing" anything, dropping tags it does not know, or touching the maker
/// note, so the block comes back as it was plus the one tag.
QByteArray addedOrientation(const QByteArray &payload, int orientation)
{
    const std::unique_ptr<ExifData, ExifDataDeleter> exif(exif_data_new());
    if (!exif)
        return {};
    exif_data_unset_option(exif.get(), EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
    exif_data_unset_option(exif.get(), EXIF_DATA_OPTION_IGNORE_UNKNOWN_TAGS);
    exif_data_set_option(exif.get(), EXIF_DATA_OPTION_DONT_CHANGE_MAKER_NOTE);
    exif_data_load_data(exif.get(), reinterpret_cast<const unsigned char *>(payload.constData()),
                        static_cast<unsigned int>(payload.size()));

    ExifEntry *entry = exif_entry_new();
    if (!entry)
        return {};
    exif_content_add_entry(exif->ifd[EXIF_IFD_0], entry);
    exif_entry_initialize(entry, static_cast<ExifTag>(TagOrientation));
    exif_set_short(entry->data, exif_data_get_byte_order(exif.get()), ExifShort(orientation));
    exif_entry_unref(entry);

    unsigned char *saved = nullptr;
    unsigned int size = 0;
    exif_data_save_data(exif.get(), &saved, &size);
    QByteArray result;
    if (saved)
        result = QByteArray(reinterpret_cast<const char *>(saved), size);
    std::free(saved);
    return result;
}

/// The smallest EXIF block that says one thing: the orientation.
QByteArray minimalBlock(int orientation)
{
    QByteArray tiff("II*\0", 4);
    const auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); tiff.append(b, 2); };
    const auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); tiff.append(b, 4); };
    u32(8);                         // IFD0 right after the header
    u16(1);                         // one entry
    u16(TagOrientation);
    u16(3);                         // SHORT
    u32(1);
    u16(quint16(orientation));
    u16(0);                         // padding to four bytes
    u32(0);                         // no IFD1
    return ExifHeader + tiff;
}

QByteArray app1(const QByteArray &payload)
{
    QByteArray segment(4, '\0');
    segment[0] = char(0xff);
    segment[1] = char(0xe1);
    qToBigEndian(quint16(payload.size() + 2), segment.data() + 2);
    return segment + payload;
}

/// Does *jpeg* decode, the right way round for *orientation*?
bool decodes(const QByteArray &jpeg, int orientation)
{
    QByteArray copy = jpeg;
    QBuffer buffer(&copy);
    QImageReader raw(&buffer, "jpeg");
    const QSize stored = raw.size();
    buffer.seek(0);
    QImageReader reader(&buffer, "jpeg");
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull() || !stored.isValid())
        return false;
    const bool sideways = orientation >= 5;
    return image.size() == (sideways ? stored.transposed() : stored);
}

QByteArray exifOf(const QByteArray &jpeg)
{
    const Layout l = layoutOf(jpeg);
    return l.exifPayload < 0 ? QByteArray() : jpeg.mid(l.exifPayload, l.exifLength);
}

} // namespace

QTransform transformFor(int orientation)
{
    switch (orientation) {
    case 2: return QTransform(-1, 0, 0, 1, 0, 0);     // mirrored
    case 3: return QTransform(-1, 0, 0, -1, 0, 0);    // upside down
    case 4: return QTransform(1, 0, 0, -1, 0, 0);     // mirrored vertically
    case 5: return QTransform(0, 1, 1, 0, 0, 0);      // transposed
    case 6: return QTransform(0, 1, -1, 0, 0, 0);     // a quarter turn clockwise
    case 7: return QTransform(0, -1, -1, 0, 0, 0);    // transversed
    case 8: return QTransform(0, -1, 1, 0, 0, 0);     // a quarter turn counter-clockwise
    default: return QTransform();
    }
}

int orientationFor(const QTransform &t)
{
    for (int o = 1; o <= 8; ++o) {
        const QTransform c = transformFor(o);
        if (qFuzzyCompare(c.m11() + 2, t.m11() + 2) && qFuzzyCompare(c.m12() + 2, t.m12() + 2)
            && qFuzzyCompare(c.m21() + 2, t.m21() + 2) && qFuzzyCompare(c.m22() + 2, t.m22() + 2))
            return o;
    }
    return 0;
}

int readOrientation(const QByteArray &jpeg)
{
    return readOrientationAt(jpeg, layoutOf(jpeg));
}

int readOrientation(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxFileBytes)
        return 0;
    return readOrientation(file.readAll());
}

QByteArray withOrientation(const QByteArray &jpeg, int orientation, Method *method,
                           QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return QByteArray();
    };
    if (orientation < 1 || orientation > 8)
        return fail(QStringLiteral("orientation %1 is not one of the eight").arg(orientation));

    const Layout l = layoutOf(jpeg);
    if (!l.valid)
        return fail(QStringLiteral("the file's JPEG structure could not be followed"));

    QByteArray out;
    Method how = Method::Patched;
    if (l.exifPayload >= 0) {
        const qint64 tiffAt = l.exifPayload + ExifHeader.size();
        bool big = false;
        if (const auto at = orientationValueAt(jpeg.mid(tiffAt, l.exifLength - ExifHeader.size()), &big)) {
            out = jpeg;
            char value[2];
            big ? qToBigEndian(quint16(orientation), value)
                : qToLittleEndian(quint16(orientation), value);
            out[tiffAt + *at] = value[0];
            out[tiffAt + *at + 1] = value[1];
        } else {
            how = Method::Rewritten;
            const QByteArray payload = addedOrientation(jpeg.mid(l.exifPayload, l.exifLength),
                                                        orientation);
            if (!payload.startsWith(ExifHeader))
                return fail(QStringLiteral("libexif could not rewrite the EXIF block"));
            if (payload.size() + 2 > 0xffff)
                return fail(QStringLiteral("the rewritten EXIF block would not fit a JPEG segment"));
            out = jpeg.left(l.exifMarker) + app1(payload)
                  + jpeg.mid(l.exifPayload + l.exifLength);
        }
    } else {
        how = Method::Inserted;
        out = jpeg.left(l.insertAt) + app1(minimalBlock(orientation)) + jpeg.mid(l.insertAt);
    }

    // Check before anything is written: the orientation reads back, the file
    // still decodes the right way round, and nothing else the camera recorded
    // has changed.
    if (readOrientation(out) != orientation)
        return fail(QStringLiteral("the new orientation did not read back"));
    if (!decodes(out, orientation))
        return fail(QStringLiteral("the result would not decode"));
    if (photoinfo::rows(photoinfo::fromExif(exifOf(out)))
        != photoinfo::rows(photoinfo::fromExif(exifOf(jpeg))))
        return fail(QStringLiteral("the camera's record changed in the rewrite"));

    if (method)
        *method = how;
    return out;
}

bool write(const QString &path, int orientation, Method *method, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return fail(in.errorString());
    if (in.size() > MaxFileBytes)
        return fail(QStringLiteral("the file is too large"));
    const QByteArray before = in.readAll();
    in.close();

    Method how = Method::Patched;
    const QByteArray after = withOrientation(before, orientation, &how, error);
    if (after.isEmpty())
        return false;

    if (how == Method::Patched) {
        // Two bytes, written where they are: nothing else in the file -- its
        // creation time, its permissions, any alternate streams -- is touched.
        qint64 first = 0;
        while (first < before.size() && before[first] == after[first])
            ++first;
        if (first == before.size()) {
            if (method)
                *method = how;
            return true;    // it already said so
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadWrite) || !file.seek(first)
            || file.write(after.constData() + first, 2) != 2)
            return fail(file.errorString());
    } else {
        // A different length: written beside the original and swapped in, so
        // a failure part-way leaves the original whole.
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(after) != after.size() || !file.commit())
            return fail(file.errorString());
    }

    if (readOrientation(path) != orientation)
        return fail(QStringLiteral("the file did not read back with the new orientation"));
    if (method)
        *method = how;
    return true;
}

} // namespace jpegorient
