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
// Concurrency: GetComicsResolver is shared (one instance). Concurrent
// requestVolume() calls are correlated via a QHash<ReqKey,ReqState> keyed by
// (seriesId, volumeNumber). The resolver's resolved/resolveFailed signals carry
// no per-call identity, so we use a FIFO queue: each requestVolume() appends to
// m_pendingKeys; the resolver's slot always dequeues the front entry. This
// means concurrent requests for DIFFERENT volumes are safe (queue order matches
// resolve dispatch order because GetComicsResolver serialises HTTP internally —
// it processes one request at a time and emits before accepting the next). If
// two concurrent requests for the SAME (seriesId,vol) arrive, the second is
// no-oped with a volumeFailed rather than overwriting the first in-flight state.
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
        QString editionTitle;
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

    // editionTitle/year/tier come from the Western edition; destinationPath is
    // the per-series comics folder (guaranteed non-empty by caller per Agent 4
    // contract). coverUrl from resolve is forwarded via coverReady so the UI
    // can paint the per-edition cover.
    void requestVolume(const QString& seriesId,
                       int            volumeNumber,
                       const QString& editionTitle,
                       int            year,
                       const QString& tierLabel,
                       const QString& destinationPath);

signals:
    void volumeProgress(const QString& seriesId, int volumeNumber, int percent);
    void volumeCompleted(const QString& seriesId, int volumeNumber, const QString& cbzPath);
    void volumeFailed(const QString& seriesId, int volumeNumber, const QString& reason);
    void coverReady(const QString& seriesId, int volumeNumber, const QString& coverUrl);

private slots:
    void onResolved(const tankoban::manga::EditionDownload& dl);
    void onResolveFailed(const QString& reason);
    void onTorrentUpdated(const QString& infoHash);
    void onTorrentCompleted(const QString& infoHash);

private:
    // Locate the best .cbz/.cbr archive under a directory (recursive).
    // Returns empty string if nothing found.
    static QString findArchiveIn(const QString& dirPath);

    // Start the next DDL link for the given request. Assumes state is in
    // m_states and remainingLinks is non-empty.
    void tryNextDdlLink(ReqState& state);

    QNetworkAccessManager* m_nam     = nullptr;
    TorrentClient*         m_torrent = nullptr;
    GetComicsResolver      m_resolver;

    // FIFO queue of keys in the order requestVolume() was called (one entry
    // per pending resolve call; dequeued by onResolved/onResolveFailed).
    QList<ReqKey>            m_pendingKeys;

    // Full state for every in-flight request, keyed by stateKey(seriesId,vol).
    // Entries inserted in requestVolume(), removed on terminate.
    QHash<QString, ReqState> m_states;

    // infoHash -> ReqKey for the magnet path (torrentCompleted correlation).
    QHash<QString, ReqKey>   m_hashToKey;
};

} // namespace tankoban::manga
