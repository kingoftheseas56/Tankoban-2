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
    bool    sequential     = false;
    bool    forceStarted   = false;
    int     queuePosition  = -1;
    int     dlLimit        = 0;   // 0 = unlimited, else bytes/s
    int     ulLimit        = 0;
    QString errorMessage;
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
    void onTorrentFinished(const QString& infoHash);
    void onTorrentError(const QString& infoHash, const QString& message);
    void onStorageMoved(const QString& infoHash, const QString& newPath);
    void onStorageMoveFailed(const QString& infoHash, const QString& message);
    void onFileRenamed(const QString& infoHash, int fileIndex, const QString& newPath);
    void onFileRenameFailed(const QString& infoHash, int fileIndex, const QString& message);

private:
    void loadRecords();
    void saveRecords();
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

    CoreBridge*          m_bridge;
    TorrentEngine*       m_engine;
    StreamDownloadIndex* m_streamDownloadIndex = nullptr;  // STREAM_DOWNLOADED_LIBRARY Phase 2; non-owning

    // Persistent records keyed by infoHash
    QJsonObject m_records;  // { "hash": { name, savePath, category, addedAt, ... } }
    QJsonObject m_streamBulkGroups;  // { "groupId": { group schema } }
    QSet<QString> m_publishCompleteNotified;

    static constexpr const char* RECORDS_FILE = "torrents.json";
    static constexpr const char* HISTORY_FILE = "torrent_history.json";
    static constexpr const char* STREAM_BULK_GROUPS_FILE = "stream_bulk_groups.json";
};
