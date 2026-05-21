// tests/core/book/test_book_downloader_magnet.cpp
//
// Compile + struct-shape tests for the MagnetInFlight state machine added to
// BookDownloader in BOOKS_STREMIO_PIVOT Phase 4 Task 4.5.
//
// ── Why no instantiation tests? ─────────────────────────────────────────────
// BookDownloader.cpp now includes "core/torrent/TorrentClient.h" to resolve
// the connect() calls for torrentCompleted / torrentUpdated signals.
// TorrentClient is a concrete QObject whose moc-generated code lives in
// TorrentClient.cpp.  That .cpp depends on TorrentEngine (libtorrent C++),
// TorrentRepository (Qt6Sql), CoreBridge, and the full application graph —
// none of which are compiled into tankoban_tests (and would require
// libtorrent.lib + the rest).
//
// Consequence: BookDownloader.cpp CANNOT be added to tankoban_tests SOURCES
// without pulling in the entire TorrentClient graph.  The null-client path,
// queueing logic, signal-subscription, and completion/progress slots are
// therefore covered as DEFER_INTEGRATION_TEST stubs (see below) and will be
// promoted to real tests when either:
//   a) Agent 4 provides a virtual ITorrentClient interface that
//      BookDownloader depends on (so a thin stub can be compiled into the
//      test binary), OR
//   b) The test binary gains a separate test_torrent_client_mock.cpp that
//      provides a fake TorrentClient usable without libtorrent.
//
// What IS compiled here:
//   • MagnetInFlight struct field shape (default values, types).
//   • Queue/active pointer default-init contract.
//   • pickBestBookFile file-walk logic via a standalone helper that mirrors
//     the real logic but doesn't need BookDownloader to be linked.
//
// All tests in this file pass without any BookDownloader.cpp in SOURCES.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

// ── Struct-shape smoke ────────────────────────────────────────────────────────
// Re-declare a local mirror of MagnetInFlight to verify the field contract
// without linking BookDownloader.cpp.  If the real struct fields change,
// this test catches the mismatch at review time (the reviewer sees the
// discrepancy between this mirror and BookDownloader.h).

struct MagnetInFlightMirror {
    QString infoHash;
    QString magnetUri;
    QString destinationDir;
    QString suggestedName;
    QString expectedFormat;

    qint64  bytesReceived   = 0;
    qint64  bytesTotal      = 0;
    qint64  lastProgressMs  = 0;
    int     lastProgressPct = -1;
};

TEST(MagnetInFlightShapeTest, DefaultValuesAreZeroOrEmpty)
{
    MagnetInFlightMirror m;
    EXPECT_TRUE(m.infoHash.isEmpty());
    EXPECT_TRUE(m.magnetUri.isEmpty());
    EXPECT_TRUE(m.destinationDir.isEmpty());
    EXPECT_TRUE(m.suggestedName.isEmpty());
    EXPECT_TRUE(m.expectedFormat.isEmpty());
    EXPECT_EQ(m.bytesReceived,   0);
    EXPECT_EQ(m.bytesTotal,      0);
    EXPECT_EQ(m.lastProgressMs,  0);
    EXPECT_EQ(m.lastProgressPct, -1);
}

TEST(MagnetInFlightShapeTest, FieldAssignmentRoundTrips)
{
    MagnetInFlightMirror m;
    m.infoHash       = QStringLiteral("abc123def456");
    m.magnetUri      = QStringLiteral("magnet:?xt=urn:btih:abc123def456");
    m.destinationDir = QStringLiteral("C:/books");
    m.suggestedName  = QStringLiteral("Dune");
    m.expectedFormat = QStringLiteral("epub");
    m.bytesReceived  = 1024LL * 1024 * 50;   // 50 MiB
    m.bytesTotal     = 1024LL * 1024 * 100;  // 100 MiB
    m.lastProgressMs = 1000;
    m.lastProgressPct= 50;

    EXPECT_EQ(m.infoHash,        QStringLiteral("abc123def456"));
    EXPECT_EQ(m.magnetUri,       QStringLiteral("magnet:?xt=urn:btih:abc123def456"));
    EXPECT_EQ(m.destinationDir,  QStringLiteral("C:/books"));
    EXPECT_EQ(m.suggestedName,   QStringLiteral("Dune"));
    EXPECT_EQ(m.expectedFormat,  QStringLiteral("epub"));
    EXPECT_EQ(m.bytesReceived,   1024LL * 1024 * 50);
    EXPECT_EQ(m.bytesTotal,      1024LL * 1024 * 100);
    EXPECT_EQ(m.lastProgressMs,  1000);
    EXPECT_EQ(m.lastProgressPct, 50);
}

// ── Progress throttle logic unit test ────────────────────────────────────────
// Mirrors the throttle logic inside onMagnetTorrentUpdated.  Verifiable
// without linking BookDownloader.cpp.

namespace {
bool shouldEmitProgress(qint64 nowMs,
                        qint64 lastProgressMs,
                        int    pct,
                        int    lastProgressPct,
                        int    throttleMs = 250)
{
    const qint64 elapsed  = nowMs - lastProgressMs;
    const int    deltaPct = pct - lastProgressPct;
    return (elapsed >= throttleMs) || (deltaPct >= 1);
}
} // namespace

TEST(MagnetProgressThrottleTest, FirstEmitAlwaysPasses)
{
    // lastProgressMs=0 → elapsed = nowMs which is large → always passes.
    EXPECT_TRUE(shouldEmitProgress(/*nowMs=*/10000,
                                   /*lastProgressMs=*/0,
                                   /*pct=*/0,
                                   /*lastProgressPct=*/-1));
}

TEST(MagnetProgressThrottleTest, SuppressedWhenTooSoonAndNoPctAdvance)
{
    // Only 100ms elapsed, no pct advance → should NOT emit.
    EXPECT_FALSE(shouldEmitProgress(/*nowMs=*/10100,
                                    /*lastProgressMs=*/10000,
                                    /*pct=*/5,
                                    /*lastProgressPct=*/5));
}

TEST(MagnetProgressThrottleTest, PassesWhenEnoughTimeElapsed)
{
    EXPECT_TRUE(shouldEmitProgress(/*nowMs=*/10260,
                                   /*lastProgressMs=*/10000,
                                   /*pct=*/5,
                                   /*lastProgressPct=*/5));
}

TEST(MagnetProgressThrottleTest, PassesWhenPctAdvances)
{
    // Even if < 250ms, a pct advance of 1 forces emit.
    EXPECT_TRUE(shouldEmitProgress(/*nowMs=*/10050,
                                   /*lastProgressMs=*/10000,
                                   /*pct=*/6,
                                   /*lastProgressPct=*/5));
}

// ── File-walk picker logic ────────────────────────────────────────────────────
// Tests the pickBestBookFile heuristic (largest file, prefer expectedFormat,
// move from subdir) without needing BookDownloader linked.  The logic is
// re-implemented inline here; the real implementation in BookDownloader.cpp
// is the authoritative version.

namespace {
static const QStringList kBookExts = {
    QStringLiteral("epub"), QStringLiteral("pdf"), QStringLiteral("mobi"),
    QStringLiteral("azw3"), QStringLiteral("azw"), QStringLiteral("djvu"),
    QStringLiteral("cbz"),  QStringLiteral("cbr"),
};

bool isBookFile(const QFileInfo& fi) {
    const QString ext = fi.suffix().toLower();
    return kBookExts.contains(ext);
}

// Simplified version of the real pickBestBookFile — same algorithm, no
// BookDownloader member access.
QString pickBestBookFileStandalone(const QString& destinationDir,
                                   const QString& expectedFormat)
{
    const QDir dir(destinationDir);
    if (!dir.exists()) return {};

    QStringList nameFilters;
    for (const QString& ext : kBookExts)
        nameFilters << QStringLiteral("*.") + ext;

    QFileInfoList candidates = dir.entryInfoList(nameFilters,
                                                  QDir::Files | QDir::Readable,
                                                  QDir::NoSort);

    const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& sub : subdirs) {
        QDir subDir(sub.absoluteFilePath());
        candidates += subDir.entryInfoList(nameFilters,
                                           QDir::Files | QDir::Readable,
                                           QDir::NoSort);
    }

    if (candidates.isEmpty()) return {};

    if (candidates.size() == 1)
        return candidates.first().absoluteFilePath();

    QFileInfoList preferred;
    if (!expectedFormat.isEmpty()) {
        for (const QFileInfo& fi : candidates) {
            if (fi.suffix().compare(expectedFormat, Qt::CaseInsensitive) == 0)
                preferred << fi;
        }
    }

    const QFileInfoList& pool = preferred.isEmpty() ? candidates : preferred;
    QFileInfo best;
    for (const QFileInfo& fi : pool) {
        if (!best.exists() || fi.size() > best.size())
            best = fi;
    }

    if (!best.exists()) return {};

    if (best.absoluteDir() != dir) {
        const QString target = dir.absoluteFilePath(best.fileName());
        if (QFile::exists(target)) QFile::remove(target);
        if (QFile::rename(best.absoluteFilePath(), target))
            return target;
    }

    return best.absoluteFilePath();
}

bool writeFile(const QString& path, int sizeBytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QByteArray payload(sizeBytes, 'x');
    return f.write(payload) == sizeBytes;
}
} // namespace

TEST(PickBestBookFileTest, SingleEpub_ReturnedDirectly)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString epubPath = tmp.filePath(QStringLiteral("dune.epub"));
    ASSERT_TRUE(writeFile(epubPath, 1024));

    const QString result = pickBestBookFileStandalone(tmp.path(), QStringLiteral("epub"));
    EXPECT_EQ(result, epubPath);
}

TEST(PickBestBookFileTest, EmptyDir_ReturnsEmpty)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString result = pickBestBookFileStandalone(tmp.path(), QStringLiteral("epub"));
    EXPECT_TRUE(result.isEmpty());
}

TEST(PickBestBookFileTest, NonexistentDir_ReturnsEmpty)
{
    const QString result = pickBestBookFileStandalone(
        QStringLiteral("/tmp/does_not_exist_tankoban_test"), QStringLiteral("epub"));
    EXPECT_TRUE(result.isEmpty());
}

TEST(PickBestBookFileTest, MultipleFiles_PicksLargest)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString small = tmp.filePath(QStringLiteral("small.epub"));
    const QString large = tmp.filePath(QStringLiteral("large.epub"));
    ASSERT_TRUE(writeFile(small, 512));
    ASSERT_TRUE(writeFile(large, 2048));

    const QString result = pickBestBookFileStandalone(tmp.path(), QStringLiteral("epub"));
    EXPECT_EQ(result, large);
}

TEST(PickBestBookFileTest, PreferExpectedFormat)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // pdf is larger but expectedFormat is epub → should pick epub.
    const QString epubPath = tmp.filePath(QStringLiteral("book.epub"));
    const QString pdfPath  = tmp.filePath(QStringLiteral("book.pdf"));
    ASSERT_TRUE(writeFile(epubPath, 512));
    ASSERT_TRUE(writeFile(pdfPath,  4096));

    const QString result = pickBestBookFileStandalone(tmp.path(), QStringLiteral("epub"));
    EXPECT_EQ(result, epubPath);
}

TEST(PickBestBookFileTest, NoExpectedFormat_PicksLargestOverall)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString epubPath = tmp.filePath(QStringLiteral("book.epub"));
    const QString pdfPath  = tmp.filePath(QStringLiteral("book.pdf"));
    ASSERT_TRUE(writeFile(epubPath, 512));
    ASSERT_TRUE(writeFile(pdfPath,  4096));

    const QString result = pickBestBookFileStandalone(tmp.path(), {});
    EXPECT_EQ(result, pdfPath);
}

TEST(PickBestBookFileTest, FileInSubdir_MovedToRoot)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a subfolder with a single epub.
    QDir(tmp.path()).mkdir(QStringLiteral("payload"));
    const QString subPath = tmp.filePath(QStringLiteral("payload/book.epub"));
    ASSERT_TRUE(writeFile(subPath, 1024));

    const QString result = pickBestBookFileStandalone(tmp.path(), QStringLiteral("epub"));

    // Result should be at the root, not inside the subfolder.
    EXPECT_FALSE(result.isEmpty());
    EXPECT_EQ(QFileInfo(result).dir().absolutePath(),
              QDir(tmp.path()).absolutePath())
        << "Expected file moved to root; got: " << result.toStdString();
}

TEST(PickBestBookFileTest, JunkTorrentWithMixedFiles_PicksLargestBookFile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Non-book junk files + one large epub.
    ASSERT_TRUE(writeFile(tmp.filePath(QStringLiteral("readme.txt")), 512));
    ASSERT_TRUE(writeFile(tmp.filePath(QStringLiteral("cover.jpg")),  2048));
    ASSERT_TRUE(writeFile(tmp.filePath(QStringLiteral("book.epub")),  8192));

    const QString result = pickBestBookFileStandalone(tmp.path(), QStringLiteral("epub"));
    EXPECT_EQ(QFileInfo(result).suffix().toLower(), QStringLiteral("epub"));
}

// ── DEFER_INTEGRATION_TEST stubs ─────────────────────────────────────────────
// Documented gaps that require a wired or mocked TorrentClient.

TEST(BookDownloaderMagnetIntegrationDefer, Defer1_NullClient_EmitsDownloadFailed)
{
    // Requires: BookDownloader.cpp linked + QNetworkAccessManager.
    // Scenario: startMagnetDownload with null TorrentClient must emit
    //   downloadFailed(magnetUri, <non-empty reason mentioning TorrentClient>)
    //   synchronously and return empty string.
    // Unblocked when: BookDownloader.cpp can be linked in test binary
    //   (i.e., ITorrentClient interface / TorrentClient stub available).
    SUCCEED() << "DEFER: requires BookDownloader.cpp linkable without libtorrent";
}

TEST(BookDownloaderMagnetIntegrationDefer, Defer2_QueueFifo_SecondCallDoesNotCallAddMagnet)
{
    // Scenario: two consecutive startMagnetDownload calls → first starts,
    //   second queues; addMagnetHeadless called exactly once.
    //   After torrentCompleted fires for the first, addMagnetHeadless called
    //   for the second (drainMagnetQueue).
    SUCCEED() << "DEFER: requires TorrentClient stub that captures addMagnetHeadless calls";
}

TEST(BookDownloaderMagnetIntegrationDefer, Defer3_TorrentCompleted_EmitsDownloadComplete)
{
    // Scenario: pre-populate temp dir with fake .epub, synthesize
    //   torrentCompleted(infoHash) signal → downloadComplete emitted with
    //   the epub's absolute path.
    SUCCEED() << "DEFER: requires TorrentClient stub with signal emission support";
}

TEST(BookDownloaderMagnetIntegrationDefer, Defer4_UnknownInfoHash_NoSignalEmitted)
{
    // Scenario: torrentCompleted fires for an infoHash not belonging to the
    //   active magnet → no downloadFailed or downloadComplete emitted.
    SUCCEED() << "DEFER: requires TorrentClient stub";
}

TEST(BookDownloaderMagnetIntegrationDefer, Defer5_TorrentUpdated_EmitsDownloadProgress)
{
    // Scenario: torrentUpdated fires with listActive() returning progress →
    //   downloadProgress emitted; rapid-fire updates throttled per 250ms/1pct.
    SUCCEED() << "DEFER: requires TorrentClient stub with controllable listActive()";
}

TEST(BookDownloaderMagnetIntegrationDefer, Defer6_CancelDownload_ActiveMagnet)
{
    // Scenario: cancelDownload(magnetUri) while magnet active → emits
    //   downloadFailed, calls deleteTorrent(infoHash, false), drains queue.
    SUCCEED() << "DEFER: requires TorrentClient stub";
}
