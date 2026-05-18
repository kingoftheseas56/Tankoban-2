# TANKORENT_STREAM_INTEGRATION Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the user downloads a Tankorent torrent of a Cinemeta-known show, every recognizable episode file in that torrent auto-registers into the existing StreamDownloadIndex so the show surfaces in Theatre's library and show-view with full identity binding — no filesystem matching, no AniList, no post-hoc enrichment.

**Architecture:** Extend the already-shipped `onFileRenamed → registerEpisode` hook (built for STREAM_DOWNLOADS_NETFLIX_OVERHAUL bulk-cohort downloads at `TorrentClient.cpp:2554`) to also fire for single-add Tankorent downloads originating from a new show-first picker UI. The picker captures `imdbId` + `season` at download-init time; the existing `BulkPackVerifier::matchEpisodeFileForSeason` filename regex extracts per-file `(S, E)` at completion; `StreamDownloadIndex::registerEpisode` does the persistence (with new highest-quality-wins dedup). Plus three UI deltas: a "Download via Tankorent" button inside `StreamDetailView`, a `TorrentPackPicker` modal, and a Theatre rename + Local files section + Videos page removal in Library UX.

**Tech Stack:** C++20, Qt6 (Widgets + Network + Concurrent), libtorrent-rasterbar (existing), JsonStore (existing), CMake + MSVC 2022 for main app, GoogleTest (via FetchContent) for `tankoban_tests`. No new external dependencies.

---

## Preamble — Tankoban-specific context for the executor

You are a skilled developer with no prior context for Tankoban. Read this section once before starting Task 1.

### Project facts

- **Single git checkout on master, no worktrees, no feature branches.** Per memory `feedback_no_worktrees.md`. Ignore the writing-plans skill's worktree note — does not apply here.
- **Hemanth is the user, not a developer.** Don't ask him terminal questions; do all build/test/log work yourself.
- **Brotherhood rules (CLAUDE.md):** Rule 1 = `taskkill //F //IM Tankoban.exe` before any rebuild. Rule 11 = post a `READY TO COMMIT — [...]` line to `agents/chat.md` after each task; Agent 0 batches commits. Rule 15 = self-service execution. Rule 17 = `powershell -NoProfile -File scripts/stop-tankoban.ps1` after any agent-driven smoke. Rule 19 = post `MCP LOCK` / `MCP LOCK RELEASED` lines in chat.md around any desktop-interacting MCP work.
- **Contracts-v3 RTC field:** every non-trivial RTC (≥1 src/ file OR ≥30 LOC) includes a `Skills invoked: [/skill1, /skill2, ...]` field between the body and `| files:`. See `agents/CONTRACTS.md` § Skill Provenance.

### Build commands

- **Compile-only verify (agent-safe, ~15min):** `build_check.bat` — prints `BUILD OK` or `BUILD FAILED exit=<n>` + 30-line cl.exe tail.
- **Full build + run:** `build_and_run.bat` — DO NOT run this; Hemanth opens the app for visual smoke per CLAUDE.md HEMANTH'S ROLE block.
- **Sidecar build:** `powershell -File native_sidecar/build.ps1` — not used in this arc.
- **Tests:** `cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON` then `cmake --build out --target tankoban_tests` then `cd out && ctest --output-on-failure -R tankoban_tests`. Test sources live under `tests/`. GoogleTest fetched via FetchContent on first configure.

### Where things live

- `src/core/stream/StreamDownloadIndex.{h,cpp}` — the registration store; you'll add highest-quality-wins dedup here.
- `src/core/stream/BulkPackVerifier.{h,cpp}` — the filename-regex helper at `BulkPackVerifier.cpp:125` (`matchEpisodeFileForSeason`). Reuse.
- `src/core/torrent/TorrentClient.{h,cpp}` — owns torrent records + completion alerts. Main edit site: `onTorrentFinished` at line 2382.
- `src/ui/dialogs/AddTorrentDialog.{h,cpp}` — the dialog + `AddTorrentConfig` struct. Add `imdbId` + `season` fields here.
- `src/ui/pages/stream/StreamDetailView.{h,cpp}` — show-view. Add "Download via Tankorent" button here.
- `src/ui/pages/stream/TorrentPackPicker.{h,cpp}` — new file. The show-first picker modal.
- `src/core/stream/QualityScorer.{h,cpp}` — new file. Pure-logic quality/health scoring helper.
- `src/core/stream/UnifiedProgressStore.{h,cpp}` — new file. Consolidates per-mode progress stores.
- `tests/core/stream/test_quality_scorer.cpp` — new GoogleTest file (with corresponding `CMakeLists.txt` entry under `tests/`).
- `tests/core/stream/test_stream_download_index_dedup.cpp` — new.
- `tests/core/stream/test_unified_progress_store.cpp` — new.

### Per-task commit cadence

Each task ends with a commit. Post a `READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task <N> — <summary>]` line to `agents/chat.md`; Agent 0 commits in batch. Do NOT commit directly unless explicitly authorized.

---

## File structure summary

| Path | Action | Responsibility |
|------|--------|----------------|
| `src/ui/dialogs/AddTorrentDialog.h` | Modify (struct extension) | Add `imdbId` + `season` fields to `AddTorrentConfig` |
| `src/ui/dialogs/AddTorrentDialog.cpp` | Modify | Pass through new fields in `config()` |
| `src/core/torrent/TorrentClient.cpp` | Modify | Persist `imdbId`+`season` in records; new `publishTankorentItemsForTorrent`; modify `onTorrentFinished` early-exit |
| `src/core/torrent/TorrentClient.h` | Modify | Declare `publishTankorentItemsForTorrent` |
| `src/core/stream/QualityScorer.h` | Create | Static helpers for quality/health/combined score |
| `src/core/stream/QualityScorer.cpp` | Create | Implementation |
| `src/core/stream/StreamDownloadIndex.cpp` | Modify | Add highest-quality-wins dedup to `registerEpisode` |
| `src/ui/pages/stream/TorrentPackPicker.h` | Create | Modal class header |
| `src/ui/pages/stream/TorrentPackPicker.cpp` | Create | Modal class impl (indexer fan-out, pack enrichment, sort, render) |
| `src/ui/pages/stream/StreamDetailView.cpp` | Modify | Add "Download via Tankorent" button to season header |
| `src/ui/pages/stream/StreamDetailView.h` | Modify | Declare button slot |
| `src/ui/MainWindow.cpp` | Modify | Remove Videos sidebar entry; rename Stream → Theatre user-facing strings |
| `src/ui/MainWindow.h` | Modify | (if needed for sidebar entry removal) |
| `src/ui/pages/TankorentPage.cpp` | Modify | Narrow scope to "Direct torrent search"; page title rename |
| `src/ui/pages/stream/StreamPage.cpp` | Modify | Add Local files section bottom row; route VideosScanner output |
| `src/core/stream/UnifiedProgressStore.h` | Create | Consolidated progress store class header |
| `src/core/stream/UnifiedProgressStore.cpp` | Create | Implementation |
| `tools/tankoctl.cpp` | Modify | Accept `--page theatre` as alias for `--page stream` |
| `CMakeLists.txt` | Modify | Add new sources to build target; new test sources gated on `TANKOBAN_BUILD_TESTS` |
| `tests/core/stream/test_quality_scorer.cpp` | Create | GoogleTest cases |
| `tests/core/stream/test_stream_download_index_dedup.cpp` | Create | GoogleTest cases |
| `tests/core/stream/test_unified_progress_store.cpp` | Create | GoogleTest cases |
| `tests/CMakeLists.txt` | Modify | Add new tests to `tankoban_tests` |

---

## Phase A — Identity passthrough plumbing

### Task A1: Add `imdbId` and `season` to `AddTorrentConfig`

**Files:**
- Modify: `src/ui/dialogs/AddTorrentDialog.h:16-25`
- Modify: `src/ui/dialogs/AddTorrentDialog.cpp` (config() method)

- [ ] **Step 1: Extend the `AddTorrentConfig` struct**

In `src/ui/dialogs/AddTorrentDialog.h`, change the struct from:

```cpp
struct AddTorrentConfig {
    QString category;
    QString destinationPath;
    QString contentLayout;
    QString streamGroupId;
    bool    sequential   = false;
    bool    startPaused  = false;
    QVector<int>   selectedIndices;
    QMap<int, int> filePriorities;
};
```

to:

```cpp
struct AddTorrentConfig {
    QString category;
    QString destinationPath;
    QString contentLayout;
    QString streamGroupId;
    bool    sequential   = false;
    bool    startPaused  = false;
    QVector<int>   selectedIndices;
    QMap<int, int> filePriorities;

    // TANKORENT_STREAM_INTEGRATION 2026-05-15: identity capture from show-first
    // picker flow. Empty when the dialog was invoked from non-show paths
    // (repurposed "Direct torrent search" or external callers).
    QString imdbId;       // "tt0141842" or empty
    int     season = 0;   // 1-based season; 0 = unbound (multi-season pack or non-show)
};
```

- [ ] **Step 2: Add constructor overload to accept pre-filled identity**

Below the existing constructor declaration in the same header:

```cpp
    // Constructor variant used by the show-first TorrentPackPicker flow.
    // Pre-fills imdbId + season so the dialog roundtrips them into config().
    AddTorrentDialog(const QString& torrentName,
                     const QString& infoHash,
                     const QMap<QString, QString>& defaultPaths,
                     const QString& preFilledImdbId,
                     int preFilledSeason,
                     QWidget* parent = nullptr);
```

Add a private member to hold the values:

```cpp
    QString m_preFilledImdbId;
    int     m_preFilledSeason = 0;
```

- [ ] **Step 3: Implement the new constructor in `AddTorrentDialog.cpp`**

```cpp
AddTorrentDialog::AddTorrentDialog(const QString& torrentName,
                                   const QString& infoHash,
                                   const QMap<QString, QString>& defaultPaths,
                                   const QString& preFilledImdbId,
                                   int preFilledSeason,
                                   QWidget* parent)
    : AddTorrentDialog(torrentName, infoHash, defaultPaths, parent)
{
    m_preFilledImdbId = preFilledImdbId;
    m_preFilledSeason = preFilledSeason;
}
```

- [ ] **Step 4: Pass new fields through `config()`**

Locate `AddTorrentConfig AddTorrentDialog::config() const` and append before the `return`:

```cpp
    cfg.imdbId = m_preFilledImdbId;
    cfg.season = m_preFilledSeason;
```

- [ ] **Step 5: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`

- [ ] **Step 6: Commit signal**

Post to `agents/chat.md`:

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task A1 — AddTorrentConfig extended with imdbId + season fields. New constructor overload roundtrips them; existing single-constructor call sites unchanged. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/dialogs/AddTorrentDialog.h, src/ui/dialogs/AddTorrentDialog.cpp
```

---

### Task A2: Persist identity into TorrentClient records

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp` (in `startDownload`, search for the block that writes `record["category"] = ...`)

- [ ] **Step 1: Locate the startDownload record-population block**

Run: `grep -n 'record\["category"\]' src/core/torrent/TorrentClient.cpp`

Note the line where `record["category"] = config.category;` is set. You'll add lines immediately after.

- [ ] **Step 2: Add imdbId + season persistence**

Immediately after `record["category"] = config.category;` add:

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: identity capture for single-add
    // Tankorent downloads originating from the show-first picker. Empty/0 for
    // non-Cinemeta downloads (repurposed direct torrent search).
    record["imdbId"] = config.imdbId;
    record["season"] = config.season;
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task A2 — TorrentClient persists imdbId+season in records JSON for single-add downloads. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/torrent/TorrentClient.cpp
```

---

### Task A3: Declare `publishTankorentItemsForTorrent` in TorrentClient

**Files:**
- Modify: `src/core/torrent/TorrentClient.h`

- [ ] **Step 1: Find the existing `publishStreamBulkItemsForTorrent` declaration**

Run: `grep -n 'publishStreamBulkItemsForTorrent' src/core/torrent/TorrentClient.h`

- [ ] **Step 2: Add the new method declaration immediately after**

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: registers each downloaded video
    // file in a Tankorent single-add torrent into StreamDownloadIndex, using
    // the imdbId+season persisted in m_records (Task A2) and BulkPackVerifier's
    // filename regex to detect per-file (season, episode). Called from
    // onTorrentFinished when streamGroupId is empty AND record["imdbId"] is set.
    void publishTankorentItemsForTorrent(const QString& infoHash);
```

- [ ] **Step 3: Compile-verify (will succeed since no callers yet)**

Run: `build_check.bat`

Expected: `BUILD OK`

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task A3 — TorrentClient::publishTankorentItemsForTorrent method declared. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/torrent/TorrentClient.h
```

---

### Task A4: Implement `publishTankorentItemsForTorrent` in TorrentClient

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp` (add new method body)

- [ ] **Step 1: Locate the existing `publishStreamBulkItemsForTorrent` body**

Run: `grep -n 'void TorrentClient::publishStreamBulkItemsForTorrent' src/core/torrent/TorrentClient.cpp`

Add the new method body immediately after the closing brace of that existing method.

- [ ] **Step 2: Add the method body**

```cpp
void TorrentClient::publishTankorentItemsForTorrent(const QString& infoHash)
{
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: bind every recognizable episode
    // file in a Tankorent single-add torrent to StreamDownloadIndex. The
    // record's imdbId + season were captured at download-init time by the
    // show-first picker (Task A2). For multi-season packs (season == 0),
    // BulkPackVerifier detects each file's season from its filename.

    if (!m_streamDownloadIndex) {
        qWarning() << "publishTankorentItemsForTorrent: no StreamDownloadIndex bound; skipping";
        return;
    }
    if (!m_records.contains(infoHash)) {
        qWarning() << "publishTankorentItemsForTorrent: no record for" << infoHash;
        return;
    }

    const QJsonObject record = m_records[infoHash].toObject();
    const QString imdbId = record.value(QStringLiteral("imdbId")).toString();
    const int configSeason = record.value(QStringLiteral("season")).toInt(0);
    if (imdbId.isEmpty()) {
        return;  // not a show-bound download; no identity to attach
    }

    const QString savePath = record.value(QStringLiteral("savePath")).toString();
    if (savePath.isEmpty()) {
        qWarning() << "publishTankorentItemsForTorrent: empty savePath for" << infoHash;
        return;
    }

    // Pull the file list from libtorrent via TorrentEngine.
    const QJsonArray files = m_engine ? m_engine->fileList(infoHash) : QJsonArray{};
    if (files.isEmpty()) {
        qWarning() << "publishTankorentItemsForTorrent: no files in torrent" << infoHash;
        return;
    }

    const QString sourceGroupId = QStringLiteral("tankorent:") + infoHash;
    int registeredCount = 0;

    for (int fileIdx = 0; fileIdx < files.size(); ++fileIdx) {
        const QJsonObject file = files.at(fileIdx).toObject();

        // BulkPackVerifier handles per-season detection when configSeason > 0;
        // for configSeason == 0 (multi-season pack), we iterate seasons 1..N
        // probing for a match. Most packs are single-season, so the season>0
        // path is the hot path.
        int detectedSeason = configSeason;
        int episodeNum = 0;
        int matchedFileIdx = 0;

        if (configSeason > 0) {
            const bool ok = tankostream::stream::BulkPackVerifier::matchEpisodeFileForSeason(
                file, configSeason, &episodeNum, &matchedFileIdx);
            if (!ok || episodeNum <= 0) continue;
        } else {
            // Multi-season pack: try seasons 1..30 (most shows fit) until one matches.
            for (int probeSeason = 1; probeSeason <= 30; ++probeSeason) {
                if (tankostream::stream::BulkPackVerifier::matchEpisodeFileForSeason(
                        file, probeSeason, &episodeNum, &matchedFileIdx)
                    && episodeNum > 0) {
                    detectedSeason = probeSeason;
                    break;
                }
            }
            if (episodeNum <= 0) continue;  // file not recognizable as any season's episode
        }

        // Reconstruct the absolute file path: savePath + relative file path.
        const QString relPath = file.value(QStringLiteral("path")).toString();
        if (relPath.isEmpty()) continue;
        const QString absPath = QDir(savePath).absoluteFilePath(relPath);
        const qint64 fileSize = QFileInfo(absPath).size();

        m_streamDownloadIndex->registerEpisode(
            imdbId,
            detectedSeason,
            episodeNum,
            absPath,
            sourceGroupId,
            fileSize
        );
        ++registeredCount;
    }

    qDebug() << "publishTankorentItemsForTorrent:" << infoHash
             << "registered" << registeredCount << "of" << files.size() << "files"
             << "as imdbId=" << imdbId;
}
```

- [ ] **Step 3: Add the includes if missing**

At the top of `TorrentClient.cpp`, verify these are present (add if not):

```cpp
#include "core/stream/BulkPackVerifier.h"
#include "core/stream/StreamDownloadIndex.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`

If `m_engine->fileList(infoHash)` fails to compile (no such method), use whichever existing method returns the file list — search for `QJsonArray.*files` or `fileList` in `src/core/torrent/TorrentEngine.h` and substitute. The bulk path uses the same primitive; it has to exist.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task A4 — TorrentClient::publishTankorentItemsForTorrent implemented. Enumerates downloaded files, runs BulkPackVerifier per-file, calls StreamDownloadIndex::registerEpisode for each match. Handles multi-season packs (season=0) via probe loop. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion] | files: src/core/torrent/TorrentClient.cpp
```

---

### Task A5: Modify `onTorrentFinished` to call the new method

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp:2420-2426`

- [ ] **Step 1: Replace the early-exit block**

Locate this block in `TorrentClient::onTorrentFinished`:

```cpp
    if (!streamGroupId.isEmpty())
        publishStreamBulkItemsForTorrent(infoHash);

    emit torrentCompleted(infoHash);

    if (!streamGroupId.isEmpty())
        return;
```

Replace with:

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: route to the right publisher
    // based on the source of this download.
    //   - bulk-cohort (streamGroupId set): existing Stream auto-download flow
    //   - Tankorent single-add with show identity (imdbId set, no streamGroupId):
    //         new show-first picker flow — register per-episode via Task A4
    //   - everything else (direct torrent search, no identity): fall through to
    //         the existing library-rescan path; file lands in Theatre's Local
    //         files section automatically.
    const bool hasBulkGroup = !streamGroupId.isEmpty();
    QString recordImdbId;
    if (m_records.contains(infoHash)) {
        recordImdbId = m_records[infoHash].toObject().value("imdbId").toString();
    }
    const bool hasTankorentBinding = !hasBulkGroup && !recordImdbId.isEmpty();

    if (hasBulkGroup) {
        publishStreamBulkItemsForTorrent(infoHash);
    } else if (hasTankorentBinding) {
        publishTankorentItemsForTorrent(infoHash);
    }

    emit torrentCompleted(infoHash);

    if (hasBulkGroup || hasTankorentBinding) return;
```

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`

- [ ] **Step 3: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task A5 — onTorrentFinished early-exit modified to route Tankorent single-add downloads (with show identity) through publishTankorentItemsForTorrent before the library-rescan fall-through. Bulk-cohort path unchanged; direct-torrent-search (no imdbId) falls through to existing scanner. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion] | files: src/core/torrent/TorrentClient.cpp
```

---

## Phase B — Quality scoring (pure-logic TDD)

### Task B1: Create `QualityScorer` header

**Files:**
- Create: `src/core/stream/QualityScorer.h`
- Modify: `CMakeLists.txt` (add to main app sources)

- [ ] **Step 1: Write the header**

Create `src/core/stream/QualityScorer.h`:

```cpp
#pragma once

// TANKORENT_STREAM_INTEGRATION 2026-05-15 — quality and health scoring helpers
// shared between TorrentPackPicker sort logic and StreamDownloadIndex
// highest-quality-wins dedup. Pure-logic; no Qt object state; trivially testable
// via tankoban_tests.
//
// All inputs are filename basenames (no directory prefix). Tags detected
// case-insensitively. Functions return 0..100 scores; combinedScore returns
// 0..100 weighted average.

#include <QString>

namespace tankostream::stream {

class QualityScorer {
public:
    // 4K/2160p=100, 1440p=90, 1080p=80, 720p=60, 480p=40, else=20.
    static int resolutionScore(const QString& filename);

    // BluRay=100, WEB-DL=80, HDTV=60, WEBRip=50, DVDRip=40, else=20.
    static int sourceScore(const QString& filename);

    // Weighted combo: 0.7*resolution + 0.3*source.
    static int qualityScore(const QString& filename);

    // log2(seeders + 1) * 10, capped at 100. 0 seeders = 0; 1023 seeders = 100.
    static int healthScore(int seeders);

    // (quality * wQuality + health * wHealth) / (wQuality + wHealth).
    // Caller ensures wQuality + wHealth > 0; otherwise returns 0.
    static double combinedScore(int quality, int health, double wQuality, double wHealth);
};

}  // namespace tankostream::stream
```

- [ ] **Step 2: Add to CMakeLists.txt**

In the main `CMakeLists.txt`, find the section listing `src/core/stream/StreamDownloadIndex.cpp` and add `src/core/stream/QualityScorer.cpp` to the same list. Header files are picked up via `target_include_directories`.

- [ ] **Step 3: Compile-verify (will fail until cpp exists; OK)**

Run: `build_check.bat`

Expected: `BUILD FAILED` with linker error referencing undefined `QualityScorer` symbols. Don't worry — Task B2 fixes this.

If compile fails on header alone (parse error), fix the syntax before proceeding.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task B1 — QualityScorer header declared with five static methods (resolutionScore, sourceScore, qualityScore, healthScore, combinedScore). Added to CMakeLists.txt. Header compiles standalone; full link fails pending Task B2.] | Skills invoked: [/superpowers:executing-plans] | files: src/core/stream/QualityScorer.h, CMakeLists.txt
```

---

### Task B2: TDD — `resolutionScore`

**Files:**
- Create: `tests/core/stream/test_quality_scorer.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `src/core/stream/QualityScorer.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/stream/test_quality_scorer.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/stream/QualityScorer.h"

using tankostream::stream::QualityScorer;

TEST(QualityScorerTest, ResolutionScore_DetectsCommonTags) {
    EXPECT_EQ(100, QualityScorer::resolutionScore("Show.S01E01.2160p.BluRay.mkv"));
    EXPECT_EQ(100, QualityScorer::resolutionScore("Show.S01E01.4K.HDR.mkv"));
    EXPECT_EQ(90,  QualityScorer::resolutionScore("Show.S01E01.1440p.WEB-DL.mkv"));
    EXPECT_EQ(80,  QualityScorer::resolutionScore("Show.S01E01.1080p.BluRay.mkv"));
    EXPECT_EQ(60,  QualityScorer::resolutionScore("Show.S01E01.720p.HDTV.mkv"));
    EXPECT_EQ(40,  QualityScorer::resolutionScore("Show.S01E01.480p.DVDRip.mkv"));
    EXPECT_EQ(20,  QualityScorer::resolutionScore("Show.S01E01.mkv"));
}

TEST(QualityScorerTest, ResolutionScore_CaseInsensitive) {
    EXPECT_EQ(80, QualityScorer::resolutionScore("Show.S01E01.1080P.BluRay.mkv"));
    EXPECT_EQ(80, QualityScorer::resolutionScore("show.s01e01.1080p.bluray.mkv"));
}
```

- [ ] **Step 2: Add test to `tests/CMakeLists.txt`**

In `tests/CMakeLists.txt`, find the existing test sources list (look for `target_sources(tankoban_tests` or similar). Add:

```cmake
    tests/core/stream/test_quality_scorer.cpp
```

- [ ] **Step 3: Run test to verify it fails**

```
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest"
```

Expected: build FAILS with undefined `QualityScorer::resolutionScore`. That proves the test reaches the symbol.

- [ ] **Step 4: Write minimal implementation**

Create `src/core/stream/QualityScorer.cpp`:

```cpp
#include "core/stream/QualityScorer.h"

#include <QRegularExpression>
#include <cmath>

namespace tankostream::stream {

int QualityScorer::resolutionScore(const QString& filename) {
    static const QRegularExpression re4k("(?i)\\b(2160p|4k)\\b");
    static const QRegularExpression re1440("(?i)\\b1440p\\b");
    static const QRegularExpression re1080("(?i)\\b1080p\\b");
    static const QRegularExpression re720("(?i)\\b720p\\b");
    static const QRegularExpression re480("(?i)\\b480p\\b");
    if (re4k.match(filename).hasMatch())   return 100;
    if (re1440.match(filename).hasMatch()) return 90;
    if (re1080.match(filename).hasMatch()) return 80;
    if (re720.match(filename).hasMatch())  return 60;
    if (re480.match(filename).hasMatch())  return 40;
    return 20;
}

// Stubs — implemented in subsequent tasks.
int QualityScorer::sourceScore(const QString& /*filename*/) { return 0; }
int QualityScorer::qualityScore(const QString& /*filename*/) { return 0; }
int QualityScorer::healthScore(int /*seeders*/) { return 0; }
double QualityScorer::combinedScore(int, int, double, double) { return 0.0; }

}  // namespace tankostream::stream
```

- [ ] **Step 5: Run test to verify it passes**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest.ResolutionScore"
```

Expected: PASS for both `ResolutionScore_DetectsCommonTags` and `ResolutionScore_CaseInsensitive`.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task B2 — QualityScorer::resolutionScore TDD-implemented. 8 test cases GREEN (4K/2160p/1440p/1080p/720p/480p detection + case-insensitive). sourceScore/qualityScore/healthScore/combinedScore stubbed for Tasks B3-B5.] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /superpowers:verification-before-completion] | files: src/core/stream/QualityScorer.cpp, tests/core/stream/test_quality_scorer.cpp, tests/CMakeLists.txt
```

---

### Task B3: TDD — `sourceScore`

**Files:**
- Modify: `tests/core/stream/test_quality_scorer.cpp`
- Modify: `src/core/stream/QualityScorer.cpp`

- [ ] **Step 1: Append failing tests**

Add to `tests/core/stream/test_quality_scorer.cpp`:

```cpp
TEST(QualityScorerTest, SourceScore_DetectsCommonTags) {
    EXPECT_EQ(100, QualityScorer::sourceScore("Show.S01E01.1080p.BluRay.mkv"));
    EXPECT_EQ(100, QualityScorer::sourceScore("Show.S01E01.1080p.BDRip.mkv"));
    EXPECT_EQ(80,  QualityScorer::sourceScore("Show.S01E01.1080p.WEB-DL.mkv"));
    EXPECT_EQ(80,  QualityScorer::sourceScore("Show.S01E01.1080p.WEBDL.mkv"));
    EXPECT_EQ(60,  QualityScorer::sourceScore("Show.S01E01.720p.HDTV.mkv"));
    EXPECT_EQ(50,  QualityScorer::sourceScore("Show.S01E01.720p.WEBRip.mkv"));
    EXPECT_EQ(40,  QualityScorer::sourceScore("Show.S01E01.480p.DVDRip.mkv"));
    EXPECT_EQ(20,  QualityScorer::sourceScore("Show.S01E01.mkv"));
}
```

- [ ] **Step 2: Run to verify failure**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest.SourceScore"
```

Expected: FAIL — all current returns are 0.

- [ ] **Step 3: Implement**

Replace the `sourceScore` stub in `src/core/stream/QualityScorer.cpp`:

```cpp
int QualityScorer::sourceScore(const QString& filename) {
    static const QRegularExpression reBlu("(?i)\\b(BluRay|BDRip|Blu-Ray)\\b");
    static const QRegularExpression reWebDL("(?i)\\b(WEB-DL|WEBDL)\\b");
    static const QRegularExpression reHDTV("(?i)\\bHDTV\\b");
    static const QRegularExpression reWebRip("(?i)\\b(WEBRip|WEB-RIP)\\b");
    static const QRegularExpression reDVD("(?i)\\b(DVDRip|DVD)\\b");
    if (reBlu.match(filename).hasMatch())     return 100;
    if (reWebDL.match(filename).hasMatch())   return 80;
    if (reHDTV.match(filename).hasMatch())    return 60;
    if (reWebRip.match(filename).hasMatch())  return 50;
    if (reDVD.match(filename).hasMatch())     return 40;
    return 20;
}
```

- [ ] **Step 4: Verify pass**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest.SourceScore"
```

Expected: PASS.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task B3 — QualityScorer::sourceScore TDD-implemented. 8 test cases GREEN (BluRay/BDRip/WEB-DL/WEBDL/HDTV/WEBRip/DVDRip detection).] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /superpowers:verification-before-completion] | files: src/core/stream/QualityScorer.cpp, tests/core/stream/test_quality_scorer.cpp
```

---

### Task B4: TDD — `qualityScore` and `healthScore`

**Files:**
- Modify: `tests/core/stream/test_quality_scorer.cpp`
- Modify: `src/core/stream/QualityScorer.cpp`

- [ ] **Step 1: Append failing tests**

```cpp
TEST(QualityScorerTest, QualityScore_IsWeightedCombo) {
    // 1080p BluRay: 0.7 * 80 + 0.3 * 100 = 56 + 30 = 86
    EXPECT_EQ(86, QualityScorer::qualityScore("Show.S01E01.1080p.BluRay.mkv"));
    // 720p HDTV: 0.7 * 60 + 0.3 * 60 = 60
    EXPECT_EQ(60, QualityScorer::qualityScore("Show.S01E01.720p.HDTV.mkv"));
    // No tags: 0.7 * 20 + 0.3 * 20 = 20
    EXPECT_EQ(20, QualityScorer::qualityScore("Show.S01E01.mkv"));
}

TEST(QualityScorerTest, HealthScore_LogScale) {
    EXPECT_EQ(0,   QualityScorer::healthScore(0));     // log2(1)*10 = 0
    EXPECT_EQ(10,  QualityScorer::healthScore(1));     // log2(2)*10 = 10
    EXPECT_EQ(20,  QualityScorer::healthScore(3));     // log2(4)*10 = 20
    EXPECT_EQ(50,  QualityScorer::healthScore(31));    // log2(32)*10 = 50
    EXPECT_EQ(100, QualityScorer::healthScore(1023));  // log2(1024)*10 = 100
    EXPECT_EQ(100, QualityScorer::healthScore(5000));  // capped at 100
}
```

- [ ] **Step 2: Run to verify failure**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest.(QualityScore|HealthScore)"
```

Expected: FAIL.

- [ ] **Step 3: Implement both**

Replace the stubs in `src/core/stream/QualityScorer.cpp`:

```cpp
int QualityScorer::qualityScore(const QString& filename) {
    const int res = resolutionScore(filename);
    const int src = sourceScore(filename);
    return static_cast<int>(0.7 * res + 0.3 * src);
}

int QualityScorer::healthScore(int seeders) {
    if (seeders < 0) seeders = 0;
    const double score = std::log2(static_cast<double>(seeders) + 1.0) * 10.0;
    if (score > 100.0) return 100;
    return static_cast<int>(score);
}
```

- [ ] **Step 4: Verify pass**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest.(QualityScore|HealthScore)"
```

Expected: PASS.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task B4 — QualityScorer::qualityScore and ::healthScore TDD-implemented. 9 test cases GREEN (weighted combo for quality, log2 scale for health, cap at 100).] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /superpowers:verification-before-completion] | files: src/core/stream/QualityScorer.cpp, tests/core/stream/test_quality_scorer.cpp
```

---

### Task B5: TDD — `combinedScore`

**Files:**
- Modify: `tests/core/stream/test_quality_scorer.cpp`
- Modify: `src/core/stream/QualityScorer.cpp`

- [ ] **Step 1: Append failing tests**

```cpp
TEST(QualityScorerTest, CombinedScore_WeightedAverage) {
    // (80 * 0.6 + 50 * 0.4) / (0.6 + 0.4) = (48 + 20) / 1.0 = 68
    EXPECT_DOUBLE_EQ(68.0, QualityScorer::combinedScore(80, 50, 0.6, 0.4));
    // All quality, no health: returns quality
    EXPECT_DOUBLE_EQ(80.0, QualityScorer::combinedScore(80, 50, 1.0, 0.0));
    // All health, no quality: returns health
    EXPECT_DOUBLE_EQ(50.0, QualityScorer::combinedScore(80, 50, 0.0, 1.0));
    // Equal weights
    EXPECT_DOUBLE_EQ(65.0, QualityScorer::combinedScore(80, 50, 0.5, 0.5));
}

TEST(QualityScorerTest, CombinedScore_ZeroWeightsGuard) {
    EXPECT_DOUBLE_EQ(0.0, QualityScorer::combinedScore(80, 50, 0.0, 0.0));
}
```

- [ ] **Step 2: Run to verify failure**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest.CombinedScore"
```

Expected: FAIL.

- [ ] **Step 3: Implement**

```cpp
double QualityScorer::combinedScore(int quality, int health, double wQuality, double wHealth) {
    const double sum = wQuality + wHealth;
    if (sum <= 0.0) return 0.0;
    return (static_cast<double>(quality) * wQuality + static_cast<double>(health) * wHealth) / sum;
}
```

- [ ] **Step 4: Verify pass**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "QualityScorerTest.CombinedScore"
```

Expected: PASS.

- [ ] **Step 5: Run ALL QualityScorerTest cases as regression check**

```
cd out && ctest --output-on-failure -R "QualityScorerTest"
```

Expected: ALL PASS.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task B5 — QualityScorer::combinedScore TDD-implemented. 5 test cases GREEN (weighted average, all-quality-weight, all-health-weight, equal-weights, zero-weights guard). Full QualityScorerTest suite GREEN (30 assertions).] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /superpowers:verification-before-completion] | files: src/core/stream/QualityScorer.cpp, tests/core/stream/test_quality_scorer.cpp
```

---

## Phase C — StreamDownloadIndex highest-quality-wins dedup

### Task C1: TDD — dedup in `registerEpisode`

**Files:**
- Create: `tests/core/stream/test_stream_download_index_dedup.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/core/stream/StreamDownloadIndex.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/stream/test_stream_download_index_dedup.cpp`:

```cpp
#include <gtest/gtest.h>
#include <QString>
#include <QTemporaryDir>
#include "core/stream/StreamDownloadIndex.h"
#include "core/persistence/JsonStore.h"

namespace {

class StreamDownloadIndexDedupTest : public ::testing::Test {
protected:
    QTemporaryDir m_tempDir;
    std::unique_ptr<JsonStore> m_store;
    std::unique_ptr<StreamDownloadIndex> m_index;

    void SetUp() override {
        m_store = std::make_unique<JsonStore>(m_tempDir.path());
        m_index = std::make_unique<StreamDownloadIndex>(m_store.get());
    }
};

}  // namespace

TEST_F(StreamDownloadIndexDedupTest, HigherQualityEvictsLower) {
    // Register a 720p file first.
    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.720p.HDTV.mkv",
                             "tankorent:abc", 700'000'000);

    auto first = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ("/x/Sopranos.S06E03.720p.HDTV.mkv", *first);

    // Register a 1080p file for the same episode — should evict.
    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:def", 2'500'000'000);

    auto second = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ("/x/Sopranos.S06E03.1080p.BluRay.mkv", *second);
}

TEST_F(StreamDownloadIndexDedupTest, LowerQualityDoesNotEvictHigher) {
    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:def", 2'500'000'000);

    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.720p.HDTV.mkv",
                             "tankorent:abc", 700'000'000);

    auto kept = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ("/x/Sopranos.S06E03.1080p.BluRay.mkv", *kept);
}

TEST_F(StreamDownloadIndexDedupTest, EqualQualityKeepsFirst) {
    m_index->registerEpisode("tt0141842", 6, 3,
                             "/a/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:a", 2'500'000'000);

    m_index->registerEpisode("tt0141842", 6, 3,
                             "/b/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:b", 2'500'000'000);

    auto kept = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ("/a/Sopranos.S06E03.1080p.BluRay.mkv", *kept);  // first wins on ties
}
```

- [ ] **Step 2: Add test to `tests/CMakeLists.txt`**

```cmake
    tests/core/stream/test_stream_download_index_dedup.cpp
```

- [ ] **Step 3: Run to verify it fails**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "StreamDownloadIndexDedupTest"
```

Expected: `HigherQualityEvictsLower` PASSES coincidentally (existing behavior may overwrite); `LowerQualityDoesNotEvictHigher` and `EqualQualityKeepsFirst` FAIL because current `registerEpisode` unconditionally overwrites.

- [ ] **Step 4: Add the dedup logic to `registerEpisode`**

In `src/core/stream/StreamDownloadIndex.cpp`, locate `void StreamDownloadIndex::registerEpisode(...)`. Find the spot where `m_byEpisode[episodeKey] = canonicalKey;` is set (the existing overwrite). Just before that line, add:

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: highest-quality-wins dedup.
    // If an existing entry covers this (imdbId, season, episode), compare
    // quality scores from filenames and keep the higher one. Equal-quality
    // ties keep the FIRST registered (first-wins on tie).
    {
        const auto existingIt = m_byEpisode.constFind(episodeKey);
        if (existingIt != m_byEpisode.constEnd()) {
            const QString existingCanonicalKey = *existingIt;
            const auto existingEntryIt = m_byPath.constFind(existingCanonicalKey);
            if (existingEntryIt != m_byPath.constEnd()) {
                const int existingScore = tankostream::stream::QualityScorer::qualityScore(
                    QFileInfo(existingEntryIt->canonicalPath).fileName());
                const int newScore = tankostream::stream::QualityScorer::qualityScore(
                    QFileInfo(canonicalPath).fileName());
                if (newScore <= existingScore) {
                    return;  // keep existing
                }
                // New wins: evict the old by-path entry before proceeding.
                m_byPath.remove(existingCanonicalKey);
            }
        }
    }
```

Then add the necessary includes at the top of `StreamDownloadIndex.cpp`:

```cpp
#include "core/stream/QualityScorer.h"
#include <QFileInfo>
```

- [ ] **Step 5: Run to verify pass**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "StreamDownloadIndexDedupTest"
```

Expected: ALL PASS.

- [ ] **Step 6: Regression check — bulk-cohort tests still GREEN**

```
cd out && ctest --output-on-failure -R "StreamDownloadIndex"
```

Expected: ALL PASS including any pre-existing bulk tests (if any).

- [ ] **Step 7: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task C1 — StreamDownloadIndex::registerEpisode highest-quality-wins dedup TDD-implemented. New entry compared against existing via QualityScorer::qualityScore on filename basenames. Higher wins, ties keep first-registered, lower is silently dropped. 3 dedup test cases GREEN; existing bulk-cohort tests still GREEN.] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /simplify, /superpowers:verification-before-completion] | files: src/core/stream/StreamDownloadIndex.cpp, tests/core/stream/test_stream_download_index_dedup.cpp, tests/CMakeLists.txt
```

---

## Phase D — `TorrentPackPicker` UI (Agent 4B)

### Task D1: `TorrentPackPicker` skeleton class

**Files:**
- Create: `src/ui/pages/stream/TorrentPackPicker.h`
- Create: `src/ui/pages/stream/TorrentPackPicker.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/stream/TorrentPackPicker.h`:

```cpp
#pragma once

// TANKORENT_STREAM_INTEGRATION 2026-05-15 — modal picker shown from inside
// StreamDetailView when the user clicks "Download via Tankorent" on a season
// header. Fans out indexer searches with the show's identity already in
// context (imdbId, showName, season). Displays packs sorted by combined
// quality×seeders score; pinned multi-season "complete series" row at top.

#include <QDialog>
#include <QString>
#include "core/torrent/TorrentResult.h"

class QListWidget;
class QLabel;
class QPushButton;
class TankorentClient;       // existing — owns the indexer fan-out
class StreamLibrary;          // existing — for show name lookup

class TorrentPackPicker : public QDialog
{
    Q_OBJECT
public:
    TorrentPackPicker(const QString& imdbId,
                      const QString& showName,
                      int season,                // 0 = "whole show" pack search
                      TankorentClient* client,
                      QWidget* parent = nullptr);

signals:
    // Emitted when user picks a pack. Caller (StreamDetailView) opens
    // AddTorrentDialog with imdbId + season pre-filled, then starts download.
    void packChosen(const TorrentResult& chosen,
                    const QString& imdbId,
                    int season);

private slots:
    void onIndexerResults(const QList<TorrentResult>& results);
    void onRowDoubleClicked();

private:
    void buildUI();
    void launchSearches();
    void rerankAndRender();

    QString m_imdbId;
    QString m_showName;
    int     m_season;
    TankorentClient* m_client;

    QList<TorrentResult> m_allResults;   // accumulated across indexers
    QListWidget* m_list;
    QLabel* m_status;
    QPushButton* m_downloadBtn;
};
```

- [ ] **Step 2: Write skeleton .cpp**

Create `src/ui/pages/stream/TorrentPackPicker.cpp`:

```cpp
#include "ui/pages/stream/TorrentPackPicker.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>

#include "core/stream/QualityScorer.h"
#include "core/torrent/TankorentClient.h"

TorrentPackPicker::TorrentPackPicker(const QString& imdbId,
                                     const QString& showName,
                                     int season,
                                     TankorentClient* client,
                                     QWidget* parent)
    : QDialog(parent)
    , m_imdbId(imdbId)
    , m_showName(showName)
    , m_season(season)
    , m_client(client)
    , m_list(nullptr)
    , m_status(nullptr)
    , m_downloadBtn(nullptr)
{
    setWindowTitle(tr("Download via Tankorent — %1 %2")
                       .arg(showName)
                       .arg(season > 0 ? QStringLiteral("Season %1").arg(season)
                                       : QStringLiteral("(whole show)")));
    setMinimumSize(720, 480);
    buildUI();
    launchSearches();
}

void TorrentPackPicker::buildUI() {
    auto* root = new QVBoxLayout(this);
    m_status = new QLabel(tr("Searching indexers..."), this);
    root->addWidget(m_status);
    m_list = new QListWidget(this);
    root->addWidget(m_list, 1);
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    m_downloadBtn = new QPushButton(tr("Download"), this);
    m_downloadBtn->setEnabled(false);
    connect(m_downloadBtn, &QPushButton::clicked, this, &TorrentPackPicker::onRowDoubleClicked);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_downloadBtn);
    root->addLayout(btnRow);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &TorrentPackPicker::onRowDoubleClicked);
    connect(m_list, &QListWidget::currentRowChanged, [this](int row) {
        m_downloadBtn->setEnabled(row >= 0);
    });
}

void TorrentPackPicker::launchSearches() {
    // Task D2 fills this in.
}

void TorrentPackPicker::onIndexerResults(const QList<TorrentResult>& results) {
    m_allResults.append(results);
    rerankAndRender();
}

void TorrentPackPicker::rerankAndRender() {
    // Task D4 fills this in. For now, just append flat.
    m_list->clear();
    for (const auto& r : m_allResults) {
        m_list->addItem(QStringLiteral("%1 · %2 seeders · %3 MB")
                            .arg(r.title)
                            .arg(r.seeders)
                            .arg(r.sizeBytes / 1'000'000));
    }
}

void TorrentPackPicker::onRowDoubleClicked() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_allResults.size()) return;
    emit packChosen(m_allResults[row], m_imdbId, m_season);
    accept();
}
```

- [ ] **Step 3: Add to CMakeLists.txt**

Append `src/ui/pages/stream/TorrentPackPicker.cpp` to the main app sources list (same section as `StreamDetailView.cpp`).

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`. If `TankorentClient` includes fail to resolve, find the actual class name with `grep -rn 'class Tankorent.*Client' src/`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task D1 — TorrentPackPicker skeleton class created. Dialog with status label + list widget + Cancel/Download buttons. Wires double-click + Download button to packChosen signal. launchSearches + rerankAndRender are stubs filled in by D2/D4. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TorrentPackPicker.h, src/ui/pages/stream/TorrentPackPicker.cpp, CMakeLists.txt
```

---

### Task D2: Indexer fan-out with show-context query

**Files:**
- Modify: `src/ui/pages/stream/TorrentPackPicker.cpp:launchSearches`

- [ ] **Step 1: Identify the existing indexer fan-out method**

Run: `grep -n 'dispatchIndexers\|fanOutSearch' src/ui/pages/TankorentPage.cpp src/core/torrent/`

Look for the method `TankorentClient` (or the equivalent class — likely named per the codebase grep) exposes for "search this query across all enabled indexers and signal results."

- [ ] **Step 2: Fill in `launchSearches()`**

Replace the stub body in `TorrentPackPicker.cpp`:

```cpp
void TorrentPackPicker::launchSearches() {
    if (!m_client) return;

    // Build query variations. Most packs use either:
    //   "<show> S<NN>" or "<show> Season <N>" or "<show>" (for whole-show packs).
    QStringList queries;
    if (m_season > 0) {
        queries << QStringLiteral("%1 S%2")
                       .arg(m_showName)
                       .arg(m_season, 2, 10, QLatin1Char('0'));
        queries << QStringLiteral("%1 Season %2").arg(m_showName).arg(m_season);
    } else {
        queries << QStringLiteral("%1 Complete").arg(m_showName);
        queries << QStringLiteral("%1 Complete Series").arg(m_showName);
    }

    // Fan out per query. Each callback delivers results via onIndexerResults.
    for (const QString& q : queries) {
        // The exact API name needs to match what TankorentClient exposes —
        // confirmed via grep in Step 1. Common shape:
        m_client->searchAll(q, /* maxResultsPerIndexer */ 25,
                            [this](const QList<TorrentResult>& r) {
                                onIndexerResults(r);
                            });
    }

    m_status->setText(tr("Searching %1 queries...").arg(queries.size()));
}
```

If `TankorentClient::searchAll` doesn't exist, look for the existing pattern Tankorent uses for fan-out. The grep in Step 1 will reveal the actual API.

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`. If linker error on `searchAll`, substitute the actual method discovered in Step 1.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task D2 — TorrentPackPicker::launchSearches fans out 2 query variations per season-or-whole-show invocation against TankorentClient. Results accumulate via onIndexerResults; rerankAndRender stub still appends flat. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TorrentPackPicker.cpp
```

---

### Task D3: Pack episode-count enrichment

**Files:**
- Modify: `src/ui/pages/stream/TorrentPackPicker.h` (add a struct)
- Modify: `src/ui/pages/stream/TorrentPackPicker.cpp` (enrich each result with episode-count from torrent metadata)

- [ ] **Step 1: Add an `EnrichedResult` struct to the header**

Inside the class declaration, add a private nested struct:

```cpp
private:
    struct EnrichedResult {
        TorrentResult raw;
        int detectedEpisodeCount = 0;   // 0 = not yet probed
        QSet<int> detectedSeasons;       // seasons covered (1+ entries = multi-season)
        double combinedScore = 0.0;
        bool isMultiSeason() const { return detectedSeasons.size() > 1; }
    };

    QList<EnrichedResult> m_enriched;
```

Add the `#include <QSet>` to the header.

- [ ] **Step 2: Enrichment uses BulkPackVerifier**

When indexer results arrive, we have torrent NAMES but not file lists (those require fetching metadata). Two strategies:
- **Fast path (v1):** parse episode count from the torrent name (regex matches `S\d{2}` count, `Complete`, `1-13`, etc.). Multi-season detection from name patterns ("S01-S06" or "Complete Series").
- **Accurate path (v2):** fetch torrent metadata to get file list, run `BulkPackVerifier` per file. Slower; requires per-torrent metadata fetch.

For v1 ship, fast-path-only. Implement title-parsing helpers:

In `TorrentPackPicker.cpp`, add file-scope helper functions:

```cpp
namespace {

// Detect explicit "Complete Series" / "Complete Box Set" / "S01-S06" patterns.
bool isCompleteSeriesName(const QString& title) {
    static const QRegularExpression reComplete(
        "(?i)\\b(complete[\\s._-]*series|complete[\\s._-]*box[\\s._-]*set|complete[\\s._-]*collection)\\b");
    static const QRegularExpression reSeasonRange(
        "(?i)\\bS\\d{1,2}[\\s._-]*[-\\s]?[\\s._-]*S\\d{1,2}\\b");
    return reComplete.match(title).hasMatch() || reSeasonRange.match(title).hasMatch();
}

// Try to extract the season number(s) from the torrent title.
QSet<int> detectSeasonsFromTitle(const QString& title) {
    QSet<int> seasons;
    static const QRegularExpression reRange(
        "(?i)\\bS(\\d{1,2})[\\s._-]*[-\\s][\\s._-]*S(\\d{1,2})\\b");
    auto rangeMatch = reRange.match(title);
    if (rangeMatch.hasMatch()) {
        int start = rangeMatch.captured(1).toInt();
        int end = rangeMatch.captured(2).toInt();
        for (int s = start; s <= end; ++s) seasons.insert(s);
        return seasons;
    }
    static const QRegularExpression reSingle("(?i)\\bS(\\d{1,2})\\b");
    auto it = reSingle.globalMatch(title);
    while (it.hasNext()) {
        seasons.insert(it.next().captured(1).toInt());
    }
    return seasons;
}

// Try to extract episode count: "13 Eps", "1-13", "Complete (13 episodes)", or
// 0 if not derivable from title.
int detectEpisodeCountFromTitle(const QString& title) {
    static const QRegularExpression reCount(
        "(?i)\\b(\\d{1,3})[\\s._-]*(?:eps?|episodes?)\\b");
    auto m = reCount.match(title);
    if (m.hasMatch()) return m.captured(1).toInt();
    static const QRegularExpression reRange("\\b(\\d{1,3})-(\\d{1,3})\\b");
    auto r = reRange.match(title);
    if (r.hasMatch()) return r.captured(2).toInt() - r.captured(1).toInt() + 1;
    return 0;
}

}  // namespace
```

- [ ] **Step 3: Add the enrichment step**

In `onIndexerResults`, before calling `rerankAndRender()`, enrich each new result:

```cpp
void TorrentPackPicker::onIndexerResults(const QList<TorrentResult>& results) {
    for (const auto& r : results) {
        EnrichedResult er;
        er.raw = r;
        er.detectedSeasons = detectSeasonsFromTitle(r.title);
        if (isCompleteSeriesName(r.title) && er.detectedSeasons.size() < 2) {
            // Mark as multi-season even without season tags — title says complete.
            er.detectedSeasons.insert(0);  // sentinel; rerank treats this as multi-season
        }
        er.detectedEpisodeCount = detectEpisodeCountFromTitle(r.title);
        m_enriched.append(er);
    }
    rerankAndRender();
}
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task D3 — TorrentPackPicker enriches each indexer result with detected season set + episode count + multi-season flag, parsed from torrent title via three regex helpers. Multi-season detection via "Complete Series" / "S01-S06" patterns. Stored in EnrichedResult struct. rerankAndRender still flat-renders pending Task D4.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/TorrentPackPicker.h, src/ui/pages/stream/TorrentPackPicker.cpp
```

---

### Task D4: Sort by combined score, pin multi-season at top

**Files:**
- Modify: `src/ui/pages/stream/TorrentPackPicker.cpp:rerankAndRender`

- [ ] **Step 1: Add a quality-weight Q-setting read**

In `TorrentPackPicker.cpp`, add at the top of `rerankAndRender`:

```cpp
void TorrentPackPicker::rerankAndRender() {
    QSettings settings;
    const double wQuality = settings.value("theatre/qualityWeight", 0.6).toDouble();
    const double wHealth = 1.0 - wQuality;
```

Add `#include <QSettings>`.

- [ ] **Step 2: Compute combined scores**

Continue inside `rerankAndRender`:

```cpp
    using tankostream::stream::QualityScorer;
    for (auto& er : m_enriched) {
        const int q = QualityScorer::qualityScore(er.raw.title);
        const int h = QualityScorer::healthScore(er.raw.seeders);
        er.combinedScore = QualityScorer::combinedScore(q, h, wQuality, wHealth);
    }
```

- [ ] **Step 3: Sort with multi-season at top**

```cpp
    std::stable_sort(m_enriched.begin(), m_enriched.end(),
        [](const EnrichedResult& a, const EnrichedResult& b) {
            if (a.isMultiSeason() != b.isMultiSeason()) {
                return a.isMultiSeason();  // multi-season packs first
            }
            return a.combinedScore > b.combinedScore;  // higher score first
        });
```

Add `#include <algorithm>`.

- [ ] **Step 4: Render with multi-season visual highlight**

```cpp
    m_list->clear();
    for (const auto& er : m_enriched) {
        const QString suffix = er.isMultiSeason()
            ? tr("  [WHOLE SHOW]")
            : QString();
        const QString line = QStringLiteral("%1%2  ·  %3 seeders  ·  %4 MB  ·  score %5")
            .arg(er.raw.title)
            .arg(suffix)
            .arg(er.raw.seeders)
            .arg(er.raw.sizeBytes / 1'000'000)
            .arg(static_cast<int>(er.combinedScore));
        m_list->addItem(line);
    }
    m_status->setText(tr("%1 packs (sorted by quality × seeders)").arg(m_enriched.size()));
```

- [ ] **Step 5: Fix `onRowDoubleClicked` to emit using the enriched list**

```cpp
void TorrentPackPicker::onRowDoubleClicked() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_enriched.size()) return;
    emit packChosen(m_enriched[row].raw, m_imdbId, m_season);
    accept();
}
```

- [ ] **Step 6: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 7: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task D4 — TorrentPackPicker::rerankAndRender computes per-pack combinedScore via QualityScorer (weights read from QSettings theatre/qualityWeight, default 0.6), sorts with multi-season packs pinned at top + higher-score-first within each group, renders with [WHOLE SHOW] tag on multi-season rows. onRowDoubleClicked switched to enriched list. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify] | files: src/ui/pages/stream/TorrentPackPicker.cpp
```

---

### Task D5: Quality-weight settings slider in Theatre settings

**Files:**
- Modify: `src/ui/pages/stream/StreamPage.cpp` (or wherever Theatre settings UI lives — grep first)

- [ ] **Step 1: Find the existing Theatre/Stream settings panel**

Run: `grep -rn 'streamMode\|stream/.*settings\|Stream.*Settings' src/ui/pages/ src/ui/MainWindow.cpp`

Identify the existing settings panel for Stream/Theatre. If none exists, create one as a new dialog reachable from a sidebar settings entry (probably out of v1 scope; in that case, the slider lives in a generic preferences dialog under a "Theatre" tab).

- [ ] **Step 2: Add a quality-weight slider**

In the identified settings UI (or new "Theatre preferences" pane):

```cpp
    auto* qualityWeightLabel = new QLabel(tr("Sort preference for Tankorent picker"), this);
    auto* qualityWeightSlider = new QSlider(Qt::Horizontal, this);
    qualityWeightSlider->setRange(0, 100);
    qualityWeightSlider->setValue(60);  // default 0.6
    qualityWeightSlider->setTickPosition(QSlider::TicksBelow);
    qualityWeightSlider->setTickInterval(20);
    auto* qualityWeightHint = new QLabel(
        tr("← Prefer reliability  |  Prefer quality →"), this);

    QSettings settings;
    qualityWeightSlider->setValue(static_cast<int>(
        settings.value("theatre/qualityWeight", 0.6).toDouble() * 100));

    connect(qualityWeightSlider, &QSlider::valueChanged, [](int v) {
        QSettings s;
        s.setValue("theatre/qualityWeight", v / 100.0);
    });
```

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task D5 — Quality-weight slider added to Theatre settings panel (range 0-100 maps to QSettings theatre/qualityWeight 0.0-1.0, default 0.6). Caption "Prefer reliability / Prefer quality" makes the tradeoff explicit. Picker re-reads on every rerankAndRender. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/StreamPage.cpp
```

---

## Phase E — Show-view + Local files + Tankorent page (Agent 5)

### Task E1: "Download via Tankorent" button in StreamDetailView season header

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Add a slot to the header**

In `StreamDetailView.h`, add to the private slots section:

```cpp
private slots:
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: opens TorrentPackPicker with
    // the current show identity. Bound to the new "Download via Tankorent"
    // button on each season header.
    void onDownloadViaTankorentClicked(int season);
```

- [ ] **Step 2: Wire button into the season-header builder**

Find the method in `StreamDetailView.cpp` that builds each season header (search for `seasonHeader\|buildSeasonHeader\|m_seasonHeaders`). Inside that method, after the existing "Stream" button is created, add:

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: per-season download button.
    auto* tankorentBtn = new QPushButton(tr("Download via Tankorent"), seasonHeaderWidget);
    tankorentBtn->setProperty("season", seasonNumber);
    connect(tankorentBtn, &QPushButton::clicked, [this, seasonNumber]() {
        onDownloadViaTankorentClicked(seasonNumber);
    });
    seasonHeaderLayout->addWidget(tankorentBtn);
```

- [ ] **Step 3: Implement the slot**

In `StreamDetailView.cpp`:

```cpp
void StreamDetailView::onDownloadViaTankorentClicked(int season) {
    if (m_currentImdb.isEmpty()) return;

    auto* picker = new TorrentPackPicker(
        m_currentImdb,
        m_currentShowName,
        season,
        m_tankorentClient,  // injected dep — confirm member name via grep
        this
    );
    connect(picker, &TorrentPackPicker::packChosen,
            this, [this](const TorrentResult& pack,
                         const QString& imdbId, int season) {
        // Open AddTorrentDialog with imdbId+season pre-filled, then start download.
        AddTorrentDialog dlg(
            pack.title,
            pack.infoHash,
            m_defaultPaths,
            imdbId,
            season,
            this
        );
        if (dlg.exec() == QDialog::Accepted) {
            // m_tankorentClient->startDownload routes through the existing path.
            m_tankorentClient->startDownload(pack.magnetUri, dlg.config());
        }
    });
    picker->exec();
    picker->deleteLater();
}
```

Add the includes:

```cpp
#include "ui/pages/stream/TorrentPackPicker.h"
#include "ui/dialogs/AddTorrentDialog.h"
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`. If `m_currentShowName`/`m_tankorentClient`/`m_defaultPaths` don't exist as members, find the equivalent in StreamDetailView via grep — they all must exist for the existing Stream button to work; you're matching that pattern.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 5, TANKORENT_STREAM_INTEGRATION Task E1 — "Download via Tankorent" button added to each season header in StreamDetailView. Click opens TorrentPackPicker with show identity in context; on pack chosen, opens AddTorrentDialog pre-filled with imdbId+season then starts download. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp
```

---

### Task E2: Episode-tile local-file chip in show-view

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (the per-episode-tile renderer)

- [ ] **Step 1: Find the episode tile builder**

Run: `grep -n 'episodeTile\|buildEpisode\|m_episodes' src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 2: Add the chip rendering**

Inside the episode-tile builder, after the existing thumbnail/title rendering, add:

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: local-file chip when this
    // episode has been downloaded (via bulk-cohort, Tankorent, or any
    // future source that registers into StreamDownloadIndex).
    if (m_downloadIndex) {
        const auto localPath = m_downloadIndex->filePathFor(m_currentImdb, season, episode);
        if (localPath.has_value()) {
            auto* chip = new QLabel(tr("LOCAL"), episodeTileWidget);
            chip->setStyleSheet(
                "background-color: #2a4a2a; color: #aaffaa; "
                "border-radius: 2px; padding: 1px 4px; font-size: 9px;");
            chip->setProperty("tankoban_chip", true);
            episodeTileLayout->addWidget(chip, /* alignment */ Qt::AlignTop | Qt::AlignRight);
        }
    }
```

(The exact styling already follows Tankoban's gray/black/white convention per `feedback_no_color_no_emoji`; if a green-toned chip violates that convention, fall back to a thin gray border + uppercase label without color fill.)

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 5, TANKORENT_STREAM_INTEGRATION Task E2 — Episode-tile local-file chip added to StreamDetailView. Renders "LOCAL" badge in top-right corner when StreamDownloadIndex::filePathFor returns a path for the (imdbId, season, episode) triple. Color-restraint per project convention. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/StreamDetailView.cpp
```

---

### Task E3: Theatre rename — user-facing strings + settings keys

**Files:**
- Modify: `src/ui/MainWindow.cpp` (sidebar label, env var reads)
- Modify: `src/ui/pages/stream/StreamPage.cpp` (page title)
- Modify: `tools/tankoctl.cpp` (page alias)
- Possibly others — grep first

- [ ] **Step 1: Inventory user-facing "Stream" strings**

Run: `grep -rn '"Stream"\|tr("Stream' src/ui/`

Compile the list. Common spots: sidebar entry label, page title, window title segments, settings-panel heading.

- [ ] **Step 2: Rename in MainWindow sidebar**

Find the sidebar entry creation for the Stream tab — likely in `MainWindow.cpp` near sidebar setup. Change the display label:

```cpp
// before:  m_sidebar->addEntry(tr("Stream"), "stream", iconStream);
// after:
    m_sidebar->addEntry(tr("Theatre"), "stream", iconStream);
    //         ─────────────────────  user-facing label
    //                                  ─────────────  internal pageId stays "stream"
```

- [ ] **Step 3: Rename page title**

In `StreamPage.cpp` (or wherever the page title is set):

```cpp
// before:  setWindowTitle(tr("Stream"));
// after:
    setWindowTitle(tr("Theatre"));
```

- [ ] **Step 4: Env-var rename with deprecation alias**

Where the code reads `TANKOBAN_STREAM_TELEMETRY` or similar env vars, add fallback:

```cpp
// New canonical name + old-name fallback for one release.
QByteArray envVal = qgetenv("TANKOBAN_THEATRE_TELEMETRY");
if (envVal.isEmpty()) {
    envVal = qgetenv("TANKOBAN_STREAM_TELEMETRY");
}
```

Run `grep -rn 'TANKOBAN_STREAM_' src/` to find every site needing this fallback pattern.

- [ ] **Step 5: tankoctl page alias**

In `tools/tankoctl.cpp`, find the `--page` argument parser. Add alias:

```cpp
if (pageArg == QStringLiteral("theatre")) {
    pageArg = QStringLiteral("stream");  // canonical internal name
}
```

- [ ] **Step 6: Settings-key fallback**

For `QSettings` reads of `streamMode/*`, change writes to `theatreMode/*` but keep reads fallback-aware:

```cpp
QVariant readTheatreSetting(const QString& key, const QVariant& def) {
    QSettings s;
    if (s.contains(QStringLiteral("theatreMode/") + key)) {
        return s.value(QStringLiteral("theatreMode/") + key, def);
    }
    return s.value(QStringLiteral("streamMode/") + key, def);
}
```

If only a handful of `streamMode/` keys exist, inline the fallback at each call site instead of building a helper.

- [ ] **Step 7: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 8: Commit signal**

```
READY TO COMMIT — [Agent 5, TANKORENT_STREAM_INTEGRATION Task E3 — Theatre rename sweep (user-facing strings only). Sidebar label "Stream" → "Theatre". Page title rename. Env vars TANKOBAN_STREAM_* paired with TANKOBAN_THEATRE_* (new wins; old still readable for one release). tankoctl --page theatre alias. QSettings theatreMode/* with streamMode/* read fallback. Internal C++ class names stay Stream*. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify] | files: src/ui/MainWindow.cpp, src/ui/pages/stream/StreamPage.cpp, tools/tankoctl.cpp, ...(other touched files per grep)
```

---

### Task E4: Local files section in Theatre library

**Files:**
- Modify: `src/ui/pages/stream/StreamPage.cpp` (add bottom row consuming VideosScanner output)

- [ ] **Step 1: Inspect StreamPage layout structure**

Run: `grep -n 'class StreamPage\|buildLibrary\|m_libraryLayout' src/ui/pages/stream/StreamPage.cpp`

Identify where library rows are constructed.

- [ ] **Step 2: Add a Local files row**

After the last existing row creation:

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: Local files row — surfaces
    // configured root folders' top-level subdirs as folder tiles. No matching,
    // no Cinemeta; pure folder browser. Replaces the discoverability surface
    // of the removed Videos mode tab.
    auto* localFilesRow = new HorizontalScrollRow(tr("Local files"), this);
    auto* videosScanner = m_bridge->videosScanner();  // confirm exposed member
    connect(videosScanner, &VideosScanner::scanComplete,
            localFilesRow, [localFilesRow](const QList<ShowInfo>& shows) {
        localFilesRow->clear();
        for (const ShowInfo& info : shows) {
            auto* tile = new FolderTile(info.showName, info.showPath, localFilesRow);
            connect(tile, &FolderTile::clicked, [info]() {
                // Open the folder in a flat file browser.
                FlatFolderBrowser browser(info.showPath);
                browser.exec();
            });
            localFilesRow->addTile(tile);
        }
    });
    m_libraryLayout->addWidget(localFilesRow);
    videosScanner->triggerScan();
```

If `HorizontalScrollRow`/`FolderTile`/`FlatFolderBrowser` don't exist, identify the existing tile-row primitives Tankoban uses and substitute. The pattern is: subscribe to scanner finish, render N folder tiles, click → folder browser.

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 5, TANKORENT_STREAM_INTEGRATION Task E4 — Local files row added to Theatre library bottom. Subscribes to VideosScanner::scanComplete; renders top-level subdirs as folder tiles; click opens flat folder browser. Replaces discoverability surface of removed Videos mode (Task E5). Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/StreamPage.cpp
```

---

### Task E5: Remove Videos mode sidebar entry

**Files:**
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Find and remove the Videos sidebar entry**

Run: `grep -n '"Videos"\|VideosPage' src/ui/MainWindow.cpp`

Comment out (or fully remove) the sidebar entry creation for Videos. The VideosPage class stays linked (VideosScanner is still used by E4) but is no longer reachable via the sidebar.

```cpp
// before:  m_sidebar->addEntry(tr("Videos"), "videos", iconVideos);
// after:   // [Removed 2026-05-15 — VIDEO_STREAM_MERGE → TANKORENT_STREAM_INTEGRATION pivot]
            //  m_sidebar->addEntry(tr("Videos"), "videos", iconVideos);
            //  VideosScanner output now surfaces under Theatre Local files row (Task E4).
```

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`. If linker errors arise from removed-but-still-referenced bits, fix narrowly — don't expand scope.

- [ ] **Step 3: Commit signal**

```
READY TO COMMIT — [Agent 5, TANKORENT_STREAM_INTEGRATION Task E5 — Videos sidebar entry removed from MainWindow. VideosPage class remains linked (for VideosScanner reuse by Theatre Local files row); only the sidebar route is gone. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/MainWindow.cpp
```

---

### Task E6: Repurpose Tankorent page to "Direct torrent search"

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (page title + caption + scope text)

- [ ] **Step 1: Update window title and intro text**

Find the page title / intro label in `TankorentPage.cpp`:

```cpp
// before:  setWindowTitle(tr("Tankorent"));
// after:
    setWindowTitle(tr("Direct torrent search"));
```

If there's a description label or empty-state text, update it to clarify: "Direct torrent search — for content that isn't a Cinemeta show (sports, software, random downloads). To download shows by series + season, use Theatre's show-view 'Download via Tankorent' button."

- [ ] **Step 2: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 3: Commit signal**

```
READY TO COMMIT — [Agent 4B, TANKORENT_STREAM_INTEGRATION Task E6 — TankorentPage repurposed to "Direct torrent search" for non-Cinemeta content. Window title + intro caption updated. No functional change; downloads here pass empty imdbId so they fall through to Theatre Local files via existing scanner. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/TankorentPage.cpp
```

---

## Phase F — UnifiedProgressStore refactor (Agent 4)

### Task F1: TDD — UnifiedProgressStore skeleton + episode-keyed get/set

**Files:**
- Create: `src/core/stream/UnifiedProgressStore.h`
- Create: `src/core/stream/UnifiedProgressStore.cpp`
- Create: `tests/core/stream/test_unified_progress_store.cpp`
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/core/stream/test_unified_progress_store.cpp`:

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include "core/stream/UnifiedProgressStore.h"
#include "core/persistence/JsonStore.h"

class UnifiedProgressStoreTest : public ::testing::Test {
protected:
    QTemporaryDir m_tempDir;
    std::unique_ptr<JsonStore> m_store;
    std::unique_ptr<UnifiedProgressStore> m_progress;

    void SetUp() override {
        m_store = std::make_unique<JsonStore>(m_tempDir.path());
        m_progress = std::make_unique<UnifiedProgressStore>(m_store.get());
    }
};

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_SetAndResume) {
    m_progress->setProgress("tt0141842", 6, 3, 1234.5, 3600.0);
    EXPECT_DOUBLE_EQ(1234.5, m_progress->resumePositionFor("tt0141842", 6, 3));
}

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_UnsetReturnsZero) {
    EXPECT_DOUBLE_EQ(0.0, m_progress->resumePositionFor("tt0141842", 6, 3));
}

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_Overwrite) {
    m_progress->setProgress("tt0141842", 6, 3, 100.0, 3600.0);
    m_progress->setProgress("tt0141842", 6, 3, 250.0, 3600.0);
    EXPECT_DOUBLE_EQ(250.0, m_progress->resumePositionFor("tt0141842", 6, 3));
}

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_ScrubBackLowers) {
    m_progress->setProgress("tt0141842", 6, 3, 1500.0, 3600.0);
    m_progress->setProgress("tt0141842", 6, 3, 30.0, 3600.0);  // user scrubbed back
    EXPECT_DOUBLE_EQ(30.0, m_progress->resumePositionFor("tt0141842", 6, 3));
}
```

- [ ] **Step 2: Write the header**

Create `src/core/stream/UnifiedProgressStore.h`:

```cpp
#pragma once

// TANKORENT_STREAM_INTEGRATION 2026-05-15 — single canonical progress store.
// Replaces per-mode progress tracking (Stream-mode tracker + Videos-mode
// tracker) with one keyed-by-identity store. Per locked decision: progress
// is identity-bound to (imdbId, season, episode) for shows, or canonicalPath
// for un-bound content. Any playback (Tankorent-downloaded file, Stream auto-
// source, Local files section) reads/writes the same value.

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>

class JsonStore;

class UnifiedProgressStore : public QObject
{
    Q_OBJECT
public:
    explicit UnifiedProgressStore(JsonStore* store, QObject* parent = nullptr);

    // Episode-bound progress (Tankorent-downloaded + Stream auto-source).
    void   setProgress(const QString& imdbId, int season, int episode,
                       double positionSec, double durationSec);
    double resumePositionFor(const QString& imdbId, int season, int episode) const;

    // Path-keyed progress (Local files section + non-show direct-search).
    void   setProgressByPath(const QString& canonicalPath,
                             double positionSec, double durationSec);
    double resumePositionForPath(const QString& canonicalPath) const;

signals:
    void progressChanged();

private:
    void load();
    void save();
    static QString episodeKey(const QString& imdbId, int season, int episode);

    JsonStore* m_store;
    mutable QMutex m_mutex;

    struct Entry { double positionSec; double durationSec; };
    QHash<QString, Entry> m_byEpisode;
    QHash<QString, Entry> m_byPath;

    static constexpr const char* FILENAME = "unified_progress.json";
};
```

- [ ] **Step 3: Run test — expect failure (link error)**

```
cmake --build out --target tankoban_tests
```

Expected: link error — `UnifiedProgressStore` not defined.

- [ ] **Step 4: Write minimal implementation**

Create `src/core/stream/UnifiedProgressStore.cpp`:

```cpp
#include "core/stream/UnifiedProgressStore.h"
#include "core/persistence/JsonStore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMutexLocker>

UnifiedProgressStore::UnifiedProgressStore(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store) {
    load();
}

QString UnifiedProgressStore::episodeKey(const QString& imdbId, int season, int episode) {
    return QStringLiteral("%1:%2:%3").arg(imdbId).arg(season).arg(episode);
}

void UnifiedProgressStore::setProgress(const QString& imdbId, int season, int episode,
                                       double positionSec, double durationSec) {
    const QString k = episodeKey(imdbId, season, episode);
    {
        QMutexLocker lock(&m_mutex);
        m_byEpisode[k] = {positionSec, durationSec};
    }
    save();
    emit progressChanged();
}

double UnifiedProgressStore::resumePositionFor(const QString& imdbId, int season, int episode) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_byEpisode.constFind(episodeKey(imdbId, season, episode));
    if (it == m_byEpisode.constEnd()) return 0.0;
    return it->positionSec;
}

void UnifiedProgressStore::setProgressByPath(const QString& canonicalPath,
                                             double positionSec, double durationSec) {
    {
        QMutexLocker lock(&m_mutex);
        m_byPath[canonicalPath] = {positionSec, durationSec};
    }
    save();
    emit progressChanged();
}

double UnifiedProgressStore::resumePositionForPath(const QString& canonicalPath) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_byPath.constFind(canonicalPath);
    if (it == m_byPath.constEnd()) return 0.0;
    return it->positionSec;
}

void UnifiedProgressStore::load() {
    QMutexLocker lock(&m_mutex);
    if (!m_store) return;
    const QJsonObject root = m_store->read(FILENAME).toObject();
    const QJsonObject byEp = root.value("byEpisode").toObject();
    for (auto it = byEp.constBegin(); it != byEp.constEnd(); ++it) {
        const QJsonObject e = it.value().toObject();
        m_byEpisode[it.key()] = {
            e.value("pos").toDouble(),
            e.value("dur").toDouble()
        };
    }
    const QJsonObject byPath = root.value("byPath").toObject();
    for (auto it = byPath.constBegin(); it != byPath.constEnd(); ++it) {
        const QJsonObject e = it.value().toObject();
        m_byPath[it.key()] = {
            e.value("pos").toDouble(),
            e.value("dur").toDouble()
        };
    }
}

void UnifiedProgressStore::save() {
    QMutexLocker lock(&m_mutex);
    if (!m_store) return;
    QJsonObject byEp, byPath;
    for (auto it = m_byEpisode.constBegin(); it != m_byEpisode.constEnd(); ++it) {
        byEp[it.key()] = QJsonObject{{"pos", it->positionSec}, {"dur", it->durationSec}};
    }
    for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
        byPath[it.key()] = QJsonObject{{"pos", it->positionSec}, {"dur", it->durationSec}};
    }
    m_store->write(FILENAME, QJsonObject{{"byEpisode", byEp}, {"byPath", byPath}});
}
```

- [ ] **Step 5: Add to CMakeLists.txt (main + tests)**

Main `CMakeLists.txt`: add `src/core/stream/UnifiedProgressStore.cpp` to sources.

`tests/CMakeLists.txt`: add `tests/core/stream/test_unified_progress_store.cpp` to `tankoban_tests`.

- [ ] **Step 6: Run tests**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "UnifiedProgressStoreTest.EpisodeKeyed"
```

Expected: ALL PASS (4 tests).

- [ ] **Step 7: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task F1 — UnifiedProgressStore class created with episode-keyed get/set + JsonStore persistence. 4 test cases GREEN (set/resume, unset returns 0, overwrite, scrub-back lowers). Path-keyed methods stubbed and ready for Task F2 tests.] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion] | files: src/core/stream/UnifiedProgressStore.h, src/core/stream/UnifiedProgressStore.cpp, tests/core/stream/test_unified_progress_store.cpp, CMakeLists.txt, tests/CMakeLists.txt
```

---

### Task F2: TDD — path-keyed get/set

**Files:**
- Modify: `tests/core/stream/test_unified_progress_store.cpp`

- [ ] **Step 1: Append path-keyed tests**

```cpp
TEST_F(UnifiedProgressStoreTest, PathKeyed_SetAndResume) {
    m_progress->setProgressByPath("D:\\Sports\\Game.mkv", 500.0, 7200.0);
    EXPECT_DOUBLE_EQ(500.0, m_progress->resumePositionForPath("D:\\Sports\\Game.mkv"));
}

TEST_F(UnifiedProgressStoreTest, PathKeyed_UnsetReturnsZero) {
    EXPECT_DOUBLE_EQ(0.0, m_progress->resumePositionForPath("D:\\Nope\\X.mkv"));
}

TEST_F(UnifiedProgressStoreTest, EpisodeAndPath_NoCollision) {
    m_progress->setProgress("tt0141842", 6, 3, 100.0, 3600.0);
    m_progress->setProgressByPath("D:\\Sports\\Game.mkv", 500.0, 7200.0);
    EXPECT_DOUBLE_EQ(100.0, m_progress->resumePositionFor("tt0141842", 6, 3));
    EXPECT_DOUBLE_EQ(500.0, m_progress->resumePositionForPath("D:\\Sports\\Game.mkv"));
}
```

- [ ] **Step 2: Run tests — these should PASS without further implementation (F1 already wrote both)**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "UnifiedProgressStoreTest"
```

Expected: ALL 7 PASS.

- [ ] **Step 3: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task F2 — UnifiedProgressStore path-keyed methods covered by 3 new test cases (set/resume by path, unset returns 0, episode and path independent — no collision). Full UnifiedProgressStoreTest suite GREEN (7 tests).] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development] | files: tests/core/stream/test_unified_progress_store.cpp
```

---

### Task F3: Wire UnifiedProgressStore into the playback path

**Files:**
- Modify: wherever progress is currently written (search to find existing per-mode trackers)

- [ ] **Step 1: Find current progress write paths**

Run:

```
grep -rn 'setLastPlayed\|playbackPosition\|saveProgress' src/core/ src/ui/
```

Identify the two extant trackers (one for Stream mode playback, one for Videos mode if it had its own). They're the targets for unification.

- [ ] **Step 2: Replace per-mode writes with UnifiedProgressStore calls**

At each playback-progress write site, look up the current `imdbId`/season/episode for the playing item:
- If the playback was initiated from a Stream show-view tile → those identifiers are known
- If the playback was from the Local files row → use `setProgressByPath(canonicalPath, ...)`

Sketch (apply to each found site):

```cpp
// before — example:
//   m_streamLibrary->setLastPlayed(imdbId, season, episode, posSec);

// after:
    if (m_unifiedProgress) {
        m_unifiedProgress->setProgress(imdbId, season, episode, posSec, durSec);
    }
```

For raw-folder playbacks:

```cpp
    if (m_unifiedProgress) {
        m_unifiedProgress->setProgressByPath(canonicalPath, posSec, durSec);
    }
```

- [ ] **Step 3: Replace per-mode resume reads**

Similarly for resume reads — before opening a player session for an episode:

```cpp
const double resume = m_unifiedProgress
    ? m_unifiedProgress->resumePositionFor(imdbId, season, episode)
    : 0.0;
```

- [ ] **Step 4: Drop legacy per-mode store usage**

Remove the now-unused per-mode store member variables and load/save paths. Keep code minimal.

- [ ] **Step 5: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`. Iterate if compile fails on unrelated callers — the existing per-mode-store API surface determines how invasive this is.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task F3 — Playback progress write/read paths unified through UnifiedProgressStore. Per-mode trackers (Stream-mode + Videos-mode legacy) removed; both code paths now read/write the same identity-bound store per locked decision 1. Local files section playbacks use setProgressByPath; show-view playbacks use setProgress(imdb,S,E). Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify] | files: ...(grep-discovered call sites)
```

---

## Phase H — Movie support (decision 10)

The Phase A–F tasks cover series end-to-end. Movies (single-file content with no S/E structure) need three extra hooks. This phase adds them.

### Task H1: Movie-fallback registration path in `publishTankorentItemsForTorrent`

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp` (extend the new method from Task A4)
- Modify: `src/core/stream/StreamDownloadIndex.h` + `.cpp` (relax "series-only" constraint)
- Modify: `tests/core/stream/test_stream_download_index_dedup.cpp` (add movie-registration test)

- [ ] **Step 1: Relax StreamDownloadIndex to accept movie entries**

The existing comment at `StreamDownloadIndex.h:28` says `"series" (movies excluded in v1 per spec §3 P5)`. That excludes the old `STREAM_DOWNLOADED_LIBRARY` v1 — this arc supersedes it. Update the comment:

```cpp
    struct Entry {
        QString imdbId;
        QString type;  // "series" (season+episode populated) OR "movie" (both = 0)
        ...
    };
```

Add a new public method declaration:

```cpp
    void registerMovie(const QString& imdbId,
                       const QString& canonicalPath,
                       const QString& sourceGroupId,
                       qint64 fileSizeBytes);

    std::optional<QString> filePathForMovie(const QString& imdbId) const;
```

- [ ] **Step 2: Implement registerMovie + filePathForMovie**

In `StreamDownloadIndex.cpp`, implement as a wrapper around the existing storage. Movies use season=0, episode=0, type="movie":

```cpp
void StreamDownloadIndex::registerMovie(const QString& imdbId,
                                        const QString& canonicalPath,
                                        const QString& sourceGroupId,
                                        qint64 fileSizeBytes) {
    // Internally stored with season=0, episode=0; the by-episode key
    // "imdb:0:0" disambiguates from any series episode.
    registerEpisode(imdbId, 0, 0, canonicalPath, sourceGroupId, fileSizeBytes);

    // Patch the Entry's type field to "movie" (registerEpisode defaults to "series").
    QMutexLocker lock(&m_mutex);
    const QString canonKey = computeCanonicalKey(canonicalPath);
    auto it = m_byPath.find(canonKey);
    if (it != m_byPath.end()) {
        it->type = QStringLiteral("movie");
    }
}

std::optional<QString> StreamDownloadIndex::filePathForMovie(const QString& imdbId) const {
    return filePathFor(imdbId, 0, 0);
}
```

- [ ] **Step 3: Extend `publishTankorentItemsForTorrent` with movie fallback**

In `TorrentClient::publishTankorentItemsForTorrent` (the method from Task A4), after the per-file loop, before the final `qDebug()`:

```cpp
    // Movie fallback: if zero episodes were registered, treat this as a movie
    // download. Pick the largest video file (by reported size) and register it
    // as a movie entry. Matches decision 10 (movies in v1 with full parity).
    if (registeredCount == 0 && !files.isEmpty()) {
        const QJsonObject* largest = nullptr;
        qint64 largestSize = 0;
        QJsonObject largestCopy;
        for (const QJsonValue& v : files) {
            const QJsonObject f = v.toObject();
            const QString name = f.value(QStringLiteral("path")).toString().toLower();
            // Skip non-video files (subs, nfo, etc.)
            if (!(name.endsWith(".mkv") || name.endsWith(".mp4") ||
                  name.endsWith(".webm") || name.endsWith(".m4v") ||
                  name.endsWith(".avi"))) {
                continue;
            }
            const qint64 sz = f.value(QStringLiteral("size")).toVariant().toLongLong();
            if (sz > largestSize) {
                largestSize = sz;
                largestCopy = f;
            }
        }
        if (largestSize > 0) {
            const QString relPath = largestCopy.value(QStringLiteral("path")).toString();
            const QString absPath = QDir(savePath).absoluteFilePath(relPath);
            m_streamDownloadIndex->registerMovie(
                imdbId,
                absPath,
                sourceGroupId,
                largestSize
            );
            registeredCount = 1;
        }
    }
```

- [ ] **Step 4: Add a TDD test for movie registration**

Append to `tests/core/stream/test_stream_download_index_dedup.cpp`:

```cpp
TEST_F(StreamDownloadIndexDedupTest, MovieRegistration) {
    m_index->registerMovie("tt1375666",
                           "/x/Inception.2010.1080p.BluRay.x264.mkv",
                           "tankorent:abc", 4'000'000'000);

    auto p = m_index->filePathForMovie("tt1375666");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ("/x/Inception.2010.1080p.BluRay.x264.mkv", *p);
}

TEST_F(StreamDownloadIndexDedupTest, MovieDedup_HigherQualityWins) {
    m_index->registerMovie("tt1375666",
                           "/x/Inception.2010.720p.WEBRip.mkv",
                           "tankorent:a", 1'500'000'000);
    m_index->registerMovie("tt1375666",
                           "/x/Inception.2010.2160p.BluRay.mkv",
                           "tankorent:b", 30'000'000'000);

    auto p = m_index->filePathForMovie("tt1375666");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ("/x/Inception.2010.2160p.BluRay.mkv", *p);
}
```

- [ ] **Step 5: Build and verify**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "StreamDownloadIndexDedupTest.Movie"
```

Expected: ALL PASS (2 new tests; existing 3 still GREEN).

Then `build_check.bat` for main app → `BUILD OK`.

- [ ] **Step 6: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task H1 — Movie support across StreamDownloadIndex (new registerMovie + filePathForMovie wrappers around season=0 episode=0 storage) and publishTankorentItemsForTorrent (largest-video-file fallback when zero episodes registered). Highest-quality-wins dedup applies to movies via the existing C1 path. 2 new test cases GREEN; full StreamDownloadIndexDedupTest suite GREEN (5 tests). Main app compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion] | files: src/core/stream/StreamDownloadIndex.h, src/core/stream/StreamDownloadIndex.cpp, src/core/torrent/TorrentClient.cpp, tests/core/stream/test_stream_download_index_dedup.cpp
```

---

### Task H2: "Download via Tankorent" button on movie show-views

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Locate the movie-view layout**

Run: `grep -n 'isMovie\|type == "movie"\|m_type' src/ui/pages/stream/StreamDetailView.cpp`

Find where the view conditionally renders movie layout (single poster + play button) vs series layout (season headers + episode grid).

- [ ] **Step 2: Add the button to the movie layout**

In the movie-view layout block, after the existing Play / Stream button row, add:

```cpp
    // TANKORENT_STREAM_INTEGRATION 2026-05-15: Download via Tankorent for movies.
    // Calls TorrentPackPicker with season=0 (movie sentinel); publishTankorent
    // movie-fallback path registers via StreamDownloadIndex::registerMovie.
    auto* tankorentBtn = new QPushButton(tr("Download via Tankorent"), this);
    connect(tankorentBtn, &QPushButton::clicked, [this]() {
        onDownloadViaTankorentClicked(0);  // season=0 = movie / whole-show sentinel
    });
    movieActionRow->addWidget(tankorentBtn);
```

- [ ] **Step 3: Update episode-tile chip rendering for movies (Task E2 was series-only)**

In the movie-view rendering, after the play button row, add a similar LOCAL chip if movie is downloaded:

```cpp
    if (m_downloadIndex) {
        const auto localPath = m_downloadIndex->filePathForMovie(m_currentImdb);
        if (localPath.has_value()) {
            auto* chip = new QLabel(tr("LOCAL"), this);
            chip->setStyleSheet(
                "background-color: #2a4a2a; color: #aaffaa; "
                "border-radius: 2px; padding: 1px 4px; font-size: 9px;");
            movieActionRow->addWidget(chip);
        }
    }
```

- [ ] **Step 4: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 5, TANKORENT_STREAM_INTEGRATION Task H2 — Movie show-views in StreamDetailView get the "Download via Tankorent" button (season=0 routing) and LOCAL chip when registered. Series show-views unchanged. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/ui/pages/stream/StreamDetailView.cpp
```

---

## Phase G — Integration smoke + Hemanth visual verify

### Task G1: Wire StreamDownloadIndex into TorrentClient at construction

**Files:**
- Modify: wherever TorrentClient is constructed (likely `main.cpp` or a service factory)

- [ ] **Step 1: Find the TorrentClient construction site**

Run: `grep -rn 'new TorrentClient\|TorrentClient(.*)' src/`

- [ ] **Step 2: Inject StreamDownloadIndex**

Confirm StreamDownloadIndex is constructed in scope (it must be — bulk-cohort path already uses it). Ensure TorrentClient holds the pointer:

```cpp
    m_torrentClient->setStreamDownloadIndex(m_streamDownloadIndex);
```

(If a setter doesn't exist, the bulk-cohort path must use a constructor injection or a different name — match the existing pattern.)

- [ ] **Step 3: Compile-verify**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 4: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task G1 — StreamDownloadIndex injected into TorrentClient at construction (matching existing bulk-cohort wiring). The new publishTankorentItemsForTorrent from Task A4 now has its dependency satisfied. Compile-only verify GREEN.] | Skills invoked: [/superpowers:executing-plans, /build-verify] | files: src/main.cpp (or service factory site)
```

---

### Task G2: Integration smoke recipe

**Files:**
- Create: `docs/superpowers/plans/2026-05-15-tankorent-stream-integration-smoke.md` (smoke recipe doc; companion to this plan)

- [ ] **Step 1: Write the smoke recipe**

Create the smoke doc with step-by-step verification:

```markdown
# TANKORENT_STREAM_INTEGRATION smoke recipe

## Preconditions
- `build_and_run.bat` produces a working binary
- Theatre tab visible in sidebar (no Videos tab)
- Settings → Theatre → Quality preference slider visible
- Tankorent indexers reachable (at least one)

## Recipe
1. `taskkill //F //IM Tankoban.exe` then `build_and_run.bat` (Hemanth via `build_and_run.bat`, but agent verifies via tankoctl).
2. `out\tankoctl.exe ping` — expect `{"schema":"tankoban.dev.v1","ok":true}`.
3. `out\tankoctl.exe open-page theatre` — verify activePageId = "stream" (internal) but UI shows "Theatre".
4. Search "Big Buck Bunny" in Theatre top search → confirm Cinemeta returns the matching entry (or substitute a legal test torrent that Cinemeta knows).
5. Click the show → confirm season header has both "Stream" and "Download via Tankorent" buttons.
6. Click "Download via Tankorent" on Season 1 → TorrentPackPicker opens.
7. Wait ~30s for indexer fan-out to return results.
8. Confirm: multi-season "Complete" packs appear at top if any; rest sorted by combined quality×seeders.
9. Pick the smallest test pack (to keep download time short). AddTorrentDialog opens; imdbId+season pre-filled (verify via `tankoctl get-state`).
10. Click Download.
11. Wait for completion (`tankoctl get-state` polling on the torrent infoHash; expect state="completed").
12. After completion: `tankoctl get-videos --imdb <imdbId>` (or equivalent state introspection) should show the episodes registered.
13. Back in Theatre show-view, episode tiles should now display the "LOCAL" chip.
14. Click an episode → player opens with local file (verify via `tankoctl get-player` showing local path, not a magnet URL).

## Cleanup
- `powershell -NoProfile -File scripts/stop-tankoban.ps1` per Rule 17.
```

- [ ] **Step 2: Execute the smoke recipe (agent-driven)**

Per Rule 15, the agent runs the smoke themselves (not Hemanth). Use the MCP servers + tankoctl per CLAUDE.md "Which MCP, when" block. Claim MCP LOCK in chat.md (Rule 19) before starting; release after.

- [ ] **Step 3: Document results**

Append findings to `agents/audits/tankorent_stream_integration_smoke_2026-05-15.md`. Include screenshots of the show-view with LOCAL chips visible.

- [ ] **Step 4: Hemanth visual verify ask**

Post to chat.md:

```
@Hemanth — TANKORENT_STREAM_INTEGRATION v1 ready for your visual verify.

Recipe (one screen, two clicks):
1. Open Tankoban via build_and_run.bat.
2. Search "Sopranos" in Theatre.
3. Click the Sopranos show tile.
4. Click "Download via Tankorent" on the Season 6 header.
5. Pick the 1080p BluRay pack.
6. Wait for download.
7. Confirm: Sopranos S6 episodes show the LOCAL chip in show-view, and clicking S6E3 plays the local file.

Things to look out for: missing chips, wrong file binding (S6E3 plays S6E4 by mistake), pack picker sorting feeling off, settings slider not affecting sort.
```

- [ ] **Step 5: Commit signal**

```
READY TO COMMIT — [Agent 4, TANKORENT_STREAM_INTEGRATION Task G2 — Integration smoke recipe authored at docs/superpowers/plans/2026-05-15-tankorent-stream-integration-smoke.md. Agent-driven smoke executed per Rule 15 with results at agents/audits/tankorent_stream_integration_smoke_2026-05-15.md. Hemanth visual verify ask posted to chat.md.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion] | files: docs/superpowers/plans/2026-05-15-tankorent-stream-integration-smoke.md, agents/audits/tankorent_stream_integration_smoke_2026-05-15.md, agents/chat.md
```

---

## Closing notes for the executor

- **Order:** Phases A → B → C are mostly independent (different files); A can run alongside B. Phase C depends on B (uses QualityScorer). Phase D depends on B + C. Phase E can start at any time after E3 (rename) which is independent. Phase F can run alongside any other phase. Phase G is the gate.
- **Parallelization:** If executing via subagent-driven-development, dispatch A in parallel with B in parallel with F. Then dispatch C after B, D after C, E in parallel with everything. G is sequential at the end.
- **Per Rule 14:** technical decisions inside each task (variable names, exact regex flavor, signal connection types) are your call as executor. Strategic decisions (would you change the data flow? would you pick a different scoring weight?) — ASK first; don't decide silently.
- **Per Rule 17:** any agent-driven smoke ends with `scripts/stop-tankoban.ps1`.
- **Per Rule 19:** any desktop MCP work claims/releases `MCP LOCK` in chat.md.
- **Trust the spec.** Decisions ratified in `docs/superpowers/specs/2026-05-15-tankorent-stream-integration-design.md` are not up for re-negotiation in implementation. If the spec is silent or ambiguous on a detail, that's an Agent-4-call.

This plan is complete. Estimated 3-4 elapsed wakes if parallelized; ~6 agent-wakes total work.
