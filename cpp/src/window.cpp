#include "window.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

#include "applog.h"
#include "decoders/registry.h"
#include "infopanel.h"

Q_LOGGING_CATEGORY(logWindow, "picviewer.window")

namespace {
const char *Version = "0.1.0";
}

bool naturalLess(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            // Compare whole runs of digits numerically, so IMG_2 sorts
            // before IMG_10 rather than after it.
            int startA = i;
            int startB = j;
            while (i < a.size() && a[i].isDigit())
                ++i;
            while (j < b.size() && b[j].isDigit())
                ++j;
            const QStringView runA = QStringView(a).mid(startA, i - startA);
            const QStringView runB = QStringView(b).mid(startB, j - startB);
            const qulonglong valueA = runA.toULongLong();
            const qulonglong valueB = runB.toULongLong();
            if (valueA != valueB)
                return valueA < valueB;
        } else {
            const QChar lowerA = a[i].toLower();
            const QChar lowerB = b[j].toLower();
            if (lowerA != lowerB)
                return lowerA < lowerB;
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_view(new ImageView(this))
    , m_loader(new ImageLoader(this))
{
    setWindowTitle(QStringLiteral("Pic Viewer"));
    resize(1200, 800);
    setAcceptDrops(true);
    setCentralWidget(m_view);

    m_extensions = decoders::Registry::instance().supportedExtensions();

    connect(m_loader, &ImageLoader::ready, this, &MainWindow::onReady);
    connect(m_loader, &ImageLoader::failed, this, &MainWindow::onFailed);
    connect(m_view, &ImageView::scaleChanged, this, &MainWindow::updateStatus);
    connect(m_view, &ImageView::filesDropped, this, &MainWindow::openPaths);

    buildStatusBar();
    buildActions();
    buildInfoPanel();
    buildMenus();
    buildToolBar();
    showPlaceholder();

    qCInfo(logWindow).noquote() << "window ready:" << m_extensions.size() << "extensions, decoders:"
                      << decoders::Registry::instance().decoderNames().join(QLatin1Char(' '));
}

// -- chrome ----------------------------------------------------------------

void MainWindow::buildInfoPanel()
{
    m_info = new InfoPanel(this);
    m_infoDock = new QDockWidget(tr("Info"), this);
    m_infoDock->setObjectName(QStringLiteral("info_dock"));
    m_infoDock->setWidget(m_info);
    m_infoDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_infoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_infoDock);
    // Closed until asked for: most of the time the picture is the point.
    m_infoDock->hide();

    // The dock's own toggle action, so the menu tick, the toolbar button and
    // the dock's close button can never disagree about whether it is open.
    m_actInfo = m_infoDock->toggleViewAction();
    m_actInfo->setText(tr("Image &Info"));
    m_actInfo->setShortcut(QKeySequence(QStringLiteral("I")));
    m_actInfo->setToolTip(tr("Camera and file details  (I)"));
    m_actInfo->setStatusTip(m_actInfo->toolTip());
    addAction(m_actInfo);   // so I works in fullscreen, with the menu bar hidden
}

void MainWindow::buildStatusBar()
{
    m_statusLeft = new QLabel(this);
    m_statusRight = new QLabel(this);
    statusBar()->addWidget(m_statusLeft, 1);
    statusBar()->addPermanentWidget(m_statusRight, 0);
}

QAction *MainWindow::makeAction(const QString &text, const QList<QKeySequence> &shortcuts,
                                std::function<void()> slot, QStyle::StandardPixmap icon,
                                bool hasIcon, const QString &tip)
{
    auto *action = new QAction(text, this);
    if (!shortcuts.isEmpty())
        action->setShortcuts(shortcuts);
    if (hasIcon)
        action->setIcon(style()->standardIcon(icon));

    QString hint = tip.isEmpty() ? QString(text).remove(QLatin1Char('&')) : tip;
    if (!shortcuts.isEmpty())
        hint += QStringLiteral("  (") + shortcuts.first().toString() + QLatin1Char(')');
    action->setToolTip(hint);
    action->setStatusTip(hint);

    connect(action, &QAction::triggered, this, std::move(slot));
    addAction(action);
    return action;
}

void MainWindow::buildActions()
{
    using SP = QStyle::StandardPixmap;
    const auto keys = [](std::initializer_list<const char *> list) {
        QList<QKeySequence> result;
        for (const char *key : list)
            result << QKeySequence(QString::fromLatin1(key));
        return result;
    };

    m_actOpen = makeAction(tr("&Open File..."), keys({"Ctrl+O"}),
                           [this] { openDialog(); }, SP::SP_FileIcon, true);
    m_actOpenFolder = makeAction(tr("Open &Folder..."), keys({"Ctrl+Shift+O"}),
                                 [this] { openFolderDialog(); }, SP::SP_DirOpenIcon, true);
    m_actReveal = makeAction(tr("Show in E&xplorer"), keys({"Ctrl+E"}),
                             [this] { revealCurrent(); }, SP::SP_FileIcon, false);
    m_actCopy = makeAction(tr("&Copy Image"), keys({"Ctrl+C"}),
                           [this] { copyCurrent(); }, SP::SP_FileIcon, false);
    m_actDelete = makeAction(tr("&Delete"), keys({"Delete"}),
                             [this] { deleteCurrent(); }, SP::SP_TrashIcon, true);
    m_actQuit = makeAction(tr("E&xit"), keys({"Ctrl+Q"}),
                           [this] { close(); }, SP::SP_FileIcon, false);

    m_actPrev = makeAction(tr("&Previous"), keys({"Left", "Up", "Backspace", "PgUp"}),
                           [this] { step(-1); }, SP::SP_ArrowLeft, true, tr("Previous image"));
    m_actNext = makeAction(tr("&Next"), keys({"Right", "Down", "Space", "PgDown"}),
                           [this] { step(1); }, SP::SP_ArrowRight, true, tr("Next image"));
    m_actFirst = makeAction(tr("&First"), keys({"Home"}),
                            [this] { showIndex(0); }, SP::SP_FileIcon, false);
    m_actLast = makeAction(tr("&Last"), keys({"End"}),
                           [this] { showIndex(m_playlist.size() - 1); }, SP::SP_FileIcon, false);

    m_actZoomIn = makeAction(tr("Zoom &In"), keys({"+", "="}),
                             [this] { m_view->zoomBy(ImageView::ZoomStep); }, SP::SP_FileIcon, false);
    m_actZoomOut = makeAction(tr("Zoom &Out"), keys({"-"}),
                              [this] { m_view->zoomBy(1.0 / ImageView::ZoomStep); }, SP::SP_FileIcon, false);
    m_actFit = makeAction(tr("&Fit to Window"), keys({"0"}),
                          [this] { m_view->fit(); }, SP::SP_FileIcon, false);
    m_actActual = makeAction(tr("&Actual Size"), keys({"1"}),
                             [this] { m_view->zoomToActual(); }, SP::SP_FileIcon, false);
    // Viewing only: the file is never rewritten, so a turn cannot damage a
    // photograph. It lasts until the next image.
    const auto orient = [this](void (ImageView::*turn)()) {
        return [this, turn] {
            (m_view->*turn)();
            const QString turned = describeOrientation(m_view->orientation());
            qCInfo(logWindow).noquote()
                << "orientation" << (turned.isEmpty() ? QStringLiteral("upright") : turned);
            updateStatus();
        };
    };
    m_actRotateRight = makeAction(tr("Rotate &Right"), keys({"R"}),
                                  orient(&ImageView::rotateClockwise), SP::SP_FileIcon, false,
                                  tr("Rotate clockwise"));
    m_actRotateLeft = makeAction(tr("Rotate &Left"), keys({"L"}),
                                 orient(&ImageView::rotateCounterClockwise), SP::SP_FileIcon, false,
                                 tr("Rotate counter-clockwise"));
    m_actFlipH = makeAction(tr("Flip &Horizontal"), keys({"H"}),
                            orient(&ImageView::flipHorizontally), SP::SP_FileIcon, false);
    m_actFlipV = makeAction(tr("Flip &Vertical"), keys({"V"}),
                            orient(&ImageView::flipVertically), SP::SP_FileIcon, false);

    m_actFullscreen = makeAction(tr("F&ullscreen"), keys({"F11"}),
                                 [this] { toggleFullscreen(); }, SP::SP_TitleBarMaxButton, true);

    m_actLog = makeAction(tr("Open &Log Folder"), {}, [this] { openLogFolder(); },
                          SP::SP_FileIcon, false);
    m_actAbout = makeAction(tr("&About"), {}, [this] { showAbout(); }, SP::SP_FileIcon, false);

    // Esc is not on the Exit action: it must leave fullscreen when in it, and
    // quitting on a stray Esc is a nasty surprise otherwise.
    auto *escape = new QAction(this);
    escape->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(escape, &QAction::triggered, this, &MainWindow::closeOrLeaveFullscreen);
    addAction(escape);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actOpen);
    file->addAction(m_actOpenFolder);
    file->addSeparator();
    file->addAction(m_actReveal);
    file->addAction(m_actCopy);
    file->addAction(m_actDelete);
    file->addSeparator();
    file->addAction(m_actQuit);

    QMenu *go = menuBar()->addMenu(tr("&Go"));
    go->addAction(m_actPrev);
    go->addAction(m_actNext);
    go->addSeparator();
    go->addAction(m_actFirst);
    go->addAction(m_actLast);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_actZoomIn);
    view->addAction(m_actZoomOut);
    view->addSeparator();
    view->addAction(m_actFit);
    view->addAction(m_actActual);
    view->addSeparator();
    view->addAction(m_actRotateRight);
    view->addAction(m_actRotateLeft);
    view->addAction(m_actFlipH);
    view->addAction(m_actFlipV);
    view->addSeparator();
    view->addAction(m_actInfo);
    view->addAction(m_actFullscreen);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_actLog);
    help->addAction(m_actAbout);
}

void MainWindow::buildToolBar()
{
    m_toolBar = new QToolBar(tr("Main"), this);
    m_toolBar->setObjectName(QStringLiteral("main_toolbar"));
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_toolBar->addAction(m_actOpen);
    m_toolBar->addAction(m_actOpenFolder);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actPrev);
    m_toolBar->addAction(m_actNext);
    m_toolBar->addSeparator();

    // These have no sensible standard icon, so they get short text instead
    // of the blank square an icon-only button would show.
    const auto addTextButtons = [this](const QList<QPair<QAction *, QString>> &buttons) {
        for (const auto &[action, label] : buttons) {
            auto *button = new QToolButton(m_toolBar);
            button->setDefaultAction(action);
            button->setText(label);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            m_toolBar->addWidget(button);
        }
    };
    addTextButtons({
        {m_actFit, QStringLiteral("Fit")},
        {m_actActual, QStringLiteral("1:1")},
        {m_actZoomOut, QStringLiteral("−")},
        {m_actZoomIn, QStringLiteral("+")},
    });
    m_toolBar->addSeparator();
    // Turns only: they are what a sideways phone photo needs. Flips stay in
    // the menu and on H / V, where they do not crowd the bar.
    addTextButtons({
        {m_actRotateLeft, QStringLiteral("↺")},
        {m_actRotateRight, QStringLiteral("↻")},
    });
    m_toolBar->addSeparator();
    addTextButtons({{m_actInfo, QStringLiteral("Info")}});

    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actFullscreen);
    m_toolBar->addAction(m_actDelete);
    addToolBar(m_toolBar);
}

// -- opening ---------------------------------------------------------------

void MainWindow::openDialog()
{
    QStringList patterns;
    QStringList sorted(m_extensions.cbegin(), m_extensions.cend());
    sorted.sort();
    for (const QString &ext : sorted)
        patterns << QStringLiteral("*.") + ext;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open image"), {},
        tr("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' '))));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::openFolderDialog()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open folder"));
    if (!folder.isEmpty())
        openPath(folder);
}

void MainWindow::openPath(const QString &path)
{
    qCInfo(logWindow).noquote() << "open" << path;
    const QFileInfo info(path);

    if (info.isDir()) {
        m_playlist = scan(path);
        showIndex(m_playlist.isEmpty() ? -1 : 0);
        return;
    }
    if (!info.exists()) {
        warn(tr("Not found: %1").arg(path));
        return;
    }

    m_playlist = scan(info.absolutePath());
    int index = m_playlist.indexOf(info.absoluteFilePath());
    if (index < 0) {
        // An extension we do not recognise, opened explicitly. Honour it.
        m_playlist.prepend(info.absoluteFilePath());
        index = 0;
    }
    showIndex(index);
}

void MainWindow::openPaths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    if (paths.size() == 1) {
        openPath(paths.first());
        return;
    }

    QStringList files;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir())
            files += scan(path);
        else if (info.isFile())
            files << info.absoluteFilePath();
    }
    if (files.isEmpty()) {
        warn(tr("Nothing to open in that selection"));
        return;
    }

    files.removeDuplicates();
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return naturalLess(QFileInfo(a).fileName(), QFileInfo(b).fileName());
    });
    m_playlist = files;
    showIndex(0);
}

QStringList MainWindow::scan(const QString &folder) const
{
    QStringList files;
    const QDir dir(folder);
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files, QDir::NoSort)) {
        if (m_extensions.contains(info.suffix().toLower()))
            files << info.absoluteFilePath();
    }
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return naturalLess(QFileInfo(a).fileName(), QFileInfo(b).fileName());
    });
    return files;
}

// -- navigation ------------------------------------------------------------

void MainWindow::step(int delta)
{
    if (m_playlist.isEmpty())
        return;
    const int count = m_playlist.size();
    showIndex(((m_index + delta) % count + count) % count);
}

void MainWindow::showIndex(int index)
{
    if (m_playlist.isEmpty() || index < 0 || index >= m_playlist.size()) {
        showPlaceholder();
        return;
    }

    m_index = index;
    ++m_generation;
    m_showingPreview = false;
    m_currentMeta.clear();

    const QString path = m_playlist.at(index);
    const QString name = QFileInfo(path).fileName();
    setWindowTitle(name + QStringLiteral(" - Pic Viewer"));
    m_statusLeft->setText(QStringLiteral("[%1/%2]  %3")
                              .arg(index + 1).arg(m_playlist.size()).arg(name));

    if (auto cached = m_loader->cache().get(CacheKey::of(path)); cached && !cached->isPreview) {
        display(*cached, false);
    } else {
        m_statusRight->setText(tr("Loading..."));
        m_loader->load(path, m_generation);
    }
    updateActionState();

    QTimer::singleShot(50, this, &MainWindow::prefetchNeighbours);
}

void MainWindow::prefetchNeighbours()
{
    if (m_playlist.isEmpty())
        return;
    const int count = m_playlist.size();
    QStringList targets;
    for (int offset = -PrefetchRadius; offset <= PrefetchRadius; ++offset) {
        if (offset == 0)
            continue;
        const QString &candidate = m_playlist.at(((m_index + offset) % count + count) % count);
        // With few files the wrap-around lands back on the current one, which
        // would re-queue what was just decoded.
        if (candidate != m_playlist.at(m_index) && !targets.contains(candidate))
            targets << candidate;
    }
    m_loader->prefetch(targets);
}

// -- results ---------------------------------------------------------------

void MainWindow::onReady(const QString &path, const decoders::DecodedImage &image,
                         quint64 generation)
{
    Q_UNUSED(path);
    if (generation != m_generation)
        return;     // the user moved on while this was decoding
    if (image.isPreview && !m_showingPreview && m_view->hasImage())
        return;     // never replace a full decode with a preview
    display(image, m_showingPreview);
}

void MainWindow::display(const decoders::DecodedImage &image, bool keepView)
{
    m_showingPreview = image.isPreview;
    m_currentMeta = image.metadata;
    m_view->setImage(image.image, keepView);
    m_info->showImage(currentPath(), image);
    updateStatus();
}

void MainWindow::onFailed(const QString &path, const QString &message, quint64 generation)
{
    if (generation != m_generation)
        return;
    qCDebug(logWindow) << "showing failure for" << QFileInfo(path).fileName();
    m_view->clearImage();
    m_info->showFailure(path);
    m_view->setMessage(tr("Could not open %1\n\n%2").arg(QFileInfo(path).fileName(), message));
    m_statusRight->setText(tr("Failed"));
    updateActionState();
}

void MainWindow::updateStatus()
{
    updateActionState();
    if (!m_view->hasImage()) {
        m_statusRight->clear();
        return;
    }

    QStringList parts;
    if (const QString size = m_currentMeta.value(QStringLiteral("size")); !size.isEmpty())
        parts << size;
    if (const QString format = m_currentMeta.value(QStringLiteral("format")); !format.isEmpty())
        parts << format;
    if (const QString decoder = m_currentMeta.value(QStringLiteral("decoder")); !decoder.isEmpty())
        parts << QStringLiteral("via ") + decoder;
    if (m_currentMeta.contains(QStringLiteral("icc")))
        parts << QStringLiteral("ICC");
    if (const QString frames = m_currentMeta.value(QStringLiteral("frames")); !frames.isEmpty())
        parts << frames + QStringLiteral(" frames");
    parts << QStringLiteral("%1%").arg(qRound(m_view->scaleFactor() * 100));
    if (const QString turned = describeOrientation(m_view->orientation()); !turned.isEmpty())
        parts << turned;
    if (m_showingPreview)
        parts << QStringLiteral("preview...");

    m_statusRight->setText(parts.join(QStringLiteral("   ")));
}

void MainWindow::showPlaceholder()
{
    m_index = -1;
    m_view->clearImage();
    m_info->clear();
    m_view->setMessage(tr("Drop an image here, or press Ctrl+O to open one"));
    setWindowTitle(QStringLiteral("Pic Viewer"));
    m_statusLeft->setText(tr("Ctrl+O to open, or drop a file here"));
    m_statusRight->clear();
    updateActionState();
}

void MainWindow::updateActionState()
{
    // Grey out what cannot act, so the toolbar tells the truth.
    const bool hasFile = !currentPath().isEmpty();
    const bool many = m_playlist.size() > 1;
    const bool hasImage = m_view->hasImage();

    m_actDelete->setEnabled(hasFile);
    m_actReveal->setEnabled(hasFile);
    m_actCopy->setEnabled(hasImage);
    for (QAction *action : {m_actPrev, m_actNext, m_actFirst, m_actLast})
        action->setEnabled(many);
    for (QAction *action : {m_actZoomIn, m_actZoomOut, m_actFit, m_actActual,
                            m_actRotateRight, m_actRotateLeft, m_actFlipH, m_actFlipV})
        action->setEnabled(hasImage);
}

QString MainWindow::currentPath() const
{
    if (m_playlist.isEmpty() || m_index < 0 || m_index >= m_playlist.size())
        return {};
    return m_playlist.at(m_index);
}

// -- commands --------------------------------------------------------------

void MainWindow::toggleFullscreen()
{
    const bool entering = !isFullScreen();
    qCInfo(logWindow) << "fullscreen" << (entering ? "on" : "off");
    statusBar()->setVisible(!entering);
    menuBar()->setVisible(!entering);
    m_toolBar->setVisible(!entering);
    // The panel goes with the rest of the chrome and comes back if it was
    // open. I still brings it up in fullscreen for a look.
    if (entering) {
        m_infoBeforeFullscreen = m_infoDock->isVisible();
        m_infoDock->hide();
    } else {
        m_infoDock->setVisible(m_infoBeforeFullscreen);
    }
    if (entering)
        showFullScreen();
    else
        showNormal();
}

void MainWindow::closeOrLeaveFullscreen()
{
    if (isFullScreen())
        toggleFullscreen();
    else
        close();
}

void MainWindow::deleteCurrent()
{
    const QString path = currentPath();
    if (path.isEmpty())
        return;

    const QString name = QFileInfo(path).fileName();
    if (QMessageBox::question(this, tr("Delete"),
                              tr("Move to Recycle Bin?\n\n%1").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    QFile file(path);
    if (!file.moveToTrash()) {
        warn(tr("Delete failed: %1").arg(file.errorString()));
        return;
    }
    qCInfo(logWindow).noquote() << "deleted" << path;

    m_playlist.removeAt(m_index);
    if (m_playlist.isEmpty())
        showPlaceholder();
    else
        showIndex(qMin(m_index, m_playlist.size() - 1));
}

void MainWindow::copyCurrent()
{
    if (!m_view->hasImage())
        return;
    // As shown, not as stored: having turned a sideways photo upright, the
    // user expects to paste it upright.
    QApplication::clipboard()->setPixmap(m_view->displayedPixmap());
    qCInfo(logWindow).noquote() << "copied" << QFileInfo(currentPath()).fileName() << "to clipboard";
    statusBar()->showMessage(tr("Copied to clipboard"), 2000);
}

void MainWindow::revealCurrent()
{
    const QString path = currentPath();
    if (path.isEmpty())
        return;
    qCInfo(logWindow).noquote() << "reveal" << path;
    QProcess::startDetached(QStringLiteral("explorer"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
}

void MainWindow::openLogFolder()
{
    QDir().mkpath(applog::directory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(applog::directory()));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About Pic Viewer"),
        tr("Pic Viewer %1 (C++ / Qt %2)\n\n"
           "%3 file extensions across decoders: %4\n\n"
           "Log file:\n%5")
            .arg(QString::fromLatin1(Version), QString::fromLatin1(qVersion()))
            .arg(m_extensions.size())
            .arg(decoders::Registry::instance().decoderNames().join(QStringLiteral(", ")),
                 applog::filePath()));
}

void MainWindow::warn(const QString &message)
{
    QMessageBox::warning(this, QStringLiteral("Pic Viewer"), message);
}

// -- drag and drop ---------------------------------------------------------
//
// The canvas covers almost the whole window and handles its own drops (see
// ImageView). These only catch drops on the remaining chrome, such as the
// status bar.

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!droppedPaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!droppedPaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList paths = droppedPaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    openPaths(paths);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_loader->shutdown();
    QMainWindow::closeEvent(event);
}
