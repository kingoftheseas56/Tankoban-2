// src/core/manga/mangafire/MangaWeebCentralResolver.cpp
//
// COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1).

#include "MangaWeebCentralResolver.h"

#include "core/manga/WeebCentralScraper.h"
#include "core/manga/mangafire/MangaFireCatalogClient.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QRegularExpression>

#include <algorithm>
#include <memory>

namespace tankoban::manga::mangafire {

namespace {

int chapterNumberToken(const QString& chapterId)
{
    static const QRegularExpression rx(QStringLiteral("(\\d+)"));
    const auto m = rx.match(chapterId);
    if (!m.hasMatch()) return -1;

    bool ok = false;
    const int value = m.captured(1).toInt(&ok);
    return ok ? value : -1;
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
    const QStringList& chapterIds,
    int rangeStart,
    int rangeEnd,
    bool* outIncomplete)
{
    if (outIncomplete) *outIncomplete = false;
    if (chapterIds.isEmpty() || rangeStart <= 0 || rangeEnd < rangeStart) {
        if (outIncomplete) *outIncomplete = true;
        return {};
    }

    QHash<int, QString> byNumber;
    for (const QString& id : chapterIds) {
        const int n = chapterNumberToken(id);
        if (n < rangeStart || n > rangeEnd) continue;
        if (!byNumber.contains(n)) {
            byNumber.insert(n, id);
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
};

MangaWeebCentralResolver::MangaWeebCentralResolver(QNetworkAccessManager* nam,
                                                   QObject* parent)
    : QObject(parent)
    , m_nam(nam)
    , m_scraper(nullptr)
{}

MangaWeebCentralResolver::~MangaWeebCentralResolver() = default;

void MangaWeebCentralResolver::resolve(
    const tankoban::manga::MangaCatalog&,
    int,
    const ResolveKey& key)
{
    emit skip(key, reasonCode(SkipReason::NetworkError));
}

void MangaWeebCentralResolver::stepFetchChapters(PendingResolvePtr) {}
void MangaWeebCentralResolver::filterAndEmit(PendingResolvePtr) {}
void MangaWeebCentralResolver::emitSkip(PendingResolvePtr, SkipReason) {}
void MangaWeebCentralResolver::emitViable(PendingResolvePtr, const QStringList&) {}

} // namespace tankoban::manga::mangafire
