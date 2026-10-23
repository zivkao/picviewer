// Unit tests over the pure logic, ported from the Python suite.
//
// Same split as before: assertions here, end-to-end walkthroughs elsewhere.
// Every defect found while building the Python version is a regression case,
// because the C++ port can reintroduce any of them independently.

#include <QtTest>

#include <QRegularExpression>
#include <QTemporaryFile>

#include <cstdio>

#include "cache.h"
#include "decoders/qt_decoder.h"
#include "decoders/raw_decoder.h"
#include "decoders/registry.h"
#include "sniff.h"

using sniff::Container;

// ---------------------------------------------------------------------------

class SniffTests : public QObject
{
    Q_OBJECT

private slots:
    void headerIdentifiesContainer_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::addColumn<int>("expected");

        auto row = [](const char *name, QByteArray header, Container expected) {
            QTest::newRow(name) << header << static_cast<int>(expected);
        };
        row("jpeg", QByteArray("\xff\xd8\xff\xe0\x00\x10JFIF", 11), Container::Jpeg);
        row("png",  QByteArray("\x89PNG\r\n\x1a\n\x00\x00\x00\r", 12), Container::Png);
        row("gif",  QByteArray("GIF89a\x40\x01\xf0\x00", 10), Container::Gif);
        row("bmp",  QByteArray("BM\x36\x00\x0c\x00", 6), Container::Bmp);
        row("psd",  QByteArray("8BPS\x00\x01\x00\x00", 8), Container::Psd);
        row("ico",  QByteArray("\x00\x00\x01\x00\x01\x00", 6), Container::Ico);
        row("raf",  QByteArray("FUJIFILMCCD-RAW 0201", 20), Container::Raf);
        row("x3f",  QByteArray("FOVb\x00\x00\x02\x00", 8), Container::X3f);
        row("rw2",  QByteArray("IIU\x00\x18\x00\x00\x00", 8), Container::Rw2);
        row("orf",  QByteArray("IIRO\x08\x00\x00\x00", 8), Container::Orf);
        row("tiff", QByteArray("II*\x00\x08\x00\x00\x00", 8), Container::Tiff);
        row("tiff_be", QByteArray("MM\x00*\x00\x00\x00\x08", 8), Container::Tiff);
        row("webp", QByteArray("RIFF\x24\x0c\x00\x00WEBPVP8 ", 16), Container::Webp);
        row("heif", QByteArray("\x00\x00\x00\x18" "ftypheic" "\x00\x00\x00\x00", 20), Container::Heif);
        row("avif", QByteArray("\x00\x00\x00\x1c" "ftypavif" "\x00\x00\x00\x00", 20), Container::Avif);
        row("cr3",  QByteArray("\x00\x00\x00\x18" "ftypcrx " "\x00\x00\x00\x00", 20), Container::Cr3);
        row("jp2",  QByteArray("\x00\x00\x00\x0cjP  \r\n\x87\n", 12), Container::Jp2);
        row("pnm",  QByteArray("P6\n640 480\n255\n", 15), Container::Pnm);
    }

    void headerIdentifiesContainer()
    {
        QFETCH(QByteArray, header);
        QFETCH(int, expected);
        QCOMPARE(static_cast<int>(sniff::of("whatever.bin", header)), expected);
    }

    void magicBytesBeatALyingExtension()
    {
        // A PNG named .jpg is a PNG. Extensions are a hint, not evidence.
        const QByteArray png("\x89PNG\r\n\x1a\n\x00\x00\x00\r", 12);
        QCOMPARE(sniff::of("photo.jpg", png), Container::Png);

        const QByteArray jpeg("\xff\xd8\xff\xe0\x00\x10JFIF", 11);
        QCOMPARE(sniff::of("photo.png", jpeg), Container::Jpeg);
    }

    void extensionIsTheFallbackWhenTheHeaderSaysNothing()
    {
        const QByteArray noise(32, '\0');
        QCOMPARE(sniff::of("a.jpg", noise), Container::Jpeg);
        QCOMPARE(sniff::of("a.HEIF", noise), Container::Heif);
        QCOMPARE(sniff::of("a.cr3", noise), Container::Cr3);
        QCOMPARE(sniff::of("a.ppm", noise), Container::Pnm);
    }

    void unknownWhenNeitherHeaderNorExtensionHelps()
    {
        QCOMPARE(sniff::of("mystery.xyz", QByteArray(32, '\0')), Container::Unknown);
    }

    void anEmptyHeaderFallsBackToTheExtension()
    {
        // An unreadable file yields no header; the suffix is all that is left.
        QCOMPARE(sniff::of("photo.heic", QByteArray()), Container::Heif);
    }

    void aTruncatedHeaderDoesNotCrash()
    {
        const QByteArray full("\xff\xd8\xff", 3);
        for (int size = 0; size <= full.size(); ++size)
            sniff::of("a.bin", full.left(size));   // must simply not crash
        QVERIFY(true);
    }

    void everyHeifBrandMapsToHeif()
    {
        for (const char *brand : {"heic", "heix", "heim", "heis", "hevc", "mif1", "msf1"}) {
            QByteArray header("\x00\x00\x00\x18" "ftyp", 8);
            header.append(brand, 4);
            header.append(8, '\0');
            QCOMPARE(sniff::of("x.bin", header), Container::Heif);
        }
    }

    void rw2AndOrfAreNotReadAsPlainTiff()
    {
        // Both are TIFF-shaped but use their own magic in place of the 42.
        QCOMPARE(sniff::of("x.bin", QByteArray("IIU\x00\x18\x00\x00\x00", 8)), Container::Rw2);
        QCOMPARE(sniff::of("x.bin", QByteArray("IIRO\x08\x00\x00\x00", 8)), Container::Orf);
    }

    void readHeaderOnAMissingFileReturnsEmpty()
    {
        QVERIFY(sniff::readHeader("does/not/exist.jpg").isEmpty());
    }
};

// ---------------------------------------------------------------------------

class ScoringTests : public QObject
{
    Q_OBJECT

    // Where the worst bug in the Python version lived: a TIFF-based RAW
    // scored 100 from both backends, the tie went to registration order, the
    // general-purpose decoder won, and it "succeeded" by returning the small
    // embedded preview from IFD0 instead of the photograph.
    static QString topDecoder(const QString &name, Container container)
    {
        const auto ranked = decoders::Registry::instance().candidates(name, container);
        return ranked.empty() ? QString() : ranked.front()->name();
    }

private slots:
    void tiffBasedRawGoesToTheRawDecoder_data()
    {
        QTest::addColumn<QString>("filename");
        for (const char *name : {"photo.dng", "DSC_0001.nef", "DSC00001.arw",
                                 "IMG_0001.cr2", "P1000001.rw2", "_DSC0001.pef",
                                 "SAM_0001.srw", "IMG_0001.orf", "L1000001.rwl",
                                 "0001.3fr", "0001.iiq", "0001.mef", "0001.erf",
                                 "0001.kdc", "0001.dcr", "0001.nrw", "0001.sr2",
                                 "0001.srf", "0001.mos", "0001.fff"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }

    void tiffBasedRawGoesToTheRawDecoder()
    {
        QFETCH(QString, filename);
        QCOMPARE(topDecoder(filename, Container::Tiff), QStringLiteral("raw"));
    }

    void aGenuineTiffStillGoesToQt()
    {
        QCOMPARE(topDecoder("scan.tif", Container::Tiff), QStringLiteral("qt"));
        QCOMPARE(topDecoder("scan.tiff", Container::Tiff), QStringLiteral("qt"));
        QCOMPARE(topDecoder("SCAN.TIF", Container::Tiff), QStringLiteral("qt"));
    }

    void aTiffContainerWithAnUnknownExtensionPrefersRaw()
    {
        // Almost nothing but a RAW ships TIFF magic under a foreign suffix.
        QCOMPARE(topDecoder("mystery.xyz", Container::Tiff), QStringLiteral("raw"));
    }

    void theAmbiguousCasesNeverTieAtTheTop_data()
    {
        QTest::addColumn<QString>("filename");
        for (const char *name : {"photo.dng", "DSC_0001.nef", "scan.tif", "mystery.xyz"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }

    void theAmbiguousCasesNeverTieAtTheTop()
    {
        // A tie is resolved by registration order, which is how the bug got in.
        QFETCH(QString, filename);
        const auto ranked = decoders::Registry::instance().candidates(filename, Container::Tiff);
        QVERIFY2(ranked.size() >= 2, "expected both the qt and raw decoders as candidates");
        const int best = ranked[0]->score(filename, Container::Tiff);
        const int runnerUp = ranked[1]->score(filename, Container::Tiff);
        QVERIFY2(best > runnerUp, qPrintable(filename + " ties at " + QString::number(best)));
    }

    void unambiguousFormatsReachTheirDecoder_data()
    {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<int>("container");
        QTest::addColumn<QString>("expected");

        auto row = [](const char *file, Container c, const char *decoder) {
            QTest::newRow(file) << QString::fromLatin1(file)
                                << static_cast<int>(c) << QString::fromLatin1(decoder);
        };
        row("a.jpg", Container::Jpeg, "qt");
        row("a.png", Container::Png, "qt");
        row("a.gif", Container::Gif, "qt");
        row("a.bmp", Container::Bmp, "qt");
        row("a.webp", Container::Webp, "qt");
        row("a.ico", Container::Ico, "qt");
        row("a.heic", Container::Heif, "heif");
        row("a.avif", Container::Avif, "heif");
        row("a.cr3", Container::Cr3, "raw");
        row("a.rw2", Container::Rw2, "raw");
        row("a.raf", Container::Raf, "raw");
        row("a.orf", Container::Orf, "raw");
        row("a.x3f", Container::X3f, "raw");
    }

    void unambiguousFormatsReachTheirDecoder()
    {
        QFETCH(QString, filename);
        QFETCH(int, container);
        QFETCH(QString, expected);
        QCOMPARE(topDecoder(filename, static_cast<Container>(container)), expected);
    }

    void aLyingExtensionDoesNotRedirectAClearContainer()
    {
        // PNG bytes named .heic are still PNG bytes.
        QCOMPARE(topDecoder("photo.heic", Container::Png), QStringLiteral("qt"));
    }

    void rawDeclinesFormatsItCannotRead()
    {
        const decoders::RawDecoder raw;
        for (Container c : {Container::Jpeg, Container::Png, Container::Gif,
                            Container::Webp, Container::Heif})
            QCOMPARE(raw.score("a.bin", c), 0);
    }

    void qtOffersItselfLastForAnUnrecognisedFile()
    {
        const decoders::QtDecoder qt;
        const int fallback = qt.score("mystery.xyz", Container::Unknown);
        QVERIFY(fallback > 0);
        QVERIFY(fallback < qt.score("a.jpg", Container::Jpeg));
    }

    void candidatesAreSortedByDescendingConfidence()
    {
        const auto ranked = decoders::Registry::instance().candidates("photo.dng", Container::Tiff);
        for (std::size_t i = 1; i < ranked.size(); ++i) {
            QVERIFY(ranked[i - 1]->score("photo.dng", Container::Tiff)
                    >= ranked[i]->score("photo.dng", Container::Tiff));
        }
    }

    void caseOfTheExtensionDoesNotMatter()
    {
        QCOMPARE(topDecoder("PHOTO.DNG", Container::Tiff),
                 topDecoder("photo.dng", Container::Tiff));
        QCOMPARE(topDecoder("PHOTO.TIF", Container::Tiff),
                 topDecoder("photo.tif", Container::Tiff));
    }
};

// ---------------------------------------------------------------------------

class CacheTests : public QObject
{
    Q_OBJECT

    static decoders::DecodedImage make(int width, int height)
    {
        decoders::DecodedImage item;
        item.image = QImage(width, height, QImage::Format_RGB888);
        item.image.fill(0);
        return item;
    }

    static CacheKey key(const char *name) { return {QString::fromLatin1(name), 1, 1}; }

private slots:
    void nbytesTracksThePixelBufferNotTheEntryCount()
    {
        QVERIFY(make(1000, 1000).nbytes() > make(10, 10).nbytes() * 1000);
    }

    void getReturnsWhatWasPut()
    {
        ImageCache cache(10'000'000);
        cache.put(key("a"), make(100, 100));
        const auto got = cache.get(key("a"));
        QVERIFY(got.has_value());
        QCOMPARE(got->image.size(), QSize(100, 100));
    }

    void missingKeyReturnsNothing()
    {
        ImageCache cache;
        QVERIFY(!cache.get(key("nothing")).has_value());
    }

    void evictionIsByBytesNotByCount()
    {
        // A hundred thumbnails and a hundred huge images are the same count.
        const qint64 one = make(500, 500).nbytes();
        ImageCache cache(one * 2 + 100);

        cache.put(key("a"), make(500, 500));
        cache.put(key("b"), make(500, 500));
        QVERIFY(cache.get(key("a")).has_value());
        QVERIFY(cache.get(key("b")).has_value());

        cache.put(key("c"), make(500, 500));
        QVERIFY2(!cache.get(key("a")).has_value(), "oldest should have been evicted");
        QVERIFY(cache.get(key("c")).has_value());
    }

    void manyTinyEntriesSurviveWhereFewLargeOnesWouldNot()
    {
        ImageCache cache(make(4, 4).nbytes() * 50);
        for (int i = 0; i < 40; ++i)
            cache.put({QString::number(i), 1, 1}, make(4, 4));
        int alive = 0;
        for (int i = 0; i < 40; ++i)
            alive += cache.get({QString::number(i), 1, 1}).has_value() ? 1 : 0;
        QCOMPARE(alive, 40);
    }

    void aGetRefreshesRecency()
    {
        const qint64 one = make(300, 300).nbytes();
        ImageCache cache(one * 2 + 100);
        cache.put(key("a"), make(300, 300));
        cache.put(key("b"), make(300, 300));

        cache.get(key("a"));                    // a is now most recently used
        cache.put(key("c"), make(300, 300));

        QVERIFY(cache.get(key("a")).has_value());
        QVERIFY2(!cache.get(key("b")).has_value(), "b was least recently used");
    }

    void theNewestEntrySurvivesEvenWhenItAloneExceedsBudget()
    {
        // Evicting it would mean never being able to show that image at all.
        ImageCache cache(1000);
        cache.put(key("big"), make(800, 800));
        QVERIFY(cache.get(key("big")).has_value());
        QVERIFY(cache.nbytes() > cache.budget());
    }

    void replacingAKeyDoesNotDoubleCountItsBytes()
    {
        ImageCache cache(10'000'000);
        cache.put(key("a"), make(200, 200));
        const qint64 afterFirst = cache.nbytes();
        cache.put(key("a"), make(200, 200));
        QCOMPARE(cache.nbytes(), afterFirst);
    }

    void clearResetsEverything()
    {
        ImageCache cache;
        cache.put(key("a"), make(100, 100));
        cache.markFailed(key("b"));
        cache.clear();
        QCOMPARE(cache.nbytes(), 0);
        QVERIFY(!cache.get(key("a")).has_value());
        QVERIFY(!cache.hasFailed(key("b")));
    }

    void aFailureIsRemembered()
    {
        ImageCache cache;
        QVERIFY(!cache.hasFailed(key("x")));
        cache.markFailed(key("x"));
        QVERIFY(cache.hasFailed(key("x")));
    }

    void failuresDoNotGrowWithoutBound()
    {
        ImageCache cache;
        for (std::size_t i = 0; i < ImageCache::MaxFailed + 50; ++i)
            cache.markFailed({QString::number(i), 1, 1});
        QVERIFY2(!cache.hasFailed({QStringLiteral("0"), 1, 1}),
                 "oldest failures should be dropped");
        QVERIFY(cache.hasFailed({QString::number(ImageCache::MaxFailed + 49), 1, 1}));
    }

    void keyChangesWhenTheFileChanges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("f.bin");

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("one");
        file.close();
        const CacheKey first = CacheKey::of(path);

        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("two but longer");
        file.close();

        // mtime and size are in the key, so an edited file is not served stale
        // -- and a file repaired after a failure is retried on its own.
        QVERIFY(!(CacheKey::of(path) == first));
    }

    void keyOfAMissingFileDoesNotCrash()
    {
        const CacheKey key = CacheKey::of("no/such/file.jpg");
        QCOMPARE(key.mtime, 0);
    }
};

// ---------------------------------------------------------------------------

/// Format-level behaviour that only shows up against a real file.
class DecodeTests : public QObject
{
    Q_OBJECT

    static QString sample(const char *name)
    {
        return QStringLiteral(PICVIEWER_SAMPLES_DIR "/") + QLatin1String(name);
    }

    static std::optional<decoders::DecodedImage> load(const char *name)
    {
        decoders::DecodeError error;
        return decoders::Registry::instance().loadFull(sample(name), QSize(), &error);
    }

private slots:
    void initTestCase()
    {
        if (!QFileInfo::exists(sample("sample.jpg")))
            QSKIP("sample files are not present next to the source tree");
    }

    void anIcoShowsItsLargestFrameNotItsFirst()
    {
        // Qt starts on the first directory entry, which is usually the
        // smallest: a 16x16 icon out of a file that also holds 256x256.
        const auto result = load("sample.ico");
        QVERIFY(result.has_value());
        QCOMPARE(result->image.size(), QSize(256, 256));
    }

    void exifRotationIsApplied()
    {
        // Stored 600x400 with orientation 6; it must come back transposed.
        const auto result = load("rotated_exif.jpg");
        QVERIFY(result.has_value());
        QCOMPARE(result->image.size(), QSize(400, 600));
        QCOMPARE(result->metadata.value(QStringLiteral("size")),
                 QStringLiteral("400 x 600"));
    }

    void magicBytesBeatTheExtensionOnARealFile()
    {
        // actually_png.jpg is PNG data under a .jpg name.
        const auto result = load("actually_png.jpg");
        QVERIFY(result.has_value());
        QCOMPARE(result->metadata.value(QStringLiteral("format")), QStringLiteral("PNG"));
    }

    void aTiffBasedRawReachesTheRawDecoder()
    {
        // The whole point of the scoring tie-break: decoded by "qt" this
        // would succeed and return the 200x150 embedded preview.
        const auto result = load("synthetic.dng");
        QVERIFY(result.has_value());
        QCOMPARE(result->metadata.value(QStringLiteral("decoder")), QStringLiteral("raw"));
        QCOMPARE(result->image.size(), QSize(800, 600));
    }

    void rawOffersAnEmbeddedPreviewBeforeTheFullDecode()
    {
        const auto preview = decoders::Registry::instance().loadPreview(
            sample("synthetic.dng"), QSize(3840, 3840));
        QVERIFY(preview.has_value());
        QVERIFY(preview->isPreview);
        QCOMPARE(preview->image.size(), QSize(200, 150));
    }

    void heifAndAvifBothReachTheHeifDecoder()
    {
        for (const char *name : {"sample.heic", "sample.avif"}) {
            const auto result = load(name);
            QVERIFY2(result.has_value(), name);
            QCOMPARE(result->metadata.value(QStringLiteral("decoder")),
                     QStringLiteral("heif"));
        }
    }

    void anUnreadableFileIsReportedAsUnreadableNotUnsupported()
    {
        decoders::DecodeError error;
        const auto result = decoders::Registry::instance().loadFull(
            sample("no_such_file.jpg"), QSize(), &error);
        QVERIFY(!result.has_value());
        QCOMPARE(error.kind, decoders::DecodeError::Kind::Unreadable);
    }
};

// ---------------------------------------------------------------------------

namespace {

/// Run one suite and return its report.
///
/// Each suite gets its own file rather than the default stdout logger.
/// QTest tears that logger down when a qExec returns, so with three suites in
/// one binary a plain run printed nothing at all -- the tests ran, passed, and
/// said so into a closed stream. Reading the reports back and printing them
/// here is independent of that lifecycle.
int runSuite(QObject *suite, const QString &name, int &passed, int &failed)
{
    QTemporaryFile report;
    if (!report.open()) {
        std::fprintf(stderr, "could not create a report file for %s\n", qPrintable(name));
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
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);     // QImage needs no app, QImageReader does

    SniffTests sniffTests;
    ScoringTests scoringTests;
    CacheTests cacheTests;
    DecodeTests decodeTests;

    const QList<QPair<QObject *, QString>> suites = {
        {&sniffTests, QStringLiteral("sniff")},
        {&scoringTests, QStringLiteral("scoring")},
        {&cacheTests, QStringLiteral("cache")},
        {&decodeTests, QStringLiteral("decode")},
    };

    int failures = 0;
    int passed = 0;
    int failed = 0;
    for (const auto &[suite, name] : suites) {
        std::printf("== %s ==\n", qPrintable(name));
        failures += runSuite(suite, name, passed, failed);
    }

    std::printf("\n%d passed, %d failed across %lld suites\n",
                passed, failed, static_cast<long long>(suites.size()));
    return failures;
}

#include "unit_tests.moc"
