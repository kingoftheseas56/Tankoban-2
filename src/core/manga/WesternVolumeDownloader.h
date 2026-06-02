// src/core/manga/WesternVolumeDownloader.h
#pragma once

#include "GetComicsResolver.h"
#include <QObject>
#include <QString>
#include <QHash>
#include <QTimer>

class QNetworkAccessManager;
class TorrentClient;

namespace tankoban::net { class HttpFileDownloader; }

namespace tankoban::manga {

// Western edition download provider. Mirrors TorrentVolumeProvider's signal
// shape so ComicsPage::onProviderVolumeCompleted reuses it verbatim.
//
// Concurrency (Codex review 2026-06-02): GetComicsResolver is shared and does
// NOT serialise — concurrent resolve() calls can complete OUT OF ORDER and its
// resolved/resolveFailed signals carry no per-call identity. So we SERIALISE the
// resolve step ourselves: at most one resolve is in flight (m_resolveInFlight);
// requestVolume() enqueues onto m_resolveQueue and pumpResolveQueue() starts the
// next only after the prior resolved/failed arrives. The in-flight request is
// m_activeResolveKey, so the resolver slot is unambiguous (no FIFO mis-attach).
// DOWNLOADS (magnet/DDL) after resolve still run concurrently — correlated by
// infoHash (magnet) or per-downloader lambda (DDL). A duplicate (seriesId,vol)
// request is rejected; a duplicate active infoHash is rejected.
class WesternVolumeDownloader : public QObject {
    Q_OBJECT
public:
    // Identifies one in-flight request. Public so the FIFO list and hash-map
    // members (which are private data) can use it without access issues.
    struct ReqKey {
        QString seriesId;
        int     volumeNumber = 0;
        bool operator==(const ReqKey& o) const {
            return seriesId == o.seriesId && volumeNumber == o.volumeNumber;
        }
    };

    struct ReqState {
        ReqKey  key;
        QString seriesTitle;       // search subject for GetComics (collected-edition match)
        int     year = 0;          // resolve args stashed so pumpResolveQueue can fire later
        QString tierLabel;
        QString destinationPath;
        // For magnet path:
        QString infoHash;
        // For DDL path:
        QList<tankoban::manga::getcomics::DownloadLink> remainingLinks;
        int     ddlLinkIndex = 0;  // next index into remainingLinks to try
    };

    WesternVolumeDownloader(QNetworkAccessManager* nam,
                            TorrentClient*         torrent,
                            QObject*               parent = nullptr);
    ~WesternVolumeDownloader() override;

    // seriesTitle is the SERIES name (the GetComics search subject — collected
    // editions are matched on series identity, not the per-TPB label); year/tier
    // are match hints; destinationPath is the per-series comics folder
    // (guaranteed non-empty by caller per Agent 4 contract). coverUrl from
    // resolve is forwarded via coverReady so the UI can paint the edition cover.
    void requestVolume(const QString& seriesId,
                       int            volumeNumber,
                       const QString& seriesTitle,
                       int            year,
                       const QString& tierLabel,
                       const QString& destinationPath);

signals:
    void volumeProgress(const QString& seriesId, int volumeNumber, int percent);
    void volumeCompleted(const QString& seriesId, int volumeNumber, const QString& cbzPath);
    void volumeFailed(const QString& seriesId, int volumeNumber, const QString& reason);
    void coverReady(const QString& seriesId, int volumeNumber, const QString& coverUrl);
    // The collected edition we matched on GetComics (e.g. "Invincible Compendium
    // Vol. 1 - 3") — emitted once resolve confirms a download, so the UI can show
    // EXACTLY what is being fetched (honest about the collected-edition unit).
    void volumeResolved(const QString& seriesId, int volumeNumber, const QString& editionTitle);

private slots:
    void onResolved(const tankoban::manga::EditionDownload& dl);
    void onResolveFailed(const QString& reason);
    void onTorrentUpdated(const QString& infoHash);
    void onTorrentCompleted(const QString& infoHash);

private:
    // Locate the best .cbz/.cbr archive under a directory (recursive).
    // Returns empty string if nothing found.
    static QString findArchiveIn(const QString& dirPath);

    // Start the next DDL link for the given request (operates BY KEY and copies
    // emit data before any erase — never holds a ReqState& across removal).
    void tryNextDdlLink(const QString& key);

    // Start the next queued resolve if none is in flight (serialises resolves).
    void pumpResolveQueue();

    QNetworkAccessManager* m_nam     = nullptr;
    TorrentClient*         m_torrent = nullptr;
    GetComicsResolver      m_resolver;

    // Serialised resolve step: at most one resolve in flight. m_resolveQueue
    // holds keys awaiting resolve; m_activeResolveKey is the one in flight.
    QList<ReqKey>            m_resolveQueue;
    ReqKey                   m_activeResolveKey;
    bool                     m_resolveInFlight = false;

    // Full state for every in-flight request, keyed by stateKey(seriesId,vol).
    // Entries inserted in requestVolume(), removed on terminate.
    QHash<QString, ReqState> m_states;

    // infoHash -> ReqKey for the magnet path (torrentCompleted correlation).
    QHash<QString, ReqKey>   m_hashToKey;
};

} // namespace tankoban::manga
