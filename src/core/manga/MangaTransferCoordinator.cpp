// src/core/manga/MangaTransferCoordinator.cpp
#include "MangaTransferCoordinator.h"
#include "MangaDownloader.h"
#include "TorrentVolumeProvider.h"
#include "WeebCentralVolumePacker.h"

namespace tankoban::manga::premium {

MangaTransferCoordinator::MangaTransferCoordinator(MangaDownloader*                          downloader,
                                                   TorrentVolumeProvider*                    provider,
                                                   tankoban::manga::WeebCentralVolumePacker* packer,
                                                   QObject*                                  parent)
    : QObject(parent), m_downloader(downloader), m_provider(provider), m_packer(packer)
{
}

MangaTransferCoordinator::~MangaTransferCoordinator() = default;

void MangaTransferCoordinator::pauseAll()
{
    if (m_downloader) m_downloader->pauseAll();
    if (m_provider)   m_provider->pauseAll();
    if (m_packer)     m_packer->pauseAll();
    emit pausedChanged(true);
}

void MangaTransferCoordinator::resumeAll()
{
    if (m_downloader) m_downloader->resumeAll();
    if (m_provider)   m_provider->resumeAll();
    if (m_packer)     m_packer->resumeAll();
    emit pausedChanged(false);
}

bool MangaTransferCoordinator::isPaused() const
{
    const bool dPaused  = m_downloader ? m_downloader->isPaused() : false;
    const bool pPaused  = m_provider   ? m_provider->isPaused()   : false;
    const bool wcPaused = m_packer     ? m_packer->isPaused()     : false;
    // Per Phase 11 plan literal: returns true only when all active backends
    // are paused. The "|| !m_packer" half lets the coordinator still report
    // paused if the WC packer was never wired (defensive null-guard).
    return dPaused && pPaused && (wcPaused || !m_packer);
}

void MangaTransferCoordinator::cancelAll()
{
    if (m_downloader) m_downloader->cancelAll();
    // Phase 9+ TODO: TorrentVolumeProvider has no cancelAll today. v1 ships
    // partial coverage; provider-side cancellation is exposed only via
    // cancelVolume(seriesId, volumeNumber) which requires walking the ledger.
}

} // namespace tankoban::manga::premium
