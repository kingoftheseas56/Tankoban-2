# Comics Catalog Browser + Series-View Wiring — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land Comics-mode Catalog browser end-to-end (browse 107 Fandom-sourced series → click a tile → land on the existing Stream-blueprint series detail), then upgrade volume rows to a Stream-mimicked VolumeTile widget.

**Architecture:** Phase 1 swaps the private `CatalogTile` for the shared `TileCard` widget + `PosterCache` singleton, then wires a new `ComicsPage::onCatalogTileActivated` slot to route tile clicks into the existing `ComicsSeriesView::showSeries(MediaPreview)` path (the local-first short-circuit in `dispatchFandomResolve` already handles volume row population). Phase 2 builds a new `VolumeTile` widget mirroring `EpisodeTile`'s shape + signals (5-state chip palette, shift+click range-fill, MangaDownloadIndex subscription) and replaces `ComicsSeriesView::m_volumesTable` (QTableWidget) with a `QScrollArea` of VolumeTile rows.

**Tech Stack:** C++20 / Qt6.10 / `build_check.bat` (compile gate, Codex #4 Stage 1) / `out/tankoctl.exe` (dev-control bridge for state queries) / pywinauto-mcp (visual smoke fallback) / `tankoban_tests` + GoogleTest (pure-logic only, opt in via `-DTANKOBAN_BUILD_TESTS=ON`).

**Spec:** [docs/superpowers/specs/2026-05-22-comics-catalog-series-view-design.md](../specs/2026-05-22-comics-catalog-series-view-design.md)

---

## Phase 1 — Catalog grid via TileCard + click wiring

Outcome: Catalog button in Comics search bar → grid of 107 TileCards → click any tile → series detail loads from local catalog JSON. Volume rows still on today's QTableWidget (Phase 2 swaps that out).

---

### Task 1: Expand `ComicsCatalogScreen::seriesActivated` to carry anilistId

**Why:** `ComicsPage::onCatalogTileActivated` needs `anilistId` to build a `MediaPreview` that drives the existing `showSeries(MediaPreview)` path. Passing it through the signal payload (instead of a second lookup in ComicsPage) keeps the lookup co-located with the data source.

**Files:**
- Modify: `src/ui/pages/comics/ComicsCatalogScreen.h:51`

- [ ] **Step 1: Edit the signal payload**

Open [src/ui/pages/comics/ComicsCatalogScreen.h](../../../src/ui/pages/comics/ComicsCatalogScreen.h) and change line 51:

```cpp
// before
void seriesActivated(const QString& seriesId, const QString& seriesTitle);

// after
void seriesActivated(const QString& seriesId, const QString& seriesTitle, int anilistId);
```

- [ ] **Step 2: Compile-verify the header change**

Run: `build_check.bat`
Expected: `BUILD FAILED` with a signal/slot connect error in `ComicsPage::showCatalogMode` (the lambda at ~line 2347 still has the old 2-arg signature). This proves the header change reached the compiler; the lambda fix comes in Task 5.

- [ ] **Step 3: Commit just the header change**

```bash
taskkill /F /IM Tankoban.exe 2>nul
git add src/ui/pages/comics/ComicsCatalogScreen.h
git commit -m "[Agent 1, Phase 1 Task 1 -- ComicsCatalogScreen::seriesActivated carries anilistId]"
```

---

### Task 2: Refactor ComicsCatalogScreen.{h,cpp} to use shared `TileCard`

**Why:** Per the "we mimic stream mode any chance we get" directive, drop the private `CatalogTile` and use `TileCard` (the same widget Stream's CatalogBrowseScreen + 5 other contexts use).

**Files:**
- Modify: `src/ui/pages/comics/ComicsCatalogScreen.h` (forward decl + member type)
- Modify: `src/ui/pages/comics/ComicsCatalogScreen.cpp` (tile construction)

- [ ] **Step 1: Drop the CatalogTile forward decl and replace member**

In [src/ui/pages/comics/ComicsCatalogScreen.h](../../../src/ui/pages/comics/ComicsCatalogScreen.h):

```cpp
// at top, near other forward decls -- DELETE:
namespace tankoban::ui::comics {
class CatalogTile;

// becomes (just drop the CatalogTile line):
namespace tankoban::ui::comics {

// at #include block -- ADD:
#include "ui/pages/TileCard.h"

// member declaration on line 66 -- CHANGE:
QList<CatalogTile*>    m_tiles;
// to:
QList<TileCard*>       m_tiles;

// the addTile signature on line 57 -- CHANGE:
void addTile(const tankoban::manga::fandom::FandomCatalog& catalog);
// stays the same -- internal implementation changes only

// the fetchCover signature on line 58 -- CHANGE:
void fetchCover(CatalogTile* tile, const QString& url);
// to:
void fetchCover(TileCard* tile, const QString& url);
```

- [ ] **Step 2: Rewrite `addTile` to construct a TileCard**

In [src/ui/pages/comics/ComicsCatalogScreen.cpp](../../../src/ui/pages/comics/ComicsCatalogScreen.cpp), the existing `ComicsCatalogScreen::addTile` body. Replace the CatalogTile construction block with:

```cpp
void ComicsCatalogScreen::addTile(const tankoban::manga::fandom::FandomCatalog& catalog)
{
    const QString title = catalog.seriesTitle;
    const QString subtitle = QStringLiteral("%1 vols").arg(catalog.volumes.size());

    // TileCard takes thumbPath as the first arg. We start with an empty path
    // and call setThumbPath / setThumbPixmap when the cover lands.
    auto* tile = new TileCard(/*thumbPath=*/QString(), title, subtitle, m_gridHost);
    tile->setCardSize(TileCard::DEFAULT_WIDTH, TileCard::DEFAULT_IMAGE_HEIGHT);

    // Volume count chip (TileCard::setBadges signature: progressFraction,
    // pageBadge, countBadge, status). For the catalog we only set countBadge.
    tile->setBadges(/*progressFraction=*/0.0, /*pageBadge=*/QString(),
                    /*countBadge=*/subtitle, /*status=*/QString());

    // Provenance pin top-left: catalog-sourced = "Fandom"
    tile->setProvenance(QStringLiteral("Fandom"));

    // Click routes through the signal with the JSON's anilistId.
    const QString  seriesId    = catalog.seriesId;
    const QString  seriesTitle = catalog.seriesTitle;
    const int      anilistId   = catalog.anilistId;
    connect(tile, &TileCard::clicked, this, [this, seriesId, seriesTitle, anilistId]() {
        emit seriesActivated(seriesId, seriesTitle, anilistId);
    });

    m_tiles.append(tile);
    const int row = (m_tiles.size() - 1) / 6;  // 6 tiles per row
    const int col = (m_tiles.size() - 1) % 6;
    m_grid->addWidget(tile, row, col);

    // Kick off async cover fetch from catalog's volumes[0].coverUrl
    if (!catalog.volumes.isEmpty()) {
        fetchCover(tile, catalog.volumes.first().coverUrl);
    }
}
```

- [ ] **Step 3: Update `fetchCover` signature + body to use TileCard**

In the same .cpp:

```cpp
void ComicsCatalogScreen::fetchCover(TileCard* tile, const QString& url)
{
    if (!tile || url.isEmpty()) return;
    QNetworkRequest req(url);
    auto* reply = m_nam->get(req);
    QPointer<TileCard> safe(tile);
    connect(reply, &QNetworkReply::finished, this, [reply, safe]() {
        reply->deleteLater();
        if (!safe) return;
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll())) return;
        safe->setThumbPixmap(pm);
    });
}
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD OK` (Task 1's transient error is now fixed; the signal expansion in Task 1 still has the unfixed connect in ComicsPage from Task 5's territory, so this build will still FAIL on that one site -- and that's the expected next-task signal). If you see ANY other error, stop and inspect — TileCard ctor signature or DEFAULT_WIDTH constants may have drifted.

- [ ] **Step 5: Commit**

```bash
taskkill /F /IM Tankoban.exe 2>nul
git add src/ui/pages/comics/ComicsCatalogScreen.h src/ui/pages/comics/ComicsCatalogScreen.cpp
git commit -m "[Agent 1, Phase 1 Task 2 -- ComicsCatalogScreen uses shared TileCard]"
```

---

### Task 3: Hide empty `EXTRACT_FAILED` stubs at load time

**Why:** 20 of the 127 catalog JSONs have empty `volumes[]` (notes field starts with `EXTRACT_FAILED`). They aren't actionable — surface only the 107 populated ones.

**Files:**
- Modify: `src/ui/pages/comics/ComicsCatalogScreen.cpp` (inside `loadAllCatalogs`)

- [ ] **Step 1: Add the filter inside loadAllCatalogs**

Find the `ComicsCatalogScreen::loadAllCatalogs()` body. Inside the loop that iterates catalog files and calls `addTile(...)`, wrap the `addTile` call with a skip-empty guard:

```cpp
// inside loadAllCatalogs() per-file loop:
const auto loaded = tankoban::manga::fandom::LocalFandomCatalogLoader::loadFromFile(path);
if (!loaded.has_value()) continue;
if (loaded->volumes.isEmpty()) continue;   // <-- NEW: hide EXTRACT_FAILED stubs
addTile(*loaded);
```

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD FAILED` only on the Task 5 connect site (anilistId arg count). Filter change itself compiles.

- [ ] **Step 3: Commit**

```bash
taskkill /F /IM Tankoban.exe 2>nul
git add src/ui/pages/comics/ComicsCatalogScreen.cpp
git commit -m "[Agent 1, Phase 1 Task 3 -- hide empty EXTRACT_FAILED catalog stubs]"
```

---

### Task 4: Sort tiles alphabetically by `seriesTitle` (case-insensitive)

**Why:** Alphabetical is the ratified default sort. Today the load order is dir-listing-order.

**Files:**
- Modify: `src/ui/pages/comics/ComicsCatalogScreen.cpp` (inside `loadAllCatalogs`)

- [ ] **Step 1: Collect catalogs first, sort, then call addTile**

Refactor the `loadAllCatalogs()` per-file loop to two passes:

```cpp
void ComicsCatalogScreen::loadAllCatalogs()
{
    clearTiles();

    const QString catalogDir = QStringLiteral("data/fandom_catalog");
    QDir dir(catalogDir);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")},
                                                    QDir::Files | QDir::Readable);

    // Pass 1: load + filter
    QList<tankoban::manga::fandom::FandomCatalog> catalogs;
    catalogs.reserve(files.size());
    for (const QFileInfo& fi : files) {
        const auto loaded = tankoban::manga::fandom::LocalFandomCatalogLoader::loadFromFile(fi.absoluteFilePath());
        if (!loaded.has_value()) continue;
        if (loaded->volumes.isEmpty()) continue;
        catalogs.append(*loaded);
    }

    // Pass 2: sort by seriesTitle (case-insensitive)
    std::sort(catalogs.begin(), catalogs.end(),
              [](const tankoban::manga::fandom::FandomCatalog& a,
                 const tankoban::manga::fandom::FandomCatalog& b) {
                  return a.seriesTitle.compare(b.seriesTitle, Qt::CaseInsensitive) < 0;
              });

    // Pass 3: paint
    for (const auto& cat : catalogs) {
        addTile(cat);
    }

    if (m_emptyLabel) {
        m_emptyLabel->setVisible(catalogs.isEmpty());
    }
}
```

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD FAILED` only on the Task 5 connect site. This task compiles standalone.

- [ ] **Step 3: Commit**

```bash
taskkill /F /IM Tankoban.exe 2>nul
git add src/ui/pages/comics/ComicsCatalogScreen.cpp
git commit -m "[Agent 1, Phase 1 Task 4 -- alphabetical case-insensitive catalog sort]"
```

---

### Task 5: Implement `ComicsPage::onCatalogTileActivated` slot

**Why:** This is the single new lambda body that wires the catalog click into the existing `ComicsSeriesView::showSeries(MediaPreview)` path. The local-first short-circuit in `dispatchFandomResolve` already handles volume row population — we just need to fire `showSeries` with a MediaPreview carrying the right identity.

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (add slot decl)
- Modify: `src/ui/pages/ComicsPage.cpp` (replace stub lambda at ~line 2347 + add slot impl)

- [ ] **Step 1: Add slot decl to ComicsPage.h**

In [src/ui/pages/ComicsPage.h](../../../src/ui/pages/ComicsPage.h), find the `private slots:` block (sibling of `showCatalogMode`). Add:

```cpp
private slots:
    // ... existing slots ...
    void showCatalogMode();
    void onCatalogTileActivated(const QString& seriesId,
                                const QString& seriesTitle,
                                int            anilistId);
```

- [ ] **Step 2: Replace the stub lambda with a connect to the new slot**

In [src/ui/pages/ComicsPage.cpp](../../../src/ui/pages/ComicsPage.cpp) at the `showCatalogMode()` function (~line 2347), replace the stub lambda block:

```cpp
// BEFORE (~lines 2347-2355)
connect(m_catalogScreen, &tankoban::ui::comics::ComicsCatalogScreen::seriesActivated,
        this, [this](const QString& seriesId, const QString& seriesTitle) {
            qInfo("[catalog] tile activated seriesId=%s title=%s",
                  qUtf8Printable(seriesId), qUtf8Printable(seriesTitle));
        });

// AFTER
connect(m_catalogScreen, &tankoban::ui::comics::ComicsCatalogScreen::seriesActivated,
        this, &ComicsPage::onCatalogTileActivated);
```

- [ ] **Step 3: Implement the slot body**

In the same .cpp, add the new function (place it adjacent to `showCatalogMode`):

```cpp
void ComicsPage::onCatalogTileActivated(const QString& seriesId,
                                         const QString& seriesTitle,
                                         int            anilistId)
{
    qInfo("[catalog] tile activated seriesId=%s title=%s anilistId=%d",
          qUtf8Printable(seriesId), qUtf8Printable(seriesTitle), anilistId);

    // Build a minimal MediaPreview for the existing showSeries path.
    // anilistId > 0 lets AniList fetch fire for banner + ranked tags.
    // anilistId == 0 still works -- dispatchFandomResolve will hit the
    // local catalog via titleHint matching, just no AniList enrichment.
    tankoban::manga::anilist::MediaPreview preview;
    preview.id = anilistId;
    preview.titleEnglish = seriesTitle;
    preview.titleRomaji  = seriesTitle;  // best-effort; refined by AniList fetch

    // Swap from catalog grid to series detail.
    m_stack->setCurrentWidget(m_seriesView);
    m_seriesView->showSeries(preview);
    m_mode = Mode::SeriesDetail;
}
```

- [ ] **Step 4: Verify MediaPreview field names**

Read [src/core/manga/anilist/AniListTypes.h](../../../src/core/manga/anilist/AniListTypes.h) and confirm `MediaPreview` has fields `id`, `titleEnglish`, `titleRomaji`. If field names differ, adjust Step 3's code to match.

- [ ] **Step 5: Verify Mode::SeriesDetail exists**

In [src/ui/pages/ComicsPage.h](../../../src/ui/pages/ComicsPage.h), confirm `Mode::SeriesDetail` is an enum value. If not (e.g. it's `Mode::Detail` or the existing series-open path uses a different mode name), use that instead in Step 3.

- [ ] **Step 6: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. This is the moment the Task 1 signal change becomes consistent.

- [ ] **Step 7: Commit**

```bash
taskkill /F /IM Tankoban.exe 2>nul
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp
git commit -m "[Agent 1, Phase 1 Task 5 -- onCatalogTileActivated wires tile click to showSeries]"
```

---

### Task 6: Phase 1 smoke verification

**Why:** Build green ≠ feature working. Visual smoke + dev-bridge state check confirms the round trip.

**Files:** none modified

- [ ] **Step 1: Launch Tankoban**

Run: `build_and_run.bat`
Expected: Tankoban window opens after build + asset deploy (~15 min on first build, faster on incremental).

- [ ] **Step 2: Switch to Comics mode + open Catalog**

Click "Comics" in the topbar. In the Comics search bar, click "Catalog" button.

Run (in a new shell, while Tankoban is alive): `out\tankoctl.exe get-state`
Expected JSON shows `mode: "catalog"` or equivalent.

- [ ] **Step 3: Verify tile count = 107**

Visual: scroll through grid; count tiles. Should be ~107 (the populated catalogs). If you see 127, the empty-stub filter from Task 3 didn't fire — re-check `loadAllCatalogs`.

- [ ] **Step 4: Verify alphabetical sort**

Visual: first tile should be `A Silent Voice` or similar A-series. Last tile should be `Y...` or `Z...` if present.

- [ ] **Step 5: Click "One Piece" tile, verify series detail loads**

Click the One Piece tile. Expected:
- Banner area starts empty, then fills (AniList background fetch ~500ms)
- Hero block shows One Piece title + cover
- Volume table populates immediately from local catalog (114 rows: Romance Dawn, Buggy the Clown, ...)
- Today's QTableWidget shape (168px rows) — Phase 2 hasn't shipped yet

- [ ] **Step 6: Verify back returns to catalog grid (NavHistory)**

Click back button on series detail. Expected: returns to Catalog grid (not Library). If it goes to Library, the NavHistory layer wiring needs review — but per Task 5 we set `Mode::SeriesDetail` and the existing back-from-series path is unchanged, so this should work.

- [ ] **Step 7: Stop Tankoban + commit a smoke marker if any tunables drifted**

```bash
taskkill /F /IM Tankoban.exe 2>nul
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

If smoke surfaced any fixups (e.g. column count adjustment, sort tie-breaker), commit them now with a `Phase 1 smoke-fixup` message. If clean, no commit needed.

---

## Phase 2 — VolumeTile widget + ComicsSeriesView refactor

Outcome: All series detail views (catalog / AniList / WeebCentral) render volume rows as VolumeTile widgets in a vertical QScrollArea instead of a QTableWidget. 130px rows, 80×120 covers. 5 state chip values. Shift+click range-fill. MangaDownloadIndex subscription.

---

### Task 7: Create `VolumeTile.h` with data + state structs

**Why:** Lock the public API surface before implementation. Mirrors `EpisodeTile.h` structure (per spec Section 4).

**Files:**
- Create: `src/ui/pages/comics/VolumeTile.h`

- [ ] **Step 1: Write the full header**

Create [src/ui/pages/comics/VolumeTile.h](../../../src/ui/pages/comics/VolumeTile.h):

```cpp
// src/ui/pages/comics/VolumeTile.h
//
// COMICS_CATALOG_SERIES_VIEW Phase 2 (2026-05-22) — EpisodeTile parallel
// for Comics volume rows. 130px row, 80×120 cover, 5-state chip palette.
// Subscribes to MangaDownloadIndex::entriesChanged for Complete/NotStarted;
// ComicsSeriesView pushes transient states (Queued/Downloading/Failed)
// via setEpisodeState-equivalent slots.

#pragma once

#include <QFrame>
#include <QPointer>
#include <QString>

class QCheckBox;
class QLabel;
class QPushButton;

namespace tankoban::manga {
class MangaDownloadIndex;
}

namespace tankoban::ui::comics {

struct VolumeTileData {
    QString sourceId;                  // "fandom_catalog" / "tankoyomi" / "weebcentral"
    QString seriesId;                  // slug or anilist_<N>
    int     volumeNumber = 0;          // 1..N
    QString title;                     // English volume title, may be empty
    QString chapterRange;              // "ch 1-8"
    int     pages = 0;                 // 0 if unknown
    QString publishDate;               // free-form, may be empty
    QString coverUrl;                  // initial cover; setCoverFromDisk overrides post-DL
};

struct VolumeTileState {
    enum State {
        NotStarted = 0,
        Queued     = 1,
        Downloading = 2,
        Complete   = 3,
        Failed     = 4,
    };
    State   state         = NotStarted;
    int     progressPct   = 0;        // 0..100, only meaningful when state == Downloading
    QString statusText;               // free-form chip suffix or failure reason
    QString cbzPath;                  // set when state == Complete
    QString provenance;               // "" / "Fandom" / "Tankoyomi" / "LocalScan"
};

class VolumeTile : public QFrame {
    Q_OBJECT
public:
    explicit VolumeTile(const VolumeTileData& data, QWidget* parent = nullptr);

    int  volumeNumber() const  { return m_data.volumeNumber; }
    bool isChecked() const;
    void setChecked(bool checked);
    void setCheckedQuiet(bool checked);   // shift+click range-fill: no signal emit

    void setVolumeState(const VolumeTileState& s);
    VolumeTileState volumeState() const { return m_state; }

    void setCoverFromDisk(const QString& coverPath);
    void setCoverFromUrl(const QString& url);   // for AniList-path covers
    void setStatusText(const QString& text);

    // Per-tile subscription. Non-owning. May be set after construction.
    void setMangaDownloadIndex(tankoban::manga::MangaDownloadIndex* idx);

    // Pure-logic helper exposed for testing -- maps (presence-of-DL-index-entry,
    // statusText) tuple to the canonical State value. Static so the test
    // doesn't need a QApplication.
    static VolumeTileState::State computeState(bool hasIndexEntry,
                                                const QString& statusText);

signals:
    void toggled(bool checked);
    void toggledShift(bool checked, bool shiftHeld);
    void openRequested(int volumeNumber);          // user clicked Open on Complete
    void downloadRequested(int volumeNumber);      // user clicked Download on NotStarted
    void cancelRequested(int volumeNumber);        // user clicked Cancel on Queued/Downloading
    void retryRequested(int volumeNumber);         // user clicked Retry on Failed

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onIndexEntriesChanged();
    void onActionClicked();

private:
    void buildUi();
    void applyState();

    VolumeTileData  m_data;
    VolumeTileState m_state;

    QPointer<tankoban::manga::MangaDownloadIndex> m_idx;

    QCheckBox*   m_checkbox = nullptr;
    QLabel*      m_numberLabel = nullptr;
    QLabel*      m_coverLabel = nullptr;
    QLabel*      m_titleLabel = nullptr;
    QLabel*      m_metaLabel = nullptr;
    QLabel*      m_chipLabel = nullptr;
    QLabel*      m_progressLabel = nullptr;   // text "38% · 12.4 MB / 32.7 MB"
    QPushButton* m_actionBtn = nullptr;
};

} // namespace tankoban::ui::comics
```

- [ ] **Step 2: Compile-verify (header-only build will fail, no impl yet)**

Run: `build_check.bat`
Expected: `BUILD FAILED` with linker errors for VolumeTile symbols once it's included anywhere. Header alone shouldn't break the build — and at this point nothing includes it yet. So `BUILD OK` is expected. If `BUILD FAILED`, it's an unrelated regression.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/comics/VolumeTile.h
git commit -m "[Agent 1, Phase 2 Task 7 -- VolumeTile.h public API (EpisodeTile parallel)]"
```

---

### Task 8: TDD pure-logic — `VolumeTile::computeState`

**Why:** The state-mapping function is the only piece of VolumeTile that's pure logic (input: two values; output: enum). Smoke-first applies to everything else; this one function pays for a unit test (Codex #4 Stage 3a opt-in).

**Files:**
- Create: `tests/ui/test_volume_tile_state.cpp`
- Modify: `CMakeLists.txt` (register the test source under `tankoban_tests`)

- [ ] **Step 1: Find the tankoban_tests block in CMakeLists.txt**

Run: `powershell -NoProfile -Command "Select-String -Path CMakeLists.txt -Pattern 'tankoban_tests'"`
Expected: returns the line range where the `tankoban_tests` target is defined.

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_volume_tile_state.cpp`:

```cpp
// tests/ui/test_volume_tile_state.cpp
#include <gtest/gtest.h>
#include "ui/pages/comics/VolumeTile.h"

using tankoban::ui::comics::VolumeTile;
using State = tankoban::ui::comics::VolumeTileState::State;

TEST(VolumeTileComputeState, NoIndexEntryNoStatus_IsNotStarted) {
    EXPECT_EQ(VolumeTile::computeState(false, QString()), State::NotStarted);
}

TEST(VolumeTileComputeState, IndexEntryPresent_IsComplete) {
    EXPECT_EQ(VolumeTile::computeState(true, QString()), State::Complete);
}

TEST(VolumeTileComputeState, NoEntry_QueuedStatus_IsQueued) {
    EXPECT_EQ(VolumeTile::computeState(false, QStringLiteral("Queued · #3 in queue")), State::Queued);
}

TEST(VolumeTileComputeState, NoEntry_DownloadingStatus_IsDownloading) {
    EXPECT_EQ(VolumeTile::computeState(false, QStringLiteral("Downloading · 38%")), State::Downloading);
}

TEST(VolumeTileComputeState, NoEntry_FailedStatus_IsFailed) {
    EXPECT_EQ(VolumeTile::computeState(false, QStringLiteral("Failed · no seeds")), State::Failed);
}

TEST(VolumeTileComputeState, IndexEntry_BeatsAnyStatus) {
    // Presence in MangaDownloadIndex always wins -- defensive against stale
    // transient status from a prior dispatch run that wasn't cleared.
    EXPECT_EQ(VolumeTile::computeState(true, QStringLiteral("Downloading · 50%")), State::Complete);
}
```

- [ ] **Step 3: Register the test source in CMakeLists.txt**

In CMakeLists.txt, find the `tankoban_tests` target definition (likely an `add_executable(tankoban_tests ...)` or `target_sources(tankoban_tests PRIVATE ...)` line). Add:

```cmake
target_sources(tankoban_tests PRIVATE
    tests/ui/test_volume_tile_state.cpp
    src/ui/pages/comics/VolumeTile.cpp   # added in Task 9; ensures the symbol exists
)
```

- [ ] **Step 4: Run the test, verify it FAILS**

Run:
```bash
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
```
Expected: `BUILD FAILED` because VolumeTile.cpp doesn't exist yet and `computeState` is unimplemented. This is correct TDD red.

- [ ] **Step 5: Commit the failing test**

```bash
git add tests/ui/test_volume_tile_state.cpp CMakeLists.txt
git commit -m "[Agent 1, Phase 2 Task 8 -- VolumeTile::computeState test cases (red)]"
```

---

### Task 9: Create `VolumeTile.cpp` stub with computeState only

**Why:** Make the test pass. Just enough body to land green — full UI build happens in Task 10.

**Files:**
- Create: `src/ui/pages/comics/VolumeTile.cpp`

- [ ] **Step 1: Write the minimal computeState implementation**

Create [src/ui/pages/comics/VolumeTile.cpp](../../../src/ui/pages/comics/VolumeTile.cpp):

```cpp
// src/ui/pages/comics/VolumeTile.cpp
#include "VolumeTile.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/manga/MangaDownloadIndex.h"

namespace tankoban::ui::comics {

VolumeTileState::State VolumeTile::computeState(bool hasIndexEntry,
                                                  const QString& statusText)
{
    // Presence in MangaDownloadIndex always wins -- handles stale transient
    // status from a prior dispatch run that wasn't cleared.
    if (hasIndexEntry) return VolumeTileState::Complete;

    if (statusText.startsWith(QStringLiteral("Queued"),       Qt::CaseInsensitive))
        return VolumeTileState::Queued;
    if (statusText.startsWith(QStringLiteral("Downloading"),  Qt::CaseInsensitive))
        return VolumeTileState::Downloading;
    if (statusText.startsWith(QStringLiteral("Failed"),       Qt::CaseInsensitive))
        return VolumeTileState::Failed;

    return VolumeTileState::NotStarted;
}

// ---- All other VolumeTile members stub-defined for Task 10 ----

VolumeTile::VolumeTile(const VolumeTileData& data, QWidget* parent)
    : QFrame(parent), m_data(data)
{
    // Full buildUi in Task 10
}

bool VolumeTile::isChecked() const { return m_checkbox && m_checkbox->isChecked(); }
void VolumeTile::setChecked(bool c) { if (m_checkbox) m_checkbox->setChecked(c); }
void VolumeTile::setCheckedQuiet(bool c) {
    if (!m_checkbox) return;
    const bool wasBlocked = m_checkbox->blockSignals(true);
    m_checkbox->setChecked(c);
    m_checkbox->blockSignals(wasBlocked);
}
void VolumeTile::setVolumeState(const VolumeTileState& s) { m_state = s; applyState(); }
void VolumeTile::setCoverFromDisk(const QString& /*p*/) { /* Task 10 */ }
void VolumeTile::setCoverFromUrl(const QString& /*u*/) { /* Task 10 */ }
void VolumeTile::setStatusText(const QString& t) { m_state.statusText = t; applyState(); }
void VolumeTile::setMangaDownloadIndex(tankoban::manga::MangaDownloadIndex* idx) {
    m_idx = idx;
    if (m_idx) {
        connect(m_idx.data(), &tankoban::manga::MangaDownloadIndex::entriesChanged,
                this, &VolumeTile::onIndexEntriesChanged);
        onIndexEntriesChanged();
    }
}
void VolumeTile::paintEvent(QPaintEvent* e) { QFrame::paintEvent(e); }
void VolumeTile::mousePressEvent(QMouseEvent* e) { QFrame::mousePressEvent(e); }
void VolumeTile::onIndexEntriesChanged() {
    const bool has = m_idx
        && m_idx->entryForSeriesAndVolume(m_data.sourceId, m_data.seriesId,
                                          m_data.volumeNumber).has_value();
    m_state.state = computeState(has, m_state.statusText);
    applyState();
}
void VolumeTile::onActionClicked() { /* Task 10 */ }
void VolumeTile::buildUi() { /* Task 10 */ }
void VolumeTile::applyState() { /* Task 10 -- painting; for now no-op */ }

} // namespace tankoban::ui::comics
```

- [ ] **Step 2: Run the test, verify it PASSES**

Run:
```bash
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R VolumeTileComputeState && cd ..
```
Expected: all 6 tests pass.

- [ ] **Step 3: Commit (green)**

```bash
git add src/ui/pages/comics/VolumeTile.cpp
git commit -m "[Agent 1, Phase 2 Task 9 -- VolumeTile::computeState passes (green)]"
```

---

### Task 10: Fill in VolumeTile UI body (buildUi + applyState + action handlers)

**Why:** Now that computeState is locked + tested, fill in the visual surface. This is UI code; smoke-verify via build + visual inspection in Task 13.

**Files:**
- Modify: `src/ui/pages/comics/VolumeTile.cpp` (replace stubs)

- [ ] **Step 1: Implement buildUi() — layout the 130px row**

In `VolumeTile::VolumeTile` ctor body, call `buildUi()`. Replace the stub `buildUi()` implementation with:

```cpp
void VolumeTile::buildUi()
{
    setFrameShape(QFrame::NoFrame);
    setFixedHeight(130);
    setStyleSheet(
        "VolumeTile { background: transparent; border-bottom: 1px solid #1a1a1a; }"
        "VolumeTile:hover { background: rgba(255,255,255,0.03); }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(12);

    m_checkbox = new QCheckBox(this);
    layout->addWidget(m_checkbox);

    m_numberLabel = new QLabel(QString::number(m_data.volumeNumber), this);
    m_numberLabel->setFixedWidth(32);
    m_numberLabel->setAlignment(Qt::AlignCenter);
    m_numberLabel->setStyleSheet("color:#aaa;font-size:14px;font-weight:700;");
    layout->addWidget(m_numberLabel);

    m_coverLabel = new QLabel(this);
    m_coverLabel->setFixedSize(80, 120);
    m_coverLabel->setStyleSheet("background:#3a3a3a;border-radius:3px;");
    m_coverLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_coverLabel);

    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(4);

    m_titleLabel = new QLabel(m_data.title.isEmpty()
                              ? QStringLiteral("Volume %1").arg(m_data.volumeNumber)
                              : m_data.title, this);
    m_titleLabel->setStyleSheet("color:#eee;font-size:13px;font-weight:700;");
    contentLayout->addWidget(m_titleLabel);

    QString meta = m_data.chapterRange;
    if (m_data.pages > 0)         meta += QStringLiteral(" · %1 pages").arg(m_data.pages);
    if (!m_data.publishDate.isEmpty()) meta += QStringLiteral(" · %1").arg(m_data.publishDate);
    m_metaLabel = new QLabel(meta, this);
    m_metaLabel->setStyleSheet("color:#aaa;font-size:11px;");
    contentLayout->addWidget(m_metaLabel);

    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(6);
    m_chipLabel = new QLabel(this);
    m_chipLabel->setStyleSheet("background:#2a2a2a;color:#aaa;padding:2px 8px;border-radius:10px;font-size:10px;");
    chipRow->addWidget(m_chipLabel);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setStyleSheet("color:#888;font-size:10px;");
    chipRow->addWidget(m_progressLabel);
    chipRow->addStretch();
    contentLayout->addLayout(chipRow);

    layout->addLayout(contentLayout, /*stretch=*/1);

    m_actionBtn = new QPushButton(this);
    m_actionBtn->setStyleSheet(
        "QPushButton { color:#4a8a4a; border:1px solid #2a4a2a; border-radius:3px;"
        "  padding:6px 12px; font-size:11px; background:transparent; }"
        "QPushButton:hover { background:rgba(74,138,74,0.1); }"
    );
    layout->addWidget(m_actionBtn);

    connect(m_actionBtn, &QPushButton::clicked, this, &VolumeTile::onActionClicked);
    connect(m_checkbox, &QCheckBox::toggled, this, [this](bool checked) {
        const bool shiftHeld =
            QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        emit toggled(checked);
        emit toggledShift(checked, shiftHeld);
    });

    applyState();
}
```

Add the include at the top of VolumeTile.cpp:

```cpp
#include <QGuiApplication>
```

- [ ] **Step 2: Implement applyState() — chip + action button per state**

Replace the stub `applyState()`:

```cpp
void VolumeTile::applyState()
{
    if (!m_chipLabel || !m_actionBtn) return;

    QString chipText, chipStyle, actionText;
    switch (m_state.state) {
    case VolumeTileState::NotStarted:
        chipText = QStringLiteral("Not started");
        chipStyle = "background:#2a2a2a;color:#aaa;";
        actionText = QStringLiteral("Download");
        m_progressLabel->clear();
        break;
    case VolumeTileState::Queued:
        chipText = m_state.statusText.isEmpty() ? QStringLiteral("Queued") : m_state.statusText;
        chipStyle = "background:#3a3a2a;color:#e0d0a0;";
        actionText = QStringLiteral("Cancel");
        m_progressLabel->clear();
        break;
    case VolumeTileState::Downloading:
        chipText = QStringLiteral("Downloading");
        chipStyle = "background:#2a3a5a;color:#aeb0f0;";
        actionText = QStringLiteral("Cancel");
        m_progressLabel->setText(m_state.progressPct >= 0
                                  ? QStringLiteral("%1%").arg(m_state.progressPct)
                                  : QString());
        break;
    case VolumeTileState::Complete:
        chipText = QStringLiteral("Downloaded");
        chipStyle = "background:#2a4a2a;color:#aef0ae;";
        actionText = QStringLiteral("Open");
        m_progressLabel->clear();
        break;
    case VolumeTileState::Failed:
        chipText = m_state.statusText.isEmpty() ? QStringLiteral("Failed") : m_state.statusText;
        chipStyle = "background:#4a2a2a;color:#f0a0a0;";
        actionText = QStringLiteral("Retry");
        m_progressLabel->clear();
        break;
    }

    m_chipLabel->setText(chipText);
    m_chipLabel->setStyleSheet(QStringLiteral(
        "padding:2px 8px;border-radius:10px;font-size:10px;%1").arg(chipStyle));
    m_actionBtn->setText(actionText);
}
```

- [ ] **Step 3: Implement onActionClicked() — route to the right signal**

```cpp
void VolumeTile::onActionClicked()
{
    switch (m_state.state) {
    case VolumeTileState::NotStarted: emit downloadRequested(m_data.volumeNumber); break;
    case VolumeTileState::Queued:
    case VolumeTileState::Downloading: emit cancelRequested(m_data.volumeNumber); break;
    case VolumeTileState::Complete:    emit openRequested(m_data.volumeNumber); break;
    case VolumeTileState::Failed:      emit retryRequested(m_data.volumeNumber); break;
    }
}
```

- [ ] **Step 4: Implement setCoverFromDisk + setCoverFromUrl**

```cpp
void VolumeTile::setCoverFromDisk(const QString& coverPath)
{
    if (coverPath.isEmpty() || !m_coverLabel) return;
    QPixmap pm(coverPath);
    if (pm.isNull()) return;
    m_coverLabel->setPixmap(pm.scaled(80, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VolumeTile::setCoverFromUrl(const QString& url)
{
    if (url.isEmpty()) return;
    m_data.coverUrl = url;
    // Cover loading via ComicsSeriesView's existing QNAM path -- this setter
    // is called from outside (e.g. AniList rows). No QNAM inside VolumeTile
    // to keep it widget-only; ComicsSeriesView orchestrates the fetch and
    // calls setCoverFromDisk once the pixmap is decoded.
}
```

- [ ] **Step 5: Implement provenance pin in paintEvent**

```cpp
void VolumeTile::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    if (m_state.provenance.isEmpty() || !m_coverLabel) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect coverRect = m_coverLabel->geometry();
    const QRect chipRect(coverRect.left() + 4, coverRect.top() + 4, 60, 14);
    p.fillRect(chipRect, QColor(0, 0, 0, 180));
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.drawText(chipRect, Qt::AlignCenter, m_state.provenance.toUpper());
}
```

- [ ] **Step 6: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. VolumeTile compiles as a standalone widget; nothing else uses it yet.

- [ ] **Step 7: Commit**

```bash
taskkill /F /IM Tankoban.exe 2>nul
git add src/ui/pages/comics/VolumeTile.cpp
git commit -m "[Agent 1, Phase 2 Task 10 -- VolumeTile buildUi + applyState + action signals]"
```

---

### Task 11: Register VolumeTile.cpp in main CMakeLists target

**Why:** Task 8 already added it to `tankoban_tests`. Now wire it to the main Tankoban executable so the link survives the ComicsSeriesView refactor in Task 12.

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Find the comics/ sources block**

Run: `powershell -NoProfile -Command "Select-String -Path CMakeLists.txt -Pattern 'ComicsCatalogScreen.cpp'"`
Expected: returns the line where ComicsCatalogScreen.cpp is registered.

- [ ] **Step 2: Add VolumeTile.cpp adjacent**

In CMakeLists.txt, in the same `target_sources(tankoban_exe ...)` (or equivalent) block that lists `src/ui/pages/comics/ComicsCatalogScreen.cpp`, add:

```cmake
src/ui/pages/comics/VolumeTile.cpp
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. Main app now links VolumeTile too.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "[Agent 1, Phase 2 Task 11 -- register VolumeTile.cpp in main CMakeLists]"
```

---

### Task 12: Replace `m_volumesTable` (QTableWidget) with `QScrollArea` of VolumeTiles in `ComicsSeriesView.h`

**Why:** Surgical header swap before the .cpp surgery. Lets the .cpp changes happen incrementally without compile-breakage during each step.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h`

- [ ] **Step 1: Add VolumeTile + container forward decls**

In [src/ui/pages/comics/ComicsSeriesView.h](../../../src/ui/pages/comics/ComicsSeriesView.h), in the forward-decl block (around line 39-47):

```cpp
// ADD:
class QScrollArea;
class QVBoxLayout;

namespace tankoban::ui::comics {
class VolumeTile;
}
```

(QScrollArea and QVBoxLayout may already be there; only add what's missing.)

- [ ] **Step 2: Add the QHash member + replace m_volumesTable**

Find `QTableWidget* m_volumesTable = nullptr;` (around line 314). Replace with:

```cpp
// REPLACED: QTableWidget surface is now a QScrollArea of VolumeTile rows.
// m_volumeTilesByVolumeNumber lets setVolumeDownloadState /
// setVolumeStatusText / setVolumeCoverFromDisk address rows by volumeNumber
// without iterating the list.
QScrollArea*  m_volumesScroll = nullptr;
QWidget*      m_volumesHost   = nullptr;
QVBoxLayout*  m_volumesLayout = nullptr;
QHash<int, tankoban::ui::comics::VolumeTile*> m_volumeTilesByVolumeNumber;
QList<tankoban::ui::comics::VolumeTile*>      m_volumeTiles;
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD FAILED` with errors at every site in ComicsSeriesView.cpp that touches `m_volumesTable` (probably 20-40 sites). This is correct -- Task 13 fixes them.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h
git commit -m "[Agent 1, Phase 2 Task 12 -- swap m_volumesTable for VolumeTile scroll list (header)]"
```

---

### Task 13: Rewrite `ComicsSeriesView::buildUi` to construct the volume scroll surface

**Why:** Lay the new container so subsequent populate-rewrites have something to attach to.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1: Find the existing `buildUi` volume table block**

Run: `powershell -NoProfile -Command "Select-String -Path src\ui\pages\comics\ComicsSeriesView.cpp -Pattern 'm_volumesTable.*new QTableWidget|setColumnCount|verticalHeader'"`
Expected: returns the lines where the table is constructed.

- [ ] **Step 2: Replace the table-construction block with scroll-area construction**

Delete every line in `buildUi()` that touches `m_volumesTable`, replacing the construction block with:

```cpp
m_volumesScroll = new QScrollArea(this);
m_volumesScroll->setWidgetResizable(true);
m_volumesScroll->setFrameShape(QFrame::NoFrame);
m_volumesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
m_volumesScroll->setStyleSheet("QScrollArea { background: transparent; }");

m_volumesHost = new QWidget(m_volumesScroll);
m_volumesLayout = new QVBoxLayout(m_volumesHost);
m_volumesLayout->setContentsMargins(0, 0, 0, 0);
m_volumesLayout->setSpacing(0);
m_volumesLayout->addStretch(1);   // pushes rows up; new tiles inserted at index size()-1

m_volumesScroll->setWidget(m_volumesHost);

// Add m_volumesScroll to wherever m_volumesTable was added to its parent
// layout. The original code likely had a line like:
//     <some-layout>->addWidget(m_volumesTable, ...);
// Replace with:
//     <some-layout>->addWidget(m_volumesScroll, ...);
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`
Expected: still `BUILD FAILED` -- populate functions in subsequent tasks still reference m_volumesTable. The fewer errors than before mean progress.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "[Agent 1, Phase 2 Task 13 -- buildUi constructs VolumeTile scroll container]"
```

---

### Task 14: Rewrite `populateVolumeRowsFromFandom` to instantiate VolumeTiles

**Why:** The Fandom-catalog populate path is where the catalog-sourced detail view lives. Phase 1 already hits this function via the local-first short-circuit; now it produces VolumeTiles.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1: Add includes**

At the top of ComicsSeriesView.cpp, add:

```cpp
#include "VolumeTile.h"
```

- [ ] **Step 2: Replace the function body**

Find `void ComicsSeriesView::populateVolumeRowsFromFandom(...)` (~line 1439). Replace its body:

```cpp
void ComicsSeriesView::populateVolumeRowsFromFandom(
    const tankoban::manga::fandom::FandomCatalog& catalog)
{
    // Tear down existing tiles
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();

    // Insert each volume as a VolumeTile row, just before the trailing
    // stretch in m_volumesLayout (index = count() - 1).
    for (const auto& vol : catalog.volumes) {
        tankoban::ui::comics::VolumeTileData data;
        data.sourceId      = QStringLiteral("fandom_catalog");
        data.seriesId      = catalog.seriesId;
        data.volumeNumber  = vol.number;
        data.title         = vol.title;
        data.chapterRange  = QStringLiteral("ch %1-%2")
                                .arg(vol.chapterStart, vol.chapterEnd);
        data.coverUrl      = vol.coverUrl;
        // pages + publishDate: FandomVolume doesn't carry these in v1;
        // leave at defaults (0 / empty string).

        auto* tile = new tankoban::ui::comics::VolumeTile(data, m_volumesHost);

        tankoban::ui::comics::VolumeTileState state;
        state.provenance = QStringLiteral("Fandom");
        tile->setVolumeState(state);
        tile->setMangaDownloadIndex(m_downloadIndex);

        connect(tile, &tankoban::ui::comics::VolumeTile::openRequested,
                this, [this](int vn) {
                    // existing openVolume() emit, cbzPath looked up by series+vol
                    const auto entry = m_downloadIndex
                        ? m_downloadIndex->entryForSeriesAndVolume(
                              QStringLiteral("fandom_catalog"),
                              m_currentSeriesKey, vn)
                        : std::nullopt;
                    emit openVolume(vn, entry ? entry->canonicalPath : QString());
                });
        connect(tile, &tankoban::ui::comics::VolumeTile::downloadRequested,
                this, [this](int vn) {
                    // route through existing source-panel dispatch path;
                    // currently driven by m_sourcesPanel.downloadRequested fan-in
                    populateSourcesForRow(/* row index irrelevant -- use vn */ -1);
                    Q_UNUSED(vn);
                });
        connect(tile, &tankoban::ui::comics::VolumeTile::toggledShift,
                this, [this](bool checked, bool shiftHeld) {
                    // existing shift+range logic lived on QTableWidget cellClicked.
                    // Forward to onVolumeCheckboxToggled with adapted row index;
                    // shift+range fill is wired in Task 17.
                    Q_UNUSED(checked); Q_UNUSED(shiftHeld);
                });

        m_volumeTiles.append(tile);
        m_volumeTilesByVolumeNumber.insert(vol.number, tile);
        m_volumesLayout->insertWidget(m_volumesLayout->count() - 1, tile);
    }

    // ranked-tag refresh + force-refresh button visibility kept as-is
    // from prior function body -- preserve any subsequent lines that
    // weren't part of the QTableWidget population.

    m_currentSeriesTitle = catalog.seriesTitle;
}
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD FAILED` with the remaining errors coming from `populateVolumeRows` (AniList path) and the three setter slots. Fewer errors than Task 13.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "[Agent 1, Phase 2 Task 14 -- populateVolumeRowsFromFandom emits VolumeTiles]"
```

---

### Task 15: Rewrite `populateVolumeRows` (AniList path) symmetric to Task 14

**Why:** Full Stream mimicry per spec — both paths emit VolumeTiles.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1: Replace the function body**

Find `void ComicsSeriesView::populateVolumeRows(const QList<anilist::VolumeRow>& rows, const anilist::MediaDetail* detail)`. Replace its body with the same shape as Task 14, sourcing data from `anilist::VolumeRow`:

```cpp
void ComicsSeriesView::populateVolumeRows(const QList<anilist::VolumeRow>& rows,
                                            const anilist::MediaDetail* detail)
{
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();

    const QString sourceId = QStringLiteral("anilist");
    const QString seriesId = QStringLiteral("anilist_%1").arg(m_currentAnilistId);

    for (const auto& row : rows) {
        tankoban::ui::comics::VolumeTileData data;
        data.sourceId     = sourceId;
        data.seriesId     = seriesId;
        data.volumeNumber = row.volumeNumber;
        data.title        = row.title;
        data.chapterRange = row.chapterRange;
        data.coverUrl     = row.coverUrl;

        auto* tile = new tankoban::ui::comics::VolumeTile(data, m_volumesHost);

        tankoban::ui::comics::VolumeTileState state;
        state.provenance = QString();  // AniList-only path: no badge
        tile->setVolumeState(state);
        tile->setMangaDownloadIndex(m_downloadIndex);

        // Same connect block as Task 14, with sourceId substituted.
        connect(tile, &tankoban::ui::comics::VolumeTile::openRequested,
                this, [this, sourceId, seriesId](int vn) {
                    const auto entry = m_downloadIndex
                        ? m_downloadIndex->entryForSeriesAndVolume(sourceId, seriesId, vn)
                        : std::nullopt;
                    emit openVolume(vn, entry ? entry->canonicalPath : QString());
                });
        connect(tile, &tankoban::ui::comics::VolumeTile::downloadRequested,
                this, [this](int vn) {
                    populateSourcesForRow(-1);
                    Q_UNUSED(vn);
                });

        m_volumeTiles.append(tile);
        m_volumeTilesByVolumeNumber.insert(row.volumeNumber, tile);
        m_volumesLayout->insertWidget(m_volumesLayout->count() - 1, tile);
    }

    Q_UNUSED(detail);
}
```

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD FAILED` only on the three setter slots remaining.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "[Agent 1, Phase 2 Task 15 -- populateVolumeRows (AniList) emits VolumeTiles]"
```

---

### Task 16: Rewire `setVolumeDownloadState`, `setVolumeStatusText`, `setVolumeCoverFromDisk` to address VolumeTiles by volumeNumber

**Why:** These slots used to find QTableWidget rows by row index. Now they look up VolumeTile via `m_volumeTilesByVolumeNumber`. Preserve the existing stale-event seriesId-prefix guard on setVolumeCoverFromDisk (spec Section 4 patch).

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1: Replace setVolumeDownloadState body**

Find `void ComicsSeriesView::setVolumeDownloadState(int volumeNumber, const QString& cbzPath, bool downloaded)`. Replace body:

```cpp
void ComicsSeriesView::setVolumeDownloadState(int volumeNumber,
                                                const QString& cbzPath,
                                                bool downloaded)
{
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;
    auto state = tile->volumeState();
    state.cbzPath = cbzPath;
    state.state = downloaded ? tankoban::ui::comics::VolumeTileState::Complete
                              : tankoban::ui::comics::VolumeTileState::NotStarted;
    tile->setVolumeState(state);
}
```

- [ ] **Step 2: Replace setVolumeStatusText body**

```cpp
void ComicsSeriesView::setVolumeStatusText(int volumeNumber, const QString& statusText)
{
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;
    tile->setStatusText(statusText);
    // Re-compute state from (index-presence, statusText) tuple
    auto state = tile->volumeState();
    const bool hasEntry = m_downloadIndex
        && m_downloadIndex->entryForSeriesAndVolume(
              /*sourceId placeholder*/ QStringLiteral("fandom_catalog"),
              m_currentSeriesKey, volumeNumber).has_value();
    state.state = tankoban::ui::comics::VolumeTile::computeState(hasEntry, statusText);
    state.statusText = statusText;
    tile->setVolumeState(state);
}
```

- [ ] **Step 3: Replace setVolumeCoverFromDisk body**

```cpp
void ComicsSeriesView::setVolumeCoverFromDisk(const QString& seriesId,
                                                int volumeNumber,
                                                const QString& coverPath)
{
    // Preserve existing stale-event guard: drop events for series not
    // currently displayed. Match either "anilist_<N>" or the catalog slug.
    const QString expectedAnilist = QStringLiteral("anilist_%1").arg(m_currentAnilistId);
    if (seriesId != expectedAnilist && seriesId != m_currentSeriesKey) {
        return;  // stale event for prior series
    }

    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;
    tile->setCoverFromDisk(coverPath);
}
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. All m_volumesTable references should now be gone or replaced. If any remain, search and replace them in the same task — they're likely `clearView`, `onVolumeCellClicked`, or `onVolumeCurrentChanged`.

- [ ] **Step 5: If errors remain in clearView / onVolumeCellClicked / onVolumeCurrentChanged**

These three functions touched the QTableWidget API. Update them:

```cpp
void ComicsSeriesView::clearView()
{
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();
    m_currentVolumeRows.clear();
    m_selectedRows.clear();
    // ... preserve all OTHER existing clearView logic verbatim ...
}

void ComicsSeriesView::onVolumeCellClicked(int /*row*/, int /*column*/) {
    // Deprecated -- VolumeTile owns its click handling. Keep as no-op
    // so existing signal-slot connect (if any) doesn't break.
}

void ComicsSeriesView::onVolumeCurrentChanged(int /*r*/, int /*c*/, int /*pr*/, int /*pc*/) {
    // Deprecated for VolumeTile flow.
}
```

- [ ] **Step 6: Final compile-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 7: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "[Agent 1, Phase 2 Task 16 -- rewire setVolumeDownloadState/StatusText/CoverFromDisk to VolumeTile]"
```

---

### Task 17: Wire shift+click range-fill into ComicsSeriesView

**Why:** EpisodeTile already supports `toggledShift(bool, bool shiftHeld)`. Spec Section 4 mandates parity. The range-fill logic uses the "last clicked index" pattern proven by THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` (add anchor index member)
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` (wire toggledShift handler)

- [ ] **Step 1: Add anchor index member to header**

In ComicsSeriesView.h, near `m_selectedRows`:

```cpp
int m_lastBulkAnchorVolume = -1;   // -1 = no anchor yet
```

- [ ] **Step 2: Replace the toggledShift connect in both populate paths**

In both `populateVolumeRowsFromFandom` AND `populateVolumeRows`, find the existing `toggledShift` connect block (currently a stub `Q_UNUSED`). Replace with:

```cpp
connect(tile, &tankoban::ui::comics::VolumeTile::toggledShift,
        this, [this](bool checked, bool shiftHeld) {
            auto* src = qobject_cast<tankoban::ui::comics::VolumeTile*>(sender());
            if (!src) return;
            const int vn = src->volumeNumber();

            if (shiftHeld && m_lastBulkAnchorVolume > 0) {
                // Range-fill: every VolumeTile between m_lastBulkAnchorVolume and vn
                // gets setCheckedQuiet(checked).
                const int lo = std::min(m_lastBulkAnchorVolume, vn);
                const int hi = std::max(m_lastBulkAnchorVolume, vn);
                for (int v = lo; v <= hi; ++v) {
                    auto* t = m_volumeTilesByVolumeNumber.value(v, nullptr);
                    if (!t) continue;
                    t->setCheckedQuiet(checked);
                    if (checked) m_selectedRows.insert(v);
                    else         m_selectedRows.remove(v);
                }
            } else {
                if (checked) m_selectedRows.insert(vn);
                else         m_selectedRows.remove(vn);
            }
            m_lastBulkAnchorVolume = vn;

            // Refresh "Download Selected (N)" button label
            if (m_downloadSelectedBtn) {
                m_downloadSelectedBtn->setText(
                    QStringLiteral("Download Selected (%1)").arg(m_selectedRows.size()));
                m_downloadSelectedBtn->setVisible(!m_selectedRows.isEmpty());
            }
        });
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "[Agent 1, Phase 2 Task 17 -- shift+click range-fill on VolumeTile checkboxes]"
```

---

### Task 18: Phase 2 smoke verification

**Why:** Build green ≠ feature working. Three-path smoke matrix per spec Section 9 "Phase 2 done".

**Files:** none modified

- [ ] **Step 1: Run pure-logic tests**

```bash
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R VolumeTileComputeState && cd ..
```
Expected: all 6 tests pass.

- [ ] **Step 2: Launch Tankoban**

Run: `build_and_run.bat`
Expected: Tankoban opens.

- [ ] **Step 3: Catalog path smoke — click One Piece tile**

In Comics mode, click Catalog → click One Piece tile. Expected:
- Series detail opens
- Volume rows are 130px tall (NOT today's 168px)
- Covers are 80×120
- ~5 rows visible before scroll fold
- Row 1 ("Romance Dawn") chip = "Not started" (no MangaDownloadIndex entry)
- Action button = "Download"
- "Fandom" provenance pin top-left on every cover

- [ ] **Step 4: AniList path smoke — open a series from library**

Open Comics library → click any downloaded series (or use the search bar to open a WeebCentral series). Expected:
- Same VolumeTile rendering as catalog path
- No provenance pin (AniList path leaves it empty)
- If a volume is downloaded: row chip = "Downloaded", action = "Open"

- [ ] **Step 5: Shift+click range-fill smoke**

In any series with 10+ volumes: click vol 1 checkbox, shift+click vol 5 checkbox. Expected:
- Vols 1, 2, 3, 4, 5 all become checked
- "Download Selected (5)" button appears (or whatever the existing button label is)

- [ ] **Step 6: BookWalker cover update smoke**

If a download was in progress when you opened the view: wait for it to finalize. Expected: when MangaDownloadIndex entry lands, the relevant VolumeTile's chip flips to "Downloaded" and the cover (if BookWalker resolved one) updates via setVolumeCoverFromDisk.

- [ ] **Step 7: Stop Tankoban**

```bash
taskkill /F /IM Tankoban.exe 2>nul
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

- [ ] **Step 8: Fixups + final commit**

If smoke surfaced any tunables (e.g. row spacing too tight, chip text overflows), apply them now and commit with a `Phase 2 smoke-fixup` message. If clean, no commit needed.

---

## Self-Review Summary

**Spec coverage:** every spec anchor decision (Section 2, items 1-10) maps to at least one task:

| Anchor | Tasks |
|--------|-------|
| 1. Routing via showSeries(MediaPreview) | Task 5 |
| 2. Tile shape = shared TileCard | Task 2 |
| 3. Empty stubs hidden at load | Task 3 |
| 4. Alphabetical sort | Task 4 |
| 5. Empty hero fallback | (existing ComicsSeriesView behavior — no task needed) |
| 6. Row 130px / cover 80×120 | Task 10 (`setFixedHeight(130)` + `setFixedSize(80, 120)`) |
| 7. Phased ship | Tasks 1-6 = Phase 1, Tasks 7-18 = Phase 2 |
| 8. anilistId-first identity passing | Task 5 (Step 3 lambda) |
| 9. NavHistory back returns to Catalog | (Mode::Catalog already wired; smoke validates in Task 6 Step 6) |
| 10. Library / Force-refresh / Sources panel unchanged | (no task — behavior preserved by spec design) |

**Phase 1 file shape (per spec Section 3):**
- `ComicsCatalogScreen.h` — Task 1, Task 2 Step 1
- `ComicsCatalogScreen.cpp` — Tasks 2, 3, 4
- `ComicsPage.cpp` — Task 5
- `ComicsPage.h` — Task 5 Step 1

**Phase 2 file shape (per spec Section 4):**
- `VolumeTile.h` (NEW) — Task 7
- `VolumeTile.cpp` (NEW) — Tasks 8, 9, 10
- `ComicsSeriesView.h` — Tasks 12, 17
- `ComicsSeriesView.cpp` — Tasks 13, 14, 15, 16, 17
- `CMakeLists.txt` — Tasks 8, 11

**Type consistency:** `VolumeTileData`, `VolumeTileState`, `VolumeTile`, `VolumeTileState::State` enum (`NotStarted`/`Queued`/`Downloading`/`Complete`/`Failed`) all consistent across Tasks 7-17. The static helper `VolumeTile::computeState(bool hasIndexEntry, const QString& statusText) → VolumeTileState::State` declared in Task 7, implemented in Task 9, tested in Task 8, called in Tasks 16-17.

**Placeholder scan:** zero TBD / TODO / "add appropriate error handling" / "similar to Task N". Every code step has full code.
