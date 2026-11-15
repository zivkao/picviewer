#include "instance.h"

#include <QLoggingCategory>
#include <QThread>

#ifdef Q_OS_WIN
// libstdc++ already defines NOMINMAX on MinGW.
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

Q_LOGGING_CATEGORY(logInstance, "picviewer.instance")

namespace {

// Marks a WM_COPYDATA as ours: "PVW1".
constexpr unsigned long Magic = 0x50565731;

#ifdef Q_OS_WIN
std::wstring mutexName(const QString &key)
{
    // Local\: one per signed-in session, so two users never share a window.
    return (QStringLiteral("Local\\PicViewer.Instance.") + key).toStdWString();
}

std::wstring className(const QString &key)
{
    return (QStringLiteral("PicViewer.Instance.") + key).toStdWString();
}

LRESULT CALLBACK instanceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_COPYDATA) {
        auto *self = reinterpret_cast<SingleInstance *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        const auto *data = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
        if (self && data && data->dwData == Magic) {
            const QString text = data->cbData
                ? QString::fromWCharArray(static_cast<const wchar_t *>(data->lpData),
                                          int(data->cbData / sizeof(wchar_t)))
                : QString();
            const QStringList paths = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            // Queued: the sender is blocked in SendMessage until this returns,
            // and opening files is no reason to keep it waiting.
            QMetaObject::invokeMethod(self, [self, paths] { emit self->received(paths); },
                                      Qt::QueuedConnection);
            return TRUE;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
#endif

} // namespace

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
#ifdef Q_OS_WIN
    const std::wstring name = mutexName(key);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    // The mutex goes when its process does, a crashed one included, so a
    // first launch that died never leaves later ones handing files to nobody.
    m_primary = !(mutex && GetLastError() == ERROR_ALREADY_EXISTS);
    if (m_primary) {
        m_mutex = mutex;
    } else if (mutex) {
        CloseHandle(mutex);
    }
#endif
}

SingleInstance::~SingleInstance()
{
    stop();
}

QString SingleInstance::defaultKey()
{
    const QString key = qEnvironmentVariable("PICVIEWER_INSTANCE_KEY");
    return key.isEmpty() ? QStringLiteral("PicViewer") : key;
}

bool SingleInstance::forward(const QStringList &paths, int timeoutMs) const
{
#ifdef Q_OS_WIN
    const std::wstring cls = className(m_key);
    QElapsedTimer waited;
    waited.start();
    HWND target = nullptr;
    // A message-only window, which FindWindow does not see.
    while (!(target = FindWindowExW(HWND_MESSAGE, nullptr, cls.c_str(), nullptr))) {
        if (waited.elapsed() >= timeoutMs)
            return false;
        QThread::msleep(50);
    }

    // This launch came from the user's double-click, so Windows lets it bring
    // a window forward; the first launch, in the background, may not. Pass
    // that on, or the first one's window only flashes on the taskbar.
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    AllowSetForegroundWindow(pid);

    std::wstring text = paths.join(QLatin1Char('\n')).toStdWString();
    COPYDATASTRUCT data{};
    data.dwData = Magic;
    data.cbData = DWORD(text.size() * sizeof(wchar_t));
    data.lpData = text.empty() ? nullptr : text.data();
    DWORD_PTR result = 0;
    const LRESULT sent = SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                             SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                             UINT(qMax(1, timeoutMs)), &result);
    return sent != 0 && result == TRUE;
#else
    Q_UNUSED(paths)
    Q_UNUSED(timeoutMs)
    return false;
#endif
}

void SingleInstance::listen()
{
#ifdef Q_OS_WIN
    if (m_window || !m_primary)
        return;
    const std::wstring cls = className(m_key);
    HINSTANCE module = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = instanceProc;
    wc.hInstance = module;
    wc.lpszClassName = cls.c_str();
    // Already registered is fine: a test makes more than one of these.
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(0, cls.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                  nullptr, module, nullptr);
    if (!window) {
        qCWarning(logInstance) << "no message window; each launch opens its own, error"
                               << GetLastError();
        return;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_window = window;
#endif
}

void SingleInstance::stop()
{
#ifdef Q_OS_WIN
    if (m_window) {
        DestroyWindow(static_cast<HWND>(m_window));
        m_window = nullptr;
    }
    if (m_mutex) {
        CloseHandle(static_cast<HANDLE>(m_mutex));
        m_mutex = nullptr;
    }
#endif
}

// ---------------------------------------------------------------------------

OpenBatcher::OpenBatcher(const QStringList &startupPaths, int quietMs, int startupWindowMs,
                         QObject *parent)
    : QObject(parent)
    , m_startup(startupPaths)
    , m_startupWindowMs(startupWindowMs)
{
    m_age.start();
    m_quiet.setSingleShot(true);
    m_quiet.setInterval(quietMs);
    connect(&m_quiet, &QTimer::timeout, this, &OpenBatcher::flush);
}

void OpenBatcher::add(const QStringList &paths)
{
    m_pending += paths;
    m_quiet.start();
}

void OpenBatcher::flush()
{
    QStringList batch = m_pending;
    m_pending.clear();
    if (!batch.isEmpty() && !m_startupUsed && m_age.elapsed() < m_startupWindowMs)
        batch = m_startup + batch;
    if (!batch.isEmpty())
        m_startupUsed = true;
    batch.removeDuplicates();
    emit ready(batch);
}
