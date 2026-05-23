// src/ui/pages/comics/ComicsCatalogScreen.h
//
// COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23) — updated to use
// LocalMangaCatalogLoader + MangaCatalog (source-agnostic rename from
// LocalFandomCatalogLoader + FandomCatalog).
//
// Comics-mode Catalog browser. Mirrors the visual shape of Theatre-mode's
// CatalogBrowseScreen but bound to data/mangafire_catalog/ JSON files
// scanned at construction via LocalMangaCatalogLoader.
//
// Tile click emits seriesActivated(seriesId) — ComicsPage handles routing
// into the per-series view. Cover loading is lazy via QNetworkAccessManager.

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

namespace tankoban::manga {
struct MangaCatalog;
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
    void addTile(const tankoban::manga::MangaCatalog& catalog);
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
