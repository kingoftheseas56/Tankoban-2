// src/core/manga/mangafire/MangaWeebCentralResolver.cpp
//
// COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1).

#include "MangaWeebCentralResolver.h"

#include "core/manga/MangaScraper.h"
#include "core/manga/MangaResult.h"
#include "core/manga/WeebCentralScraper.h"
#include "core/manga/mangafire/MangaFireCatalogClient.h"

#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace tankoban::manga::mangafire {

namespace {

int normalizedChapterNumber(double value)
{
    if (value <= 0.0) return -1;
    const double rounded = std::round(value);
    if (std::abs(value - rounded) > 0.0001) return -1;
    if (rounded > static_cast<double>(std::numeric_limits<int>::max())) return -1;
    return static_cast<int>(rounded);
}

} // namespace

QString MangaWeebCentralResolver::reasonCode(SkipReason reason)
{
    switch (reason) {
    case SkipReason::NoSeriesMatch: return QStringLiteral("NoSeriesMatch");
    case SkipReason::NoChapterOverlap: return QStringLiteral("NoChapterOverlap");
    case SkipReason::IncompleteCoverage: return QStringLiteral("IncompleteCoverage");
    case SkipReason::NetworkError: return QStringLiteral("NetworkError");
    }
    return QStringLiteral("Unknown");
}

QStringList MangaWeebCentralResolver::filterChaptersToRange(
    const QList<ChapterRef>& chapters,
    int rangeStart,
    int rangeEnd,
    bool* outIncomplete)
{
    if (outIncomplete) *outIncomplete = false;
    if (chapters.isEmpty() || rangeStart <= 0 || rangeEnd < rangeStart) {
        if (outIncomplete) *outIncomplete = true;
        return {};
    }

    QHash<int, QString> byNumber;
    for (const ChapterRef& chapter : chapters) {
        const int n = chapter.number;
        if (n < rangeStart || n > rangeEnd) continue;
        if (!byNumber.contains(n)) {
            byNumber.insert(n, chapter.id);
        }
    }

    for (int n = rangeStart; n <= rangeEnd; ++n) {
        if (!byNumber.contains(n)) {
            if (outIncomplete) *outIncomplete = true;
            return {};
        }
    }

    QStringList out;
    out.reserve(rangeEnd - rangeStart + 1);
    for (int n = rangeStart; n <= rangeEnd; ++n) {
        out.append(byNumber.value(n));
    }
    return out;
}

struct MangaWeebCentralResolver::PendingResolve {
    ResolveKey key;
    int chapterRangeStart = 0;
    int chapterRangeEnd = 0;
    QString seriesTitle;
    QString mangaFireSeriesId;
    QString cachedWcSeriesId;
};

MangaWeebCentralResolver::MangaWeebCentralResolver(QNetworkAccessManager* nam,
                                                   QObject* parent)
    : QObject(parent)
    , m_nam(nam)
    , m_scraper(new WeebCentralScraper(nam, this))
{
    connect(m_scraper, &MangaScraper::searchFinished,
            this, [this](const QList<MangaResult>& results) {
        if (m_inflightSearch.isEmpty()) return;
        const QString mangaFireSeriesId = m_inflightSearch;
        m_inflightSearch.clear();

        auto queued = m_pendingByMangafireSeriesId.value(mangaFireSeriesId);
        if (queued.isEmpty()) return;

        if (results.isEmpty() || results.first().id.isEmpty()) {
            m_pendingByMangafireSeriesId.remove(mangaFireSeriesId);
            for (const auto& pending : queued) {
                emitSkip(pending, SkipReason::NoSeriesMatch);
            }
            return;
        }

        const QString wcSeriesId = results.first().id;
        tankoban::manga::WeebCentralCacheBlock block;
        block.seriesId = wcSeriesId;
        block.chaptersFetchedAt = QDateTime::currentDateTimeUtc();
        MangaFireCatalogClient::patchWeebCentralBlock(mangaFireSeriesId, block);

        auto& liveQueue = m_pendingByMangafireSeriesId[mangaFireSeriesId];
        for (const auto& pending : liveQueue) {
            pending->cachedWcSeriesId = wcSeriesId;
        }
        if (!liveQueue.isEmpty()) {
            stepFetchChapters(liveQueue.first());
        }
    });

    connect(m_scraper, &MangaScraper::chaptersReady,
            this, [this](const QList<ChapterInfo>& chapters) {
        if (m_inflightFetch.isEmpty()) return;
        const QString wcSeriesId = m_inflightFetch;
        m_inflightFetch.clear();

        QList<ChapterRef> chapterRefs;
        chapterRefs.reserve(chapters.size());
        for (const auto& ch : chapters) {
            const int chapterNumber = normalizedChapterNumber(ch.chapterNumber);
            if (chapterNumber > 0 && !ch.id.isEmpty()) {
                chapterRefs.append(ChapterRef{chapterNumber, ch.id});
            }
        }
        m_chapterCache.insert(wcSeriesId, chapterRefs);

        QList<PendingResolvePtr> ready;
        for (auto it = m_pendingByMangafireSeriesId.begin();
             it != m_pendingByMangafireSeriesId.end(); ) {
            QList<PendingResolvePtr> stillPending;
            for (const auto& pending : it.value()) {
                if (pending->cachedWcSeriesId == wcSeriesId) {
                    ready.append(pending);
                } else {
                    stillPending.append(pending);
                }
            }

            if (stillPending.isEmpty()) {
                it = m_pendingByMangafireSeriesId.erase(it);
            } else {
                it.value() = stillPending;
                ++it;
            }
        }

        for (const auto& pending : ready) {
            filterAndEmit(pending);
        }
    });

    connect(m_scraper, &MangaScraper::errorOccurred,
            this, [this](const QString&) {
        const auto allPending = m_pendingByMangafireSeriesId;
        m_pendingByMangafireSeriesId.clear();
        m_inflightSearch.clear();
        m_inflightFetch.clear();

        for (const auto& queue : allPending) {
            for (const auto& pending : queue) {
                emitSkip(pending, SkipReason::NetworkError);
            }
        }
    });
}

MangaWeebCentralResolver::~MangaWeebCentralResolver() = default;

void MangaWeebCentralResolver::resolve(
    const tankoban::manga::MangaCatalog& catalog,
    int volumeNumber,
    const ResolveKey& key)
{
    int rangeStart = 0;
    int rangeEnd = 0;
    for (const auto& volume : catalog.volumes) {
        if (volume.volumeNumber == volumeNumber) {
            rangeStart = volume.chapterRangeStart;
            rangeEnd = volume.chapterRangeEnd;
            break;
        }
    }

    if (rangeStart <= 0 || rangeEnd < rangeStart) {
        emit skip(key, reasonCode(SkipReason::NoChapterOverlap));
        return;
    }

    auto pending = std::make_shared<PendingResolve>();
    pending->key = key;
    pending->chapterRangeStart = rangeStart;
    pending->chapterRangeEnd = rangeEnd;
    pending->seriesTitle = catalog.seriesTitle;
    pending->mangaFireSeriesId = catalog.seriesId;
    pending->cachedWcSeriesId = catalog.weebCentral.seriesId;

    if (!pending->cachedWcSeriesId.isEmpty()
        && m_chapterCache.contains(pending->cachedWcSeriesId)) {
        filterAndEmit(pending);
        return;
    }

    m_pendingByMangafireSeriesId[catalog.seriesId].append(pending);
    if (m_pendingByMangafireSeriesId[catalog.seriesId].size() > 1) {
        return;
    }

    if (pending->cachedWcSeriesId.isEmpty()) {
        if (!m_scraper || !m_inflightSearch.isEmpty()) {
            m_pendingByMangafireSeriesId.remove(catalog.seriesId);
            emitSkip(pending, SkipReason::NetworkError);
            return;
        }
        m_inflightSearch = catalog.seriesId;
        m_scraper->search(pending->seriesTitle, 1);
        return;
    }

    stepFetchChapters(pending);
}

void MangaWeebCentralResolver::stepFetchChapters(PendingResolvePtr pending)
{
    const QString wcSeriesId = pending->cachedWcSeriesId;
    if (wcSeriesId.isEmpty()) {
        m_pendingByMangafireSeriesId.remove(pending->mangaFireSeriesId);
        emitSkip(pending, SkipReason::NoSeriesMatch);
        return;
    }

    if (m_chapterCache.contains(wcSeriesId)) {
        const auto queued =
            m_pendingByMangafireSeriesId.take(pending->mangaFireSeriesId);
        if (queued.isEmpty()) {
            filterAndEmit(pending);
            return;
        }
        for (const auto& item : queued) {
            filterAndEmit(item);
        }
        return;
    }

    if (!m_scraper || !m_inflightFetch.isEmpty()) {
        m_pendingByMangafireSeriesId.remove(pending->mangaFireSeriesId);
        emitSkip(pending, SkipReason::NetworkError);
        return;
    }

    m_inflightFetch = wcSeriesId;
    m_scraper->fetchChapters(wcSeriesId);
}

void MangaWeebCentralResolver::filterAndEmit(PendingResolvePtr pending)
{
    const auto it = m_chapterCache.constFind(pending->cachedWcSeriesId);
    if (it == m_chapterCache.constEnd() || it.value().isEmpty()) {
        emitSkip(pending, SkipReason::NoChapterOverlap);
        return;
    }

    bool hasOverlap = false;
    for (const ChapterRef& chapter : it.value()) {
        const int n = chapter.number;
        if (n >= pending->chapterRangeStart && n <= pending->chapterRangeEnd) {
            hasOverlap = true;
            break;
        }
    }
    if (!hasOverlap) {
        emitSkip(pending, SkipReason::NoChapterOverlap);
        return;
    }

    bool incomplete = false;
    const QStringList chapterIds = filterChaptersToRange(
        it.value(),
        pending->chapterRangeStart,
        pending->chapterRangeEnd,
        &incomplete);

    if (chapterIds.isEmpty()) {
        emitSkip(pending, incomplete ? SkipReason::IncompleteCoverage
                                     : SkipReason::NoChapterOverlap);
        return;
    }

    emitViable(pending, chapterIds);
}

void MangaWeebCentralResolver::emitSkip(PendingResolvePtr pending, SkipReason reason)
{
    if (!pending) return;
    emit skip(pending->key, reasonCode(reason));
}

void MangaWeebCentralResolver::emitViable(PendingResolvePtr pending,
                                          const QStringList& chapterIds)
{
    if (!pending) return;
    emit viable(pending->key, chapterIds);
}

} // namespace tankoban::manga::mangafire
