#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <optional>
#include <QHash>
#include <QSharedPointer>

namespace tankoban::queue { class TransferQueue; }

// TORRENT_PERSISTENCE_COLLAPSE Phase 1 (2026-05-20) — TorrentRepository is a
// QObject-based member with internal QSqlDatabase state; the by-value member
// declaration below needs the full type, so this is a real include rather
// than a forward declaration.
#include "TorrentRepository.h"

// IDLE_PROGRESS_SCAN_FIX P1 (2026-06-07) — m_parsedPackCache holds a ParsedPack
// by value, so the full type is needed here (not a forward declaration).
#include "core/stream/StreamPackParser.h"

class CoreBridge;
class TorrentEngine;
class StreamDownloadIndex;
struct AddTorrentConfig;
namespace tankostream::stream { struct BulkPackVerificationResult; }

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
    QString streamGroupId;
    QString imdbId;        // show binding ("tt..." / kitsu seriesId); empty = standalone torrent. Theatre/stream downloads set this; manual Tankorent adds leave it empty. Used to keep show-bound downloads off the Tankorent page.
    bool    sequential     = false;
    bool    forceStarted   = false;
    int     queuePosition  = -1;
    int     dlLimit        = 0;   // 0 = unlimited, else bytes/s
    int     ulLimit        = 0;
    QString errorMessage;
    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.1 (2026-05-20) — true when the
    // row was imported from a legacy torrents.json that had no magnetUri
    // field (every pre-Phase-0 row matches that shape per audit Part C +
    // D10). The Tankorent transfers row uses this to surface a "Needs re-add"
    // recovery button; Stream detail view uses it on its movie row variant.
    bool legacyNoMagnet = false;
};

enum class StreamBulkItemState {
    Pending,
    Downloading,
    Publishing,
    Published,
    MissingSource,
    MetadataFailed,
    PublishFailed,
    Failed,
    Completed,
    Cancelled,
    Orphaned,
    Paused,
};

struct StreamBulkGroupItem {
    QString itemKey;
    QString destinationKey;
    QString infoHash;
    int     fileIndex = -1;
    QString canonicalFilename;
    StreamBulkItemState itemState = StreamBulkItemState::Pending;
    QString lastError;
};

struct StreamBulkGroupRecord {
    QString groupId;
    QString groupKind = QStringLiteral("streamSeason");
    QString label;
    QString sourceSeriesId;
    int     sourceSeason = -1;
    QString destinationRoot;
    QString stagingPath;
    QList<StreamBulkGroupItem> items;
    QMap<QString, QString> canonicalNames;
    int     retryGeneration = 0;
    qint64  createdAtMs = 0;
    qint64  updatedAtMs = 0;
};

// TANKORENT_QUALITY_AND_QUEUE P1 (2026-05-27) — deferred-start args stored
// while a transfer waits its turn in its show's TransferQueue lane. When
// the queue advances (itemStateChanged → Running), TorrentClient pops the
// args and calls addMagnetHeadless to actually start the libtorrent transfer.
//
// Only carries the addMagnetHeadless parameter set + lane-keying identity.
// Callers that need richer AddTorrentConfig fields (selectedIndices, file
// priorities, etc.) go through startDownload directly and stay outside the
// lane discipline for Phase 1 — future phases extend coverage.
struct TransferStartArgs {
    QString magnetOrPath;       // magnet URI (isMagnet=true) or .torrent path
    QString category;           // "videos" / "comics" / "books"
    QString destinationPath;    // resolved destination root
    QString showId;             // lane key (empty = standalone, immediate start)
    QString transferId;         // infohash — queue key + libtorrent identity
    QString displayTitle;
    int     season = 0;         // 0 = unbound / non-show / multi-season pack
    bool    isMagnet = true;    // false reserved for .torrent file path support
};

// ── TorrentClient ───────────────────────────────────────────────────────────
class TorrentClient : public QObject
{
    Q_OBJECT

public:
    explicit TorrentClient(CoreBridge* bridge, QObject* parent = nullptr);
    ~TorrentClient();

    TorrentEngine* engine() const { return m_engine; }

    // TANKORENT_QUALITY_AND_QUEUE P1 T1.8 (2026-05-27) — install the per-show
    // transfer lane queue. Non-owning; pointer must outlive TorrentClient
    // (MainWindow owns both).
    void setTransferQueue(tankoban::queue::TransferQueue* q);

    // TANKORENT_QUALITY_AND_QUEUE P1 T1.9 (2026-05-27) — queue-aware magnet
    // add. Routes through TransferQueue: if imdbId is non-empty and the
    // show's lane already has a current item, the call defers — the actual
    // libtorrent addMagnetHeadless fires when the queue advances. If imdbId
    // is empty, behaves like a direct addMagnetHeadless call (standalone =
    // own one-item lane, immediate start). Returns the infohash on
    // immediate-start; returns the prospective infohash on defer.
    //
    // Callers needing AddTorrentConfig richness beyond magnet/category/dest
    // should continue to use startDownload directly — Phase 1 lane coverage
    // is limited to the headless-magnet shape.
    QString addMagnetForShow(const QString& magnetUri,
                             const QString& category,
                             const QString& destinationPath,
                             const QString& imdbId,
                             int season);

    // TORRENT_PERSISTENCE_COLLAPSE Phase 3.4 (2026-05-20) — exposes the
    // SQLite-backed durable store so peers like StreamDownloadIndex can be
    // wired to it post-construction. Non-const because the repo's read
    // methods are non-const (QSqlQuery::prepare/exec aren't const-clean on
    // QSqlDatabase). The reference is stable for the lifetime of the
    // TorrentClient. Phase 4 may relocate this when m_records goes away.
    tankoban::torrent::TorrentRepository& repository() { return m_repo; }

    // Add flow
    QString resolveMetadata(const QString& magnetUri);
    void    startDownload(const QString& infoHash, const AddTorrentConfig& config);

    // v1.5 Phase D.3 (2026-05-19) — dialog-free magnet add for dev-bridge use.
    // Calls resolveMetadata + startDownload with a minimal AddTorrentConfig
    // (category resolved against defaultPaths(); destination = defaultPaths()
    // entry for that category; empty streamGroupId; non-sequential; not
    // paused). Returns the infoHash on success, empty QString if resolveMetadata
    // failed or the magnet was a duplicate. Mirrors what the AddTorrentDialog
    // submit path produces but without the user-confirm prompt.
    QString addMagnetHeadless(const QString& magnetUri,
                              const QString& category   = QString(),
                              const QString& destinationPath = QString());

    // Query
    QList<TorrentInfo> listActive() const;
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — set of infoHashes libtorrent
    // currently tracks. Used by StreamDownloadIndex::validateInFlightEntries at
    // startup to evict stale Pending/Downloading entries.
    QSet<QString> activeInfoHashes() const;
    QJsonArray         listHistory() const;
    QJsonObject        streamBulkGroups() const;
    QJsonArray         devTorrentsSnapshot(bool activeOnly) const;
    QJsonArray         devBulkGroupsSnapshot() const;

    // Aggregate progress [0..1] across all active torrents whose save path
    // is under `folderPath`. Weighted by torrent size so a big mostly-done
    // download and a tiny just-started one aggregate sensibly. Returns 0.0
    // when no matching active torrents. Case-insensitive prefix match on
    // Windows-style paths.
    float downloadProgress(const QString& folderPath) const;

    // Direct Theatre movie downloads are single torrent records with imdbId
    // and season=0, not stream-bulk groups. Returns {state, pct}; empty state
    // means no active movie download should be shown in the detail view.
    QPair<QString, int> streamMovieDownloadSnapshot(const QString& imdbId) const;

    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.2 (2026-05-20) — true when at
    // least one Theatre-style movie row (imdb match + season=0 + empty
    // streamGroupId) was migrated from the legacy torrents.json without a
    // magnetUri and so cannot be auto-resumed. StreamDetailView surfaces
    // this via a "NEEDS RE-ADD" chip; the existing Download button already
    // routes the re-add intent through m_lastChoices so no button-handler
    // change is needed.
    bool streamMovieIsLegacyNoMagnet(const QString& imdbId) const;

    // Control
    void pauseTorrent(const QString& infoHash);
    void resumeTorrent(const QString& infoHash);
    void deleteTorrent(const QString& infoHash, bool deleteFiles);

    // Relocate the torrent's downloaded files to a new save path. Optimistic
    // update of the persisted record + immediate libtorrent move_storage call;
    // libtorrent emits storage_moved_alert on success or
    // storage_moved_failed_alert on failure (handled in onStorageMoved /
    // onStorageMoveFailed). Triggers a library rescan for both the old and
    // new path's category roots so library views refresh after the move
    // completes.
    void moveStorage(const QString& infoHash, const QString& newSavePath);

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

    // STREAM_BULK_DOWNLOAD Phase 1: durable generic group-store transitions.
    // UI/orchestrator phases call these; Phase 1 itself only persists and
    // reconciles the store.
    void upsertStreamBulkGroup(const StreamBulkGroupRecord& group);
    void dispatchStreamBulkGroup(
        const StreamBulkGroupRecord& group,
        const tankostream::stream::BulkPackVerificationResult& verifierOutput);
    void cancelStreamBulkGroup(const QString& groupId);
    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — explicit-deleteFiles
    // overload for the Tankorent group menu's "Remove" vs "Remove +
    // Delete Files" actions. When deleteFilesOverride.has_value(), it
    // takes precedence over the existing allPublished heuristic — every
    // non-Publishing/Published torrent in the group is removed with the
    // chosen file-deletion flag. The single-arg overload preserves the
    // legacy auto-heuristic for any non-menu callers.
    void cancelStreamBulkGroup(const QString& groupId, bool deleteFiles);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — per-item cancel + delete.
    // Removes the libtorrent record (with delete_files=deleteFile),
    // marks the cohort item Cancelled, evicts the StreamDownloadIndex
    // entry if Published. Emits streamBulkGroupsChanged.
    void cancelStreamBulkItem(const QString& groupId,
                              const QString& itemKey,
                              bool deleteFile);

    // STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) — query whether any
    // active (non-terminal) bulk group references this imdbId. Used by
    // Remove from Library to gate the destructive action behind a
    // confirmation dialog. Spec §10.10.
    bool hasActiveStreamBulkGroupsForImdb(const QString& imdbId) const;
    QStringList streamBulkGroupIdsForImdb(const QString& imdbId) const;

    // STREAM_BULK_DOWNLOAD_V2 Phase 3 — per-episode bulk-download snapshot
    // for the StreamDetailView's progress column. Walks m_streamBulkGroups
    // for groups matching imdbId + season; returns a hash keyed by
    // episode number with values {itemState string, progressPct int}.
    // progressPct is sourced from listActive() for active torrents
    // (Downloading), 0 for Pending (paused-queued), 100 for Published,
    // and -1 for terminal failures. Empty hash means no bulk activity
    // for this show+season.
    QHash<int, QPair<QString, int>>
        streamBulkSnapshotForImdbSeason(const QString& imdbId, int season) const;
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — full snapshot of m_streamBulkGroups
    // for cross-show consumers (StreamDownloadsPage). Returns a deep copy so
    // the caller can iterate without holding the mutex.
    QJsonObject streamBulkGroupsSnapshot() const;
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — true iff any group keyed
    // "stream:<imdbId>:*" has any non-terminal item (Pending, Downloading,
    // Publishing, or Paused). Drives the DOWNLOADING tile chip on Stream
    // library home. Paused counts as non-terminal so the chip stays visible
    // while a cohort is user-paused.
    bool imdbHasActiveCohort(const QString& imdbId) const;
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — flip a cohort item's state
    // between Paused and Downloading. Caller is responsible for the
    // libtorrent pauseTorrent/resumeTorrent call that mirrors this.
    void setStreamBulkItemPaused(const QString& infoHash, bool paused);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — per-item retry filter. If itemKey
    // is empty, retries all failed items in the group (existing v1 behavior).
    // If non-empty, retries ONLY the matching item.
    void retryStreamBulkGroupFailedItems(const QString& groupId,
                                         const QString& itemKey = QString());

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — restart-group recovery
    // action. Clears libtorrent error state on non-terminal-success items
    // via forceRecheck, resets non-Published items to Pending, then
    // re-engages the cohort scheduler via cohortMaybeAdvance. Published
    // items are left alone (terminal-success; restarting would erase
    // user-visible library state). Used by the Tankorent group context
    // menu "Restart group" action.
    void restartStreamBulkGroup(const QString& groupId);
    bool updateStreamBulkGroupItemState(const QString& groupId,
                                        const QString& itemKey,
                                        StreamBulkItemState state,
                                        const QString& lastError = QString());
    bool bumpStreamBulkGroupRetryGeneration(const QString& groupId);

    static QString streamBulkGroupIdToFolderName(const QString& groupId);
    static QString streamBulkFolderNameToGroupId(const QString& folderName);
    static QString streamBulkStagingPath(const QString& videosRoot, const QString& groupId);

    // Release any active torrent record whose on-disk root folder matches the
    // given absolute path, leaving files in place. Used by the videos library
    // when a user-driven rename takes ownership of a download folder — the
    // rename would otherwise orphan libtorrent's view of save_path/name,
    // causing libtorrent to re-create the original folder + re-download (or
    // re-seed into a phantom shell) on next periodic resume-data save or boot.
    // Returns true if a record was released. Match is case-insensitive on the
    // canonicalized full folder path (savePath + "/" + name).
    bool releaseFolder(const QString& folderPath);

    // Default paths per category from CoreBridge
    QMap<QString, QString> defaultPaths() const;

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 2 — wire-in for the
    // stream-side download index. Set by MainWindow after both objects are
    // constructed; onFileRenamed() registers per-episode entries here as the
    // bulk publish path renames each completed file to its canonical path.
    // Pointer is non-owning; nullptr is tolerated (defensive no-op).
    void setStreamDownloadIndex(StreamDownloadIndex* idx);

signals:
    void torrentAdded(const QString& infoHash);
    void torrentUpdated(const QString& infoHash);
    void torrentRemoved(const QString& infoHash);
    void torrentCompleted(const QString& infoHash);
    void groupPublishComplete(const QString& groupId);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — emitted on any cohort-item state
    // transition (Pending→Downloading→Publishing→Published, or any failure-
    // state edge, or user-paused transition). Subscribers (StreamLibraryLayout
    // tile chips, StreamDownloadsPage cards, StreamDetailView rows) repaint
    // their derived state. groupId identifies which group changed; subscribers
    // who care about a specific imdb walk groups themselves via
    // imdbHasActiveCohort or streamBulkGroupsSnapshot.
    void streamBulkGroupsChanged(const QString& groupId);
    void streamBulkRetrySourcePickRequested(const QString& groupId,
                                            const QStringList& itemKeys);

private slots:
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — drives per-episode progress
    // updates into StreamDownloadIndex as pieces arrive. Connected with
    // Qt::QueuedConnection because pieceFinished emits from the AlertWorker
    // thread (same as metadataReady).
    void onPieceFinished(const QString& infoHash, int pieceIndex);
    void onTorrentFinished(const QString& infoHash);
    void onTorrentError(const QString& infoHash, const QString& message);
    void onStorageMoved(const QString& infoHash, const QString& newPath);
    void onStorageMoveFailed(const QString& infoHash, const QString& message);
    void onFileRenamed(const QString& infoHash, int fileIndex, const QString& newPath);
    void onFileRenameFailed(const QString& infoHash, int fileIndex, const QString& message);

private:
    // loadRecords()/saveRecords() retired 2026-05-21 P5.5 close-out.
    // m_records JSON cache is gone; TorrentRepository owns persistence.
    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.5 (2026-05-20) — rename legacy
    // torrents.json / stream_bulk_groups.json / stream_downloads.json + every
    // *.fastresume in torrent_cache/resume to a dated .bak suffix. Called from
    // the ctor on the second clean boot post-migration. Renames not deletes.
    void renameLegacyFilesToBak(const QString& dataDir);
    void loadStreamBulkGroups();
    void saveStreamBulkGroups();
    void reconcileStreamBulkGroups();
    void markStreamBulkItemsForTorrent(const QString& infoHash,
                                       StreamBulkItemState state,
                                       const QString& lastError = QString());
    void publishStreamBulkItemsForTorrent(const QString& infoHash);
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: registers each downloaded video
    // file in a Tankorent single-add torrent into StreamDownloadIndex, using
    // the imdbId+season persisted in m_records (Task A2) and BulkPackVerifier's
    // filename regex to detect per-file (season, episode). Called from
    // onTorrentFinished when streamGroupId is empty AND record["imdbId"] is set.
    void publishTankorentItemsForTorrent(const QString& infoHash);
    void retryStreamBulkPublishing();
    void maybeEmitStreamBulkGroupPublishComplete(const QString& groupId);
    // STREAM_BULK_DOWNLOAD_V2 backfill — one-shot at index-wire time. Walks
    // every Published item in m_streamBulkGroups and registers it into
    // m_streamDownloadIndex if not already present. Repairs items that
    // published in a prior Tankoban session BEFORE the on-publish
    // registerEpisode fallback chain at line ~2270 shipped.
    void backfillStreamDownloadIndex();
    // THEATRE_DOWNLOAD_INDEX_REGISTRATION P1.6 Gap 2 (2026-05-30) —
    // reconcile single-episode torrents that finished but never
    // registered in StreamDownloadIndex (mirrors backfill but
    // walks TorrentRepository rows instead of bulk groups).
    void reconcileUnregisteredSingleEpisodes();
    // F9 fix 2026-05-19 Task 9 (reconcileMovieRecordOrphans) retired in
    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.3 (2026-05-20). The 2s-delayed
    // m_records sweep was a band-aid for the F9 "movie row without engine
    // handle" symptom; Phase 2.3's pending_engine_add row replay handles
    // the same scenario deterministically with no heuristic delay.

    // STREAM_BULK_DOWNLOAD_V2 Phase 2 — cohort-sequential scheduler.
    // Within a per-episode bulk group, only ONE magnet runs at a time.
    // The remaining magnets are added to libtorrent paused (state=Pending,
    // non-empty infoHash) and the scheduler resumes the next-in-order
    // when the head transitions out of Downloading. cohortMaybeAdvance is
    // idempotent and self-healing: if 0 items in the group are currently
    // Downloading, it resumes the first Pending+infoHash item; otherwise
    // it returns early. Pack-mode groups have all items pointing at one
    // infoHash that starts active immediately, so they show Downloading
    // and the method short-circuits naturally — pack mode is sequential
    // by construction (one torrent = one slot). cohortMaybeAdvanceAll
    // walks every group; called from reconcileStreamBulkGroups for
    // restart resilience.
    void cohortMaybeAdvance(const QString& groupId);
    void cohortMaybeAdvanceAll();

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — private 3-arg form used
    // by both public cancelStreamBulkGroup overloads. When the override
    // has a value, it pins every torrent's deleteFiles to that bool;
    // otherwise the legacy allPublished heuristic decides per-torrent.
    void cancelStreamBulkGroup(const QString& groupId,
                               std::optional<bool> deleteFilesOverride);
    void appendHistory(const TorrentInfo& info);
    void compactHistory();
    QString extractInfoHash(const QString& magnetUri) const;
    struct PieceMeta {
        QString imdbId;
        QString streamGroupId;
        int season = 0;
    };
    void cachePieceMeta(const QString& infoHash,
                        const tankoban::torrent::TorrentRow& row);
    bool ensurePieceMetaCached(const QString& infoHash);
    void clearPieceProgressState(const QString& infoHash);
    void processPieceFinishedProgress(const QString& infoHash);
    void flushPieceFinishedProgress(const QString& infoHash);
    void emitStreamBulkProgressChangedForTorrent(const QString& infoHash);

    CoreBridge*          m_bridge;
    TorrentEngine*       m_engine;
    StreamDownloadIndex* m_streamDownloadIndex = nullptr;  // STREAM_DOWNLOADED_LIBRARY Phase 2; non-owning

    // TANKORENT_QUALITY_AND_QUEUE P1 T1.8 (2026-05-27) — non-owning queue
    // pointer + pending-args staging map. T1.9 populates m_pendingByTransferId
    // when a transfer is enqueued behind a current; pops it when the queue
    // advances and emits itemStateChanged(Running).
    tankoban::queue::TransferQueue* m_transferQueue = nullptr;
    QHash<QString, TransferStartArgs> m_pendingByTransferId;

    // TANKORENT_QUALITY_AND_QUEUE P1 T1.10 (2026-05-27) — staged AddTorrentConfig
    // for startDownload callers (Stream / Theatre / TankoLibrary). When the
    // config carries a non-empty imdbId, startDownload routes through the
    // queue; if not at lane head, the full config is staged here and replayed
    // when the queue advances. QSharedPointer keeps the forward-decl of
    // AddTorrentConfig at TorrentClient.h:26 viable (no include of
    // ui/dialogs/AddTorrentDialog.h needed here).
    QHash<QString, QSharedPointer<AddTorrentConfig>> m_pendingStartConfigs;
    QHash<QString, PieceMeta> m_pieceMetaCache;
    // IDLE_PROGRESS_SCAN_FIX P1 (2026-06-07) — parse the immutable pack layout
    // ONCE per torrent, not on every debounced progress tick. Keyed by infoHash;
    // invalidated in clearPieceProgressState alongside m_pieceMetaCache.
    QHash<QString, tankostream::stream::ParsedPack> m_parsedPackCache;
    QHash<QString, qint64> m_pieceProgressLastRunMs;
    QSet<QString> m_pieceProgressPending;

    // TORRENT_PERSISTENCE_COLLAPSE Phase 1 (2026-05-20) — SQLite-backed
    // durable store that will replace the legacy m_records / m_streamBulkGroups
    // JSON objects + .fastresume cache. Opened in the ctor; populated on first
    // boot by LegacyImporter::importInto when torrents.db is absent but the
    // legacy stores exist on disk. During Phase 1 the legacy hydration path
    // continues to drive UI state — the repository is written into but not yet
    // read from. Phase 3 cuts over the consumers; Phase 4 removes m_records +
    // saveRecords entirely.
    //
    // `mutable` because TorrentRepository's read methods (listTorrents,
    // getTorrent, listStreamGroups, etc.) cannot be `const` — QSqlQuery::prepare
    // + exec are not const-clean on QSqlDatabase. The conceptual state of
    // TorrentClient is unchanged by these queries, matching the standard
    // C++ idiom for cache/storage members accessed from const methods (e.g.
    // listActive() const, which Phase 3.1 routes through m_repo).
    mutable tankoban::torrent::TorrentRepository m_repo;

    // Persistent records keyed by infoHash — DELETED 2026-05-21 P5.5 close-out
    // of TORRENT_PERSISTENCE_COLLAPSE. The SQLite-backed TorrentRepository
    // (m_repo above) is now the durable source of truth. Reader callsites
    // were migrated across P5.1-P5.4; writes were vestigial post-P4.4.
    QJsonObject m_streamBulkGroups;  // { "groupId": { group schema } }
    QSet<QString> m_publishCompleteNotified;

    static constexpr const char* RECORDS_FILE = "torrents.json";
    static constexpr const char* HISTORY_FILE = "torrent_history.json";
    static constexpr const char* STREAM_BULK_GROUPS_FILE = "stream_bulk_groups.json";
};
