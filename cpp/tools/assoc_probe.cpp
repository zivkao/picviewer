// What Explorer's "Open with" actually offers, asked of the shell itself.
//
//   assoc_probe.exe <path to picviewer.exe> .jpg .png ...
//
// One line per extension:
//
//   .jpg recommended=1 all=1 name=Pic Viewer
//
// recommended counts the entries for that executable in the list "Open with"
// shows first; all counts them in the full list behind "Choose another app".
// tools/verify_installer.ps1 wants 1 and 1 after an install -- offered up
// front, and only once -- and 0 and 0 after an uninstall.
//
// Registry keys alone cannot answer this. The first installer wrote the
// "Open with" key Microsoft documents for an application and the checks on
// it passed, yet the shell listed the viewer only in the full list, beside
// Notepad and Word, and Settings > Default apps did not list it at all.

#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>

#include <cstdio>
#include <string>

namespace {

std::string utf8(const wchar_t *text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size - 1 : 0, '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

/// Entries in one list that run the given executable, and the name the last
/// of them showed.
int count(const wchar_t *ext, ASSOC_FILTER filter, const wchar_t *exe, std::string *name)
{
    IEnumAssocHandlers *list = nullptr;
    if (FAILED(SHAssocEnumHandlers(ext, filter, &list)))
        return -1;
    int found = 0;
    IAssocHandler *handler = nullptr;
    ULONG fetched = 0;
    while (list->Next(1, &handler, &fetched) == S_OK && fetched == 1) {
        LPWSTR path = nullptr;
        if (SUCCEEDED(handler->GetName(&path)) && path && lstrcmpiW(path, exe) == 0) {
            ++found;
            LPWSTR shown = nullptr;
            if (SUCCEEDED(handler->GetUIName(&shown)) && shown) {
                *name = utf8(shown);
                CoTaskMemFree(shown);
            }
        }
        CoTaskMemFree(path);
        handler->Release();
    }
    list->Release();
    return found;
}

} // namespace

int main()
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) {
        std::fprintf(stderr, "usage: assoc_probe <path to exe> .ext [.ext ...]\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (int i = 2; i < argc; ++i) {
        std::string name;
        const int recommended = count(argv[i], ASSOC_FILTER_RECOMMENDED, argv[1], &name);
        const int all = count(argv[i], ASSOC_FILTER_NONE, argv[1], &name);
        std::printf("%s recommended=%d all=%d name=%s\n",
                    utf8(argv[i]).c_str(), recommended, all, name.c_str());
    }
    CoUninitialize();
    LocalFree(argv);
    return 0;
}
