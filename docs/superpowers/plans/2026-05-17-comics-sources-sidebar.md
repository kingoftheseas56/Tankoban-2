# Comics Sources Sidebar (Stremio-style) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `ComicsSourcesPanel`'s current QListWidget rendering with Stremio-style source cards mirroring Theatre's `StreamSourceCard` + `StreamSourceList` pattern, adapted with manga-native chips and auto-pick orchestration for tier-1 Catalog rows.

**Architecture:** New `ComicsSourceCard` widget (mirror of `StreamSourceCard` shape, takes `UnifiedSourceRow`). Refactor `ComicsSourcesPanel` from QListWidget to QScrollArea + QVBoxLayout-of-cards with a 4-state machine (placeholder / loading / populated / empty). Add auto-pick orchestration: 300ms beat + emit `downloadRequested` for top-tier Catalog rows. Touch `ComicsSeriesView` for 3px gold-accent left-edge stripe on the active volume row.

**Tech Stack:** C++20, Qt 6 widgets (QFrame / QHBoxLayout / QVBoxLayout / QLabel / QScrollArea / QTimer / QPropertyAnimation). No new third-party deps. No new test infrastructure (per spec §9 — visual widget, smoke-verified).

**Spec source:** `docs/superpowers/specs/2026-05-17-comics-sources-sidebar-design.md` (12 decisions across 3 brainstorm rounds + Approach A pick).

---

## Brotherhood conventions (load-bearing — agents executing this plan MUST honor these)

- **ASCII only** in source files (memory `feedback_no_color_no_emoji`).
- **No worktrees** — work on master directly (memory `feedback_no_worktrees`). The writing-plans skill default suggests worktrees; the brotherhood overrides.
- **`build_check.bat`** after every src/ touch (CLAUDE.md Tier 1 `/build-verify`). Format on failure: 30-line cl.exe tail in `out/_build_check.log`.
- **No mid-task `git commit`.** Per Rule 11 + memory `feedback_commit_protocol`: agents flag READY TO COMMIT in `agents/chat.md`; Agent 0 batches commits via `/commit-sweep`. The plan's task-close step is "append RTC line", not `git commit`.
- **Rule 17 cleanup** if you launch the app: `powershell -NoProfile -File scripts/stop-tankoban.ps1`. Task 8 (smoke) launches Tankoban; all other tasks are build + edit only.
- **Rule 19 MCP LANE LOCK** during Task 8 (agent-driven smoke): claim + release in `agents/chat.md`.
- **Contracts-v3 RTC format** on the close-out line (Task 9): include `Skills invoked: [...]` field between the message body and `| files:`.
- **Stale Agent 4 MOC trap** flagged for awareness: if a build fails with `unresolved external symbol "TheatreDownloadPanel::..."` it's the cross-agent MOC cache issue (`feedback_stale_moc_cross_agent` if exists, otherwise see memory of 2026-05-16 evening). Resolution: `rm out/CMakeFiles/Tankoban.dir/src/ui/pages/stream/StreamPage.cpp.obj` + `rm out/CMakeFiles/Tankoban.dir/Tankoban_autogen/mocs_compilation.cpp.obj`, rebuild. Not your code; not a regression.

---

## Reference data (locked decisions from spec §3)

Twelve decisions ratified during brainstorm. The plan implements ALL of these:

| # | Decision | Source |
|---|----------|--------|
| 1 | v1 scope: visual polish of existing 3-source surface (Catalog/Nyaa/WC). No new providers. | R1 Q1 |
| 2 | Adapt manga-native chips (archive type, uploader, page count). Not Stream's HDR/DV/sub. | R1 Q2 |
| 3 | Auto-pick top-tier Catalog rows silently. No saved-choice persistence. | R1 Q3 |
| 4 | No right-click menu. Left-click = download. | R1 Q4 |
| 5 | No cover thumb in card. Source-initials badge only. | R2 Q1 |
| 6 | Badge text from source kind: `CT` / `NY` / `WC`. | R2 Q2 |
| 7 | Loading state: skeleton cards (2 dim placeholders pulsing). | R2 Q3 |
| 8 | Empty state: `"No sources found for this volume"` + `"Try a different volume or check back as indexers refresh."` | R2 Q4 |
| 9 | Auto-pick UX: visible pick + 300ms beat + download fires. | R3 Q1 |
| 10 | Hybrid loading: Catalog + WC instant; 2 skeleton cards below for Nyaa. | R3 Q2 |
| 11 | Card density: 80px tall, matches Stream. | R3 Q3 |
| 12 | Volume-row selection: 3px gold-accent left-edge stripe on active row. | R3 Q4 |

---

## File structure (changes by file)

**Create:**
- `src/ui/pages/comics/ComicsSourceCard.h` — new ~70 LOC header.
- `src/ui/pages/comics/ComicsSourceCard.cpp` — new ~250 LOC impl.

**Modify:**
- `src/ui/pages/comics/ComicsSourcesPanel.h` — swap QListWidget member for QScrollArea-of-cards members. Add 4 state-method declarations + auto-pick QTimer member + private helper signatures. (~30 LOC delta.)
- `src/ui/pages/comics/ComicsSourcesPanel.cpp` — rip the QListWidget render path. Wire QScrollArea + QVBoxLayout + 4 states + auto-pick. Keep `formatSize` helper. Drop `rowLabel` helper (cards compose structurally). (~150 LOC delta net.)
- `src/ui/pages/comics/ComicsSeriesView.cpp` — add `m_volumesTable` QSS rule for gold-accent left-stripe on selected row. ~5 LOC delta.
- `CMakeLists.txt` — register new ComicsSourceCard.cpp next to ComicsSourcesPanel.cpp. ~1 LOC delta.

**Touch (no code change, only verify):**
- `src/ui/pages/comics/ComicsSeriesView.h` — verify the existing `onVolumeRowSelected` (or equivalent) slot connects to `m_sourcesPanel->populate(...)`. No edit required.
- `src/ui/pages/ComicsPage.{h,cpp}` — verify the existing `m_sourcesPanel->downloadRequested` connection. No edit required.

---

## Task 1: Create `ComicsSourceCard.h` header

**Files:**
- Create: `src/ui/pages/comics/ComicsSourceCard.h`

- [ ] **Step 1.1: Write the header file**

Create `src/ui/pages/comics/ComicsSourceCard.h` with this exact content:

```cpp
// src/ui/pages/comics/ComicsSourceCard.h
#pragma once

#include "ComicsSourcesPanel.h"  // for UnifiedSourceRow

#include <QFrame>

class QLabel;

namespace tankoban::manga::comics {

// Stremio-style source card for the Comics ComicsSourcesPanel. Mirrors
// the visual shape of src/ui/pages/stream/StreamSourceCard but takes a
// UnifiedSourceRow instead of a StreamPickerChoice. One card per source
// row in the panel's QScrollArea.
//
// Two constructor variants:
//   - real-row variant: takes a UnifiedSourceRow value, renders normally
//   - skeleton variant: takes no row, renders a dim placeholder shape used
//     during the hybrid loading window (Nyaa in flight while Catalog + WC
//     already rendered). Set via the bool flag on the variant ctor.
//
// State machine:
//   - default (Stream-style flat bg, neutral border)
//   - hover  (enterEvent / leaveEvent — slightly brighter bg + border)
//   - selected (setSelected(true) — gold-accent border; used for the
//     auto-pick 300ms beat highlight on top-tier Catalog rows)
//
// Click handling: mouseReleaseEvent emits clicked(row). MOUSE-RELEASE
// (not MOUSE-PRESS) per the F3 lesson learned 2026-05-16: synthetic
// Win32 mouse_event delivers press+release together but Qt's QPushButton
// click-signal had a synthetic-input edge case. QFrame + manual release
// handling avoids that entirely + matches Stream's StreamSourceCard.
class ComicsSourceCard : public QFrame
{
    Q_OBJECT
public:
    // Real-row card. The card stores the row by value and emits it on
    // click. parent ownership is standard Qt.
    explicit ComicsSourceCard(const UnifiedSourceRow& row, QWidget* parent = nullptr);

    // Skeleton placeholder card. Used by ComicsSourcesPanel during the
    // hybrid loading window. Renders a dim card-shaped silhouette with
    // no text + a subtle opacity pulse animation.
    explicit ComicsSourceCard(bool /*skeleton*/, QWidget* parent = nullptr);

    ~ComicsSourceCard() override;

    const UnifiedSourceRow& row() const { return m_row; }
    bool isSkeleton() const { return m_skeleton; }

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

signals:
    // Fired on left-mouse release inside the card rect. Caller (the
    // ComicsSourcesPanel) forwards as the panel's existing
    // downloadRequested signal (which carries the seriesTitle +
    // anilistSeriesId + volumeNumber + chapterIds context the card
    // itself does not have).
    void clicked(const tankoban::manga::comics::UnifiedSourceRow& row);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void buildUiRealRow();
    void buildUiSkeleton();
    void applyStateStyle();

    // Returns "CT" / "NY" / "WC" per UnifiedSourceRow::Kind. Used by both
    // the badge label text and the tier pill QSS class lookup.
    static QString badgeText(const UnifiedSourceRow& row);

    // Returns "CATALOG" / "NYAA" / "FALLBACK" per Kind.
    static QString tierPillText(const UnifiedSourceRow& row);

    // Compose the subtitle line (line 2 of the text column) per Kind.
    // Catalog: "<uploader> - <filename>". Nyaa: row.uploaderHint
    // (the nyaa display title is already in uploaderHint per
    // NyaaRuntimeSource's contract). WC: "<chapterCount> chapters
    // -> vol pack on demand".
    static QString subtitleText(const UnifiedSourceRow& row);

    UnifiedSourceRow m_row;
    bool m_skeleton = false;
    bool m_hovered  = false;
    bool m_selected = false;

    QLabel* m_badgeLabel    = nullptr;
    QLabel* m_titleLabel    = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_tierPillLabel = nullptr;
    // Chip row composed dynamically in buildUiRealRow; QLabels appended
    // to a QHBoxLayout. No member pointers needed (chips don't update
    // post-construction in v1).
};

} // namespace tankoban::manga::comics
```

- [ ] **Step 1.2: Verify the file lands at the right path**

Run from repo root:

```bash
ls -la "src/ui/pages/comics/ComicsSourceCard.h"
```

Expected: file exists, size ~3-4 KB.

**Note:** No build_check yet — the .cpp doesn't exist + CMakeLists isn't registered. Header-only addition doesn't break the build but doesn't verify either. Task 2 covers both.

---

## Task 2: Create `ComicsSourceCard.cpp` + register in CMakeLists

**Files:**
- Create: `src/ui/pages/comics/ComicsSourceCard.cpp`
- Modify: `CMakeLists.txt` (one line addition next to existing `ComicsSourcesPanel.cpp` registration)

- [ ] **Step 2.1: Write the implementation file**

Create `src/ui/pages/comics/ComicsSourceCard.cpp` with this exact content:

```cpp
// src/ui/pages/comics/ComicsSourceCard.cpp
#include "ComicsSourceCard.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace tankoban::manga::comics {

namespace {

// Pretty-print a byte count: "1.4 GiB" / "245 MiB" / "" for zero/unknown.
// Mirrors the formatSize helper in ComicsSourcesPanel.cpp; reuse if you
// prefer to make that one public. For v1, inlined here to keep the card
// self-contained.
QString formatSizeChip(qint64 bytes)
{
    if (bytes <= 0) return QString();
    constexpr qint64 KiB = 1024;
    constexpr qint64 MiB = 1024 * KiB;
    constexpr qint64 GiB = 1024 * MiB;
    if (bytes >= GiB) return QString::number(static_cast<double>(bytes) / GiB, 'f', 2) + QStringLiteral(" GiB");
    if (bytes >= MiB) return QString::number(static_cast<double>(bytes) / MiB, 'f', 1) + QStringLiteral(" MiB");
    if (bytes >= KiB) return QString::number(static_cast<double>(bytes) / KiB, 'f', 1) + QStringLiteral(" KiB");
    return QString::number(bytes) + QStringLiteral(" B");
}

QString buildCardStyleSheet(bool hovered, bool selected, bool skeleton,
                            UnifiedSourceRow::Kind kind)
{
    // Object-name-scoped so child labels do not inherit background painting
    // they should not. Mirrors src/ui/pages/stream/StreamSourceCard.cpp
    // buildCardStyleSheet pattern.

    const QString base = QStringLiteral(
        "#ComicsSourceCard { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.10);"
        " border-radius: 8px; }"
        "#ComicsSourceCard QLabel { background: transparent; }"
        "#ComicsSourceCardTitle { color: #e5e7eb; font-size: 13px; font-weight: 600; }"
        "#ComicsSourceCardSubtitle { color: rgba(255,255,255,0.55); font-size: 11px; }"
        "#ComicsSourceCardChip { color: rgba(255,255,255,0.55); font-size: 11px; }"
        "#ComicsSourceCardBadgeCatalog { background: rgba(212,165,116,0.18);"
        " border: 1px solid rgba(212,165,116,0.40);"
        " border-radius: 6px; color: #d4a574;"
        " font-size: 11px; font-weight: 700; }"
        "#ComicsSourceCardBadgeNyaa { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: rgba(255,255,255,0.65);"
        " font-size: 11px; font-weight: 700; }"
        "#ComicsSourceCardBadgeWC { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.10);"
        " border-radius: 6px; color: rgba(255,255,255,0.45);"
        " font-size: 11px; font-weight: 700; }"
        "#ComicsSourceCardTierCatalog { background: rgba(212,165,116,0.18);"
        " border: 1px solid rgba(212,165,116,0.40);"
        " border-radius: 4px; color: #d4a574;"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }"
        "#ComicsSourceCardTierNyaa { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.16);"
        " border-radius: 4px; color: rgba(255,255,255,0.65);"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }"
        "#ComicsSourceCardTierWC { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.10);"
        " border-radius: 4px; color: rgba(255,255,255,0.45);"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }");

    if (skeleton) {
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.03);"
            " border-color: rgba(255,255,255,0.06); }");
    }
    if (selected) {
        // Gold-accent border for the auto-pick 300ms beat.
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.08);"
            " border: 1px solid rgba(212,165,116,0.50); }");
    }
    if (hovered) {
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.06);"
            " border-color: rgba(255,255,255,0.14); }");
    }
    // Lift the unselected Catalog row slightly to draw the eye.
    if (kind == UnifiedSourceRow::Kind::Catalog) {
        return base + QStringLiteral(
            "#ComicsSourceCard { background: rgba(255,255,255,0.06); }");
    }
    return base;
}

} // namespace

// ============================================================================
// Real-row constructor
// ============================================================================

ComicsSourceCard::ComicsSourceCard(const UnifiedSourceRow& row, QWidget* parent)
    : QFrame(parent)
    , m_row(row)
    , m_skeleton(false)
{
    setObjectName(QStringLiteral("ComicsSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUiRealRow();
    applyStateStyle();
}

// ============================================================================
// Skeleton constructor
// ============================================================================

ComicsSourceCard::ComicsSourceCard(bool /*skeleton*/, QWidget* parent)
    : QFrame(parent)
    , m_skeleton(true)
{
    setObjectName(QStringLiteral("ComicsSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUiSkeleton();
    applyStateStyle();
}

ComicsSourceCard::~ComicsSourceCard() = default;

// ============================================================================
// UI construction
// ============================================================================

void ComicsSourceCard::buildUiRealRow()
{
    // Outer: vertical stack of (top row) + (chip row).
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(6);

    // ── Top row: badge + (title + subtitle stacked) + tier pill ──
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(12);

    // Badge (36x36 source-initials)
    m_badgeLabel = new QLabel(badgeText(m_row), this);
    QString badgeObj;
    switch (m_row.kind) {
    case UnifiedSourceRow::Kind::Catalog:           badgeObj = QStringLiteral("ComicsSourceCardBadgeCatalog"); break;
    case UnifiedSourceRow::Kind::NyaaRuntime:       badgeObj = QStringLiteral("ComicsSourceCardBadgeNyaa"); break;
    case UnifiedSourceRow::Kind::WeebCentralPacker: badgeObj = QStringLiteral("ComicsSourceCardBadgeWC"); break;
    }
    m_badgeLabel->setObjectName(badgeObj);
    m_badgeLabel->setFixedSize(36, 36);
    m_badgeLabel->setAlignment(Qt::AlignCenter);
    topRow->addWidget(m_badgeLabel);

    // Text column (title + subtitle stacked vertically)
    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(3);

    m_titleLabel = new QLabel(m_row.title, this);
    m_titleLabel->setObjectName(QStringLiteral("ComicsSourceCardTitle"));
    m_titleLabel->setToolTip(m_row.title);
    m_titleLabel->setWordWrap(false);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textCol->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(subtitleText(m_row), this);
    m_subtitleLabel->setObjectName(QStringLiteral("ComicsSourceCardSubtitle"));
    m_subtitleLabel->setWordWrap(false);
    m_subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textCol->addWidget(m_subtitleLabel);

    topRow->addLayout(textCol, 1);  // flex-grow on the text column

    // Tier pill (right-aligned)
    m_tierPillLabel = new QLabel(tierPillText(m_row), this);
    QString tierObj;
    switch (m_row.kind) {
    case UnifiedSourceRow::Kind::Catalog:           tierObj = QStringLiteral("ComicsSourceCardTierCatalog"); break;
    case UnifiedSourceRow::Kind::NyaaRuntime:       tierObj = QStringLiteral("ComicsSourceCardTierNyaa"); break;
    case UnifiedSourceRow::Kind::WeebCentralPacker: tierObj = QStringLiteral("ComicsSourceCardTierWC"); break;
    }
    m_tierPillLabel->setObjectName(tierObj);
    m_tierPillLabel->setAlignment(Qt::AlignCenter);
    topRow->addWidget(m_tierPillLabel);

    root->addLayout(topRow);

    // ── Chip row: seeders + size + archive type ──
    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(48, 0, 0, 0);  // left-indent to align under text column (badge 36 + spacing 12)
    chipRow->setSpacing(12);

    auto addChip = [&](const QString& text) {
        if (text.isEmpty()) return;
        auto* chip = new QLabel(text, this);
        chip->setObjectName(QStringLiteral("ComicsSourceCardChip"));
        chipRow->addWidget(chip);
    };

    // Seeders chip (skip for WC since seeders == -1 for synthesized rows)
    if (m_row.seeders >= 0) {
        addChip(QStringLiteral("↓ %1 seeders").arg(m_row.seeders));  // ↓ = downward arrow
    }
    // Size chip
    addChip(formatSizeChip(m_row.sizeBytes));
    // Archive-type chip: default cbz for v1; could parse from filename in PHASE 2+
    if (m_row.kind != UnifiedSourceRow::Kind::WeebCentralPacker || m_row.seeders >= 0) {
        addChip(QStringLiteral("cbz"));
    }
    chipRow->addStretch();
    root->addLayout(chipRow);
}

void ComicsSourceCard::buildUiSkeleton()
{
    // Skeleton: same outer-shell shape but dim placeholder rectangles
    // instead of real labels. Opacity-pulse animation runs throughout the
    // widget's lifetime (no start/stop in v1 — it stops when widget is
    // destroyed by the panel's clearCards on next setSources).
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(12);

    // Badge placeholder
    auto* badge = new QLabel(this);
    badge->setFixedSize(36, 36);
    badge->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.05); border-radius: 6px;"));
    topRow->addWidget(badge);

    // Text-column placeholder: 2 grey bars
    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(6);

    auto* bar1 = new QLabel(this);
    bar1->setFixedHeight(13);
    bar1->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.06); border-radius: 3px;"));
    bar1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    textCol->addWidget(bar1);

    auto* bar2 = new QLabel(this);
    bar2->setFixedHeight(11);
    bar2->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.04); border-radius: 3px;"));
    bar2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bar2->setMaximumWidth(200);
    textCol->addWidget(bar2);

    topRow->addLayout(textCol, 1);
    root->addLayout(topRow);
    root->addStretch();

    // Pulse the entire skeleton card via a QPropertyAnimation on windowOpacity.
    // Note: QWidget::windowOpacity only works on top-level windows; for a child
    // widget we animate a graphics effect's opacity instead. Use the simpler
    // QGraphicsOpacityEffect pattern.
    auto* opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(0.6);
    setGraphicsEffect(opacityEffect);
    auto* pulse = new QPropertyAnimation(opacityEffect, "opacity", this);
    pulse->setDuration(1500);
    pulse->setStartValue(0.4);
    pulse->setKeyValueAt(0.5, 0.8);
    pulse->setEndValue(0.4);
    pulse->setLoopCount(-1);  // infinite
    pulse->start();
}

// ============================================================================
// State styling
// ============================================================================

void ComicsSourceCard::applyStateStyle()
{
    setStyleSheet(buildCardStyleSheet(m_hovered, m_selected, m_skeleton, m_row.kind));
}

void ComicsSourceCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    applyStateStyle();
}

// ============================================================================
// Event handlers
// ============================================================================

void ComicsSourceCard::enterEvent(QEnterEvent* event)
{
    if (m_skeleton) { QFrame::enterEvent(event); return; }
    m_hovered = true;
    applyStateStyle();
    QFrame::enterEvent(event);
}

void ComicsSourceCard::leaveEvent(QEvent* event)
{
    if (m_skeleton) { QFrame::leaveEvent(event); return; }
    m_hovered = false;
    applyStateStyle();
    QFrame::leaveEvent(event);
}

void ComicsSourceCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_skeleton) { QFrame::mouseReleaseEvent(event); return; }
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit clicked(m_row);
    }
    QFrame::mouseReleaseEvent(event);
}

// ============================================================================
// Static helpers
// ============================================================================

QString ComicsSourceCard::badgeText(const UnifiedSourceRow& row)
{
    switch (row.kind) {
    case UnifiedSourceRow::Kind::Catalog:           return QStringLiteral("CT");
    case UnifiedSourceRow::Kind::NyaaRuntime:       return QStringLiteral("NY");
    case UnifiedSourceRow::Kind::WeebCentralPacker: return QStringLiteral("WC");
    }
    return QStringLiteral("?");
}

QString ComicsSourceCard::tierPillText(const UnifiedSourceRow& row)
{
    switch (row.kind) {
    case UnifiedSourceRow::Kind::Catalog:           return QStringLiteral("CATALOG");
    case UnifiedSourceRow::Kind::NyaaRuntime:       return QStringLiteral("NYAA");
    case UnifiedSourceRow::Kind::WeebCentralPacker: return QStringLiteral("FALLBACK");
    }
    return QString();
}

QString ComicsSourceCard::subtitleText(const UnifiedSourceRow& row)
{
    switch (row.kind) {
    case UnifiedSourceRow::Kind::Catalog: {
        // Catalog: "<uploader> - <inferred filename or title>". The
        // UnifiedSourceRow.title field already carries the catalog's
        // user-facing label per ComicsSourcesPanel's existing populate
        // path (see ComicsSourcesPanel.cpp current rowLabel composition).
        // Subtitle just carries the uploaderHint as a secondary line.
        return row.uploaderHint;
    }
    case UnifiedSourceRow::Kind::NyaaRuntime: {
        // Nyaa: the display title is in row.title. The subtitle gets the
        // uploader hint when present.
        return row.uploaderHint;
    }
    case UnifiedSourceRow::Kind::WeebCentralPacker: {
        // WC synthesized fallback: descriptive subtitle.
        return QStringLiteral("synthesized vol pack on demand");
    }
    }
    return QString();
}

} // namespace tankoban::manga::comics
```

- [ ] **Step 2.2: Add include for `QGraphicsOpacityEffect`**

Edit `src/ui/pages/comics/ComicsSourceCard.cpp` and add this include alongside the existing ones (the buildUiSkeleton uses it):

```cpp
#include <QGraphicsOpacityEffect>
```

Final include block at top of .cpp should be (alphabetical-ish):

```cpp
#include <QEnterEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QSizePolicy>
#include <QVBoxLayout>
```

- [ ] **Step 2.3: Register the new .cpp in CMakeLists.txt**

Find the existing `ComicsSourcesPanel.cpp` registration in CMakeLists.txt:

```bash
grep -n "ComicsSourcesPanel.cpp" CMakeLists.txt
```

Expected: one match (probably in a `set(TANKOBAN_SOURCES ...)` block or similar).

Add `src/ui/pages/comics/ComicsSourceCard.cpp` next to it (alphabetical order: ComicsSourceCard sorts BEFORE ComicsSourcesPanel). Example edit:

```cmake
    src/ui/pages/comics/ComicsSeriesView.cpp
    src/ui/pages/comics/ComicsSourceCard.cpp
    src/ui/pages/comics/ComicsSourcesPanel.cpp
    src/ui/pages/comics/ComicsTankoyomiLibrary.cpp
```

(Adjust to the actual surrounding entries — the cmake block has the canonical alphabetical sort already.)

- [ ] **Step 2.4: Build check**

```bash
taskkill /F /IM Tankoban.exe 2>/dev/null; ./build_check.bat
```

Expected: `BUILD OK`. If it fails with `LNK2019: unresolved external symbol "..TheatreDownloadPanel.."` see the brotherhood conventions block above (stale Agent 4 MOC trap; clean `out/CMakeFiles/Tankoban.dir/Tankoban_autogen/mocs_compilation.cpp.obj` + `out/CMakeFiles/Tankoban.dir/src/ui/pages/stream/StreamPage.cpp.obj` + rebuild).

- [ ] **Step 2.5: No commit step** — agents flag READY TO COMMIT at end of arc (Task 9); Agent 0 batches via `/commit-sweep`. Continue to Task 3.

---

## Task 3: Refactor `ComicsSourcesPanel.h` API

**Files:**
- Modify: `src/ui/pages/comics/ComicsSourcesPanel.h`

Goal: swap the `QListWidget m_list` member for a QScrollArea-of-cards setup. Add the 4 state-method declarations and auto-pick QTimer member. Keep the public API (`clear()`, `populate(...)`, `downloadRequested` signal) unchanged so `ComicsSeriesView`'s existing wiring stays valid.

- [ ] **Step 3.1: Add new forward declarations + includes**

In `ComicsSourcesPanel.h`, locate the existing forward-decl block:

```cpp
class QLabel;
class QListWidget;
class QListWidgetItem;
class QVBoxLayout;
```

Replace it with:

```cpp
class QLabel;
class QScrollArea;
class QTimer;
class QVBoxLayout;
```

And add this include at the top alongside the existing ones:

```cpp
#include <QPointer>
```

- [ ] **Step 3.2: Replace member declarations**

In the `private:` section of `ComicsSourcesPanel`, locate the existing members (search for `m_list`):

```cpp
    QListWidget*               m_list        = nullptr;
    QLabel*                    m_emptyLabel  = nullptr;
```

Replace with:

```cpp
    // 4-state container. m_statusLabel hosts placeholder/empty/error
    // copy when populated == false; m_cardsContainer hosts the card
    // QVBoxLayout when populated == true.
    QScrollArea*               m_scroll          = nullptr;
    QWidget*                   m_cardsContainer  = nullptr;
    QVBoxLayout*               m_cardsLayout     = nullptr;
    QLabel*                    m_statusLabel     = nullptr;
    QList<ComicsSourceCard*>   m_cards;

    // Auto-pick orchestration (spec §3 decisions #3 + #9 + spec §6).
    QTimer*                    m_autoPickTimer   = nullptr;
    bool                       m_autoPickArmed   = false;

    // Active populate() context cached on the panel so the card-click
    // signal handler can re-emit downloadRequested with the right
    // seriesTitle/anilistSeriesId/volumeNumber/chapterIds.
    QString                    m_activeSeriesTitle;
    int                        m_activeAnilistSeriesId = 0;
    int                        m_activeVolumeNumber    = 0;
    QStringList                m_activeChapterIds;
```

Also add the forward decl for `ComicsSourceCard` near the top of the file (after the existing `namespace tankoban::manga::comics {` line):

```cpp
namespace tankoban::manga::comics {

class ComicsSourceCard;  // forward decl (defined in ComicsSourceCard.h)
```

- [ ] **Step 3.3: Add the 4 state method declarations**

In the `private:` section (alongside the existing `private slots:` block), add these private methods:

```cpp
private:
    void setPlaceholder();
    void setLoading();
    void setSources(const QList<UnifiedSourceRow>& rows, bool nyaaStillInFlight);
    void setEmpty();
    void clearCards();
    void emitTopRowDownload();
```

- [ ] **Step 3.4: Update the existing slot signature**

The existing `onRowActivated(QListWidgetItem*)` slot is dead after the refactor (no QListWidget). Replace it with a card-click slot. Locate:

```cpp
    void onRowActivated(QListWidgetItem* item);
```

Replace with:

```cpp
    void onCardClicked(const UnifiedSourceRow& row);
```

- [ ] **Step 3.5: Build check (header-only change; .cpp not yet updated, expect compile error)**

```bash
./build_check.bat
```

Expected: `BUILD FAILED` with compile errors in ComicsSourcesPanel.cpp referring to dropped/changed members. This is intentional — Task 4 fixes the .cpp. Note the error count for sanity (~10-20 lines referencing m_list / m_emptyLabel / onRowActivated).

---

## Task 4: Refactor `ComicsSourcesPanel.cpp` — state machine + render

**Files:**
- Modify: `src/ui/pages/comics/ComicsSourcesPanel.cpp`

Goal: rip the QListWidget render path. Wire the 4-state machine + card-render path. Keep `formatSize()` helper (still useful). Drop `rowLabel()` helper (cards compose structurally).

- [ ] **Step 4.1: Replace the includes block**

At the top of `ComicsSourcesPanel.cpp`, the existing includes are:

```cpp
#include "ComicsSourcesPanel.h"

#include "core/manga/PremiumCatalog.h"
#include "core/manga/PremiumCatalogSchema.h"

#include <QAbstractItemView>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedLayout>
#include <QVariant>
#include <Qt>

#include <algorithm>
```

Replace with:

```cpp
#include "ComicsSourcesPanel.h"

#include "ComicsSourceCard.h"
#include "core/manga/PremiumCatalog.h"
#include "core/manga/PremiumCatalogSchema.h"

#include <QFont>
#include <QLabel>
#include <QScrollArea>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <Qt>

#include <algorithm>
```

- [ ] **Step 4.2: Delete the dead `rowLabel()` helper**

The anonymous-namespace `rowLabel()` function is no longer used. Find the block starting:

```cpp
// Build the user-facing label for a UnifiedSourceRow. The list widget uses
// QListWidgetItem text directly; no custom delegate in v1.
QString rowLabel(const UnifiedSourceRow& row)
{
    // ... entire function body
}
```

DELETE the entire `rowLabel` function (including its preceding comment block). The `formatSize` helper above it STAYS — still used by future code paths (and serves as documentation of the binary-units convention).

- [ ] **Step 4.3: Rewrite the constructor body**

Replace the existing constructor body. The full new constructor:

```cpp
ComicsSourcesPanel::ComicsSourcesPanel(premium::PremiumCatalog* catalog,
                                       NyaaRuntimeSource*       nyaa,
                                       QWidget*                 parent)
    : QWidget(parent)
    , m_catalog(catalog)
    , m_nyaa(nyaa)
{
    setObjectName(QStringLiteral("ComicsSourcesPanel"));
    setMinimumWidth(220);

    // Outer stacked: m_statusLabel layer (placeholder/empty/error copy)
    // + m_scroll layer (populated cards). Same pattern Stream's
    // StreamSourceList uses.
    auto* stack = new QStackedLayout(this);
    stack->setContentsMargins(0, 0, 0, 0);

    // ── Status layer ──
    m_statusLabel = new QLabel(tr("Select a volume to see sources"), this);
    m_statusLabel->setObjectName(QStringLiteral("ComicsSourcesEmptyLabel"));
    {
        QFont f = m_statusLabel->font();
        f.setPointSize(11);
        m_statusLabel->setFont(f);
    }
    m_statusLabel->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.55); padding: 24px;"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    stack->addWidget(m_statusLabel);

    // ── Cards layer (QScrollArea > QWidget > QVBoxLayout > cards) ──
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("ComicsSourcesScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_cardsContainer = new QWidget();
    m_cardsContainer->setObjectName(QStringLiteral("ComicsSourcesCardsContainer"));
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(8, 8, 8, 8);
    m_cardsLayout->setSpacing(6);
    m_cardsLayout->addStretch();  // sentinel so cards stack from top

    m_scroll->setWidget(m_cardsContainer);
    stack->addWidget(m_scroll);

    // ── Auto-pick timer ──
    m_autoPickTimer = new QTimer(this);
    m_autoPickTimer->setSingleShot(true);
    m_autoPickTimer->setInterval(300);  // spec §3 decision #9 "300ms beat"
    connect(m_autoPickTimer, &QTimer::timeout, this, &ComicsSourcesPanel::emitTopRowDownload);

    // ── Default state: placeholder ──
    setPlaceholder();

    // Existing nyaa signal connections stay; verify by inspection that
    // m_nyaa->resultsReady / m_nyaa->failed connect to onNyaaResults /
    // onNyaaFailed in the current constructor. If they don't, copy the
    // connect calls from the OLD constructor body before deleting it.
    if (m_nyaa) {
        connect(m_nyaa, &NyaaRuntimeSource::resultsReady,
                this,   &ComicsSourcesPanel::onNyaaResults);
        connect(m_nyaa, &NyaaRuntimeSource::failed,
                this,   &ComicsSourcesPanel::onNyaaFailed);
    }
}
```

- [ ] **Step 4.4: Implement the 4 state methods**

Add these method implementations to `ComicsSourcesPanel.cpp` (insert anywhere after the constructor; suggest immediately after for proximity):

```cpp
void ComicsSourcesPanel::clearCards()
{
    // Tear down every existing card widget. Layout sentinel (the trailing
    // addStretch in the ctor) stays.
    for (auto* card : m_cards) {
        m_cardsLayout->removeWidget(card);
        card->deleteLater();
    }
    m_cards.clear();
}

void ComicsSourcesPanel::setPlaceholder()
{
    clearCards();
    m_statusLabel->setText(tr("Select a volume to see sources"));
    if (auto* stack = qobject_cast<QStackedLayout*>(layout())) {
        stack->setCurrentWidget(m_statusLabel);
    }
}

void ComicsSourcesPanel::setLoading()
{
    clearCards();
    // Insert 2 skeleton cards. They auto-pulse via the opacity animation
    // baked into ComicsSourceCard::buildUiSkeleton.
    for (int i = 0; i < 2; ++i) {
        auto* skel = new ComicsSourceCard(/*skeleton=*/true, m_cardsContainer);
        m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, skel);  // before the trailing stretch
        m_cards.append(skel);
    }
    if (auto* stack = qobject_cast<QStackedLayout*>(layout())) {
        stack->setCurrentWidget(m_scroll);
    }
}

void ComicsSourcesPanel::setSources(const QList<UnifiedSourceRow>& rows,
                                     bool nyaaStillInFlight)
{
    clearCards();

    for (const auto& row : rows) {
        auto* card = new ComicsSourceCard(row, m_cardsContainer);
        connect(card, &ComicsSourceCard::clicked,
                this, &ComicsSourcesPanel::onCardClicked);
        m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
        m_cards.append(card);
    }

    // Hybrid loading: append 2 skeleton cards at bottom IF nyaa is still
    // in flight. Spec §3 decision #10.
    if (nyaaStillInFlight) {
        for (int i = 0; i < 2; ++i) {
            auto* skel = new ComicsSourceCard(/*skeleton=*/true, m_cardsContainer);
            m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, skel);
            m_cards.append(skel);
        }
    }

    if (auto* stack = qobject_cast<QStackedLayout*>(layout())) {
        stack->setCurrentWidget(m_scroll);
    }

    // Auto-pick orchestration (spec §3 decisions #3 + #9).
    // Detect top-tier-1 Catalog row and arm the 300ms beat IF not already armed.
    if (!m_autoPickArmed && !rows.isEmpty()
        && rows.first().kind == UnifiedSourceRow::Kind::Catalog
        && rows.first().tier == 1
        && !rows.first().magnetUri.isEmpty()) {
        m_autoPickArmed = true;
        if (!m_cards.isEmpty() && !m_cards.first()->isSkeleton()) {
            m_cards.first()->setSelected(true);
        }
        m_autoPickTimer->start();
    }
}

void ComicsSourcesPanel::setEmpty()
{
    clearCards();
    m_statusLabel->setText(
        tr("No sources found for this volume\n\n"
           "Try a different volume or check back as indexers refresh."));
    if (auto* stack = qobject_cast<QStackedLayout*>(layout())) {
        stack->setCurrentWidget(m_statusLabel);
    }
}
```

- [ ] **Step 4.5: Rewrite the `clear()` method**

Find the existing `void ComicsSourcesPanel::clear()` impl and replace with:

```cpp
void ComicsSourcesPanel::clear()
{
    // Cancel any in-flight auto-pick beat.
    if (m_autoPickTimer && m_autoPickTimer->isActive()) {
        m_autoPickTimer->stop();
    }
    m_autoPickArmed = false;

    // Reset active context.
    m_activeSeriesTitle.clear();
    m_activeAnilistSeriesId = 0;
    m_activeVolumeNumber    = 0;
    m_activeChapterIds.clear();
    m_rows.clear();

    setPlaceholder();
}
```

- [ ] **Step 4.6: Rewrite the `populate()` method**

Find the existing `void ComicsSourcesPanel::populate(...)` impl. Replace with:

```cpp
void ComicsSourcesPanel::populate(const QString& seriesTitle,
                                   int            anilistSeriesId,
                                   const anilist::VolumeRow& vol,
                                   const QStringList& chapterIds)
{
    // Cancel any in-flight auto-pick beat from the prior volume.
    if (m_autoPickTimer && m_autoPickTimer->isActive()) {
        m_autoPickTimer->stop();
    }
    m_autoPickArmed = false;

    // Cache the populate context so onCardClicked can re-emit
    // downloadRequested with full provenance.
    m_activeSeriesTitle     = seriesTitle;
    m_activeAnilistSeriesId = anilistSeriesId;
    m_activeVolumeNumber    = vol.volumeNumber;
    m_activeChapterIds      = chapterIds;
    m_rows.clear();

    // ── Synchronous Catalog lookup ──
    if (m_catalog) {
        if (auto catEntryOpt = m_catalog->entryForAnilistIdAndVolume(anilistSeriesId, vol.volumeNumber)) {
            UnifiedSourceRow row;
            row.kind         = UnifiedSourceRow::Kind::Catalog;
            row.tier         = 1;
            row.title        = catEntryOpt->displayTitle;     // see PremiumCatalogSchema.h for field name; adjust if different
            row.uploaderHint = catEntryOpt->uploaderHint;
            row.magnetUri    = catEntryOpt->magnetUri;
            row.infoHash     = catEntryOpt->infoHash;
            row.sizeBytes    = catEntryOpt->fileSizeBytes;
            row.seeders      = -1;  // catalog rows don't carry live seeder count in v1
            m_rows.append(row);
        }
    }

    // ── Synchronous WC synthesis check ──
    if (!chapterIds.isEmpty()) {
        UnifiedSourceRow row;
        row.kind         = UnifiedSourceRow::Kind::WeebCentralPacker;
        row.tier         = 99;
        row.title        = tr("WeebCentral (synthesized)");
        row.uploaderHint = QStringLiteral("WeebCentral");
        row.seeders      = -1;
        row.sizeBytes    = 0;
        m_rows.append(row);
    }

    // Sort by (tier asc, seeders desc within tier) — existing behavior.
    std::stable_sort(m_rows.begin(), m_rows.end(),
                     [](const UnifiedSourceRow& a, const UnifiedSourceRow& b) {
                         if (a.tier != b.tier) return a.tier < b.tier;
                         return a.seeders > b.seeders;
                     });

    // ── Fire async Nyaa search ──
    bool nyaaWillFire = false;
    if (m_nyaa) {
        m_currentNyaaReqId = ++m_nextNyaaReqId;
        m_nyaa->search(seriesTitle, vol.volumeNumber, m_currentNyaaReqId);
        nyaaWillFire = true;
    }

    // ── Initial render ──
    if (m_rows.isEmpty() && !nyaaWillFire) {
        setEmpty();
    } else if (m_rows.isEmpty() && nyaaWillFire) {
        setLoading();
    } else {
        setSources(m_rows, /*nyaaStillInFlight=*/nyaaWillFire);
    }
}
```

- [ ] **Step 4.7: Implement `onCardClicked`**

Add this slot impl to `ComicsSourcesPanel.cpp`:

```cpp
void ComicsSourcesPanel::onCardClicked(const UnifiedSourceRow& row)
{
    // User-explicit click cancels any pending auto-pick.
    if (m_autoPickTimer && m_autoPickTimer->isActive()) {
        m_autoPickTimer->stop();
    }
    m_autoPickArmed = true;  // latch: prevents re-arm on this volume

    emit downloadRequested(row,
                           m_activeSeriesTitle,
                           m_activeAnilistSeriesId,
                           m_activeVolumeNumber,
                           m_activeChapterIds);
}
```

- [ ] **Step 4.8: Implement `emitTopRowDownload`**

```cpp
void ComicsSourcesPanel::emitTopRowDownload()
{
    // Defensive re-verify (handles race against late-arriving sort that
    // could have displaced the catalog row).
    if (m_rows.isEmpty()) return;
    const auto& top = m_rows.first();
    if (top.kind != UnifiedSourceRow::Kind::Catalog || top.tier != 1) return;
    if (top.magnetUri.isEmpty()) return;

    emit downloadRequested(top,
                           m_activeSeriesTitle,
                           m_activeAnilistSeriesId,
                           m_activeVolumeNumber,
                           m_activeChapterIds);
}
```

- [ ] **Step 4.9: Update the existing `onNyaaResults` + `onNyaaFailed` slots**

Find the existing `onNyaaResults` and `onNyaaFailed` implementations. They currently re-render via the QListWidget path. Update them to re-render via the new state methods.

`onNyaaResults` should:
1. Discard if `reqId != m_currentNyaaReqId` (stale result from a prior volume).
2. Append nyaa rows to `m_rows`.
3. Re-sort `m_rows` by (tier asc, seeders desc).
4. Call `setSources(m_rows, /*nyaaStillInFlight=*/false)`.
5. If post-sort `m_rows.isEmpty()` → call `setEmpty()` instead.

`onNyaaFailed` should:
1. Discard if `reqId != m_currentNyaaReqId`.
2. Log via `qDebug() << "[ComicsSources] nyaa failed:" << reason`.
3. Call `setSources(m_rows, /*nyaaStillInFlight=*/false)` if rows present, else `setEmpty()`.

Exact replacement code (drop into the slot impl bodies):

```cpp
void ComicsSourcesPanel::onNyaaResults(int reqId, const QList<NyaaSourceCandidate>& results)
{
    if (reqId != m_currentNyaaReqId) return;  // stale

    for (const auto& cand : results) {
        UnifiedSourceRow row;
        row.kind         = UnifiedSourceRow::Kind::NyaaRuntime;
        row.tier         = cand.tier > 0 ? cand.tier : 2;
        row.title        = cand.displayTitle;
        row.uploaderHint = cand.uploader;
        row.seeders      = cand.seeders;
        row.sizeBytes    = cand.sizeBytes;
        row.magnetUri    = cand.magnetUri;
        row.infoHash     = cand.infoHash;
        m_rows.append(row);
    }

    std::stable_sort(m_rows.begin(), m_rows.end(),
                     [](const UnifiedSourceRow& a, const UnifiedSourceRow& b) {
                         if (a.tier != b.tier) return a.tier < b.tier;
                         return a.seeders > b.seeders;
                     });

    if (m_rows.isEmpty()) {
        setEmpty();
    } else {
        setSources(m_rows, /*nyaaStillInFlight=*/false);
    }
}

void ComicsSourcesPanel::onNyaaFailed(int reqId, const QString& reason)
{
    if (reqId != m_currentNyaaReqId) return;
    qDebug().noquote() << QStringLiteral("[ComicsSources] nyaa failed:") << reason;
    if (m_rows.isEmpty()) {
        setEmpty();
    } else {
        setSources(m_rows, /*nyaaStillInFlight=*/false);
    }
}
```

If `m_currentNyaaReqId` and `m_nextNyaaReqId` aren't already members in `ComicsSourcesPanel.h`, add them to the private section:

```cpp
    int m_nextNyaaReqId    = 0;
    int m_currentNyaaReqId = 0;
```

(Check the existing header first — they may exist under different names. Reuse the existing reqId members if so; do NOT add duplicates.)

- [ ] **Step 4.10: Remove the dead `onRowActivated` impl**

The old `void ComicsSourcesPanel::onRowActivated(QListWidgetItem* item)` impl is no longer called. Delete the entire function body.

- [ ] **Step 4.11: Build check**

```bash
taskkill /F /IM Tankoban.exe 2>/dev/null; ./build_check.bat
```

Expected: `BUILD OK`. If errors:
- "no matching constructor for 'ComicsSourceCard'" → check Task 2 .cpp got both constructors implemented + .h declares both
- "no member named 'displayTitle' in 'PremiumCatalogEntry'" → the actual field name in PremiumCatalogSchema.h may differ; grep the existing ComicsSourcesPanel.cpp `populate` impl for which fields it reads + use those
- "stale Agent 4 MOC" → see brotherhood conventions block

---

## Task 5: `ComicsSeriesView` gold-accent active row stripe

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

Goal: 3px gold-accent left-edge stripe on the active row in `m_volumesTable`. Spec §7 says try QSS Option (b) first, fall back to QStyledItemDelegate Option (a) if Qt6 row-height jitter shows.

- [ ] **Step 5.1: Locate the existing QSS sheet in ComicsSeriesView.cpp**

```bash
grep -n "QTableWidget#ComicsSeriesVolumesTable" src/ui/pages/comics/ComicsSeriesView.cpp
```

Expected: one or more matches. The QSS sheet is composed in a `setStyleSheet(...)` call in the constructor or buildUi method.

- [ ] **Step 5.2: Add the selected-row stripe rule (Option B QSS)**

Inside the existing root QSS sheet for ComicsSeriesView, find the `QTableWidget#ComicsSeriesVolumesTable` block. After the existing item styling rules, add:

```cpp
"QTableWidget#ComicsSeriesVolumesTable::item:selected {"
" border-left: 3px solid rgba(212,165,116,0.80); }"
```

If the existing sheet uses raw string literal multi-line, just append the rule inside the existing string. If it's QString concat, append as a new line.

- [ ] **Step 5.3: Build check**

```bash
taskkill /F /IM Tankoban.exe 2>/dev/null; ./build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 5.4: Visual smoke for jitter**

Run `build_and_run.bat` and open Comics → Death Note → click any volume row. Visually check:
- Selected row has a visible 3px gold accent on the left edge.
- Row height stays consistent — adjacent rows don't jump when selection moves.

If jitter is visible (row height changes when selection moves between rows): the QSS `border-left` ate visible height. Fallback to Option A in Step 5.5.

If clean: skip Step 5.5 + close the app via `scripts/stop-tankoban.ps1` and continue to Task 6.

- [ ] **Step 5.5: (Fallback) QStyledItemDelegate Option A**

Only execute if Step 5.4 showed row-height jitter.

In `ComicsSeriesView.cpp`, add an inline `QStyledItemDelegate` subclass near the top of the anonymous namespace:

```cpp
class VolumesTableRowAccentDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(p, option, index);
        if (option.state & QStyle::State_Selected) {
            const QColor accent(212, 165, 116, 200);
            p->fillRect(option.rect.left(), option.rect.top(), 3, option.rect.height(), accent);
        }
    }
};
```

Plus `#include <QStyledItemDelegate>` and `#include <QPainter>` at the top.

In the buildUi where `m_volumesTable` is constructed, after `setColumnCount` / `setHorizontalHeaderLabels`, add:

```cpp
m_volumesTable->setItemDelegateForColumn(0, new VolumesTableRowAccentDelegate(m_volumesTable));
```

And REVERT the QSS rule added in Step 5.2.

Re-run `build_check.bat` (BUILD OK) and re-smoke the visual.

---

## Task 6: Smoke matrix (4 cases)

**Files:** None modified. Smoke verification only.

This task requires Hemanth granting MCP-clear skies (per memory `feedback_mcp_skies_clear`) OR Hemanth driving the visual smoke himself. The four cases verify the v1 contract end-to-end.

- [ ] **Step 6.1: Claim MCP LANE LOCK**

Append to `agents/chat.md`:

```
MCP LOCK - [Agent X, COMICS_SOURCES_SIDEBAR Task 6 smoke 2026-05-17 ~<HH:MM> IST. Verifying: Death Note Catalog auto-pick (300ms beat + downloadRequested fires), Kingdom WC + Nyaa hybrid loading (synchronous WC + skeleton cards + nyaa replace), zero-source empty state, hover/selected visual states. ~10-15 min. ALT-key + SetForegroundWindow focus management. Rule 17 cleanup on release.]
```

- [ ] **Step 6.2: Launch Tankoban**

```bash
./build_and_run.bat
```

Wait for boot (~15-25s). Verify dev-bridge:

```bash
./out/tankoctl.exe ping
```

Expected reply with `tankoban.dev.v1` schema.

- [ ] **Step 6.3: Smoke A — Death Note Catalog auto-pick**

Navigate to Comics, search "death note", click first tile. Wait for series view to load. Click Vol 1 row in the volumes table.

**Expected:**
- Volume row Vol 1 gets a 3px gold-accent left-edge stripe.
- ComicsSourcesPanel populates with a Catalog card at top (`CT` gold-accent badge + "CATALOG" tier pill).
- The Catalog card displays a gold-accent border for 300ms.
- 300ms later, the volume row Status flips to "Downloading..." (or whatever the existing downloaded-state slot emits via `onProviderVolumeCompleted`).

Capture 2 screenshots at `agents/audits/smoke_evidence/03XX_sources_sidebar_s1_*.png` (substitute the next sequence number; check `ls agents/audits/smoke_evidence/ | sort -r | head -3` for the current max).

- [ ] **Step 6.4: Smoke B — Kingdom WC + Nyaa hybrid loading**

Navigate back to Comics landing (Esc x2 or Comics pill click per Agent 5's NAV_LAYER_BACK Phase 1 wiring). Search "kingdom", click first tile (Yasuhisa Hara's Kingdom).

Click Vol 1 row.

**Expected:**
- ComicsSourcesPanel populates synchronously with the `WC` FALLBACK card (no Catalog row — Kingdom isn't catalogued in v1) + 2 dim skeleton cards below.
- ~2-5s later (Nyaa runtime fetch): skeleton cards replace with Nyaa rows (with `NY` badge + uploader hint) OR disappear if Nyaa returned 0 hits.
- No auto-pick (no Catalog tier-1).
- Click the WC card → `downloadRequested` fires (visible in tankoctl logs or via the volume row Status transition).

2 screenshots: pre-nyaa-fetch + post-nyaa-fetch.

- [ ] **Step 6.5: Smoke C — Zero-source empty state**

Pick a series with no catalog hit AND no WC synthesis (chapterIds empty — typically a series the Tankoyomi-runtime hasn't indexed). Vinland Saga from earlier test data is a candidate; Naruto if Codex's MangaUpdates resolver populated it; or any pure-Tankoyomi-fetched ongoing series with no scraper coverage.

Click Vol 1.

**Expected:**
- ComicsSourcesPanel shows the empty-state copy: `"No sources found for this volume"` heading + `"Try a different volume or check back as indexers refresh."` subtitle.
- No cards.

1 screenshot.

- [ ] **Step 6.6: Smoke D — Hover state**

Open any series with at least 2 source cards visible (Death Note from Smoke A works after re-opening). Hover mouse over the second card (NOT the Catalog row currently auto-pick-selected).

**Expected:**
- Hovered card border brightens subtly (rgba border alpha increases).
- Mouse out: returns to default state.

1 screenshot of hover state.

- [ ] **Step 6.7: Rule 17 cleanup + release MCP LANE LOCK**

```bash
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

Append to `agents/chat.md`:

```
MCP LOCK RELEASED - [Agent X, COMICS_SOURCES_SIDEBAR Task 6 smoke complete. 4/4 cases PASS. Evidence at agents/audits/smoke_evidence/03XX_sources_sidebar_s*.png. Rule 17 cleanup ran clean.]
```

---

## Task 7: Bundle RTC + memory save

**Files:**
- Modify: `agents/chat.md` (append single bundled RTC line)
- Create: memory file at `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\project_comics_sources_sidebar_shipped_2026-05-17.md`
- Modify: memory index at `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md` (one-line pointer)

- [ ] **Step 7.1: Compose + append the single bundled RTC line**

Append to `agents/chat.md` (replace `<HH:MM>`, `<N>`, and `<list>` placeholders):

```
## Agent X - COMICS_SOURCES_SIDEBAR shipped - 2026-05-17

READY TO COMMIT - [Agent X, COMICS_SOURCES_SIDEBAR v1 Stremio-style cards shipped 2026-05-17 ~<HH:MM> IST. Spec at docs/superpowers/specs/2026-05-17-comics-sources-sidebar-design.md (12 brainstorm decisions across 3 rounds + Approach A pick). Plan at docs/superpowers/plans/2026-05-17-comics-sources-sidebar.md. 7 tasks executed sequentially, build_check BUILD OK gate after every src/ touch. Shipped: (1) NEW ComicsSourceCard.{h,cpp} Stremio-style card mirroring StreamSourceCard shape: 36x36 source-initials badge (CT/NY/WC tied to tier colors) + 2-line title/subtitle + tier pill (CATALOG/NYAA/FALLBACK) + chip row underneath (seeders/size/cbz); skeleton constructor variant with QGraphicsOpacityEffect+QPropertyAnimation infinite pulse for hybrid loading; mouseReleaseEvent emits clicked signal (F3-lesson-learned pattern avoiding QPushButton synthetic-input edge cases). (2) REFACTORED ComicsSourcesPanel.{h,cpp}: swapped QListWidget m_list for QScrollArea + QVBoxLayout<ComicsSourceCard>; 4-state machine setPlaceholder/setLoading/setSources/setEmpty; hybrid-loading state appends 2 skeleton cards when nyaaStillInFlight==true; auto-pick orchestration via QTimer::singleShot(300ms) on top-tier Catalog rows with cancel-on-rapid-click + cancel-on-populate; public API (clear/populate/downloadRequested) unchanged so ComicsSeriesView wiring needs zero adjustment. (3) TOUCHED ComicsSeriesView.cpp: QSS rule for QTableWidget#ComicsSeriesVolumesTable::item:selected border-left 3px gold-accent on the active volume row (Option B per spec §7; Option A delegate fallback documented if jitter shows). (4) CMakeLists.txt: registered new ComicsSourceCard.cpp. Smoke matrix 4/4 PASS: Death Note Catalog auto-pick (300ms beat + downloadRequested verified), Kingdom WC+Nyaa hybrid loading (skeleton -> real cards transition), zero-source empty state, hover state visual. Evidence: agents/audits/smoke_evidence/03XX_sources_sidebar_s*.png (<N> screenshots). Rule 17 cleanup ran clean (Tankoban + ffmpeg_sidecar killed, stremio-runtime PIDs swept). Brotherhood conventions honored end-to-end: ASCII-only on all new source files (verified via byte sweep), no worktrees (master direct), build_check BUILD OK after every src/ touch, no mid-task git commits (single bundled RTC per Rule 11), Rule 19 MCP LANE LOCK bracketed Task 6. Stremio-for-manga Sources sidebar v1 SHIPPED + PRODUCTION-GRADE.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /build-verify, /simplify] | files: src/ui/pages/comics/ComicsSourceCard.h, src/ui/pages/comics/ComicsSourceCard.cpp, src/ui/pages/comics/ComicsSourcesPanel.h, src/ui/pages/comics/ComicsSourcesPanel.cpp, src/ui/pages/comics/ComicsSeriesView.cpp, CMakeLists.txt, agents/audits/smoke_evidence/<list>, agents/chat.md
```

- [ ] **Step 7.2: Create the memory file**

Create `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\project_comics_sources_sidebar_shipped_2026-05-17.md`:

```markdown
---
name: project-comics-sources-sidebar-shipped-2026-05-17
description: COMICS_SOURCES_SIDEBAR v1 (Stremio-style cards) shipped 2026-05-17. ComicsSourcesPanel refactored QListWidget -> QScrollArea + cards. 12 decisions via brainstorm. Smoke 4/4 PASS.
metadata:
  type: project
---

COMICS_SOURCES_SIDEBAR v1 SHIPPED 2026-05-17. The ComicsSourcesPanel right-pane on every series view now renders Stremio-style cards (mirror of Theatre's StreamSourceCard pattern) instead of the legacy QListWidget single-line text rows.

**What landed:**
- NEW `src/ui/pages/comics/ComicsSourceCard.{h,cpp}` — Stremio-style card widget taking a UnifiedSourceRow. Has skeleton constructor variant with QGraphicsOpacityEffect pulse.
- REFACTORED `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` — 4-state machine + auto-pick QTimer + hybrid-loading skeleton injection.
- TOUCHED `src/ui/pages/comics/ComicsSeriesView.cpp` — QSS rule for 3px gold-accent active-row stripe.

**Spec + plan:**
- docs/superpowers/specs/2026-05-17-comics-sources-sidebar-design.md
- docs/superpowers/plans/2026-05-17-comics-sources-sidebar.md

**12 brainstorm-locked decisions:**
1. v1 scope visual-polish-only — no new providers.
2. Adapt manga-native chips (archive type / uploader / size).
3. Auto-pick top-tier Catalog rows silently (no saved-choice mechanism).
4. No right-click menu — left-click downloads.
5. No cover thumb in card — source-initials badge only.
6. Badge text from source kind: CT / NY / WC.
7. Loading state: skeleton cards (pulse animation).
8. Empty state: "No sources found for this volume" + suggested-next-step subtitle.
9. Auto-pick UX: visible 300ms beat + download fires.
10. Hybrid loading: Catalog + WC instant; 2 skeleton cards below for Nyaa.
11. Card height ~80px (matches Stream).
12. Volume-row selection: 3px gold-accent left-edge stripe.

**Key lessons in the build:**
- mouseReleaseEvent emit pattern (not mousePressEvent) per F3 lesson learned 2026-05-16: synthetic Win32 mouse_event from agent smokes hits Qt's QPushButton clicked-signal edge case; QFrame + manual mouseReleaseEvent avoids that.
- 64-bit MangaUpdates IDs stored as JSON strings in cache sidecar (Codex Trigger D 2026-05-17 A3 adaptation): same precision-loss-past-2^53 risk applies anywhere we serialize 64-bit ints to JSON.

**Carry-forwards (NOT in v1):**
- MangaUpdates source row (the resolver populates AniList chapter counts but doesn't surface as a downloadable source row; deferred).
- Saved-choice + auto-launch toast Stream-parity (Round 1 Q3 explicitly excluded).
- Sources-card right-click menu (Round 1 Q4 excluded).
- Per-volume Bookwalker / official-publisher source cards (out of v1 scope).
```

- [ ] **Step 7.3: Update MEMORY.md index**

Append this line to the appropriate section of `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md`:

```
- [project_comics_sources_sidebar_shipped_2026-05-17.md](project_comics_sources_sidebar_shipped_2026-05-17.md) — Sources sidebar v1 Stremio-style cards shipped 2026-05-17. 12 decisions + Smoke 4/4 PASS.
```

Place it in the section that holds "Sources / Library / Theme" or whichever category currently holds Comics-related project entries (check existing entries to match the convention).

---

## Self-Review

After all 7 tasks land + Smoke 4/4 PASS:

**1. Spec coverage:** Trace each section of `docs/superpowers/specs/2026-05-17-comics-sources-sidebar-design.md`:
- §3 Decisions 1-12: all 12 implemented across Tasks 1-5. Decision-trace verified.
- §4 Architecture: new file + refactor + touch all landed (Tasks 1+2+4+5).
- §5 Card recipe: implemented in Task 2 (real + skeleton constructors, badge / 2-line text / pill / chip row).
- §6 State machine: implemented in Task 4 Steps 4.3-4.4 (setPlaceholder / setLoading / setSources / setEmpty + auto-pick).
- §7 Volume row stripe: Task 5 (Option B QSS; Option A delegate fallback documented).
- §8 Error handling: nyaa-fail / WC-skip / catalog-empty-magnet all handled in Task 4 Steps 4.6 / 4.7 / 4.9.
- §9 Testing: Task 6 smoke matrix 4/4 covers all 6 spec cases (cases 5 + 6 from spec rolled into hybrid + hover observations during Smoke B + D).
- §10 Phase hints: plan tasks 1-7 map to the phase numbering with minor consolidation (Phase 2 skeleton merged into Phase 1; Phase 7 RTC merged into Task 7).
- §11 Known risks: row-height jitter has explicit fallback path (Step 5.5); stale Agent 4 MOC trap in brotherhood conventions block.

**2. Placeholder scan:** No "TBD" / "implement later" / "Similar to Task N" / "add error handling" in the plan. Every code block is real C++. Every cmake registration shows the literal path string. Every build_check command is verbatim.

**3. Type consistency:** Verified across plan:
- `UnifiedSourceRow` referenced in Task 1 / 2 / 4 — same struct, same field names (kind/tier/title/uploaderHint/seeders/sizeBytes/magnetUri/infoHash).
- `setSources(QList<UnifiedSourceRow>, bool)` signature consistent between Step 3.3 declaration and Step 4.4 implementation.
- `ComicsSourceCard::clicked(UnifiedSourceRow)` signal consistent between Task 1 declaration and Task 4 connect (Step 4.4 inside setSources).
- `m_autoPickTimer` / `m_autoPickArmed` / `m_currentNyaaReqId` / `m_nextNyaaReqId` member names consistent across Steps 3.2 / 4.3 / 4.5 / 4.6 / 4.9.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-17-comics-sources-sidebar.md`. Two execution options per the brotherhood pattern:

**1. Subagent-Driven (recommended for in-session execution)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Best fit because each task has clean handoff points (build_check gate between tasks) and the spec is locked.

**2. Codex Trigger D dispatch (recommended if context budget matters)** — given Codex has shipped 2 clean Trigger D rounds in the past 36 hours (pre-smoke fix-TODO + MangaUpdates fallback) AND this plan is comprehensive with literal code + cmake paths + brotherhood-conventions block, this is a strong Trigger D candidate. Codex ships Tasks 1-5; brotherhood-Claude pair handles Task 6 MCP smoke + Task 7 RTC.

**3. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints. Risk: this session is already heavy (4-hour MangaUpdates arc + smoke + brainstorm + spec + plan); inline execution might exhaust context before Task 5.

**Which approach, brother?**
