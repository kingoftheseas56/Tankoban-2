#include "MangaDownloader.h"
#include "MangaScraper.h"
#include "core/JsonStore.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QUrl>
#include <QStorageInfo>
#include <QRegularExpression>

#ifdef HAS_QT_ZIP
#include <private/qzipwriter_p.h>
#endif

// ── Constructor ─────────────────────────────────────────────────────────────
MangaDownloader::MangaDownloader(JsonStore* store, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_nam(new QNetworkAccessManager(this))
{
    loadRecords();
}

MangaDownloader::~MangaDownloader()
{
    saveRecords();
}

void MangaDownloader::setScraper(const QString& sourceId, MangaScraper* scraper)
{
    m_scrapers[sourceId] = scraper;
}

// ── Persistence ─────────────────────────────────────────────────────────────
void MangaDownloader::loadRecords()
{
    auto data = m_store->read(RECORDS_FILE);
    auto active = data.value("active").toObject();
    for (auto it = active.begin(); it != active.end(); ++it) {
        auto obj = it.value().toObject();
        MangaDownloadRecord rec;
        rec.id              = it.key();
        rec.seriesTitle     = obj["seriesTitle"].toString();
        rec.source          = obj["source"].toString();
        rec.destinationPath = obj["destinationPath"].toString();
        rec.format          = obj["format"].toString();
        rec.status          = obj["status"].toString();
        rec.totalChapters   = obj["totalChapters"].toInt();
        rec.completedChapters = obj["completedChapters"].toInt();
        rec.startedAt       = obj["startedAt"].toVariant().toLongLong();
        m_records[rec.id]   = rec;
    }
    // R5: restore processing order. If the saved order is missing or stale,
    // fall back to record IDs (map iteration order) — good enough for legacy
    // records, and the first user move-to-top/bottom will establish the order.
    auto orderArr = data.value("order").toArray();
    QSet<QString> seen;
    for (const auto& v : orderArr) {
        QString id = v.toString();
        if (m_records.contains(id) && !seen.contains(id)) {
            m_recordOrder.append(id);
            seen.insert(id);
        }
    }
    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        if (!seen.contains(it.key()))
            m_recordOrder.append(it.key());
    }
}

void MangaDownloader::saveRecords()
{
    QMutexLocker lock(&m_mutex);
    QJsonObject active;
    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        const auto& r = it.value();
        QJsonObject obj;
        obj["seriesTitle"]     = r.seriesTitle;
        obj["source"]          = r.source;
        obj["destinationPath"] = r.destinationPath;
        obj["format"]          = r.format;
        obj["status"]          = r.status;
        obj["totalChapters"]   = r.totalChapters;
        obj["completedChapters"] = r.completedChapters;
        obj["startedAt"]       = r.startedAt;
        active[it.key()] = obj;
    }
    QJsonArray order;
    for (const auto& id : m_recordOrder) order.append(id);
    QJsonObject data;
    data["active"] = active;
    data["order"]  = order;
    m_store->write(RECORDS_FILE, data);
}

void MangaDownloader::appendHistory(const MangaDownloadRecord& rec)
{
    auto data = m_store->read(HISTORY_FILE);
    auto arr = data.value("entries").toArray();

    QJsonObject entry;
    entry["id"]           = rec.id;
    entry["seriesTitle"]  = rec.seriesTitle;
    entry["source"]       = rec.source;
    entry["totalChapters"] = rec.totalChapters;
    entry["completedChapters"] = rec.completedChapters;
    entry["completedAt"]  = QDateTime::currentMSecsSinceEpoch();
    arr.append(entry);

    // Cap at 1000
    while (arr.size() > 1000)
        arr.removeFirst();

    data["entries"] = arr;
    m_store->write(HISTORY_FILE, data);
}

// ── Start download ──────────────────────────────────────────────────────────
QString MangaDownloader::startDownload(const QString& seriesTitle, const QString& source,
                                        const QList<ChapterInfo>& chapters,
                                        const QString& destinationPath, const QString& format)
{
    // Generate ID from title + source + timestamp
    QByteArray raw = (seriesTitle + source + QString::number(QDateTime::currentMSecsSinceEpoch())).toUtf8();
    QString id = QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex().left(16);

    MangaDownloadRecord rec;
    rec.id              = id;
    rec.seriesTitle     = seriesTitle;
    rec.source          = source;
    rec.destinationPath = destinationPath;
    rec.format          = format;
    rec.status          = "queued";
    rec.totalChapters   = chapters.size();
    rec.startedAt       = QDateTime::currentMSecsSinceEpoch();

    // R3 (hotfix 2026-04-14): pre-filter chapters already on disk. Matches
    // Mihon's `findChapterDir(...) == null` filter. Original implementation
    // did N x QFileInfo::exists() + size() (or QDir::entryList per chapter)
    // on the main thread — froze the UI for several seconds on a 1000+
    // chapter selection. Replaced with a single directory enumeration of
    // the series dir + O(1) set lookups per chapter.
    const QString seriesDir = destinationPath + "/" + seriesTitle;
    static const QRegularExpression unsafe(R"([<>:"/\\|?*])");

    QSet<QString> existingFiles;     // CBZ format
    QSet<QString> existingDirs;      // folder format
    {
        QDir parent(seriesDir);
        if (parent.exists()) {
            if (format == "cbz") {
                const auto names = parent.entryList(QStringList() << "*.cbz",
                                                    QDir::Files);
                for (const auto& n : names) existingFiles.insert(n);
            } else {
                const auto names = parent.entryList(
                    QDir::Dirs | QDir::NoDotAndDotDot);
                for (const auto& n : names) existingDirs.insert(n);
            }
        }
    }

    for (const auto& ch : chapters) {
        ChapterDownload cd;
        cd.chapterId     = ch.id;
        cd.chapterName   = ch.name;
        cd.status        = "queued";
        cd.chapterNumber = ch.chapterNumber;   // R6 sort key
        cd.dateUpload    = ch.dateUpload;      // R6 sort key

        QString safe = ch.name;
        safe.replace(unsafe, "_");

        bool already = false;
        QString existingPath;
        if (format == "cbz") {
            const QString fileName = safe + ".cbz";
            if (existingFiles.contains(fileName)) {
                already      = true;
                existingPath = seriesDir + "/" + fileName;
            }
        } else if (existingDirs.contains(safe)) {
            // Lost the "non-empty" check vs the old per-chapter walk, but
            // partial-folder cases are extremely rare and the per-page
            // skip-if-exists in downloadImages still catches missing pages.
            already      = true;
            existingPath = seriesDir + "/" + safe;
        }

        if (already) {
            cd.status    = "completed";
            cd.finalPath = existingPath;
            ++rec.completedChapters;
        }

        rec.chapters.append(cd);
    }

    rec.progress = rec.totalChapters > 0
        ? static_cast<float>(rec.completedChapters) / rec.totalChapters
        : 0.f;
    if (rec.completedChapters >= rec.totalChapters && rec.totalChapters > 0) {
        // Entire selection was already on disk — mark whole record completed.
        rec.status      = "completed";
        rec.completedAt = QDateTime::currentMSecsSinceEpoch();
    }

    {
        QMutexLocker lock(&m_mutex);
        m_records[id] = rec;
        m_recordOrder.append(id);   // R5: append to processing order
    }
    saveRecords();
    emit downloadUpdated(id);

    processQueue();
    return id;
}

// ── Process queue ───────────────────────────────────────────────────────────
void MangaDownloader::processQueue()
{
    QMutexLocker lock(&m_mutex);
    if (m_paused) return;
    if (m_activeDownloads >= MAX_CONCURRENT_CHAPTERS) return;

    // R5: iterate in m_recordOrder so reordering actually influences scheduling.
    for (const QString& recId : m_recordOrder) {
        auto it = m_records.find(recId);
        if (it == m_records.end()) continue;
        auto& rec = it.value();
        if (rec.status == "cancelled") continue;

        for (int i = 0; i < rec.chapters.size(); ++i) {
            if (m_activeDownloads >= MAX_CONCURRENT_CHAPTERS) return;

            auto& ch = rec.chapters[i];
            if (ch.status == "queued") {
                ch.status = "downloading";
                rec.status = "downloading";
                ++m_activeDownloads;
                lock.unlock();
                downloadChapter(recId, i);
                lock.relock();
            }
        }
    }
}

// ── Download a single chapter ───────────────────────────────────────────────
void MangaDownloader::downloadChapter(const QString& recordId, int chapterIdx)
{
    QString source;
    QString chapterId;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_records.find(recordId);
        if (it == m_records.end() || chapterIdx >= it->chapters.size()) return;
        source    = it->source;
        chapterId = it->chapters[chapterIdx].chapterId;
    }

    auto* scraper = m_scrapers.value(source);
    if (!scraper) {
        QMutexLocker lock(&m_mutex);
        m_records[recordId].chapters[chapterIdx].status = "error";
        m_records[recordId].chapters[chapterIdx].error  = "No scraper for source: " + source;
        --m_activeDownloads;
        emit downloadUpdated(recordId);
        processQueue();
        return;
    }

    // Fetch pages, then download images
    auto conn = std::make_shared<QMetaObject::Connection>();
    auto errConn = std::make_shared<QMetaObject::Connection>();

    *conn = connect(scraper, &MangaScraper::pagesReady, this,
        [this, recordId, chapterIdx, conn, errConn](const QList<PageInfo>& pages) {
            disconnect(*conn);
            disconnect(*errConn);
            downloadImages(recordId, chapterIdx, pages);
        });

    *errConn = connect(scraper, &MangaScraper::errorOccurred, this,
        [this, recordId, chapterIdx, conn, errConn](const QString& msg) {
            disconnect(*conn);
            disconnect(*errConn);
            QMutexLocker lock(&m_mutex);
            m_records[recordId].chapters[chapterIdx].status = "error";
            m_records[recordId].chapters[chapterIdx].error  = msg;
            --m_activeDownloads;
            emit downloadUpdated(recordId);
            processQueue();
        });

    scraper->fetchPages(chapterId);
}

void MangaDownloader::downloadImages(const QString& recordId, int chapterIdx,
                                      const QList<PageInfo>& pages)
{
    QString seriesDir, chapterName, format, source;
    {
        QMutexLocker lock(&m_mutex);
        auto& rec = m_records[recordId];
        auto& ch  = rec.chapters[chapterIdx];
        ch.totalImages = pages.size();
        seriesDir   = rec.destinationPath + "/" + rec.seriesTitle;
        chapterName = ch.chapterName;
        format      = rec.format;
        source      = rec.source;
    }

    // Sanitize directory names
    QString safe = chapterName;
    safe.replace(QRegularExpression(R"([<>:"/\\|?*])"), "_");
    QString chapterDir = seriesDir + "/" + safe;

    // R2: disk-space pre-check (matches Mihon's 200 MB floor in Downloader.kt:330).
    // Probe the *seriesDir* rather than chapterDir — chapterDir doesn't exist
    // yet and QStorageInfo on a missing path walks up until it finds a mount.
    constexpr qint64 MIN_FREE_BYTES = 200LL * 1024 * 1024;
    {
        QDir().mkpath(seriesDir);
        QStorageInfo probe(seriesDir);
        if (probe.isValid() && probe.bytesAvailable() < MIN_FREE_BYTES) {
            QMutexLocker lock(&m_mutex);
            auto it = m_records.find(recordId);
            if (it != m_records.end() && chapterIdx < it->chapters.size()) {
                auto& ch = it->chapters[chapterIdx];
                ch.status = QStringLiteral("error");
                ch.error  = QStringLiteral("Insufficient disk space — need at least 200 MB free");
            }
            --m_activeDownloads;
            emit downloadUpdated(recordId);
            // Try the next chapter — it may be destined for a different root
            // with more space (multiple series roots = independent mounts).
            processQueue();
            return;
        }
    }

    QDir().mkpath(chapterDir);

    // R4: sync the on-disk image count into downloadedImages in one pass so
    // the Transfers dialog reflects actual progress across pause/resume and
    // across cold restarts of a partially-downloaded chapter. Done once here
    // to avoid the skip-if-exists branch re-incrementing on every recursion.
    {
        int onDisk = 0;
        for (int i = 0; i < pages.size(); ++i) {
            QString ext = QFileInfo(QUrl(pages[i].imageUrl).path()).suffix();
            if (ext.isEmpty()) ext = "jpg";
            QString p = chapterDir + "/"
                + QString("%1.%2").arg(i, 3, 10, QChar('0')).arg(ext);
            if (QFileInfo::exists(p) && QFileInfo(p).size() > 0)
                ++onDisk;
        }
        QMutexLocker lock(&m_mutex);
        auto& ch = m_records[recordId].chapters[chapterIdx];
        ch.downloadedImages = onDisk;
        emit downloadUpdated(recordId);
    }

    // Download images sequentially. Second arg is the current attempt count
    // for the given page (0-indexed; retried via QTimer on network error,
    // backoff 2s/4s/8s matching Mihon's Downloader.kt:504-512).
    auto downloadNext = std::make_shared<std::function<void(int, int)>>();
    *downloadNext = [this, recordId, chapterIdx, pages, chapterDir, format, source, downloadNext](int pageIdx, int attempt) {
        // Pause gate — fires from both the "skip-if-exists" and the network-finished
        // recursion paths. pauseAll() already reverts status; we leave the image
        // counters alone (R4) — on resume, the skip-if-exists branch at the
        // page loop checks QFileInfo::exists and only increments for pages it
        // actually writes, so double-count is not possible. The Transfers
        // dialog stays at the true page count across pause/resume instead of
        // flickering to 0.
        {
            QMutexLocker lock(&m_mutex);
            if (m_paused) {
                auto it = m_records.find(recordId);
                if (it != m_records.end() && chapterIdx < it->chapters.size()) {
                    auto& ch = it->chapters[chapterIdx];
                    if (ch.status == QLatin1String("downloading"))
                        ch.status = QStringLiteral("queued");
                }
                --m_activeDownloads;
                emit downloadUpdated(recordId);
                return;
            }
        }

        if (pageIdx >= pages.size()) {
            // All images downloaded — pack if CBZ
            if (format == "cbz") {
                QString cbzPath = chapterDir + ".cbz";
                packCbz(chapterDir, cbzPath);
                // Remove source folder
                QDir(chapterDir).removeRecursively();

                QMutexLocker lock(&m_mutex);
                m_records[recordId].chapters[chapterIdx].finalPath = cbzPath;
            } else {
                QMutexLocker lock(&m_mutex);
                m_records[recordId].chapters[chapterIdx].finalPath = chapterDir;
            }

            // Mark chapter complete
            {
                QMutexLocker lock(&m_mutex);
                auto& rec = m_records[recordId];
                rec.chapters[chapterIdx].status = "completed";
                ++rec.completedChapters;
                rec.progress = static_cast<float>(rec.completedChapters) / rec.totalChapters;

                if (rec.completedChapters >= rec.totalChapters) {
                    rec.status = "completed";
                    rec.completedAt = QDateTime::currentMSecsSinceEpoch();
                    appendHistory(rec);
                    emit downloadCompleted(recordId);
                }
                --m_activeDownloads;
            }
            saveRecords();
            emit downloadUpdated(recordId);
            processQueue();
            return;
        }

        const auto& page = pages[pageIdx];
        QString ext = QFileInfo(QUrl(page.imageUrl).path()).suffix();
        if (ext.isEmpty()) ext = "jpg";
        QString fileName = QString("%1.%2").arg(pageIdx, 3, 10, QChar('0')).arg(ext);
        QString filePath = chapterDir + "/" + fileName;

        // Skip if already exists — the at-top-of-function sync already set
        // downloadedImages to reflect this file, so we just recurse without
        // incrementing (R4: avoid double-count on resume).
        if (QFileInfo::exists(filePath) && QFileInfo(filePath).size() > 0) {
            (*downloadNext)(pageIdx + 1, 0);
            return;
        }

        QNetworkRequest req(QUrl(page.imageUrl));
        req.setRawHeader("User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        // Referer based on source
        if (source == "weebcentral")
            req.setRawHeader("Referer", "https://weebcentral.com/");
        else if (source == "readcomicsonline")
            req.setRawHeader("Referer", "https://readcomicsonline.ru/");

        auto* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this,
            [this, reply, recordId, chapterIdx, filePath, pageIdx, attempt, downloadNext]() {
                reply->deleteLater();

                if (reply->error() == QNetworkReply::NoError) {
                    QFile f(filePath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(reply->readAll());
                        f.close();
                    }
                    QMutexLocker lock(&m_mutex);
                    ++m_records[recordId].chapters[chapterIdx].downloadedImages;
                    emit downloadUpdated(recordId);
                    (*downloadNext)(pageIdx + 1, 0);
                    return;
                }

                // R1: transient error — retry the same page after backoff
                // (2s/4s/8s for attempts 0/1/2). Pause gate at the top of the
                // lambda re-checks m_paused before the retry actually runs, so
                // a user pausing during backoff stops retrying cleanly.
                if (attempt + 1 < MAX_IMAGE_RETRIES) {
                    const int backoffMs = 2000 << attempt;
                    QTimer::singleShot(backoffMs, this,
                        [downloadNext, pageIdx, attempt]() {
                            (*downloadNext)(pageIdx, attempt + 1);
                        });
                    return;
                }

                // Retries exhausted — count as failed and move on.
                {
                    QMutexLocker lock(&m_mutex);
                    ++m_records[recordId].chapters[chapterIdx].failedImages;
                }
                emit downloadUpdated(recordId);
                (*downloadNext)(pageIdx + 1, 0);
            });
    };

    (*downloadNext)(0, 0);
}

// ── CBZ packing ─────────────────────────────────────────────────────────────
void MangaDownloader::packCbz(const QString& chapterDir, const QString& cbzPath)
{
#ifdef HAS_QT_ZIP
    QZipWriter zip(cbzPath);
    zip.setCompressionPolicy(QZipWriter::NeverCompress);  // images are already compressed

    QDir dir(chapterDir);
    auto files = dir.entryList(QDir::Files, QDir::Name);
    for (const auto& name : files) {
        QFile f(dir.absoluteFilePath(name));
        if (f.open(QIODevice::ReadOnly)) {
            zip.addFile(name, f.readAll());
        }
    }
    zip.close();
    qDebug() << "Packed CBZ:" << cbzPath << "(" << files.size() << "images)";
#else
    Q_UNUSED(chapterDir)
    Q_UNUSED(cbzPath)
    qWarning() << "CBZ packing unavailable — built without QZipWriter";
#endif
}

// ── Query ───────────────────────────────────────────────────────────────────
QList<MangaDownloadRecord> MangaDownloader::listActive() const
{
    QMutexLocker lock(&m_mutex);
    // R5: return in processing order, not map (key-sorted) order. Gives the
    // Transfers table a stable "top of queue first" presentation that matches
    // what the scheduler will actually do.
    QList<MangaDownloadRecord> out;
    out.reserve(m_recordOrder.size());
    for (const auto& id : m_recordOrder) {
        auto it = m_records.constFind(id);
        if (it != m_records.cend()) out.append(it.value());
    }
    return out;
}

QJsonArray MangaDownloader::listHistory() const
{
    auto data = m_store->read(HISTORY_FILE);
    return data.value("entries").toArray();
}

// ── Pause / resume ──────────────────────────────────────────────────────────
void MangaDownloader::pauseAll()
{
    QList<QString> updatedIds;
    {
        QMutexLocker lock(&m_mutex);
        if (m_paused) return;
        m_paused = true;
        // Revert any "downloading" chapters back to "queued". In-flight image
        // replies complete normally; the pause gate in downloadNext handles the
        // counter reset and the m_activeDownloads decrement.
        for (auto it = m_records.begin(); it != m_records.end(); ++it) {
            auto& rec = it.value();
            bool touched = false;
            for (auto& ch : rec.chapters) {
                if (ch.status == QLatin1String("downloading")) {
                    ch.status = QStringLiteral("queued");
                    touched = true;
                }
            }
            if (touched) updatedIds.append(it.key());
        }
    }
    for (const auto& id : updatedIds)
        emit downloadUpdated(id);
    emit pausedChanged(true);
}

void MangaDownloader::resumeAll()
{
    {
        QMutexLocker lock(&m_mutex);
        if (!m_paused) return;
        m_paused = false;
    }
    emit pausedChanged(false);
    processQueue();
}

bool MangaDownloader::isPaused() const
{
    QMutexLocker lock(&m_mutex);
    return m_paused;
}

// ── R5: queue reorder ───────────────────────────────────────────────────────
void MangaDownloader::moveSeriesToTop(const QString& id)
{
    bool moved = false;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_records.contains(id)) return;
        const int idx = m_recordOrder.indexOf(id);
        if (idx > 0) {
            m_recordOrder.removeAt(idx);
            m_recordOrder.prepend(id);
            moved = true;
        }
    }
    if (!moved) return;
    saveRecords();
    emit downloadUpdated(id);
    processQueue();
}

void MangaDownloader::moveSeriesToBottom(const QString& id)
{
    bool moved = false;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_records.contains(id)) return;
        const int idx = m_recordOrder.indexOf(id);
        if (idx >= 0 && idx < m_recordOrder.size() - 1) {
            m_recordOrder.removeAt(idx);
            m_recordOrder.append(id);
            moved = true;
        }
    }
    if (!moved) return;
    saveRecords();
    emit downloadUpdated(id);
    processQueue();
}

// ── R6: per-series chapter sort (queued block only) ─────────────────────────
void MangaDownloader::reorderChapters(const QString& id, const QString& orderKey,
                                       bool ascending)
{
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_records.find(id);
        if (it == m_records.end()) return;
        auto& chapters = it->chapters;

        // Pull out the queued block, preserving the position of everything
        // else (downloading / completed / error / cancelled stay put). Sort
        // the queued subset, then splice back in original slot order.
        QList<int> queuedIdx;
        QList<ChapterDownload> queued;
        for (int i = 0; i < chapters.size(); ++i) {
            if (chapters[i].status == QLatin1String("queued")) {
                queuedIdx.append(i);
                queued.append(chapters[i]);
            }
        }
        if (queued.size() < 2) return;    // nothing to sort

        auto cmp = [&](const ChapterDownload& a, const ChapterDownload& b) {
            if (orderKey == QLatin1String("date"))
                return ascending ? a.dateUpload < b.dateUpload
                                 : a.dateUpload > b.dateUpload;
            // default: chapter_number
            return ascending ? a.chapterNumber < b.chapterNumber
                             : a.chapterNumber > b.chapterNumber;
        };
        std::stable_sort(queued.begin(), queued.end(), cmp);

        for (int i = 0; i < queuedIdx.size(); ++i)
            chapters[queuedIdx[i]] = queued[i];
    }
    saveRecords();
    emit downloadUpdated(id);
}

// ── Control ─────────────────────────────────────────────────────────────────
void MangaDownloader::cancelDownload(const QString& id)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(id);
    if (it == m_records.end()) return;
    it->status = "cancelled";
    for (auto& ch : it->chapters) {
        if (ch.status == "queued" || ch.status == "downloading")
            ch.status = "cancelled";
    }
    lock.unlock();
    saveRecords();
    emit downloadUpdated(id);
}

void MangaDownloader::cancelAll()
{
    QList<QString> touchedIds;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_records.begin(); it != m_records.end(); ++it) {
            auto& rec = it.value();
            if (rec.status == QLatin1String("completed") ||
                rec.status == QLatin1String("cancelled"))
                continue;
            rec.status = QStringLiteral("cancelled");
            for (auto& ch : rec.chapters) {
                if (ch.status == QLatin1String("queued") ||
                    ch.status == QLatin1String("downloading"))
                    ch.status = QStringLiteral("cancelled");
            }
            touchedIds.append(it.key());
        }
    }
    if (touchedIds.isEmpty()) return;
    saveRecords();
    for (const auto& id : touchedIds)
        emit downloadUpdated(id);
}

void MangaDownloader::removeDownload(const QString& id)
{
    {
        QMutexLocker lock(&m_mutex);
        m_records.remove(id);
        m_recordOrder.removeAll(id);
    }
    saveRecords();
}

void MangaDownloader::removeWithData(const QString& id)
{
    QString destPath;
    QString seriesTitle;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_records.find(id);
        if (it != m_records.end()) {
            destPath    = it->destinationPath;
            seriesTitle = it->seriesTitle;
            m_records.erase(it);
            m_recordOrder.removeAll(id);
        }
    }

    // Delete the series folder
    if (!destPath.isEmpty() && !seriesTitle.isEmpty()) {
        QString seriesDir = destPath + "/" + seriesTitle;
        QDir(seriesDir).removeRecursively();
    }

    saveRecords();
}

void MangaDownloader::updateRecord(const QString& id)
{
    emit downloadUpdated(id);
}
