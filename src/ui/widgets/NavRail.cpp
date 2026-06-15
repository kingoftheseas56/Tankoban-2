// src/ui/widgets/NavRail.cpp
//
// HARBOR_REDESIGN Phase 1 Task 3 (2026-06-15, Agent 5). See NavRail.h.

#include "ui/widgets/NavRail.h"

#include "ui/Theme.h"

#include <algorithm>

#include <QColor>
#include <QEasingCurve>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace tankoban::ui {

namespace {

// Rail geometry — spec §4 left rail. Expanded shows icon + label; collapsed
// shrinks to an icon strip. kEasePull ~ cubic-bezier(0.32, 0.72, 0.24, 1)
// (Theme.h authoring note) built below with addCubicBezierSegment.
// Electron Sidebar parity: collapsed 72px / expanded 240px; icon 24px.
constexpr int kExpandedW = 240;
constexpr int kCollapsedW = 72;
constexpr int kCollapseDurationMs = 320;
constexpr int kIconSize = 24;
constexpr int kFlyoutGap = 8;   // px past the rail's right edge (Electron: railRight + 8)

// Build the Harbor "pull" easing curve. QSS cannot consume easing curves, so
// this lives in C++ for the maximumWidth QPropertyAnimation. addCubicBezierSegment
// approximates cubic-bezier(0.32, 0.72, 0.24, 1) over the unit interval.
QEasingCurve makeEasePull()
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.32, 0.72),
                                QPointF(0.24, 1.0),
                                QPointF(1.0, 1.0));
    return curve;
}

// Tint a group-button icon with the active theme's text color. SVGs hardcode
// stroke="#c6c6c6"; tintedSvgIcon string-replaces it (see Theme.cpp). Empty
// iconPath → null icon (text-only button), which is valid.
QIcon railIcon(const QString& iconPath)
{
    if (iconPath.isEmpty()) return QIcon();
    return ::Theme::tintedSvgIcon(iconPath, QColor(::Theme::current().text), kIconSize);
}

// Gold variant — the collapsed mode trigger (active mode) and the current
// flyout pill read as gold (Electron: collapsed active = icon turned gold).
QIcon railIconAccent(const QString& iconPath)
{
    if (iconPath.isEmpty()) return QIcon();
    return ::Theme::tintedSvgIcon(iconPath, QColor(::Theme::current().accent), kIconSize);
}

// On-accent (dark ink) variant — the ACTIVE flyout pill sits on a gold fill, so
// its glyph must be the dark on-accent ink (gold-on-gold = invisible icon).
QIcon railIconOnAccent(const QString& iconPath)
{
    if (iconPath.isEmpty()) return QIcon();
    return ::Theme::tintedSvgIcon(iconPath, QColor(::Theme::current().onAccent), kIconSize);
}

// JSON-string-escape a value (NavRail ids/labels are simple, but be safe for
// the dev-bridge consumer).
QString jsonEscape(const QString& s)
{
    QString out;
    out.reserve(s.size() + 2);
    for (const QChar c : s) {
        switch (c.unicode()) {
            case '"':  out += QStringLiteral("\\\""); break;
            case '\\': out += QStringLiteral("\\\\"); break;
            case '\n': out += QStringLiteral("\\n");  break;
            case '\t': out += QStringLiteral("\\t");  break;
            default:   out += c;                      break;
        }
    }
    return out;
}

QString idListJson(const std::vector<NavRail::Item>& items)
{
    QString out = QStringLiteral("[");
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += QLatin1Char(',');
        out += QLatin1Char('"') + jsonEscape(items[i].id) + QLatin1Char('"');
    }
    out += QLatin1Char(']');
    return out;
}

} // namespace

NavRail::NavRail(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("NavRail"));
    setFixedWidth(kExpandedW);
    setMinimumWidth(kCollapsedW);
    setMaximumWidth(kExpandedW);

    // Electron Sidebar: scroll region padding 12px 16px, group gap 6px.
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(16, 12, 16, 12);
    m_layout->setSpacing(6);

    // Width animator drives the collapse/expand transition on maximumWidth so
    // the QHBoxLayout in MainWindow reflows the page stack as the rail slides.
    m_widthAnim = new QPropertyAnimation(this, QByteArrayLiteral("maximumWidth"), this);
    m_widthAnim->setDuration(kCollapseDurationMs);
    m_widthAnim->setEasingCurve(makeEasePull());

    rebuild();
}

void NavRail::setModes(const std::vector<Item>& modes)
{
    m_modes = modes;
    rebuild();
}

void NavRail::setPages(const std::vector<Item>& pages)
{
    m_pages = pages;
    // "Remain still" on mode switch: refill ONLY the pages host. A full rebuild()
    // here tore down brand/modes/footer + closed the flyout on every mode switch
    // (the visible "sidebar closes" jank Hemanth flagged). Pre-rebuild (host not
    // built yet), fall back to a full build.
    if (m_pagesHost) {
        fillGroup(m_pagesHost, m_pages);
        applyCollapsedToButtons();
        if (!m_activeId.isEmpty()) setActiveId(m_activeId);
    } else {
        rebuild();
    }
}

void NavRail::setCollections(const std::vector<Item>& cols)
{
    m_collections = cols;
    if (m_collectionsHost) {
        fillGroup(m_collectionsHost, m_collections);
        applyCollapsedToButtons();
        if (!m_activeId.isEmpty()) setActiveId(m_activeId);
    } else {
        rebuild();
    }
}

void NavRail::setActiveId(const QString& id)
{
    m_activeId = id;
    for (auto& [btnId, btn] : m_buttons) {
        if (!btn) continue;
        const bool active = (btnId == id);
        btn->setChecked(active);
        // Dynamic property drives the QSS QPushButton#NavRailButton[active="true"]
        // gold rule. Re-polish so the style re-evaluates the selector.
        btn->setProperty("active", active);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }

    // Collapsed: the single trigger always shows the ACTIVE mode's icon (gold).
    // Re-tint it whenever the active mode changes.
    if (m_collapsed && m_modeTrigger) {
        m_modeTrigger->setIcon(railIconAccent(activeModeIconPath()));
    }
}

bool NavRail::isCollapsed() const
{
    return m_collapsed;
}

void NavRail::setCollapsed(bool collapsed)
{
    if (collapsed == m_collapsed) return;
    m_collapsed = collapsed;

    // Animate the rail width. minimumWidth must drop to the collapsed target
    // first (or the maximumWidth animation can't shrink past it); setting both
    // bounds around the animation keeps the layout honest at the endpoints.
    const int from = width();
    const int to = collapsed ? kCollapsedW : kExpandedW;
    setMinimumWidth(kCollapsedW);

    m_widthAnim->stop();
    m_widthAnim->setStartValue(from);
    m_widthAnim->setEndValue(to);
    m_widthAnim->start();

    // The MODES group changes SHAPE on collapse (labeled list ↔ single trigger
    // + flyout), so a full rebuild swaps the section. rebuild() re-applies the
    // active highlight and collapsed state at the end.
    rebuild();

    // Collapse toggle glyph follows direction: chevron_left = "collapse",
    // chevron_right = "expand".
    if (m_collapseBtn) {
        const QString glyph = collapsed ? QStringLiteral(":/icons/chevron_right.svg")
                                        : QStringLiteral(":/icons/chevron_left.svg");
        m_collapseBtn->setIcon(::Theme::tintedSvgIcon(glyph, QColor(::Theme::current().text), kIconSize));
    }

    emit collapseToggled(m_collapsed);
}

void NavRail::applyCollapsedToButtons()
{
    // Labels hide when collapsed (icon-only strip); brand text follows too. The
    // `collapsed` dynamic property lets the QSS center the icon + flip the active
    // treatment to "gold icon, no background pill" (Electron collapsed-active).
    for (auto& [btnId, btn] : m_buttons) {
        if (!btn) continue;
        if (m_collapsed) {
            btn->setProperty("fullText", btn->text());
            btn->setText(QString());
            btn->setToolTip(btn->property("fullText").toString());
        } else {
            const QString full = btn->property("fullText").toString();
            if (!full.isEmpty()) btn->setText(full);
            btn->setToolTip(QString());
        }
        btn->setProperty("collapsed", m_collapsed);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
    if (m_collapseBtn) {
        m_collapseBtn->setProperty("collapsed", m_collapsed);
        m_collapseBtn->style()->unpolish(m_collapseBtn);
        m_collapseBtn->style()->polish(m_collapseBtn);
    }
    if (m_brand) m_brand->setVisible(!m_collapsed);
}

bool NavRail::eventFilter(QObject* watched, QEvent* event)
{
    // The brand QLabel has no clicked() signal; a left mouse-release on it acts
    // as "go home" (mirrors the top-pill brand behavior pre-Harbor).
    if (watched == m_brand && event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && m_brand->rect().contains(me->position().toPoint())) {
            emit itemActivated(QStringLiteral("__home__"));
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

QString NavRail::devSnapshot() const
{
    // Small JSON string for the dev-bridge introspect-object verb. Mirrors the
    // IDevInspectable spirit; the plan contract fixes the return type as QString.
    return QStringLiteral("{\"widget\":\"NavRail\",\"collapsed\":%1,\"activeId\":\"%2\","
                          "\"modes\":%3,\"pages\":%4,\"collections\":%5,"
                          "\"modeFlyoutOpen\":%6}")
        .arg(m_collapsed ? QStringLiteral("true") : QStringLiteral("false"),
             jsonEscape(m_activeId),
             idListJson(m_modes),
             idListJson(m_pages),
             idListJson(m_collections),
             m_modeFlyout ? QStringLiteral("true") : QStringLiteral("false"));
}

void NavRail::rebuild()
{
    if (!m_layout) return;

    // Tear down everything the layout owns, then re-assemble. NavRail is rebuilt
    // on each setModes/setPages/setCollections; the item sets are small so a
    // full rebuild is cheaper than diffing and keeps ordering deterministic.
    closeModeFlyout();
    m_buttons.clear();
    m_brand = nullptr;
    m_divider = nullptr;
    m_collapseBtn = nullptr;
    m_modeTrigger = nullptr;
    m_pagesHost = nullptr;
    m_collectionsHost = nullptr;

    QLayoutItem* item = nullptr;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    // ── Brand (serif, gold via QSS; click → home) ──────────────────────────
    m_brand = new QLabel(QStringLiteral("Tankoban"), this);
    m_brand->setObjectName(QStringLiteral("Brand"));
    m_brand->setCursor(Qt::PointingHandCursor);
    m_brand->installEventFilter(this);
    m_layout->addWidget(m_brand);
    m_layout->addSpacing(8);

    // ── MODES group ────────────────────────────────────────────────────────
    // Expanded → the four modes are a labeled vertical list (first group).
    // Collapsed → they FOLD into a single trigger button (active mode icon,
    // gold) that opens a sideways flyout. buildModesGroup() picks the shape.
    buildModesGroup();

    // ── Divider (gradient hairline) ──────────────────────────────────────────
    m_divider = new QFrame(this);
    m_divider->setObjectName(QStringLiteral("NavDivider"));
    m_divider->setFixedHeight(1);
    m_layout->addSpacing(6);
    m_layout->addWidget(m_divider);
    m_layout->addSpacing(6);

    // ── PAGES group (stable host — setPages refills this IN PLACE so a mode
    //    switch updates only the pages, leaving brand/modes/footer still) ──────
    m_pagesHost = new QWidget(this);
    m_pagesHost->setObjectName(QStringLiteral("NavGroupHost"));
    auto* pagesLay = new QVBoxLayout(m_pagesHost);
    pagesLay->setContentsMargins(0, 0, 0, 0);
    pagesLay->setSpacing(6);
    m_layout->addWidget(m_pagesHost);
    fillGroup(m_pagesHost, m_pages);

    m_layout->addStretch(1);

    // ── COLLECTIONS group (bottom, stable host) ───────────────────────────────
    m_collectionsHost = new QWidget(this);
    m_collectionsHost->setObjectName(QStringLiteral("NavGroupHost"));
    auto* colLay = new QVBoxLayout(m_collectionsHost);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(6);
    m_layout->addWidget(m_collectionsHost);
    fillGroup(m_collectionsHost, m_collections);

    // ── Collapse toggle (bottom) ──────────────────────────────────────────────
    m_collapseBtn = new QToolButton(this);
    m_collapseBtn->setObjectName(QStringLiteral("NavRailButton"));
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn->setAutoRaise(true);
    m_collapseBtn->setIconSize(QSize(kIconSize, kIconSize));
    const QString glyph = m_collapsed ? QStringLiteral(":/icons/chevron_right.svg")
                                      : QStringLiteral(":/icons/chevron_left.svg");
    m_collapseBtn->setIcon(::Theme::tintedSvgIcon(glyph, QColor(::Theme::current().text), kIconSize));
    m_collapseBtn->setToolTip(QStringLiteral("Collapse navigation"));
    connect(m_collapseBtn, &QToolButton::clicked, this, [this]() {
        setCollapsed(!m_collapsed);
    });
    m_layout->addWidget(m_collapseBtn);

    // Re-apply the active highlight + collapsed state to the freshly-built tree.
    if (!m_activeId.isEmpty()) setActiveId(m_activeId);
    applyCollapsedToButtons();
}

// Refill one group host in place: evict this host's buttons from the shared
// highlight list + delete them, then add the new set (parented to the host so
// they're identifiable). Lets setPages/setCollections update a single group
// without a destructive full rebuild — the rest of the rail stays put.
void NavRail::fillGroup(QWidget* host, const std::vector<Item>& items)
{
    if (!host) return;
    auto* hl = qobject_cast<QVBoxLayout*>(host->layout());
    if (!hl) return;

    m_buttons.erase(std::remove_if(m_buttons.begin(), m_buttons.end(),
                        [host](const std::pair<QString, QPushButton*>& pr) {
                            return pr.second && pr.second->parentWidget() == host;
                        }),
                    m_buttons.end());
    QLayoutItem* li = nullptr;
    while ((li = hl->takeAt(0)) != nullptr) {
        if (QWidget* w = li->widget()) w->deleteLater();
        delete li;
    }

    for (const Item& it : items) {
        auto* btn = new QPushButton(it.label, host);
        btn->setObjectName(QStringLiteral("NavRailButton"));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(railIcon(it.iconPath));
        btn->setIconSize(QSize(kIconSize, kIconSize));
        btn->setProperty("fullText", it.label);
        const QString id = it.id;
        connect(btn, &QPushButton::clicked, this, [this, id]() {
            emit itemActivated(id);
        });
        hl->addWidget(btn);
        m_buttons.emplace_back(id, btn);
    }
}

// Find the active mode's icon path (the collapsed trigger shows it, gold). Falls
// back to the first mode's icon so the trigger always has a glyph.
QString NavRail::activeModeIconPath() const
{
    for (const Item& it : m_modes) {
        if (it.id == m_activeId) return it.iconPath;
    }
    return m_modes.empty() ? QString() : m_modes.front().iconPath;
}

// Build the MODES section into the (currently-empty) modes slot of m_layout.
// Called from rebuild() and whenever the collapse state flips (the trigger↔list
// swap). Adds either the labeled vertical list (expanded) or the single trigger
// (collapsed) and registers the buttons in m_buttons (active-highlight + label
// hide machinery already iterate that list).
void NavRail::buildModesGroup()
{
    if (m_collapsed) {
        // Collapsed: ONE trigger showing the active mode's icon (gold). Clicking
        // it opens the sideways flyout. Registered in m_buttons so setActiveId
        // keeps re-tinting it when the mode changes.
        m_modeTrigger = new QPushButton(this);
        m_modeTrigger->setObjectName(QStringLiteral("NavRailModeTrigger"));
        m_modeTrigger->setCheckable(false);
        m_modeTrigger->setCursor(Qt::PointingHandCursor);
        m_modeTrigger->setIcon(railIconAccent(activeModeIconPath()));
        m_modeTrigger->setIconSize(QSize(kIconSize, kIconSize));
        m_modeTrigger->setToolTip(QStringLiteral("Change mode"));
        m_modeTrigger->setProperty("collapsed", true);
        connect(m_modeTrigger, &QPushButton::clicked, this, [this]() {
            if (m_modeFlyout) closeModeFlyout();
            else showModeFlyout();
        });
        m_layout->addWidget(m_modeTrigger);
        return;
    }

    // Expanded: the four modes as a labeled vertical list (icon + label).
    for (const Item& it : m_modes) {
        auto* btn = new QPushButton(it.label, this);
        btn->setObjectName(QStringLiteral("NavRailButton"));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(railIcon(it.iconPath));
        btn->setIconSize(QSize(kIconSize, kIconSize));
        btn->setProperty("fullText", it.label);
        const QString id = it.id;
        connect(btn, &QPushButton::clicked, this, [this, id]() {
            emit itemActivated(id);
        });
        m_layout->addWidget(btn);
        m_buttons.emplace_back(id, btn);
    }
}

// Collapsed mode flyout — a frameless Qt::Popup holding a horizontal bar of mode
// pills, anchored just past the rail's right edge, vertically aligned to the
// trigger. Qt::Popup auto-closes on outside-click (mirrors Electron's
// click-outside handler). The current mode reads as a filled gold pill.
void NavRail::showModeFlyout()
{
    if (m_modeFlyout || !m_modeTrigger) return;

    auto* flyout = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint
                                         | Qt::NoDropShadowWindowHint);
    flyout->setObjectName(QStringLiteral("NavModeFlyout"));
    flyout->setAttribute(Qt::WA_DeleteOnClose, false);

    auto* row = new QHBoxLayout(flyout);
    row->setContentsMargins(6, 6, 6, 6);   // Electron: padding 6px
    row->setSpacing(6);                     // Electron: gap 6px

    for (const Item& it : m_modes) {
        auto* pill = new QPushButton(flyout);
        pill->setObjectName(QStringLiteral("NavModeFlyoutItem"));
        pill->setCursor(Qt::PointingHandCursor);
        pill->setToolTip(it.label);
        const bool isActive = (it.id == m_activeId);
        pill->setIcon(isActive ? railIconOnAccent(it.iconPath) : railIcon(it.iconPath));
        pill->setIconSize(QSize(kIconSize, kIconSize));
        pill->setProperty("active", isActive);   // QSS [active="true"] → gold-filled pill
        const QString id = it.id;
        connect(pill, &QPushButton::clicked, this, [this, id]() {
            closeModeFlyout();
            emit itemActivated(id);
        });
        row->addWidget(pill);
    }

    m_modeFlyout = flyout;
    flyout->adjustSize();

    // Anchor: rail's right edge + gap, vertically aligned to the trigger top.
    const QPoint railTopRight = mapToGlobal(QPoint(width(), 0));
    const QPoint trigGlobal = m_modeTrigger->mapToGlobal(QPoint(0, 0));
    flyout->move(railTopRight.x() + kFlyoutGap, trigGlobal.y());

    // Repaint the trigger lit while open (QSS [open="true"]).
    m_modeTrigger->setProperty("open", true);
    m_modeTrigger->style()->unpolish(m_modeTrigger);
    m_modeTrigger->style()->polish(m_modeTrigger);

    flyout->show();
}

void NavRail::closeModeFlyout()
{
    if (!m_modeFlyout) return;
    m_modeFlyout->hide();
    m_modeFlyout->deleteLater();
    m_modeFlyout = nullptr;
    if (m_modeTrigger) {
        m_modeTrigger->setProperty("open", false);
        m_modeTrigger->style()->unpolish(m_modeTrigger);
        m_modeTrigger->style()->polish(m_modeTrigger);
    }
}

} // namespace tankoban::ui
