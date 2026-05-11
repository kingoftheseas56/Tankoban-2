#include "TorrentClient.h"
#include "TorrentEngine.h"
#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "core/stream/BulkPackVerifier.h"
#include "core/stream/StreamBulkPlan.h"
#include "core/stream/StreamDownloadIndex.h"
#include "ui/dialogs/AddTorrentDialog.h"  // for AddTorrentConfig

#include <QRegularExpression>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonValue>
#include <QStringList>
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
    if (stagingPath.isEmpty() || canonicalPath.isEmpty())
        return {};
    return QDir::fromNativeSeparators(
        QDir(stagingPath).relativeFilePath(canonicalPath));
}

QMap<int, int> priorityVectorToMap(const QVector<int>& priorities)
{
    QMap<int, int> mapped;
    for (int i = 0; i < priorities.size(); ++i)
        mapped.insert(i, priorities.at(i));
    return mapped;
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
    constexpr qint64 kGcAgeMs = 7LL * 24LL * 60LL * 60LL * 1000LL;

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
            this, &TorrentClient::onMetadataReady);
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

    loadRecords();
    loadStreamBulkGroups();
    m_engine->start();
    reconcileStreamBulkGroups();

    // Re-add persisted torrents from fastresume files
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
        if (m_records.contains(hash)) continue;
        qDebug() << "TorrentClient: removing orphan resume file:" << fi.fileName();
        QFile::remove(fi.absoluteFilePath());
    }

    // One-shot history retro-compact — collapses duplicate entries per
    // infoHash (bloat accumulated before Phase 1 Batch 1.1's re-fire guard
    // landed). Idempotent after first pass.
    compactHistory();
}

TorrentClient::~TorrentClient()
{
    m_engine->stop();
    saveRecords();
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
    QJsonObject data;
    data["active"] = m_records;
    m_bridge->store().write(RECORDS_FILE, data);
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
    m_streamBulkGroups = pruneTerminalStreamBulkGroups(m_streamBulkGroups);
    QJsonObject data;
    data["groups"] = m_streamBulkGroups;
    m_bridge->store().write(STREAM_BULK_GROUPS_FILE, data);
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
            const bool hasRecord = !infoHash.isEmpty() && m_records.contains(infoHash);
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

                const QJsonObject rec = m_records.value(infoHash).toObject();
                const QString persistedState = rec.value("state").toString();
                const QString activeState = activeStates.value(torrentKey);
                const bool seedingLike =
                    persistedState == QLatin1String("completed") ||
                    persistedState == QLatin1String("seeding") ||
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

    if (changed)
        saveStreamBulkGroups();

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
            if (state == QLatin1String(kStatePending))
                m_engine->pauseTorrent(infoHash);
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
    m_streamBulkGroups[group.groupId] = streamBulkGroupToJson(group);
    saveStreamBulkGroups();
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

    const QString prefix = QStringLiteral("stream:") + imdbId + QLatin1Char(':');
    for (auto groupIt = m_streamBulkGroups.constBegin();
         groupIt != m_streamBulkGroups.constEnd(); ++groupIt) {
        if (!groupIt.key().startsWith(prefix))
            continue;
        const QJsonObject group = groupIt.value().toObject();
        const QJsonObject sourceIds = group.value("sourceIds").toObject();
        if (sourceIds.value("season").toInt(-1) != season)
            continue;

        const QJsonArray items = group.value("items").toArray();
        for (const auto& v : items) {
            const QJsonObject item = v.toObject();
            const QString itemKey = item.value("itemKey").toString();
            // Parse "<imdb>:S<NN>E<NN>" — locate the trailing "E" after
            // the "S<NN>" segment. Fallback to ":episode_num" field if
            // present (defensive against schema drift).
            int episodeNum = item.value("episode_num").toInt(0);
            if (episodeNum <= 0) {
                const int eIdx = itemKey.lastIndexOf(QLatin1Char('E'));
                if (eIdx > 0) {
                    bool ok = false;
                    const int parsed = itemKey.mid(eIdx + 1).toInt(&ok);
                    if (ok)
                        episodeNum = parsed;
                }
            }
            if (episodeNum <= 0)
                continue;

            const QString state = item.value("itemState").toString();
            int pct = -1;
            if (state == QLatin1String(kStateDownloading)) {
                const QString hash = item.value("infoHash").toString().toLower();
                pct = progressByHash.value(hash, 0);
            } else if (state == QLatin1String(kStatePending)) {
                pct = 0;
            } else if (state == QLatin1String(kStatePublished) ||
                       state == QLatin1String(kStatePublishing)) {
                pct = 100;
            }  // failed states: pct = -1 sentinel

            // If multiple groups touch the same episode (re-issued bulk),
            // prefer the latest entry (overwrite) — group iteration order
            // is QJsonObject insertion order which mirrors save chronology.
            out.insert(episodeNum, qMakePair(state, pct));
        }
    }
    return out;
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
        saveStreamBulkGroups();
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
        saveStreamBulkGroups();
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
    }

    if (!stagingPath.isEmpty() && (allPublished || !hasPublishing))
        QDir(stagingPath).removeRecursively();
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

void TorrentClient::retryStreamBulkGroupFailedItems(const QString& groupId)
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
        const QString itemKey = item.value("itemKey").toString();
        const QString state = item.value("itemState").toString();
        const QString infoHash = item.value("infoHash").toString();
        const QString lastError = item.value("lastError").toString();

        if (isStreamBulkSourceRetryState(state, infoHash)) {
            item["itemState"] = QString::fromLatin1(kStatePending);
            item["lastError"] = QString();
            items.replace(i, item);
            changed = true;
            if (!itemKey.isEmpty() && !sourceRetryItemKeys.contains(itemKey))
                sourceRetryItemKeys.push_back(itemKey);
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
            m_records.contains(infoHash) &&
            lastError.startsWith(QStringLiteral("Torrent error:"), Qt::CaseInsensitive)) {
            m_engine->resumeTorrent(infoHash);
            QJsonObject rec = m_records.value(infoHash).toObject();
            rec["state"] = QStringLiteral("downloading");
            rec.remove("errorMessage");
            m_records[infoHash] = rec;
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

    retryStreamBulkPublishing();

    if (!sourceRetryItemKeys.isEmpty())
        emit streamBulkRetrySourcePickRequested(groupId, sourceRetryItemKeys);
    else if (changed)
        maybeEmitStreamBulkGroupPublishComplete(groupId);
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

    // Snapshot active states so we can detect libtorrent-error torrents.
    QHash<QString, QString> activeStates;
    for (const TorrentInfo& info : listActive())
        activeStates.insert(info.infoHash.toLower(), info.stateString);

    bool changed = false;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        const QString state = item.value("itemState").toString();
        // Leave Published + Completed alone — they're terminal-success.
        if (state == QLatin1String(kStatePublished) ||
            state == QLatin1String(kStateCompleted))
            continue;

        const QString infoHash = item.value("infoHash").toString();
        if (!infoHash.isEmpty()) {
            const QString live = activeStates.value(infoHash.toLower());
            if (live == QLatin1String("error"))
                forceRecheck(infoHash);  // public wrapper — emits torrentUpdated for UI/persistence listeners
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
    }

    // Re-engage the cohort scheduler. After the reset above, every
    // non-Published item is Pending — cohortMaybeAdvance picks the
    // first eligible and resumes it.
    cohortMaybeAdvance(groupId);
}

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
    for (const QString& groupId : affectedGroups)
        cohortMaybeAdvance(groupId);
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
            if (!isDownloadingStreamBulkState(item.value("itemState").toString()))
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
        // STREAM_BULK_DOWNLOAD_V2 Phase 2 — Downloading → Publishing
        // transition frees the cohort slot. Advancing here means the
        // next per-episode magnet starts in parallel with the file
        // rename (rename is sub-second + uses no bandwidth).
        cohortMaybeAdvance(groupId);
        maybeEmitStreamBulkGroupPublishComplete(groupId);
    }
}

void TorrentClient::retryStreamBulkPublishing()
{
    if (m_streamBulkGroups.isEmpty())
        return;

    QHash<QString, QString> activeStates;
    for (const TorrentInfo& info : listActive())
        activeStates.insert(info.infoHash.toLower(), info.stateString);

    auto seedingLike = [&](const QString& infoHash) {
        if (!m_records.contains(infoHash))
            return false;
        const QJsonObject rec = m_records.value(infoHash).toObject();
        const QString persisted = rec.value("state").toString();
        const QString active = activeStates.value(infoHash.toLower());
        return persisted == QLatin1String("completed") ||
               persisted == QLatin1String("seeding") ||
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
            if (isDownloadingStreamBulkState(state)) {
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
    return m_records.contains(hash);
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

void TorrentClient::startDownload(const QString& infoHash, const AddTorrentConfig& config)
{
    // Apply file priorities
    if (!config.filePriorities.isEmpty()) {
        int maxIdx = 0;
        for (auto it = config.filePriorities.begin(); it != config.filePriorities.end(); ++it)
            maxIdx = qMax(maxIdx, it.key());

        QVector<int> priorities(maxIdx + 1, 0);
        for (auto it = config.filePriorities.begin(); it != config.filePriorities.end(); ++it)
            priorities[it.key()] = it.value();

        m_engine->setFilePriorities(infoHash, priorities);
    }

    // Sequential download
    if (config.sequential)
        m_engine->setSequentialDownload(infoHash, true);

    // Content layout: strip root folder if "no_subfolder"
    if (config.contentLayout == QLatin1String("no_subfolder"))
        m_engine->flattenFiles(infoHash);

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
    m_records[infoHash]    = rec;
    saveRecords();

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
    m_engine->startTorrent(infoHash, config.destinationPath);
    if (config.startPaused)
        m_engine->pauseTorrent(infoHash);

    emit torrentAdded(infoHash);
}

void TorrentClient::moveStorage(const QString& infoHash, const QString& newSavePath)
{
    if (newSavePath.isEmpty()) return;
    if (!m_records.contains(infoHash)) return;

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
QList<TorrentInfo> TorrentClient::listActive() const
{
    QList<TorrentInfo> result;
    auto statuses = m_engine->allStatuses();

    // Build lookup from engine
    QMap<QString, TorrentStatus> statusMap;
    for (const auto& s : statuses)
        statusMap[s.infoHash] = s;

    // Merge with stored records
    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        QString hash = it.key();
        QJsonObject rec = it.value().toObject();

        TorrentInfo info;
        info.infoHash    = hash;
        info.name        = rec["name"].toString();
        info.savePath    = rec["savePath"].toString();
        info.category    = rec["category"].toString();
        info.addedAt      = rec["addedAt"].toVariant().toLongLong();
        info.streamGroupId = rec["streamGroupId"].toString();
        info.sequential   = rec["sequential"].toBool();
        info.errorMessage = rec["errorMessage"].toString();

        if (statusMap.contains(hash)) {
            const auto& st = statusMap[hash];
            info.stateString = st.stateString;
            info.progress    = st.progress;
            info.dlSpeed     = st.downloadRate;
            info.ulSpeed     = st.uploadRate;
            info.peers       = st.numPeers;
            info.seeds       = st.numSeeds;
            info.totalDone    = st.totalDone;
            info.totalWanted  = st.totalWanted;
            info.forceStarted = st.forceStarted;
            info.queuePosition = st.queuePosition;
            info.dlLimit      = st.dlLimit;
            info.ulLimit      = st.ulLimit;
            if (info.name.isEmpty())
                info.name = st.name;
        } else {
            info.stateString = rec["state"].toString();
        }

        result.append(info);
    }

    return result;
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
    if (m_records.contains(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("paused");
        m_records[infoHash] = rec;
        saveRecords();
    }
    emit torrentUpdated(infoHash);
}

void TorrentClient::resumeTorrent(const QString& infoHash)
{
    m_engine->resumeTorrent(infoHash);
    if (m_records.contains(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("downloading");
        rec.remove("errorMessage");
        m_records[infoHash] = rec;
        saveRecords();
    }
    emit torrentUpdated(infoHash);
}

void TorrentClient::deleteTorrent(const QString& infoHash, bool deleteFiles)
{
    const bool hadRecord = m_records.contains(infoHash);
    m_engine->removeTorrent(infoHash, deleteFiles);
    m_records.remove(infoHash);
    if (hadRecord) {
        markStreamBulkItemsForTorrent(
            infoHash,
            StreamBulkItemState::Cancelled,
            QStringLiteral("Torrent removed from Tankorent"));
    }
    saveRecords();
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
    if (m_records.contains(infoHash)) {
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
                                     qint64 /*totalSize*/, const QJsonArray& /*files*/)
{
    if (m_records.contains(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["name"]  = name;
        rec["state"] = QStringLiteral("metadata_ready");
        m_records[infoHash] = rec;
        saveRecords();
    }
    emit torrentUpdated(infoHash);
}

void TorrentClient::onTorrentFinished(const QString& infoHash)
{
    // libtorrent fires torrent_finished_alert every time a resumed already-
    // completed torrent re-enters the finished state (resume → recheck →
    // finished). Without this guard every app startup would append a dup
    // history entry, rewrite records.json, and re-trigger a library rescan.
    // Only first-time completions should fire side-effects.
    if (m_records.contains(infoHash) &&
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
    if (m_records.contains(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("completed");
        m_records[infoHash] = rec;
        saveRecords();
        category = rec.value("category").toString();
        savePath = rec.value("savePath").toString();
        streamGroupId = rec.value("streamGroupId").toString();
    }

    if (!streamGroupId.isEmpty())
        publishStreamBulkItemsForTorrent(infoHash);

    emit torrentCompleted(infoHash);

    if (!streamGroupId.isEmpty())
        return;

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
    if (m_records.contains(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("error");
        rec["errorMessage"] = message;
        m_records[infoHash] = rec;
        saveRecords();
    }
    markStreamBulkItemsForTorrent(
        infoHash,
        StreamBulkItemState::Failed,
        QStringLiteral("Torrent error: %1").arg(message));
    emit torrentUpdated(infoHash);
}

void TorrentClient::onStorageMoved(const QString& infoHash, const QString& newPath)
{
    qDebug() << "Torrent storage moved:" << infoHash << "→" << newPath;
    if (!m_records.contains(infoHash)) return;

    // Reconcile the persisted savePath with what libtorrent actually settled
    // on (paths may have been canonicalized/normalized by the OS during move).
    QJsonObject rec = m_records[infoHash].toObject();
    rec["savePath"] = newPath;
    m_records[infoHash] = rec;
    saveRecords();

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
    if (m_records.contains(infoHash)) {
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
            if (m_streamDownloadIndex) {
                const QString imdbId   = item.value(QStringLiteral("imdbId")).toString();
                const int seasonNum    = item.value(QStringLiteral("streamSeason")).toInt(0);
                const int episodeNum   = item.value(QStringLiteral("episodeNum")).toInt(0);
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
