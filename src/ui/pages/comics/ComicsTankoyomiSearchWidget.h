#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — forked from
// src/ui/pages/stream/StreamSearchWidget.{h,cpp} per brainstorm §6.1.
// Same shape: search input → fan-out to scrapers → two-section grid
// (Manga / Comics, by MangaResult::type) → click emits seriesActivated.
// Diverges: manga types instead of MetaItemPreview; QList<MangaScraper*>
// fan-out instead of MetaAggregator; no addon registry; no "in library"
// badge on result tiles (the comics library tile shows the chip).

#include "core/manga/MangaResult.h"
#include <QHash>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>

class MangaSourceRegistry;
class ComicsTankoyomiLibrary;
class TileStrip;
class TileCard;
class QNetworkAccessManager;

// TANKOYOMI_PREMIUM Phase 8 -- Premium catalog routing forward decl.
namespace tankoban { namespace manga { namespace premium { class PremiumCatalog; } } }

class ComicsTankoyomiSearchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ComicsTankoyomiSearchWidget(MangaSourceRegistry* registry,
                                         QNetworkAccessManager* nam,
                                         QWidget* parent = nullptr);

    void search(const QString& query);   // entry point from ComicsPage's search bar
    void clearResults();                  // when Back is clicked

    // TANKOYOMI_PREMIUM Phase 8 -- ComicsPage wires the loaded catalog right
    // after construction. Nullable: a missing catalog (load failure) silently
    // disables Premium routing + synthetic injection; the two existing
    // sections (Manga / Comics) continue to render normally.
    void setPremiumCatalog(tankoban::manga::premium::PremiumCatalog* catalog);

signals:
    void backRequested();
    void seriesActivated(const MangaResult& preview);

private:
    void buildUI();
    void addResultCard(const MangaResult& r, TileStrip* targetStrip);
    void downloadPoster(const MangaResult& r, TileCard* card);
    void onSearchFinished(const QList<MangaResult>& batch, int generation);
    void onSearchError(const QString& message, int generation);

    static constexpr int kInitialCap = 6;     // mirrors Stream's per-section cap
    void revealMangaOverflow();
    void revealComicsOverflow();
    // TANKOYOMI_PREMIUM Phase 8 -- Premium section overflow reveal + synthetic
    // catalog injection.
    void revealPremiumOverflow();
    // Inject synthetic Premium-catalog-only hits (titles present in catalog
    // but with no live scraper match) into the Premium strip. Called once at
    // search start; live-scraper results layer in after via onSearchFinished.
    // PHASE 9+ TODO: clicking a synthetic tile (source="tankoyomi_premium")
    // emits seriesActivated but the page-side handler currently routes by
    // source to a real scraper; "tankoyomi_premium" has no scraper. Visible-
    // only until Phase 9 adopt-folder migration wires a synthetic-aware path.
    void injectPremiumCatalogSynthetics(const QString& query);

    MangaSourceRegistry*    m_registry;
    QNetworkAccessManager*  m_nam;

    QString m_currentQuery;
    int     m_pendingSearches = 0;
    int     m_searchGeneration = 0;
    QString m_posterCacheDir;

    QPushButton* m_backBtn      = nullptr;
    QLabel*      m_statusLabel  = nullptr;
    QScrollArea* m_scroll       = nullptr;

    // TANKOYOMI_PREMIUM Phase 8 -- Premium section sits ABOVE the existing
    // Manga / Comics two-section grid. Routing decision in onSearchFinished
    // checks PremiumCatalog::isPremiumSeries(title) and steers Premium hits
    // here instead of the original Manga or Comics strip. Synthetic catalog-
    // only injections (no live scraper match) also land in this strip.
    QLabel*      m_premiumHeader    = nullptr;
    TileStrip*   m_premiumStrip     = nullptr;
    QPushButton* m_premiumShowMore  = nullptr;

    QLabel*      m_mangaHeader    = nullptr;
    TileStrip*   m_mangaStrip     = nullptr;
    QPushButton* m_mangaShowMore  = nullptr;
    QLabel*      m_comicsHeader   = nullptr;
    TileStrip*   m_comicsStrip    = nullptr;
    QPushButton* m_comicsShowMore = nullptr;

    QList<MangaResult> m_premiumOverflow;
    QList<MangaResult> m_mangaOverflow;
    QList<MangaResult> m_comicsOverflow;

    QHash<QString, MangaResult> m_previewsByKey;  // sourceId:seriesId -> MangaResult

    // TANKOYOMI_PREMIUM Phase 8 -- nullable; injection / routing both no-op
    // when null. Owned by ComicsPage, set via setPremiumCatalog().
    tankoban::manga::premium::PremiumCatalog* m_premiumCatalog = nullptr;
};
