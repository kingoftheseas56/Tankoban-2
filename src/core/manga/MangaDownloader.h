#pragma once

#include "MangaResult.h"
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QMap>

class QNetworkAccessManager;
class MangaScraper;
class JsonStore;

// ── Per-chapter download state ──────────────────────────────────────────────
struct ChapterDownload {
    QString chapterId;
    QString chapterName;
    QString status;           // "queued", "downloading", "completed", "error", "cancelled"
    int     totalImages    = 0;
    int     downloadedImages = 0;
    int     failedImages   = 0;
    QString finalPath;
    QString error;
};

// ── Per-series download record ──────────────────────────────────────────────
struct MangaDownloadRecord {
    QString id;               // SHA-256 truncated
    QString seriesTitle;
    QString source;           // "weebcentral" or "readcomicsonline"
    QString destinationPath;
    QString format;           // "cbz" or "folder"
    QString status;           // "queued", "downloading", "completed", "error", "cancelled"
    float   progress       = 0.f;
    int     totalChapters  = 0;
    int     completedChapters = 0;
    qint64  startedAt      = 0;
    qint64  completedAt    = 0;
    QList<ChapterDownload> chapters;
};

// ── MangaDownloader ─────────────────────────────────────────────────────────
class MangaDownloader : public QObject
{
    Q_OBJECT

public:
    explicit MangaDownloader(JsonStore* store, QObject* parent = nullptr);
    ~MangaDownloader();

    void setScraper(const QString& sourceId, MangaScraper* scraper);

    // Start a new series download
    QString startDownload(const QString& seriesTitle, const QString& source,
                          const QList<ChapterInfo>& chapters,
                          const QString& destinationPath, const QString& format);

    // Query
    QList<MangaDownloadRecord> listActive() const;
    QJsonArray listHistory() const;

    // Control
    void cancelDownload(const QString& id);
    void removeDownload(const QString& id);
    void removeWithData(const QString& id);

signals:
    void downloadUpdated(const QString& id);
    void downloadCompleted(const QString& id);

private:
    void processQueue();
    void downloadChapter(const QString& recordId, int chapterIdx);
    void downloadImages(const QString& recordId, int chapterIdx,
                        const QList<PageInfo>& pages);
    void packCbz(const QString& chapterDir, const QString& cbzPath);
    void updateRecord(const QString& id);
    void saveRecords();
    void loadRecords();
    void appendHistory(const MangaDownloadRecord& rec);

    JsonStore* m_store;
    QMap<QString, MangaScraper*> m_scrapers;
    mutable QMutex m_mutex;
    QMap<QString, MangaDownloadRecord> m_records;
    QNetworkAccessManager* m_nam;
    int m_activeDownloads = 0;

    static constexpr int MAX_CONCURRENT_CHAPTERS = 2;
    static constexpr int MAX_IMAGE_RETRIES = 2;
    static constexpr const char* RECORDS_FILE = "manga_downloads.json";
    static constexpr const char* HISTORY_FILE = "manga_history.json";
};
