#include "TileStrip.h"
#include "TileCard.h"

#include <QResizeEvent>
#include <QTimer>
#include <algorithm>

TileStrip::TileStrip(QWidget* parent)
    : QScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(true);
    setStyleSheet("background: transparent;");

    m_container = new QWidget(this);
    m_container->setStyleSheet("background: transparent;");
    setWidget(m_container);
}

void TileStrip::clear()
{
    for (auto* tile : m_tiles)
        tile->deleteLater();
    m_tiles.clear();
    m_container->setMinimumHeight(0);
}

void TileStrip::addTile(TileCard* card)
{
    card->setParent(m_container);
    m_tiles.append(card);

    // Defer reflow to next event loop tick so geometry is valid
    QTimer::singleShot(0, this, &TileStrip::reflowTiles);
}

void TileStrip::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    reflowTiles();
}

void TileStrip::showEvent(QShowEvent* event)
{
    QScrollArea::showEvent(event);
    QTimer::singleShot(0, this, &TileStrip::reflowTiles);
}

void TileStrip::reflowTiles()
{
    if (m_tiles.isEmpty())
        return;

    int availWidth = viewport()->width();
    if (availWidth <= 0)
        availWidth = width();
    if (availWidth <= 0)
        return;

    int cardW = TileCard::CARD_WIDTH;
    int cols = std::max(1, (availWidth - 2 * PADDING + TILE_SPACING_H)
                           / (cardW + TILE_SPACING_H));

    // Center the grid
    int gridWidth = cols * cardW + (cols - 1) * TILE_SPACING_H;
    int leftMargin = (availWidth - gridWidth) / 2;

    int maxBottom = 0;

    for (int i = 0; i < m_tiles.size(); ++i) {
        int col = i % cols;
        int row = i / cols;

        int x = leftMargin + col * (cardW + TILE_SPACING_H);
        int y = PADDING + row * (TileCard::IMAGE_HEIGHT + 50 + TILE_SPACING_V);

        m_tiles[i]->move(x, y);
        m_tiles[i]->show();

        int bottom = y + TileCard::IMAGE_HEIGHT + 50;
        if (bottom > maxBottom)
            maxBottom = bottom;
    }

    m_container->setMinimumHeight(maxBottom + PADDING);
}
