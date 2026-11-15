#include "exe_resources.h"

#include <QFile>

// libstdc++ already defines NOMINMAX on MinGW.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

// GRPICONDIR in a module: a 6-byte header then 14-byte entries; the width is
// the first byte of each entry, 0 meaning 256. An .ico file has the same
// header with 16-byte entries.
constexpr int HeaderBytes = 6;
constexpr int GroupEntryBytes = 14;
constexpr int FileEntryBytes = 16;

QList<int> widths(const uchar *data, qsizetype size, int entryBytes)
{
    QList<int> out;
    if (size < HeaderBytes)
        return out;
    const int count = data[4] | (data[5] << 8);
    for (int i = 0; i < count; ++i) {
        const qsizetype at = HeaderBytes + qsizetype(i) * entryBytes;
        if (at >= size)
            break;
        out << (data[at] == 0 ? 256 : data[at]);
    }
    return out;
}

BOOL CALLBACK collectName(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param)
{
    auto *names = reinterpret_cast<QList<LPWSTR> *>(param);
    // An integer ID is passed as is; a string name would need copying, and
    // the .rc here gives the icon a number.
    if (IS_INTRESOURCE(name))
        names->append(name);
    else
        names->append(nullptr);
    return TRUE;
}

QString queryString(const QByteArray &block, const wchar_t *key)
{
    void *value = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(block.constData(), key, &value, &length) || length == 0)
        return {};
    // length counts UTF-16 units including the terminator.
    return QString::fromWCharArray(static_cast<const wchar_t *>(value), int(length) - 1);
}

} // namespace

ExeResources readExeResources(const QString &exePath)
{
    ExeResources out;
    const std::wstring path = exePath.toStdWString();

    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                    LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        out.error = QStringLiteral("could not load %1 as data (error %2)")
                        .arg(exePath).arg(GetLastError());
        return out;
    }

    QList<LPWSTR> names;
    EnumResourceNamesW(module, RT_GROUP_ICON, collectName, reinterpret_cast<LONG_PTR>(&names));
    out.iconGroups = int(names.size());
    if (!names.isEmpty() && names.first()) {
        if (HRSRC found = FindResourceW(module, names.first(), RT_GROUP_ICON)) {
            if (HGLOBAL loaded = LoadResource(module, found)) {
                const auto *data = static_cast<const uchar *>(LockResource(loaded));
                out.iconSizes = widths(data, SizeofResource(module, found), GroupEntryBytes);
            }
        }
    }
    FreeLibrary(module);

    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0)
        return out;
    QByteArray block(qsizetype(size), '\0');
    if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data()))
        return out;

    void *fixed = nullptr;
    UINT fixedLength = 0;
    if (VerQueryValueW(block.constData(), L"\\", &fixed, &fixedLength)
        && fixedLength >= sizeof(VS_FIXEDFILEINFO)) {
        const auto *info = static_cast<const VS_FIXEDFILEINFO *>(fixed);
        out.fixedVersion = QStringLiteral("%1.%2.%3")
                               .arg(HIWORD(info->dwFileVersionMS))
                               .arg(LOWORD(info->dwFileVersionMS))
                               .arg(HIWORD(info->dwFileVersionLS));
    }
    out.fileVersion = queryString(block, L"\\StringFileInfo\\040904B0\\FileVersion");
    out.fileDescription = queryString(block, L"\\StringFileInfo\\040904B0\\FileDescription");
    return out;
}

QList<int> iconFileSizes(const QString &icoPath)
{
    QFile file(icoPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    return widths(reinterpret_cast<const uchar *>(bytes.constData()), bytes.size(), FileEntryBytes);
}
