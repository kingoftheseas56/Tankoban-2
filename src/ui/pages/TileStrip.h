#pragma once

#include <QScrollArea>
#include <QWidget>
#include <QList>

class TileCard;

class TileStrip : public QScrollArea {
    Q_OBJECT
public:
    explicit TileStrip(QWidget* parent = nullptr);

    void clear();
    void addTile(TileCard* card);

signals:
    void tileClicked(const QString& seriesPath);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void reflowTiles();

    QWidget* m_container = nullptr;
    QList<TileCard*> m_tiles;

    static constexpr int TILE_SPACING_H = 16;
    static constexpr int TILE_SPACING_V = 20;
    static constexpr int PADDING = 24;
};
