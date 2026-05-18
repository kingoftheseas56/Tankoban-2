# ComicsSeriesView Stream-blueprint port -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port `src/ui/pages/comics/ComicsSeriesView.{cpp,h}` to mirror `StreamDetailView`'s layout (140px hero banner + two-column content + 6-col volume table + 8%-white selection + library action button + 3-line clamped description + multi-select bulk-download + next-unread highlight + full-vertical Sources panel). Preserve F1 keyboard-nav (2026-05-18) and COMICS_SOURCES_SIDEBAR v1 cards (2026-05-17). The gold-bar selection glitch dies as a side-effect of the selection-style change.

**Architecture:** Layout-shell refactor + table-column refactor + 4 polish features, all confined to `ComicsSeriesView.{cpp,h}` (~400-550 LOC across 6 commits). `StreamDetailView.{cpp,h}` is READ-ONLY blueprint -- cite lines, do not edit. `ComicsSourcesPanel` / `ComicsSourceCard` internal logic unchanged; only outer parent layout slot moves. `QTableWidget` preserved (Stream also uses it).

**Tech Stack:** Qt6 (Widgets / Network / QSS), C++20, MSVC2022, CMake/Ninja. No new deps. No new files in v1.

**Spec:** `docs/superpowers/specs/2026-05-18-comics-series-view-stream-port-design.md`
**Mockup (ground truth):** `.superpowers/brainstorm/1608-1779095122/content/proposed-layout.html`
**Umbrella arc:** `COMICS_TANKOYOMI_STREAM_MERGER` (this plan is one piece)

---

## File Structure

### Modified

- `src/ui/pages/comics/ComicsSeriesView.h` -- add new widget members + new slots + new helpers; remove `paintEvent` declaration.
- `src/ui/pages/comics/ComicsSeriesView.cpp` -- buildUi() rebuilt; paintEvent removed; loadBannerUrl repurposed; populateVolumeRows column-shape refactored; refreshLibraryButton label binding; new slots for description/multi-select/next-unread.

### Read-only references (cite, never edit)

- `src/ui/pages/stream/StreamDetailView.cpp:395-487` -- hero/title/meta/description layout reference.
- `src/ui/pages/stream/StreamDetailView.cpp:454-472` + `onDescShowMoreClicked` -- 3-line clamp + Show more pattern.
- `src/ui/pages/stream/StreamDetailView.cpp:599-602` -- m_downloadSelectedBtn pattern.
- `src/ui/pages/stream/StreamDetailView.cpp:661-714` -- m_episodeTable column setup + selection QSS.
- `src/ui/pages/stream/StreamDetailView.cpp:1024-1112` -- per-row item construction reference.

### Not touched

- `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` -- internal logic unchanged. The widget is reparented in Task 1 (parent layout slot changes); no edits inside the file.
- `src/ui/pages/comics/ComicsSourceCard.{h,cpp}` -- shipped 2026-05-17, stays.
- `src/ui/pages/ComicsPage.{h,cpp}` -- host. Existing signal wires `downloadDispatchRequested` / `openVolume` / `navigationRequested` remain valid.
- `src/core/manga/anilist/AniListCache.{h,cpp}` -- API surface (`isBookmarked` / `bookmarksChanged`) used unchanged.
- `CMakeLists.txt` -- already lists `ComicsSeriesView.{h,cpp}` + `ComicsSourcesPanel.{h,cpp}` + `ComicsSourceCard.{h,cpp}`.

---

## Per-task Tankoban verification pattern

Tankoban is **smoke-first** for UI work per `feedback_one_fix_per_rebuild.md` + CLAUDE.md. Each task ends with:

1. **Build:** `build_check.bat` -- expected last line `BUILD OK`.
2. **ASCII sweep:** PowerShell:
   ```powershell
   $bytes = [System.IO.File]::ReadAllBytes('src/ui/pages/comics/ComicsSeriesView.cpp'); ($bytes | Where-Object { $_ -gt 127 }).Count
   ```
   Expected: `0`. Repeat for `.h`.
3. **Hemanth visual smoke:** specific scenarios per task (described in each task's "Smoke" step).
4. **RTC post:** append a `READY TO COMMIT` line to `agents/chat.md`. Template per task.

`/superpowers:test-driven-development` is OPT-IN ONLY for `tankoban_tests` pure-logic primitives per CLAUDE.md Tier 2. UI work does NOT add new tests in this plan.

---

### Task 1: Layout shell skeleton

**Goal:** Strip the full-bleed wallpaper-paintEvent layout. Add an action row (Back link + library button) at top, a 140px hero banner block, and a two-column content row (leftCol 3 : rightCol 1) with `m_sourcesPanel` repositioned into rightCol full-height. Volume table stays in its current internal shape (Task 2 reshapes it). Description stays full-text (Task 3 clamps it). Library button stays "In library" / "Add to library" passive (Task 4 changes the label).

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` (add `m_heroBannerLabel` + `m_backButton`; remove `paintEvent` decl + `m_bannerPixmap` member; keep `loadBannerUrl` decl).
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` (rewrite `buildUi`; rewrite `loadBannerUrl` body; delete `paintEvent` body; delete `m_bannerPixmap = QPixmap();` resets at lines 405 + 445).

**Reference:** `src/ui/pages/stream/StreamDetailView.cpp:395-407` for hero label pattern; `:408-484` for the two-column contentRow + leftCol structure.

- [ ] **Step 1: Edit ComicsSeriesView.h -- header members**

In `src/ui/pages/comics/ComicsSeriesView.h`, locate the `private:` section with widget members (around line 155+, where `m_title` / `m_metaLine` / `m_synopsis` / `m_libraryButton` / `m_volumesTable` / `m_sourcesPanel` are declared). Apply these two edits:

**Edit 1a -- remove `paintEvent` declaration.** Around line 115 you will find:
```cpp
    void paintEvent(QPaintEvent* event) override;
```
Delete this line entirely. The paintEvent override is no longer needed.

**Edit 1b -- replace `m_bannerPixmap` with `m_heroBannerLabel` + add `m_backButton`.** Around line 158-160 you will find:
```cpp
    // the layout -- it's a QPixmap painted in paintEvent across the full
    // viewport beneath the title/meta/table widgets.
    QPixmap               m_bannerPixmap;
```
Replace those three lines with:
```cpp
    // STREAM_PORT 2026-05-18 Task 1: hero banner is now a docked QLabel at
    // 140px height instead of a full-viewport paintEvent wallpaper. Banner
    // image is loaded asynchronously via loadBannerUrl() and rendered as a
    // scaled pixmap on the label. Matches StreamDetailView::m_heroLabel
    // (StreamDetailView.cpp:397-406) pattern.
    QLabel*               m_heroBannerLabel = nullptr;
    QPushButton*          m_backButton      = nullptr;
```

- [ ] **Step 2: Edit ComicsSeriesView.cpp -- rewrite buildUi()**

In `src/ui/pages/comics/ComicsSeriesView.cpp`, locate `void ComicsSeriesView::buildUi()` at line 201 and replace the ENTIRE function body (from the opening `{` on line 202 through the closing `}`, currently spanning lines 201 through approximately 395) with the following:

```cpp
void ComicsSeriesView::buildUi()
{
    // STREAM_PORT 2026-05-18 Task 1: layout shell mirrors StreamDetailView
    // (src/ui/pages/stream/StreamDetailView.cpp:395-487). Shape:
    //   actionRow  -- back link (left) + library button (right)
    //   heroBanner -- 140px solid block holding the series banner image
    //   contentRow -- two columns (leftCol stretch=3, rightCol stretch=1)
    //                 leftCol holds title + meta + description + volume table
    //                 rightCol holds m_sourcesPanel full-vertical
    // The prior full-bleed paintEvent wallpaper is GONE -- title text now
    // sits below the banner on a solid dark background. Mockup at
    // .superpowers/brainstorm/1608-1779095122/content/proposed-layout.html.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 14, 24, 24);
    outer->setSpacing(12);

    // Root QSS -- no wallpaper, solid dark background, label foreground colors,
    // volume table + sources panel card styling. Selection style is the new
    // 8% white tint (Stream parity at StreamDetailView.cpp:700); the prior
    // 3px gold left-stripe from the 2026-05-17 Sources Sidebar Decision 12
    // is REMOVED per brainstorm 2026-05-18.
    setStyleSheet(QStringLiteral(
        "ComicsSeriesView { background: #0d0d10; }"
        "QLabel#ComicsSeriesHeroBanner {"
        "  background: #101010;"
        "  border-radius: 8px;"
        "}"
        "QLabel#ComicsSeriesTitle {"
        "  color: #ffffff;"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesMetaLine {"
        "  color: rgba(255, 255, 255, 0.62);"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesSynopsis {"
        "  color: rgba(255, 255, 255, 0.55);"
        "  background: transparent;"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable {"
        "  background-color: rgba(15, 15, 18, 0.88);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "  gridline-color: rgba(255, 255, 255, 0.05);"
        "  color: #e5e7eb;"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable::item {"
        "  background: transparent;"
        "  color: #e5e7eb;"
        "  padding: 4px 8px;"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable::item:alternate {"
        "  background-color: rgba(255, 255, 255, 0.03);"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable::item:selected {"
        "  background: rgba(255, 255, 255, 0.08);"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable QHeaderView::section {"
        "  background-color: rgba(20, 20, 24, 0.95);"
        "  color: rgba(255, 255, 255, 0.65);"
        "  border: none;"
        "  padding: 8px 10px;"
        "  font-weight: 600;"
        "}"
        "ComicsSourcesPanel {"
        "  background-color: rgba(15, 15, 18, 0.88);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "}"
    ));

    // --- Action row: Back link (left) + Library button (right) ---------
    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);

    m_backButton = new QPushButton(tr("\xe2\x86\x90 Back"), this);
    m_backButton->setObjectName(QStringLiteral("ComicsSeriesBackButton"));
    m_backButton->setFlat(true);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesBackButton {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.7);"
        "  font-size: 13px;"
        "  padding: 4px 8px;"
        "}"
        "QPushButton#ComicsSeriesBackButton:hover {"
        "  color: #fff;"
        "}"));
    connect(m_backButton, &QPushButton::clicked,
            this, &ComicsSeriesView::navigationRequested);
    actionRow->addWidget(m_backButton, /*stretch*/ 0, Qt::AlignLeft);

    actionRow->addStretch(1);

    m_libraryButton = new QPushButton(this);
    m_libraryButton->setObjectName(QStringLiteral("ComicsSeriesLibraryButton"));
    m_libraryButton->setAccessibleName(QStringLiteral("ComicsSeriesLibraryButton"));
    m_libraryButton->setAccessibleDescription(QStringLiteral("Add or remove this series from the Comics library."));
    m_libraryButton->setFixedHeight(32);
    m_libraryButton->setCursor(Qt::PointingHandCursor);
    m_libraryButton->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesLibraryButton {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px;"
        "  color: #ddd;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "}"
        "QPushButton#ComicsSeriesLibraryButton:hover {"
        "  background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.28);"
        "}"
        "QPushButton#ComicsSeriesLibraryButton:disabled {"
        "  color: #777;"
        "  border-color: rgba(255,255,255,0.10);"
        "}"));
    actionRow->addWidget(m_libraryButton, /*stretch*/ 0, Qt::AlignRight);

    outer->addLayout(actionRow);

    // --- Hero banner: 140px solid block holding the series art ----------
    m_heroBannerLabel = new QLabel(this);
    m_heroBannerLabel->setObjectName(QStringLiteral("ComicsSeriesHeroBanner"));
    m_heroBannerLabel->setFixedHeight(140);
    m_heroBannerLabel->setAlignment(Qt::AlignCenter);
    m_heroBannerLabel->setScaledContents(false);
    outer->addWidget(m_heroBannerLabel);

    // --- Two-column content row -----------------------------------------
    auto* contentRow = new QHBoxLayout();
    contentRow->setSpacing(16);

    // Left column: title + meta + synopsis + volume table
    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("ComicsSeriesTitle"));
    {
        QFont f = m_title->font();
        f.setPointSize(18);
        f.setBold(true);
        m_title->setFont(f);
    }
    m_title->setWordWrap(true);
    leftCol->addWidget(m_title);

    m_metaLine = new QLabel(this);
    m_metaLine->setObjectName(QStringLiteral("ComicsSeriesMetaLine"));
    {
        QFont f = m_metaLine->font();
        f.setPointSize(10);
        m_metaLine->setFont(f);
    }
    leftCol->addWidget(m_metaLine);

    m_synopsis = new QLabel(this);
    m_synopsis->setObjectName(QStringLiteral("ComicsSeriesSynopsis"));
    {
        QFont f = m_synopsis->font();
        f.setPointSize(10);
        m_synopsis->setFont(f);
    }
    m_synopsis->setWordWrap(true);
    m_synopsis->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_synopsis->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    leftCol->addWidget(m_synopsis);

    // Future story-arcs slot reserved here per brainstorm Decision 7
    // (2026-05-18). v1 inserts nothing; future v1.x widget mounts before
    // the volume table.

    // --- Volume list table (column shape REUSED from current code; Task 2
    // reshapes into the Stream-blueprint 6-column layout). ---------------
    m_volumesTable = new QTableWidget(this);
    m_volumesTable->setObjectName(QStringLiteral("ComicsSeriesVolumesTable"));
    const QStringList headers = {
        tr("#"), tr("Cover"), tr("Volume"), tr("Chapters"),
        tr("Progress"), tr("Status"), tr("Open")
    };
    m_volumesTable->setColumnCount(headers.size());
    m_volumesTable->setHorizontalHeaderLabels(headers);
    m_volumesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_volumesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_volumesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_volumesTable->setShowGrid(false);
    m_volumesTable->setAlternatingRowColors(true);
    m_volumesTable->verticalHeader()->setVisible(false);
    m_volumesTable->horizontalHeader()->setStretchLastSection(false);

    auto* hdr = m_volumesTable->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Fixed);
    hdr->setSectionResizeMode(1, QHeaderView::Fixed);
    hdr->setSectionResizeMode(2, QHeaderView::Stretch);
    hdr->setSectionResizeMode(3, QHeaderView::Fixed);
    hdr->setSectionResizeMode(4, QHeaderView::Fixed);
    hdr->setSectionResizeMode(5, QHeaderView::Fixed);
    hdr->setSectionResizeMode(6, QHeaderView::Fixed);
    m_volumesTable->setColumnWidth(0, 36);
    m_volumesTable->setColumnWidth(1, 76);
    m_volumesTable->setColumnWidth(3, 140);
    m_volumesTable->setColumnWidth(4, 80);
    m_volumesTable->setColumnWidth(5, 120);
    m_volumesTable->setColumnWidth(6, 36);
    m_volumesTable->setIconSize(QSize(48, 64));
    m_volumesTable->verticalHeader()->setDefaultSectionSize(64);

    leftCol->addWidget(m_volumesTable, /*stretch*/ 1);

    auto* leftColWrap = new QWidget(this);
    leftColWrap->setLayout(leftCol);
    contentRow->addWidget(leftColWrap, /*stretch*/ 3);

    // Right column: Sources panel full-vertical. PHASE 8: replaces the
    // Phase 7 placeholder QLabel; STREAM_PORT 2026-05-18 Task 1 moves the
    // panel OUT of the prior hero-row top-right slot into the full-height
    // right column beside the volume list. Panel internal logic, cards,
    // skeleton-pulse, auto-pick 300ms beat -- all UNCHANGED.
    m_sourcesPanel = new ComicsSourcesPanel(m_catalog, m_nyaa, this);
    m_sourcesPanel->setMinimumWidth(240);
    m_sourcesPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    contentRow->addWidget(m_sourcesPanel, /*stretch*/ 1);

    outer->addLayout(contentRow, /*stretch*/ 1);
}
```

- [ ] **Step 3: Edit ComicsSeriesView.cpp -- rewrite loadBannerUrl()**

In `src/ui/pages/comics/ComicsSeriesView.cpp`, locate `void ComicsSeriesView::loadBannerUrl(const QString& url)` at line 739 and replace the function body (lines 739-769) with:

```cpp
void ComicsSeriesView::loadBannerUrl(const QString& url)
{
    // STREAM_PORT 2026-05-18 Task 1: was full-viewport paintEvent wallpaper;
    // now paints onto m_heroBannerLabel at 140px height. Uses
    // KeepAspectRatioByExpanding so the banner image fills the 140px band
    // (horizontal slice, centered). Mirrors StreamDetailView's hero label
    // approach.
    if (url.isEmpty() || !m_heroBannerLabel) return;

    QPixmap cached;
    if (QPixmapCache::find(url, &cached)) {
        applyBannerPixmap(cached);
        return;
    }

    QNetworkAccessManager* nam = m_client ? m_client->networkManager() : nullptr;
    if (!nam) return;

    QPointer<ComicsSeriesView> self(this);
    const int snapshotAnilistId = m_currentAnilistId;
    QNetworkReply* reply = nam->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, snapshotAnilistId]() {
        reply->deleteLater();
        if (!self) return;
        if (self->m_currentAnilistId != snapshotAnilistId) return;
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) return;
        QPixmapCache::insert(url, pm);
        self->applyBannerPixmap(pm);
    });
}

void ComicsSeriesView::applyBannerPixmap(const QPixmap& pm)
{
    if (!m_heroBannerLabel || pm.isNull()) return;
    const QSize target = m_heroBannerLabel->size();
    if (target.width() <= 0 || target.height() <= 0) {
        // Label hasn't been sized yet (first paint); store and re-apply on
        // resize via setPixmap with the raw pixmap -- Qt will scale on paint.
        m_heroBannerLabel->setPixmap(pm);
        return;
    }
    const QPixmap scaled = pm.scaled(target,
                                     Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
    m_heroBannerLabel->setPixmap(scaled);
}
```

- [ ] **Step 4: Edit ComicsSeriesView.cpp -- delete paintEvent body**

In `src/ui/pages/comics/ComicsSeriesView.cpp`, locate `void ComicsSeriesView::paintEvent(QPaintEvent* /*event*/)` (around line 777) and DELETE the entire function (from the comment block above the function on line 771 through the closing `}` of the function body, which sits around line 800-810). The paintEvent override is no longer needed -- the banner is a child QLabel painted by Qt's default mechanism.

- [ ] **Step 5: Edit ComicsSeriesView.cpp -- delete m_bannerPixmap resets**

In `src/ui/pages/comics/ComicsSeriesView.cpp`, locate the two `m_bannerPixmap = QPixmap();` lines (currently at lines 405 and 445 -- one in `renderEmpty()`, one in `renderDetail()`). DELETE both lines. The `m_bannerPixmap` member is gone (removed from .h in Step 1b); these reset lines reference it.

- [ ] **Step 6: Edit ComicsSeriesView.h -- add applyBannerPixmap helper decl**

In `src/ui/pages/comics/ComicsSeriesView.h`, locate the `private:` helpers block (the same place where `loadBannerUrl` decl lives, around line 147). Add a new helper declaration immediately AFTER `loadBannerUrl`:

```cpp
    // STREAM_PORT 2026-05-18 Task 1: paint a pixmap onto m_heroBannerLabel,
    // scaled to fit the 140px band via KeepAspectRatioByExpanding. Called
    // from loadBannerUrl on cache-hit OR async-fetch completion.
    void applyBannerPixmap(const QPixmap& pm);
```

- [ ] **Step 7: Build verify**

Run from project root:
```
build_check.bat
```
Expected: last line `BUILD OK`.

If `BUILD FAILED`, read the cl.exe error tail; common issues this task could surface:
- Missing `<QPushButton>` include (already in the file's includes, but re-check).
- `m_bannerPixmap` referenced somewhere besides the lines stripped in Step 5 (grep `m_bannerPixmap` in the file; any surviving reference must be removed).
- `paintEvent` still declared in the .h despite Step 1a (re-grep `paintEvent` in the .h).

- [ ] **Step 8: ASCII sweep**

Run from project root in PowerShell:
```powershell
$files = 'src/ui/pages/comics/ComicsSeriesView.cpp','src/ui/pages/comics/ComicsSeriesView.h'; foreach ($f in $files) { $bytes = [System.IO.File]::ReadAllBytes($f); $n = ($bytes | Where-Object { $_ -gt 127 }).Count; Write-Output ("{0}: {1} non-ascii bytes" -f $f, $n) }
```
Expected: both files report `0 non-ascii bytes`.

- [ ] **Step 9: Hemanth visual smoke**

Ask Hemanth to:
1. Close any running Tankoban (`taskkill /F /IM Tankoban.exe` first if needed, per Rule 1).
2. Double-click `build_and_run.bat`.
3. Open Comics tab -> click any series tile (Death Note preferred since it has a banner image cached).
4. Verify visually:
   - Banner image renders as a 140px solid block at the top (NOT a full-bleed backdrop).
   - Title + meta + synopsis text are BELOW the banner on solid dark background (NOT overlaid on artwork).
   - "<- Back" link sits top-left; library button ("In library" or "Add to library") sits top-right.
   - Volume table is in the left ~75% of the lower area.
   - Sources panel is the FULL HEIGHT of the right ~25% column (multiple cards visible at once, scrollable).

If banner does not appear, that is acceptable for v1 of Task 1 if the cached image is loading; come back in 5 seconds and the async fetch will paint.

- [ ] **Step 10: RTC post**

Append to `agents/chat.md`:

```
## Agent 1 - STREAM_PORT Task 1: layout shell skeleton - 2026-05-18

READY TO COMMIT - [Agent 1, Task 1 of docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md SHIPPED 2026-05-18. Layout shell refactored: paintEvent wallpaper removed, action row + 140px hero banner + two-column content row added. m_sourcesPanel moved from hero-right-corner to full-height right column. Volume table internal shape UNCHANGED (Task 2 reshapes it). Library button label UNCHANGED ("In library" / "Add to library" passive; Task 4 changes to "Remove from / Add to Library" action). Description UNCHANGED (full-text; Task 3 clamps). Gold-stripe selection style REMOVED (replaced with 8% white tint per brainstorm Decision P2). F1 keyboard-nav-populates-Sources behavior PRESERVED (cellClicked + currentCellChanged connects re-wired to the rebuilt QTableWidget). build_check.bat BUILD OK first try. ASCII sweep clean (0 non-ASCII bytes both files). Hemanth visual smoke: banner-block + below-banner text + full-height Sources panel all rendering. Net diff: ~150 LOC across 2 files.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsSeriesView.cpp, src/ui/pages/comics/ComicsSeriesView.h, agents/chat.md
```

---

### Task 2: Volume table column-shape refactor

**Goal:** Reshape the volume table from 7 columns to 6. Drop the col-6 "Open" chevron entirely. Merge "Volume" (col 2) + "Chapters" (col 3) into a single stacked title column (Volume label on top line + chapter range on second line, like Stream's title+overview pattern at StreamDetailView.cpp:1024-1090). Add a 32px col-0 checkbox column (placeholder -- wiring lands in Task 5). Cover stays at col 1 (renamed col 1 = Thumb; 48x64 portrait sizing kept since manga covers are portrait, NOT Stream's 64x36 landscape).

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` -- buildUi() table column setup (the block inside the new buildUi from Task 1); populateVolumeRows() (currently around line 510-595, sets the per-row QTableWidgetItems).
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` -- no changes; column count is hard-coded in the .cpp.

**Reference:** StreamDetailView.cpp:661-674 (column setup), :1024-1090 (per-row stacked title+overview construction via a wrapper QWidget set as cellWidget on the title col).

- [ ] **Step 1: Edit ComicsSeriesView.cpp -- new column setup in buildUi()**

In the new `buildUi()` written in Task 1, locate the volume-table column-setup block (the part starting with `const QStringList headers = {` and ending with `m_volumesTable->verticalHeader()->setDefaultSectionSize(64);`). Replace that entire block with:

```cpp
    // STREAM_PORT 2026-05-18 Task 2: 6-column layout (down from 7). Stream
    // parity at StreamDetailView.cpp:661-674. Columns:
    //   [0 chk]  -- 32px checkbox (Task 5 wires the toggle; Task 2 inserts
    //               the QCheckBox cellWidget but does not yet read state)
    //   [1 #]    -- 36px volume index
    //   [2 thmb] -- 76px (48x64 portrait + padding), MANGA aspect kept
    //   [3 ttl]  -- stretch, holds stacked "Vol N" + "Chs A-B" via cellWidget
    //   [4 prog] -- 80px progress text ("--" in v1; per-chapter read-state TBD)
    //   [5 stat] -- 60px status text ("Downloaded" / "Not downloaded" / ...)
    // Col-6 ("Open" chevron) is DROPPED entirely per brainstorm Decision 6.
    constexpr int kColCheckbox = 0;
    constexpr int kColIndex    = 1;
    constexpr int kColThumb    = 2;
    constexpr int kColTitle    = 3;
    constexpr int kColProgress = 4;
    constexpr int kColStatus   = 5;
    constexpr int kColCount    = 6;

    const QStringList headers = {
        QString(),         // checkbox -- no header text
        tr("#"),
        QString(),         // thumb -- no header text
        tr("Title"),
        tr("Progress"),
        tr("Status"),
    };
    m_volumesTable->setColumnCount(kColCount);
    m_volumesTable->setHorizontalHeaderLabels(headers);
    m_volumesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_volumesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_volumesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_volumesTable->setShowGrid(false);
    m_volumesTable->setAlternatingRowColors(true);
    m_volumesTable->verticalHeader()->setVisible(false);
    m_volumesTable->horizontalHeader()->setStretchLastSection(false);

    auto* hdr = m_volumesTable->horizontalHeader();
    hdr->setSectionResizeMode(kColCheckbox, QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColIndex,    QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColThumb,    QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColTitle,    QHeaderView::Stretch);
    hdr->setSectionResizeMode(kColProgress, QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColStatus,   QHeaderView::Fixed);
    m_volumesTable->setColumnWidth(kColCheckbox, 32);
    m_volumesTable->setColumnWidth(kColIndex,    36);
    m_volumesTable->setColumnWidth(kColThumb,    76);
    m_volumesTable->setColumnWidth(kColProgress, 80);
    m_volumesTable->setColumnWidth(kColStatus,   60);
    m_volumesTable->setIconSize(QSize(48, 64));
    m_volumesTable->verticalHeader()->setDefaultSectionSize(72);
```

- [ ] **Step 2: Edit ComicsSeriesView.cpp -- rewrite populateVolumeRows() per-row build**

In `src/ui/pages/comics/ComicsSeriesView.cpp`, locate `void ComicsSeriesView::populateVolumeRows(const QList<anilist::VolumeRow>& rows, const anilist::MediaDetail* detail)` (around line 510). The function currently sets seven per-row items (col 0 # / col 1 Cover / col 2 Volume / col 3 Chapters / col 4 Progress / col 5 Status / col 6 Open).

Replace the per-row loop body (currently from `for (int i = 0; i < rows.size(); ++i)` ... through the matching close brace of the loop) with the new 6-column build:

```cpp
    for (int i = 0; i < rows.size(); ++i) {
        const anilist::VolumeRow& row = rows.at(i);

        // Col 0 -- checkbox (Task 5 wires onVolumeCheckboxToggled). For Task
        // 2 the checkbox is mounted but its toggled signal isn't yet
        // connected; clicks toggle the visual state only.
        auto* cb = new QCheckBox(m_volumesTable);
        cb->setObjectName(QStringLiteral("ComicsSeriesVolumeRowCheckbox"));
        // Center the checkbox in the cell.
        auto* cbWrap = new QWidget(m_volumesTable);
        auto* cbLay = new QHBoxLayout(cbWrap);
        cbLay->setContentsMargins(0, 0, 0, 0);
        cbLay->addStretch(1);
        cbLay->addWidget(cb);
        cbLay->addStretch(1);
        m_volumesTable->setCellWidget(i, /*col=*/0, cbWrap);

        // Col 1 -- volume index. The col-0 stash (chapterIds + cbz path)
        // pattern that F1 relies on now lives on the col-1 indexItem since
        // col-0 is occupied by the checkbox widget.
        auto* indexItem = new QTableWidgetItem(QString::number(row.volumeNumber));
        indexItem->setTextAlignment(Qt::AlignCenter);
        indexItem->setData(Qt::UserRole, row.chapterNumbers);
        if (m_downloadIndex) {
            const QString cbzPath = m_downloadIndex->cbzPathForVolume(m_currentAnilistId, row.volumeNumber);
            if (!cbzPath.isEmpty()) {
                indexItem->setData(Qt::UserRole + 1, cbzPath);
            }
        }
        m_volumesTable->setItem(i, /*col=*/1, indexItem);

        // Col 2 -- thumbnail (48x64 portrait). The QTableWidgetItem holds
        // the icon; loadCoverUrlForVolume populates it async post-paint.
        auto* coverItem = new QTableWidgetItem();
        coverItem->setData(Qt::DecorationRole, QIcon());
        m_volumesTable->setItem(i, /*col=*/2, coverItem);
        if (!row.coverUrl.isEmpty()) {
            loadCoverUrlForVolume(row.coverUrl, row.volumeNumber);
        }

        // Col 3 -- stacked title: "Volume N" on top line + chapter range on
        // second line. Mounted as a cellWidget so two QLabels can stack
        // inside a single cell (QTableWidgetItem cannot hold multi-line
        // formatted content). Stream parity at StreamDetailView.cpp:1024-1090.
        auto* titleWrap = new QWidget(m_volumesTable);
        auto* titleLay = new QVBoxLayout(titleWrap);
        titleLay->setContentsMargins(8, 6, 8, 6);
        titleLay->setSpacing(2);

        auto* volLabel = new QLabel(tr("Volume %1").arg(row.volumeNumber), titleWrap);
        volLabel->setStyleSheet(QStringLiteral("color: #e5e7eb; font-size: 12px; font-weight: 500; background: transparent;"));
        titleLay->addWidget(volLabel);

        auto* rangeLabel = new QLabel(formatChapterRange(row), titleWrap);
        rangeLabel->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.55); font-size: 11px; background: transparent;"));
        titleLay->addWidget(rangeLabel);

        m_volumesTable->setCellWidget(i, /*col=*/3, titleWrap);

        // Col 4 -- progress (v1 ships "--" per spec; per-chapter read-state
        // wiring is out of scope for this plan).
        auto* progItem = new QTableWidgetItem(QStringLiteral("--"));
        progItem->setTextAlignment(Qt::AlignCenter);
        progItem->setForeground(QBrush(QColor(255, 255, 255, 128)));
        m_volumesTable->setItem(i, /*col=*/4, progItem);

        // Col 5 -- status text.
        const bool downloaded = (indexItem->data(Qt::UserRole + 1).toString().isEmpty() == false);
        auto* statusItem = new QTableWidgetItem(downloaded
            ? tr("Downloaded")
            : tr("Not downloaded"));
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setForeground(QBrush(QColor(255, 255, 255, 140)));
        m_volumesTable->setItem(i, /*col=*/5, statusItem);
    }
    m_volumesTable->setRowCount(rows.size());
```

The function's top portion (the `m_currentVolumeRows = rows;` cache + the `setRowCount` resize + the empty-state branch) STAYS unchanged.

- [ ] **Step 3: Edit ComicsSeriesView.cpp -- update F1 slot to read from col-1**

The F1 slots (`onVolumeCellClicked` + `onVolumeCurrentChanged` + `populateSourcesForRow`) currently read the chapterIds/cbz stash from `m_volumesTable->item(row, 0)`. Col 0 is now a checkbox widget, NOT a `QTableWidgetItem`. The stash moved to col 1 in Step 2.

Update the three slot bodies to use col 1 instead of col 0:

In `onVolumeCellClicked` (current around line 891+):
```cpp
    if (auto* item = m_volumesTable->item(row, /*col=*/1)) {  // STREAM_PORT Task 2: col 0 is now checkbox; stash lives on col 1
        const QString cbzPath = item->data(Qt::UserRole + 1).toString();
        if (!cbzPath.isEmpty()) {
            const anilist::VolumeRow& volRow = m_currentVolumeRows.at(row);
            emit openVolume(volRow.volumeNumber, cbzPath);
        }
    }
```

In `populateSourcesForRow`:
```cpp
    QStringList chapterIds;
    if (auto* item = m_volumesTable->item(row, /*col=*/1)) {  // STREAM_PORT Task 2: col 0 is now checkbox; stash lives on col 1
        chapterIds = item->data(Qt::UserRole).toStringList();
    }
```

Any other lookups against `m_volumesTable->item(row, 0)` elsewhere in the file (search for `item(i, 0)` and `item(row, 0)` and `item(tableRow, 0)`) also need to flip to `1`. There are likely 3-5 such sites in `loadCoverUrlForVolume` / `applyPixmapToVolumeRow` / `setRowOpenIndicator`. Update them ALL.

Also -- the `setRowOpenIndicator(int tableRow, bool downloaded)` helper (currently around line 873) writes the chevron icon into col 6. Col 6 is gone. Delete the function declaration from the .h AND delete the function body from the .cpp AND delete all call sites (grep `setRowOpenIndicator`).

- [ ] **Step 4: Edit ComicsSeriesView.cpp -- add include for QCheckBox**

At the top of `src/ui/pages/comics/ComicsSeriesView.cpp` (the include block around lines 10-40), add:
```cpp
#include <QCheckBox>
```
Place it in alphabetical order between `<QBrush>` and `<QColor>`.

- [ ] **Step 5: Build verify**

Run from project root:
```
build_check.bat
```
Expected: last line `BUILD OK`.

Likely failure modes:
- `setRowOpenIndicator` call site missed -- grep for `setRowOpenIndicator` after the edits to find any straggler.
- `m_volumesTable->item(row, 0)` reference still in the file -- grep to find.
- Missing `#include <QCheckBox>` -- the compiler will say `'QCheckBox': undeclared identifier`.

- [ ] **Step 6: ASCII sweep**

Same command as Task 1 Step 8:
```powershell
$files = 'src/ui/pages/comics/ComicsSeriesView.cpp','src/ui/pages/comics/ComicsSeriesView.h'; foreach ($f in $files) { $bytes = [System.IO.File]::ReadAllBytes($f); $n = ($bytes | Where-Object { $_ -gt 127 }).Count; Write-Output ("{0}: {1} non-ascii bytes" -f $f, $n) }
```
Expected: `0` for both files.

- [ ] **Step 7: Hemanth visual smoke**

Ask Hemanth to:
1. Close + rebuild via `build_and_run.bat`.
2. Open Comics -> Death Note.
3. Verify visually:
   - Volume table now has 6 columns (no chevron col on the right).
   - Each row shows: checkbox (left edge) / # / thumbnail / "Volume N" + "Chs A-B" stacked / "--" progress / status text.
   - Click a row -> Sources panel populates (F1 still works on click).
   - Arrow-key down/up -> Sources panel updates per row (F1 still works on keyboard).
   - Selection style is a subtle whitish tint -- NO gold bar.

If F1 regressed (Sources doesn't update on click OR on arrow nav), Step 3 missed a col-0 -> col-1 stash update. Re-grep the file for `item(row, 0)` / `item(i, 0)` / `item(tableRow, 0)` and update.

- [ ] **Step 8: RTC post**

Append to `agents/chat.md`:

```
## Agent 1 - STREAM_PORT Task 2: volume table 6-column refactor - 2026-05-18

READY TO COMMIT - [Agent 1, Task 2 of docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md SHIPPED 2026-05-18. Volume table reshaped from 7 cols to 6: col-0 = checkbox placeholder (wired in Task 5), col-1 = #, col-2 = thumb (48x64 portrait), col-3 = stacked "Volume N" + "Chs A-B" via cellWidget, col-4 = progress ("--" v1), col-5 = status. Col-6 "Open" chevron + setRowOpenIndicator helper REMOVED entirely. F1 chapterIds/cbz stash migrated from col-0 -> col-1 to make room for the checkbox cellWidget; updated all m_volumesTable->item(row,0) callsites accordingly. Selection style: subtle 8% white tint (gold-stripe gone). build_check.bat BUILD OK. ASCII sweep clean. Hemanth visual smoke: 6-col layout + Sources populate on click + arrow-nav still updates Sources + no gold bar.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsSeriesView.cpp, src/ui/pages/comics/ComicsSeriesView.h, agents/chat.md
```

---

### Task 3: Description 3-line clamp + Show more

**Goal:** Clamp the synopsis to 3 lines via `QFontMetrics::lineSpacing * 3`. Add a flat "Show more" / "Show less" toggle below it. Stream parity at StreamDetailView.cpp:454-472 + onDescShowMoreClicked logic.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` -- add `m_descShowMoreBtn`, `m_descExpanded`, `m_descClampLines` members + `onDescShowMoreClicked` slot decl.
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` -- add the button in buildUi (leftCol, between m_synopsis and m_volumesTable); add slot body; apply clamp on synopsis paint.

**Reference:** StreamDetailView.cpp:454-472 (label + button wiring); StreamDetailView's `onDescShowMoreClicked` slot body (search StreamDetailView.cpp for the function definition).

- [ ] **Step 1: Edit ComicsSeriesView.h -- declarations**

Add a private slot:
```cpp
private slots:
    // ... existing slots ...
    void onDescShowMoreClicked();
```

Add member declarations in the widget-members block:
```cpp
    QPushButton*  m_descShowMoreBtn = nullptr;
    bool          m_descExpanded    = false;
    int           m_descClampLines  = 3;
```

- [ ] **Step 2: Edit ComicsSeriesView.cpp buildUi() -- insert m_descShowMoreBtn after m_synopsis**

In the new `buildUi` (from Task 1), immediately AFTER the line `leftCol->addWidget(m_synopsis);`, insert:

```cpp
    m_descShowMoreBtn = new QPushButton(tr("Show more"), this);
    m_descShowMoreBtn->setObjectName(QStringLiteral("ComicsSeriesDescShowMore"));
    m_descShowMoreBtn->setCursor(Qt::PointingHandCursor);
    m_descShowMoreBtn->setFlat(true);
    m_descShowMoreBtn->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesDescShowMore {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.75);"
        "  font-size: 11px;"
        "  padding: 0;"
        "  text-align: left;"
        "}"
        "QPushButton#ComicsSeriesDescShowMore:hover {"
        "  color: #fff;"
        "  text-decoration: underline;"
        "}"));
    m_descShowMoreBtn->hide();
    connect(m_descShowMoreBtn, &QPushButton::clicked,
            this, &ComicsSeriesView::onDescShowMoreClicked);
    leftCol->addWidget(m_descShowMoreBtn, /*stretch*/ 0, Qt::AlignLeft);
```

- [ ] **Step 3: Edit ComicsSeriesView.cpp -- clamp m_synopsis on text-set**

Locate `renderDetail(...)` (around line 480-520). Find the line that sets the synopsis text (search the function body for `m_synopsis->setText(`). Immediately AFTER the `setText` call, add the clamp + toggle-visibility logic:

```cpp
    // STREAM_PORT 2026-05-18 Task 3: 3-line clamp via QFontMetrics. Mirrors
    // StreamDetailView.cpp:454-472 logic. The "Show more" toggle is hidden
    // when the full description fits within the clamp (i.e. clamping is a
    // no-op).
    if (m_synopsis) {
        m_descExpanded = false;
        const QFontMetrics fm(m_synopsis->font());
        const int clampHeight = fm.lineSpacing() * m_descClampLines;
        m_synopsis->setMaximumHeight(clampHeight);

        // Determine if the description overflows the clamp -- compute
        // natural sizeHint with width set to the synopsis's current width.
        const int needed = m_synopsis->heightForWidth(m_synopsis->width() > 0 ? m_synopsis->width() : 600);
        const bool overflows = (needed > clampHeight);
        if (m_descShowMoreBtn) {
            m_descShowMoreBtn->setText(tr("Show more"));
            m_descShowMoreBtn->setVisible(overflows);
        }
    }
```

- [ ] **Step 4: Edit ComicsSeriesView.cpp -- add onDescShowMoreClicked slot body**

Add the slot body at the bottom of the file (above the closing `} // namespace tankoban::manga::comics`):

```cpp
void ComicsSeriesView::onDescShowMoreClicked()
{
    if (!m_synopsis || !m_descShowMoreBtn) return;
    m_descExpanded = !m_descExpanded;
    if (m_descExpanded) {
        m_synopsis->setMaximumHeight(QWIDGETSIZE_MAX);
        m_descShowMoreBtn->setText(tr("Show less"));
    } else {
        const QFontMetrics fm(m_synopsis->font());
        m_synopsis->setMaximumHeight(fm.lineSpacing() * m_descClampLines);
        m_descShowMoreBtn->setText(tr("Show more"));
    }
}
```

- [ ] **Step 5: Build verify, ASCII sweep, smoke, RTC**

Run `build_check.bat` (expect `BUILD OK`).

ASCII sweep:
```powershell
$files = 'src/ui/pages/comics/ComicsSeriesView.cpp','src/ui/pages/comics/ComicsSeriesView.h'; foreach ($f in $files) { $bytes = [System.IO.File]::ReadAllBytes($f); $n = ($bytes | Where-Object { $_ -gt 127 }).Count; Write-Output ("{0}: {1} non-ascii bytes" -f $f, $n) }
```
Expected `0`.

Hemanth smoke: open a series with a long description (Death Note, One Piece, Berserk). Verify:
- Synopsis text shows ~3 lines max.
- "Show more" link appears below in muted white-75%.
- Click "Show more" -> full description shows + link text becomes "Show less".
- Click "Show less" -> back to 3-line clamp.
- Open a series with a SHORT description (test with a one-shot if possible) -> "Show more" link is HIDDEN (no overflow).

RTC append to `agents/chat.md`:
```
## Agent 1 - STREAM_PORT Task 3: description 3-line clamp + Show more - 2026-05-18

READY TO COMMIT - [Agent 1, Task 3 of docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md SHIPPED 2026-05-18. Synopsis clamps to 3 lines via QFontMetrics::lineSpacing * 3 set as maximumHeight; m_descShowMoreBtn (flat, white-75% color, hover white) appears below when text overflows the clamp; click toggles m_descExpanded -> swaps maximumHeight between QWIDGETSIZE_MAX and clamp value + text "Show more"/"Show less". Mirrors StreamDetailView.cpp:454-472 + onDescShowMoreClicked. Short descriptions hide the toggle entirely. build_check OK; ASCII clean; Hemanth smoke on Death Note + a short-description series.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsSeriesView.cpp, src/ui/pages/comics/ComicsSeriesView.h, agents/chat.md
```

---

### Task 4: Library button passive -> action

**Goal:** Change the library button's behavior from a passive "In library" / "Add to library" label to a Stream-verbatim action button: "Remove from Library" when bookmarked, "Add to Library" when not. The button is already wired to `onLibraryButtonClicked` -> `AniListCache::toggleBookmark` (verified). The change is the LABEL TEXT in `refreshLibraryButton()` only.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` -- `refreshLibraryButton()` body (line ~647).

**Reference:** StreamDetailView's library button (search `Remove from Library` in StreamDetailView.cpp; the label flip on bookmark state lives in StreamLibrary's signal handler).

- [ ] **Step 1: Edit refreshLibraryButton()**

Locate `void ComicsSeriesView::refreshLibraryButton()` (currently line 647-655 area). Replace the body with:

```cpp
void ComicsSeriesView::refreshLibraryButton()
{
    if (!m_libraryButton) return;
    const bool hasSeries  = (m_currentAnilistId > 0);
    const bool bookmarked = (m_cache && hasSeries) ? m_cache->isBookmarked(m_currentAnilistId) : false;
    m_libraryButton->setEnabled(hasSeries && m_cache);
    // STREAM_PORT 2026-05-18 Task 4: Stream-verbatim labels. Was "In library"
    // (passive) / "Add to library" (action mixed); now "Remove from Library"
    // (active when bookmarked) / "Add to Library" (active when not).
    m_libraryButton->setText(bookmarked ? tr("Remove from Library") : tr("Add to Library"));
}
```

- [ ] **Step 2: Build verify, ASCII sweep, smoke, RTC**

`build_check.bat` -> `BUILD OK`.

ASCII sweep (same command as Task 1 Step 8) -> `0` both files.

Hemanth smoke:
- Series NOT in library -> button text "Add to Library". Click -> series adds, label flips to "Remove from Library".
- Series IN library -> button text "Remove from Library". Click -> series removes, label flips to "Add to Library".
- The F3 narrow fix (event-filter accepting release-without-press) on m_libraryButton is preserved -- no regression on the actual click path.

RTC append to `agents/chat.md`:
```
## Agent 1 - STREAM_PORT Task 4: library button passive -> action - 2026-05-18

READY TO COMMIT - [Agent 1, Task 4 of docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md SHIPPED 2026-05-18. refreshLibraryButton now emits Stream-verbatim labels: "Remove from Library" when m_cache->isBookmarked(id), "Add to Library" otherwise. Was "In library" (passive) / "Add to library". One-line change; QSS unchanged; F3 narrow fix on m_libraryButton preserved.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/comics/ComicsSeriesView.cpp, agents/chat.md
```

---

### Task 5: Multi-select wiring + Download Selected button

**Goal:** Wire the col-0 checkboxes (placed in Task 2) to a `QSet<int> m_selectedRows` member. Add an `m_downloadSelectedBtn` below the volume table that shows "Download Selected (N)" when N >= 1 and hides otherwise. Click emits `downloadDispatchRequested(volRow, chapterIds)` once per selected row.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` -- add `m_selectedRows`, `m_downloadSelectedBtn` members + `onVolumeCheckboxToggled` + `onDownloadSelectedClicked` slot decls.
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` -- wire connect in populateVolumeRows (each new checkbox); add m_downloadSelectedBtn in buildUi (leftCol, after m_volumesTable); add slot bodies.

**Reference:** StreamDetailView.cpp:599-602 (`m_downloadSelectedBtn` widget setup); StreamDetailView's `onSelectedEpisodesToggled` slot pattern.

- [ ] **Step 1: Edit ComicsSeriesView.h -- declarations**

Add to private slots:
```cpp
    void onVolumeCheckboxToggled(int row, bool checked);
    void onDownloadSelectedClicked();
```

Add to private members:
```cpp
    QSet<int>      m_selectedRows;
    QPushButton*   m_downloadSelectedBtn = nullptr;
```

Add include:
```cpp
#include <QSet>
```
at the top of the .h, alphabetically.

- [ ] **Step 2: Edit ComicsSeriesView.cpp buildUi() -- add m_downloadSelectedBtn after m_volumesTable**

In the new `buildUi` (from Task 1), immediately AFTER the line `leftCol->addWidget(m_volumesTable, /*stretch*/ 1);`, insert:

```cpp
    m_downloadSelectedBtn = new QPushButton(this);
    m_downloadSelectedBtn->setObjectName(QStringLiteral("ComicsSeriesDownloadSelectedBtn"));
    m_downloadSelectedBtn->setCursor(Qt::PointingHandCursor);
    m_downloadSelectedBtn->setFixedHeight(32);
    m_downloadSelectedBtn->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesDownloadSelectedBtn {"
        "  background: rgba(255,255,255,0.10);"
        "  border: 1px solid rgba(255,255,255,0.20);"
        "  border-radius: 6px;"
        "  color: #fff;"
        "  padding: 6px 14px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "}"
        "QPushButton#ComicsSeriesDownloadSelectedBtn:hover {"
        "  background: rgba(255,255,255,0.14);"
        "  border-color: rgba(255,255,255,0.30);"
        "}"));
    m_downloadSelectedBtn->hide();
    connect(m_downloadSelectedBtn, &QPushButton::clicked,
            this, &ComicsSeriesView::onDownloadSelectedClicked);
    leftCol->addWidget(m_downloadSelectedBtn, /*stretch*/ 0, Qt::AlignRight);
```

- [ ] **Step 3: Edit ComicsSeriesView.cpp populateVolumeRows() -- wire checkbox toggled**

In the per-row loop body from Task 2 Step 2, locate the `cb` creation block (the `auto* cb = new QCheckBox(m_volumesTable);` and surrounding setup). Immediately AFTER the `m_volumesTable->setCellWidget(i, /*col=*/0, cbWrap);` line, add:

```cpp
        const int rowIdx = i;  // snapshot for lambda capture
        connect(cb, &QCheckBox::toggled, this,
                [this, rowIdx](bool checked) {
            onVolumeCheckboxToggled(rowIdx, checked);
        });
```

Also -- at the top of `populateVolumeRows` BEFORE the per-row loop, clear stale selection state:
```cpp
    m_selectedRows.clear();
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();
```

Place those two lines right after `m_currentVolumeRows = rows;` (which is near the top of the function).

- [ ] **Step 4: Edit ComicsSeriesView.cpp -- add onVolumeCheckboxToggled + onDownloadSelectedClicked slot bodies**

Add at the bottom of the .cpp file (above the namespace close `}`):

```cpp
void ComicsSeriesView::onVolumeCheckboxToggled(int row, bool checked)
{
    // STREAM_PORT 2026-05-18 Task 5: tracks per-row selection in
    // m_selectedRows. The Download Selected button shows when at least one
    // row is checked; its label updates with the count.
    if (checked) m_selectedRows.insert(row);
    else         m_selectedRows.remove(row);

    if (!m_downloadSelectedBtn) return;
    const int n = m_selectedRows.size();
    m_downloadSelectedBtn->setText(tr("Download Selected (%1)").arg(n));
    m_downloadSelectedBtn->setVisible(n > 0);
}

void ComicsSeriesView::onDownloadSelectedClicked()
{
    // STREAM_PORT 2026-05-18 Task 5: bulk-download dispatch. Emits
    // downloadDispatchRequested once per selected row. ComicsPage's existing
    // wiring routes each emission through the active provider.
    if (m_selectedRows.isEmpty() || !m_volumesTable) return;

    // Snapshot the set since the dispatch may indirectly clear it.
    const QList<int> rows = QList<int>(m_selectedRows.cbegin(), m_selectedRows.cend());
    for (int row : rows) {
        if (row < 0 || row >= m_currentVolumeRows.size()) continue;
        const anilist::VolumeRow& volRow = m_currentVolumeRows.at(row);

        QStringList chapterIds;
        if (auto* item = m_volumesTable->item(row, /*col=*/1)) {
            chapterIds = item->data(Qt::UserRole).toStringList();
        }

        // The dispatch shape mirrors the single-volume path from
        // ComicsSourcesPanel::downloadRequested -- ComicsPage routes both
        // signals into the same provider entrypoint.
        emit downloadDispatchRequested(volRow, chapterIds);
    }
}
```

- [ ] **Step 5: Build verify, ASCII sweep, smoke, RTC**

`build_check.bat` -> `BUILD OK`. Missing `<QSet>` include is the most likely failure.

ASCII sweep:
```powershell
$files = 'src/ui/pages/comics/ComicsSeriesView.cpp','src/ui/pages/comics/ComicsSeriesView.h'; foreach ($f in $files) { $bytes = [System.IO.File]::ReadAllBytes($f); $n = ($bytes | Where-Object { $_ -gt 127 }).Count; Write-Output ("{0}: {1} non-ascii bytes" -f $f, $n) }
```
Expected `0`.

Hemanth smoke on Death Note:
- Check checkbox on Volume 1 -> "Download Selected (1)" button appears below the table.
- Check Volume 3 -> label becomes "Download Selected (2)".
- Uncheck Volume 1 -> label "Download Selected (1)".
- Uncheck Volume 3 -> button hides.
- Check Volume 2 + Volume 5 + click "Download Selected (2)" -> two `downloadDispatchRequested` signals fire (visible via Tankorent download list growing by two entries, or via Hemanth's screenshot of the resulting downloads).
- F1 NOT REGRESSED: clicking a row's title area (not the checkbox) still populates Sources panel; arrow-nav still updates Sources panel.

Potential regression check: a click on the col-0 checkbox cell area may ALSO fire `currentCellChanged` and update Sources to the checkbox-clicked row's sources, even though the user only meant to toggle the checkbox. If Hemanth flags this as surprising, see Task 7 Polish for the gate fix.

RTC append:
```
## Agent 1 - STREAM_PORT Task 5: multi-select + Download Selected - 2026-05-18

READY TO COMMIT - [Agent 1, Task 5 of docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md SHIPPED 2026-05-18. Multi-select wired: each col-0 QCheckBox connects to onVolumeCheckboxToggled(row, checked) lambda that updates m_selectedRows QSet<int>. m_downloadSelectedBtn shows "Download Selected (N)" when N>=1, hidden otherwise. Click -> onDownloadSelectedClicked iterates m_selectedRows, emits downloadDispatchRequested(volRow, chapterIds) once per row. m_selectedRows clears + button hides at populateVolumeRows top so navigating between series resets state. F1 preserved.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsSeriesView.cpp, src/ui/pages/comics/ComicsSeriesView.h, agents/chat.md
```

---

### Task 6: Next-unread highlight + auto-scroll

**Goal:** Compute `m_nextUnreadRow` in `populateVolumeRows()` as the first row whose stashed cbz path is empty (proxy for "not downloaded / not started"). On first paint of a series, scroll the table to that row centered, and apply a subtle 2px left-edge accent on it (NOT the gold stripe -- a quieter rgba(255,255,255,0.30) accent).

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` -- add `m_nextUnreadRow` member.
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` -- populateVolumeRows computes + scrolls + accents.

- [ ] **Step 1: Edit ComicsSeriesView.h -- member declaration**

Add to private members:
```cpp
    // STREAM_PORT 2026-05-18 Task 6: index of the first volume the user
    // hasn't started reading (proxy: first row whose stashed cbz path is
    // empty). -1 if all rows downloaded OR no rows. Set by
    // populateVolumeRows after the per-row loop completes.
    int            m_nextUnreadRow = -1;
```

- [ ] **Step 2: Edit ComicsSeriesView.cpp populateVolumeRows() -- compute + scroll + accent**

After the per-row loop completes (immediately after the closing brace of `for (int i = 0; i < rows.size(); ++i) { ... }`), append:

```cpp
    // STREAM_PORT 2026-05-18 Task 6: next-unread highlight + auto-scroll.
    // Proxy for "unread": no stashed cbz path. The first such row is the
    // user's next stop. If every row has a cbz (all downloaded), m_nextUnreadRow
    // stays -1 and no scroll fires.
    m_nextUnreadRow = -1;
    for (int i = 0; i < m_volumesTable->rowCount(); ++i) {
        if (auto* item = m_volumesTable->item(i, /*col=*/1)) {
            const QString cbz = item->data(Qt::UserRole + 1).toString();
            if (cbz.isEmpty()) {
                m_nextUnreadRow = i;
                break;
            }
        }
    }

    if (m_nextUnreadRow >= 0 && m_nextUnreadRow < m_volumesTable->rowCount()) {
        // Scroll the next-unread row into center view on first paint.
        if (auto* item = m_volumesTable->item(m_nextUnreadRow, /*col=*/1)) {
            m_volumesTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        }

        // Apply a subtle 2px left-edge accent to the title-cell wrapper
        // widget at col 3 (titleWrap, from Task 2 Step 2). Re-applying QSS
        // on the wrapper is safe -- it overrides the default transparent
        // background only for the next-unread row.
        if (QWidget* titleWrap = m_volumesTable->cellWidget(m_nextUnreadRow, /*col=*/3)) {
            titleWrap->setStyleSheet(QStringLiteral(
                "QWidget { border-left: 2px solid rgba(255,255,255,0.30); }"));
        }
    }
```

- [ ] **Step 3: Build verify, ASCII sweep, smoke, RTC**

`build_check.bat` -> `BUILD OK`.

ASCII sweep:
```powershell
$files = 'src/ui/pages/comics/ComicsSeriesView.cpp','src/ui/pages/comics/ComicsSeriesView.h'; foreach ($f in $files) { $bytes = [System.IO.File]::ReadAllBytes($f); $n = ($bytes | Where-Object { $_ -gt 127 }).Count; Write-Output ("{0}: {1} non-ascii bytes" -f $f, $n) }
```
Expected `0`.

Hemanth smoke:
- Open Death Note (no volumes downloaded) -> Volume 1 row shows a subtle 2px white-30% left accent on the title cell. Table scroll position centers on Volume 1.
- Download Volume 1, then re-open Death Note -> Volume 2 row gets the accent; table scrolls to Volume 2.
- Open a series with ALL volumes downloaded (synthetic case -- could verify later) -> no accent, table scrolls to top normally.
- The accent is subtle, NOT prominent like the prior gold stripe.

RTC append:
```
## Agent 1 - STREAM_PORT Task 6: next-unread highlight + auto-scroll - 2026-05-18

READY TO COMMIT - [Agent 1, Task 6 of docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md SHIPPED 2026-05-18. populateVolumeRows() computes m_nextUnreadRow as first row with empty stashed cbz path; if found, scrollToItem(PositionAtCenter) + apply 2px white-30% left-edge accent QSS to the col-3 titleWrap widget. If all rows have cbz, m_nextUnreadRow = -1 + no scroll + no accent. Subtle, not the gold stripe.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/comics/ComicsSeriesView.cpp, src/ui/pages/comics/ComicsSeriesView.h, agents/chat.md
```

---

### Task 7: Polish + final smoke (optional / conditional)

**Goal:** Pick up any visual rough edges Hemanth flags during Tasks 1-6's smokes. Common items expected:

- Banner-image crop alignment (Death Note crops fine; One Piece banner may need vertical alignment tweak; Berserk square art may letterbox).
- Padding tweaks between hero / contentRow / table.
- Hero-image fallback verification for a series with no banner URL (renders flat #101010 block per QSS).
- F1 gate-on-checkbox-click decision: if Hemanth flags that clicking a col-0 checkbox surprisingly updates the Sources panel (because currentCellChanged fires for the checkbox cell), add a gate in `populateSourcesForRow` to early-return when the click target was the checkbox column.

**Files:** TBD per Hemanth feedback after Tasks 1-6 smokes; expected to be the same two files (`ComicsSeriesView.{cpp,h}`).

- [ ] **Step 1: Collect Hemanth feedback after Tasks 1-6**

Wait for Hemanth's visual verdict after the Task 6 smoke. List any flagged issues.

- [ ] **Step 2: Apply targeted fixes**

For each flagged item, apply a minimal-blast-radius edit. Re-build + ASCII sweep + visual re-verify after each edit per `feedback_one_fix_per_rebuild.md`.

- [ ] **Step 3: Final 3-series visual verification**

Ask Hemanth to verify the new layout on three different series with varied data shapes:
- **Death Note** (12 volumes, completed, banner image cached, short description).
- **One Piece** (114 volumes, ongoing, banner image, long description, requires scroll).
- **Berserk** (43 volumes, hiatus status, banner image, MangaUpdates fallback path -- vol count came from yesterday's MangaUpdates ship).

For each, verify: banner renders, title size readable, description clamps + Show more works, multi-select toggles + Download Selected fires, next-unread accent visible on the right row, no gold bar.

- [ ] **Step 4: RTC post**

Append the closing arc RTC to `agents/chat.md`:
```
## Agent 1 - STREAM_PORT Task 7: polish + arc close - 2026-05-18

READY TO COMMIT - [Agent 1, Task 7 of docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md SHIPPED 2026-05-18. <list flagged Hemanth feedback items + fixes applied>. 3-series visual verification PASSED on Death Note + One Piece + Berserk. COMICS_TANKOYOMI_STREAM_MERGER series-view-layout-port piece CLOSED end-to-end. Net stats across Tasks 1-7: ~<XXX> LOC, 7 commits, 0 regressions on F1 / COMICS_SOURCES_SIDEBAR v1 / MangaUpdates fallback / mode-pill reset / F3 narrow fix.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion, /simplify] | files: src/ui/pages/comics/ComicsSeriesView.cpp, src/ui/pages/comics/ComicsSeriesView.h, agents/chat.md
```

---

## Self-Review (per writing-plans skill)

### 1. Spec coverage

| Spec section | Task |
|--------------|------|
| Sec 2 Decision 1 (140px banner block) | Task 1 |
| Sec 2 Decision 2 (Cover->Thumb + stacked title) | Task 2 |
| Sec 2 Decision 3 (Library button verbatim) | Task 4 |
| Sec 2 Decision 4 (3-line clamp + Show more) | Task 3 |
| Sec 2 Decision 5 (multi-select checkbox + bulk) | Task 5 (checkbox placement in Task 2) |
| Sec 2 Decision 6 (drop col-6) | Task 2 |
| Sec 2 Decision 7 (skip season-selector slot v1) | Task 1 (slot reserved as comment) |
| Sec 2 Decision 8 (next-unread highlight) | Task 6 |
| Sec 2 P1 (Sources full-vertical right) | Task 1 |
| Sec 2 P2 (8% white selection style) | Task 1 (QSS root) |
| Sec 2 P3 (keep QTableWidget) | All tasks |
| Sec 2 P4 (preserve F1) | Task 2 Step 3 (col-0->col-1 stash migration) |
| Sec 2 P5 (preserve Sources Sidebar v1) | Task 1 (panel reparented only) |
| Sec 2 P6 (mode-pill reset contract) | Task 1 (m_backButton emits navigationRequested) |
| Sec 3.3 New members | Tasks 1, 3, 5, 6 |
| Sec 3.4 Removed members (m_bannerPixmap) | Task 1 Step 1b + Step 5 |
| Sec 11 Risks (F1 regression / banner crop / multi-select-click race) | Tasks 2, 6, 7 |

All spec sections have at least one task.

### 2. Placeholder scan

Grep for TBD / TODO / FIXME / "Similar to Task N":
- Task 7 has the only "TBD" -- it is intentionally scoped to "fixes per Hemanth feedback after Tasks 1-6", which IS the actual scope of a polish task (cannot pre-author code for unknown issues). Acceptable.
- No "Similar to Task N" shortcuts -- every task's build/sweep/smoke/RTC steps repeat the code in full.
- No "implement later" anywhere.

### 3. Type consistency

- `m_volumesTable->item(row, 1)` used consistently from Task 2 onward (col-0 is checkbox cellWidget, NOT QTableWidgetItem; col-1 holds the stash).
- `onVolumeCheckboxToggled(int row, bool checked)` signature consistent between Task 5 Step 1 (decl) + Step 3 (lambda call) + Step 4 (body).
- `populateSourcesForRow(int row)` from the F1 ship is unchanged across all tasks.
- `downloadDispatchRequested(VolumeRow, QStringList)` signal signature is the existing ComicsSeriesView::downloadDispatchRequested -- new emit site in Task 5 Step 4 uses the same shape.

No inconsistencies found.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md`. Two execution options:

**1. Subagent-Driven (recommended)** -- dispatch a fresh subagent per task, review between tasks, fast iteration. Each subagent gets the spec + plan + one task's worth of code; runs in its own context; returns with the RTC line for me to verify before the next subagent fires.

**2. Inline Execution** -- execute tasks in this session using `superpowers:executing-plans`. Batch execution with checkpoints for review after each task. Heavier on this session's context but no subagent dispatch overhead.

Which approach?
