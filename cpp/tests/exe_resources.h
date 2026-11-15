#pragma once

// What Windows itself reads out of a built .exe: the icon Explorer and the
// shortcuts show, and the version block in Properties > Details.
//
// Kept to its own file so <windows.h> and its macros stay out of the test
// sources that include this.

#include <QList>
#include <QString>

struct ExeResources {
    QString error;                  ///< Empty when the file could be read.
    int iconGroups = 0;             ///< RT_GROUP_ICON resources in the file.
    QList<int> iconSizes;           ///< Widths in the first group, 256 for 0.
    QString fixedVersion;           ///< "major.minor.patch" from VS_FIXEDFILEINFO.
    QString fileVersion;            ///< The FileVersion string.
    QString fileDescription;        ///< What Task Manager names the process.
    QString legalCopyright;         ///< The LegalCopyright string.
};

ExeResources readExeResources(const QString &exePath);

/// The frame widths an .ico file holds, 256 for 0, in file order.
QList<int> iconFileSizes(const QString &icoPath);
