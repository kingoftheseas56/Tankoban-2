#pragma once

#include <QScrollArea>
#include <QWidget>
#include <QList>
#include <QSet>

class TileCard;

class TileStrip : public QScrollArea {
    Q_OBJECT
public:
    explicit TileStrip(QWidget* parent = nullptr);

    void clear();
    void addTile(TileCard* card);
    void filterTiles(const QString& query);
    int visibleCount() const;
    int totalCount() const;

signals:
    void tileClicked(const QString& seriesPath);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void reflowTiles();

    QWidget* m_container = nullptr;
    QList<TileCard*> m_tiles;
    QSet<TileCard*> m_filteredOut;   // tiles hidden by search filter
    QString m_filterQuery;

    static constexpr int TILE_SPACING_H = 16;
    static constexpr int TILE_SPACING_V = 20;
    static constexpr int PADDING = 24;
};
