// src/core/manga/FallbackChainResolver.cpp

#include "FallbackChainResolver.h"

#include "core/manga/fandom/FandomCatalogCache.h"
#include "core/manga/fandom/FandomVolumeResolver.h"
#include "core/manga/wikipedia/WikipediaResolver.h"

#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcFallback, "tankoban.manga.fallback")

namespace tankoban::manga {

namespace {

using tankoban::manga::fandom::FandomCatalog;
using tankoban::manga::fandom::FandomVolume;
using tankoban::manga::fandom::kFandomCatalogSchemaVersion;

// Pick the first non-empty QString in (a, b). For paired-source fallback.
QString pickNonEmpty(const QString& a, const QString& b)
{
    return a.isEmpty() ? b : a;
}

// Pick the first valid QDate in (a, b).
QDate pickValid(const QDate& a, const QDate& b)
{
    return a.isValid() ? a : b;
}

} // anonymous

FallbackChainResolver::FallbackChainResolver(
    fandom::FandomVolumeResolver* fandomResolver,
    wikipedia::WikipediaResolver* wikipediaResolver,
    QObject* parent)
    : QObject(parent), m_fandom(fandomResolver), m_wikipedia(wikipediaResolver)
{
    if (m_fandom) {
        connect(m_fandom, &fandom::FandomVolumeResolver::resolved,
                this, &FallbackChainResolver::onFandomResolved);
        connect(m_fandom, &fandom::FandomVolumeResolver::unresolved,
                this, &FallbackChainResolver::onFandomUnresolved);
    }
    if (m_wikipedia) {
        connect(m_wikipedia, &wikipedia::WikipediaResolver::resolved,
                this, &FallbackChainResolver::onWikipediaResolved);
        connect(m_wikipedia, &wikipedia::WikipediaResolver::unresolved,
                this, &FallbackChainResolver::onWikipediaUnresolved);
    }
}

void FallbackChainResolver::resolveForSeries(const QString& seriesId,
                                             const QString& wikidataQidHint,
                                             const QString& englishTitleHint)
{
    if (m_pending.contains(seriesId)) {
        qCInfo(lcFallback).noquote()
            << "drop duplicate resolveForSeries for" << seriesId
            << "(prior call still pending)";
        return;
    }

    if (!m_fandom && !m_wikipedia) {
        emit unresolved(seriesId, QStringLiteral("no-resolvers-attached"));
        return;
    }

    Pending p;
    if (!m_fandom)
        p.fandomSettled = true;     // counts as settled without success
    if (!m_wikipedia)
        p.wikipediaSettled = true;
    m_pending.insert(seriesId, p);

    if (m_fandom)
        m_fandom->resolveForSeries(seriesId, wikidataQidHint, englishTitleHint);
    if (m_wikipedia && !englishTitleHint.isEmpty())
        m_wikipedia->resolveForTitle(seriesId, englishTitleHint);
    else if (m_wikipedia) {
        // No english title hint → can't construct Wikipedia URL. Mark
        // Wikipedia leg settled-without-success and check whether we can
        // finalize from Fandom alone.
        auto& pp = m_pending[seriesId];
        pp.wikipediaSettled    = true;
        pp.wikipediaFailReason = QStringLiteral("no-english-title-hint");
        maybeFinalize(seriesId);
    }
}

void FallbackChainResolver::forceRefreshSeries(const QString& seriesId,
                                                const QString& wikidataQidHint,
                                                const QString& englishTitleHint)
{
    if (!wikidataQidHint.isEmpty()) {
        const bool ok = fandom::FandomCatalogCache::invalidateByQid(wikidataQidHint);
        qCInfo(lcFallback).noquote()
            << "forceRefreshSeries: invalidate" << wikidataQidHint
            << "→" << (ok ? "ok" : "fail");
    } else {
        qCInfo(lcFallback).noquote()
            << "forceRefreshSeries: empty qidHint for" << seriesId
            << "— skipping cache invalidation, will still re-resolve";
    }
    // Drop any pending in-flight resolve for this series so the re-fire
    // doesn't get rejected by resolveForSeries' dedup guard.
    m_pending.remove(seriesId);
    resolveForSeries(seriesId, wikidataQidHint, englishTitleHint);
}

void FallbackChainResolver::onFandomResolved(const QString& seriesId,
                                             const FandomCatalog& catalog)
{
    if (!m_pending.contains(seriesId))
        return;
    auto& p = m_pending[seriesId];
    p.fandomSettled = true;
    p.fandomOk      = true;
    p.fandomCatalog = catalog;
    maybeFinalize(seriesId);
}

void FallbackChainResolver::onFandomUnresolved(const QString& seriesId,
                                               const QString& reason)
{
    if (!m_pending.contains(seriesId))
        return;
    auto& p = m_pending[seriesId];
    p.fandomSettled    = true;
    p.fandomOk         = false;
    p.fandomFailReason = reason;
    maybeFinalize(seriesId);
}

void FallbackChainResolver::onWikipediaResolved(
    const wikipedia::WikipediaCatalog& catalog)
{
    const QString seriesId = catalog.seriesId;
    if (!m_pending.contains(seriesId))
        return;
    auto& p = m_pending[seriesId];
    p.wikipediaSettled  = true;
    p.wikipediaOk       = true;
    p.wikipediaVolumes  = catalog.volumes;
    maybeFinalize(seriesId);
}

void FallbackChainResolver::onWikipediaUnresolved(const QString& seriesId,
                                                  const QString& reason)
{
    if (!m_pending.contains(seriesId))
        return;
    auto& p = m_pending[seriesId];
    p.wikipediaSettled    = true;
    p.wikipediaOk         = false;
    p.wikipediaFailReason = reason;
    maybeFinalize(seriesId);
}

void FallbackChainResolver::maybeFinalize(const QString& seriesId)
{
    if (!m_pending.contains(seriesId))
        return;
    const Pending p = m_pending.value(seriesId);
    if (!p.fandomSettled || !p.wikipediaSettled)
        return;  // still waiting on the other leg

    m_pending.remove(seriesId);

    if (!p.fandomOk && !p.wikipediaOk) {
        emit unresolved(seriesId,
                        QStringLiteral("both-sources-failed: fandom=%1, wikipedia=%2")
                            .arg(p.fandomFailReason, p.wikipediaFailReason));
        return;
    }

    FandomCatalog merged = mergeCatalogs(seriesId,
                                          p.fandomCatalog,
                                          p.wikipediaVolumes,
                                          p.fandomOk,
                                          p.wikipediaOk);
    qCInfo(lcFallback).noquote()
        << "resolved" << seriesId
        << "(fandom=" << p.fandomOk << "wikipedia=" << p.wikipediaOk
        << "volumes=" << merged.volumes.size() << ")";
    emit resolved(seriesId, merged);
}

// Per-field merge policy: when BOTH sources have a value, Fandom wins.
// When ONE source has a value, that one wins. When neither has it, the
// field stays empty/invalid.
//
// Identity match: same volumeNumber across catalogs.
FandomCatalog FallbackChainResolver::mergeCatalogs(
    const QString& seriesId,
    const FandomCatalog& fandom,
    const QList<FandomVolume>& wikipedia,
    bool fandomOk,
    bool wikipediaOk)
{
    FandomCatalog out;
    out.seriesId         = seriesId;
    out.schemaVersion    = kFandomCatalogSchemaVersion;
    out.fetchedAt        = QDateTime::currentDateTimeUtc();

    // Fast path: only one source succeeded.
    if (fandomOk && !wikipediaOk) {
        out                    = fandom;
        out.seriesId           = seriesId;        // ensure caller-supplied id wins
        out.schemaVersion      = kFandomCatalogSchemaVersion;
        return out;
    }
    if (!fandomOk && wikipediaOk) {
        out.volumes = wikipedia;
        return out;
    }

    // Both succeeded — per-field merge.
    out.wikidataQid      = fandom.wikidataQid;
    out.fandomWikiId     = fandom.fandomWikiId;
    out.fandomVolumePath = fandom.fandomVolumePath;
    out.seriesSynopsis   = fandom.seriesSynopsis;

    // Build a Wikipedia-by-volumeNumber lookup.
    QHash<int, FandomVolume> wpByNum;
    for (const auto& wv : wikipedia)
        wpByNum.insert(wv.volumeNumber, wv);

    // Walk Fandom's volumes (canonical), per-field fall through to Wikipedia.
    for (const auto& fv : fandom.volumes) {
        FandomVolume merged = fv;
        if (wpByNum.contains(fv.volumeNumber)) {
            const FandomVolume& wv = wpByNum.value(fv.volumeNumber);
            merged.titleEnglish     = pickNonEmpty(fv.titleEnglish,     wv.titleEnglish);
            merged.titleJapanese    = pickNonEmpty(fv.titleJapanese,    wv.titleJapanese);
            merged.titleRomaji      = pickNonEmpty(fv.titleRomaji,      wv.titleRomaji);
            merged.synopsis         = pickNonEmpty(fv.synopsis,         wv.synopsis);
            merged.coverUrlEnglish  = pickNonEmpty(fv.coverUrlEnglish,  wv.coverUrlEnglish);
            merged.coverUrlJapanese = pickNonEmpty(fv.coverUrlJapanese, wv.coverUrlJapanese);
            merged.isbnJp           = pickNonEmpty(fv.isbnJp,           wv.isbnJp);
            merged.isbnEn           = pickNonEmpty(fv.isbnEn,           wv.isbnEn);
            merged.releaseDateJp    = pickValid(fv.releaseDateJp,       wv.releaseDateJp);
            merged.releaseDateEn    = pickValid(fv.releaseDateEn,       wv.releaseDateEn);
            // groupingLabel + chapterRange + chapterList stay Fandom-only
            // (Wikipedia doesn't surface arc/era data).
        }
        out.volumes.append(merged);
    }

    // Wikipedia-only volumes (vol numbers Fandom missed) — append.
    QSet<int> seen;
    for (const auto& fv : fandom.volumes)
        seen.insert(fv.volumeNumber);
    for (const auto& wv : wikipedia) {
        if (!seen.contains(wv.volumeNumber))
            out.volumes.append(wv);
    }

    return out;
}

} // namespace tankoban::manga
