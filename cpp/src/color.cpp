#include "color.h"

#include <QColorSpace>
#include <QImageReader>

namespace color {

void toSRgb(QImage &image)
{
    const QColorSpace source = image.colorSpace();
    if (!source.isValid())
        return;     // untagged: nothing reliable to convert from

    // Comparing against QColorSpace::SRgb catches the many spellings of sRGB
    // in one go. The Python port had to match profile *descriptions*, and
    // matching only the exact name "sRGB" missed the common
    // "sRGB IEC61966-2.1", so every sRGB JPEG paid for a transform that
    // changed nothing.
    if (source == QColorSpace(QColorSpace::SRgb))
        return;

    image.convertToColorSpace(QColorSpace::SRgb);
}

QSize orientedSize(const QImageReader &reader)
{
    const QSize stored = reader.size();
    if (!stored.isValid())
        return stored;

    // transformation() reports what autoTransform will apply. A 90 or 270
    // degree rotation swaps the axes; mirroring alone does not.
    const QImageIOHandler::Transformations transform = reader.transformation();
    if (transform & QImageIOHandler::TransformationRotate90)
        return stored.transposed();
    return stored;
}

} // namespace color
