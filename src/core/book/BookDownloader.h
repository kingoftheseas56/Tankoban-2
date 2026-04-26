#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QTimer;

// HTTP streaming downloader for book files from resolved direct-URL lists.
//
// Input contract: caller (TankoLibraryPage) kicks off with a mirror URL list
// from BookScraper::downloadResolved(). Caller is expected to have just
// resolved the URLs (LibGen's get.php key is ephemeral ~60s; stale keys
// 307-redirect back to ads.php and we'd receive HTML instead of binary).
//
// Output contract: emits progress + complete signals for the UI to render.
// On complete, the file lives at <destinationDir>/<suggestedName> and the
// caller is expected to fire CoreBridge::notifyRootFoldersChanged("books")
// so BooksPage rescans and surfaces the new book.
//
// Key behaviors:
//   - Chunked streaming via QNetworkReply::readyRead -> QFile::write (no
//     readAll — books can be 500+ MB; readAll would OOM).
//   - Mirror failover: URLs tried in order, next on failure.
//   - Retry per URL with exponential backoff (2s / 4s / 8s, 3 attempts).
//   - Stale-key detection: if first chunk arrives with Content-Type
//     text/html (LibGen's ads.php page) when we expected binary, treat as
//     key-rotated and try the next URL.
//   - Temp-file-then-rename: writes to "<name>.part"; renames on success.
//   - Disk-space pre-check against destinationDir if expectedBytes known.
//   - Redirects followed via NoLessSafeRedirectPolicy (same-scheme only).
//
// Scope v1 (M2.4):
//   - One download at a time. Second startDownload with a different md5
//     queues FIFO and kicks off when the first completes.
//   - No Range-request resume — interrupted downloads restart from 0 on
//     retry. LibGen's EPUBs are small; PDFs occasionally hit 200 MB but
//     a fresh retry is acceptable for v1.
//   - No MD5-of-downloaded-bytes verification against the LibGen MD5
//     primary key. Reserved for M2.5.
class BookDownloader : public QObject
{
    Q_OBJECT
public:
    explicit BookDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~BookDownloader() override;

    // Start a download. Returns the md5 as a caller-side handle; same md5
    // cannot be started twice concurrently — if already active, returns
    // empty string and emits downloadFailed. Caller should resolve URLs
    // immediately before calling this so LibGen's key is fresh.
    QString startDownload(const QString& md5,
                          const QStringList& urls,
                          const QString& destinationDir,
                          const QString& suggestedName,
                          qint64 expectedBytes = 0);

    void cancelDownload(const QString& md5);
    bool isActive(const QString& md5) const;

signals:
    void downloadProgress(const QString& md5, qint64 bytesReceived, qint64 bytesTotal);
    void downloadComplete(const QString& md5, const QString& filePath);
    void downloadFailed(const QString& md5, const QString& reason);

private slots:
    void onReadyRead();
    void onFinished();
    void onDownloadProgressFromReply(qint64 received, qint64 total);

private:
    // State for the in-flight download. We keep only one active at a time
    // in v1; tracking by md5 lets cancel/isActive work uniformly + paves
    // the way for multi-download in a later batch.
    struct InFlight {
        QString     md5;
        QStringList urls;          // remaining URLs to try (front = current)
        int         urlIdx = 0;    // index of URL currently being attempted
        int         attempt = 0;   // retry attempt for the current URL (0-based)
        QString     destinationDir;
        QString     suggestedName;
        qint64      expectedBytes = 0;

        QPointer<QNetworkReply> reply;
        QFile*      file = nullptr;           // open for write on the .part path
        QString     partPath;                 // absolute .part path
        QString     finalPath;                // absolute final path (post-rename)

        bool        sanityChecked = false;    // have we inspected Content-Type yet?
        qint64      lastProgressEmit = 0;     // ms since startDownload for throttle
        qint64      lastProgressBytes = 0;    // bytes at last emit
        qint64      receivedBytes = 0;
    };

    void startNextUrlOrFail(InFlight& f);
    void startAttempt(InFlight& f);
    void retryOrFailover(InFlight& f, const QString& reason);
    void failAndCleanup(InFlight& f, const QString& reason);
    void finalizeSuccess(InFlight& f);
    void closeAndDeletePart(InFlight& f);

    bool detectStaleHtml(const QByteArray& firstChunk, const QString& contentType) const;

    // Sets f.finalPath + f.partPath based on suggestedName + server-provided
    // Content-Disposition (if safe). Returns false if the destinationDir
    // cannot be prepared (missing + mkpath failure).
    bool pickTargetFilename(InFlight& f, const QString& contentDisposition);

    QNetworkAccessManager* m_nam = nullptr;

    // One-slot v1. Queue for FIFO pending downloads.
    InFlight*       m_active = nullptr;
    QList<InFlight> m_queue;
};
