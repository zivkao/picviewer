// Saving a turn back into a JPEG, losslessly, by its EXIF orientation tag.
//
// The pixels are never touched: re-encoding a JPEG to rotate it costs quality
// every time, and the orientation tag exists so that nothing has to be. Where
// the file already has the tag, its two bytes are overwritten in place and
// nothing else in the file changes. Where it has EXIF without the tag, the
// block is rewritten with the tag added; where it has no EXIF, a minimal block
// is inserted. Either way the new file is written beside the old one, checked
// -- it decodes, carries the orientation asked for, and the rest of the
// camera's record reads back unchanged -- and only then put in its place.

#pragma once

#include <QByteArray>
#include <QString>
#include <QTransform>

namespace jpegorient {

/// EXIF orientation 1-8 as the transform from the stored pixels to the
/// picture as meant to be seen; 1 is the identity.
QTransform transformFor(int orientation);

/// The orientation whose transform is *t*, or 0 if *t* is not one of the
/// eight quarter turns and mirrors.
int orientationFor(const QTransform &t);

/// The orientation a JPEG records: 1 when it records none, 0 if the file
/// cannot be read as a JPEG at all.
int readOrientation(const QByteArray &jpeg);
int readOrientation(const QString &path);

enum class Method {
    Patched,    ///< the tag was there; two bytes changed in place
    Rewritten,  ///< EXIF was there without the tag; the block was rewritten
    Inserted,   ///< no EXIF at all; a minimal block was added
};

/// Make *path* record *orientation*. Returns false, with the reason in
/// *error*, and leaves the file as it was if anything does not check out.
bool write(const QString &path, int orientation, Method *method = nullptr,
           QString *error = nullptr);

/// The same, on bytes in memory: the new file, or empty on failure.
QByteArray withOrientation(const QByteArray &jpeg, int orientation, Method *method = nullptr,
                           QString *error = nullptr);

} // namespace jpegorient
