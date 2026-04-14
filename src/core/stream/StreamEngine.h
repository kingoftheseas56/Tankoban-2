#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QTimer>

class TorrentEngine;
class StreamHttpServer;

struct StreamFileResult {
    bool ok = false;
    QString url;                // http://127.0.0.1:{port}/stream/{hash}/{fileIndex}
    QString infoHash;           // canonical 40-char hex hash from libtorrent
    QString errorCode;          // METADATA_NOT_READY, FILE_NOT_READY, UNKNOWN_TORRENT, ENGINE_ERROR
    QString errorMessage;
    bool readyToStart = false;
    bool queued = false;
    double fileProgress = 0.0;  // 0.0 - 1.0
    qint64 downloadedBytes = 0;
    qint64 fileSize = 0;
    int selectedFileIndex = -1;
    QString selectedFileName;
};

struct StreamTorrentStatus {
    int peers = 0;
    int dlSpeed = 0;            // bytes/sec
};

class StreamEngine : public QObject
{
    Q_OBJECT

public:
    explicit StreamEngine(TorrentEngine* engine, const QString& cacheDir,
                          QObject* parent = nullptr);
    ~StreamEngine() override;

    // Start/stop the HTTP server
    bool start();
    void stop();
    int httpPort() const;

    // Main streaming API — caller polls this until ok==true
    StreamFileResult streamFile(const QString& magnetUri,
                                int fileIndex = -1,
                                const QString& fileNameHint = {});

    // Stop and clean up a stream
    void stopStream(const QString& infoHash);

    // Stop all active streams
    void stopAll();

    // Query torrent status for UI
    StreamTorrentStatus torrentStatus(const QString& infoHash) const;

    // Clean up orphaned cache data from previous sessions
    void cleanupOrphans();

    // Start periodic cleanup timer (call once after start)
    void startPeriodicCleanup();

signals:
    void streamReady(const QString& infoHash, const QString& url);
    void streamError(const QString& infoHash, const QString& message);

private slots:
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onTorrentProgress(const QString& infoHash, float progress,
                           int dlSpeed, int ulSpeed, int peers, int seeds);
    void onTorrentError(const QString& infoHash, const QString& message);

private:
    struct StreamRecord {
        QString infoHash;
        QString magnetUri;
        QString savePath;
        int requestedFileIndex = -1;    // from caller, -1 = auto-select
        QString fileNameHint;
        int selectedFileIndex = -1;     // resolved after metadata
        QString selectedFileName;
        qint64 selectedFileSize = 0;
        bool metadataReady = false;
        bool registered = false;        // registered with HTTP server
        int peers = 0;
        int dlSpeed = 0;
    };

    int autoSelectVideoFile(const QJsonArray& files, const QString& hint) const;
    QString buildStreamUrl(const QString& infoHash, int fileIndex) const;
    void applyStreamPriorities(const QString& infoHash, int fileIndex, int totalFiles);

    static bool isVideoExtension(const QString& path);

    TorrentEngine* m_torrentEngine;
    StreamHttpServer* m_httpServer;
    QString m_cacheDir;
    QTimer* m_cleanupTimer = nullptr;

    mutable QMutex m_mutex;
    QHash<QString, StreamRecord> m_streams;
};
