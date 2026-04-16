#include "TorrentClient.h"
#include "TorrentEngine.h"
#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "ui/dialogs/AddTorrentDialog.h"  // for AddTorrentConfig

#include <QRegularExpression>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QDebug>

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

    loadRecords();
    m_engine->start();

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
}

void TorrentClient::saveRecords()
{
    QJsonObject data;
    data["active"] = m_records;
    m_bridge->store().write(RECORDS_FILE, data);
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
    rec["sequential"]      = config.sequential;
    m_records[infoHash]    = rec;
    saveRecords();

    // Start or keep paused
    if (!config.startPaused)
        m_engine->startTorrent(infoHash, config.destinationPath);

    emit torrentAdded(infoHash);
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
    m_engine->removeTorrent(infoHash, deleteFiles);
    m_records.remove(infoHash);
    saveRecords();
    emit torrentRemoved(infoHash);
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
    if (m_records.contains(infoHash)) {
        QJsonObject rec = m_records[infoHash].toObject();
        rec["state"] = QStringLiteral("completed");
        m_records[infoHash] = rec;
        saveRecords();
        category = rec.value("category").toString();
        savePath = rec.value("savePath").toString();
    }

    emit torrentCompleted(infoHash);

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
    emit torrentUpdated(infoHash);
}
