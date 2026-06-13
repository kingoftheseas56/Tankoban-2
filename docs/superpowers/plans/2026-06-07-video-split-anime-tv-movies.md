# Video Split (Anime + TV + Movies) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> This is **Arc 2** of the six-mode restructure (spec: `docs/superpowers/specs/2026-06-07-six-mode-restructure-design.md`, §5). Arc 1 (Comics → Manga + Western) is a separate, already-written plan: `docs/superpowers/plans/2026-06-07-comics-split-manga-western.md`. The two arcs share **one file** (`src/ui/MainWindow.cpp`) — see Phase 7 coordination.

**Goal:** Split the single "Theatre" video mode into three top-level modes — **Anime** (all Japanese animation: series *and* films), **TV** (non-anime series), **Movies** (non-anime films) — each with its own page, library, Continue strip, and tailored catalog, over the **shared** video engine (libtorrent + addons + player + `StreamDownloadIndex` + `MetaAggregator`).

**Architecture:** "Split the faces, share the engines." The engine already computes the classification data (`type` = movie/series; anime detection via `AnimeCatalogResolver::isAnimeSeries(genres,country)`). The work is: (1) make the anime discriminator **persistent** (it is currently transient), (2) **decouple mode classification from the 5-season Kitsu-reroute gate** so short anime series + anime films classify correctly, (3) parameterize the existing `StreamPage` / `StreamLibrary` / `StreamContinueStrip` / `StreamDetailView` by a new `StreamMode`, instantiate them **three times** over the shared engine, and (4) wire three top-level pills. **Start fresh** on video content (new libraries begin empty; old `stream_library.json` left untouched on disk).

**Tech Stack:** C++17, Qt 6 (Widgets), CMake + ninja, GoogleTest. Build: `build_check.bat` (compile-verify) / `build_and_run.bat` (Hemanth smoke). Dev-control bridge (`out\tankoctl.exe`) for headless state checks.

**Migration:** Video side = **start fresh** (spec §6). The three new pages use **new filenames** (`anime_library.json` / `tv_library.json` / `movies_library.json`), so they start empty automatically; the old `stream_library.json` is never read by the new pages and is **left in place, not rebucketed**. Downloaded files on disk are untouched; legacy `StreamDownloadIndex` rows still resolve for playback (disk-first).

---

## Phase 0 — Locked Decisions (design gate, no code)

These resolve the spec-vs-reality forks the grounding pass surfaced. They are **technical** decisions (Rule 14, made by the producer); the product decisions were locked in the spec's brainstorm. Recorded here so the executor does not re-litigate them.

- **D1 — Page-shell model: ONE `StreamPage` class, parameterized by `StreamMode`, instantiated 3×** (distinct `objectName` per instance: `"anime"`/`"tv"`/`"movies"`). Rejected: 3 forked classes (3× maintenance) and a base+subclass tree (no gain over a ctor arg). Rationale: lowest churn, single `qobject_cast<StreamPage*>` in `MainWindow` dispatch (lower collision surface vs Agent 1's parallel edits), literal "share the engines." **Caveat (heaviness):** the app already has idle/startup-cost tickets open in this domain — each `StreamPage` instance MUST defer heavy work (catalog fetch + detail-view population) to `activate()` / first-show, never the ctor. Verified at smoke (Task 15).
- **D2 — Detail view: parameterize the existing `StreamDetailView`** (it already branches on `m_currentType` + `m_isAnime`). Each `StreamPage` instance owns its own `StreamDetailView` with the mode set; anime-only layout is contained in private helpers. No fork.
- **D3 — Theatre pill retired.** `PAGE_STREAM = "stream"` is removed as a pill. The shared read-only downloads page (`PAGE_STREAM_DOWNLOADS`) is **kept once** (shared across all three video modes); its back-target becomes the launching video mode (default Movies) instead of the retired `PAGE_STREAM`.
- **D4 — Vestigial `VideosPage` (local-file, `PAGE_VIDEOS`, Ctrl+3): DEFERRED, untouched** (spec §7). Stays as the power-user escape hatch. Not folded into Movies in this arc.
- **D5 — Keybinds (SHARED with Agent 1): final pill order `Manga · Comics · Books · Anime · TV · Movies`** → `Ctrl+1..6`. Conflict: `Ctrl+5` is today the sidebar toggle → it moves to **`Ctrl+0`**. This single table is edited **once**, jointly agreed with Agent 1 (Phase 7).
- **D6 — Classification is data-driven and decoupled from the reroute gate.** Mode = `classifyStreamMode(isAnime, type)` where `isAnime = AnimeCatalogResolver::isAnimeSeries(genres, country)` (Animation genre AND country == "Japan" — works for films too; it is genre+country only). The existing `seasons.size() >= 5` gate at `MetaAggregator.cpp:483` stays as a *Kitsu-episode-list-rendering* optimization but is **never** used for mode assignment. Anime films (type `"movie"`, Animation+Japan) → Anime.
- **D7 — Library ownership: a thin `VideoLibraryHub`** owns the three `StreamLibrary` instances and routes adds by `classifyStreamMode` (so an anime title lands in Anime regardless of entry point — DoD requirement). Each page reads its own mode's library. Cross-mode delete-scoping lives here too.
- **D8 — Sub/dub scope (v1): a SUB/DUB preference on the Anime sources/detail that prioritizes release results by filename tag** (`[SUB]`/`[DUB]`/`Dual-Audio` parsed from Torrentio/Nyaa names). Play-time mpv audio-track switching (Agent 3's domain) is a future enhancement, explicitly out of this arc. This is the data-backed, buildable interpretation of spec's "sub/dub selection."

---

## File Structure

**Create:**
- `src/core/stream/StreamMode.h` — `enum class StreamMode { Anime, TV, Movies }` + pure `classifyStreamMode(bool isAnime, const QString& type)` + `streamModeKey()` / `streamModeFromKey()` / `streamLibraryFilename()` helpers. Header-only-friendly (small .cpp if needed for the table).
- `src/core/stream/StreamMode.cpp` — the helper tables (if not inlined).
- `src/core/stream/VideoLibraryHub.{h,cpp}` — owns 3 `StreamLibrary` instances; `libraryForMode(StreamMode)`, `addClassified(entry, bool isAnime)`, cross-mode remove-scoping.
- `tests/stream/test_stream_mode.cpp` — classification + key round-trip (pure logic, TDD).
- `tests/stream/test_stream_library_anime_flag.cpp` — entry serialization round-trip + filename parameterization.
- `tests/stream/test_video_library_hub.cpp` — add-routing-by-classification + per-mode isolation.
- `tests/stream/test_progress_domains.cpp` — CoreBridge/UnifiedProgressStore domain routing.

**Modify:**
- `src/core/stream/StreamLibrary.{h,cpp}` — add `animeFlag` + `kitsuId` to `StreamLibraryEntry` + (de)serialize; parameterize ctor filename.
- `src/core/stream/StreamDownloadIndex.{h,cpp}` — add `animeFlag` to `Entry` + repo codec (backward-compatible default).
- `src/core/CoreBridge.{h,cpp}` — add `anime`/`tv`/`movies` progress domains (4 sites).
- `src/core/stream/UnifiedProgressStore.{h,cpp}` — generalize the `stream:` key prefix + per-domain payload accessor.
- `src/core/stream/MetaAggregator.{h,cpp}` — `entryResolved(imdbId, kitsuId, isAnime)` signal; mode-scoped catalog requests; decouple classification from the reroute gate.
- `src/core/stream/CatalogAggregator.{h,cpp}`, `src/core/stream/StreamAggregator.{h,cpp}`, `src/ui/pages/stream/CatalogBrowseScreen.cpp` — thread a `StreamMode` filter through the catalog path.
- `src/ui/pages/StreamPage.{h,cpp}` — accept a `StreamMode` ctor arg + own a mode-scoped library/continue/catalog/detail; defer heavy work to `activate()`.
- `src/ui/pages/stream/StreamContinueStrip.{h,cpp}` — domain string via ctor/setter (currently hardcodes `"stream"`).
- `src/ui/pages/stream/StreamSearchWidget.{h,cpp}` — mode-filtered results + anime section.
- `src/ui/pages/stream/StreamDetailView.{h,cpp}` — per-mode formatting (`Ep N` vs `SxEy`), anime sub/dub preference, season-combo gating.
- `src/ui/MainWindow.{cpp,h}` — **SHARED FILE** — constants, navDefs, buildPageStack, activatePage, resetActivePageToRoot, onLayerRestoreRequested, bindShortcuts.
- `CMakeLists.txt` (root sources list) — register the new files.

**Pre-task read (executor):** before Phase 5/6, read `src/ui/pages/StreamPage.cpp` and `src/ui/pages/stream/StreamDetailView.cpp` **in full** — they are large (StreamDetailView ≈ 4666 lines). The tasks below say *what to parameterize and where*; the executor reproduces the surrounding code by reading the real file, exactly as Arc 1 handles the `ComicsPage` split. Do not invent line bodies you have not read.

---

## Phase 1 — Classification core + persistent discriminator (pure logic, TDD)

Do this first: it is pure, testable, and everything downstream depends on a stable classifier + a persisted flag.

### Task 1: `StreamMode` enum + pure classifier

**Files:**
- Create: `src/core/stream/StreamMode.h`, `src/core/stream/StreamMode.cpp`
- Test: `tests/stream/test_stream_mode.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "core/stream/StreamMode.h"

TEST(StreamMode, AnimeFlagWinsForBothTypes) {
    EXPECT_EQ(classifyStreamMode(/*isAnime=*/true,  "series"), StreamMode::Anime);
    EXPECT_EQ(classifyStreamMode(/*isAnime=*/true,  "movie"),  StreamMode::Anime);  // anime film
}
TEST(StreamMode, NonAnimeSeriesIsTv) {
    EXPECT_EQ(classifyStreamMode(false, "series"), StreamMode::TV);
}
TEST(StreamMode, NonAnimeMovieIsMovies) {
    EXPECT_EQ(classifyStreamMode(false, "movie"), StreamMode::Movies);
    EXPECT_EQ(classifyStreamMode(false, ""),      StreamMode::Movies);  // unknown type defaults to movie
}
TEST(StreamMode, KeyRoundTrip) {
    EXPECT_EQ(streamModeKey(StreamMode::Anime),  QStringLiteral("anime"));
    EXPECT_EQ(streamModeKey(StreamMode::TV),     QStringLiteral("tv"));
    EXPECT_EQ(streamModeKey(StreamMode::Movies), QStringLiteral("movies"));
    EXPECT_EQ(streamModeFromKey("anime"),  StreamMode::Anime);
    EXPECT_EQ(streamModeFromKey("tv"),     StreamMode::TV);
    EXPECT_EQ(streamModeFromKey("movies"), StreamMode::Movies);
    EXPECT_EQ(streamModeFromKey("bogus"),  StreamMode::Movies);  // safe default
}
TEST(StreamMode, LibraryFilenamePerMode) {
    EXPECT_EQ(streamLibraryFilename(StreamMode::Anime),  QStringLiteral("anime_library.json"));
    EXPECT_EQ(streamLibraryFilename(StreamMode::TV),     QStringLiteral("tv_library.json"));
    EXPECT_EQ(streamLibraryFilename(StreamMode::Movies), QStringLiteral("movies_library.json"));
}
```

- [ ] **Step 2: Run it, verify it fails** — Run: `ctest -R StreamMode -V` · Expected: FAIL (header not found / undeclared). (Tests build via `-DTANKOBAN_BUILD_TESTS=ON`.)

- [ ] **Step 3: Implement `StreamMode.h`**

```cpp
#pragma once
#include <QString>

// Six-mode restructure (2026-06-07), Arc 2. The video engine is shared; an
// item's mode is derived from data the engine already computes. anime-flag wins
// for both films and series; otherwise series -> TV, movie (or unknown) -> Movies.
enum class StreamMode { Anime, TV, Movies };

inline StreamMode classifyStreamMode(bool isAnime, const QString& type) {
    if (isAnime) return StreamMode::Anime;
    if (type.compare(QStringLiteral("series"), Qt::CaseInsensitive) == 0)
        return StreamMode::TV;
    return StreamMode::Movies;  // "movie" and unknown both land here
}

QString    streamModeKey(StreamMode mode);          // "anime" | "tv" | "movies"
StreamMode streamModeFromKey(const QString& key);   // inverse; unknown -> Movies
QString    streamLibraryFilename(StreamMode mode);  // "<key>_library.json"
```

- [ ] **Step 4: Implement `StreamMode.cpp`**

```cpp
#include "core/stream/StreamMode.h"

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

- [ ] **Step 5:** Add `src/core/stream/StreamMode.cpp` to the CMake sources list and `tests/stream/test_stream_mode.cpp` to the test sources list.
- [ ] **Step 6: Run it, verify it passes** — Run: `ctest -R StreamMode -V` · Expected: PASS (5/5). Confirm `StreamMode.cpp.obj` built (new-source false-green guard).
- [ ] **Step 7: Commit** — `git add src/core/stream/StreamMode.* tests/stream/test_stream_mode.cpp CMakeLists.txt && git commit -m "feat(video-split): StreamMode enum + pure classifier (anime/tv/movies)"`

### Task 2: Persist `animeFlag` + `kitsuId` on `StreamLibraryEntry`

**Files:**
- Modify: `src/core/stream/StreamLibrary.h` (struct at :13-22), `src/core/stream/StreamLibrary.cpp` (`fromJson`/`toJson`)
- Test: `tests/stream/test_stream_library_anime_flag.cpp`

- [ ] **Step 1: Write the failing test** (round-trip + backward-compat default)

```cpp
#include <gtest/gtest.h>
#include <QJsonObject>
#include "core/stream/StreamLibrary.h"

// fromJson/toJson are private; expose them for test via a friend or a thin
// public static. Step 3 adds `static StreamLibraryEntry entryFromJson(...)` +
// `static QJsonObject entryToJson(...)` public wrappers (the private ones stay).
TEST(StreamLibraryAnimeFlag, RoundTripsNewFields) {
    StreamLibraryEntry e;
    e.imdb = "tt9335498"; e.type = "series"; e.name = "Demon Slayer";
    e.animeFlag = true;   e.kitsuId = 41370;
    const QJsonObject j = StreamLibrary::entryToJson(e);
    const StreamLibraryEntry back = StreamLibrary::entryFromJson(j);
    EXPECT_TRUE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, 41370);
    EXPECT_EQ(back.type, QStringLiteral("series"));
}
TEST(StreamLibraryAnimeFlag, LegacyRowsDefaultNonAnime) {
    QJsonObject legacy;  // a pre-split row: no animeFlag/kitsuId keys
    legacy["imdb"] = "tt0111161"; legacy["type"] = "movie"; legacy["name"] = "X";
    const StreamLibraryEntry back = StreamLibrary::entryFromJson(legacy);
    EXPECT_FALSE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, -1);
}
```

- [ ] **Step 2: Run it, verify it fails** — Run: `ctest -R StreamLibraryAnimeFlag -V` · Expected: FAIL (no `animeFlag` member / no public wrappers).

- [ ] **Step 3: Add the fields + public wrappers + (de)serialize.** In `StreamLibrary.h`, extend the struct:

```cpp
struct StreamLibraryEntry {
    QString imdb;           // "tt1234567"
    QString type;           // "movie" or "series"
    QString name;
    QString year;
    QString poster;         // URL
    QString description;
    QString imdbRating;
    qint64  addedAt = 0;    // ms since epoch
    bool    animeFlag = false;  // Six-mode (2026-06-07): true => Anime mode (film or series)
    int     kitsuId   = -1;     // resolved Kitsu id for the play path; -1 = none/unresolved
};
```

Add public test/seam wrappers in `StreamLibrary.h` (the private `fromJson`/`toJson` remain; these just expose them):

```cpp
public:
    static StreamLibraryEntry entryFromJson(const QJsonObject& obj) { return fromJson(obj); }
    static QJsonObject        entryToJson(const StreamLibraryEntry& e) { return toJson(e); }
```

In `StreamLibrary.cpp`, extend `toJson` to write `animeFlag` + `kitsuId`, and `fromJson` to read them with the struct defaults when absent:

```cpp
// in toJson(...):
obj["animeFlag"] = entry.animeFlag;
obj["kitsuId"]   = entry.kitsuId;
// in fromJson(...):
e.animeFlag = obj.value(QStringLiteral("animeFlag")).toBool(false);
e.kitsuId   = obj.value(QStringLiteral("kitsuId")).toInt(-1);
```

- [ ] **Step 4: Run it, verify it passes** — Run: `ctest -R StreamLibraryAnimeFlag -V` · Expected: PASS (2/2).
- [ ] **Step 5: Commit** — `git add src/core/stream/StreamLibrary.* tests/stream/test_stream_library_anime_flag.cpp CMakeLists.txt && git commit -m "feat(video-split): persist animeFlag + kitsuId on StreamLibraryEntry"`

### Task 3: `animeFlag` on `StreamDownloadIndex::Entry`

**Files:** Modify `src/core/stream/StreamDownloadIndex.{h,cpp}` (Entry struct + its JSON/repo codec). Extend `tests/stream/test_stream_library_anime_flag.cpp` or add a sibling.

- [ ] **Step 1:** Read `StreamDownloadIndex.h` to find the `Entry` struct + its codec (the `Entry`-to-JSON and JSON-to-`Entry` functions, and any repo row mapping). Write a failing test that round-trips an `Entry` with `animeFlag = true` and asserts a legacy row (no key) reads back `false`.
- [ ] **Step 2:** Run, verify FAIL.
- [ ] **Step 3:** Add `bool animeFlag = false;` to `Entry`; serialize in the codec; default `false` on read (backward-compatible — legacy download rows are non-anime, which is harmless since the grid scopes by library, not by this flag). Match the existing field/codec naming exactly (read first).
- [ ] **Step 4:** Run, verify PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): animeFlag on StreamDownloadIndex::Entry (back-compat default)"`

---

## Phase 2 — Storage parameterization

### Task 4: Parameterize `StreamLibrary` filename (enable 3 instances)

**Files:** Modify `src/core/stream/StreamLibrary.{h,cpp}` (ctor + `FILENAME` at :77 + `load()` :150 + `save()` :173); `src/ui/pages/StreamPage.cpp` (the single existing call site ≈ :337). Extend `tests/stream/test_stream_library_anime_flag.cpp`.

- [ ] **Step 1: Write the failing test** — two libraries with different filenames stay isolated:

```cpp
TEST(StreamLibraryFilename, TwoInstancesAreIsolated) {
    JsonStore store(/* temp dir per existing JsonStore test pattern — read JsonStore.h */);
    StreamLibrary a(&store, QStringLiteral("anime_library.json"));
    StreamLibrary t(&store, QStringLiteral("tv_library.json"));
    StreamLibraryEntry e; e.imdb = "tt9335498"; e.type = "series"; e.animeFlag = true;
    a.add(e);
    EXPECT_TRUE(a.has("tt9335498"));
    EXPECT_FALSE(t.has("tt9335498"));   // different file => isolated
}
```

- [ ] **Step 2:** Run, verify FAIL (ctor signature mismatch).
- [ ] **Step 3: Change the ctor + replace the constant with a member.** In `StreamLibrary.h`:

```cpp
    explicit StreamLibrary(JsonStore* store, const QString& filename, QObject* parent = nullptr);
    // ...
private:
    const QString m_filename;   // was: static constexpr const char* FILENAME = "stream_library.json";
```

In `StreamLibrary.cpp`: set `m_filename(filename)` in the ctor init list; replace every `FILENAME` use in `load()`/`save()` with `m_filename`.

- [ ] **Step 4: Update the existing single call site** — in `StreamPage.cpp` (≈ :337), pass the current filename so behavior is unchanged for now: `new StreamLibrary(store, QStringLiteral("stream_library.json"), this)`. (This call site is replaced in Phase 5; the literal keeps the build green in between.)
- [ ] **Step 5: Build-verify** — Run: `build_check.bat` · Expected: BUILD OK (verify `out/Tankoban.exe` mtime advanced). Then `ctest -R StreamLibraryFilename -V` → PASS.
- [ ] **Step 6: Commit** — `git commit -m "refactor(video-split): StreamLibrary takes a filename (enables per-mode libraries)"`

### Task 5: `anime`/`tv`/`movies` progress domains

**Files:** Modify `src/core/CoreBridge.{h,cpp}` (`PROGRESS_FILES` table ≈ :27-31; `allProgress` ≈ :188; `progress` ≈ :265; `saveProgress` ≈ :227), `src/core/stream/UnifiedProgressStore.{h,cpp}` (`allEpisodePayloadsForStreamDomain` ≈ :84; `streamDomainKeyForEntry` ≈ :256). Test: `tests/stream/test_progress_domains.cpp`.

- [ ] **Step 1: Read all four CoreBridge sites + the two UnifiedProgressStore functions** to see the exact `domain == "stream"` branching pattern + the `"stream:"` key prefix.
- [ ] **Step 2: Write the failing test** — saving + reading back a progress payload under domain `"anime"` round-trips, and an `"anime"` key is prefixed `anime:` not `stream:`. (Mirror the existing stream-domain test if one exists; otherwise drive through `CoreBridge::saveProgress("anime", ...)` → `allProgress("anime")`.)
- [ ] **Step 3:** Run, verify FAIL.
- [ ] **Step 4: Generalize.** Add `anime`/`tv`/`movies` rows to `PROGRESS_FILES`; extend the three `domain ==` branches (`allProgress`, `progress`, `saveProgress`) to route the new domains to `UnifiedProgressStore`. Generalize `allEpisodePayloadsForStreamDomain()` + `streamDomainKeyForEntry()` to take a domain prefix parameter (e.g. `forDomain(const QString& domain)`), defaulting existing callers to `"stream"`. **Touch all sites** — a missed site = progress silently not saved/loaded for that mode (Risk #4).
- [ ] **Step 5:** Run, verify PASS; then `build_check.bat` → BUILD OK.
- [ ] **Step 6: Commit** — `git commit -m "feat(video-split): anime/tv/movies progress domains in CoreBridge + UnifiedProgressStore"`

---

## Phase 3 — Add-time classification (close the timing race) + play-path persistence

### Task 6: Deterministic add-time classification + `entryResolved` signal

**Files:** Modify `src/core/stream/MetaAggregator.{h,cpp}` (add the signal near `animeCatalogActive` ≈ MetaAggregator.h:100; emit at `emitSeriesResult` ≈ :609-627 and the movie path; relax the mode-classification path vs the reroute gate at :481-491), `src/ui/pages/stream/StreamDetailView.cpp` (add path ≈ :2334-2342), `src/core/stream/AnimeCatalogResolver.{h,cpp}` (confirm/expose `isAnimeSeries`). Test: extend `tests/stream/test_stream_mode.cpp` or add `tests/stream/test_classification.cpp`.

> **Why a signal:** `animeCatalogActive` can arrive *after* the user clicks Add (Risk #3). The fix has two layers: (a) at add-time, classify **synchronously** from the preview's genres+country (available on `MetaItemPreview`) via `isAnimeSeries` — no waiting; (b) `entryResolved(imdb, kitsuId, isAnime)` lets the library backfill the resolved `kitsuId` deterministically once known, without changing the already-correct `animeFlag`.

- [ ] **Step 1: Confirm `isAnimeSeries` is reusable.** Read `AnimeCatalogResolver.{h,cpp}` (≈ :12-20). If `isAnimeSeries(const QStringList& genres, const QString& country)` is free/static, reuse it directly. If it is private to a translation unit, promote it to a static member or a free function in `AnimeCatalogResolver.h` (no logic change — Animation genre AND country == "Japan"). Note: the name says "Series" but the logic is genre+country only, so it correctly identifies anime **films** too.
- [ ] **Step 2: Write the failing test** — drive the classifier on three shaped previews:

```cpp
// Pseudocode shape — match the real isAnimeSeries signature found in Step 1.
EXPECT_TRUE (isAnimeSeries({"Animation","Action"}, "Japan"));   // Demon Slayer
EXPECT_TRUE (isAnimeSeries({"Animation"},          "Japan"));   // an anime film
EXPECT_FALSE(isAnimeSeries({"Drama"},              "United States")); // a TV drama
EXPECT_FALSE(isAnimeSeries({"Animation"},          "United States")); // a Western cartoon -> not Anime mode
// then mode:
EXPECT_EQ(classifyStreamMode(isAnimeSeries({"Animation"},"Japan"), "movie"),  StreamMode::Anime);
EXPECT_EQ(classifyStreamMode(isAnimeSeries({"Drama"},"United States"), "series"), StreamMode::TV);
```

- [ ] **Step 3:** Run, verify FAIL (if `isAnimeSeries` needed promotion) or confirm the new mode assertions fail before wiring.
- [ ] **Step 4: Add the signal + emit it.** In `MetaAggregator.h`, declare `void entryResolved(const QString& imdbId, int kitsuId, bool isAnime);`. Emit it from the series result path (`emitSeriesResult`, both the rerouted-anime and the plain-series branches) and from the movie-resolution path, carrying the resolved `kitsuId` (or -1) and the `isAnime` computed from genres+country. **Do not gate this emit on `seasons.size() >= 5`** — that gate stays only on the Kitsu *reroute* (episode-list rendering); classification is independent (D6).
- [ ] **Step 5: Populate the entry at add-time.** In `StreamDetailView.cpp`'s add path (≈ :2334-2342), set `entry.animeFlag = isAnimeSeries(previewGenres, previewCountry)` and `entry.kitsuId = <resolved-or--1>` synchronously from the preview already in hand — so the add is correct even before any async signal. (The hub in Task 9 routes by this flag.)
- [ ] **Step 6:** Run the classification test → PASS; `build_check.bat` → BUILD OK.
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): deterministic add-time anime classification + entryResolved signal"`

### Task 7: Persist + reuse resolved `kitsuId` in the play path; verify pre-warm

**Files:** Modify `src/ui/pages/StreamPage.cpp` (play-path build ≈ :2423-2438), `src/core/stream/MetaAggregator.cpp` (startup pre-warm ≈ :259 — add a log marker), subscribe the library to `entryResolved`.

> **Scope correction:** `AnimeIdMapCache` **already** persists the IMDb→Kitsu map to disk (`AnimeIdMapCache.cpp:17`) and **already** pre-warms at startup (`MetaAggregator.cpp:259`). The spec's "persist + pre-warm" ask is therefore largely already met. The *real* gap is the per-series **resolved** `kitsuId` (transient `m_animeKitsuId` QHash) — persist it onto the library entry (Task 2 added the field) so re-opening an anime title after restart builds the `kitsu:<id>:<ep>` route with **no live re-fetch**.

- [ ] **Step 1:** Subscribe the owning `StreamLibrary` (or the hub) to `MetaAggregator::entryResolved` and, when the imdb is in-library, write the resolved `kitsuId` onto the stored entry (idempotent; skip if unchanged) and persist.
- [ ] **Step 2:** In the play-path build (`StreamPage.cpp` ≈ :2428), read `entry.kitsuId` **first**; only fall back to `m_metaAggregator->kitsuIdForSeries(imdbId)` when the stored id is `-1`. (Keeps the existing `kitsu:<id>:<absoluteEp>` route; just sources the id from disk when available.)
- [ ] **Step 3:** Add a one-line startup log marker in the pre-warm path (`MetaAggregator.cpp` ≈ :259) — `qInfo() << "[anime-id-map] prewarm: stale=" << ... ;` — so the smoke can confirm pre-warm fired before the first anime open.
- [ ] **Step 4: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 5: Smoke (headless, deferred to Task 15 corpus):** cold start, open a previously-added anime title, confirm the log shows the kitsu route built from the stored id with no network fetch. (Captured in the Task 15 evidence bundle.)
- [ ] **Step 6: Commit** — `git commit -m "feat(video-split): reuse persisted kitsuId in play path; mark startup prewarm"`

---

## Phase 4 — Catalog-brain mode scoping

### Task 8: Thread a `StreamMode` filter through the catalog path

**Files:** Modify `src/core/stream/CatalogAggregator.{h,cpp}` (`planRequests` ≈ :134), `src/core/stream/StreamAggregator.{h,cpp}` (id-prefix gate ≈ :571), `src/core/stream/MetaAggregator.cpp` (candidate-sort ≈ :326-343), `src/ui/pages/stream/CatalogBrowseScreen.cpp` (`rebuildSelectors` ≈ :308).

> **Goal:** each page asks only its brain — **Anime** keeps `kitsu`/`anilist`-prefixed addons (Kitsu/AniList + Amatsu/Nyaa); **TV** keeps Cinemeta *series*; **Movies** keeps Cinemeta *movie*. The id-prefix scoping mechanism already exists (Amatsu is scoped to `{kitsu,anilist}`); this task makes the *request* carry a mode so the existing scoping is applied per page.

- [ ] **Step 1: Read the catalog request path end-to-end** (`CatalogAggregator::planRequests` → `StreamAggregator::dispatchRequests` id-prefix gate → addon selection) to see how addons are currently chosen and where `type` is threaded.
- [ ] **Step 2:** Add an optional `StreamMode` (or a small `{ animeOnly, type }` filter struct) parameter to the catalog request entry point, defaulting to today's behavior (all addons, by `type`). For **Anime**: select only `kitsu`/`anilist`-prefixed addons. For **TV**/**Movies**: select Cinemeta-family addons and set `type` to `series`/`movie` respectively, **excluding** anime-prefixed addons.
- [ ] **Step 3:** Decide the reroute interaction (per D6): the anime *reroute* (`MetaAggregator.cpp:483`) stays data-driven/global (detection happens regardless of page), but in **TV/Movies** mode the catalog request never asks the anime brains, so anime titles do not surface there. Anime titles always classify to Anime at add-time (Task 6) even if a user reaches one from search.
- [ ] **Step 4: Handle seriesCache cross-mode contamination (Risk #6).** `m_seriesCache` is imdb-keyed with a 24h TTL. Ensure `entryResolved`/`animeCatalogActive` is **re-emitted on a cache hit** (or key the cache by `(imdb, mode)`), so a second open still drives the correct detail layout. Pick the lower-churn option (re-emit on hit) unless reading shows keying is cleaner.
- [ ] **Step 5: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 6: Smoke (headless, Task 15 corpus):** `tankoctl stream-search` on the anime page returns Kitsu/anime results; on TV/Movies returns Cinemeta non-anime. (Captured later.)
- [ ] **Step 7: Commit** — `git commit -m "feat(video-split): mode-scoped catalog brains (Anime=Kitsu/Amatsu, TV/Movies=Cinemeta)"`

---

## Phase 5 — The three pages over the shared engine

### Task 9: `VideoLibraryHub` (owns 3 libraries, routes adds by classification)

**Files:** Create `src/core/stream/VideoLibraryHub.{h,cpp}`; Test: `tests/stream/test_video_library_hub.cpp`; Modify `CMakeLists.txt`.

- [ ] **Step 1: Write the failing test** — add routes by classification, modes stay isolated:

```cpp
#include <gtest/gtest.h>
#include "core/stream/VideoLibraryHub.h"

TEST(VideoLibraryHub, RoutesAnimeToAnimeLibrary) {
    JsonStore store(/* temp dir */);
    VideoLibraryHub hub(&store);
    StreamLibraryEntry e; e.imdb="tt9335498"; e.type="series"; e.animeFlag=true;
    hub.addClassified(e);
    EXPECT_TRUE (hub.libraryForMode(StreamMode::Anime)->has("tt9335498"));
    EXPECT_FALSE(hub.libraryForMode(StreamMode::TV)->has("tt9335498"));
}
TEST(VideoLibraryHub, RoutesNonAnimeSeriesToTv) {
    JsonStore store(/* temp dir */);
    VideoLibraryHub hub(&store);
    StreamLibraryEntry e; e.imdb="tt0903747"; e.type="series"; e.animeFlag=false;
    hub.addClassified(e);
    EXPECT_TRUE(hub.libraryForMode(StreamMode::TV)->has("tt0903747"));   // Breaking Bad
}
TEST(VideoLibraryHub, RoutesNonAnimeMovieToMovies) {
    JsonStore store(/* temp dir */);
    VideoLibraryHub hub(&store);
    StreamLibraryEntry e; e.imdb="tt0111161"; e.type="movie"; e.animeFlag=false;
    hub.addClassified(e);
    EXPECT_TRUE(hub.libraryForMode(StreamMode::Movies)->has("tt0111161"));
}
```

- [ ] **Step 2:** Run, verify FAIL.
- [ ] **Step 3: Implement the hub.** `VideoLibraryHub.h`:

```cpp
#pragma once
#include <QObject>
#include "core/stream/StreamMode.h"
#include "core/stream/StreamLibrary.h"

class JsonStore;

// Six-mode (2026-06-07): owns the three video libraries and routes adds by
// classification so an anime title lands in Anime regardless of entry point.
class VideoLibraryHub : public QObject {
    Q_OBJECT
public:
    explicit VideoLibraryHub(JsonStore* store, QObject* parent = nullptr);
    StreamLibrary* libraryForMode(StreamMode mode) const;
    void addClassified(const StreamLibraryEntry& entry);  // routes via classifyStreamMode(entry.animeFlag, entry.type)
    void removeEverywhere(const QString& imdbId);         // cross-mode delete-scoping
private:
    StreamLibrary* m_anime;
    StreamLibrary* m_tv;
    StreamLibrary* m_movies;
};
```

`VideoLibraryHub.cpp`: construct the three `StreamLibrary` instances with `streamLibraryFilename(mode)`; `libraryForMode` switches; `addClassified` computes `classifyStreamMode(entry.animeFlag, entry.type)` and calls that library's `add`; `removeEverywhere` calls `remove` on whichever library has it.

- [ ] **Step 4:** Add to CMake; run test → PASS; confirm `.obj` built.
- [ ] **Step 5: Commit** — `git commit -m "feat(video-split): VideoLibraryHub routes adds by classification"`

### Task 10: Parameterize `StreamPage` by `StreamMode`

**Files:** Modify `src/ui/pages/StreamPage.{h,cpp}`.

> **Pre-task read:** read `StreamPage.cpp` in full first. This task changes *construction + selection wiring*, not the page's behavior. The page keeps its search/home/detail/downloads/sources structure; it just becomes mode-scoped.

- [ ] **Step 1:** Add a `StreamMode m_mode;` member + a ctor parameter: `StreamPage(CoreBridge* bridge, TorrentClient* client, StreamMode mode, VideoLibraryHub* hub, QObject*/QWidget* parent)`. In the ctor, `setObjectName(streamModeKey(mode))` (**critical** — activation matches by objectName; a missed set = silent non-activation, Risk #2).
- [ ] **Step 2:** Point the page's library at `hub->libraryForMode(mode)` instead of constructing its own `StreamLibrary` (replaces the Task-4 call site ≈ :337). Route add-to-library through `hub->addClassified(...)`.
- [ ] **Step 3:** Pass `mode` to the page's `StreamContinueStrip` (Task 11) and its catalog requests (Task 8 filter) and its `StreamDetailView` (Task 12).
- [ ] **Step 4: Defer heavy work (D1 caveat).** Ensure the ctor does **not** fire a catalog fetch or build the detail view; gate that on `activate()` / first show. If the current ctor does eager work, move it. Verify the grid only iterates this mode's library.
- [ ] **Step 5: Build-verify** — `build_check.bat` → BUILD OK. (Still one instance wired in MainWindow until Phase 7; behavior unchanged for the "movies"/default instance.)
- [ ] **Step 6: Commit** — `git commit -m "refactor(video-split): StreamPage is mode-scoped (ctor StreamMode + hub library)"`

### Task 11: Mode-scope `StreamContinueStrip` + `StreamSearchWidget`

**Files:** Modify `src/ui/pages/stream/StreamContinueStrip.{h,cpp}` (hardcoded `allProgress("stream")` ≈ :79), `src/ui/pages/stream/StreamSearchWidget.{h,cpp}` (results split ≈ :69-77).

- [ ] **Step 1:** Add a domain string to `StreamContinueStrip` via ctor/setter (default `"stream"` to keep any other caller working). Replace the hardcoded `allProgress("stream")` with `allProgress(m_domain)`. The owning `StreamPage` passes `streamModeKey(mode)`.
- [ ] **Step 2:** `StreamSearchWidget` already splits results into movies + series sections — add an **anime** section (shown only in Anime mode) and a `setMode(StreamMode)` that filters which sections render. In TV mode show only series; Movies only movies; Anime only the anime section.
- [ ] **Step 3: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 4: Commit** — `git commit -m "feat(video-split): per-mode Continue strip domain + search sections"`

---

## Phase 6 — Detail view per-mode behavior

### Task 12: Anime extras (Ep N vs SxEy, season gating, sub/dub preference)

**Files:** Modify `src/ui/pages/stream/StreamDetailView.{h,cpp}`.

> **Pre-task read:** read `StreamDetailView.cpp` in full (≈ 4666 lines). It already branches on `m_currentType` + `m_isAnime` (season combo hidden for anime ≈ :1019-1020; episode label ≈ :1216). Contain new code in private helpers to limit blast radius (Risk #7).

- [ ] **Step 1:** Add a `StreamMode m_mode` (set by the owning page). Format the episode column label as `"Ep N"` (anime, absolute) vs `"S{n}E{m}"` (TV) at ≈ :1216, driven by `m_mode == StreamMode::Anime`. Keep the season combo hidden for Anime (already driven by `m_isAnime`) **and** Movies; shown for TV.
- [ ] **Step 2:** For Movies mode, render the single-file play affordance (no season/episode list) — reuse the existing movie branch; ensure no episode UI shows.
- [ ] **Step 3: Sub/dub preference (D8).** Add a SUB/DUB toggle, visible **only** in Anime mode, that prioritizes sources whose release name matches the chosen track (`[SUB]`/`[DUB]`/`Dual`/`Dual-Audio` — case-insensitive substring) when ranking/sorting the sources list. Implement as a sort key over the already-fetched Torrentio/Nyaa results — **no new fetch**, no metadata dependency. If neither tag is present, leave order unchanged.
- [ ] **Step 4: Avoid progress-key collision.** Anime keys are absolute (no season); TV keys are season-relative. Confirm Task 5's domain prefixing (`anime:`/`tv:`) keeps them in separate domains so an absolute `Ep 12` cannot collide with a TV `S1E12`.
- [ ] **Step 5: Build-verify** — `build_check.bat` → BUILD OK.
- [ ] **Step 6: Commit** — `git commit -m "feat(video-split): anime detail extras (Ep N, sub/dub preference) + movie/TV gating"`

---

## Phase 7 — MainWindow nav wiring (SHARED FILE — append-only, AFTER Agent 1)

> **SHARED-FILE COORDINATION (do not skip):** `src/ui/MainWindow.cpp` is edited by **both** arcs.
> 1. **Land Agent 1's Phase 4 first.** Agent 1 renames `ComicsPage`→`MangaPage` across the exact methods this task touches (`activatePage` qobject_cast ≈ :1083, `resetActivePageToRoot` ≈ :1115, `onLayerRestoreRequested` findChild ≈ :1184). Doing video first forces him to re-rename our additions. **Before starting this phase: `git pull`/sync, confirm `MangaPage` is in the tree, and post a line in `agents/chat.md` claiming the nav block.**
> 2. **navDefs is line-disjoint IF order is preserved.** Agent 1 leaves `{ PAGE_STREAM, "Theatre" }` as the **last** entry (his array = `Manga, Comics, Books, Theatre`). We replace **only that trailing line** with three entries. Final array = `Manga, Comics, Books, Anime, TV, Movies`. **Agree this exact order + the Ctrl+1..6 map with Agent 1 before either edits** (D5).
> 3. **Append, don't reformat.** Add our cast/activate branches at distinct points in the existing if-chains; do not reflow Agent 1's blocks.

### Task 13: Constants, pills, page stack, dispatch, keybinds

**Files:** Modify `src/ui/MainWindow.{cpp,h}`.

- [ ] **Step 1: Constants (≈ :63-75).** Add a contiguous block **after** `PAGE_BOOKS` (not interleaved with the comics constants Agent 1 touches):

```cpp
static constexpr const char *PAGE_ANIME  = "anime";
static constexpr const char *PAGE_TV     = "tv";
static constexpr const char *PAGE_MOVIES = "movies";
// PAGE_STREAM retired as a pill (six-mode 2026-06-07); PAGE_STREAM_DOWNLOADS kept,
// shared by the three video modes (back-target = launching mode).
```

- [ ] **Step 2: navDefs (≈ :531-542).** Replace the single trailing `{ PAGE_STREAM, "Theatre" }` with:

```cpp
        { PAGE_ANIME,  "Anime"  },
        { PAGE_TV,     "TV"     },
        { PAGE_MOVIES, "Movies" },
```

(Final array, after Agent 1: `Manga, Comics, Books, Anime, TV, Movies`.)

- [ ] **Step 3: buildPageStack (≈ :838-921).** Replace the single `StreamPage` block with: one `VideoLibraryHub` (shared `JsonStore`), then three `StreamPage` instances over the **shared** `m_bridge`/`torrentClient`/`StreamDownloadIndex`/`MetaAggregator`, each with its `StreamMode` + `hub`. Add each to `m_pageStack`. Wire each page's `enteredLayer`/`exitedLayer` to `m_navController->pushLayer/popLayer` with **its own** pageId (`streamModeKey(mode)`). Cache pointers (`m_animePage`/`m_tvPage`/`m_moviesPage` in `MainWindow.h`, replacing `m_streamPage`).
- [ ] **Step 4: PerModeNavController roots.** Call `m_navController->setRootLayer(pageId, { pageId, "library", "Library" })` for `"anime"`, `"tv"`, `"movies"` (mirrors the existing `"stream"` seed). No controller code changes (it is generic by pageId — verified).
- [ ] **Step 5: activatePage (≈ :1039-1098).** The objectName loop already finds the new pages. Extend the post-switch `qobject_cast` + `activate()` dispatch to cover all three (one cast type — `StreamPage*` — since D1; check `pageId` to pick behavior if needed). Extend the **Organise-button visibility** (≈ :1062) and **sidebar-downloads visibility** (≈ :1087-1093) conditionals to treat `anime`/`tv`/`movies` like the old `stream` (Risk #11).
- [ ] **Step 6: resetActivePageToRoot (≈ :1111-1125)** + **onLayerRestoreRequested (≈ :1183-1208).** Add `anime`/`tv`/`movies` branches calling `StreamPage::resetToRoot()` / the layer-restore path. Append after Agent 1's `MangaPage`/`WesternComicsPage` branches.
- [ ] **Step 7: StreamDownloadsPage back-target (D3).** Where `PAGE_STREAM_DOWNLOADS` `backRequested` → `activatePage(PAGE_STREAM)` (≈ :928-944), change the target to the recorded launching video mode (store it when the downloads page is opened; default `PAGE_MOVIES`).
- [ ] **Step 8: bindShortcuts (≈ :985-1004) — JOINTLY AGREED, append after Agent 1.** Map `Ctrl+4`=Anime, `Ctrl+5`=TV, `Ctrl+6`=Movies; move the sidebar toggle off `Ctrl+5` to **`Ctrl+0`**. (If Agent 1 already added our three binds in his keybind task per the agreement, this step is a no-op verification.)
- [ ] **Step 9: Build-verify** — `build_check.bat` → BUILD OK. Verify each new page compiles + `out/Tankoban.exe` mtime advanced.
- [ ] **Step 10: Commit (small + early)** — `git commit -m "feat(video-split): register Anime + TV + Movies as top-level modes; retire Theatre pill"` then **push** so Agent 1 can rebase.

---

## Phase 8 — Migration guard + verification

### Task 14: Start-fresh guard (video side)

**Files:** Verify (mostly no-op by design) across `VideoLibraryHub.cpp` + the three pages.

- [ ] **Step 1:** Confirm the three pages read **only** `anime_library.json`/`tv_library.json`/`movies_library.json` (new files → empty on first run) and that **nothing** reads `stream_library.json` into a new page. Leave `stream_library.json` on disk untouched (no rename — preserves rollback).
- [ ] **Step 2:** Confirm `StreamDownloadIndex` legacy rows still resolve for playback (disk-first) even though they predate `animeFlag`; downloaded files on disk untouched.
- [ ] **Step 3:** Add a brief one-time user-facing note (toast or first-run line) that the new video libraries start fresh — so an empty Theatre-successor library reads as intended, not as data loss (Risk #9). Keep it Hemanth-language (e.g. "Your Anime / TV / Movies libraries start fresh — your downloaded files are safe on disk.").
- [ ] **Step 4: Commit** — `git commit -m "feat(video-split): start-fresh guard + first-run note (downloaded files untouched)"`

### Task 15: Build + classification smoke + cross-model review

- [ ] **Step 1: Build-verify (false-green guard)** — `build_check.bat` → BUILD OK; confirm `.obj` exists for `StreamMode.cpp`, `VideoLibraryHub.cpp`, and any new page TUs (new-source false-green memory).
- [ ] **Step 2: Headless state check** — `build_and_run.bat`, then `out\tankoctl.exe ping` + `introspect-tree` (or `get-modes`): six pills present (`Manga · Comics · Books · Anime · TV · Movies`); `stream-*` snapshots return per-mode libraries.
- [ ] **Step 3: Classification smoke (DoD, spec §10 line 129)** — verify on real titles:
  - **Demon Slayer** (anime series) → **Anime**.
  - An **anime film** (e.g. *A Silent Voice* / *Your Name*) → **Anime**.
  - A **non-anime series** (e.g. *Breaking Bad*) → **TV**.
  - A **film** (e.g. *Inception*) → **Movies**.
  - No cross-mode bleed in libraries or Continue strips; pill-from-deep-view resets to that mode's root; anime detail shows `Ep N` + sub/dub toggle; TV shows seasons; Movies shows single-file.
- [ ] **Step 4: Pre-warm smoke** — cold start, re-open a previously-added anime title; confirm (via the Task-7 log marker) the kitsu route builds from the stored `kitsuId` with no live re-fetch.
- [ ] **Step 5: Regression check** — Books unchanged; comic reader unchanged; video player + torrent engine behave as before (play a movie + a TV episode end-to-end).
- [ ] **Step 6: Cross-model review (producer ≠ reviewer)** — `/codex-review` the full diff against this plan's Definition of Done. Address every NOT-MET. (Threading/engine-sharing changes especially — route through review per the domain norm.)
- [ ] **Step 7: RTC + close** — post a contracts-v3 RTC line to `agents/chat.md` (`Done-when:` = the DoD below) with `Skills invoked:` provenance. Hemanth's smoke on the running app is the final gate.

---

## Definition of Done

- **Six mode pills** in the topbar (`Manga · Comics · Books · Anime · TV · Movies`); each opens its own page with its own per-mode back-stack.
- **Anime / TV / Movies correctly classified:** every anime title (series **or** film) lands in Anime; non-anime series in TV; non-anime films in Movies — verified on real titles (Demon Slayer + an anime film → Anime; Breaking Bad → TV; Inception → Movies).
- **Anime extras present:** sub/dub preference + `Ep N` absolute numbering + flat episode list (no Cinemeta season chips); TV shows seasons/episodes; Movies single-file.
- Each video mode has its **own catalog brain** (Anime: Kitsu/AniList + Amatsu/Nyaa; TV/Movies: Cinemeta) + **own library + Continue strip**; no cross-mode bleed.
- **Persisted discriminator:** `animeFlag` + resolved `kitsuId` survive restart; re-opening an anime title builds the kitsu play route with no live re-fetch.
- **No data loss / start fresh:** the three new libraries begin empty; old `stream_library.json` left in place; downloaded files on disk untouched; legacy downloads still play.
- **No regression:** Books, the comic reader, the video player, and the torrent engine behave as before.
- `build_check.bat` green; unit tests pass (StreamMode, library serialization, hub routing, progress domains); `/codex-review` APPROVE; **Hemanth smoke on the running app passes** (the only done-gate).

---

## Self-Review (author pass)

- **Spec coverage (§5 + §6 + §9 + §10):**
  - §5 classification rule → Tasks 1, 6 (decoupled from the 5-season gate — a correction the spec implies but does not state).
  - §5 Anime catalog brain + extras → Tasks 8 (catalog), 11 (search section), 12 (Ep N + sub/dub).
  - §5 TV/Movies Cinemeta → Task 8.
  - §5 per-mode storage (3 libraries + 3 Continue domains) → Tasks 4, 5, 9, 11.
  - §5 pre-warm (`AnimeIdMapCache`) → Task 7 (scope-corrected: map persist/prewarm already exist; real work = persist resolved `kitsuId`).
  - §6 nav wiring (3 PAGE_* + navDefs + buildPageStack + PerModeNav) → Task 13.
  - §6 migration (video = start fresh) → Task 14.
  - §9 file refs → cited inline per task. §10 DoD → Tasks 13 (pills) + 15 (classification/no-bleed/no-regression).
- **Contradiction caveats baked in:** topology (3 real pages, not internal sub-modes — D1); 5-season reroute gate ≠ classification (D6); `animeFlag` not persisted today → Tasks 2/3/6; `StreamLibrary` filename was compile-time constant → Task 4; CoreBridge progress hardcoded to 2 domains → Task 5; sub/dub has no metadata backing → D8/Task 12.
- **Placeholder scan:** pure-logic/mechanical tasks (1, 2, 4, 5, 9) carry complete code. The large-page tasks (10, 12) deliberately say "read the real file, then parameterize at these anchors" rather than reproduce thousands of unread lines — the honest shape for a 4666-line-file refactor (mirrors Arc 1's handling of `ComicsPage`). The Phase-5/6 pre-task read note makes this explicit. Not a placeholder gap.
- **Type consistency:** `classifyStreamMode(bool, QString)`, `streamModeKey`, `streamModeFromKey`, `streamLibraryFilename`, `StreamMode{Anime,TV,Movies}`, `VideoLibraryHub::{libraryForMode,addClassified,removeEverywhere}`, `StreamLibraryEntry::{animeFlag,kitsuId}`, `MetaAggregator::entryResolved(imdb,kitsuId,isAnime)` — used identically across Tasks 1, 2, 6, 9, 10, 13.
- **Shared-file safety:** Phase 7 is gated behind Agent 1's Phase 4 landing + a chat.md claim + an agreed 6-pill order + append-only edits. The navDefs diff is one trailing line replaced → line-disjoint from Agent 1's top-of-array additions.
- **Heaviness guard:** D1 mandates deferred per-instance work (no eager catalog fetch / detail build in ctor) — directly relevant given the open idle/startup-cost tickets in this domain; verified at Task 15 smoke.
