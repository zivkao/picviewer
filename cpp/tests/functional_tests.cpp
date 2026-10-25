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

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMenuBar>
#include <QMimeData>
#include <QProcess>
#include <QStatusBar>
#include <QToolBar>

#include "applog.h"
#include "viewer.h"
#include "window.h"

namespace {

QString samplesDir() { return QStringLiteral(PICVIEWER_SAMPLES_DIR); }
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

        // Nothing open: navigation, deletion and zoom have nothing to act on.
        const QStringList shouldBeOff = {"&Previous", "&Next", "&Delete",
                                         "&Fit to Window", "Show in E&xplorer"};
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
        const QFileInfo log(applog::filePath());
        QVERIFY2(log.exists(), qPrintable("no log at " + log.filePath()));
        QVERIFY(log.size() > 0);
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
    std::printf("display scale %s\n\n", scale.constData());

    DropRoutingTests dropRouting;
    DragDropTests dragDrop;
    NavigationTests navigation;
    ShortcutTests shortcuts;
    ChromeTests chrome;
    FailureTests failures;
    HighDpiTests highDpi;

    const QList<QPair<QObject *, QString>> suites = {
        {&dropRouting, QStringLiteral("drop-routing")},
        {&dragDrop, QStringLiteral("dragdrop")},
        {&navigation, QStringLiteral("navigation")},
        {&shortcuts, QStringLiteral("shortcuts")},
        {&chrome, QStringLiteral("chrome")},
        {&failures, QStringLiteral("failures")},
        {&highDpi, QStringLiteral("high-dpi")},
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
