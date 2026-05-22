// src/ui/pages/comics/ComicsCatalogScreen.h
//
// COMICS_TANKOYOMI_STREAM_MERGER (2026-05-22) — Comics-mode Catalog browser.
// Mirrors the visual shape of Theatre-mode's CatalogBrowseScreen but bound
// to a different data source: instead of fetching addon /catalog endpoints,
// it scans every JSON file in <repo-root>/data/fandom_catalog/ at construction
// (via LocalFandomCatalogLoader) and renders one tile per Fandom-catalogued
// series.
//
// Tile click emits seriesActivated(seriesId) — ComicsPage handles routing
// back into the existing per-series open path (Tankoyomi search-result style
// or AniList resolve, depending on what data we have for that slug).
//
// Cover loading is lazy via QNetworkAccessManager: placeholder shown until
// the canonical coverUrlJapanese arrives. No on-disk caching at this layer;
// QNetworkDiskCache wiring is a future polish.

#pragma once

#include <QWidget>
#include <QString>
#include <QList>
#include <QHash>
#include "../TileCard.h"

class QGridLayout;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QScrollArea;

namespace tankoban::manga::fandom {
struct FandomCatalog;
}

namespace tankoban::ui::comics {

class ComicsCatalogScreen : public QWidget {
    Q_OBJECT
public:
    explicit ComicsCatalogScreen(QNetworkAccessManager* nam, QWidget* parent = nullptr);
    ~ComicsCatalogScreen() override;

    // Re-scan the catalog directory and rebuild the tile grid. Cheap: file
    // count is ~30 today, ~150 ceiling. Safe to call on tab activation.
    void refresh();

signals:
    void backRequested();
    void seriesActivated(const QString& seriesId, const QString& seriesTitle, int anilistId);

private:
    void buildUi();
    void clearTiles();
    void loadAllCatalogs();
    void addTile(const tankoban::manga::fandom::FandomCatalog& catalog);
    void fetchCover(TileCard* tile, const QString& url);

    QNetworkAccessManager* m_nam = nullptr;  // not owned; provided by ComicsPage
    QScrollArea*           m_scroll = nullptr;
    QWidget*               m_gridHost = nullptr;
    QGridLayout*           m_grid = nullptr;
    QLabel*                m_emptyLabel = nullptr;
    QPushButton*           m_backBtn = nullptr;
    QList<TileCard*>       m_tiles;
};

} // namespace tankoban::ui::comics
