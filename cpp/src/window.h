// Main window: playlist, navigation, shortcuts, status.

#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QSet>
#include <QStyle>

#include <functional>
#include <QStringList>
#include <QToolBar>

#include "decoders/decoder.h"
#include "loader.h"
#include "viewer.h"

inline constexpr int PrefetchRadius = 2;

/// Sorts IMG_2.jpg before IMG_10.jpg, the way a file manager would.
bool naturalLess(const QString &a, const QString &b);

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    /// Open a file (with its folder as the playlist), or a whole folder.
    void openPath(const QString &path);

    /// Dropped items: one path behaves as openPath, many become the playlist.
    void openPaths(const QStringList &paths);

    // Exposed for the functional tests, which drive the window directly.
    const QStringList &playlist() const { return m_playlist; }
    int index() const { return m_index; }
    bool showingPreview() const { return m_showingPreview; }
    ImageView *view() const { return m_view; }
    QString statusText() const { return m_statusRight->text(); }

public slots:
    void openDialog();
    void openFolderDialog();
    void step(int delta);
    void showIndex(int index);
    void toggleFullscreen();
    void closeOrLeaveFullscreen();
    void deleteCurrent();
    void copyCurrent();
    void revealCurrent();
    void openLogFolder();
    void showAbout();

private slots:
    void onReady(const QString &path, const decoders::DecodedImage &image, quint64 generation);
    void onFailed(const QString &path, const QString &message, quint64 generation);
    void updateStatus();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QAction *makeAction(const QString &text, const QList<QKeySequence> &shortcuts,
                        std::function<void()> slot, QStyle::StandardPixmap icon,
                        bool hasIcon, const QString &tip = {});
    void buildStatusBar();
    void buildActions();
    void buildMenus();
    void buildToolBar();

    QStringList scan(const QString &folder) const;
    void prefetchNeighbours();
    void display(const decoders::DecodedImage &image, bool keepView);
    void showPlaceholder();
    void updateActionState();
    QString currentPath() const;
    void warn(const QString &message);

    ImageView *m_view;
    ImageLoader *m_loader;
    QLabel *m_statusLeft;
    QLabel *m_statusRight;
    QToolBar *m_toolBar = nullptr;

    QStringList m_playlist;
    QSet<QString> m_extensions;
    int m_index = -1;
    quint64 m_generation = 0;
    bool m_showingPreview = false;
    QMap<QString, QString> m_currentMeta;

    QAction *m_actOpen = nullptr;
    QAction *m_actOpenFolder = nullptr;
    QAction *m_actReveal = nullptr;
    QAction *m_actCopy = nullptr;
    QAction *m_actDelete = nullptr;
    QAction *m_actQuit = nullptr;
    QAction *m_actPrev = nullptr;
    QAction *m_actNext = nullptr;
    QAction *m_actFirst = nullptr;
    QAction *m_actLast = nullptr;
    QAction *m_actZoomIn = nullptr;
    QAction *m_actZoomOut = nullptr;
    QAction *m_actFit = nullptr;
    QAction *m_actActual = nullptr;
    QAction *m_actFullscreen = nullptr;
    QAction *m_actLog = nullptr;
    QAction *m_actAbout = nullptr;
};
