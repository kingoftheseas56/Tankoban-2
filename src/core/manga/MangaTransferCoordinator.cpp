// src/core/manga/MangaTransferCoordinator.cpp
#include "MangaTransferCoordinator.h"
#include "MangaDownloader.h"
#include "TorrentVolumeProvider.h"

namespace tankoban::manga::premium {

MangaTransferCoordinator::MangaTransferCoordinator(MangaDownloader*       downloader,
                                                   TorrentVolumeProvider* provider,
                                                   QObject*               parent)
    : QObject(parent), m_downloader(downloader), m_provider(provider)
{
}

MangaTransferCoordinator::~MangaTransferCoordinator() = default;

void MangaTransferCoordinator::pauseAll()
{
    if (m_downloader) m_downloader->pauseAll();
    if (m_provider)   m_provider->pauseAll();
    emit pausedChanged(true);
}

void MangaTransferCoordinator::resumeAll()
{
    if (m_downloader) m_downloader->resumeAll();
    if (m_provider)   m_provider->resumeAll();
    emit pausedChanged(false);
}

bool MangaTransferCoordinator::isPaused() const
{
    const bool dPaused = m_downloader ? m_downloader->isPaused() : false;
    const bool pPaused = m_provider   ? m_provider->isPaused()   : false;
    return dPaused && pPaused;
}

void MangaTransferCoordinator::cancelAll()
{
    if (m_downloader) m_downloader->cancelAll();
    // Phase 9+ TODO: TorrentVolumeProvider has no cancelAll today. v1 ships
    // partial coverage; provider-side cancellation is exposed only via
    // cancelVolume(seriesId, volumeNumber) which requires walking the ledger.
}

} // namespace tankoban::manga::premium
