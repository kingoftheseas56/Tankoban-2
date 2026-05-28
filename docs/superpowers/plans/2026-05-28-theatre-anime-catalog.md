# Theatre Anime Catalog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make anime (Animation + Japan) in Theatre auto-read from the Kitsu anime catalog — one flat absolute-numbered episode list + an "Anime" badge — at every entry point, with Torrentio streaming and batch-torrent downloads.

**Architecture:** A new pure-logic `AnimeCatalogResolver` detects anime from Cinemeta meta and bridges its IMDb id to a Kitsu id (cached Fribb mapping table, with a title-search-confirm fallback). `MetaAggregator` reroutes detected anime to a seeded Anime Kitsu meta addon (which already returns all-`season=1` absolute episodes), and emits an `isAnime` signal the series view consumes for the badge + chip-hide. Streams thread the `kitsu:<id>:<ep>` id to the existing Torrentio addon; downloads search broad batch queries and select an episode range via libtorrent file-priority.

**Tech Stack:** C++17 / Qt6 / GoogleTest (`tankoban_tests`). Existing Stremio addon layer (`src/core/stream/addon/`), `MetaAggregator`, `StreamAggregator`, `TheatreDownloadPanel`. Build via `build_check.bat`; per-lane build dir `out_agent4`.

**Spec:** `docs/superpowers/specs/2026-05-28-theatre-anime-catalog-design.md` (APPROVED 2026-05-28).

---

## Brotherhood execution notes (override the generic skill defaults)

- **Worktree (gov-v9 Path B).** This arc runs in a dedicated worktree on branch `agent4/theatre-anime-catalog` (isolated git index). Self-commit freely per task — own the history.
- **Build.** Builds run in the worktree's own checkout (naturally isolated). Always `taskkill /F /IM Tankoban.exe` before a rebuild (Rule 1 — running exe silently no-ops the link).
- **Commits + integration.** Per-task self-commits use `THEATRE_ANIME_CATALOG TN.M: <desc>` tags (Path B — no per-phase RTC needed). The single coordination point is the final merge to master: claim the build lane + self-merge, or post `READY TO MERGE — [Agent 4, agent4/theatre-anime-catalog]` for Agent 0's merge-sweep (Rule 16). The per-phase "READY TO COMMIT" lines later in this plan are superseded — treat each as "verify build-green + self-commit".
- **Build command:** `$env:TANKOBAN_BUILD_LANE="agent4"; .\build_check.bat` → expect tail `BUILD OK`.
- **Test command:** `$env:TANKOBAN_BUILD_LANE="agent4"; cmake -S . -B out_agent4 -DTANKOBAN_BUILD_TESTS=ON; cmake --build out_agent4 --target tankoban_tests` then run the test exe with Qt's `bin` on PATH (the CMake post-build ctest auto-discovery needs `Qt6Core.dll` reachable — see the COMIC_READER pairing-test lesson). Filter: `ctest --test-dir out_agent4 -R <suite> --output-on-failure`.

## Grounding flags for the executor (read before the relevant phase)

- **Phases 1–2 are fully specified below** (pure logic + signatures verified in planning).
- **Phases 3–5 touch UI / play-path / download surfaces NOT fully read during planning** (deliberate — fabricated widget signatures were the failure mode this arc avoided). Each of those tasks names the exact file to **read first** and states the contract to satisfy. Finalize the exact edit after reading the named file at task start.

## File structure

| File | Responsibility | Status |
|---|---|---|
| `src/core/stream/AnimeCatalogResolver.{h,cpp}` | NEW. Pure detection predicate + `AnimeIdMap` (Fribb table parse/lookup) + async bridge orchestration (table → search-confirm) | create |
| `src/core/stream/AnimeIdMapCache.{h,cpp}` | NEW. File-backed cache for the Fribb mapping JSON (mirrors `AniListCache` pattern) | create |
| `tests/core/stream/test_anime_catalog_resolver.cpp` | NEW. Pure-logic tests: detection, map parse/lookup, confirm predicate | create |
| `src/core/stream/addon/MetaItem.h` | Add `QString country` to `MetaItemPreview` | modify |
| `src/core/stream/addon/<meta JSON parser>` | Parse `meta.country` into `MetaItemPreview.country` | modify (locate at task start) |
| `src/core/stream/addon/AddonRegistry.cpp` | Seed the Anime Kitsu addon (meta+catalog) | modify (near `:705`) |
| `src/core/stream/MetaAggregator.{h,cpp}` | Reroute detected anime to Anime Kitsu; emit `animeCatalogActive(imdbId,isAnime)` | modify |
| `src/ui/pages/stream/TheatreDownloadPanel.cpp` + series/detail view | Flat list, hide season chips, "Anime" badge, episode-range picker | modify (read first) |
| `src/core/stream/StreamAggregator.cpp` | Anime batch query strategy + lift 25-cap for batch path | modify (`:683`, `:704-712`) |
| play path (stream request build) | Thread `kitsu:<id>:<ep>` to Torrentio for anime | modify (locate at task start) |
| `QualityScorer` / `PackClassifier` / `TitleMetadataEstimator` | Bias against live-action contamination on anime results | modify (read first) |

---

## Phase 1 — AnimeCatalogResolver core (pure logic, TDD)

### Task 1.1: Anime detection predicate

**Files:**
- Create: `src/core/stream/AnimeCatalogResolver.h`, `src/core/stream/AnimeCatalogResolver.cpp`
- Test: `tests/core/stream/test_anime_catalog_resolver.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "core/stream/AnimeCatalogResolver.h"

using tankostream::stream::isAnimeSeries;

TEST(AnimeDetect, AnimationPlusJapanIsAnime) {
    EXPECT_TRUE(isAnimeSeries({"Animation", "Action", "Adventure"}, "Japan"));
}
TEST(AnimeDetect, AnimationNonJapanIsNot) {
    EXPECT_FALSE(isAnimeSeries({"Animation", "Comedy"}, "United States"));
}
TEST(AnimeDetect, JapanNonAnimationIsNot) {
    EXPECT_FALSE(isAnimeSeries({"Drama"}, "Japan"));
}
TEST(AnimeDetect, CaseInsensitive) {
    EXPECT_TRUE(isAnimeSeries({"animation"}, "japan"));
}
TEST(AnimeDetect, EmptyIsNot) {
    EXPECT_FALSE(isAnimeSeries({}, ""));
}
```

- [ ] **Step 2: Write the header**

```cpp
// src/core/stream/AnimeCatalogResolver.h
#pragma once
#include <QString>
#include <QStringList>

namespace tankostream::stream {
// A series is "anime" for catalog-reroute purposes when its Cinemeta meta is
// animation AND originates in Japan. Genre-only would wrongly catch western
// cartoons; country-only would catch live-action JP drama.
bool isAnimeSeries(const QStringList& genres, const QString& country);
}
```

- [ ] **Step 3: Implement**

```cpp
// src/core/stream/AnimeCatalogResolver.cpp
#include "core/stream/AnimeCatalogResolver.h"
#include <algorithm>

namespace tankostream::stream {
bool isAnimeSeries(const QStringList& genres, const QString& country) {
    const bool animation = std::any_of(
        genres.cbegin(), genres.cend(), [](const QString& g) {
            return g.compare(QStringLiteral("Animation"), Qt::CaseInsensitive) == 0;
        });
    const bool japan =
        country.trimmed().compare(QStringLiteral("Japan"), Qt::CaseInsensitive) == 0;
    return animation && japan;
}
}
```

- [ ] **Step 4: Wire the test target.** Add `test_anime_catalog_resolver.cpp` to the `tankoban_tests` source list and `AnimeCatalogResolver.cpp` to the app + test build in `CMakeLists.txt` (grep for `test_quality_scorer.cpp` and add the new file beside it; add `AnimeCatalogResolver.cpp` beside `StreamAggregator.cpp` in the stream-core source list). **Grep-verify the edit landed (CMakeLists multi-agent collision risk).**

- [ ] **Step 5: Build + run the test**

Run: `$env:TANKOBAN_BUILD_LANE="agent4"; cmake -S . -B out_agent4 -DTANKOBAN_BUILD_TESTS=ON; cmake --build out_agent4 --target tankoban_tests` then `ctest --test-dir out_agent4 -R AnimeDetect --output-on-failure` (Qt bin on PATH).
Expected: 5/5 PASS.

- [ ] **Step 6: Commit** — `THEATRE_ANIME_CATALOG T1.1: anime detection predicate (Animation+Japan) + test target`

### Task 1.2: AnimeIdMap — Fribb table parse + IMDb→Kitsu lookup

**Files:**
- Modify: `src/core/stream/AnimeCatalogResolver.h/.cpp` (add `AnimeIdMap`)
- Test: `tests/core/stream/test_anime_catalog_resolver.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include "core/stream/AnimeCatalogResolver.h"
using tankostream::stream::AnimeIdMap;

TEST(AnimeIdMap, ParsesImdbToKitsu) {
    AnimeIdMap m;
    m.loadFromJson(QByteArrayLiteral(
        R"([{"imdb_id":"tt0388629","kitsu_id":12,"mal_id":21},)"
        R"({"imdb_id":"tt0409591","kitsu_id":11}])"));
    EXPECT_EQ(m.kitsuIdForImdb("tt0388629").value_or(-1), 12);
    EXPECT_EQ(m.kitsuIdForImdb("tt0409591").value_or(-1), 11);
    EXPECT_FALSE(m.kitsuIdForImdb("tt9999999").has_value());
}
TEST(AnimeIdMap, SkipsEntriesWithoutImdbOrKitsu) {
    AnimeIdMap m;
    m.loadFromJson(QByteArrayLiteral(
        R"([{"kitsu_id":5},{"imdb_id":"","kitsu_id":6},{"imdb_id":"tt7"}])"));
    EXPECT_EQ(m.size(), 0);
}
```

- [ ] **Step 2: Add the declaration** to `AnimeCatalogResolver.h`

```cpp
#include <QByteArray>
#include <QHash>
#include <optional>

namespace tankostream::stream {
// Parses the Fribb/anime-lists "anime-list-full.json" array into an
// imdb_id -> kitsu_id lookup. Entries lacking either a non-empty imdb_id
// string or an integer kitsu_id are skipped. imdb_id may be string-only in v1.
class AnimeIdMap {
public:
    void loadFromJson(const QByteArray& json);
    std::optional<int> kitsuIdForImdb(const QString& imdbId) const;
    int size() const { return m_imdbToKitsu.size(); }
private:
    QHash<QString, int> m_imdbToKitsu;
};
}
```

- [ ] **Step 3: Implement** in `AnimeCatalogResolver.cpp`

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

void AnimeIdMap::loadFromJson(const QByteArray& json) {
    m_imdbToKitsu.clear();
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        const QString imdb = o.value(QStringLiteral("imdb_id")).toString().trimmed();
        const QJsonValue kitsu = o.value(QStringLiteral("kitsu_id"));
        if (imdb.isEmpty() || !kitsu.isDouble()) continue;
        m_imdbToKitsu.insert(imdb, kitsu.toInt());
    }
}
std::optional<int> AnimeIdMap::kitsuIdForImdb(const QString& imdbId) const {
    auto it = m_imdbToKitsu.constFind(imdbId);
    if (it == m_imdbToKitsu.constEnd()) return std::nullopt;
    return *it;
}
```

- [ ] **Step 4: Build + run** — `ctest --test-dir out_agent4 -R AnimeIdMap --output-on-failure`. Expected: 2/2 PASS.

- [ ] **Step 5: Commit** — `THEATRE_ANIME_CATALOG T1.2: AnimeIdMap Fribb-table parse + IMDb->Kitsu lookup`

### Task 1.3: Kitsu-match confirm predicate (fallback safety)

**Files:** Modify `AnimeCatalogResolver.{h,cpp}`; Test same file.

- [ ] **Step 1: Write the failing test**

```cpp
using tankostream::stream::confirmsKitsuMatch;
TEST(KitsuConfirm, ImdbRoundTripEqualMatches) {
    EXPECT_TRUE(confirmsKitsuMatch("tt0388629", "tt0388629"));
}
TEST(KitsuConfirm, MismatchOrEmptyRejected) {
    EXPECT_FALSE(confirmsKitsuMatch("tt0388629", "tt1234567"));
    EXPECT_FALSE(confirmsKitsuMatch("tt0388629", ""));
    EXPECT_FALSE(confirmsKitsuMatch("", "tt0388629"));
}
```

- [ ] **Step 2: Declare + implement**

```cpp
// header
namespace tankostream::stream {
// The search-confirm fallback resolves a Kitsu candidate by title, then
// confirms it by the imdb_id Anime Kitsu embeds in its meta. Match only when
// both ids are non-empty and equal — never trust a title hit alone.
bool confirmsKitsuMatch(const QString& wantedImdb, const QString& kitsuMetaImdb);
}
// cpp
bool confirmsKitsuMatch(const QString& wantedImdb, const QString& kitsuMetaImdb) {
    return !wantedImdb.isEmpty() && wantedImdb == kitsuMetaImdb;
}
```

- [ ] **Step 3: Build + run** — `ctest --test-dir out_agent4 -R KitsuConfirm --output-on-failure`. Expected: 2/2 PASS.

- [ ] **Step 4: Commit** — `THEATRE_ANIME_CATALOG T1.3: Kitsu-match confirm predicate`

### Task 1.4: AnimeIdMapCache — file-backed Fribb cache

**Files:**
- Create: `src/core/stream/AnimeIdMapCache.{h,cpp}` (mirror `AniListCache` shape: ctor takes `cacheDir`, `QMutex`-guarded, load-from-disk on construct, save off-lock)
- Test: extend `test_anime_catalog_resolver.cpp` with a parse-roundtrip test against a tmp dir.

- [ ] **Step 1: Write the failing test** — write a small JSON to a `QTemporaryDir`, construct the cache pointed at it, assert `kitsuIdForImdb("tt0388629") == 12`. (Network refresh is NOT unit-tested; only the file load + lookup.)

```cpp
#include <QTemporaryDir>
#include "core/stream/AnimeIdMapCache.h"
using tankostream::stream::AnimeIdMapCache;
TEST(AnimeIdMapCache, LoadsCachedFileAndLooksUp) {
    QTemporaryDir dir;
    QFile f(dir.filePath("anime-id-map.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral(R"([{"imdb_id":"tt0388629","kitsu_id":12}])"));
    f.close();
    AnimeIdMapCache cache(dir.path());
    EXPECT_EQ(cache.kitsuIdForImdb("tt0388629").value_or(-1), 12);
}
```

- [ ] **Step 2: Implement** the cache: ctor reads `<cacheDir>/anime-id-map.json` if present into an `AnimeIdMap` (Task 1.2); expose `std::optional<int> kitsuIdForImdb(const QString&) const` (mutex-guarded); add `void refreshAsync(QNetworkAccessManager*)` that GETs `https://raw.githubusercontent.com/Fribb/anime-lists/master/anime-list-mini.json`, writes it to disk, reloads, emits `refreshed()`. TTL helper `bool isStale(qint64 maxAgeMs)` off the file mtime. (Refresh is integration; not unit-tested.)

- [ ] **Step 3: Wire test target + build + run** — add to CMake; `ctest --test-dir out_agent4 -R AnimeIdMapCache --output-on-failure`. Expected: PASS.

- [ ] **Step 4: Commit** — `THEATRE_ANIME_CATALOG T1.4: file-backed Fribb IMDb->Kitsu cache`

### Phase 1 boundary
- [ ] Full app build green: `$env:TANKOBAN_BUILD_LANE="agent4"; .\build_check.bat` → `BUILD OK`.
- [ ] Post `READY TO COMMIT — [Agent 4, THEATRE_ANIME_CATALOG_P1]` to `agents/chat.md` with the file list + `Skills invoked: [superpowers:test-driven-development, build-verify, verification-before-completion, simplify]`.

---

## Phase 2 — Data plumbing: country field, addon seed, reroute wiring

### Task 2.1: Add `country` to MetaItemPreview + parse it

**Files:**
- Modify: `src/core/stream/addon/MetaItem.h` (add field, after `genres`)
- Modify: the meta-JSON parser that builds `MetaItemPreview` — **read first** to locate (grep `genres` assignment under `src/core/stream/addon/`; likely `AddonTransport.cpp` or a `Meta*Parser`). Add `preview.country = obj.value("country").toString().trimmed();`.

- [ ] **Step 1:** Add to `MetaItemPreview` in `MetaItem.h`:

```cpp
    QStringList genres;
    QString country;   // origin country, e.g. "Japan" — anime detection signal
```

- [ ] **Step 2:** Grep `src/core/stream/addon/` for where `preview.genres` (or `.genres =`) is assigned during meta parse; add the `country` parse on the adjacent line.

- [ ] **Step 3:** Build: `$env:TANKOBAN_BUILD_LANE="agent4"; .\build_check.bat` → `BUILD OK`.

- [ ] **Step 4: Commit** — `THEATRE_ANIME_CATALOG T2.1: parse origin country into MetaItemPreview`

### Task 2.2: Seed the Anime Kitsu addon

**Files:** Modify `src/core/stream/addon/AddonRegistry.cpp` (in `seedDefaults`, after the Torrentio block near `:705`).

- [ ] **Step 1:** Add a descriptor mirroring the Torrentio seed block:

```cpp
AddonDescriptor animeKitsu;
animeKitsu.transportUrl = QUrl(QStringLiteral("https://anime-kitsu.strem.fun/manifest.json"));
animeKitsu.flags.official = false;
animeKitsu.flags.enabled = true;
animeKitsu.flags.protectedAddon = false;
animeKitsu.manifest.id = QStringLiteral("community.anime.kitsu");
animeKitsu.manifest.version = QStringLiteral("0.0.10");
animeKitsu.manifest.name = QStringLiteral("Anime Kitsu");
animeKitsu.manifest.types = {
    QStringLiteral("anime"), QStringLiteral("series"), QStringLiteral("movie"),
};
{
    ManifestResource metaRes;
    metaRes.name = QStringLiteral("meta");
    metaRes.hasTypes = true;
    metaRes.types = {QStringLiteral("series"), QStringLiteral("movie"), QStringLiteral("anime")};
    metaRes.hasIdPrefixes = true;
    metaRes.idPrefixes = {QStringLiteral("kitsu"), QStringLiteral("mal"),
                          QStringLiteral("anilist"), QStringLiteral("anidb")};
    ManifestResource catalogRes;
    catalogRes.name = QStringLiteral("catalog");
    animeKitsu.manifest.resources = {metaRes, catalogRes};
}
m_addons.push_back(animeKitsu);
```

- [ ] **Step 2:** Build → `BUILD OK`. **Smoke:** `out\tankoctl.exe` (or a debug print) to confirm the addon list now includes `community.anime.kitsu`. (Verify after the existing addon-storage merge — installed-addon state may need a reset; if a stale stored set hides the seed, document the reset path.)

- [ ] **Step 3: Commit** — `THEATRE_ANIME_CATALOG T2.2: seed Anime Kitsu meta+catalog addon`

### Task 2.3: Reroute detected anime in MetaAggregator

**Files:** Modify `src/core/stream/MetaAggregator.{h,cpp}` — **read `fetchSeriesMeta` (cpp:261-305), `dispatchSeriesMeta`, `finalizeSeries`, and the series-meta response parse first.**

**Contract:**
- In the series-meta response handler, also parse top-level `genres` + `country` from the Cinemeta payload (same JSON `parseSeriesEpisodes` reads).
- If `isAnimeSeries(genres, country)` (Task 1.1): resolve a Kitsu id via the resolver — `AnimeIdMapCache::kitsuIdForImdb(imdbId)`, else the async `searchByTitle` + `confirmsKitsuMatch` fallback. On success, fetch Anime Kitsu meta `meta/series/kitsu:<id>.json`, run it through `parseSeriesEpisodes` (all `season=1` → single flat list), and emit `seriesMetaReady(imdbId, seasons)` as today PLUS a new `animeCatalogActive(imdbId, true)`.
- On any failure (no kitsu id, Kitsu unreachable), fall through to the existing Cinemeta path and emit `animeCatalogActive(imdbId, false)`.
- Entry id stays `tt`; do NOT loosen the tt-only gate at `cpp:263` for general callers — the Kitsu meta fetch is an internal dispatch with the resolved kitsu id.

- [ ] **Step 1:** Add signal to `MetaAggregator.h`: `void animeCatalogActive(const QString& imdbId, bool isAnime);`
- [ ] **Step 2:** Inject an `AnimeCatalogResolver`/`AnimeIdMapCache` dependency (ctor or setter) so it's testable + mockable.
- [ ] **Step 3:** Implement the reroute per the contract above (read the response handler first to match its exact shape).
- [ ] **Step 4:** Build → `BUILD OK`.
- [ ] **Step 5: Commit** — `THEATRE_ANIME_CATALOG T2.3: reroute anime series-meta to Anime Kitsu + animeCatalogActive signal`

### Phase 2 boundary
- [ ] Build green. Post `READY TO COMMIT — [Agent 4, THEATRE_ANIME_CATALOG_P2]` with `Skills invoked: [build-verify, verification-before-completion, simplify, security-review]` (security-review: addon-seed touches a network-fetched source).

---

## Phase 3 — Series page: flat list + hide chips + Anime badge

**Files:** Modify the Theatre series/detail view + `src/ui/pages/stream/TheatreDownloadPanel.cpp`. **Read both first** — locate where season tabs/chips render and where `seriesMetaReady` is consumed.

**Contract:**
- Connect `MetaAggregator::animeCatalogActive(imdbId, isAnime)`. When `isAnime` for the open series:
  - Render the single-season map as one continuous absolute-numbered list (the QMap has one key `1`; existing rendering should already produce a flat list — verify it does not show a "Season 1" chrome it would for a 1-season show).
  - Hide the season / multi-season chips (they're meaningless).
  - Show a small **"Anime"** badge near the title/source row (mirror an existing badge widget's style — grep for an existing chip/badge in this view).
- When `!isAnime`, behavior is byte-for-byte unchanged.

- [ ] **Step 1:** Read the series/detail view + `TheatreDownloadPanel.cpp`; identify the chip-render block + a reusable badge style.
- [ ] **Step 2:** Add the `animeCatalogActive` connection + an `m_isAnime` member; gate chip visibility on it; add the badge.
- [ ] **Step 3:** Build → `BUILD OK`.
- [ ] **Step 4:** Hemanth visual smoke gate (Phase 6) covers this — note it; do not claim visual success from a compile.
- [ ] **Step 5: Commit** — `THEATRE_ANIME_CATALOG T3: flat anime list + hide season chips + Anime badge`

### Phase 3 boundary
- [ ] Post `READY TO COMMIT — [Agent 4, THEATRE_ANIME_CATALOG_P3]`.

---

## Phase 4 — Watching: thread kitsu id to Torrentio + contamination bias

**Files:** the stream-request build / play path (**locate at task start** — grep for where an episode play builds the Torrentio stream request id, likely `StreamAggregator` or the play controller); `QualityScorer` / `PackClassifier` / `TitleMetadataEstimator` (**read first**).

**Contract:**
- For an anime series (carry the resolved kitsu id alongside the open series), an episode play must request streams keyed `kitsu:<kitsuId>:<episode>` from Torrentio instead of `tt<id>:<season>:<episode>`. (Torrentio is already seeded with `kitsu` idPrefix + `anime` type — verified; no addon change needed.)
- Bias ranking AGAINST live-action contamination: when the series is anime, down-rank results whose title parses as live-action (e.g. `S01E01` western pattern, `WEB-DL`/`NF` without fansub markers) and up-rank fansub/anime-tagged results. This is ranking, not exclusion.

- [ ] **Step 1:** Read the play/stream-request path + the scorer; find the id-build site + the ranking site.
- [ ] **Step 2:** Thread the kitsu id + episode through to the Torrentio stream request for anime.
- [ ] **Step 3:** Add the anime-aware ranking bias (pure-logic helper → add a unit test for the bias predicate in `tests/core/stream/test_quality_scorer.cpp` or the estimator test).
- [ ] **Step 4:** Build + run the affected scorer tests → PASS; `BUILD OK`.
- [ ] **Step 5: Commit** — `THEATRE_ANIME_CATALOG T4: kitsu stream ids + anime contamination ranking bias`

### Phase 4 boundary
- [ ] Post `READY TO COMMIT — [Agent 4, THEATRE_ANIME_CATALOG_P4]` with `Skills invoked: [..., security-review]` (touches stream-source input).

---

## Phase 5 — Downloading: batch query strategy + range picker

### Task 5.1: Anime batch query strategy + lift the 25-cap

**Files:** Modify `src/core/stream/StreamAggregator.cpp` (`searchPacks` query build `:704-712`; `kPackSearchPerIndexerLimit` `:683`).

**Contract:**
- Add an anime flag/param to `searchPacks` (or a sibling). For anime, build broad batch queries instead of `S<NN>`/`Season N`: `"<title>"`, `"<title> 1080p"`, `"<title> Complete"`, `"<title> Batch"`. Keep the non-anime path unchanged.
- For the anime/batch path, raise the per-indexer limit above 25 (Finding 3) — e.g. a `kAnimeBatchPerIndexerLimit` (start 100). Non-anime stays 25.

- [ ] **Step 1:** Add a pure-logic query-builder helper `QStringList buildAnimePackQueries(const QString& title)` and unit-test it in `tests/core/stream/` (assert the 4 query forms). (Net fan-out stays integration.)
- [ ] **Step 2:** Branch `searchPacks` on the anime flag to use the helper + the higher cap.
- [ ] **Step 3:** Build + run the new query-builder test → PASS; `BUILD OK`.
- [ ] **Step 4: Commit** — `THEATRE_ANIME_CATALOG T5.1: anime batch query strategy + lift per-indexer cap`

### Task 5.2: In-torrent episode-range picker

**Files:** `TheatreDownloadPanel.cpp` + the file-priority download path. **Read first** — and read the manga `TorrentVolumeProvider` ("give me Vol N" → libtorrent file-priority) as the reuse reference.

**Contract:**
- When the user selects an anime batch torrent, list its episode files (libtorrent `file_storage`; split paths on `[\\/]` — Windows uses backslash, per the libtorrent-separator memory).
- Let the user select a contiguous range (and/or individual files); download only those via file-priority (priority 0 = skip, >0 = download), exactly like the manga volume path.
- No arc logic (per spec §2 non-goal).

- [ ] **Step 1:** Read `TheatreDownloadPanel.cpp` + `TorrentVolumeProvider` + the file-priority API in use.
- [ ] **Step 2:** Add the episode-file list UI + range selection; wire selected files → file-priority download.
- [ ] **Step 3:** Build → `BUILD OK`.
- [ ] **Step 4: Commit** — `THEATRE_ANIME_CATALOG T5.2: in-torrent episode-range picker via file-priority`

### Phase 5 boundary
- [ ] Post `READY TO COMMIT — [Agent 4, THEATRE_ANIME_CATALOG_P5]` with `Skills invoked: [..., security-review]` (torrent + indexer input).

---

## Phase 6 — Smoke + Hemanth verification

- [ ] Build + run via `build_and_run.bat`. Agent-driven smoke first (tankoctl / pywinauto where mechanical), then Hemanth visual gate.
- [ ] **Hemanth smoke checklist:**
  - Open One Piece **from the search bar** → one flat absolute episode list (no 23 seasons) + "Anime" badge.
  - Open a **single-season modern anime** → also routes to Kitsu (flat), badge present.
  - Open a **live-action** show and a **western cartoon** → unchanged (no badge, normal seasons).
  - **Play** an anime episode → stream resolves (anime result preferred, not the live-action One Piece).
  - **Download:** pick a batch torrent → episode file list shows → select a range → only that range downloads.
- [ ] Capture smoke evidence (PNGs + logs) per the smoke-package pattern.
- [ ] Post final `READY TO COMMIT — [Agent 4, THEATRE_ANIME_CATALOG_SMOKE]` + close-out; update STATUS / arc memory.

---

## Self-review (against the spec)

- **§5.1 detection** → Task 1.1 + 2.1 (country parse) + 2.3 (wire). ✅
- **§5.2 bridge (table + search-confirm + degrade)** → Tasks 1.2, 1.3, 1.4, 2.3. ✅
- **§5.3 metadata swap (addon seed + reroute)** → Tasks 2.2, 2.3. ✅
- **§5.4 series UI (flat list, chips, badge)** → Phase 3. ✅
- **§5.5 watching (kitsu ids + contamination bias)** → Phase 4. ✅
- **§5.6 downloading (batch queries, 25-cap lift, range picker)** → Tasks 5.1, 5.2. ✅
- **§5.7 scope/safety (anime-only, all entry points, graceful degrade)** → 2.3 fall-through + Phase 6 non-anime smoke. ✅
- **Type consistency:** `isAnimeSeries`, `AnimeIdMap::kitsuIdForImdb`, `confirmsKitsuMatch`, `animeCatalogActive` used consistently across tasks. ✅
- **Known placeholders by design:** Phases 3–5 carry "read first / locate at task start" for surfaces not ground-truthed in planning — deliberate, to avoid fabricated signatures (see grounding flags). Each still states exact files + contract + test intent.
