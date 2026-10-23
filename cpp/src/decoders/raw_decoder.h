// RAW decoder built on LibRaw, with a fast embedded-preview path.
//
// Demosaicing a 24MP RAW costs 1-3 seconds, which would make every page turn
// feel broken. Cameras embed a full-size JPEG preview in the file, so we show
// that within milliseconds and let the real decode replace it when it lands.

#pragma once

#include "decoder.h"

#include <QSet>

namespace decoders {

class RawDecoder : public Decoder {
public:
    QString name() const override { return QStringLiteral("raw"); }
    int score(const QString &path, sniff::Container container) const override;

    std::optional<DecodedImage> loadPreview(const QString &path,
                                            sniff::Container container,
                                            QSize maxSize) const override;

    std::optional<DecodedImage> loadFull(const QString &path,
                                         sniff::Container container,
                                         QSize maxSize,
                                         QString *error) const override;

    static const QSet<QString> &extensions();

    /// Extensions that mean "this really is an ordinary TIFF, not a RAW".
    static const QSet<QString> &tiffExtensions();
};

} // namespace decoders
