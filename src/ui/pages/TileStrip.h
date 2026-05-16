#pragma once

#include <QWidget>
#include <QList>
#include <QSet>

class TileCard;
class QPushButton;
class QPropertyAnimation;

class TileStrip : public QWidget {
    Q_OBJECT
    // CONTINUE_SCROLL_ARROWS 2026-05-02 — animated horizontal scroll offset
    // for "continue" mode. QPropertyAnimation drives this on arrow click.
    Q_PROPERTY(int scrollOffsetX READ scrollOffsetX WRITE setScrollOffsetX)
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

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 33 — exposes
    // the internal m_tiles list so ComicsPage::refreshTileChips can iterate
    // Tankoyomi-origin tiles + drive the DOWNLOADING chip from MangaDownloader
    // subscription. Returns a const reference; callers must not mutate.
    const QList<TileCard*>& tiles() const { return m_tiles; }

    // Selection
    void clearSelection();
    void selectAll();
    QList<TileCard*> selectedTiles() const;

    // Scroll-offset accessors (animated by QPropertyAnimation in continue mode).
    int scrollOffsetX() const { return m_scrollOffsetX; }
    void setScrollOffsetX(int x);

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

    // CONTINUE_SCROLL_ARROWS 2026-05-02 helpers (continue mode only).
    void ensureScrollChrome();
    void positionArrows();
    void updateArrowVisibility();
    void animateScrollBy(int direction);

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

    // Continue-mode scroll state.
    int m_scrollOffsetX = 0;
    int m_totalContentWidth = 0;
    QPushButton* m_leftArrow = nullptr;
    QPushButton* m_rightArrow = nullptr;
    QPropertyAnimation* m_scrollAnim = nullptr;

    static constexpr int TILE_SPACING_V = 20;
    static constexpr int PADDING = 0;
};
