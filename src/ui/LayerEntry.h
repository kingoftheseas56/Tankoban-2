// src/ui/LayerEntry.h
#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace tankoban::ui {

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- one entry on the per-mode
// back stack. Each page constructs entries for its own in-page layers
// (Library, SearchResults, Detail, etc.) and emits them via the page's
// enteredLayer signal. PerModeNavController stores them; topbar Back
// chevron reads back-destination label from peekBack(currentMode). The
// stateBlob is opaque to the controller -- the emitting page is
// responsible for round-trip serialization in its restoreLayer slot.
struct LayerEntry {
    QString     pageId;       // "comics" / "stream" / "books" / etc.
    QString     kind;         // page-local layer kind: "library", "searchResults", "seriesView", "detail", "catalogBrowse", "addonManager", "calendar", "search"
    QString     label;        // human-readable tooltip text, e.g. "Search Results"
    QJsonObject stateBlob;    // page-private state needed to re-render the layer
};

}  // namespace tankoban::ui

Q_DECLARE_METATYPE(tankoban::ui::LayerEntry)
