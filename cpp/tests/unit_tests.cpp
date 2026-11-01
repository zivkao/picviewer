// Unit tests over the pure logic, ported from the Python suite.
//
// Same split as before: assertions here, end-to-end walkthroughs elsewhere.
// Every defect found while building the Python version is a regression case,
// because the C++ port can reintroduce any of them independently.

#include <QtTest>

#include <QBuffer>
#include <QImageReader>
#include <QRegularExpression>
#include <QtEndian>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <cstdio>

#include "cache.h"
#include "decoders/qt_decoder.h"
#include "decoders/raw_decoder.h"
#include "decoders/registry.h"
#include "loader.h"
#include "jpegorient.h"
#include "photoinfo.h"
#include "settings.h"
#include "thumbnails.h"
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

    void loweringTheBudgetEvictsAtOnce()
    {
        // The setting takes effect when it is changed, not at the next
        // insert: a user who lowers it to free memory expects it freed.
        ImageCache cache(100'000'000);
        cache.put(key("a"), make(1000, 1000));     // 3 MB each
        cache.put(key("b"), make(1000, 1000));
        cache.put(key("c"), make(1000, 1000));
        QCOMPARE(cache.nbytes(), qint64(9'000'000));
        cache.setBudget(4'000'000);
        QCOMPARE(cache.nbytes(), qint64(3'000'000));
        QVERIFY(!cache.get(key("a")));
        QVERIFY(cache.get(key("c")));             // the newest stays
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

    void qtsAllocationLimitCoversTheLoadersOwnCap()
    {
        // Qt refuses an image whose pixels exceed its allocation limit before
        // the loader's MaxDecodePixels is ever consulted, so if the two
        // disagree the smaller silently decides and the larger is decoration.
        // This runs always; the decode below needs a large file to be present.
        const qint64 capBytes = qint64(MaxDecodePixels) * 4;    // Qt decodes to RGB32
        const qint64 limitBytes = qint64(decoders::AllocationLimitMb) * 1024 * 1024;
        QVERIFY2(limitBytes >= capBytes,
                 qPrintable(QStringLiteral("Qt would refuse at %1 MB, below the "
                                           "loader's own cap of %2 MB")
                                .arg(limitBytes / 1048576).arg(capBytes / 1048576)));
    }

    void anImagePastQtsDefaultLimitStillDecodes()
    {
        // Qt declines anything over 256 MiB of pixels by default. The first
        // real photograph this project ever saw -- 101 megapixels, 305 MB --
        // failed on exactly that, and the limit is now raised to match the
        // loader's own cap.
        //
        // The test for it used to read that photograph, which is untracked;
        // when it was removed the test silently became a skip and a mutation
        // putting the default back survived. The fixture is committed instead:
        // 249 KiB on disk, 309 MiB once decoded to RGB32.
        const QString path = QStringLiteral(PICVIEWER_FIXTURES_DIR "/large_9000x9000.png");
        QVERIFY2(QFileInfo::exists(path),
                 "fixture missing; regenerate with cpp/tests/fixtures/make_large.py");

        decoders::DecodeError error;
        const auto result = decoders::Registry::instance().loadFull(path, QSize(), &error);
        QVERIFY2(result.has_value(), qPrintable(error.message));
        QCOMPARE(result->image.size(), QSize(9000, 9000));
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

// ---------------------------------------------------------------------------

/// Thumbnails: made small, kept on disk under a key that changes with the
/// file, and pruned oldest first.
class ThumbnailTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_cache;
    QTemporaryDir m_files;

    static QString sampleFile(const char *name)
    {
        return QStringLiteral(PICVIEWER_SAMPLES_DIR "/") + QLatin1String(name);
    }
    QString copied(const char *name, const QString &as)
    {
        const QString to = m_files.filePath(as);
        QFile::remove(to);
        QFile::copy(sampleFile(name), to);
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
        return to;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_cache.isValid() && m_files.isValid());
        thumbnails::setDirectory(m_cache.path());
    }
    void cleanupTestCase() { thumbnails::setDirectory(QString()); }

    void aThumbnailIsSmallAndKeepsItsShape()
    {
        const QImage thumb = thumbnails::make(sampleFile("sample.jpg"));     // 640x480
        QCOMPARE(thumb.size(), QSize(thumbnails::Size, thumbnails::Size * 3 / 4));
    }

    void aRawGivesOneToo()
    {
        const QImage thumb = thumbnails::make(sampleFile("synthetic.dng"));
        QVERIFY(!thumb.isNull());
        QVERIFY(thumb.width() <= thumbnails::Size && thumb.height() <= thumbnails::Size);
    }

    void whatWillNotDecodeGivesNothing()
    {
        QVERIFY(thumbnails::make(m_files.filePath(QStringLiteral("absent.jpg"))).isNull());
        QFile junk(m_files.filePath(QStringLiteral("junk.jpg")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not a picture");
        junk.close();
        QVERIFY(thumbnails::make(junk.fileName()).isNull());
    }

    void transparencyLandsOnTheStripsGrey()
    {
        QImage clear(50, 50, QImage::Format_ARGB32);
        clear.fill(Qt::transparent);
        const QString path = m_files.filePath(QStringLiteral("clear.png"));
        QVERIFY(clear.save(path));
        const QImage thumb = thumbnails::make(path);
        QCOMPARE(thumb.pixelColor(10, 10), QColor(58, 60, 64));
    }

    void whatIsStoredIsFound()
    {
        const QString path = copied("sample.jpg", QStringLiteral("stored.jpg"));
        QVERIFY(thumbnails::loadCached(path).isNull());
        thumbnails::store(path, thumbnails::make(path));
        QVERIFY(QFileInfo::exists(thumbnails::cacheFile(path)));
        QCOMPARE(thumbnails::loadCached(path).size(), QSize(192, 144));
    }

    void anEditedFileMissesTheCache()
    {
        // Same name, same size, a new modified time: a photograph edited in
        // place. The old thumbnail must not be shown for it.
        const QString path = copied("sample.jpg", QStringLiteral("edited.jpg"));
        thumbnails::store(path, thumbnails::make(path));
        QVERIFY(!thumbnails::loadCached(path).isNull());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(3600),
                                 QFileDevice::FileModificationTime));
        file.close();
        QVERIFY2(thumbnails::loadCached(path).isNull(), "an edited file found its old thumbnail");
    }

    void pruningTakesTheLongestUnseenFirst()
    {
        QTemporaryDir dir;
        thumbnails::setDirectory(dir.path());
        const QDateTime now = QDateTime::currentDateTime();
        const QStringList names = {"old.jpg", "middle.jpg", "new.jpg"};
        for (int i = 0; i < names.size(); ++i) {
            QFile f(dir.filePath(names.at(i)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(1000, 'x'));
            QVERIFY(f.setFileTime(now.addDays(i - 3), QFileDevice::FileModificationTime));
        }
        thumbnails::prune(2000);
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("old.jpg"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("middle.jpg"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("new.jpg"))));
        thumbnails::setDirectory(m_cache.path());
    }
};

// ---------------------------------------------------------------------------

/// Saving a turn into a JPEG's orientation tag, without touching its pixels.
///
/// Three samples take the three paths: rotated_exif.jpg has the tag (two
/// bytes patched), exif_camera.jpg has EXIF without it (the block
/// rewritten), sample.jpg has no EXIF at all (a block inserted).
class JpegOrientTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static QString sampleFile(const char *name)
    {
        return QStringLiteral(PICVIEWER_SAMPLES_DIR "/") + QLatin1String(name);
    }
    static QString fixtureFile(const char *name)
    {
        return QStringLiteral(PICVIEWER_FIXTURES_DIR "/") + QLatin1String(name);
    }
    static QImage decoded(const QByteArray &jpeg)
    {
        QByteArray copy = jpeg;
        QBuffer buffer(&copy);
        QImageReader reader(&buffer, "jpeg");
        reader.setAutoTransform(true);
        return reader.read();
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void everyOrientationMapsToItselfAndBack()
    {
        for (int o = 1; o <= 8; ++o)
            QCOMPARE(jpegorient::orientationFor(jpegorient::transformFor(o)), o);
        QCOMPARE(jpegorient::orientationFor(QTransform().rotate(45)), 0);
    }

    void theyMatchWhatQtDoesWithTheTag()
    {
        // The same pixels under each tag, decoded by Qt, must come out as
        // the transform says -- otherwise "save what is shown" saves
        // something else.
        const QByteArray original = read(sampleFile("sample.jpg"));
        const QImage upright = decoded(original);
        for (int o = 1; o <= 8; ++o) {
            const QByteArray tagged = jpegorient::withOrientation(original, o);
            QVERIFY2(!tagged.isEmpty(), qPrintable(QStringLiteral("orientation %1").arg(o)));
            const QImage expected = upright.transformed(jpegorient::transformFor(o));
            QCOMPARE(decoded(tagged).size(), expected.size());
            QCOMPARE(decoded(tagged).pixel(5, 5), expected.pixel(5, 5));
        }
    }

    void aTagAlreadyThereIsPatchedInPlace()
    {
        const QByteArray before = read(sampleFile("rotated_exif.jpg"));
        QCOMPARE(jpegorient::readOrientation(before), 6);
        jpegorient::Method how = jpegorient::Method::Inserted;
        const QByteArray after = jpegorient::withOrientation(before, 1, &how);
        QVERIFY(how == jpegorient::Method::Patched);
        QCOMPARE(after.size(), before.size());
        int differing = 0;
        for (qsizetype i = 0; i < before.size(); ++i)
            differing += before[i] != after[i];
        QVERIFY2(differing >= 1 && differing <= 2,
                 qPrintable(QStringLiteral("%1 bytes changed; the tag is two").arg(differing)));
        QCOMPARE(jpegorient::readOrientation(after), 1);
    }

    void exifWithoutTheTagIsRewrittenAndKeepsItsRecord()
    {
        const QByteArray before = read(fixtureFile("exif_camera.jpg"));
        QCOMPARE(jpegorient::readOrientation(before), 1);
        jpegorient::Method how = jpegorient::Method::Patched;
        const QByteArray after = jpegorient::withOrientation(before, 6, &how);
        QVERIFY(how == jpegorient::Method::Rewritten);
        QCOMPARE(jpegorient::readOrientation(after), 6);
        const auto camera = [](const QByteArray &jpeg) {
            QTemporaryFile file;
            if (!file.open())
                return QList<QPair<QString, QString>>();
            file.write(jpeg);
            file.flush();
            return photoinfo::rows(photoinfo::fromExif(photoinfo::exifFromJpeg(file.fileName())));
        };
        QCOMPARE(camera(after), camera(before));
        QVERIFY(!camera(after).isEmpty());
    }

    void noExifGetsAMinimalBlock()
    {
        const QByteArray before = read(sampleFile("sample.jpg"));
        QCOMPARE(jpegorient::readOrientation(before), 1);
        jpegorient::Method how = jpegorient::Method::Patched;
        const QByteArray after = jpegorient::withOrientation(before, 8, &how);
        QVERIFY(how == jpegorient::Method::Inserted);
        QCOMPARE(jpegorient::readOrientation(after), 8);
        // Take the one new segment out again and the original is what is
        // left, byte for byte. (It goes after JFIF's APP0, which must stay
        // first, so it is not simply at the front.)
        const qsizetype at = after.indexOf(QByteArray("\xff\xe1", 2));
        QVERIFY(at > 0);
        const qsizetype length = 2 + qFromBigEndian<quint16>(after.constData() + at + 2);
        QCOMPARE(after.left(at) + after.mid(at + length), before);
        QVERIFY(length < 64);
        QVERIFY2(before.mid(2, 2) == QByteArray("\xff\xe0", 2), "the sample has a JFIF APP0");
        QVERIFY2(after.mid(2, 2) == QByteArray("\xff\xe0", 2), "JFIF's APP0 is no longer first");
    }

    void notAJpegIsRefused()
    {
        QString error;
        QVERIFY(jpegorient::withOrientation(read(sampleFile("sample.png")), 6, nullptr, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(jpegorient::readOrientation(read(sampleFile("sample.png"))), 0);
    }

    void writingTheFileLeavesWhatIsNotTheTag()
    {
        const QString path = m_dir.filePath(QStringLiteral("patched.jpg"));
        QFile::remove(path);
        QVERIFY(QFile::copy(sampleFile("rotated_exif.jpg"), path));
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        const QByteArray before = read(path);
        jpegorient::Method how = jpegorient::Method::Inserted;
        QString error;
        QVERIFY2(jpegorient::write(path, 3, &how, &error), qPrintable(error));
        QVERIFY(how == jpegorient::Method::Patched);
        const QByteArray after = read(path);
        QCOMPARE(after.size(), before.size());
        QCOMPARE(jpegorient::readOrientation(path), 3);

        const QString inserted = m_dir.filePath(QStringLiteral("inserted.jpg"));
        QFile::remove(inserted);
        QVERIFY(QFile::copy(sampleFile("sample.jpg"), inserted));
        QFile::setPermissions(inserted, QFile::ReadOwner | QFile::WriteOwner);
        QVERIFY2(jpegorient::write(inserted, 6, &how, &error), qPrintable(error));
        QVERIFY(how == jpegorient::Method::Inserted);
        QCOMPARE(jpegorient::readOrientation(inserted), 6);
    }
};

// ---------------------------------------------------------------------------

/// The settings file: defaults, round trips, and hand edits.
class SettingsTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_path;

    void writeRaw(const QByteArray &text)
    {
        QFile file(m_path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_path = m_dir.filePath(QStringLiteral("settings.ini"));
        QFile::remove(m_path);
        settings::setFilePath(m_path);
    }
    void cleanupTestCase() { settings::setFilePath(QString()); }

    void noFileMeansTheDefaults()
    {
        QCOMPARE(settings::load(), Preferences());
    }

    void whatIsSavedIsWhatLoads()
    {
        Preferences p;
        p.cacheMb = 2048;
        p.prefetchRadius = 4;
        p.slideshowSeconds = 10;
        settings::save(p);
        QCOMPARE(settings::load(), p);
    }

    void handEditedNonsenseIsReinedIn()
    {
        // It is a text file in the user's own folder; anything can be in it.
        writeRaw("[cache]\nmegabytes=5\nprefetch=99\n[slideshow]\nseconds=soon\n");
        const Preferences p = settings::load();
        QCOMPARE(p.cacheMb, Preferences::MinCacheMb);
        QCOMPARE(p.prefetchRadius, Preferences::MaxPrefetch);
        QCOMPARE(p.slideshowSeconds, Preferences::DefaultSlideshowSeconds);
    }

    void windowValuesRoundTrip()
    {
        settings::setWindowValue(QStringLiteral("geometry"), QByteArray("\x01\x02\xff", 3));
        QCOMPARE(settings::windowValue(QStringLiteral("geometry")), QByteArray("\x01\x02\xff", 3));
        QVERIFY(settings::windowValue(QStringLiteral("never-set")).isEmpty());
    }

    void theDefaultFileLivesBesideTheLogs()
    {
        settings::setFilePath(QString());
        const QFileInfo file(settings::filePath());
        QCOMPARE(file.fileName(), QStringLiteral("picviewer-cpp.ini"));
        QCOMPARE(QFileInfo(file.absolutePath()).fileName(), QStringLiteral("PicViewer"));
    }
};

// ---------------------------------------------------------------------------

/// Paths Windows can name but the ANSI code page cannot.
///
/// LibRaw and libheif were handed `toLocal8Bit()`. A HEIC anywhere under a
/// folder called 照片 would not open, and a RAW named in Simplified Chinese
/// opened as the 200x150 thumbnail from its first TIFF directory, because
/// when the RAW decoder failed the Qt decoder took over and succeeded. These
/// copy the samples to such names and require the right decoder and the
/// full size. On CI the code page is 1252, so every row below is outside it.
class UnicodePathTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static QString sample(const char *name)
    {
        return QStringLiteral(PICVIEWER_SAMPLES_DIR "/") + QLatin1String(name);
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void decodesUnderAnyName_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("decoder");
        QTest::addColumn<QSize>("size");
        const QSize raw(800, 600), heif(640, 480);
        QTest::newRow("RAW, ASCII") << "synthetic.dng" << "plain.dng" << "raw" << raw;
        QTest::newRow("RAW, Traditional Chinese")
            << "synthetic.dng" << QStringLiteral("繁體測試.dng") << "raw" << raw;
        QTest::newRow("RAW, Simplified Chinese")
            << "synthetic.dng" << QStringLiteral("简体们.dng") << "raw" << raw;
        QTest::newRow("RAW, emoji") << "synthetic.dng" << QStringLiteral("emoji😀.dng") << "raw" << raw;
        QTest::newRow("RAW, in a Chinese folder")
            << "synthetic.dng" << QStringLiteral("照片/inside.dng") << "raw" << raw;
        QTest::newRow("HEIC, ASCII") << "sample.heic" << "plain.heic" << "heif" << heif;
        QTest::newRow("HEIC, Traditional Chinese")
            << "sample.heic" << QStringLiteral("繁體測試.heic") << "heif" << heif;
        QTest::newRow("HEIC, Simplified Chinese")
            << "sample.heic" << QStringLiteral("简体们.heic") << "heif" << heif;
        QTest::newRow("HEIC, in a Chinese folder")
            << "sample.heic" << QStringLiteral("照片/inside.heic") << "heif" << heif;
    }

    void decodesUnderAnyName()
    {
        QFETCH(QString, source);
        QFETCH(QString, name);
        QFETCH(QString, decoder);
        QFETCH(QSize, size);

        const QString path = m_dir.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile::remove(path);
        QVERIFY(QFile::copy(sample(source.toLatin1().constData()), path));

        decoders::DecodeError error;
        const auto image = decoders::Registry::instance().loadFull(path, QSize(), &error);
        QVERIFY2(image, qPrintable("did not decode: " + error.message));
        QCOMPARE(image->metadata.value(QStringLiteral("decoder")), decoder);
        QCOMPARE(image->fullSize, size);
    }

    void aRawPreviewOpensUnderAnyName()
    {
        const QString path = m_dir.filePath(QStringLiteral("预览😀.dng"));
        QFile::remove(path);
        QVERIFY(QFile::copy(sample("synthetic.dng"), path));
        const auto plain = decoders::Registry::instance().loadPreview(sample("synthetic.dng"), QSize(512, 512));
        const auto preview = decoders::Registry::instance().loadPreview(path, QSize(512, 512));
        QCOMPARE(preview.has_value(), plain.has_value());
    }
};

// ---------------------------------------------------------------------------

/// What the camera recorded: reading it, and putting it into words.
class PhotoInfoTests : public QObject
{
    Q_OBJECT

    static QString fixture(const char *name)
    {
        return QStringLiteral(PICVIEWER_FIXTURES_DIR "/") + QLatin1String(name);
    }

    /// A TIFF-structured EXIF block written by hand, in either byte order.
    /// Pillow, which made the committed fixtures, only writes little-endian,
    /// and Nikon and older Canon bodies write big-endian -- so the other
    /// order has to come from somewhere.
    class Block
    {
    public:
        explicit Block(bool bigEndian) : m_big(bigEndian) {}

        void ascii(bool exifIfd, quint16 tag, const QByteArray &text)
        {
            add(exifIfd, tag, 2, text.size() + 1, text + '\0');
        }
        void rational(bool exifIfd, quint16 tag, quint32 num, quint32 den, bool isSigned = false)
        {
            add(exifIfd, tag, isSigned ? 10 : 5, 1, u32(num) + u32(den));
        }
        void shortValue(bool exifIfd, quint16 tag, quint16 value)
        {
            add(exifIfd, tag, 3, 1, u16(value));
        }
        /// A GPS position: references and degrees-minutes-seconds, as
        /// cameras write it, and an altitude.
        void position(double latitude, double longitude, quint32 metres, bool below = false)
        {
            const auto dms = [&](double value) {
                value = std::abs(value);
                const quint32 d = quint32(value);
                const quint32 m = quint32((value - d) * 60);
                const quint32 s = quint32(std::lround(((value - d) * 60 - m) * 60 * 10000));
                return u32(d) + u32(1) + u32(m) + u32(1) + u32(s) + u32(10000);
            };
            m_gps << Entry{1, 2, 2, QByteArray(latitude < 0 ? "S" : "N") + '\0'}
                  << Entry{2, 5, 3, dms(latitude)}
                  << Entry{3, 2, 2, QByteArray(longitude < 0 ? "W" : "E") + '\0'}
                  << Entry{4, 5, 3, dms(longitude)}
                  << Entry{5, 1, 1, QByteArray(1, below ? '\1' : '\0')}
                  << Entry{6, 5, 1, u32(metres) + u32(1)};
        }

        /// *padding* bytes sit between the header and the first directory,
        /// as pixel data does in a TIFF written by libtiff.
        QByteArray bytes(quint32 padding = 0) const
        {
            // Header, IFD0 (with pointers to the EXIF and GPS IFDs added),
            // those IFDs, then every value too long to sit in its entry.
            const quint32 ifd0 = 8 + padding;
            const quint32 ifd0Size = 2 + 12 * (m_ifd0.size() + 1 + (m_gps.isEmpty() ? 0 : 1)) + 4;
            const quint32 exif = ifd0 + ifd0Size;
            const quint32 exifSize = 2 + 12 * m_exif.size() + 4;
            const quint32 gps = exif + exifSize;
            const quint32 gpsSize = m_gps.isEmpty() ? 0 : 2 + 12 * m_gps.size() + 4;
            const quint32 overflowAt = gps + gpsSize;

            QByteArray overflow;
            const auto directory = [&](QList<Entry> entries) {
                QByteArray out = u16(entries.size());
                for (const Entry &e : entries) {
                    out += u16(e.tag) + u16(e.type) + u32(e.count);
                    if (e.value.size() <= 4) {
                        out += e.value + QByteArray(4 - e.value.size(), '\0');
                    } else {
                        out += u32(overflowAt + overflow.size());
                        overflow += e.value;
                        if (overflow.size() % 2)
                            overflow += '\0';
                    }
                }
                return out + u32(0);
            };

            QList<Entry> first = m_ifd0;
            first << Entry{0x8769, 4, 1, u32(exif)};
            if (!m_gps.isEmpty())
                first << Entry{0x8825, 4, 1, u32(gps)};
            QByteArray out = m_big ? QByteArray("MM\0*", 4) : QByteArray("II*\0", 4);
            out += u32(ifd0);
            out += QByteArray(padding, '\x55');
            out += directory(first);
            out += directory(m_exif);
            if (!m_gps.isEmpty())
                out += directory(m_gps);
            return out + overflow;
        }

    private:
        struct Entry { quint16 tag; quint16 type; quint32 count; QByteArray value; };

        void add(bool exifIfd, quint16 tag, quint16 type, quint32 count, const QByteArray &value)
        {
            (exifIfd ? m_exif : m_ifd0) << Entry{tag, type, count, value};
        }
        QByteArray u16(quint16 v) const
        {
            QByteArray b(2, '\0');
            m_big ? qToBigEndian(v, b.data()) : qToLittleEndian(v, b.data());
            return b;
        }
        QByteArray u32(quint32 v) const
        {
            QByteArray b(4, '\0');
            m_big ? qToBigEndian(v, b.data()) : qToLittleEndian(v, b.data());
            return b;
        }

        bool m_big;
        QList<Entry> m_ifd0;
        QList<Entry> m_exif;
        QList<Entry> m_gps;
    };

    static Block typicalBlock(bool bigEndian, bool withDate)
    {
        Block block(bigEndian);
        block.ascii(false, 0x010f, "NIKON CORPORATION");
        block.ascii(false, 0x0110, "NIKON Z 6");
        block.rational(true, 0x829a, 1, 125);           // exposure time
        block.rational(true, 0x829d, 40, 10);           // f-number
        block.shortValue(true, 0x8827, 800);            // ISO
        block.rational(true, 0x9204, quint32(-1), 1, true);   // bias, -1 EV
        if (withDate)
            block.ascii(true, 0x9003, "2024:12:31 23:59:58");
        return block;
    }

private slots:
    // -- putting it into words ------------------------------------------------

    void exposureTime_data()
    {
        QTest::addColumn<double>("seconds");
        QTest::addColumn<QString>("expected");
        QTest::newRow("1/250") << 0.004 << "1/250 s";
        QTest::newRow("1/60") << 1.0 / 60 << "1/60 s";
        QTest::newRow("1/2") << 0.5 << "1/2 s";
        QTest::newRow("1/3, as 0.3") << 0.3 << "1/3 s";
        QTest::newRow("0.8") << 0.8 << "0.8 s";
        QTest::newRow("1") << 1.0 << "1 s";
        QTest::newRow("2.5") << 2.5 << "2.5 s";
        QTest::newRow("30") << 30.0 << "30 s";
        QTest::newRow("not recorded") << 0.0 << "";
    }
    void exposureTime()
    {
        QFETCH(double, seconds);
        QFETCH(QString, expected);
        QCOMPARE(photoinfo::exposureTime(seconds), expected);
    }

    void aperture_data()
    {
        QTest::addColumn<double>("fNumber");
        QTest::addColumn<QString>("expected");
        QTest::newRow("5.6") << 5.6 << "f/5.6";
        QTest::newRow("8, no decimal") << 8.0 << "f/8";
        QTest::newRow("1.4") << 1.4 << "f/1.4";
        QTest::newRow("not recorded") << 0.0 << "";
    }
    void aperture()
    {
        QFETCH(double, fNumber);
        QFETCH(QString, expected);
        QCOMPARE(photoinfo::aperture(fNumber), expected);
    }

    void exposureBias_data()
    {
        QTest::addColumn<double>("ev");
        QTest::addColumn<QString>("expected");
        QTest::newRow("+2/3") << 2.0 / 3 << "+0.7 EV";
        QTest::newRow("-1") << -1.0 << "-1 EV";
        QTest::newRow("-1/3") << -1.0 / 3 << "-0.3 EV";
        QTest::newRow("zero says nothing") << 0.0 << "";
    }
    void exposureBias()
    {
        QFETCH(double, ev);
        QFETCH(QString, expected);
        QCOMPARE(photoinfo::exposureBias(ev), expected);
    }

    void camera_data()
    {
        QTest::addColumn<QString>("make");
        QTest::addColumn<QString>("model");
        QTest::addColumn<QString>("expected");
        QTest::newRow("Canon repeats itself") << "Canon" << "Canon EOS R5" << "Canon EOS R5";
        QTest::newRow("Nikon, in full") << "NIKON CORPORATION" << "NIKON Z 6" << "NIKON Z 6";
        QTest::newRow("Olympus, model alone")
            << "OLYMPUS IMAGING CORP." << "E-M1" << "OLYMPUS E-M1";
        QTest::newRow("Apple") << "Apple" << "iPhone 13" << "Apple iPhone 13";
        QTest::newRow("model only") << "" << "X100V" << "X100V";
        QTest::newRow("make only") << "FUJIFILM" << "" << "FUJIFILM";
    }
    void camera()
    {
        QFETCH(QString, make);
        QFETCH(QString, model);
        QFETCH(QString, expected);
        photoinfo::PhotoInfo info;
        info.make = make;
        info.model = model;
        QCOMPARE(photoinfo::camera(info), expected);
    }

    void focalLengthShowsTheEquivalentOnlyWhenItDiffers()
    {
        photoinfo::PhotoInfo info;
        info.focalLength = 70;
        info.focalLength35 = 70;
        QCOMPARE(photoinfo::focalLength(info), QStringLiteral("70 mm"));
        info.focalLength = 5.1;
        info.focalLength35 = 26;
        QCOMPARE(photoinfo::focalLength(info), QStringLiteral("5.1 mm (26 mm equivalent)"));
        info.focalLength35 = 0;
        QCOMPARE(photoinfo::focalLength(info), QStringLiteral("5.1 mm"));
    }

    // -- reading it -------------------------------------------------------------

    void bothByteOrdersRead_data()
    {
        QTest::addColumn<bool>("bigEndian");
        QTest::newRow("big-endian (MM)") << true;
        QTest::newRow("little-endian (II)") << false;
    }
    void bothByteOrdersRead()
    {
        QFETCH(bool, bigEndian);
        const photoinfo::PhotoInfo info =
            photoinfo::fromExif(typicalBlock(bigEndian, true).bytes());
        QCOMPARE(info.make, QStringLiteral("NIKON CORPORATION"));
        QCOMPARE(info.model, QStringLiteral("NIKON Z 6"));
        QCOMPARE(info.exposureTime, 1.0 / 125);
        QCOMPARE(info.aperture, 4.0);
        QCOMPARE(info.iso, 800);
        QVERIFY(info.exposureBias.has_value());
        QCOMPARE(*info.exposureBias, -1.0);
        QCOMPARE(info.taken, QDateTime(QDate(2024, 12, 31), QTime(23, 59, 58)));
    }

    void anUndatedPhotographStaysUndated()
    {
        // A date from anywhere but the camera -- a library default, the
        // file's modified time -- would read as when the photograph was
        // taken. No date recorded means none shown.
        const QByteArray block = typicalBlock(true, false).bytes();
        const photoinfo::PhotoInfo info = photoinfo::fromExif(block);
        QCOMPARE(info.model, QStringLiteral("NIKON Z 6"));     // the block did parse
        QVERIFY2(!info.taken.isValid(),
                 qPrintable("a block with no date read as taken " + info.taken.toString()));
    }

    void theExifHeaderIsOptional()
    {
        const QByteArray tiff = typicalBlock(false, true).bytes();
        const QByteArray withHeader = QByteArray("Exif\0\0", 6) + tiff;
        QCOMPARE(photoinfo::fromExif(withHeader).model, QStringLiteral("NIKON Z 6"));
        QCOMPARE(photoinfo::fromExif(tiff).model, QStringLiteral("NIKON Z 6"));
    }

    void garbageGivesNothingRatherThanFailing()
    {
        QVERIFY(photoinfo::fromExif(QByteArray()).isEmpty());
        QVERIFY(photoinfo::fromExif(QByteArray("not exif at all")).isEmpty());
        // A real header over a directory that points past the end.
        QByteArray truncated = typicalBlock(true, true).bytes().left(30);
        QVERIFY(photoinfo::fromExif(truncated).iso == 0);
    }

    void heifItemsSkipTheirOffset_data()
    {
        QTest::addColumn<QByteArray>("item");
        QTest::addColumn<bool>("readable");
        const QByteArray tiff = typicalBlock(true, true).bytes();
        const QByteArray header("Exif\0\0", 6);
        QTest::newRow("offset 0, TIFF follows") << QByteArray(4, '\0') + tiff << true;
        QTest::newRow("offset 6, past an Exif header")
            << QByteArray("\0\0\0\x06", 4) + header + tiff << true;
        QTest::newRow("offset 0, Exif header follows") << QByteArray(4, '\0') + header + tiff << true;
        QTest::newRow("offset past the end") << QByteArray("\0\0\x10\0", 4) + tiff << false;
        QTest::newRow("too short for an offset") << QByteArray("\0\0", 2) << false;
    }
    void heifItemsSkipTheirOffset()
    {
        QFETCH(QByteArray, item);
        QFETCH(bool, readable);
        const photoinfo::PhotoInfo info = photoinfo::fromExif(photoinfo::exifFromHeifItem(item));
        QCOMPARE(info.model == QStringLiteral("NIKON Z 6"), readable);
    }

    void aJpegsExifIsFoundInItsSegments()
    {
        const QString path = fixture("exif_camera.jpg");
        QVERIFY2(QFileInfo::exists(path), "fixture missing; regenerate with make_exif.py");
        const photoinfo::PhotoInfo info = photoinfo::fromExif(photoinfo::exifFromJpeg(path));
        QCOMPARE(photoinfo::camera(info), QStringLiteral("Canon EOS R5"));
        QCOMPARE(info.lens, QStringLiteral("RF24-105mm F4 L IS USM"));
        QCOMPARE(photoinfo::focalLength(info), QStringLiteral("70 mm"));
        QCOMPARE(photoinfo::aperture(info.aperture), QStringLiteral("f/5.6"));
        QCOMPARE(photoinfo::exposureTime(info.exposureTime), QStringLiteral("1/250 s"));
        QCOMPARE(info.iso, 400);
        QCOMPARE(photoinfo::exposureBias(info.exposureBias.value_or(0)), QStringLiteral("+0.7 EV"));
        QCOMPARE(info.taken, QDateTime(QDate(2025, 5, 1), QTime(13, 22, 10)));
    }

    void aJpegWithoutCameraExifHasNothingToShow()
    {
        // This one carries EXIF, for its orientation, but nothing a camera
        // records about the shot.
        const QString path = QStringLiteral(PICVIEWER_SAMPLES_DIR "/rotated_exif.jpg");
        QVERIFY(!photoinfo::exifFromJpeg(path).isEmpty());
        QVERIFY(photoinfo::fromExif(photoinfo::exifFromJpeg(path)).isEmpty());
        QVERIFY(photoinfo::exifFromJpeg(QStringLiteral(PICVIEWER_SAMPLES_DIR "/sample.png")).isEmpty());
    }

    // -- position ---------------------------------------------------------------

    void gpsPositionsReadWithTheirSigns_data()
    {
        QTest::addColumn<bool>("bigEndian");
        QTest::addColumn<double>("latitude");
        QTest::addColumn<double>("longitude");
        QTest::addColumn<QString>("expected");
        QTest::newRow("Taipei, N E, big-endian")
            << true << 25.033964 << 121.564472 << QStringLiteral("25.033964° N, 121.564472° E");
        QTest::newRow("Rio, S W, little-endian")
            << false << -22.951916 << -43.210487 << QStringLiteral("22.951916° S, 43.210487° W");
        QTest::newRow("Reykjavik, N W")
            << true << 64.146582 << -21.942635 << QStringLiteral("64.146582° N, 21.942635° W");
    }
    void gpsPositionsReadWithTheirSigns()
    {
        QFETCH(bool, bigEndian);
        QFETCH(double, latitude);
        QFETCH(double, longitude);
        QFETCH(QString, expected);
        Block block = typicalBlock(bigEndian, true);
        block.position(latitude, longitude, 31);
        const photoinfo::PhotoInfo info = photoinfo::fromExif(block.bytes());
        QCOMPARE(photoinfo::location(info), expected);
        QVERIFY(info.altitude.has_value());
        QCOMPARE(*info.altitude, 31.0);
    }

    void belowSeaLevelIsNegative()
    {
        Block block = typicalBlock(true, true);
        block.position(31.5, 35.5, 430, true);      // the Dead Sea shore
        QCOMPARE(*photoinfo::fromExif(block.bytes()).altitude, -430.0);
    }

    void zeroZeroIsNoFixRatherThanAPlace()
    {
        // What a camera writes when it has no fix: a point in the Gulf of
        // Guinea, and not where anybody took the photograph.
        Block block = typicalBlock(true, true);
        block.position(0, 0, 0);
        const photoinfo::PhotoInfo info = photoinfo::fromExif(block.bytes());
        QVERIFY(!info.latitude.has_value());
        QVERIFY(photoinfo::location(info).isEmpty());
        QVERIFY(photoinfo::mapUrl(info).isEmpty());
    }

    void theMapLinkCarriesThePosition()
    {
        photoinfo::PhotoInfo info;
        photoinfo::setPosition(info, -22.951916, -43.210487);
        QCOMPARE(photoinfo::mapUrl(info),
                 QStringLiteral("https://www.openstreetmap.org/?mlat=-22.951916&mlon=-43.210487"
                                "#map=16/-22.951916/-43.210487"));
    }

    // -- PNG, WebP, TIFF --------------------------------------------------------

    void tiffDirectoriesPastSixtyFourKilobytesStillRead()
    {
        // libexif reads at most 64 KiB of a block, and a TIFF written by
        // libtiff puts its directories after the pixels. Handed the whole
        // file it sees nothing; condensed first, everything.
        QByteArray tiff = typicalBlock(true, true).bytes(100'000);
        QVERIFY(tiff.size() > 100'000);
        QVERIFY2(photoinfo::fromExif(tiff).model.isEmpty(),
                 "libexif read past 64 KiB after all -- the condensing may not be needed");
        const photoinfo::PhotoInfo info = photoinfo::fromExif(photoinfo::exifFromTiffBytes(tiff));
        QCOMPARE(info.model, QStringLiteral("NIKON Z 6"));
        QCOMPARE(info.iso, 800);
        QCOMPARE(info.taken, QDateTime(QDate(2024, 12, 31), QTime(23, 59, 58)));
    }

    void tiffCondensingKeepsThePosition()
    {
        Block block = typicalBlock(false, true);
        block.position(-22.951916, -43.210487, 700);
        const photoinfo::PhotoInfo info =
            photoinfo::fromExif(photoinfo::exifFromTiffBytes(block.bytes(70'000)));
        QCOMPARE(photoinfo::location(info), QStringLiteral("22.951916° S, 43.210487° W"));
        QCOMPARE(*info.altitude, 700.0);
    }

    void brokenContainersGiveNothing()
    {
        QVERIFY(photoinfo::exifFromTiffBytes(QByteArray()).isEmpty());
        QVERIFY(photoinfo::exifFromTiffBytes(QByteArray("MM\0*\xff\xff\xff\xff", 8)).isEmpty());
        QVERIFY(photoinfo::exifFromTiffBytes(typicalBlock(true, true).bytes().left(20)).isEmpty());
        // The right reader on the wrong file finds nothing, and does not crash.
        QVERIFY(photoinfo::exifFromPng(fixture("exif_camera.jpg")).isEmpty());
        QVERIFY(photoinfo::exifFromWebp(fixture("exif_camera.jpg")).isEmpty());
        QVERIFY(photoinfo::exifFromJpeg(fixture("exif_png.png")).isEmpty());
    }

    void everyContainerCarriesTheRecord_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("location");
        QTest::newRow("PNG eXIf") << "exif_png.png" << QString();
        QTest::newRow("WebP EXIF") << "exif_webp.webp" << QString();
        QTest::newRow("TIFF, directories after the pixels")
            << "exif_tiff.tif" << QStringLiteral("22.951916° S, 43.210487° W");
        QTest::newRow("HEIC, with a position")
            << "exif_phone.heic" << QStringLiteral("25.033964° N, 121.564472° E");
    }
    void everyContainerCarriesTheRecord()
    {
        QFETCH(QString, file);
        QFETCH(QString, location);
        decoders::DecodeError error;
        const auto image = decoders::Registry::instance().loadFull(
            fixture(file.toLatin1().constData()), QSize(), &error);
        QVERIFY2(image, qPrintable(error.message));
        QVERIFY2(!image->info.model.isEmpty(), "no camera record came through");
        QVERIFY(image->info.aperture > 0);
        QCOMPARE(photoinfo::location(image->info), location);
    }

    // -- through the decoders ---------------------------------------------------

    void theJpegDecoderCarriesTheRecord()
    {
        decoders::DecodeError error;
        const auto image = decoders::Registry::instance().loadFull(
            fixture("exif_camera.jpg"), QSize(), &error);
        QVERIFY2(image, qPrintable(error.message));
        QCOMPARE(image->info.iso, 400);
    }

    void theHeifDecoderCarriesTheRecord()
    {
        // HEIF keeps EXIF as a metadata item behind a 4-byte offset, so this
        // is the path through libheif rather than through the file.
        decoders::DecodeError error;
        const auto image = decoders::Registry::instance().loadFull(
            fixture("exif_phone.heic"), QSize(), &error);
        QVERIFY2(image, qPrintable(error.message));
        QCOMPARE(photoinfo::camera(image->info), QStringLiteral("Apple iPhone 13"));
        QCOMPARE(photoinfo::focalLength(image->info), QStringLiteral("5.1 mm (26 mm equivalent)"));
        QCOMPARE(photoinfo::aperture(image->info.aperture), QStringLiteral("f/1.6"));
        QCOMPARE(photoinfo::exposureTime(image->info.exposureTime), QStringLiteral("1/60 s"));
        QCOMPARE(image->info.iso, 125);
        QVERIFY(!image->info.exposureBias.has_value());
        QCOMPARE(image->info.taken, QDateTime(QDate(2025, 6, 2), QTime(8, 5, 9)));
    }

    void everyRealRawDecodes_data()
    {
        // One row per camera file in image_example/ -- untracked, so on CI
        // there are none and the single placeholder row skips.
        QTest::addColumn<QString>("path");
        const QDir real(QStringLiteral(PICVIEWER_SAMPLES_DIR "/../image_example"));
        const QStringList raws = real.entryList(
            {"*.cr2", "*.cr3", "*.nef", "*.arw", "*.raf", "*.orf", "*.rw2", "*.pef", "*.dng",
             "*.srw", "*.rwl"},
            QDir::Files, QDir::Name);
        for (const QString &name : raws)
            QTest::newRow(qPrintable(name)) << real.filePath(name);
        if (raws.isEmpty())
            QTest::newRow("none present") << QString();
    }

    void everyRealRawDecodes()
    {
        QFETCH(QString, path);
        if (path.isEmpty())
            QSKIP("no camera RAW in image_example/ (it is untracked)");

        const auto preview = decoders::Registry::instance().loadPreview(path, QSize(1024, 1024));
        QVERIFY2(preview, "no embedded preview");

        decoders::DecodeError error;
        // Half the size the full decode would give, which is what the RAW
        // decoder does for a display smaller than the sensor; full size for
        // eight makers' files is minutes of a test run.
        const auto image = decoders::Registry::instance().loadFull(path, QSize(2000, 2000), &error);
        QVERIFY2(image, qPrintable(error.message));
        QCOMPARE(image->metadata.value(QStringLiteral("decoder")), QStringLiteral("raw"));
        QVERIFY2(image->fullSize.width() >= 2000,
                 qPrintable(QStringLiteral("full size reads %1x%2 -- a thumbnail, not the sensor")
                                .arg(image->fullSize.width()).arg(image->fullSize.height())));
        QVERIFY2(!image->info.make.isEmpty(), "no camera make in the metadata");
        QVERIFY2(image->info.iso > 0 || image->info.exposureTime > 0,
                 "no exposure in the metadata");
    }

    void aRealRawCarriesTheRecord()
    {
        // The real camera files are untracked, so this runs only where they
        // are -- and the CR2 is the one body the owner actually shoots.
        const QString path = QStringLiteral(PICVIEWER_SAMPLES_DIR "/../image_example/IMG_0893.CR2");
        if (!QFileInfo::exists(path))
            QSKIP("image_example/IMG_0893.CR2 is not present (it is untracked)");
        const auto preview = decoders::Registry::instance().loadPreview(path, QSize(512, 512));
        QVERIFY(preview);
        QCOMPARE(preview->info.make, QStringLiteral("Canon"));
        QVERIFY(!preview->info.model.isEmpty());
        QVERIFY(preview->info.iso > 0);
        QVERIFY(preview->info.exposureTime > 0);
        QVERIFY(preview->info.aperture > 0);
        QVERIFY(preview->info.taken.isValid());
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
    ThumbnailTests thumbnailTests;
    JpegOrientTests jpegOrientTests;
    SettingsTests settingsTests;
    UnicodePathTests unicodePathTests;
    PhotoInfoTests photoInfoTests;

    const QList<QPair<QObject *, QString>> suites = {
        {&sniffTests, QStringLiteral("sniff")},
        {&scoringTests, QStringLiteral("scoring")},
        {&cacheTests, QStringLiteral("cache")},
        {&decodeTests, QStringLiteral("decode")},
        {&thumbnailTests, QStringLiteral("thumbnails")},
        {&jpegOrientTests, QStringLiteral("jpeg-orientation")},
        {&settingsTests, QStringLiteral("settings")},
        {&unicodePathTests, QStringLiteral("unicode-paths")},
        {&photoInfoTests, QStringLiteral("photo-info")},
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
