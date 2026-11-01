// End-to-end suites: a real MainWindow, real events, real files.
//
// Ported from the Python functional suites, with one deliberate change. Those
// printed observations and left a runner to grep the output; these assert.
// Most of what they establish turned out to be pinnable after all -- which
// widget received a drop, whether a modal appeared, what the toolbar's enabled
// state was -- and an assertion says it once instead of asking a reader to
// notice.
//
// Everything runs offscreen, so no display is needed.

#include <QtTest>

#include <QClipboard>
#include <QDockWidget>
#include <QSpinBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMenuBar>
#include <QGraphicsItem>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QStatusBar>
#include <QToolBar>

#include "applog.h"
#include "infopanel.h"
#include "jpegorient.h"
#include "settingsdialog.h"
#include "viewer.h"
#include "window.h"

namespace {

QString samplesDir() { return QStringLiteral(PICVIEWER_SAMPLES_DIR); }

/// Logged by main() before any suite runs, and looked for in the log file.
/// It has to be then: inside qExec QTest installs its own message handler,
/// which keeps every message for the test report and passes none on, so
/// nothing a test logs reaches the application's log at all.
const QString &startupMarker()
{
    static const QString marker = QStringLiteral("functional tests started, run %1")
                                      .arg(QDateTime::currentMSecsSinceEpoch());
    return marker;
}
QString sample(const char *name) { return samplesDir() + QLatin1Char('/') + QLatin1String(name); }

/// Spin the event loop until *predicate* holds or the deadline passes.
template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qSleep(10);
    }
    return predicate();
}

bool waitForImage(MainWindow &window, int timeoutMs = 15000)
{
    return waitFor([&] { return window.view()->hasImage() && !window.showingPreview(); },
                   timeoutMs);
}

/// A QMimeData carrying local paths. Ownership passes to the event.
QMimeData *urlsFor(const QStringList &paths)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls << QUrl::fromLocalFile(path);
    mime->setUrls(urls);
    return mime;
}

} // namespace

// ---------------------------------------------------------------------------

/// Which widget actually receives a drop.
///
/// This is the suite that exists because of a real defect. A QGraphicsView
/// turns on acceptDrops for its *viewport* so it can offer drags to graphics
/// items, and marks the event handled whether or not anything took it. The
/// canvas covers the window, so the cursor is always over the viewport, and
/// drops handled on the window never fired once.
class DropRoutingTests : public QObject
{
    Q_OBJECT

private slots:
    void theViewportIsWhereADropActuallyLands()
    {
        // A fresh window per target: openPaths({}) returns early on an empty
        // list, so there is no reset that would let one window serve all three.
        for (int which = 0; which < 3; ++which) {
            MainWindow window;
            window.resize(900, 600);
            window.show();

            const char *name = nullptr;
            QWidget *target = nullptr;
            switch (which) {
            case 0: name = "MainWindow";          target = &window; break;
            case 1: name = "ImageView";           target = window.view(); break;
            default: name = "ImageView::viewport"; target = window.view()->viewport(); break;
            }

            QMimeData *enterMime = urlsFor({sample("sample.png")});
            QDragEnterEvent enter(QPointF(400, 300), Qt::CopyAction, enterMime,
                                  Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &enter);

            QMimeData *dropMime = urlsFor({sample("sample.png")});
            QDropEvent drop(QPointF(400, 300), Qt::CopyAction, dropMime,
                            Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &drop);

            QVERIFY2(!window.playlist().isEmpty(),
                     qPrintable(QStringLiteral("a drop sent to %1 opened nothing; on the "
                                               "desktop the viewport is what receives it")
                                    .arg(QLatin1String(name))));
            delete enterMime;
            delete dropMime;
        }
    }
};

// ---------------------------------------------------------------------------

class DragDropTests : public QObject
{
    Q_OBJECT

    static void dropOn(MainWindow &window, const QStringList &paths)
    {
        QWidget *viewport = window.view()->viewport();

        QMimeData *enterMime = urlsFor(paths);
        QDragEnterEvent enter(QPointF(400, 300), Qt::CopyAction, enterMime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &enter);

        QMimeData *dropMime = urlsFor(paths);
        QDropEvent drop(QPointF(400, 300), Qt::CopyAction, dropMime,
                        Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &drop);

        delete enterMime;
        delete dropMime;
    }

private slots:
    void oneFileOpensItAndItsFolderBecomesThePlaylist()
    {
        MainWindow window;
        window.show();
        dropOn(window, {sample("sample.png")});
        QVERIFY(window.playlist().size() > 1);
        QCOMPARE(QFileInfo(window.playlist().at(window.index())).fileName(),
                 QStringLiteral("sample.png"));
    }

    void severalFilesBecomeThePlaylistThemselves()
    {
        MainWindow window;
        window.show();
        dropOn(window, {sample("sample.jpg"), sample("sample.webp"), sample("synthetic.dng")});
        QCOMPARE(window.playlist().size(), 3);
    }

    void aFolderOpensEverythingInIt()
    {
        MainWindow window;
        window.show();
        dropOn(window, {samplesDir()});
        QVERIFY(window.playlist().size() > 5);
    }

    void aFolderAndAFileTogetherAreDeduplicated()
    {
        MainWindow window;
        window.show();
        dropOn(window, {samplesDir(), sample("sample.png")});
        const QStringList playlist = window.playlist();
        QCOMPARE(playlist.size(), QSet<QString>(playlist.begin(), playlist.end()).size());
    }

    void plainTextIsRefused()
    {
        MainWindow window;
        window.show();
        QWidget *viewport = window.view()->viewport();

        QMimeData *mime = new QMimeData;
        mime->setText(QStringLiteral("just some text"));
        QDragEnterEvent enter(QPointF(400, 300), Qt::CopyAction, mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &enter);

        QVERIFY(!enter.isAccepted());
        QVERIFY(window.playlist().isEmpty());
        delete mime;
    }

    void aDragThatLeavesWithoutDroppingClearsTheHighlight()
    {
        MainWindow window;
        window.show();
        QWidget *viewport = window.view()->viewport();

        QMimeData *mime = urlsFor({sample("sample.png")});
        QDragEnterEvent enter(QPointF(400, 300), Qt::CopyAction, mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &enter);
        QVERIFY(enter.isAccepted());

        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(viewport, &leave);

        // Nothing opened, and the canvas is back to its idle state.
        QVERIFY(window.playlist().isEmpty());
        delete mime;
    }

    void aDragMustStayAcceptedWhileItMoves()
    {
        // Without dragMoveEvent the drag is refused mid-flight and the cursor
        // shows "no entry" even though dragEnterEvent accepted it.
        MainWindow window;
        window.show();
        QWidget *viewport = window.view()->viewport();

        QMimeData *enterMime = urlsFor({sample("sample.png")});
        QDragEnterEvent enter(QPointF(400, 300), Qt::CopyAction, enterMime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &enter);

        QMimeData *moveMime = urlsFor({sample("sample.png")});
        QDragMoveEvent move(QPointF(420, 320), Qt::CopyAction, moveMime,
                            Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &move);

        QVERIFY2(move.isAccepted(), "the drag was refused while moving over the canvas");
        delete enterMime;
        delete moveMime;
    }
};

// ---------------------------------------------------------------------------

class NavigationTests : public QObject
{
    Q_OBJECT

private slots:
    void everySampleInAFolderDecodes()
    {
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.openPath(samplesDir());
        QVERIFY(waitForImage(window));

        const int count = window.playlist().size();
        QVERIFY(count > 5);

        QStringList failures;
        for (int i = 0; i < count; ++i) {
            window.showIndex(i);
            if (!waitForImage(window))
                failures << QFileInfo(window.playlist().at(i)).fileName();
        }
        QVERIFY2(failures.isEmpty(), qPrintable("did not decode: " + failures.join(", ")));
    }

    void steppingWrapsAtBothEnds()
    {
        MainWindow window;
        window.show();
        window.openPath(samplesDir());
        QVERIFY(waitForImage(window));
        const int count = window.playlist().size();

        window.showIndex(count - 1);
        window.step(1);
        QCOMPARE(window.index(), 0);

        window.step(-1);
        QCOMPARE(window.index(), count - 1);
    }

    void fitShrinksALargeImageAndLeavesASmallOneAlone()
    {
        MainWindow window;
        // Small enough that a 640x480 image overflows the canvas in physical
        // pixels even at 2x display scaling, where 400x300 would not.
        window.resize(300, 220);        // smaller than the 640x480 samples
        window.show();
        window.openPath(sample("sample.png"));
        QVERIFY(waitForImage(window));

        window.view()->fit();
        QVERIFY2(window.view()->scaleFactor() < 1.0,
                 "a 640x480 image in a 400x300 window should have been shrunk");

        window.view()->zoomToActual();
        QCOMPARE(window.view()->scaleFactor(), 1.0);
    }

    void zoomIsClampedAtBothEnds()
    {
        MainWindow window;
        window.show();
        window.openPath(sample("sample.png"));
        QVERIFY(waitForImage(window));

        for (int i = 0; i < 60; ++i)
            window.view()->zoomBy(ImageView::ZoomStep);
        QCOMPARE(window.view()->scaleFactor(), ImageView::MaxScale);

        for (int i = 0; i < 120; ++i)
            window.view()->zoomBy(1.0 / ImageView::ZoomStep);
        QCOMPARE(window.view()->scaleFactor(), ImageView::MinScale);
    }

    void aResultFromAnEarlierGenerationIsDiscarded()
    {
        // Results carry the generation they were requested under, and anything
        // the user has already moved past must not land on screen.
        //
        // Delivered directly rather than provoked by navigating quickly. The
        // racing version of this test passed with the check deleted: the last
        // sample in the folder happens to be the slowest to decode, so it
        // landed last whether or not stale results were being discarded.
        MainWindow window;
        window.show();
        window.openPath(sample("sample.png"));
        QVERIFY(waitForImage(window));
        const QSize shown = window.view()->pixmap().size();

        auto *loader = window.findChild<ImageLoader *>();
        QVERIFY(loader);

        decoders::DecodedImage stale;
        stale.image = QImage(123, 45, QImage::Format_RGB32);
        stale.image.fill(Qt::red);
        stale.fullSize = stale.image.size();

        // Generation 0 predates every navigation this window has made.
        emit loader->ready(sample("sample.png"), stale, 0);
        QCoreApplication::processEvents();

        QCOMPARE(window.view()->pixmap().size(), shown);
    }

    void rapidNavigationSettlesOnTheLastImage()
    {
        MainWindow window;
        window.show();
        window.openPath(samplesDir());
        QVERIFY(waitForImage(window));

        const int count = window.playlist().size();
        for (int i = 0; i < count; ++i)
            window.showIndex(i);        // no waiting: every decode but the last is stale

        QVERIFY(waitForImage(window));
        QCOMPARE(window.index(), count - 1);
        QVERIFY(window.view()->hasImage());
    }
};

// ---------------------------------------------------------------------------

class ShortcutTests : public QObject
{
    Q_OBJECT

private slots:
    void navigationKeys_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("expected");     // index after the key, from 5
        QTest::newRow("Right") << int(Qt::Key_Right) << 6;
        QTest::newRow("Down") << int(Qt::Key_Down) << 6;
        QTest::newRow("Space") << int(Qt::Key_Space) << 6;
        QTest::newRow("PageDown") << int(Qt::Key_PageDown) << 6;
        QTest::newRow("Left") << int(Qt::Key_Left) << 4;
        QTest::newRow("Up") << int(Qt::Key_Up) << 4;
        QTest::newRow("Backspace") << int(Qt::Key_Backspace) << 4;
        QTest::newRow("PageUp") << int(Qt::Key_PageUp) << 4;
        QTest::newRow("Home") << int(Qt::Key_Home) << 0;
    }

    void navigationKeys()
    {
        QFETCH(int, key);
        QFETCH(int, expected);

        MainWindow window;
        window.show();
        window.openPath(samplesDir());
        QVERIFY(waitForImage(window));
        QVERIFY(window.playlist().size() > 7);

        window.showIndex(5);
        QTest::qWait(60);
        QTest::keyClick(&window, static_cast<Qt::Key>(key));
        QTest::qWait(60);
        QCOMPARE(window.index(), expected);
    }

    void endGoesToTheLastImage()
    {
        MainWindow window;
        window.show();
        window.openPath(samplesDir());
        QVERIFY(waitForImage(window));

        window.showIndex(0);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::qWait(60);
        QCOMPARE(window.index(), window.playlist().size() - 1);
    }

    void zoomKeysChangeTheScale()
    {
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.openPath(sample("sample.png"));
        QVERIFY(waitForImage(window));

        window.view()->zoomToActual();
        QTest::keyClick(&window, Qt::Key_Plus);
        QTest::qWait(40);
        QVERIFY2(window.view()->scaleFactor() > 1.0, "+ did not zoom in");

        window.view()->zoomToActual();
        QTest::keyClick(&window, Qt::Key_Minus);
        QTest::qWait(40);
        QVERIFY2(window.view()->scaleFactor() < 1.0, "- did not zoom out");
    }

    void oneRestoresActualSizeAndZeroFits()
    {
        MainWindow window;
        // Small enough that a 640x480 image overflows the canvas in physical
        // pixels even at 2x display scaling, where 400x300 would not.
        window.resize(300, 220);
        window.show();
        window.openPath(sample("sample.png"));
        QVERIFY(waitForImage(window));

        QTest::keyClick(&window, Qt::Key_1);
        QTest::qWait(40);
        QCOMPARE(window.view()->scaleFactor(), 1.0);

        QTest::keyClick(&window, Qt::Key_0);
        QTest::qWait(40);
        QVERIFY(window.view()->scaleFactor() < 1.0);
    }
};

// ---------------------------------------------------------------------------

class ChromeTests : public QObject
{
    Q_OBJECT

private slots:
    void everyMenuIsPresent()
    {
        MainWindow window;
        window.show();
        QStringList titles;
        for (QAction *action : window.menuBar()->actions())
            titles << action->text().remove(QLatin1Char('&'));
        QCOMPARE(titles, QStringList({"File", "Go", "View", "Help"}));
    }

    void theToolbarExists()
    {
        MainWindow window;
        window.show();
        auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("main_toolbar"));
        QVERIFY2(toolbar, "no toolbar: every action would be keyboard-only");
        QVERIFY(toolbar->isVisible());
    }

    void actionsAreDisabledWhenTheyCannotAct()
    {
        MainWindow window;
        window.show();

        // Nothing open: navigation, deletion, zoom and turns have nothing to
        // act on.
        const QStringList shouldBeOff = {"&Previous", "&Next", "&Delete",
                                         "&Fit to Window", "Show in E&xplorer",
                                         "Rotate &Right", "Rotate &Left",
                                         "Flip &Horizontal", "Flip &Vertical"};
        for (QAction *action : window.actions()) {
            if (shouldBeOff.contains(action->text()))
                QVERIFY2(!action->isEnabled(),
                         qPrintable(action->text() + " is enabled with nothing open"));
        }

        window.openPath(samplesDir());
        QVERIFY(waitForImage(window));
        for (QAction *action : window.actions()) {
            if (shouldBeOff.contains(action->text()))
                QVERIFY2(action->isEnabled(),
                         qPrintable(action->text() + " is disabled with a folder open"));
        }
    }

    void fullscreenHidesTheChromeAndRestoresIt()
    {
        MainWindow window;
        window.show();
        auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("main_toolbar"));

        window.toggleFullscreen();
        QTest::qWait(100);
        QVERIFY(!window.menuBar()->isVisible());
        QVERIFY(!toolbar->isVisible());
        QVERIFY(!window.statusBar()->isVisible());

        window.toggleFullscreen();
        QTest::qWait(100);
        QVERIFY(window.menuBar()->isVisible());
        QVERIFY(toolbar->isVisible());
        QVERIFY(window.statusBar()->isVisible());
    }

    void escapeLeavesFullscreenRatherThanQuitting()
    {
        MainWindow window;
        window.show();
        window.toggleFullscreen();
        QTest::qWait(100);
        QVERIFY(window.isFullScreen());

        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::qWait(100);
        QVERIFY2(!window.isFullScreen(), "Escape did not leave fullscreen");
        QVERIFY2(window.isVisible(), "Escape closed the window instead");
    }

    void theStatusBarNamesTheDecoderThatWon()
    {
        // Which backend handled a file is the first thing worth knowing when
        // an image looks wrong, so it belongs on screen and not only in the log.
        MainWindow window;
        window.show();
        window.openPath(sample("synthetic.dng"));
        QVERIFY(waitForImage(window));
        QVERIFY2(window.statusText().contains(QStringLiteral("via raw")),
                 qPrintable("status bar reads: " + window.statusText()));
    }

    void theLogIsBeingWritten()
    {
        // Looks for the line main() logged at startup, unique to this run.
        // This used to check only that the file was not empty, which on any
        // machine with a log from an earlier session passed whether or not
        // anything was written now. A fresh CI runner, with no earlier
        // session, is what showed the file was in fact empty.
        QFile log(applog::filePath());
        QVERIFY2(log.open(QIODevice::ReadOnly), qPrintable("cannot read " + log.fileName()));
        QVERIFY2(QString::fromUtf8(log.readAll()).contains(startupMarker()),
                 "the line logged at startup is not in the file; it is still "
                 "buffered, or the log is going somewhere else");
    }
};

// ---------------------------------------------------------------------------

class FailureTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QStringList m_locked;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());

        // Real image bytes behind a deny-read ACE: the same shape of failure as
        // a cloud placeholder the sync engine cannot fetch.
        const QStringList sources = {"sample.heic", "sample.jpg", "synthetic.dng"};
        const QString user = qEnvironmentVariable("USERNAME");
        QVERIFY(!user.isEmpty());

        for (const QString &name : sources) {
            const QString target = m_dir.filePath(QStringLiteral("locked_") + name);
            QVERIFY(QFile::copy(samplesDir() + QLatin1Char('/') + name, target));
            QProcess icacls;
            icacls.start(QStringLiteral("icacls"),
                         {QDir::toNativeSeparators(target), QStringLiteral("/deny"),
                          user + QStringLiteral(":(R)")});
            QVERIFY(icacls.waitForFinished(10000));
            m_locked << target;
        }
        // If the ACE did not take, the rest of this suite proves nothing.
        QFile probe(m_locked.first());
        QVERIFY2(!probe.open(QIODevice::ReadOnly),
                 "the deny-read ACE did not apply; the fixture is not unreadable");
    }

    void cleanupTestCase()
    {
        const QString user = qEnvironmentVariable("USERNAME");
        for (const QString &path : m_locked) {
            QProcess icacls;
            icacls.start(QStringLiteral("icacls"),
                         {QDir::toNativeSeparators(path), QStringLiteral("/remove:d"), user});
            icacls.waitForFinished(10000);
        }
    }

    void anUnreadableFileSaysSoOnTheCanvasAndNotInADialog()
    {
        MainWindow window;
        window.resize(900, 600);
        window.show();
        window.openPath(m_locked.first());

        QVERIFY(waitFor([&] { return window.statusText() == QStringLiteral("Failed"); }));
        QVERIFY2(!window.view()->hasImage(), "something was shown for an unreadable file");

        // Stepping through a folder of these would mean one modal per image.
        QCoreApplication::processEvents();
        QVERIFY2(QApplication::activeModalWidget() == nullptr,
                 "a modal dialog appeared for an unreadable file");
    }

    void browsingAFolderOfThemRaisesNoDialogs()
    {
        MainWindow window;
        window.show();
        window.openPath(m_dir.path());
        QVERIFY(waitFor([&] { return !window.playlist().isEmpty(); }));

        for (int i = 0; i < window.playlist().size(); ++i) {
            window.showIndex(i);
            QVERIFY(waitFor([&] { return window.statusText() == QStringLiteral("Failed"); }));
            QVERIFY2(QApplication::activeModalWidget() == nullptr,
                     "a modal appeared while browsing unreadable files");
        }
    }

    void areadableFileAfterwardsRecovers()
    {
        MainWindow window;
        window.show();
        window.openPath(m_locked.first());
        QVERIFY(waitFor([&] { return window.statusText() == QStringLiteral("Failed"); }));

        window.openPath(sample("sample.jpg"));
        QVERIFY(waitForImage(window));
        QVERIFY(window.view()->hasImage());
        QVERIFY(window.statusText().contains(QStringLiteral("JPEG")));
    }
};

// ---------------------------------------------------------------------------

/// Display scaling. Run with --scale 1.5 or --scale 2 to mean anything.
///
/// The transform a QGraphicsView applies is in logical pixels, and on a
/// display scaled to 150% each of those is 1.5 physical ones. Without
/// correcting for that, "100%" showed the image at 150% and resampled it on
/// the way -- the one thing a "1:1" view exists not to do.
class HighDpiTests : public QObject
{
    Q_OBJECT

    /// One-pixel checkerboard: at true 1:1 every adjacent screen pixel
    /// differs; scaled it comes out in blocks, smoothed it comes out grey.
    static QImage pixelCheckerboard(int size)
    {
        QImage image(size, size, QImage::Format_RGB32);
        for (int y = 0; y < size; ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < size; ++x)
                line[x] = ((x + y) % 2) ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
        }
        return image;
    }

    static qreal requestedScale()
    {
        return qEnvironmentVariable("QT_SCALE_FACTOR", QStringLiteral("1")).toDouble();
    }

    /// Width the image actually occupies on screen, in physical pixels.
    ///
    /// Measured from the transform and the display ratio directly, never via
    /// scaleFactor(). The first version of these tests asked scaleFactor()
    /// whether the zoom was 1:1, and with the fix reverted -- scaleFactor()
    /// included -- the code's own idea of 1:1 and its transform agreed with
    /// each other and both disagreed with the screen. Two tests passed on the
    /// bug they were written for.
    static qreal physicalWidthOnScreen(const ImageView &view)
    {
        // transform().mapRect rather than mapFromScene: the latter returns an
        // integer polygon whose inclusive bounding rect is a pixel wider, and
        // at 1.5x that rounding alone read a correct 640 as 642.
        const QRectF logical = view.transform().mapRect(view.sceneRect());
        return logical.width() * view.viewport()->devicePixelRatio();
    }

private slots:
    void theRequestedScaleIsTheScaleInEffect()
    {
        // If the platform ignored QT_SCALE_FACTOR, everything below would run
        // at ratio 1 and prove nothing about scaled displays.
        ImageView view;
        view.resize(400, 300);
        view.show();
        QTest::qWait(30);
        QCOMPARE(view.viewport()->devicePixelRatio(), requestedScale());
    }

    void actualSizeIsOneScreenPixelPerImagePixel()
    {
        ImageView view;
        view.resize(400, 300);
        view.show();
        view.setImage(pixelCheckerboard(96));
        view.zoomToActual();
        QTest::qWait(30);
        QCOMPARE(view.scaleFactor(), 1.0);

        // Render the canvas at its true device resolution and read the pixels
        // back, rather than trusting the arithmetic that set the transform.
        const QImage shot = view.viewport()->grab().toImage();
        const int cx = shot.width() / 2;
        const int cy = shot.height() / 2;
        const int span = 20;

        int alternations = 0;
        for (int i = -span; i < span; ++i) {
            const int a = qGray(shot.pixel(cx + i, cy));
            const int b = qGray(shot.pixel(cx + i + 1, cy));
            QVERIFY2(a < 24 || a > 231,
                     qPrintable(QStringLiteral("grey value %1 at 1:1 -- the image was resampled")
                                    .arg(a)));
            if ((a > 127) != (b > 127))
                ++alternations;
        }
        QVERIFY2(alternations == 2 * span,
                 qPrintable(QStringLiteral("%1 of %2 neighbouring screen pixels alternate; "
                                           "at true 1:1 every one does")
                                .arg(alternations).arg(2 * span)));
    }

    void aSmallImageFitsAtOneToOneNotEnlarged()
    {
        // 200x150 in a 400x300 canvas fits whatever the scaling, so fit
        // leaves it at 1:1 -- one image pixel per screen pixel, not one per
        // logical pixel, which on a scaled display is an enlargement.
        ImageView view;
        view.resize(400, 300);
        view.show();
        view.setImage(pixelCheckerboard(150).scaled(200, 150));
        view.fit();
        QTest::qWait(30);

        QVERIFY2(qAbs(physicalWidthOnScreen(view) - 200.0) < 0.5,
                 qPrintable(QStringLiteral("a 200-pixel image occupies %1 screen pixels after fit")
                                .arg(physicalWidthOnScreen(view))));
        QCOMPARE(view.scaleFactor(), 1.0);
    }

    void fitJudgesSizeInScreenPixelsNotLogicalOnes()
    {
        // 600x450 in a 400x300 canvas. At 1x it overflows and is shrunk. At 2x
        // the canvas is 800x600 real pixels, the image fits, and it should be
        // shown 1:1 -- comparing against the logical 400x300 instead would
        // call it too big and fit it to 400 logical pixels, which at 2x is 800
        // real ones: an enlargement, from the command meant to shrink.
        ImageView view;
        view.resize(400, 300);
        view.show();
        QImage image(600, 450, QImage::Format_RGB32);
        image.fill(Qt::darkMagenta);
        view.setImage(image);
        view.fit();
        QTest::qWait(30);

        const qreal ratio = view.viewport()->devicePixelRatio();
        const QSizeF screen = QSizeF(view.viewport()->size()) * ratio;
        const qreal width = physicalWidthOnScreen(view);
        if (600 <= screen.width() && 450 <= screen.height())
            QVERIFY2(qAbs(width - 600.0) < 0.5,
                     qPrintable(QStringLiteral("fits in %1x%2 screen pixels but was drawn "
                                               "%3 wide").arg(screen.width()).arg(screen.height())
                                    .arg(width)));
        else
            QVERIFY2(width < 600.0, "an image too big for the screen was not shrunk");
    }

    void aLargeImageFitsInsideTheViewport()
    {
        ImageView view;
        view.resize(400, 300);
        view.show();
        QImage large(3000, 2000, QImage::Format_RGB32);
        large.fill(Qt::darkCyan);
        view.setImage(large);
        view.fit();
        QTest::qWait(30);

        const QRect onScreen = view.mapFromScene(view.sceneRect()).boundingRect();
        QVERIFY2(onScreen.width() <= view.viewport()->width() + 1
                     && onScreen.height() <= view.viewport()->height() + 1,
                 "a fitted image overflows the canvas");
        QVERIFY(view.scaleFactor() < 1.0);
    }

    void theStatusBarSaysOneHundredPercentAtOneToOne()
    {
        MainWindow window;
        window.resize(900, 600);
        window.show();
        window.openPath(sample("sample.png"));
        QVERIFY(waitForImage(window));
        QTest::keyClick(&window, Qt::Key_1);
        QTest::qWait(40);
        QVERIFY2(window.statusText().contains(QStringLiteral("100%")),
                 qPrintable("status bar reads: " + window.statusText()));

        // And the label has to be true. Before the fix it said 100% at every
        // display scale, consistent with the transform and wrong about the
        // screen: the 640-pixel sample was 1280 screen pixels wide at 2x.
        const qreal onScreen = physicalWidthOnScreen(*window.view());
        QVERIFY2(qAbs(onScreen - 640.0) < 0.5,
                 qPrintable(QStringLiteral("status says 100%, but the 640-pixel image is "
                                           "%1 screen pixels wide").arg(onScreen)));
    }
};

// ---------------------------------------------------------------------------

/// Rotate and flip, for viewing only.
///
/// What can go wrong here is mostly invisible to a test that asks the code
/// what it did: a turn the wrong way round, a flip and a turn composed in the
/// wrong order, a fit computed for the image as it was before it was turned.
/// So these read the answer off the rendered canvas, from an image whose
/// orientation shows in its colours.
class OrientationTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    /// Red top-left, green top-right, blue bottom-left, yellow bottom-right.
    static QImage quadrants(int width, int height)
    {
        QImage image(width, height, QImage::Format_RGB32);
        QPainter painter(&image);
        const int w = width / 2;
        const int h = height / 2;
        painter.fillRect(0, 0, w, h, Qt::red);
        painter.fillRect(w, 0, width - w, h, Qt::green);
        painter.fillRect(0, h, w, height - h, Qt::blue);
        painter.fillRect(w, h, width - w, height - h, Qt::yellow);
        return image;
    }

    /// The colour a fraction of the way across and down the image as drawn,
    /// read from the rendered canvas rather than from any state of the view.
    static QColor colourOnScreen(const ImageView &view, qreal across, qreal down)
    {
        const QRect shown = view.mapFromScene(view.scene()->itemsBoundingRect()).boundingRect();
        const qreal ratio = view.viewport()->devicePixelRatio();
        const QImage shot = view.viewport()->grab().toImage();
        const QPointF logical(shown.x() + shown.width() * across,
                              shown.y() + shown.height() * down);
        return shot.pixelColor((logical * ratio).toPoint());
    }

    static bool near(const QColor &a, const QColor &b)
    {
        return qAbs(a.red() - b.red()) < 40 && qAbs(a.green() - b.green()) < 40
               && qAbs(a.blue() - b.blue()) < 40;
    }

    static QString name(const QColor &colour)
    {
        static const QList<QPair<QColor, QString>> names = {
            {Qt::red, "red"}, {Qt::green, "green"}, {Qt::blue, "blue"}, {Qt::yellow, "yellow"}};
        for (const auto &[known, label] : names) {
            if (near(colour, known))
                return label;
        }
        return colour.name();
    }

    static Qt::Key keyFor(QChar letter)
    {
        switch (letter.toLatin1()) {
        case 'R': return Qt::Key_R;
        case 'L': return Qt::Key_L;
        case 'H': return Qt::Key_H;
        case 'V': return Qt::Key_V;
        }
        return Qt::Key_unknown;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(quadrants(240, 160).save(m_dir.filePath(QStringLiteral("turn.png"))));
        // A different size, so the test can tell when it is on screen.
        QVERIFY(quadrants(200, 100).save(m_dir.filePath(QStringLiteral("other.png"))));
    }

    void keysTurnTheImageAndTheStatusSaysSo_data()
    {
        QTest::addColumn<QString>("keys");
        QTest::addColumn<QColor>("topLeft");
        QTest::addColumn<QColor>("topRight");
        QTest::addColumn<QString>("label");     // empty: upright, nothing shown

        const QColor red(Qt::red), green(Qt::green), blue(Qt::blue), yellow(Qt::yellow);
        QTest::newRow("R: clockwise") << "R" << blue << red << QStringLiteral("rotated 90°");
        QTest::newRow("L: counter-clockwise")
            << "L" << green << yellow << QStringLiteral("rotated 270°");
        QTest::newRow("RR: upside down") << "RR" << yellow << blue << QStringLiteral("rotated 180°");
        QTest::newRow("RRRR: round again") << "RRRR" << red << green << QString();
        QTest::newRow("H: mirrored") << "H" << green << red << QStringLiteral("flipped horizontally");
        QTest::newRow("V: upside-down mirror")
            << "V" << blue << yellow << QStringLiteral("flipped vertically");
        QTest::newRow("HH: back again") << "HH" << red << green << QString();
        QTest::newRow("HV: same as a half turn")
            << "HV" << yellow << blue << QStringLiteral("rotated 180°");
        // The two that say order matters. Turn-then-flip and flip-then-turn
        // are different pictures, and the user means the order they pressed.
        QTest::newRow("RH: turn, then flip")
            << "RH" << red << blue << QStringLiteral("rotated 270°, flipped");
        QTest::newRow("HR: flip, then turn")
            << "HR" << yellow << green << QStringLiteral("rotated 90°, flipped");
    }

    void keysTurnTheImageAndTheStatusSaysSo()
    {
        QFETCH(QString, keys);
        QFETCH(QColor, topLeft);
        QFETCH(QColor, topRight);
        QFETCH(QString, label);

        MainWindow window;
        window.resize(900, 600);
        window.show();
        window.openPath(m_dir.filePath(QStringLiteral("turn.png")));
        QVERIFY(waitForImage(window));

        for (const QChar letter : keys)
            QTest::keyClick(&window, keyFor(letter));
        QTest::qWait(40);

        const QColor seenLeft = colourOnScreen(*window.view(), 0.25, 0.25);
        const QColor seenRight = colourOnScreen(*window.view(), 0.75, 0.25);
        QVERIFY2(near(seenLeft, topLeft) && near(seenRight, topRight),
                 qPrintable(QStringLiteral("after %1 the top row reads %2, %3; expected %4, %5")
                                .arg(keys, name(seenLeft), name(seenRight), name(topLeft),
                                     name(topRight))));

        const QStringList status = window.statusText().split(QStringLiteral("   "));
        if (label.isEmpty())
            QVERIFY2(!window.statusText().contains(QStringLiteral("rotated"))
                         && !window.statusText().contains(QStringLiteral("flipped")),
                     qPrintable("upright, but the status bar reads: " + window.statusText()));
        else
            QVERIFY2(status.contains(label),
                     qPrintable("status bar reads: " + window.statusText()));
    }

    void aNewImageStartsUpright()
    {
        MainWindow window;
        window.resize(900, 600);
        window.show();
        window.openPath(m_dir.filePath(QStringLiteral("turn.png")));
        QVERIFY(waitForImage(window));
        QTest::keyClick(&window, Qt::Key_R);
        QVERIFY(!window.view()->orientation().isIdentity());

        window.step(1);
        QVERIFY(waitFor([&] { return window.view()->pixmap().size() == QSize(200, 100); }));
        QVERIFY2(window.view()->orientation().isIdentity(),
                 qPrintable("the next image arrived " + describeOrientation(window.view()->orientation())));
        QVERIFY2(!window.statusText().contains(QStringLiteral("rotated")),
                 qPrintable("status bar reads: " + window.statusText()));
    }

    void theSharperDecodeKeepsTheTurn()
    {
        // A RAW shows its embedded preview first and swaps in the full decode
        // a second or more later. A turn made in that second is the user's,
        // and the swap must not undo it.
        ImageView view;
        view.resize(400, 300);
        view.show();
        view.setImage(quadrants(120, 80));
        view.rotateClockwise();
        view.setImage(quadrants(240, 160), true);
        QTest::qWait(30);

        QCOMPARE(describeOrientation(view.orientation()), QStringLiteral("rotated 90°"));
        QCOMPARE(view.sceneRect().size(), QSizeF(160, 240));
        QVERIFY2(near(colourOnScreen(view, 0.25, 0.25), Qt::blue),
                 qPrintable("top-left reads " + name(colourOnScreen(view, 0.25, 0.25))));
    }

    void aTurnKeepsTheZoom()
    {
        // The turn belongs to the image, not the view. The view's transform
        // is the zoom, read as its m11 -- and a quarter turn makes m11 zero.
        ImageView view;
        view.resize(400, 300);
        view.show();
        view.setImage(quadrants(1200, 800));
        view.zoomToActual();
        view.rotateClockwise();
        QTest::qWait(30);

        QCOMPARE(view.scaleFactor(), 1.0);
        const qreal width = view.transform().mapRect(view.scene()->itemsBoundingRect()).width()
                            * view.viewport()->devicePixelRatio();
        QVERIFY2(qAbs(width - 800.0) < 0.5,
                 qPrintable(QStringLiteral("turned at 1:1, the 800-pixel side is %1 screen "
                                           "pixels wide").arg(width)));
    }

    void fitMeasuresTheImageAsTurned()
    {
        // 3000x1000 fitted to a 400x300 canvas is limited by its width. Turned,
        // it is 1000x3000 and limited by its height; fitting it by the old
        // shape would leave it taller than the canvas.
        ImageView view;
        view.resize(400, 300);
        view.show();
        view.setImage(quadrants(3000, 1000));
        view.fit();
        view.rotateClockwise();
        QTest::qWait(30);

        const QRect onScreen = view.mapFromScene(view.scene()->itemsBoundingRect()).boundingRect();
        const QRect canvas = view.viewport()->rect();
        QVERIFY2(onScreen.height() <= canvas.height() + 1 && onScreen.width() <= canvas.width() + 1,
                 qPrintable(QStringLiteral("turned and fitted, the image is %1x%2 in a %3x%4 canvas")
                                .arg(onScreen.width()).arg(onScreen.height())
                                .arg(canvas.width()).arg(canvas.height())));
        // Fitted, not merely small: the long side fills the canvas.
        QVERIFY2(onScreen.height() >= canvas.height() - 8,
                 qPrintable(QStringLiteral("turned and fitted, the image is %1 tall in a %2 canvas")
                                .arg(onScreen.height()).arg(canvas.height())));
        QVERIFY(qAbs(onScreen.center().x() - canvas.center().x()) <= 2);
    }

    void zoomedInTheDetailStaysUnderTheCentre()
    {
        ImageView view;
        view.resize(400, 300);
        view.show();
        view.setImage(quadrants(2000, 1600));
        view.zoomToActual();

        QGraphicsItem *item = view.scene()->items().constFirst();
        const QPointF detail(500, 400);
        view.centerOn(item->mapToScene(detail));
        const auto underCentre = [&] {
            return item->mapFromScene(view.mapToScene(view.viewport()->rect().center()));
        };
        // Scrolling is in whole logical pixels, each worth `ratio` image
        // pixels at 1:1, and centring rounds once before the turn and once
        // after. Losing the detail altogether is hundreds of pixels.
        const qreal slack = 2 * view.viewport()->devicePixelRatio() + 1;
        const auto closeTo = [&](const QPointF &p) {
            return qAbs(p.x() - detail.x()) <= slack && qAbs(p.y() - detail.y()) <= slack;
        };
        QVERIFY(closeTo(underCentre()));     // the premise

        view.rotateClockwise();
        QTest::qWait(30);
        const QPointF now = underCentre();
        QVERIFY2(closeTo(now),
                 qPrintable(QStringLiteral("looking at image pixel (500, 400) before the turn "
                                           "and (%1, %2) after it")
                                .arg(qRound(now.x())).arg(qRound(now.y()))));
    }

    void copyHandsOverTheImageAsShown()
    {
        MainWindow window;
        window.resize(900, 600);
        window.show();
        window.openPath(m_dir.filePath(QStringLiteral("turn.png")));
        QVERIFY(waitForImage(window));

        QTest::keyClick(&window, Qt::Key_R);
        QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier);
        QTest::qWait(40);

        const QImage copied = QApplication::clipboard()->image();
        QCOMPARE(copied.size(), QSize(160, 240));
        QVERIFY2(near(copied.pixelColor(40, 60), Qt::blue),
                 qPrintable("the copy's top-left is " + name(copied.pixelColor(40, 60))));
        QVERIFY2(near(copied.pixelColor(120, 60), Qt::red),
                 qPrintable("the copy's top-right is " + name(copied.pixelColor(120, 60))));
    }
};

// ---------------------------------------------------------------------------

/// The info panel: opened with I, filled from the camera's record, and kept
/// in step with whatever is on screen.
class InfoPanelTests : public QObject
{
    Q_OBJECT

    // The fixtures are copied out rather than opened in place: opening one
    // makes its folder the playlist, and the fixtures folder also holds a
    // 309 MiB-decoded PNG that prefetch would then decode on every test.
    QTemporaryDir m_dir;

    QString copy(const QString &from, const QString &name)
    {
        const QString to = m_dir.filePath(name);
        QFile::remove(to);
        return QFile::copy(from, to) ? to : QString();
    }

    static QString fixture(const char *name)
    {
        return QStringLiteral(PICVIEWER_FIXTURES_DIR "/") + QLatin1String(name);
    }

    static QString panelText(const MainWindow &window) { return window.infoPanel()->text(); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(!copy(fixture("exif_camera.jpg"), QStringLiteral("a_camera.jpg")).isEmpty());
        QVERIFY(!copy(fixture("exif_phone.heic"), QStringLiteral("b_phone.heic")).isEmpty());
        QVERIFY(!copy(sample("sample.png"), QStringLiteral("c_plain.png")).isEmpty());
        QFile broken(m_dir.filePath(QStringLiteral("d_broken.jpg")));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("\xff\xd8 this is not a JPEG beyond its first two bytes");
        QVERIFY(!copy(fixture("exif_png.png"), QStringLiteral("e_png.png")).isEmpty());
        QVERIFY(!copy(fixture("exif_webp.webp"), QStringLiteral("f_webp.webp")).isEmpty());
        QVERIFY(!copy(fixture("exif_tiff.tif"), QStringLiteral("g_tiff.tif")).isEmpty());
    }

    void iOpensAndClosesThePanel()
    {
        MainWindow window;
        window.show();
        // Shortcuts only fire in the active window, and with nothing to load
        // there is no wait below in which the window becomes active.
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY2(!window.infoDock()->isVisible(), "the panel should start closed");
        QTest::keyClick(&window, Qt::Key_I);
        QVERIFY(waitFor([&] { return window.infoDock()->isVisible(); }, 2000));
        QTest::keyClick(&window, Qt::Key_I);
        QVERIFY(waitFor([&] { return !window.infoDock()->isVisible(); }, 2000));
    }

    void itShowsWhatTheCameraRecorded_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QStringList>("expected");
        QTest::addColumn<QStringList>("absent");
        QTest::newRow("camera JPEG")
            << "a_camera.jpg"
            << QStringList{"Camera: Canon EOS R5", "Lens: RF24-105mm F4 L IS USM",
                           "Focal length: 70 mm", "Aperture: f/5.6", "Shutter: 1/250 s",
                           "ISO: 400", "Exposure bias: +0.7 EV", "Taken: 2025-05-01 13:22:10",
                           "Name: a_camera.jpg", "Dimensions: 64 x 48", "Format: JPEG"}
            << QStringList{"equivalent", "No camera information"};
        QTest::newRow("phone HEIC")
            << "b_phone.heic"
            << QStringList{"Camera: Apple iPhone 13", "Focal length: 5.1 mm (26 mm equivalent)",
                           "Aperture: f/1.6", "Shutter: 1/60 s", "ISO: 125",
                           "Taken: 2025-06-02 08:05:09", "Format: HEIF",
                           "Location: 25.033964° N, 121.564472° E", "Altitude: 12 m"}
            << QStringList{"Exposure bias"};
        QTest::newRow("PNG, no camera")
            << "c_plain.png"
            << QStringList{"No camera information in this file", "Name: c_plain.png"}
            << QStringList{"Camera: ", "Taken:"};
        QTest::newRow("PNG with eXIf")
            << "e_png.png" << QStringList{"Camera: Canon EOS R5", "Aperture: f/5.6", "Format: PNG"}
            << QStringList{"No camera information", "Location"};
        QTest::newRow("WebP with EXIF")
            << "f_webp.webp" << QStringList{"Camera: Canon EOS R5", "Shutter: 1/250 s"}
            << QStringList{"No camera information"};
        QTest::newRow("TIFF, directories after the pixels, south and west")
            << "g_tiff.tif"
            << QStringList{"Camera: Canon EOS R5", "ISO: 400",
                           "Location: 22.951916° S, 43.210487° W", "Altitude: 700 m"}
            << QStringList{"No camera information"};
    }

    void itShowsWhatTheCameraRecorded()
    {
        QFETCH(QString, file);
        QFETCH(QStringList, expected);
        QFETCH(QStringList, absent);

        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.openPath(m_dir.filePath(file));
        QVERIFY(waitForImage(window));
        QTest::keyClick(&window, Qt::Key_I);

        const QString text = panelText(window);
        for (const QString &line : expected)
            QVERIFY2(text.contains(line), qPrintable("missing \"" + line + "\" in:\n" + text));
        for (const QString &line : absent)
            QVERIFY2(!text.contains(line), qPrintable("unexpected \"" + line + "\" in:\n" + text));
    }

    void theLocationIsALinkToAMap()
    {
        // A link, and only a link: the map opens if the user clicks it, and
        // nothing is looked up otherwise.
        MainWindow window;
        window.show();
        window.openPath(m_dir.filePath(QStringLiteral("b_phone.heic")));
        QVERIFY(waitForImage(window));
        const QString rich = window.infoPanel()->richText();
        QVERIFY2(rich.contains(QStringLiteral(
                     "<a href='https://www.openstreetmap.org/?mlat=25.033964&amp;mlon=121.564472")),
                 qPrintable(rich));
    }

    void theNextImageReplacesTheDetails()
    {
        MainWindow window;
        window.show();
        window.openPath(m_dir.filePath(QStringLiteral("a_camera.jpg")));
        QVERIFY(waitForImage(window));
        QVERIFY(panelText(window).contains(QStringLiteral("Canon EOS R5")));

        window.showIndex(window.index() + 2);   // c_plain.png, by name order
        QVERIFY(waitFor([&] { return panelText(window).contains(QStringLiteral("c_plain.png")); }));
        QVERIFY2(!panelText(window).contains(QStringLiteral("Canon")),
                 qPrintable("the PNG's panel still shows the JPEG's camera:\n" + panelText(window)));
    }

    void aFileThatWillNotDecodeStillShowsWhatItIs()
    {
        MainWindow window;
        window.show();
        window.openPath(m_dir.filePath(QStringLiteral("d_broken.jpg")));
        QVERIFY(waitFor([&] { return panelText(window).contains(QStringLiteral("d_broken.jpg")); }));
        QVERIFY(panelText(window).contains(QStringLiteral("Could not be decoded")));
        QVERIFY(!panelText(window).contains(QStringLiteral("Canon")));
    }

    void fullscreenTakesThePanelAndGivesItBack()
    {
        MainWindow window;
        window.show();
        window.openPath(m_dir.filePath(QStringLiteral("a_camera.jpg")));
        QVERIFY(waitForImage(window));

        QTest::keyClick(&window, Qt::Key_I);
        QVERIFY(waitFor([&] { return window.infoDock()->isVisible(); }, 2000));
        window.toggleFullscreen();
        QTest::qWait(100);
        QVERIFY2(!window.infoDock()->isVisible(), "the panel stayed up in fullscreen");
        window.toggleFullscreen();
        QVERIFY2(waitFor([&] { return window.infoDock()->isVisible(); }, 2000),
                 "the panel did not come back after fullscreen");

        // And one that was closed stays closed.
        QTest::keyClick(&window, Qt::Key_I);
        QVERIFY(waitFor([&] { return !window.infoDock()->isVisible(); }, 2000));
        window.toggleFullscreen();
        QTest::qWait(100);
        window.toggleFullscreen();
        QTest::qWait(100);
        QVERIFY(!window.infoDock()->isVisible());
    }
};

// ---------------------------------------------------------------------------

/// What carries over to the next session, and settings that take effect.
class PersistenceTests : public QObject
{
    Q_OBJECT

private slots:
    // Each test starts from no saved state, and leaves none behind for the
    // suites after it, which expect a fresh window.
    void init() { QFile::remove(settings::filePath()); }
    void cleanup() { QFile::remove(settings::filePath()); }

    void theWindowComesBackAsItWasLeft()
    {
        // A size that fits the screen: the offscreen screen is 800x600
        // physical, so at 2x only 400x300 logical, and a saved geometry
        // larger than the screen is -- rightly -- shrunk to fit on restore.
        QSize left;
        {
            MainWindow first;
            const QSize room = first.screen()->availableGeometry().size();
            first.resize(room * 3 / 4);
            first.show();
            QVERIFY(QTest::qWaitForWindowActive(&first));
            QTest::keyClick(&first, Qt::Key_I);
            QVERIFY(waitFor([&] { return first.infoDock()->isVisible(); }, 2000));
            QVERIFY(first.width() < room.width() && first.height() < room.height());
            left = first.size();
            QVERIFY(left != MainWindow().size());   // differs from a fresh window's
            first.close();
        }
        MainWindow second;
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QVERIFY2(second.size() == left,
                 qPrintable(QStringLiteral("left at %1x%2, came back %3x%4")
                                .arg(left.width()).arg(left.height())
                                .arg(second.width()).arg(second.height())));
        QVERIFY2(second.infoDock()->isVisible(), "the info panel was open, and did not come back");
    }

    void fullscreenIsNotWhatGetsRemembered()
    {
        {
            MainWindow first;
            first.resize(640, 480);
            first.show();
            QVERIFY(QTest::qWaitForWindowActive(&first));
            QTest::keyClick(&first, Qt::Key_I);
            QVERIFY(waitFor([&] { return first.infoDock()->isVisible(); }, 2000));
            first.toggleFullscreen();
            QTest::qWait(100);
            first.close();      // closed while fullscreen, panel hidden by it
        }
        MainWindow second;
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QVERIFY(!second.isFullScreen());
        QVERIFY2(second.infoDock()->isVisible(),
                 "closing in fullscreen saved the panel as hidden");
        QVERIFY(second.menuBar()->isVisible());
    }

    void theDialogHandsBackWhatWasSet()
    {
        SettingsDialog dialog(Preferences{});
        dialog.cacheBox()->setValue(2048);
        dialog.prefetchBox()->setValue(1);
        dialog.slideshowBox()->setValue(9);
        QCOMPARE(dialog.preferences().cacheMb, 2048);
        QCOMPARE(dialog.preferences().prefetchRadius, 1);
        QCOMPARE(dialog.preferences().slideshowSeconds, 9);
        // And the limits are the ones the settings file is held to.
        dialog.cacheBox()->setValue(1);
        QCOMPARE(dialog.preferences().cacheMb, Preferences::MinCacheMb);
    }

    void newPreferencesTakeEffectAtOnce()
    {
        MainWindow window;
        window.show();
        window.openPath(samplesDir());
        QVERIFY(waitForImage(window));
        QVERIFY(window.playlist().size() >= 6);

        Preferences p = window.preferences();
        QCOMPARE(window.prefetchTargets().size(), 2 * p.prefetchRadius);
        p.prefetchRadius = 1;
        p.cacheMb = 256;
        window.applyPreferences(p);
        QCOMPARE(window.prefetchTargets().size(), 2);
        QCOMPARE(window.loader()->cache().budget(), qint64(256) * 1024 * 1024);
        p.prefetchRadius = 0;
        window.applyPreferences(p);
        QVERIFY(window.prefetchTargets().isEmpty());
    }
};

// ---------------------------------------------------------------------------

/// F5: a fullscreen slideshow that advances, loops, and stops cleanly.
class SlideshowTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_three;
    QTemporaryDir m_one;

    static bool place(const char *sampleName, const QString &to)
    {
        QFile::remove(to);
        return QFile::copy(sample(sampleName), to);
    }

    /// A window on the three-image folder, active, with a short interval.
    static void startOn(MainWindow &window, const QString &folder)
    {
        window.show();
        window.openPath(folder);
        QVERIFY(waitForImage(window));
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.setSlideshowInterval(80);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_three.isValid() && m_one.isValid());
        QVERIFY(place("sample.png", m_three.filePath(QStringLiteral("a.png"))));
        QVERIFY(place("sample.jpg", m_three.filePath(QStringLiteral("b.jpg"))));
        QVERIFY(place("sample.bmp", m_three.filePath(QStringLiteral("c.bmp"))));
        QVERIFY(place("sample.png", m_one.filePath(QStringLiteral("only.png"))));
    }

    void f5GoesFullscreenAdvancesAndLoops()
    {
        MainWindow window;
        startOn(window, m_three.path());
        const int first = window.index();
        QTest::keyClick(&window, Qt::Key_F5);
        QVERIFY(window.slideshowRunning());
        QVERIFY2(window.isFullScreen(), "a slideshow should take the whole screen");
        QVERIFY2(waitFor([&] { return window.index() != first; }, 5000), "it never advanced");
        QVERIFY2(waitFor([&] { return window.index() == first; }, 5000),
                 "it did not come round to the first image again");
    }

    void escapeStopsItWithoutClosingTheWindow()
    {
        MainWindow window;
        startOn(window, m_three.path());
        QTest::keyClick(&window, Qt::Key_F5);
        QVERIFY(window.slideshowRunning());
        QTest::keyClick(&window, Qt::Key_Escape);
        QVERIFY(!window.slideshowRunning());
        QVERIFY(!window.isFullScreen());
        QVERIFY2(window.isVisible(), "Escape closed the window instead of ending the slideshow");
        const int stoppedAt = window.index();
        QTest::qWait(400);      // five intervals
        QCOMPARE(window.index(), stoppedAt);
    }

    void f5AgainOrF11StopsIt()
    {
        MainWindow window;
        startOn(window, m_three.path());
        QTest::keyClick(&window, Qt::Key_F5);
        QTest::keyClick(&window, Qt::Key_F5);
        QVERIFY(!window.slideshowRunning());
        QVERIFY(!window.isFullScreen());

        QTest::keyClick(&window, Qt::Key_F5);
        QTest::keyClick(&window, Qt::Key_F11);
        QVERIFY2(!window.slideshowRunning(), "leaving fullscreen left the slideshow running");
        QVERIFY(!window.isFullScreen());
    }

    void startedInFullscreenItStaysThere()
    {
        MainWindow window;
        startOn(window, m_three.path());
        window.toggleFullscreen();
        QTest::keyClick(&window, Qt::Key_F5);
        QTest::keyClick(&window, Qt::Key_F5);
        QVERIFY(!window.slideshowRunning());
        QVERIFY2(window.isFullScreen(),
                 "stopping took the window out of a fullscreen the user had chosen");
    }

    void oneImageIsNotASlideshow()
    {
        // Two guards, each checked on its own: the action greys out, and the
        // slot refuses even if reached some other way.
        MainWindow window;
        startOn(window, m_one.path());
        for (QAction *action : window.actions()) {
            if (action->text() == QStringLiteral("Slide&show"))
                QVERIFY2(!action->isEnabled(), "the slideshow action is live with one image");
        }
        window.toggleSlideshow();
        QVERIFY(!window.slideshowRunning());
        QVERIFY(!window.isFullScreen());
    }

    void fullscreenFromMaximizedReturnsToMaximized()
    {
        MainWindow window;
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.isMaximized());
        window.toggleFullscreen();
        QTest::qWait(50);
        window.toggleFullscreen();
        QTest::qWait(50);
        QVERIFY2(window.isMaximized(), "left fullscreen as a normal window, not maximized");
    }
};

// ---------------------------------------------------------------------------

/// Animated GIF and WebP: they play, they stop when left, P pauses them.
///
/// Read off the canvas over time: each fixture frame is one solid colour,
/// so the set of colours seen at the centre says whether frames are landing.
class AnimationTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static QString name(const QColor &c)
    {
        const auto near = [&](int r, int g, int b) {
            return qAbs(c.red() - r) < 40 && qAbs(c.green() - g) < 40 && qAbs(c.blue() - b) < 40;
        };
        if (near(255, 0, 0)) return QStringLiteral("red");
        if (near(0, 255, 0)) return QStringLiteral("green");
        if (near(0, 0, 255)) return QStringLiteral("blue");
        return c.name();
    }

    /// Every colour seen at the centre of the canvas during *ms*.
    static QSet<QString> coloursOver(const MainWindow &window, int ms)
    {
        QSet<QString> seen;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            const QImage shot = window.view()->viewport()->grab().toImage();
            seen << name(shot.pixelColor(shot.width() / 2, shot.height() / 2));
            QTest::qSleep(10);
        }
        return seen;
    }

    static QString fixture(const char *file)
    {
        return QStringLiteral(PICVIEWER_FIXTURES_DIR "/") + QLatin1String(file);
    }

    QString path(const char *file) const { return m_dir.filePath(QLatin1String(file)); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(QFile::copy(fixture("anim.gif"), path("a_anim.gif")));
        QVERIFY(QFile::copy(fixture("anim.webp"), path("b_anim.webp")));
        QVERIFY(QFile::copy(sample("sample.png"), path("c_still.png")));
    }

    void itPlays_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("GIF") << "a_anim.gif";
        QTest::newRow("WebP") << "b_anim.webp";
    }

    void itPlays()
    {
        QFETCH(QString, file);
        MainWindow window;
        window.show();
        window.openPath(m_dir.filePath(file));
        QVERIFY(waitForImage(window));
        QVERIFY2(window.animating(), "an animated file was shown still");
        const QSet<QString> seen = coloursOver(window, 700);
        QVERIFY2(seen.contains(QStringLiteral("red")) && seen.contains(QStringLiteral("green"))
                     && seen.contains(QStringLiteral("blue")),
                 qPrintable("frames seen: " + QStringList(seen.begin(), seen.end()).join(", ")));
    }

    void movingOnStopsIt()
    {
        MainWindow window;
        window.show();
        window.openPath(path("a_anim.gif"));
        QVERIFY(waitForImage(window));
        QVERIFY(window.animating());

        window.showIndex(window.playlist().indexOf(path("c_still.png")) >= 0
                             ? window.playlist().indexOf(path("c_still.png"))
                             : window.index() + 2);
        QVERIFY(waitFor([&] { return window.view()->pixmap().size() == QSize(640, 480); }));
        QVERIFY(!window.animating());
        QTest::qWait(300);
        QVERIFY2(window.view()->pixmap().size() == QSize(640, 480),
                 "a frame of the GIF landed on the next image");
        QCOMPARE(coloursOver(window, 300).size(), 1);
    }

    void pPausesAndResumes()
    {
        MainWindow window;
        window.show();
        window.openPath(path("a_anim.gif"));
        QVERIFY(waitForImage(window));
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QTest::keyClick(&window, Qt::Key_P);
        QCOMPARE(coloursOver(window, 400).size(), 1);
        QTest::keyClick(&window, Qt::Key_P);
        QVERIFY2(coloursOver(window, 500).size() >= 2, "P did not resume it");
    }

    void aTurnHoldsAcrossFrames()
    {
        MainWindow window;
        window.show();
        window.openPath(path("a_anim.gif"));
        QVERIFY(waitForImage(window));
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::keyClick(&window, Qt::Key_R);
        QTest::qWait(400);      // several frames
        QVERIFY2(!window.view()->orientation().isIdentity(), "a new frame undid the turn");
        QCOMPARE(window.view()->sceneRect().size(), QSizeF(32, 48));
    }

    void aStillImageDoesNotAnimate()
    {
        MainWindow window;
        window.show();
        window.openPath(path("c_still.png"));
        QVERIFY(waitForImage(window));
        QVERIFY(!window.animating());
        for (QAction *action : window.actions()) {
            if (action->text() == QStringLiteral("&Pause Animation"))
                QVERIFY(!action->isEnabled());
        }
    }
};

// ---------------------------------------------------------------------------

/// Ctrl+S: the turn on screen written into the JPEG, and nothing lost.
class SaveOrientationTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_count = 0;

    QString writable(const QString &from, const QString &name)
    {
        const QString to = m_dir.filePath(name);
        QFile::remove(to);
        if (!QFile::copy(from, to))
            return {};
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
        return to;
    }

    static QAction *saveAction(MainWindow &window)
    {
        for (QAction *action : window.actions()) {
            if (action->text() == QStringLiteral("Save &Orientation to File"))
                return action;
        }
        return nullptr;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void whatIsShownIsWhatIsSaved_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("expected");      // the tag afterwards
        QTest::newRow("tag already there, turned left")
            << sample("rotated_exif.jpg") << int(Qt::Key_L) << 1;
        // A mirror on top of a turn: the one case where the order the two
        // are composed in changes the answer. The file's own turn first,
        // then the view's mirror, is a transpose (5); the other way round
        // is its opposite (7).
        QTest::newRow("tag already there, mirrored")
            << sample("rotated_exif.jpg") << int(Qt::Key_H) << 5;
        QTest::newRow("EXIF without the tag, turned right")
            << QStringLiteral(PICVIEWER_FIXTURES_DIR "/exif_camera.jpg") << int(Qt::Key_R) << 6;
        QTest::newRow("no EXIF, mirrored")
            << sample("sample.jpg") << int(Qt::Key_H) << 2;
    }

    void whatIsShownIsWhatIsSaved()
    {
        QFETCH(QString, source);
        QFETCH(int, key);
        QFETCH(int, expected);
        const QString path = writable(source, QStringLiteral("turn_%1.jpg").arg(++m_count));
        QVERIFY(!path.isEmpty());

        MainWindow window;
        window.show();
        window.openPath(path);
        QVERIFY(waitForImage(window));
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::keyClick(&window, static_cast<Qt::Key>(key));
        const QImage shown = window.view()->displayedPixmap().toImage().convertToFormat(QImage::Format_RGB32);
        QVERIFY(saveAction(window)->isEnabled());

        QString error;
        QVERIFY2(window.writeOrientationToFile(&error), qPrintable(error));
        QCOMPARE(jpegorient::readOrientation(path), expected);

        // Read back from the file, upright in the view, and the same pixels.
        QVERIFY(waitFor([&] {
            return window.view()->orientation().isIdentity() && window.view()->hasImage()
                   && window.view()->pixmap().size() == shown.size();
        }));
        QVERIFY(waitForImage(window));
        const QImage reloaded = window.view()->pixmap().toImage().convertToFormat(QImage::Format_RGB32);
        QVERIFY2(reloaded == shown, "the file reads back differently from what was on screen");
        QVERIFY(!window.statusText().contains(QStringLiteral("rotated")));
        QVERIFY2(!saveAction(window)->isEnabled(), "nothing left to save, but the command is live");
    }

    void onlyATurnedJpegCanBeSaved()
    {
        const QString jpeg = writable(sample("sample.jpg"), QStringLiteral("plain.jpg"));
        MainWindow window;
        window.show();
        window.openPath(jpeg);
        QVERIFY(waitForImage(window));
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY2(!saveAction(window)->isEnabled(), "offered with nothing turned");
        QTest::keyClick(&window, Qt::Key_R);
        QVERIFY(saveAction(window)->isEnabled());

        const QString png = writable(sample("sample.png"), QStringLiteral("plain.png"));
        MainWindow other;
        other.show();
        other.openPath(png);
        QVERIFY(waitForImage(other));
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QTest::keyClick(&other, Qt::Key_R);
        QVERIFY2(!saveAction(other)->isEnabled(), "offered for a PNG, which has no lossless way");
    }
};

// ---------------------------------------------------------------------------

namespace {

int runSuite(QObject *suite, const QString &name, int &passed, int &failed)
{
    QTemporaryFile report;
    if (!report.open()) {
        std::fprintf(stderr, "no report file for %s\n", qPrintable(name));
        return 1;
    }
    const QString path = report.fileName();
    report.close();

    const int result = QTest::qExec(
        suite, QStringList{QCoreApplication::applicationFilePath(),
                           QStringLiteral("-o"), path + QStringLiteral(",txt")});

    QFile written(path);
    if (written.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(written.readAll());
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("FAIL!")) || line.contains(QLatin1String("Totals:")))
                std::printf("  %s\n", qPrintable(line.trimmed()));
        }
        static const QRegularExpression totals(
            QStringLiteral("Totals: (\\d+) passed, (\\d+) failed"));
        if (const auto match = totals.match(text); match.hasMatch()) {
            passed += match.captured(1).toInt();
            failed += match.captured(2).toInt();
        }
    }
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    // --scale N runs every suite as though on a display scaled by N. It has
    // to be in the environment before the application object exists, which
    // is why it is read from argv by hand rather than through QTest.
    QByteArray scale = "1";
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--scale") == 0)
            scale = argv[i + 1];
    }
    qputenv("QT_SCALE_FACTOR", scale);
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);       // widgets, so QApplication not QGuiApplication
    applog::install(false);
    qInfo().noquote() << startupMarker();

    // Every window here reads and writes settings; none of it may touch the
    // settings of whoever runs the tests.
    QTemporaryDir settingsDir;
    settings::setFilePath(settingsDir.filePath(QStringLiteral("settings.ini")));
    std::printf("display scale %s\n\n", scale.constData());

    DropRoutingTests dropRouting;
    DragDropTests dragDrop;
    NavigationTests navigation;
    ShortcutTests shortcuts;
    ChromeTests chrome;
    FailureTests failures;
    HighDpiTests highDpi;
    OrientationTests orientation;
    InfoPanelTests infoPanel;
    PersistenceTests persistence;
    SlideshowTests slideshow;
    AnimationTests animation;
    SaveOrientationTests saveOrientation;

    const QList<QPair<QObject *, QString>> suites = {
        {&dropRouting, QStringLiteral("drop-routing")},
        {&dragDrop, QStringLiteral("dragdrop")},
        {&navigation, QStringLiteral("navigation")},
        {&shortcuts, QStringLiteral("shortcuts")},
        {&chrome, QStringLiteral("chrome")},
        {&failures, QStringLiteral("failures")},
        {&highDpi, QStringLiteral("high-dpi")},
        {&orientation, QStringLiteral("orientation")},
        {&infoPanel, QStringLiteral("info-panel")},
        {&persistence, QStringLiteral("persistence")},
        {&slideshow, QStringLiteral("slideshow")},
        {&animation, QStringLiteral("animation")},
        {&saveOrientation, QStringLiteral("save-orientation")},
    };

    int exitCode = 0;
    int passed = 0;
    int failedCount = 0;
    for (const auto &[suite, name] : suites) {
        std::printf("== %s ==\n", qPrintable(name));
        exitCode += runSuite(suite, name, passed, failedCount);
    }

    std::printf("\n%d passed, %d failed across %lld suites\n",
                passed, failedCount, static_cast<long long>(suites.size()));
    applog::shutdown();
    return exitCode;
}

#include "functional_tests.moc"
