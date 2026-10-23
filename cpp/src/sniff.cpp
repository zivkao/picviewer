#include "sniff.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <cstring>

namespace sniff {
namespace {

// ISO-BMFF brands, read from bytes 8..12. HEIC and AVIF share the container.
const QHash<QByteArray, Container> &ftypBrands()
{
    static const QHash<QByteArray, Container> brands = {
        {"heic", Container::Heif}, {"heix", Container::Heif},
        {"heim", Container::Heif}, {"heis", Container::Heif},
        {"hevc", Container::Heif}, {"hevx", Container::Heif},
        {"mif1", Container::Heif}, {"msf1", Container::Heif},
        {"avif", Container::Avif}, {"avis", Container::Avif},
        {"crx ", Container::Cr3},
        {"jp2 ", Container::Jp2},  {"jpx ", Container::Jp2},
    };
    return brands;
}

// Extensions we accept when the header tells us nothing useful.
const QHash<QString, Container> &extensionFallback()
{
    static const QHash<QString, Container> map = {
        {"jpg", Container::Jpeg}, {"jpeg", Container::Jpeg},
        {"jpe", Container::Jpeg}, {"jfif", Container::Jpeg},
        {"png", Container::Png},  {"apng", Container::Png},
        {"gif", Container::Gif},
        {"bmp", Container::Bmp},  {"dib", Container::Bmp},
        {"tif", Container::Tiff}, {"tiff", Container::Tiff},
        {"webp", Container::Webp},
        {"heic", Container::Heif}, {"heif", Container::Heif},
        {"hif", Container::Heif},
        {"avif", Container::Avif},
        {"psd", Container::Psd},
        {"ico", Container::Ico},
        {"jp2", Container::Jp2},  {"j2k", Container::Jp2},
        {"jpf", Container::Jp2},  {"jpx", Container::Jp2},
        {"ppm", Container::Pnm},  {"pgm", Container::Pnm},
        {"pbm", Container::Pnm},  {"pnm", Container::Pnm},
        {"pfm", Container::Pnm},
        {"cr3", Container::Cr3},
        {"raf", Container::Raf},
        {"rw2", Container::Rw2},
        {"orf", Container::Orf},
        {"x3f", Container::X3f},
    };
    return map;
}

bool startsWith(const QByteArray &header, const char *magic, int length)
{
    return header.size() >= length && std::memcmp(header.constData(), magic, length) == 0;
}

Container fromHeader(const QByteArray &h)
{
    if (startsWith(h, "\xff\xd8\xff", 3))          return Container::Jpeg;
    if (startsWith(h, "\x89PNG\r\n\x1a\n", 8))     return Container::Png;
    if (startsWith(h, "GIF87a", 6) || startsWith(h, "GIF89a", 6)) return Container::Gif;
    if (startsWith(h, "BM", 2))                    return Container::Bmp;
    if (startsWith(h, "8BPS", 4))                  return Container::Psd;
    if (startsWith(h, "\x00\x00\x01\x00", 4))      return Container::Ico;
    if (startsWith(h, "\x00\x00\x00\x0cjP  ", 8))  return Container::Jp2;
    if (startsWith(h, "FUJIFILMCCD-RAW", 15))      return Container::Raf;
    if (startsWith(h, "FOVb", 4))                  return Container::X3f;

    if (startsWith(h, "RIFF", 4) && h.size() >= 12 && h.mid(8, 4) == "WEBP")
        return Container::Webp;

    if (h.size() >= 12 && h.mid(4, 4) == "ftyp")
        return ftypBrands().value(h.mid(8, 4).toLower(), Container::Unknown);

    // Panasonic RW2 and Olympus ORF are TIFF-ish but use their own magic
    // numbers in place of the standard 42, so check them before plain TIFF.
    if (startsWith(h, "IIU\x00", 4))               return Container::Rw2;
    if (startsWith(h, "IIRO", 4) || startsWith(h, "IIRS", 4) || startsWith(h, "MMOR", 4))
        return Container::Orf;

    if (startsWith(h, "II*\x00", 4) || startsWith(h, "MM\x00*", 4)
        || startsWith(h, "II+\x00", 4) || startsWith(h, "MM\x00+", 4))
        return Container::Tiff;

    // PNM is "P" followed by a format digit; P7 is PAM, which Qt does not read.
    if (h.size() >= 2 && h[0] == 'P'
        && (QByteArray("123456f").contains(h[1])))
        return Container::Pnm;

    return Container::Unknown;
}

} // namespace

QString name(Container container)
{
    switch (container) {
    case Container::Jpeg:    return QStringLiteral("jpeg");
    case Container::Png:     return QStringLiteral("png");
    case Container::Gif:     return QStringLiteral("gif");
    case Container::Bmp:     return QStringLiteral("bmp");
    case Container::Tiff:    return QStringLiteral("tiff");
    case Container::Webp:    return QStringLiteral("webp");
    case Container::Heif:    return QStringLiteral("heif");
    case Container::Avif:    return QStringLiteral("avif");
    case Container::Psd:     return QStringLiteral("psd");
    case Container::Ico:     return QStringLiteral("ico");
    case Container::Jp2:     return QStringLiteral("jp2");
    case Container::Pnm:     return QStringLiteral("pnm");
    case Container::Cr3:     return QStringLiteral("cr3");
    case Container::Raf:     return QStringLiteral("raf");
    case Container::Rw2:     return QStringLiteral("rw2");
    case Container::Orf:     return QStringLiteral("orf");
    case Container::X3f:     return QStringLiteral("x3f");
    case Container::Unknown: break;
    }
    return QStringLiteral("unknown");
}

QByteArray readHeader(const QString &path, int size)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(size);
}

Container of(const QString &path)
{
    return of(path, readHeader(path));
}

Container of(const QString &path, const QByteArray &header)
{
    if (!header.isEmpty()) {
        const Container found = fromHeader(header);
        if (found != Container::Unknown)
            return found;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    return extensionFallback().value(suffix, Container::Unknown);
}

} // namespace sniff
