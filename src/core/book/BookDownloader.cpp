#include "BookDownloader.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

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

BookDownloader::BookDownloader(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

BookDownloader::~BookDownloader()
{
    if (m_active) {
        closeAndDeletePart(*m_active);
        delete m_active;
        m_active = nullptr;
    }
}

bool BookDownloader::isActive(const QString& md5) const
{
    if (m_active && m_active->md5 == md5) return true;
    for (const InFlight& q : m_queue) {
        if (q.md5 == md5) return true;
    }
    return false;
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
    // Active slot cancel
    if (m_active && m_active->md5 == md5) {
        failAndCleanup(*m_active, QStringLiteral("cancelled by user"));
        return;
    }
    // Queue cancel
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].md5 == md5) {
            emit downloadFailed(md5, QStringLiteral("cancelled by user (queued)"));
            m_queue.removeAt(i);
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
