// src/core/manga/FallbackChainResolver.h
//
// Phase 6 composer: dispatches a resolveForSeries call to BOTH
// FandomVolumeResolver and WikipediaResolver in parallel, waits for both
// to settle (resolved or unresolved), and emits a per-field-merged
// FandomCatalog with Fandom values winning when both sources have a
// value for the same field.
//
// Scope (v1): Fandom + Wikipedia only. The Codex amendment to Task 16
// (timer-based budgets + partial emit + late-replace) is deferred to
// Task 19 (ComicsPage wiring) where the UI loading semantics + request-
// generation identity properly live. This resolver stays a deterministic
// composer; the UI layer wraps it with latency budgets.
//
// AniList + MangaUpdates + BookWalker chains land alongside the
// ComicsLibraryRecord schema extension in Task 17 — they'll need new
// FandomCatalog fields or a richer carrier type. v1 keeps the contract
// narrow to the two HTML-scrape sources the prior phases built.
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §6, §7
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 16

#pragma once

#include "core/manga/fandom/FandomTypes.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace tankoban::manga::fandom { class FandomVolumeResolver; }
namespace tankoban::manga::wikipedia { class WikipediaResolver; struct WikipediaCatalog; }

namespace tankoban::manga {

class FallbackChainResolver : public QObject
{
    Q_OBJECT
public:
    FallbackChainResolver(fandom::FandomVolumeResolver* fandom,
                          wikipedia::WikipediaResolver* wikipedia,
                          QObject* parent = nullptr);

    // Drive both chains in parallel; emit merged result when both settle.
    // englishTitleHint is consulted by WikipediaResolver (it doesn't have
    // a manifest to derive its own title from).
    void resolveForSeries(const QString& seriesId,
                          const QString& wikidataQidHint = {},
                          const QString& englishTitleHint = {});

    // Fandom catalog redesign Task 19 (Phase 7, 2026-05-20). Invalidate the
    // 7d FandomCatalogCache entry for the given Q-ID + immediately re-fire
    // resolveForSeries. Used by the force-refresh affordance on the series
    // view when a wiki has updated since the last cache fetch. No-op
    // tolerated when qidHint is empty (cache key requires a Q-ID) — caller
    // logs the skip + still re-resolves via the normal path. Wikipedia
    // side has no cache layer in v1, so this only touches the Fandom cache.
    void forceRefreshSeries(const QString& seriesId,
                            const QString& wikidataQidHint = {},
                            const QString& englishTitleHint = {});

signals:
    void resolved(const QString& seriesId,
                  const tankoban::manga::fandom::FandomCatalog& catalog);
    void unresolved(const QString& seriesId, const QString& reason);

private slots:
    void onFandomResolved(const QString& seriesId,
                          const tankoban::manga::fandom::FandomCatalog& catalog);
    void onFandomUnresolved(const QString& seriesId, const QString& reason);
    void onWikipediaResolved(
        const tankoban::manga::wikipedia::WikipediaCatalog& catalog);
    void onWikipediaUnresolved(const QString& seriesId, const QString& reason);

private:
    struct Pending {
        bool                                fandomSettled    = false;
        bool                                wikipediaSettled = false;
        bool                                fandomOk         = false;
        bool                                wikipediaOk      = false;
        tankoban::manga::fandom::FandomCatalog fandomCatalog;
        QList<tankoban::manga::fandom::FandomVolume> wikipediaVolumes;
        QString                             fandomFailReason;
        QString                             wikipediaFailReason;
    };

    void maybeFinalize(const QString& seriesId);
    static tankoban::manga::fandom::FandomCatalog mergeCatalogs(
        const QString& seriesId,
        const tankoban::manga::fandom::FandomCatalog& fandom,
        const QList<tankoban::manga::fandom::FandomVolume>& wikipedia,
        bool fandomOk,
        bool wikipediaOk);

    fandom::FandomVolumeResolver*    m_fandom    = nullptr; // non-owning
    wikipedia::WikipediaResolver*    m_wikipedia = nullptr; // non-owning

    // Keyed by seriesId. One pending state per series; a second call for
    // the same seriesId while one is in flight gets ignored (logged).
    QHash<QString, Pending> m_pending;
};

} // namespace tankoban::manga
