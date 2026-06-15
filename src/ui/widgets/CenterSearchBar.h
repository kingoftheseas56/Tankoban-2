// src/ui/widgets/CenterSearchBar.h
//
// HARBOR_REDESIGN Phase 1 Task 5 (2026-06-15, Agent 0) — the window-centered
// frosted search pill. Lives in the top ChromeStrip, dead-center between the
// left rail-action cluster and the right window-chrome cluster (kept centered
// by stretches in buildTopBar). Mirrors the reference's window-centered search.
//
// Pure re-skin over the existing engine: the bar is mode-agnostic. It emits
// searchSubmitted(query) on Enter; MainWindow routes that to the active page's
// own search entry (the same per-page search the pages already own). Placeholder
// tracks the active mode ("Search Theatre…") via setPlaceholder() from
// activatePage(). All colors come from the app QSS (Theme.cpp kTemplate,
// QFrame#CenterSearch + QLineEdit#CenterSearchEdit rules) — no inline styling.
//
// devSnapshot() returns a small JSON string for the dev-bridge introspect-object
// verb (mirrors the NavRail/IDevInspectable spirit).

#pragma once

#include <QFrame>
#include <QString>

class QLineEdit;
class QToolButton;
class QLabel;

namespace tankoban::ui {

class CenterSearchBar : public QFrame {
    Q_OBJECT

public:
    explicit CenterSearchBar(QWidget* parent = nullptr);

    void setPlaceholder(const QString& modeLabel);  // edit placeholder -> "Search <modeLabel>…"
    void focusSearch();                              // setFocus on the edit + selectAll()
    QString text() const;                            // trimmed current text
    void clearSearch();                              // clears edit, hides clear button (no signal)
    QString devSnapshot() const;                     // dev-bridge introspection JSON string

signals:
    void searchSubmitted(const QString& query);      // trimmed, non-empty (Enter)
    void searchCleared();

private:
    QLineEdit* m_searchEdit = nullptr;
    QToolButton* m_clearButton = nullptr;
    QLabel* m_hintLabel = nullptr;
    QString m_placeholderBase;
};

} // namespace tankoban::ui
