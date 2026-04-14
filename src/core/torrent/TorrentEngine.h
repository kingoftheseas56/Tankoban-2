#pragma once

#include <QObject>
#include <QThread>
#include <QJsonArray>
#include <QMutex>
#include <QHash>
#include <QString>
#include <QVector>

#ifdef HAS_LIBTORRENT
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#endif

// ── Per-torrent record (thread-safe via m_mutex) ────────────────────────────
struct TorrentRecord {
    QString   infoHash;
    QString   name;
    QString   savePath;
    bool      metadataReady = false;
#ifdef HAS_LIBTORRENT
    lt::torrent_handle handle;
#endif
};

// ── Status snapshot for UI consumption ──────────────────────────────────────
struct TorrentStatus {
    QString infoHash;
    QString name;
    QString savePath;
    QString stateString;  // "downloading", "seeding", "paused", "checking", "metadata"
    float   progress     = 0.f;
    int     downloadRate = 0;
    int     uploadRate   = 0;
    int     numPeers     = 0;
    int     numSeeds     = 0;
    qint64  totalDone    = 0;
    qint64  totalWanted  = 0;
    bool    forceStarted  = false;
    int     queuePosition = -1;
    int     dlLimit       = 0;   // 0 = unlimited, else bytes/s
    int     ulLimit       = 0;
};

// ── TorrentEngine ───────────────────────────────────────────────────────────
class TorrentEngine : public QObject
{
    Q_OBJECT

public:
    explicit TorrentEngine(const QString& cacheDir, QObject* parent = nullptr);
    ~TorrentEngine();

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // Torrent operations (all thread-safe)
    QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused = true);
    QString addFromResume(const QString& resumePath, const QString& savePath, bool paused);
    void    setFilePriorities(const QString& infoHash, const QVector<int>& priorities);
    void    setSequentialDownload(const QString& infoHash, bool sequential);
    void    flattenFiles(const QString& infoHash);
    void    startTorrent(const QString& infoHash, const QString& newSavePath);
    void    resumeTorrent(const QString& infoHash);
    void    pauseTorrent(const QString& infoHash);
    void    removeTorrent(const QString& infoHash, bool deleteFiles = false);
    void    forceStart(const QString& infoHash);
    void    forceRecheck(const QString& infoHash);
    void    forceReannounce(const QString& infoHash);
    void    queuePositionUp(const QString& infoHash);
    void    queuePositionDown(const QString& infoHash);
    void    setQueueLimits(int maxDownloads, int maxUploads, int maxActive);
    void    setSpeedLimits(const QString& infoHash, int dlLimitBps, int ulLimitBps);
    void    setGlobalSpeedLimits(int dlLimitBps, int ulLimitBps);
    void    setSeedingRules(const QString& infoHash, float ratioLimit, int seedTimeLimitSecs);
    void    setGlobalSeedingRules(float ratioLimit, int seedTimeLimitSecs);

    // Query (thread-safe snapshot)
    QList<TorrentStatus> allStatuses() const;
    QJsonArray torrentFiles(const QString& infoHash) const;

    // Check if contiguous bytes [fileOffset .. fileOffset+length) are fully downloaded.
    // Used by StreamEngine to verify the file header is available before handing to player.
    bool haveContiguousBytes(const QString& infoHash, int fileIndex,
                             qint64 fileOffset, qint64 length) const;

    // Flush libtorrent's disk write cache for a specific torrent so all
    // downloaded pieces are immediately readable from disk via QFile.
    void flushCache(const QString& infoHash);

signals:
    void metadataReady(const QString& infoHash, const QString& name,
                       qint64 totalSize, const QJsonArray& files);
    void torrentProgress(const QString& infoHash, float progress,
                         int dlSpeed, int ulSpeed, int peers, int seeds);
    void torrentFinished(const QString& infoHash);
    void torrentError(const QString& infoHash, const QString& message);

private:
#ifdef HAS_LIBTORRENT
    // Alert worker runs on a dedicated QThread
    class AlertWorker;
    friend class AlertWorker;

    lt::session        m_session;
    QThread            m_alertThread;
    AlertWorker*       m_alertWorker = nullptr;

    void applySettings();
    void loadDhtState();
    void saveDhtState();
    void saveAllResumeData();
    void ensureDirs();

    static QString hashToHex(const lt::torrent_handle& h);
    static QString stateToString(lt::torrent_status::state_t s, bool paused);

    struct SeedingRule { float ratioLimit = 0.f; int seedTimeSecs = 0; };
    QHash<QString, SeedingRule> m_seedingRules;
    SeedingRule                 m_globalSeedRule;
    void checkSeedingRules();
#endif

    mutable QMutex                    m_mutex;
    QHash<QString, TorrentRecord>     m_records;
    QString                           m_cacheDir;
    bool                              m_running = false;
};
