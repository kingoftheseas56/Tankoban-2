#include "TileStrip.h"
#include "TileCard.h"

#include <QResizeEvent>
#include <QTimer>
#include <QRegularExpression>
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
    m_filteredOut.clear();
    m_filterQuery.clear();
    m_container->setMinimumHeight(0);
}

static bool matchesFilter(TileCard* card, const QString& query)
{
    if (query.isEmpty()) return true;
    QString title = card->property("tileTitle").toString().toLower();
    const auto tokens = query.split(' ', Qt::SkipEmptyParts);
    for (const auto& tok : tokens) {
        if (!title.contains(tok)) return false;
    }
    return true;
}

void TileStrip::addTile(TileCard* card)
{
    card->setParent(m_container);
    m_tiles.append(card);

    // Apply active filter to the new tile
    if (!matchesFilter(card, m_filterQuery))
        m_filteredOut.insert(card);

    // Defer reflow to next event loop tick so geometry is valid
    QTimer::singleShot(0, this, &TileStrip::reflowTiles);
}

void TileStrip::filterTiles(const QString& query)
{
    m_filterQuery = query.trimmed().toLower();
    m_filteredOut.clear();

    for (auto* tile : m_tiles) {
        if (!matchesFilter(tile, m_filterQuery))
            m_filteredOut.insert(tile);
    }

    reflowTiles();
}

int TileStrip::visibleCount() const
{
    return m_tiles.size() - m_filteredOut.size();
}

int TileStrip::totalCount() const
{
    return m_tiles.size();
}

// Natural sort: split on digits, compare chunks (int for digits, lowercase string for text)
static bool naturalLessThan(const QString& a, const QString& b)
{
    static QRegularExpression re(QStringLiteral("(\\d+)"));
    int ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        auto ma = re.match(a, ia);
        auto mb = re.match(b, ib);

        // Text before next digit group
        int endA = ma.hasMatch() ? ma.capturedStart() : a.size();
        int endB = mb.hasMatch() ? mb.capturedStart() : b.size();

        QString textA = a.mid(ia, endA - ia).toLower();
        QString textB = b.mid(ib, endB - ib).toLower();
        if (textA != textB)
            return textA < textB;

        // Digit groups
        if (!ma.hasMatch() || !mb.hasMatch())
            return ma.hasMatch() < mb.hasMatch(); // one has digits, other doesn't

        qint64 numA = ma.captured(1).toLongLong();
        qint64 numB = mb.captured(1).toLongLong();
        if (numA != numB)
            return numA < numB;

        ia = ma.capturedEnd();
        ib = mb.capturedEnd();
    }
    return a.size() < b.size();
}

void TileStrip::sortTiles(const QString& sortKey)
{
    if (m_tiles.isEmpty()) return;

    QString base = sortKey.section('_', 0, 0);
    bool desc = sortKey.endsWith("_desc");

    std::sort(m_tiles.begin(), m_tiles.end(),
              [&](TileCard* a, TileCard* b) {
        bool less = false;
        if (base == "name" || base == "title") {
            QString ta = a->property("tileTitle").toString();
            QString tb = b->property("tileTitle").toString();
            less = naturalLessThan(ta, tb);
        } else if (base == "updated") {
            qint64 ma = a->property("newestMtime").toLongLong();
            qint64 mb = b->property("newestMtime").toLongLong();
            less = ma < mb;
        } else if (base == "count") {
            int ca = a->property("fileCount").toInt();
            int cb = b->property("fileCount").toInt();
            less = ca < cb;
        } else {
            // Default: name asc
            less = naturalLessThan(
                a->property("tileTitle").toString(),
                b->property("tileTitle").toString());
        }
        return desc ? !less : less;
    });

    reflowTiles();
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
    int visibleIndex = 0;

    for (int i = 0; i < m_tiles.size(); ++i) {
        if (m_filteredOut.contains(m_tiles[i])) {
            m_tiles[i]->hide();
            continue;
        }

        int col = visibleIndex % cols;
        int row = visibleIndex / cols;

        int x = leftMargin + col * (cardW + TILE_SPACING_H);
        int y = PADDING + row * (TileCard::IMAGE_HEIGHT + 50 + TILE_SPACING_V);

        m_tiles[i]->move(x, y);
        m_tiles[i]->show();

        int bottom = y + TileCard::IMAGE_HEIGHT + 50;
        if (bottom > maxBottom)
            maxBottom = bottom;

        ++visibleIndex;
    }

    m_container->setMinimumHeight(maxBottom + PADDING);
}
