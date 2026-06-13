# Video Split (Anime + TV + Movies) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> This is **Arc 2** of the six-mode restructure (spec: `docs/superpowers/specs/2026-06-07-six-mode-restructure-design.md`, §5). Arc 1 (Comics → Manga + Western) is a separate, already-written plan: `docs/superpowers/plans/2026-06-07-comics-split-manga-western.md`. The two arcs share **one file** (`src/ui/MainWindow.cpp`) — see Phase 7 coordination.
> **This is v2** — revised after a cross-model (Codex) plan review. See the **v2 Revision Log** below for what changed and why.

**Goal:** Split the single "Theatre" video mode into three top-level modes — **Anime** (all Japanese animation: series *and* films), **TV** (non-anime series), **Movies** (non-anime films) — each with its own page, library, Continue strip, and tailored catalog, over the **shared** video engine (libtorrent + addons + player + `StreamDownloadIndex` + `MetaAggregator`).

**Architecture:** "Split the faces, share the engine." The engine already computes the classification inputs (`type` = movie/series; anime detection via `AnimeCatalogResolver::isAnimeSeries(genres,country)`). The work is: (1) make the anime discriminator **persistent** (currently transient) + classify at the **moment full meta resolves** (genres/country are not on catalog previews), (2) **decouple mode classification from the 5-season Kitsu-reroute gate** so short anime series + anime films classify correctly, (3) **hoist the shared engine** (`MetaAggregator`/`StreamAggregator`/`AddonRegistry`) out of `StreamPage` into an injected `VideoModeServices` so three pages share one engine (not 3×), (4) instantiate the now-mode-parameterized `StreamPage` **three times**, and (5) wire three top-level pills. **Start fresh** on video content (new libraries begin empty; old `stream_library.json` left untouched).

**Tech Stack:** C++17, Qt 6 (Widgets), CMake + ninja, GoogleTest. Build: `build_check.bat` (compile-verify) / `build_and_run.bat` (Hemanth smoke). Dev-control bridge (`out\tankoctl.exe`) for headless state checks.

**Migration:** Video side = **start fresh** (spec §6). The three new pages use **new filenames** (`anime_library.json` / `tv_library.json` / `movies_library.json`), so they start empty automatically; the old `stream_library.json` is never read by the new pages and is **left in place, not rebucketed**. Downloaded files on disk are untouched; legacy `StreamDownloadIndex` rows still resolve for playback (disk-first).

---

## v2 Revision Log (response to Codex plan review, all 9 findings)

The cross-model review returned **CHANGES-NEEDED**. Every finding was verified against the real code and is addressed here (no pushback — all were valid). Verified facts that drove the changes:

1. **[BLOCKER → fixed] Compile sequencing.** `MainWindow.cpp:840` constructs `new StreamPage(m_bridge, torrentClient)`. → **Task 10 keeps a legacy ctor overload** (defaults to Movies mode + self-owned services) so every checkpoint compiles; **Task 13** switches to the 3-instance injected wiring and removes the legacy ctor.
2. **[BLOCKER → fixed] Add-time classification data.** Verified: `MetaItemPreview` *has* `genres`/`country` fields (`MetaItem.h:67-68`) but **catalog/search parsing does not populate them** — they arrive only with full meta (`onMetaItemReady`). → **Task 6** classifies from the **resolved full meta**, and if Add is clicked before meta resolves, **defers the add** until `onMetaItemReady`. Test added for "Add before meta ready."
3. **[BLOCKER → fixed] Detail-view add routing.** Verified: `StreamDetailView` calls `m_library->add/remove` directly (`:2342/:2392/:2406`). → The detail view takes `VideoModeServices*` and routes add/remove through it (`addClassified`/`removeEverywhere`) — **Tasks 9, 10, 12**.
4. **[BLOCKER → fixed] Progress not fully generalized.** Verified more sites than v1 listed: `parseStreamProgressKey()`, `clearProgress()` stream-only branch, and `StreamContinueStrip` `startsWith("stream:")` at multiple lines. → **Task 5 + Task 11** enumerate all of them.
5. **[BLOCKER → fixed] Single-`m_streamPage` assumption (37 refs) + engine ownership.** Verified: `StreamPage` *owns* `m_metaAggregator`/`m_streamAggregator`/`m_addonRegistry` (`StreamPage.cpp:306-307`); `VideosPage` borrows the meta via `m_streamPage->metaAggregator()` (`:870`); 37 `m_streamPage` refs across dev-control routing/snapshots/command-forwarding. → New **Task 9 `VideoModeServices`** owns the hoisted shared engine + 3 libraries + classification cache; **Task 13** adds `pageForVideoMode()`, enumerates all 37 refs, points VideosPage at `services->metaAggregator()`, and extends dev-control to `anime/tv/movies`.
6. **[BLOCKER → fixed] Cinemeta can return anime.** → **Task 8**: mode-scoped catalogs are best-effort; **classification (add/open-time) is the authoritative landing guarantee**, plus a classification cache that filters known-anime from TV/Movies grids, plus a **redirect-on-open** backstop with a visible note. The DoD's "anime lands in Anime" is met by routing, not by perfect grid filtering.
7. **[SHOULD → fixed] Country matching.** `isAnimeSeries` does exact `country == "Japan"`. → **Task 6** normalizes country tokens (`Japan`/`JP`/comma-or-slash lists); film + multi-country tests added.
8. **[SHOULD → fixed] Anime-film play path.** Verified: kitsu route is series-only (`StreamPage.cpp:2428` skips it for `movie`). → **Task 7 + D10**: anime films classify to **Anime** (home/library) but **play via the standard movie path** (IMDb→Torrentio). Task 15 smokes an anime film.
9. **[SHOULD → fixed] Shared-file preflight.** Agent 1 uses objectName `"western_comics"` vs mode page-id `"comics"`; `activatePage` matches by objectName. → **Task 13** opens with a preflight checklist verifying the *actual* post-Agent-1 page-ids/objectNames/dispatch branches, not just "MangaPage present."

---

## Phase 0 — Locked Decisions (design gate, no code)

Technical decisions (Rule 14, producer's call); product decisions were locked in the spec brainstorm.

- **D1 — Page-shell model: ONE `StreamPage` class, parameterized by `StreamMode`, instantiated 3×** (objectName `"anime"`/`"tv"`/`"movies"`). The shared engine is **hoisted** out of `StreamPage` into `VideoModeServices` (D7) and injected, so 3 pages share **one** `MetaAggregator`/`StreamAggregator`/`AddonRegistry`/`StreamDownloadIndex`. **Heaviness caveat:** each instance defers heavy work (catalog fetch + detail-view construction/population) to `activate()` / first-show, never the ctor — the app already has open idle/startup-cost tickets in this domain.
- **D2 — Detail view: parameterize the existing `StreamDetailView`** (already branches on `m_currentType` + `m_isAnime`); each page owns its own instance (lazily built), fed by the shared engine via services. No fork.
- **D3 — Theatre pill retired.** `PAGE_STREAM = "stream"` removed as a pill. The shared read-only downloads page (`PAGE_STREAM_DOWNLOADS`) is kept once, shared by all three modes; its back-target becomes the launching video mode (default Movies).
- **D4 — Vestigial `VideosPage` (`PAGE_VIDEOS`, Ctrl+3): DEFERRED, untouched** (spec §7), but its `MetaAggregator` source moves from `m_streamPage->metaAggregator()` to `services->metaAggregator()` (Task 13).
- **D5 — Keybinds (SHARED with Agent 1): order `Manga · Comics · Books · Anime · TV · Movies`** → `Ctrl+1..6`; sidebar toggle moves off `Ctrl+5` to **`Ctrl+0`**. One table, edited once, jointly agreed.
- **D6 — Classification is data-driven, decoupled from the reroute gate, and evaluated when full meta resolves.** Mode = `classifyStreamMode(isAnime, type)`, `isAnime = isAnimeTitle(genres, country)` (Animation genre AND normalized-Japan country; works for films). The `seasons.size() >= 5` gate (`MetaAggregator.cpp:483`) stays *only* on the Kitsu episode-list reroute, never on mode assignment.
- **D7 — `VideoModeServices`** (new) owns the **shared engine** (`AddonRegistry`, `MetaAggregator`, `StreamAggregator`) + the **three `StreamLibrary` instances** + an **anime-classification cache** (`imdb → {isAnime, kitsuId}`). It exposes the engine to all 3 pages and `VideosPage`, routes adds by classification, and scopes cross-mode removal. Constructed once in `MainWindow::buildPageStack`.
- **D8 — Sub/dub (v1): a SUB/DUB preference on the Anime detail** that prioritizes release results by filename tag (`[SUB]`/`[DUB]`/`Dual`/`Dual-Audio`, case-insensitive). No new fetch, no metadata dependency. Play-time mpv track-switching (Agent 3) is out of scope.
- **D9 — Catalog cleanliness is best-effort; landing is guaranteed.** TV/Movies catalog/search may transiently surface an anime title (Cinemeta returns some, and previews lack genres/country). The **authoritative** guarantee is at add/open-time classification → routing to the right library/Continue. A classification cache filters *known* anime from TV/Movies grids; opening an anime title from a TV/Movies grid **redirects** it (adds land in Anime) with a visible one-line note.
- **D10 — Anime films play via the movie path.** Anime films classify to **Anime** for home/library/Continue, but their source/play path is the standard IMDb-movie → Torrentio route (the `kitsu:<id>:<ep>` route is episode-based and already skipped for `type == "movie"`). No episode UI for anime films.
- **D11 — Dev-control gains `anime`/`tv`/`movies` modes.** The legacy `stream` / `stream_*` tankoctl commands map to a default video page (Movies) for back-compat; new commands accept the mode page-id. (Agent 4 owns the `stream-*` prefix; extend it.)

---

## File Structure

**Create:**
- `src/core/stream/StreamMode.h` (+ `.cpp`) — `enum class StreamMode { Anime, TV, Movies }`; pure `classifyStreamMode(bool isAnime, const QString& type)`; `isAnimeTitle(const QStringList& genres, const QString& country)` (normalized); `streamModeKey()` / `streamModeFromKey()` / `streamLibraryFilename()`.
- `src/core/stream/VideoModeServices.{h,cpp}` — owns the hoisted shared engine (`AddonRegistry`, `MetaAggregator`, `StreamAggregator`) + 3 `StreamLibrary` + classification cache; `metaAggregator()/streamAggregator()/addonRegistry()/downloadIndex()`, `libraryForMode()`, `addClassified()`, `removeEverywhere()`, `cacheIsAnime()/cachedIsAnime()`.
- `tests/stream/test_stream_mode.cpp`, `tests/stream/test_stream_library_anime_flag.cpp`, `tests/stream/test_video_mode_services.cpp`, `tests/stream/test_progress_domains.cpp`.

**Modify:**
- `src/core/stream/StreamLibrary.{h,cpp}` — `animeFlag` + `kitsuId` on the entry + (de)serialize; ctor filename param.
- `src/core/stream/StreamDownloadIndex.{h,cpp}` — `animeFlag` on `Entry` + codec (back-compat default).
- `src/core/CoreBridge.{h,cpp}` — `anime`/`tv`/`movies` progress domains across `PROGRESS_FILES`, `allProgress`, `progress`, `saveProgress`, `clearProgress`, and `parseStreamProgressKey`.
- `src/core/stream/UnifiedProgressStore.{h,cpp}` — domain-prefix-parameterized key build/parse + payload accessor.
- `src/core/stream/MetaAggregator.{h,cpp}` — `entryResolved(imdbId, kitsuId, isAnime)` signal; mode-scoped catalog requests; classification cache feed; decouple classification from the reroute gate.
- `src/core/stream/CatalogAggregator.{h,cpp}`, `src/core/stream/StreamAggregator.{h,cpp}`, `src/ui/pages/stream/CatalogBrowseScreen.cpp` — thread a `StreamMode` filter + best-effort anime exclusion.
- `src/core/stream/AnimeCatalogResolver.{h,cpp}` — expose/normalize the anime-title test.
- `src/ui/pages/StreamPage.{h,cpp}` — mode + injected `VideoModeServices`; legacy ctor kept until Task 13; defer heavy work.
- `src/ui/pages/stream/StreamContinueStrip.{h,cpp}` — domain string via ctor/setter (replaces hardcoded `"stream"` at `:79`, `:116`, `:275`).
- `src/ui/pages/stream/StreamSearchWidget.{h,cpp}` — mode-filtered sections + anime section.
- `src/ui/pages/stream/StreamDetailView.{h,cpp}` — take `VideoModeServices*` + mode; route add/remove through services; per-mode formatting + sub/dub.
- `src/ui/MainWindow.{cpp,h}` — **SHARED FILE** — constants, navDefs, `buildPageStack`, `activatePage`, `resetActivePageToRoot`, `onLayerRestoreRequested`, `bindShortcuts`, all 37 `m_streamPage` refs, dev-control modes.
- `CMakeLists.txt` — register new files.

**Pre-task read (executor):** before Phase 3/5/6, read `src/ui/pages/StreamPage.cpp` and `src/ui/pages/stream/StreamDetailView.cpp` **in full** (StreamDetailView ≈ 4666 lines). Tasks below say *what to change and where (anchors)*; reproduce the surrounding code from the real file. Do not invent unread line bodies.

---

## Phase 1 — Classification core + persistent discriminator (pure logic, TDD)

### Task 1: `StreamMode` enum + pure classifier + normalized anime-title test

**Files:** Create `src/core/stream/StreamMode.{h,cpp}`; Test `tests/stream/test_stream_mode.cpp`; Modify `CMakeLists.txt`.

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "core/stream/StreamMode.h"

TEST(StreamMode, AnimeFlagWinsForBothTypes) {
    EXPECT_EQ(classifyStreamMode(true,  "series"), StreamMode::Anime);
    EXPECT_EQ(classifyStreamMode(true,  "movie"),  StreamMode::Anime);  // anime film
}
TEST(StreamMode, NonAnimeSeriesIsTv) {
    EXPECT_EQ(classifyStreamMode(false, "series"), StreamMode::TV);
}
TEST(StreamMode, NonAnimeMovieIsMovies) {
    EXPECT_EQ(classifyStreamMode(false, "movie"), StreamMode::Movies);
    EXPECT_EQ(classifyStreamMode(false, ""),      StreamMode::Movies);
}
TEST(StreamMode, AnimeTitleNormalizesCountryAndGenre) {
    EXPECT_TRUE (isAnimeTitle({"Animation","Action"}, "Japan"));
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "JP"));            // alt token
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "Japan, China")); // multi-country list
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "Japan / USA"));  // slash list
    EXPECT_FALSE(isAnimeTitle({"Animation"},          "United States"));// Western cartoon
    EXPECT_FALSE(isAnimeTitle({"Drama"},              "Japan"));        // live-action JP
    EXPECT_FALSE(isAnimeTitle({},                      "Japan"));        // no genre
}
TEST(StreamMode, KeyAndFilenameRoundTrip) {
    EXPECT_EQ(streamModeKey(StreamMode::Anime),  QStringLiteral("anime"));
    EXPECT_EQ(streamModeFromKey("tv"),           StreamMode::TV);
    EXPECT_EQ(streamModeFromKey("bogus"),        StreamMode::Movies);  // safe default
    EXPECT_EQ(streamLibraryFilename(StreamMode::Movies), QStringLiteral("movies_library.json"));
}
```

- [ ] **Step 2: Run it, verify it fails** — `ctest -R StreamMode -V` → FAIL (header not found).

- [ ] **Step 3: Implement `StreamMode.h`**

```cpp
#pragma once
#include <QString>
#include <QStringList>

// Six-mode restructure (2026-06-07), Arc 2. anime-flag wins for films AND series;
// otherwise series -> TV, movie/unknown -> Movies.
enum class StreamMode { Anime, TV, Movies };

inline StreamMode classifyStreamMode(bool isAnime, const QString& type) {
    if (isAnime) return StreamMode::Anime;
    if (type.compare(QStringLiteral("series"), Qt::CaseInsensitive) == 0)
        return StreamMode::TV;
    return StreamMode::Movies;
}

// Animation genre AND a Japan-origin country token. country is normalized so
// "Japan", "JP", and lists like "Japan, China" / "Japan / USA" all match.
bool       isAnimeTitle(const QStringList& genres, const QString& country);
QString    streamModeKey(StreamMode mode);
StreamMode streamModeFromKey(const QString& key);
QString    streamLibraryFilename(StreamMode mode);
```

- [ ] **Step 4: Implement `StreamMode.cpp`**

```cpp
#include "core/stream/StreamMode.h"

bool isAnimeTitle(const QStringList& genres, const QString& country) {
    bool hasAnimation = false;
    for (const QString& g : genres)
        if (g.compare(QStringLiteral("Animation"), Qt::CaseInsensitive) == 0) { hasAnimation = true; break; }
    if (!hasAnimation) return false;
    // Split on comma/slash, trim, match Japan tokens.
    const QStringList toks = country.split(QRegularExpression(QStringLiteral("[,/]")),
                                           Qt::SkipEmptyParts);
    for (QString t : toks) {
        t = t.trimmed();
        if (t.compare(QStringLiteral("Japan"), Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("JP"),    Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("JPN"),   Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString streamModeKey(StreamMode mode) {
    switch (mode) {
        case StreamMode::Anime:  return QStringLiteral("anime");
        case StreamMode::TV:     return QStringLiteral("tv");
        case StreamMode::Movies: return QStringLiteral("movies");
    }
    return QStringLiteral("movies");
}
StreamMode streamModeFromKey(const QString& key) {
    const QString k = key.toLower();
    if (k == QStringLiteral("anime")) return StreamMode::Anime;
    if (k == QStringLiteral("tv"))    return StreamMode::TV;
    return StreamMode::Movies;
}
QString streamLibraryFilename(StreamMode mode) {
    return streamModeKey(mode) + QStringLiteral("_library.json");
}
```

(Add `#include <QRegularExpression>` to the .cpp.)

- [ ] **Step 5:** Add `StreamMode.cpp` to CMake sources + `test_stream_mode.cpp` to test sources.
- [ ] **Step 6: Run, verify PASS** — `ctest -R StreamMode -V` → PASS. Confirm `StreamMode.cpp.obj` built.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): StreamMode enum + classifier + normalized isAnimeTitle"`

### Task 2: Persist `animeFlag` + `kitsuId` on `StreamLibraryEntry`

**Files:** Modify `src/core/stream/StreamLibrary.{h,cpp}`; Test `tests/stream/test_stream_library_anime_flag.cpp`.

- [ ] **Step 1: Write the failing test** (round-trip + legacy default)

```cpp
#include <gtest/gtest.h>
#include <QJsonObject>
#include "core/stream/StreamLibrary.h"

TEST(StreamLibraryAnimeFlag, RoundTripsNewFields) {
    StreamLibraryEntry e;
    e.imdb = "tt9335498"; e.type = "series"; e.name = "Demon Slayer";
    e.animeFlag = true;   e.kitsuId = 41370;
    const QJsonObject j = StreamLibrary::entryToJson(e);
    const StreamLibraryEntry back = StreamLibrary::entryFromJson(j);
    EXPECT_TRUE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, 41370);
}
TEST(StreamLibraryAnimeFlag, LegacyRowsDefaultNonAnime) {
    QJsonObject legacy;
    legacy["imdb"] = "tt0111161"; legacy["type"] = "movie"; legacy["name"] = "X";
    const StreamLibraryEntry back = StreamLibrary::entryFromJson(legacy);
    EXPECT_FALSE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, -1);
}
```

- [ ] **Step 2:** Run, verify FAIL.
- [ ] **Step 3:** Extend the struct (`StreamLibrary.h:13-22`) with `bool animeFlag = false; int kitsuId = -1;`. Add public seam wrappers `static StreamLibraryEntry entryFromJson(const QJsonObject&)` / `static QJsonObject entryToJson(const StreamLibraryEntry&)` delegating to the private `fromJson`/`toJson`. In `StreamLibrary.cpp`, write both fields in `toJson` and read them with struct defaults in `fromJson` (`obj.value("animeFlag").toBool(false)`, `obj.value("kitsuId").toInt(-1)`).
- [ ] **Step 4:** Run, verify PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): persist animeFlag + kitsuId on StreamLibraryEntry"`

### Task 3: `animeFlag` on `StreamDownloadIndex::Entry`

**Files:** Modify `src/core/stream/StreamDownloadIndex.{h,cpp}`; extend the same test file.

- [ ] **Step 1:** Read `StreamDownloadIndex.h` for the `Entry` struct + codec. Write a failing round-trip test (`animeFlag = true` survives; legacy row → `false`).
- [ ] **Step 2:** Run, verify FAIL.
- [ ] **Step 3:** Add `bool animeFlag = false;` to `Entry`; serialize in the codec; default `false` on read. Match existing field/codec naming (read first).
- [ ] **Step 4:** Run, verify PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): animeFlag on StreamDownloadIndex::Entry (back-compat)"`

---

## Phase 2 — Storage parameterization

### Task 4: Parameterize `StreamLibrary` filename

**Files:** Modify `src/core/stream/StreamLibrary.{h,cpp}` (ctor; `FILENAME` :77 → member; `load()` :150; `save()` :173); `src/ui/pages/StreamPage.cpp` (the existing call site — note: `m_library` is created inside StreamPage; find it, ≈ where `new StreamLibrary` appears). Extend `tests/stream/test_stream_library_anime_flag.cpp`.

- [ ] **Step 1: Write the failing test** — two libraries with different filenames stay isolated:

```cpp
TEST(StreamLibraryFilename, TwoInstancesAreIsolated) {
    JsonStore store(/* temp dir per JsonStore test pattern — read JsonStore.h */);
    StreamLibrary a(&store, QStringLiteral("anime_library.json"));
    StreamLibrary t(&store, QStringLiteral("tv_library.json"));
    StreamLibraryEntry e; e.imdb = "tt9335498"; e.type = "series"; e.animeFlag = true;
    a.add(e);
    EXPECT_TRUE (a.has("tt9335498"));
    EXPECT_FALSE(t.has("tt9335498"));
}
```

- [ ] **Step 2:** Run, verify FAIL (ctor signature).
- [ ] **Step 3:** Change ctor to `StreamLibrary(JsonStore* store, const QString& filename, QObject* parent = nullptr)`; replace `static constexpr const char* FILENAME` with `const QString m_filename;` set in the init list; use `m_filename` in `load()`/`save()`.
- [ ] **Step 4:** Update the existing call site in `StreamPage.cpp` to pass `QStringLiteral("stream_library.json")` (no behavior change; replaced in Phase 5).
- [ ] **Step 5: Build-verify** — `build_check.bat` → BUILD OK (verify exe mtime). `ctest -R StreamLibraryFilename -V` → PASS.
- [ ] **Step 6: Commit** — `git commit -m "refactor(video-split): StreamLibrary takes a filename"`

### Task 5: `anime`/`tv`/`movies` progress domains (FULL generalization)

**Files:** Modify `src/core/CoreBridge.{h,cpp}` — `PROGRESS_FILES` (≈ :27-31), `allProgress` (≈ :188), `progress` (≈ :263-274), `saveProgress` (≈ :227), `clearProgress` (the `stream`-only unified branch), and **`parseStreamProgressKey()`** (≈ :44, currently accepts only `stream:`). Modify `src/core/stream/UnifiedProgressStore.{h,cpp}` — `allEpisodePayloadsForStreamDomain()` (≈ :84) + `streamDomainKeyForEntry()` (≈ :256). Test `tests/stream/test_progress_domains.cpp`.

> **Codex finding #4:** changing only `allProgress(m_domain)` leaves Continue strips broken because the **key parser** and **clearProgress** are `stream:`-only. This task makes the key prefix a parameter everywhere.

- [ ] **Step 1: Read all the sites above** to capture the exact `domain == "stream"` branch pattern + the `"stream:"` key prefix usage in `parseStreamProgressKey` + `clearProgress`.
- [ ] **Step 2: Write the failing test** — for each of `anime`/`tv`: `saveProgress(domain, payload)` → `allProgress(domain)` round-trips; the stored key is prefixed `anime:`/`tv:` (not `stream:`); `clearProgress(domain, id)` removes only that domain's entry; `parseStreamProgressKey("anime:tt123:1:2")` parses correctly.
- [ ] **Step 3:** Run, verify FAIL.
- [ ] **Step 4: Generalize.** Add `anime`/`tv`/`movies` rows to `PROGRESS_FILES`. Extend `allProgress`/`progress`/`saveProgress`/`clearProgress` branches to route the new domains to `UnifiedProgressStore`. Make `parseStreamProgressKey()` accept any of the four prefixes (or take the domain). Parameterize `streamDomainKeyForEntry()`/`allEpisodePayloadsForStreamDomain()` by domain prefix (default `"stream"` for legacy callers). **Touch every site** — a miss = silent progress loss for that mode.
- [ ] **Step 5:** Run, verify PASS; `build_check.bat` → BUILD OK.
- [ ] **Step 6: Commit** — `git commit -m "feat(video-split): full anime/tv/movies progress generalization (keys + clear + parse)"`

---

## Phase 3 — Classification wiring (resolve-time, race-safe) + play-path persistence

### Task 6: Classify at meta-resolve time, feed the cache, emit `entryResolved`

**Files:** Modify `src/core/stream/MetaAggregator.{h,cpp}` (signal near `animeCatalogActive` ≈ :100; emit in `emitSeriesResult` ≈ :609 + the movie path; decouple classification from the `:481-491` reroute gate); `src/ui/pages/stream/StreamDetailView.{cpp}` (add path ≈ :2342); `src/core/stream/AnimeCatalogResolver.{h,cpp}` (reuse `isAnimeTitle`). Test `tests/stream/test_stream_mode.cpp` (extend) / a new `test_classification.cpp`.

> **Codex finding #2 + #7:** previews lack populated genres/country; classify from the **resolved full meta** (delivered via `onMetaItemReady` to the detail view). If Add is clicked before resolution, **defer** the add.

- [ ] **Step 1: Reuse the normalized test.** Point `AnimeCatalogResolver`'s anime detection at `isAnimeTitle()` (Task 1) — one source of truth. If `isAnimeSeries` is referenced elsewhere, keep it as a thin wrapper calling `isAnimeTitle` (no behavior regression for the existing reroute path beyond country normalization, which is a strict superset).
- [ ] **Step 2: Write the failing test** — drive classification on resolved-meta shapes (Demon Slayer series → Anime; an anime film → Anime; Breaking Bad → TV; Inception → Movies), asserting `classifyStreamMode(isAnimeTitle(g,c), type)`.
- [ ] **Step 3:** Run, verify FAIL (if any wiring missing) / confirm assertions.
- [ ] **Step 4: Add `entryResolved(imdbId, kitsuId, isAnime)`** to `MetaAggregator`. Emit it from the series-result path (both the rerouted-anime and plain-series branches) and the movie-resolution path, carrying the resolved `kitsuId` (or -1) and `isAnime` from `isAnimeTitle(meta.genres, meta.country)`. **Not** gated on `seasons.size() >= 5` (the gate stays only on the Kitsu episode reroute).
- [ ] **Step 5: Race-safe add (detail view ≈ :2342).** When the user adds: build the entry, set `animeFlag`/`kitsuId` from the **resolved** meta if present; if meta is not yet resolved, **defer** the add — hook `onMetaItemReady` (or `entryResolved`) once, then build + route. Never read genres/country off the unresolved preview.
- [ ] **Step 6:** Run the classification test → PASS; `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): resolve-time anime classification + entryResolved (race-safe add)"`

### Task 7: Persist + reuse resolved `kitsuId`; anime-film play path; verify pre-warm

**Files:** Modify `src/ui/pages/StreamPage.cpp` (play-path ≈ :2428), `src/core/stream/MetaAggregator.cpp` (pre-warm ≈ :259 — log marker), library subscribes to `entryResolved`.

> **Scope note:** `AnimeIdMapCache` already persists the IMDb→Kitsu map (`AnimeIdMapCache.cpp:17`) and pre-warms at startup (`MetaAggregator.cpp:259`). Real work = persist the per-series **resolved** `kitsuId` onto the entry so a restart needs no live re-fetch. **D10:** anime *films* (`type == "movie"`) keep the **movie** play path (`:2428` already skips kitsu for movies) — they are in Anime mode but play like a movie.

- [ ] **Step 1:** Subscribe the owning library (via `VideoModeServices`, Task 9) to `MetaAggregator::entryResolved`; when the imdb is in-library, write the resolved `kitsuId` onto the stored entry (idempotent) and persist.
- [ ] **Step 2:** In the series play-path build (`StreamPage.cpp:2428`), read `entry.kitsuId` first; fall back to `m_metaAggregator->kitsuIdForSeries(imdbId)` only when stored id is `-1`. Leave the `type == "movie"` branch unchanged (anime films use the movie path, D10).
- [ ] **Step 3:** Add a one-line startup log marker in the pre-warm path (`MetaAggregator.cpp:259`) so the smoke can confirm pre-warm fired.
- [ ] **Step 4: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): reuse persisted kitsuId (series); anime films use movie path; mark prewarm"`

---

## Phase 4 — Catalog-brain mode scoping + best-effort anime exclusion

### Task 8: Mode-scoped catalog brains + classification-cache exclusion + redirect-on-open

**Files:** Modify `src/core/stream/CatalogAggregator.{h,cpp}` (`planRequests` ≈ :134, addon select ≈ :71-88), `src/core/stream/StreamAggregator.{h,cpp}` (id-prefix gate ≈ :571), `src/core/stream/MetaAggregator.cpp` (candidate-sort ≈ :326-343; seriesCache ≈ where imdb-keyed), `src/ui/pages/stream/CatalogBrowseScreen.cpp` (`rebuildSelectors` ≈ :308), `src/ui/pages/stream/StreamSearchWidget.cpp` (≈ :220-229).

> **Codex finding #6 + D9:** Cinemeta can return anime; previews lack genres/country, so grid exclusion can't be per-tile-synchronous. Landing is guaranteed by classification (Task 6); this task makes grids *best-effort* clean + adds the redirect backstop.

- [ ] **Step 1: Read the catalog request path** end-to-end (`planRequests` → `dispatchRequests` id-prefix gate → addon selection) to see how addons are chosen + where `type` threads.
- [ ] **Step 2: Thread a `StreamMode` filter** into the catalog request entry point (default = today's behavior). **Anime:** select only `kitsu`/`anilist`-prefixed addons. **TV/Movies:** select Cinemeta-family addons with `type` = `series`/`movie`, excluding anime-prefixed addons.
- [ ] **Step 3: Best-effort grid exclusion via the classification cache.** When rendering TV/Movies catalog/search results, drop any imdb that `VideoModeServices::cachedIsAnime(imdb)` returns true for (the cache is fed by Task 6 as full metas resolve). First-ever sightings (not yet cached) may pass through — acceptable per D9.
- [ ] **Step 4: Redirect-on-open backstop.** Opening a title whose resolved meta classifies as anime, from a TV/Movies page: the detail still shows, but Add routes to **Anime** (via `addClassified`, Task 9) and a one-line note appears ("Added to Anime"). Feed the cache so subsequent grids exclude it.
- [ ] **Step 5: seriesCache contamination (#6/Risk).** `m_seriesCache` is imdb-keyed (24h TTL). Ensure `entryResolved`/`animeCatalogActive` is re-emitted on a cache hit (or key by `(imdb, mode)`) so a second open still drives correct layout + cache. Pick re-emit-on-hit unless reading shows keying is cleaner.
- [ ] **Step 6: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): mode-scoped catalog brains + classification-cache exclusion + redirect-on-open"`

---

## Phase 5 — Shared services + the three pages

### Task 9: `VideoModeServices` (hoisted shared engine + 3 libraries + classification cache + add routing)

**Files:** Create `src/core/stream/VideoModeServices.{h,cpp}`; Test `tests/stream/test_video_mode_services.cpp`; Modify `CMakeLists.txt`.

> **Codex finding #5 + #3:** `StreamPage` currently *owns* `m_addonRegistry`/`m_metaAggregator`/`m_streamAggregator` (`StreamPage.cpp:306-307`); three pages would mean three engines + a broken `VideosPage` meta-borrow. Hoist the engine here; route adds here (the detail view must not call `m_library` directly).

- [ ] **Step 1: Write the failing test** — add-routing by classification + isolation + cache:

```cpp
#include <gtest/gtest.h>
#include "core/stream/VideoModeServices.h"

TEST(VideoModeServices, RoutesByClassification) {
    JsonStore store(/* temp dir */);
    VideoModeServices svc(&store, /*addonRegistry deps per ctor*/);
    StreamLibraryEntry anime; anime.imdb="tt9335498"; anime.type="series"; anime.animeFlag=true;
    StreamLibraryEntry tv;    tv.imdb="tt0903747";    tv.type="series";    tv.animeFlag=false;
    StreamLibraryEntry film;  film.imdb="tt1375666";  film.type="movie";   film.animeFlag=false;
    svc.addClassified(anime); svc.addClassified(tv); svc.addClassified(film);
    EXPECT_TRUE(svc.libraryForMode(StreamMode::Anime )->has("tt9335498"));
    EXPECT_TRUE(svc.libraryForMode(StreamMode::TV    )->has("tt0903747"));
    EXPECT_TRUE(svc.libraryForMode(StreamMode::Movies)->has("tt1375666"));
    EXPECT_FALSE(svc.libraryForMode(StreamMode::TV)->has("tt9335498"));
}
TEST(VideoModeServices, ClassificationCacheRoundTrips) {
    JsonStore store(/* temp dir */);
    VideoModeServices svc(&store);
    svc.cacheIsAnime("tt9335498", true, 41370);
    EXPECT_TRUE(svc.cachedIsAnime("tt9335498"));
    EXPECT_FALSE(svc.cachedIsAnime("tt0000000"));   // unknown
}
TEST(VideoModeServices, RemoveEverywhereScopes) {
    JsonStore store(/* temp dir */);
    VideoModeServices svc(&store);
    StreamLibraryEntry tv; tv.imdb="tt0903747"; tv.type="series"; tv.animeFlag=false;
    svc.addClassified(tv);
    svc.removeEverywhere("tt0903747");
    EXPECT_FALSE(svc.libraryForMode(StreamMode::TV)->has("tt0903747"));
}
```

- [ ] **Step 2:** Run, verify FAIL.
- [ ] **Step 3: Implement.** `VideoModeServices.h` (sketch — match real `AddonRegistry`/`MetaAggregator`/`StreamAggregator` ctor signatures read from the headers):

```cpp
#pragma once
#include <QObject>
#include <QHash>
#include "core/stream/StreamMode.h"
#include "core/stream/StreamLibrary.h"

class JsonStore; class StreamDownloadIndex; class TorrentClient;
namespace tankostream::stream { class AddonRegistry; class MetaAggregator; class StreamAggregator; }

// Six-mode (2026-06-07): the shared video engine + per-mode libraries, hoisted out
// of StreamPage so the three pages share ONE engine. Constructed once in MainWindow.
class VideoModeServices : public QObject {
    Q_OBJECT
public:
    explicit VideoModeServices(JsonStore* store, QObject* parent = nullptr);
    // engine (shared, one instance each)
    tankostream::stream::AddonRegistry*   addonRegistry()  const { return m_addons; }
    tankostream::stream::MetaAggregator*  metaAggregator() const { return m_meta; }
    tankostream::stream::StreamAggregator* streamAggregator() const { return m_streams; }
    void setStreamDownloadIndex(StreamDownloadIndex* idx);   // injected from MainWindow
    StreamDownloadIndex* downloadIndex() const { return m_downloadIndex; }
    void setTorrentClient(TorrentClient* tc);
    // per-mode libraries + routing
    StreamLibrary* libraryForMode(StreamMode mode) const;
    void addClassified(const StreamLibraryEntry& entry);     // routes via classifyStreamMode(animeFlag,type)
    void removeEverywhere(const QString& imdbId);
    // classification cache (fed by MetaAggregator::entryResolved)
    void cacheIsAnime(const QString& imdb, bool isAnime, int kitsuId);
    bool cachedIsAnime(const QString& imdb) const;           // false if unknown
signals:
    void libraryChanged(StreamMode mode);
private:
    tankostream::stream::AddonRegistry*    m_addons;
    tankostream::stream::MetaAggregator*   m_meta;
    tankostream::stream::StreamAggregator* m_streams;
    StreamDownloadIndex* m_downloadIndex = nullptr;
    StreamLibrary* m_anime;  StreamLibrary* m_tv;  StreamLibrary* m_movies;
    QHash<QString, QPair<bool,int>> m_classCache;            // imdb -> {isAnime, kitsuId}
};
```

`.cpp`: construct `m_addons`/`m_meta`/`m_streams` (the code lifted from `StreamPage.cpp:306-307`); construct the 3 libraries with `streamLibraryFilename(mode)`; subscribe to `m_meta->entryResolved` to feed the cache + backfill kitsuId; `addClassified` routes via `classifyStreamMode`; `removeEverywhere` removes from whichever library `has()` it.

- [ ] **Step 4:** Add to CMake; run test → PASS; confirm `.obj`.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): VideoModeServices (shared engine + 3 libraries + classification cache + routing)"`

### Task 10: Mode-parameterize `StreamPage` over injected services (legacy ctor kept)

**Files:** Modify `src/ui/pages/StreamPage.{h,cpp}`.

> **Pre-task read:** read `StreamPage.cpp` in full. **Codex finding #1:** keep the old ctor working so the Task-10 checkpoint compiles (MainWindow still uses it until Task 13).

- [ ] **Step 1: Add a new ctor + a mode member.** Keep `StreamPage(CoreBridge*, TorrentClient*, ...)` (legacy) building its own services internally (today's behavior, mode = Movies). Add `StreamPage(CoreBridge*, StreamMode mode, VideoModeServices* services, QWidget* parent)`. Store `m_mode`; in **both** ctors `setObjectName(streamModeKey(m_mode))` (**critical** — Risk: silent non-activation).
- [ ] **Step 2: New ctor uses injected services** — `m_addonRegistry = services->addonRegistry()`, `m_metaAggregator = services->metaAggregator()`, `m_streamAggregator = services->streamAggregator()`, library = `services->libraryForMode(mode)`, download index = `services->downloadIndex()`. Do **not** `new` these in the injected path. (Lifetime: services owns them; the page borrows — mirror the existing `metaAggregator()` "owned elsewhere, stable" contract.)
- [ ] **Step 3: Route adds via services.** Where the page (or its detail view) adds/removes to the library, call `services->addClassified(...)` / `services->removeEverywhere(...)` (the detail-view wiring lands in Task 12).
- [ ] **Step 4: Pass `mode` + `services`** to the page's `StreamContinueStrip` (domain), `StreamSearchWidget` (filter), `StreamDetailView` (Task 12), and catalog requests (Task 8 filter).
- [ ] **Step 5: Defer heavy work (D1).** Construct/populate the detail view + fire the first catalog fetch on `activate()` / first show, not in the ctor. Verify the library grid iterates only this mode's library.
- [ ] **Step 6: Build-verify** — `build_check.bat` → BUILD OK (legacy ctor keeps MainWindow compiling; behavior unchanged).
- [ ] **Step 7: Commit** — `git commit -m "refactor(video-split): StreamPage mode + injected services (legacy ctor retained)"`

### Task 11: Mode-scope `StreamContinueStrip` + `StreamSearchWidget`

**Files:** Modify `src/ui/pages/stream/StreamContinueStrip.{h,cpp}` (`allProgress("stream")` ≈ :79; `startsWith("stream:")` ≈ :116, :275), `src/ui/pages/stream/StreamSearchWidget.{h,cpp}` (results split ≈ :69-77, :220-229).

- [ ] **Step 1:** Add a domain string to `StreamContinueStrip` (ctor/setter, default `"stream"`). Replace **all** `"stream"` / `"stream:"` literals (`:79`, `:116`, `:275`) with `m_domain` / `m_domain + ":"`. The owning page passes `streamModeKey(mode)`.
- [ ] **Step 2:** `StreamSearchWidget`: add a `setMode(StreamMode)`; render only the relevant section(s) — Anime → anime section; TV → series; Movies → movies. Add the anime section (extends the existing movies/series split).
- [ ] **Step 3: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 4: Commit** — `git commit -m "feat(video-split): per-mode Continue domain (all key sites) + search sections"`

---

## Phase 6 — Detail view per-mode behavior

### Task 12: Detail view takes services + mode; anime extras; routed add/remove

**Files:** Modify `src/ui/pages/stream/StreamDetailView.{h,cpp}`.

> **Pre-task read:** read `StreamDetailView.cpp` in full (≈ 4666 lines). It branches on `m_currentType` + `m_isAnime` (season combo ≈ :1019-1020; episode label ≈ :1216) and calls `m_library->add/remove` at `:2342/:2392/:2406`. Contain new code in private helpers (Risk: blast radius).

- [ ] **Step 1: Constructor.** Change `StreamDetailView(bridge, metaAggregator, library, parent)` → `StreamDetailView(bridge, VideoModeServices* services, StreamMode mode, parent)`. Use `services->metaAggregator()` for fetches and `services->libraryForMode(mode)` for read/`has()`.
- [ ] **Step 2: Routed add/remove (#3).** Replace the direct `m_library->add(entry)` (`:2342`, `:2406`) with `services->addClassified(entry)` and `m_library->remove(...)` (`:2392`) with `services->removeEverywhere(...)`. (Add routes by classification, so an anime title opened from a TV grid lands in Anime; show the D9 one-line note when the routed mode ≠ the page's mode.)
- [ ] **Step 3: Episode label** — `"Ep N"` (anime absolute) vs `"S{n}E{m}"` (TV) at `:1216`, driven by `m_mode == StreamMode::Anime`. Season combo hidden for Anime (already) **and** Movies; shown for TV.
- [ ] **Step 4: Movies** — single-file play affordance, no episode list (reuse the movie branch). Anime films (movie type in Anime mode) also use the single-file affordance (D10).
- [ ] **Step 5: Sub/dub (D8)** — a SUB/DUB toggle visible only in Anime mode; sorts the already-fetched sources by filename tag (`[SUB]`/`[DUB]`/`Dual`/`Dual-Audio`, case-insensitive). No new fetch; unchanged order if no tags.
- [ ] **Step 6: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): detail view services+mode, routed add, anime extras, movie/TV gating"`

---

## Phase 7 — MainWindow nav wiring (SHARED FILE — append-only, AFTER Agent 1)

> **SHARED-FILE COORDINATION:** `src/ui/MainWindow.cpp` is edited by both arcs.
> 1. **Land Agent 1's Phase 4 first**, then `git pull`/sync.
> 2. **Preflight (Codex #9) — verify the *actual* post-Agent-1 contract, not just "MangaPage present":** confirm `PAGE_MANGA`/`PAGE_COMICS` constants + values; each comics page's `setObjectName` (`"manga"`, `"western_comics"` vs mode page-id `"comics"`); the `activatePage`/`resetActivePageToRoot`/`onLayerRestoreRequested` branches; and the `bindShortcuts` table. Reconcile our additions against what actually landed.
> 3. **navDefs is line-disjoint IF order holds:** Agent 1 leaves `{ PAGE_STREAM, "Theatre" }` last; we replace *only that line* with three entries → final `Manga, Comics, Books, Anime, TV, Movies`. Agree this + the Ctrl+1..6 map with Agent 1 up front (D5).
> 4. **Append, don't reformat.** Post a BUILD-LANE claim in chat.md before touching the file.

### Task 13: Constants, pills, page stack (3× over shared services), dispatch, dev-control, keybinds

**Files:** Modify `src/ui/MainWindow.{cpp,h}`.

- [ ] **Step 1: Constants (≈ :63-75).** Add `PAGE_ANIME="anime"`, `PAGE_TV="tv"`, `PAGE_MOVIES="movies"` as a contiguous block after `PAGE_BOOKS`. Note `PAGE_STREAM` retires as a pill (kept as a string only if still referenced by the shared downloads page).
- [ ] **Step 2: navDefs (≈ :531-542).** Replace the trailing `{ PAGE_STREAM, "Theatre" }` with `{PAGE_ANIME,"Anime"},{PAGE_TV,"TV"},{PAGE_MOVIES,"Movies"}`.
- [ ] **Step 3: buildPageStack (≈ :838-921).** Construct ONE `VideoModeServices` (with the shared `JsonStore`); `services->setStreamDownloadIndex(m_streamDownloadIndex)` (the existing MainWindow-owned index) + `setTorrentClient(torrentClient)`. Construct three `StreamPage(m_bridge, mode, services, ...)`; add each to `m_pageStack`; wire each page's `enteredLayer`/`exitedLayer` to `pushLayer/popLayer` with its own pageId. Replace the `m_streamPage` member with `m_animePage`/`m_tvPage`/`m_moviesPage` (MainWindow.h) **and** add a helper `StreamPage* pageForVideoMode(const QString& pageId) const`.
- [ ] **Step 4: VideosPage meta source (≈ :870).** Change `m_videosPage->setMetaAggregator(m_streamPage->metaAggregator())` → `services->metaAggregator()`.
- [ ] **Step 5: PerModeNav roots.** `setRootLayer("anime"|"tv"|"movies", { pageId, "library", "Library" })`. No controller code change.
- [ ] **Step 6: activatePage (≈ :1039-1098).** ObjectName loop already finds the pages. Extend the post-switch `qobject_cast<StreamPage*>` + `activate()` to all three via `pageForVideoMode(pageId)`. Extend the Organise-button (≈ :1062) + sidebar-downloads (≈ :1087-1093) conditionals to treat `anime/tv/movies` like the old `stream`.
- [ ] **Step 7: resetActivePageToRoot (≈ :1111-1125) + onLayerRestoreRequested (≈ :1183-1208).** Replace the single `pageId == "stream"` branch (`:1189`) with `pageForVideoMode(pageId)` handling all three; add reset branches. Append after Agent 1's comics branches.
- [ ] **Step 8: Enumerate + update ALL `m_streamPage` refs (Codex #5).** The remaining sites: dev-control library snapshot (≈ :318-324, :2124-2127), dev search/dispatch (≈ :2177-2206), dev snapshot (≈ :2340-2342, composite :2387-2388), mode→page maps (≈ :2457, :2550), `stream_`-command forwarding (≈ :2592). Route each through `pageForVideoMode(pageId)`; **extend dev-control accepted modes to `anime`/`tv`/`movies`** (D11), keeping `stream`/`stream_*` as a back-compat alias → `m_moviesPage` (default). Verify no dangling `m_streamPage`.
- [ ] **Step 9: StreamDownloadsPage back-target (D3, ≈ :928-944).** Record the launching video mode when the downloads page opens; `backRequested` → `activatePage(<launching mode>)` (default `PAGE_MOVIES`).
- [ ] **Step 10: bindShortcuts (≈ :985-1004) — jointly agreed.** `Ctrl+4`=Anime, `Ctrl+5`=TV, `Ctrl+6`=Movies; sidebar toggle → `Ctrl+0`. (No-op verify if Agent 1 already added these per the agreement.)
- [ ] **Step 11: Build-verify** — `build_check.bat` → BUILD OK; verify each new page `.obj` + exe mtime advanced. Grep confirms zero stale `m_streamPage`.
- [ ] **Step 12: Commit (small) + push** — `git commit -m "feat(video-split): Anime+TV+Movies top-level modes over shared services; retire Theatre pill; dev-control modes"` then push so Agent 1 can rebase.

---

## Phase 8 — Migration guard + verification

### Task 14: Start-fresh guard (video side)

- [ ] **Step 1:** Confirm the three pages read only `anime_library.json`/`tv_library.json`/`movies_library.json` (empty on first run); nothing reads `stream_library.json` into a new page; leave `stream_library.json` on disk (no rename — preserves rollback).
- [ ] **Step 2:** Confirm `StreamDownloadIndex` legacy rows still resolve for playback (disk-first); downloaded files untouched.
- [ ] **Step 3:** Add a one-time first-run note (Hemanth-language): "Your Anime / TV / Movies libraries start fresh — your downloaded files are safe on disk."
- [ ] **Step 4: Commit** — `git commit -m "feat(video-split): start-fresh guard + first-run note"`

### Task 15: Build + classification smoke + cross-model review

- [ ] **Step 1: Build-verify (false-green guard)** — `build_check.bat` → BUILD OK; confirm `.obj` for `StreamMode.cpp`, `VideoModeServices.cpp`, new TUs.
- [ ] **Step 2: Headless** — `build_and_run.bat`; `out\tankoctl.exe ping` + `introspect-tree`/`get-modes`: six pills; dev-control responds for `anime`/`tv`/`movies`; per-mode library snapshots return.
- [ ] **Step 3: Classification smoke (DoD)** —
  - **Demon Slayer** (anime series) → **Anime**.
  - An **anime film** (*A Silent Voice* / *Your Name*) → **Anime**, and it **produces playable sources** via the movie path (D10).
  - **Breaking Bad** (non-anime series) → **TV**.
  - **Inception** (film) → **Movies**.
  - No cross-mode bleed in libraries/Continue; pill-from-deep-view resets to mode root; anime detail shows `Ep N` + sub/dub; TV shows seasons; Movies single-file. Open an anime title from the TV grid → it routes to Anime with the note (D9).
- [ ] **Step 4: Pre-warm smoke** — cold start, re-open a previously-added anime; the Task-7 log marker shows the kitsu route built from the stored id, no live re-fetch.
- [ ] **Step 5: Regression** — Books unchanged; comic reader unchanged; play a movie + a TV episode end-to-end; torrent engine unchanged.
- [ ] **Step 6: Cross-model review (producer ≠ reviewer)** — `/codex-review` (or `codex exec`) the full diff against this DoD. Address every NOT-MET.
- [ ] **Step 7: RTC + close** — contracts-v3 RTC to `agents/chat.md` (`Done-when:` = DoD) with `Skills invoked:` provenance. Hemanth's smoke on the running app is the final gate.

---

## Definition of Done

- **Six mode pills** (`Manga · Comics · Books · Anime · TV · Movies`); each its own page + per-mode back-stack.
- **Correct classification:** every anime title (series **or** film) lands in Anime; non-anime series → TV; non-anime films → Movies — verified on Demon Slayer + an anime film + Breaking Bad + Inception. An anime title opened from a TV/Movies grid routes to Anime (note shown).
- **Anime extras:** sub/dub preference + `Ep N` absolute numbering + flat episode list; TV seasons/episodes; Movies (and anime films) single-file.
- **One shared engine** (`MetaAggregator`/`StreamAggregator`/`AddonRegistry`/`StreamDownloadIndex`) behind all three pages — not 3×; `VideosPage` meta sourced from the same.
- Each mode: **own catalog brain** (Anime: Kitsu/AniList + Amatsu/Nyaa; TV/Movies: Cinemeta) + **own library + Continue strip**; no cross-mode bleed; best-effort anime exclusion from TV/Movies grids.
- **Persisted discriminator:** `animeFlag` + resolved `kitsuId` survive restart; re-opening an anime series builds the kitsu route with no live re-fetch.
- **Start fresh:** three new libraries begin empty; old `stream_library.json` left in place; downloaded files untouched; legacy downloads still play.
- **No regression:** Books, comic reader, video player, torrent engine behave as before.
- `build_check.bat` green; unit tests pass (StreamMode, library serialization, VideoModeServices routing+cache, progress domains); `/codex-review` APPROVE; **Hemanth smoke passes** (the only done-gate).

---

## Self-Review (author pass, v2)

- **Spec coverage (§5/§6/§9/§10):** classification rule → Tasks 1, 6 (decoupled from the 5-season gate); Anime catalog + extras → Tasks 8, 11, 12; TV/Movies Cinemeta → Task 8; per-mode storage → Tasks 4, 5, 9, 11; pre-warm → Task 7 (scope-corrected); nav → Task 13; migration → Task 14; §10 DoD → Tasks 13 + 15.
- **All 9 Codex findings mapped** (see v2 Revision Log): #1→Task 10/13 (legacy ctor), #2→Task 6 (resolve-time + deferred add), #3→Tasks 9/10/12 (routed add), #4→Tasks 5/11 (all key sites), #5→Tasks 9/13 (hoist + 37 refs + dev-control), #6→Task 8 (cache + redirect), #7→Task 1/6 (country normalization), #8→Task 7/15 (anime-film movie path), #9→Task 13 preflight.
- **Placeholder scan:** pure-logic/mechanical tasks (1, 2, 4, 5, 9) carry complete code; large-page tasks (10, 12, 13) cite exact anchors + a full-file pre-task read (honest shape for 4666-line refactors — mirrors Arc 1). Not placeholder gaps.
- **Type consistency:** `classifyStreamMode(bool,QString)`, `isAnimeTitle(QStringList,QString)`, `streamModeKey/FromKey/streamLibraryFilename`, `StreamMode{Anime,TV,Movies}`, `VideoModeServices::{addonRegistry,metaAggregator,streamAggregator,downloadIndex,libraryForMode,addClassified,removeEverywhere,cacheIsAnime,cachedIsAnime}`, `StreamLibraryEntry::{animeFlag,kitsuId}`, `MetaAggregator::entryResolved(imdb,kitsuId,isAnime)`, `pageForVideoMode(pageId)` — used identically across tasks.
- **Compile-at-every-checkpoint:** the legacy `StreamPage` ctor (Task 10) keeps MainWindow building until Task 13 swaps it; each task's build-verify is independently green.
- **Shared-file safety:** Phase 7 gated behind Agent 1 + a preflight against the *actual* landed contract + append-only edits + agreed pill order + a chat.md BUILD-LANE claim.
