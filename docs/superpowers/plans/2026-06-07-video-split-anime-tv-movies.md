# Video Split (Anime + TV + Movies) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> This is **Arc 2** of the six-mode restructure (spec: `docs/superpowers/specs/2026-06-07-six-mode-restructure-design.md`, §5). Arc 1 (Comics → Manga + Western) is a separate plan: `docs/superpowers/plans/2026-06-07-comics-split-manga-western.md`. The two arcs share **one file** (`src/ui/MainWindow.cpp`) — see Phase 7.
> **This is v3** — revised after two cross-model (Codex) plan reviews. See the **Revision Logs** for what changed and why.

**Goal:** Split the single "Theatre" video mode into three top-level modes — **Anime** (all Japanese animation: series *and* films), **TV** (non-anime series), **Movies** (non-anime films) — each with its own page, library, Continue strip, and tailored catalog, over the **shared** video engine.

**Architecture:** "Split the faces, share the engine." The engine already computes the classification inputs (`type` = movie/series; anime detection via genre+country). The work is: (1) make the anime discriminator **persistent** (currently transient) + classify at the **moment full meta resolves** (genres/country are not on catalog previews), (2) **decouple mode classification from the 5-season Kitsu-reroute gate** so short anime series + anime films classify correctly, (3) **hoist the shared engine** — `AddonRegistry`/`MetaAggregator`/`StreamAggregator` **and** the `StreamServerEngine` (Stremio subprocess) + `StreamPlayerController` — out of `StreamPage` into an injected `VideoModeServices` so three pages share **one** engine + **one** subprocess (not 3×), (4) instantiate the now-mode-parameterized `StreamPage` **three times**, (5) wire three top-level pills. **Start fresh** on video content.

**Tech Stack:** C++17, Qt 6 (Widgets), CMake + ninja, GoogleTest. Build: `build_check.bat` / `build_and_run.bat`. Dev-control: `out\tankoctl.exe`. **Source list:** `cmake/TankobanSources.cmake`. **Test list:** `cmake/TankobanTests.cmake`. **Tests live under** `tests/core/stream/`.

**Migration:** Video side = **start fresh** (spec §6). The three pages use **new filenames** (`anime_library.json`/`tv_library.json`/`movies_library.json`) → empty on first run; old `stream_library.json` is never read by the new pages and left in place. **The boot `StreamRescueScanner` materialization (MainWindow.cpp:299-336) is disabled for the split** so existing on-disk downloads do NOT auto-populate any new library (Task 14). Downloaded files untouched; legacy `StreamDownloadIndex` rows still resolve for playback (disk-first).

---

## Revision Log — Round 1 (Codex review of v1, all 9 findings RESOLVED in v2/v3)

1. **Compile sequencing** — Task 10 keeps a **legacy `StreamPage` ctor** so every checkpoint compiles; Task 13 switches to the 3-instance wiring.
2. **Add-time classification data** — `MetaItemPreview` *has* `genres`/`country` (`MetaItem.h:67-68`) but catalog/search parsing does **not** populate them; only full meta (`onMetaItemReady`) does. → Task 6 classifies at meta-resolve + **defers** the add if unresolved.
3. **Detail-view add routing** — `StreamDetailView` calls `m_library->add/remove` directly (`:2342/:2392/:2406`). → routed through `VideoModeServices` (Tasks 9/10/12).
4. **Progress generalization** — covers `parseStreamProgressKey`, `clearProgress`, and all `StreamContinueStrip` `startsWith("stream:")` sites (Tasks 5/11).
5. **Single-`m_streamPage` + engine ownership** — `StreamPage` owns the engine (`StreamPage.cpp:306-307`); 37 refs. → hoist into `VideoModeServices` (Task 9) + `pageForVideoMode()` + enumerated refs + dev-control modes (Task 13).
6. **Cinemeta returns anime** — classification (add/open-time) is the authoritative landing guarantee; cache + redirect-on-open backstop (Task 8/D9).
7. **Country matching** — `isAnimeTitle` normalizes `Japan`/`JP`/lists (Task 1).
8. **Anime-film play path** — D10: anime films classify to Anime, play via the movie path (Task 7).
9. **Shared-file preflight** — Task 13 verifies the *actual* post-Agent-1 contract.

## Revision Log — Round 2 (Codex re-review of v2: 3 NEW blockers + 1 SHOULD, all verified + fixed in v3)

- **B1 [BLOCKER] — `TorrentClient` dropped.** The new injected ctor omitted `TorrentClient*`, but `StreamPage` uses `m_torrentClient` for retry wiring, detail remove-safeguards, the download panel, library layout, and most download/play actions. → **D1/Task 10:** the injected ctor **keeps `TorrentClient*`**; wired exactly like the legacy path.
- **B2 [BLOCKER] — 3× Stremio subprocess.** Verified `StreamPage::buildUI()` constructs `m_streamEngine = new StreamServerEngine(cacheDir, this); m_streamEngine->start()` + `StreamPlayerController` (`StreamPage.cpp:817-833`); the cache dir is shared (`dataDir/stream_server_cache`). Three pages = three subprocesses over one cache. → **D12/Task 9:** hoist `StreamServerEngine` + `StreamPlayerController` into `VideoModeServices` (single each); the controller's 4 UI signals route to the active video page.
- **B3 [BLOCKER] — start-fresh misses the boot rescue scanner.** Verified `MainWindow.cpp:299-336`: a deferred first-launch migration walks Videos roots and **materializes `StreamLibrary` entries** via `StreamRescueScanner` into `m_streamPage->streamLibrary()`. → **Task 14:** disable the library-materialization for the split (index-only at most); smoke asserts the three libraries stay empty on first boot **with existing downloads on disk**.
- **S1 [SHOULD] — CMake/test reality.** Sources are in `cmake/TankobanSources.cmake`, tests in `cmake/TankobanTests.cmake`, tests under `tests/core/stream/`; the test target does **not** link `StreamLibrary.cpp` (it pulls `TorrentClient`→libtorrent). → all CMake/test refs corrected; the `StreamLibraryEntry` JSON codec is **extracted to a dep-free `StreamLibraryCodec.{h,cpp}`** so codec tests link cleanly; library/hub integration is verified by the Task 15 smoke (matching the existing pure-logic-unit + integration-by-smoke pattern).

## Revision Log — Round 3 (Codex confirmation of v3: B1/B2/B3/S1 RESOLVED; 1 NEW blocker from the v3 D12 fix + 1 SHOULD, fixed in v4)

- **B1/B2/B3/S1 — all confirmed RESOLVED.**
- **[BLOCKER] — controller signal routing (my v3 D12 introduced it).** Connecting the shared `StreamPlayerController` signals to the *active* page on `activatePage` is wrong for an in-flight stream: switching pages mid-stream would deliver `readyToPlay`/`streamFailed`/`streamStopped` to a page that didn't start it, corrupting its pending-title/detail-status/cleanup. → **D12 rewritten:** signals route to the stream-**OWNER** (the initiating page) for the session via `VideoModeServices::beginStreamSession/endStreamSession` (`Qt::UniqueConnection` + stored `QMetaObject::Connection`s); page switches never re-route a live stream. Removed the activatePage connect (Task 13 step 6); session lifecycle lives in Tasks 9-10. Lazy `playerController()` now inits on first *play*, not page build — so visiting a page never spawns the subprocess.
- **[SHOULD] — boot scanner index-only, not disabled.** Disabling the rescue scanner would leave unindexed on-disk downloads unplayable. → **Task 13 step 8** makes it strictly index-only (registers `StreamDownloadIndex` rows for disk-first playback, materializes **no** library entry); **Task 15 step 6** adds a smoke that a rescued row plays disk-first while libraries stay empty.

---

## Phase 0 — Locked Decisions (design gate, no code)

- **D1 — Page-shell: ONE `StreamPage` class, parameterized by `StreamMode`, instantiated 3×** (objectName `"anime"`/`"tv"`/`"movies"`). The shared engine is hoisted into `VideoModeServices` (D7/D12) and injected. The injected ctor **keeps `CoreBridge*` + `TorrentClient*`** (B1). Heaviness caveat: each instance defers heavy work (catalog fetch + detail-view build) to `activate()`/first-show.
- **D2 — Detail view: parameterize the existing `StreamDetailView`**; each page owns its own (lazily built), fed by the shared engine via services. No fork.
- **D3 — Theatre pill retired.** `PAGE_STREAM` removed as a pill; the shared read-only downloads page (`PAGE_STREAM_DOWNLOADS`) kept once, shared; its back-target = launching video mode (default Movies).
- **D4 — Vestigial `VideosPage` (`PAGE_VIDEOS`, Ctrl+3): DEFERRED, untouched** (spec §7), but its `MetaAggregator` source moves from `m_streamPage->metaAggregator()` to `services->metaAggregator()` (Task 13).
- **D5 — Keybinds (SHARED with Agent 1): order `Manga · Comics · Books · Anime · TV · Movies`** → `Ctrl+1..6`; sidebar toggle moves off `Ctrl+5` to `Ctrl+0`. One table, edited once, jointly agreed.
- **D6 — Classification is data-driven, decoupled from the reroute gate, evaluated when full meta resolves.** Mode = `classifyStreamMode(isAnime, type)`, `isAnime = isAnimeTitle(genres, country)`. The `seasons.size() >= 5` gate (`MetaAggregator.cpp:483`) stays *only* on the Kitsu episode-list reroute.
- **D7 — `VideoModeServices`** owns the shared **engine** (`AddonRegistry`, `MetaAggregator`, `StreamAggregator`, **`StreamServerEngine`, `StreamPlayerController`** — see D12), the **three `StreamLibrary` instances**, and an **anime-classification cache** (`imdb → {isAnime, kitsuId}`). Routes adds by classification; scopes cross-mode removal. Constructed once in `MainWindow::buildPageStack`; receives the MainWindow-owned `StreamDownloadIndex` + `TorrentClient` via setters.
- **D8 — Sub/dub (v1):** a SUB/DUB preference on the Anime detail that sorts already-fetched sources by filename tag (`[SUB]`/`[DUB]`/`Dual`/`Dual-Audio`, case-insensitive). No new fetch. Play-time mpv track-switching (Agent 3) is out of scope.
- **D9 — Catalog cleanliness best-effort; landing guaranteed.** Add/open-time classification → routing is authoritative; a classification cache filters known anime from TV/Movies grids; opening an anime title from a TV/Movies grid redirects (adds land in Anime) with a one-line note.
- **D10 — Anime films play via the movie path** (the `kitsu:<id>:<ep>` route is episode-based and already skipped for `type == "movie"` at `StreamPage.cpp:2428`). Anime films are Anime-mode library items; no episode UI.
- **D11 — Dev-control gains `anime`/`tv`/`movies` modes.** Legacy `stream`/`stream_*` tankoctl commands alias to a default video page (Movies) for back-compat.
- **D12 — Hoist the streaming engine + player controller (B2), routed to the stream-OWNER (not the active page).** `StreamServerEngine` (the Stremio subprocess; one cache dir) and `StreamPlayerController` move into `VideoModeServices` as **single instances**, created lazily on first use (also helps startup cost, D1). The controller's 4 signals (`bufferUpdate`/`readyToPlay`/`streamFailed`/`streamStopped`) route to the page that **initiated** the current stream — its *owner* — for the session's lifetime, **not** the currently-active page. (R3 fix: connecting on page-activation would deliver `readyToPlay`/`streamStopped` to a page that didn't start the stream if the user switches pages mid-stream, corrupting its `m_pendingStreamTitle`/detail-status/cleanup.) Mechanism: `VideoModeServices::beginStreamSession(StreamPage* owner)` disconnects the prior owner and connects this one (`Qt::UniqueConnection` + stored `QMetaObject::Connection`s); `endStreamSession(owner)` disconnects on `streamStopped`/`streamFailed`. A page initiates play → `beginStreamSession(this)`; switching pages does **not** re-route or tear down a live stream. Only one stream plays at a time (a new `beginStreamSession` ends the prior), so the single engine/controller is never driven concurrently.

---

## File Structure

**Create:**
- `src/core/stream/StreamMode.h` (+ `.cpp`) — `enum class StreamMode`; `classifyStreamMode`; `isAnimeTitle` (normalized); `streamModeKey`/`streamModeFromKey`/`streamLibraryFilename`.
- `src/core/stream/StreamLibraryCodec.{h,cpp}` — **dep-free** `StreamLibraryEntry ↔ QJsonObject` (only Qt JSON + the POD struct; no `TorrentClient`). Used by `StreamLibrary.cpp`; unit-testable.
- `src/core/stream/VideoModeServices.{h,cpp}` — shared engine (incl. `StreamServerEngine` + `StreamPlayerController`, lazy) + 3 `StreamLibrary` + classification cache + add routing.
- `tests/core/stream/test_stream_mode.cpp`, `tests/core/stream/test_stream_library_codec.cpp`, `tests/core/stream/test_progress_domains.cpp`.

**Modify:**
- `src/core/stream/StreamLibrary.{h,cpp}` — `animeFlag`+`kitsuId` on the entry (via the codec); ctor filename param.
- `src/core/stream/StreamDownloadIndex.{h,cpp}` — `animeFlag` on `Entry` + codec (back-compat default).
- `src/core/CoreBridge.{h,cpp}` — `anime`/`tv`/`movies` domains across `PROGRESS_FILES`/`allProgress`/`progress`/`saveProgress`/`clearProgress`/`parseStreamProgressKey`.
- `src/core/stream/UnifiedProgressStore.{h,cpp}` — domain-prefix-parameterized key build/parse + payload accessor.
- `src/core/stream/MetaAggregator.{h,cpp}` — `entryResolved` signal; mode-scoped catalog requests; classification-cache feed; classification decoupled from the reroute gate.
- `src/core/stream/CatalogAggregator.{h,cpp}`, `src/core/stream/StreamAggregator.{h,cpp}`, `src/ui/pages/stream/CatalogBrowseScreen.cpp` — `StreamMode` filter + best-effort anime exclusion.
- `src/core/stream/AnimeCatalogResolver.{h,cpp}` — point anime detection at `isAnimeTitle`.
- `src/ui/pages/StreamPage.{h,cpp}` — mode + injected services (+ `TorrentClient`); legacy ctor kept until Task 13; defer heavy work; engine/controller borrowed from services.
- `src/ui/pages/stream/StreamContinueStrip.{h,cpp}` — domain via ctor/setter (`:79`, `:116`, `:275`).
- `src/ui/pages/stream/StreamSearchWidget.{h,cpp}` — mode-filtered sections + anime section.
- `src/ui/pages/stream/StreamDetailView.{h,cpp}` — services + mode; routed add/remove; per-mode formatting + sub/dub.
- `src/ui/MainWindow.{cpp,h}` — **SHARED** — constants, navDefs, `buildPageStack`, `activatePage`, `resetActivePageToRoot`, `onLayerRestoreRequested`, `bindShortcuts`, all 37 `m_streamPage` refs, the boot rescue scanner, dev-control modes.
- `cmake/TankobanSources.cmake` — register new source `.cpp`s. `cmake/TankobanTests.cmake` — register new test `.cpp`s.

**Pre-task read (executor):** before Phase 3/5/6, read `src/ui/pages/StreamPage.cpp` and `src/ui/pages/stream/StreamDetailView.cpp` **in full** (StreamDetailView ≈ 4666 lines). Tasks cite anchors; reproduce surrounding code from the real file.

---

## Phase 1 — Classification core + persistent discriminator (pure logic, TDD)

### Task 1: `StreamMode` enum + pure classifier + normalized anime-title test

**Files:** Create `src/core/stream/StreamMode.{h,cpp}`; Test `tests/core/stream/test_stream_mode.cpp`; register in `cmake/TankobanSources.cmake` + `cmake/TankobanTests.cmake`.

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "core/stream/StreamMode.h"

TEST(StreamMode, AnimeFlagWinsForBothTypes) {
    EXPECT_EQ(classifyStreamMode(true,  "series"), StreamMode::Anime);
    EXPECT_EQ(classifyStreamMode(true,  "movie"),  StreamMode::Anime);  // anime film
}
TEST(StreamMode, NonAnimeSeriesIsTv)     { EXPECT_EQ(classifyStreamMode(false,"series"), StreamMode::TV); }
TEST(StreamMode, NonAnimeMovieIsMovies)  {
    EXPECT_EQ(classifyStreamMode(false,"movie"), StreamMode::Movies);
    EXPECT_EQ(classifyStreamMode(false,""),      StreamMode::Movies);
}
TEST(StreamMode, AnimeTitleNormalizesCountryAndGenre) {
    EXPECT_TRUE (isAnimeTitle({"Animation","Action"}, "Japan"));
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "JP"));
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "Japan, China"));
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "Japan / USA"));
    EXPECT_FALSE(isAnimeTitle({"Animation"},          "United States"));
    EXPECT_FALSE(isAnimeTitle({"Drama"},              "Japan"));
    EXPECT_FALSE(isAnimeTitle({},                      "Japan"));
}
TEST(StreamMode, KeyAndFilenameRoundTrip) {
    EXPECT_EQ(streamModeKey(StreamMode::Anime),  QStringLiteral("anime"));
    EXPECT_EQ(streamModeFromKey("tv"),           StreamMode::TV);
    EXPECT_EQ(streamModeFromKey("bogus"),        StreamMode::Movies);
    EXPECT_EQ(streamLibraryFilename(StreamMode::Movies), QStringLiteral("movies_library.json"));
}
```

- [ ] **Step 2: Run, verify it fails** — `ctest -R StreamMode -V` → FAIL (header not found).
- [ ] **Step 3: Implement `StreamMode.h`** — `enum class StreamMode { Anime, TV, Movies };` + inline `classifyStreamMode(bool isAnime, const QString& type)` (anime→Anime; "series"→TV; else Movies) + free decls `isAnimeTitle`, `streamModeKey`, `streamModeFromKey`, `streamLibraryFilename`.
- [ ] **Step 4: Implement `StreamMode.cpp`** — `isAnimeTitle`: require an "Animation" genre (case-insensitive) AND a Japan-origin token after splitting `country` on `[,/]` and trimming (match `Japan`/`JP`/`JPN`, case-insensitive). Key/filename helpers per the test. `#include <QRegularExpression>`.
- [ ] **Step 5:** Register `StreamMode.cpp` in `cmake/TankobanSources.cmake` (near the other `src/core/stream/*.cpp`) and `tests/core/stream/test_stream_mode.cpp` in `cmake/TankobanTests.cmake`.
- [ ] **Step 6: Run, verify PASS** — `ctest -R StreamMode -V` → PASS. Confirm `StreamMode.cpp.obj` built.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): StreamMode enum + classifier + normalized isAnimeTitle"`

### Task 2: Extract dep-free entry codec + add `animeFlag`/`kitsuId`

**Files:** Create `src/core/stream/StreamLibraryCodec.{h,cpp}`; Modify `src/core/stream/StreamLibrary.{h,cpp}` (struct + use the codec); Test `tests/core/stream/test_stream_library_codec.cpp`; register in both cmake files.

> **S1:** the codec must NOT depend on `TorrentClient` (so the test links without libtorrent). `StreamLibrary.cpp` keeps its `TorrentClient` cascade in `remove()`; only the pure JSON↔struct mapping moves out.

- [ ] **Step 1: Write the failing test** (codec round-trip + legacy default)

```cpp
#include <gtest/gtest.h>
#include <QJsonObject>
#include "core/stream/StreamLibraryCodec.h"

TEST(StreamLibraryCodec, RoundTripsNewFields) {
    StreamLibraryEntry e; e.imdb="tt9335498"; e.type="series"; e.name="Demon Slayer";
    e.animeFlag=true; e.kitsuId=41370;
    const StreamLibraryEntry back = streamLibraryEntryFromJson(streamLibraryEntryToJson(e));
    EXPECT_TRUE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, 41370);
    EXPECT_EQ(back.type, QStringLiteral("series"));
}
TEST(StreamLibraryCodec, LegacyRowsDefaultNonAnime) {
    QJsonObject legacy; legacy["imdb"]="tt0111161"; legacy["type"]="movie"; legacy["name"]="X";
    const StreamLibraryEntry back = streamLibraryEntryFromJson(legacy);
    EXPECT_FALSE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, -1);
}
```

- [ ] **Step 2: Run, verify FAIL.**
- [ ] **Step 3:** Add `bool animeFlag = false; int kitsuId = -1;` to `StreamLibraryEntry` (`StreamLibrary.h:13-22`). Create `StreamLibraryCodec.h` declaring `QJsonObject streamLibraryEntryToJson(const StreamLibraryEntry&)` + `StreamLibraryEntry streamLibraryEntryFromJson(const QJsonObject&)`; `.cpp` implements them (all 10 fields; new fields default `false`/`-1` when absent). **`StreamLibraryEntry` must be visible to the codec** — keep the struct in `StreamLibrary.h` and have `StreamLibraryCodec.h` include it (it pulls no `TorrentClient`).
- [ ] **Step 4:** In `StreamLibrary.cpp`, replace the bodies of the private `fromJson`/`toJson` with calls to the free codec functions (or delete them and call the codec at the load/save sites).
- [ ] **Step 5:** Register `StreamLibraryCodec.cpp` in `cmake/TankobanSources.cmake`; register the codec `.cpp` **and** the test in `cmake/TankobanTests.cmake` (the test links only `StreamLibraryCodec.cpp` + Qt — no `TorrentClient`).
- [ ] **Step 6: Run, verify PASS;** `build_check.bat` → BUILD OK (the app still builds with the codec extracted).
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): dep-free StreamLibraryCodec + animeFlag/kitsuId"`

### Task 3: `animeFlag` on `StreamDownloadIndex::Entry`

**Files:** Modify `src/core/stream/StreamDownloadIndex.{h,cpp}`; extend `tests/core/stream/test_stream_download_index_state.cpp` (already linked) or add a sibling registered in `cmake/TankobanTests.cmake`.

- [ ] **Step 1:** Read the `Entry` struct + codec. Write a failing round-trip test (`animeFlag=true` survives; legacy row → `false`).
- [ ] **Step 2:** Run, verify FAIL.
- [ ] **Step 3:** Add `bool animeFlag = false;` to `Entry`; serialize; default `false` on read. Match existing naming.
- [ ] **Step 4:** Run, verify PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): animeFlag on StreamDownloadIndex::Entry (back-compat)"`

---

## Phase 2 — Storage parameterization

### Task 4: Parameterize `StreamLibrary` filename

**Files:** Modify `src/core/stream/StreamLibrary.{h,cpp}` (ctor; `FILENAME` :77 → member; `load()` :150; `save()` :173); `src/ui/pages/StreamPage.cpp` (the `new StreamLibrary(...)` call site — find it).

> Isolation is structural (distinct `m_filename` → distinct `JsonStore` key). Verified at the Task 15 integration smoke (a `StreamLibrary`-instantiating unit test would pull `TorrentClient`→libtorrent into the test link — avoided per S1).

- [ ] **Step 1:** Change ctor to `StreamLibrary(JsonStore* store, const QString& filename, QObject* parent = nullptr)`; replace `static constexpr const char* FILENAME` with `const QString m_filename;` set in the init list; use `m_filename` in `load()`/`save()`.
- [ ] **Step 2:** Update the existing call site in `StreamPage.cpp` to pass `QStringLiteral("stream_library.json")` (no behavior change; replaced in Phase 5).
- [ ] **Step 3: Build-verify** — `build_check.bat` → BUILD OK (verify exe mtime advanced).
- [ ] **Step 4: Commit** — `git commit -m "refactor(video-split): StreamLibrary takes a filename"`

### Task 5: `anime`/`tv`/`movies` progress domains (FULL generalization)

**Files:** Modify `src/core/CoreBridge.{h,cpp}` — `PROGRESS_FILES` (≈:27-31), `allProgress` (≈:188), `progress` (≈:263-274), `saveProgress` (≈:227), `clearProgress` (stream-only branch), `parseStreamProgressKey` (≈:44). Modify `src/core/stream/UnifiedProgressStore.{h,cpp}` — `allEpisodePayloadsForStreamDomain` (≈:84) + `streamDomainKeyForEntry` (≈:256). Test: extend `tests/core/stream/test_unified_progress_store.cpp` (UnifiedProgressStore is already in the test link).

> **R1#4:** changing only `allProgress(m_domain)` leaves Continue strips broken because the key parser + `clearProgress` are `stream:`-only. Generalize the prefix everywhere. Unit-test the **UnifiedProgressStore** key build/parse (already linked); CoreBridge domain routing is covered by the Task 15 smoke.

- [ ] **Step 1: Read** all the sites to capture the `domain == "stream"` branch pattern + the `"stream:"` prefix usage.
- [ ] **Step 2: Write the failing test** (in `test_unified_progress_store.cpp`) — building/parsing a key under prefix `"anime"` yields `anime:<imdb>:...` and parses back correctly; `"tv"` likewise; the legacy `"stream"` default still works.
- [ ] **Step 3:** Run, verify FAIL.
- [ ] **Step 4:** Parameterize `streamDomainKeyForEntry()`/`allEpisodePayloadsForStreamDomain()` (and `parseStreamProgressKey`) by a domain prefix (default `"stream"`). In `CoreBridge.cpp`, add `anime`/`tv`/`movies` rows to `PROGRESS_FILES` and extend `allProgress`/`progress`/`saveProgress`/`clearProgress` to route the new domains to `UnifiedProgressStore`. Touch **every** site.
- [ ] **Step 5:** Run UnifiedProgressStore test → PASS; `build_check.bat` → BUILD OK.
- [ ] **Step 6: Commit** — `git commit -m "feat(video-split): full anime/tv/movies progress generalization (keys+clear+parse)"`

---

## Phase 3 — Classification wiring (resolve-time, race-safe) + play-path persistence

### Task 6: Classify at meta-resolve, feed the cache, emit `entryResolved`

**Files:** Modify `src/core/stream/MetaAggregator.{h,cpp}` (signal ≈:100; emit in `emitSeriesResult` ≈:609 + the movie path; decouple from the `:481-491` gate); `src/ui/pages/stream/StreamDetailView.cpp` (add path ≈:2342); `src/core/stream/AnimeCatalogResolver.{h,cpp}` (use `isAnimeTitle`). Test: extend `tests/core/stream/test_anime_catalog_resolver.cpp` (already linked).

> **R1#2/#7:** classify from the **resolved full meta** (delivered via `onMetaItemReady`); previews lack populated genres/country. If Add fires before resolution, **defer**.

- [ ] **Step 1:** Point `AnimeCatalogResolver`'s detection at `isAnimeTitle()` (Task 1) — one source of truth. If `isAnimeSeries` is referenced elsewhere, keep it as a thin wrapper calling `isAnimeTitle` (country normalization is a strict superset — no regression).
- [ ] **Step 2: Write the failing test** (in `test_anime_catalog_resolver.cpp`) — Demon Slayer (Animation+Japan) → `isAnimeTitle` true → `classifyStreamMode(..., "series")` == Anime; an anime film (Animation+Japan, "movie") → Anime; Breaking Bad → TV; Inception → Movies.
- [ ] **Step 3:** Run, verify FAIL/confirm.
- [ ] **Step 4:** Add `entryResolved(imdbId, kitsuId, isAnime)` to `MetaAggregator`; emit from the series-result path (both rerouted-anime and plain-series branches) and the movie path, carrying resolved `kitsuId` (or -1) + `isAnime` from `isAnimeTitle(meta.genres, meta.country)`. **Not** gated on `seasons.size() >= 5`.
- [ ] **Step 5: Race-safe add (`StreamDetailView` ≈:2342).** Build the entry; set `animeFlag`/`kitsuId` from the **resolved** meta if present; else hook `onMetaItemReady`/`entryResolved` once, then build + route. Never read genres/country off an unresolved preview.
- [ ] **Step 6:** Run test → PASS; `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): resolve-time anime classification + entryResolved (race-safe add)"`

### Task 7: Persist + reuse resolved `kitsuId`; anime-film path; verify pre-warm

**Files:** Modify `src/ui/pages/StreamPage.cpp` (play-path ≈:2428), `src/core/stream/MetaAggregator.cpp` (pre-warm ≈:259 — log marker); library subscribes to `entryResolved` (via `VideoModeServices`, Task 9).

> **Scope:** `AnimeIdMapCache` already persists the map (`AnimeIdMapCache.cpp:17`) + pre-warms (`MetaAggregator.cpp:259`). Real work = persist the per-series **resolved** `kitsuId` onto the entry. **D10:** anime films (`type == "movie"`) keep the movie play path (`:2428` already skips kitsu for movies).

- [ ] **Step 1:** Have `VideoModeServices` (Task 9) subscribe to `MetaAggregator::entryResolved` → write the resolved `kitsuId` onto the stored entry (idempotent) + persist; also feed the classification cache.
- [ ] **Step 2:** Series play-path (`StreamPage.cpp:2428`): read `entry.kitsuId` first; fall back to `kitsuIdForSeries(imdbId)` only when stored id is `-1`. Leave the `movie` branch unchanged (anime films use it).
- [ ] **Step 3:** Add a one-line startup log marker in the pre-warm path (`MetaAggregator.cpp:259`).
- [ ] **Step 4: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): reuse persisted kitsuId (series); anime films use movie path; mark prewarm"`

---

## Phase 4 — Catalog-brain mode scoping + best-effort anime exclusion

### Task 8: Mode-scoped catalog brains + classification-cache exclusion + redirect-on-open

**Files:** Modify `src/core/stream/CatalogAggregator.{h,cpp}` (`planRequests` ≈:134; addon select ≈:71-88), `src/core/stream/StreamAggregator.{h,cpp}` (id-prefix gate ≈:571), `src/core/stream/MetaAggregator.cpp` (candidate-sort ≈:326-343; seriesCache), `src/ui/pages/stream/CatalogBrowseScreen.cpp` (≈:308), `src/ui/pages/stream/StreamSearchWidget.cpp` (≈:220-229).

> **R1#6/D9:** Cinemeta returns anime; previews lack genres/country. Landing is guaranteed by classification (Task 6); grids are best-effort.

- [ ] **Step 1: Read** the catalog request path end-to-end.
- [ ] **Step 2: Thread a `StreamMode` filter** into the catalog request entry point (default = today). Anime → only `kitsu`/`anilist` addons; TV/Movies → Cinemeta-family with `type` = `series`/`movie`, excluding anime-prefixed addons.
- [ ] **Step 3: Best-effort grid exclusion** — when rendering TV/Movies grids, drop imdbs where `services->cachedIsAnime(imdb)` is true (cache fed by Task 6). First-ever sightings may pass (acceptable, D9).
- [ ] **Step 4: Redirect-on-open** — opening a title that resolves to anime from a TV/Movies page: detail shows, Add routes to Anime (`addClassified`), one-line note; feed the cache.
- [ ] **Step 5: seriesCache** (imdb-keyed, 24h TTL) — re-emit `entryResolved`/`animeCatalogActive` on a cache hit (or key by `(imdb,mode)`) so a second open drives correct layout + cache. Prefer re-emit-on-hit.
- [ ] **Step 6: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): mode-scoped catalog brains + cache exclusion + redirect-on-open"`

---

## Phase 5 — Shared services + the three pages

### Task 9: `VideoModeServices` (hoisted engine incl. subprocess + 3 libraries + cache + routing)

**Files:** Create `src/core/stream/VideoModeServices.{h,cpp}`; register in `cmake/TankobanSources.cmake`.

> **R1#5 + R2#B2/#B3:** `StreamPage` owns `m_addonRegistry`/`m_metaAggregator`/`m_streamAggregator` (`StreamPage.cpp:306-307`) **and** constructs `StreamServerEngine` + `StreamPlayerController` (`:817-833`). Hoist all of it here as **single** instances (engine + subprocess lazy, D12). No heavy unit test (would pull libtorrent + the subprocess); routing = `classifyStreamMode` (tested in Task 1); integration verified at Task 15 smoke.

- [ ] **Step 1: Read** `StreamPage.cpp:300-320` + `:807-833` to capture the exact construction of `AddonRegistry`/`MetaAggregator`/`StreamAggregator`/`StreamServerEngine`/`StreamPlayerController` (ctor args, cache dir, signal wiring).
- [ ] **Step 2: Implement `VideoModeServices.h`** (match real ctor signatures from the headers):

```cpp
#pragma once
#include <QObject>
#include <QHash>
#include "core/stream/StreamMode.h"
#include "core/stream/StreamLibrary.h"

class JsonStore; class StreamDownloadIndex; class TorrentClient; class CoreBridge; class StreamPage;
namespace tankostream::stream { class AddonRegistry; class MetaAggregator; class StreamAggregator; }
class StreamServerEngine; class StreamPlayerController;

// Six-mode (2026-06-07): the shared video engine + per-mode libraries, hoisted out
// of StreamPage so the three pages share ONE engine and ONE Stremio subprocess.
class VideoModeServices : public QObject {
    Q_OBJECT
public:
    explicit VideoModeServices(CoreBridge* bridge, JsonStore* store, QObject* parent = nullptr);
    // shared engine (one instance each; meta/streams created in ctor, subprocess lazy)
    tankostream::stream::AddonRegistry*    addonRegistry()  const;
    tankostream::stream::MetaAggregator*   metaAggregator() const;
    tankostream::stream::StreamAggregator* streamAggregator() const;
    StreamServerEngine*     streamEngine();          // lazily start()s the subprocess on first call
    StreamPlayerController*  playerController();      // lazily created over streamEngine()
    // R3/D12: controller signals route to the stream-OWNING page, not the active page.
    void beginStreamSession(StreamPage* owner);      // disconnect prior owner, connect this (Qt::UniqueConnection)
    void endStreamSession(StreamPage* owner);        // disconnect on streamStopped/streamFailed
    void setStreamDownloadIndex(StreamDownloadIndex* idx);
    StreamDownloadIndex* downloadIndex() const;
    void setTorrentClient(TorrentClient* tc);
    TorrentClient* torrentClient() const;
    // per-mode libraries + routing
    StreamLibrary* libraryForMode(StreamMode mode) const;
    void addClassified(const StreamLibraryEntry& entry);   // classifyStreamMode(animeFlag,type) -> library
    void removeEverywhere(const QString& imdbId);
    // classification cache (fed by MetaAggregator::entryResolved)
    void cacheIsAnime(const QString& imdb, bool isAnime, int kitsuId);
    bool cachedIsAnime(const QString& imdb) const;
signals:
    void libraryChanged(StreamMode mode);
private:
    /* m_bridge, m_addons, m_meta, m_streams, m_engine(lazy), m_controller(lazy),
       m_downloadIndex, m_torrentClient, m_anime/m_tv/m_movies, m_classCache */
};
```

`.cpp`: construct `m_addons`/`m_meta`/`m_streams` (code lifted from `StreamPage.cpp:306-307`); construct the 3 libraries via `streamLibraryFilename(mode)`; `streamEngine()` lazily `new StreamServerEngine(bridge->dataDir()+"/stream_server_cache", this)` + `start()`+`cleanupOrphans()`+`startPeriodicCleanup()` once; `playerController()` lazily over the engine; subscribe `m_meta->entryResolved` → `cacheIsAnime` + backfill the in-library entry's `kitsuId`; `addClassified` routes via `classifyStreamMode`; `removeEverywhere` removes from whichever library `has()` it. **Session ownership (D12):** hold `StreamPage* m_streamOwner` + `QList<QMetaObject::Connection> m_ownerConns`; `beginStreamSession(owner)` disconnects `m_ownerConns`, connects the controller's 4 signals to `owner`'s slots (`Qt::UniqueConnection`), sets `m_streamOwner = owner`; `endStreamSession(owner)` disconnects + clears if `owner == m_streamOwner`. The lazy `playerController()` is created on the first `beginStreamSession` (a stream actually starting), **not** on page activation — so visiting a page never spawns the subprocess; only playing does.

- [ ] **Step 3:** Register `VideoModeServices.cpp` in `cmake/TankobanSources.cmake`.
- [ ] **Step 4: Build-verify** — `build_check.bat` → BUILD OK; confirm `VideoModeServices.cpp.obj` built.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): VideoModeServices (shared engine incl. subprocess + 3 libraries + cache + routing)"`

### Task 10: Mode-parameterize `StreamPage` over injected services (legacy ctor kept; keeps TorrentClient)

**Files:** Modify `src/ui/pages/StreamPage.{h,cpp}`.

> **Pre-task read:** read `StreamPage.cpp` in full. **R2#B1:** the injected ctor MUST keep `TorrentClient*`. **R1#1:** keep the legacy ctor compiling until Task 13.

- [ ] **Step 1: New ctor + mode member.** Keep `StreamPage(CoreBridge*, TorrentClient*, ...)` (legacy; self-owns its engine; mode = Movies). Add `StreamPage(CoreBridge* bridge, TorrentClient* torrentClient, StreamMode mode, VideoModeServices* services, QWidget* parent)`. Store `m_mode`; in **both** ctors `setObjectName(streamModeKey(m_mode))` (**critical** — silent non-activation risk).
- [ ] **Step 2: Injected path borrows from services** — `m_addonRegistry = services->addonRegistry()`, `m_metaAggregator = services->metaAggregator()`, `m_streamAggregator = services->streamAggregator()`, library = `services->libraryForMode(mode)`, download index = `services->downloadIndex()`, and store `m_services`. **Do not `new` these** in the injected path, and **do not** grab `streamEngine()`/`playerController()` at construction (that would spawn the subprocess just by building a page — D12). Set `m_torrentClient = torrentClient` and wire it to the detail view / library layout / download panel exactly as the legacy `buildUI()` does (B1). **Stream session ownership (D12/R3):** when this page initiates a stream (the "watch" path), call `m_services->beginStreamSession(this)` (which connects the shared controller's 4 signals to *this* page's `onBufferUpdate`/`onReadyToPlay`/`onStreamFailed`/`onStreamStopped`), then drive playback via `m_services->playerController()`; on `onStreamStopped`/`onStreamFailed`, call `m_services->endStreamSession(this)`. Switching pages does **not** touch a live session.
- [ ] **Step 3: Route adds via services** — the detail-view add/remove (Task 12) calls `services->addClassified`/`removeEverywhere`.
- [ ] **Step 4: Pass `mode` + `services`** to `StreamContinueStrip` (domain), `StreamSearchWidget` (filter), `StreamDetailView` (Task 12), catalog requests (Task 8).
- [ ] **Step 5: Defer heavy work (D1)** — build/populate the detail view + first catalog fetch on `activate()`/first show, not the ctor. Grid iterates only this mode's library.
- [ ] **Step 6: Build-verify** — `build_check.bat` → BUILD OK (legacy ctor keeps MainWindow compiling).
- [ ] **Step 7: Commit** — `git commit -m "refactor(video-split): StreamPage mode + injected services (keeps TorrentClient; legacy ctor retained)"`

### Task 11: Mode-scope `StreamContinueStrip` + `StreamSearchWidget`

**Files:** Modify `src/ui/pages/stream/StreamContinueStrip.{h,cpp}` (`allProgress("stream")` :79; `startsWith("stream:")` :116, :275), `src/ui/pages/stream/StreamSearchWidget.{h,cpp}` (:69-77, :220-229).

- [ ] **Step 1:** Add a domain string (ctor/setter, default `"stream"`); replace **all** `"stream"`/`"stream:"` literals (`:79`, `:116`, `:275`) with `m_domain`/`m_domain + ":"`. Owning page passes `streamModeKey(mode)`.
- [ ] **Step 2:** `StreamSearchWidget::setMode(StreamMode)` — render only relevant sections; add the anime section.
- [ ] **Step 3: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 4: Commit** — `git commit -m "feat(video-split): per-mode Continue domain (all sites) + search sections"`

---

## Phase 6 — Detail view per-mode behavior

### Task 12: Detail view takes services + mode; anime extras; routed add/remove

**Files:** Modify `src/ui/pages/stream/StreamDetailView.{h,cpp}`.

> **Pre-task read:** read in full (≈4666 lines). Branches on `m_currentType`+`m_isAnime` (season combo ≈:1019-1020; ep label ≈:1216); direct `m_library->add/remove` at `:2342/:2392/:2406`. Contain new code in private helpers.

- [ ] **Step 1: Ctor** — `StreamDetailView(bridge, VideoModeServices* services, StreamMode mode, parent)` (was `(bridge, metaAggregator, library, parent)`). Use `services->metaAggregator()` for fetches; `services->libraryForMode(mode)` for read/`has()`. (Only `StreamPage` constructs the detail view — verified — so no other caller to update.)
- [ ] **Step 2: Routed add/remove (R1#3)** — `m_library->add` (`:2342`,`:2406`) → `services->addClassified(entry)`; `m_library->remove` (`:2392`) → `services->removeEverywhere(...)`. When the routed mode ≠ this page's mode (anime opened from TV), show the D9 one-line note.
- [ ] **Step 3: Episode label** — `"Ep N"` (anime) vs `"S{n}E{m}"` (TV) at `:1216`, by `m_mode == StreamMode::Anime`. Season combo hidden for Anime (already) **and** Movies; shown for TV.
- [ ] **Step 4: Movies** (and anime films, D10) — single-file affordance, no episode list.
- [ ] **Step 5: Sub/dub (D8)** — Anime-only SUB/DUB toggle that sorts already-fetched sources by filename tag; no new fetch.
- [ ] **Step 6: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): detail view services+mode, routed add, anime extras, movie/TV gating"`

---

## Phase 7 — MainWindow nav wiring (SHARED FILE — append-only, AFTER Agent 1)

> **COORDINATION:** `src/ui/MainWindow.cpp` is edited by both arcs.
> 1. **Land Agent 1's Phase 4 first**, then sync.
> 2. **Preflight (R1#9):** confirm the *actual* landed contract — `PAGE_MANGA`/`PAGE_COMICS` values; each comics page's `setObjectName` (`"manga"`, `"western_comics"` vs mode page-id `"comics"`); the `activatePage`/`resetActivePageToRoot`/`onLayerRestoreRequested` branches; the `bindShortcuts` table.
> 3. **navDefs line-disjoint:** replace only the trailing `{ PAGE_STREAM, "Theatre" }` → three entries → `Manga, Comics, Books, Anime, TV, Movies`. Agree order + Ctrl-map with Agent 1 first.
> 4. **Append, don't reformat.** Post a BUILD-LANE claim in chat.md first.

### Task 13: Constants, pills, page stack (3× over shared services), dispatch, boot-scanner, dev-control, keybinds

**Files:** Modify `src/ui/MainWindow.{cpp,h}`.

- [ ] **Step 1: Constants (≈:63-75)** — add `PAGE_ANIME="anime"`, `PAGE_TV="tv"`, `PAGE_MOVIES="movies"` as a contiguous block after `PAGE_BOOKS`. `PAGE_STREAM` retires as a pill (keep the string if the downloads page still references it).
- [ ] **Step 2: navDefs (≈:531-542)** — replace trailing `{ PAGE_STREAM, "Theatre" }` with `{PAGE_ANIME,"Anime"},{PAGE_TV,"TV"},{PAGE_MOVIES,"Movies"}`.
- [ ] **Step 3: buildPageStack (≈:838-921)** — construct ONE `VideoModeServices(m_bridge, store)`; `services->setStreamDownloadIndex(m_streamDownloadIndex)` + `setTorrentClient(torrentClient)`. Construct three `StreamPage(m_bridge, torrentClient, mode, services, ...)` (B1 — TorrentClient passed); add each to `m_pageStack`; wire each `enteredLayer`/`exitedLayer` with its own pageId. Replace `m_streamPage` with `m_animePage`/`m_tvPage`/`m_moviesPage` (MainWindow.h) + add `StreamPage* pageForVideoMode(const QString& pageId) const`. Hold `m_videoServices` for the shared accessors.
- [ ] **Step 4: VideosPage meta source (≈:870)** — `m_videosPage->setMetaAggregator(services->metaAggregator())`.
- [ ] **Step 5: PerModeNav roots** — `setRootLayer("anime"|"tv"|"movies", { pageId, "library", "Library" })`.
- [ ] **Step 6: activatePage (≈:1039-1098)** — objectName loop finds the pages; extend the `qobject_cast<StreamPage*>`+`activate()` via `pageForVideoMode(pageId)`; extend the Organise-button (≈:1062) + sidebar-downloads (≈:1087-1093) conditionals for `anime/tv/movies`. **Do NOT (re)connect the shared player-controller signals here** — controller signal ownership is the stream-OWNER's, managed by `beginStreamSession`/`endStreamSession` on play-start/stop (D12/R3, Tasks 9-10), independent of which page is active.
- [ ] **Step 7: resetActivePageToRoot (≈:1111-1125) + onLayerRestoreRequested (≈:1183-1208)** — replace the single `pageId == "stream"` branch (`:1189`) with `pageForVideoMode(pageId)` for all three; append after Agent 1's branches.
- [ ] **Step 8: Boot rescue scanner (≈:299-336) — R2#B3 + R3#S.** This block references `m_streamPage->streamLibrary()/metaAggregator()`. For the split, make the `StreamRescueScanner` **index-only — do NOT disable it** (R3: disabling would leave unindexed on-disk downloads unplayable). Concretely: it still walks Videos roots + registers `StreamDownloadIndex` rows (so legacy downloads resolve + play disk-first), but it **must not materialize any `StreamLibrary` entry** into a mode library (pass a null/no-op library, or split the scanner's index-registration from its library-materialization and call only the former). Use `services->metaAggregator()` for the meta source; pin `migrationVersion` so it doesn't re-run. The three libraries stay empty even with existing downloads (Task 14/15).
- [ ] **Step 9: Enumerate + update ALL remaining `m_streamPage` refs (R1#5)** — dev-control library snapshot (≈:318-324, :2124-2127), dev search/dispatch (≈:2177-2206), dev snapshot (≈:2340-2342, :2387-2388), mode→page maps (≈:2457, :2550), `stream_`-command forwarding (≈:2592). Route each via `pageForVideoMode(pageId)`; **add dev-control modes `anime`/`tv`/`movies`** (D11); keep `stream`/`stream_*` aliased → `m_moviesPage`. Grep confirms zero dangling `m_streamPage`.
- [ ] **Step 10: StreamDownloadsPage back-target (D3, ≈:928-944)** — record the launching video mode on open; `backRequested` → `activatePage(<launching mode>)` (default `PAGE_MOVIES`).
- [ ] **Step 11: bindShortcuts (≈:985-1004) — jointly agreed** — `Ctrl+4`=Anime, `Ctrl+5`=TV, `Ctrl+6`=Movies; sidebar → `Ctrl+0`. (No-op verify if Agent 1 already added these.)
- [ ] **Step 12: Build-verify** — `build_check.bat` → BUILD OK; each new page `.obj` built; exe mtime advanced; grep → zero stale `m_streamPage`.
- [ ] **Step 13: Commit (small) + push** — `git commit -m "feat(video-split): Anime+TV+Movies top-level modes over shared services; retire Theatre pill; dev-control modes; index-only boot scanner"` then push.

---

## Phase 8 — Migration guard + verification

### Task 14: Start-fresh guard (video side) — R2#B3

- [ ] **Step 1:** Confirm the three pages read only `anime_library.json`/`tv_library.json`/`movies_library.json` (empty on first run); nothing reads `stream_library.json` into a new page; leave `stream_library.json` on disk.
- [ ] **Step 2:** Confirm the boot `StreamRescueScanner` (Task 13 step 8) **does not materialize** into any mode library; `StreamDownloadIndex` legacy rows still resolve for playback (disk-first); downloaded files untouched.
- [ ] **Step 3: Empty-library smoke (with existing downloads).** With real downloaded video files on disk + a pre-split `stream_library.json` present, first boot of the split → `anime_library.json`/`tv_library.json`/`movies_library.json` are **absent or empty**. (Headless: check the three files via `tankoctl`/filesystem.)
- [ ] **Step 4:** Add a one-time first-run note (Hemanth-language): "Your Anime / TV / Movies libraries start fresh — your downloaded files are safe on disk."
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): start-fresh guard (no boot materialization) + first-run note"`

### Task 15: Build + classification smoke + cross-model review

- [ ] **Step 1: Build-verify (false-green guard)** — `build_check.bat` → BUILD OK; confirm `.obj` for `StreamMode.cpp`, `StreamLibraryCodec.cpp`, `VideoModeServices.cpp`, new TUs.
- [ ] **Step 2: Headless** — `build_and_run.bat`; `out\tankoctl.exe ping` + `introspect-tree`/`get-modes`: six pills; dev-control responds for `anime`/`tv`/`movies`; per-mode library snapshots return.
- [ ] **Step 3: One subprocess (R2#B2)** — with all three video pages visited, confirm exactly **one** `stremio-runtime.exe` (or the stream-server) process is running (Task Manager / `tasklist`), not three.
- [ ] **Step 4: Classification smoke (DoD)** — Demon Slayer (anime series) → Anime; an anime film (*A Silent Voice*/*Your Name*) → Anime **and produces playable sources via the movie path** (D10); Breaking Bad → TV; Inception → Movies. No cross-mode bleed in libraries/Continue; pill-from-deep-view resets to mode root; anime detail shows `Ep N` + sub/dub; TV shows seasons; Movies single-file; opening an anime title from the TV grid routes to Anime with the note (D9).
- [ ] **Step 5: Pre-warm smoke** — cold start, re-open a previously-added anime; the Task-7 log marker shows the kitsu route built from the stored id, no live re-fetch.
- [ ] **Step 6: Start-fresh + disk-first smoke** — Task 14 step 3 (three libraries empty on first boot with existing downloads on disk) **AND** (R3) a rescued/index-only `StreamDownloadIndex` row for an existing on-disk download still **plays disk-first** even though no mode library lists it.
- [ ] **Step 7: Regression** — Books unchanged; comic reader unchanged; play a movie + a TV episode end-to-end; torrent engine unchanged.
- [ ] **Step 8: Cross-model review (producer ≠ reviewer)** — `/codex-review` (or `codex exec`) the full diff against this DoD. Address every NOT-MET.
- [ ] **Step 9: RTC + close** — contracts-v3 RTC to `agents/chat.md` (`Done-when:` = DoD) with `Skills invoked:` provenance. Hemanth's smoke is the final gate.

---

## Definition of Done

- **Six mode pills** (`Manga · Comics · Books · Anime · TV · Movies`); each its own page + per-mode back-stack.
- **Correct classification:** every anime title (series **or** film) → Anime; non-anime series → TV; non-anime films → Movies — verified on Demon Slayer + an anime film + Breaking Bad + Inception. Anime opened from a TV/Movies grid routes to Anime (note shown).
- **Anime extras:** sub/dub preference + `Ep N` absolute numbering + flat episode list; TV seasons/episodes; Movies (and anime films) single-file.
- **One shared engine + one Stremio subprocess** behind all three pages (not 3×); `VideosPage` meta sourced from the same `VideoModeServices`.
- Each mode: **own catalog brain** (Anime: Kitsu/AniList + Amatsu/Nyaa; TV/Movies: Cinemeta) + **own library + Continue strip**; no cross-mode bleed; best-effort anime exclusion from TV/Movies grids.
- **Persisted discriminator:** `animeFlag` + resolved `kitsuId` survive restart; re-opening an anime series builds the kitsu route with no live re-fetch.
- **Start fresh:** three new libraries begin empty even with existing downloads on disk; old `stream_library.json` left in place; downloaded files untouched; legacy downloads still play; the boot rescue scanner does not materialize into a mode library.
- **No regression:** Books, comic reader, video player, torrent engine behave as before.
- `build_check.bat` green; unit tests pass (StreamMode, StreamLibraryCodec, UnifiedProgressStore progress prefixes, StreamDownloadIndex animeFlag); `/codex-review` APPROVE; **Hemanth smoke passes** (the only done-gate).

---

## Self-Review (author pass, v3)

- **All 9 round-1 findings RESOLVED; 3 round-2 blockers + 1 SHOULD fixed; 1 round-3 blocker + 1 SHOULD fixed** (see Revision Logs): B1→D1/Task 10 (ctor keeps TorrentClient); B2→D12/Task 9 (engine+subprocess+controller hoisted, single, lazy); B3→Task 13 step 8 + Task 14 (boot scanner index-only, empty-library smoke); S1→corrected CMake files (`cmake/TankobanSources.cmake`/`cmake/TankobanTests.cmake`), test paths (`tests/core/stream/`), dep-free `StreamLibraryCodec` so codec tests link without `TorrentClient`. **Round 3:** controller signals route to the stream-OWNER via `beginStreamSession`/`endStreamSession` (not the active page) — Tasks 9/10, D12; boot scanner is index-only (disk-first playback preserved) — Task 13 step 8 + Task 15 step 6.
- **Spec coverage (§5/§6/§9/§10):** classification → Tasks 1, 6; Anime catalog + extras → 8, 11, 12; TV/Movies Cinemeta → 8; per-mode storage → 4, 5, 9, 11; pre-warm → 7; nav → 13; migration → 13/14; §10 DoD → 13 + 15.
- **Compile-at-every-checkpoint:** legacy `StreamPage` ctor holds MainWindow building until Task 13; each task's build-verify is independently green; tests link only dep-free units (`StreamMode`, `StreamLibraryCodec`, `UnifiedProgressStore`) — no libtorrent in the test graph.
- **Test honesty:** pure logic is unit-tested (classifier, codec, progress prefixes, download-index flag); integration (hub routing, 3 pages, single subprocess, start-fresh, nav) is smoke-tested (Task 15) — matching the repo's existing stream-test pattern.
- **Type consistency:** `classifyStreamMode`, `isAnimeTitle`, `streamModeKey/FromKey/streamLibraryFilename`, `StreamMode{Anime,TV,Movies}`, `streamLibraryEntryToJson/FromJson`, `VideoModeServices::{addonRegistry,metaAggregator,streamAggregator,streamEngine,playerController,downloadIndex,torrentClient,libraryForMode,addClassified,removeEverywhere,cacheIsAnime,cachedIsAnime}`, `StreamLibraryEntry::{animeFlag,kitsuId}`, `MetaAggregator::entryResolved(imdb,kitsuId,isAnime)`, `pageForVideoMode(pageId)` — consistent across tasks.
- **Shared-file safety:** Phase 7 gated behind Agent 1 + a preflight against the actual landed contract + append-only edits + agreed pill order + a chat.md BUILD-LANE claim.
