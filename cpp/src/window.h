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
#include "settings.h"
#include "viewer.h"

class InfoPanel;
class QDockWidget;

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
    InfoPanel *infoPanel() const { return m_info; }
    QDockWidget *infoDock() const { return m_infoDock; }
    ImageLoader *loader() const { return m_loader; }
    const Preferences &preferences() const { return m_prefs; }
    /// The neighbours prefetch would queue now, nearest first.
    QStringList prefetchTargets() const;

    /// Take new preferences: the cache budget and prefetch radius at once,
    /// the slideshow interval from its next step. Does not save them.
    void applyPreferences(const Preferences &preferences);

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
    void openSettings();

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
    void buildInfoPanel();
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
    QDockWidget *m_infoDock = nullptr;
    InfoPanel *m_info = nullptr;
    /// Whether the panel was open when fullscreen hid it, to put it back.
    bool m_infoBeforeFullscreen = false;

    QStringList m_playlist;
    QSet<QString> m_extensions;
    int m_index = -1;
    quint64 m_generation = 0;
    bool m_showingPreview = false;
    QMap<QString, QString> m_currentMeta;
    Preferences m_prefs;

    QAction *m_actOpen = nullptr;
    QAction *m_actOpenFolder = nullptr;
    QAction *m_actReveal = nullptr;
    QAction *m_actCopy = nullptr;
    QAction *m_actDelete = nullptr;
    QAction *m_actSettings = nullptr;
    QAction *m_actQuit = nullptr;
    QAction *m_actPrev = nullptr;
    QAction *m_actNext = nullptr;
    QAction *m_actFirst = nullptr;
    QAction *m_actLast = nullptr;
    QAction *m_actZoomIn = nullptr;
    QAction *m_actZoomOut = nullptr;
    QAction *m_actFit = nullptr;
    QAction *m_actActual = nullptr;
    QAction *m_actRotateRight = nullptr;
    QAction *m_actRotateLeft = nullptr;
    QAction *m_actFlipH = nullptr;
    QAction *m_actFlipV = nullptr;
    QAction *m_actInfo = nullptr;
    QAction *m_actFullscreen = nullptr;
    QAction *m_actLog = nullptr;
    QAction *m_actAbout = nullptr;
};
