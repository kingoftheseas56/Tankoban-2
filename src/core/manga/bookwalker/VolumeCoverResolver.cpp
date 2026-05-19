// src/core/manga/bookwalker/VolumeCoverResolver.cpp
#include "VolumeCoverResolver.h"

#include "BookWalkerCache.h"
#include "BookWalkerClient.h"
#include "BookWalkerSeriesPageParser.h"
#include "VolumeCoverAlignment.h"

#include "core/manga/PremiumCatalog.h"
#include "core/manga/mangaupdates/JapaneseTitlePicker.h"
#include "core/manga/mangaupdates/VolumeMetadataResolver.h"

#include <QDateTime>
#include <QtGlobal>

namespace tankoban::manga::bookwalker {

VolumeCoverResolver::VolumeCoverResolver(
        BookWalkerClient* bwClient,
        tankoban::manga::mangaupdates::VolumeMetadataResolver* muResolver,
        tankoban::manga::premium::PremiumCatalog* premium,
        QObject* parent)
    : QObject(parent), m_bwClient(bwClient), m_muResolver(muResolver), m_premium(premium)
{
    if (m_muResolver) {
        connect(m_muResolver.data(),
                &tankoban::manga::mangaupdates::VolumeMetadataResolver::resolvedBySeriesKey,
                this, &VolumeCoverResolver::onMuResolvedBySeriesKey);
        connect(m_muResolver.data(),
                &tankoban::manga::mangaupdates::VolumeMetadataResolver::unresolvedBySeriesKey,
                this, &VolumeCoverResolver::onMuUnresolvedBySeriesKey);
    }
    if (m_bwClient) {
        connect(m_bwClient.data(), &BookWalkerClient::searchSucceeded,
                this, &VolumeCoverResolver::onBwSearchSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::searchFailed,
                this, &VolumeCoverResolver::onBwSearchFailed);
        connect(m_bwClient.data(), &BookWalkerClient::coversSucceeded,
                this, &VolumeCoverResolver::onBwCoversSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::coversFailed,
                this, &VolumeCoverResolver::onBwCoversFailed);
    }
}

VolumeCoverResolver::~VolumeCoverResolver()
{
    if (m_muResolver) disconnect(m_muResolver.data(), nullptr, this, nullptr);
    if (m_bwClient) disconnect(m_bwClient.data(), nullptr, this, nullptr);
}

void VolumeCoverResolver::resolveForSeries(const QString& seriesKey,
                                           const QString& englishTitle,
                                           int anilistIdOptional)
{
    if (seriesKey.isEmpty()) {
        emit unresolved(QString(), QStringLiteral("empty-series-key"));
        return;
    }

    // Premium short-circuit
    if (m_premium && anilistIdOptional > 0 && m_premium->hasPremiumEntry(anilistIdOptional)) {
        emit skipped(seriesKey, QStringLiteral("premium-short-circuit"));
        return;
    }

    // Cache check
    auto cached = BookWalkerCache::loadByKey(seriesKey, /*currentCanonicalCount=*/0);
    if (cached) {
        QMap<int, QString> m;
        for (const auto& e : cached->volumes) m.insert(e.volume, e.url);
        emit resolved(seriesKey, m);
        return;
    }

    if (!m_muResolver) {
        emit unresolved(seriesKey, QStringLiteral("mu-resolver-null"));
        return;
    }

    PendingResolve p;
    p.seriesKey          = seriesKey;
    p.englishTitle       = englishTitle;
    p.anilistIdOptional  = anilistIdOptional;
    m_pendingBySeriesKey.insert(seriesKey, p);

    m_muResolver->resolveBySeriesKey(seriesKey, englishTitle);
}

void VolumeCoverResolver::onMuResolvedBySeriesKey(const QString& seriesKey,
                                                  int volumeCount,
                                                  int /*chapterCount*/,
                                                  const QStringList& altTitles)
{
    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    PendingResolve p = it.value();
    p.canonicalCount = volumeCount;
    p.japaneseTitle = tankoban::manga::mangaupdates::JapaneseTitlePicker::pickFirstJapanese(altTitles);
    it.value() = p;

    if (p.japaneseTitle.isEmpty()) {
        m_pendingBySeriesKey.erase(it);
        emit unresolved(seriesKey, QStringLiteral("no-japanese-title"));
        return;
    }

    if (!m_bwClient) {
        m_pendingBySeriesKey.erase(it);
        emit unresolved(seriesKey, QStringLiteral("bw-client-null"));
        return;
    }

    const int bwReqId = m_nextBwRequestId++;
    m_bwRequestIdToSeriesKey.insert(bwReqId, seriesKey);
    m_bwClient->searchSeries(p.japaneseTitle, bwReqId);
}

void VolumeCoverResolver::onMuUnresolvedBySeriesKey(const QString& seriesKey,
                                                    const QString& reason)
{
    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    m_pendingBySeriesKey.erase(it);
    emit unresolved(seriesKey, QStringLiteral("mu-unresolved: ") + reason);
}

void VolumeCoverResolver::onBwSearchSucceeded(int requestId,
                                              const QList<BookWalkerSearchHit>& hits)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    PendingResolve p = it.value();

    const QString bwSeriesId = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, p.japaneseTitle);
    if (bwSeriesId.isEmpty()) {
        m_pendingBySeriesKey.erase(it);
        emit unresolved(seriesKey, QStringLiteral("series-not-on-bookwalker"));
        return;
    }
    p.bookwalkerSeriesId = bwSeriesId;
    it.value() = p;

    const int bwReqId = m_nextBwRequestId++;
    m_bwRequestIdToSeriesKey.insert(bwReqId, seriesKey);
    m_bwClient->fetchSeriesCovers(bwSeriesId, bwReqId);
}

void VolumeCoverResolver::onBwSearchFailed(int requestId, const QString& reason)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    m_pendingBySeriesKey.remove(seriesKey);
    emit unresolved(seriesKey, QStringLiteral("bw-search-failed: ") + reason);
}

void VolumeCoverResolver::onBwCoversSucceeded(int requestId,
                                              const QList<QString>& orderedCoverUrls)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    const PendingResolve p = it.value();
    m_pendingBySeriesKey.erase(it);

    auto aligned = VolumeCoverAlignment::align(orderedCoverUrls, p.canonicalCount);
    if (aligned.isEmpty()) {
        emit unresolved(seriesKey, QStringLiteral("alignment-empty"));
        return;
    }

    BookWalkerCacheRecord rec;
    rec.schemaVersion     = 1;
    rec.fetchedAt         = QDateTime::currentDateTimeUtc();
    rec.canonicalCount    = (p.canonicalCount > 0 ? p.canonicalCount : aligned.size());
    rec.bookwalkerSeriesId = p.bookwalkerSeriesId;
    for (auto k = aligned.constBegin(); k != aligned.constEnd(); ++k) {
        BookWalkerCoverEntry e;
        e.volume = k.key();
        e.url    = k.value();
        rec.volumes.append(e);
    }
    if (!BookWalkerCache::storeByKey(seriesKey, rec)) {
        qWarning("VolumeCoverResolver: failed to persist BookWalker cache for seriesKey=%s",
                 qUtf8Printable(seriesKey));
    }

    emit resolved(seriesKey, aligned);
}

void VolumeCoverResolver::onBwCoversFailed(int requestId, const QString& reason)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    m_pendingBySeriesKey.remove(seriesKey);
    emit unresolved(seriesKey, QStringLiteral("bw-covers-failed: ") + reason);
}

} // namespace tankoban::manga::bookwalker
