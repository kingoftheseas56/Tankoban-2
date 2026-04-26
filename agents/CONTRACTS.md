# Cross-Agent Contracts
<!-- contracts-version: contracts-v2 -->

Append-only. Never delete entries — mark `[DEPRECATED]` if superseded. These are the interface specs that caused real build breaks when violated.

Read this file when `agents/VERSIONS.md` shows your `Contracts seen` pin in your STATUS block is behind the live version. Bump your pin in the same edit as your re-read.

---

## Progress Save Payloads

All readers save progress via `CoreBridge::saveProgress(domain, key, data)`.

### Comics (Agent 1 → CoreBridge → Agent 5)
```cpp
domain: "comics"
key:    SHA1(absoluteFilePath).toHex().left(20)

QJsonObject data;
data["page"]      = currentPage;       // int, 0-based
data["pageCount"] = totalPages;        // int
data["path"]      = m_cbzPath;         // QString, absolute file path — REQUIRED for continue strip
```

### Books (Agent 2 → CoreBridge → Agent 5)
```cpp
domain: "books"
key:    SHA1(absoluteFilePath).toHex().left(20)

QJsonObject data;
data["chapter"]        = currentChapter;   // int, 0-based
data["chapterCount"]   = totalChapters;    // int
data["scrollFraction"] = scrollFraction;   // double 0.0–1.0
data["percent"]        = percentRead;      // double 0.0–100.0
data["finished"]       = isFinished;       // bool
data["path"]           = filePath;         // QString, absolute — REQUIRED for continue strip
data["bookmarks"]      = bookmarksArray;   // QJsonArray of chapter indices
```

### Videos (Agent 3 → CoreBridge → Agent 5)
```cpp
domain: "videos"
key:    SHA1((absoluteFilePath + "::" + fileSize + "::" + lastModifiedMs).toUtf8()).toHex()
        // full hex string — NOT .left(20)

QJsonObject data;
data["positionSec"] = positionSec;    // double
data["durationSec"] = durationSec;   // double
data["path"]        = currentFilePath; // QString, absolute — REQUIRED for continue strip
```

The `path` field is non-negotiable in all three domains. Without it, Agent 5's continue strips cannot map a progress hash back to a file.

---

## Thumbnail Cache Key Format

Used by LibraryScanner (Agent 5) when generating per-file cover thumbnails. Any agent that needs to look up a cover thumbnail must use this format:

```cpp
// Per-file cover (used by continue strips, SeriesView, BookSeriesView)
QString key = QCryptographicHash::hash(
    (absoluteFilePath + "::" + fileSize + "::" + lastModifiedMs).toUtf8(),
    QCryptographicHash::Sha1
).toHex().left(20);
// Cache path: QStandardPaths::AppLocalDataLocation + "/data/thumbs/" + key + ".jpg"
```

Thumbnail dimensions: 240x369 px (ratio 0.65 — matches groundwork `cover_ratio`).

---

## Constructor Signatures (Breaking Changes History)

These signatures caused real `C2512` linker errors when callers used the wrong form. Reference before calling.

### SeriesView (Agent 5)
```cpp
// Header: src/ui/pages/SeriesView.h
SeriesView(CoreBridge* bridge, QWidget* parent = nullptr);
// showSeries() signature:
void showSeries(const QStringList& cbzPaths, const QString& seriesName,
                const QString& coverThumbPath = QString());
// Signals:
void backRequested();
void issueSelected(const QString& cbzPath, const QStringList& seriesCbzList,
                   const QString& seriesName);
```

### ShowView (Agent 5)
```cpp
// Header: src/ui/pages/ShowView.h
ShowView(CoreBridge* bridge, QWidget* parent = nullptr);
// showFolder() signature:
void showFolder(const QString& folderPath, const QString& coverThumbPath = QString());
// Signals:
void backRequested();
void episodeSelected(const QString& filePath);
```

### TankorentPage (Agent 4)
```cpp
// Header: src/ui/pages/TankorentPage.h
TankorentPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent = nullptr);
```

### SourcesPage (Agent 4)
```cpp
// Header: src/ui/pages/SourcesPage.h
SourcesPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent = nullptr);
```

### BookSeriesView (Agent 2)
```cpp
// Header: src/ui/pages/BookSeriesView.h
// showSeries() signature (optional coverThumbPath added by Agent 5):
void showSeries(const QStringList& filePaths, const QString& seriesName,
                const QString& coverThumbPath = QString());
```

---

## Cross-Agent Signals

Signals that cross subsystem boundaries — both sides must agree on the signature.

### VideosPage → MainWindow (Agent 5 → Agent 3)
```cpp
// Declared in VideosPage.h
signals:
    void playVideo(const QString& filePath);
// Connected in MainWindow.cpp to VideoPlayer::openVideo()
```

### ComicsPage → MainWindow (Agent 1 → Agent 0)
```cpp
// Declared in ComicsPage.h
signals:
    void fullscreenRequested(bool enter);
// Connected in MainWindow.cpp (additive, Agent 1 wired it)
```

### BooksPage → MainWindow (Agent 2 → Agent 0)
```cpp
// Declared in BooksPage.h
signals:
    void openBook(const QString& filePath);
// Connected in MainWindow.cpp to BookReader::openBook()
```

### TorrentClient → LibraryScanner (Agent 4 → Agent 5)
When a torrent download completes, Agent 4 should emit `rootFoldersChanged` so Agent 5's scanners auto-trigger. This signal is owned by CoreBridge. Agent 4 calls `m_bridge->emitRootFoldersChanged()` (or equivalent) on torrent completion. Agent 5 scanners listen to it.

---

## Shared File Touch Rules

These files require coordination before editing. See GOVERNANCE.md for the announce-before-touch rule.

| File | Rule |
|------|------|
| `CMakeLists.txt` | Post exact lines added in chat.md. No silent additions. |
| `src/ui/MainWindow.h` | Additive only. No removal of existing members. |
| `src/ui/MainWindow.cpp` | Additive only. Announce before editing. |
| `resources/resources.qrc` | Additive only. |

---

## Build Verification Rule

Two tiers — main app build is honor-system (agents do NOT run it from bash), sidecar build is agent-runnable (amended 2026-04-16 per Hemanth).

### Main app build — agents must NOT run from bash

The Windows MSVC build environment (`vcvarsall.bat`, Ninja, `cl.exe`) does not work reliably from the bash shell agents run in. Attempting to self-verify causes: zombie `cl.exe`/`ninja.exe` processes, 30+ minute hangs, false failures, and wasted session time.

**Rule for main app:**
- Write your code changes
- Post your completion summary in chat.md
- Stop. Do NOT run `cmake --build out`, `build_and_run.bat`, `build2.bat`, or any MSVC/Ninja command from bash.
- Hemanth runs `build_and_run.bat` natively to verify.

### Sidecar build — agents MAY run themselves (amended 2026-04-16)

The sidecar build uses MinGW + CMake via `native_sidecar/build.ps1` (and `native_sidecar/build_qrhi.bat` for the Qt RHI variant). MinGW from bash is reliable — no vcvarsall/cl.exe dependency, no zombie-process failure class. Sidecar builds are on the critical path for Agent 3's PLAYER_LIFECYCLE + PLAYER_PERF work (stop_ack handshake, GPU overlay upload, etc.) and blocking on Hemanth-initiated rebuilds creates a wait gate that isn't worth the safety margin.

**Rule for sidecar:**
- Agents MAY invoke `powershell -File native_sidecar/build.ps1` or `native_sidecar/build_qrhi.bat` from bash to verify their own sidecar-touching batches.
- Capture the last ~30 lines of output in the batch's chat.md ship post (or note "BUILD_EXIT=0" if clean).
- On failure: do NOT retry blindly — read the error, diagnose, fix, single-rebuild to confirm. The rule is still "ship code, build once to verify, post summary." Not "build-break-rebuild-loop."
- Agents may also use the `/build-verify sidecar` slash command if they prefer — it wraps the same invocation with tail-capture + taskkill hygiene.

### Who builds what

- **Hemanth**: main app + all smoke testing.
- **Any agent**: sidecar, when their batch touched `native_sidecar/**`.
- **No one from bash**: main app / Ninja / `cl.exe`. Honor-system.

If a build break is discovered post-commit, Agent 0 routes it back to the responsible agent.

---

## Build Configuration

- **Build type:** Release (required when libtorrent is enabled — it is MSVC Release-built)
- **libtorrent guard:** Wrapped in `if(CMAKE_BUILD_TYPE STREQUAL "Release")` — Debug builds skip it cleanly
- **Build directory:** `out/` is the canonical build. `out2/` and `out_test/` are for experiments only.
- **Qt modules in use:** `Qt6::Core`, `Qt6::Widgets`, `Qt6::Gui`, `Qt6::Network`, `Qt6::Svg`, `Qt6::OpenGL`, `Qt6::OpenGLWidgets`, `Qt6::GuiPrivate` (for QRhiWidget)

---

## Skill Provenance in RTCs (added 2026-04-25, contracts-v3)

Per Rule 11 in `agents/GOVERNANCE.md`. Contract owner: Agent 0 (coordination). All agents are consumers + producers.

### What

Every non-trivial RTC line in `agents/chat.md` carries a `Skills invoked: [...]` field listing the slash-skills the authoring agent actually invoked during the work the RTC commits.

### Why

The brotherhood's 21-skill discipline (per `CLAUDE.md` § Required Skills & Protocols) is honor-system today. Empirically (Agent 7 audit `skill_discipline_audit_2026-04-25.md`): only 1 of 116 code-touch RTCs explicitly named `/build-verify`; only 2 of 168 debug-shaped RTCs named `/superpowers:systematic-debugging`. Without provenance, the system cannot distinguish "skill skipped" from "skill ran but unrecorded." This contract adds provenance.

### Format

```
READY TO COMMIT - [Agent N, Tag]: <message> | Skills invoked: [/skill1, /skill2, /skill3] | files: <paths>
```

- ASCII delimiters per Rule 16 (` | ` between sections; commas inside the list).
- Each entry must start with `/` — that's the grep anchor for the Phase 4 hook + commit-sweeper.
- Order does not matter; deduplicate if a skill fired multiple times.
- Trivial RTCs (per § Trivial vs Non-trivial below) MAY omit the field entirely — the hook will not nag.
- Field placement is **between** the message body and `| files:` (which stays terminal so commit-sweeper's regex anchor stays stable).

### Trivial vs Non-trivial

A non-trivial RTC is any RTC matching ANY of:
- ≥1 file under `src/` or `native_sidecar/src/` in the `files:` list
- ≥30 lines changed cumulative across all listed files (use `git diff --shortstat` against working tree to evaluate)

Trivial RTCs are everything else: doc-only edits (`*.md`, `agents/*.md`, `*_TODO.md`), governance-only (`agents/GOVERNANCE.md`, `agents/CONTRACTS.md`, `agents/VERSIONS.md`), STATUS pivots, single-line edits, agent-state churn, archive moves.

### Minimum-expected list

For non-trivial RTCs, the field SHOULD include at least:
- `/superpowers:verification-before-completion` (every non-trivial RTC, no exception — this is the evidence-before-assertion check)
- `/simplify` (whenever the diff has non-trivial edits; usually the same threshold as "non-trivial RTC")
- `/build-verify` IF `src/` or `native_sidecar/src/` touched
- `/security-review` IF stream / torrent / sidecar / network / user-input paths touched
- `/superpowers:requesting-code-review` (recommended, not required — self-review primer)
- `/superpowers:systematic-debugging` IF the work was bug-shaped (test failure, unexpected behavior, log-grep, smoke iteration)

Beyond the minimum: list whatever else fired. Honest under-listing is better than dishonest padding.

### Enforcement

- **Phase 3 (this contract):** documentation only. The field is now defined as required for non-trivial RTCs; no automated check yet.
- **Phase 4** (`SKILL_DISCIPLINE_FIX_TODO` next phase): pre-RTC checker hook reads the pending RTC text on `Stop`/equivalent, scans for the field, and prints a nag warning if missing on a non-trivial RTC. Nag-only first 30 days; promote to block iff compliance plateaus below 80% under telemetry.
- **commit-sweeper:** parses + preserves the field on sweep. If a non-trivial RTC arrives without the field, sweep continues but the final report counts it under "non-trivial RTCs missing skill provenance" for telemetry visibility.

### Cross-platform note

Codex (Agent 7) does not have the same `Skill` tool surface as Claude Code. Codex authors include skill names in chat.md prose / RTC bodies as plain text — the field is text-only at the contract level, so platform mismatch is invisible to the parser. Codex RTCs are graded under the same threshold; if Codex-authored work touches `src/` ≥1 file, the field is required.

### Versioning

contracts-v3 introduces this section. Future amendments (e.g. promote-to-block, threshold tweaks, additional minimum-expected entries) bump contracts-v4+.
