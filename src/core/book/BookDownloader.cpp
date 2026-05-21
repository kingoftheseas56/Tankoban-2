#include "BookDownloader.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDirIterator>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

#include "core/torrent/TorrentClient.h"

namespace {

// Match LibGenScraper's UA string — some mirrors (esp. CF-protected CDNs
// like cdn2.booksdl.lc) may flag plain `curl/*` or bare Qt defaults.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// Safety margin above expectedBytes — disk-space pre-check. 50 MB covers
// filesystem overhead, journal rotation, and the .part+final coexistence
// moment during rename.
constexpr qint64 kDiskSpaceSafetyBytes = 50LL * 1024 * 1024;

// Progress-emit throttle budgets. Whichever condition fires first drives
// an emit; final emit always fires on finished() regardless.
constexpr int    kProgressThrottleMs    = 500;
constexpr qint64 kProgressThrottleBytes = 512LL * 1024;

// Retry policy — copied from MangaDownloader (proven in manga chapter
// flows): 3 attempts per URL with exponential backoff 2s / 4s / 8s.
constexpr int kMaxAttempts = 3;

// Magnet metadata / completion timeout. Five minutes covers tracker-warm
// + handshake + a generous metadata fetch window; if a torrent hasn't even
// announced bytes-total by then, the magnet is almost certainly dead-tracker
// + dead-peers and the user is better off failing fast than blocking the
// queue forever. Brotherhood code-review of c7acf74 added this guard.
constexpr int kMagnetTimeoutMs = 5 * 60 * 1000;

// Bounded recursive subdir search for pickBestBookFile. Libtorrent commonly
// nests payloads two levels deep (savePath/torrent-name/file.epub) but legit
// torrents rarely exceed ~4. Cap protects against pathological zip-bomb-style
// torrents without ruining real-world payloads.
constexpr int kMagnetWalkMaxDepth = 6;

// Backoff delay for attempt N (0-based).
int attemptDelayMs(int attempt)
{
    switch (attempt) {
    case 0: return 0;           // first try immediate
    case 1: return 2000;
    case 2: return 4000;
    default: return 8000;
    }
}

// Strip path separators + NTFS-illegal chars from a filename candidate.
QString sanitizeFilename(const QString& raw)
{
    static const QRegularExpression kBadCharRe(
        QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QString s = raw;
    s.replace(kBadCharRe, QStringLiteral("_"));
    s = s.trimmed();
    // Windows reserves trailing '.' and ' '.
    while (s.endsWith(QChar('.')) || s.endsWith(QChar(' '))) s.chop(1);
    if (s.isEmpty()) s = QStringLiteral("download");
    // Cap absurdly long names — NTFS max 255 per segment.
    if (s.size() > 200) s = s.left(200);
    return s;
}

// Parse Content-Disposition for a filename attribute. Best-effort — returns
// empty on malformed header. Caller still sanitizes before writing.
QString filenameFromContentDisposition(const QString& cd)
{
    if (cd.isEmpty()) return {};
    // RFC 6266 style + legacy ASCII "filename=..." — try filename* first
    // (UTF-8), then plain filename.
    static const QRegularExpression kFilenameStarRe(
        QStringLiteral(R"RX(filename\*\s*=\s*(?:UTF-8|utf-8)'[^']*'([^;]+))RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFilenameRe(
        QStringLiteral(R"RX(filename\s*=\s*"([^"]+)")RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFilenameBareRe(
        QStringLiteral(R"RX(filename\s*=\s*([^;]+))RX"),
        QRegularExpression::CaseInsensitiveOption);

    auto m = kFilenameStarRe.match(cd);
    if (m.hasMatch()) return QUrl::fromPercentEncoding(m.captured(1).toLatin1()).trimmed();
    m = kFilenameRe.match(cd);
    if (m.hasMatch()) return m.captured(1).trimmed();
    m = kFilenameBareRe.match(cd);
    if (m.hasMatch()) return m.captured(1).trimmed();
    return {};
}

} // namespace

BookDownloader::BookDownloader(QNetworkAccessManager* nam,
                               TorrentClient* torrentClient,
                               QObject* parent)
    : QObject(parent)
    , m_nam(nam)
    , m_torrentClient(torrentClient)
{
}

QString BookDownloader::startMagnetDownload(const QString& magnetUri,
                                            const QString& destinationDir,
                                            const QString& suggestedName,
                                            const QString& expectedFormat)
{
    if (!m_torrentClient) {
        emit downloadFailed(magnetUri,
            QStringLiteral("BookDownloader::startMagnetDownload requires a TorrentClient (not wired)"));
        return {};
    }

    // Queue FIFO if a magnet download is already active.
    if (m_activeMagnet) {
        qInfo() << "[BookDownloader] queuing magnet download"
                << magnetUri
                << "(active infoHash is" << m_activeMagnet->infoHash << ")";
        MagnetInFlight q;
        q.magnetUri      = magnetUri;
        q.destinationDir = destinationDir;
        q.suggestedName  = suggestedName;
        q.expectedFormat = expectedFormat.toLower();
        m_magnetQueue.append(std::move(q));
        return magnetUri;
    }

    // Kick off the libtorrent-side download.
    const QString infoHash = m_torrentClient->addMagnetHeadless(
        magnetUri, QStringLiteral("books"), destinationDir);

    if (infoHash.isEmpty()) {
        emit downloadFailed(magnetUri,
            QStringLiteral("addMagnetHeadless failed (duplicate or metadata error)"));
        return {};
    }

    // Populate the active-magnet slot.
    m_activeMagnet = new MagnetInFlight;
    m_activeMagnet->infoHash      = infoHash;
    m_activeMagnet->magnetUri     = magnetUri;
    m_activeMagnet->destinationDir= destinationDir;
    m_activeMagnet->suggestedName = suggestedName;
    m_activeMagnet->expectedFormat= expectedFormat.toLower();

    qInfo() << "[BookDownloader] magnet started infoHash=" << infoHash
            << "magnetUri=" << magnetUri;

    // Wire TorrentClient signals via the guarded helper — at most one
    // connection pair active at a time regardless of reentry shape.
    connectMagnetSignals();

    // Arm the metadata / completion timeout. Lazily construct on first use.
    if (!m_magnetTimeoutTimer) {
        m_magnetTimeoutTimer = new QTimer(this);
        m_magnetTimeoutTimer->setSingleShot(true);
        connect(m_magnetTimeoutTimer, &QTimer::timeout,
                this, &BookDownloader::onMagnetTimeout);
    }
    m_magnetTimeoutTimer->start(kMagnetTimeoutMs);

    return magnetUri;
}

void BookDownloader::connectMagnetSignals()
{
    if (m_magnetSignalsConnected || !m_torrentClient) return;
    connect(m_torrentClient, &TorrentClient::torrentCompleted,
            this, &BookDownloader::onMagnetTorrentCompleted,
            Qt::QueuedConnection);
    connect(m_torrentClient, &TorrentClient::torrentUpdated,
            this, &BookDownloader::onMagnetTorrentUpdated,
            Qt::QueuedConnection);
    m_magnetSignalsConnected = true;
}

void BookDownloader::disconnectMagnetSignals()
{
    if (!m_magnetSignalsConnected) return;
    if (m_torrentClient) {
        disconnect(m_torrentClient, &TorrentClient::torrentCompleted,
                   this, &BookDownloader::onMagnetTorrentCompleted);
        disconnect(m_torrentClient, &TorrentClient::torrentUpdated,
                   this, &BookDownloader::onMagnetTorrentUpdated);
    }
    m_magnetSignalsConnected = false;
    if (m_magnetTimeoutTimer) m_magnetTimeoutTimer->stop();
}

void BookDownloader::onMagnetTimeout()
{
    if (!m_activeMagnet) return;
    const QString handle = m_activeMagnet->magnetUri;
    const QString infoHash = m_activeMagnet->infoHash;
    qWarning() << "[BookDownloader] magnet timeout infoHash=" << infoHash
               << "after" << (kMagnetTimeoutMs / 1000) << "s with no completion."
               << "Aborting + draining queue.";
    // Best-effort libtorrent-side teardown — same surface cancelDownload uses.
    if (m_torrentClient) m_torrentClient->deleteTorrent(infoHash, false);
    emit downloadFailed(handle,
        QStringLiteral("Magnet timed out after %1s with no metadata / completion"
                       " (likely dead tracker + no peers)")
            .arg(kMagnetTimeoutMs / 1000));
    drainMagnetQueue();
}

// ── Magnet-path slot: completion ────────────────────────────────────────────

void BookDownloader::onMagnetTorrentCompleted(const QString& infoHash)
{
    if (!m_activeMagnet || m_activeMagnet->infoHash != infoHash) return;

    const QString handle = m_activeMagnet->magnetUri;

    qInfo() << "[BookDownloader] magnet complete infoHash=" << infoHash
            << "magnetUri=" << handle;

    // Emit 100% progress before completion.
    const qint64 total = (m_activeMagnet->bytesTotal > 0)
        ? m_activeMagnet->bytesTotal : m_activeMagnet->bytesReceived;
    if (total > 0) {
        emit downloadProgress(handle, total, total);
    }

    const QString bestFile = pickBestBookFile(*m_activeMagnet);
    if (bestFile.isEmpty()) {
        emit downloadFailed(handle,
            QStringLiteral("no qualifying book file found in torrent payload"));
    } else {
        emit downloadComplete(handle, bestFile);
    }

    drainMagnetQueue();
}

// ── Magnet-path slot: progress ───────────────────────────────────────────────

void BookDownloader::onMagnetTorrentUpdated(const QString& infoHash)
{
    if (!m_activeMagnet || m_activeMagnet->infoHash != infoHash) return;

    // Walk listActive() to find per-torrent progress fields.
    const QList<TorrentInfo> active = m_torrentClient->listActive();
    for (const TorrentInfo& t : active) {
        if (t.infoHash != infoHash) continue;

        const qint64 received = t.totalDone;
        const qint64 total    = t.totalWanted;
        const int    pct      = (total > 0)
            ? static_cast<int>(100.0 * received / total)
            : 0;

        // Throttle: emit at most once per 250 ms OR when pct advances by ≥1.
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 elapsedMs = nowMs - m_activeMagnet->lastProgressMs;
        const int    deltaPct  = pct - m_activeMagnet->lastProgressPct;
        const bool   pass      = (elapsedMs >= 250) || (deltaPct >= 1);

        if (pass) {
            m_activeMagnet->bytesReceived   = received;
            m_activeMagnet->bytesTotal      = total;
            m_activeMagnet->lastProgressMs  = nowMs;
            m_activeMagnet->lastProgressPct = pct;
            emit downloadProgress(m_activeMagnet->magnetUri, received, total);
        }
        break;
    }
}

// ── Magnet-path helpers ──────────────────────────────────────────────────────

QString BookDownloader::pickBestBookFile(const MagnetInFlight& m) const
{
    // Book file extensions we consider "qualifying". Ordered by preference so
    // epub > pdf > mobi if we have to pick from mixed junk.
    static const QStringList kBookExts = {
        QStringLiteral("epub"),
        QStringLiteral("pdf"),
        QStringLiteral("mobi"),
        QStringLiteral("azw3"),
        QStringLiteral("azw"),
        QStringLiteral("djvu"),
        QStringLiteral("cbz"),
        QStringLiteral("cbr"),
    };

    const QDir dir(m.destinationDir);
    if (!dir.exists()) return {};

    // Collect all qualifying files recursively under destinationDir. Depth is
    // bounded by kMagnetWalkMaxDepth to defang pathological zip-bomb torrents.
    // Brotherhood code-review of c7acf74 flagged the original one-level scan:
    // libtorrent commonly nests payloads two levels deep (savePath/torrent-name
    // /file.epub) and even legit scanlation packs occasionally reach three.
    QStringList nameFilters;
    for (const QString& ext : kBookExts) {
        nameFilters << QStringLiteral("*.") + ext;
    }
    QFileInfoList candidates;
    const QString rootPath = dir.absolutePath();
    QDirIterator it(rootPath, nameFilters,
                    QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        // Cheap depth check: relative path segment count from the root.
        const QString rel = dir.relativeFilePath(fi.absoluteFilePath());
        const int depth = rel.count(QLatin1Char('/'));
        if (depth > kMagnetWalkMaxDepth) continue;
        candidates << fi;
    }

    if (candidates.isEmpty()) return {};

    // Single qualifying file — return it directly. BooksScanner picks it up
    // without any move needed.
    if (candidates.size() == 1) {
        return candidates.first().absoluteFilePath();
    }

    // Multiple files. If caller specified an expectedFormat, prefer that ext.
    QFileInfoList preferred;
    if (!m.expectedFormat.isEmpty()) {
        for (const QFileInfo& fi : candidates) {
            if (fi.suffix().compare(m.expectedFormat, Qt::CaseInsensitive) == 0) {
                preferred << fi;
            }
        }
    }

    const QFileInfoList& pool = preferred.isEmpty() ? candidates : preferred;

    // From the pool, pick the largest file (heuristic: junk payloads tend to
    // pad small files; the real book is usually the biggest one). Use the
    // filePath-empty sentinel (not exists()) so we don't stat the filesystem
    // on every iteration (brotherhood code-review note c7acf74).
    QFileInfo best;
    for (const QFileInfo& fi : pool) {
        if (best.filePath().isEmpty() || fi.size() > best.size()) {
            best = fi;
        }
    }

    if (best.filePath().isEmpty()) return {};

    // If the best file lives in a subdirectory, move it up to destinationDir
    // so BooksScanner sees it at the library root level. Sanitize the target
    // name to scrub NTFS-illegal chars that torrent payloads occasionally
    // carry (parity with the HTTP path's sanitizeFilename — brotherhood
    // code-review of c7acf74 caught the asymmetry).
    if (best.absoluteDir() != dir) {
        const QString safeName = sanitizeFilename(best.fileName());
        const QString target = dir.absoluteFilePath(safeName);
        // Overwrite if a same-named file already sits at the root.
        if (QFile::exists(target)) QFile::remove(target);
        if (QFile::rename(best.absoluteFilePath(), target)) {
            return target;
        }
        // rename failed — return the original nested path (scanner will still
        // find it if BooksScanner walks subfolders).
        qWarning() << "[BookDownloader] could not move" << best.absoluteFilePath()
                   << "to" << target << "— returning nested path";
    }

    return best.absoluteFilePath();
}

void BookDownloader::drainMagnetQueue()
{
    // Disconnect TorrentClient signals + stop the metadata timeout via the
    // guarded helper. Re-entry through startMagnetDownload below re-arms both
    // for the next magnet, so the connection-pair count is invariant at <= 1.
    disconnectMagnetSignals();

    delete m_activeMagnet;
    m_activeMagnet = nullptr;

    if (!m_magnetQueue.isEmpty()) {
        MagnetInFlight next = std::move(m_magnetQueue.takeFirst());
        // Re-enter startMagnetDownload for the next queued item.
        startMagnetDownload(next.magnetUri,
                            next.destinationDir,
                            next.suggestedName,
                            next.expectedFormat);
    }
}

BookDownloader::~BookDownloader()
{
    if (m_active) {
        closeAndDeletePart(*m_active);
        delete m_active;
        m_active = nullptr;
    }
    // Magnet path: disconnect signals + release state without calling
    // drainMagnetQueue (drainMagnetQueue would re-enter startMagnetDownload
    // which is unsafe in the destructor).
    disconnectMagnetSignals();
    delete m_activeMagnet;
    m_activeMagnet = nullptr;
    m_magnetQueue.clear();
}

bool BookDownloader::isActive(const QString& md5) const
{
    if (m_active && m_active->md5 == md5) return true;
    for (const InFlight& q : m_queue) {
        if (q.md5 == md5) return true;
    }
    return false;
}

QJsonObject BookDownloader::devSnapshot() const
{
    // v1.5 Phase D.3 (2026-05-19) — agent-readable snapshot for the
    // sources-get-tankolibrary-state command.
    QJsonObject snap;
    if (m_active) {
        QJsonObject a;
        a["md5"]             = m_active->md5;
        a["urlIdx"]          = m_active->urlIdx;
        a["urlCount"]        = m_active->urls.size();
        a["attempt"]         = m_active->attempt;
        a["destinationDir"]  = m_active->destinationDir;
        a["suggestedName"]   = m_active->suggestedName;
        a["expectedBytes"]   = static_cast<double>(m_active->expectedBytes);
        a["receivedBytes"]   = static_cast<double>(m_active->receivedBytes);
        snap["active"] = a;
    } else {
        snap["active"] = QJsonValue::Null;
    }
    QJsonArray queueArr;
    for (const InFlight& q : m_queue) {
        QJsonObject o;
        o["md5"]            = q.md5;
        o["destinationDir"] = q.destinationDir;
        o["suggestedName"]  = q.suggestedName;
        o["expectedBytes"]  = static_cast<double>(q.expectedBytes);
        queueArr.append(o);
    }
    snap["queue"] = queueArr;
    snap["queueLength"] = queueArr.size();
    return snap;
}

QString BookDownloader::startDownload(const QString& md5,
                                      const QStringList& urls,
                                      const QString& destinationDir,
                                      const QString& suggestedName,
                                      qint64 expectedBytes)
{
    const QString trimmedMd5 = md5.trimmed();
    if (trimmedMd5.isEmpty()) {
        emit downloadFailed(trimmedMd5, QStringLiteral("empty md5"));
        return {};
    }
    if (urls.isEmpty()) {
        emit downloadFailed(trimmedMd5, QStringLiteral("no download URLs supplied"));
        return {};
    }
    if (destinationDir.isEmpty()) {
        emit downloadFailed(trimmedMd5,
                            QStringLiteral("no destination directory (library path empty?)"));
        return {};
    }
    if (isActive(trimmedMd5)) {
        emit downloadFailed(trimmedMd5, QStringLiteral("download already in flight"));
        return {};
    }

    InFlight f;
    f.md5            = trimmedMd5;
    f.urls           = urls;
    f.urlIdx         = 0;
    f.attempt        = 0;
    f.destinationDir = destinationDir;
    f.suggestedName  = sanitizeFilename(suggestedName);
    f.expectedBytes  = expectedBytes;

    if (m_active) {
        qInfo() << "[BookDownloader] queuing download for md5" << trimmedMd5
                << "(active md5 is" << m_active->md5 << ")";
        m_queue.append(std::move(f));
        return trimmedMd5;
    }

    m_active = new InFlight(std::move(f));
    startAttempt(*m_active);
    return trimmedMd5;
}

void BookDownloader::cancelDownload(const QString& md5)
{
    // Active HTTP-path slot cancel
    if (m_active && m_active->md5 == md5) {
        failAndCleanup(*m_active, QStringLiteral("cancelled by user"));
        return;
    }
    // HTTP-path queue cancel
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].md5 == md5) {
            emit downloadFailed(md5, QStringLiteral("cancelled by user (queued)"));
            m_queue.removeAt(i);
            return;
        }
    }

    // Magnet-path cancel — handle is the magnetUri (same string returned by
    // startMagnetDownload). TorrentClient has deleteTorrent(infoHash, deleteFiles)
    // as the removal surface; we call it with deleteFiles=false so the partially-
    // downloaded files stay on disk (caller decides if they want to clean up).
    // NOTE: TorrentClient does NOT have a dedicated "cancelDownload" method; the
    // correct surface is deleteTorrent(infoHash, false).
    if (m_activeMagnet && m_activeMagnet->magnetUri == md5) {
        const QString infoHash = m_activeMagnet->infoHash;
        emit downloadFailed(md5, QStringLiteral("cancelled by user"));
        // Tell libtorrent to release this torrent BEFORE the queue drain
        // starts the next magnet (brotherhood code-review of c7acf74 flagged
        // the inverted order — next magnet could race against cleanup if
        // they share libtorrent resources). deleteFiles=false keeps partial
        // payloads on disk; caller decides cleanup policy.
        if (m_torrentClient && !infoHash.isEmpty()) {
            m_torrentClient->deleteTorrent(infoHash, /*deleteFiles=*/false);
        }
        drainMagnetQueue();   // disconnects signals + clears slot + starts next
        return;
    }
    // Magnet-path queue cancel
    for (int i = 0; i < m_magnetQueue.size(); ++i) {
        if (m_magnetQueue[i].magnetUri == md5) {
            emit downloadFailed(md5, QStringLiteral("cancelled by user (queued)"));
            m_magnetQueue.removeAt(i);
            return;
        }
    }
}

void BookDownloader::startAttempt(InFlight& f)
{
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("all mirror URLs exhausted"));
        return;
    }

    const QString url = f.urls.value(f.urlIdx);
    if (url.isEmpty()) {
        startNextUrlOrFail(f);
        return;
    }

    qInfo() << "[BookDownloader] attempt url"
            << f.urlIdx + 1 << "of" << f.urls.size()
            << "(retry" << f.attempt + 1 << "of" << kMaxAttempts << ")"
            << url;

    // Disk-space pre-check if we have an expected size. Skip silently if
    // LibGen reported "1 B" or other garbage we couldn't parse (caller
    // passed 0).
    if (f.expectedBytes > 0) {
        const QStorageInfo storage(f.destinationDir);
        if (storage.isValid() && storage.isReady()) {
            const qint64 avail = storage.bytesAvailable();
            if (avail < f.expectedBytes + kDiskSpaceSafetyBytes) {
                failAndCleanup(f, QString(QStringLiteral(
                    "insufficient disk space at %1 (need %2 bytes, have %3)"))
                    .arg(f.destinationDir)
                    .arg(f.expectedBytes + kDiskSpaceSafetyBytes)
                    .arg(avail));
                return;
            }
        }
    }

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    // Accept anything — LibGen CDN serves application/octet-stream;
    // EPUBs are just zips but the server may not know the MIME.
    req.setRawHeader("Accept", "*/*");

    // Apply backoff delay before firing the request. On attempt 0 (first
    // try or fresh URL-failover), delay is 0 → issue request immediately.
    const int delay = attemptDelayMs(f.attempt);
    if (delay <= 0) {
        // Open the .part file — existing file from a prior attempt is
        // truncated; v1 has no resume (Range-request) support.
        if (!pickTargetFilename(f, QString())) {
            failAndCleanup(f, QStringLiteral("could not prepare destination path"));
            return;
        }
        f.file = new QFile(f.partPath);
        if (!f.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString err = f.file->errorString();
            delete f.file;
            f.file = nullptr;
            failAndCleanup(f, QString(QStringLiteral("cannot open .part file: %1"))
                             .arg(err));
            return;
        }
        f.receivedBytes     = 0;
        f.sanityChecked     = false;
        f.lastProgressEmit  = 0;
        f.lastProgressBytes = 0;

        QNetworkReply* reply = m_nam->get(req);
        f.reply = reply;
        connect(reply, &QNetworkReply::readyRead,
                this, &BookDownloader::onReadyRead);
        connect(reply, &QNetworkReply::finished,
                this, &BookDownloader::onFinished);
        connect(reply, &QNetworkReply::downloadProgress,
                this, &BookDownloader::onDownloadProgressFromReply);
    } else {
        // Schedule delayed start — captures `this` + md5 for re-entry-safe
        // dispatch. On re-entry we look up the active slot by md5 to make
        // sure we're still the active download (cancel may have changed it).
        const QString md5 = f.md5;
        QTimer::singleShot(delay, this, [this, md5]() {
            if (!m_active || m_active->md5 != md5) return;
            // Clear the delay counter (attempt already incremented before
            // this schedule fired) and re-enter startAttempt with delay=0.
            InFlight& f2 = *m_active;
            if (!pickTargetFilename(f2, QString())) {
                failAndCleanup(f2, QStringLiteral("could not prepare destination path"));
                return;
            }
            f2.file = new QFile(f2.partPath);
            if (!f2.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                const QString err = f2.file->errorString();
                delete f2.file;
                f2.file = nullptr;
                failAndCleanup(f2, QString(QStringLiteral("cannot open .part file: %1"))
                                 .arg(err));
                return;
            }
            f2.receivedBytes     = 0;
            f2.sanityChecked     = false;
            f2.lastProgressEmit  = 0;
            f2.lastProgressBytes = 0;

            QNetworkRequest req2{QUrl(f2.urls.value(f2.urlIdx))};
            req2.setRawHeader("User-Agent", kUserAgent);
            req2.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                              QNetworkRequest::NoLessSafeRedirectPolicy);
            req2.setRawHeader("Accept", "*/*");
            QNetworkReply* reply2 = m_nam->get(req2);
            f2.reply = reply2;
            connect(reply2, &QNetworkReply::readyRead,
                    this, &BookDownloader::onReadyRead);
            connect(reply2, &QNetworkReply::finished,
                    this, &BookDownloader::onFinished);
            connect(reply2, &QNetworkReply::downloadProgress,
                    this, &BookDownloader::onDownloadProgressFromReply);
        });
    }
}

void BookDownloader::onReadyRead()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty()) return;

    // First-chunk sanity check — detect stale LibGen key (server returned
    // the ads.php HTML page instead of binary content).
    if (!f.sanityChecked) {
        f.sanityChecked = true;
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (detectStaleHtml(chunk, ct)) {
            qWarning() << "[BookDownloader] stale key detected for url"
                       << f.urls.value(f.urlIdx)
                       << "(Content-Type=" << ct << ") — failing over";
            // Disconnect BEFORE abort to prevent finished() re-entry muddying state.
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
            f.reply.clear();
            if (f.file) {
                f.file->close();
                f.file->remove();
                delete f.file;
                f.file = nullptr;
            }
            // Skip remaining retries for this URL — stale key is a URL-level
            // problem, not a transient network hiccup.
            startNextUrlOrFail(f);
            return;
        }

        // Honor Content-Disposition if present + safe. We ONLY update
        // f.finalPath — f.partPath must stay in sync with the already-open
        // QFile's on-disk filename (opened at startAttempt). Rename at
        // finalizeSuccess moves actual-partPath → new-finalPath.
        const QString cd = reply->header(QNetworkRequest::ContentDispositionHeader).toString();
        const QString cdParsed = filenameFromContentDisposition(cd);
        if (!cdParsed.isEmpty()) {
            const QString sanitized = sanitizeFilename(cdParsed);
            if (!sanitized.isEmpty()) {
                QDir dir(f.destinationDir);
                f.finalPath = dir.absoluteFilePath(sanitized);
            }
        }
    }

    if (f.file) {
        const qint64 written = f.file->write(chunk);
        if (written < 0) {
            failAndCleanup(f, QString(QStringLiteral("disk write failed: %1"))
                             .arg(f.file->errorString()));
            return;
        }
        f.receivedBytes += written;
    }
}

void BookDownloader::onDownloadProgressFromReply(qint64 received, qint64 total)
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;

    // Qt emits downloadProgress before readyRead on some platforms; the
    // canonical "bytes written to disk" count is f.receivedBytes (from
    // onReadyRead). Use the reply's `received` for progress reporting
    // only — it includes in-socket-buffer bytes not yet flushed to disk,
    // which is close enough for UI.
    const qint64 budgetBytes = received;

    // Throttle emits — don't flood the UI thread with per-kB updates.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = (f.lastProgressEmit == 0)
        ? (kProgressThrottleMs + 1)
        : (nowMs - f.lastProgressEmit);
    const qint64 deltaBytes = budgetBytes - f.lastProgressBytes;
    const bool passThreshold =
        (elapsedMs >= kProgressThrottleMs) ||
        (deltaBytes >= kProgressThrottleBytes);

    if (passThreshold) {
        f.lastProgressEmit  = nowMs;
        f.lastProgressBytes = budgetBytes;
        emit downloadProgress(f.md5, budgetBytes, total);
    }
}

void BookDownloader::onFinished()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();
    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Flush any trailing bytes we haven't read yet.
    if (err == QNetworkReply::NoError) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && f.file) {
            f.file->write(tail);
            f.receivedBytes += tail.size();
        }
    }

    reply->deleteLater();
    f.reply.clear();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[BookDownloader] reply error" << err
                   << "http=" << httpStatus << "msg=" << errString;
        retryOrFailover(f, QString(QStringLiteral("HTTP error: %1 (status %2)"))
                          .arg(errString).arg(httpStatus));
        return;
    }

    // Success path — emit final progress at 100% using receivedBytes as
    // the authoritative total (the reply's downloadProgress `total` may
    // have been -1 if server didn't send Content-Length).
    emit downloadProgress(f.md5, f.receivedBytes, f.receivedBytes);
    finalizeSuccess(f);
}

void BookDownloader::finalizeSuccess(InFlight& f)
{
    if (f.file) {
        f.file->close();
        delete f.file;
        f.file = nullptr;
    }

    // Sanity-check that we actually received bytes. A 0-byte file means
    // something went wrong silently (server closed with nothing to send).
    if (f.receivedBytes <= 0) {
        QFile::remove(f.partPath);
        failAndCleanup(f, QStringLiteral("server returned empty body"));
        return;
    }

    // Rename .part -> final. If final already exists (user re-downloads
    // the same book), we overwrite.
    if (QFile::exists(f.finalPath)) {
        QFile::remove(f.finalPath);
    }
    if (!QFile::rename(f.partPath, f.finalPath)) {
        const QString reason = QString(QStringLiteral(
            "rename %1 -> %2 failed")).arg(f.partPath, f.finalPath);
        QFile::remove(f.partPath);
        failAndCleanup(f, reason);
        return;
    }

    const QString finalPath = f.finalPath;
    const QString md5 = f.md5;

    qInfo() << "[BookDownloader] complete md5=" << md5
            << "path=" << finalPath
            << "bytes=" << f.receivedBytes;

    emit downloadComplete(md5, finalPath);

    // Clear active slot + drain queue (one at a time).
    delete m_active;
    m_active = nullptr;
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue.takeFirst()));
        startAttempt(*m_active);
    }
}

void BookDownloader::retryOrFailover(InFlight& f, const QString& reason)
{
    // Clean up the failed .part; we don't support resume in v1, so next
    // attempt re-opens from 0.
    closeAndDeletePart(f);

    f.attempt += 1;
    if (f.attempt < kMaxAttempts) {
        qInfo() << "[BookDownloader] retrying url" << f.urlIdx
                << "attempt" << f.attempt + 1 << "/" << kMaxAttempts
                << "(reason:" << reason << ")";
        startAttempt(f);
        return;
    }

    // Exhausted retries on this URL — try next URL.
    qInfo() << "[BookDownloader] url exhausted, failover:" << reason;
    startNextUrlOrFail(f);
}

void BookDownloader::startNextUrlOrFail(InFlight& f)
{
    f.urlIdx += 1;
    f.attempt = 0;
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("all mirror URLs failed"));
        return;
    }
    startAttempt(f);
}

void BookDownloader::failAndCleanup(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    const QString md5 = f.md5;
    emit downloadFailed(md5, reason);

    delete m_active;
    m_active = nullptr;
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue.takeFirst()));
        startAttempt(*m_active);
    }
}

void BookDownloader::closeAndDeletePart(InFlight& f)
{
    if (f.reply) {
        QNetworkReply* r = f.reply.data();
        if (r) {
            r->disconnect(this);
            r->abort();
            r->deleteLater();
        }
        f.reply.clear();
    }
    if (f.file) {
        f.file->close();
        const QString path = f.file->fileName();
        delete f.file;
        f.file = nullptr;
        QFile::remove(path);
    } else if (!f.partPath.isEmpty() && QFile::exists(f.partPath)) {
        QFile::remove(f.partPath);
    }
}

bool BookDownloader::detectStaleHtml(const QByteArray& firstChunk,
                                     const QString& contentType) const
{
    // Fast path — server declared text/html MIME; it's not a binary file.
    if (contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)) {
        return true;
    }
    // Slow path — server didn't send Content-Type OR claimed octet-stream
    // but served HTML anyway. Peek at the first chunk.
    if (firstChunk.size() >= 5) {
        const QByteArray head = firstChunk.left(512).trimmed().toLower();
        if (head.startsWith("<!doctype html") ||
            head.startsWith("<html") ||
            head.startsWith("<!doctype")) {
            return true;
        }
    }
    return false;
}

bool BookDownloader::pickTargetFilename(InFlight& f, const QString& contentDisposition)
{
    QDir dir(f.destinationDir);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            qWarning() << "[BookDownloader] mkpath failed for" << f.destinationDir;
            return false;
        }
    }

    QString chosen = f.suggestedName;
    if (!contentDisposition.isEmpty()) {
        const QString cdName = filenameFromContentDisposition(contentDisposition);
        if (!cdName.isEmpty()) {
            chosen = sanitizeFilename(cdName);
        }
    }
    if (chosen.isEmpty()) chosen = sanitizeFilename(f.suggestedName);

    f.finalPath = dir.absoluteFilePath(chosen);
    f.partPath  = f.finalPath + QStringLiteral(".part");
    return true;
}
