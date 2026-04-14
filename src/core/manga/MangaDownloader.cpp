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
    QJsonObject data;
    data["active"] = active;
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

    for (const auto& ch : chapters) {
        ChapterDownload cd;
        cd.chapterId   = ch.id;
        cd.chapterName = ch.name;
        cd.status      = "queued";
        rec.chapters.append(cd);
    }

    {
        QMutexLocker lock(&m_mutex);
        m_records[id] = rec;
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
    if (m_activeDownloads >= MAX_CONCURRENT_CHAPTERS) return;

    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
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
                downloadChapter(it.key(), i);
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
    QDir().mkpath(chapterDir);

    // Download images sequentially
    auto downloadNext = std::make_shared<std::function<void(int)>>();
    *downloadNext = [this, recordId, chapterIdx, pages, chapterDir, format, source, downloadNext](int pageIdx) {
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

        // Skip if already exists (resume support)
        if (QFileInfo::exists(filePath) && QFileInfo(filePath).size() > 0) {
            QMutexLocker lock(&m_mutex);
            ++m_records[recordId].chapters[chapterIdx].downloadedImages;
            emit downloadUpdated(recordId);
            (*downloadNext)(pageIdx + 1);
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
            [this, reply, recordId, chapterIdx, filePath, pageIdx, downloadNext]() {
                reply->deleteLater();

                if (reply->error() == QNetworkReply::NoError) {
                    QFile f(filePath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(reply->readAll());
                        f.close();
                    }
                    QMutexLocker lock(&m_mutex);
                    ++m_records[recordId].chapters[chapterIdx].downloadedImages;
                } else {
                    QMutexLocker lock(&m_mutex);
                    ++m_records[recordId].chapters[chapterIdx].failedImages;
                }

                emit downloadUpdated(recordId);
                (*downloadNext)(pageIdx + 1);
            });
    };

    (*downloadNext)(0);
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
    return m_records.values();
}

QJsonArray MangaDownloader::listHistory() const
{
    auto data = m_store->read(HISTORY_FILE);
    return data.value("entries").toArray();
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

void MangaDownloader::removeDownload(const QString& id)
{
    {
        QMutexLocker lock(&m_mutex);
        m_records.remove(id);
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
