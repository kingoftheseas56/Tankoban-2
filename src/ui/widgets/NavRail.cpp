// src/ui/widgets/NavRail.cpp
//
// HARBOR_REDESIGN Phase 1 Task 3 (2026-06-15, Agent 5). See NavRail.h.

#include "ui/widgets/NavRail.h"

#include "ui/Theme.h"

#include <QColor>
#include <QEasingCurve>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPointF>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace tankoban::ui {

namespace {

// Rail geometry — spec §4 left rail. Expanded shows icon + label; collapsed
// shrinks to an icon strip. kEasePull ~ cubic-bezier(0.32, 0.72, 0.24, 1)
// (Theme.h authoring note) built below with addCubicBezierSegment.
constexpr int kExpandedW = 210;
constexpr int kCollapsedW = 62;
constexpr int kCollapseDurationMs = 320;
constexpr int kIconSize = 20;

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

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(10, 14, 10, 12);
    m_layout->setSpacing(4);

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
    rebuild();
}

void NavRail::setCollections(const std::vector<Item>& cols)
{
    m_collections = cols;
    rebuild();
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

    applyCollapsedToButtons();

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
    // Labels hide when collapsed (icon-only strip); brand text follows too.
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
                          "\"modes\":%3,\"pages\":%4,\"collections\":%5}")
        .arg(m_collapsed ? QStringLiteral("true") : QStringLiteral("false"),
             jsonEscape(m_activeId),
             idListJson(m_modes),
             idListJson(m_pages),
             idListJson(m_collections));
}

void NavRail::rebuild()
{
    if (!m_layout) return;

    // Tear down everything the layout owns, then re-assemble. NavRail is rebuilt
    // on each setModes/setPages/setCollections; the item sets are small so a
    // full rebuild is cheaper than diffing and keeps ordering deterministic.
    m_buttons.clear();
    m_brand = nullptr;
    m_divider = nullptr;
    m_collapseBtn = nullptr;

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

    auto addGroup = [this](const std::vector<Item>& items) {
        for (const Item& it : items) {
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
    };

    // ── MODES group ────────────────────────────────────────────────────────
    addGroup(m_modes);

    // ── Divider (gradient hairline) ──────────────────────────────────────────
    m_divider = new QFrame(this);
    m_divider->setObjectName(QStringLiteral("NavDivider"));
    m_divider->setFixedHeight(1);
    m_layout->addSpacing(6);
    m_layout->addWidget(m_divider);
    m_layout->addSpacing(6);

    // ── PAGES group ──────────────────────────────────────────────────────────
    addGroup(m_pages);

    m_layout->addStretch(1);

    // ── COLLECTIONS group (bottom) ────────────────────────────────────────────
    addGroup(m_collections);

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

} // namespace tankoban::ui
