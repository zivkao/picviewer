#include "qt_decoder.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>

#include "color.h"

namespace decoders {

const QSet<sniff::Container> &QtDecoder::containers()
{
    using C = sniff::Container;
    static const QSet<C> set = {
        C::Jpeg, C::Png, C::Gif, C::Bmp, C::Tiff, C::Webp, C::Ico, C::Pnm,
    };
    return set;
}

const QSet<QString> &QtDecoder::extensions()
{
    static const QSet<QString> set = {
        "jpg", "jpeg", "jpe", "jfif", "png", "apng", "gif", "bmp", "dib",
        "tif", "tiff", "webp", "ico", "icns", "tga", "ppm", "pgm", "pbm",
        "pnm", "xbm", "xpm", "wbmp", "svg", "svgz",
    };
    return set;
}

int QtDecoder::score(const QString &path, sniff::Container container) const
{
    const QString ext = QFileInfo(path).suffix().toLower();

    if (containers().contains(container)) {
        // A container match alone is not enough to win. Every TIFF-based RAW
        // (NEF, ARW, CR2, DNG...) sniffs as plain TIFF, and Qt will happily
        // decode such a file -- returning the small embedded preview from
        // IFD0 instead of the actual photo, without ever failing. Requiring
        // the extension to agree keeps those on the RAW decoder, which ranks
        // itself higher for them.
        return extensions().contains(ext) ? ScoreExact : ScoreLikely;
    }
    if (extensions().contains(ext))
        return ScoreExtension;

    // Qt reads a long tail of formats we do not enumerate, so let it try last
    // rather than refusing outright.
    return container == sniff::Container::Unknown ? ScoreFallback : 0;
}

std::optional<DecodedImage> QtDecoder::loadFull(const QString &path,
                                                sniff::Container container,
                                                QSize maxSize,
                                                QString *error) const
{
    Q_UNUSED(container);

    QImageReader reader(path);
    reader.setAutoTransform(true);      // apply EXIF orientation while decoding

    // Qt refuses any image whose pixels would exceed 256 MB, which is a
    // reasonable default for a program that did not expect one and a wrong
    // one here: a 101-megapixel photograph needs 305 MB and is an ordinary
    // thing to open in a viewer. The real guard is the caller's maxSize,
    // derived from MaxDecodePixels, which this is raised to match rather
    // than disabled outright.
    reader.setAllocationLimit(AllocationLimitMb);

    // Read these before read(): QImageReader resets format() and imageCount()
    // once the stream has been consumed, so asking afterwards gives nothing.
    const QByteArray format = reader.format();
    const int frames = reader.imageCount();

    QSize full = color::orientedSize(reader);
    if (!full.isValid()) {
        if (error)
            *error = reader.errorString();
        return std::nullopt;
    }

    // An .ico holds several sizes in one file and Qt starts on the first
    // directory entry, which is usually the smallest -- a 16x16 icon where
    // the file also carries 256x256. A viewer should show the largest.
    if (format == "ico" && frames > 1) {
        int bestFrame = 0;
        qint64 bestPixels = 0;
        for (int frame = 0; frame < frames; ++frame) {
            if (!reader.jumpToImage(frame))
                break;
            const QSize size = reader.size();
            const qint64 pixels = qint64(size.width()) * size.height();
            if (pixels > bestPixels) {
                bestPixels = pixels;
                bestFrame = frame;
                full = size;
            }
        }
        reader.jumpToImage(bestFrame);
    }

    if (maxSize.isValid() && (full.width() > maxSize.width() || full.height() > maxSize.height())) {
        // setScaledSize does not preserve the aspect ratio, so compute the
        // target rather than handing it the bounding box. For JPEG this makes
        // the decoder scale during the inverse DCT, several times faster than
        // decoding full size and resampling afterwards.
        const QSize target = full.scaled(maxSize, Qt::KeepAspectRatio);
        reader.setScaledSize(target);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = reader.errorString();
        return std::nullopt;
    }

    DecodedImage result;
    result.metadata.insert(QStringLiteral("format"), QString::fromLatin1(format).toUpper());
    if (image.colorSpace().isValid())
        result.metadata.insert(QStringLiteral("icc"), QStringLiteral("embedded"));
    if (frames > 1)
        result.metadata.insert(QStringLiteral("frames"), QString::number(frames));

    color::toSRgb(image);

    result.image = std::move(image);
    result.isPreview = false;
    result.fullSize = full;
    result.metadata.insert(QStringLiteral("size"),
                           QStringLiteral("%1 x %2").arg(full.width()).arg(full.height()));
    return result;
}

} // namespace decoders
