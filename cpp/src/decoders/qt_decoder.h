// Qt's own image plugins: the workhorse for mainstream formats.
//
// Stands in for the Python port's Pillow backend. QImageReader covers jpeg,
// png, bmp, gif, tiff, webp, ico, tga and pnm, applies EXIF orientation while
// decoding, and can decode a JPEG straight to a reduced size -- the same win
// Pillow's draft() gave, for the same reason: libjpeg scales during the
// inverse DCT rather than afterwards.

#pragma once

#include "decoder.h"

#include <QSet>

namespace decoders {

class QtDecoder : public Decoder {
public:
    QString name() const override { return QStringLiteral("qt"); }
    int score(const QString &path, sniff::Container container) const override;
    std::optional<DecodedImage> loadFull(const QString &path,
                                         sniff::Container container,
                                         QSize maxSize,
                                         QString *error) const override;

    static const QSet<sniff::Container> &containers();
    static const QSet<QString> &extensions();
};

} // namespace decoders
