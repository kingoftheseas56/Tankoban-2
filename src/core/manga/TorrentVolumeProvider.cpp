// src/core/manga/TorrentVolumeProvider.cpp
//
// TANKOYOMI_PREMIUM Phase 3 -- implementation. See header for design rationale.
// Logging routed through DebugLogBuffer so smoke evidence shows up in
// `out\tankoctl.exe logs`.
#include "TorrentVolumeProvider.h"
#include "PremiumCatalog.h"
#include "PremiumArchiveValidator.h"
#include "PremiumCoverExtractor.h"
#include "MangaDownloadIndex.h"
#include "../DebugLogBuffer.h"
#include "../torrent/TorrentEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <algorithm>
#include <optional>

namespace tankoban::manga::premium {

namespace {
constexpr int kFilePriorityTarget = 7; // libtorrent max priority for the requested file
constexpr int kFilePriorityOff    = 0; // explicitly do-not-download for everything else
constexpr const char* kLogSource  = "TorrentVolumeProvider";
// Single catalog source-id used by all ledger keys + Inflight rows. DRY the
// three previous hardcoded literals (review issue Important #4); future
// non-Premium catalog sources would extend by adding sibling constants.
constexpr const char* kCatalogIdTankoyomiPremium = "tankoyomi_premium";

struct MetadataCbzCandidate {
    int index = -1;
    QString path;
    qint64 sizeBytes = 0;
};

QString metadataFilePath(const QJsonObject& obj)
{
    QString path = obj.value(QStringLiteral("name")).toString();
    if (path.isEmpty()) path = obj.value(QStringLiteral("path")).toString();
    return path;
}

QList<MetadataCbzCandidate> cbzCandidatesFromMetadata(const QJsonArray& files)
{
    QList<MetadataCbzCandidate> out;
    for (int i = 0; i < files.size(); ++i) {
        const QJsonObject obj = files.at(i).toObject();
        const QString path = metadataFilePath(obj);
        if (!path.endsWith(QStringLiteral(".cbz"), Qt::CaseInsensitive)) continue;

        MetadataCbzCandidate c;
        c.index = obj.value(QStringLiteral("index")).toInt(i);
        c.path = path;
        c.sizeBytes = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble(0));
        if (c.index >= 0) out.append(c);
    }
    return out;
}

QJsonArray candidatePathList(const QList<MetadataCbzCandidate>& candidates)
{
    QJsonArray out;
    for (const auto& c : candidates) out.append(c.path);
    return out;
}

bool volumeTokenMatches(const QString& fileName, int volumeNumber)
{
    if (volumeNumber <= 0) return false;
    const QString n = QString::number(volumeNumber);
    const QRegularExpression re(
        QStringLiteral("(?:^|[^A-Za-z0-9])(?:v|vol(?:ume)?)\\.?\\s*0*%1(?:[^0-9]|$)")
            .arg(QRegularExpression::escape(n)),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(fileName).hasMatch();
}

std::optional<MetadataCbzCandidate> chooseMetadataCbz(
    const QList<MetadataCbzCandidate>& candidates,
    const QString& cbzFileName,
    int volumeNumber)
{
    if (candidates.isEmpty()) return std::nullopt;
    if (candidates.size() == 1) return candidates.first();

    const QString wantedBase = QFileInfo(cbzFileName).fileName();
    if (!wantedBase.isEmpty()) {
        for (const auto& c : candidates) {
            if (QFileInfo(c.path).fileName().compare(wantedBase, Qt::CaseInsensitive) == 0) {
                return c;
            }
        }
    }

    QList<MetadataCbzCandidate> fuzzy;
    for (const auto& c : candidates) {
        if (volumeTokenMatches(QFileInfo(c.path).fileName(), volumeNumber)) {
            fuzzy.append(c);
        }
    }
    if (fuzzy.size() == 1) return fuzzy.first();
    return std::nullopt;
}
} // anonymous namespace

TorrentVolumeProvider::TorrentVolumeProvider(TorrentEngine*        engine,
                                             PremiumCatalog*       catalog,
                                             TorrentRequestLedger* ledger,
                                             MangaDownloadIndex*   index,
                                             const QString&        stagingRoot,
                                             const QString&        coversDir,
                                             QObject*              parent)
    : QObject(parent)
    , m_engine(engine)
    , m_catalog(catalog)
    , m_ledger(ledger)
    , m_index(index)
    , m_stagingRoot(stagingRoot)
    , m_coversDir(coversDir)
{
    Q_ASSERT(m_engine);
    Q_ASSERT(m_catalog);
    Q_ASSERT(m_ledger);
    Q_ASSERT(m_index);
    QDir().mkpath(m_stagingRoot);
    QDir().mkpath(m_coversDir);

    // TANKOYOMI_PREMIUM Phase 10 -- own the cover extractor and proxy its
    // coverReady out as volumeCoverReady. coverFailed routes to a debug log
    // (UI falls back to the series-level poster). Connect BEFORE the engine
    // signal block to keep the diff localized.
    m_coverExtractor = new PremiumCoverExtractor(this);
    connect(m_coverExtractor, &PremiumCoverExtractor::coverReady,
            this, [this](const QString& s, int v, const QString& p) {
                emit volumeCoverReady(s, v, p);
            },
            Qt::QueuedConnection);
    connect(m_coverExtractor, &PremiumCoverExtractor::coverFailed,
            this, [](const QString& s, int v, const QString& reason) {
                QJsonObject details;
                details[QStringLiteral("seriesId")]     = s;
                details[QStringLiteral("volumeNumber")] = v;
                details[QStringLiteral("reason")]       = reason;
                DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
                    QStringLiteral("cover extraction failed"), details);
            },
            Qt::QueuedConnection);

    // Per Codex section 18: TorrentEngine emits from its alert worker thread.
    // Every connection is queued so we receive on the provider's owning thread.
    connect(m_engine, &TorrentEngine::metadataReady,
            this, &TorrentVolumeProvider::onMetadataReady,
            Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::pieceFinished,
            this, &TorrentVolumeProvider::onPieceFinished,
            Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::torrentError,
            this, &TorrentVolumeProvider::onTorrentError,
            Qt::QueuedConnection);
}

TorrentVolumeProvider::~TorrentVolumeProvider() = default;

QString TorrentVolumeProvider::stagingPathFor(const QString& infoHash) const
{
    return m_stagingRoot + QChar('/') + infoHash.toLower();
}

void TorrentVolumeProvider::requestVolume(const PremiumCatalogEntry& entry,
                                          const PremiumVolumeEntry&  volumeEntry,
                                          const QString&             destinationPath)
{
    // Per review Important #2: normalize at the storage boundary so the
    // m_byInfoHash keying agrees with onPieceFinished's lower-case lookup
    // (and ledger's lower-case storage). The catalog validator already
    // rejects non-lowercase hex, but this defends against future schema drift.
    const QString infoHash = entry.expectedInfoHash.toLower();
    const QString key      = requestKey(QString::fromLatin1(kCatalogIdTankoyomiPremium),
                                        entry.seriesId, volumeEntry.vol);

    // Idempotency: if already in-flight, no-op.
    for (const auto& iff : m_byInfoHash.value(infoHash)) {
        if (iff.requestKey == key) {
            QJsonObject details;
            details[QStringLiteral("key")] = key;
            DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
                QStringLiteral("requestVolume noop (already in-flight)"), details);
            return;
        }
    }

    Inflight iff;
    iff.requestKey               = key;
    iff.catalogId                = QString::fromLatin1(kCatalogIdTankoyomiPremium);
    iff.seriesId                 = entry.seriesId;
    iff.volumeNumber             = volumeEntry.vol;
    iff.expectedInfoHash         = infoHash;
    iff.fileIndex                = volumeEntry.fileIndex;
    iff.cbzFileName              = volumeEntry.cbzFileName;
    iff.fileSizeBytes            = volumeEntry.fileSizeBytes;
    iff.pieceStart               = volumeEntry.pieceStart;
    iff.pieceEnd                 = volumeEntry.pieceEnd;
    iff.stagingPath              = stagingPathFor(infoHash);
    iff.canonicalDestinationPath = destinationPath + QChar('/') + volumeEntry.cbzFileName;
    QDir().mkpath(iff.stagingPath);

    m_byInfoHash[infoHash].append(iff);

    // Persist the request row before any external side-effect (engine add).
    TorrentRequest row;
    row.catalogId                = iff.catalogId;
    row.seriesId                 = iff.seriesId;
    row.volumeNumber             = iff.volumeNumber;
    row.expectedInfoHash         = iff.expectedInfoHash;
    row.magnetUri                = entry.magnetUri;
    row.fileIndex                = iff.fileIndex;
    row.cbzFileName              = iff.cbzFileName;
    row.fileSizeBytes            = iff.fileSizeBytes;
    row.pieceStart               = iff.pieceStart;
    row.pieceEnd                 = iff.pieceEnd;
    row.stagingPath              = iff.stagingPath;
    row.canonicalDestinationPath = iff.canonicalDestinationPath;
    row.status                   = TorrentRequest::Status::Pending;
    row.createdAtMsEpoch         = QDateTime::currentMSecsSinceEpoch();
    row.updatedAtMsEpoch         = row.createdAtMsEpoch;
    m_ledger->upsert(row);

    ensureTorrentAdded(iff);
}

void TorrentVolumeProvider::ensureTorrentAdded(const Inflight& iff)
{
    // Per Codex section 17.3: addMagnet(paused=true) keeps the torrent in
    // upload-only mode so metadata arrives without an all-files-download
    // window. The matching startTorrent() call clears upload-only AFTER
    // priorities are set (see applyUnionPriorities).
    //
    // We pass the catalog's series-id as the savePath suffix so libtorrent's
    // resume data finds the same staging tree across restarts.
    const QString stagingPath = iff.stagingPath;

    QJsonObject details;
    details[QStringLiteral("infoHash")] = iff.expectedInfoHash;
    details[QStringLiteral("staging")]  = stagingPath;
    DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
        QStringLiteral("addMagnet(paused=true)"), details);

    const auto pending = m_ledger->find(iff.requestKey);
    if (!pending) return;

    m_engine->addMagnet(pending->magnetUri, stagingPath, /*paused=*/true);
    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::AwaitingMetadata);
}

void TorrentVolumeProvider::onMetadataReady(const QString& infoHash,
                                            const QString& name,
                                            qint64         totalSize,
                                            const QJsonArray& files)
{
    Q_UNUSED(name)
    Q_UNUSED(totalSize)
    auto it = m_byInfoHash.find(infoHash.toLower());
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    QJsonObject details;
    details[QStringLiteral("infoHash")] = infoHash.toLower();
    details[QStringLiteral("inflight")] = it->size();
    DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
        QStringLiteral("metadataReady"), details);

    // Codex section 24 v1 requirement #1: app rejects any magnet that
    // resolves to an infoHash different from the catalog's expectedInfoHash.
    // m_byInfoHash is keyed by the catalog's expectedInfoHash; if libtorrent
    // resolved metadata to a DIFFERENT infoHash, this iterator-find would
    // not have succeeded above. So a divergence is only reachable if a
    // bucket carries Inflight entries whose .expectedInfoHash disagrees
    // with the lookup key -- defensive against future refactors that might
    // mutate that invariant. Verify anyway.
    bool anyMismatch = false;
    for (const auto& iff : *it) {
        if (iff.expectedInfoHash != infoHash.toLower()) {
            anyMismatch = true;
            m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                                   QStringLiteral("infohash_mismatch"),
                                   QStringLiteral("metadata resolved to ") + infoHash.toLower() +
                                   QStringLiteral(" but catalog expected ") + iff.expectedInfoHash);
            emit volumeFailed(iff.seriesId, iff.volumeNumber,
                              QStringLiteral("infohash_mismatch"),
                              QStringLiteral("metadata infoHash does not match catalog"));
        }
    }
    if (anyMismatch) {
        m_engine->removeTorrent(infoHash.toLower(), /*deleteFiles=*/true);
        m_byInfoHash.remove(infoHash.toLower());
        return;
    }

    resolveUnresolvedFileIndices(infoHash.toLower(), files);
    it = m_byInfoHash.find(infoHash.toLower());
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    applyUnionPriorities(infoHash.toLower());

    // Clear upload-only mode now that priorities pin to-download to the
    // requested files only. The savePath argument is the existing staging
    // dir; TorrentEngine validates this against its current resume data.
    const QString stagingPath = stagingPathFor(infoHash.toLower());
    m_engine->startTorrent(infoHash.toLower(), stagingPath);

    for (auto& iff : *it) {
        iff.startedAfterMeta = true;
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Downloading);
    }
}

void TorrentVolumeProvider::resolveUnresolvedFileIndices(const QString& infoHash,
                                                         const QJsonArray& files)
{
    auto it = m_byInfoHash.find(infoHash);
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    const auto candidates = cbzCandidatesFromMetadata(files);
    QList<Inflight> kept;
    kept.reserve(it->size());

    for (Inflight iff : *it) {
        if (iff.fileIndex >= 0) {
            kept.append(iff);
            continue;
        }

        const auto chosen = chooseMetadataCbz(candidates, iff.cbzFileName, iff.volumeNumber);
        if (!chosen.has_value()) {
            QJsonObject details;
            details[QStringLiteral("infoHash")] = infoHash;
            details[QStringLiteral("seriesId")] = iff.seriesId;
            details[QStringLiteral("volumeNumber")] = iff.volumeNumber;
            details[QStringLiteral("wanted")] = iff.cbzFileName;
            details[QStringLiteral("candidates")] = candidatePathList(candidates);
            DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
                QStringLiteral("metadata file match failed"), details);
            m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                                   QStringLiteral("metadata_file_match_failed"),
                                   QStringLiteral("could not match volume to a .cbz file in torrent metadata"));
            emit volumeFailed(iff.seriesId, iff.volumeNumber,
                              QStringLiteral("metadata_file_match_failed"),
                              QStringLiteral("could not match volume to a .cbz file in torrent metadata"));
            continue;
        }

        const qint64 fileSize = chosen->sizeBytes;
        const auto pieceRange = m_engine->pieceRangeForFileOffset(infoHash, chosen->index,
                                                                  0, fileSize);
        if (fileSize <= 0 || pieceRange.first < 0 || pieceRange.second < pieceRange.first) {
            QJsonObject details;
            details[QStringLiteral("infoHash")] = infoHash;
            details[QStringLiteral("seriesId")] = iff.seriesId;
            details[QStringLiteral("volumeNumber")] = iff.volumeNumber;
            details[QStringLiteral("fileIndex")] = chosen->index;
            details[QStringLiteral("fileSizeBytes")] = fileSize;
            DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
                QStringLiteral("metadata file range resolve failed"), details);
            m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                                   QStringLiteral("metadata_file_range_failed"),
                                   QStringLiteral("could not compute piece range for matched .cbz"));
            emit volumeFailed(iff.seriesId, iff.volumeNumber,
                              QStringLiteral("metadata_file_range_failed"),
                              QStringLiteral("could not compute piece range for matched .cbz"));
            continue;
        }

        iff.fileIndex = chosen->index;
        iff.cbzFileName = chosen->path;
        iff.fileSizeBytes = fileSize;
        iff.pieceStart = pieceRange.first;
        iff.pieceEnd = pieceRange.second;

        if (auto row = m_ledger->find(iff.requestKey)) {
            TorrentRequest updated = *row;
            updated.fileIndex = iff.fileIndex;
            updated.cbzFileName = iff.cbzFileName;
            updated.fileSizeBytes = iff.fileSizeBytes;
            updated.pieceStart = iff.pieceStart;
            updated.pieceEnd = iff.pieceEnd;
            updated.updatedAtMsEpoch = QDateTime::currentMSecsSinceEpoch();
            m_ledger->upsert(updated);
        }

        QJsonObject details;
        details[QStringLiteral("infoHash")] = infoHash;
        details[QStringLiteral("seriesId")] = iff.seriesId;
        details[QStringLiteral("volumeNumber")] = iff.volumeNumber;
        details[QStringLiteral("fileIndex")] = iff.fileIndex;
        details[QStringLiteral("filePath")] = iff.cbzFileName;
        details[QStringLiteral("fileSizeBytes")] = iff.fileSizeBytes;
        details[QStringLiteral("pieceStart")] = iff.pieceStart;
        details[QStringLiteral("pieceEnd")] = iff.pieceEnd;
        DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
            QStringLiteral("metadata file match resolved"), details);

        kept.append(iff);
    }

    if (kept.isEmpty()) {
        m_engine->removeTorrent(infoHash, /*deleteFiles=*/false);
        m_byInfoHash.erase(it);
        return;
    }
    *it = kept;
}

void TorrentVolumeProvider::applyUnionPriorities(const QString& infoHash)
{
    auto it = m_byInfoHash.find(infoHash);
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    // The largest fileIndex across our in-flight requests defines the size
    // of the priorities vector we need to send. TorrentEngine will pad with
    // priority 0 internally if the vector is shorter than the torrent's
    // actual file count, but being explicit is safer when the torrent has
    // many files.
    int maxIndex = 0;
    for (const auto& iff : *it) maxIndex = qMax(maxIndex, iff.fileIndex);

    QVector<int> priorities(maxIndex + 1, kFilePriorityOff);
    for (const auto& iff : *it) {
        if (iff.fileIndex >= 0 && iff.fileIndex < priorities.size()) {
            priorities[iff.fileIndex] = kFilePriorityTarget;
        }
    }
    m_engine->setFilePriorities(infoHash, priorities);

    for (auto& iff : *it) iff.prioritiesApplied = true;

    QJsonObject details;
    details[QStringLiteral("infoHash")] = infoHash;
    details[QStringLiteral("targets")]  = it->size();
    DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
        QStringLiteral("applied union priorities"), details);
}

void TorrentVolumeProvider::onTorrentError(const QString& infoHash,
                                           const QString& errorMessage)
{
    auto it = m_byInfoHash.find(infoHash.toLower());
    if (it == m_byInfoHash.end()) return;
    for (const auto& iff : *it) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("engine_error"), errorMessage);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("engine_error"), errorMessage);
    }
}

// =====================================================================
// Task 3.5 -- event-driven file completion (pieceFinished + checkFileCompletion
// + finalizeCompletion + emitProgressIfChanged).
// =====================================================================

void TorrentVolumeProvider::onPieceFinished(const QString& infoHash, int pieceIndex)
{
    auto it = m_byInfoHash.find(infoHash.toLower());
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    // For each in-flight request that owns this piece's range, check whether
    // its file is now byte-complete and emit progress.
    QList<Inflight> completedThisCall;
    for (auto& iff : *it) {
        if (pieceIndex < iff.pieceStart || pieceIndex > iff.pieceEnd) continue;

        emitProgressIfChanged(iff);

        if (checkFileCompletion(infoHash.toLower(), iff)) {
            completedThisCall.append(iff);
        }
    }

    // Finalize completed requests AFTER the iteration (finalize mutates the
    // m_byInfoHash list to remove the completed entry).
    for (const auto& done : completedThisCall) {
        finalizeCompletion(done);
    }
}

bool TorrentVolumeProvider::checkFileCompletion(const QString& infoHash,
                                                const Inflight& iff)
{
    // Per Codex section 19: fileByteRangesOfHavePieces returns a list of
    // contiguous byte ranges within the file that are fully downloaded.
    // File is complete when the merged ranges cover [0, fileSizeBytes).
    const auto ranges = m_engine->fileByteRangesOfHavePieces(infoHash, iff.fileIndex);

    qint64 covered = 0;
    for (const auto& rng : ranges) {
        // TorrentEngine::fileByteRangesOfHavePieces returns {startByte, endByte}
        // in file-local coordinates, endByte exclusive. covered = sum of widths.
        const qint64 start = qMax<qint64>(0, rng.first);
        const qint64 end   = qMax<qint64>(start, rng.second);
        covered += (end - start);
    }
    return covered >= iff.fileSizeBytes && iff.fileSizeBytes > 0;
}

void TorrentVolumeProvider::emitProgressIfChanged(Inflight& iff)
{
    const auto ranges = m_engine->fileByteRangesOfHavePieces(iff.expectedInfoHash, iff.fileIndex);
    qint64 covered = 0;
    for (const auto& rng : ranges) {
        const qint64 start = qMax<qint64>(0, rng.first);
        const qint64 end   = qMax<qint64>(start, rng.second);
        covered += (end - start);
    }
    const double pct = (iff.fileSizeBytes > 0)
        ? double(covered) / double(iff.fileSizeBytes)
        : 0.0;

    // Quantize to 0.5% steps to bound emit frequency.
    const double quantized = qRound(pct * 200.0) / 200.0;
    if (qFuzzyCompare(1.0 + quantized, 1.0 + iff.lastReportedPct)) return;
    iff.lastReportedPct = quantized;
    emit volumeProgress(iff.seriesId, iff.volumeNumber, quantized);
}

void TorrentVolumeProvider::finalizeCompletion(Inflight iff)
{
    // Phase 4 lifecycle per Codex section 21:
    //   flushCache -> Validating ledger state -> copy-to-.tankoban-part
    //   (outside the scanner glob so LibraryScanner cannot pick up a partial
    //   archive) -> validate -> atomic rename to .cbz on success; on
    //   validation failure, move .tankoban-part to the quarantine dir +
    //   emit volumeFailed with a stable error code.
    m_engine->flushCache(iff.expectedInfoHash);
    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Validating);

    const QString stagingFile = iff.stagingPath + QChar('/') + iff.cbzFileName;
    const QString finalFile   = iff.canonicalDestinationPath;
    const QString partFile    = finalFile + QStringLiteral(".tankoban-part");

    QFileInfo fi(finalFile);
    QDir().mkpath(fi.absolutePath());

    auto removeFromBucket = [&]() {
        auto bucket = m_byInfoHash.find(iff.expectedInfoHash);
        if (bucket != m_byInfoHash.end()) {
            bucket->erase(std::remove_if(bucket->begin(), bucket->end(),
                [&](const Inflight& x){ return x.requestKey == iff.requestKey; }),
                bucket->end());
        }
    };

    // Collision guard per Codex section 18: never overwrite an existing cbz.
    if (QFile::exists(finalFile)) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("destination_exists"),
                               QStringLiteral("refusing to overwrite ") + finalFile);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("destination_exists"),
                          QStringLiteral("refusing to overwrite ") + finalFile);
        removeFromBucket();
        return;
    }

    // Step 1: copy from staging to .tankoban-part (outside scanner glob).
    if (QFile::exists(partFile)) QFile::remove(partFile);
    if (!QFile::copy(stagingFile, partFile)) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("copy_to_part_failed"),
                               QStringLiteral("could not copy ") + stagingFile +
                               QStringLiteral(" -> ") + partFile);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("copy_to_part_failed"), QString());
        removeFromBucket();
        return;
    }

    // Step 2: validate. Pass the catalog page-count hint when available.
    int expectedPages = 0;
    if (m_catalog) {
        if (auto entry = m_catalog->entryById(iff.seriesId)) {
            for (const auto& v : entry->volumes) {
                if (v.vol == iff.volumeNumber) { expectedPages = v.pageCount; break; }
            }
        }
    }
    const auto vr = tankoban::manga::premium::PremiumArchiveValidator::validate(
                        partFile, expectedPages);
    if (vr.code != tankoban::manga::premium::ArchiveValidationCode::Ok) {
        const QString quarantineDir = QStandardPaths::writableLocation(
                                          QStandardPaths::AppDataLocation)
                                    + QStringLiteral("/manga_premium_quarantine");
        QDir().mkpath(quarantineDir);
        const QString quarantineName = QStringLiteral("%1_v%2_%3.cbz.bad")
            .arg(iff.seriesId)
            .arg(iff.volumeNumber, 2, 10, QChar('0'))
            .arg(QDateTime::currentMSecsSinceEpoch());
        const QString quarantinePath = quarantineDir + QChar('/') + quarantineName;
        if (!QFile::rename(partFile, quarantinePath)) {
            // Cross-volume case (e.g. AppData on C: but library on D:) or
            // permission denial can fail the move. Never leave a stray
            // .tankoban-part orphaned in the user's library directory:
            // remove it so the library stays clean even though the
            // quarantine evidence is lost in this edge case.
            QFile::remove(partFile);
            QJsonObject moveDetails;
            moveDetails[QStringLiteral("partFile")]       = partFile;
            moveDetails[QStringLiteral("quarantinePath")] = quarantinePath;
            DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
                QStringLiteral("quarantine rename failed; .tankoban-part removed"),
                moveDetails);
        }

        const QString code = [c = vr.code]{
            using C = tankoban::manga::premium::ArchiveValidationCode;
            switch (c) {
                case C::NotCbzExtension:                return QStringLiteral("not_cbz");
                case C::OpenFailed:                     return QStringLiteral("open_failed");
                case C::Empty:                          return QStringLiteral("archive_empty");
                case C::NonImageEntry:                  return QStringLiteral("non_image_entry");
                case C::NestedArchiveEntry:             return QStringLiteral("nested_archive");
                case C::ExecutableEntry:                return QStringLiteral("executable_entry");
                case C::PageCountExceedsBound:          return QStringLiteral("page_count_too_high");
                case C::PageCountMismatchCatalog:       return QStringLiteral("page_count_mismatch");
                case C::DecompressedFirstImageTooLarge: return QStringLiteral("first_image_too_large");
                case C::ReadFailed:                     return QStringLiteral("read_failed");
                case C::Ok:                             return QStringLiteral("ok");
            }
            return QStringLiteral("unknown");
        }();

        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               code, vr.detail);
        emit volumeFailed(iff.seriesId, iff.volumeNumber, code, vr.detail);

        QJsonObject details;
        details[QStringLiteral("quarantinePath")] = quarantinePath;
        details[QStringLiteral("code")]           = code;
        details[QStringLiteral("detail")]         = vr.detail;
        DebugLogBuffer::instance().warning(QString::fromLatin1(kLogSource),
            QStringLiteral("validation failed; quarantined"), details);

        removeFromBucket();
        return;
    }

    // Step 3: atomic rename .tankoban-part -> .cbz.
    if (!QFile::rename(partFile, finalFile)) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("final_rename_failed"),
                               QStringLiteral("could not rename .tankoban-part to .cbz"));
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("final_rename_failed"), QString());
        removeFromBucket();
        return;
    }

    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Completed);

    // Per Codex section 18 step 5: register the volume in the index BEFORE
    // emitting volumeCompleted so UI receivers that consult the index on
    // signal find the row populated. registerVolume creates ONE m_byPath
    // entry serving N m_byChapter entries (one per chapter in the volume).
    if (m_catalog && m_index) {
        if (auto entryOpt = m_catalog->entryById(iff.seriesId)) {
            QStringList chapterIds;
            for (const auto& v : entryOpt->volumes) {
                if (v.vol != iff.volumeNumber) continue;
                for (const auto& ch : v.chapters) {
                    if (!ch.chapterNumber.isEmpty()) chapterIds.append(ch.chapterNumber);
                }
                break;
            }
            if (!chapterIds.isEmpty()) {
                m_index->registerVolume(QString::fromLatin1(kCatalogIdTankoyomiPremium),
                                        iff.seriesId,
                                        iff.volumeNumber,
                                        finalFile,
                                        QFileInfo(finalFile).size(),
                                        chapterIds);
            }
        }
    }

    removeFromBucket();
    emit volumeCompleted(iff.seriesId, iff.volumeNumber, finalFile);

    // TANKOYOMI_PREMIUM Phase 10 -- kick off off-thread cover extraction.
    // Per Codex section 21 cover generation does NOT gate completion; the
    // emit above ships first so the volume is immediately readable, and the
    // cover thumbnail trickles in via volumeCoverReady. The extractor
    // re-walks the archive itself when precomputedImageEntries is empty
    // (Phase 4 validator's result struct goes out of scope above before we
    // reach this kickoff -- documented in PremiumCoverExtractor.h).
    QString coverHint;
    if (m_catalog) {
        if (auto entryOpt = m_catalog->entryById(iff.seriesId)) {
            for (const auto& vv : entryOpt->volumes) {
                if (vv.vol == iff.volumeNumber) {
                    coverHint = vv.coverPageHint;
                    break;
                }
            }
        }
    }
    if (m_coverExtractor) {
        m_coverExtractor->extract(finalFile, iff.seriesId, iff.volumeNumber,
                                  m_coversDir, coverHint, QStringList{});
    }

    QJsonObject details;
    details[QStringLiteral("seriesId")]     = iff.seriesId;
    details[QStringLiteral("volumeNumber")] = iff.volumeNumber;
    details[QStringLiteral("path")]         = finalFile;
    details[QStringLiteral("pageCount")]    = vr.pageCount;
    DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
        QStringLiteral("volume completed"), details);
}

// =====================================================================
// Task 3.6 -- crash-resume + cancel + pause/resume.
// =====================================================================

void TorrentVolumeProvider::replayLedger()
{
    if (!m_ledger || !m_catalog) return;

    const auto pendingStates = {
        TorrentRequest::Status::Pending,
        TorrentRequest::Status::AwaitingMetadata,
        TorrentRequest::Status::Downloading,
        TorrentRequest::Status::Validating,
    };
    for (auto status : pendingStates) {
        for (const auto& row : m_ledger->findByStatus(status)) {
            const auto entryOpt = m_catalog->entryById(row.seriesId);
            if (!entryOpt) {
                m_ledger->updateStatus(requestKey(row.catalogId, row.seriesId, row.volumeNumber),
                                       TorrentRequest::Status::CatalogMissing,
                                       QStringLiteral("catalog_missing"),
                                       QStringLiteral("series no longer in catalog at startup"));
                continue;
            }

            // Find the matching volume.
            std::optional<PremiumVolumeEntry> volOpt;
            for (const auto& v : entryOpt->volumes) {
                if (v.vol == row.volumeNumber) { volOpt = v; break; }
            }
            if (!volOpt) {
                m_ledger->updateStatus(requestKey(row.catalogId, row.seriesId, row.volumeNumber),
                                       TorrentRequest::Status::CatalogMissing,
                                       QStringLiteral("volume_missing"),
                                       QStringLiteral("volume row no longer in catalog at startup"));
                continue;
            }

            Inflight iff;
            iff.requestKey               = requestKey(row.catalogId, row.seriesId, row.volumeNumber);
            iff.catalogId                = row.catalogId;
            iff.seriesId                 = row.seriesId;
            iff.volumeNumber             = row.volumeNumber;
            iff.expectedInfoHash         = row.expectedInfoHash;
            iff.fileIndex                = row.fileIndex;
            iff.cbzFileName              = row.cbzFileName;
            iff.fileSizeBytes            = row.fileSizeBytes;
            iff.pieceStart               = row.pieceStart;
            iff.pieceEnd                 = row.pieceEnd;
            iff.stagingPath              = row.stagingPath;
            iff.canonicalDestinationPath = row.canonicalDestinationPath;
            m_byInfoHash[iff.expectedInfoHash].append(iff);

            ensureTorrentAdded(iff);
        }
    }
    QJsonObject details;
    details[QStringLiteral("torrentCount")] = m_byInfoHash.size();
    DebugLogBuffer::instance().info(QString::fromLatin1(kLogSource),
        QStringLiteral("replayLedger done"), details);
}

void TorrentVolumeProvider::cancelVolume(const QString& seriesId, int volumeNumber, bool deleteStaged)
{
    QString matchedInfoHash;
    QString matchedKey;
    Inflight removed;
    for (auto it = m_byInfoHash.begin(); it != m_byInfoHash.end(); ++it) {
        for (auto& iff : *it) {
            if (iff.seriesId == seriesId && iff.volumeNumber == volumeNumber) {
                matchedInfoHash = it.key();
                matchedKey      = iff.requestKey;
                removed         = iff;
                break;
            }
        }
        if (!matchedInfoHash.isEmpty()) break;
    }

    if (matchedInfoHash.isEmpty()) {
        // Maybe it's not in-flight but is in the ledger. Just update ledger.
        const QString key = requestKey(QString::fromLatin1(kCatalogIdTankoyomiPremium),
                                       seriesId, volumeNumber);
        m_ledger->updateStatus(key, TorrentRequest::Status::Cancelled);
        return;
    }

    // Drop the priority for this volume; recompute union.
    auto& list = m_byInfoHash[matchedInfoHash];
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](const Inflight& x){ return x.requestKey == matchedKey; }),
        list.end());

    if (list.isEmpty()) {
        // No more requests against this torrent. Remove it entirely.
        m_engine->removeTorrent(matchedInfoHash, /*deleteFiles=*/deleteStaged);
        m_byInfoHash.remove(matchedInfoHash);
    } else {
        applyUnionPriorities(matchedInfoHash);
    }

    if (deleteStaged) {
        QFile::remove(removed.stagingPath + QChar('/') + removed.cbzFileName);
    }

    m_ledger->updateStatus(matchedKey, TorrentRequest::Status::Cancelled);
}

void TorrentVolumeProvider::pauseAll()
{
    m_paused = true;
    for (auto it = m_byInfoHash.begin(); it != m_byInfoHash.end(); ++it) {
        m_engine->pauseTorrent(it.key());
    }
}

void TorrentVolumeProvider::resumeAll()
{
    m_paused = false;
    for (auto it = m_byInfoHash.begin(); it != m_byInfoHash.end(); ++it) {
        m_engine->resumeTorrent(it.key());
    }
}

bool TorrentVolumeProvider::isPaused() const
{
    return m_paused;
}

} // namespace tankoban::manga::premium
