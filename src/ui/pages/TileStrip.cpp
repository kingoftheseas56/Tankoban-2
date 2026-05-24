#include "TileStrip.h"
#include "TileCard.h"

#include <QResizeEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QRegularExpression>
#include <QPushButton>
#include <QIcon>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <algorithm>

TileStrip::TileStrip(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background: transparent;");
    setFocusPolicy(Qt::ClickFocus);
}

void TileStrip::clear()
{
    for (auto* tile : m_tiles)
        tile->deleteLater();
    m_tiles.clear();
    m_filteredOut.clear();
    m_selected.clear();
    m_filterQuery.clear();
    m_focusedIndex = -1;
    m_lastClickedIndex = -1;
    setMinimumHeight(0);
    setFixedHeight(0);
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
    card->setParent(this);
    card->setCardSize(m_cardWidth, m_imageHeight);
    m_tiles.append(card);

    if (!matchesFilter(card, m_filterQuery))
        m_filteredOut.insert(card);

    QTimer::singleShot(0, this, &TileStrip::reflowTiles);
}

void TileStrip::setMode(const QString& mode)
{
    m_mode = mode;
    if (m_mode == "continue") {
        m_cardWidth = 150;
        m_imageHeight = static_cast<int>(150 / 0.65);
        m_tileSpacingH = 12;
        for (auto* tile : m_tiles)
            tile->setCardSize(m_cardWidth, m_imageHeight);
        ensureScrollChrome();
    }
    reflowTiles();
}

void TileStrip::setDensity(int level)
{
    // 2026-04-25 — gate dropped per Hemanth: the density slider is meant
    // to be a cohesive control across all strips on the page, including
    // the "continue" mode strip (which previously rendered at a fixed
    // size). All TileStrip instances on a page now respond to setDensity
    // uniformly. If continue strips need a "hero" larger ratio later,
    // that's a follow-up — for now Hemanth wants the simple shape.
    m_density = qBound(0, level, 2);
    static const int widths[]  = { 150, 200, 240 };
    static const int gaps[]    = { 10,  16,  20  };
    m_cardWidth    = widths[m_density];
    m_imageHeight  = static_cast<int>(m_cardWidth / 0.65);
    m_tileSpacingH = gaps[m_density];

    for (auto* tile : m_tiles)
        tile->setCardSize(m_cardWidth, m_imageHeight);

    reflowTiles();
}

void TileStrip::setStripLabel(const QString& label)
{
    setProperty("stripLabel", label);
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

// ── Natural sort ────────────────────────────────────────────
static bool naturalLessThan(const QString& a, const QString& b)
{
    static QRegularExpression re(QStringLiteral("(\\d+)"));
    int ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        auto ma = re.match(a, ia);
        auto mb = re.match(b, ib);

        int endA = ma.hasMatch() ? ma.capturedStart() : a.size();
        int endB = mb.hasMatch() ? mb.capturedStart() : b.size();

        QString textA = a.mid(ia, endA - ia).toLower();
        QString textB = b.mid(ib, endB - ib).toLower();
        if (textA != textB)
            return textA < textB;

        if (!ma.hasMatch() || !mb.hasMatch())
            return ma.hasMatch() < mb.hasMatch();

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
            less = naturalLessThan(
                a->property("tileTitle").toString(),
                b->property("tileTitle").toString());
        } else if (base == "updated") {
            less = a->property("newestMtime").toLongLong()
                 < b->property("newestMtime").toLongLong();
        } else if (base == "count") {
            less = a->property("fileCount").toInt()
                 < b->property("fileCount").toInt();
        } else {
            less = naturalLessThan(
                a->property("tileTitle").toString(),
                b->property("tileTitle").toString());
        }
        return desc ? !less : less;
    });

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

TileCard* TileStrip::tileAt(const QPoint& pos) const
{
    QWidget* w = childAt(pos);
    while (w && w != this) {
        if (auto* card = qobject_cast<TileCard*>(w))
            return card;
        w = w->parentWidget();
    }
    return nullptr;
}

TileCard* TileStrip::tileAtIndex(int index) const
{
    if (index >= 0 && index < m_tiles.size())
        return m_tiles[index];
    return nullptr;
}

// ── Selection ───────────────────────────────────────────────

QList<TileCard*> TileStrip::visibleTileList() const
{
    QList<TileCard*> vis;
    for (auto* t : m_tiles)
        if (!m_filteredOut.contains(t))
            vis.append(t);
    return vis;
}

int TileStrip::visibleIndexOf(TileCard* card) const
{
    int idx = 0;
    for (auto* t : m_tiles) {
        if (m_filteredOut.contains(t)) continue;
        if (t == card) return idx;
        ++idx;
    }
    return -1;
}

QList<TileCard*> TileStrip::selectedTiles() const
{
    QList<TileCard*> sel;
    for (auto* t : m_tiles)
        if (m_selected.contains(t))
            sel.append(t);
    return sel;
}

void TileStrip::clearSelection()
{
    for (auto* t : m_selected)
        t->setSelected(false);
    m_selected.clear();
    m_lastClickedIndex = -1;
    emit selectionChanged({});
}

void TileStrip::selectAll()
{
    auto vis = visibleTileList();
    for (auto* t : vis) {
        t->setSelected(true);
        m_selected.insert(t);
    }
    emit selectionChanged(selectedTiles());
}

void TileStrip::selectTile(TileCard* card, Qt::KeyboardModifiers mods)
{
    if (!card) return;

    auto vis = visibleTileList();
    int idx = vis.indexOf(card);
    if (idx < 0) return;

    if (mods & Qt::ControlModifier) {
        // Ctrl+click: toggle this tile
        if (m_selected.contains(card)) {
            m_selected.remove(card);
            card->setSelected(false);
        } else {
            m_selected.insert(card);
            card->setSelected(true);
        }
    } else if ((mods & Qt::ShiftModifier) && m_lastClickedIndex >= 0) {
        // Shift+click: select contiguous range
        int from = std::min(m_lastClickedIndex, idx);
        int to   = std::max(m_lastClickedIndex, idx);
        // Clear previous selection
        for (auto* t : m_selected)
            t->setSelected(false);
        m_selected.clear();
        // Select range
        for (int i = from; i <= to && i < vis.size(); ++i) {
            vis[i]->setSelected(true);
            m_selected.insert(vis[i]);
        }
    } else {
        // Plain click: select only this tile
        for (auto* t : m_selected)
            t->setSelected(false);
        m_selected.clear();
        m_selected.insert(card);
        card->setSelected(true);
    }

    m_lastClickedIndex = idx;
    // Clear keyboard focus ring on mouse click — focus ring is for keyboard nav only
    setFocusedTile(-1);
    emit selectionChanged(selectedTiles());
}

void TileStrip::setFocusedTile(int visibleIndex)
{
    auto vis = visibleTileList();
    // Unfocus previous
    if (m_focusedIndex >= 0 && m_focusedIndex < vis.size())
        vis[m_focusedIndex]->setFocused(false);

    m_focusedIndex = visibleIndex;

    // Focus new
    if (m_focusedIndex >= 0 && m_focusedIndex < vis.size())
        vis[m_focusedIndex]->setFocused(true);
}

// ── Events ──────────────────────────────────────────────────

void TileStrip::mousePressEvent(QMouseEvent* event)
{
    TileCard* card = tileAt(event->pos());

    if (event->button() == Qt::LeftButton) {
        if (card) {
            selectTile(card, event->modifiers());
            // Only emit single-click activation on an unmodified left-click —
            // Ctrl/Shift/Alt are reserved for multi-select semantics and must
            // not open detail views.
            if (event->modifiers() == Qt::NoModifier) {
                emit tileSingleClicked(card);
            }
        } else {
            clearSelection();
        }
    } else if (event->button() == Qt::RightButton && card) {
        // Ensure right-clicked tile is in selection
        if (!m_selected.contains(card))
            selectTile(card, Qt::NoModifier);
        emit tileRightClicked(card, event->globalPosition().toPoint());
    }

    QWidget::mousePressEvent(event);
}

void TileStrip::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        TileCard* card = tileAt(event->pos());
        if (card)
            emit tileDoubleClicked(card);
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TileStrip::keyPressEvent(QKeyEvent* event)
{
    auto vis = visibleTileList();
    if (vis.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }

    int idx = m_focusedIndex;

    switch (event->key()) {
    case Qt::Key_Left:
        if (idx > 0) setFocusedTile(idx - 1);
        break;
    case Qt::Key_Right:
        if (idx < vis.size() - 1) setFocusedTile(idx + 1);
        break;
    case Qt::Key_Up:
        if (idx >= m_currentCols) setFocusedTile(idx - m_currentCols);
        break;
    case Qt::Key_Down:
        if (idx + m_currentCols < vis.size()) setFocusedTile(idx + m_currentCols);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (idx >= 0 && idx < vis.size())
            emit tileDoubleClicked(vis[idx]);
        break;
    case Qt::Key_Escape:
        clearSelection();
        setFocusedTile(-1);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void TileStrip::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    reflowTiles();
}

void TileStrip::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, &TileStrip::reflowTiles);
}

// ── Layout ──────────────────────────────────────────────────

void TileStrip::reflowTiles()
{
    if (m_tiles.isEmpty()) {
        setFixedHeight(0);
        return;
    }

    int availWidth = width();
    if (availWidth <= 0)
        return;

    if (m_mode == "continue") {
        // CONTINUE_SCROLL_ARROWS 2026-05-02 — tiles laid out left-to-right
        // with m_scrollOffsetX subtracted from each x position. Tiles past
        // the visible width are simply rendered off-canvas (Qt clips child
        // widgets at parent bounds).
        int x = PADDING - m_scrollOffsetX;
        int contentEnd = PADDING;
        for (auto* tile : m_tiles) {
            if (m_filteredOut.contains(tile)) {
                tile->hide();
                continue;
            }
            tile->setCardSize(m_cardWidth, m_imageHeight);
            tile->move(x, 4);
            tile->show();
            x += m_cardWidth + m_tileSpacingH;
            contentEnd += m_cardWidth + m_tileSpacingH;
        }
        // Last tile doesn't need trailing spacing.
        if (!m_tiles.isEmpty())
            contentEnd -= m_tileSpacingH;
        m_totalContentWidth = contentEnd;
        setFixedHeight(m_imageHeight + 56);
        positionArrows();
        updateArrowVisibility();
        return;
    }

    // Fixed grid mode keeps the exact density-selected card dimensions.
    // Comics Library uses this to match Continue Reading tile size while
    // retaining multi-row grid layout.
    if (m_mode == "fixedGrid") {
        const int avail = availWidth - 4;
        const int cols = std::max(1, (avail + m_tileSpacingH) / (m_cardWidth + m_tileSpacingH));

        m_currentCols = cols;

        int maxBottom = 0;
        int visibleIndex = 0;

        for (auto* tile : m_tiles) {
            if (m_filteredOut.contains(tile)) {
                tile->hide();
                continue;
            }

            tile->setCardSize(m_cardWidth, m_imageHeight);

            const int col = visibleIndex % cols;
            const int row = visibleIndex / cols;

            const int x = 2 + col * (m_cardWidth + m_tileSpacingH);
            const int y = PADDING + row * (m_imageHeight + 50 + TILE_SPACING_V);

            tile->move(x, y);
            tile->show();

            const int bottom = y + m_imageHeight + 50;
            if (bottom > maxBottom)
                maxBottom = bottom;

            ++visibleIndex;
        }

        setFixedHeight(visibleIndex > 0 ? maxBottom + PADDING : 0);
        return;
    }

    // Grid mode with card width expansion (groundwork algorithm)
    const int avail  = availWidth - 4;
    const int baseW  = m_cardWidth;
    const int gap    = m_tileSpacingH;

    const int cols    = std::max(1, (avail + gap) / (baseW + gap));
    const int actualW = std::max(baseW, (avail - (cols - 1) * gap) / cols);
    const int actualH = static_cast<int>(actualW / 0.65);

    m_currentCols = cols;

    int maxBottom = 0;
    int visibleIndex = 0;

    for (auto* tile : m_tiles) {
        if (m_filteredOut.contains(tile)) {
            tile->hide();
            continue;
        }

        tile->setCardSize(actualW, actualH);

        int col = visibleIndex % cols;
        int row = visibleIndex / cols;

        int x = 2 + col * (actualW + gap);
        int y = PADDING + row * (actualH + 50 + TILE_SPACING_V);

        tile->move(x, y);
        tile->show();

        int bottom = y + actualH + 50;
        if (bottom > maxBottom)
            maxBottom = bottom;

        ++visibleIndex;
    }

    setFixedHeight(visibleIndex > 0 ? maxBottom + PADDING : 0);
}

// ── CONTINUE_SCROLL_ARROWS 2026-05-02 — overflow chrome for continue mode ──
//
// Lazy-create two transparent chevron buttons (left + right) over the strip.
// Their visibility is recomputed on every reflow against m_scrollOffsetX vs
// the overflow extent. Click triggers a QPropertyAnimation on scrollOffsetX
// (OutCubic 280ms) which re-flows the tiles at the new offset. Window resize
// → reflowTiles() → updateArrowVisibility() picks up automatically.

void TileStrip::ensureScrollChrome()
{
    if (m_leftArrow) return;  // already created

    auto makeArrow = [this](const char* objName, const QString& iconPath,
                            int direction, const QString& tip) {
        auto* btn = new QPushButton(this);
        btn->setObjectName(objName);
        btn->setFixedSize(36, 56);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(20, 20));
        btn->setToolTip(tip);
        btn->setFocusPolicy(Qt::NoFocus);
        // Transparent surface; subtle hover lift only. Stays out of the way
        // visually until needed (Stremio-style discretion).
        btn->setStyleSheet(
            "QPushButton { background: rgba(0,0,0,0.20); border: none; "
            "border-radius: 18px; }"
            "QPushButton:hover { background: rgba(0,0,0,0.40); }"
            "QPushButton:pressed { background: rgba(0,0,0,0.55); }");
        connect(btn, &QPushButton::clicked, this,
                [this, direction]() { animateScrollBy(direction); });
        btn->hide();  // visibility decided by updateArrowVisibility
        return btn;
    };

    m_leftArrow  = makeArrow("ContinueScrollLeft",
                             ":/icons/chevron_left.svg",  -1, "Scroll left");
    m_rightArrow = makeArrow("ContinueScrollRight",
                             ":/icons/chevron_right.svg", +1, "Scroll right");

    m_scrollAnim = new QPropertyAnimation(this, "scrollOffsetX", this);
    m_scrollAnim->setDuration(280);
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
}

void TileStrip::positionArrows()
{
    if (!m_leftArrow || !m_rightArrow) return;
    const int btnW = 36;
    const int btnH = 56;
    // Vertically centered on the tile image area (which sits at y=4 with
    // m_imageHeight tall).
    const int yCenter = 4 + m_imageHeight / 2;
    const int y = yCenter - btnH / 2;
    m_leftArrow->move(8, y);
    m_rightArrow->move(width() - btnW - 8, y);
    m_leftArrow->raise();
    m_rightArrow->raise();
}

void TileStrip::updateArrowVisibility()
{
    if (!m_leftArrow || !m_rightArrow) return;
    const bool overflows = m_totalContentWidth > width();
    const int maxOffset = std::max(0, m_totalContentWidth - width());
    m_leftArrow->setVisible(overflows && m_scrollOffsetX > 0);
    m_rightArrow->setVisible(overflows && m_scrollOffsetX < maxOffset);
}

void TileStrip::setScrollOffsetX(int x)
{
    const int maxOffset = std::max(0, m_totalContentWidth - width());
    const int clamped = std::clamp(x, 0, maxOffset);
    if (clamped == m_scrollOffsetX) return;
    m_scrollOffsetX = clamped;
    reflowTiles();  // re-positions tiles + re-evaluates arrow visibility
}

void TileStrip::animateScrollBy(int direction)
{
    if (!m_scrollAnim) return;
    // Step = 85% of viewport width so a small slice of the prior column
    // remains visible after each click — Stremio's behavior.
    const int step = static_cast<int>(width() * 0.85);
    const int target = m_scrollOffsetX + direction * step;
    const int maxOffset = std::max(0, m_totalContentWidth - width());
    const int clampedTarget = std::clamp(target, 0, maxOffset);
    m_scrollAnim->stop();
    m_scrollAnim->setStartValue(m_scrollOffsetX);
    m_scrollAnim->setEndValue(clampedTarget);
    m_scrollAnim->start();
}
