#pragma once

#include <QWidget>
#include <QList>
#include <QSet>

class TileCard;

class TileStrip : public QWidget {
    Q_OBJECT
public:
    explicit TileStrip(QWidget* parent = nullptr);

    void clear();
    void addTile(TileCard* card);
    void filterTiles(const QString& query);
    void sortTiles(const QString& sortKey);
    void setDensity(int level);
    void setMode(const QString& mode);
    void setStripLabel(const QString& label);
    int visibleCount() const;
    int totalCount() const;
    TileCard* tileAt(const QPoint& pos) const;
    TileCard* tileAtIndex(int index) const;

    // Selection
    void clearSelection();
    void selectAll();
    QList<TileCard*> selectedTiles() const;

signals:
    void tileClicked(const QString& seriesPath);
    // Fires on a single left-click on a tile (receiver gets the card).
    // Distinct from `tileClicked(QString)` which is the legacy comics-mode
    // seriesPath signal. Stream mode uses this for single-click-opens-detail.
    void tileSingleClicked(TileCard* card);
    void tileDoubleClicked(TileCard* card);
    void tileRightClicked(TileCard* card, const QPoint& globalPos);
    void selectionChanged(const QList<TileCard*>& selected);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void reflowTiles();
    void selectTile(TileCard* card, Qt::KeyboardModifiers mods);
    void setFocusedTile(int visibleIndex);
    QList<TileCard*> visibleTileList() const;
    int visibleIndexOf(TileCard* card) const;

    QList<TileCard*> m_tiles;
    QSet<TileCard*> m_filteredOut;
    QSet<TileCard*> m_selected;
    QString m_filterQuery;

    int m_focusedIndex = -1;
    int m_lastClickedIndex = -1;
    int m_currentCols = 1;

    QString m_mode = "grid";
    int m_density = 1;
    int m_cardWidth = 200;
    int m_imageHeight = 308;
    int m_tileSpacingH = 16;

    static constexpr int TILE_SPACING_V = 20;
    static constexpr int PADDING = 0;
};
