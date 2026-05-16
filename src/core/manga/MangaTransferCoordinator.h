// src/core/manga/MangaTransferCoordinator.h
#pragma once

#include <QObject>
#include <QPointer>

class MangaDownloader;

namespace tankoban::manga {
class WeebCentralVolumePacker;
}

namespace tankoban::manga::premium {

class TorrentVolumeProvider;

// Thin facade over MangaDownloader (HTTP-image chapter download, deprecated
// but still linked for migration), TorrentVolumeProvider (torrent volume
// download), and WeebCentralVolumePacker (WeebCentral HTTP-image volume
// packer). The UI binds one "Transfers paused" affordance to this; all
// three backends respond.
//
// Per Codex section 18 bullet 3; extended in Phase 11 of the
// TANKOYOMI_VOLUME_PIVOT arc to fan out to the WC packer as well.
class MangaTransferCoordinator : public QObject
{
    Q_OBJECT
public:
    MangaTransferCoordinator(MangaDownloader*                       downloader,
                             TorrentVolumeProvider*                 provider,
                             tankoban::manga::WeebCentralVolumePacker* packer  = nullptr,
                             QObject*                               parent = nullptr);
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
    QPointer<MangaDownloader>                          m_downloader;
    QPointer<TorrentVolumeProvider>                    m_provider;
    QPointer<tankoban::manga::WeebCentralVolumePacker> m_packer;
};

} // namespace tankoban::manga::premium
