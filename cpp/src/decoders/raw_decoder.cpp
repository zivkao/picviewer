#include "raw_decoder.h"

#include <QFileInfo>

#include <libraw/libraw.h>

#include <cstring>
#include <memory>

namespace decoders {
namespace {

// LibRaw hands back a malloc'd struct with its own free function.
struct ProcessedImageDeleter {
    void operator()(libraw_processed_image_t *image) const
    {
        if (image)
            LibRaw::dcraw_clear_mem(image);
    }
};
using ProcessedImage = std::unique_ptr<libraw_processed_image_t, ProcessedImageDeleter>;

/// Copy LibRaw's RGB buffer into a QImage that owns its pixels.
QImage toQImage(const libraw_processed_image_t &source)
{
    if (source.type != LIBRAW_IMAGE_BITMAP || source.colors != 3 || source.bits != 8)
        return {};

    QImage image(source.width, source.height, QImage::Format_RGB888);
    const int stride = source.width * 3;
    for (int y = 0; y < source.height; ++y)
        std::memcpy(image.scanLine(y), source.data + static_cast<qsizetype>(y) * stride, stride);
    return image;
}

} // namespace

const QSet<QString> &RawDecoder::extensions()
{
    // LibRaw covers far more than this, but these are the ones worth claiming
    // with high confidence. Anything else TIFF-shaped still gets a shot below.
    static const QSet<QString> set = {
        "cr2", "cr3", "crw",            // Canon
        "nef", "nrw",                   // Nikon
        "arw", "srf", "sr2",            // Sony
        "orf",                          // Olympus
        "rw2",                          // Panasonic
        "raf",                          // Fujifilm
        "pef", "dng",                   // Pentax / Adobe
        "srw",                          // Samsung
        "x3f",                          // Sigma
        "erf", "kdc", "dcr",            // Epson / Kodak
        "mrw",                          // Minolta
        "rwl",                          // Leica
        "iiq",                          // Phase One
        "3fr", "fff",                   // Hasselblad
        "mef", "mos",                   // Mamiya / Leaf
        "ari", "raw", "rwz",
    };
    return set;
}

const QSet<QString> &RawDecoder::tiffExtensions()
{
    static const QSet<QString> set = {"tif", "tiff"};
    return set;
}

int RawDecoder::score(const QString &path, sniff::Container container) const
{
    using C = sniff::Container;
    const QString ext = QFileInfo(path).suffix().toLower();

    static const QSet<C> ownContainers = {C::Cr3, C::Raf, C::Rw2, C::Orf, C::X3f};
    if (ownContainers.contains(container) || extensions().contains(ext))
        return ScoreExact;

    // NEF, ARW, DNG, CR2 and friends are all TIFF containers, so magic bytes
    // alone cannot separate them from a photo saved as TIFF. A TIFF container
    // not named .tif is overwhelmingly a RAW file, so claim it; a genuine
    // .tif goes to Qt first and falls through to here only if that fails.
    if (container == C::Tiff)
        return tiffExtensions().contains(ext) ? ScoreLikely : ScoreExact;

    return 0;
}

std::optional<DecodedImage> RawDecoder::loadPreview(const QString &path,
                                                    sniff::Container container,
                                                    QSize maxSize) const
{
    Q_UNUSED(container);

    LibRaw raw;
    if (raw.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
        return std::nullopt;

    const QSize full(raw.imgdata.sizes.width, raw.imgdata.sizes.height);

    if (raw.unpack_thumb() != LIBRAW_SUCCESS)
        return std::nullopt;

    int status = 0;
    const ProcessedImage thumb(raw.dcraw_make_mem_thumb(&status));
    if (!thumb || status != LIBRAW_SUCCESS)
        return std::nullopt;

    QImage image;
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        // The embedded preview is a JPEG with its own EXIF orientation, which
        // loadFromData does not apply, so the tag is read and used below.
        if (!image.loadFromData(thumb->data, static_cast<int>(thumb->data_size), "JPEG"))
            return std::nullopt;
    } else {
        image = toQImage(*thumb);
    }
    if (image.isNull())
        return std::nullopt;

    // LibRaw reports the camera's flip for the full image; the thumbnail is
    // stored the same way round, so the same correction applies to both.
    switch (raw.imgdata.sizes.flip) {
    case 3: image = image.transformed(QTransform().rotate(180)); break;
    case 5: image = image.transformed(QTransform().rotate(270)); break;
    case 6: image = image.transformed(QTransform().rotate(90)); break;
    default: break;
    }

    if (maxSize.isValid() && (image.width() > maxSize.width() || image.height() > maxSize.height()))
        image = image.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    DecodedImage result;
    result.image = std::move(image);
    result.isPreview = true;
    result.fullSize = full;
    result.metadata.insert(QStringLiteral("format"), QStringLiteral("RAW"));
    result.metadata.insert(QStringLiteral("size"),
                           QStringLiteral("%1 x %2").arg(full.width()).arg(full.height()));
    return result;
}

std::optional<DecodedImage> RawDecoder::loadFull(const QString &path,
                                                 sniff::Container container,
                                                 QSize maxSize,
                                                 QString *error) const
{
    Q_UNUSED(container);

    LibRaw raw;
    int status = raw.open_file(path.toLocal8Bit().constData());
    if (status != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(LibRaw::strerror(status));
        return std::nullopt;
    }

    const QSize full(raw.imgdata.sizes.width, raw.imgdata.sizes.height);

    // Half-size demosaicing is roughly 4x faster. Only take it when the result
    // still covers what we are asked to display.
    const bool half = maxSize.isValid()
                      && full.width() >= maxSize.width() * 2
                      && full.height() >= maxSize.height() * 2;

    raw.imgdata.params.use_camera_wb = 1;
    raw.imgdata.params.output_color = 1;    // sRGB
    raw.imgdata.params.output_bps = 8;
    raw.imgdata.params.half_size = half ? 1 : 0;
    raw.imgdata.params.no_auto_bright = 0;

    status = raw.unpack();
    if (status != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(LibRaw::strerror(status));
        return std::nullopt;
    }
    status = raw.dcraw_process();
    if (status != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(LibRaw::strerror(status));
        return std::nullopt;
    }

    const ProcessedImage processed(raw.dcraw_make_mem_image(&status));
    if (!processed || status != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(LibRaw::strerror(status));
        return std::nullopt;
    }

    QImage image = toQImage(*processed);
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("LibRaw returned an unexpected pixel layout");
        return std::nullopt;
    }

    if (maxSize.isValid() && (image.width() > maxSize.width() || image.height() > maxSize.height()))
        image = image.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    DecodedImage result;
    result.image = std::move(image);
    result.isPreview = false;
    result.fullSize = full;
    result.metadata.insert(QStringLiteral("format"), QStringLiteral("RAW"));
    result.metadata.insert(QStringLiteral("size"),
                           QStringLiteral("%1 x %2").arg(full.width()).arg(full.height()));
    result.metadata.insert(QStringLiteral("demosaic"),
                           half ? QStringLiteral("half-size") : QStringLiteral("full"));
    result.metadata.insert(QStringLiteral("colors"), QString::number(raw.imgdata.idata.colors));
    result.metadata.insert(QStringLiteral("pattern"),
                           QString::fromLatin1(raw.imgdata.idata.cdesc));
    return result;
}

} // namespace decoders
