#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QMap>

class CoreBridge;
class TorrentEngine;
struct AddTorrentConfig;

// ── Info struct for UI consumption ──────────────────────────────────────────
struct TorrentInfo {
    QString infoHash;
    QString name;
    QString savePath;
    QString category;
    QString stateString;  // "downloading", "seeding", "paused", "checking", "metadata", "completed"
    float   progress    = 0.f;
    int     dlSpeed     = 0;
    int     ulSpeed     = 0;
    int     peers       = 0;
    int     seeds       = 0;
    qint64  totalDone   = 0;
    qint64  totalWanted = 0;
    qint64  addedAt     = 0;
    bool    sequential     = false;
    bool    forceStarted   = false;
    int     queuePosition  = -1;
    int     dlLimit        = 0;   // 0 = unlimited, else bytes/s
    int     ulLimit        = 0;
    QString errorMessage;
};

// ── TorrentClient ───────────────────────────────────────────────────────────
class TorrentClient : public QObject
{
    Q_OBJECT

public:
    explicit TorrentClient(CoreBridge* bridge, QObject* parent = nullptr);
    ~TorrentClient();

    TorrentEngine* engine() const { return m_engine; }

    // Add flow
    QString resolveMetadata(const QString& magnetUri);
    void    startDownload(const QString& infoHash, const AddTorrentConfig& config);

    // Query
    QList<TorrentInfo> listActive() const;
    QJsonArray         listHistory() const;

    // Control
    void pauseTorrent(const QString& infoHash);
    void resumeTorrent(const QString& infoHash);
    void deleteTorrent(const QString& infoHash, bool deleteFiles);

    // Force operations
    void forceStart(const QString& infoHash);
    void clearForceStart(const QString& infoHash);
    void forceRecheck(const QString& infoHash);
    void forceReannounce(const QString& infoHash);

    // Queue
    void queuePositionUp(const QString& infoHash);
    void queuePositionDown(const QString& infoHash);
    void setQueueLimits(int maxDownloads, int maxUploads, int maxActive);

    // Speed limits
    void setSpeedLimits(const QString& infoHash, int dlLimitBps, int ulLimitBps);
    void setGlobalSpeedLimits(int dlLimitBps, int ulLimitBps);

    // Seeding rules
    void setSeedingRules(const QString& infoHash, float ratioLimit, int seedTimeSecs);
    void setGlobalSeedingRules(float ratioLimit, int seedTimeSecs);

    // Dedup check
    bool isDuplicate(const QString& magnetUri) const;

    // Default paths per category from CoreBridge
    QMap<QString, QString> defaultPaths() const;

signals:
    void torrentAdded(const QString& infoHash);
    void torrentUpdated(const QString& infoHash);
    void torrentRemoved(const QString& infoHash);
    void torrentCompleted(const QString& infoHash);

private slots:
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onTorrentFinished(const QString& infoHash);
    void onTorrentError(const QString& infoHash, const QString& message);

private:
    void loadRecords();
    void saveRecords();
    void appendHistory(const TorrentInfo& info);
    QString extractInfoHash(const QString& magnetUri) const;

    CoreBridge*    m_bridge;
    TorrentEngine* m_engine;

    // Persistent records keyed by infoHash
    QJsonObject m_records;  // { "hash": { name, savePath, category, addedAt, ... } }

    static constexpr const char* RECORDS_FILE = "torrents.json";
    static constexpr const char* HISTORY_FILE = "torrent_history.json";
};
