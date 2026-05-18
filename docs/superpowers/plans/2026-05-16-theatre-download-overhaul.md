# THEATRE_DOWNLOAD_OVERHAUL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Theatre's drop-out-to-Tankorent download flow with a single unified Theatre-native panel that handles pack selection, scope picking, and progress display — supporting season packs, multi-season packs, full-show packs, and movies, with ONE `Download` button replacing the two existing buttons (`Download Season` + `Download via Tankorent`).

**Architecture:** A new `TheatreDownloadPanel` QWidget replaces the Sources panel slot when `Download` is clicked. Internal two-state machine: PackList (default after open) → ScopePicker (after a pack is selected). Source-merge layer queries Stremio addons via `StreamAggregator` + Tankorent indexers via the D2 owned-QNAM pattern in parallel, normalizes to a single `EnrichedPack` list. `libtorrent` (via existing `TorrentClient`) handles all downloads uniformly with programmatically-driven `AddTorrentConfig.filePriorities` (no `AddTorrentDialog` UI). Pack-type classification (`SingleEpisode`/`MultiEpisode`/`SeasonPack`/`MultiSeason`/`CompleteSeries`) drives badges + filter chips + auto-fallback widening.

**Tech Stack:** Qt6 widgets (QWidget panel + QPropertyAnimation slide transition + custom child widgets for pack rows / episode tiles / season-header progress badge), GoogleTest (TDD for `PackClassifier` + `TitleMetadataEstimator` pure-logic), libtorrent (via existing `TorrentClient`), `StreamAggregator` (existing Stremio addon fan-out), `BulkPackVerifier` (existing filename-regex matching for episode binding).

---

## Preamble — Tankoban-specific context for the executor

### Project facts

- Single-checkout-on-master workflow per `feedback_no_worktrees.md`. No worktrees. No feature branches.
- Source root: `src/` (UI + core split: `src/ui/...`, `src/core/...`).
- Existing prior-arc state: `TANKORENT_STREAM_INTEGRATION` (Phases A–G, shipped 2026-05-15) provides the substrate this arc builds on. Don't rewrite what works — see Section 4 of the brainstorm for the full reuse list.

### Build commands

- Main app compile-only verify: `build_check.bat` → expect `BUILD OK` (~30–90s).
- Unit tests (opt-in): `cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON` (one-time reconfigure when adding test files) → `cmake --build out --target tankoban_tests` → `out\tankoban_tests.exe --gtest_filter=<TestSuite>.*` for specific filters.
- Full app launch + run (Hemanth-driven, not normally invoked by executor): `build_and_run.bat`.

### Where things live

- Existing `TorrentPackPicker.{h,cpp}` at `src/ui/pages/stream/` — Phase D output from prior arc. Will be replaced by `TheatreDownloadPanel` over the course of this arc; deletion in Phase G.
- Existing `StreamDetailView.{h,cpp}` at `src/ui/pages/stream/` — the show-view widget. Hosts the season header (currently with two Download buttons), the episode table, and the Sources panel slot.
- `StreamAggregator.{h,cpp}` at `src/core/stream/` — Stremio addon fan-out. Currently drives the "Download Season" bulk-cohort flow.
- `BulkPackVerifier.{h,cpp}` at `src/core/stream/` — filename-regex episode matcher. `matchEpisodeFileForSeason(file, season, &epNum, &matchedIdx)` is the workhorse for binding torrent files to episodes.
- `StreamDownloadIndex.{h,cpp}` at `src/core/stream/` — the canonical episode-key store. `filePathFor(imdbId, season, episode)` is the read API. `registerEpisode` / `registerMovie` are the write APIs.
- `TorrentClient.{h,cpp}` at `src/core/torrent/` — libtorrent wrapper. `startDownload(infoHash, AddTorrentConfig)` is the entry point. `resolveMetadata(magnetUri)` returns hash + kicks off metadata fetch.
- `AddTorrentConfig` struct at `src/ui/dialogs/AddTorrentDialog.h` — config record with `imdbId` / `season` / `selectedIndices` / `filePriorities` / etc. Phase A1 added the identity fields.
- Existing `TileCard.{h,cpp}` and `TileStrip.{h,cpp}` at `src/ui/pages/` — visual primitives for tile rendering. Codex's expansion (5.3.A + 5.5.A) recommends reusing `TileCard` density + the `StreamSourceCard` row hierarchy.
- Spec doc at `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md` — read end-to-end before starting; Section 5 has all UI/UX details (Codex-expanded).

### Per-task commit cadence

- Each task ends with a `READY TO COMMIT` line per `feedback_commit_protocol.md`. Agent 0 batches commits via `/commit-sweep`.
- For tasks that touch the main app: `build_check.bat` must return `BUILD OK` before the RTC line.
- For tasks that touch test sources: `tankoban_tests` build clean + the new test suite GREEN before the RTC line.

### Visual + UX specs

All UI/UX details (pixel values, color tokens, timings, transition shapes, keyboard navigation) live in Section 5 of the brainstorm-md, inside HTML-comment-bracketed Codex expansion blocks (`<!-- AGENT_7 EXPAND START / AGENT_7 EXPAND END -->`). Each task in this plan references the relevant marker (e.g. "see 5.1.A in brainstorm-md") — open the brainstorm in a second pane while implementing the UI tasks; key values are also copied inline in the code blocks below for convenience.

---

## File structure summary

### New files

- `src/core/stream/PackClassifier.h` + `.cpp` — pure-logic pack-type classifier (extends D3 regex helpers).
- `src/core/stream/TitleMetadataEstimator.h` + `.cpp` — pure-logic title→ScopeEstimate parser.
- `src/core/stream/UnifiedPackSearchEngine.h` + `.cpp` — orchestrates parallel Stremio + Tankorent fan-out; emits unified `EnrichedPack` list.
- `src/ui/pages/stream/TheatreDownloadPanel.h` + `.cpp` — the new QWidget panel.
- `src/ui/pages/stream/PackListItem.h` + `.cpp` — pack-row widget (title + chips + meta).
- `src/ui/pages/stream/EpisodeTile.h` + `.cpp` — scope-picker tile widget.
- `src/ui/pages/stream/SeasonHeaderProgressBadge.h` + `.cpp` — aggregate-progress badge for season header.
- `tests/core/stream/test_pack_classifier.cpp` — TDD tests.
- `tests/core/stream/test_title_metadata_estimator.cpp` — TDD tests.

### Modified files

- `src/ui/pages/stream/StreamDetailView.h` + `.cpp` — remove two buttons, add single `Download` button + the panel + the badge; remove the H2 button's qDebug stub call path (it routes through the new flow).
- `src/ui/pages/StreamPage.h` + `.cpp` — wire the panel's source-merge layer entry point.
- `src/core/stream/StreamAggregator.h` + `.cpp` — extract `searchPacks(imdbId, season) → QList<TorrentResult>` method from the existing "Download Season" bulk-cohort path.
- `CMakeLists.txt` — register new sources in `SOURCES`/`HEADERS` lists + add new test sources to `tankoban_tests`.

### Deleted files

- `src/ui/pages/stream/TorrentPackPicker.h` + `.cpp` — replaced entirely by `TheatreDownloadPanel`. Removal at Phase G after verifying no remaining call sites.

---

## Phase A — Pure-logic substrate (TDD)

### Task A1: `PackClassifier` — pure-logic pack-type classifier

**Files:**
- Create: `src/core/stream/PackClassifier.h`
- Create: `src/core/stream/PackClassifier.cpp`
- Create: `tests/core/stream/test_pack_classifier.cpp`
- Modify: `CMakeLists.txt` (add to `SOURCES` + `HEADERS` + `tankoban_tests` source list)

- [ ] **Step 1: Write the header**

Create `src/core/stream/PackClassifier.h`:

```cpp
#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — pure-logic pack-type classifier.
// Extends the Phase D EnrichedResult.completeSeries flag + detectedSeasons
// QSet into a 5-way enum that drives badges, filter chips, and the
// auto-fallback widening decision.

#include <QSet>
#include <QString>

namespace tankoban::stream::theatre {

enum class PackType {
    Unknown,         // could not classify; safe fallback (treated as Single)
    SingleEpisode,   // one episode: title has SxxExx and no range/multi/complete tokens
    MultiEpisode,    // episode range within one season: SxxExx-Exx or SxxExx.Exx
    SeasonPack,      // one complete season: Sxx tag + "complete"/"full"/"S0N.Full" hint
    MultiSeason,     // explicit range across seasons: Sxx-Sxx
    CompleteSeries   // literal "complete series" / "complete box set" / "complete collection"
};

struct PackClassification {
    PackType  type           = PackType::Unknown;
    QSet<int> detectedSeasons;     // populated from \bS\d{1,2}\b tokens; empty for Complete Series
    int       detectedEpisodeCount = 0;  // best-effort; 0 if not derivable from title
    bool      isCompleteSeries     = false;  // shortcut for type == CompleteSeries
};

// Classify a torrent title. Robust to noise (resolution, encoding, release-
// group tags). Returns Unknown only if absolutely no signal is present.
PackClassification classify(const QString& title);

// Human-readable label for badge rendering. ASCII only, no emoji per
// feedback_no_color_no_emoji.md.
QString labelForType(PackType type);

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 2: Write the failing tests**

Create `tests/core/stream/test_pack_classifier.cpp`:

```cpp
#include <gtest/gtest.h>

#include "core/stream/PackClassifier.h"

using tankoban::stream::theatre::classify;
using tankoban::stream::theatre::PackType;

TEST(PackClassifierTest, CompleteSeries_LiteralString) {
    const auto r = classify("Daredevil.Born.Again.Complete.Series.1080p.WEB-DL");
    EXPECT_EQ(PackType::CompleteSeries, r.type);
    EXPECT_TRUE(r.isCompleteSeries);
}

TEST(PackClassifierTest, CompleteSeries_BoxSet) {
    const auto r = classify("The.Sopranos.Complete.Box.Set.1080p.BluRay");
    EXPECT_EQ(PackType::CompleteSeries, r.type);
}

TEST(PackClassifierTest, CompleteSeries_Collection) {
    const auto r = classify("Seinfeld Complete Collection 1989-1998 720p");
    EXPECT_EQ(PackType::CompleteSeries, r.type);
}

TEST(PackClassifierTest, MultiSeason_ExplicitRange) {
    const auto r = classify("Breaking.Bad.S01-S05.1080p.BluRay");
    EXPECT_EQ(PackType::MultiSeason, r.type);
    EXPECT_EQ(5, r.detectedSeasons.size());
    EXPECT_TRUE(r.detectedSeasons.contains(1));
    EXPECT_TRUE(r.detectedSeasons.contains(5));
}

TEST(PackClassifierTest, MultiSeason_DottedRange) {
    const auto r = classify("Game.Of.Thrones.S01.S02.S03.S04.S05.S06.S07.S08.1080p");
    EXPECT_EQ(PackType::MultiSeason, r.type);
    EXPECT_EQ(8, r.detectedSeasons.size());
}

TEST(PackClassifierTest, SeasonPack_CompleteKeyword) {
    const auto r = classify("Daredevil.Born.Again.S01.COMPLETE.1080p.DSNP.WEB-DL");
    EXPECT_EQ(PackType::SeasonPack, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, SeasonPack_FullKeyword) {
    const auto r = classify("Sopranos.Season.1.COMPLETE.S01.Full-MIK");
    EXPECT_EQ(PackType::SeasonPack, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, SeasonPack_SeasonTagNoCompleteWord) {
    // Just S02 without "complete" / "full" — still likely a season pack if
    // size context says so, but title-only we conservatively call this
    // SeasonPack only when paired with a size or filename hint. With no
    // hint, fall back to SeasonPack since the season tag is the strongest
    // signal absent SxxExx episode patterns.
    const auto r = classify("Sopranos.S02.2160p.HDR.WEB-DL");
    EXPECT_EQ(PackType::SeasonPack, r.type);
}

TEST(PackClassifierTest, SingleEpisode_StandardSxxExx) {
    const auto r = classify("Daredevil.Born.Again.S01E03.1080p.WEB-DL");
    EXPECT_EQ(PackType::SingleEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, SingleEpisode_LowercaseE) {
    const auto r = classify("Daredevil Born Again.2025.Season.01.e03.1080p");
    EXPECT_EQ(PackType::SingleEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, MultiEpisode_RangeWithinSeason) {
    const auto r = classify("Daredevil Born Again S01E01-E03 1080p");
    EXPECT_EQ(PackType::MultiEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
    EXPECT_EQ(3, r.detectedEpisodeCount);
}

TEST(PackClassifierTest, MultiEpisode_DottedEpisodes) {
    const auto r = classify("Daredevil.Born.Again.2025.Season.01.e01.e02.1080p");
    EXPECT_EQ(PackType::MultiEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, Unknown_NoSignal) {
    const auto r = classify("Random.Movie.2025.1080p.WEB-DL");
    EXPECT_EQ(PackType::Unknown, r.type);
    EXPECT_TRUE(r.detectedSeasons.isEmpty());
}

TEST(PackClassifierTest, Unknown_EmptyString) {
    const auto r = classify("");
    EXPECT_EQ(PackType::Unknown, r.type);
}

TEST(PackClassifierTest, LabelForType_HumanReadable) {
    EXPECT_EQ(QStringLiteral("Single Episode"),  tankoban::stream::theatre::labelForType(PackType::SingleEpisode));
    EXPECT_EQ(QStringLiteral("Multi-Episode"),   tankoban::stream::theatre::labelForType(PackType::MultiEpisode));
    EXPECT_EQ(QStringLiteral("Season Pack"),     tankoban::stream::theatre::labelForType(PackType::SeasonPack));
    EXPECT_EQ(QStringLiteral("Multi-Season"),    tankoban::stream::theatre::labelForType(PackType::MultiSeason));
    EXPECT_EQ(QStringLiteral("Complete Series"), tankoban::stream::theatre::labelForType(PackType::CompleteSeries));
}
```

- [ ] **Step 3: Run tests — expect link failure**

Run:

```
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
```

Expected: build failure with unresolved external `classify` / `labelForType`. This confirms the test file links into the test target correctly (i.e. CMakeLists wiring already done in Step 6, if running in order swap with Step 6).

- [ ] **Step 4: Write the implementation**

Create `src/core/stream/PackClassifier.cpp`:

```cpp
#include "core/stream/PackClassifier.h"

#include <QRegularExpression>
#include <QSet>

namespace tankoban::stream::theatre {

namespace {

// "Complete Series" / "Complete Box Set" / "Complete Collection" — the
// canonical multi-season marker. Case-insensitive, whitespace/dot/dash
// flexible separators.
QRegularExpression reCompleteSeries() {
    return QRegularExpression(
        "(?i)\\b(complete[\\s._-]*series|complete[\\s._-]*box[\\s._-]*set|complete[\\s._-]*collection)\\b");
}

// Season range like S01-S05 or S01 S05 — captures both bounds.
QRegularExpression reSeasonRange() {
    return QRegularExpression(
        "(?i)\\bS(\\d{1,2})[\\s._-]*[-\\s][\\s._-]*S(\\d{1,2})\\b");
}

// Single season tag SxxNN — used both for single-season classification AND
// for collecting all season tokens in a multi-season title (e.g.
// "S01.S02.S03.S04" → 4 hits).
QRegularExpression reSingleSeason() {
    return QRegularExpression("(?i)\\bS(\\d{1,2})\\b");
}

// Season-pack marker words paired with a season tag: COMPLETE / Full /
// FULLPACK / SEASON.PACK / etc.
QRegularExpression reSeasonPackWord() {
    return QRegularExpression(
        "(?i)\\b(complete|full|season[\\s._-]*pack|fullpack)\\b");
}

// Single episode tag — SxxEyy with no range. Captures season + episode.
QRegularExpression reSingleEpisode() {
    return QRegularExpression(
        "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})\\b");
}

// Episode-range / multi-episode tag — SxxEyy-Ezz or SxxEyy.Ezz or
// Season.NN.eAA.eBB (per Daredevil sample observed in the smoke).
QRegularExpression reEpisodeRange() {
    return QRegularExpression(
        "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})[\\s._-]*[-\\s.][\\s._-]*E(\\d{1,3})\\b");
}

QRegularExpression reLowerCaseDottedEpisodes() {
    return QRegularExpression(
        "(?i)\\bseason[\\s._-]*0?(\\d{1,2})[\\s._-]*e(\\d{1,3})[\\s._-]*\\.[\\s._-]*e(\\d{1,3})\\b");
}

}  // namespace

PackClassification classify(const QString& title) {
    PackClassification out;

    if (title.isEmpty())
        return out;

    // 1. Complete Series literal — highest-priority marker; short-circuit.
    if (reCompleteSeries().match(title).hasMatch()) {
        out.type = PackType::CompleteSeries;
        out.isCompleteSeries = true;
        return out;
    }

    // 2. Season range Sxx-Syy → MultiSeason; populate detectedSeasons with
    // every season in the inclusive range.
    if (auto m = reSeasonRange().match(title); m.hasMatch()) {
        const int lo = m.captured(1).toInt();
        const int hi = m.captured(2).toInt();
        if (lo > 0 && hi > 0 && hi >= lo) {
            for (int s = lo; s <= hi; ++s)
                out.detectedSeasons.insert(s);
            out.type = PackType::MultiSeason;
            return out;
        }
    }

    // 3. Collect all season tags. Used by:
    //    - MultiSeason fallback (>=2 distinct seasons via dotted enumeration)
    //    - SeasonPack / SingleEpisode classification (==1 season tag)
    auto seasonIt = reSingleSeason().globalMatch(title);
    while (seasonIt.hasNext())
        out.detectedSeasons.insert(seasonIt.next().captured(1).toInt());

    // Also fold in lowercase "season NN" prefix patterns that don't carry
    // the SxxNN tag (e.g. "Daredevil Born Again.2025.Season.01.e03").
    static const QRegularExpression reLowerSeason("(?i)\\bseason[\\s._-]*0?(\\d{1,2})\\b");
    auto lowerSeasonIt = reLowerSeason.globalMatch(title);
    while (lowerSeasonIt.hasNext())
        out.detectedSeasons.insert(lowerSeasonIt.next().captured(1).toInt());

    if (out.detectedSeasons.size() >= 2) {
        out.type = PackType::MultiSeason;
        return out;
    }

    // 4. Episode-range patterns → MultiEpisode.
    if (auto m = reEpisodeRange().match(title); m.hasMatch()) {
        const int lo = m.captured(2).toInt();
        const int hi = m.captured(3).toInt();
        if (lo > 0 && hi > 0 && hi >= lo)
            out.detectedEpisodeCount = hi - lo + 1;
        out.type = PackType::MultiEpisode;
        return out;
    }
    if (reLowerCaseDottedEpisodes().match(title).hasMatch()) {
        out.type = PackType::MultiEpisode;
        return out;
    }

    // 5. SxxExx single-episode tag → SingleEpisode.
    if (auto m = reSingleEpisode().match(title); m.hasMatch()) {
        out.type = PackType::SingleEpisode;
        // Backfill seasons set if globalMatch missed.
        if (out.detectedSeasons.isEmpty())
            out.detectedSeasons.insert(m.captured(1).toInt());
        return out;
    }

    // Lowercase eXX after Season.NN (matches "Season.01.e03").
    static const QRegularExpression reLowerEpisode(
        "(?i)\\bseason[\\s._-]*0?(\\d{1,2})[\\s._-]*e(\\d{1,3})\\b");
    if (reLowerEpisode.match(title).hasMatch()) {
        out.type = PackType::SingleEpisode;
        return out;
    }

    // 6. Season tag with no episode → SeasonPack. The "complete"/"full"
    // keyword is a strong signal but not strictly required (a bare "S02"
    // pack is almost always a season pack in practice).
    if (out.detectedSeasons.size() == 1) {
        out.type = PackType::SeasonPack;
        return out;
    }

    // 7. No signal — Unknown.
    out.type = PackType::Unknown;
    return out;
}

QString labelForType(PackType type) {
    switch (type) {
    case PackType::SingleEpisode:  return QStringLiteral("Single Episode");
    case PackType::MultiEpisode:   return QStringLiteral("Multi-Episode");
    case PackType::SeasonPack:     return QStringLiteral("Season Pack");
    case PackType::MultiSeason:    return QStringLiteral("Multi-Season");
    case PackType::CompleteSeries: return QStringLiteral("Complete Series");
    case PackType::Unknown:        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 5: Wire CMakeLists**

Modify `CMakeLists.txt`:

In the main `set(SOURCES ...)` block, add (group with other `src/core/stream/` entries):

```cmake
    src/core/stream/PackClassifier.cpp
```

In the main `set(HEADERS ...)` block, add:

```cmake
    src/core/stream/PackClassifier.h
```

In the `if(TANKOBAN_BUILD_TESTS)` block's `add_executable(tankoban_tests ...)` source list, add (group with other test files):

```cmake
        tests/core/stream/test_pack_classifier.cpp
        src/core/stream/PackClassifier.cpp
```

- [ ] **Step 6: Reconfigure + build + run tests**

Run:

```
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
```

Expected: `-- Configuring done` + `-- Generating done`.

Then build:

```
cmake --build out --target tankoban_tests
```

Expected: `BUILD OK` (no link errors).

Then run:

```
out\tankoban_tests.exe --gtest_filter=PackClassifierTest.*
```

Expected: all 16 tests PASSED.

- [ ] **Step 7: Verify main app still builds**

Run:

```
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 8: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task A1 — PackClassifier pure-logic class + 16 GREEN tests. 5-way classification (SingleEpisode/MultiEpisode/SeasonPack/MultiSeason/CompleteSeries) extending Phase D regex helpers. Robust against title noise (release group tags, resolution markers, mixed-case episode patterns from observed indexer results). Compile-only verify GREEN; tankoban_tests GREEN.] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion] | files: src/core/stream/PackClassifier.h, src/core/stream/PackClassifier.cpp, tests/core/stream/test_pack_classifier.cpp, CMakeLists.txt
```

---

### Task A2: `TitleMetadataEstimator` — title→ScopeEstimate parser

**Files:**
- Create: `src/core/stream/TitleMetadataEstimator.h`
- Create: `src/core/stream/TitleMetadataEstimator.cpp`
- Create: `tests/core/stream/test_title_metadata_estimator.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/core/stream/TitleMetadataEstimator.h`:

```cpp
#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — pure-logic title→ScopeEstimate
// parser. Powers the scope-picker tile grid's INSTANT render BEFORE
// libtorrent fetches real torrent metadata. Real metadata replaces this
// estimate when it arrives via TorrentEngine::metadataReady.

#include <QList>
#include <QString>

namespace tankoban::stream::theatre {

struct EpisodeEstimate {
    int  season  = 0;
    int  episode = 0;
    // Title is best-effort if derivable from the title string; otherwise
    // empty (real metadata will populate filename-based titles when it
    // arrives).
    QString title;
};

struct ScopeEstimate {
    QList<int>            detectedSeasons;       // sorted ascending
    QList<EpisodeEstimate> episodes;             // sorted by (season, episode)
    bool                  isCompleteSeries = false;
    bool                  hasExplicitEpisodeCount = false;  // true if title had eAA-eBB or "N Eps"
};

// Estimate pack contents from title alone. For Complete Series with no
// embedded episode count, returns an empty episodes list — the panel
// renders only per-season headers and waits for real metadata.
//
// For single-season packs ("S01.COMPLETE"), guesses a default of 10
// episodes (matches the median season length across modern TV; refined
// by real metadata when it arrives).
ScopeEstimate estimate(const QString& title);

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 2: Write the failing tests**

Create `tests/core/stream/test_title_metadata_estimator.cpp`:

```cpp
#include <gtest/gtest.h>

#include "core/stream/TitleMetadataEstimator.h"

using tankoban::stream::theatre::estimate;
using tankoban::stream::theatre::ScopeEstimate;

TEST(TitleMetadataEstimatorTest, CompleteSeries_NoEpisodeCount) {
    const auto s = estimate("Sopranos.Complete.Series.1080p.BluRay");
    EXPECT_TRUE(s.isCompleteSeries);
    EXPECT_TRUE(s.episodes.isEmpty());
    EXPECT_TRUE(s.detectedSeasons.isEmpty());
}

TEST(TitleMetadataEstimatorTest, SeasonPack_DefaultEpisodeCount) {
    const auto s = estimate("Daredevil.Born.Again.S01.COMPLETE.1080p");
    EXPECT_FALSE(s.isCompleteSeries);
    EXPECT_EQ(QList<int>{1}, s.detectedSeasons);
    // Default to 10-episode estimate for a season pack with no count hint.
    EXPECT_EQ(10, s.episodes.size());
    EXPECT_EQ(1,  s.episodes.first().season);
    EXPECT_EQ(1,  s.episodes.first().episode);
    EXPECT_EQ(1,  s.episodes.last().season);
    EXPECT_EQ(10, s.episodes.last().episode);
}

TEST(TitleMetadataEstimatorTest, SeasonPack_ExplicitEpisodeCount) {
    const auto s = estimate("Show.S02.Complete.13.Episodes.1080p");
    EXPECT_TRUE(s.hasExplicitEpisodeCount);
    EXPECT_EQ(13, s.episodes.size());
}

TEST(TitleMetadataEstimatorTest, MultiEpisode_RangeFromTitle) {
    const auto s = estimate("Show.S01E01-E03.1080p");
    EXPECT_FALSE(s.isCompleteSeries);
    EXPECT_TRUE(s.hasExplicitEpisodeCount);
    EXPECT_EQ(3, s.episodes.size());
    EXPECT_EQ(1, s.episodes.first().episode);
    EXPECT_EQ(3, s.episodes.last().episode);
}

TEST(TitleMetadataEstimatorTest, SingleEpisode_OneTile) {
    const auto s = estimate("Show.S01E05.1080p");
    EXPECT_EQ(1, s.episodes.size());
    EXPECT_EQ(1, s.episodes.first().season);
    EXPECT_EQ(5, s.episodes.first().episode);
}

TEST(TitleMetadataEstimatorTest, MultiSeason_DefaultEpisodesPerSeason) {
    const auto s = estimate("Show.S01-S03.1080p.BluRay");
    EXPECT_EQ((QList<int>{1, 2, 3}), s.detectedSeasons);
    // 3 seasons * 10 default episodes each = 30 episodes total.
    EXPECT_EQ(30, s.episodes.size());
}

TEST(TitleMetadataEstimatorTest, SortedByEpisodeOrder) {
    const auto s = estimate("Show.S03.S01.S02.Complete");
    EXPECT_EQ((QList<int>{1, 2, 3}), s.detectedSeasons);
    // First episode should be S1E1, last should be S3E10.
    EXPECT_EQ(1, s.episodes.first().season);
    EXPECT_EQ(1, s.episodes.first().episode);
    EXPECT_EQ(3, s.episodes.last().season);
    EXPECT_EQ(10, s.episodes.last().episode);
}

TEST(TitleMetadataEstimatorTest, EmptyTitle_EmptyEstimate) {
    const auto s = estimate("");
    EXPECT_FALSE(s.isCompleteSeries);
    EXPECT_TRUE(s.episodes.isEmpty());
    EXPECT_TRUE(s.detectedSeasons.isEmpty());
}
```

- [ ] **Step 3: Write the implementation**

Create `src/core/stream/TitleMetadataEstimator.cpp`:

```cpp
#include "core/stream/TitleMetadataEstimator.h"

#include "core/stream/PackClassifier.h"

#include <QRegularExpression>
#include <algorithm>

namespace tankoban::stream::theatre {

namespace {

constexpr int kDefaultEpisodesPerSeason = 10;

// Extract explicit episode count from title patterns like "13 Episodes",
// "13 Eps", "10ep", "complete (10 eps)".
int episodeCountFromTitle(const QString& title) {
    static const QRegularExpression reCount(
        "(?i)\\b(\\d{1,3})[\\s._-]*(?:eps?|episodes?)\\b");
    auto m = reCount.match(title);
    if (m.hasMatch())
        return m.captured(1).toInt();
    return 0;
}

}  // namespace

ScopeEstimate estimate(const QString& title) {
    ScopeEstimate out;
    if (title.isEmpty())
        return out;

    const auto classification = classify(title);
    out.isCompleteSeries = classification.isCompleteSeries;

    // Sort detected seasons ascending.
    out.detectedSeasons = classification.detectedSeasons.values();
    std::sort(out.detectedSeasons.begin(), out.detectedSeasons.end());

    // For Complete Series with no embedded count, leave episodes empty —
    // the panel renders only per-season headers until real metadata arrives.
    if (out.isCompleteSeries && out.detectedSeasons.isEmpty())
        return out;

    // For multi-episode patterns with an explicit range like SxxEyy-Ezz,
    // populate episodes for that exact span.
    static const QRegularExpression reEpisodeRange(
        "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})[\\s._-]*[-\\s.][\\s._-]*E(\\d{1,3})\\b");
    if (auto m = reEpisodeRange.match(title); m.hasMatch()) {
        const int season = m.captured(1).toInt();
        const int lo     = m.captured(2).toInt();
        const int hi     = m.captured(3).toInt();
        if (season > 0 && lo > 0 && hi >= lo) {
            out.hasExplicitEpisodeCount = true;
            for (int ep = lo; ep <= hi; ++ep) {
                EpisodeEstimate e;
                e.season = season;
                e.episode = ep;
                out.episodes.append(e);
            }
            return out;
        }
    }

    // Single episode tag → one tile.
    static const QRegularExpression reSingleEpisode(
        "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})\\b");
    if (auto m = reSingleEpisode.match(title); m.hasMatch()) {
        EpisodeEstimate e;
        e.season  = m.captured(1).toInt();
        e.episode = m.captured(2).toInt();
        out.episodes.append(e);
        return out;
    }

    // Determine the per-season episode count: explicit if title says so,
    // otherwise default (10 episodes per season).
    const int explicitCount = episodeCountFromTitle(title);
    const int perSeason     = explicitCount > 0 ? explicitCount : kDefaultEpisodesPerSeason;
    if (explicitCount > 0)
        out.hasExplicitEpisodeCount = true;

    // For season-pack / multi-season, populate episodes 1..perSeason for each
    // detected season.
    for (int season : out.detectedSeasons) {
        for (int ep = 1; ep <= perSeason; ++ep) {
            EpisodeEstimate e;
            e.season  = season;
            e.episode = ep;
            out.episodes.append(e);
        }
    }
    return out;
}

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 4: Wire CMakeLists**

Same pattern as Task A1. Add to `SOURCES`, `HEADERS`, and `tankoban_tests` source list.

- [ ] **Step 5: Reconfigure + build + run tests**

Run:

```
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
out\tankoban_tests.exe --gtest_filter=TitleMetadataEstimatorTest.*
```

Expected: 8 tests PASSED.

- [ ] **Step 6: Verify main app builds**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 7: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task A2 — TitleMetadataEstimator pure-logic class + 8 GREEN tests. Drives the scope-picker tile grid instant-render BEFORE libtorrent metadata arrives (hybrid metadata strategy per brainstorm decision 9). Default 10-episodes-per-season heuristic + explicit-count override from "N Episodes" titles + episode range pattern detection. Builds on PackClassifier (Task A1).] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /build-verify] | files: src/core/stream/TitleMetadataEstimator.h, src/core/stream/TitleMetadataEstimator.cpp, tests/core/stream/test_title_metadata_estimator.cpp, CMakeLists.txt
```

---

## Phase B — Source-merge layer

### Task B1: Extract `StreamAggregator::searchPacks(imdbId, season)`

**Files:**
- Modify: `src/core/stream/StreamAggregator.h`
- Modify: `src/core/stream/StreamAggregator.cpp`

- [ ] **Step 1: Inspect existing "Download Season" path**

Run:

```
grep -n "searchByTitle\|bulkPackSearch\|fetchPacksFor\|seasonPacks" src/core/stream/StreamAggregator.h src/core/stream/StreamAggregator.cpp
```

Identify the existing entry point that the "Download Season" button uses to fetch Stremio addon packs for a (imdbId, season) tuple. There's an existing method (likely `searchByTitle` or `fetchPacks`) that returns `QList<TorrentResult>`. The new `searchPacks` may be just a renaming + signature cleanup of an existing method.

- [ ] **Step 2: Add `searchPacks` method to header**

In `StreamAggregator.h`, add the public method (group with other public search/aggregate methods):

```cpp
public:
    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — synchronous-ish fan-out of all
    // configured Stremio addons for (imdbId, season) packs. Internally
    // wraps the existing addon-fetch infrastructure that "Download Season"
    // uses; returns the aggregated TorrentResult list once all addons have
    // replied or timed out.
    //
    // For movies: pass season=0. For multi-season / whole-show search:
    // pass season=0 and the addons' "complete series" probe runs.
    //
    // Result delivery is async via the existing packsAvailable signal
    // (already wired for "Download Season"). Callers connect to that
    // signal + filter on imdbId.
    void searchPacks(const QString& imdbId, int season);

signals:
    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — exposed for the unified picker.
    // Fires once per imdbId+season search with all aggregated results.
    void packsAvailable(const QString& imdbId, int season,
                        const QList<TorrentResult>& results);
```

- [ ] **Step 3: Implement `searchPacks` in .cpp**

In `StreamAggregator.cpp`, add the implementation. The body delegates to whatever existing addon-fan-out the "Download Season" path uses — find that path via grep in Step 1 and route into it:

```cpp
void StreamAggregator::searchPacks(const QString& imdbId, int season) {
    // Delegate to the existing addon-fan-out infrastructure. The existing
    // path (Download Season) uses a method that takes (imdbId, season) and
    // emits results via a per-addon signal; aggregate them here under one
    // packsAvailable emit.
    //
    // Implementation note for the executor: this body wraps the existing
    // bulk-cohort fetch. If the existing method is "fetchBulkPacksFor" or
    // similar, call it here and connect its result signal to emit
    // packsAvailable once all addons have responded or after a 15s timeout.
    //
    // Match the existing bulk-cohort timeout behavior. Don't reinvent the
    // timeout if one is already in place.

    QList<TorrentResult> aggregated;
    // [Adapt to the actual existing call. The Phase D D2 indexer fan-out
    // pattern in TorrentPackPicker.cpp uses a per-indexer connection + a
    // result accumulator; StreamAggregator already has its own equivalent
    // for Stremio addons. Use that.]

    // Once the existing call's terminal signal fires:
    //   emit packsAvailable(imdbId, season, aggregated);
}
```

The executor MUST grep for the actual existing addon-fan-out method during Step 1 + adapt this implementation accordingly. If the existing method has the same signature, this is mostly a wrapper / signal rename. If it's different, the executor adapts.

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task B1 — StreamAggregator::searchPacks(imdbId, season) method extracted/exposed for the unified picker. Wraps existing Stremio addon fan-out used by current "Download Season" path; emits packsAvailable signal with QList<TorrentResult>. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/core/stream/StreamAggregator.h, src/core/stream/StreamAggregator.cpp
```

---

### Task B2: `UnifiedPackSearchEngine` orchestrator class

**Files:**
- Create: `src/core/stream/UnifiedPackSearchEngine.h`
- Create: `src/core/stream/UnifiedPackSearchEngine.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/core/stream/UnifiedPackSearchEngine.h`:

```cpp
#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — orchestrates the unified pack
// fan-out. Queries StreamAggregator (Stremio addons) AND Tankorent
// indexers (PirateBay / 1337x / etc.) in parallel; normalizes the union
// into a single QList<EnrichedPack> tagged with source (Stremio | Tankorent).

#include "core/TorrentResult.h"
#include "core/stream/PackClassifier.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class StreamAggregator;

namespace tankoban::stream::theatre {

enum class PackSource {
    Stremio,
    Tankorent
};

struct EnrichedPack {
    TorrentResult        raw;
    PackSource           source = PackSource::Tankorent;
    QString              sourceLabel;  // "Stremio" or "1337x" / "PirateBay" / etc.
    PackClassification   classification;
    double               combinedScore = 0.0;  // QualityScorer output
};

class UnifiedPackSearchEngine : public QObject {
    Q_OBJECT
public:
    explicit UnifiedPackSearchEngine(StreamAggregator* aggregator,
                                     QNetworkAccessManager* nam,
                                     QObject* parent = nullptr);

    // Fire a unified search. Results stream via packResults; final signal
    // searchComplete fires after all sources respond / time out.
    //
    // season == 0 → whole-show / multi-season probe (Complete Series packs).
    void search(const QString& imdbId, const QString& showName, int season);

signals:
    // Streamed results — fired as each source responds, can fire multiple
    // times per search. Panel appends to its m_packs list + re-renders.
    void packResults(const QString& imdbId, int season,
                     const QList<EnrichedPack>& results);

    // Terminal signal — fires once all sources respond / time out. Panel
    // hides loading state.
    void searchComplete(const QString& imdbId, int season,
                        int totalPacks);

private slots:
    void onStremioPacksAvailable(const QString& imdbId, int season,
                                 const QList<TorrentResult>& results);

private:
    void launchTankorentSearches();
    void normalizeAndEmit(const QList<TorrentResult>& rawResults,
                          PackSource source,
                          const QString& sourceLabel);

    StreamAggregator*      m_aggregator   = nullptr;
    QNetworkAccessManager* m_nam          = nullptr;
    QString                m_pendingImdb;
    QString                m_pendingShow;
    int                    m_pendingSeason = 0;
    int                    m_pendingSourceCount = 0;  // sources still awaiting
    int                    m_totalEmitted       = 0;  // running total for searchComplete
};

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 2: Write the implementation**

Create `src/core/stream/UnifiedPackSearchEngine.cpp`:

```cpp
#include "core/stream/UnifiedPackSearchEngine.h"

#include "core/stream/StreamAggregator.h"
#include "core/stream/QualityScorer.h"

#include <QFileInfo>
#include <QSettings>

namespace tankoban::stream::theatre {

UnifiedPackSearchEngine::UnifiedPackSearchEngine(StreamAggregator* aggregator,
                                                 QNetworkAccessManager* nam,
                                                 QObject* parent)
    : QObject(parent), m_aggregator(aggregator), m_nam(nam) {
    if (m_aggregator) {
        connect(m_aggregator, &StreamAggregator::packsAvailable,
                this, &UnifiedPackSearchEngine::onStremioPacksAvailable);
    }
}

void UnifiedPackSearchEngine::search(const QString& imdbId,
                                     const QString& showName,
                                     int season) {
    m_pendingImdb   = imdbId;
    m_pendingShow   = showName;
    m_pendingSeason = season;
    m_totalEmitted  = 0;

    // 2 sources to wait on: Stremio addons (one aggregated reply) + Tankorent
    // indexers (one aggregated reply launched below).
    m_pendingSourceCount = 2;

    // Kick off Stremio addon fan-out (existing infrastructure from B1).
    if (m_aggregator)
        m_aggregator->searchPacks(imdbId, season);
    else
        --m_pendingSourceCount;  // skip if no aggregator wired

    // Kick off Tankorent indexer fan-out via the existing per-indexer pattern
    // (mirrors TorrentPackPicker.cpp's D2 indexer launch). Each enabled
    // indexer per QSettings("tankorent/indexers/<id>/enabled") gets one
    // search request; their searchFinished signals aggregate into a single
    // QList<TorrentResult> + normalizeAndEmit when all respond.
    launchTankorentSearches();
}

void UnifiedPackSearchEngine::launchTankorentSearches() {
    // Build query variations per the D2 pattern:
    //   season > 0: "<show> S<NN>"  +  "<show> Season <N>"
    //   season == 0: "<show> Complete"  +  "<show> Complete Series"
    QStringList queries;
    if (m_pendingSeason > 0) {
        queries << QStringLiteral("%1 S%2")
                       .arg(m_pendingShow)
                       .arg(m_pendingSeason, 2, 10, QLatin1Char('0'));
        queries << QStringLiteral("%1 Season %2")
                       .arg(m_pendingShow).arg(m_pendingSeason);
    } else {
        queries << QStringLiteral("%1 Complete").arg(m_pendingShow);
        queries << QStringLiteral("%1 Complete Series").arg(m_pendingShow);
    }

    // [Adapt to actual Tankorent indexer launch pattern. Look at
    //  TorrentPackPicker.cpp's launchSearches() implementation for the
    //  exact owned-QNAM + per-indexer instantiation + searchFinished
    //  connect pattern. Each indexer emits its results; this engine
    //  accumulates them all into one list + calls normalizeAndEmit.]

    // After all indexers respond:
    //   QList<TorrentResult> tankorentResults = /* accumulated */;
    //   normalizeAndEmit(tankorentResults, PackSource::Tankorent,
    //                    /* per-result indexer name */);
    //   --m_pendingSourceCount;
    //   if (m_pendingSourceCount == 0)
    //       emit searchComplete(m_pendingImdb, m_pendingSeason, m_totalEmitted);
}

void UnifiedPackSearchEngine::onStremioPacksAvailable(const QString& imdbId,
                                                     int season,
                                                     const QList<TorrentResult>& results) {
    if (imdbId != m_pendingImdb || season != m_pendingSeason)
        return;  // stale callback
    normalizeAndEmit(results, PackSource::Stremio, QStringLiteral("Stremio"));
    --m_pendingSourceCount;
    if (m_pendingSourceCount == 0)
        emit searchComplete(m_pendingImdb, m_pendingSeason, m_totalEmitted);
}

void UnifiedPackSearchEngine::normalizeAndEmit(const QList<TorrentResult>& rawResults,
                                               PackSource source,
                                               const QString& sourceLabel) {
    QList<EnrichedPack> enriched;
    for (const auto& raw : rawResults) {
        // F1 fix from prior smoke: drop empty-magnet/infoHash records.
        if (raw.magnetUri.isEmpty() && raw.infoHash.isEmpty())
            continue;

        EnrichedPack p;
        p.raw            = raw;
        p.source         = source;
        p.sourceLabel    = (source == PackSource::Tankorent)
                             ? (raw.sourceName.isEmpty() ? sourceLabel : raw.sourceName)
                             : sourceLabel;
        p.classification = classify(raw.title);

        const int qScore = tankostream::stream::QualityScorer::qualityScore(
            QFileInfo(raw.title).fileName());  // basename for filename-only scoring
        const int hScore = tankostream::stream::QualityScorer::healthScore(raw.seeders);
        p.combinedScore  = tankostream::stream::QualityScorer::combinedScore(
            qScore, hScore, /*wQuality=*/0.6, /*wHealth=*/0.4);

        enriched.append(p);
        ++m_totalEmitted;
    }
    emit packResults(m_pendingImdb, m_pendingSeason, enriched);
}

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 3: Wire CMakeLists**

Add `src/core/stream/UnifiedPackSearchEngine.cpp` to `SOURCES`, `.h` to `HEADERS`. No test file for B2 — it's an orchestrator, not pure logic; verified via smoke later.

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`. If link error on indexer-class names, executor adapts the implementation per the actual TorrentPackPicker.cpp pattern (which is what they're modeling on).

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task B2 — UnifiedPackSearchEngine orchestrator class. Fans out Stremio (via StreamAggregator) + Tankorent indexers (per D2 owned-QNAM pattern) in parallel; normalizes union into QList<EnrichedPack> tagged with PackSource + PackClassification + combinedScore. F1 defensive filter (empty magnet+infoHash drop) baked in. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/core/stream/UnifiedPackSearchEngine.h, src/core/stream/UnifiedPackSearchEngine.cpp, CMakeLists.txt
```

---

## Phase C — Panel pack-list state

### Task C1: `TheatreDownloadPanel` skeleton class

**Files:**
- Create: `src/ui/pages/stream/TheatreDownloadPanel.h`
- Create: `src/ui/pages/stream/TheatreDownloadPanel.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/stream/TheatreDownloadPanel.h`:

```cpp
#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — the unified Theatre-native
// download panel. Replaces the Sources panel slot in StreamDetailView's
// right pane when the user clicks Download. Two internal states:
//   PackList     — default after open; shows aggregated Stremio + Tankorent
//                  pack rows with badges + filter chips.
//   ScopePicker  — after a pack is selected; shows episode tiles with
//                  per-season toggle + pre-uncheck-already-have logic.
//
// UI/UX details (pixel values, color tokens, timings, transitions) live
// in Section 5 of docs/superpowers/specs/2026-05-16-theatre-download-
// overhaul-brainstorm.md under AGENT_7_EXPAND markers.

#include "core/stream/UnifiedPackSearchEngine.h"
#include "core/stream/TitleMetadataEstimator.h"

#include <QStackedWidget>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class StreamDownloadIndex;
class TorrentClient;

namespace tankoban::stream::theatre {

class PackListItem;
class EpisodeTile;

class TheatreDownloadPanel : public QWidget {
    Q_OBJECT
public:
    enum class State { PackList, ScopePicker };

    explicit TheatreDownloadPanel(QWidget* parent = nullptr);

    void setSearchEngine(UnifiedPackSearchEngine* engine);
    void setStreamDownloadIndex(StreamDownloadIndex* index);
    void setTorrentClient(TorrentClient* client);

    // Called by StreamDetailView when the user clicks the new Download button.
    // imdbId + showName + season identify the search; mediaType is "series"
    // or "movie" (drives the degenerate movie scope-picker mode).
    void openFor(const QString& imdbId, const QString& showName,
                 int season, const QString& mediaType);

    // Called by StreamDetailView when the user dismisses the panel
    // (Cancel / back-nav / focus change). Resets internal state to empty.
    void reset();

signals:
    // Emitted when the user confirms a download. StreamDetailView routes
    // this to TorrentClient::startDownload with the supplied config.
    void downloadRequested(const QString& imdbId, int season,
                           const QString& magnetUri,
                           const QString& infoHash,
                           const AddTorrentConfig& config);

    // Emitted on Cancel or back-from-scope-picker. Panel itself transitions
    // internally; this is for the host (StreamDetailView) to maybe re-show
    // the Sources panel.
    void dismissRequested();

private slots:
    void onPackResults(const QString& imdbId, int season,
                       const QList<EnrichedPack>& results);
    void onSearchComplete(const QString& imdbId, int season, int totalPacks);
    void onPackRowSelected(int row);
    void onScopeBackClicked();
    void onDownloadClicked();
    void onFilterChipClicked();

private:
    void buildUI();
    void buildPackListState();
    void buildScopePickerState();
    void transitionTo(State newState);
    void rerenderPackList();
    void rerenderScopePicker();
    void autoFallbackToShowWide();

    UnifiedPackSearchEngine* m_searchEngine = nullptr;
    StreamDownloadIndex*     m_downloadIndex = nullptr;
    TorrentClient*           m_torrentClient = nullptr;

    // Current search context.
    QString m_imdbId;
    QString m_showName;
    int     m_season = 0;
    QString m_mediaType;

    // PackList state.
    QList<EnrichedPack> m_packs;
    QList<EnrichedPack> m_filteredPacks;  // post-filter view
    QString m_typeFilter   = QStringLiteral("All");
    QString m_sourceFilter = QStringLiteral("All sources");
    bool    m_widenedAutoFallback = false;

    // ScopePicker state.
    EnrichedPack    m_selectedPack;
    ScopeEstimate   m_scopeEstimate;
    // Tile selection: keyed by (season << 16) | episode → bool checked.
    // QMap for stable iteration order.
    QMap<quint32, bool> m_tileChecked;

    // UI hierarchy.
    QStackedWidget* m_stack = nullptr;   // 0: PackList, 1: ScopePicker
    QWidget*        m_packListPage = nullptr;
    QWidget*        m_scopePickerPage = nullptr;

    QLabel*         m_packHeading = nullptr;
    QWidget*        m_filterChipRow = nullptr;
    QLabel*         m_statusLine = nullptr;
    QListWidget*    m_packList = nullptr;

    QLabel*         m_scopeHeading = nullptr;
    QWidget*        m_scopeTileContainer = nullptr;
    QLabel*         m_scopeStatusLine = nullptr;
    QPushButton*    m_scopeBackBtn = nullptr;
    QPushButton*    m_scopeDownloadBtn = nullptr;
    QPushButton*    m_scopeCancelBtn = nullptr;
};

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 2: Write skeleton .cpp**

Create `src/ui/pages/stream/TheatreDownloadPanel.cpp`:

```cpp
#include "ui/pages/stream/TheatreDownloadPanel.h"

#include "core/stream/StreamDownloadIndex.h"
#include "core/torrent/TorrentClient.h"
#include "ui/dialogs/AddTorrentDialog.h"  // for AddTorrentConfig struct only

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace tankoban::stream::theatre {

TheatreDownloadPanel::TheatreDownloadPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("TheatreDownloadPanel"));
    buildUI();
}

void TheatreDownloadPanel::setSearchEngine(UnifiedPackSearchEngine* engine) {
    if (m_searchEngine == engine) return;
    if (m_searchEngine) {
        disconnect(m_searchEngine, nullptr, this, nullptr);
    }
    m_searchEngine = engine;
    if (m_searchEngine) {
        connect(m_searchEngine, &UnifiedPackSearchEngine::packResults,
                this, &TheatreDownloadPanel::onPackResults);
        connect(m_searchEngine, &UnifiedPackSearchEngine::searchComplete,
                this, &TheatreDownloadPanel::onSearchComplete);
    }
}

void TheatreDownloadPanel::setStreamDownloadIndex(StreamDownloadIndex* idx) {
    m_downloadIndex = idx;
}

void TheatreDownloadPanel::setTorrentClient(TorrentClient* client) {
    m_torrentClient = client;
}

void TheatreDownloadPanel::openFor(const QString& imdbId,
                                   const QString& showName,
                                   int season,
                                   const QString& mediaType) {
    m_imdbId    = imdbId;
    m_showName  = showName;
    m_season    = season;
    m_mediaType = mediaType;
    m_packs.clear();
    m_filteredPacks.clear();
    m_widenedAutoFallback = false;
    m_tileChecked.clear();

    if (m_packHeading) {
        const QString suffix = (mediaType == QLatin1String("movie"))
            ? QString()
            : (season > 0 ? QStringLiteral(" · Season %1").arg(season)
                          : QStringLiteral(" · Whole show"));
        m_packHeading->setText(tr("Download · %1%2").arg(showName, suffix));
    }
    if (m_statusLine)
        m_statusLine->setText(tr("Searching sources..."));

    transitionTo(State::PackList);
    if (m_searchEngine)
        m_searchEngine->search(imdbId, showName, season);
}

void TheatreDownloadPanel::reset() {
    m_packs.clear();
    m_filteredPacks.clear();
    m_tileChecked.clear();
    if (m_packList) m_packList->clear();
    if (m_statusLine) m_statusLine->clear();
    transitionTo(State::PackList);
}

void TheatreDownloadPanel::buildUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, /*stretch=*/1);

    buildPackListState();
    buildScopePickerState();

    m_stack->addWidget(m_packListPage);     // index 0
    m_stack->addWidget(m_scopePickerPage);  // index 1
    m_stack->setCurrentIndex(0);
}

void TheatreDownloadPanel::buildPackListState() {
    m_packListPage = new QWidget(this);
    auto* col = new QVBoxLayout(m_packListPage);
    col->setContentsMargins(12, 12, 12, 12);
    col->setSpacing(8);

    m_packHeading = new QLabel(m_packListPage);
    m_packHeading->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #f3f4f6;");
    col->addWidget(m_packHeading);

    m_filterChipRow = new QWidget(m_packListPage);
    m_filterChipRow->setLayout(new QHBoxLayout);
    m_filterChipRow->layout()->setContentsMargins(0, 0, 0, 0);
    m_filterChipRow->layout()->setSpacing(6);
    col->addWidget(m_filterChipRow);
    // Chip widgets get populated in Task C3.

    m_statusLine = new QLabel(m_packListPage);
    m_statusLine->setStyleSheet(
        "font-size: 11px; color: rgba(255,255,255,0.56);");
    col->addWidget(m_statusLine);

    m_packList = new QListWidget(m_packListPage);
    m_packList->setObjectName(QStringLiteral("TheatreDownloadPackList"));
    m_packList->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 0; }");
    connect(m_packList, &QListWidget::currentRowChanged,
            this, &TheatreDownloadPanel::onPackRowSelected);
    col->addWidget(m_packList, /*stretch=*/1);
}

void TheatreDownloadPanel::buildScopePickerState() {
    m_scopePickerPage = new QWidget(this);
    auto* col = new QVBoxLayout(m_scopePickerPage);
    col->setContentsMargins(12, 12, 12, 12);
    col->setSpacing(8);

    auto* topRow = new QHBoxLayout();
    m_scopeBackBtn = new QPushButton(QStringLiteral("←"), m_scopePickerPage);
    m_scopeBackBtn->setObjectName(QStringLiteral("TheatreScopeBackBtn"));
    m_scopeBackBtn->setFixedSize(30, 30);
    m_scopeBackBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scopeBackBtn, &QPushButton::clicked,
            this, &TheatreDownloadPanel::onScopeBackClicked);
    topRow->addWidget(m_scopeBackBtn);

    m_scopeHeading = new QLabel(m_scopePickerPage);
    m_scopeHeading->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #f3f4f6;");
    topRow->addWidget(m_scopeHeading, /*stretch=*/1);
    col->addLayout(topRow);

    m_scopeStatusLine = new QLabel(m_scopePickerPage);
    m_scopeStatusLine->setStyleSheet(
        "font-size: 10px; color: rgba(255,255,255,0.48);");
    col->addWidget(m_scopeStatusLine);

    m_scopeTileContainer = new QWidget(m_scopePickerPage);
    m_scopeTileContainer->setLayout(new QVBoxLayout);
    m_scopeTileContainer->layout()->setContentsMargins(0, 0, 0, 0);
    m_scopeTileContainer->layout()->setSpacing(4);
    col->addWidget(m_scopeTileContainer, /*stretch=*/1);
    // Tile widgets get populated in Task D1.

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_scopeCancelBtn = new QPushButton(tr("Cancel"), m_scopePickerPage);
    m_scopeCancelBtn->setObjectName(QStringLiteral("TheatreScopeCancelBtn"));
    m_scopeCancelBtn->setFixedHeight(30);
    m_scopeCancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scopeCancelBtn, &QPushButton::clicked, this, [this]() {
        reset();
        emit dismissRequested();
    });
    btnRow->addWidget(m_scopeCancelBtn);

    m_scopeDownloadBtn = new QPushButton(tr("Download"), m_scopePickerPage);
    m_scopeDownloadBtn->setObjectName(QStringLiteral("TheatreScopeDownloadBtn"));
    m_scopeDownloadBtn->setFixedHeight(30);
    m_scopeDownloadBtn->setCursor(Qt::PointingHandCursor);
    m_scopeDownloadBtn->setEnabled(false);
    connect(m_scopeDownloadBtn, &QPushButton::clicked,
            this, &TheatreDownloadPanel::onDownloadClicked);
    btnRow->addWidget(m_scopeDownloadBtn);
    col->addLayout(btnRow);
}

void TheatreDownloadPanel::transitionTo(State newState) {
    if (!m_stack) return;
    m_stack->setCurrentIndex(newState == State::ScopePicker ? 1 : 0);
}

void TheatreDownloadPanel::onPackResults(const QString& imdbId, int season,
                                        const QList<EnrichedPack>& results) {
    if (imdbId != m_imdbId || season != m_season)
        return;
    m_packs.append(results);
    rerenderPackList();
}

void TheatreDownloadPanel::onSearchComplete(const QString& imdbId, int season,
                                           int totalPacks) {
    if (imdbId != m_imdbId || season != m_season)
        return;
    if (totalPacks == 0 && !m_widenedAutoFallback) {
        autoFallbackToShowWide();
        return;
    }
    if (m_statusLine)
        m_statusLine->setText(tr("%1 packs found").arg(m_filteredPacks.size()));
}

void TheatreDownloadPanel::onPackRowSelected(int row) {
    if (row < 0 || row >= m_filteredPacks.size())
        return;
    m_selectedPack = m_filteredPacks.at(row);
    m_scopeEstimate = estimate(m_selectedPack.raw.title);
    if (m_scopeHeading)
        m_scopeHeading->setText(m_selectedPack.raw.title);
    // Tile rendering lands in Task D2.
    rerenderScopePicker();
    transitionTo(State::ScopePicker);
}

void TheatreDownloadPanel::onScopeBackClicked() {
    transitionTo(State::PackList);
}

void TheatreDownloadPanel::onDownloadClicked() {
    // Build AddTorrentConfig from current tile selections; emit
    // downloadRequested for the host to dispatch via TorrentClient.
    // Wired fully in Task D6.
}

void TheatreDownloadPanel::onFilterChipClicked() {
    // Filter chip logic lands in Task C3.
}

void TheatreDownloadPanel::rerenderPackList() {
    // Filter + sort + populate. Full implementation in Task C2 (PackListItem
    // widget) + Task C3 (filter chips).
    m_filteredPacks = m_packs;  // placeholder: no filter yet
    if (m_packList) {
        m_packList->clear();
        for (const auto& p : m_filteredPacks) {
            m_packList->addItem(p.raw.title);  // placeholder; C2 swaps for PackListItem widget
        }
    }
}

void TheatreDownloadPanel::rerenderScopePicker() {
    // Tile rendering lands in Task D2.
}

void TheatreDownloadPanel::autoFallbackToShowWide() {
    m_widenedAutoFallback = true;
    if (m_statusLine)
        m_statusLine->setText(tr("No season packs found · showing whole-show packs"));
    if (m_searchEngine)
        m_searchEngine->search(m_imdbId, m_showName, /*season=*/0);
}

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 3: Wire CMakeLists**

Add `src/ui/pages/stream/TheatreDownloadPanel.cpp` to `SOURCES` + `.h` to `HEADERS` (group with other `src/ui/pages/stream/` entries).

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task C1 — TheatreDownloadPanel skeleton class. QWidget with internal QStackedWidget for PackList ↔ ScopePicker state transition. Heading + filter chip row container + status line + QListWidget for pack rows. Back arrow + heading + status + tile container + Cancel/Download action bar for scope picker. UnifiedPackSearchEngine signals connected. AutoFallback widening + placeholder pack-list rendering wired. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TheatreDownloadPanel.h, src/ui/pages/stream/TheatreDownloadPanel.cpp, CMakeLists.txt
```

---

### Task C2: `PackListItem` widget (title + chips + meta layout)

**Files:**
- Create: `src/ui/pages/stream/PackListItem.h`
- Create: `src/ui/pages/stream/PackListItem.cpp`
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp` (use `PackListItem` instead of `QListWidget::addItem`)
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/stream/PackListItem.h`:

```cpp
#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — pack-list row widget.
// 3-line vertical layout: bold title (13px, single line, elide-right) +
// chip row (10px, 4px chip radius, grayscale borders) + meta line
// (11px, 48% opacity). Reuses StreamSourceCard hierarchy per Codex
// expansion 5.3.A.

#include "core/stream/UnifiedPackSearchEngine.h"

#include <QFrame>

class QLabel;
class QHBoxLayout;

namespace tankoban::stream::theatre {

class PackListItem : public QFrame {
    Q_OBJECT
public:
    explicit PackListItem(const EnrichedPack& pack, QWidget* parent = nullptr);

    void setSelected(bool selected);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void buildUI();
    QLabel* makeChip(const QString& text);

    EnrichedPack m_pack;
    bool         m_selected = false;
    bool         m_hovered  = false;

    QLabel*      m_titleLabel = nullptr;
    QHBoxLayout* m_chipRow    = nullptr;
    QLabel*      m_metaLabel  = nullptr;
};

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/pages/stream/PackListItem.cpp`:

```cpp
#include "ui/pages/stream/PackListItem.h"

#include "core/stream/PackClassifier.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace tankoban::stream::theatre {

PackListItem::PackListItem(const EnrichedPack& pack, QWidget* parent)
    : QFrame(parent), m_pack(pack) {
    setObjectName(QStringLiteral("PackListItem"));
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(82);
    buildUI();
}

void PackListItem::buildUI() {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 10, 12, 10);
    col->setSpacing(4);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextFormat(Qt::PlainText);
    QFontMetrics fm(m_titleLabel->font());
    const QString elided = fm.elidedText(m_pack.raw.title,
                                          Qt::ElideRight,
                                          width() > 0 ? width() - 24 : 400);
    m_titleLabel->setText(elided);
    m_titleLabel->setToolTip(m_pack.raw.title);
    m_titleLabel->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #f3f4f6;");
    col->addWidget(m_titleLabel);

    auto* chipRowWrapper = new QWidget(this);
    m_chipRow = new QHBoxLayout(chipRowWrapper);
    m_chipRow->setContentsMargins(0, 0, 0, 0);
    m_chipRow->setSpacing(6);

    // Pack-type chip.
    m_chipRow->addWidget(makeChip(labelForType(m_pack.classification.type)));

    // Source chip.
    m_chipRow->addWidget(makeChip(m_pack.sourceLabel));

    m_chipRow->addStretch();
    col->addWidget(chipRowWrapper);

    m_metaLabel = new QLabel(this);
    const QString sizeStr = m_pack.raw.sizeBytes > 0
        ? QStringLiteral("%1 GB").arg(m_pack.raw.sizeBytes / 1'000'000'000.0, 0, 'f', 1)
        : QStringLiteral("size unknown");
    m_metaLabel->setText(QStringLiteral("%1 seeders · %2 · score %3")
                            .arg(m_pack.raw.seeders)
                            .arg(sizeStr)
                            .arg(static_cast<int>(m_pack.combinedScore)));
    m_metaLabel->setStyleSheet(
        "font-size: 11px; color: rgba(255,255,255,0.48);");
    col->addWidget(m_metaLabel);

    setStyleSheet(
        "PackListItem { background: transparent; border-top: 1px solid rgba(255,255,255,0.06); }"
        "PackListItem:hover { background: rgba(255,255,255,0.04); }");
}

QLabel* PackListItem::makeChip(const QString& text) {
    auto* chip = new QLabel(text, this);
    chip->setObjectName(QStringLiteral("PackListItemChip"));
    chip->setStyleSheet(
        "QLabel#PackListItemChip {"
        "  background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.14);"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  font-size: 10px; font-weight: 600;"
        "  color: rgba(255,255,255,0.78); }");
    return chip;
}

void PackListItem::setSelected(bool selected) {
    if (m_selected == selected) return;
    m_selected = selected;
    setStyleSheet(
        QStringLiteral("PackListItem { background: %1; border-top: 1px solid rgba(255,255,255,0.06); }"
                       "PackListItem:hover { background: rgba(255,255,255,0.06); }")
            .arg(selected ? "rgba(255,255,255,0.08)" : "transparent"));
}

void PackListItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QFrame::mousePressEvent(event);
}

void PackListItem::enterEvent(QEnterEvent*) { m_hovered = true; update(); }
void PackListItem::leaveEvent(QEvent*)     { m_hovered = false; update(); }

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 3: Use `PackListItem` in `TheatreDownloadPanel::rerenderPackList`**

Modify `src/ui/pages/stream/TheatreDownloadPanel.cpp` — replace the placeholder `m_packList->addItem(p.raw.title)` block:

```cpp
void TheatreDownloadPanel::rerenderPackList() {
    if (!m_packList) return;
    m_packList->clear();
    m_filteredPacks = m_packs;  // filter logic added in Task C3
    // Sort by combinedScore descending.
    std::sort(m_filteredPacks.begin(), m_filteredPacks.end(),
        [](const EnrichedPack& a, const EnrichedPack& b) {
            return a.combinedScore > b.combinedScore;
        });
    for (const auto& p : m_filteredPacks) {
        auto* item = new QListWidgetItem(m_packList);
        auto* widget = new PackListItem(p, m_packList);
        item->setSizeHint(widget->minimumSizeHint());
        m_packList->addItem(item);
        m_packList->setItemWidget(item, widget);
    }
}
```

Also add `#include "ui/pages/stream/PackListItem.h"` + `#include <algorithm>` at the top of the .cpp.

- [ ] **Step 4: Wire CMakeLists**

Add `src/ui/pages/stream/PackListItem.cpp` to `SOURCES` + `.h` to `HEADERS`.

- [ ] **Step 5: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task C2 — PackListItem widget + integration into TheatreDownloadPanel::rerenderPackList. 3-line layout per Codex expansion 5.3.A (title 13px weight 600 + chip row with pack-type + source chips + meta line 11px 48% opacity). Hover state via QSS. Score-descending sort. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/PackListItem.h, src/ui/pages/stream/PackListItem.cpp, src/ui/pages/stream/TheatreDownloadPanel.cpp, CMakeLists.txt
```

---

### Task C3: Filter chip row (single-select per dimension)

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.h` (add helper member if needed)
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp`

- [ ] **Step 1: Add filter-chip-row populator method**

In `TheatreDownloadPanel.cpp`, add a new private helper `populateFilterChips()` called from `buildPackListState`:

```cpp
namespace {

QPushButton* makeFilterChip(const QString& text, bool isActive, QWidget* parent) {
    auto* chip = new QPushButton(text, parent);
    chip->setCheckable(true);
    chip->setChecked(isActive);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setFixedHeight(26);
    chip->setObjectName(QStringLiteral("TheatreFilterChip"));
    chip->setStyleSheet(
        "QPushButton#TheatreFilterChip {"
        "  background: transparent;"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 4px;"
        "  color: rgba(255,255,255,0.62);"
        "  padding: 0 6px;"
        "  font-size: 10px; font-weight: 500; }"
        "QPushButton#TheatreFilterChip:hover {"
        "  background: rgba(255,255,255,0.06); }"
        "QPushButton#TheatreFilterChip:checked {"
        "  background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.26);"
        "  color: #f3f4f6; font-weight: 600; }");
    return chip;
}

}  // namespace
```

Then in `buildPackListState`, after creating `m_filterChipRow`, replace the empty placeholder with:

```cpp
    auto* chipLayout = qobject_cast<QHBoxLayout*>(m_filterChipRow->layout());
    chipLayout->setSpacing(6);

    // Type filter group: All / Complete Series / Multi-Season / Season Pack / Single Episode
    const QStringList typeOptions = {
        QStringLiteral("All"),
        QStringLiteral("Complete Series"),
        QStringLiteral("Multi-Season"),
        QStringLiteral("Season Pack"),
        QStringLiteral("Single Episode"),
    };
    for (const QString& opt : typeOptions) {
        auto* chip = makeFilterChip(opt, opt == m_typeFilter, m_filterChipRow);
        chip->setProperty("filterDimension", "type");
        chip->setProperty("filterValue", opt);
        connect(chip, &QPushButton::clicked, this, &TheatreDownloadPanel::onFilterChipClicked);
        chipLayout->addWidget(chip);
    }

    chipLayout->addSpacing(10);  // visual separator between dimension groups

    // Source filter group: All sources / Stremio / Indexers
    const QStringList sourceOptions = {
        QStringLiteral("All sources"),
        QStringLiteral("Stremio"),
        QStringLiteral("Indexers"),
    };
    for (const QString& opt : sourceOptions) {
        auto* chip = makeFilterChip(opt, opt == m_sourceFilter, m_filterChipRow);
        chip->setProperty("filterDimension", "source");
        chip->setProperty("filterValue", opt);
        connect(chip, &QPushButton::clicked, this, &TheatreDownloadPanel::onFilterChipClicked);
        chipLayout->addWidget(chip);
    }

    chipLayout->addStretch();
```

- [ ] **Step 2: Implement `onFilterChipClicked`**

Replace the empty `onFilterChipClicked` body in `TheatreDownloadPanel.cpp`:

```cpp
void TheatreDownloadPanel::onFilterChipClicked() {
    auto* sender = qobject_cast<QPushButton*>(QObject::sender());
    if (!sender) return;
    const QString dim = sender->property("filterDimension").toString();
    const QString val = sender->property("filterValue").toString();
    if (dim == QLatin1String("type"))
        m_typeFilter = val;
    else if (dim == QLatin1String("source"))
        m_sourceFilter = val;

    // Single-select within dimension: uncheck other chips in same dimension.
    if (m_filterChipRow) {
        const auto chips = m_filterChipRow->findChildren<QPushButton*>(
            QStringLiteral("TheatreFilterChip"));
        for (auto* c : chips) {
            const QString cDim = c->property("filterDimension").toString();
            const QString cVal = c->property("filterValue").toString();
            if (cDim == dim)
                c->setChecked(cVal == val);
        }
    }
    rerenderPackList();
}
```

- [ ] **Step 3: Apply filter in `rerenderPackList`**

Update `rerenderPackList` to apply both filter dimensions before sort:

```cpp
void TheatreDownloadPanel::rerenderPackList() {
    if (!m_packList) return;
    m_packList->clear();

    m_filteredPacks.clear();
    for (const auto& p : m_packs) {
        // Type filter.
        if (m_typeFilter != QLatin1String("All")) {
            if (labelForType(p.classification.type) != m_typeFilter)
                continue;
        }
        // Source filter.
        if (m_sourceFilter == QLatin1String("Stremio") && p.source != PackSource::Stremio)
            continue;
        if (m_sourceFilter == QLatin1String("Indexers") && p.source != PackSource::Tankorent)
            continue;
        m_filteredPacks.append(p);
    }

    std::sort(m_filteredPacks.begin(), m_filteredPacks.end(),
        [](const EnrichedPack& a, const EnrichedPack& b) {
            return a.combinedScore > b.combinedScore;
        });

    for (const auto& p : m_filteredPacks) {
        auto* item = new QListWidgetItem(m_packList);
        auto* widget = new PackListItem(p, m_packList);
        item->setSizeHint(widget->minimumSizeHint());
        m_packList->addItem(item);
        m_packList->setItemWidget(item, widget);
    }
    if (m_statusLine && !m_packs.isEmpty())
        m_statusLine->setText(tr("%1 packs found").arg(m_filteredPacks.size()));
}
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task C3 — Filter chip row with two single-select dimensions (pack type + source) per Codex expansion 5.2.B. 26px chips, grayscale active/inactive states, single-select-within-dimension via sender's filterDimension/filterValue properties. Live re-filter on click. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TheatreDownloadPanel.cpp
```

---

### Task C4: Loading status line + indeterminate progress bar

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.h`
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp`

- [ ] **Step 1: Add `QProgressBar*` member**

In `TheatreDownloadPanel.h`, add to private members:

```cpp
    QProgressBar* m_loadingBar = nullptr;
```

Forward-declare `QProgressBar` at the top of the header.

- [ ] **Step 2: Create the bar in `buildPackListState`**

In `TheatreDownloadPanel.cpp`, after the `m_statusLine` creation:

```cpp
    m_loadingBar = new QProgressBar(m_packListPage);
    m_loadingBar->setObjectName(QStringLiteral("TheatreLoadingBar"));
    m_loadingBar->setMinimum(0);
    m_loadingBar->setMaximum(0);  // indeterminate
    m_loadingBar->setFixedHeight(2);
    m_loadingBar->setTextVisible(false);
    m_loadingBar->setStyleSheet(
        "QProgressBar#TheatreLoadingBar {"
        "  background: rgba(255,255,255,0.08);"
        "  border: none; border-radius: 1px; }"
        "QProgressBar#TheatreLoadingBar::chunk {"
        "  background: rgba(255,255,255,0.34); }");
    col->addWidget(m_loadingBar);
```

Add `#include <QProgressBar>` at the top.

- [ ] **Step 3: Show/hide on search lifecycle**

In `openFor`:

```cpp
    if (m_loadingBar) m_loadingBar->show();
```

In `onSearchComplete`:

```cpp
    if (m_loadingBar) m_loadingBar->hide();
```

In `reset`:

```cpp
    if (m_loadingBar) m_loadingBar->hide();
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task C4 — Indeterminate 2px loading progress bar pinned below status line per Codex expansion 5.2.C. Grayscale chunk + track. Shown on search start, hidden on searchComplete. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TheatreDownloadPanel.h, src/ui/pages/stream/TheatreDownloadPanel.cpp
```

---

### Task C5: Auto-fallback widening (zero-results → show-wide search)

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp`

- [ ] **Step 1: Strengthen `autoFallbackToShowWide`**

The skeleton already has this method. Strengthen its behavior to:
1. Show an inline explanation (NOT silent, per Codex's cross-cutting pushback on decision 11).
2. Re-trigger the search with season=0.
3. Show the loading bar again until the second search completes.

Update `autoFallbackToShowWide`:

```cpp
void TheatreDownloadPanel::autoFallbackToShowWide() {
    m_widenedAutoFallback = true;
    if (m_statusLine)
        m_statusLine->setText(tr("No Season %1 packs found · showing whole-show packs that include this season")
                                  .arg(m_season));
    if (m_loadingBar) m_loadingBar->show();
    if (m_searchEngine)
        m_searchEngine->search(m_imdbId, m_showName, /*season=*/0);
}
```

- [ ] **Step 2: Filter widened results to those that include the original season**

Update `rerenderPackList` to additionally filter Complete Series / Multi-Season packs that include `m_season` when `m_widenedAutoFallback == true`:

```cpp
    for (const auto& p : m_packs) {
        // Apply existing type + source filters first.
        // [...existing filter logic from Task C3...]

        // Auto-fallback filter: only show packs that include m_season.
        if (m_widenedAutoFallback) {
            const auto& seasons = p.classification.detectedSeasons;
            const bool isComplete = p.classification.isCompleteSeries;
            const bool includesRequested =
                isComplete  // Complete Series always includes every season
                || seasons.contains(m_season);
            if (!includesRequested)
                continue;
        }
        m_filteredPacks.append(p);
    }
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task C5 — Auto-fallback widening with inline explanation (not silent, per Codex cross-cutting pushback on brainstorm decision 11). When season-specific search returns zero, re-issues search with season=0 + status line reads "No Season N packs found · showing whole-show packs that include this season". Widened results filtered to those whose classification.detectedSeasons contains the original season OR isCompleteSeries. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:receiving-code-review] | files: src/ui/pages/stream/TheatreDownloadPanel.cpp
```

---

## Phase D — Panel scope-picker state

### Task D1: `EpisodeTile` widget

**Files:**
- Create: `src/ui/pages/stream/EpisodeTile.h`
- Create: `src/ui/pages/stream/EpisodeTile.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/stream/EpisodeTile.h`:

```cpp
#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — scope-picker episode tile.
// Compact row: S·E label + title (if known) + size + checkbox + optional
// "Have" badge for already-downloaded episodes. Per Codex expansion
// 5.5.A + 5.5.B.

#include <QFrame>

class QCheckBox;
class QLabel;

namespace tankoban::stream::theatre {

struct EpisodeTileData {
    int     season = 0;
    int     episode = 0;
    QString title;
    qint64  sizeBytes = 0;
    bool    alreadyHave = false;
};

class EpisodeTile : public QFrame {
    Q_OBJECT
public:
    explicit EpisodeTile(const EpisodeTileData& data, QWidget* parent = nullptr);

    bool isChecked() const;
    void setChecked(bool checked);

    int season() const  { return m_data.season; }
    int episode() const { return m_data.episode; }

signals:
    void toggled(bool checked);

private:
    void buildUI();

    EpisodeTileData m_data;
    QCheckBox*      m_checkBox = nullptr;
    QLabel*         m_seLabel  = nullptr;
    QLabel*         m_titleLabel = nullptr;
    QLabel*         m_sizeLabel  = nullptr;
    QLabel*         m_haveBadge  = nullptr;
};

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/pages/stream/EpisodeTile.cpp`:

```cpp
#include "ui/pages/stream/EpisodeTile.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>

namespace tankoban::stream::theatre {

EpisodeTile::EpisodeTile(const EpisodeTileData& data, QWidget* parent)
    : QFrame(parent), m_data(data) {
    setObjectName(QStringLiteral("EpisodeTile"));
    setMinimumHeight(36);
    buildUI();
}

void EpisodeTile::buildUI() {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(8);

    m_checkBox = new QCheckBox(this);
    // Default check: true unless the episode is already-have (smart skip).
    m_checkBox->setChecked(!m_data.alreadyHave);
    connect(m_checkBox, &QCheckBox::toggled, this, &EpisodeTile::toggled);
    row->addWidget(m_checkBox);

    m_seLabel = new QLabel(
        QStringLiteral("S%1E%2")
            .arg(m_data.season, 2, 10, QLatin1Char('0'))
            .arg(m_data.episode, 2, 10, QLatin1Char('0')),
        this);
    m_seLabel->setFixedWidth(56);
    m_seLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: rgba(255,255,255,0.78);");
    row->addWidget(m_seLabel);

    m_titleLabel = new QLabel(m_data.title, this);
    m_titleLabel->setStyleSheet("font-size: 11px; color: #e0e0e0;");
    row->addWidget(m_titleLabel, /*stretch=*/1);

    m_sizeLabel = new QLabel(this);
    if (m_data.sizeBytes > 0) {
        const double mb = m_data.sizeBytes / 1'000'000.0;
        m_sizeLabel->setText(QStringLiteral("%1 MB").arg(mb, 0, 'f', 0));
    } else {
        m_sizeLabel->setText(QStringLiteral("—"));
    }
    m_sizeLabel->setStyleSheet("font-size: 10px; color: rgba(255,255,255,0.48);");
    row->addWidget(m_sizeLabel);

    m_haveBadge = new QLabel(tr("Have"), this);
    m_haveBadge->setObjectName(QStringLiteral("EpisodeTileHaveBadge"));
    m_haveBadge->setStyleSheet(
        "QLabel#EpisodeTileHaveBadge {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 2px;"
        "  padding: 1px 5px;"
        "  font-size: 9px; font-weight: 700;"
        "  color: rgba(255,255,255,0.62); }");
    m_haveBadge->setVisible(m_data.alreadyHave);
    row->addWidget(m_haveBadge);

    setStyleSheet(
        "EpisodeTile { background: transparent; border-bottom: 1px solid rgba(255,255,255,0.04); }"
        "EpisodeTile:hover { background: rgba(255,255,255,0.03); }");
}

bool EpisodeTile::isChecked() const { return m_checkBox && m_checkBox->isChecked(); }

void EpisodeTile::setChecked(bool checked) {
    if (m_checkBox) m_checkBox->setChecked(checked);
}

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 3: Wire CMakeLists**

Add to `SOURCES` + `HEADERS`.

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task D1 — EpisodeTile widget. Compact row: checkbox + S·E label + title + size + Have badge per Codex expansion 5.5.B. Default-checked unless data.alreadyHave (smart-skip). Grayscale Have badge respects no-color rule. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/EpisodeTile.h, src/ui/pages/stream/EpisodeTile.cpp, CMakeLists.txt
```

---

### Task D2: Per-season collapsible scope-picker rendering

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp`

- [ ] **Step 1: Implement `rerenderScopePicker`**

Replace the empty `rerenderScopePicker` body. The method consumes `m_scopeEstimate` (populated in `onPackRowSelected`) and builds per-season collapsible groups containing `EpisodeTile`s:

```cpp
#include "ui/pages/stream/EpisodeTile.h"
// ... other includes

namespace {
quint32 tileKey(int season, int episode) {
    return (static_cast<quint32>(season) << 16) | static_cast<quint32>(episode);
}
}

void TheatreDownloadPanel::rerenderScopePicker() {
    if (!m_scopeTileContainer) return;
    // Clear existing children.
    auto* layout = qobject_cast<QVBoxLayout*>(m_scopeTileContainer->layout());
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_tileChecked.clear();

    // Movie mode: degenerate single-tile.
    if (m_mediaType == QLatin1String("movie")) {
        EpisodeTileData d;
        d.season = 0;
        d.episode = 0;
        d.title = m_selectedPack.raw.title;
        d.sizeBytes = m_selectedPack.raw.sizeBytes;
        d.alreadyHave = m_downloadIndex
            && m_downloadIndex->filePathForMovie(m_imdbId).has_value();
        auto* tile = new EpisodeTile(d, m_scopeTileContainer);
        layout->addWidget(tile);
        m_tileChecked[tileKey(0, 0)] = tile->isChecked();
        connect(tile, &EpisodeTile::toggled, this, [this, tile](bool checked) {
            m_tileChecked[tileKey(tile->season(), tile->episode())] = checked;
            // Update Download button label/state below.
        });
        // Movie-mode action button label.
        if (m_scopeDownloadBtn) {
            m_scopeDownloadBtn->setEnabled(d.alreadyHave ? false : true);
            m_scopeDownloadBtn->setText(tr("Download · %1 GB")
                                          .arg(d.sizeBytes / 1'000'000'000.0, 0, 'f', 2));
        }
        return;
    }

    // Series mode: group tiles by season.
    QMap<int, QList<EpisodeEstimate>> bySeason;
    for (const auto& ep : m_scopeEstimate.episodes)
        bySeason[ep.season].append(ep);

    auto addSeasonGroup = [this, layout](int season, const QList<EpisodeEstimate>& eps) {
        auto* header = new QLabel(
            QStringLiteral("Season %1  ·  %2 episodes").arg(season).arg(eps.size()),
            m_scopeTileContainer);
        header->setStyleSheet(
            "font-size: 11px; font-weight: 700; color: rgba(255,255,255,0.66);"
            "padding: 6px 8px; background: rgba(255,255,255,0.04);"
            "border-bottom: 1px solid rgba(255,255,255,0.08);");
        layout->addWidget(header);

        for (const auto& ep : eps) {
            EpisodeTileData d;
            d.season  = ep.season;
            d.episode = ep.episode;
            d.title   = ep.title;
            d.alreadyHave = m_downloadIndex
                && m_downloadIndex->filePathFor(m_imdbId, ep.season, ep.episode).has_value();
            auto* tile = new EpisodeTile(d, m_scopeTileContainer);
            layout->addWidget(tile);
            m_tileChecked[tileKey(ep.season, ep.episode)] = tile->isChecked();
            connect(tile, &EpisodeTile::toggled, this, [this, tile](bool checked) {
                m_tileChecked[tileKey(tile->season(), tile->episode())] = checked;
                // Live update Download button label.
                int count = 0;
                for (auto it = m_tileChecked.constBegin(); it != m_tileChecked.constEnd(); ++it)
                    if (it.value()) ++count;
                if (m_scopeDownloadBtn) {
                    m_scopeDownloadBtn->setEnabled(count > 0);
                    m_scopeDownloadBtn->setText(tr("Download %1 episode%2")
                                                  .arg(count)
                                                  .arg(count == 1 ? QString() : QStringLiteral("s")));
                }
            });
        }
    };

    for (auto it = bySeason.constBegin(); it != bySeason.constEnd(); ++it)
        addSeasonGroup(it.key(), it.value());

    // Initial Download button label.
    int initialCount = 0;
    for (auto it = m_tileChecked.constBegin(); it != m_tileChecked.constEnd(); ++it)
        if (it.value()) ++initialCount;
    if (m_scopeDownloadBtn) {
        m_scopeDownloadBtn->setEnabled(initialCount > 0);
        m_scopeDownloadBtn->setText(tr("Download %1 episodes").arg(initialCount));
    }
}
```

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 3: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task D2 — Per-season scope-picker rendering. Tiles grouped under per-season headers; movie mode renders single degenerate tile. Already-have detection via StreamDownloadIndex::filePathFor (or filePathForMovie). Live-update Download button label as user toggles tiles. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TheatreDownloadPanel.cpp
```

---

### Task D3: Real-metadata refresh hook

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.h`
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp`

- [ ] **Step 1: Add metadata-refresh slot + member**

In `TheatreDownloadPanel.h`, add a private slot + new state member:

```cpp
private slots:
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);

private:
    QString m_pendingMetadataHash;  // hash we're waiting on
    QJsonArray m_realFiles;          // populated from metadata
```

Forward-declare `QJsonArray`.

- [ ] **Step 2: Connect to engine on `setTorrentClient`**

In `TheatreDownloadPanel.cpp`, update `setTorrentClient`:

```cpp
void TheatreDownloadPanel::setTorrentClient(TorrentClient* client) {
    if (m_torrentClient == client) return;
    if (m_torrentClient && m_torrentClient->engine()) {
        disconnect(m_torrentClient->engine(), &TorrentEngine::metadataReady,
                   this, &TheatreDownloadPanel::onMetadataReady);
    }
    m_torrentClient = client;
    if (m_torrentClient && m_torrentClient->engine()) {
        connect(m_torrentClient->engine(), &TorrentEngine::metadataReady,
                this, &TheatreDownloadPanel::onMetadataReady,
                Qt::QueuedConnection);
    }
}
```

Add `#include "core/torrent/TorrentClient.h"` and `#include "core/torrent/TorrentEngine.h"` and `#include <QJsonArray>` if not present.

- [ ] **Step 3: Kick off `resolveMetadata` when entering scope picker**

In `onPackRowSelected`, after assigning `m_selectedPack`:

```cpp
    // Kick off real metadata resolution so the scope picker tiles can
    // refresh with real filenames + sizes.
    if (m_torrentClient && !m_selectedPack.raw.magnetUri.isEmpty()) {
        m_pendingMetadataHash = m_torrentClient->resolveMetadata(m_selectedPack.raw.magnetUri);
    }
```

- [ ] **Step 4: Refresh tiles on metadata arrival**

Implement `onMetadataReady`:

```cpp
void TheatreDownloadPanel::onMetadataReady(const QString& infoHash, const QString& name,
                                           qint64 totalSize, const QJsonArray& files) {
    Q_UNUSED(name);
    Q_UNUSED(totalSize);
    if (infoHash != m_pendingMetadataHash) return;
    m_realFiles = files;

    // Rebuild scope picker using real file list.
    // Map real files → episode tiles via BulkPackVerifier.
    // Implementation: iterate files, for each call matchEpisodeFileForSeason
    // for each detected season, and use the file's name + size to populate
    // tile data.

    // For now (Task D3 scope), trigger a rerender that the executor will
    // wire to real metadata in Task D5; here we capture the file list +
    // signal that real data is available.
    rerenderScopePicker();  // re-runs with potentially real data
}
```

Real file-list-to-tile mapping lands in Task D4 (filePriorities driver) since that's where file-index matters most.

- [ ] **Step 5: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task D3 — Real-metadata refresh hook. resolveMetadata fires on pack selection; metadataReady signal connected to onMetadataReady which captures m_realFiles for D5 use. Hybrid metadata strategy per brainstorm decision 9. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TheatreDownloadPanel.h, src/ui/pages/stream/TheatreDownloadPanel.cpp
```

---

### Task D4: File-priority driver + `onDownloadClicked` implementation

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp`

- [ ] **Step 1: Implement `onDownloadClicked`**

Replace the empty `onDownloadClicked` body:

```cpp
void TheatreDownloadPanel::onDownloadClicked() {
    if (m_selectedPack.raw.magnetUri.isEmpty() && m_selectedPack.raw.infoHash.isEmpty())
        return;
    if (!m_torrentClient) return;

    AddTorrentConfig config;
    config.category        = QStringLiteral("videos");
    config.destinationPath = m_torrentClient->defaultPaths().value("videos");
    config.contentLayout   = QStringLiteral("original");
    config.streamGroupId   = QString();
    config.sequential      = false;
    config.startPaused     = false;
    config.imdbId          = m_imdbId;
    config.season          = m_season;

    // Build filePriorities based on tile selection. For each real file in
    // m_realFiles, run BulkPackVerifier::matchEpisodeFileForSeason for each
    // detected season; if the matched (season, episode) is in m_tileChecked
    // and checked, set priority=1, else priority=0.
    QMap<int, int> priorities;
    QVector<int> selectedIndices;
    for (int idx = 0; idx < m_realFiles.size(); ++idx) {
        QJsonObject file = m_realFiles.at(idx).toObject();
        if (!file.contains("index")) file.insert("index", idx);

        int matchedEp = 0;
        int matchedIdx = 0;
        bool keepFile = false;
        const auto seasons = m_scopeEstimate.detectedSeasons;
        for (int season : seasons) {
            const bool ok = tankostream::stream::BulkPackVerifier::matchEpisodeFileForSeason(
                file, season, &matchedEp, &matchedIdx);
            if (ok && matchedEp > 0) {
                const quint32 key = tileKey(season, matchedEp);
                if (m_tileChecked.value(key, false)) {
                    keepFile = true;
                    selectedIndices.append(idx);
                }
                break;
            }
        }
        priorities[idx] = keepFile ? 1 : 0;
    }
    config.filePriorities = priorities;
    config.selectedIndices = selectedIndices;

    // Dispatch via host signal.
    const QString hash = m_pendingMetadataHash.isEmpty()
        ? m_torrentClient->resolveMetadata(m_selectedPack.raw.magnetUri)
        : m_pendingMetadataHash;
    if (hash.isEmpty()) {
        qWarning() << "TheatreDownloadPanel::onDownloadClicked: empty hash; aborting";
        return;
    }

    emit downloadRequested(m_imdbId, m_season,
                           m_selectedPack.raw.magnetUri, hash, config);

    // Reset + return to PackList state for further picking.
    reset();
}
```

Add `#include "core/stream/BulkPackVerifier.h"` at the top.

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 3: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task D4 — onDownloadClicked + file-priority driver. Builds AddTorrentConfig with imdbId/season identity (Phase A1 contract) + filePriorities computed by running BulkPackVerifier::matchEpisodeFileForSeason against each real file; tile-unchecked files get priority 0, non-episode files (no match) get priority 0 (preemptive skip per brainstorm decision 19). Emits downloadRequested for host to dispatch via TorrentClient. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TheatreDownloadPanel.cpp
```

---

## Phase E — Show-view integration

### Task E1: Replace two season-header buttons with single `Download` button

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Remove old buttons in `buildUI`**

In `StreamDetailView.cpp`'s `buildUI` (around lines 525-580 per recent Tankorent integration), delete:

- `m_downloadSeasonBtn` creation block + its connects + its addWidget
- `seasonTankorentBtn` creation block (local, from Phase E1 of prior arc) + its connects + its addWidget

- [ ] **Step 2: Add single `m_downloadBtn`**

In `StreamDetailView.h`, replace `m_downloadSeasonBtn` member with `m_downloadBtn`. Delete `m_downloadSelectedBtn` if it's not used elsewhere (audit usages first).

In `StreamDetailView.cpp`'s `buildUI`, replace the old buttons section with:

```cpp
    m_downloadBtn = new QPushButton(tr("Download"), m_seasonRow);
    m_downloadBtn->setObjectName(QStringLiteral("DetailDownloadBtn"));
    m_downloadBtn->setFixedHeight(30);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    m_downloadBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
    m_downloadBtn->setStyleSheet(
        "#DetailDownloadBtn { background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px;"
        "  color: #ddd; padding: 0 12px; font-size: 12px; }"
        "#DetailDownloadBtn:hover { background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.22); }");
    connect(m_downloadBtn, &QPushButton::clicked, this, [this]() {
        const int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
        emit theatreDownloadRequested(m_currentImdb,
                                       m_titleLabel ? m_titleLabel->text() : QString(),
                                       season, m_currentType);
    });
    seasonLayout->addWidget(m_downloadBtn);
```

- [ ] **Step 3: Add `theatreDownloadRequested` signal**

In `StreamDetailView.h` signals section:

```cpp
signals:
    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — unified Download button. Host
    // (StreamPage / MainWindow) opens TheatreDownloadPanel with this context.
    void theatreDownloadRequested(const QString& imdbId,
                                  const QString& showName,
                                  int season,
                                  const QString& mediaType);
```

- [ ] **Step 4: Remove the H2 movie button click handler routing**

The old `onDownloadViaTankorentClicked` slot (filled in Phase E1 of prior arc) can be deleted — its behavior is replaced by the new `theatreDownloadRequested` signal. Audit call sites and remove.

The movie `m_movieTankorentBtn` (from Phase H2 of prior arc) on the `movieActionRow` should be renamed to `m_movieDownloadBtn` (or kept as-is) but its click handler should emit `theatreDownloadRequested` with season=0 + mediaType="movie".

- [ ] **Step 5: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task E1 — Replace two season-header buttons (Download Season + Download via Tankorent) with single Download button. Movie show-view movieActionRow button retargeted to emit same theatreDownloadRequested signal with season=0 + mediaType="movie". Delete onDownloadViaTankorentClicked slot + its call sites. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp
```

---

### Task E2: Slide-swap with Sources panel in right pane

**Files:**
- Modify: `src/ui/pages/StreamPage.h`
- Modify: `src/ui/pages/StreamPage.cpp`

- [ ] **Step 1: Add `TheatreDownloadPanel*` + `UnifiedPackSearchEngine*` members**

In `StreamPage.h`, add forward declarations + member pointers:

```cpp
namespace tankoban::stream::theatre {
class TheatreDownloadPanel;
}

class UnifiedPackSearchEngine;  // namespace shorthand if used

// In private members:
    tankoban::stream::theatre::TheatreDownloadPanel* m_theatreDownloadPanel = nullptr;
    tankoban::stream::theatre::UnifiedPackSearchEngine* m_unifiedSearchEngine = nullptr;
    QWidget* m_rightPaneStack = nullptr;  // host for Sources panel ↔ TheatreDownloadPanel
```

- [ ] **Step 2: Construct in `buildUI` or right-pane setup**

Find the existing right-pane construction (Sources panel parent) in `StreamPage.cpp`. Add alongside:

```cpp
    m_unifiedSearchEngine = new tankoban::stream::theatre::UnifiedPackSearchEngine(
        m_streamAggregator, m_nam, this);
    m_theatreDownloadPanel = new tankoban::stream::theatre::TheatreDownloadPanel(this);
    m_theatreDownloadPanel->setSearchEngine(m_unifiedSearchEngine);
    m_theatreDownloadPanel->setStreamDownloadIndex(m_streamDownloadIndex);
    m_theatreDownloadPanel->setTorrentClient(m_torrentClient);
    m_theatreDownloadPanel->hide();
    // Add to the same right-pane container as Sources, but hidden by default.
```

- [ ] **Step 3: Wire `theatreDownloadRequested` signal**

Connect StreamDetailView's signal:

```cpp
    connect(m_detailView, &StreamDetailView::theatreDownloadRequested,
            this, [this](const QString& imdbId, const QString& showName,
                         int season, const QString& mediaType) {
        if (!m_theatreDownloadPanel) return;
        // Hide Sources panel, show download panel.
        if (m_sourcesPanel) m_sourcesPanel->hide();
        m_theatreDownloadPanel->show();
        m_theatreDownloadPanel->openFor(imdbId, showName, season, mediaType);
    });
```

And handle dismiss:

```cpp
    connect(m_theatreDownloadPanel, &tankoban::stream::theatre::TheatreDownloadPanel::dismissRequested,
            this, [this]() {
        if (m_theatreDownloadPanel) m_theatreDownloadPanel->hide();
        if (m_sourcesPanel) m_sourcesPanel->show();
    });
```

And handle `downloadRequested` → dispatch via TorrentClient:

```cpp
    connect(m_theatreDownloadPanel, &tankoban::stream::theatre::TheatreDownloadPanel::downloadRequested,
            this, [this](const QString& imdbId, int season,
                         const QString& magnetUri, const QString& infoHash,
                         const AddTorrentConfig& config) {
        if (!m_torrentClient || infoHash.isEmpty()) return;
        m_torrentClient->startDownload(infoHash, config);
        // Slide panel out, Sources back in.
        if (m_theatreDownloadPanel) m_theatreDownloadPanel->hide();
        if (m_sourcesPanel) m_sourcesPanel->show();
    });
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task E2 — Slide-swap with Sources panel. StreamPage owns TheatreDownloadPanel + UnifiedPackSearchEngine; both connected to existing StreamAggregator + StreamDownloadIndex + TorrentClient. theatreDownloadRequested signal triggers panel open + Sources hide; dismissRequested / downloadRequested both restore Sources. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/StreamPage.h, src/ui/pages/StreamPage.cpp
```

---

### Task E3: Slide transition animation (180ms cross-slide)

**Files:**
- Modify: `src/ui/pages/StreamPage.cpp` (animation logic for both panels)

- [ ] **Step 1: Add `QPropertyAnimation` helpers**

Wrap the show/hide calls with `QPropertyAnimation` on the panel widgets' `pos` + `opacity` properties. Per Codex expansion 5.2.A: simultaneous slide + fade, 180ms total, `OutCubic` for entry / `InCubic` for exit, anchor to right-pane slot edge.

```cpp
namespace {

void slideOutToRight(QWidget* widget, int dxPx, int durationMs) {
    if (!widget) return;
    auto* anim = new QPropertyAnimation(widget, "pos", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(widget->pos());
    anim->setEndValue(widget->pos() + QPoint(dxPx, 0));
    anim->setEasingCurve(QEasingCurve::InCubic);
    QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
        widget->hide();
        widget->move(widget->pos() - QPoint(/*dxPx=*/24, 0));  // reset
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void slideInFromRight(QWidget* widget, int durationMs) {
    if (!widget) return;
    widget->move(widget->pos() + QPoint(/*startOffset=*/32, 0));
    widget->show();
    auto* anim = new QPropertyAnimation(widget, "pos", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(widget->pos());
    anim->setEndValue(widget->pos() - QPoint(32, 0));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

}  // namespace
```

Add `#include <QPropertyAnimation>` + `#include <QEasingCurve>`.

- [ ] **Step 2: Replace show/hide calls with slide helpers**

In the three connect lambdas from Task E2, replace `hide()` / `show()` calls:

```cpp
    // theatreDownloadRequested:
    slideOutToRight(m_sourcesPanel, 24, 180);
    slideInFromRight(m_theatreDownloadPanel, 180);

    // dismissRequested / downloadRequested:
    slideOutToRight(m_theatreDownloadPanel, 24, 180);
    slideInFromRight(m_sourcesPanel, 180);
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task E3 — 180ms simultaneous slide-swap transition per Codex expansion 5.2.A. QPropertyAnimation on pos; OutCubic for entry / InCubic for exit; anchored to right-pane slot edge (24-32px deltas, not full window edge). Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/StreamPage.cpp
```

---

## Phase F — Progress + completion UX

### Task F1: `SeasonHeaderProgressBadge` widget

**Files:**
- Create: `src/ui/pages/stream/SeasonHeaderProgressBadge.h`
- Create: `src/ui/pages/stream/SeasonHeaderProgressBadge.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/stream/SeasonHeaderProgressBadge.h`:

```cpp
#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — aggregate per-season download
// progress badge. Sits in the season-header row of StreamDetailView next
// to the Download button. Shows "Downloading SN: M/N · X.X GB / Y.Y GB ·
// Z KB/s" when active; hidden when idle. Has a Stop affordance for cancel
// per Codex expansion 5.7.A + 5.9.

#include <QFrame>

class QLabel;
class QPushButton;
class StreamDownloadIndex;
class TorrentClient;

namespace tankoban::stream::theatre {

class SeasonHeaderProgressBadge : public QFrame {
    Q_OBJECT
public:
    explicit SeasonHeaderProgressBadge(QWidget* parent = nullptr);

    void setContext(const QString& imdbId, int season);
    void setTorrentClient(TorrentClient* client);
    void setStreamDownloadIndex(StreamDownloadIndex* idx);

    void refresh();  // recompute aggregate state + show/hide

signals:
    void stopRequested(const QString& imdbId, int season);

private:
    void buildUI();

    QString m_imdbId;
    int     m_season = 0;
    TorrentClient*       m_torrentClient = nullptr;
    StreamDownloadIndex* m_downloadIndex = nullptr;

    QLabel*      m_text = nullptr;
    QPushButton* m_stopBtn = nullptr;
};

}  // namespace tankoban::stream::theatre
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/pages/stream/SeasonHeaderProgressBadge.cpp`:

```cpp
#include "ui/pages/stream/SeasonHeaderProgressBadge.h"

#include "core/stream/StreamDownloadIndex.h"
#include "core/torrent/TorrentClient.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace tankoban::stream::theatre {

SeasonHeaderProgressBadge::SeasonHeaderProgressBadge(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("SeasonHeaderProgressBadge"));
    setStyleSheet(
        "SeasonHeaderProgressBadge {"
        "  background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.14);"
        "  border-radius: 4px; }");
    buildUI();
    hide();
}

void SeasonHeaderProgressBadge::buildUI() {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 4, 4);
    row->setSpacing(6);

    m_text = new QLabel(this);
    m_text->setStyleSheet(
        "font-size: 11px; color: rgba(255,255,255,0.78);");
    row->addWidget(m_text);

    m_stopBtn = new QPushButton(QStringLiteral("×"), this);
    m_stopBtn->setObjectName(QStringLiteral("SeasonProgressStopBtn"));
    m_stopBtn->setFixedSize(20, 20);
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setStyleSheet(
        "QPushButton#SeasonProgressStopBtn {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.56); font-size: 14px;"
        "  padding: 0; }"
        "QPushButton#SeasonProgressStopBtn:hover {"
        "  background: rgba(255,255,255,0.10);"
        "  border-radius: 10px;"
        "  color: #ffffff; }");
    connect(m_stopBtn, &QPushButton::clicked, this, [this]() {
        emit stopRequested(m_imdbId, m_season);
    });
    row->addWidget(m_stopBtn);
}

void SeasonHeaderProgressBadge::setContext(const QString& imdbId, int season) {
    m_imdbId = imdbId;
    m_season = season;
    refresh();
}

void SeasonHeaderProgressBadge::setTorrentClient(TorrentClient* client) {
    m_torrentClient = client;
    refresh();
}

void SeasonHeaderProgressBadge::setStreamDownloadIndex(StreamDownloadIndex* idx) {
    m_downloadIndex = idx;
    refresh();
}

void SeasonHeaderProgressBadge::refresh() {
    if (!m_torrentClient || m_imdbId.isEmpty() || m_season <= 0) {
        hide();
        return;
    }
    // Query TorrentClient for in-flight torrents matching this imdbId+season.
    // Reuse the existing streamBulkSnapshotForImdbSeason where possible;
    // extend for Tankorent-bound downloads via a parallel snapshot.
    //
    // For now: aggregate any records where record.imdbId == m_imdbId and
    // record.season == m_season and state != completed.
    const auto records = m_torrentClient->snapshotRecordsForImdbSeason(m_imdbId, m_season);
    int total = records.size();
    int done = 0;
    qint64 sizeTotal = 0;
    qint64 sizeDone  = 0;
    double rateSum   = 0.0;
    bool anyActive = false;
    for (const auto& r : records) {
        if (r.state == QLatin1String("completed")) {
            ++done;
            sizeDone += r.totalSize;
        } else {
            anyActive = true;
            rateSum += r.downloadRate;
        }
        sizeTotal += r.totalSize;
    }
    if (!anyActive) {
        hide();
        return;
    }
    const QString text = tr("Downloading S%1: %2/%3 · %4 GB / %5 GB · %6 KB/s")
        .arg(m_season)
        .arg(done)
        .arg(total)
        .arg(sizeDone / 1'000'000'000.0, 0, 'f', 1)
        .arg(sizeTotal / 1'000'000'000.0, 0, 'f', 1)
        .arg(static_cast<int>(rateSum / 1024.0));
    m_text->setText(text);
    show();
}

}  // namespace tankoban::stream::theatre
```

NOTE: `snapshotRecordsForImdbSeason` may not exist on `TorrentClient`. The executor will need to either:
- Add a new public method to `TorrentClient` that walks `m_records` filtering by imdbId + season, OR
- Reuse `streamBulkSnapshotForImdbSeason` (exists per existing code) and extend its return shape to include non-bulk records too.

The executor adapts based on what's actually exposed.

- [ ] **Step 3: Wire CMakeLists**

Add to `SOURCES` + `HEADERS`.

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task F1 — SeasonHeaderProgressBadge widget. "Downloading SN: M/N · sizes · rate" + × Stop affordance. Hidden when idle. Aggregates via TorrentClient::snapshotRecordsForImdbSeason (new method) or extension of streamBulkSnapshotForImdbSeason. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/SeasonHeaderProgressBadge.h, src/ui/pages/stream/SeasonHeaderProgressBadge.cpp, CMakeLists.txt
```

---

### Task F2: Wire `SeasonHeaderProgressBadge` into StreamDetailView season header

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Add member + create in `buildUI`**

In `StreamDetailView.h`:

```cpp
class SeasonHeaderProgressBadge;  // forward

// In private members:
    tankoban::stream::theatre::SeasonHeaderProgressBadge* m_seasonProgressBadge = nullptr;
```

In `StreamDetailView.cpp::buildUI`, after `m_downloadBtn` creation but before `seasonLayout->addStretch()`:

```cpp
    m_seasonProgressBadge = new tankoban::stream::theatre::SeasonHeaderProgressBadge(m_seasonRow);
    seasonLayout->addWidget(m_seasonProgressBadge);
    connect(m_seasonProgressBadge,
            &tankoban::stream::theatre::SeasonHeaderProgressBadge::stopRequested,
            this, &StreamDetailView::onStopSeasonDownloadRequested);
```

Add `onStopSeasonDownloadRequested` slot that emits a signal up to StreamPage / MainWindow for actual stop dispatch.

- [ ] **Step 2: Refresh on season change**

In `onSeasonChanged` (where the season combo selection changes), update the badge context:

```cpp
    if (m_seasonProgressBadge) {
        const int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
        m_seasonProgressBadge->setContext(m_currentImdb, season);
    }
```

Also refresh on `entriesChanged` / periodic timer (1Hz) — match the existing `refreshEpisodeBulkProgress` cadence.

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task F2 — SeasonHeaderProgressBadge wired into StreamDetailView season header. Badge context updates on season combo change + on entriesChanged. Stop button emits stopRequested → onStopSeasonDownloadRequested slot → host. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp
```

---

### Task F3: Stop semantics — cancel-and-preserve

**Files:**
- Modify: `src/core/torrent/TorrentClient.h` (add `stopTorrentForImdbSeason` method)
- Modify: `src/core/torrent/TorrentClient.cpp`
- Modify: `src/ui/pages/StreamPage.cpp` (wire stop signal → TorrentClient)

- [ ] **Step 1: Add method declaration**

In `TorrentClient.h`:

```cpp
public:
    // THEATRE_DOWNLOAD_OVERHAUL F3 2026-05-16 — stop all torrents bound to
    // (imdbId, season). Finished-episode entries in StreamDownloadIndex
    // stay registered + on disk (Phase A4 already-registered files survive).
    // In-flight episode entries get evicted; their part-files removed.
    void stopTorrentForImdbSeason(const QString& imdbId, int season);
```

- [ ] **Step 2: Implement in .cpp**

```cpp
void TorrentClient::stopTorrentForImdbSeason(const QString& imdbId, int season) {
    QStringList hashesToStop;
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        const QJsonObject rec = it.value().toObject();
        if (rec.value("imdbId").toString() != imdbId) continue;
        if (rec.value("season").toInt() != season) continue;
        hashesToStop.append(it.key());
    }
    for (const QString& hash : hashesToStop) {
        // Stop libtorrent download for this hash.
        if (m_engine) m_engine->stopTorrent(hash);
        // Evict in-flight episode entries from StreamDownloadIndex.
        // Finished entries (those whose file exists on disk) stay.
        if (m_streamDownloadIndex) {
            // Walk index entries for this imdbId + season; evict those whose
            // file does not exist on disk (means they were in-flight, not
            // finished).
            const auto entries = m_streamDownloadIndex->entriesForImdb(imdbId);
            for (const auto& e : entries) {
                if (e.season != season) continue;
                if (!QFileInfo::exists(e.canonicalPath))
                    m_streamDownloadIndex->evictByPath(
                        StreamDownloadIndex::computeCanonicalKey(e.canonicalPath));
            }
        }
        // Remove the torrent record (deletes part-files for unfinished files).
        deleteTorrent(hash, /*deleteFiles=*/true);
    }
    saveRecords();
}
```

- [ ] **Step 3: Wire in StreamPage**

In `StreamPage.cpp`, connect StreamDetailView's `onStopSeasonDownloadRequested` (or equivalent signal) to TorrentClient:

```cpp
    connect(m_detailView, &StreamDetailView::stopSeasonDownloadRequested,
            this, [this](const QString& imdbId, int season) {
        if (m_torrentClient)
            m_torrentClient->stopTorrentForImdbSeason(imdbId, season);
    });
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task F3 — Stop-and-preserve semantics. TorrentClient::stopTorrentForImdbSeason stops all torrents matching identity, evicts in-flight StreamDownloadIndex entries (file missing on disk), preserves finished entries (file present). Wired through StreamDetailView::stopSeasonDownloadRequested → StreamPage → TorrentClient. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/core/torrent/TorrentClient.h, src/core/torrent/TorrentClient.cpp, src/ui/pages/StreamPage.cpp
```

---

## Phase G — Cleanup + smoke

### Task G1: Delete `TorrentPackPicker`

**Files:**
- Delete: `src/ui/pages/stream/TorrentPackPicker.h`
- Delete: `src/ui/pages/stream/TorrentPackPicker.cpp`
- Modify: `CMakeLists.txt` (remove from SOURCES + HEADERS)

- [ ] **Step 1: Confirm no remaining call sites**

Run:

```
grep -rn "TorrentPackPicker" src/
```

Expected: zero matches outside the files being deleted. If any remain, route them through the new TheatreDownloadPanel before proceeding.

- [ ] **Step 2: Delete the files**

```
git rm src/ui/pages/stream/TorrentPackPicker.h
git rm src/ui/pages/stream/TorrentPackPicker.cpp
```

- [ ] **Step 3: Update CMakeLists**

Remove the two lines (.cpp from SOURCES, .h from HEADERS).

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat` → expect `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task G1 — Delete TorrentPackPicker (Phase D output from prior TANKORENT_STREAM_INTEGRATION arc) now that TheatreDownloadPanel + UnifiedPackSearchEngine fully subsume its responsibilities. CMakeLists entries removed. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TorrentPackPicker.h (deleted), src/ui/pages/stream/TorrentPackPicker.cpp (deleted), CMakeLists.txt
```

---

### Task G2: Integration smoke recipe + agent-driven smoke

**Files:**
- Create: `docs/superpowers/plans/2026-05-16-theatre-download-overhaul-smoke.md`
- Append: `agents/audits/theatre_download_overhaul_smoke_2026-05-16.md`

- [ ] **Step 1: Write the smoke recipe doc**

Create `docs/superpowers/plans/2026-05-16-theatre-download-overhaul-smoke.md` following the template at `docs/superpowers/plans/2026-05-15-tankorent-stream-integration-smoke.md` (recipe + per-step verification + Hemanth visual-verify ask + cleanup).

The recipe verifies:
1. Theatre opens; topbar shows `[Comics, Books, Theatre]`.
2. Click any popular show → detail view opens.
3. Season header has ONE `Download` button (not two).
4. Click Download → Sources panel slides out, TheatreDownloadPanel slides in (180ms cross-slide).
5. Filter chip row visible with [All / Complete Series / Multi-Season / Season Pack / Single Episode] + [All sources / Stremio / Indexers].
6. Loading bar visible; status text "Searching sources..."; results stream in.
7. Each pack row shows title + chips (type + source) + meta line.
8. Filter chips live-filter the list.
9. Auto-fallback fires when zero season-specific results.
10. Click a pack → scope picker state opens; episode tiles render instantly from title estimate; loading indicator for metadata.
11. Already-downloaded tiles show "Have" badge + pre-unchecked.
12. Download button label updates live as tiles toggle.
13. Click Download → libtorrent download starts; SeasonHeaderProgressBadge appears with aggregate text.
14. After completion → per-episode ✓ icons in episode table; click episode → instant local playback.
15. Click × Stop → finished episodes preserved; in-flight evicted.

- [ ] **Step 2: Run the agent-driven smoke**

Per Rule 19: claim MCP LOCK in chat.md, run the smoke via pywinauto-mcp + tankoctl per the Phase G smoke pattern from TANKORENT_STREAM_INTEGRATION (which already established the full pattern including 10-screenshot capture, audit doc, Hemanth visual-verify ask).

- [ ] **Step 3: Document results**

Append to `agents/audits/theatre_download_overhaul_smoke_2026-05-16.md`.

- [ ] **Step 4: Post Hemanth visual-verify ask**

Recipe per the prior arc's pattern — Hemanth tests with a real popular show + real download completion + LOCAL chip verification.

- [ ] **Step 5: Release MCP LOCK + cleanup**

Run: `scripts/stop-tankoban.ps1` per Rule 17.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent X, THEATRE_DOWNLOAD_OVERHAUL Task G2 — Integration smoke recipe authored + agent-driven smoke executed + audit doc updated + Hemanth visual-verify ask posted. MCP LOCK claimed/released per Rule 19. Cleanup via stop-tankoban.ps1 per Rule 17.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion] | files: docs/superpowers/plans/2026-05-16-theatre-download-overhaul-smoke.md, agents/audits/theatre_download_overhaul_smoke_2026-05-16.md, agents/audits/smoke_evidence/*, agents/chat.md
```

---

## Out of scope (explicit punts)

See Section 6 of the brainstorm-md. Not re-litigated here.

## Parallelization + ownership

- **Phase A** (A1 + A2): pure-logic substrate, fully parallelizable across different files. TDD discipline throughout.
- **Phase B** (B1 + B2): B1 must land before B2; B2 depends on the `searchPacks` signal contract.
- **Phase C** (C1–C5): sequential. C1 (skeleton) gates C2/C3/C4. C2 + C3 + C4 can run in parallel after C1.
- **Phase D** (D1–D4): D1 (EpisodeTile) gates D2. D2 + D3 can run in parallel. D4 depends on D2 (for tile checked state) + D3 (for real files).
- **Phase E** (E1–E3): sequential. E1 gates E2 (StreamPage uses the new signal); E3 polishes E2.
- **Phase F** (F1–F3): F1 (badge widget) is independent; F2 depends on F1; F3 is parallel to F2.
- **Phase G** (G1 + G2): G1 (deletion) only after E1 lands and confirms no remaining call sites. G2 is the final smoke.

Subagent dispatch sweet spots:
- Phase A is the cleanest pure-logic TDD work — Codex Trigger D candidate (matches prior dispatches).
- Phase B is small, integration-shaped — Claude-side likely fastest.
- Phase C + D are UI-heavy with cross-task dependencies — single-agent execution recommended.
- Phase E + F + G are integration + polish + smoke — Claude-side execution preferred (MCP smoke can't be delegated to Codex per `project_codex_trigger_d_handoff_pattern.md`).

## Closing notes for the executor

- Always read Section 5 of the brainstorm-md (Codex's UI/UX expansion) for the visual specs the code references. Pixel values + color tokens + timings + transitions are sourced there.
- Per Rule 14: technical decisions inside each task (variable names, exact regex flavor, signal connection types) are your call. Strategic decisions (would you change the data flow? would you pick a different scoring weight?) — ASK first; don't decide silently.
- Per Rule 17: any agent-driven smoke ends with `scripts/stop-tankoban.ps1`.
- Per Rule 19: any desktop MCP work claims/releases `MCP LOCK` in chat.md.
- Trust the spec. Decisions ratified in Section 3 of the brainstorm-md are not up for re-negotiation in implementation.
- If a brainstorm-md decision conflicts with a real-world code path you discover during implementation, surface the tension (don't silently work around or violate). The TANKORENT_STREAM_INTEGRATION arc had several plan-sketch latent bugs that Codex silently-corrected; that pattern continues here — flag your corrections explicitly in the RTC.

This plan is complete. Estimated ~22 tasks across 7 phases. ~3-5 elapsed wakes if parallelized.
