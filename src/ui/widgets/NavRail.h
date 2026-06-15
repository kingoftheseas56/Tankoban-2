// src/ui/widgets/NavRail.h
//
// HARBOR_REDESIGN Phase 1 Task 3 (2026-06-15, Agent 5) — the left navigation
// rail. Replaces the centered top-pill bar (wired into MainWindow in Task 4).
//
// Three vertical groups stacked top→bottom:
//   • MODES       — Theatre / Manga / Comics / Books (id == pageId)
//   • PAGES       — the active mode's sub-pages (middle group)
//   • COLLECTIONS — Library / Downloads / Settings (bottom group)
// plus a serif brand label at the top (click → "__home__") and a collapse
// toggle at the very bottom that animates the rail 210↔62 px.
//
// Pure re-skin over the existing engine: every button click emits
// itemActivated(id); MainWindow routes that into the unchanged activatePage()
// contract. setActiveId() gold-highlights the matching button via the QSS
// QPushButton#NavRailButton[active="true"] rule (see Theme.cpp kTemplate).
//
// IDevInspectable spirit: devSnapshot() returns a small JSON string so the
// dev-bridge introspect-object verb can read the rail's live state (collapsed,
// activeId, per-group id lists). The plan's contract fixes the return type as
// QString (not the QJsonObject of the IDevInspectable interface) — followed
// verbatim here; the string is valid JSON.

#pragma once

#include <QFrame>
#include <QString>
#include <utility>
#include <vector>

class QVBoxLayout;
class QPushButton;
class QLabel;
class QToolButton;
class QFrame;
class QPropertyAnimation;
class QEvent;

namespace tankoban::ui {

class NavRail : public QFrame {
    Q_OBJECT
public:
    struct Item { QString id; QString label; QString iconPath; }; // id == pageId for modes

    explicit NavRail(QWidget* parent = nullptr);

    void setModes(const std::vector<Item>& modes);      // top group
    void setPages(const std::vector<Item>& pages);      // active-mode pages (middle group)
    void setCollections(const std::vector<Item>& cols); // bottom group (Library/Downloads/Settings)
    void setActiveId(const QString& id);                // gold-highlights the matching button
    void setCollapsed(bool collapsed);                  // 210 <-> 62 px, animated
    bool isCollapsed() const;
    QString devSnapshot() const;                        // dev-bridge introspection (mirror IDevInspectable)

signals:
    void itemActivated(const QString& id);              // any group click
    void collapseToggled(bool collapsed);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // brand-label click

private:
    void rebuild();
    void applyCollapsedToButtons();

    bool m_collapsed = false;
    QString m_activeId;
    std::vector<Item> m_modes, m_pages, m_collections;
    QVBoxLayout* m_layout = nullptr;
    std::vector<std::pair<QString, QPushButton*>> m_buttons;

    // Chrome the rebuild() owns between the dynamic groups.
    QLabel* m_brand = nullptr;
    QFrame* m_divider = nullptr;
    QToolButton* m_collapseBtn = nullptr;
    QPropertyAnimation* m_widthAnim = nullptr;
};

} // namespace tankoban::ui
