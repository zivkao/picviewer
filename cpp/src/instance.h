#pragma once

// One window per session.
//
// Explorer starts the viewer afresh for every image double-clicked, so
// without this each one opened another window. The first launch now holds a
// named mutex and a hidden message window; a later one finds that window,
// hands it its files with WM_COPYDATA and exits, and the first opens them
// and comes to the front.
//
// Win32 rather than QLocalServer: nothing extra to package, and the
// handover happens before the later launch builds a QApplication at all.

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    /// key names the session-wide mutex and window class. The application
    /// passes defaultKey(); tests pass their own, so they never hand files to
    /// a viewer the user has open.
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);
    ~SingleInstance() override;

    /// "PicViewer", or PICVIEWER_INSTANCE_KEY when that is set.
    static QString defaultKey();

    /// True for the first launch in the session.
    bool isPrimary() const { return m_primary; }

    /// From a later launch: hand paths to the first -- none means just come
    /// to the front. Waits up to timeoutMs for the first to be ready, since
    /// several launches can start together. False if none answered.
    bool forward(const QStringList &paths, int timeoutMs = 5000) const;

    /// In the first launch: start taking paths from later ones.
    void listen();

    /// Stop taking them and give up being first, so a launch from now on
    /// opens its own window rather than handing its files to one closing.
    void stop();

signals:
    void received(const QStringList &paths);

private:
    QString m_key;
    bool m_primary = true;
    void *m_mutex = nullptr;   // HANDLE
    void *m_window = nullptr;  // HWND
};

/// Gathers paths that arrive close together into one open.
///
/// Selecting several images and pressing Enter starts one launch per image,
/// all at once: one becomes the first and opens its own file, the rest hand
/// theirs over within moments. Opened one by one, whichever arrived last
/// would win. Gathered, they open together as a playlist of the selection,
/// the first launch's own file included when the batch comes while it is
/// still new.
class OpenBatcher : public QObject
{
    Q_OBJECT

public:
    OpenBatcher(const QStringList &startupPaths, int quietMs, int startupWindowMs,
                QObject *parent = nullptr);

    void add(const QStringList &paths);

signals:
    /// Possibly empty: a launch with no file, which only asks to be shown.
    void ready(const QStringList &paths);

private:
    void flush();

    QStringList m_startup;
    QStringList m_pending;
    QTimer m_quiet;
    QElapsedTimer m_age;
    int m_startupWindowMs;
    bool m_startupUsed = false;
};
