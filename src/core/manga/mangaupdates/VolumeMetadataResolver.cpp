#include "VolumeMetadataResolver.h"

#include "MangaUpdatesClient.h"
#include "MangaUpdatesDisambiguator.h"
#include "core/manga/anilist/AniListCache.h"

#include <QDateTime>

namespace tankoban::manga::mangaupdates {

namespace {
constexpr qint64 kSidecarMaxAgeMs = 7LL * 24 * 60 * 60 * 1000;
}

VolumeMetadataResolver::VolumeMetadataResolver(
    MangaUpdatesClient* client,
    tankoban::manga::anilist::AniListCache* cache,
    QObject* parent)
    : QObject(parent), m_client(client), m_cache(cache)
{
    if (m_client) {
        connect(m_client.data(), &MangaUpdatesClient::searchSucceeded,
                this, &VolumeMetadataResolver::onSearchSucceeded);
        connect(m_client.data(), &MangaUpdatesClient::searchFailed,
                this, &VolumeMetadataResolver::onSearchFailed);
        connect(m_client.data(), &MangaUpdatesClient::seriesSucceeded,
                this, &VolumeMetadataResolver::onSeriesSucceeded);
        connect(m_client.data(), &MangaUpdatesClient::seriesFailed,
                this, &VolumeMetadataResolver::onSeriesFailed);
    }
}

VolumeMetadataResolver::~VolumeMetadataResolver() = default;

int VolumeMetadataResolver::nextRequestId()
{
    return m_nextRequestId++;
}

bool VolumeMetadataResolver::isSidecarFresh(const MangaUpdatesSeriesInfo& info) const
{
    if (info.fetchedAtMs <= 0) return false;
    return (QDateTime::currentMSecsSinceEpoch() - info.fetchedAtMs) < kSidecarMaxAgeMs;
}

void VolumeMetadataResolver::resolveForAnilist(
    int anilistId,
    const tankoban::manga::anilist::MediaPreview& preview,
    const QStringList& anilistAuthors)
{
    if (anilistId <= 0) {
        emit unresolved(anilistId, QStringLiteral("invalid anilistId"));
        return;
    }
    if (!m_client || !m_cache) {
        emit unresolved(anilistId, QStringLiteral("resolver not wired"));
        return;
    }

    const auto cached = m_cache->getMangaUpdatesSidecar(anilistId);
    if (cached.has_value() && isSidecarFresh(*cached) && cached->volumeCount > 0) {
        emit resolved(anilistId, cached->volumeCount, cached->latestChapter);
        return;
    }

    PendingResolve pending;
    pending.anilistId = anilistId;
    pending.preview = preview;
    pending.anilistAuthors = anilistAuthors;
    const int requestId = nextRequestId();
    m_pending.insert(requestId, pending);
    m_client->searchByTitle(preview.title, requestId);
}

void VolumeMetadataResolver::onSearchSucceeded(
    int requestId,
    const QList<MangaUpdatesSearchHit>& hits)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const PendingResolve pending = it.value();

    const qint64 seriesId = MangaUpdatesDisambiguator::bestMatch(
        hits, pending.preview, pending.anilistAuthors);
    if (seriesId <= 0) {
        m_pending.remove(requestId);
        emit unresolved(pending.anilistId, QStringLiteral("no disambiguated match"));
        return;
    }

    m_client->seriesById(seriesId, requestId);
}

void VolumeMetadataResolver::onSearchFailed(int requestId, const QString& reason)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const int anilistId = it->anilistId;
    m_pending.remove(requestId);
    emit unresolved(anilistId, QStringLiteral("search failed: %1").arg(reason));
}

void VolumeMetadataResolver::onSeriesSucceeded(
    int requestId,
    const MangaUpdatesSeriesInfo& info)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const PendingResolve pending = it.value();
    m_pending.remove(requestId);

    if (info.volumeCount <= 0) {
        emit unresolved(pending.anilistId,
            QStringLiteral("detail returned volumeCount<=0 (raw: %1)").arg(info.rawStatus));
        return;
    }

    m_cache->putMangaUpdatesSidecar(pending.anilistId, info);
    emit resolved(pending.anilistId, info.volumeCount, info.latestChapter);
}

void VolumeMetadataResolver::onSeriesFailed(int requestId, const QString& reason)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const int anilistId = it->anilistId;
    m_pending.remove(requestId);
    emit unresolved(anilistId, QStringLiteral("detail failed: %1").arg(reason));
}

} // namespace tankoban::manga::mangaupdates
