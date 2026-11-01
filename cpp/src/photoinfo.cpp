#include "photoinfo.h"

#include <QFile>
#include <QtEndian>

#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>

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
           && !taken.isValid();
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
    return info;
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
    };
    QList<QPair<QString, QString>> shown;
    for (const auto &row : all) {
        if (!row.second.isEmpty())
            shown << row;
    }
    return shown;
}

} // namespace photoinfo
