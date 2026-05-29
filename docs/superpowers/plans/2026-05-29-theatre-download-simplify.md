# Theatre Download Simplify — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collapse Theatre's download UX to two auto-pick actions — *Download* (episode/movie) and *Download Season* — using Torrentio only, with a silent best-source picker, and delete the picker/sidebar/multi-mode complexity.

**Architecture:** A new pure-logic `AutoSourcePicker` (filter → rank → size-guardrail) chooses one source silently. `StreamPage` orchestrates: fetch Torrentio sources (`StreamAggregator`), pick (`AutoSourcePicker`), start the download (`TorrentClient::startDownload`) stamped with a `streamGroupId` so it stays out of Tankorent, and register progress in `StreamDownloadIndex` (which already drives the `EpisodeTile` 3-state chip). Season = fan-out over the existing `TransferQueue`. Final phase deletes the obsolete picker UI.

**Tech Stack:** C++17, Qt 6, libtorrent (via `TorrentClient`/`TorrentEngine`), GoogleTest (`tankoban_tests`). Build via `build_check.bat`; structural smoke via `out\tankoctl.exe`.

**Source spec:** [docs/superpowers/specs/2026-05-29-theatre-download-simplify-design.md](../specs/2026-05-29-theatre-download-simplify-design.md)

---

## Discipline notes (read before starting)

- **TDD applies to pure-logic only.** `AutoSourcePicker` + its CAM/bitrate helpers get real GoogleTest coverage (Phase 1, Task 1). UI/IPC/libtorrent-integration wiring ships under **code-walk verification + build + tankoctl smoke** per CLAUDE.md — those tasks specify exact files/signals/connect-lines and a build+smoke gate, not unit tests.
- **One change, one build, one check** (`feedback_one_fix_per_rebuild`). Build in an isolated lane (`TANKOBAN_BUILD_LANE=agent4`/worktree), never shared `out/` (`feedback_no_concurrent_builds_same_out_dir`).
- **Kill `Tankoban.exe` before every rebuild** (Rule 1).
- **Phase order is deliberate:** the working single-episode path lands first (Phase 1); deletion is last (Phase 4), only after the new path is proven.

---

## File Structure

**Create:**
- `src/core/stream/AutoSourcePicker.h` — pure-logic source selector: `SourceCandidate` POD + `AutoSourcePicker::pick()`.
- `src/core/stream/AutoSourcePicker.cpp` — implementation.
- `tests/core/stream/test_auto_source_picker.cpp` — GoogleTest coverage.

**Modify:**
- `src/ui/pages/StreamPage.cpp` — single-episode + season download orchestration; map `StreamPickerChoice`→`SourceCandidate`; stamp `streamGroupId`; register pending in index. (`beginPlayOrDownload` ~4030, `onSingleEpisodeDownloadRequested` ~3074, `onDirectDownloadRequested` ~3152, `onSeasonDownloadRequested` ~3062.)
- `src/ui/pages/StreamPage.h` — new private helpers/members (pending-source-fetch state).
- `CMakeLists.txt` — add `AutoSourcePicker.cpp` to the app sources and `test_auto_source_picker.cpp` to `tankoban_tests`.
- `src/ui/pages/TankorentPage.cpp:~2201` — (Phase 2) belt-and-suspenders: also skip `imdbId`-bound torrents.

**Delete (Phase 4):**
- `src/ui/pages/stream/TorrentPackPicker.{h,cpp}`, `PackListItem.{h,cpp}`
- `src/ui/pages/stream/TheatreDownloadPanel.{h,cpp}`
- Theatre-side wiring of `StreamSourceList` source-card direct-download / the Tankorent-in-Theatre sidebar.

---

## Phase 1 — Single-episode auto-pick download, end to end

Goal of phase: click *Download* on one episode/movie → silent best-1080p pick → download starts → tile shows progress → completes → plays from disk.

### Task 1: `AutoSourcePicker` pure-logic selector (TDD)

**Files:**
- Create: `src/core/stream/AutoSourcePicker.h`
- Create: `src/core/stream/AutoSourcePicker.cpp`
- Test: `tests/core/stream/test_auto_source_picker.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
// src/core/stream/AutoSourcePicker.h
#pragma once
#include <QList>
#include <QString>
#include <optional>

namespace tankostream::stream {

// One Torrentio candidate reduced to the fields auto-pick needs.
// Pure data; no Qt-UI dependency (mirrors QualityScorer's layering).
struct SourceCandidate {
    QString title;            // release title (CAM detection + tiebreak)
    int     seeders = 0;
    qint64  sizeBytes = 0;
    int     qualitySort = 0;  // 5=2160p 4=1440p 3=1080p 2=720p 1=480p 0=unknown
};

// Silent best-source selection for Theatre's one-tap download.
// Filter (1080p / seeders>0 / not-CAM) -> rank by seeders -> size guardrail
// only for the weakly-seeded tail. Returns the index into `candidates` of
// the chosen source, or std::nullopt for "no source found".
class AutoSourcePicker {
public:
    // runtimeMinutes <= 0 means "unknown" -> size guardrail skipped.
    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
                                   int runtimeMinutes = 0);

    static bool   isCamRip(const QString& title);
    static double impliedBitrateMbps(qint64 sizeBytes, int runtimeMinutes);

    static constexpr int    kRequiredQualitySort = 3;     // 1080p only
    static constexpr int    kLowSeedThreshold     = 30;    // below = tail
    static constexpr double kMinBitrateMbps       = 1.5;   // re-encode floor
    static constexpr double kMaxBitrateMbps       = 20.0;  // remux/4K ceiling
};

}  // namespace tankostream::stream
```

- [ ] **Step 2: Write the failing test**

```cpp
// tests/core/stream/test_auto_source_picker.cpp
#include <gtest/gtest.h>
#include "core/stream/AutoSourcePicker.h"

using tankostream::stream::AutoSourcePicker;
using tankostream::stream::SourceCandidate;

static SourceCandidate c(const QString& title, int seeders, qint64 sizeBytes, int qualitySort) {
    SourceCandidate s; s.title = title; s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
}

TEST(AutoSourcePicker, PicksHighestSeeded1080p) {
    QList<SourceCandidate> v {
        c("One Piece S01E01 [SubsPlease] 1080p", 1200, 1400000000LL, 3),
        c("One Piece S01E01 1080p WEB-DL",          300, 1500000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, ExcludesNon1080p) {
    QList<SourceCandidate> v {
        c("Show S01E01 720p",  900, 700000000LL, 2),
        c("Show S01E01 2160p", 800, 9000000000LL, 5),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesZeroSeeders) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 0, 1400000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesCamRips) {
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie 2024 1080p CAM"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.TELESYNC.1080p"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.HDCAM.1080p"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("Movie.2024.1080p.WEB-DL"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("GUTS.S01E01.1080p"));  // no false-positive on 'TS'

    QList<SourceCandidate> v { c("Movie 2024 1080p CAM", 500, 3000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 120).has_value());
}

TEST(AutoSourcePicker, WellSeededIgnoresSize) {
    // 1200 seeders, absurdly large file -> seeders negate size, still picked.
    QList<SourceCandidate> v { c("Show S01E01 1080p", 1200, 30000000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, LowSeedTailDropsReencode) {
    // 4 seeders, 180MB/24min -> ~1 Mbps -> below floor -> dropped -> none.
    QList<SourceCandidate> v { c("Show S01E01 1080p", 4, 180000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailDropsRemux) {
    // 6 seeders, 13GB/24min -> way over ceiling -> dropped -> none.
    QList<SourceCandidate> v { c("Show S01E01 1080p", 6, 13000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailKeepsSaneSized) {
    // 5 seeders, 1.4GB/24min -> ~7.8 Mbps -> in band -> picked.
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 1400000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, UnknownRuntimeSkipsSizeGuardrail) {
    // runtime 0 -> can't compute bitrate -> low-seed candidate kept on seeders.
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 180000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 0);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, EmptyListReturnsNone) {
    EXPECT_FALSE(AutoSourcePicker::pick({}, 24).has_value());
}
```

- [ ] **Step 3: Add both new files to CMake**

In `CMakeLists.txt`, add `src/core/stream/AutoSourcePicker.cpp` to the app target's source list (next to `src/core/stream/QualityScorer.cpp`), and add `tests/core/stream/test_auto_source_picker.cpp` to the `tankoban_tests` sources (next to `tests/core/stream/test_quality_scorer.cpp`). Grep both anchor strings first to land in the right list:

Run: `grep -n "QualityScorer.cpp\|test_quality_scorer.cpp" CMakeLists.txt`

- [ ] **Step 4: Run the test to verify it fails**

Run: `cmake --build out --target tankoban_tests` then `cd out && ctest -R AutoSourcePicker --output-on-failure`
Expected: FAIL — link error / `AutoSourcePicker` undefined (no .cpp yet).

- [ ] **Step 5: Write the implementation**

```cpp
// src/core/stream/AutoSourcePicker.cpp
#include "core/stream/AutoSourcePicker.h"
#include <QRegularExpression>
#include <algorithm>

namespace tankostream::stream {

bool AutoSourcePicker::isCamRip(const QString& title) {
    // Camcorder-class tags, bounded so ordinary words don't match.
    // "TS"/"TC" intentionally NOT matched (false positives like "GUTS");
    // can be added with care later (spec §8.2).
    static const QRegularExpression re(
        QStringLiteral("(^|[^a-z0-9])(cam|camrip|hdcam|telesync|telecine|hdts)([^a-z0-9]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(title).hasMatch();
}

double AutoSourcePicker::impliedBitrateMbps(qint64 sizeBytes, int runtimeMinutes) {
    if (sizeBytes <= 0 || runtimeMinutes <= 0) return 0.0;
    return (static_cast<double>(sizeBytes) * 8.0)
         / (static_cast<double>(runtimeMinutes) * 60.0) / 1.0e6;
}

std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidates,
                                          int runtimeMinutes) {
    // Step 1 — hard filters.
    QList<int> survivors;
    for (int i = 0; i < candidates.size(); ++i) {
        const SourceCandidate& c = candidates.at(i);
        if (c.qualitySort != kRequiredQualitySort) continue;  // 1080p only
        if (c.seeders <= 0) continue;                          // dead torrent
        if (isCamRip(c.title)) continue;                       // camcorder rip
        survivors.append(i);
    }
    if (survivors.isEmpty()) return std::nullopt;              // no source found

    auto bySeedersDesc = [&](int a, int b) {
        return candidates.at(a).seeders > candidates.at(b).seeders;
    };
    std::sort(survivors.begin(), survivors.end(), bySeedersDesc);

    // Step 2 — if the healthiest survivor is well-seeded, seeders decide;
    // size is never consulted (seeders negate size doubt).
    if (candidates.at(survivors.first()).seeders >= kLowSeedThreshold)
        return survivors.first();

    // Step 3 — weakly-seeded tail: drop implausible implied-bitrate releases
    // (only when runtime is known), then take the best-seeded of the rest.
    QList<int> sane;
    for (int idx : survivors) {
        const double mbps = impliedBitrateMbps(candidates.at(idx).sizeBytes, runtimeMinutes);
        if (mbps == 0.0) { sane.append(idx); continue; }       // unknown -> keep
        if (mbps < kMinBitrateMbps || mbps > kMaxBitrateMbps) continue;  // junk
        sane.append(idx);
    }
    if (sane.isEmpty()) return std::nullopt;
    std::sort(sane.begin(), sane.end(), bySeedersDesc);
    return sane.first();
}

}  // namespace tankostream::stream
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build out --target tankoban_tests` then `cd out && ctest -R AutoSourcePicker --output-on-failure`
Expected: PASS (11 assertions/tests green).

- [ ] **Step 7: Commit**

```bash
git add src/core/stream/AutoSourcePicker.h src/core/stream/AutoSourcePicker.cpp tests/core/stream/test_auto_source_picker.cpp CMakeLists.txt
git commit -m "[Agent 4 (Opus), THEATRE_DOWNLOAD_SIMPLIFY]: P1.T1 — AutoSourcePicker pure-logic selector (filter 1080p/seeders/no-CAM, rank by seeders, low-seed-tail size guardrail) + GoogleTest"
```

### Task 2: Wire single-episode/movie *Download* → fetch → auto-pick → start

**Files:**
- Modify: `src/ui/pages/StreamPage.cpp` (`beginPlayOrDownload` ~4030, `onSingleEpisodeDownloadRequested` ~3074, `onDirectDownloadRequested` ~3152)
- Modify: `src/ui/pages/StreamPage.h`

Verification = **code-walk + build + tankoctl smoke** (UI/IPC orchestration, not unit-testable).

- [ ] **Step 1: Add a pending-fetch helper to StreamPage.** When *Download* is clicked for a not-owned episode/movie, kick a Torrentio fetch and remember the request context (imdbId/season/episode/runtime) so the async `streamsReady` handler can finish the pick. Reuse the existing id-format builder at `StreamPage.cpp:2379-2385` (`imdbId:season:episode`, or `kitsu:<id>:<ep>` for anime, or bare `imdbId` for movies) and `StreamAggregator::load(StreamLoadRequest)`.

Add to `StreamPage.h` (private):
```cpp
struct PendingAutoDownload {
    bool    active = false;
    QString imdbId;
    QString mediaType;   // "series" | "movie"
    int     season = 0;
    int     episode = 0;
    int     runtimeMinutes = 0;   // from MetaItem if known, else 0
};
PendingAutoDownload m_pendingAuto;
void startAutoDownload(const QString& imdbId, const QString& mediaType,
                       int season, int episode);   // fetch + arm m_pendingAuto
void finishAutoDownloadPick();                     // called from streamsReady
```

- [ ] **Step 2: Implement `startAutoDownload`.** Build the `StreamLoadRequest` exactly as the existing play path does (same id/type construction), arm `m_pendingAuto`, set the episode's tile to a pending/"searching" state via `m_streamDownloadIndex->registerPendingEpisode(imdbId, season, episode, /*canonicalPath*/QString(), /*sourceGroupId*/streamGroupId, 0)` (movies: `registerPendingMovie`). `streamGroupId` = `QStringLiteral("theatre:%1").arg(imdbId)` — this is the mode stamp (Phase 2 relies on it). Then call `m_streamAggregator->load(req)`.

- [ ] **Step 3: Route the existing entry points through it.** In `beginPlayOrDownload` (not-owned branch, ~4049-4067), replace the `onSingleEpisodeDownloadRequested(...)` / picker routing with `startAutoDownload(imdbId, mediaType, season, episode)`. `onSingleEpisodeDownloadRequested` becomes a thin forwarder to `startAutoDownload`. Leave `playLocalFileFromStreamRequested` (owned branch) untouched.

- [ ] **Step 4: Implement `finishAutoDownloadPick` and connect it.** Connect `StreamAggregator::streamsReady` (signature `void streamsReady(const QList<tankostream::addon::Stream>&, const QHash<QString,QString>&)`) to a slot that — when `m_pendingAuto.active` — converts the streams to `StreamPickerChoice` via the existing `buildPickerChoices(...)` path, maps each to a `SourceCandidate{displayTitle, seeders, sizeBytes, qualitySort}`, calls `AutoSourcePicker::pick(candidates, m_pendingAuto.runtimeMinutes)`, and:
  - **on a value:** build `AddTorrentConfig` exactly like `onDirectDownloadRequested` (`category="videos"`, `destinationPath=defaultPaths()["videos"]`, `contentLayout="original"`, `imdbId`, `season`, `magnetUri=chosen.magnetUri`) **plus `config.streamGroupId = QStringLiteral("theatre:%1").arg(imdbId)`**, resolve the infoHash (`chosen.infoHash` or `m_torrentClient->resolveMetadata(chosen.magnetUri)`), and call `m_torrentClient->startDownload(hash, config)`.
  - **on `nullopt`:** set the tile to a "no source found" state (Task 4) and clear `m_pendingAuto`.

- [ ] **Step 5: Build.**

Run: `taskkill //F //IM Tankoban.exe 2>NUL & build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Smoke (tankoctl).** Launch via `build_and_run.bat`; open a show; trigger a single-episode download; confirm a torrent is added and bound to the show.

Run: `out\tankoctl.exe stream-get-downloads` (expect the episode present, progressing) and `out\tankoctl.exe stream-get-torrents`.
Expected: the episode appears as an active download with a `theatre:<imdbId>` group; tile shows a progress %.

- [ ] **Step 7: Commit.**

```bash
git add src/ui/pages/StreamPage.cpp src/ui/pages/StreamPage.h
git commit -m "[Agent 4 (Opus), THEATRE_DOWNLOAD_SIMPLIFY]: P1.T2 — single-episode/movie Download routes through Torrentio fetch + AutoSourcePicker + startDownload (stamped streamGroupId=theatre:<imdbId>)"
```

### Task 3: Confirm + complete progress wiring (download → tile → completion)

**Files:**
- Modify: `src/ui/pages/StreamPage.cpp` (signal wiring near the StreamPage ctor torrent-client connects, ~332-360)

- [ ] **Step 1: Verify existing progress wiring covers single-episode.** Grep for who calls `StreamDownloadIndex::updateEpisodeProgress` and who handles `TorrentEngine::torrentProgress` / `TorrentClient::torrentCompleted` for stream downloads.

Run: `grep -rn "updateEpisodeProgress\|torrentCompleted\|torrentProgress" src/ui/pages/StreamPage.cpp src/core/stream/`

- [ ] **Step 2: If single-episode is not covered, wire it.** On `TorrentClient::torrentUpdated(infoHash)` / the progress signal, look up the episode this infoHash belongs to (track `infoHash → {imdbId,season,episode}` in a small `QHash` populated in Task 2 Step 4) and call `m_streamDownloadIndex->updateEpisodeProgress(imdbId, season, episode, pct)`. On `TorrentClient::torrentCompleted(infoHash)`, resolve the on-disk file and call `registerEpisode(...)` (final path → flips to Complete). The index already emits `entryStateChanged`, which `EpisodeTile` already consumes — no tile code needed.

- [ ] **Step 3: Build.**

Run: `taskkill //F //IM Tankoban.exe 2>NUL & build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Smoke — full lifecycle.** `build_and_run.bat`; download one small episode; watch the tile go pending → % → Downloaded; click it; it plays from disk (assumes Agent 0's `ffmpeg_sidecar` deploy fix is in).

Run: `out\tankoctl.exe stream-get-downloads` during, and `out\tankoctl.exe library-* ` / play after.
Expected: tile reaches Downloaded; play opens the local file.

- [ ] **Step 5: Commit.**

```bash
git add src/ui/pages/StreamPage.cpp
git commit -m "[Agent 4 (Opus), THEATRE_DOWNLOAD_SIMPLIFY]: P1.T3 — wire single-episode torrent progress/completion into StreamDownloadIndex (drives the 3-state tile)"
```

### Task 4: "No source found" tile state

**Files:**
- Modify: `src/ui/pages/StreamPage.cpp` (the `nullopt` branch from Task 2 Step 4)
- Modify: `src/ui/pages/stream/EpisodeTile.{h,cpp}` only if a distinct visual is wanted

- [ ] **Step 1: Decide the surface.** Simplest: on `nullopt`, evict the pending index entry (`evictByImdb`/path) and show a transient toast "No 1080p source found" via the existing toast path StreamPage uses in `onDirectDownloadRequested`. (No new tile state needed for v1; spec §5 only requires the user not be left in a fake "downloading" state.)

- [ ] **Step 2: Implement** in the `nullopt` branch: clear the pending entry so the tile returns to "not downloaded", and post the toast.

- [ ] **Step 3: Build + smoke.** Force a no-source case (obscure title) and confirm the tile resets and a toast appears.

Run: `taskkill //F //IM Tankoban.exe 2>NUL & build_check.bat` → `BUILD OK`.

- [ ] **Step 4: Commit.**

```bash
git add src/ui/pages/StreamPage.cpp
git commit -m "[Agent 4 (Opus), THEATRE_DOWNLOAD_SIMPLIFY]: P1.T4 — no-source-found resets tile + toast instead of fake downloading state"
```

---

## Phase 2 — Mode separation (Theatre downloads stay out of Tankorent)

### Task 5: Guarantee Theatre downloads never appear in the Tankorent page

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp:~2201-2223`

- [ ] **Step 1: Confirm the stamp.** Task 2 stamps every Theatre download with `streamGroupId = "theatre:<imdbId>"`. `TankorentPage` already does `if (!t.streamGroupId.isEmpty()) continue;` — so stamped Theatre downloads are already excluded. Verify by reading the loop.

- [ ] **Step 2: Belt-and-suspenders.** If `TorrentInfo` carries `imdbId`, also `continue` when `!t.imdbId.isEmpty()` (any show-bound torrent is not Tankorent's). Confirm field exists first:

Run: `grep -n "struct TorrentInfo" -A40 src/core/torrent/TorrentClient.h`
If `imdbId` exists on `TorrentInfo`, add the extra skip; if not, the `streamGroupId` stamp alone is sufficient — document that in a code comment and skip this step.

- [ ] **Step 3: Build + smoke.** Download an episode in Theatre; open the Tankorent page; confirm it is **absent** there but present in Theatre's Downloads.

Run: `out\tankoctl.exe stream-get-downloads` (present) and visually/`tankoctl` confirm Tankorent list excludes it.

- [ ] **Step 4: Commit.**

```bash
git add src/ui/pages/TankorentPage.cpp
git commit -m "[Agent 4 (Opus), THEATRE_DOWNLOAD_SIMPLIFY]: P2.T5 — Tankorent page excludes show-bound Theatre downloads (streamGroupId stamp + imdbId guard)"
```

---

## Phase 3 — Download Season (per-episode fan-out over the existing queue)

### Task 6: *Download Season* fans out to per-episode auto-picks via `TransferQueue`

**Files:**
- Modify: `src/ui/pages/StreamPage.cpp` (`onSeasonDownloadRequested` ~3062, `triggerBulkSeasonDownload` ~2966)
- Reuse: `src/core/queue/TransferQueue.h`, `TorrentClient::addMagnetForShow(magnet, category, dest, imdbId, season)` / `startDownload` queue-deferral (already integrated).

- [ ] **Step 1: Enumerate the season's episodes.** In `onSeasonDownloadRequested(season)`, get the episode list from `m_detailView->episodesForSeason(season)` (same call `triggerBulkSeasonDownload` uses, ~2977).

- [ ] **Step 2: Fan out.** For each episode, run the **same** `startAutoDownload(imdbId, "series", season, episode)` path from Phase 1 — but route the chosen magnet through `m_torrentClient->addMagnetForShow(magnet, "videos", dest, imdbId, season)` so the existing `TransferQueue` lane (keyed by show identity) serializes them one-at-a-time, in order, while other shows' lanes run in parallel. The queue + deferral logic already exists (`TorrentClient::setTransferQueue`, `startDownload` staging in `m_pendingStartConfigs`); do not rebuild it.

- [ ] **Step 3: Source fetches are async — sequence them.** Since each episode needs its own Torrentio fetch, queue the *fetches* too (one `StreamLoadRequest` at a time, advancing on each `streamsReady`) so `m_pendingAuto` isn't clobbered. Extend `m_pendingAuto` into a small FIFO (`QQueue<PendingAutoDownload>`); `finishAutoDownloadPick` pops the next and issues its fetch. (Per-show download serialization is still `TransferQueue`'s job; this FIFO only serializes the *metadata fetches* to keep the single `streamsReady` channel unambiguous.)

- [ ] **Step 4: Build.**

Run: `taskkill //F //IM Tankoban.exe 2>NUL & build_check.bat` → `BUILD OK`.

- [ ] **Step 5: Smoke — the spec's checkpoint.** Queue two shows' seasons. Both shows progress in parallel; within each show, episodes download one at a time in order; E1 becomes playable while E2 downloads.

Run: `out\tankoctl.exe stream-get-downloads` repeatedly; confirm one active episode per show, queued episodes behind it.

- [ ] **Step 6: Commit.**

```bash
git add src/ui/pages/StreamPage.cpp src/ui/pages/StreamPage.h
git commit -m "[Agent 4 (Opus), THEATRE_DOWNLOAD_SIMPLIFY]: P3.T6 — Download Season fans out per-episode auto-picks through the existing TransferQueue (sequential within show, parallel across)"
```

---

## Phase 4 — Deletion pass (remove the complexity)

Only after Phases 1–3 are smoke-proven. **Grep every caller before deleting.**

### Task 7: Remove the source picker, Tankorent-in-Theatre sidebar, and multi-mode buttons

**Files:**
- Delete: `src/ui/pages/stream/TorrentPackPicker.{h,cpp}`, `PackListItem.{h,cpp}`, `TheatreDownloadPanel.{h,cpp}`
- Modify: `src/ui/pages/StreamPage.{h,cpp}` (remove panel/picker members, ctor wiring ~815-900, the `theatreDownloadRequested`/`directDownloadRequested`/`addToTankorentRequested` slots), `src/ui/pages/stream/StreamDetailView.*` (remove multi-season/full-series buttons + the Tankorent-source sidebar + the source-card direct-download path), `CMakeLists.txt` (drop deleted sources).

- [ ] **Step 1: Grep callers of each symbol to be deleted.**

Run: `grep -rn "TorrentPackPicker\|TheatreDownloadPanel\|PackListItem\|onAddToTankorentRequested\|UnifiedPackSearchEngine" src/`
Record every hit; each must be removed or rerouted in this task.

- [ ] **Step 2: Remove the Theatre-side wiring** in `StreamPage` ctor (the `theatreDownloadRequested` lambda ~844, `directDownloadRequested`/`onAddToTankorentRequested` connects ~832-862) and the panel construction (~815-824). Keep `singleEpisodeDownloadRequested` / `seasonDownloadRequested` (Phases 1+3).

- [ ] **Step 3: Remove the UI affordances** in `StreamDetailView` — multi-season + full-series buttons, the Tankorent-source sidebar section, and the source-card right-click direct-download. Leave per-episode *Download* and the season *Download Season* button.

- [ ] **Step 4: Delete the files + CMake entries.**

```bash
git rm src/ui/pages/stream/TorrentPackPicker.h src/ui/pages/stream/TorrentPackPicker.cpp \
       src/ui/pages/stream/PackListItem.h src/ui/pages/stream/PackListItem.cpp \
       src/ui/pages/stream/TheatreDownloadPanel.h src/ui/pages/stream/TheatreDownloadPanel.cpp
```
Then remove their lines from `CMakeLists.txt`.

- [ ] **Step 5: Clean-from-scratch build (gov-v11 gate).**

Run (isolated lane): `taskkill //F //IM Tankoban.exe 2>NUL` then a clean configure+build in `out_agent4` / a worktree.
Expected: `BUILD OK` with zero references to deleted symbols.

- [ ] **Step 6: Smoke — the calm surface.** Open a show: only per-episode *Download* + *Download Season* exist; no picker, no Tankorent sidebar, no multi-season/full-series buttons.

- [ ] **Step 7: Commit.**

```bash
git add -A
git commit -m "[Agent 4 (Opus), THEATRE_DOWNLOAD_SIMPLIFY]: P4.T7 — delete TorrentPackPicker/PackListItem/TheatreDownloadPanel + Tankorent-in-Theatre sidebar + multi-season/full-series buttons; clean-from-scratch BUILD OK"
```

---

## Self-Review (against the spec)

**Spec coverage:**
- §2/§3/§5 auto-pick (filter→rank→size-guardrail, 1080p-or-nothing, no-CAM, seeders-primary, size-as-low-seed-tail) → **Task 1** (full TDD).
- §3 single-episode/movie one-tap Download → **Tasks 2+3**.
- §4 three-state tile → reuses existing `EpisodeTile`+`StreamDownloadIndex`; fed by **Tasks 2+3**.
- §3 "no source found" → **Task 4**.
- §6/§7 mode separation (Theatre downloads not in Tankorent) → **Task 5** (streamGroupId stamp + filter).
- §3 Download Season (per-episode fan-out, sequential within / parallel across) → **Task 6** (reuses `TransferQueue`).
- §6 deletions (picker, Tankorent-sidebar, multi-season/full-series, orphaned panel) → **Task 7**.
- §5 "title parsing is the real reliability lever" → CAM/quality parse is unit-tested in **Task 1**; the `StreamPickerChoice` quality/seeders/size parse is the existing `buildPickerChoices` path consumed in Task 2.
- §8 deferred (audio/dub, group-rep/codec/HDR, 720p fallback, Tankorent packs, streaming, season-pack torrents) → **explicitly not in any task** (correct).

**Placeholder scan:** Task 1 ships full code + tests. Wiring tasks (2,3,5,6,7) cite exact files/methods/signals and use build+smoke gates per the codebase's TDD-pure-logic-only discipline (declared up top) — not placeholders, but the honest verification model for Qt/IPC glue.

**Type consistency:** `SourceCandidate{title,seeders,sizeBytes,qualitySort}` defined Task 1, mapped from `StreamPickerChoice` (fields `displayTitle/seeders/sizeBytes/qualitySort` per `StreamSourceChoice.h`) in Task 2. `AutoSourcePicker::pick(QList<SourceCandidate>, int)→optional<int>` used consistently. `streamGroupId="theatre:<imdbId>"` stamp defined Task 2, relied on Task 5. `TransferQueue`/`addMagnetForShow` reused as-is (no signature invention).

**Open verification carried into execution (not gaps):** whether `TorrentInfo` exposes `imdbId` (Task 5 Step 2 greps before acting); whether single-episode progress wiring already exists (Task 3 Step 1 greps before adding). Both are guarded with a grep-first step, not assumptions.

---

## EXECUTION ADDENDUM (discovered 2026-05-29 during P1.T2/T3 grounding) — READ BEFORE T3/T5

**Status:** P1.T1 DONE (`88bd12a`+`8819d5c`). P1.T2 committed (`263c2f2`) but needs the streamGroupId correction below in T3.

**Key discovery — the progress/completion lifecycle is ALREADY WIRED in `TorrentClient`, gated on an EMPTY `streamGroupId`:**
- `TorrentClient::onMetadataReady` (~3340-3358) registers a Pending episode/movie when `row->imdbId` is set.
- `TorrentClient::onPieceFinished` (3361-3416) parses the torrent's filenames via `StreamPackParser::parsePack(files, imdbId, season)` to derive each episode, then calls `m_streamDownloadIndex->updateEpisodeProgress(imdbId, season, episode, pct)`. **This drives the `EpisodeTile` 3-state chip already** (tile subscribes to `entryStateChanged`).
- `onTorrentFinished` → `registerEpisode`/`registerMovie` (Complete + real path) — same parse-based episode derivation.
- **THE GATE (TorrentClient.cpp:3373-3379):** these fire only when `row->imdbId` is non-empty **AND `row->streamGroupId` is EMPTY** (non-empty streamGroupId → early-return, "bulk-cohort path handles its own progress").

**Consequence — T2's `streamGroupId="theatre:<imdbId>"` stamp is WRONG.** It suppresses the very progress tracking we need. **T3 must change `finishAutoDownloadPick` to leave `config.streamGroupId` EMPTY** (matching `onDirectDownloadRequested`). With imdbId set + streamGroupId empty, the whole lifecycle (pending → progress → complete → tile) works for free — T3 is then mostly **verify via build + live smoke** (now end-to-end testable: A0's `ffmpeg_sidecar` deploy fix `b7acc97` is in, so completed downloads play).

**Re-home mode separation to `imdbId` (this becomes the PRIMARY mechanism, not belt-and-suspenders):** since Theatre downloads now carry empty streamGroupId, the existing `TankorentPage` filter (`if(!t.streamGroupId.isEmpty()) continue;`) no longer hides them. **P2.T5 must add `if(!t.imdbId.isEmpty()) continue;` to TankorentPage's render loop** (show-bound torrent = not Tankorent's). To avoid a regression window, fold this TankorentPage filter into the SAME build as the T3 stamp-removal (i.e., do T3 + T5 together).

**Also fold into T3 (from T2 code-quality review of `263c2f2`):**
- **I1:** `startAutoDownload` must `disconnect`+`connect` the `streamError` signal (mirror `onPlayRequested` ~2388-2399) with an auto-path handler that clears `m_pendingAuto.active` + shows the error. Currently absent.
- **M1:** use `QLatin1String("movie")` consistently in `startAutoDownload` (one plain `"movie"` literal slipped in).
- **M2/M4:** `m_autoDownloadByHash` is now **dead weight** — TorrentClient derives the episode via `parsePack`, so StreamPage doesn't need the hash→episode map. **Remove it** (and its insert in `finishAutoDownloadPick`) in T3.
- **M3:** n/a once the streamGroupId stamp is removed.

**Net effect:** T3 = (1) empty the streamGroupId stamp, (2) remove `m_autoDownloadByHash`, (3) add the I1 streamError handler + M1 fix, (4) add the TankorentPage `imdbId` filter (P2.T5), (5) one full build, (6) live end-to-end smoke: click Download on an episode → tile goes pending→%→Downloaded → click → plays from disk; confirm it does NOT appear in the Tankorent page. This collapses most of T3+T4+T5 into one verified build.
