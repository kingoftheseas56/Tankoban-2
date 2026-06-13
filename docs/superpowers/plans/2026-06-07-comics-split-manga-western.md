# Comics Split (Manga + Western) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> This is **Arc 1** of the six-mode restructure (spec: `docs/superpowers/specs/2026-06-07-six-mode-restructure-design.md`). Arc 2 (Anime/TV/Movies) is a separate plan.

**Goal:** Split the single "Comics" mode into two top-level modes — **Manga** (Asian: manga/manhwa/manhua/webtoons) and **Comics** (Western) — each with its own page, library, Continue strip, and search, over the existing (already-separate) comics brains.

**Architecture:** The manga and Western brains are *already* separate classes (manga: AniList/MangaUpdates/MangaFire/Nyaa/Premium + `ComicsTankoyomiLibrary` + `MangaDownloader`; Western: `ReadComicsScraper`/`ReadAllComicsScraper` + `WesternLibrary` + `WesternVolumeDownloader`). The work is **UI-layer surgery**: the manga-centric `ComicsPage` becomes `MangaPage` (Western bits removed), and the extracted Western half becomes a new `WesternComicsPage`. Then wire both as top-level modes. The comic *reader* is unchanged.

**Tech Stack:** C++17, Qt 6 (Widgets), CMake + ninja, GoogleTest. Build: `build_check.bat` (compile-verify) / `build_and_run.bat` (Hemanth smoke). Dev-control bridge (`tankoctl`) for headless state checks.

**Migration:** No data loss — `ComicsTankoyomiLibrary` (manga) maps to Manga, `WesternLibrary` maps to Comics; they are already separate stores. `MangaDownloadIndex` is partitioned by `sourceId` prefix.

---

## File Structure

**Modify:**
- `src/ui/MainWindow.cpp` — add `PAGE_MANGA` constant; add Manga to `navDefs`; instantiate `WesternComicsPage` + rename comics slot to manga in `buildPageStack()`; add nav-layer wiring + PerModeNav root seed for both; Ctrl+N keybind.
- `src/ui/pages/ComicsPage.{h,cpp}` → **rename to** `MangaPage.{h,cpp}` (class `ComicsPage` → `MangaPage`, objectName `"comics"` → `"manga"`); delete the Western members/flows (they move to the new page).
- `src/core/manga/MangaDownloadIndex.{h,cpp}` — add `originForSource(sourceId)` helper (`anilist|mangaupdates|mangafire|weebcentral|tankoyomi_premium → manga`; `getcomics|readcomics|readallcomics → western`) + a filtered accessor `entriesForOrigin(origin)`.
- `CMakeLists.txt` (root, the `add_executable`/sources list) — register the new + renamed files.

**Create:**
- `src/ui/pages/WesternComicsPage.{h,cpp}` — the Western half extracted from ComicsPage (ReadComics/ReadAllComics catalog + search + `WesternLibrary` grid + Continue strip + `WesternVolumeDownloader` flows + the Western `ComicsSeriesView` usage).
- `tests/manga/test_manga_download_index_origin.cpp` — unit test for the origin partition.

**Pre-task read (executor):** before Phase 2/3, read `src/ui/pages/ComicsPage.cpp` in full to see which members/slots belong to manga vs Western — the forward-decls in `ComicsPage.h:25-62` are the inventory (manga classes vs `ReadComicsScraper`/`ReadAllComicsScraper`/`WesternVolumeDownloader`/`WesternLibrary`).

---

## Phase 1 — MangaDownloadIndex origin partition (pure logic, TDD)

Do this first: it's pure logic, testable, and unblocks per-mode download views.

### Task 1: Origin classifier on MangaDownloadIndex

**Files:**
- Modify: `src/core/manga/MangaDownloadIndex.h`, `src/core/manga/MangaDownloadIndex.cpp`
- Test: `tests/manga/test_manga_download_index_origin.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "core/manga/MangaDownloadIndex.h"

TEST(MangaDownloadIndexOrigin, ClassifiesAsianSourcesAsManga) {
    EXPECT_EQ(MangaDownloadIndex::originForSource("anilist"),        "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("mangafire_catalog"), "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("weebcentral"),    "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("tankoyomi_premium"), "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("mangaupdates"),   "manga");
}

TEST(MangaDownloadIndexOrigin, ClassifiesWesternSourcesAsWestern) {
    EXPECT_EQ(MangaDownloadIndex::originForSource("getcomics"),      "western");
    EXPECT_EQ(MangaDownloadIndex::originForSource("readcomics"),     "western");
    EXPECT_EQ(MangaDownloadIndex::originForSource("readallcomics"),  "western");
}

TEST(MangaDownloadIndexOrigin, UnknownDefaultsToManga) {
    // Conservative: unknown sources stay in Manga (the historical home) so
    // nothing silently vanishes from both views.
    EXPECT_EQ(MangaDownloadIndex::originForSource("mystery_source"), "manga");
}
```

- [ ] **Step 2: Run it, verify it fails** — Run: `ctest -R MangaDownloadIndexOrigin -V` · Expected: FAIL (`originForSource` undeclared). (Tests build via `-DTANKOBAN_BUILD_TESTS=ON`.)

- [ ] **Step 3: Implement the classifier**

In `MangaDownloadIndex.h` (public, static):
```cpp
// Six-mode restructure (2026-06-07): classify a download's sourceId into the
// owning comics mode. Western = getcomics/readcomics/readallcomics; everything
// else (anilist/mangaupdates/mangafire/weebcentral/tankoyomi_premium/unknown)
// is Asian "manga". Prefix-tolerant (matches "getcomics", "getcomics_v2", ...).
static QString originForSource(const QString& sourceId);
```
In `MangaDownloadIndex.cpp`:
```cpp
QString MangaDownloadIndex::originForSource(const QString& sourceId) {
    const QString s = sourceId.toLower();
    if (s.startsWith("getcomics") || s.startsWith("readcomics")
        || s.startsWith("readallcomics")) {
        return QStringLiteral("western");
    }
    return QStringLiteral("manga");
}
```

- [ ] **Step 4: Run it, verify it passes** — Run: `ctest -R MangaDownloadIndexOrigin -V` · Expected: PASS (3/3).

- [ ] **Step 5: Commit** — `git add src/core/manga/MangaDownloadIndex.* tests/manga/test_manga_download_index_origin.cpp CMakeLists.txt && git commit -m "feat(comics-split): MangaDownloadIndex origin classifier (manga vs western)"`

### Task 2: Origin-filtered accessor

**Files:** Modify `src/core/manga/MangaDownloadIndex.{h,cpp}`; extend the same test file.

- [ ] **Step 1:** Add a test that builds an index with mixed entries (one `anilist:` seriesId, one `getcomics:` seriesId) and asserts `entriesForOrigin("western")` returns only the `getcomics` one and `entriesForOrigin("manga")` only the `anilist` one. (Use the existing `MangaDownloadIndex` add/insert API — read it first to match signatures.)
- [ ] **Step 2:** Run, verify FAIL.
- [ ] **Step 3:** Implement `QList<...> entriesForOrigin(const QString& origin) const` that iterates existing entries and keeps those where `originForSource(entry.sourceId) == origin`. Match the existing entry struct + accessor names exactly (read the header first).
- [ ] **Step 4:** Run, verify PASS.
- [ ] **Step 5:** Commit — `git commit -m "feat(comics-split): origin-filtered download accessor"`

---

## Phase 2 — Rename ComicsPage → MangaPage (manga-only)

The existing page is manga-centric; rename it and remove the Western bits (which move to Phase 3). Keep the manga flows untouched.

### Task 3: Rename the class + files, retarget objectName

**Files:** rename `src/ui/pages/ComicsPage.{h,cpp}` → `MangaPage.{h,cpp}`; modify `CMakeLists.txt`; update `#include "pages/ComicsPage.h"` references.

- [ ] **Step 1:** `git mv src/ui/pages/ComicsPage.h src/ui/pages/MangaPage.h && git mv src/ui/pages/ComicsPage.cpp src/ui/pages/MangaPage.cpp`
- [ ] **Step 2:** In both files: class `ComicsPage` → `MangaPage`, ctor/dtor, `#include "MangaPage.h"`, include guard/`#pragma once` stays. Set the page objectName to `"manga"` (grep the .cpp for `setObjectName` / where `"comics"` identity is assigned).
- [ ] **Step 3:** Update the CMake sources list: `src/ui/pages/ComicsPage.cpp` → `src/ui/pages/MangaPage.cpp`.
- [ ] **Step 4:** Update all references: `grep -rn "ComicsPage" src/` → rename type usages in `MainWindow.cpp` (the `comicsPage` local + `new ComicsPage` → `new MangaPage`, but leave the *mode wiring* edits for Phase 4) and any other includers (`ComicsDownloadsPage`, dev-control dispatcher). Keep behavior identical for now.
- [ ] **Step 5: Build-verify** — Run: `build_check.bat` · Expected: BUILD OK (verify `out/Tankoban.exe` mtime advanced per the verify-exe-mtime rule). No behavior change yet.
- [ ] **Step 6: Commit** — `git commit -m "refactor(comics-split): rename ComicsPage -> MangaPage (no behavior change)"`

### Task 4: Strip Western members/flows out of MangaPage

**Files:** Modify `src/ui/pages/MangaPage.{h,cpp}`.

- [ ] **Step 1:** In `MangaPage.h`, identify the Western-only members/decls to remove (forward-decls + members for `ReadComicsScraper`, `ReadAllComicsScraper`, `WesternVolumeDownloader`, `tankoban::manga::WesternLibrary`, and the Western dev commands `devOpenWesternSeries`/`devDownloadWesternEdition`/`devWesternDownloadState`). **Do not delete the code — cut it to a scratch file** `western_extract.txt` (executor keeps it for Phase 3 paste).
- [ ] **Step 2:** In `MangaPage.cpp`, cut the Western flows: Western search/catalog wiring, the Western branch of the search results, the Western `ComicsSeriesView` open path, Western library grid/Continue contributions, Western download dispatch. Preserve all manga flows.
- [ ] **Step 3:** Remove now-unused Western `#include`s from MangaPage.
- [ ] **Step 4: Build-verify** — `build_check.bat` → BUILD OK. (MangaPage compiles without Western symbols.)
- [ ] **Step 5: Smoke (manual gate):** `build_and_run.bat`; confirm the Comics/Manga grid still loads the manga library, search works, a series opens, reader opens. (Hemanth's eyes; or `tankoctl comics-*` snapshot.)
- [ ] **Step 6: Commit** — `git commit -m "refactor(comics-split): MangaPage = manga-only; Western flows extracted to scratch"`

---

## Phase 3 — Create WesternComicsPage

### Task 5: New WesternComicsPage class shell

**Files:** Create `src/ui/pages/WesternComicsPage.{h,cpp}`; modify `CMakeLists.txt`.

- [ ] **Step 1:** Create `WesternComicsPage.h` mirroring MangaPage's *shape* (QWidget subclass; `activate()`, `resetToRoot()`, `refreshContinueStrip()`, `enteredLayer`/`exitedLayer` signals, `setTorrentClient()`, dev-snapshot accessors). objectName `"western_comics"`. Members = the Western set extracted in Task 4 (`ReadComicsScraper`, `ReadAllComicsScraper`, `WesternVolumeDownloader`, `WesternLibrary`, `WesternCatalogLoader`, a `ComicsSeriesView`).
- [ ] **Step 2:** Implement `WesternComicsPage.cpp` by pasting the Western flows from `western_extract.txt` into the standard page skeleton (build the grid from `WesternLibrary`, wire Western search → results → `ComicsSeriesView`, wire `WesternVolumeDownloader` dispatch, Continue strip from Western progress). Reuse the shared tile/grid/search widgets (`TileStrip`, `LibraryListView`, `TileCard`) the same way MangaPage does.
- [ ] **Step 3:** Add both files to the CMake sources list.
- [ ] **Step 4: Build-verify** — `build_check.bat` → BUILD OK. Confirm `WesternComicsPage.cpp.obj` built (new-source false-green guard: check the .obj exists).
- [ ] **Step 5: Commit** — `git commit -m "feat(comics-split): WesternComicsPage (Western half extracted)"`

---

## Phase 4 — Wire Manga + Comics as top-level modes

### Task 6: Mode constants, nav pill, page-stack registration

**Files:** Modify `src/ui/MainWindow.cpp`.

- [ ] **Step 1:** Add the Manga page-id constant beside the others (near `MainWindow.cpp:63-75`):
```cpp
static constexpr const char *PAGE_MANGA = "manga";
// PAGE_COMICS ("comics") now denotes the WESTERN comics mode.
```
- [ ] **Step 2:** Update `navDefs` (`MainWindow.cpp:531-542`) — Manga first, then Comics (Western), preserving order intent:
```cpp
const NavDef navDefs[] = {
    { PAGE_MANGA,   "Manga"   },
    { PAGE_COMICS,  "Comics"  },   // Western
    { PAGE_BOOKS,   "Books"   },
    { PAGE_STREAM,  "Theatre" },
};
```
- [ ] **Step 3:** In `buildPageStack()` (`MainWindow.cpp:707-735`): the renamed `MangaPage` (was `comicsPage`) keeps its layer wiring but retarget its nav-layer domain to `"manga"` (the `enteredLayer`/`exitedLayer`/`setRootLayer` calls use `"manga"`, root label `"Library"`). Then instantiate `WesternComicsPage` with its own block: add to stack, wire `enteredLayer`→`pushLayer`, `exitedLayer`→`popLayer("comics")`, and `setRootLayer("comics", {pageId:"comics", id:"library", label:"Library"})`.
- [ ] **Step 4:** In `resetActivePageToRoot()` (`MainWindow.cpp:1111-1125`): add the `western` page to the polymorphic dispatch (`westernPage->resetToRoot()`), and ensure the manga branch calls the renamed page.
- [ ] **Step 5:** Wire `setTorrentClient` for `WesternComicsPage` wherever ComicsPage's was wired (Western Premium/torrent downloads). Wire `ComicsDownloadsPage` to read both via the new `entriesForOrigin` partition (Manga downloads view = `"manga"`, a Western downloads view = `"western"`), or scope the existing downloads page per active mode.
- [ ] **Step 6: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(comics-split): register Manga + Comics(Western) as top-level modes"`

### Task 7: Keybinds + per-mode progress domains

**Files:** Modify `src/ui/MainWindow.cpp` (keybinds), the progress-store callers in MangaPage/WesternComicsPage.

- [ ] **Step 1:** Add/adjust mode keybinds (grep `Ctrl+1`/`Ctrl+2` mode shortcuts in MainWindow): Manga + Comics get distinct shortcuts; Books/Theatre shift accordingly.
- [ ] **Step 2:** Point MangaPage progress writes at `UnifiedProgressStore` domain `"manga"` and WesternComicsPage at `"western_comics"` (grep how ComicsPage currently keys continue-reading; split the domain). Existing manga progress already lives under the manga library — confirm it surfaces in Manga's Continue strip; Western progress in Comics'.
- [ ] **Step 3: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 4: Commit** — `git commit -m "feat(comics-split): per-mode keybinds + Continue domains"`

---

## Phase 5 — Verify + close

### Task 8: Full smoke + review gate

- [ ] **Step 1: Smoke (Hemanth gate)** — `build_and_run.bat`. Confirm:
  - Five pills: **Manga · Comics · Books · Theatre** (Manga + Comics distinct).
  - **Manga** mode: manga library loads, search→series→reader works, Continue strip shows manga progress.
  - **Comics** (Western) mode: Western library loads, Western search→series works, Western download dispatch works, Continue strip shows Western progress.
  - No manga item appears in Comics or vice-versa; clicking a pill from a deep view resets to that mode's root.
  - Books + Theatre unchanged.
- [ ] **Step 2:** `tankoctl` state check — `out\tankoctl.exe get-modes` (or introspect-tree) shows both manga + comics modes; `comics-*` snapshots return per-mode libraries.
- [ ] **Step 3: Cross-model review** — `/codex-review` the full diff against this plan's Definition of Done (producer ≠ reviewer). Address every NOT-MET.
- [ ] **Step 4:** Post RTC line to `agents/chat.md` (contracts-v3, `Done-when:` = the DoD below). Delete `western_extract.txt`.

---

## Definition of Done

- Topbar shows **Manga** and **Comics** as separate pills (plus Books, Theatre).
- Manga mode = Asian sources only (AniList/MangaUpdates/MangaFire/Nyaa/Premium); Comics mode = Western only (GetComics/ReadComics/ReadAllComics).
- Each mode: its own library grid, its own search, its own Continue strip, its own per-mode back-stack reset; no cross-mode bleed.
- Downloads partitioned by `MangaDownloadIndex::originForSource`.
- No data loss: existing manga library shows in Manga, Western in Comics; downloaded files on disk untouched.
- No regression: comic reader, Books, Theatre, torrent engine unchanged.
- `build_check.bat` green; unit tests pass; Hemanth smoke passes; Codex review APPROVE.

---

## Self-Review (author pass)

- **Spec coverage:** spec §4 (Comics arc) + §2 Manga/Comics line + §6 comics-maps-over + §9 file refs + §10 DoD comics rows → covered by Phases 1-5. Video arc (spec §5) intentionally deferred to Arc 2 plan.
- **Placeholder scan:** the page-split tasks (4, 5) intentionally say "cut/paste the Western flows" rather than reproduce thousands of lines of unread `ComicsPage.cpp` — the executor reads the real file per the Phase-2 pre-task note. The mechanical wiring (Tasks 1, 6) has complete code. This is the honest shape for a large-page refactor; not a placeholder gap.
- **Type consistency:** `originForSource`/`entriesForOrigin` names used consistently (Tasks 1, 2, 6, DoD). objectNames: `"manga"`, `"comics"` (Western), `"western_comics"` (page objectName) — note the page objectName vs the mode page-id `"comics"`; Task 6 Step 3 uses the mode page-id `"comics"` for nav domain.

---

## Execution notes (2026-06-14, Agent 1 — ground-truth corrections from the coupling map)

A read-only 5-agent coupling map of `ComicsPage.{h,cpp}` (5745 lines) refined **how** to execute. Target shape unchanged (two top-level pills, no cross-bleed, spec §4 DoD).

1. **PageId mapping (refines Tasks 3 + 6).** The manga page **inherits the legacy `"comics"` pageId / nav-domain / `comics_*` dev-prefix** — class renamed `ComicsPage`→`MangaPage` but **objectName/pageId stays `"comics"`**; the Western page gets a **new pageId `"western_comics"`**. Rationale: ~10 hardcoded `"comics"` literals (nav `setRootLayer`/`pushLayer`/`restoreLayer` + `activatePage` in dev handlers) and the user's existing manga library + progress data are keyed `"comics"`; inheriting avoids a fragile literal-sweep + a data migration. User-facing labels are still **Manga** + **Comics** (label ≠ pageId). Explicitly within spec §6 latitude. (This reverses Task 6 Step 1's `PAGE_MANGA="manga"` / Western-keeps-"comics" assignment.)

2. **Shared engine is INJECTED, not duplicated (refines Tasks 4/5).** `MangaDownloader`, `MangaDownloadIndex`, `MangaSourceRegistry`, the NAM, and `TorrentClient` are the shared comics engine — **Western downloads run through `MangaDownloader`** (source `readallcomics`). MangaPage owns them; `WesternComicsPage` receives them via setters (mirroring `setTorrentClient`). Each page constructs its **own `ComicsSeriesView` + `m_stack`** (the single shared `m_tyVolumeSeriesView` is the deepest coupling). Shared chrome (`buildSearchRow`, search-history dropdown, `fetchPosterForTile`) + pure utils (`humanizeSlug`, `resolveSourceLabel`, `normalizeWesternTitle`) get promoted to a shared helper.

3. **CMake correction (Tasks 3/5).** Source registration is **`cmake/TankobanSources.cmake:38,287`** — NOT root `CMakeLists.txt` (zero refs) and NOT `cmake/TankobanTests.cmake` (its `ReadComicsPage` hits are the unrelated `ReadComicsPageParse` scraper).

4. **Dev-bridge (Task 6).** One `comicsPage()` resolver (`MainWindow.cpp:1802-1804`) gates all 15 dev handlers; the 3 western handlers move to a new `westernPage()` resolver. The catch-all forwarder (`:2587-2589`) prefix-routes `comics_*` — western dev-cmds need a distinct prefix or co-handling.

5. **17 pure-Western methods** (exact `ComicsPage.cpp` line ranges) cut to `WesternComicsPage`; **~14 branching methods** keep a manga path and lose their Western arm. Line-range inventory captured in coupling-map workflow `wf_ca12835e-cf0` (transient, not committed).

---

## Execution order revision (2026-06-14, Agent 1 — expand-contract, post-rename)

**Why the plan's Task 4-before-5 order can't stay:** MangaPage's 3 Western dev
methods (`devOpenWesternSeries`/`devDownloadWesternEdition`/`devWesternDownloadState`)
are called from MainWindow's dev-bridge (`comics_open_western_series` etc.,
MainWindow.cpp:2301-2329 via the `comicsPage()` resolver). Deleting them from
MangaPage *first* breaks the MainWindow link. The Western UI methods are
internal, but the dev methods + their transitive Western deps are externally
referenced. So the split uses the **expand-contract refactor pattern** (add new,
migrate callers, remove old) — every commit stays build-green:

1. **Create `WesternComicsPage.{h,cpp}` (ADDITIVE).** Self-contained `QWidget`
   page: the 17 Western methods (own `ComicsSeriesView` + `m_stack` + Western
   search bar + `WesternLibrary` + `WesternVolumeDownloader`); the shared engine
   (`MangaDownloader`/`MangaDownloadIndex`/`MangaSourceRegistry`/NAM/`TorrentClient`)
   received via **injection setters** (NOT constructed — single shared store);
   the shared chrome (`buildSearchRow`, search-history dropdown, `fetchPosterForTile`,
   `setSearchBusy`) copied for now (de-dup in step 4). objectName `"western_comics"`.
   Register in `cmake/TankobanSources.cmake`. NOT wired in MainWindow yet → builds
   green as dead code. Commit.
2. **Wire MainWindow (Phase 4, re-claim + announce navDefs to Agent 4).** Add
   `WesternComicsPage` to the page stack; navDefs labels → **Manga** + **Comics**
   (Western pill, pageId `"western_comics"`); enteredLayer/exitedLayer + setRootLayer;
   activate/resetToRoot/restoreLayer dispatch (the polymorphic trio); inject the
   shared engine + `setTorrentClient`; add a `westernPage()` dev resolver and
   **repoint the 3 Western dev handlers** at it (+ `western_*` prefix or co-handle).
   Build green. **Hemanth smoke: both pills, Western works.** Commit.
3. **Strip Western from MangaPage (was Task 4).** Now that MainWindow drives Western
   via `WesternComicsPage`, remove the 17 Western methods + branches + members +
   includes from MangaPage → manga-only. Build green. **Hemanth smoke: manga still
   works.** Commit. (No more `western_extract.txt` scratch needed — step 1 already
   moved the code.)
4. **Extract `ComicsPageBase`.** De-dup the shared chrome between the two pages into
   a base both derive from. Build green. Commit.
5. **Task 7** (per-mode keybinds + Continue domains) + **Task 8** (full smoke +
   Codex review vs DoD + RTC).

This reorders the plan's Tasks 4↔5 but is the only sequence that keeps the build
green given the MainWindow→Western-dev coupling. Target/DoD unchanged.
