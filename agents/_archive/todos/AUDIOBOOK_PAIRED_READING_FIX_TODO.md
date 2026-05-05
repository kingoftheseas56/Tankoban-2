# AUDIOBOOK_PAIRED_READING_FIX_TODO — Audiobooks as a paired-reading surface

**Author:** Agent 2 (Book Reader), 2026-04-22 (second authoring pass — first pass was retracted the same day after scope drift)
**Shape:** 14-section ratified template (per `feedback_fix_todo_authoring_shape.md`)
**Supersedes:** the earlier `AUDIOBOOK_PLAYBACK_FIX_TODO.md` that was reverted 2026-04-22 at Hemanth direction. This TODO carries the correct spec.

---

## Context

Earlier this session I authored `AUDIOBOOK_PLAYBACK_FIX_TODO.md` + shipped Phase 1.1 + Phase 1.5/1.6/1.7 adding a separate "Audiobooks" top-nav mode with wrapper-as-series grouping + volume-picker view. Hemanth reverted it ("you ruined everything. audiobooks are not supposed to be a seperate mode"). Full revert shipped + verified 2026-04-22 14:09; tree back to pre-session state, 2026-04-21 `AUDIOBOOK_FOLDER_DETECTION_FIX` preserved.

Then Hemanth showed me his live Tankoban-Max-master install at `C:\Users\Suprabha\Downloads\Tankoban-Max-master\Tankoban-Max-master\` and spelled out the actual spec. The key insight I had missed twice: **audiobooks in Tankoban are never listened to standalone. They exist only as a paired-reading companion to the book reader.** That reframes every scope decision below.

**The actual feature:** a user opens a book in BookReader, uses the reader's "Audio" sidebar tab to pick a previously-scanned audiobook, and listens along while reading. User controls page-turning + audiobook transport manually — no auto-sync. When the reader closes, both book position + audiobook state save together; next open restores both.

---

## 1. Problem Statement

The audiobook library-side work (scanner, tiles, detail view) + reader-side work (sidebar Audio tab, embedded HTML5 Audio transport, combined progress save) together close the "I want to read the book while the audiobook narrates" workflow. Today Tankoban 2 has scanner-side discovery only (shipped 2026-04-21); every other piece is missing.

## 2. Reference Alignment

### Tankoban-Max master — what we MATCH 1:1

| Concern | Max location | Our target |
|---|---|---|
| Top nav | `src/state/mode_router.js:5` → `{comics, videos, books, sources}` | Same 4 modes (plus our Stream) — audiobooks NOT a sibling mode |
| Books tab layout | `src/index.html:257-358` — Continue Reading + SERIES + AUDIOBOOKS all stacked in one scroll view | Audiobook strip stays inside Books tab (existing) |
| Audiobook detail view on tile click | `src/domains/books/audiobook_detail.js` → chapter-list view with cover + "N chapters · HH:MM:SS" + "In-reader only" badge + per-row {#, chapter, duration, progress} table | `AudiobookDetailView` native Qt view with identical shape |
| Reader sidebar Audio tab | `src/index.html:570-625` + `src/domains/books/reader/reader_audiobook_pairing.js` + `src/domains/books/reader/reader_audiobook.js` | Inject HTML/CSS/JS Audio tab into Foliate reader shell; `HTMLAudioElement` for playback |
| Persistence shape | `audiobook_progress.json` per-audiobook + `audiobook_pairings.json` per-book | Same two files (bridge methods already shipped in `BookBridge`) |
| Playback engine | Native `HTMLAudioElement` | Same — lives inside the reader's WebEngine context |

### Tankoban-Max — what we DIVERGE from (explicit overrides)

| Concern | Max | Us | Rationale |
|---|---|---|---|
| Audiobook scanner | Flat-leaf — one tile per folder-with-direct-audio. `Stormlight Archive/{5 leaves}/` would render as 5 tiles. | **Wrapper-flatten** — wrapper folder whose subdirs each have direct audio emits ONE tile with natural-sorted chapters concatenated across all subdirs. | Hemanth 2026-04-22: "the folder I downloaded" is one audiobook even if it has sub-folders internally. |
| Standalone audiobook player | Max has a full-screen overlay player + separate `apps/audiobook-app/` window | **None.** Audiobooks only play from inside the BookReader. | Hemanth 2026-04-22: "audiobooks in tankoban are never supposed to be listened to alone." |
| Continue Listening strip | Max has `booksAbContinuePanel` at `src/index.html:341-348` | **None.** | Hemanth 2026-04-22: the paired-reading workflow doesn't need a standalone resume surface. Book Continue Reading is sufficient — it resumes the book + its paired audiobook atomically. |
| Auto-sync on page turn | Max's `syncAudiobookToCurrentReaderChapter()` (`reader_audiobook_pairing.js:93-121`) auto-advances audiobook chapter when book chapter changes | **None.** User controls both manually. | Hemanth 2026-04-22: "the sync between the book and audiobook is completely manual taken care of by the fact the user themselves will be reading alongside the audiobook." |
| Chapter-level pairing map | Max stores `{bookChapterHref, abChapterIndex}[]` mappings (`reader_audiobook_pairing.js:148-170`) | **Book-level pairing only** — just `{audiobookId}` per bookId. Current audiobook chapter + position tracked separately in audiobook progress. | No auto-sync means no chapter-map needed. Simpler schema. |

## 3. Current State Inventory

- **Complete (preserved from 2026-04-21 `AUDIOBOOK_FOLDER_DETECTION_FIX`):** `BooksScanner::walkAudiobooks` (recursive 6-deep walk, cross-domain scan, cover fallback, `.wma` extension), `BookBridge::walkAudiobooksJson` mirror, `BooksPage` audiobook strip render.
- **Skeletal:** Audiobook tile click has no handler. `audiobook_progress.json` schema has only `updatedAt`. `audiobook_pairings.json` has full CRUD bridge methods (`audiobooksGetPairing/Save/Delete`) but zero consumer.
- **Missing:** Wrapper-flatten scanner upgrade. `AudiobookMetaCache` (ffprobe duration + sidecar cache — was shipped earlier this session, reverted). Duration in tile subtitle. `AudiobookDetailView`. Reader-side Audio tab (picker + transport + pairing UI). Combined reader-close save of book+audiobook state. Per-chapter `perChapterListenedMs` tracking for the detail view's PROGRESS column.

## 4. Scope — 4 phases

- **Phase 1 — Scanner + duration metadata** (~3 batches). AudiobookMetaCache ffprobe pipeline + `.audiobook_meta.json` per-folder cache. Wrapper-flatten walker. Tile subtitle "N chapters · HH:MM:SS".
- **Phase 2 — AudiobookDetailView** (~2 batches). Click-tile → chapter-list info view with "In-reader only" badge + per-chapter progress column.
- **Phase 3 — Reader Audio-tab sidebar** (~3 batches). HTML/CSS/JS injection into Foliate shell. Audiobook picker dropdown. HTMLAudioElement + transport bar. Pairing save.
- **Phase 4 — Combined progress save + restore** (~2 batches). Atomic reader-close save of book+audiobook state. Per-chapter listened-ms tracking. Resume on reader open.

## 5. Anti-Scope (explicit NOT-doing)

- **Separate Audiobooks top-nav mode** — reverted from the earlier TODO; will not revisit.
- **Series grouping for audiobooks** (wrapper-as-series with volume picker) — Hemanth wants flat-chapter-list under one tile, not a hierarchy with drill-down. The wrapper-flatten scanner upgrade achieves "one tile per downloaded folder" without introducing a series struct.
- **Standalone audiobook player** (overlay player, full-screen player, mini-player) — audiobooks are reader-only.
- **Continue Listening strip** on BooksPage.
- **Auto-sync on page turn** — user controls manually.
- **Chapter-level pairing mappings** (book chapter N ↔ audiobook chapter M) — simpler book-level pairing only.
- **Bookmarks + sleep timer + SMTC/media-key integration** — scope-reserved for a future TODO if ratified separately.
- **M4B embedded chapter extraction** — treat container as one file; chapters are separate audio files per Max-parity.
- **Pairing pairing UI for books without audiobooks** — if a user hasn't added any audiobook roots, the reader's Audio tab is empty/hidden.

## 6. Files in Scope

### New files
- `src/core/AudiobookMetaCache.{h,cpp}` — ffprobe duration extraction + `.audiobook_meta.json` sidecar cache. Reviving the previously-reverted code.
- `src/tests/test_audiobook_meta_cache.cpp` — gtest fixture. Reviving previous tests.
- `src/ui/pages/AudiobookDetailView.{h,cpp}` — native Qt chapter-list info view, pattern mirrors existing `BookSeriesView`.
- `resources/ffmpeg_sidecar/ffprobe.exe` — bundled binary (229 KB), auto-preserved by patched `native_sidecar/build.ps1`.
- `resources/book_reader/domains/books/reader/reader_audiobook.js` — in-reader sidebar Audio tab logic (mirror of Max's file).
- `resources/book_reader/domains/books/reader/reader_audiobook_pairing.js` — in-reader pairing picker (simplified: no chapter-map UI, just audiobook selector).
- `build_tests.bat` — agent-safe unit test runner.

### Modified files
- `src/core/BooksScanner.{h,cpp}` — wrapper-flatten walker. `AudiobookInfo` gains `totalDurationMs` + per-chapter duration metadata. No new struct (`AudiobookSeriesInfo` from the reverted TODO does NOT come back).
- `src/ui/readers/BookBridge.{h,cpp}` — mirror wrapper-flatten in `walkAudiobooksJson`. Extend `audiobooksGetProgress/Save` schema with `perChapterListenedMs`. Possibly new helper to return "paired audiobook info for this bookId" in one call.
- `src/ui/pages/BooksPage.{h,cpp}` — audiobook tile click → push `AudiobookDetailView` to the existing `m_stack` (same pattern as `BookSeriesView` nav).
- `src/ui/readers/BookReader.{h,cpp}` — likely untouched Qt-side; Audio tab lives entirely inside the Foliate WebEngine. May need to bump the QWebChannel exposure to confirm audiobook bridge methods are available.
- `resources/book_reader/index.html` (or wherever Foliate's shell is served from) — add Audio tab HTML alongside existing sidebar tabs.
- `resources/book_reader/domains/books/reader/reader_shell.js` (or equivalent) — register Audio tab activation, route click.
- `CMakeLists.txt` — new `AudiobookMetaCache.*` + `AudiobookDetailView.*` sources/headers. Announce per Rule 7 with exact lines.
- `native_sidecar/build.ps1` — +7 lines for ffprobe.exe copy (previously shipped, now re-shipped).
- `src/tests/CMakeLists.txt` — +2 lines for the test fixture.

### Read-only references during implementation
- Tankoban-Max master: `src/domains/books/reader/reader_audiobook.js`, `reader_audiobook_pairing.js`, `audiobook_detail.js`, `src/index.html:257-358` + `:570-625`, `workers/audiobook_scan_worker_impl.js`.
- Tankoban 2 existing patterns: `src/ui/pages/BookSeriesView.{h,cpp}` (detail-view nav shape), `src/core/JsonStore.*` (persistence shape).

## 7. Architectural Decisions (Rule 14)

- **Playback engine: `HTMLAudioElement`** inside the reader's WebEngine context. This is literal Max-parity — no new native Qt player, no libavformat link, no QMediaPlayer. The entire audio surface (UI + playback + state) lives in the web layer. The reverted AudioDecoder code stays retired.
- **Wrapper-flatten, not wrapper-series.** `BooksScanner::walkAudiobooks` upgrades with: if the current folder has no direct audio AND ≥1 subdir has direct audio AND no "loose" non-audio subdirs, treat the wrapper as ONE AudiobookInfo with `tracks[]` = natural-sorted union of all subdir audio files. No new struct. No volume picker. Standalone leaf folders (direct audio in the folder itself) emit unchanged.
- **`AudiobookMetaCache` comes back exactly as shipped earlier.** The ffprobe subprocess + per-folder `.audiobook_meta.json` cache + mtime stale-check + mutex-coalesced access — all proven. ffprobe.exe is bundled at `resources/ffmpeg_sidecar/ffprobe.exe` (229 KB, same MinGW build as the existing avformat-62/avcodec-62 DLLs, zero version drift).
- **Book-level pairing, no chapter map.** Simpler than Max. `audiobook_pairings.json` entry shape: `{bookId: {audiobookId, updatedAt}}`. Audiobook progress (chapterIndex + positionMs) lives separately in `audiobook_progress.json` keyed by `audiobookId`. When user opens a book: lookup pairedAbId → load audiobook progress → resume at (chapterIndex, positionMs).
- **Combined reader-close save is two JsonStore writes, not one transaction.** JsonStore is async-coalescing (shipped 2026-04-21), so sequential `saveProgress("books", bookId, ...)` + `audiobooksSaveProgress(abId, ...)` calls coalesce with microsecond main-thread cost. No new atomic-write API needed.
- **"In-reader only" badge is a static HTML label** in AudiobookDetailView — no behavior gate. If we ever ship a standalone player, the badge gets removed; for v1 it's purely informational.
- **Per-chapter PROGRESS column fills via `perChapterListenedMs` map** in audiobook_progress.json, updated on every ~5s of listening (debounced via reader JS). "-" when never-listened. A chapter counts as "complete" (could show a checkmark in future polish) when `listenedMs >= 0.95 * chapterDurationMs` — but v1 just shows presence/absence, no visual completion state yet.

## 8. Phase Map + Batch Breakdown

### Phase 1 — Scanner + duration metadata (~3 batches)

**1.1 AudiobookMetaCache + ffprobe pipeline** (revive)
- Re-ship `src/core/AudiobookMetaCache.{h,cpp}` exactly as previously shipped: static `durationMsFor(folderPath, audioFilePath)` + `ffprobePath()` + `invalidateFolder()` + test-only override. Mutex-coalesced, releases lock during subprocess, 15s timeout.
- Re-bundle `resources/ffmpeg_sidecar/ffprobe.exe` + patch `native_sidecar/build.ps1` +7 lines.
- Re-ship `src/tests/test_audiobook_meta_cache.cpp` (7 tests — cache-hit / roundtrip-multi / stale-reprobe / missing-ffprobe / missing-audio / invalidate / gated-real-probe).
- Re-ship `build_tests.bat`.
- **Exit:** `build_check.bat` BUILD OK + `build_tests.bat` 7/7 PASS including real-probe on Way of Kings 01-38.mp3.

**1.2 Wrapper-flatten walker**
- `BooksScanner::walkAudiobooks` upgrade: wrapper detection branch. If current dir has no direct audio AND has ≥1 subdir with direct audio AND non-audio-subdir count == 0, emit ONE `AudiobookInfo` with `tracks[]` = union of all subdir audio files, natural-sorted across subdirs. Cover search: wrapper dir first, fall back to first subdir's cover.
- Standalone leaf (direct audio in dir itself): unchanged.
- `BookBridge::walkAudiobooksJson` mirror.
- **Exit:** Stormlight Archive 0.5-4 pack (5 leaf subdirs, each with ~10 chapters) renders as ONE tile. Standalone Way of Kings (38 chapters direct) still its own tile. `AudiobookInfo.tracks` for the Stormlight tile has ~50 files in natural order across all 5 subdirs.

**1.3 Duration population + tile subtitle**
- Scanner calls `AudiobookMetaCache::durationMsFor` for each chapter during walk. `AudiobookInfo.totalDurationMs` + per-chapter duration populated.
- Tile subtitle format: `"{N} chapter{s} · {HH}:{MM}:{SS}"` when totalDurationMs > 0, fall back to `"{N} chapter(s)"` when cache is cold or probe failed.
- **Exit:** MCP smoke — Stormlight tile subtitle reads `"50 chapters · 228:14:23"` (or similar summed duration). Way of Kings tile reads `"38 chapters · 45:33:18"`.

### Phase 2 — AudiobookDetailView (~2 batches)

**2.1 View scaffold + BooksPage routing**
- New `src/ui/pages/AudiobookDetailView.{h,cpp}`, pattern mirrors `BookSeriesView`. Header: back button + cover + metadata ("N chapters · HH:MM:SS") + "In-reader only" pill-shaped badge. Body: `QTableWidget` with columns {#, Chapter, Duration, Progress}.
- `BooksPage::addAudiobookTile` click handler: push `AudiobookDetailView` to the existing `m_stack`. Back button returns to grid.
- Chapter rows populated from `AudiobookInfo.tracks` + cached durations. Progress column reads from `audiobook_progress.json`'s `perChapterListenedMs`; shows `"-"` when absent/zero.
- **Exit:** click audiobook tile → detail view opens with chapter list; back button returns to Books grid.

**2.2 "In-reader only" badge + visual polish**
- Badge is a QLabel with icon + text "In-reader only" in a subtle pill frame. No click, no behavior, no tooltip needed.
- PROGRESS column formatting: `"-"` when 0, `HH:MM:SS / HH:MM:SS` when partial, `"✓"` or `Listened` text when ≥95% listened.
- **Exit:** visual polish matches Max's screenshot.

### Phase 3 — Reader Audio-tab sidebar (~3 batches)

**3.1 Sidebar Audio tab scaffolding**
- Add Audio tab to the Foliate reader shell's sidebar. Locate the existing sidebar HTML in `resources/book_reader/` (may need Explore subagent to map Foliate's specific structure). Inject new tab next to existing ones.
- Audio tab content: audiobook picker `<select>` dropdown (populated via `window.__audiobookAPI.audiobooksGetState()`), empty container for transport bar (Phase 3.2), empty container for chapter list (Phase 3.3).
- **Exit:** open any book → sidebar has new Audio tab; clicking it reveals empty dropdown + placeholder transport.

**3.2 HTMLAudioElement + transport bar**
- Pick an audiobook from dropdown → call `window.__audiobookAPI.audiobooksSavePairing(bookId, {audiobookId})` → load audiobook's first chapter into a hidden `<audio>` element.
- Transport UI: Prev / -15s / Play|Pause / +15s / Next + seek slider + time label `M:SS / M:SS` + volume slider + speed dropdown (0.5/0.75/1.0/1.25/1.5/1.75/2.0/2.5/3.0 — Max-parity).
- On chapter `ended` event: auto-advance to next chapter in the audiobook's flat chapters[] list.
- Save playback state (chapterIdx, positionMs, speed, volume) on each transport action + 5s debounced during playback via `audiobooksSaveProgress(abId, ...)`.
- **Exit:** MCP smoke — open book, pick audiobook, play → audio plays. Pause/seek/prev/next/speed all work. Change chapter → progress persists.

**3.3 Chapter list panel + pairing restore**
- Below transport bar: collapsible chapter list (one row per chapter in paired audiobook, click row to jump). Current chapter highlighted.
- On reader open: lookup `audiobooksGetPairing(bookId)` → if non-empty, pre-select the audiobook in the dropdown + restore its last `chapterIndex + positionMs + speed + volume` from `audiobooksGetProgress(abId)`. Audio stays paused (user explicit-plays).
- If no pairing: dropdown is empty/placeholder, no audio loaded.
- **Exit:** MCP smoke — pair audiobook with book, close book, reopen book → same audiobook pre-selected + same chapter+position loaded + playback ready (paused).

### Phase 4 — Combined save + per-chapter progress (~2 batches)

**4.1 Combined reader-close save**
- On reader close (`closeRequested` / `hideEvent`), JS layer flushes: `booksProgressSave(bookId, {...bookPos})` + `audiobooksSavePairing(bookId, {audiobookId})` + `audiobooksSaveProgress(abId, {chapterIndex, positionMs, speed, volume, perChapterListenedMs})`. Three sequential writes via JsonStore (async-coalesced).
- **Exit:** play 30s of audiobook → read a few pages → close reader → inspect `books_progress.json` + `audiobook_pairings.json` + `audiobook_progress.json` → all three reflect final state. Reopen book → all three restore.

**4.2 Per-chapter listened tracking**
- `HTMLAudioElement.timeupdate` handler accumulates listened-ms per chapter in a JS-side map. On chapter change (either ended-auto-advance OR manual jump), commit the accumulated ms to `perChapterListenedMs[chapterIdx]` in `audiobook_progress.json` via `audiobooksSaveProgress`. Periodic 15s-debounced flush during playback for crash safety.
- AudiobookDetailView queries this map for the PROGRESS column. "-" when 0, `HH:MM:SS / HH:MM:SS` when partial, completion marker when ≥95%.
- **Exit:** listen to 3 consecutive chapters → close reader → open that audiobook's detail view from library → PROGRESS column shows listened time for those 3 chapters, "-" for rest.

## 9. Per-Phase Exit Criteria (Summary Matrix)

| Phase | Artifact | Smoke criterion (Windows-MCP self-drive) |
|---|---|---|
| 1.1 | AudiobookMetaCache | `build_tests.bat` → 7/7 AudiobookMetaCacheTest PASS |
| 1.2 | Wrapper-flatten walker | Stormlight Archive 0.5-4 pack → ONE tile; standalone Way of Kings → separate tile; scanner emits correct `tracks[]` union in natural order |
| 1.3 | Duration subtitle | Tile subtitle reads "{N} chapters · HH:MM:SS" on Stormlight tile after first scan (cache cold → slow ~30s first scan, instant thereafter) |
| 2.1 | Detail view | Click audiobook tile → detail view opens with chapter table; back button returns |
| 2.2 | "In-reader only" + polish | Badge visible, progress column formatting matches |
| 3.1 | Audio tab scaffold | Open book → Audio tab visible in sidebar with picker dropdown populated |
| 3.2 | Transport + playback | Pick audiobook → audio plays; all transport controls work; speed change persists |
| 3.3 | Pairing restore | Close + reopen book → same audiobook + same chapter + position ready (paused) |
| 4.1 | Combined save | Play 30s + read → close → 3 JSON files reflect all state; reopen restores all |
| 4.2 | Per-chapter progress | Listen to 3 chapters → detail view PROGRESS column shows listened time for those chapters |

## 10. Contracts (Schemas + Bridge)

### `audiobook_progress.json` entry
```json
{
  "<audiobookId>": {
    "chapterIndex": <int, 0-based>,
    "positionMs": <int, within-chapter>,
    "speed": <float, 0.5-3.0>,
    "volume": <float, 0.0-1.0>,
    "perChapterListenedMs": { "<chapterIdx as string>": <int ms listened>, ... },
    "updatedAt": <int ms since epoch>
  }
}
```

### `audiobook_pairings.json` entry (simplified vs Max — no chapter map)
```json
{
  "<bookId>": {
    "audiobookId": "<audiobookId>",
    "updatedAt": <int ms>
  }
}
```

### `.audiobook_meta.json` sidecar (per audiobook folder — wrapper or leaf)
```json
{
  "schemaVersion": 1,
  "chapters": { "<filename>": { "durationMs": <int>, "mtimeMs": <int> }, ... },
  "updatedAt": <int ms>
}
```
For wrapper folders, keys include the subdir prefix: `"0.5 Edgedancer/01.mp3"` instead of just `"01.mp3"`. Stale-check per-file.

### Bridge methods (all pre-existing, reuse as-is)
- `audiobooksGetState()` → full library state
- `audiobooksGetProgress(abId)` / `audiobooksSaveProgress(abId, obj)`
- `audiobooksGetPairing(bookId)` / `audiobooksSavePairing(bookId, obj)` / `audiobooksDeletePairing(bookId)`
- `booksProgressGet(bookId)` / `booksProgressSave(bookId, obj)` (existing books-side)

No new bridge methods required.

## 11. Smoke Matrix

Windows-MCP self-drive per Rule 17 + 18, after each phase:

1. **Scanner regression** — 2026-04-21 AUDIOBOOK_FOLDER_DETECTION_FIX still works on non-wrapper layouts.
2. **Wrapper flatten** — Stormlight pack = 1 tile with correct chapter count summed across subdirs.
3. **Duration display** — tile subtitle format correct on fresh + cached scans.
4. **Detail view** — click → open; back → return; chapter table renders.
5. **Audio tab render** — sidebar shows tab; dropdown populated.
6. **Playback** — audio actually plays (can verify via SMTC taskbar indicator or system volume meter).
7. **Transport controls** — play/pause/seek/±15s/prev/next/speed/volume all work.
8. **Pairing save** — close book → `audiobook_pairings.json` has the pairing.
9. **Pairing restore** — reopen book → same audiobook preselected.
10. **Combined save** — all three JSON files update on reader close.
11. **Per-chapter progress** — detail view PROGRESS column fills after listening.
12. **No standalone player regression** — clicking audiobook tile opens DetailView, does NOT open a player.
13. **No Continue Listening regression** — BooksPage does NOT show such a strip.

## 12. Rollback Plan

Each phase ships as an independent commit with `[Agent 2, AUDIOBOOK_PAIRED_READING_FIX Phase X.Y]` tag. Rollback per phase via `git revert <hash>`. Dependencies:
- Phase 1 is independent. Revert → scanner returns to 2026-04-21 flat-leaf state.
- Phase 2 depends on Phase 1's `AudiobookInfo.totalDurationMs` for subtitle, otherwise standalone.
- Phase 3 depends on Phase 1 (audiobook listing for dropdown) + scanner wrapper-flatten so tracks[] is cohesive.
- Phase 4 depends on Phase 3 (audiobook state to save).

Full-rollback: revert all four phases → 2026-04-21 state preserved (scanner + strip render still work, just no detail view + no reader Audio tab + no durations).

## 13. Decision Log

### Ratified 2026-04-22 (Hemanth direct)

1. **Top-nav IA:** 4 modes + Stream. Audiobooks NOT a sibling mode. (Session-earlier mistake reverted and settled.)
2. **Wrapper-flatten audiobook scanner:** "the folder I downloaded" = one tile. No series struct, no volume picker. Overrides Max's flat-leaf default.
3. **NO standalone audiobook player.** Reader-only playback.
4. **NO Continue Listening strip.** Book Continue Reading handles both resumes atomically.
5. **NO auto-sync on page turn.** Manual user control.
6. **Click audiobook tile → AudiobookDetailView** (chapter-list info view with "In-reader only" badge). No play action.
7. **Reader Audio tab in sidebar** — Max-parity visual + behavior (HTMLAudioElement + transport + picker).
8. **Playback engine = HTMLAudioElement inside WebEngine.** No native Qt player, no libavformat, no QMediaPlayer.
9. **Book-level pairing only** (no chapter map).
10. **Combined progress save on reader close** = three sequential JsonStore writes (async-coalesced).

### Open items (none)

All scope decisions ratified. Phase 1 executes on next Agent 2 summon.

## 14. Deferred Ledger

Scope-reserved for future TODOs:
- Bookmarks (add/remove/jump inside audiobook).
- Sleep timer (pause after N minutes / end-of-chapter).
- SMTC + media-key integration (Windows lockscreen controls).
- Keep-awake during playback (`SetThreadExecutionState`).
- M4B embedded-chapter extraction (single m4b → N logical chapters).
- ID3 / MP4 tag extraction (author, narrator, series metadata).
- Cloud progress sync (never — local-only is brotherhood convention).
- Chapter-level pairing map (if manual sync turns out to be fiddly and users ask for auto-advance).
- Standalone audiobook player (if the paired-reading model ever gets relaxed).

---

## Estimated Shape

- **~10 batches** across 4 phases.
- **~2,200–2,800 LOC** new (AudiobookMetaCache + AudiobookDetailView + reader Audio-tab JS/HTML + wrapper-flatten walker upgrade).
- **~150 LOC** modified (scanner hooks, bridge schema extensions, BooksPage click handler, CMakeLists, build.ps1).
- **Session count estimate:** 4–6 wakes. Phase 1 compressible to 1–2 wakes (all independent sub-batches). Phase 3 probably 2 wakes (scaffold + full transport+restore).

## Verification End-to-End

On completion of all 4 phases:
1. Drop Stormlight Archive 0.5-4 (5 leaf subdirs) + standalone Way of Kings into `/Media/Audiobooks/`.
2. Launch Tankoban via `build_and_run.bat`.
3. Books tab shows: SERIES row (ebooks) + AUDIOBOOKS row with **2 tiles** — one "Stormlight Archive 0.5-4" (wrapper, ~50 chapters · summed duration) + one "The Way of Kings by Brandon Sanderson" (standalone, 38 chapters · 45:33:18). NO Continue Listening strip.
4. Click Stormlight tile → AudiobookDetailView opens with full chapter list spanning all 5 subdirs in natural order + "In-reader only" badge + cover + metadata. Back button returns to Books tab.
5. Open an ebook in the library (e.g. Stormlight Archive epub).
6. BookReader opens → sidebar has Audio tab → pick Stormlight audiobook from dropdown.
7. Transport bar appears → Play → audio plays from chapter 0 position 0.
8. Read a few pages, listen a few minutes, change speed to 1.5×, change to chapter 3 mid-way, seek +15s.
9. Close reader.
10. `audiobook_pairings.json` → has `{stormlight_ebook_id: {audiobookId: stormlight_ab_id}}`.
11. `audiobook_progress.json` → has `{stormlight_ab_id: {chapterIndex: 2, positionMs: <somewhere in ch 2>, speed: 1.5, volume: ..., perChapterListenedMs: {0: <full dur>, 1: <full dur>, 2: <partial>}}}`.
12. `books_progress.json` → book position at last read page.
13. Reopen Stormlight ebook → book opens at same page + Audio tab preselects Stormlight audiobook + transport shows chapter 3 at last seeked position + speed 1.5x. Playback paused.
14. Open AudiobookDetailView for Stormlight → PROGRESS column shows full duration for chapters 0+1, partial for 2, "-" for 3+.
15. `scripts/stop-tankoban.ps1` — Rule 17 cleanup.

Evidence preserved at `out/audiobook_e2e_smoke_<timestamp>.log` + MCP screenshots.
