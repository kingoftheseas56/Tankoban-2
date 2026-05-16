// src/core/manga/TorrentVolumeProvider.h
//
// TANKOYOMI_PREMIUM Phase 3 -- torrent-side volume orchestration. Sibling to
// MangaDownloader, not a second downloader. Consumes TorrentEngine via its
// existing public surface; owns a persistent request ledger and an infoHash-
// keyed staging tree.
//
// Per Codex section 18 the provider is single long-lived, owned by ComicsPage,
// but its persistent state outlives the UI. All TorrentEngine signal
// connections are Qt::QueuedConnection because TorrentEngine emits from its
// alert worker thread.
//
// Per Codex section 17.3 + 17.5 the magnet add uses paused=true (upload-only
// mode) so metadata arrives without an all-files-download window; once
// metadata + file priorities are set, startTorrent clears upload-only.
//
// Per Codex section 19 + section 27.1 completion detection is event-driven
// via pieceFinished + fileByteRangesOfHavePieces, NOT polled.
//
// Archive validation (Phase 4) and cover extraction (Phase 10) are owned by
// separate classes; this provider calls them at the finalize step. v1 ships
// without those wired (Phase 4 + Phase 10 add them).
#pragma once

#include "PremiumCatalogSchema.h"
#include "TorrentRequestLedger.h"

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <QPointer>
#include <QJsonArray>

class TorrentEngine;
class MangaDownloadIndex;

namespace tankoban::manga::premium {

class PremiumCatalog;
class PremiumCoverExtractor;

class TorrentVolumeProvider : public QObject
{
    Q_OBJECT
public:
    TorrentVolumeProvider(TorrentEngine*          engine,
                          PremiumCatalog*         catalog,
                          TorrentRequestLedger*   ledger,
                          MangaDownloadIndex*     index,
                          const QString&          stagingRoot,
                          const QString&          coversDir,
                          QObject*                parent = nullptr);
    ~TorrentVolumeProvider() override;

    // Kicks off a single-volume fetch. Idempotent: a second call with the
    // same triple is a no-op if the request is already in flight, or a
    // resume if it had completed.
    //
    // `destinationPath` is the canonical series folder; the final cbz lands
    // at `<destinationPath>/<volumeEntry.cbzFileName>`.
    void requestVolume(const PremiumCatalogEntry& entry,
                       const PremiumVolumeEntry&  volumeEntry,
                       const QString&             destinationPath);

    // Cancels one in-flight request. Drops the volume's file-priority slot
    // back to 0. Does NOT remove the torrent if other volumes for the same
    // series are still being requested. If `deleteStaged` is true the
    // partially-downloaded staging file is removed.
    void cancelVolume(const QString& seriesId, int volumeNumber, bool deleteStaged = false);

    // Global pause/resume. Pause halts piece-request bursts but keeps the
    // torrent attached. Per Codex section 18 the UI shares one "Transfers
    // paused" state across MangaDownloader + TorrentVolumeProvider via
    // MangaTransferCoordinator (Phase 8).
    void pauseAll();
    void resumeAll();
    bool isPaused() const;

    // Crash-resume entry point. Called once after construction (post-engine
    // start, post-catalog load). Replays every ledger row whose status is
    // Pending/AwaitingMetadata/Downloading/Validating; re-attaches torrents
    // by expectedInfoHash; reapplies union file priorities.
    void replayLedger();

signals:
    // 0.0 to 1.0 monotone within a request. Emitted at most every ~500ms
    // per request to keep UI repaint cost bounded.
    void volumeProgress(QString seriesId, int volumeNumber, double pct);

    // Fired AFTER the cbz is durable at canonicalDestinationPath AND the
    // request row's status has been updated to Completed. Per Codex section
    // 18 step 5 the order is: finalize file -> register in index -> emit
    // volumeCompleted. Phase 4 inserts archive validation between the
    // finalize-file and index-register steps.
    void volumeCompleted(QString seriesId, int volumeNumber, QString cbzPath);

    // Fired on any failure path (engine error, archive validation failure,
    // expectedInfoHash mismatch on metadata, etc). errorCode is a stable
    // identifier (e.g. "infohash_mismatch", "validation_failed_no_images").
    void volumeFailed(QString seriesId, int volumeNumber,
                      QString errorCode, QString errorMessage);

    // Swarm-quality heartbeat. Emitted every ~2s while a volume is in flight.
    // 0 piecePeersOnline for >=30s is the "Waiting for peers" UX trigger
    // (Phase 6 Volume row indicator).
    void swarmStatus(QString seriesId, int volumeNumber, int piecePeersOnline);

    // TANKOYOMI_PREMIUM Phase 10 -- per-volume cover thumbnail ready. Emitted
    // AFTER volumeCompleted (cover extraction does NOT gate completion per
    // Codex section 21). Receiver repaints the detail view's Cover column.
    void volumeCoverReady(QString seriesId, int volumeNumber, QString coverPath);

private slots:
    // TorrentEngine emits metadataReady(infoHash, name, totalSize, files).
    // The slot signature matches the engine signal exactly (Qt 5/6
    // queued-connection rule: positional match, all args copyable).
    void onMetadataReady(const QString& infoHash,
                         const QString& name,
                         qint64         totalSize,
                         const QJsonArray& files);
    void onPieceFinished(const QString& infoHash, int pieceIndex);
    void onTorrentError(const QString& infoHash, const QString& errorMessage);

private:
    struct Inflight {
        QString  requestKey;
        QString  catalogId;
        QString  seriesId;
        int      volumeNumber       = 0;
        QString  expectedInfoHash;
        int      fileIndex          = -1;
        QString  cbzFileName;
        qint64   fileSizeBytes      = 0;
        int      pieceStart         = -1;
        int      pieceEnd           = -1;
        QString  stagingPath;       // <stagingRoot>/<infoHash>/
        QString  canonicalDestinationPath;
        bool     prioritiesApplied  = false;
        bool     startedAfterMeta   = false;
        double   lastReportedPct    = -1.0;
    };

    QString stagingPathFor(const QString& infoHash) const;

    void ensureTorrentAdded(const Inflight& iff);
    void applyUnionPriorities(const QString& infoHash);
    bool checkFileCompletion(const QString& infoHash, const Inflight& iff);
    void finalizeCompletion(Inflight iff);
    void emitProgressIfChanged(Inflight& iff);

    TorrentEngine*                            m_engine    = nullptr;
    QPointer<PremiumCatalog>                  m_catalog;
    QPointer<TorrentRequestLedger>            m_ledger;
    QPointer<MangaDownloadIndex>              m_index;
    QString                                   m_stagingRoot;
    QString                                   m_coversDir;
    bool                                      m_paused    = false;

    // TANKOYOMI_PREMIUM Phase 10 -- off-thread cover extractor. Owned by
    // this provider (lives on the provider's thread); each request fans out
    // to QThreadPool::globalInstance() and posts results back via
    // QMetaObject::invokeMethod(Qt::QueuedConnection).
    PremiumCoverExtractor*                    m_coverExtractor = nullptr;

    // infoHash -> list of in-flight requests against that torrent.
    QHash<QString, QList<Inflight>>           m_byInfoHash;
};

} // namespace tankoban::manga::premium
