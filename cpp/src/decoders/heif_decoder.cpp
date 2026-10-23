#include "heif_decoder.h"

#include <QColorSpace>
#include <QFileInfo>

#include <libheif/heif.h>

#include <cstring>
#include <memory>

#include "color.h"

namespace decoders {
namespace {

// libheif hands out raw pointers with matching release functions; wrapping
// them means an early return cannot leak a context or a decoded plane.
struct ContextDeleter {
    void operator()(heif_context *c) const { if (c) heif_context_free(c); }
};
struct HandleDeleter {
    void operator()(heif_image_handle *h) const { if (h) heif_image_handle_release(h); }
};
struct ImageDeleter {
    void operator()(heif_image *i) const { if (i) heif_image_release(i); }
};

using Context = std::unique_ptr<heif_context, ContextDeleter>;
using Handle = std::unique_ptr<heif_image_handle, HandleDeleter>;
using Image = std::unique_ptr<heif_image, ImageDeleter>;

/// Attach the embedded ICC profile so the sRGB conversion has something to
/// work from. Without this a Display P3 photo from a phone paints its
/// wide-gamut numbers as sRGB and comes out visibly wrong.
void applyColorProfile(QImage &image, const heif_image_handle *handle)
{
    const std::size_t size = heif_image_handle_get_raw_color_profile_size(
        const_cast<heif_image_handle *>(handle));
    if (size == 0)
        return;

    QByteArray profile(static_cast<int>(size), Qt::Uninitialized);
    const heif_error error = heif_image_handle_get_raw_color_profile(
        const_cast<heif_image_handle *>(handle), profile.data());
    if (error.code != heif_error_Ok)
        return;

    const QColorSpace space = QColorSpace::fromIccProfile(profile);
    if (space.isValid())
        image.setColorSpace(space);
}

} // namespace

const QSet<QString> &HeifDecoder::extensions()
{
    static const QSet<QString> set = {"heic", "heif", "hif", "avif", "avifs"};
    return set;
}

int HeifDecoder::score(const QString &path, sniff::Container container) const
{
    if (container == sniff::Container::Heif || container == sniff::Container::Avif)
        return ScoreExact;
    if (extensions().contains(QFileInfo(path).suffix().toLower()))
        return ScoreExtension;
    return 0;
}

std::optional<DecodedImage> HeifDecoder::loadFull(const QString &path,
                                                  sniff::Container container,
                                                  QSize maxSize,
                                                  QString *error) const
{
    const Context ctx(heif_context_alloc());
    if (!ctx) {
        if (error)
            *error = QStringLiteral("could not allocate a libheif context");
        return std::nullopt;
    }

    heif_error status = heif_context_read_from_file(
        ctx.get(), path.toLocal8Bit().constData(), nullptr);
    if (status.code != heif_error_Ok) {
        if (error)
            *error = QString::fromUtf8(status.message);
        return std::nullopt;
    }

    heif_image_handle *rawHandle = nullptr;
    status = heif_context_get_primary_image_handle(ctx.get(), &rawHandle);
    if (status.code != heif_error_Ok) {
        if (error)
            *error = QString::fromUtf8(status.message);
        return std::nullopt;
    }
    const Handle handle(rawHandle);

    const bool hasAlpha = heif_image_handle_has_alpha_channel(handle.get());
    const heif_chroma chroma = hasAlpha ? heif_chroma_interleaved_RGBA
                                        : heif_chroma_interleaved_RGB;

    heif_image *rawImage = nullptr;
    // Transformations (rotation, mirroring) are applied by default, which is
    // what we want: phone photos carry them and must not come out sideways.
    status = heif_decode_image(handle.get(), &rawImage, heif_colorspace_RGB, chroma, nullptr);
    if (status.code != heif_error_Ok) {
        if (error)
            *error = QString::fromUtf8(status.message);
        return std::nullopt;
    }
    const Image decoded(rawImage);

    const int width = heif_image_get_width(decoded.get(), heif_channel_interleaved);
    const int height = heif_image_get_height(decoded.get(), heif_channel_interleaved);
    int stride = 0;
    const uint8_t *plane = heif_image_get_plane_readonly(
        decoded.get(), heif_channel_interleaved, &stride);
    if (!plane || width <= 0 || height <= 0) {
        if (error)
            *error = QStringLiteral("libheif returned an empty image plane");
        return std::nullopt;
    }

    // The plane belongs to libheif and dies with `decoded`, so copy rather
    // than wrapping it: QImage's borrowing constructor would leave the viewer
    // holding freed memory the moment this function returns.
    const QImage::Format format = hasAlpha ? QImage::Format_RGBA8888
                                           : QImage::Format_RGB888;
    QImage image(width, height, format);
    const int bytesPerLine = width * (hasAlpha ? 4 : 3);
    for (int y = 0; y < height; ++y)
        std::memcpy(image.scanLine(y), plane + static_cast<qsizetype>(y) * stride, bytesPerLine);

    applyColorProfile(image, handle.get());
    color::toSRgb(image);

    const QSize full(width, height);
    if (maxSize.isValid() && (width > maxSize.width() || height > maxSize.height()))
        image = image.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    DecodedImage result;
    result.image = std::move(image);
    result.isPreview = false;
    result.fullSize = full;
    result.metadata.insert(QStringLiteral("format"),
                           container == sniff::Container::Avif ? QStringLiteral("AVIF")
                                                               : QStringLiteral("HEIF"));
    result.metadata.insert(QStringLiteral("size"),
                           QStringLiteral("%1 x %2").arg(full.width()).arg(full.height()));
    if (hasAlpha)
        result.metadata.insert(QStringLiteral("alpha"), QStringLiteral("yes"));
    return result;
}

} // namespace decoders
