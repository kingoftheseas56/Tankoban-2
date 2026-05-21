#include "TorrentClient.h"
#include "LegacyImporter.h"  // TORRENT_PERSISTENCE_COLLAPSE P1.6 first-boot importer
#include "TorrentEngine.h"
#include "core/CoreBridge.h"
#include "core/JsonlEventLog.h"
#include "core/JsonStore.h"
#include "core/stream/BulkPackVerifier.h"
#include "core/stream/StreamBulkPlan.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/StreamPackParser.h"
#include "ui/dialogs/AddTorrentDialog.h"  // for AddTorrentConfig

#include <QRegularExpression>
#include <QDate>    // Phase 4.5 — date-stamped legacy file .bak suffix
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>  // P3.3 std::sort over StreamGroupRow vector by createdAt
#include <QHash>
#include <QJsonValue>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QDebug>

namespace {

constexpr const char* kGroupKindStreamSeason = "streamSeason";
constexpr const char* kStatePending = "Pending";
constexpr const char* kStateDownloading = "Downloading";
constexpr const char* kStatePublishing = "Publishing";
constexpr const char* kStatePublished = "Published";
constexpr const char* kStateMissingSource = "MissingSource";
constexpr const char* kStateMetadataFailed = "MetadataFailed";
constexpr const char* kStatePublishFailed = "PublishFailed";
constexpr const char* kStateFailed = "Failed";
constexpr const char* kStateCompleted = "Completed";
constexpr const char* kStateCancelled = "Cancelled";
constexpr const char* kStateOrphaned = "Orphaned";
constexpr const char* kStatePaused = "Paused";

QString streamBulkItemStateToString(StreamBulkItemState state)
{
    switch (state) {
    case StreamBulkItemState::Pending:     return QStringLiteral("Pending");
    case StreamBulkItemState::Downloading: return QStringLiteral("Downloading");
    case StreamBulkItemState::Publishing:  return QStringLiteral("Publishing");
    case StreamBulkItemState::Published:   return QStringLiteral("Published");
    case StreamBulkItemState::MissingSource: return QStringLiteral("MissingSource");
    case StreamBulkItemState::MetadataFailed: return QStringLiteral("MetadataFailed");
    case StreamBulkItemState::PublishFailed: return QStringLiteral("PublishFailed");
    case StreamBulkItemState::Failed:      return QStringLiteral("Failed");
    case StreamBulkItemState::Completed:   return QStringLiteral("Completed");
    case StreamBulkItemState::Cancelled:   return QStringLiteral("Cancelled");
    case StreamBulkItemState::Orphaned:    return QStringLiteral("Orphaned");
    case StreamBulkItemState::Paused:      return QStringLiteral("Paused");
    }
    return QStringLiteral("Pending");
}

StreamBulkItemState streamBulkItemStateFromString(const QString& state)
{
    if (state == QLatin1String(kStateDownloading)) return StreamBulkItemState::Downloading;
    if (state == QLatin1String(kStatePublishing))  return StreamBulkItemState::Publishing;
    if (state == QLatin1String(kStatePublished))   return StreamBulkItemState::Published;
    if (state == QLatin1String(kStateMissingSource)) return StreamBulkItemState::MissingSource;
    if (state == QLatin1String(kStateMetadataFailed)) return StreamBulkItemState::MetadataFailed;
    if (state == QLatin1String(kStatePublishFailed)) return StreamBulkItemState::PublishFailed;
    if (state == QLatin1String(kStateFailed))      return StreamBulkItemState::Failed;
    if (state == QLatin1String(kStateCompleted)) return StreamBulkItemState::Completed;
    if (state == QLatin1String(kStateCancelled)) return StreamBulkItemState::Cancelled;
    if (state == QLatin1String(kStateOrphaned))  return StreamBulkItemState::Orphaned;
    if (state == QLatin1String(kStatePaused))    return StreamBulkItemState::Paused;
    return StreamBulkItemState::Pending;
}

bool isTerminalStreamBulkState(const QString& state)
{
    return state == QLatin1String(kStatePublished)
        || state == QLatin1String(kStateMissingSource)
        || state == QLatin1String(kStateMetadataFailed)
        || state == QLatin1String(kStatePublishFailed)
        || state == QLatin1String(kStateFailed)
        || state == QLatin1String(kStateCompleted)
        || state == QLatin1String(kStateCancelled)
        || state == QLatin1String(kStateOrphaned);
}

bool isPublishingStreamBulkState(const QString& state)
{
    return state == QLatin1String(kStatePublishing);
}

bool isDownloadingStreamBulkState(const QString& state)
{
    return state == QLatin1String(kStatePending)
        || state == QLatin1String(kStateDownloading);
}

bool isStreamBulkSourceRetryState(const QString& state, const QString& infoHash)
{
    return state == QLatin1String(kStateMissingSource)
        || state == QLatin1String(kStateMetadataFailed)
        || (state == QLatin1String(kStateFailed) && infoHash.isEmpty())
        || (state == QLatin1String(kStatePending) && infoHash.isEmpty());
}

bool isStreamBulkFailureState(const QString& state)
{
    return state == QLatin1String(kStateMissingSource)
        || state == QLatin1String(kStateMetadataFailed)
        || state == QLatin1String(kStatePublishFailed)
        || state == QLatin1String(kStateFailed);
}

QJsonObject streamBulkItemToJson(const StreamBulkGroupItem& item)
{
    QJsonObject obj;
    obj["itemKey"] = item.itemKey;
    obj["destinationKey"] = item.destinationKey;
    obj["infoHash"] = item.infoHash;
    obj["torrentKey"] = tankostream::stream::makeTorrentKey(item.infoHash);
    obj["fileIndex"] = item.fileIndex;
    obj["fileKey"] = tankostream::stream::makeFileKey(item.infoHash, item.fileIndex);
    obj["canonicalFilename"] = item.canonicalFilename;
    obj["itemState"] = streamBulkItemStateToString(item.itemState);
    obj["lastError"] = item.lastError;
    return obj;
}

QJsonObject streamBulkGroupToJson(const StreamBulkGroupRecord& group)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 created = group.createdAtMs > 0 ? group.createdAtMs : now;
    const qint64 updated = group.updatedAtMs > 0 ? group.updatedAtMs : created;

    QJsonObject sourceIds;
    sourceIds["seriesId"] = group.sourceSeriesId;
    sourceIds["season"] = group.sourceSeason;

    QJsonArray items;
    for (const auto& item : group.items)
        items.append(streamBulkItemToJson(item));

    QJsonObject canonicalNames;
    for (auto it = group.canonicalNames.begin(); it != group.canonicalNames.end(); ++it)
        canonicalNames[it.key()] = it.value();

    QJsonObject obj;
    obj["groupId"] = group.groupId;
    obj["groupKind"] = group.groupKind.isEmpty()
        ? QStringLiteral("streamSeason")
        : group.groupKind;
    obj["label"] = group.label;
    obj["sourceIds"] = sourceIds;
    obj["destinationRoot"] = group.destinationRoot;
    obj["stagingPath"] = group.stagingPath.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(group.stagingPath);
    obj["items"] = items;
    obj["canonicalNames"] = canonicalNames;
    obj["retryGeneration"] = group.retryGeneration;
    obj["createdAtMs"] = created;
    obj["updatedAtMs"] = updated;
    return obj;
}

QJsonObject normalizeStreamBulkItem(QJsonObject item, bool* changed)
{
    auto setDefault = [&](const QString& key, const QJsonValue& value) {
        if (!item.contains(key)) {
            item.insert(key, value);
            if (changed) *changed = true;
        }
    };

    setDefault(QStringLiteral("itemKey"), QString());
    setDefault(QStringLiteral("destinationKey"), QString());
    setDefault(QStringLiteral("infoHash"), QString());
    setDefault(QStringLiteral("fileIndex"), -1);
    setDefault(QStringLiteral("canonicalFilename"), QString());
    setDefault(QStringLiteral("itemState"), QString::fromLatin1(kStatePending));
    setDefault(QStringLiteral("lastError"), QString());

    const QString infoHash = item.value("infoHash").toString();
    const int fileIndex = item.value("fileIndex").toInt(-1);
    const QString torrentKey = tankostream::stream::makeTorrentKey(infoHash);
    const QString fileKey = tankostream::stream::makeFileKey(infoHash, fileIndex);
    if (item.value("torrentKey").toString() != torrentKey) {
        item["torrentKey"] = torrentKey;
        if (changed) *changed = true;
    }
    if (item.value("fileKey").toString() != fileKey) {
        item["fileKey"] = fileKey;
        if (changed) *changed = true;
    }

    const QString state = streamBulkItemStateToString(
        streamBulkItemStateFromString(item.value("itemState").toString()));
    if (item.value("itemState").toString() != state) {
        item["itemState"] = state;
        if (changed) *changed = true;
    }

    return item;
}

QJsonObject normalizeStreamBulkGroup(const QString& key, QJsonObject group, bool* changed)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto setDefault = [&](const QString& field, const QJsonValue& value) {
        if (!group.contains(field)) {
            group.insert(field, value);
            if (changed) *changed = true;
        }
    };

    if (group.value("groupId").toString().isEmpty()) {
        group["groupId"] = key;
        if (changed) *changed = true;
    }
    setDefault(QStringLiteral("groupKind"), QString::fromLatin1(kGroupKindStreamSeason));
    setDefault(QStringLiteral("label"), QString());
    if (!group.value("sourceIds").isObject()) {
        QJsonObject sourceIds;
        sourceIds["seriesId"] = QString();
        sourceIds["season"] = -1;
        group["sourceIds"] = sourceIds;
        if (changed) *changed = true;
    }
    setDefault(QStringLiteral("destinationRoot"), QString());
    if (!group.contains("stagingPath")) {
        group["stagingPath"] = QJsonValue(QJsonValue::Null);
        if (changed) *changed = true;
    }
    if (!group.value("canonicalNames").isObject()) {
        group["canonicalNames"] = QJsonObject();
        if (changed) *changed = true;
    }
    if (!group.value("items").isArray()) {
        group["items"] = QJsonArray();
        if (changed) *changed = true;
    }
    setDefault(QStringLiteral("retryGeneration"), 0);
    setDefault(QStringLiteral("createdAtMs"), now);
    setDefault(QStringLiteral("updatedAtMs"), group.value("createdAtMs").toVariant().toLongLong());

    QJsonArray normalizedItems;
    const QJsonArray inputItems = group.value("items").toArray();
    for (const auto& value : inputItems)
        normalizedItems.append(normalizeStreamBulkItem(value.toObject(), changed));
    if (group.value("items").toArray() != normalizedItems) {
        group["items"] = normalizedItems;
        if (changed) *changed = true;
    }

    return group;
}

QString canonicalPathForStreamBulkItem(const QJsonObject& group, const QJsonObject& item)
{
    const QString destinationKey = item.value("destinationKey").toString();
    if (!destinationKey.isEmpty() && QDir::isAbsolutePath(destinationKey))
        return QDir::cleanPath(destinationKey);

    const QString root = group.value("destinationRoot").toString();
    if (root.isEmpty()) return {};

    if (!destinationKey.isEmpty())
        return QDir::cleanPath(QDir(root).filePath(destinationKey));

    const QString filename = item.value("canonicalFilename").toString();
    if (!filename.isEmpty())
        return QDir::cleanPath(QDir(root).filePath(filename));

    return {};
}

QString relativePublishTarget(const QString& stagingPath, const QString& canonicalPath)
{
    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — return ABSOLUTE
    // canonical path instead of the prior relative-with-`..`-traversal
    // form. Hemanth reported 2026-05-11 that all 4 Daredevil S02
    // child torrents reached 100% download but every publish rename
    // failed with "The filename, directory name, or volume label
    // syntax is incorrect" (Windows ERROR_INVALID_NAME 123). Phase 1
    // evidence confirmed the en-dash "–" in "Daredevil Born Again
    // (2025–)" was NOT the cause: `ls` showed the canonical show
    // directory was successfully created by mkpath. The prior
    // relative form produced paths like
    // `../../Daredevil Born Again (2025–)/Season 02/...mkv` which
    // libtorrent forwarded LITERALLY to Windows MoveFileEx; depending
    // on which Windows path-API libtorrent calls (`\\?\` prefixed
    // paths in particular skip normalization of `..` components),
    // Windows can reject the path syntax even though the resolved
    // absolute is valid. libtorrent's rename_file accepts absolute
    // paths regardless of save_path placement, so returning the
    // absolute canonical sidesteps the `..` traversal entirely.
    // stagingPath param preserved for ABI compat with existing
    // callers; intentionally unused.
    Q_UNUSED(stagingPath);
    if (canonicalPath.isEmpty())
        return {};
    return QDir::fromNativeSeparators(QDir::cleanPath(canonicalPath));
}

QMap<int, int> priorityVectorToMap(const QVector<int>& priorities)
{
    QMap<int, int> mapped;
    for (int i = 0; i < priorities.size(); ++i)
        mapped.insert(i, priorities.at(i));
    return mapped;
}

int streamBulkEpisodeFromItem(const QJsonObject& item)
{
    int episode = item.value(QStringLiteral("episode")).toInt(0);
    if (episode <= 0)
        episode = item.value(QStringLiteral("episodeNum")).toInt(0);
    if (episode <= 0) {
        const QString itemKey = item.value(QStringLiteral("itemKey")).toString();
        const int eIdx = itemKey.lastIndexOf(QLatin1Char('E'));
        if (eIdx > 0)
            episode = itemKey.mid(eIdx + 1).toInt();
    }
    return episode;
}

tankoban::torrent::StreamGroupRow streamGroupRowFromBulkGroup(
    const QString& fallbackGroupId,
    const QJsonObject& group)
{
    tankoban::torrent::StreamGroupRow row;
    row.groupId = group.value(QStringLiteral("groupId")).toString(fallbackGroupId);
    const QJsonObject sourceIds = group.value(QStringLiteral("sourceIds")).toObject();
    row.imdbId = group.value(QStringLiteral("imdbId")).toString();
    if (row.imdbId.isEmpty())
        row.imdbId = sourceIds.value(QStringLiteral("seriesId")).toString();
    row.season = group.contains(QStringLiteral("season"))
        ? group.value(QStringLiteral("season")).toInt(0)
        : sourceIds.value(QStringLiteral("season")).toInt(0);
    row.label = group.value(QStringLiteral("label")).toString();
    row.state = group.value(QStringLiteral("state")).toString();
    row.retryGeneration = group.value(QStringLiteral("retryGeneration")).toInt(0);
    const QJsonValue stagingPath = group.value(QStringLiteral("stagingPath"));
    row.stagingPath = stagingPath.isNull() ? QString() : stagingPath.toString();
    const qint64 createdAtMs = group.value(QStringLiteral("createdAtMs")).toVariant().toLongLong();
    if (createdAtMs > 0)
        row.createdAt = QDateTime::fromMSecsSinceEpoch(createdAtMs, Qt::UTC);
    row.packMode = group.value(QStringLiteral("packMode")).toBool(false);
    return row;
}

tankoban::torrent::StreamGroupItemRow streamGroupItemRowFromBulkItem(
    const QString& groupId,
    const QJsonObject& item)
{
    tankoban::torrent::StreamGroupItemRow row;
    row.groupId = groupId;
    row.itemId = item.value(QStringLiteral("itemId")).toString();
    if (row.itemId.isEmpty())
        row.itemId = item.value(QStringLiteral("itemKey")).toString();
    row.episode = streamBulkEpisodeFromItem(item);
    row.infoHash = item.value(QStringLiteral("infoHash")).toString();
    row.state = item.value(QStringLiteral("state")).toString();
    if (row.state.isEmpty())
        row.state = item.value(QStringLiteral("itemState")).toString();
    row.errorMessage = item.value(QStringLiteral("errorMessage")).toString();
    if (row.errorMessage.isEmpty())
        row.errorMessage = item.value(QStringLiteral("lastError")).toString();
    row.fileIndex = item.value(QStringLiteral("fileIndex")).toInt(-1);
    return row;
}

tankoban::torrent::TorrentRow torrentRowFromStartConfig(
    const QString& hash,
    const AddTorrentConfig& config,
    tankoban::torrent::TorrentState state)
{
    tankoban::torrent::TorrentRow row;
    row.hash = hash.toLower();
    row.state = state;
    row.addedAt = QDateTime::currentDateTimeUtc();
    row.category = config.category;
    row.savePath = config.destinationPath;
    row.contentLayout = config.contentLayout;
    row.streamGroupId = config.streamGroupId;
    row.sequential = config.sequential;
    row.imdbId = config.imdbId;
    row.season = config.season;
    row.magnetUri = config.magnetUri;
    row.legacyNoMagnet = config.magnetUri.isEmpty();
    return row;
}

bool streamBulkGroupAllTerminal(const QJsonObject& group)
{
    const QJsonArray items = group.value("items").toArray();
    if (items.isEmpty()) return false;
    for (const auto& value : items) {
        if (!isTerminalStreamBulkState(value.toObject().value("itemState").toString()))
            return false;
    }
    return true;
}

bool streamBulkGroupAllPublished(const QJsonObject& group)
{
    const QJsonArray items = group.value("items").toArray();
    if (items.isEmpty()) return false;
    for (const auto& value : items) {
        const QString state = value.toObject().value("itemState").toString();
        if (state != QLatin1String(kStatePublished)
            && state != QLatin1String(kStateCompleted)) {
            return false;
        }
    }
    return true;
}

QJsonObject pruneTerminalStreamBulkGroups(const QJsonObject& groups)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — TTL bumped 7d → 90d so the
    // StreamDownloadsPage History section reads completed cohorts directly
    // from stream_bulk_groups.json. No new persistence file; zero schema
    // change. Pruning still happens at-save + at-load — no scheduled pruner.
    constexpr qint64 kGcAgeMs = 90LL * 24LL * 60LL * 60LL * 1000LL;

    QJsonObject pruned;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const QJsonObject group = it.value().toObject();
        const QJsonArray items = group.value("items").toArray();
        bool allTerminal = !items.isEmpty();
        for (const auto& value : items) {
            const QString state = value.toObject().value("itemState").toString();
            if (!isTerminalStreamBulkState(state)) {
                allTerminal = false;
                break;
            }
        }
        const qint64 updatedAt = group.value("updatedAtMs").toVariant().toLongLong();
        if (allTerminal && updatedAt > 0 && now - updatedAt > kGcAgeMs)
            continue;
        pruned.insert(it.key(), group);
    }
    return pruned;
}

} // namespace

// ── Constructor ─────────────────────────────────────────────────────────────
TorrentClient::TorrentClient(CoreBridge* bridge, QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
    , m_engine(new TorrentEngine(bridge->dataDir() + "/torrent_cache", this))
{
    connect(m_engine, &TorrentEngine::metadataReady,
            this, &TorrentClient::onMetadataReady,
            Qt::QueuedConnection);  // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — emit fires from AlertWorker thread
    connect(m_engine, &TorrentEngine::pieceFinished,
            this, &TorrentClient::onPieceFinished,
            Qt::QueuedConnection);  // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — emit fires from AlertWorker thread
    connect(m_engine, &TorrentEngine::torrentAddedConfirmed,
            this, [this](const QString& infoHash) {
                const QString hash = infoHash.toLower();
                const auto row = m_repo.getTorrent(hash);
                if (row.has_value() &&
                    row->state == tankoban::torrent::TorrentState::PendingEngineAdd) {
                    m_repo.updateTorrentState(
                        hash, tankoban::torrent::TorrentState::Active);
                }
            },
            Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::torrentAddFailed,
            this, [this](const QString& infoHash, const QString& errorMessage) {
                const QString hash = infoHash.toLower();
                if (m_repo.getTorrent(hash).has_value()) {
                    m_repo.updateTorrentState(
                        hash, tankoban::torrent::TorrentState::Error,
                        errorMessage);
                }
            },
            Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::resumeDataAvailable,
            this, [this](const QString& infoHash, const QByteArray& blob) {
                m_repo.updateTorrentResumeData(infoHash.toLower(), blob);
            },
            Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::torrentFinished,
            this, &TorrentClient::onTorrentFinished);
    connect(m_engine, &TorrentEngine::torrentError,
            this, &TorrentClient::onTorrentError);
    connect(m_engine, &TorrentEngine::storageMoved,
            this, &TorrentClient::onStorageMoved);
    connect(m_engine, &TorrentEngine::storageMoveFailed,
            this, &TorrentClient::onStorageMoveFailed);
    connect(m_engine, &TorrentEngine::fileRenamed,
            this, &TorrentClient::onFileRenamed);
    connect(m_engine, &TorrentEngine::fileRenameFailed,
            this, &TorrentClient::onFileRenameFailed);
    connect(this, &TorrentClient::groupPublishComplete, this,
            [this](const QString&) {
                if (m_bridge)
                    m_bridge->notifyRootFoldersChanged(QStringLiteral("videos"));
            });

    // ── TORRENT_PERSISTENCE_COLLAPSE Phase 1.6 — first-boot legacy import ─
    //
    // Opens the SQLite repository at <dataDir>/torrents.db. If the DB file
    // does not yet exist BUT a legacy torrents.json sits next to it, run
    // LegacyImporter::importInto exactly once to pre-populate the four
    // SQLite tables from the legacy stores. On subsequent boots the DB
    // exists and the importer is skipped — schema_meta.migration_completed_at
    // is the authoritative "already migrated" marker (importInto stamps it
    // inside the same transaction as the bulk inserts).
    //
    // Critically: the legacy loadRecords() / loadStreamBulkGroups() /
    // addFromResume hydration path BELOW continues to run regardless. The
    // repository is the new durable substrate but Phase 1 does not yet
    // route any consumer through it — Phase 3 cuts over readers, Phase 4
    // removes the legacy writers entirely. Both stores coexist for the
    // Phase 1-3 window so a partial rollout never breaks UI behaviour.
    {
        const QString dataDir = m_bridge ? m_bridge->dataDir() : QString();
        if (!dataDir.isEmpty()) {
            const QString dbPath           = QDir(dataDir).filePath(QStringLiteral("torrents.db"));
            const QString torrentsJsonPath = QDir(dataDir).filePath(QStringLiteral("torrents.json"));
            const bool dbExists     = QFile::exists(dbPath);
            const bool legacyExists = QFile::exists(torrentsJsonPath);

            if (!m_repo.open(dbPath)) {
                qWarning() << "[TorrentClient] Failed to open repository at"
                           << dbPath;
            } else if (!dbExists && legacyExists) {
                qInfo() << "[TorrentClient] First-boot migration: legacy "
                           "JSON -> SQLite";
                tankoban::torrent::LegacySources sources;
                sources.torrentsJsonPath           = torrentsJsonPath;
                sources.streamBulkGroupsJsonPath   = QDir(dataDir).filePath(QStringLiteral("stream_bulk_groups.json"));
                sources.streamDownloadsJsonPath    = QDir(dataDir).filePath(QStringLiteral("stream_downloads.json"));
                sources.resumeCacheDir             = QDir(dataDir).filePath(QStringLiteral("torrent_cache/resume"));
                tankoban::torrent::LegacyImporter imp;
                const auto summary = imp.importInto(m_repo, sources);
                qInfo().noquote()
                    << "[TorrentClient] Migration summary:"
                    << "torrents="            << summary.torrentsImported
                    << "legacy_no_magnet="    << summary.torrentsLegacyNoMagnet
                    << "resume_blobs="        << summary.resumeBlobsAttached
                    << "groups="              << summary.streamGroupsImported
                    << "items="               << summary.streamGroupItemsImported
                    << "downloads="           << summary.streamDownloadsImported;
                for (const auto& w : summary.warnings) {
                    qWarning().noquote()
                        << "[TorrentClient] migration warning:" << w;
                }
            }
        } else {
            qWarning() << "[TorrentClient] Bridge dataDir unavailable; "
                          "skipping repository open + first-boot importer";
        }

        // ── TORRENT_PERSISTENCE_COLLAPSE Phase 4.5 — two-boot retention ──
        //
        // After Phase 1.6 migration completes (schema_meta has
        // migration_completed_at), the legacy files on disk are kept around
        // for two clean reboots so a user noticing a regression can roll
        // back by reverting Tankoban and continuing from JSON state. On the
        // SECOND boot post-migration (first_clean_boot_at stamped on the
        // first boot's clean shutdown), rename the legacy files to
        // .legacy-imported-YYYY-MM-DD.bak so they stop being read by the
        // importer existence-check on the third boot. Renames not deletes
        // — full manual rollback still possible until the user deletes the
        // .bak files themselves.
        if (!dataDir.isEmpty() && m_repo.isOpen()) {
            const QString migration =
                m_repo.metaValue(QStringLiteral("migration_completed_at"));
            if (!migration.isEmpty()) {
                const QString firstClean =
                    m_repo.metaValue(QStringLiteral("legacy_first_clean_boot_at"));
                const QString cleaned =
                    m_repo.metaValue(QStringLiteral("legacy_files_cleaned_at"));
                if (!firstClean.isEmpty() && cleaned.isEmpty()) {
                    renameLegacyFilesToBak(dataDir);
                    m_repo.setMetaValue(
                        QStringLiteral("legacy_files_cleaned_at"),
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
                    qInfo() << "[TorrentClient] Phase 4.5: renamed legacy "
                               "files to .bak (second boot post-migration)";
                }
            }
        }
    }

    loadRecords();
    loadStreamBulkGroups();
    m_engine->start();

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — addFromResume MUST run
    // before reconcileStreamBulkGroups. The reconcile pass's heal-libtorrent
    // hotfix (added 2026-05-11) + cohortMaybeAdvanceAll both call into
    // m_engine via startTorrent / resumeTorrent / pauseTorrent — those
    // engine methods early-return on `!handle.is_valid()`. With reconcile
    // running BEFORE addFromResume (the prior order), the libtorrent
    // session has zero handles when reconcile fires, so the heal pass
    // and cohort advance were both no-ops. Moving addFromResume up
    // ensures handles exist when reconcile + heal pass run, which is
    // what actually fixes the cohort sequential semantic on boot when
    // m_records states drifted (e.g. from a late-firing
    // metadata_received_alert in onMetadataReady).
    bool anyChanged = false;
    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        QString hash = it.key();
        QJsonObject rec = it.value().toObject();
        QString resumePath = m_bridge->dataDir() + "/torrent_cache/resume/" + hash + ".fastresume";
        QString savePath = rec["savePath"].toString();
        QString state = rec["state"].toString();

        // "paused" stays paused; everything else (downloading, completed/seeding) resumes
        bool shouldPause = (state == "paused");

        QString restored = m_engine->addFromResume(resumePath, savePath, shouldPause);
        if (restored.isEmpty()) {
            qWarning() << "Orphaned torrent record (no resume data):" << hash;
            rec["state"] = QStringLiteral("error");
            rec["errorMessage"] = QStringLiteral("Resume data missing — re-add torrent manually");
            *it = rec;
            anyChanged = true;
        }
    }
    if (anyChanged)
        saveRecords();

    for (const auto& row :
         m_repo.listTorrentsByState(tankoban::torrent::TorrentState::PendingEngineAdd)) {
        const QString hash = row.hash.toLower();
        if (hash.isEmpty())
            continue;
        if (row.magnetUri.isEmpty()) {
            m_repo.updateTorrentState(
                hash,
                tankoban::torrent::TorrentState::Error,
                QStringLiteral("Pending row with no magnet (cannot replay)"));
            continue;
        }
        if (m_engine->hasTorrent(hash)) {
            m_repo.updateTorrentState(hash, tankoban::torrent::TorrentState::Active);
            continue;
        }
        const QString addedHash = m_engine->addMagnet(row.magnetUri, row.savePath);
        if (addedHash.isEmpty() || addedHash.compare(hash, Qt::CaseInsensitive) != 0) {
            m_repo.updateTorrentState(
                hash,
                tankoban::torrent::TorrentState::Error,
                QStringLiteral("Replay failed: engine rejected magnet"));
            if (!addedHash.isEmpty())
                m_engine->removeTorrent(addedHash, /*deleteFiles=*/false);
        } else {
            m_repo.updateTorrentState(hash, tankoban::torrent::TorrentState::Active);
        }
    }

    reconcileStreamBulkGroups();

    retryStreamBulkPublishing();

    // Sweep orphan .fastresume files — any file in the resume directory whose
    // hash is not in m_records. These accumulate when a draft torrent (one in
    // AddTorrentDialog's metadata-resolution window) leaks a resume file that
    // TorrentEngine::removeTorrent never got to clean up (typical cause: app
    // crash / force-kill while the dialog was open). Phase 2 Batch 2.1 stops
    // new leaks at the source by skipping save_resume_data for drafts, but
    // existing leftover orphans need this retroactive sweep. Runs once per
    // boot after re-adds complete — cheap (tens of files at most), quiet.
    const QString resumeDir = m_bridge->dataDir()
        + QStringLiteral("/torrent_cache/resume");
    QDir rd(resumeDir);
    const auto orphanCandidates = rd.entryInfoList(
        {QStringLiteral("*.fastresume")},
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : orphanCandidates) {
        const QString hash = fi.completeBaseName();
        if (m_repo.hasTorrent(hash)) continue;
        qDebug() << "TorrentClient: removing orphan resume file:" << fi.fileName();
        QFile::remove(fi.absoluteFilePath());
    }

    // One-shot history retro-compact — collapses duplicate entries per
    // infoHash (bloat accumulated before Phase 1 Batch 1.1's re-fire guard
    // landed). Idempotent after first pass.
    compactHistory();

    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.3 (2026-05-20) — T9's 2s
    // orphan-recovery sweep is retired. Phase 2.3's pending_engine_add row
    // replay handles the deterministic recovery for the F9 scenario T9 was
    // papering over (movie records whose engine handle didn't survive a
    // restart). The 2s heuristic delay is gone.
}

TorrentClient::~TorrentClient()
{
    m_engine->stop();
    saveRecords();
    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.5 — stamp legacy_first_clean_boot_at
    // on the first clean shutdown post-migration. Next boot's ctor sees this
    // stamp + the empty legacy_files_cleaned_at and runs renameLegacyFilesToBak.
    // The two-boot window gives the user a recovery surface between migration
    // landing and the legacy files leaving the active dir.
    if (m_repo.isOpen()) {
        const QString migration =
            m_repo.metaValue(QStringLiteral("migration_completed_at"));
        const QString firstClean =
            m_repo.metaValue(QStringLiteral("legacy_first_clean_boot_at"));
        if (!migration.isEmpty() && firstClean.isEmpty()) {
            m_repo.setMetaValue(
                QStringLiteral("legacy_first_clean_boot_at"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        }
    }
}

// TORRENT_PERSISTENCE_COLLAPSE Phase 4.5 (2026-05-20) — rename helper.
// Both top-level legacy JSON files AND the per-hash .fastresume cache get a
// dated .bak suffix so the importer existence-check at next-boot doesn't
// re-trigger. Renames not deletes — manual rollback still possible until the
// user removes the .bak files. Defensive try-then-warn per file; one bad
// rename doesn't abort the sweep.
void TorrentClient::renameLegacyFilesToBak(const QString& dataDir)
{
    const QString suffix = QStringLiteral(".legacy-imported-")
                           + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
                           + QStringLiteral(".bak");

    const QStringList topLevel = {
        QStringLiteral("torrents.json"),
        QStringLiteral("stream_bulk_groups.json"),
        QStringLiteral("stream_downloads.json"),
    };
    for (const QString& name : topLevel) {
        const QString src = QDir(dataDir).filePath(name);
        if (!QFile::exists(src)) continue;
        const QString dst = src + suffix;
        if (!QFile::rename(src, dst)) {
            qWarning() << "[TorrentClient] Phase 4.5: failed to rename"
                       << src << "to" << dst;
        }
    }

    const QString resumeDir = QDir(dataDir).filePath(QStringLiteral("torrent_cache/resume"));
    QDir resume(resumeDir);
    if (!resume.exists()) return;
    const QStringList fastresumes =
        resume.entryList({QStringLiteral("*.fastresume")}, QDir::Files);
    for (const QString& f : fastresumes) {
        const QString src = resume.filePath(f);
        if (!QFile::rename(src, src + suffix)) {
            qWarning() << "[TorrentClient] Phase 4.5: failed to rename"
                       << src;
        }
    }
    qInfo() << "[TorrentClient] Phase 4.5: legacy file rename swept"
            << topLevel.size() << "top-level files +"
            << fastresumes.size() << ".fastresume cache entries";
}

// ── Persistence ─────────────────────────────────────────────────────────────
void TorrentClient::loadRecords()
{
    auto data = m_bridge->store().read(RECORDS_FILE);
    m_records = data.value("active").toObject();
    bool changed = false;
    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        QJsonObject rec = it.value().toObject();
        if (!rec.contains("streamGroupId")) {
            rec["streamGroupId"] = QString();
            *it = rec;
            changed = true;
        }
    }
    if (changed)
        saveRecords();
}

void TorrentClient::saveRecords()
{
    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.4 (2026-05-20) — JsonStore write
    // path dropped. Phase 2 alert-handler mirrors (onMetadataReady /
    // onTorrentFinished / onTorrentError / onStorageMoved + startDownload)
    // already keep the repository's torrents table in sync as the durable
    // source of truth. m_records in-memory map continues to be maintained
    // because numerous read paths (devTorrentsSnapshot, hasActive*,
    // imdb-by-hash lookups in onPieceFinished, etc.) still serve from it;
    // those readers move to the repo in a future cleanup pass alongside
    // m_records removal itself. torrents.json on disk is preserved per
    // Phase 4.5's two-boot retention window.
}

void TorrentClient::loadStreamBulkGroups()
{
    auto data = m_bridge->store().read(STREAM_BULK_GROUPS_FILE);
    m_streamBulkGroups = data.value("groups").isObject()
        ? data.value("groups").toObject()
        : data;

    bool changed = false;
    QJsonObject normalized;
    for (auto it = m_streamBulkGroups.begin(); it != m_streamBulkGroups.end(); ++it) {
        if (!it.value().isObject()) {
            changed = true;
            continue;
        }
        normalized.insert(it.key(), normalizeStreamBulkGroup(it.key(), it.value().toObject(), &changed));
    }
    if (normalized != m_streamBulkGroups) {
        m_streamBulkGroups = normalized;
        changed = true;
    }
    if (changed)
        saveStreamBulkGroups();
}

void TorrentClient::saveStreamBulkGroups()
{
    QSet<QString> beforePrune;
    for (auto it = m_streamBulkGroups.constBegin(); it != m_streamBulkGroups.constEnd(); ++it)
        beforePrune.insert(it.key());

    m_streamBulkGroups = pruneTerminalStreamBulkGroups(m_streamBulkGroups);

    QSet<QString> afterPrune;
    for (auto it = m_streamBulkGroups.constBegin(); it != m_streamBulkGroups.constEnd(); ++it)
        afterPrune.insert(it.key());
    for (const QString& groupId : beforePrune) {
        if (!afterPrune.contains(groupId))
            m_repo.removeStreamGroup(groupId);
    }

    for (auto it = m_streamBulkGroups.constBegin(); it != m_streamBulkGroups.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject group = it.value().toObject();
        const auto groupRow = streamGroupRowFromBulkGroup(it.key(), group);
        if (!groupRow.groupId.isEmpty())
            m_repo.upsertStreamGroup(groupRow);
        const QString groupId = groupRow.groupId.isEmpty() ? it.key() : groupRow.groupId;
        const QJsonArray items = group.value(QStringLiteral("items")).toArray();
        for (const auto& itemValue : items) {
            if (!itemValue.isObject())
                continue;
            const auto itemRow =
                streamGroupItemRowFromBulkItem(groupId, itemValue.toObject());
            if (!itemRow.itemId.isEmpty())
                m_repo.upsertStreamGroupItem(itemRow);
        }
    }

    // TORRENT_PERSISTENCE_COLLAPSE Phase 3.5 (2026-05-20) — JsonStore write
    // path dropped. Phase 2.6's per-group / per-item repo mirror writes
    // above are now the only durable sink. The m_streamBulkGroups in-memory
    // map is still maintained because numerous read paths (UI snapshot
    // queries, devBulkGroupsSnapshot, isTerminalStreamBulkState helpers)
    // still serve from it; those readers move to the repo in Phase 4 along
    // with the deletion of the map itself.
}

QJsonObject TorrentClient::streamBulkGroups() const
{
    return m_streamBulkGroups;
}

QString TorrentClient::streamBulkGroupIdToFolderName(const QString& groupId)
{
    QString folderName = groupId;
    folderName.replace(QLatin1Char(':'), QLatin1Char('-'));
    return folderName;
}

QString TorrentClient::streamBulkFolderNameToGroupId(const QString& folderName)
{
    QString groupId = folderName;
    groupId.replace(QLatin1Char('-'), QLatin1Char(':'));
    return groupId;
}

QString TorrentClient::streamBulkStagingPath(const QString& videosRoot, const QString& groupId)
{
    if (videosRoot.isEmpty() || groupId.isEmpty())
        return {};
    return QDir::cleanPath(QDir(videosRoot).filePath(
        QStringLiteral(".tankoban-partial/%1").arg(streamBulkGroupIdToFolderName(groupId))));
}

void TorrentClient::reconcileStreamBulkGroups()
{
    bool changed = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QHash<QString, QString> activeStates;
    for (const TorrentInfo& info : listActive())
        activeStates.insert(info.infoHash.toLower(), info.stateString);
    QHash<QString, QString> stagingPathByGroupId;
    if (m_bridge) {
        const QStringList roots = m_bridge->rootFolders(QStringLiteral("videos"));
        if (!roots.isEmpty()) {
            QDir partialDir(QDir(roots.first()).filePath(QStringLiteral(".tankoban-partial")));
            const QFileInfoList entries = partialDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& entry : entries) {
                stagingPathByGroupId.insert(
                    streamBulkFolderNameToGroupId(entry.fileName()),
                    entry.absoluteFilePath());
            }
        }
    }

    for (auto groupIt = m_streamBulkGroups.begin(); groupIt != m_streamBulkGroups.end(); ++groupIt) {
        QJsonObject group = groupIt.value().toObject();
        QJsonArray items = group.value("items").toArray();
        bool groupChanged = false;

        if (group.value("stagingPath").toString().isEmpty() &&
            stagingPathByGroupId.contains(groupIt.key())) {
            group["stagingPath"] = QDir::cleanPath(stagingPathByGroupId.value(groupIt.key()));
            groupChanged = true;
        }

        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            const QString state = item.value("itemState").toString();
            if (isTerminalStreamBulkState(state))
                continue;

            const QString infoHash = item.value("infoHash").toString();
            const QString torrentKey = infoHash.toLower();
            const bool hasRecord = !infoHash.isEmpty() && m_repo.hasTorrent(infoHash);
            const QString canonicalPath = canonicalPathForStreamBulkItem(group, item);
            const bool destinationExists =
                !canonicalPath.isEmpty() && QFileInfo::exists(canonicalPath);

            if (isPublishingStreamBulkState(state)) {
                if (!hasRecord) {
                    item["itemState"] = QString::fromLatin1(kStateOrphaned);
                    item["lastError"] = QStringLiteral("Torrent record missing during publish");
                    items.replace(i, item);
                    groupChanged = true;
                    continue;
                }

                const auto row = m_repo.getTorrent(infoHash);
                const tankoban::torrent::TorrentState persistedState =
                    row ? row->state : tankoban::torrent::TorrentState::Removed;
                const QString activeState = activeStates.value(torrentKey);
                const bool seedingLike =
                    // TorrentState::Completed collapses legacy "completed" + "seeding"
                    // strings — both meant "finished, may still be uploading".
                    persistedState == tankoban::torrent::TorrentState::Completed ||
                    activeState == QLatin1String("completed") ||
                    activeState == QLatin1String("seeding");
                if (seedingLike && destinationExists) {
                    item["itemState"] = QString::fromLatin1(kStatePublished);
                    item["lastError"] = QString();
                    items.replace(i, item);
                    groupChanged = true;
                }
                continue;
            }

            if (infoHash.isEmpty() || hasRecord)
                continue;

            if (destinationExists)
                continue;

            item["itemState"] = QString::fromLatin1(kStateOrphaned);
            item["lastError"] = QStringLiteral("Torrent record missing and canonical destination not found");
            items.replace(i, item);
            groupChanged = true;
        }

        if (groupChanged) {
            group["items"] = items;
            group["updatedAtMs"] = now;
            *groupIt = group;
            changed = true;
        }
    }

    if (changed) {
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(QString());  // empty groupId = full refresh
    }

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — repair stuck libtorrent
    // handles. Pre-fix Phase 2 dispatch (the V2 Phase 2 commit) left bulk
    // items in upload_mode at resolve_tmp/ because startDownload skipped
    // startTorrent when startPaused=true. Items showed correct cohort
    // states in our JSON (1 Downloading + N Pending) but ALL ran
    // concurrently in libtorrent at the wrong save path with upload_mode
    // set, so peer discovery was crippled. This one-shot pass calls
    // startTorrent for every non-terminal bulk item (idempotent: moves
    // from resolve_tmp → staging if not already there, clears
    // upload_mode, sets auto_managed) and immediately re-pauses any
    // item the cohort scheduler had recorded as Pending so the slot
    // semantics hold. Fixes Hemanth's "not connecting to any peers"
    // report 2026-05-11.
    for (auto groupIt = m_streamBulkGroups.constBegin();
         groupIt != m_streamBulkGroups.constEnd(); ++groupIt) {
        const QJsonObject group = groupIt.value().toObject();
        const QString stagingPath = group.value("stagingPath").toString();
        if (stagingPath.isEmpty()) continue;
        const QJsonArray groupItems = group.value("items").toArray();
        for (const auto& v : groupItems) {
            const QJsonObject item = v.toObject();
            const QString infoHash = item.value("infoHash").toString();
            if (infoHash.isEmpty()) continue;
            const QString state = item.value("itemState").toString();
            if (isTerminalStreamBulkState(state)) continue;
            // startTorrent is idempotent — only moves storage if the
            // current save path differs, always unsets upload_mode.
            m_engine->startTorrent(infoHash, stagingPath);
            // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — route the
            // pause / resume through TorrentClient's public wrappers (not
            // m_engine->...) so m_records.state gets synced to "paused" or
            // "downloading" + torrentUpdated signal fires. The bare engine
            // calls used previously left m_records.state stuck at whatever
            // it was loaded as (e.g. "metadata_ready" leaked from the
            // onMetadataReady defect fixed in the same wake), which on
            // subsequent boots routed back into addFromResume's
            // `shouldPause = (state == "paused")` gate as false, causing
            // every cohort item to re-add unpaused.
            if (state == QLatin1String(kStatePending))
                pauseTorrent(infoHash);
            else if (state == QLatin1String(kStateDownloading))
                resumeTorrent(infoHash);
        }
    }

    // STREAM_BULK_DOWNLOAD_V2 Phase 2 — restart-time cohort recovery.
    // After the existing reconcile pass classifies each item against
    // libtorrent ground truth, walk every group and ensure the cohort
    // invariant: ≤1 Downloading item per group + first Pending+infoHash
    // item resumed when slot is empty. Idempotent — if the cohort is
    // already healthy, every advance call short-circuits.
    cohortMaybeAdvanceAll();
}

void TorrentClient::upsertStreamBulkGroup(const StreamBulkGroupRecord& group)
{
    if (group.groupId.isEmpty()) return;
    const bool existed = m_streamBulkGroups.contains(group.groupId);
    m_streamBulkGroups[group.groupId] = streamBulkGroupToJson(group);
    saveStreamBulkGroups();
    if (!existed) {
        JsonlEventLog::instance().emitEvent(
            QStringLiteral("bulk.group_created"),
            QStringLiteral("upsert"),
            QJsonObject{{QStringLiteral("groupId"), group.groupId},
                        {QStringLiteral("imdb"), group.sourceSeriesId},
                        {QStringLiteral("kind"), group.groupKind},
                        {QStringLiteral("destinationRoot"), group.destinationRoot},
                        {QStringLiteral("items"), group.items.size()}});
    }
}

void TorrentClient::dispatchStreamBulkGroup(
    const StreamBulkGroupRecord& group,
    const tankostream::stream::BulkPackVerificationResult& verifierOutput)
{
    if (group.groupId.isEmpty() || !m_bridge)
        return;

    const QStringList videoRoots = m_bridge->rootFolders(QStringLiteral("videos"));
    if (videoRoots.isEmpty() || videoRoots.first().isEmpty()) {
        qWarning() << "Stream bulk dispatch failed: videos root is not configured for group"
                   << group.groupId;
        return;
    }

    const QString videosRoot = QDir(videoRoots.first()).absolutePath();
    const QString stagingPath = streamBulkStagingPath(videosRoot, group.groupId);
    if (stagingPath.isEmpty() || !QDir().mkpath(stagingPath)) {
        qWarning() << "Stream bulk dispatch failed: cannot create staging path"
                   << stagingPath << "for group" << group.groupId;
        return;
    }

    const tankostream::stream::BulkSelectionPlan& plan = verifierOutput.updatedPlan;
    QHash<QString, tankostream::stream::BulkSelectionItem> selectionByItemKey;
    for (const auto& item : plan.items)
        selectionByItemKey.insert(item.itemKey, item);

    StreamBulkGroupRecord prepared = group;
    prepared.stagingPath = stagingPath;
    if (prepared.destinationRoot.isEmpty())
        prepared.destinationRoot = videosRoot;
    for (StreamBulkGroupItem& item : prepared.items) {
        const auto selectionIt = selectionByItemKey.constFind(item.itemKey);
        if (selectionIt == selectionByItemKey.cend())
            continue;
        if (selectionIt->reason == tankostream::stream::BulkSelectionReason::MissingNoSource) {
            item.infoHash.clear();
            item.fileIndex = -1;
            item.itemState = StreamBulkItemState::MissingSource;
            item.lastError = QStringLiteral("No source found for episode");
            continue;
        }
        item.infoHash = tankostream::stream::makeTorrentKey(selectionIt->choice.infoHash);
        item.fileIndex = selectionIt->choice.fileIndex;
        item.itemState = StreamBulkItemState::Pending;
        item.lastError.clear();
    }

    // Persist before the first add/start call so restart reconciliation sees
    // the group even if dispatch is interrupted mid-loop.
    upsertStreamBulkGroup(prepared);
    emit streamBulkGroupsChanged(group.groupId);

    auto setItemDispatchState = [&](const QString& itemKey,
                                    const QString& infoHash,
                                    int fileIndex,
                                    StreamBulkItemState state,
                                    const QString& lastError = QString()) {
        if (!m_streamBulkGroups.contains(group.groupId) || itemKey.isEmpty())
            return;
        QJsonObject groupObj = m_streamBulkGroups.value(group.groupId).toObject();
        QJsonArray items = groupObj.value("items").toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            if (item.value("itemKey").toString() != itemKey)
                continue;
            item["infoHash"] = infoHash;
            item["torrentKey"] = tankostream::stream::makeTorrentKey(infoHash);
            item["fileIndex"] = fileIndex;
            item["fileKey"] = tankostream::stream::makeFileKey(infoHash, fileIndex);
            item["itemState"] = streamBulkItemStateToString(state);
            item["lastError"] = lastError;
            items.replace(i, item);
            groupObj["items"] = items;
            groupObj["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
            m_streamBulkGroups[group.groupId] = groupObj;
            return;
        }
    };

    auto baseConfig = [&]() {
        AddTorrentConfig config;
        config.category = QStringLiteral("videos");
        config.destinationPath = stagingPath;
        config.contentLayout = QStringLiteral("original");
        config.streamGroupId = group.groupId;
        // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — sequential=false for
        // bulk dispatch. Hemanth flagged crawl-speeds on Daredevil S02 pack
        // (4 KB/s with 1 peer / 0 seeds). The prior sequential=true was a
        // streaming-mode artifact carried over from initial Phase 5 dispatch
        // authoring: it forces strict piece-order which starves peer-piece
        // matching in low-seed swarms (the one available peer rarely has
        // the next-in-order piece). V2 bulk semantic is "download to library,
        // then play" — the user opens completed files from the Stream
        // library AFTER download finishes, NOT while in flight. Rarest-first
        // (libtorrent default, sequential=false) matches what manual Tankorent
        // adds (TankorentPage.cpp:2342) and TankoLibraryPage book downloads
        // (TankoLibraryPage.cpp:1555) already use. Note: this removes the
        // artificial throttle but doesn't change the swarm reality — a pack
        // with 0 seeds will still be slow because the bandwidth literally
        // isn't there.
        config.sequential = false;
        config.startPaused = false;
        return config;
    };

    if (plan.mode == tankostream::stream::BulkSelectionMode::Pack) {
        tankostream::stream::BulkSelectionItem packItem;
        bool foundPackItem = false;
        for (const auto& item : plan.items) {
            if (item.reason == tankostream::stream::BulkSelectionReason::PackCovered) {
                packItem = item;
                foundPackItem = true;
                break;
            }
        }
        if (!foundPackItem)
            return;

        const QString infoHash = tankostream::stream::makeTorrentKey(
            resolveMetadata(packItem.choice.magnetUri));
        if (infoHash.isEmpty()) {
            for (const auto& item : plan.items) {
                if (item.reason == tankostream::stream::BulkSelectionReason::PackCovered) {
                    setItemDispatchState(
                        item.itemKey, {}, item.choice.fileIndex,
                        StreamBulkItemState::MetadataFailed,
                        QStringLiteral("Pack magnet rejected by torrent engine"));
                }
            }
            saveStreamBulkGroups();
            emit streamBulkGroupsChanged(group.groupId);
            return;
        }

        AddTorrentConfig config = baseConfig();
        config.filePriorities = priorityVectorToMap(verifierOutput.filePriorities);
        startDownload(infoHash, config);

        for (const auto& item : plan.items) {
            if (item.reason != tankostream::stream::BulkSelectionReason::PackCovered)
                continue;
            setItemDispatchState(item.itemKey, infoHash, item.choice.fileIndex,
                                 StreamBulkItemState::Downloading);
        }
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(group.groupId);
        return;
    }

    // STREAM_BULK_DOWNLOAD_V2 Phase 2 — per-episode dispatch goes paused.
    // All Picked magnets are added to libtorrent with startPaused=true and
    // their item state stays at Pending. cohortMaybeAdvance below resumes
    // the first eligible item; subsequent advance calls fire from
    // mark/publish/onFileRenamed hook sites as the active slot frees up.
    for (const auto& item : plan.items) {
        if (item.reason != tankostream::stream::BulkSelectionReason::Picked)
            continue;

        const QString infoHash = tankostream::stream::makeTorrentKey(
            resolveMetadata(item.choice.magnetUri));
        if (infoHash.isEmpty()) {
            setItemDispatchState(
                item.itemKey, {}, item.choice.fileIndex,
                StreamBulkItemState::MetadataFailed,
                QStringLiteral("Episode magnet rejected by torrent engine"));
            continue;
        }

        AddTorrentConfig config = baseConfig();
        config.startPaused = true;
        startDownload(infoHash, config);
        setItemDispatchState(item.itemKey, infoHash, item.choice.fileIndex,
                             StreamBulkItemState::Pending);
    }
    saveStreamBulkGroups();
    emit streamBulkGroupsChanged(group.groupId);
    cohortMaybeAdvance(group.groupId);
}

// STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) — Remove from Library
// confirmation gating. Group key format `stream:<imdbId>:s<NN>:<unix-ms>`
// from the bulk spec gives us the prefix to filter. Reuses the existing
// streamBulkGroupAllTerminal helper.
bool TorrentClient::hasActiveStreamBulkGroupsForImdb(const QString& imdbId) const
{
    if (imdbId.isEmpty()) return false;
    const QString prefix = QStringLiteral("stream:") + imdbId + QLatin1Char(':');
    for (auto it = m_streamBulkGroups.constBegin();
         it != m_streamBulkGroups.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) continue;
        if (!streamBulkGroupAllTerminal(it.value().toObject()))
            return true;
    }
    return false;
}

QStringList TorrentClient::streamBulkGroupIdsForImdb(const QString& imdbId) const
{
    QStringList out;
    if (imdbId.isEmpty()) return out;
    const QString prefix = QStringLiteral("stream:") + imdbId + QLatin1Char(':');
    for (auto it = m_streamBulkGroups.constBegin();
         it != m_streamBulkGroups.constEnd(); ++it) {
        if (it.key().startsWith(prefix))
            out.append(it.key());
    }
    return out;
}

// STREAM_BULK_DOWNLOAD_V2 Phase 3 — episode-level bulk-download snapshot
// for the StreamDetailView progress column. Group key format
// `stream:<imdbId>:s<NN>:<unix-ms>` filters by imdbId; the group's
// `sourceIds.season` field disambiguates the season. Per item, the
// itemKey format `<seriesId>:S<NN>E<NN>` (per StreamBulkPlan.h
// makeItemKey) carries the episode number after "E". progressPct comes
// from listActive() for the matching infoHash for active torrents and
// is bracketed for terminal states.
QHash<int, QPair<QString, int>>
TorrentClient::streamBulkSnapshotForImdbSeason(const QString& imdbId, int season) const
{
    // TORRENT_PERSISTENCE_COLLAPSE Phase 3.3 (2026-05-20) — third UI projection
    // cutover. Stream season detail view's per-episode badge data (which
    // renders the small "ep3 80%, ep4 paused, ep5 done" badges in the season
    // table) now sources its (groupId, itemId, state, infoHash) rows from the
    // SQLite repository's stream_groups + stream_group_items tables instead
    // of the legacy m_streamBulkGroups JSON map.
    //
    // Contract preserved verbatim from the legacy walker:
    //   - Returns episode_num -> (state, pct) keyed by episode.
    //   - state is the cohort item state string ("Pending"/"Downloading"/
    //     "Published"/...).
    //   - pct semantics: Downloading -> engine progress in 0..100;
    //     Pending -> 0; Published or Publishing -> 100; failed/cancelled
    //     terminal -> -1 sentinel (UI renders as failure badge).
    //   - Dedup: when multiple groups touch the same episode (re-issued
    //     bulk), the LATEST group's entry wins. Legacy relied on
    //     QJsonObject insertion order matching save chronology; we now
    //     sort StreamGroupRow vector by createdAt ascending so the later-
    //     created group overwrites the earlier one via QHash::insert,
    //     reproducing the same semantic without depending on a SQL
    //     ORDER BY clause in the repo API.
    //   - Engine progress is sourced from listActive() (now also repo-
    //     backed post-Phase 3.1), so the live runtime overlay path is
    //     a single chain rooted at the repo.
    //
    // Episode number resolution defensive fallback:
    //   The Phase 1 importer (parseStreamGroupItems) currently reads only
    //   the "episode" JSON key when populating StreamGroupItemRow.episode.
    //   Real legacy data wrote items with itemKey "<imdb>:S<NN>E<NN>" and
    //   no clean "episode" field, so already-migrated rows can have
    //   row.episode == 0. The fallback below parses the trailing E<NN>
    //   off row.itemId, matching the exact regex the legacy code used.
    //   Filed for cleanup as a follow-up commit to the importer; until
    //   then this defensive parse keeps the post-cutover behaviour
    //   identical for the existing data set.
    QHash<int, QPair<QString, int>> out;
    if (imdbId.isEmpty() || season <= 0)
        return out;

    QHash<QString, int> progressByHash;  // infoHash (lowercase) -> 0..100
    for (const TorrentInfo& info : listActive()) {
        if (info.infoHash.isEmpty())
            continue;
        progressByHash.insert(info.infoHash.toLower(),
                              static_cast<int>(qBound(0.0f, info.progress, 1.0f) * 100.0f));
    }

    // Sort groups by createdAt ascending so QHash::insert overwrites the
    // earlier group with the later one — reproducing the legacy save-
    // chronology dedup semantic.
    auto groups = m_repo.listStreamGroupsByImdbSeason(imdbId, season);
    std::sort(groups.begin(), groups.end(),
              [](const tankoban::torrent::StreamGroupRow& a,
                 const tankoban::torrent::StreamGroupRow& b) {
                  return a.createdAt < b.createdAt;
              });

    for (const auto& g : groups) {
        const auto items = m_repo.listStreamGroupItems(g.groupId);
        for (const auto& it : items) {
            int episodeNum = it.episode;
            if (episodeNum <= 0) {
                const int eIdx = it.itemId.lastIndexOf(QLatin1Char('E'));
                if (eIdx > 0) {
                    bool ok = false;
                    const int parsed = it.itemId.mid(eIdx + 1).toInt(&ok);
                    if (ok)
                        episodeNum = parsed;
                }
            }
            if (episodeNum <= 0)
                continue;

            const QString& state = it.state;
            int pct = -1;
            if (state == QLatin1String(kStateDownloading)) {
                pct = progressByHash.value(it.infoHash.toLower(), 0);
            } else if (state == QLatin1String(kStatePending)) {
                pct = 0;
            } else if (state == QLatin1String(kStatePublished) ||
                       state == QLatin1String(kStatePublishing)) {
                pct = 100;
            }  // failed states: pct = -1 sentinel

            out.insert(episodeNum, qMakePair(state, pct));
        }
    }
    return out;
}

QJsonObject TorrentClient::streamBulkGroupsSnapshot() const
{
    return m_streamBulkGroups;   // QJsonObject is implicitly shared, this is cheap
}

bool TorrentClient::imdbHasActiveCohort(const QString& imdbId) const
{
    if (imdbId.isEmpty())
        return false;
    const QString prefix = QStringLiteral("stream:") + imdbId + QLatin1Char(':');
    for (auto it = m_streamBulkGroups.constBegin();
         it != m_streamBulkGroups.constEnd(); ++it) {
        if (!it.key().startsWith(prefix))
            continue;
        const QJsonArray items = it.value().toObject()
            .value(QStringLiteral("items")).toArray();
        for (const auto& v : items) {
            const QString state = v.toObject()
                .value(QStringLiteral("itemState")).toString();
            if (!isTerminalStreamBulkState(state))
                return true;
        }
    }
    return false;
}

void TorrentClient::cancelStreamBulkGroup(const QString& groupId)
{
    cancelStreamBulkGroup(groupId, /*deleteFilesOverride=*/std::nullopt);
}

void TorrentClient::cancelStreamBulkGroup(const QString& groupId, bool deleteFiles)
{
    cancelStreamBulkGroup(groupId, std::optional<bool>(deleteFiles));
}

void TorrentClient::cancelStreamBulkGroup(const QString& groupId,
                                          std::optional<bool> deleteFilesOverride)
{
    if (groupId.isEmpty() || !m_streamBulkGroups.contains(groupId))
        return;

    const QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    const bool allPublished = streamBulkGroupAllPublished(group);
    const QString stagingPath = group.value("stagingPath").toString();
    const QJsonArray items = group.value("items").toArray();

    QHash<QString, bool> deleteFilesByHash;
    bool hasPublishing = false;
    bool allOrphaned = !items.isEmpty();
    for (const auto& value : items) {
        const QJsonObject item = value.toObject();
        allOrphaned = allOrphaned &&
            item.value("itemState").toString() == QLatin1String(kStateOrphaned);
        const QString infoHash = item.value("infoHash").toString();
        if (infoHash.isEmpty()) continue;
        const QString state = item.value("itemState").toString();
        if (isPublishingStreamBulkState(state) ||
            state == QLatin1String(kStatePublished) ||
            state == QLatin1String(kStateCompleted)) {
            hasPublishing = true;
            deleteFilesByHash.insert(infoHash, false);
        } else if (!deleteFilesByHash.contains(infoHash)) {
            deleteFilesByHash.insert(
                infoHash,
                deleteFilesOverride.has_value()
                    ? *deleteFilesOverride
                    : !allPublished);
        }
    }

    if (allOrphaned) {
        m_streamBulkGroups.remove(groupId);
        m_repo.removeStreamGroup(groupId);
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(groupId);
        return;
    }

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-10 — extend the "remove stuck
    // group" shortcut to all-failure-or-cancelled groups (no live torrent
    // state to clean up, no published items to preserve). Without this,
    // a user-cancelled-from-Tankorent group whose items all went to
    // Cancelled state was permanently stuck in m_streamBulkGroups: line
    // 932 below skips already-terminal items, so the function would no-op
    // out without removing the group. Hemanth flagged 2026-05-10.
    bool allTerminalNoSuccess = !items.isEmpty();
    for (const auto& value : items) {
        const QString state = value.toObject().value("itemState").toString();
        const bool isCleanupState =
            state == QLatin1String(kStateOrphaned) ||
            state == QLatin1String(kStateCancelled) ||
            state == QLatin1String(kStateFailed) ||
            state == QLatin1String(kStateMissingSource) ||
            state == QLatin1String(kStateMetadataFailed) ||
            state == QLatin1String(kStatePublishFailed);
        if (!isCleanupState) {
            allTerminalNoSuccess = false;
            break;
        }
    }
    if (allTerminalNoSuccess) {
        m_streamBulkGroups.remove(groupId);
        m_repo.removeStreamGroup(groupId);
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(groupId);
        return;
    }

    for (auto it = deleteFilesByHash.begin(); it != deleteFilesByHash.end(); ++it)
        deleteTorrent(it.key(), it.value());

    QJsonObject mutableGroup = m_streamBulkGroups.value(groupId).toObject();
    QJsonArray mutableItems = mutableGroup.value("items").toArray();
    bool changed = false;
    for (int i = 0; i < mutableItems.size(); ++i) {
        QJsonObject item = mutableItems.at(i).toObject();
        const QString state = item.value("itemState").toString();
        if (isTerminalStreamBulkState(state))
            continue;
        item["itemState"] = QString::fromLatin1(kStateCancelled);
        item["lastError"] = QStringLiteral("Stream bulk group cancelled");
        mutableItems.replace(i, item);
        changed = true;
    }
    if (changed) {
        mutableGroup["items"] = mutableItems;
        mutableGroup["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
        m_streamBulkGroups[groupId] = mutableGroup;
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(groupId);
    }

    if (!stagingPath.isEmpty() && (allPublished || !hasPublishing))
        QDir(stagingPath).removeRecursively();
}

void TorrentClient::cancelStreamBulkItem(const QString& groupId,
                                          const QString& itemKey,
                                          bool deleteFile)
{
    if (groupId.isEmpty() || itemKey.isEmpty()) return;
    if (!m_streamBulkGroups.contains(groupId)) return;

    QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    QJsonArray items = group.value("items").toArray();
    QString infoHash;
    int imdbSeason = 0;
    int episodeNum = 0;
    QString imdbId;

    // Find the item; capture its infoHash + identity fields BEFORE marking
    // it Cancelled (after marking, the JSON object is replaced).
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items[i].toObject();
        if (item.value(QStringLiteral("itemKey")).toString() != itemKey) continue;
        infoHash = item.value(QStringLiteral("infoHash")).toString();

        // Pull identity fields for StreamDownloadIndex eviction. Prefer
        // explicit fields, fall back to group.sourceIds + itemKey parse
        // (mirrors the fallback chain in publishStreamBulkItemsForTorrent).
        imdbId    = item.value(QStringLiteral("imdbId")).toString();
        imdbSeason= item.value(QStringLiteral("streamSeason")).toInt(0);
        episodeNum= item.value(QStringLiteral("episodeNum")).toInt(0);
        if (imdbId.isEmpty()) {
            imdbId = group.value(QStringLiteral("sourceIds")).toObject()
                          .value(QStringLiteral("seriesId")).toString();
        }
        if (imdbSeason == 0) {
            imdbSeason = group.value(QStringLiteral("sourceIds")).toObject()
                              .value(QStringLiteral("season")).toInt(0);
        }
        if (episodeNum == 0) {
            const int eIdx = itemKey.lastIndexOf(QLatin1Char('E'));
            if (eIdx > 0) {
                episodeNum = itemKey.mid(eIdx + 1).toInt();
            }
        }

        item[QStringLiteral("itemState")] = QString::fromLatin1(kStateCancelled);
        item[QStringLiteral("lastError")] = QString();
        items.replace(i, item);
        break;
    }

    group[QStringLiteral("items")] = items;
    group[QStringLiteral("updatedAtMs")] = QDateTime::currentMSecsSinceEpoch();
    m_streamBulkGroups[groupId] = group;
    saveStreamBulkGroups();

    if (!infoHash.isEmpty()) {
        deleteTorrent(infoHash, deleteFile);
    }
    if (deleteFile && m_streamDownloadIndex && !imdbId.isEmpty()
        && imdbSeason > 0 && episodeNum > 0) {
        // StreamDownloadIndex has no per-episode evict; use filePathFor to
        // resolve the canonical path then evictByPath to remove exactly this
        // one episode entry without disturbing other episodes for the same show.
        const auto maybePath =
            m_streamDownloadIndex->filePathFor(imdbId, imdbSeason, episodeNum);
        if (maybePath.has_value()) {
            m_streamDownloadIndex->evictByPath(
                StreamDownloadIndex::computeCanonicalKey(*maybePath));
        }
    }
    emit streamBulkGroupsChanged(groupId);
}

bool TorrentClient::updateStreamBulkGroupItemState(const QString& groupId,
                                                   const QString& itemKey,
                                                   StreamBulkItemState state,
                                                   const QString& lastError)
{
    if (!m_streamBulkGroups.contains(groupId) || itemKey.isEmpty())
        return false;

    QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    QJsonArray items = group.value("items").toArray();
    bool changed = false;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        if (item.value("itemKey").toString() != itemKey)
            continue;
        item["itemState"] = streamBulkItemStateToString(state);
        item["lastError"] = lastError;
        items.replace(i, item);
        changed = true;
        break;
    }

    if (!changed) return false;
    group["items"] = items;
    group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
    m_streamBulkGroups[groupId] = group;
    saveStreamBulkGroups();
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("bulk.item_state_changed"),
        QStringLiteral("update_item_state"),
        QJsonObject{{QStringLiteral("groupId"), groupId},
                    {QStringLiteral("itemKey"), itemKey},
                    {QStringLiteral("state"), streamBulkItemStateToString(state)},
                    {QStringLiteral("lastError"), lastError}});
    return true;
}

bool TorrentClient::bumpStreamBulkGroupRetryGeneration(const QString& groupId)
{
    if (!m_streamBulkGroups.contains(groupId))
        return false;
    QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    group["retryGeneration"] = group.value("retryGeneration").toInt(0) + 1;
    group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
    m_streamBulkGroups[groupId] = group;
    saveStreamBulkGroups();
    return true;
}

void TorrentClient::retryStreamBulkGroupFailedItems(const QString& groupId,
                                                    const QString& itemKey)
{
    if (groupId.isEmpty() || !m_streamBulkGroups.contains(groupId))
        return;

    QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    QJsonArray items = group.value("items").toArray();
    QStringList sourceRetryItemKeys;
    bool changed = false;
    bool recordsChanged = false;

    group["retryGeneration"] = group.value("retryGeneration").toInt(0) + 1;

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — single-item filter.
        if (!itemKey.isEmpty() &&
            item.value(QStringLiteral("itemKey")).toString() != itemKey) {
            continue;
        }
        const QString thisItemKey = item.value("itemKey").toString();
        const QString state = item.value("itemState").toString();
        const QString infoHash = item.value("infoHash").toString();
        const QString lastError = item.value("lastError").toString();

        if (isStreamBulkSourceRetryState(state, infoHash)) {
            item["itemState"] = QString::fromLatin1(kStatePending);
            item["lastError"] = QString();
            items.replace(i, item);
            changed = true;
            if (!thisItemKey.isEmpty() && !sourceRetryItemKeys.contains(thisItemKey))
                sourceRetryItemKeys.push_back(thisItemKey);
            continue;
        }

        if (state == QLatin1String(kStatePublishFailed)) {
            if (infoHash.isEmpty()) {
                item["itemState"] = QString::fromLatin1(kStateOrphaned);
                item["lastError"] = QStringLiteral("Publish retry unavailable: torrent record missing");
            } else {
                item["itemState"] = QString::fromLatin1(kStatePublishing);
                item["lastError"] = QString();
            }
            items.replace(i, item);
            changed = true;
            continue;
        }

        if (state == QLatin1String(kStateFailed) &&
            !infoHash.isEmpty() &&
            m_repo.hasTorrent(infoHash) &&
            lastError.startsWith(QStringLiteral("Torrent error:"), Qt::CaseInsensitive)) {
            m_engine->resumeTorrent(infoHash);
            // Flip state Error → Active and clear errorMessage via the repo;
            // alert-handler mirrors keep m_records[hash] in sync until P5.5
            // close-out deletes the cache entirely.
            m_repo.updateTorrentState(infoHash,
                                      tankoban::torrent::TorrentState::Active,
                                      QString());
            recordsChanged = true;
            item["itemState"] = QString::fromLatin1(kStateDownloading);
            item["lastError"] = QString();
            items.replace(i, item);
            changed = true;
        }
    }

    group["items"] = items;
    group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
    m_streamBulkGroups[groupId] = group;
    if (recordsChanged)
        saveRecords();
    saveStreamBulkGroups();
    emit streamBulkGroupsChanged(groupId);

    retryStreamBulkPublishing();

    if (!sourceRetryItemKeys.isEmpty())
        emit streamBulkRetrySourcePickRequested(groupId, sourceRetryItemKeys);
    else if (changed)
        maybeEmitStreamBulkGroupPublishComplete(groupId);
}

void TorrentClient::setStreamBulkItemPaused(const QString& infoHash, bool paused)
{
    if (infoHash.isEmpty()) return;
    bool changed = false;
    QString affectedGroupId;
    for (auto groupIt = m_streamBulkGroups.begin();
         groupIt != m_streamBulkGroups.end(); ++groupIt) {
        QJsonObject group = groupIt.value().toObject();
        QJsonArray items = group.value(QStringLiteral("items")).toArray();
        bool groupChanged = false;
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item.value(QStringLiteral("infoHash")).toString() != infoHash) continue;
            const QString cur = item.value(QStringLiteral("itemState")).toString();
            if (paused && cur == QLatin1String(kStateDownloading)) {
                item[QStringLiteral("itemState")] = QString::fromLatin1(kStatePaused);
                items.replace(i, item);
                changed = true;
                groupChanged = true;
                affectedGroupId = groupIt.key();
            } else if (!paused && cur == QLatin1String(kStatePaused)) {
                item[QStringLiteral("itemState")] = QString::fromLatin1(kStateDownloading);
                items.replace(i, item);
                changed = true;
                groupChanged = true;
                affectedGroupId = groupIt.key();
            }
        }
        if (groupChanged) {
            group[QStringLiteral("items")] = items;
            group[QStringLiteral("updatedAtMs")] = QDateTime::currentMSecsSinceEpoch();
            *groupIt = group;
            break;
        }
    }
    if (changed) {
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(affectedGroupId);
    }
}

// STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — recovery action for the
// Tankorent group menu's "Restart group" entry. Distinct from
// retryStreamBulkGroupFailedItems (which only addresses items already in
// Failed/MissingSource/MetadataFailed/PublishFailed states): this one
// also clears libtorrent error state on stuck-but-not-yet-failed items
// via forceRecheck, then resets every non-Published item to Pending so
// cohortMaybeAdvance can re-pick the head. Published + Completed items
// are intentionally untouched — they're terminal-success and restarting
// would erase user-visible library state.
void TorrentClient::restartStreamBulkGroup(const QString& groupId)
{
    if (groupId.isEmpty() || !m_streamBulkGroups.contains(groupId))
        return;

    QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    QJsonArray items = group.value("items").toArray();

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — orphan-by-missing-resume
    // recovery. Per /superpowers:systematic-debugging Phase 1-4 trace:
    // Hemanth's Daredevil S02 group had three child torrents stuck with
    // m_records[hash].state="error" + errorMessage="Resume data missing
    // — re-add torrent manually" (set at TorrentClient.cpp:384 on boot
    // when addFromResume returned empty for them — pre-existing damage
    // from the pre-startTorrent+upload_mode hotfix dispatch path).
    // forceRecheck cannot recover these — there's no live libtorrent
    // handle to recheck. Recovery requires source-pick rerun via the
    // existing retryStreamBulkGroupFailedItems plumbing, gated on
    // MissingSource state.
    //
    // Build TWO snapshots: persisted state from listActive() (which
    // returns m_records-backed TorrentInfo with falls-back stateString
    // from the record) + live-handle presence from m_engine->allStatuses()
    // (which returns ONLY hashes with valid handles in the libtorrent
    // session). The distinction is the diagnostic signal:
    //   persisted "error" + live handle present → transient error,
    //       forceRecheck via the public wrapper
    //   persisted "error" + live handle ABSENT → orphan-by-missing-
    //       resume, demote item to MissingSource + delete the zombie
    //       record + trigger retryStreamBulkGroupFailedItems below.
    QHash<QString, QString> persistedStates;
    for (const TorrentInfo& info : listActive())
        persistedStates.insert(info.infoHash.toLower(), info.stateString);

    QSet<QString> liveHashes;
    for (const TorrentStatus& s : m_engine->allStatuses())
        liveHashes.insert(s.infoHash.toLower());

    bool changed = false;
    bool anyOrphanRecovered = false;
    QStringList orphanedInfoHashes;  // cleanup list — applied after items[] is committed

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        const QString state = item.value("itemState").toString();
        // Leave Published + Completed alone — they're terminal-success.
        if (state == QLatin1String(kStatePublished) ||
            state == QLatin1String(kStateCompleted))
            continue;

        const QString infoHash = item.value("infoHash").toString();
        if (!infoHash.isEmpty()) {
            const QString hashKey = infoHash.toLower();
            const QString live = persistedStates.value(hashKey);
            const bool hasLiveHandle = liveHashes.contains(hashKey);

            if (live == QLatin1String("error") && !hasLiveHandle) {
                // Orphan-by-missing-resume — demote to MissingSource so
                // retryStreamBulkGroupFailedItems below can pick it up.
                // CRITICAL ordering: clear the item's infoHash BEFORE the
                // deleteTorrent call later (deleteTorrent invokes
                // markStreamBulkItemsForTorrent which loops items by
                // infoHash and would otherwise overwrite this MissingSource
                // state with Cancelled).
                orphanedInfoHashes.append(infoHash);
                item["itemState"] = QString::fromLatin1(kStateMissingSource);
                item["lastError"] = QStringLiteral(
                    "Restart recovery: original torrent record was orphaned "
                    "by missing resume data");
                item["infoHash"] = QString();
                item["torrentKey"] = QString();
                item["fileIndex"] = -1;
                item["fileKey"] = QString();
                items.replace(i, item);
                changed = true;
                anyOrphanRecovered = true;
                continue;
            }

            if (live == QLatin1String("error") && hasLiveHandle) {
                // Transient libtorrent error with a valid handle —
                // forceRecheck via the public wrapper clears the error
                // state and emits torrentUpdated for UI listeners.
                forceRecheck(infoHash);
            }
        }

        if (state != QLatin1String(kStatePending)) {
            item["itemState"] = QString::fromLatin1(kStatePending);
            item["lastError"] = QString();
            items.replace(i, item);
            changed = true;
        }
    }

    if (changed) {
        group["items"] = items;
        group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
        m_streamBulkGroups[groupId] = group;
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(groupId);
    }

    // Clean up dead m_records zombie entries for the demoted orphans.
    // Safe even when no libtorrent handle exists (TorrentEngine::removeTorrent
    // early-returns if m_records.find(hash) is end; QFile::remove on a
    // missing .fastresume returns false harmlessly). Done AFTER the items[]
    // commit above so the markStreamBulkItemsForTorrent call inside
    // deleteTorrent doesn't match our just-demoted items (we cleared
    // their infoHash field above).
    for (const QString& orphanHash : orphanedInfoHashes)
        deleteTorrent(orphanHash, /*deleteFiles=*/false);

    if (anyOrphanRecovered) {
        // Orphan-recovery path — trigger source-pick rerun via the
        // existing retry plumbing. retryStreamBulkGroupFailedItems
        // matches MissingSource state (just set above), emits
        // streamBulkRetrySourcePickRequested, StreamPage rebuilds the
        // bulk plan + reruns BulkSourceCollector/buildBulkSelection +
        // dispatches fresh magnets via Phase 5 group path. New torrents
        // land paused via the cohort scheduler; if a cohort head is
        // already Downloading the slot stays occupied, otherwise the
        // first fresh dispatch resumes.
        retryStreamBulkGroupFailedItems(groupId);
    } else {
        // No orphans — non-orphan reset path. After the loop above,
        // every non-Published item is Pending; cohortMaybeAdvance
        // picks the first eligible and resumes it.
        cohortMaybeAdvance(groupId);
    }
}

// TORRENT_PERSISTENCE_COLLAPSE Phase 4.3 (2026-05-20) — reconcileMovieRecordOrphans
// retired. Phase 2.3's pending_engine_add row replay handles the F9 scenario
// deterministically (no 2s heuristic delay needed). Function body + the
// QTimer::singleShot scheduler at the end of the ctor were both removed in
// the same commit.

void TorrentClient::markStreamBulkItemsForTorrent(const QString& infoHash,
                                                  StreamBulkItemState state,
                                                  const QString& lastError)
{
    if (infoHash.isEmpty() || m_streamBulkGroups.isEmpty())
        return;

    bool changed = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList affectedGroups;
    for (auto groupIt = m_streamBulkGroups.begin(); groupIt != m_streamBulkGroups.end(); ++groupIt) {
        QJsonObject group = groupIt.value().toObject();
        QJsonArray items = group.value("items").toArray();
        bool groupChanged = false;
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            if (item.value("infoHash").toString().compare(infoHash, Qt::CaseInsensitive) != 0)
                continue;
            if (isTerminalStreamBulkState(item.value("itemState").toString()))
                continue;
            item["itemState"] = streamBulkItemStateToString(state);
            item["lastError"] = lastError;
            items.replace(i, item);
            groupChanged = true;
        }
        if (groupChanged) {
            group["items"] = items;
            group["updatedAtMs"] = now;
            *groupIt = group;
            affectedGroups.push_back(groupIt.key());
            changed = true;
        }
    }

    if (changed)
        saveStreamBulkGroups();
    // STREAM_BULK_DOWNLOAD_V2 Phase 2 — terminal/failure transitions free
    // the cohort's active slot. Advance each touched group; if the slot is
    // still occupied (e.g. by another item that was Downloading), the
    // method short-circuits naturally.
    for (const QString& groupId : affectedGroups) {
        emit streamBulkGroupsChanged(groupId);
        cohortMaybeAdvance(groupId);
    }
}

void TorrentClient::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    m_streamDownloadIndex = idx;
    // STREAM_BULK_DOWNLOAD_V2 backfill 2026-05-11 — fire one-shot index
    // backfill the moment the pointer arrives from MainWindow. Defensive
    // no-op if idx is null or m_streamBulkGroups is empty.
    backfillStreamDownloadIndex();
}

void TorrentClient::backfillStreamDownloadIndex()
{
    // STREAM_BULK_DOWNLOAD_V2 backfill 2026-05-11 — Hemanth's Daredevil S02
    // E05-E08 finished downloading + published successfully (all 4 items
    // itemState=Published, files on disk at the canonical show folder), but
    // the StreamDetailView Progress/Status columns were empty and clicking
    // an episode showed the source-picker instead of auto-playing the
    // local file. Root cause: those 4 episodes published in a Tankoban
    // session running the pre-hotfix code (line ~2270) that read
    // imdbId/streamSeason/episodeNum from per-item fields the bulk
    // dispatch path never populated, silently skipping the registerEpisode
    // call. The on-publish hotfix landed today but only fires when an item
    // ENTERS the Publishing→Published transition — already-Published items
    // never re-enter that path, so the download index stays empty for them
    // forever absent a recovery mechanism. This backfill is that recovery:
    // a one-shot walk over every Published item in every group, computing
    // canonical path from destinationRoot + destinationKey, verifying the
    // file exists on disk, then calling registerEpisode if not already
    // present in the index. Idempotent — re-running the backfill on a
    // fully-up-to-date index is a no-op except for the cheap filePathFor
    // lookups.
    if (!m_streamDownloadIndex || m_streamBulkGroups.isEmpty())
        return;

    int registered = 0;
    int skippedNoFile = 0;
    int skippedAlreadyIndexed = 0;
    int skippedUnresolved = 0;

    for (auto it = m_streamBulkGroups.constBegin();
         it != m_streamBulkGroups.constEnd(); ++it) {
        const QJsonObject group = it.value().toObject();
        const QJsonObject sourceIds =
            group.value(QStringLiteral("sourceIds")).toObject();
        const QString destinationRoot =
            group.value(QStringLiteral("destinationRoot")).toString();
        if (destinationRoot.isEmpty())
            continue;

        const QJsonArray items = group.value(QStringLiteral("items")).toArray();
        for (const auto& v : items) {
            const QJsonObject item = v.toObject();
            const QString state = item.value(QStringLiteral("itemState")).toString();
            if (state != QLatin1String(kStatePublished)
                && state != QLatin1String(kStateCompleted)) {
                continue;
            }

            // Same fallback chain as the on-publish path at line ~2270:
            // direct per-item fields first, then sourceIds + itemKey-suffix.
            QString imdbId   = item.value(QStringLiteral("imdbId")).toString();
            int seasonNum    = item.value(QStringLiteral("streamSeason")).toInt(0);
            int episodeNum   = item.value(QStringLiteral("episodeNum")).toInt(0);

            if (imdbId.isEmpty())
                imdbId = sourceIds.value(QStringLiteral("seriesId")).toString();
            if (seasonNum <= 0)
                seasonNum = sourceIds.value(QStringLiteral("season")).toInt(0);
            if (episodeNum <= 0) {
                const QString itemKey =
                    item.value(QStringLiteral("itemKey")).toString();
                const int eIdx = itemKey.lastIndexOf(QLatin1Char('E'));
                if (eIdx > 0)
                    episodeNum = itemKey.mid(eIdx + 1).toInt();
            }

            if (imdbId.isEmpty() || seasonNum <= 0 || episodeNum <= 0) {
                ++skippedUnresolved;
                continue;
            }

            // Already indexed — skip without touching the file system.
            if (m_streamDownloadIndex
                    ->filePathFor(imdbId, seasonNum, episodeNum)
                    .has_value()) {
                ++skippedAlreadyIndexed;
                continue;
            }

            const QString destinationKey =
                item.value(QStringLiteral("destinationKey")).toString();
            if (destinationKey.isEmpty()) {
                ++skippedUnresolved;
                continue;
            }
            const QString canonicalPath = QDir::cleanPath(
                QDir(destinationRoot).filePath(destinationKey));

            // Defensive: the file must actually exist before we register —
            // otherwise the index would advertise a ghost entry and
            // StreamDetailView's auto-play would fail-open into a missing
            // local file. Better to leave the item unregistered and let
            // the StreamRescueScanner pick it up via its on-disk walk if
            // the file truly exists somewhere else.
            const QFileInfo fi(canonicalPath);
            if (!fi.exists() || !fi.isFile()) {
                ++skippedNoFile;
                continue;
            }

            m_streamDownloadIndex->registerEpisode(
                imdbId, seasonNum, episodeNum, canonicalPath,
                it.key(), fi.size());
            ++registered;
        }
    }

    if (registered > 0 || skippedNoFile > 0 || skippedUnresolved > 0) {
        qInfo("TorrentClient: backfillStreamDownloadIndex registered=%d "
              "alreadyIndexed=%d skippedNoFile=%d skippedUnresolved=%d",
              registered, skippedAlreadyIndexed, skippedNoFile, skippedUnresolved);
    }
}

void TorrentClient::publishStreamBulkItemsForTorrent(const QString& infoHash)
{
    if (infoHash.isEmpty() || m_streamBulkGroups.isEmpty())
        return;

    bool changed = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList affectedGroups;

    for (auto groupIt = m_streamBulkGroups.begin(); groupIt != m_streamBulkGroups.end(); ++groupIt) {
        QJsonObject group = groupIt.value().toObject();
        const QString stagingPath = group.value("stagingPath").toString();
        QJsonArray items = group.value("items").toArray();
        bool groupChanged = false;

        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            if (item.value("infoHash").toString().compare(infoHash, Qt::CaseInsensitive) != 0)
                continue;
            // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — accept PublishFailed
            // items too so the publish-rename auto-retries on boot via
            // retryStreamBulkPublishing's seedingLike-completed branch. The
            // prior gate only matched Downloading/Pending (per
            // isDownloadingStreamBulkState), so a once-failed item was
            // permanently stuck unless the user clicked Retry failed
            // manually. Combined with the absolute-path fix in
            // relativePublishTarget, PublishFailed items recover automatically
            // on the next Tankoban launch.
            const QString currentItemState = item.value("itemState").toString();
            if (!isDownloadingStreamBulkState(currentItemState) &&
                currentItemState != QLatin1String(kStatePublishFailed))
                continue;

            const QString canonicalPath = canonicalPathForStreamBulkItem(group, item);
            if (canonicalPath.isEmpty() || stagingPath.isEmpty()) {
                item["itemState"] = QString::fromLatin1(kStatePublishFailed);
                item["lastError"] = QStringLiteral("Publish path unavailable");
                items.replace(i, item);
                groupChanged = true;
                continue;
            }

            if (QFileInfo::exists(canonicalPath)) {
                item["itemState"] = QString::fromLatin1(kStatePublishFailed);
                item["lastError"] = QStringLiteral("Canonical destination already exists: %1")
                    .arg(canonicalPath);
                items.replace(i, item);
                groupChanged = true;
                continue;
            }

            QDir().mkpath(QFileInfo(canonicalPath).absolutePath());
            const QString target = relativePublishTarget(stagingPath, canonicalPath);
            if (target.isEmpty()) {
                item["itemState"] = QString::fromLatin1(kStatePublishFailed);
                item["lastError"] = QStringLiteral("Publish target unavailable");
                items.replace(i, item);
                groupChanged = true;
                continue;
            }

            const int fileIndex = item.value("fileIndex").toInt(-1) >= 0
                ? item.value("fileIndex").toInt(-1)
                : 0;
            item["fileIndex"] = fileIndex;
            item["fileKey"] = tankostream::stream::makeFileKey(infoHash, fileIndex);
            item["itemState"] = QString::fromLatin1(kStatePublishing);
            item["lastError"] = QString();
            items.replace(i, item);
            groupChanged = true;

            m_engine->renameFile(infoHash, fileIndex, target);
        }

        if (groupChanged) {
            group["items"] = items;
            group["updatedAtMs"] = now;
            *groupIt = group;
            affectedGroups.push_back(groupIt.key());
            changed = true;
        }
    }

    if (changed)
        saveStreamBulkGroups();
    for (const QString& groupId : affectedGroups) {
        emit streamBulkGroupsChanged(groupId);
        // STREAM_BULK_DOWNLOAD_V2 Phase 2 — Downloading → Publishing
        // transition frees the cohort slot. Advancing here means the
        // next per-episode magnet starts in parallel with the file
        // rename (rename is sub-second + uses no bandwidth).
        cohortMaybeAdvance(groupId);
        maybeEmitStreamBulkGroupPublishComplete(groupId);
    }
}

void TorrentClient::publishTankorentItemsForTorrent(const QString& infoHash)
{
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — defensive double-pass.
    // Today's flow: parser already fired at metadata-ready time (Task 7) registering
    // Pending entries; pieceFinished events (Task 8) flipped them to Complete as files
    // finished. This completion-time call is the safety net: re-parse the file list
    // and register any episodes that DIDN'T parse at metadata-ready time (e.g. a file
    // was renamed mid-download). Idempotent: existing entries are no-op'd by the
    // Index's check-then-mutate logic.
    if (!m_streamDownloadIndex) {
        qWarning() << "publishTankorentItemsForTorrent: no StreamDownloadIndex bound; skipping";
        return;
    }
    const auto row = m_repo.getTorrent(infoHash);
    if (!row) {
        qWarning() << "publishTankorentItemsForTorrent: no record for" << infoHash;
        return;
    }

    const QString imdbId = row->imdbId;
    if (imdbId.isEmpty())
        return;

    const int configSeason = row->season;
    const QString savePath = row->savePath;
    if (savePath.isEmpty()) {
        qWarning() << "publishTankorentItemsForTorrent: empty savePath for" << infoHash;
        return;
    }

    const QJsonArray files = m_engine ? m_engine->torrentFiles(infoHash) : QJsonArray{};
    if (files.isEmpty()) {
        qWarning() << "publishTankorentItemsForTorrent: no files in torrent" << infoHash;
        return;
    }

    const tankostream::stream::ParsedPack pack =
        tankostream::stream::StreamPackParser::parsePack(files, imdbId, configSeason);

    const QString sourceGroupId = QStringLiteral("tankorent:") + infoHash;
    int registeredCount = 0;

    if (pack.type == QStringLiteral("series")) {
        for (const auto& pf : pack.episodes) {
            const QString absPath = QDir(savePath).absoluteFilePath(pf.relName);
            // registerEpisode is the Complete-state path (existing API).
            // Idempotent with Pending entries already in index thanks to canonical-path
            // upsert semantics in StreamDownloadIndex.
            m_streamDownloadIndex->registerEpisode(
                imdbId, pf.season, pf.episode, absPath, sourceGroupId, pf.sizeBytes);
            ++registeredCount;
        }
    } else if (pack.type == QStringLiteral("movie")) {
        const QString absPath = QDir(savePath).absoluteFilePath(pack.movieFile.relName);
        m_streamDownloadIndex->registerMovie(
            imdbId, absPath, sourceGroupId, pack.movieFile.sizeBytes);
        registeredCount = 1;
    }

    qDebug() << "publishTankorentItemsForTorrent:" << infoHash
             << "registered" << registeredCount << "items via StreamPackParser"
             << "as imdbId=" << imdbId;
}

void TorrentClient::retryStreamBulkPublishing()
{
    if (m_streamBulkGroups.isEmpty())
        return;

    QHash<QString, QString> activeStates;
    for (const TorrentInfo& info : listActive())
        activeStates.insert(info.infoHash.toLower(), info.stateString);

    auto seedingLike = [&](const QString& infoHash) {
        const auto row = m_repo.getTorrent(infoHash);
        if (!row)
            return false;
        const QString active = activeStates.value(infoHash.toLower());
        // TorrentState::Completed collapses legacy "completed" + "seeding".
        return row->state == tankoban::torrent::TorrentState::Completed ||
               active == QLatin1String("completed") ||
               active == QLatin1String("seeding");
    };

    QSet<QString> completedHashesNeedingPublish;
    for (auto groupIt = m_streamBulkGroups.begin(); groupIt != m_streamBulkGroups.end(); ++groupIt) {
        const QJsonObject group = groupIt.value().toObject();
        const QString stagingPath = group.value("stagingPath").toString();
        const QJsonArray items = group.value("items").toArray();
        for (const auto& value : items) {
            const QJsonObject item = value.toObject();
            const QString infoHash = item.value("infoHash").toString();
            if (infoHash.isEmpty() || !seedingLike(infoHash))
                continue;

            const QString state = item.value("itemState").toString();
            // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — also queue items in
            // PublishFailed state for the publish-retry path so they auto-
            // recover on boot. The seedingLike(infoHash) gate above already
            // confirms the torrent has the bytes on disk; a prior publish-
            // rename failure (now fixable via the absolute-path rename target
            // in relativePublishTarget) re-attempts here.
            if (isDownloadingStreamBulkState(state) ||
                state == QLatin1String(kStatePublishFailed)) {
                completedHashesNeedingPublish.insert(infoHash);
                continue;
            }

            if (!isPublishingStreamBulkState(state))
                continue;
            const QString canonicalPath = canonicalPathForStreamBulkItem(group, item);
            if (canonicalPath.isEmpty())
                continue;
            if (!canonicalPath.isEmpty() && QFileInfo::exists(canonicalPath))
                continue;
            if (stagingPath.isEmpty())
                continue;
            QDir().mkpath(QFileInfo(canonicalPath).absolutePath());
            const QString target = relativePublishTarget(stagingPath, canonicalPath);
            if (target.isEmpty())
                continue;
            const int fileIndex = item.value("fileIndex").toInt(-1) >= 0
                ? item.value("fileIndex").toInt(-1)
                : 0;
            m_engine->renameFile(infoHash, fileIndex, target);
        }
    }

    for (const QString& infoHash : completedHashesNeedingPublish)
        publishStreamBulkItemsForTorrent(infoHash);
}

// STREAM_BULK_DOWNLOAD_V2 Phase 2 — cohort sequential scheduler. See
// TorrentClient.h for the full contract. Walks one group's items[] in
// stored order (which is episode-numerical because the bulk dispatch
// builds them in that order from BulkPlanResult.items). At most one
// resume per call: idempotent against being fired from multiple state-
// transition hook sites for the same triggering event.
void TorrentClient::cohortMaybeAdvance(const QString& groupId)
{
    if (groupId.isEmpty() || !m_streamBulkGroups.contains(groupId))
        return;

    QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    QJsonArray items = group.value("items").toArray();

    // Slot occupancy: items currently Downloading. Pending+infoHash items
    // are paused-and-queued (cohort hasn't reached them). Publishing /
    // Published / terminal-failure items are slot-free. Pack mode marks
    // every item Downloading at dispatch time, so this method short-
    // circuits naturally for pack-mode groups.
    int downloadingCount = 0;
    for (const auto& v : items) {
        if (v.toObject().value("itemState").toString()
            == QLatin1String(kStateDownloading))
            ++downloadingCount;
    }
    if (downloadingCount > 0)
        return;

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        const QString state = item.value("itemState").toString();
        const QString infoHash = item.value("infoHash").toString();
        if (state != QLatin1String(kStatePending) || infoHash.isEmpty())
            continue;

        // Resume the engine handle and flip the persisted state. The
        // resume call is a libtorrent torrent_handle::resume() which
        // is idempotent if the handle is already running (defensive
        // against races with the user manually un-pausing via the
        // Tankorent UI mid-cohort — per brief §7.g, scheduler does not
        // re-pause user-driven changes).
        resumeTorrent(infoHash);
        item["itemState"] = QString::fromLatin1(kStateDownloading);
        items.replace(i, item);
        group["items"] = items;
        group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
        m_streamBulkGroups[groupId] = group;
        saveStreamBulkGroups();
        return;  // one advance per call; subsequent advances fire from
                 // the next state transition (publish/mark/onFileRenamed)
    }
}

void TorrentClient::cohortMaybeAdvanceAll()
{
    if (m_streamBulkGroups.isEmpty())
        return;
    const QStringList groupIds = m_streamBulkGroups.keys();
    for (const QString& groupId : groupIds)
        cohortMaybeAdvance(groupId);
}

void TorrentClient::maybeEmitStreamBulkGroupPublishComplete(const QString& groupId)
{
    if (groupId.isEmpty() || m_publishCompleteNotified.contains(groupId))
        return;
    if (!m_streamBulkGroups.contains(groupId))
        return;
    if (!streamBulkGroupAllTerminal(m_streamBulkGroups.value(groupId).toObject()))
        return;

    m_publishCompleteNotified.insert(groupId);
    emit groupPublishComplete(groupId);
}

// Collapse duplicate completion entries per infoHash to a single row (the
// earliest completedAt). Keeps the history semantically a "first-completion
// log" rather than a per-boot alert log. Idempotent — a run against an
// already-compacted file writes nothing. Called once at boot, after records
// + orphan sweep are settled.
//
// Pre-Phase 1 Batch 1.1, onTorrentFinished had no re-fire guard, so every
// app startup appended a duplicate entry for every already-completed
// torrent. Hemanth's file hit 135 entries for a single EMBER Vinland Saga
// completion. This one-shot tidies that debt.
void TorrentClient::compactHistory()
{
    auto data = m_bridge->store().read(HISTORY_FILE);
    auto arr = data.value("entries").toArray();
    if (arr.size() < 2) return;

    QHash<QString, QJsonObject> earliest;
    QStringList order;   // preserves first-seen hash order = chronological
    for (const auto& v : arr) {
        const QJsonObject e = v.toObject();
        const QString h = e.value("infoHash").toString();
        if (h.isEmpty()) continue;
        const qint64 t = e.value("completedAt").toVariant().toLongLong();
        auto it = earliest.find(h);
        if (it == earliest.end()) {
            earliest.insert(h, e);
            order << h;
        } else if (t < it.value().value("completedAt").toVariant().toLongLong()) {
            *it = e;
        }
    }

    if (earliest.size() == arr.size()) return;   // already compact

    QJsonArray compacted;
    for (const QString& h : order)
        compacted.append(earliest.value(h));

    QJsonObject out;
    out["entries"] = compacted;
    m_bridge->store().write(HISTORY_FILE, out);
    qDebug() << "TorrentClient: compacted history from" << arr.size()
             << "to" << compacted.size() << "entries";
}

void TorrentClient::appendHistory(const TorrentInfo& info)
{
    auto data = m_bridge->store().read(HISTORY_FILE);
    auto arr = data.value("entries").toArray();

    QJsonObject entry;
    entry["infoHash"]    = info.infoHash;
    entry["name"]        = info.name;
    entry["category"]    = info.category;
    entry["savePath"]    = info.savePath;
    entry["completedAt"] = QDateTime::currentMSecsSinceEpoch();
    entry["totalWanted"] = info.totalWanted;
    arr.append(entry);

    data["entries"] = arr;
    m_bridge->store().write(HISTORY_FILE, data);
}

// ── Info hash extraction from magnet URI ────────────────────────────────────
QString TorrentClient::extractInfoHash(const QString& magnetUri) const
{
    // Match v1 SHA-1 (40 hex), v2 SHA-256 (64 hex), or base32-encoded (32 chars)
    static QRegularExpression re(QStringLiteral(
        "btih:([a-fA-F0-9]{40}(?:[a-fA-F0-9]{24})?|[A-Z2-7]{32})"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = re.match(magnetUri);
    if (match.hasMatch()) {
        QString captured = match.captured(1);
        if (captured.length() == 32) {
            // Base32-encoded hash — decode to hex (Qt6 has no fromBase32)
            static const QByteArray b32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
            QByteArray input = captured.toUpper().toUtf8();
            QByteArray decoded;
            int buffer = 0, bitsLeft = 0;
            for (char c : input) {
                int val = b32.indexOf(c);
                if (val < 0) continue;
                buffer = (buffer << 5) | val;
                bitsLeft += 5;
                if (bitsLeft >= 8) {
                    bitsLeft -= 8;
                    decoded.append(static_cast<char>((buffer >> bitsLeft) & 0xFF));
                }
            }
            return decoded.toHex().toLower();
        }
        return captured.toLower();
    }
    return {};
}

// ── Dedup check ─────────────────────────────────────────────────────────────
bool TorrentClient::isDuplicate(const QString& magnetUri) const
{
    QString hash = extractInfoHash(magnetUri);
    if (hash.isEmpty()) return false;
    return m_repo.hasTorrent(hash);
}

// ── Add flow ────────────────────────────────────────────────────────────────
QString TorrentClient::resolveMetadata(const QString& magnetUri)
{
    QString tempPath = m_bridge->dataDir() + "/torrent_cache/resolve_tmp";
    QString hash = m_engine->addMagnet(magnetUri, tempPath, /*paused=*/true);

    if (hash.isEmpty()) return {};

    // Do NOT persist to m_records yet — the torrent is a draft until the user
    // confirms via startDownload(). This prevents ghost entries if the app
    // crashes while the dialog is open, and keeps the transfers tab clean.
    return hash;
}

QString TorrentClient::addMagnetHeadless(const QString& magnetUri,
                                         const QString& category,
                                         const QString& destinationPath)
{
    // v1.5 Phase D.3 (2026-05-19) — dialog-free magnet add for dev-bridge use.
    if (isDuplicate(magnetUri))
        return {};
    const QString hash = resolveMetadata(magnetUri);
    if (hash.isEmpty())
        return {};
    const QMap<QString, QString> paths = defaultPaths();
    AddTorrentConfig cfg;
    cfg.category        = category.isEmpty() ? QStringLiteral("videos") : category;
    cfg.destinationPath = destinationPath.isEmpty()
        ? paths.value(cfg.category, paths.value(QString()))
        : destinationPath;
    cfg.contentLayout   = QStringLiteral("original");
    cfg.sequential      = false;
    cfg.startPaused     = false;
    startDownload(hash, cfg);
    return hash;
}

void TorrentClient::startDownload(const QString& infoHash, const AddTorrentConfig& config)
{
    const QString hash = infoHash.toLower();

    // TORRENT_PERSISTENCE_COLLAPSE Phase 4.4 (2026-05-20) — the F9 fix
    // 2026-05-19 self-defense narrative is retired. Phase 2.2's
    // pending_engine_add row write below (BEFORE engine.addMagnet) makes
    // the contract structurally correct: every dispatch either lands a
    // durable repo row that gets re-attempted on next boot via the Phase
    // 2.3 replay, OR fails fast with a clear error transition. The
    // magnet-empty branch below is now just an input bounds check — calling
    // engine.addMagnet("") would fail anyway, the early return saves a
    // useless repo write.
    if (!m_engine->hasTorrent(hash)) {
        if (config.magnetUri.isEmpty()) {
            qWarning() << "TorrentClient::startDownload: engine has no torrent for"
                       << hash.left(16) << "and config.magnetUri is empty —"
                       << "aborting (no magnet to dispatch)."
                       << "imdbId=" << config.imdbId << "season=" << config.season;
            return;
        }

        auto pendingRow = torrentRowFromStartConfig(
            hash, config, tankoban::torrent::TorrentState::PendingEngineAdd);
        if (!m_repo.upsertTorrent(pendingRow)) {
            qWarning() << "TorrentClient::startDownload: failed to persist"
                       << "pending_engine_add row for" << hash.left(16);
        }

        const QString tempPath = m_bridge->dataDir() + "/torrent_cache/resolve_tmp";
        const QString addedHash = m_engine->addMagnet(config.magnetUri, tempPath, /*paused=*/true);
        if (addedHash.isEmpty()) {
            m_repo.updateTorrentState(
                hash,
                tankoban::torrent::TorrentState::Error,
                QStringLiteral("engine.addMagnet failed"));
            qWarning() << "TorrentClient::startDownload: addMagnet failed for"
                       << hash.left(16) << "— aborting without writing a zombie record.";
            return;
        }
        if (addedHash.compare(hash, Qt::CaseInsensitive) != 0) {
            m_repo.updateTorrentState(
                hash,
                tankoban::torrent::TorrentState::Error,
                QStringLiteral("engine.addMagnet returned mismatched hash"));
            qWarning() << "TorrentClient::startDownload: addMagnet returned hash"
                       << addedHash.left(16) << "but caller expected"
                       << hash.left(16) << "— aborting (hash mismatch could indicate"
                       << "magnet/infoHash desync from indexer).";
            m_engine->removeTorrent(addedHash, /*deleteFiles=*/false);
            return;
        }
        m_repo.updateTorrentState(hash, tankoban::torrent::TorrentState::Active);
    } else if (!m_repo.getTorrent(hash).has_value()) {
        m_repo.upsertTorrent(torrentRowFromStartConfig(
            hash, config, tankoban::torrent::TorrentState::Active));
    }

    // Apply file priorities
    if (!config.filePriorities.isEmpty()) {
        int maxIdx = 0;
        for (auto it = config.filePriorities.begin(); it != config.filePriorities.end(); ++it)
            maxIdx = qMax(maxIdx, it.key());

        QVector<int> priorities(maxIdx + 1, 0);
        for (auto it = config.filePriorities.begin(); it != config.filePriorities.end(); ++it)
            priorities[it.key()] = it.value();

        m_engine->setFilePriorities(hash, priorities);
    }

    // Sequential download
    if (config.sequential)
        m_engine->setSequentialDownload(hash, true);

    // Content layout: strip root folder if "no_subfolder"
    if (config.contentLayout == QLatin1String("no_subfolder"))
        m_engine->flattenFiles(hash);

    // Create the record only now — user has confirmed the download
    QJsonObject rec;
    rec["name"]            = QString();
    rec["state"]           = config.startPaused ? QStringLiteral("paused") : QStringLiteral("downloading");
    rec["addedAt"]         = QDateTime::currentMSecsSinceEpoch();
    rec["category"]        = config.category;
    rec["savePath"]        = config.destinationPath;
    rec["contentLayout"]   = config.contentLayout;
    rec["streamGroupId"]   = config.streamGroupId;
    rec["sequential"]      = config.sequential;
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: identity capture for single-add
    // Tankorent downloads originating from the show-first picker. Empty/0 for
    // non-Cinemeta downloads (repurposed direct torrent search).
    rec["imdbId"]          = config.imdbId;
    rec["season"]          = config.season;
    m_records[hash]        = rec;
    saveRecords();

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — same-session re-dispatch:
    // libtorrent's metadata_received_alert is single-shot per handle. On a
    // duplicate_torrent re-add (cancelled pack + re-dispatch same session),
    // libtorrent reuses the existing handle and will NOT re-fire the alert, so
    // onMetadataReady would never run for this new m_records entry. Detect
    // metadata-already-present and synthesize the call via QTimer::singleShot(0)
    // so it fires on the next event-loop tick — after the current startDownload
    // stack frame has returned and m_records is fully committed.
    if (!config.imdbId.isEmpty() && config.streamGroupId.isEmpty()
            && m_engine->hasMetadata(hash)) {
        const QString capturedHash = hash;
        QTimer::singleShot(0, this, [this, capturedHash]() {
            const QJsonArray files = m_engine->torrentFiles(capturedHash);
            onMetadataReady(capturedHash, QString(), 0, files);
        });
    }

    JsonlEventLog::instance().emitEvent(
        QStringLiteral("torrent.added"),
        QStringLiteral("start_download"),
        QJsonObject{{QStringLiteral("hash"), hash},
                    {QStringLiteral("imdbId"), config.imdbId},
                    {QStringLiteral("season"), config.season},
                    {QStringLiteral("streamGroupId"), config.streamGroupId},
                    {QStringLiteral("savePath"), config.destinationPath}});

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — always call startTorrent
    // regardless of startPaused, then pause immediately if requested. The
    // prior `if (!startPaused) startTorrent` shape skipped the storage-move
    // and upload_mode-clear when startPaused=true, leaving the torrent
    // stuck at resolve_tmp/ in upload_mode (libtorrent state that does not
    // meaningfully download). Per-episode bulk dispatch (cohort scheduler's
    // Pending items) hit this trap: items were paused-at-our-layer but
    // actively-running-in-libtorrent at the wrong save path with
    // upload_mode set. Symptoms: barely any peers, sub-KB/s download
    // speeds, ETA in the thousands of hours. startTorrent does the right
    // setup (move from resolve_tmp → destination + unset upload_mode +
    // resume); pauseTorrent then immediately suspends in the correct state.
    m_engine->startTorrent(hash, config.destinationPath);
    if (config.startPaused)
        m_engine->pauseTorrent(hash);

    emit torrentAdded(hash);
}

void TorrentClient::moveStorage(const QString& infoHash, const QString& newSavePath)
{
    if (newSavePath.isEmpty()) return;
    if (!m_repo.hasTorrent(infoHash)) return;

    QJsonObject rec = m_records[infoHash].toObject();
    const QString oldSavePath = rec.value("savePath").toString();
    const QString category    = rec.value("category").toString();

    // Same path = no-op (avoid spurious storage_moved_alert + rescan churn).
    if (QDir(oldSavePath).absolutePath().compare(
            QDir(newSavePath).absolutePath(), Qt::CaseInsensitive) == 0) {
        return;
    }

    // Ensure destination exists. libtorrent will create the torrent's own
    // subfolder, but the parent must exist or move_storage fails immediately.
    QDir().mkpath(newSavePath);

    // Optimistic record update — mirrors startTorrent's pattern. If
    // libtorrent ultimately reports failure, onStorageMoveFailed reverts
    // the record below.
    rec["savePath"] = newSavePath;
    m_records[infoHash] = rec;
    saveRecords();

    m_engine->moveStorage(infoHash, newSavePath);
    emit torrentUpdated(infoHash);

    // Notify rescan on both old and new category roots so library views
    // refresh — old folder may now be empty, new folder will be filling.
    // The actual rescan fires after libtorrent's move completes (in
    // onStorageMoved); this immediate emit covers any UI that just wants
    // the row to redraw with the new savePath.
    if (m_bridge && !category.isEmpty()) {
        m_bridge->notifyRootFoldersChanged(category);
    }
}

float TorrentClient::downloadProgress(const QString& folderPath) const
{
    if (folderPath.isEmpty()) return 0.0f;
    const QString normFolder = QDir(folderPath).absolutePath();

    qint64 totalSize = 0;
    double totalDone = 0.0;
    for (const auto& info : listActive()) {
        if (info.savePath.isEmpty() || info.totalWanted <= 0)
            continue;
        const QString normSave = QDir(info.savePath).absolutePath();
        if (!normSave.startsWith(normFolder, Qt::CaseInsensitive))
            continue;
        totalSize += info.totalWanted;
        totalDone += static_cast<double>(info.totalWanted) * info.progress;
    }
    if (totalSize <= 0) return 0.0f;
    return static_cast<float>(totalDone / static_cast<double>(totalSize));
}

// ── Query ───────────────────────────────────────────────────────────────────
QPair<QString, int> TorrentClient::streamMovieDownloadSnapshot(const QString& imdbId) const
{
    // TORRENT_PERSISTENCE_COLLAPSE Phase 3.2 (2026-05-20) — second UI projection
    // cutover. Stream detail view's movie download chip reads its source-of-
    // truth from the SQLite repository (Phase 1 importer pre-populated; Phase 2
    // mirroring keeps in sync) and overlays live engine status on top, picking
    // the most-recently-added active candidate.
    //
    // The contract this preserves verbatim from the legacy m_records merge:
    //   - imdb match + season=0 + streamGroupId empty -> "this is a direct
    //     Theatre movie download row, not a bulk-cohort row"
    //   - Engine status (when handle present) is authoritative for stateString
    //     and progress; otherwise repo state + 1.0/0.0 progress fallback.
    //   - Terminal-success / error states (completed / seeding / error) are
    //     filtered out — the chip only shows in-progress activity.
    //   - Max-addedAt wins among multiple candidates so a re-add supersedes a
    //     stale earlier row.
    //
    // listTorrentsByImdb already applies the (imdb_id == :imdb AND season == :s)
    // SQL filter, so this loop only does the cross-row predicates that don't
    // belong in the query API.
    if (imdbId.isEmpty())
        return {};

    QMap<QString, TorrentStatus> statusMap;
    for (const auto& s : m_engine->allStatuses())
        statusMap[s.infoHash] = s;

    QString bestState;
    int bestPct = 0;
    qint64 bestAddedAt = -1;

    const auto rows = m_repo.listTorrentsByImdb(imdbId, 0);
    for (const auto& row : rows) {
        if (!row.streamGroupId.isEmpty())
            continue;
        if (row.state == tankoban::torrent::TorrentState::Removed
            || row.state == tankoban::torrent::TorrentState::RemovePending) {
            continue;
        }

        QString state = tankoban::torrent::torrentStateToString(row.state);
        float progress = (row.state == tankoban::torrent::TorrentState::Completed)
                             ? 1.0f
                             : 0.0f;
        const auto stIt = statusMap.constFind(row.hash);
        if (stIt != statusMap.constEnd()) {
            const TorrentStatus& st = stIt.value();
            state = st.stateString;
            progress = st.progress;
        }

        const QString normalized = state.toLower();
        if (normalized == QLatin1String("completed")
            || normalized == QLatin1String("seeding")
            || normalized == QLatin1String("error")) {
            continue;
        }

        const qint64 addedAt =
            row.addedAt.isValid() ? row.addedAt.toMSecsSinceEpoch() : 0;
        if (addedAt < bestAddedAt)
            continue;

        bestAddedAt = addedAt;
        bestState = state.isEmpty() ? QStringLiteral("downloading") : state;
        bestPct = static_cast<int>(qBound(0.0f, progress, 1.0f) * 100.0f);
    }

    if (bestState.isEmpty())
        return {};
    return qMakePair(bestState, bestPct);
}

bool TorrentClient::streamMovieIsLegacyNoMagnet(const QString& imdbId) const
{
    // Phase 4.2 — sibling query to streamMovieDownloadSnapshot. Same imdb /
    // season=0 / empty-streamGroupId filter; returns true iff any matching
    // row carries legacy_no_magnet=1. Cheap repo read; called once per
    // refreshMovieDownloadState pass.
    if (imdbId.isEmpty()) return false;
    const auto rows = m_repo.listTorrentsByImdb(imdbId, 0);
    for (const auto& row : rows) {
        if (!row.streamGroupId.isEmpty()) continue;
        if (row.state == tankoban::torrent::TorrentState::Removed
            || row.state == tankoban::torrent::TorrentState::RemovePending) {
            continue;
        }
        if (row.legacyNoMagnet) return true;
    }
    return false;
}

QList<TorrentInfo> TorrentClient::listActive() const
{
    // TORRENT_PERSISTENCE_COLLAPSE Phase 3.1 (2026-05-20) — first UI projection
    // cutover. The durable existence-truth now comes from the SQLite repository
    // (Phase 1 importer pre-populated it, Phase 2 mirroring keeps it in sync).
    // The engine still owns the LIVE runtime fields (progress / speed / peers /
    // seeds / queue position) — we overlay those when the engine has a handle
    // for the hash, exactly as the legacy m_records merge did. Rows past their
    // Removed terminal are skipped so the Transfers list doesn't render
    // tombstones.
    //
    // Phase 1 invariant honoured: m_records still exists in memory and is
    // still written to by the legacy path; it is just no longer read here.
    // Phase 4 deletes m_records + saveRecords entirely.
    QList<TorrentInfo> result;

    QMap<QString, TorrentStatus> statusMap;
    for (const auto& s : m_engine->allStatuses())
        statusMap[s.infoHash] = s;

    const auto rows = m_repo.listTorrents();
    result.reserve(static_cast<int>(rows.size()));
    for (const auto& row : rows) {
        if (row.state == tankoban::torrent::TorrentState::Removed)
            continue;

        TorrentInfo info;
        info.infoHash      = row.hash;
        info.name          = row.name;
        info.savePath      = row.savePath;
        info.category      = row.category;
        info.addedAt       = row.addedAt.isValid()
                                 ? row.addedAt.toMSecsSinceEpoch()
                                 : 0;
        info.streamGroupId = row.streamGroupId;
        info.sequential    = row.sequential;
        info.errorMessage  = row.errorMessage;
        info.legacyNoMagnet = row.legacyNoMagnet;  // Phase 4.1 — surfaces in Tankorent transfers row "Needs re-add"

        const auto stIt = statusMap.constFind(row.hash);
        if (stIt != statusMap.constEnd()) {
            const TorrentStatus& st = stIt.value();
            // Engine is authoritative for live runtime — keep its stateString
            // (which carries "downloading"/"seeding"/"checking"/"metadata"
            // distinctions the repo's coarser TorrentState enum collapses
            // into "active").
            info.stateString   = st.stateString;
            info.progress      = st.progress;
            info.dlSpeed       = st.downloadRate;
            info.ulSpeed       = st.uploadRate;
            info.peers         = st.numPeers;
            info.seeds         = st.numSeeds;
            info.totalDone     = st.totalDone;
            info.totalWanted   = st.totalWanted;
            info.forceStarted  = st.forceStarted;
            info.queuePosition = st.queuePosition;
            info.dlLimit       = st.dlLimit;
            info.ulLimit       = st.ulLimit;
            if (info.name.isEmpty())
                info.name = st.name;
        } else {
            // No engine handle: row is paused / errored / pending-engine-add.
            // Fall back to the repo's persisted state, mapped to the legacy
            // string vocabulary every existing UI consumer already understands.
            info.stateString = tankoban::torrent::torrentStateToString(row.state);
        }

        result.append(info);
    }

    return result;
}

QSet<QString> TorrentClient::activeInfoHashes() const
{
    QSet<QString> result;
    if (!m_engine)
        return result;
    for (const TorrentInfo& info : listActive())
        result.insert(info.infoHash);
    return result;
}

QJsonArray TorrentClient::devTorrentsSnapshot(bool activeOnly) const
{
    QMap<QString, TorrentStatus> statusMap;
    for (const TorrentStatus& s : m_engine->allStatuses())
        statusMap.insert(s.infoHash, s);

    // Preserve dev-bridge JSON shape across the P5.3.1 cutover: OLD m_records
    // only ever wrote 4 strings ("downloading" / "paused" / "completed" /
    // "error"). torrentStateToString emits the NEW 8-string vocabulary (active /
    // pending_engine_add / etc) which would break downstream consumers
    // (`tankoctl get-torrents`, Agent 0/7 readers). This lambda maps the new
    // enum back to the old strings byte-for-byte.
    auto legacyStateString = [](tankoban::torrent::TorrentState s) -> QString {
        using tankoban::torrent::TorrentState;
        switch (s) {
            case TorrentState::Active:           return QStringLiteral("downloading");
            case TorrentState::Paused:           return QStringLiteral("paused");
            case TorrentState::Completed:        return QStringLiteral("completed");
            case TorrentState::Error:            return QStringLiteral("error");
            case TorrentState::PendingEngineAdd: return QStringLiteral("downloading");
            case TorrentState::MetadataProbe:    return QStringLiteral("downloading");
            case TorrentState::RemovePending:    return QStringLiteral("removed");
            case TorrentState::Removed:          return QStringLiteral("removed");
        }
        return QStringLiteral("downloading");
    };

    QJsonArray out;
    for (const auto& row : m_repo.listTorrents()) {
        const QString hash = row.hash;
        const auto statusIt = statusMap.constFind(hash);
        const bool hasLiveStatus = statusIt != statusMap.constEnd();
        const QString state = hasLiveStatus
            ? statusIt->stateString
            : legacyStateString(row.state);

        const bool terminal =
            state == QLatin1String("completed") || state == QLatin1String("seeding");
        if (activeOnly && (!hasLiveStatus || terminal))
            continue;

        QJsonObject o;
        o[QStringLiteral("hash")] = hash;
        o[QStringLiteral("name")] = row.name;
        o[QStringLiteral("state")] = state;
        o[QStringLiteral("imdbId")] = row.imdbId;
        o[QStringLiteral("season")] = row.season;
        o[QStringLiteral("streamGroupId")] = row.streamGroupId;
        o[QStringLiteral("savePath")] = row.savePath;

        if (hasLiveStatus) {
            const TorrentStatus& s = statusIt.value();
            o[QStringLiteral("name")] =
                o.value(QStringLiteral("name")).toString().isEmpty()
                    ? s.name
                    : o.value(QStringLiteral("name")).toString();
            o[QStringLiteral("progressPct")] =
                static_cast<int>(qBound(0.0f, s.progress, 1.0f) * 100.0f);
            o[QStringLiteral("downRate")] = s.downloadRate;
            o[QStringLiteral("upRate")] = s.uploadRate;
            o[QStringLiteral("peers")] = s.numPeers;
        } else {
            o[QStringLiteral("progressPct")] = 0;
            o[QStringLiteral("downRate")] = 0;
            o[QStringLiteral("upRate")] = 0;
            o[QStringLiteral("peers")] = 0;
        }
        out.append(o);
    }
    return out;
}

QJsonArray TorrentClient::devBulkGroupsSnapshot() const
{
    static const QRegularExpression episodeRe(QStringLiteral("[Ee](\\d{1,3})"));

    QJsonArray groups;
    for (auto it = m_streamBulkGroups.constBegin();
         it != m_streamBulkGroups.constEnd(); ++it) {
        const QJsonObject src = it.value().toObject();
        const QJsonObject sourceIds = src.value(QStringLiteral("sourceIds")).toObject();

        QJsonObject group;
        group[QStringLiteral("groupId")] =
            src.value(QStringLiteral("groupId")).toString(it.key());
        group[QStringLiteral("imdb")] = sourceIds.value(QStringLiteral("seriesId")).toString();
        group[QStringLiteral("kind")] =
            src.value(QStringLiteral("groupKind")).toString(QStringLiteral("streamSeason"));
        group[QStringLiteral("destinationRoot")] =
            src.value(QStringLiteral("destinationRoot")).toString();

        QJsonArray items;
        const QJsonArray srcItems = src.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& value : srcItems) {
            const QJsonObject srcItem = value.toObject();
            const QString itemKey = srcItem.value(QStringLiteral("itemKey")).toString();
            QJsonObject item;
            item[QStringLiteral("itemKey")] = itemKey;
            const QRegularExpressionMatch m = episodeRe.match(itemKey);
            item[QStringLiteral("episode")] = m.hasMatch() ? m.captured(1).toInt() : 0;
            item[QStringLiteral("state")] =
                srcItem.value(QStringLiteral("itemState")).toString();
            item[QStringLiteral("infoHash")] =
                srcItem.value(QStringLiteral("infoHash")).toString();
            item[QStringLiteral("canonicalFilename")] =
                srcItem.value(QStringLiteral("canonicalFilename")).toString();
            items.append(item);
        }
        group[QStringLiteral("items")] = items;
        groups.append(group);
    }
    return groups;
}

QJsonArray TorrentClient::listHistory() const
{
    auto data = m_bridge->store().read(HISTORY_FILE);
    return data.value("entries").toArray();
}

// ── Control ─────────────────────────────────────────────────────────────────
void TorrentClient::pauseTorrent(const QString& infoHash)
{
    m_engine->pauseTorrent(infoHash);
    if (m_repo.hasTorrent(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("paused");
        m_records[infoHash] = rec;
        saveRecords();
    }
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("torrent.state_changed"),
        QStringLiteral("paused"),
        QJsonObject{{QStringLiteral("hash"), infoHash}});
    emit torrentUpdated(infoHash);
}

void TorrentClient::resumeTorrent(const QString& infoHash)
{
    m_engine->resumeTorrent(infoHash);
    if (m_repo.hasTorrent(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("downloading");
        rec.remove("errorMessage");
        m_records[infoHash] = rec;
        saveRecords();
    }
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("torrent.state_changed"),
        QStringLiteral("downloading"),
        QJsonObject{{QStringLiteral("hash"), infoHash}});
    emit torrentUpdated(infoHash);
}

void TorrentClient::deleteTorrent(const QString& infoHash, bool deleteFiles)
{
    const auto row = m_repo.getTorrent(infoHash);
    const bool hadRecord = row.has_value();
    m_engine->removeTorrent(infoHash, deleteFiles);
    m_records.remove(infoHash);
    if (hadRecord) {
        markStreamBulkItemsForTorrent(
            infoHash,
            StreamBulkItemState::Cancelled,
            QStringLiteral("Torrent removed from Tankorent"));
    }
    saveRecords();
    JsonlEventLog::instance().emitEvent(
        QStringLiteral("torrent.removed"),
        QStringLiteral("delete_torrent"),
        QJsonObject{{QStringLiteral("hash"), infoHash},
                    {QStringLiteral("deleteFiles"), deleteFiles},
                    {QStringLiteral("imdbId"), row ? row->imdbId : QString()},
                    {QStringLiteral("streamGroupId"), row ? row->streamGroupId : QString()}});
    emit torrentRemoved(infoHash);
}

bool TorrentClient::releaseFolder(const QString& folderPath)
{
    if (folderPath.isEmpty()) return false;
    const QString target = QDir(folderPath).absolutePath();

    QString matchedHash;
    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        const QJsonObject rec = it.value().toObject();
        const QString savePath = rec.value("savePath").toString();
        const QString name     = rec.value("name").toString();
        if (savePath.isEmpty() || name.isEmpty())
            continue;
        const QString folder =
            QDir(savePath + QLatin1Char('/') + name).absolutePath();
        if (folder.compare(target, Qt::CaseInsensitive) == 0) {
            matchedHash = it.key();
            break;
        }
    }

    if (matchedHash.isEmpty()) return false;

    qDebug() << "TorrentClient: releasing torrent" << matchedHash
             << "from folder rename of" << target
             << "(files preserved; libtorrent record + .fastresume dropped)";
    deleteTorrent(matchedHash, /*deleteFiles=*/false);
    return true;
}

// ── Force operations ─────────────────────────────────────────────────────────
void TorrentClient::forceStart(const QString& infoHash)
{
    m_engine->forceStart(infoHash);
    if (m_repo.hasTorrent(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("downloading");
        rec.remove("errorMessage");
        m_records[infoHash] = rec;
        saveRecords();
    }
    emit torrentUpdated(infoHash);
}

void TorrentClient::clearForceStart(const QString& infoHash)
{
    m_engine->resumeTorrent(infoHash);  // re-enables auto_managed
    emit torrentUpdated(infoHash);
}

void TorrentClient::forceRecheck(const QString& infoHash)
{
    m_engine->forceRecheck(infoHash);
    emit torrentUpdated(infoHash);
}

void TorrentClient::forceReannounce(const QString& infoHash)
{
    m_engine->forceReannounce(infoHash);
}

// ── Queue ────────────────────────────────────────────────────────────────────
void TorrentClient::queuePositionUp(const QString& infoHash)
{
    m_engine->queuePositionUp(infoHash);
}

void TorrentClient::queuePositionDown(const QString& infoHash)
{
    m_engine->queuePositionDown(infoHash);
}

void TorrentClient::setQueueLimits(int maxDownloads, int maxUploads, int maxActive)
{
    m_engine->setQueueLimits(maxDownloads, maxUploads, maxActive);
}

// ── Speed limits ─────────────────────────────────────────────────────────────
void TorrentClient::setSpeedLimits(const QString& infoHash, int dlLimitBps, int ulLimitBps)
{
    m_engine->setSpeedLimits(infoHash, dlLimitBps, ulLimitBps);
}

void TorrentClient::setGlobalSpeedLimits(int dlLimitBps, int ulLimitBps)
{
    m_engine->setGlobalSpeedLimits(dlLimitBps, ulLimitBps);
}

// ── Seeding rules ────────────────────────────────────────────────────────────
void TorrentClient::setSeedingRules(const QString& infoHash, float ratioLimit, int seedTimeSecs)
{
    m_engine->setSeedingRules(infoHash, ratioLimit, seedTimeSecs);
}

void TorrentClient::setGlobalSeedingRules(float ratioLimit, int seedTimeSecs)
{
    m_engine->setGlobalSeedingRules(ratioLimit, seedTimeSecs);
}

// ── Default paths ───────────────────────────────────────────────────────────
QMap<QString, QString> TorrentClient::defaultPaths() const
{
    QMap<QString, QString> paths;
    auto addFirst = [&](const QString& domain) {
        QStringList roots = m_bridge->rootFolders(domain);
        paths[domain] = roots.isEmpty() ? QString() : roots.first();
    };
    addFirst(QStringLiteral("comics"));
    addFirst(QStringLiteral("books"));
    addFirst(QStringLiteral("audiobooks"));
    addFirst(QStringLiteral("videos"));
    return paths;
}

// ── Signal handlers ─────────────────────────────────────────────────────────
void TorrentClient::onMetadataReady(const QString& infoHash, const QString& name,
                                     qint64 /*totalSize*/, const QJsonArray& files)
{
    if (m_repo.hasTorrent(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["name"] = name;
        // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — state write REMOVED
        // here. Previously this overwrote rec["state"] with "metadata_ready"
        // regardless of the existing state. After my 2026-05-11 startTorrent
        // + upload_mode hotfix, startDownload writes state explicitly to
        // "paused" or "downloading" per config.startPaused — BEFORE libtorrent
        // fires the metadata_received_alert (seconds later for fresh magnets).
        // The late-firing alert routed here clobbered the persisted "paused"
        // state with "metadata_ready", breaking the cohort scheduler on the
        // NEXT boot: addFromResume at line 380 below reads state="paused" to
        // pick its shouldPause flag, so a state of "metadata_ready" causes
        // ALL cohort items to re-add UNPAUSED in parallel — not sequential.
        // Hemanth reported "not sequentially downloading" 2026-05-11 — that
        // was this defect. Name update preserved (load-bearing for UI
        // display + history persistence). Records are created exclusively by
        // startDownload (line 1707), so onMetadataReady only ever updates
        // an already-stated record — the prior overwrite was net-negative.
        m_records[infoHash] = rec;
        saveRecords();
        m_repo.updateTorrentName(infoHash, name);
    }
    emit torrentUpdated(infoHash);

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — register parsed episodes as
    // Pending at metadata-ready time. Only fires for Tankorent-source torrents
    // (those with imdbId in the record). Bulk-cohort torrents take the
    // streamBulkGroups path; addon-only torrents have no imdbId binding.
    if (!m_streamDownloadIndex)
        return;
    const auto row = m_repo.getTorrent(infoHash);
    if (!row)
        return;

    const QString imdbId = row->imdbId;
    if (imdbId.isEmpty())
        return;  // not a Tankorent-source torrent

    const QString streamGroupId = row->streamGroupId;
    if (!streamGroupId.isEmpty())
        return;  // bulk-cohort path handles its own registration

    const int configSeason = row->season;
    const QString savePath = row->savePath;
    if (savePath.isEmpty())
        return;

    if (files.isEmpty())
        return;

    const tankostream::stream::ParsedPack pack =
        tankostream::stream::StreamPackParser::parsePack(files, imdbId, configSeason);

    const QString sourceGroupId = QStringLiteral("tankorent:") + infoHash;

    if (pack.type == QStringLiteral("series")) {
        for (const auto& pf : pack.episodes) {
            const QString absPath = QDir(savePath).absoluteFilePath(pf.relName);
            m_streamDownloadIndex->registerPendingEpisode(
                imdbId, pf.season, pf.episode, absPath, sourceGroupId, pf.sizeBytes);
        }
        qDebug() << "onMetadataReady:" << infoHash
                 << "registered" << pack.episodes.size() << "Pending episodes for" << imdbId;
    } else if (pack.type == QStringLiteral("movie")) {
        const QString absPath = QDir(savePath).absoluteFilePath(pack.movieFile.relName);
        m_streamDownloadIndex->registerPendingMovie(
            imdbId, absPath, sourceGroupId, pack.movieFile.sizeBytes);
        qDebug() << "onMetadataReady:" << infoHash
                 << "registered Pending movie for" << imdbId;
    }
}

void TorrentClient::onPieceFinished(const QString& infoHash, int /*pieceIndex*/)
{
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — compute per-file progress for
    // every file in this torrent and push updates into StreamDownloadIndex.
    // Coarse but correct: query libtorrent's per-file progress array on every
    // piece event. libtorrent's file_progress is O(files), so this is cheap.
    if (!m_streamDownloadIndex || !m_engine)
        return;
    const auto row = m_repo.getTorrent(infoHash);
    if (!row)
        return;

    const QString imdbId = row->imdbId;
    if (imdbId.isEmpty())
        return;  // not a Tankorent-source torrent

    const QString streamGroupId = row->streamGroupId;
    if (!streamGroupId.isEmpty())
        return;  // bulk-cohort path handles its own progress tracking

    const int configSeason = row->season;
    const QJsonArray files = m_engine->torrentFiles(infoHash);
    if (files.isEmpty())
        return;

    const tankostream::stream::ParsedPack pack =
        tankostream::stream::StreamPackParser::parsePack(files, imdbId, configSeason);

    const QJsonArray fileProgress = m_engine->torrentFileProgress(infoHash);
    if (fileProgress.isEmpty())
        return;

    if (pack.type == QStringLiteral("series")) {
        for (const auto& pf : pack.episodes) {
            if (pf.fileIndex < 0 || pf.fileIndex >= fileProgress.size())
                continue;
            const qint64 downloaded =
                fileProgress.at(pf.fileIndex).toVariant().toLongLong();
            const int pct = pf.sizeBytes > 0
                ? static_cast<int>((downloaded * 100LL) / pf.sizeBytes)
                : 0;
            m_streamDownloadIndex->updateEpisodeProgress(
                imdbId, pf.season, pf.episode, pct);
        }
    } else if (pack.type == QStringLiteral("movie")) {
        const int fileIndex = pack.movieFile.fileIndex;
        if (fileIndex < 0 || fileIndex >= fileProgress.size())
            return;
        const qint64 downloaded =
            fileProgress.at(fileIndex).toVariant().toLongLong();
        const int pct = pack.movieFile.sizeBytes > 0
            ? static_cast<int>((downloaded * 100LL) / pack.movieFile.sizeBytes)
            : 0;
        m_streamDownloadIndex->updateEpisodeProgress(imdbId, 0, 0, pct);
    }
}

void TorrentClient::onTorrentFinished(const QString& infoHash)
{
    // libtorrent fires torrent_finished_alert every time a resumed already-
    // completed torrent re-enters the finished state (resume → recheck →
    // finished). Without this guard every app startup would append a dup
    // history entry, rewrite records.json, and re-trigger a library rescan.
    // Only first-time completions should fire side-effects.
    if (m_repo.hasTorrent(infoHash) &&
        m_records[infoHash].toObject().value("state").toString()
            == QLatin1String("completed")) {
        return;
    }

    qDebug() << "Torrent completed:" << infoHash;

    // Build info for history
    auto active = listActive();
    for (const auto& info : active) {
        if (info.infoHash == infoHash) {
            appendHistory(info);
            break;
        }
    }

    // Update record state + capture category/savePath for the rescan notify.
    QString category;
    QString savePath;
    QString streamGroupId;
    if (m_repo.hasTorrent(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("completed");
        m_records[infoHash] = rec;
        saveRecords();
        m_repo.updateTorrentState(
            infoHash, tankoban::torrent::TorrentState::Completed);
        category = rec.value("category").toString();
        savePath = rec.value("savePath").toString();
        streamGroupId = rec.value("streamGroupId").toString();
        JsonlEventLog::instance().emitEvent(
            QStringLiteral("torrent.state_changed"),
            QStringLiteral("completed"),
            QJsonObject{{QStringLiteral("hash"), infoHash},
                        {QStringLiteral("imdbId"), rec.value(QStringLiteral("imdbId")).toString()},
                        {QStringLiteral("season"), rec.value(QStringLiteral("season")).toInt(0)},
                        {QStringLiteral("streamGroupId"), streamGroupId},
                        {QStringLiteral("savePath"), savePath}});
        JsonlEventLog::instance().emitEvent(
            QStringLiteral("download.file_completed"),
            QStringLiteral("torrent_finished"),
            QJsonObject{{QStringLiteral("hash"), infoHash},
                        {QStringLiteral("imdbId"), rec.value(QStringLiteral("imdbId")).toString()},
                        {QStringLiteral("streamGroupId"), streamGroupId}});
    }

    // TANKORENT_STREAM_INTEGRATION 2026-05-15: route to the right publisher
    // based on the source of this download.
    //   - bulk-cohort (streamGroupId set): existing Stream auto-download flow
    //   - Tankorent single-add with show identity (imdbId set, no streamGroupId):
    //         new show-first picker flow — register per-episode via Task A4
    //   - everything else (direct torrent search, no identity): fall through to
    //         the existing library-rescan path; file lands in Theatre's Local
    //         files section automatically.
    const bool hasBulkGroup = !streamGroupId.isEmpty();
    QString recordImdbId;
    if (m_repo.hasTorrent(infoHash)) {
        recordImdbId = m_records[infoHash].toObject().value(QStringLiteral("imdbId")).toString();
    }
    const bool hasTankorentBinding = !hasBulkGroup && !recordImdbId.isEmpty();

    if (hasBulkGroup) {
        publishStreamBulkItemsForTorrent(infoHash);
    } else if (hasTankorentBinding) {
        publishTankorentItemsForTorrent(infoHash);
    }

    emit torrentCompleted(infoHash);

    if (hasBulkGroup || hasTankorentBinding) return;

    // Trigger a library rescan if the completed torrent saved into a tracked
    // root for its category. Prefix-match savePath against each root
    // (case-insensitive + path-normalized for Windows).
    if (m_bridge && !category.isEmpty() && !savePath.isEmpty()) {
        const QString normSave = QDir(savePath).absolutePath();
        for (const QString& root : m_bridge->rootFolders(category)) {
            const QString normRoot = QDir(root).absolutePath();
            if (normSave.startsWith(normRoot, Qt::CaseInsensitive)) {
                m_bridge->notifyRootFoldersChanged(category);
                break;
            }
        }
    }
}

void TorrentClient::onTorrentError(const QString& infoHash, const QString& message)
{
    qWarning() << "Torrent error:" << infoHash << message;
    if (m_repo.hasTorrent(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("error");
        rec["errorMessage"] = message;
        m_records[infoHash] = rec;
        saveRecords();
    }
    m_repo.updateTorrentState(
        infoHash, tankoban::torrent::TorrentState::Error, message);
    markStreamBulkItemsForTorrent(
        infoHash,
        StreamBulkItemState::Failed,
        QStringLiteral("Torrent error: %1").arg(message));
    emit torrentUpdated(infoHash);
}

void TorrentClient::onStorageMoved(const QString& infoHash, const QString& newPath)
{
    qDebug() << "Torrent storage moved:" << infoHash << "→" << newPath;
    if (!m_repo.hasTorrent(infoHash)) return;

    // Reconcile the persisted savePath with what libtorrent actually settled
    // on (paths may have been canonicalized/normalized by the OS during move).
    QJsonObject rec = m_records[infoHash].toObject();
    rec["savePath"] = newPath;
    m_records[infoHash] = rec;
    saveRecords();
    m_repo.updateTorrentSavePath(infoHash, newPath);

    emit torrentUpdated(infoHash);

    // Library rescan now that files are physically at the new location. Match
    // newPath against this category's tracked roots — same prefix-match shape
    // as onTorrentFinished (TorrentClient.cpp completion handler).
    const QString category = rec.value("category").toString();
    if (m_bridge && !category.isEmpty()) {
        const QString normNew = QDir(newPath).absolutePath();
        for (const QString& root : m_bridge->rootFolders(category)) {
            const QString normRoot = QDir(root).absolutePath();
            if (normNew.startsWith(normRoot, Qt::CaseInsensitive)) {
                m_bridge->notifyRootFoldersChanged(category);
                break;
            }
        }
    }
}

void TorrentClient::onStorageMoveFailed(const QString& infoHash, const QString& message)
{
    qWarning() << "Torrent move_storage failed:" << infoHash << message;
    // The optimistic savePath update from moveStorage() above is now stale —
    // files never made it. Stamp the error onto the record so the UI can
    // surface it, but leave the savePath as-is: libtorrent will keep serving
    // from wherever the files actually are, and the user can retry the move
    // or delete+re-add. Reverting savePath would risk pointing at a folder
    // that's now half-moved (libtorrent doesn't roll back partial copies).
    if (m_repo.hasTorrent(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["errorMessage"] = QStringLiteral("Move failed: ") + message;
        m_records[infoHash] = rec;
        saveRecords();
    }
    emit torrentUpdated(infoHash);
}

void TorrentClient::onFileRenamed(const QString& infoHash, int fileIndex, const QString& newPath)
{
    bool changed = false;
    QStringList affectedGroups;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto groupIt = m_streamBulkGroups.begin(); groupIt != m_streamBulkGroups.end(); ++groupIt) {
        QJsonObject group = groupIt.value().toObject();
        QJsonArray items = group.value("items").toArray();
        bool groupChanged = false;
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            if (item.value("infoHash").toString().compare(infoHash, Qt::CaseInsensitive) != 0)
                continue;
            if (item.value("fileIndex").toInt(-1) != fileIndex)
                continue;
            if (!isPublishingStreamBulkState(item.value("itemState").toString()))
                continue;
            item["itemState"] = QString::fromLatin1(kStatePublished);
            item["lastError"] = QString();
            // STREAM_BULK_DOWNLOAD_V2 Phase 2 — Publishing → Published
            // is the terminal-success transition; if any same-cohort
            // siblings are still Pending, cohortMaybeAdvance below will
            // resume the next one.

            // STREAM_DOWNLOADED_LIBRARY Phase 2 (2026-05-10) — register the
            // published file in the stream-side download index. Per-file
            // granularity: tile badge in Stream library home flips to
            // DOWNLOADED as soon as the FIRST episode lands, not at group
            // completion. Spec §6.1 Data Flow A. Empty registerEpisode call
            // is no-op if downloadIndex pointer not wired (defensive).
            //
            // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — the prior code
            // read imdbId / streamSeason / episodeNum from per-item fields
            // that the current bulk dispatch path never populates (they
            // were intended for a future enrichment that didn't ship), so
            // the registerEpisode call was silently skipped on EVERY
            // successful publish. Consequence: StreamDownloadIndex had
            // ZERO entries → StreamDetailView::onEpisodeActivated's
            // filePathFor() always returned empty → click fell through to
            // source-pick → spec Rule C ("Click downloaded → auto-play")
            // was violated for every bulk-downloaded episode. Fallback
            // chain now sources the missing fields from the group's
            // sourceIds object (seriesId + season) + the itemKey's
            // S<NN>E<NN> suffix (episode). All three are guaranteed-
            // present in any group dispatched via Phase 0+ buildBulkPlan.
            if (m_streamDownloadIndex) {
                QString imdbId   = item.value(QStringLiteral("imdbId")).toString();
                int seasonNum    = item.value(QStringLiteral("streamSeason")).toInt(0);
                int episodeNum   = item.value(QStringLiteral("episodeNum")).toInt(0);

                if (imdbId.isEmpty() || seasonNum <= 0 || episodeNum <= 0) {
                    const QJsonObject sourceIds =
                        group.value(QStringLiteral("sourceIds")).toObject();
                    if (imdbId.isEmpty())
                        imdbId = sourceIds.value(QStringLiteral("seriesId")).toString();
                    if (seasonNum <= 0)
                        seasonNum = sourceIds.value(QStringLiteral("season")).toInt(0);
                    if (episodeNum <= 0) {
                        const QString itemKey = item.value(QStringLiteral("itemKey")).toString();
                        const int eIdx = itemKey.lastIndexOf(QLatin1Char('E'));
                        if (eIdx > 0)
                            episodeNum = itemKey.mid(eIdx + 1).toInt();
                    }
                }

                if (!imdbId.isEmpty() && seasonNum > 0 && episodeNum > 0
                    && !newPath.isEmpty()) {
                    const qint64 fileSize = QFileInfo(newPath).size();
                    m_streamDownloadIndex->registerEpisode(
                        imdbId, seasonNum, episodeNum, newPath,
                        groupIt.key(), fileSize);
                }
            }

            items.replace(i, item);
            groupChanged = true;
        }
        if (groupChanged) {
            group["items"] = items;
            group["updatedAtMs"] = now;
            *groupIt = group;
            affectedGroups.push_back(groupIt.key());
            changed = true;
        }
    }

    if (changed)
        saveStreamBulkGroups();
    for (const QString& groupId : affectedGroups) {
        // STREAM_BULK_DOWNLOAD_V2 Phase 2 — Publishing → Published is the
        // terminal-success transition. Defensive double-advance: the slot
        // was already freed when Downloading → Publishing happened in
        // publishStreamBulkItemsForTorrent, so cohortMaybeAdvance here is
        // usually a no-op. Kept for resilience against state-machine
        // races (e.g. user manually paused next-in-queue after publish
        // started, then un-paused — invariant restoration on next event).
        cohortMaybeAdvance(groupId);
        maybeEmitStreamBulkGroupPublishComplete(groupId);
    }
}

void TorrentClient::onFileRenameFailed(const QString& infoHash, int fileIndex, const QString& message)
{
    bool changed = false;
    QStringList affectedGroups;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto groupIt = m_streamBulkGroups.begin(); groupIt != m_streamBulkGroups.end(); ++groupIt) {
        QJsonObject group = groupIt.value().toObject();
        QJsonArray items = group.value("items").toArray();
        bool groupChanged = false;
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            if (item.value("infoHash").toString().compare(infoHash, Qt::CaseInsensitive) != 0)
                continue;
            if (item.value("fileIndex").toInt(-1) != fileIndex)
                continue;
            if (!isPublishingStreamBulkState(item.value("itemState").toString()))
                continue;
            item["itemState"] = QString::fromLatin1(kStatePublishFailed);
            item["lastError"] = message;
            items.replace(i, item);
            groupChanged = true;
        }
        if (groupChanged) {
            group["items"] = items;
            group["updatedAtMs"] = now;
            *groupIt = group;
            affectedGroups.push_back(groupIt.key());
            changed = true;
        }
    }

    if (changed)
        saveStreamBulkGroups();
    for (const QString& groupId : affectedGroups) {
        // STREAM_BULK_DOWNLOAD_V2 Phase 2 — PublishFailed is terminal;
        // cohort still advances so a single rename failure doesn't
        // halt the rest of the download queue.
        cohortMaybeAdvance(groupId);
        maybeEmitStreamBulkGroupPublishComplete(groupId);
    }
}
