// Colour handling.
//
// Skipping this is what makes a home-grown viewer look wrong: phone photos
// come out sideways, and Adobe RGB or Display P3 files come out desaturated
// because their wide-gamut numbers get painted as sRGB.
//
// Much thinner than the Python version, because Qt does both jobs natively:
// QImageReader::setAutoTransform applies the EXIF orientation while decoding,
// and QColorSpace carries the embedded ICC profile so the conversion is one
// call. The Python port needed Pillow's ImageOps and ImageCms by hand.

#pragma once

#include <QImage>
#include <QSize>

class QImageReader;

namespace color {

/// Convert to sRGB if the image carries a different profile. A no-op when the
/// image is untagged or already sRGB, so it is safe to call on everything.
void toSRgb(QImage &image);

/// Source dimensions as they appear once the EXIF rotation is applied.
/// QImageReader::size() reports the stored size, which for orientations 5-8
/// is transposed relative to what the viewer will show.
QSize orientedSize(const QImageReader &reader);

} // namespace color
