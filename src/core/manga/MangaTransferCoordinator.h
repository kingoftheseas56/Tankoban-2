// src/core/manga/MangaTransferCoordinator.h
#pragma once

#include <QObject>
#include <QPointer>

class MangaDownloader;

namespace tankoban::manga::premium {

class TorrentVolumeProvider;

// Thin facade over MangaDownloader (HTTP-image chapter download) and
// TorrentVolumeProvider (torrent volume download). The UI binds one
// "Transfers paused" affordance to this; both backends respond.
//
// Per Codex section 18 bullet 3.
class MangaTransferCoordinator : public QObject
{
    Q_OBJECT
public:
    MangaTransferCoordinator(MangaDownloader*       downloader,
                             TorrentVolumeProvider* provider,
                             QObject*               parent = nullptr);
    ~MangaTransferCoordinator() override;

    void pauseAll();
    void resumeAll();
    bool isPaused() const;

    // v1 cancelAll targets MangaDownloader only; TorrentVolumeProvider has
    // no cancelAll surface today. Future: walk the provider's m_byInfoHash
    // and call cancelVolume per inflight, or add a provider.cancelAll().
    void cancelAll();

signals:
    void pausedChanged(bool paused);

private:
    QPointer<MangaDownloader>       m_downloader;
    QPointer<TorrentVolumeProvider> m_provider;
};

} // namespace tankoban::manga::premium
