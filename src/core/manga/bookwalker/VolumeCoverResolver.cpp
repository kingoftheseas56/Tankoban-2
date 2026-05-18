// src/core/manga/bookwalker/VolumeCoverResolver.cpp
#include "VolumeCoverResolver.h"

#include "BookWalkerCache.h"
#include "BookWalkerClient.h"
#include "BookWalkerSeriesPageParser.h"
#include "VolumeCoverAlignment.h"

#include "core/manga/PremiumCatalog.h"
#include "core/manga/anilist/AniListCache.h"

#include <QDateTime>
#include <QtGlobal>

namespace tankoban::manga::bookwalker {

VolumeCoverResolver::VolumeCoverResolver(
        BookWalkerClient* bwClient,
        tankoban::manga::anilist::AniListCache* anilistCache,
        tankoban::manga::premium::PremiumCatalog* premium,
        QObject* parent)
    : QObject(parent)
    , m_bwClient(bwClient)
    , m_anilistCache(anilistCache)
    , m_premium(premium)
{
    if (m_bwClient) {
        connect(m_bwClient.data(), &BookWalkerClient::searchSucceeded,
                this, &VolumeCoverResolver::onSearchSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::searchFailed,
                this, &VolumeCoverResolver::onSearchFailed);
        connect(m_bwClient.data(), &BookWalkerClient::coversSucceeded,
                this, &VolumeCoverResolver::onCoversSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::coversFailed,
                this, &VolumeCoverResolver::onCoversFailed);
    }
}

VolumeCoverResolver::~VolumeCoverResolver()
{
    if (m_bwClient) {
        disconnect(m_bwClient.data(), nullptr, this, nullptr);
    }
}

int VolumeCoverResolver::nextRequestId() { return m_nextRequestId++; }

void VolumeCoverResolver::emitFromCache(int anilistId, const BookWalkerCacheRecord& rec)
{
    QMap<int, QString> m;
    for (const auto& e : rec.volumes) m.insert(e.volume, e.url);
    emit resolved(anilistId, m);
}

void VolumeCoverResolver::serveCachedOrFallback(int anilistId,
                                                int /*canonicalCount*/,
                                                const QString& failureReason)
{
    // Degradation per spec section 6: AniList/MangaUpdates unreachable BUT a
    // fresh cache exists -> serve cached covers, skip drift check (pass 0 so
    // BookWalkerCache::load skips the count-drift guard).
    auto cached = BookWalkerCache::load(anilistId, 0);
    if (cached) {
        emitFromCache(anilistId, *cached);
        return;
    }
    emit unresolved(anilistId, failureReason);
}

void VolumeCoverResolver::resolveForAnilist(int anilistId)
{
    // Step 1: Premium short-circuit.
    // Spec Decision #5: BookWalker is never consulted for Premium series;
    // curated Premium covers are handled by PremiumCoverExtractor upstream.
    if (m_premium && m_premium->hasPremiumEntry(anilistId)) {
        // Premium-curated covers come via PremiumCoverExtractor in the existing pipeline.
        // BookWalker explicitly does not run for Premium series (spec Decision #5).
        // Emit `skipped` (not `unresolved`) so the caller can route to its Premium path
        // without conflating with BookWalker-failure fallback.
        emit skipped(anilistId, QStringLiteral("premium-short-circuit"));
        return;
    }

    // Step 2: AniList alt-title (Japanese).
    // japaneseTitleFor scans alternateTitles for a CJK-script entry; returns
    // empty if the series is not cached in AniListCache yet.
    QString japaneseTitle;
    if (m_anilistCache) japaneseTitle = m_anilistCache->japaneseTitleFor(anilistId);
    if (japaneseTitle.isEmpty()) {
        serveCachedOrFallback(anilistId, 0,
                              QStringLiteral("no-japanese-title"));
        return;
    }

    // Step 3: MangaUpdates volume count via AniListCache sidecar.
    // The sidecar is warmed by VolumeMetadataResolver; we read it here so
    // VolumeCoverResolver has no live network dependency on MangaUpdatesClient.
    // canonicalCount = 0 is the degraded path; VolumeCoverAlignment handles it
    // by mapping all raw URLs sequentially starting at volume 1.
    int canonicalCount = 0;
    if (m_anilistCache) {
        auto sidecar = m_anilistCache->getMangaUpdatesSidecar(anilistId);
        if (sidecar.has_value()) canonicalCount = sidecar->volumeCount;
    }

    // Step 4: Cache check (TTL + drift guard in BookWalkerCache::load).
    auto cached = BookWalkerCache::load(anilistId, canonicalCount);
    if (cached) {
        emitFromCache(anilistId, *cached);
        return;
    }

    // Step 5: BookWalker search.
    if (!m_bwClient) {
        emit unresolved(anilistId, QStringLiteral("bookwalker-client-null"));
        return;
    }
    PendingResolve p;
    p.anilistId      = anilistId;
    p.japaneseTitle  = japaneseTitle;
    p.canonicalCount = canonicalCount;
    const int reqId = nextRequestId();
    m_pending.insert(reqId, p);
    m_bwClient->searchSeries(japaneseTitle, reqId);
}

void VolumeCoverResolver::onSearchSucceeded(int requestId,
                                            const QList<BookWalkerSearchHit>& hits)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    PendingResolve p = it.value();

    // Step 6: Disambiguate search hits by exact title match (strips parenthetical
    // publisher suffix per BookWalkerSeriesPageParser::pickSeriesIdByTitle).
    const QString seriesId = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, p.japaneseTitle);
    if (seriesId.isEmpty()) {
        m_pending.erase(it);
        emit unresolved(p.anilistId, QStringLiteral("series-not-on-bookwalker"));
        return;
    }
    p.bookwalkerSeriesId = seriesId;
    it.value() = p;

    // Step 7: Fetch ordered cover URLs from the series page.
    m_bwClient->fetchSeriesCovers(seriesId, requestId);
}

void VolumeCoverResolver::onSearchFailed(int requestId, const QString& reason)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const PendingResolve p = it.value();
    m_pending.erase(it);
    emit unresolved(p.anilistId, QStringLiteral("search-failed: ") + reason);
}

void VolumeCoverResolver::onCoversSucceeded(int requestId,
                                            const QList<QString>& orderedCoverUrls)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const PendingResolve p = it.value();
    m_pending.erase(it);

    // Step 8: Index alignment (maps raw ordered URLs to 1-based volume numbers).
    QMap<int, QString> aligned = VolumeCoverAlignment::align(
        orderedCoverUrls, p.canonicalCount);
    if (aligned.isEmpty()) {
        emit unresolved(p.anilistId, QStringLiteral("alignment-empty"));
        return;
    }

    // Step 9: Cache write. Disk-write failure is soft (log-and-continue).
    BookWalkerCacheRecord rec;
    rec.schemaVersion     = 1;
    rec.fetchedAt         = QDateTime::currentDateTimeUtc();
    rec.canonicalCount    = (p.canonicalCount > 0 ? p.canonicalCount
                                                   : aligned.size());
    rec.bookwalkerSeriesId = p.bookwalkerSeriesId;
    for (auto k = aligned.constBegin(); k != aligned.constEnd(); ++k) {
        BookWalkerCoverEntry e;
        e.volume = k.key();
        e.url    = k.value();
        rec.volumes.append(e);
    }
    if (!BookWalkerCache::store(p.anilistId, rec)) {
        qWarning("VolumeCoverResolver: failed to persist BookWalker cache for anilistId=%d", p.anilistId);
    }

    // Step 10: Emit.
    emit resolved(p.anilistId, aligned);
}

void VolumeCoverResolver::onCoversFailed(int requestId, const QString& reason)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const PendingResolve p = it.value();
    m_pending.erase(it);
    emit unresolved(p.anilistId, QStringLiteral("covers-failed: ") + reason);
}

} // namespace tankoban::manga::bookwalker
