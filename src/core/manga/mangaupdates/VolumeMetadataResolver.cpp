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

void VolumeMetadataResolver::resolveBySeriesKey(
    const QString& seriesKey,
    const QString& englishTitle,
    const QStringList& authorsHint)
{
    if (!m_client || seriesKey.isEmpty() || englishTitle.trimmed().isEmpty()) {
        emit unresolvedBySeriesKey(seriesKey, QStringLiteral("invalid-args"));
        return;
    }
    PendingResolve p;
    p.anilistId = 0;
    p.seriesKey = seriesKey;
    p.englishTitle = englishTitle.trimmed();
    p.anilistAuthors = authorsHint;
    // Populate preview.title so MangaUpdatesDisambiguator::bestMatch can
    // perform its title-match step without any changes to onSearchSucceeded.
    p.preview.title = p.englishTitle;

    const int reqId = nextRequestId();
    m_pending.insert(reqId, p);
    m_client->searchByTitle(p.englishTitle, reqId);
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
        if (pending.seriesKey.isEmpty()) {
            emit unresolved(pending.anilistId, QStringLiteral("no disambiguated match"));
        } else {
            emit unresolvedBySeriesKey(pending.seriesKey, QStringLiteral("no disambiguated match"));
        }
        return;
    }

    m_client->seriesById(seriesId, requestId);
}

void VolumeMetadataResolver::onSearchFailed(int requestId, const QString& reason)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const PendingResolve p = it.value();
    m_pending.remove(requestId);
    if (p.seriesKey.isEmpty()) {
        emit unresolved(p.anilistId, QStringLiteral("search failed: %1").arg(reason));
    } else {
        emit unresolvedBySeriesKey(p.seriesKey, QStringLiteral("search failed: %1").arg(reason));
    }
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
        if (pending.seriesKey.isEmpty()) {
            emit unresolved(pending.anilistId,
                QStringLiteral("detail returned volumeCount<=0 (raw: %1)").arg(info.rawStatus));
        } else {
            emit unresolvedBySeriesKey(pending.seriesKey,
                QStringLiteral("detail returned volumeCount<=0 (raw: %1)").arg(info.rawStatus));
        }
        return;
    }

    if (pending.seriesKey.isEmpty()) {
        // Legacy by-anilist path — write through to AniList cache.
        m_cache->putMangaUpdatesSidecar(pending.anilistId, info);
        emit resolved(pending.anilistId, info.volumeCount, info.latestChapter);
    } else {
        emit resolvedBySeriesKey(pending.seriesKey,
                                 info.volumeCount,
                                 info.latestChapter,
                                 info.altTitles);
    }
}

void VolumeMetadataResolver::onSeriesFailed(int requestId, const QString& reason)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const PendingResolve p = it.value();
    m_pending.remove(requestId);
    if (p.seriesKey.isEmpty()) {
        emit unresolved(p.anilistId, QStringLiteral("detail failed: %1").arg(reason));
    } else {
        emit unresolvedBySeriesKey(p.seriesKey, QStringLiteral("detail failed: %1").arg(reason));
    }
}

} // namespace tankoban::manga::mangaupdates
