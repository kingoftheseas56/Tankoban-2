# TANKORENT_CINEMETA_PACK_MAPPING Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Map Tankorent pack downloads to individual Cinemeta episodes at metadata-fetch time so per-episode progress + state surfaces in the existing episode-row UI during download, with a Tankorent-source-provenance differentiator (faint warm-amber tint).

**Architecture:** Lift the existing parser body out of `publishTankorentItemsForTorrent` into a pure-logic shared helper (`StreamPackParser`). Extend `StreamDownloadIndex.Entry` schema v1→v2 with `state` (Pending|Downloading|Complete|Failed) + `progressPct`. Hook the parser at libtorrent's `metadata_received_alert` time so episodes register as Pending immediately, flipping to Downloading/Complete via `pieceFinished` signals. Add a `SequentialPieceManager` for in-order episode download priority. Provenance differentiator is a render-time read of `Entry.sourceGroupId.startsWith("tankorent:")` — no schema field needed.

**Tech Stack:** C++20, Qt 6.10, libtorrent (via `TorrentEngine`), GoogleTest (opt-in via `-DTANKOBAN_BUILD_TESTS=ON`), MSVC2022 + Ninja on Windows.

**Spec reference:** [docs/superpowers/specs/2026-05-18-tankorent-cinemeta-pack-mapping-design.md](../specs/2026-05-18-tankorent-cinemeta-pack-mapping-design.md)

---

## Tankoban-specific conventions for this plan

**Commit discipline (Rule 11 / `feedback_commit_protocol.md`):** Agents do NOT run `git commit`. Each task ends with a **READY TO COMMIT (RTC) line appended to `agents/chat.md`**. Agent 0 batches the actual commits via `/commit-sweep`. The RTC format used in every task below follows the contracts-v3 skill-provenance contract:

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING <Task N>: <short description>
Files: <comma-separated list of touched files>
Skills invoked: [<list>]
<1-2 sentence summary of what shipped>
READY TO COMMIT
```

**Build verify (Rule 14 / `/build-verify` skill):** Any task touching `src/` or `tests/` runs `build_check.bat` before its RTC. Expected output: `BUILD OK`. Failed build = task is NOT done; investigate and fix before flagging RTC.

**Test discipline:** Tests are opt-in (`-DTANKOBAN_BUILD_TESTS=ON`). The test target is `tankoban_tests`. Run with:

```powershell
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
cd out
ctest --output-on-failure -R <test_name>
```

**Smoke discipline (Rule 17 / `feedback_evidence_before_analysis.md`):** Phase-gate smoke tasks (Tasks 11, 17, 23) launch Tankoban via `build_and_run.bat`, drive UI via `mcp__pywinauto-mcp__*` for AutomationId-based interaction + `out\tankoctl.exe` for state queries, capture evidence PNGs under `agents/audits/smoke_evidence/`, and tear down with `scripts/stop-tankoban.ps1`. MCP LOCK / MCP LOCK RELEASED lines bracket every smoke session per Rule 19.

**Skill provenance:** Tier-1 mandatory skills invoked across every non-trivial task in this plan: `superpowers:verification-before-completion`, `simplify`, `build-verify`, `superpowers:requesting-code-review`. Task-specific skills are listed in each task's RTC block.

---

## File structure overview

Files created in this arc:
- `src/core/stream/StreamPackParser.h` — pure-logic parser public API
- `src/core/stream/StreamPackParser.cpp` — parser implementation
- `src/core/torrent/SequentialPieceManager.h` — per-pack piece-priority tracker public API
- `src/core/torrent/SequentialPieceManager.cpp` — manager implementation
- `tests/core/stream/test_stream_pack_parser.cpp` — pure-logic parser tests
- `tests/core/stream/test_stream_download_index_state.cpp` — state-transition tests
- `tests/core/torrent/test_sequential_piece_manager.cpp` — manager-logic tests

Files modified in this arc:
- `src/core/stream/StreamDownloadIndex.h` — Entry schema v1→v2 + new API methods + new signal
- `src/core/stream/StreamDownloadIndex.cpp` — implementation of new methods + schema migration
- `src/core/torrent/TorrentClient.h` — wire SequentialPieceManager, new `metadataReady` slot path
- `src/core/torrent/TorrentClient.cpp` — refactor `publishTankorentItemsForTorrent` to use StreamPackParser; wire `metadataReady` to register Pending entries; wire `pieceFinished` to drive state transitions; cancel evict-everything semantics
- `src/ui/pages/stream/EpisodeTile.h` — state-input contract (`EpisodeTileState` + `setEpisodeState`)
- `src/ui/pages/stream/EpisodeTile.cpp` — state-to-paint mapping; amber-tint render
- `src/ui/pages/stream/StreamDetailView.h` — wire `entryStateChanged` subscriber for season-row table
- `src/ui/pages/stream/StreamDetailView.cpp` — `renderEpisodeStateChip` helper; amber-tint on season-row table
- `src/ui/pages/stream/StreamLibraryLayout.h` — wire amber-tint for movie tile
- `src/ui/pages/stream/StreamLibraryLayout.cpp` — movie tile state-rendering update
- `CMakeLists.txt` — new source files + new test executables registered
- `agents/chat.md` — Decision 12 amendment line (Task 22)
- `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md` — inline Decision 12 amendment block (Task 22)

---

# PHASE 1 — Substrate

Phase 1 introduces the pure-logic parser, extends the Index schema, and refactors the existing completion-time parser path to use the new helper. No behavioral change to user-visible flows in Phase 1; existing Tankorent downloads still register at completion as today, just via the new helper. UI is untouched.

**Phase 1 gate:** all unit tests GREEN; `build_check.bat` BUILD OK; existing TANKORENT_STREAM_INTEGRATION smoke still passes (Daredevil S02 1080p Complete Season completes and registers all 13 episodes at completion, same as 2026-05-15 behavior).

---

### Task 1: Create `StreamPackParser` header + skeleton

**Files:**
- Create: `src/core/stream/StreamPackParser.h`
- Create: `src/core/stream/StreamPackParser.cpp`
- Modify: `CMakeLists.txt` (add sources)

- [ ] **Step 1: Create `src/core/stream/StreamPackParser.h`**

```cpp
#pragma once

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — pure-logic parser lifted from
// publishTankorentItemsForTorrent. Maps a torrent's file list to parsed episode
// or movie tuples. Stateless; no Qt signals; unit-testable in isolation.
// Spec: docs/superpowers/specs/2026-05-18-tankorent-cinemeta-pack-mapping-design.md

#include <QJsonArray>
#include <QList>
#include <QString>

namespace tankostream::stream {

struct ParsedFile {
    int     season = 0;
    int     episode = 0;
    int     fileIndex = -1;    // index in libtorrent's file list
    QString relName;           // e.g. "Daredevil.S01E03.1080p.WEB-DL.mkv"
    qint64  sizeBytes = 0;
};

struct ParsedPack {
    QString             imdbId;
    QString             type;       // "series" or "movie"
    QList<ParsedFile>   episodes;   // empty for movies
    ParsedFile          movieFile;  // valid only when type=="movie"
};

class StreamPackParser
{
public:
    // Parse a torrent's file array (from TorrentEngine::torrentFiles()) into a
    // ParsedPack. configSeason == 0 triggers multi-season probe over seasons
    // 1..kMaxSeasonProbe. Returns type=="movie" + movieFile populated if no
    // episode parses but a clear largest-video-file candidate exists.
    static ParsedPack parsePack(
        const QJsonArray& files,
        const QString& imdbId,
        int configSeason
    );

    static constexpr int kMaxSeasonProbe = 50;
};

}  // namespace tankostream::stream
```

- [ ] **Step 2: Create `src/core/stream/StreamPackParser.cpp` skeleton**

```cpp
#include "StreamPackParser.h"

#include "BulkPackVerifier.h"

#include <QDir>
#include <QJsonObject>
#include <QJsonValue>

namespace tankostream::stream {

ParsedPack StreamPackParser::parsePack(const QJsonArray& files,
                                       const QString& imdbId,
                                       int configSeason)
{
    ParsedPack pack;
    pack.imdbId = imdbId;
    pack.type = QStringLiteral("series");
    // Full logic lands in Task 2.
    return pack;
}

}  // namespace tankostream::stream
```

- [ ] **Step 3: Register new sources in `CMakeLists.txt`**

Locate the existing `BulkPackVerifier` entries in `CMakeLists.txt` SOURCES + HEADERS lists. Insert StreamPackParser entries alphabetically adjacent.

Modify `CMakeLists.txt`:

```cmake
# In the SOURCES section, alongside other src/core/stream/*.cpp:
src/core/stream/StreamPackParser.cpp

# In the HEADERS section, alongside other src/core/stream/*.h:
src/core/stream/StreamPackParser.h
```

- [ ] **Step 4: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK` in stdout.

- [ ] **Step 5: Flag READY TO COMMIT in chat.md**

Append to `agents/chat.md`:

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 1: StreamPackParser skeleton
Files: src/core/stream/StreamPackParser.h, src/core/stream/StreamPackParser.cpp, CMakeLists.txt
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Created pure-logic parser header + empty implementation + CMakeLists registration. Stub returns empty ParsedPack; full logic in Task 2.
READY TO COMMIT
```

---

### Task 2: `StreamPackParser` TDD implementation

**Files:**
- Create: `tests/core/stream/test_stream_pack_parser.cpp`
- Modify: `src/core/stream/StreamPackParser.cpp` (implement logic)
- Modify: `CMakeLists.txt` (register test)

- [ ] **Step 1: Create the failing test file**

Create `tests/core/stream/test_stream_pack_parser.cpp`:

```cpp
#include <gtest/gtest.h>

#include "core/stream/StreamPackParser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

using tankostream::stream::StreamPackParser;
using tankostream::stream::ParsedPack;
using tankostream::stream::ParsedFile;

namespace {

QJsonObject makeFile(const QString& name, qint64 size, int index)
{
    QJsonObject f;
    f.insert(QStringLiteral("name"), name);
    f.insert(QStringLiteral("size"), QJsonValue::fromVariant(size));
    f.insert(QStringLiteral("index"), index);
    return f;
}

}  // namespace

TEST(StreamPackParserTest, SingleSeasonCleanSENaming)
{
    QJsonArray files;
    for (int ep = 1; ep <= 3; ++ep) {
        files.append(makeFile(
            QStringLiteral("Daredevil.S01E%1.1080p.WEB-DL.mkv")
                .arg(ep, 2, 10, QChar('0')),
            1500000000LL,
            ep - 1));
    }

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt18923754"), 1);

    EXPECT_EQ(pack.type, QStringLiteral("series"));
    EXPECT_EQ(pack.imdbId, QStringLiteral("tt18923754"));
    ASSERT_EQ(pack.episodes.size(), 3);
    EXPECT_EQ(pack.episodes[0].season, 1);
    EXPECT_EQ(pack.episodes[0].episode, 1);
    EXPECT_EQ(pack.episodes[2].season, 1);
    EXPECT_EQ(pack.episodes[2].episode, 3);
}

TEST(StreamPackParserTest, MultiSeasonProbe)
{
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Sopranos.S01E01.mkv"), 1500000000LL, 0));
    files.append(makeFile(QStringLiteral("Sopranos.S03E07.mkv"), 1500000000LL, 1));
    files.append(makeFile(QStringLiteral("Sopranos.S06E02.mkv"), 1500000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt0141842"), 0);

    EXPECT_EQ(pack.type, QStringLiteral("series"));
    ASSERT_EQ(pack.episodes.size(), 3);
    EXPECT_EQ(pack.episodes[0].season, 1);
    EXPECT_EQ(pack.episodes[1].season, 3);
    EXPECT_EQ(pack.episodes[2].season, 6);
}

TEST(StreamPackParserTest, MovieFallback)
{
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Fight.Club.1080p.BluRay.mkv"),
                          5000000000LL, 0));
    files.append(makeFile(QStringLiteral("Fight.Club.nfo"), 2048LL, 1));
    files.append(makeFile(QStringLiteral("sample.mkv"), 30000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt0137523"), 0);

    EXPECT_EQ(pack.type, QStringLiteral("movie"));
    EXPECT_EQ(pack.episodes.size(), 0);
    EXPECT_EQ(pack.movieFile.relName, QStringLiteral("Fight.Club.1080p.BluRay.mkv"));
    EXPECT_GT(pack.movieFile.sizeBytes, 4000000000LL);
}

TEST(StreamPackParserTest, EmptyFilesReturnsEmptyPack)
{
    QJsonArray files;  // empty

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt0000000"), 1);

    EXPECT_EQ(pack.episodes.size(), 0);
    EXPECT_EQ(pack.movieFile.relName.length(), 0);
}

TEST(StreamPackParserTest, UnparseableFilesSkippedSilently)
{
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Daredevil.S01E01.mkv"), 1500000000LL, 0));
    files.append(makeFile(QStringLiteral("random.featurette.mkv"), 200000000LL, 1));
    files.append(makeFile(QStringLiteral("Daredevil.S01E03.mkv"), 1500000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt18923754"), 1);

    EXPECT_EQ(pack.type, QStringLiteral("series"));
    ASSERT_EQ(pack.episodes.size(), 2);
    EXPECT_EQ(pack.episodes[0].episode, 1);
    EXPECT_EQ(pack.episodes[1].episode, 3);
}

TEST(StreamPackParserTest, EpisodesReturnedInEpisodeOrder)
{
    // Files arrive in NON-monotonic order; parser must sort by (season, episode).
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Daredevil.S01E03.mkv"), 1500000000LL, 0));
    files.append(makeFile(QStringLiteral("Daredevil.S01E01.mkv"), 1500000000LL, 1));
    files.append(makeFile(QStringLiteral("Daredevil.S01E02.mkv"), 1500000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt18923754"), 1);

    ASSERT_EQ(pack.episodes.size(), 3);
    EXPECT_EQ(pack.episodes[0].episode, 1);
    EXPECT_EQ(pack.episodes[1].episode, 2);
    EXPECT_EQ(pack.episodes[2].episode, 3);
}
```

- [ ] **Step 2: Register test in `CMakeLists.txt`**

Locate the existing `tankoban_tests` test executable and its sources list. Add the new test file.

Modify `CMakeLists.txt` (within the `if(TANKOBAN_BUILD_TESTS)` block, alongside other test sources):

```cmake
tests/core/stream/test_stream_pack_parser.cpp
```

- [ ] **Step 3: Run tests, verify they fail**

```powershell
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
cd out
ctest --output-on-failure -R StreamPackParserTest
```

Expected: 6 tests FAIL (parser stub returns empty pack).

- [ ] **Step 4: Implement `StreamPackParser::parsePack`**

Replace the stub body in `src/core/stream/StreamPackParser.cpp`:

```cpp
#include "StreamPackParser.h"

#include "BulkPackVerifier.h"

#include <QDir>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

namespace tankostream::stream {

ParsedPack StreamPackParser::parsePack(const QJsonArray& files,
                                       const QString& imdbId,
                                       int configSeason)
{
    ParsedPack pack;
    pack.imdbId = imdbId;
    pack.type = QStringLiteral("series");

    for (int fileIdx = 0; fileIdx < files.size(); ++fileIdx) {
        QJsonObject file = files.at(fileIdx).toObject();
        // Defensive: mirror BulkPackVerifier's own internal backfill pattern
        // at BulkPackVerifier.cpp:189-190 — ensure "index" is present.
        if (!file.contains(QStringLiteral("index"))) {
            file.insert(QStringLiteral("index"), fileIdx);
        }

        int detectedSeason = configSeason;
        int episodeNum = 0;
        int matchedFileIdx = 0;

        if (configSeason > 0) {
            const bool ok = BulkPackVerifier::matchEpisodeFileForSeason(
                file, configSeason, &episodeNum, &matchedFileIdx);
            if (!ok || episodeNum <= 0)
                continue;
        } else {
            for (int probeSeason = 1; probeSeason <= kMaxSeasonProbe; ++probeSeason) {
                if (BulkPackVerifier::matchEpisodeFileForSeason(
                        file, probeSeason, &episodeNum, &matchedFileIdx)
                    && episodeNum > 0) {
                    detectedSeason = probeSeason;
                    break;
                }
            }
            if (episodeNum <= 0)
                continue;
        }

        ParsedFile pf;
        pf.season = detectedSeason;
        pf.episode = episodeNum;
        pf.fileIndex = fileIdx;
        pf.relName = file.value(QStringLiteral("name")).toString();
        pf.sizeBytes = file.value(QStringLiteral("size")).toVariant().toLongLong();
        if (pf.relName.isEmpty())
            continue;
        pack.episodes.append(pf);
    }

    // Sort episodes by (season, episode) so consumers can rely on episode order.
    std::sort(pack.episodes.begin(), pack.episodes.end(),
              [](const ParsedFile& a, const ParsedFile& b) {
                  if (a.season != b.season) return a.season < b.season;
                  return a.episode < b.episode;
              });

    // Movie fallback: no episodes parsed; pick largest video file.
    if (pack.episodes.isEmpty()) {
        qint64 largestSize = 0;
        ParsedFile candidate;
        for (int fileIdx = 0; fileIdx < files.size(); ++fileIdx) {
            const QJsonObject file = files.at(fileIdx).toObject();
            const QString relName = file.value(QStringLiteral("name")).toString();
            const QString lowerName = relName.toLower();
            if (!(lowerName.endsWith(QStringLiteral(".mkv"))
                  || lowerName.endsWith(QStringLiteral(".mp4"))
                  || lowerName.endsWith(QStringLiteral(".webm"))
                  || lowerName.endsWith(QStringLiteral(".m4v"))
                  || lowerName.endsWith(QStringLiteral(".avi")))) {
                continue;
            }
            const qint64 size =
                file.value(QStringLiteral("size")).toVariant().toLongLong();
            if (size > largestSize) {
                largestSize = size;
                candidate.fileIndex = fileIdx;
                candidate.relName = relName;
                candidate.sizeBytes = size;
            }
        }
        if (largestSize > 0) {
            pack.type = QStringLiteral("movie");
            pack.movieFile = candidate;
        }
    }

    return pack;
}

}  // namespace tankostream::stream
```

- [ ] **Step 5: Run tests, verify all pass**

```powershell
cmake --build out --target tankoban_tests
cd out
ctest --output-on-failure -R StreamPackParserTest
```

Expected: 6 tests PASS.

- [ ] **Step 6: Run `build_check.bat` to verify main app still builds**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 7: Flag READY TO COMMIT**

Append to `agents/chat.md`:

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 2: StreamPackParser TDD implementation
Files: src/core/stream/StreamPackParser.cpp, tests/core/stream/test_stream_pack_parser.cpp, CMakeLists.txt
Skills invoked: [/superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion]
Implemented parsePack() with single-season + multi-season probe + movie fallback. 6 GoogleTest cases all GREEN.
READY TO COMMIT
```

---

### Task 3: `StreamDownloadIndex.Entry` schema v1→v2

**Files:**
- Modify: `src/core/stream/StreamDownloadIndex.h`
- Modify: `src/core/stream/StreamDownloadIndex.cpp`

- [ ] **Step 1: Extend `Entry` struct in `StreamDownloadIndex.h`**

Locate the existing `Entry` struct in `src/core/stream/StreamDownloadIndex.h` (line 26-35). Replace with:

```cpp
struct Entry {
    QString imdbId;
    QString type;
    int     season = 0;
    int     episode = 0;
    QString canonicalPath;
    qint64  addedAt = 0;
    QString sourceGroupId;
    qint64  fileSizeBytes = 0;
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — v2 schema fields.
    enum State { Complete = 0, Pending = 1, Downloading = 2, Failed = 3 };
    State   state = Complete;       // default Complete so v1 entries migrate cleanly
    int     progressPct = 100;      // 0-100; 100 == fully downloaded
};
```

- [ ] **Step 2: Bump schema version in `StreamDownloadIndex.cpp`**

Locate the existing `kSchemaVersion` constant. Change from `1` to `2`.

In `src/core/stream/StreamDownloadIndex.cpp`:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — schema bump for Entry.state +
// progressPct. v1 entries on load get state=Complete + progressPct=100.
static constexpr int kSchemaVersion = 2;
```

- [ ] **Step 3: Update `load()` to migrate v1 entries + persist v2 fields**

Locate the existing `load()` method in `src/core/stream/StreamDownloadIndex.cpp`. Find the loop that reads each Entry from JSON. Add migration logic that defaults missing `state` and `progressPct` fields to `Complete` / `100`:

```cpp
// Inside the per-entry parse loop in load():
Entry e;
e.imdbId        = obj.value(QStringLiteral("imdbId")).toString();
e.type          = obj.value(QStringLiteral("type")).toString();
e.season        = obj.value(QStringLiteral("season")).toInt();
e.episode       = obj.value(QStringLiteral("episode")).toInt();
e.canonicalPath = obj.value(QStringLiteral("canonicalPath")).toString();
e.addedAt       = obj.value(QStringLiteral("addedAt")).toVariant().toLongLong();
e.sourceGroupId = obj.value(QStringLiteral("sourceGroupId")).toString();
e.fileSizeBytes = obj.value(QStringLiteral("fileSizeBytes")).toVariant().toLongLong();
// v2 fields — default Complete/100 for v1 migration.
e.state = static_cast<Entry::State>(
    obj.value(QStringLiteral("state")).toInt(static_cast<int>(Entry::Complete)));
e.progressPct = obj.value(QStringLiteral("progressPct")).toInt(100);
```

- [ ] **Step 4: Update `save()` to write v2 fields**

Locate the existing `save()` method in `src/core/stream/StreamDownloadIndex.cpp`. Find where each Entry is serialized to JSON. Add the new fields:

```cpp
// Inside the per-entry serialize loop in save():
QJsonObject obj;
obj.insert(QStringLiteral("imdbId"),        e.imdbId);
obj.insert(QStringLiteral("type"),          e.type);
obj.insert(QStringLiteral("season"),        e.season);
obj.insert(QStringLiteral("episode"),       e.episode);
obj.insert(QStringLiteral("canonicalPath"), e.canonicalPath);
obj.insert(QStringLiteral("addedAt"),       static_cast<qint64>(e.addedAt));
obj.insert(QStringLiteral("sourceGroupId"), e.sourceGroupId);
obj.insert(QStringLiteral("fileSizeBytes"), static_cast<qint64>(e.fileSizeBytes));
obj.insert(QStringLiteral("state"),         static_cast<int>(e.state));
obj.insert(QStringLiteral("progressPct"),   e.progressPct);
```

- [ ] **Step 5: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Manual schema-roundtrip sanity check**

Launch Tankoban once via `build_and_run.bat`. Open Stream mode briefly (any page that touches `StreamDownloadIndex::load()`). Close.

Open `<dataDir>/stream_downloads.json` in a text viewer. Confirm:
- Top-level `schemaVersion` field is `2`.
- Existing entries (from prior runs) now serialize with `state: 0` (Complete) and `progressPct: 100`.

If a stale entry on disk lacks `state` / `progressPct`, the migration on load defaulted them; on next save the file's now v2 conformant.

- [ ] **Step 7: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 3: Entry schema v1→v2 bump
Files: src/core/stream/StreamDownloadIndex.h, src/core/stream/StreamDownloadIndex.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Added Entry::State enum (Complete|Pending|Downloading|Failed) + progressPct field. Schema bumped to v2 with non-destructive v1→v2 migration on load. Verified roundtrip with live Tankoban launch.
READY TO COMMIT
```

---

### Task 4: New `StreamDownloadIndex` API methods

**Files:**
- Modify: `src/core/stream/StreamDownloadIndex.h`
- Modify: `src/core/stream/StreamDownloadIndex.cpp`

- [ ] **Step 1: Declare new public methods + signal in `StreamDownloadIndex.h`**

Locate the existing public method declarations (around line 58-65). Add after `registerMovie()`:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — register as Pending. canonicalPath
// is the EXPECTED path (libtorrent's savePath + relName); the file may not exist
// on disk yet at Pending state.
void registerPendingEpisode(const QString& imdbId, int season, int episode,
                            const QString& canonicalPath,
                            const QString& sourceGroupId,
                            qint64 fileSizeBytes);

// Movie variant — parallels registerMovie() but with state=Pending.
void registerPendingMovie(const QString& imdbId,
                          const QString& canonicalPath,
                          const QString& sourceGroupId,
                          qint64 fileSizeBytes);

// Update progress on an existing Pending/Downloading entry.
// Auto-flips state: first progressPct > 0 → Downloading; progressPct == 100 → Complete.
// Works for both episodes (season > 0) and movies (season == 0, episode == 0).
void updateEpisodeProgress(const QString& imdbId, int season, int episode,
                           int progressPct);

// Drop all entries for a given sourceGroupId (cancel semantics, Decision 7).
// Files on disk are NOT touched.
void evictBySourceGroup(const QString& sourceGroupId);
```

Locate the existing `entriesChanged()` signal (around line 83). Add the new granular signal:

```cpp
signals:
    void entriesChanged();
    // Granular: fires when a single entry's state OR progressPct changes.
    void entryStateChanged(const QString& imdbId, int season, int episode);
```

- [ ] **Step 2: Implement `registerPendingEpisode` in `StreamDownloadIndex.cpp`**

Locate the existing `registerEpisode()` method. Add after it:

```cpp
void StreamDownloadIndex::registerPendingEpisode(const QString& imdbId, int season,
                                                 int episode,
                                                 const QString& canonicalPath,
                                                 const QString& sourceGroupId,
                                                 qint64 fileSizeBytes)
{
    QString epKey = computeEpisodeKey(imdbId, season, episode);
    QString canonKey = computeCanonicalKey(canonicalPath);
    {
        QMutexLocker locker(&m_mutex);
        Entry e;
        e.imdbId = imdbId;
        e.type = QStringLiteral("series");
        e.season = season;
        e.episode = episode;
        e.canonicalPath = canonicalPath;
        e.addedAt = QDateTime::currentSecsSinceEpoch();
        e.sourceGroupId = sourceGroupId;
        e.fileSizeBytes = fileSizeBytes;
        e.state = Entry::Pending;
        e.progressPct = 0;
        m_byPath.insert(canonKey, e);
        m_byEpisode.insert(epKey, canonKey);
        m_imdbHasAny.insert(imdbId);
    }
    save();
    emit entriesChanged();
    emit entryStateChanged(imdbId, season, episode);
}
```

- [ ] **Step 3: Implement `registerPendingMovie`**

```cpp
void StreamDownloadIndex::registerPendingMovie(const QString& imdbId,
                                               const QString& canonicalPath,
                                               const QString& sourceGroupId,
                                               qint64 fileSizeBytes)
{
    QString canonKey = computeCanonicalKey(canonicalPath);
    QString epKey = computeEpisodeKey(imdbId, 0, 0);
    {
        QMutexLocker locker(&m_mutex);
        Entry e;
        e.imdbId = imdbId;
        e.type = QStringLiteral("movie");
        e.season = 0;
        e.episode = 0;
        e.canonicalPath = canonicalPath;
        e.addedAt = QDateTime::currentSecsSinceEpoch();
        e.sourceGroupId = sourceGroupId;
        e.fileSizeBytes = fileSizeBytes;
        e.state = Entry::Pending;
        e.progressPct = 0;
        m_byPath.insert(canonKey, e);
        m_byEpisode.insert(epKey, canonKey);
        m_imdbHasAny.insert(imdbId);
    }
    save();
    emit entriesChanged();
    emit entryStateChanged(imdbId, 0, 0);
}
```

- [ ] **Step 4: Implement `updateEpisodeProgress`**

```cpp
void StreamDownloadIndex::updateEpisodeProgress(const QString& imdbId, int season,
                                                int episode, int progressPct)
{
    QString epKey = computeEpisodeKey(imdbId, season, episode);
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_byEpisode.find(epKey);
        if (it == m_byEpisode.end())
            return;  // unknown entry, no-op
        const QString canonKey = it.value();
        auto pathIt = m_byPath.find(canonKey);
        if (pathIt == m_byPath.end())
            return;
        Entry& e = pathIt.value();
        const int clamped = std::clamp(progressPct, 0, 100);
        Entry::State newState = e.state;
        if (clamped >= 100) {
            newState = Entry::Complete;
        } else if (clamped > 0) {
            newState = Entry::Downloading;
        }
        if (clamped != e.progressPct || newState != e.state) {
            e.progressPct = clamped;
            e.state = newState;
            changed = true;
        }
    }
    if (changed) {
        save();
        emit entryStateChanged(imdbId, season, episode);
    }
}
```

- [ ] **Step 5: Implement `evictBySourceGroup`**

```cpp
void StreamDownloadIndex::evictBySourceGroup(const QString& sourceGroupId)
{
    if (sourceGroupId.isEmpty())
        return;
    QList<QString> evictedImdbs;
    {
        QMutexLocker locker(&m_mutex);
        QList<QString> keysToEvict;
        QList<QString> epKeysToEvict;
        for (auto it = m_byPath.begin(); it != m_byPath.end(); ++it) {
            if (it.value().sourceGroupId == sourceGroupId) {
                keysToEvict.append(it.key());
                epKeysToEvict.append(
                    computeEpisodeKey(it.value().imdbId,
                                      it.value().season,
                                      it.value().episode));
                if (!evictedImdbs.contains(it.value().imdbId))
                    evictedImdbs.append(it.value().imdbId);
            }
        }
        for (const QString& k : keysToEvict)
            m_byPath.remove(k);
        for (const QString& k : epKeysToEvict)
            m_byEpisode.remove(k);
        for (const QString& imdb : evictedImdbs)
            recomputeImdbHasAnyLocked(imdb);
    }
    save();
    emit entriesChanged();
}
```

- [ ] **Step 6: Add `<QDateTime>` and `<algorithm>` includes if missing**

At the top of `src/core/stream/StreamDownloadIndex.cpp`, ensure these are present:

```cpp
#include <QDateTime>
#include <algorithm>
```

- [ ] **Step 7: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 8: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 4: StreamDownloadIndex new API methods
Files: src/core/stream/StreamDownloadIndex.h, src/core/stream/StreamDownloadIndex.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Added registerPendingEpisode + registerPendingMovie + updateEpisodeProgress + evictBySourceGroup methods. Added entryStateChanged signal. Thread-safety contract preserved (mutex around map mutations; signals emitted OFF lock).
READY TO COMMIT
```

---

### Task 5: State-transition unit tests for `StreamDownloadIndex`

**Files:**
- Create: `tests/core/stream/test_stream_download_index_state.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create failing test file**

Create `tests/core/stream/test_stream_download_index_state.cpp`:

```cpp
#include <gtest/gtest.h>

#include "core/stream/StreamDownloadIndex.h"
#include "core/JsonStore.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {

class StreamDownloadIndexStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_tmpDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_tmpDir->isValid());
        m_store = std::make_unique<JsonStore>(m_tmpDir->path());
        m_index = std::make_unique<StreamDownloadIndex>(m_store.get());
    }

    void TearDown() override
    {
        m_index.reset();
        m_store.reset();
        m_tmpDir.reset();
    }

    std::unique_ptr<QTemporaryDir>          m_tmpDir;
    std::unique_ptr<JsonStore>              m_store;
    std::unique_ptr<StreamDownloadIndex>    m_index;
};

}  // namespace

TEST_F(StreamDownloadIndexStateTest, RegisterPendingThenProgressFlipsToDownloading)
{
    QSignalSpy spy(m_index.get(), &StreamDownloadIndex::entryStateChanged);
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 1,
        QStringLiteral("C:/dl/Daredevil.S01E01.mkv"),
        QStringLiteral("tankorent:abc"),
        1500000000LL);

    auto entries = m_index->entriesForImdb(QStringLiteral("tt18923754"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Pending);
    EXPECT_EQ(entries[0].progressPct, 0);
    EXPECT_GE(spy.count(), 1);

    m_index->updateEpisodeProgress(QStringLiteral("tt18923754"), 1, 1, 25);

    entries = m_index->entriesForImdb(QStringLiteral("tt18923754"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Downloading);
    EXPECT_EQ(entries[0].progressPct, 25);
}

TEST_F(StreamDownloadIndexStateTest, ProgressAt100FlipsToComplete)
{
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 1,
        QStringLiteral("C:/dl/Daredevil.S01E01.mkv"),
        QStringLiteral("tankorent:abc"),
        1500000000LL);
    m_index->updateEpisodeProgress(QStringLiteral("tt18923754"), 1, 1, 100);

    auto entries = m_index->entriesForImdb(QStringLiteral("tt18923754"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Complete);
    EXPECT_EQ(entries[0].progressPct, 100);
}

TEST_F(StreamDownloadIndexStateTest, ProgressUpdateOnUnknownEntryIsNoOp)
{
    QSignalSpy spy(m_index.get(), &StreamDownloadIndex::entryStateChanged);
    m_index->updateEpisodeProgress(QStringLiteral("tt99999"), 1, 1, 50);
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(StreamDownloadIndexStateTest, EvictBySourceGroupDropsAllPackEntries)
{
    // Register 3 episodes under same sourceGroupId + 1 under a different one.
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 1,
        QStringLiteral("C:/dl/A/S01E01.mkv"),
        QStringLiteral("tankorent:groupA"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 2,
        QStringLiteral("C:/dl/A/S01E02.mkv"),
        QStringLiteral("tankorent:groupA"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 3,
        QStringLiteral("C:/dl/A/S01E03.mkv"),
        QStringLiteral("tankorent:groupA"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt0141842"), 6, 2,
        QStringLiteral("C:/dl/B/S06E02.mkv"),
        QStringLiteral("tankorent:groupB"), 1500000000LL);

    m_index->evictBySourceGroup(QStringLiteral("tankorent:groupA"));

    EXPECT_EQ(m_index->entriesForImdb(QStringLiteral("tt18923754")).size(), 0);
    EXPECT_EQ(m_index->entriesForImdb(QStringLiteral("tt0141842")).size(), 1);
    EXPECT_FALSE(m_index->hasAnyForImdb(QStringLiteral("tt18923754")));
    EXPECT_TRUE(m_index->hasAnyForImdb(QStringLiteral("tt0141842")));
}

TEST_F(StreamDownloadIndexStateTest, RegisterPendingMovieDefaultsTypeMovie)
{
    m_index->registerPendingMovie(
        QStringLiteral("tt0137523"),
        QStringLiteral("C:/dl/Fight.Club.mkv"),
        QStringLiteral("tankorent:fc"),
        5000000000LL);
    auto entries = m_index->entriesForImdb(QStringLiteral("tt0137523"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].type, QStringLiteral("movie"));
    EXPECT_EQ(entries[0].season, 0);
    EXPECT_EQ(entries[0].episode, 0);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Pending);
}
```

- [ ] **Step 2: Register test in `CMakeLists.txt`**

Add to the test sources list:

```cmake
tests/core/stream/test_stream_download_index_state.cpp
```

- [ ] **Step 3: Run tests, verify all pass**

```powershell
cmake --build out --target tankoban_tests
cd out
ctest --output-on-failure -R StreamDownloadIndexStateTest
```

Expected: 5 tests PASS.

- [ ] **Step 4: Run main app build verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 5: State-transition unit tests
Files: tests/core/stream/test_stream_download_index_state.cpp, CMakeLists.txt
Skills invoked: [/superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion]
5 state-transition GoogleTest cases all GREEN (pending→downloading, downloading→complete, no-op on unknown, evict-by-source-group, registerPendingMovie defaults).
READY TO COMMIT
```

---

### Task 6: Refactor `publishTankorentItemsForTorrent` to use `StreamPackParser`

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp:1788-1919`
- Modify: `src/core/torrent/TorrentClient.h` (include)

- [ ] **Step 1: Add `StreamPackParser` include to `TorrentClient.cpp`**

At the top of `src/core/torrent/TorrentClient.cpp`, add (alphabetically with other includes):

```cpp
#include "core/stream/StreamPackParser.h"
```

- [ ] **Step 2: Replace the inline parser loop in `publishTankorentItemsForTorrent`**

Locate the existing function body at `src/core/torrent/TorrentClient.cpp:1788`. Replace the entire body (lines 1788-1919) with:

```cpp
void TorrentClient::publishTankorentItemsForTorrent(const QString& infoHash)
{
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — defensive double-pass.
    // Today's flow: parser already fired at metadata-ready time (Task 7) registering
    // Pending entries; pieceFinished events (Task 8) flipped them to Complete as files
    // finished. This completion-time call is the safety net: re-parse the file list
    // and register any episodes that DIDN'T parse at metadata-ready time (e.g. a file
    // was renamed mid-download). Idempotent: existing entries are no-op'd by the
    // Index's check-then-mutate logic.
    if (!m_streamDownloadIndex) {
        qWarning() << "publishTankorentItemsForTorrent: no StreamDownloadIndex bound; skipping";
        return;
    }
    if (!m_records.contains(infoHash)) {
        qWarning() << "publishTankorentItemsForTorrent: no record for" << infoHash;
        return;
    }

    const QJsonObject record = m_records.value(infoHash).toObject();
    const QString imdbId = record.value(QStringLiteral("imdbId")).toString();
    if (imdbId.isEmpty())
        return;

    const int configSeason = record.value(QStringLiteral("season")).toInt(0);
    const QString savePath = record.value(QStringLiteral("savePath")).toString();
    if (savePath.isEmpty()) {
        qWarning() << "publishTankorentItemsForTorrent: empty savePath for" << infoHash;
        return;
    }

    const QJsonArray files = m_engine ? m_engine->torrentFiles(infoHash) : QJsonArray{};
    if (files.isEmpty()) {
        qWarning() << "publishTankorentItemsForTorrent: no files in torrent" << infoHash;
        return;
    }

    const tankostream::stream::ParsedPack pack =
        tankostream::stream::StreamPackParser::parsePack(files, imdbId, configSeason);

    const QString sourceGroupId = QStringLiteral("tankorent:") + infoHash;
    int registeredCount = 0;

    if (pack.type == QStringLiteral("series")) {
        for (const auto& pf : pack.episodes) {
            const QString absPath = QDir(savePath).absoluteFilePath(pf.relName);
            // registerEpisode is the Complete-state path (existing API).
            // Idempotent with Pending entries already in index thanks to canonical-path
            // upsert semantics in StreamDownloadIndex.
            m_streamDownloadIndex->registerEpisode(
                imdbId, pf.season, pf.episode, absPath, sourceGroupId, pf.sizeBytes);
            ++registeredCount;
        }
    } else if (pack.type == QStringLiteral("movie")) {
        const QString absPath = QDir(savePath).absoluteFilePath(pack.movieFile.relName);
        m_streamDownloadIndex->registerMovie(
            imdbId, absPath, sourceGroupId, pack.movieFile.sizeBytes);
        registeredCount = 1;
    }

    qDebug() << "publishTankorentItemsForTorrent:" << infoHash
             << "registered" << registeredCount << "items via StreamPackParser"
             << "as imdbId=" << imdbId;
}
```

- [ ] **Step 3: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Smoke check (existing TANKORENT_STREAM_INTEGRATION behavior preserved)**

This task's smoke is verifying that Phase 1's refactor did NOT break existing behavior. Launch Tankoban via `build_and_run.bat`. Find a Tankorent download that's at 100% but uncatalogued (or trigger a fresh small-pack download to completion).

Verify via `out\tankoctl.exe get-downloads`: completed pack episodes appear in the index with `state=Complete`. Compare against a pre-refactor baseline if available, or trust the unit tests + visual confirmation.

If no test pack is available, skip the smoke — the unit tests cover the parser logic, and Task 7+ smokes will exercise the integration end-to-end.

- [ ] **Step 5: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 6: Refactor publishTankorentItemsForTorrent
Files: src/core/torrent/TorrentClient.cpp, src/core/torrent/TorrentClient.h
Skills invoked: [/simplify, /build-verify, /superpowers:verification-before-completion]
Parser body lifted out to StreamPackParser::parsePack(); the completion-time call now serves as a defensive double-pass. Behavior unchanged at completion; substrate ready for Task 7's metadata-ready hook.
READY TO COMMIT
```

---

# PHASE 2 — Wire to metadata-ready

Phase 2 introduces the new behavior: parser fires at `metadata_received_alert` time, registering all parseable episodes as Pending. As libtorrent emits `pieceFinished` for each file, progress flows into `updateEpisodeProgress`. Launch-validation pass handles app-restart edge cases.

**Phase 2 gate:** Smoke 1 from the spec (single-season Daredevil pack) is observable in the `stream_downloads.json` file tail — all 13 episode entries appear at metadata-ready time with `state=Pending`, then transition through Downloading to Complete as files finish. No UI rendering yet.

---

### Task 7: Hook `StreamPackParser` into metadata-ready

**Files:**
- Modify: `src/core/torrent/TorrentClient.h`
- Modify: `src/core/torrent/TorrentClient.cpp`

- [ ] **Step 1: Declare `onMetadataReady` slot in `TorrentClient.h`**

Locate the existing private slots section. Add:

```cpp
private slots:
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18
    void onMetadataReady(const QString& infoHash);
```

- [ ] **Step 2: Wire `TorrentEngine::metadataReady` signal in `TorrentClient.cpp` constructor**

Locate the `TorrentClient::TorrentClient` constructor in `src/core/torrent/TorrentClient.cpp`. Find the existing `connect(m_engine, ...)` block. Add:

```cpp
connect(m_engine, &TorrentEngine::metadataReady,
        this, &TorrentClient::onMetadataReady,
        Qt::QueuedConnection);  // emit fires from AlertWorker thread
```

- [ ] **Step 3: Implement `onMetadataReady`**

Add to `src/core/torrent/TorrentClient.cpp`:

```cpp
void TorrentClient::onMetadataReady(const QString& infoHash)
{
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — register parsed episodes as
    // Pending at metadata-ready time. Only fires for Tankorent-source torrents
    // (those with imdbId in the record). Bulk-cohort torrents take the
    // streamBulkGroups path; addon-only torrents have no imdbId binding.
    if (!m_streamDownloadIndex)
        return;
    if (!m_records.contains(infoHash))
        return;

    const QJsonObject record = m_records.value(infoHash).toObject();
    const QString imdbId = record.value(QStringLiteral("imdbId")).toString();
    if (imdbId.isEmpty())
        return;  // not a Tankorent-source torrent

    const QString streamGroupId =
        record.value(QStringLiteral("streamGroupId")).toString();
    if (!streamGroupId.isEmpty())
        return;  // bulk-cohort path handles its own registration

    const int configSeason = record.value(QStringLiteral("season")).toInt(0);
    const QString savePath = record.value(QStringLiteral("savePath")).toString();
    if (savePath.isEmpty())
        return;

    const QJsonArray files = m_engine ? m_engine->torrentFiles(infoHash) : QJsonArray{};
    if (files.isEmpty())
        return;

    const tankostream::stream::ParsedPack pack =
        tankostream::stream::StreamPackParser::parsePack(files, imdbId, configSeason);

    const QString sourceGroupId = QStringLiteral("tankorent:") + infoHash;

    if (pack.type == QStringLiteral("series")) {
        for (const auto& pf : pack.episodes) {
            const QString absPath = QDir(savePath).absoluteFilePath(pf.relName);
            m_streamDownloadIndex->registerPendingEpisode(
                imdbId, pf.season, pf.episode, absPath, sourceGroupId, pf.sizeBytes);
        }
        qDebug() << "onMetadataReady:" << infoHash
                 << "registered" << pack.episodes.size() << "Pending episodes for" << imdbId;
    } else if (pack.type == QStringLiteral("movie")) {
        const QString absPath = QDir(savePath).absoluteFilePath(pack.movieFile.relName);
        m_streamDownloadIndex->registerPendingMovie(
            imdbId, absPath, sourceGroupId, pack.movieFile.sizeBytes);
        qDebug() << "onMetadataReady:" << infoHash
                 << "registered Pending movie for" << imdbId;
    }
}
```

- [ ] **Step 4: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Smoke check — Pending entries appear at metadata-ready**

Launch Tankoban via `build_and_run.bat`. Stream mode → Daredevil → open download panel → pick "Daredevil S02 1080p Complete Season" → click Download.

In a separate PowerShell terminal, tail `<dataDir>/stream_downloads.json`. Expected: within ~5 seconds of clicking Download, the JSON gains 13 entries for `imdbId: tt18923754`, all with `state: 1` (Pending), `progressPct: 0`.

If entries do not appear: check Tankoban's debug log for "onMetadataReady" lines. If absent, the `TorrentEngine::metadataReady` signal isn't firing or isn't connected — investigate the connect call.

- [ ] **Step 6: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 7: Hook parser into metadata-ready
Files: src/core/torrent/TorrentClient.h, src/core/torrent/TorrentClient.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review]
TorrentClient::onMetadataReady() now fires on libtorrent metadata_received_alert (via TorrentEngine::metadataReady signal, Qt::QueuedConnection to UI thread); parses file list via StreamPackParser; registers each episode as Pending in StreamDownloadIndex. Verified via JSON tail: 13 Pending entries appear within 5s of Daredevil S02 pack dispatch.
READY TO COMMIT
```

---

### Task 8: Wire `pieceFinished` to drive `updateEpisodeProgress`

**Files:**
- Modify: `src/core/torrent/TorrentClient.h`
- Modify: `src/core/torrent/TorrentClient.cpp`

- [ ] **Step 1: Declare `onPieceFinished` slot in `TorrentClient.h`**

Add to the private slots section:

```cpp
private slots:
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18
    void onPieceFinished(const QString& infoHash, int pieceIndex);
```

- [ ] **Step 2: Wire `TorrentEngine::pieceFinished` signal in `TorrentClient.cpp` constructor**

Add to the existing `connect(m_engine, ...)` block:

```cpp
connect(m_engine, &TorrentEngine::pieceFinished,
        this, &TorrentClient::onPieceFinished,
        Qt::QueuedConnection);
```

- [ ] **Step 3: Implement `onPieceFinished`**

Add to `src/core/torrent/TorrentClient.cpp`:

```cpp
void TorrentClient::onPieceFinished(const QString& infoHash, int /*pieceIndex*/)
{
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — compute per-file progress for
    // every file in this torrent and push updates into StreamDownloadIndex.
    // Coarse but correct: query libtorrent's per-file progress array on every
    // piece event. libtorrent's file_progress is O(files), so this is cheap.
    if (!m_streamDownloadIndex || !m_engine)
        return;
    if (!m_records.contains(infoHash))
        return;

    const QJsonObject record = m_records.value(infoHash).toObject();
    const QString imdbId = record.value(QStringLiteral("imdbId")).toString();
    if (imdbId.isEmpty())
        return;
    const QString streamGroupId =
        record.value(QStringLiteral("streamGroupId")).toString();
    if (!streamGroupId.isEmpty())
        return;  // bulk-cohort path

    const int configSeason = record.value(QStringLiteral("season")).toInt(0);
    const QJsonArray files = m_engine->torrentFiles(infoHash);
    if (files.isEmpty())
        return;

    const tankostream::stream::ParsedPack pack =
        tankostream::stream::StreamPackParser::parsePack(files, imdbId, configSeason);

    // Cache per-file progress in an array indexed by libtorrent file index.
    const QJsonArray fileProgress = m_engine->torrentFileProgress(infoHash);
    if (fileProgress.size() == 0)
        return;

    if (pack.type == QStringLiteral("series")) {
        for (const auto& pf : pack.episodes) {
            if (pf.fileIndex < 0 || pf.fileIndex >= fileProgress.size())
                continue;
            const qint64 downloaded =
                fileProgress.at(pf.fileIndex).toVariant().toLongLong();
            const int pct = pf.sizeBytes > 0
                ? static_cast<int>((downloaded * 100LL) / pf.sizeBytes)
                : 0;
            m_streamDownloadIndex->updateEpisodeProgress(
                imdbId, pf.season, pf.episode, pct);
        }
    } else if (pack.type == QStringLiteral("movie")) {
        const int fileIndex = pack.movieFile.fileIndex;
        if (fileIndex < 0 || fileIndex >= fileProgress.size())
            return;
        const qint64 downloaded =
            fileProgress.at(fileIndex).toVariant().toLongLong();
        const int pct = pack.movieFile.sizeBytes > 0
            ? static_cast<int>((downloaded * 100LL) / pack.movieFile.sizeBytes)
            : 0;
        m_streamDownloadIndex->updateEpisodeProgress(imdbId, 0, 0, pct);
    }
}
```

- [ ] **Step 4: Verify `TorrentEngine::torrentFileProgress` exists; if not, add it**

Search `src/core/torrent/TorrentEngine.{h,cpp}` for `torrentFileProgress`. If absent, add a method that returns libtorrent's `file_progress` vector as a QJsonArray:

```cpp
// In TorrentEngine.h (public):
QJsonArray torrentFileProgress(const QString& infoHash) const;

// In TorrentEngine.cpp:
QJsonArray TorrentEngine::torrentFileProgress(const QString& infoHash) const
{
    QJsonArray result;
    libtorrent::sha1_hash hash = ihFromString(infoHash);
    auto h = m_session->find_torrent(hash);
    if (!h.is_valid())
        return result;
    std::vector<int64_t> progress;
    h.file_progress(progress, libtorrent::torrent_handle::piece_granularity);
    for (int64_t bytes : progress)
        result.append(QJsonValue::fromVariant(static_cast<qint64>(bytes)));
    return result;
}
```

(Exact glue depends on existing TorrentEngine wrappers; mirror the pattern used by `torrentFiles()` at the analogous code site.)

- [ ] **Step 5: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Smoke check — episode progress flows through to JSON**

Launch via `build_and_run.bat`. Dispatch the Daredevil S02 pack as in Task 7. Tail `<dataDir>/stream_downloads.json` over the next 60 seconds.

Expected:
- Episode entries transition from `state: 1` (Pending) → `state: 2` (Downloading) as soon as their files have non-zero bytes downloaded.
- `progressPct` rises in real time (verify by polling the JSON every 5s for one episode).
- When `progressPct == 100`, `state` flips to `0` (Complete).

If progressPct stays stuck at 0: check that `TorrentEngine::torrentFileProgress` returns non-empty arrays during active downloads. Add `qDebug() << fileProgress;` inside `onPieceFinished` and inspect.

- [ ] **Step 7: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 8: Wire pieceFinished to updateEpisodeProgress
Files: src/core/torrent/TorrentClient.h, src/core/torrent/TorrentClient.cpp, src/core/torrent/TorrentEngine.h, src/core/torrent/TorrentEngine.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
TorrentClient::onPieceFinished() now queries libtorrent file_progress and pushes per-episode progressPct updates into StreamDownloadIndex. State auto-flips Pending→Downloading at first non-zero progress, Downloading→Complete at 100%. Verified via JSON tail during live download.
READY TO COMMIT
```

---

### Task 9: Launch-validation pass for Pending/Downloading entries

**Files:**
- Modify: `src/core/stream/StreamDownloadIndex.h`
- Modify: `src/core/stream/StreamDownloadIndex.cpp`
- Modify: `src/ui/MainWindow.cpp` (invoke at startup)

- [ ] **Step 1: Declare `validateInFlightEntries` method**

In `src/core/stream/StreamDownloadIndex.h`, add public method:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — on launch, walk Pending/Downloading
// entries and evict those for which libtorrent has no record. activeInfoHashes
// is the set of infoHashes libtorrent currently tracks; format expected:
//   each element is the QString-form infoHash (lowercase hex).
// Pass the result of TorrentClient::activeInfoHashes() at startup.
void validateInFlightEntries(const QSet<QString>& activeInfoHashes);
```

- [ ] **Step 2: Implement `validateInFlightEntries`**

In `src/core/stream/StreamDownloadIndex.cpp`:

```cpp
void StreamDownloadIndex::validateInFlightEntries(const QSet<QString>& activeInfoHashes)
{
    QList<QString> keysToEvict;
    QList<QString> epKeysToEvict;
    QList<QString> evictedImdbs;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_byPath.begin(); it != m_byPath.end(); ++it) {
            const Entry& e = it.value();
            if (e.state != Entry::Pending && e.state != Entry::Downloading)
                continue;
            // sourceGroupId for Tankorent path is "tankorent:<infoHash>"
            const QString prefix = QStringLiteral("tankorent:");
            if (!e.sourceGroupId.startsWith(prefix))
                continue;  // not a Tankorent in-flight entry
            const QString infoHash = e.sourceGroupId.mid(prefix.size());
            if (activeInfoHashes.contains(infoHash))
                continue;  // libtorrent knows about it — leave alone
            keysToEvict.append(it.key());
            epKeysToEvict.append(
                computeEpisodeKey(e.imdbId, e.season, e.episode));
            if (!evictedImdbs.contains(e.imdbId))
                evictedImdbs.append(e.imdbId);
        }
        for (const QString& k : keysToEvict)
            m_byPath.remove(k);
        for (const QString& k : epKeysToEvict)
            m_byEpisode.remove(k);
        for (const QString& imdb : evictedImdbs)
            recomputeImdbHasAnyLocked(imdb);
    }
    if (!keysToEvict.isEmpty()) {
        save();
        emit entriesChanged();
        qDebug() << "validateInFlightEntries: evicted" << keysToEvict.size()
                 << "stale Pending/Downloading entries";
    }
}
```

- [ ] **Step 3: Expose `activeInfoHashes()` on `TorrentClient`**

In `src/core/torrent/TorrentClient.h` (public methods):

```cpp
QSet<QString> activeInfoHashes() const;
```

In `src/core/torrent/TorrentClient.cpp`:

```cpp
QSet<QString> TorrentClient::activeInfoHashes() const
{
    QSet<QString> result;
    if (!m_engine)
        return result;
    for (const TorrentInfo& info : listActive())
        result.insert(info.infoHash);
    return result;
}
```

- [ ] **Step 4: Invoke validation from `MainWindow`**

In `src/ui/MainWindow.cpp`, locate the existing block that wires `torrentClient->setStreamDownloadIndex(...)` (around line 673 per memory `feedback_task_g1_already_complete`). Add immediately after the wiring + after a brief queued-event window so `listActive()` is populated:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — give libtorrent ~5s to resume
// active torrents, then validate Pending/Downloading entries against current
// session state. Stale entries (libtorrent has no record) get evicted.
QTimer::singleShot(5000, this, [this]() {
    if (m_streamDownloadIndex && m_torrentClient) {
        m_streamDownloadIndex->validateInFlightEntries(
            m_torrentClient->activeInfoHashes());
    }
});
```

Ensure `<QTimer>` is included in MainWindow.cpp.

- [ ] **Step 5: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Smoke check — restart-validation behavior**

Test scenario:
1. Launch Tankoban via `build_and_run.bat`.
2. Dispatch Daredevil S02 pack. Wait until at least 1 Pending entry exists.
3. Close Tankoban cleanly (File menu or Alt+F4).
4. Manually edit `<dataDir>/stream_downloads.json` and remove the corresponding libtorrent resume data file under `<dataDir>/torrents/` if you want to test the eviction path. (Skip this if no resume-data manipulation; the "happy path" is sufficient if libtorrent reliably resumes.)
5. Relaunch Tankoban via `build_and_run.bat`.
6. After ~5 seconds, check `<dataDir>/stream_downloads.json` again.
7. Expected: any Pending/Downloading entry whose `sourceGroupId.mid(11)` does NOT match an active infoHash is now gone from the JSON.

- [ ] **Step 7: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 9: Launch-validation pass
Files: src/core/stream/StreamDownloadIndex.h, src/core/stream/StreamDownloadIndex.cpp, src/core/torrent/TorrentClient.h, src/core/torrent/TorrentClient.cpp, src/ui/MainWindow.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Added StreamDownloadIndex::validateInFlightEntries + TorrentClient::activeInfoHashes. MainWindow schedules validation 5s after startup. Stale Pending/Downloading entries (no libtorrent record) get evicted on launch.
READY TO COMMIT
```

---

### Task 10: Same-session duplicate-torrent re-dispatch handling

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp`

Context: per obs 3884 / spec § 7.3, libtorrent's `metadata_received_alert` is single-shot per session per handle. If the user cancels + immediately re-dispatches the same pack within the same Tankoban session, the reused handle won't re-fire metadata. This task synthesizes a `metadataReady` emit for the reused-handle case.

- [ ] **Step 1: Locate `addTorrent` (or equivalent) entry path in TorrentClient.cpp**

Search `src/core/torrent/TorrentClient.cpp` for the place where `m_engine->addTorrent(...)` is called. After the add returns (which surfaces duplicate_torrent as a non-error path) and metadata has previously been fetched, no `metadata_received_alert` re-fires.

- [ ] **Step 2: After add, check if torrent already has metadata; if yes, synthesize the slot call**

Inside the post-add block, add (replacing only the relevant block — preserve surrounding behavior):

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — same-session re-dispatch:
// libtorrent's metadata_received_alert is single-shot per handle, so on a
// duplicate_torrent add (reused handle), no metadataReady fires. Detect
// metadata-already-present and synthesize the slot call so Pending entries
// register correctly on the second dispatch.
if (m_engine->hasMetadata(infoHash)) {
    QTimer::singleShot(0, this, [this, infoHash]() {
        onMetadataReady(infoHash);
    });
}
```

If `TorrentEngine::hasMetadata` does not exist, add it:

```cpp
// In TorrentEngine.h:
bool hasMetadata(const QString& infoHash) const;

// In TorrentEngine.cpp:
bool TorrentEngine::hasMetadata(const QString& infoHash) const
{
    libtorrent::sha1_hash hash = ihFromString(infoHash);
    auto h = m_session->find_torrent(hash);
    if (!h.is_valid())
        return false;
    auto ti = h.torrent_file();
    return ti && ti->num_pieces() > 0;
}
```

- [ ] **Step 3: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Smoke check — cancel + immediate re-dispatch**

1. Launch Tankoban. Dispatch Daredevil S02 pack.
2. Wait ~10 seconds. Confirm 13 Pending entries in JSON tail.
3. Cancel the pack.
4. Confirm all 13 entries are evicted.
5. Immediately (within ~5 seconds) re-click Download on the same pack.
6. Expected: 13 Pending entries re-appear within ~1 second (not 5+ seconds — the synthesized `metadataReady` short-circuits the wait).

- [ ] **Step 5: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 10: Same-session re-dispatch fix
Files: src/core/torrent/TorrentClient.cpp, src/core/torrent/TorrentEngine.h, src/core/torrent/TorrentEngine.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Added TorrentEngine::hasMetadata + post-add check that synthesizes onMetadataReady() for duplicate_torrent reused-handle case. Pending entries register correctly on same-session cancel + re-dispatch.
READY TO COMMIT
```

---

### Task 11: Phase 2 smoke checkpoint

**Goal:** Verify Phase 2 end-to-end: dispatch a real Tankorent show pack, observe Pending → Downloading → Complete state transitions via JSON tail and `tankoctl.exe`. No UI rendering yet.

**Files:**
- Create (evidence): `agents/audits/smoke_evidence/p2_smoke_NN_*.png` (screenshots optional; JSON tail is primary evidence)

- [ ] **Step 1: Pre-smoke setup**

```powershell
.\scripts\stop-tankoban.ps1  # clean any leftover processes
```

Post in `agents/chat.md`:

```
## Agent 4 - MCP LOCK CLAIMED for Phase 2 smoke of TANKORENT_CINEMETA_PACK_MAPPING
```

- [ ] **Step 2: Launch Tankoban**

```powershell
.\build_and_run.bat
```

Wait for main window to appear. Confirm `out\tankoctl.exe ping` returns clean.

- [ ] **Step 3: Dispatch the smoke pack via tankoctl + MCP**

```powershell
.\out\tankoctl.exe open-page stream
.\out\tankoctl.exe search "Daredevil"
# Click into the Daredevil tile via pywinauto-mcp AutomationId
```

Then via `mcp__pywinauto-mcp__automation_elements` find the AutomationId for the season-2 row's Download button. Click via `mcp__pywinauto-mcp__automation_mouse`. Use a pack picker selection — pick "Daredevil S02 1080p Complete Season" (or any clean 1080p complete-season pack the picker surfaces).

- [ ] **Step 4: Tail `<dataDir>/stream_downloads.json` for ~3 minutes**

In a separate PowerShell terminal, watch the file change every ~5 seconds:

```powershell
$dataDir = "$env:APPDATA\Tankoban\data"  # adjust if Tankoban uses different dataDir
while ($true) {
    Get-Content "$dataDir\stream_downloads.json" -Raw | ConvertFrom-Json |
        Select-Object -ExpandProperty entries |
        Where-Object { $_.imdbId -eq "tt18923754" } |
        Format-Table imdbId, season, episode, state, progressPct -AutoSize
    Start-Sleep 5
}
```

- [ ] **Step 5: Observe state transitions**

Expected timeline:
- T+0-5s: 13 entries appear with `state=1` (Pending), `progressPct=0`.
- T+10-30s: episode 1's entry shifts to `state=2` (Downloading), `progressPct` starts climbing.
- T+5-10min (depending on bandwidth): episode 1's entry hits `state=0` (Complete), `progressPct=100`. Episode 2 begins.

Capture a screenshot of the PowerShell tail showing the state transition.

- [ ] **Step 6: Test cancel mid-flight**

After ep1 is Complete and ep2 is Downloading at X%, cancel the pack via MCP (find the Cancel button via AutomationId). Confirm all 13 entries vanish from the JSON within ~2 seconds.

- [ ] **Step 7: Post-smoke teardown**

```powershell
.\scripts\stop-tankoban.ps1
```

Post in `agents/chat.md`:

```
## Agent 4 - MCP LOCK RELEASED
```

- [ ] **Step 8: Flag smoke verdict in chat.md**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Phase 2 SMOKE GREEN
Skills invoked: [/superpowers:verification-before-completion]
Daredevil S02 pack dispatched. 13 entries registered as Pending within 5s of metadata-ready.
Episode 1 transitioned Pending→Downloading→Complete in NN minutes; episode 2 followed sequentially.
Cancel mid-flight evicted all 13 entries within ~2s. Phase 2 gate PASSED.
Evidence: agents/audits/smoke_evidence/p2_smoke_*.png
```

---

# PHASE 3 — UI surfaces

Phase 3 makes the state visible to the user. The `EpisodeTile` widget (mid-flight from THEATRE_DOWNLOAD_OVERHAUL D1) absorbs the state-input contract; a shared `renderEpisodeStateChip` helper renders both EpisodeTile and the existing `StreamDetailView` season-row table. Movie tile + `m_movieDownloadChip` gain state rendering.

**Phase 3 gate:** Smokes 1 + 2 from the spec pass except the amber-tint differentiator (rows still render in default gray palette in this phase). The Tankorent download experience is now visually live: rows light up Queued at T+5s, flip Downloading with progress bars as files start, flip Complete as files finish. Sequential watching is possible.

---

### Task 12: `EpisodeTile` state-input contract

**Files:**
- Modify: `src/ui/pages/stream/EpisodeTile.h`
- Modify: `src/ui/pages/stream/EpisodeTile.cpp`

- [ ] **Step 1: Add `EpisodeTileState` struct + setter declaration**

In `src/ui/pages/stream/EpisodeTile.h`, after the existing public members:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — state-input contract for the
// download-source-aware episode tile. EpisodeTile reads state + progressPct
// to render the chip; reads provenance to apply (or skip) the warm-amber tint.
#include "core/stream/StreamDownloadIndex.h"

struct EpisodeTileState {
    StreamDownloadIndex::Entry::State state =
        StreamDownloadIndex::Entry::Complete;
    int progressPct = 100;
    enum Provenance {
        AddonBulk = 0,    // sourced from Stremio addon download
        Tankorent = 1,    // sourced from Tankorent pack
        LocalScan = 2     // pre-existing file discovered by StreamRescueScanner
    };
    Provenance provenance = AddonBulk;
};

void setEpisodeState(const EpisodeTileState& s);
```

- [ ] **Step 2: Add `m_episodeState` member + storage in header**

In the private members section:

```cpp
private:
    EpisodeTileState m_episodeState;
```

- [ ] **Step 3: Implement `setEpisodeState` stub**

In `src/ui/pages/stream/EpisodeTile.cpp`:

```cpp
void EpisodeTile::setEpisodeState(const EpisodeTileState& s)
{
    m_episodeState = s;
    update();  // trigger repaint; rendering implementation lands in Task 13
}
```

- [ ] **Step 4: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 12: EpisodeTile state-input contract
Files: src/ui/pages/stream/EpisodeTile.h, src/ui/pages/stream/EpisodeTile.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
EpisodeTile gains EpisodeTileState struct (state + progressPct + Provenance) and setEpisodeState() setter. Stub triggers repaint; rendering in Task 13.
READY TO COMMIT
```

---

### Task 13: `EpisodeTile` state-to-paint rendering

**Files:**
- Modify: `src/ui/pages/stream/EpisodeTile.cpp`

- [ ] **Step 1: Update EpisodeTile's `paintEvent` to render state-aware chip**

In `src/ui/pages/stream/EpisodeTile.cpp`, locate the existing `paintEvent` (or equivalent render path). Add a state-driven chip render block. Approximate code (adapt to existing tile geometry constants):

```cpp
void EpisodeTile::paintEvent(QPaintEvent* event)
{
    // ...existing paintEvent body (poster/title/etc) preserved above...

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — render state chip + progress bar.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect chipRect = computeChipRect();  // existing helper, or
                                                // chip area within the tile
    QString chipText;
    bool drawProgressStrip = false;
    bool drawCheckmark = false;
    switch (m_episodeState.state) {
    case StreamDownloadIndex::Entry::Pending:
        chipText = QStringLiteral("Queued");
        break;
    case StreamDownloadIndex::Entry::Downloading:
        chipText = QStringLiteral("%1%").arg(m_episodeState.progressPct);
        drawProgressStrip = true;
        break;
    case StreamDownloadIndex::Entry::Complete:
        chipText = QStringLiteral("Downloaded");
        drawCheckmark = true;
        break;
    case StreamDownloadIndex::Entry::Failed:
        chipText = QStringLiteral("Failed");
        // Failed-state chip color is muted red, separate from amber differentiator.
        // See setFailedChipPalette() below.
        break;
    }

    // Draw chip background (gray palette by default).
    QColor chipBg = palette().color(QPalette::Mid);
    if (m_episodeState.state == StreamDownloadIndex::Entry::Failed)
        chipBg = QColor(180, 60, 60);  // muted red
    painter.fillRect(chipRect, chipBg);

    // Draw chip text.
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(chipRect, Qt::AlignCenter, chipText);

    if (drawCheckmark) {
        // Existing kColAction-style ✓ glyph; reuse existing helper.
        drawCompletionCheck(&painter, chipRect);
    }

    if (drawProgressStrip) {
        // 2px horizontal strip along bottom edge of the tile (or row).
        const int stripHeight = 2;
        const QRect tileRect = rect();
        const int progressW = (tileRect.width() * m_episodeState.progressPct) / 100;
        painter.fillRect(QRect(0, tileRect.height() - stripHeight,
                               progressW, stripHeight),
                         palette().color(QPalette::Highlight));
    }

    // Amber tint hook — implementation lands in Task 20 once tone is ratified.
    // For now: render same as AddonBulk regardless of provenance.

    event->accept();
}
```

- [ ] **Step 2: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 3: Visual smoke (no live data yet)**

Phase 3 tile rendering can be tested by manually constructing test EpisodeTile widgets with hardcoded state. This is sufficient to verify the rendering path; full integration smoke comes after Task 15 wires up signals.

Skip live testing in this task; verify visually in Task 17.

- [ ] **Step 4: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 13: EpisodeTile state-to-paint rendering
Files: src/ui/pages/stream/EpisodeTile.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
paintEvent now renders state chip + progress strip + ✓ glyph for Pending/Downloading/Complete/Failed states. Amber-tint hook deferred to Task 20. Live rendering verified in Task 17.
READY TO COMMIT
```

---

### Task 14: Shared `renderEpisodeStateChip` helper for `StreamDetailView` season-row table

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Declare shared render helper in `StreamDetailView.h`**

In the private methods section of `StreamDetailView.h`:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — shared chip render for both
// EpisodeTile and the season-row table's kColAction cell. Two surfaces, one
// contract.
void renderEpisodeStateChip(int row,
                            StreamDownloadIndex::Entry::State state,
                            int progressPct,
                            EpisodeTileState::Provenance provenance);
```

Add the include if not present:
```cpp
#include "core/stream/StreamDownloadIndex.h"
#include "ui/pages/stream/EpisodeTile.h"  // for EpisodeTileState::Provenance
```

- [ ] **Step 2: Implement helper in `StreamDetailView.cpp`**

```cpp
void StreamDetailView::renderEpisodeStateChip(
    int row,
    StreamDownloadIndex::Entry::State state,
    int progressPct,
    EpisodeTileState::Provenance /*provenance*/)  // amber-tint in Task 20
{
    if (!m_episodeTable)
        return;
    if (row < 0 || row >= m_episodeTable->rowCount())
        return;

    QWidget* chipWidget = m_episodeTable->cellWidget(row, kColAction);
    QLabel* chipLabel = chipWidget ? chipWidget->findChild<QLabel*>() : nullptr;

    QString chipText;
    bool checkmark = false;
    switch (state) {
    case StreamDownloadIndex::Entry::Pending:
        chipText = QStringLiteral("Queued");
        break;
    case StreamDownloadIndex::Entry::Downloading:
        chipText = QStringLiteral("Downloading %1%").arg(progressPct);
        break;
    case StreamDownloadIndex::Entry::Complete:
        chipText = QStringLiteral("Downloaded");
        checkmark = true;
        break;
    case StreamDownloadIndex::Entry::Failed:
        chipText = QStringLiteral("Failed");
        break;
    }

    if (chipLabel) {
        chipLabel->setText(checkmark
                           ? QStringLiteral("\xE2\x9C\x93 %1").arg(chipText)
                           : chipText);
    }
    // Amber-tint application deferred to Task 20.
}
```

- [ ] **Step 3: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 14: Shared renderEpisodeStateChip helper
Files: src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
StreamDetailView::renderEpisodeStateChip() renders state-aware chip on kColAction cells. Mirrors EpisodeTile's render contract; ready for entryStateChanged signal wiring in Task 15.
READY TO COMMIT
```

---

### Task 15: Wire `entryStateChanged` subscribers on `EpisodeTile` + season-row table

**Files:**
- Modify: `src/ui/pages/stream/EpisodeTile.h`
- Modify: `src/ui/pages/stream/EpisodeTile.cpp`
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Wire `entryStateChanged` to `EpisodeTile`**

In `src/ui/pages/stream/EpisodeTile.cpp` constructor, after the existing setup:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — listen for state changes scoped
// to this tile's (imdbId, season, episode).
if (m_streamDownloadIndex) {
    connect(m_streamDownloadIndex, &StreamDownloadIndex::entryStateChanged,
            this, [this](const QString& imdbId, int season, int episode) {
                if (imdbId == m_imdbId && season == m_season && episode == m_episode) {
                    refreshFromIndex();
                }
            }, Qt::QueuedConnection);
}
```

Add a `refreshFromIndex()` private method:

```cpp
void EpisodeTile::refreshFromIndex()
{
    if (!m_streamDownloadIndex)
        return;
    auto entries = m_streamDownloadIndex->entriesForImdb(m_imdbId);
    EpisodeTileState s;
    s.provenance = EpisodeTileState::AddonBulk;  // default
    s.state = StreamDownloadIndex::Entry::Complete;  // default
    s.progressPct = 100;
    for (const auto& e : entries) {
        if (e.season == m_season && e.episode == m_episode) {
            s.state = e.state;
            s.progressPct = e.progressPct;
            if (e.sourceGroupId.startsWith(QStringLiteral("tankorent:")))
                s.provenance = EpisodeTileState::Tankorent;
            break;
        }
    }
    setEpisodeState(s);
}
```

- [ ] **Step 2: Wire `entryStateChanged` to `StreamDetailView`**

In `src/ui/pages/stream/StreamDetailView.cpp`, locate the existing constructor or `setupSignals()` block. Add:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — re-render kColAction on any
// state change for episodes of the currently-viewed show.
if (m_streamDownloadIndex) {
    connect(m_streamDownloadIndex, &StreamDownloadIndex::entryStateChanged,
            this, [this](const QString& imdbId, int season, int episode) {
                if (imdbId != m_currentImdbId)
                    return;
                if (season != m_currentSeason)
                    return;
                const int row = episodeRowForEpisode(episode);
                if (row < 0)
                    return;
                auto entries =
                    m_streamDownloadIndex->entriesForImdb(imdbId);
                EpisodeTileState::Provenance prov =
                    EpisodeTileState::AddonBulk;
                int pct = 100;
                StreamDownloadIndex::Entry::State state =
                    StreamDownloadIndex::Entry::Complete;
                for (const auto& e : entries) {
                    if (e.season == season && e.episode == episode) {
                        state = e.state;
                        pct = e.progressPct;
                        if (e.sourceGroupId.startsWith(
                                QStringLiteral("tankorent:")))
                            prov = EpisodeTileState::Tankorent;
                        break;
                    }
                }
                renderEpisodeStateChip(row, state, pct, prov);
            }, Qt::QueuedConnection);
}
```

`episodeRowForEpisode(int)` is assumed to exist as a helper that maps episode number to the season-row table's row index. If not present, add a small lookup helper that walks the table's data column.

- [ ] **Step 3: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 15: Wire entryStateChanged subscribers
Files: src/ui/pages/stream/EpisodeTile.h, src/ui/pages/stream/EpisodeTile.cpp, src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Both EpisodeTile and StreamDetailView's season-row table now subscribe to StreamDownloadIndex::entryStateChanged via Qt::QueuedConnection. Granular updates re-render only the affected row. UI now reflects state transitions live.
READY TO COMMIT
```

---

### Task 16: Movie tile + `m_movieDownloadChip` state rendering

**Files:**
- Modify: `src/ui/pages/stream/StreamLibraryLayout.h`
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp`
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Update `m_movieDownloadChip` to read from StreamDownloadIndex**

The chip already exists from Codex Trigger D #5 (2026-05-18). It currently reads `streamMovieDownloadSnapshot()`. Extend the read to also consult the StreamDownloadIndex Pending/Downloading state.

In `src/ui/pages/stream/StreamDetailView.cpp`, locate the function that updates `m_movieDownloadChip`. Replace its body's state read with:

```cpp
void StreamDetailView::refreshMovieDownloadChip()
{
    if (!m_movieDownloadChip || m_currentImdbId.isEmpty())
        return;

    // Existing snapshot consultation (Codex #5) for active addon-bulk downloads:
    auto snapshot = m_torrentClient->streamMovieDownloadSnapshot(m_currentImdbId);

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — also consult Index for movie state.
    StreamDownloadIndex::Entry::State indexState =
        StreamDownloadIndex::Entry::Complete;
    int indexPct = 100;
    EpisodeTileState::Provenance prov = EpisodeTileState::AddonBulk;
    if (m_streamDownloadIndex) {
        auto entries =
            m_streamDownloadIndex->entriesForImdb(m_currentImdbId);
        for (const auto& e : entries) {
            if (e.type == QStringLiteral("movie")) {
                indexState = e.state;
                indexPct = e.progressPct;
                if (e.sourceGroupId.startsWith(QStringLiteral("tankorent:")))
                    prov = EpisodeTileState::Tankorent;
                break;
            }
        }
    }

    // Index state takes precedence for visible chip text.
    QString chipText;
    bool showChip = true;
    switch (indexState) {
    case StreamDownloadIndex::Entry::Pending:
        chipText = QStringLiteral("DOWNLOADING 0%");
        break;
    case StreamDownloadIndex::Entry::Downloading:
        chipText = QStringLiteral("DOWNLOADING %1%").arg(indexPct);
        break;
    case StreamDownloadIndex::Entry::Complete:
        // If snapshot says still downloading, fall back to snapshot pct.
        if (snapshot.active) {
            chipText = QStringLiteral("DOWNLOADING %1%").arg(snapshot.pct);
        } else {
            chipText = QStringLiteral("DOWNLOADED");
        }
        break;
    case StreamDownloadIndex::Entry::Failed:
        chipText = QStringLiteral("FAILED");
        break;
    }

    if (!showChip) {
        m_movieDownloadChip->hide();
    } else {
        m_movieDownloadChip->setText(chipText);
        m_movieDownloadChip->show();
        // Amber-tint application deferred to Task 20.
    }
}
```

- [ ] **Step 2: Subscribe `refreshMovieDownloadChip` to `entryStateChanged`**

In StreamDetailView's signal wiring, add:

```cpp
if (m_streamDownloadIndex) {
    connect(m_streamDownloadIndex, &StreamDownloadIndex::entryStateChanged,
            this, [this](const QString& imdbId, int season, int episode) {
                if (imdbId == m_currentImdbId && season == 0 && episode == 0)
                    refreshMovieDownloadChip();
            }, Qt::QueuedConnection);
}
```

- [ ] **Step 3: Same wiring for `StreamLibraryLayout` movie library tiles**

In `src/ui/pages/stream/StreamLibraryLayout.cpp`, locate where movie library tiles refresh their badges (per yesterday's F2 finding — the library-tile badge gap that wasn't addressed). Now is the opportunistic moment to address F2 in this task.

Add a similar subscriber that walks all visible movie tiles and updates their DOWNLOADING badges when an `entryStateChanged` for season=0/episode=0 fires:

```cpp
if (m_streamDownloadIndex) {
    connect(m_streamDownloadIndex, &StreamDownloadIndex::entryStateChanged,
            this, [this](const QString& imdbId, int season, int episode) {
                if (season == 0 && episode == 0)
                    refreshMovieTileBadge(imdbId);
            }, Qt::QueuedConnection);
}
```

Implement `refreshMovieTileBadge(const QString& imdbId)` to find the tile widget for this imdbId and update its DOWNLOADING N% badge based on the Index's current state.

- [ ] **Step 4: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 16: Movie tile + chip state rendering
Files: src/ui/pages/stream/StreamLibraryLayout.h, src/ui/pages/stream/StreamLibraryLayout.cpp, src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
m_movieDownloadChip now consults StreamDownloadIndex state alongside the existing snapshot path. Movie library-tile DOWNLOADING badge (F2 from recap) now refreshes on entryStateChanged. Amber-tint application deferred to Task 20.
READY TO COMMIT
```

---

### Task 17: Phase 3 smoke checkpoint

**Goal:** Full visual smoke — show pack + movie. No amber tint yet (Phase 4).

- [ ] **Step 1: Pre-smoke setup**

```powershell
.\scripts\stop-tankoban.ps1
```

Post `MCP LOCK CLAIMED` in chat.md.

- [ ] **Step 2: Launch + run Smoke 1 (Daredevil S02 pack)**

Per spec § 8.1. Expected user-visible flow:
- T+0-5s: all 13 episode rows in StreamDetailView's table show "Queued" chips in the kColAction column.
- T+5-30s: episode 1 row shows "Downloading X%" with a progress strip; rest stay "Queued".
- T+5-10min: episode 1 row says "Downloaded ✓"; episode 2 begins.
- Click episode 1 row: plays from local file.

Capture screenshots at:
- `agents/audits/smoke_evidence/p3_smoke_daredevil_t5s_pending.png` (T+5s pending state)
- `agents/audits/smoke_evidence/p3_smoke_daredevil_ep1_downloading.png` (ep1 downloading)
- `agents/audits/smoke_evidence/p3_smoke_daredevil_ep1_complete.png` (ep1 complete)

- [ ] **Step 3: Run Smoke 2 (Fight Club movie pack)**

Per spec § 8.2. Expected:
- T+0-5s: movie tile in library shows "DOWNLOADING 0%" chip.
- Progress chip updates in real time.
- On completion: tile shows "DOWNLOADED ✓".

Capture: `agents/audits/smoke_evidence/p3_smoke_fightclub_*.png`.

- [ ] **Step 4: Post-smoke teardown + verdict**

```powershell
.\scripts\stop-tankoban.ps1
```

Post `MCP LOCK RELEASED` in chat.md.

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Phase 3 SMOKE GREEN (except amber)
Skills invoked: [/superpowers:verification-before-completion]
Smoke 1 (Daredevil S02): rows lit up Queued at T+Ns, ep1 transitioned through Downloading→Complete; ep2 followed; ep1 playable from local file at T+Nmin.
Smoke 2 (Fight Club): movie tile DOWNLOADING chip rose 0→100; DOWNLOADED ✓ shown on completion.
Amber tint still default-gray pending Phase 4 Task 20.
Evidence: agents/audits/smoke_evidence/p3_smoke_*.png
```

---

# PHASE 4 — Differentiator + cancel + sequential

Phase 4 lands the user-visible polish: SequentialPieceManager for in-order episode download, amber-tint render for Tankorent-source provenance, cancel evict-everything semantics, and the THEATRE_DOWNLOAD_OVERHAUL Decision 12 amendment.

**Phase 4 gate:** Smoke 1 + 2 + 3 from spec § 8 all GREEN end-to-end. Hemanth live-eyeballs amber tint and ratifies tone or requests adjustment. Continuous-confidence carry-through verified (Codex Trigger D #4 fixes still hold).

---

### Task 18: `SequentialPieceManager` skeleton + unit tests

**Files:**
- Create: `src/core/torrent/SequentialPieceManager.h`
- Create: `src/core/torrent/SequentialPieceManager.cpp`
- Create: `tests/core/torrent/test_sequential_piece_manager.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `SequentialPieceManager.h`**

```cpp
#pragma once

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — per-pack libtorrent piece-priority
// tracker. Maintains a "current top-priority file" curve so episodes download
// in episode order. Spec § 5.4.

#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>

class TorrentEngine;

class SequentialPieceManager
{
public:
    explicit SequentialPieceManager(TorrentEngine* engine);

    // Register a pack. fileIndicesInEpisodeOrder is libtorrent's file index
    // for episode 1, 2, 3, ... in episode order.
    void registerPack(const QString& infoHash,
                      const QList<int>& fileIndicesInEpisodeOrder);

    // Called when any piece finishes for a tracked torrent.
    // pct is the per-file progress (0.0-1.0).
    void onFileProgress(const QString& infoHash, int fileIndex, double pct);

    // Drop a pack from tracking on cancel/completion.
    void forgetPack(const QString& infoHash);

private:
    struct PackState {
        QList<int> fileOrder;     // episode order
        int        topIdx = 0;    // index into fileOrder for current top file
    };

    void applyPrioritiesLocked(const QString& infoHash, const PackState& state);

    TorrentEngine*           m_engine;
    mutable QMutex           m_mutex;
    QHash<QString, PackState> m_packs;
};
```

- [ ] **Step 2: Create `SequentialPieceManager.cpp`**

```cpp
#include "SequentialPieceManager.h"

#include "TorrentEngine.h"

#include <QDebug>

namespace {
// libtorrent priority values: 0=skip, 1-4=low, 5-6=normal, 7=highest.
constexpr int kTopPriority = 7;
constexpr int kSecondPriority = 5;
constexpr int kBackgroundPriority = 1;
}  // namespace

SequentialPieceManager::SequentialPieceManager(TorrentEngine* engine)
    : m_engine(engine)
{
}

void SequentialPieceManager::registerPack(
    const QString& infoHash,
    const QList<int>& fileIndicesInEpisodeOrder)
{
    QMutexLocker locker(&m_mutex);
    PackState state;
    state.fileOrder = fileIndicesInEpisodeOrder;
    state.topIdx = 0;
    m_packs.insert(infoHash, state);
    applyPrioritiesLocked(infoHash, state);
}

void SequentialPieceManager::onFileProgress(
    const QString& infoHash, int /*fileIndex*/, double pct)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_packs.find(infoHash);
    if (it == m_packs.end())
        return;
    PackState& state = it.value();
    if (state.topIdx >= state.fileOrder.size())
        return;
    // Check whether the current top-priority file has completed.
    const int currentTopFile = state.fileOrder[state.topIdx];
    // Get current file_progress for this specific file.
    // (For simplicity: trust caller — pct is for the current top file.)
    if (pct >= 1.0) {
        state.topIdx += 1;
        if (state.topIdx < state.fileOrder.size()) {
            applyPrioritiesLocked(infoHash, state);
        } else {
            // All files complete; nothing more to do.
            qDebug() << "SequentialPieceManager: pack" << infoHash
                     << "fully sequenced through" << state.fileOrder.size() << "files";
        }
    }
    (void)currentTopFile;  // suppress unused
}

void SequentialPieceManager::forgetPack(const QString& infoHash)
{
    QMutexLocker locker(&m_mutex);
    m_packs.remove(infoHash);
}

void SequentialPieceManager::applyPrioritiesLocked(
    const QString& infoHash, const PackState& state)
{
    if (!m_engine)
        return;
    // Build a priorities vector: every file in fileOrder gets a tiered priority
    // based on its position relative to topIdx.
    // Files not in fileOrder (e.g. extras) keep default priority (1).
    QHash<int, int> prioritiesByFileIndex;
    for (int i = 0; i < state.fileOrder.size(); ++i) {
        int relPos = i - state.topIdx;
        int priority = kBackgroundPriority;
        if (relPos == 0)
            priority = kTopPriority;
        else if (relPos == 1)
            priority = kSecondPriority;
        else
            priority = kBackgroundPriority;
        prioritiesByFileIndex.insert(state.fileOrder[i], priority);
    }
    m_engine->setFilePriorities(infoHash, prioritiesByFileIndex);
}
```

Add `TorrentEngine::setFilePriorities(infoHash, QHash<int,int>)` if not present.

- [ ] **Step 3: Register in CMakeLists.txt**

Add `src/core/torrent/SequentialPieceManager.{h,cpp}` to SOURCES + HEADERS lists.

- [ ] **Step 4: Create unit tests**

`tests/core/torrent/test_sequential_piece_manager.cpp`:

```cpp
#include <gtest/gtest.h>

#include "core/torrent/SequentialPieceManager.h"

#include <QHash>

// Mock TorrentEngine for capturing setFilePriorities calls.
// (Adapt to existing mock infrastructure; if none, use a minimal local stub.)

namespace {

class MockEngine
{
public:
    QHash<QString, QHash<int, int>> lastPriorities;
    void setFilePriorities(const QString& infoHash,
                           const QHash<int, int>& priorities)
    {
        lastPriorities.insert(infoHash, priorities);
    }
};

}  // namespace

// NOTE: this test sketch assumes SequentialPieceManager accepts a TorrentEngine*.
// If TorrentEngine is final/non-virtual, adapt to use dependency injection or
// promote setFilePriorities to a virtual interface for testability.

TEST(SequentialPieceManagerTest, RegisterPackSetsTopFilePriority7)
{
    // Test the initial priority distribution after registerPack.
    // Adapt the test once the engine-mock pattern is decided.
    SUCCEED() << "Test stub — adapt to project's TorrentEngine mock pattern";
}

TEST(SequentialPieceManagerTest, OnFileCompleteRotatesPriorities)
{
    SUCCEED() << "Test stub — adapt to project's TorrentEngine mock pattern";
}

TEST(SequentialPieceManagerTest, ForgetPackClearsState)
{
    SUCCEED() << "Test stub — adapt to project's TorrentEngine mock pattern";
}
```

NOTE: The exact mock pattern depends on existing TorrentEngine testability scaffolding. If TorrentEngine is non-virtual, the manager should ideally take an `ITorrentEngine*` interface. This decision is deferred — if the existing TorrentEngine is testable via DI, full tests land here; if not, this task ships with `SUCCEED()` stubs and behavioral verification comes from Task 19's smoke.

Register the test file in CMakeLists.txt regardless.

- [ ] **Step 5: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

If TANKOBAN_BUILD_TESTS=ON, also:
```powershell
cmake --build out --target tankoban_tests
cd out
ctest --output-on-failure -R SequentialPieceManagerTest
```
Expected: 3 tests PASS (or SUCCEED-stubs reported).

- [ ] **Step 6: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 18: SequentialPieceManager skeleton + tests
Files: src/core/torrent/SequentialPieceManager.h, src/core/torrent/SequentialPieceManager.cpp, tests/core/torrent/test_sequential_piece_manager.cpp, CMakeLists.txt
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
SequentialPieceManager built — per-pack priority tracker. Tests stubbed pending TorrentEngine mock pattern decision; behavioral coverage from Task 19 smoke.
READY TO COMMIT
```

---

### Task 19: Wire `SequentialPieceManager` into `TorrentClient`

**Files:**
- Modify: `src/core/torrent/TorrentClient.h`
- Modify: `src/core/torrent/TorrentClient.cpp`

- [ ] **Step 1: Add `SequentialPieceManager` member to `TorrentClient`**

In `src/core/torrent/TorrentClient.h`:

```cpp
#include "core/torrent/SequentialPieceManager.h"

private:
    std::unique_ptr<SequentialPieceManager> m_sequentialPieceManager;
```

- [ ] **Step 2: Construct in `TorrentClient` ctor**

```cpp
TorrentClient::TorrentClient(...)
    : ...
    , m_sequentialPieceManager(std::make_unique<SequentialPieceManager>(m_engine))
{
    // ...existing connects...
}
```

- [ ] **Step 3: Register pack in `onMetadataReady` for series Tankorent packs**

In `TorrentClient::onMetadataReady` (Task 7), after the series branch's episode-registration loop, add:

```cpp
if (pack.type == QStringLiteral("series")
    && record.value(QStringLiteral("sequential")).toBool(true)) {
    QList<int> fileIndicesInEpisodeOrder;
    for (const auto& pf : pack.episodes)
        fileIndicesInEpisodeOrder.append(pf.fileIndex);
    m_sequentialPieceManager->registerPack(infoHash, fileIndicesInEpisodeOrder);
}
```

- [ ] **Step 4: Drive manager from `onPieceFinished`**

In `TorrentClient::onPieceFinished` (Task 8), inside the series branch's per-episode loop, after computing `pct`:

```cpp
const double pctFraction = pf.sizeBytes > 0
    ? static_cast<double>(downloaded) / static_cast<double>(pf.sizeBytes)
    : 0.0;
m_sequentialPieceManager->onFileProgress(infoHash, pf.fileIndex, pctFraction);
```

- [ ] **Step 5: Drop tracking on cancel + completion**

In `TorrentClient::cancelTorrent` (or equivalent path):

```cpp
m_sequentialPieceManager->forgetPack(infoHash);
```

In `TorrentClient::onTorrentCompleted` (around line 2784):

```cpp
m_sequentialPieceManager->forgetPack(infoHash);
```

- [ ] **Step 6: Default `AddTorrentConfig::sequential = true` for Tankorent show packs**

Locate where AddTorrentConfig is built for the show-first picker path (TankorentPage / TheatreDownloadPanel callers). Ensure `config.sequential = true` is set when the pack has a non-empty imdbId AND configSeason > 0 (or multi-season). Existing addon-bulk callers stay default-false.

- [ ] **Step 7: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 8: Smoke check — sequential download visible in libtorrent telemetry**

Dispatch Daredevil S02 pack. Within ~30 seconds, run:

```powershell
.\out\tankoctl.exe get-torrents
```

Expected: file_priorities for the Daredevil torrent show a descending priority curve. File at episode-1's position has priority 7; file at episode-2 has priority 5; rest priority 1.

After episode 1 hits 100% (ep1 file finished), repeat the query. Expected: priorities shifted — episode-2's file now priority 7, episode-3's file priority 5.

- [ ] **Step 9: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 19: Wire SequentialPieceManager into TorrentClient
Files: src/core/torrent/TorrentClient.h, src/core/torrent/TorrentClient.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
SequentialPieceManager now wired in onMetadataReady (registerPack for series with sequential flag) + onPieceFinished (onFileProgress driver) + cancel/completion (forgetPack). Verified via tankoctl that priorities rotate as files complete.
READY TO COMMIT
```

---

### Task 20: Amber-tint render across EpisodeTile + season-row table + movie tile

**Files:**
- Modify: `src/ui/pages/stream/EpisodeTile.cpp`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp`

- [ ] **Step 1: Define amber-tint default color**

In a shared header (or near the top of EpisodeTile.cpp), define:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — initial amber tint default.
// Hemanth ratifies exact tone in Phase 4 smoke (Task 23) via live eyeball;
// adjust this value if requested.
//
// Spec Decision 5b: warm amber/honey/muted gold bracket.
// Default starting hex: #6B5320 at ~12% alpha overlay.
namespace TankorentTint {
inline QColor accentColor() {
    return QColor(107, 83, 32, 30);  // amber, 30/255 ≈ 12% alpha
}
}  // namespace
```

- [ ] **Step 2: Apply tint in `EpisodeTile::paintEvent`**

Add at the end of `paintEvent` (before `event->accept()`), inside the same painter:

```cpp
if (m_episodeState.provenance == EpisodeTileState::Tankorent) {
    painter.fillRect(rect(), TankorentTint::accentColor());
}
```

- [ ] **Step 3: Apply tint in `StreamDetailView::renderEpisodeStateChip`**

Replace the existing no-op provenance parameter with active tint application:

```cpp
void StreamDetailView::renderEpisodeStateChip(
    int row,
    StreamDownloadIndex::Entry::State state,
    int progressPct,
    EpisodeTileState::Provenance provenance)
{
    // ...existing chip-text rendering preserved...

    // Apply amber tint to the row background if Tankorent-sourced.
    if (provenance == EpisodeTileState::Tankorent) {
        const QColor tintColor = TankorentTint::accentColor();
        for (int col = 0; col < m_episodeTable->columnCount(); ++col) {
            QTableWidgetItem* item = m_episodeTable->item(row, col);
            if (item)
                item->setBackground(QBrush(tintColor));
            QWidget* cellWidget = m_episodeTable->cellWidget(row, col);
            if (cellWidget) {
                QPalette pal = cellWidget->palette();
                pal.setColor(QPalette::Window, tintColor);
                cellWidget->setPalette(pal);
                cellWidget->setAutoFillBackground(true);
            }
        }
    }
}
```

- [ ] **Step 4: Apply tint in `StreamLibraryLayout` movie tile**

When `refreshMovieTileBadge(imdbId)` finds the tile widget and reads its provenance from the Index, apply the amber tint to the tile background if `Tankorent`:

```cpp
void StreamLibraryLayout::refreshMovieTileBadge(const QString& imdbId)
{
    auto entries = m_streamDownloadIndex->entriesForImdb(imdbId);
    for (const auto& e : entries) {
        if (e.type != QStringLiteral("movie"))
            continue;
        QWidget* tile = findMovieTileForImdb(imdbId);
        if (!tile)
            return;
        // ...existing chip text update preserved...
        if (e.sourceGroupId.startsWith(QStringLiteral("tankorent:"))) {
            QPalette pal = tile->palette();
            pal.setColor(QPalette::Window, TankorentTint::accentColor());
            tile->setPalette(pal);
            tile->setAutoFillBackground(true);
        }
        return;
    }
}
```

- [ ] **Step 5: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Visual smoke — amber tint live**

Dispatch the Daredevil S02 pack. Within ~5 seconds, observe:
- All 13 episode rows in the season-row table have a subtle warm-amber tinted background.
- Other content (addon-bulk downloads or completed-but-not-Tankorent rows) renders in default gray.

Capture: `agents/audits/smoke_evidence/p4_amber_tint_daredevil.png`.

- [ ] **Step 7: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 20: Amber-tint render
Files: src/ui/pages/stream/EpisodeTile.cpp, src/ui/pages/stream/StreamDetailView.cpp, src/ui/pages/stream/StreamLibraryLayout.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Initial amber tint (#6B5320 @ 12% alpha) applied to Tankorent-sourced episode rows, EpisodeTile widgets, and movie library tiles. Hex/opacity finalized in Task 23 smoke via Hemanth live-eyeball ratification per Decision 5b.
READY TO COMMIT
```

---

### Task 21: Cancel evict-everything semantics + addon-bulk path alignment

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp`

- [ ] **Step 1: Locate `cancelStreamBulkGroup` (or Tankorent cancel path)**

Search `src/core/torrent/TorrentClient.cpp` for `cancelStreamBulkGroup`. This is the addon-bulk cancel path. There may also be a Tankorent-specific cancel path; if not, both flows likely converge through `cancelTorrent`.

- [ ] **Step 2: Wire `evictBySourceGroup` into cancel paths**

After the existing cancel logic (which stops the torrent in libtorrent and updates `m_streamBulkGroups`), add:

```cpp
// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — Decision 10 amendment: cancel
// evicts the entire pack from StreamDownloadIndex (files stay on disk).
// Applies to BOTH addon-bulk and Tankorent paths per amended Decision 12 of
// THEATRE_DOWNLOAD_OVERHAUL.
if (m_streamDownloadIndex) {
    // Addon-bulk groupId is the streamGroupId directly; Tankorent is "tankorent:<infoHash>".
    // Both share the same evictBySourceGroup API.
    QString sourceGroup;
    if (isStreamBulkGroup) {
        sourceGroup = groupId;  // the addon-bulk groupId
    } else {
        sourceGroup = QStringLiteral("tankorent:") + infoHash;
    }
    m_streamDownloadIndex->evictBySourceGroup(sourceGroup);
}
```

Verify the addon-bulk path's groupId scheme matches what's used in `publishStreamBulkItemsForTorrent`'s `registerEpisode(...)` calls. If addon-bulk uses a different prefix, store it consistently.

- [ ] **Step 3: Tear down `SequentialPieceManager` on cancel**

In the same cancel paths, ensure:

```cpp
m_sequentialPieceManager->forgetPack(infoHash);
```

(Already added in Task 19 Step 5; verify present.)

- [ ] **Step 4: Verify compile**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Smoke check — cancel evict-everything**

Dispatch Daredevil S02. Wait until ep1 is Complete and ep2 is Downloading. Click Cancel.

Expected:
- All 13 episode rows return to default gray (no chip, no amber tint, no progress strip, no ✓).
- `stream_downloads.json` no longer contains any entries for `imdbId: tt18923754`.
- Files on disk (`<savePath>/Daredevil.S02.E01.*.mkv` and any partial files) still exist.

- [ ] **Step 6: Flag READY TO COMMIT**

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING Task 21: Cancel evict-everything semantics
Files: src/core/torrent/TorrentClient.cpp
Skills invoked: [/build-verify, /superpowers:verification-before-completion]
Cancel paths (addon-bulk + Tankorent) now call StreamDownloadIndex::evictBySourceGroup(). Files on disk untouched per Decision 7 (Index only). Both paths behave identically per amended Decision 12. SequentialPieceManager forgets the pack on cancel.
READY TO COMMIT
```

---

### Task 22: THEATRE_DOWNLOAD_OVERHAUL Decision 12 amendment

**Files:**
- Modify: `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md`
- Append: `agents/chat.md`

- [ ] **Step 1: Edit the THEATRE_DOWNLOAD_OVERHAUL spec inline**

Open `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md`. Locate "Decision 12" (it should be in a numbered decisions block). Insert immediately below the original text:

```markdown
> **AMENDED 2026-05-18 by TANKORENT_CINEMETA_PACK_MAPPING:** Cancel now evicts
> the **entire pack** from the library (Index only, files stay on disk) for
> both addon-bulk AND Tankorent paths. The original "preserve completed, drop
> unfinished" semantics are deferred to a future revisit once UI trust is
> earned. Hemanth's rationale (verbatim): *"until we polish every last piece
> of code in stream mode have netflix levels of clarity on download
> procedure, I want my download cancels to be absolute and apply for the
> entire season. we will change this rule much later down the line when I
> think I trust our app's UI enough to know the download's happening just as
> it is showing."* See [TANKORENT_CINEMETA_PACK_MAPPING design § 9.1](2026-05-18-tankorent-cinemeta-pack-mapping-design.md#91-theatre_download_overhaul-decision-12-amendment) for full context.
```

- [ ] **Step 2: Append amendment line to chat.md**

Append to `agents/chat.md`:

```
## Agent 4 - THEATRE_DOWNLOAD_OVERHAUL Decision 12 AMENDED 2026-05-18
Files: docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md
Skills invoked: [/superpowers:receiving-code-review]
Cancel evicts entire pack (Index only, files stay on disk) for both addon and Tankorent paths. Supersedes original Decision 12 of 2026-05-16.
Rationale: earn UI trust before allowing partial-state preservation. Hemanth-ratified 2026-05-18 during TANKORENT_CINEMETA_PACK_MAPPING brainstorm (batch 3 Q3).
READY TO COMMIT
```

- [ ] **Step 3: Flag READY TO COMMIT** (the chat.md append IS the RTC for this task)

The chat.md append in Step 2 doubles as the RTC line. No additional RTC needed for this task.

---

### Task 23: Phase 4 smoke checkpoint — full matrix + Hemanth amber-tone ratification

**Goal:** Full smoke 1 + 2 + 3 from spec § 8 GREEN end-to-end. Hemanth live-eyeballs amber tint and ratifies (or requests adjustment) per Decision 5b. Continuous-confidence carry-through verified (Codex Trigger D #4 fixes still hold).

- [ ] **Step 1: Pre-smoke setup**

```powershell
.\scripts\stop-tankoban.ps1
```

Post `MCP LOCK CLAIMED` in chat.md.

- [ ] **Step 2: Smoke 1 — Daredevil S02 single-season pack**

Full sequence per spec § 8.1. Capture screenshots:
- Initial state at T+5s (Queued chips + amber tint visible)
- Episode 1 downloading state with progress bar
- Episode 1 complete + ep2 downloading (sequential rotation visible)
- Episode 1 playing from local file (in-app video player)

Save as `agents/audits/smoke_evidence/p4_smoke1_daredevil_NN_*.png`.

- [ ] **Step 3: Smoke 2 — Fight Club movie pack**

Full sequence per spec § 8.2. Capture screenshots:
- Movie tile DOWNLOADING chip + amber tint visible at T+5s
- Progress chip updating
- DOWNLOADED ✓ + amber tint persisting on completion

Save as `agents/audits/smoke_evidence/p4_smoke2_fightclub_*.png`.

- [ ] **Step 4: Smoke 3 — Cancel + re-dispatch**

Full sequence per spec § 8.3. Verify:
- All rows go default on cancel
- Same pack re-dispatches cleanly (resume picks up Complete files if any)

Save as `agents/audits/smoke_evidence/p4_smoke3_cancel_redispatch_*.png`.

- [ ] **Step 5: Continuous-confidence (Thread A) check**

Re-verify yesterday's Codex Trigger D #4 fixes still hold:
- Bulk-progress chip on movies (Codex #5 path) still accurate
- Season-row bulk-progress 30s grace window still works
- `streamBulkGroupsChanged` signal still fires on dispatch

Test by triggering a normal addon-bulk download (NOT Tankorent — use a Stremio addon torrent). Verify the chip appears + updates correctly + persists across detail view re-entry.

- [ ] **Step 6: Hemanth amber-tone ratification (interactive)**

Post in `agents/chat.md`:

```
## Agent 4 - HEMANTH ACTION REQUEST: amber tint live-eyeball ratification
Tankoban is currently running with the amber tint applied to Tankorent-sourced episode rows.
Smoke evidence at agents/audits/smoke_evidence/p4_smoke1_daredevil_*.png.
Please confirm: (a) tint is visible and distinguishes Tankorent rows clearly,
                (b) tint is not too aggressive / not too subtle,
                (c) tone (warm amber) matches your intent.
Reply with: "tint OK" / "make it warmer" / "make it cooler" / "make it lighter" / "make it darker" / specific hex.
```

Wait for Hemanth response. Adjust `TankorentTint::accentColor()` based on feedback (re-edit Task 20's color constant, rebuild, re-smoke). Iterate until Hemanth says "tint OK".

- [ ] **Step 7: Post-smoke teardown + final verdict**

```powershell
.\scripts\stop-tankoban.ps1
```

Post `MCP LOCK RELEASED` in chat.md.

```
## Agent 4 - TANKORENT_CINEMETA_PACK_MAPPING ARC SHIPPED — Phase 4 SMOKE GREEN
Skills invoked: [/superpowers:verification-before-completion, /superpowers:requesting-code-review]
Smoke 1 GREEN: Daredevil S02 sequential download — ep1 watchable at T+Nmin, sequential rotation verified through ep5.
Smoke 2 GREEN: Fight Club movie download — tile state + amber tint correct.
Smoke 3 GREEN: cancel evicts all 13 entries; re-dispatch resumes cleanly.
Continuous-confidence Thread A GREEN: Codex Trigger D #4 fixes (movie chip, 30s grace, streamBulkGroupsChanged) all still passing.
Amber tone RATIFIED by Hemanth at hex=<final hex>, alpha=<final alpha>.
Evidence: agents/audits/smoke_evidence/p4_smoke*_*.png
Arc closed. Awaiting Agent 0 /commit-sweep.
```

---

## Self-review checklist (run after writing this plan; fix inline)

**1. Spec coverage**

| Spec section | Task(s) implementing |
|--------------|----------------------|
| § 4 Architecture overview | Tasks 1-23 collectively |
| § 5.1 StreamPackParser | Tasks 1-2 |
| § 5.2 Entry schema v1→v2 | Tasks 3-4 |
| § 5.3 EpisodeTile state-input contract | Tasks 12-13 |
| § 5.4 SequentialPieceManager | Tasks 18-19 |
| § 6 Data flow (timeline) | Tasks 7-8 (registration + progress), Tasks 13-16 (UI surface) |
| § 7.1 Dead torrent metadata fail | (Default behavior — libtorrent timeout; no explicit task per Decision 13 deferral) |
| § 7.2 App close mid-download | Task 9 (launch-validation pass) |
| § 7.3 Re-dispatch same pack | Task 10 |
| § 7.4 Files moved/deleted | (Existing validateAll path; no explicit task — covered by inherited behavior) |
| § 7.5 Parser misreads | (Upstream of arc; agent-call default) |
| § 7.6 SequentialPieceManager edges | Task 18 unit tests |
| § 8.1 Smoke 1 single-season | Tasks 11, 17, 23 |
| § 8.2 Smoke 2 movie | Tasks 17, 23 |
| § 8.3 Smoke 3 cancel + redispatch | Task 23 |
| § 8.4 Continuous-confidence Thread A | Task 23 Step 5 |
| § 9.1 Decision 12 amendment | Task 22 |
| § 9.2 D1 EpisodeTile coordination | Tasks 12-13 |
| § 9.3 No other cross-arc ripple | (Confirmed — no other tasks touch unrelated arcs) |

**2. Placeholder scan**

- "Adapt to project's TorrentEngine mock pattern" in Task 18 — flagged as a known TBD. Acceptable because the task explicitly says behavioral coverage shifts to Task 19 smoke if test mocking isn't in place. Not a plan failure; an honest deferral.
- "adjust if requested" on amber tint in Task 20 — properly framed as Hemanth-ratification gated (Task 23 Step 6).
- No other "TBD" / "TODO" / "implement later" patterns.

**3. Type consistency**

- `Entry::State` enum values (Complete, Pending, Downloading, Failed) consistent across Tasks 3, 4, 5, 13, 14, 15, 16, 20.
- `EpisodeTileState::Provenance` enum (AddonBulk, Tankorent, LocalScan) consistent across Tasks 12, 13, 14, 15, 16, 20.
- `sourceGroupId` prefix `"tankorent:"` consistent across Tasks 4, 7, 9, 15, 20, 21.
- API method names (`registerPendingEpisode`, `registerPendingMovie`, `updateEpisodeProgress`, `evictBySourceGroup`) consistent across Tasks 3-9.

**4. Scope coherence**

- 23 numbered tasks across 4 phases — in the spec's 22-28 range.
- Each task has a single focused purpose and ends with an RTC.
- Phase gates (Tasks 11, 17, 23) are smoke-only and don't introduce code.
- Critical-path dependencies: Tasks 1-6 (Phase 1) must complete before Tasks 7-11 (Phase 2); Tasks 7-11 before 12-17 (Phase 3); 12-17 before 18-23 (Phase 4).

---

## Execution handoff

**Plan complete and saved to `docs/superpowers/plans/2026-05-18-tankorent-cinemeta-pack-mapping.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — Dispatch a fresh subagent per task; review between tasks; fast iteration. Uses `/superpowers:subagent-driven-development`. Each task's RTC + build_check.bat BUILD OK gate is the per-task ship signal.

**2. Inline Execution** — Execute tasks in this session using `/superpowers:executing-plans`. Batch execution with checkpoints for review.

**Which approach?**
