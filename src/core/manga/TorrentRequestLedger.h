// src/core/manga/TorrentRequestLedger.h
//
// TANKOYOMI_PREMIUM Phase 3 -- persistent request ledger.
//
// Records every premium-volume torrent request to disk (one row per
// catalogId / seriesId / volumeNumber triple). The ledger backs crash-
// resume: on startup, TorrentVolumeProvider::replayLedger() walks rows in
// Pending / AwaitingMetadata / Downloading / Validating states and
// re-attaches each torrent by its expectedInfoHash.
//
// JSON-backed (schemaVersion=1), atomic writes via QSaveFile, all mutations
// fan out an off-lock `ledgerChanged()` emit so UI receivers can refresh
// without blocking the mutex. Logging is routed through DebugLogBuffer so
// smoke evidence is surfaced via tankoctl logs.
#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QMutex>
#include <chrono>
#include <optional>

namespace tankoban::manga::premium {

// Per-request state. Persisted to <appData>/manga_premium_requests.json.
// Each request is keyed by (catalogId, seriesId, volumeNumber); the JSON
// stores them as an array where each entry carries the full triple plus the
// state fields. Crash-resume loads this on startup and replays each
// row's intent against the live PremiumCatalog + TorrentEngine.
struct TorrentRequest {
    enum class Status {
        Pending,            // freshly created; awaiting metadata + priorities
        AwaitingMetadata,   // magnet added (upload-only); waiting for metadataReady
        Downloading,        // priorities set; bytes flowing
        Validating,         // bytes done; archive validation in progress
        Completed,          // cbz finalized at canonical path; row stays for audit until garbage-collected
        Failed,             // validation failed OR engine error; surfaced to UI
        Cancelled,          // user-cancelled before completion
        CatalogMissing      // restart found the request but the catalog entry vanished; recoverable
    };

    QString    catalogId;                  // matches PremiumCatalogManifest::id
    QString    seriesId;
    int        volumeNumber       = 0;
    QString    expectedInfoHash;           // lowercase 40-char hex; trust identity
    QString    magnetUri;                  // captured at create time for resume
    int        fileIndex          = -1;
    QString    cbzFileName;
    qint64     fileSizeBytes      = 0;
    int        pieceStart         = -1;
    int        pieceEnd           = -1;
    QString    stagingPath;                // <appData>/manga_premium_staging/<infoHash>/
    QString    canonicalDestinationPath;   // <seriesFolder>/<cbzFileName>.cbz (the final resting place)
    Status     status             = Status::Pending;
    qint64     createdAtMsEpoch   = 0;
    qint64     updatedAtMsEpoch   = 0;
    QString    lastErrorCode;              // populated on Failed; empty otherwise
    QString    lastErrorMessage;
};

inline QString requestKey(const QString& catalogId, const QString& seriesId, int vol)
{
    return catalogId + QChar('/') + seriesId + QChar('/') + QString::number(vol);
}

// Thread-safe JSON-backed ledger. All mutations save the file synchronously
// before returning. The save happens off-lock to keep callers responsive.
class TorrentRequestLedger : public QObject
{
    Q_OBJECT
public:
    explicit TorrentRequestLedger(const QString& filePath, QObject* parent = nullptr);
    ~TorrentRequestLedger() override;

    // Load from disk. Called once at construction; safe to re-call.
    void load();

    // Save to disk. Called after every mutation. Atomic write via .part rename.
    void save();

    // Mutations. Each calls save() after returning.
    void upsert(const TorrentRequest& req);
    void updateStatus(const QString& key,
                      TorrentRequest::Status newStatus,
                      const QString& errorCode = QString(),
                      const QString& errorMessage = QString());
    void remove(const QString& key);

    // Read access (returns copies; safe from any thread).
    std::optional<TorrentRequest> find(const QString& key) const;
    QList<TorrentRequest>         all() const;
    QList<TorrentRequest>         findByInfoHash(const QString& infoHash) const;
    QList<TorrentRequest>         findByStatus(TorrentRequest::Status status) const;

signals:
    // Emitted off-lock after a successful save. UI receivers should connect queued.
    void ledgerChanged();

private:
    void saveLocked();

    QString                              m_filePath;
    mutable QMutex                       m_mutex;
    QHash<QString, TorrentRequest>       m_byKey;
};

} // namespace tankoban::manga::premium
