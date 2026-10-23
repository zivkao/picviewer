// HEIC / HEIF / AVIF via libheif.
//
// The Python port got these through pillow-heif, which plugs into Pillow's
// opener table. Here libheif is used directly: Qt's own plugin set does not
// include HEIF, and going straight to the library means the ICC profile and
// the alpha channel are available rather than whatever a wrapper chose to
// pass along.

#pragma once

#include "decoder.h"

#include <QSet>

namespace decoders {

class HeifDecoder : public Decoder {
public:
    QString name() const override { return QStringLiteral("heif"); }
    int score(const QString &path, sniff::Container container) const override;
    std::optional<DecodedImage> loadFull(const QString &path,
                                         sniff::Container container,
                                         QSize maxSize,
                                         QString *error) const override;

    static const QSet<QString> &extensions();
};

} // namespace decoders
