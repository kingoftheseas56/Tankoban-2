# Tankoyomi Page Domain — Agent 1

This file auto-loads when any agent reads a file under `src/ui/pages/tankoyomi/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). Sister files at `src/ui/pages/comics/CLAUDE.md` (Comics UI — the merge destination) and `src/core/manga/CLAUDE.md` (manga source / scraper layer).

## Domain owner

**Agent 1** (Comic Reader + Tankoyomi as source-side ingestion into Comics mode). Tankoyomi ownership inherited from Agent 4B on 2026-05-14.

## Status — transitional

This directory is **transitional**. Under the active COMICS_TANKOYOMI_STREAM_MERGER arc (vision locked 2026-05-14), Tankoyomi dissolves as a standalone Source; its UI widgets either migrate into `src/ui/pages/comics/` or are deleted. The files here today are the pre-merger UI components still in service during the transition window.

**Current contents:**
- `ChapterDownloadIndicator` — 28×28 custom widget with 5-state animated paint methods; shows download progress inline on chapter rows. This widget's logic may migrate to Comics-mode volume rows or be replaced by a volume-scale equivalent.
- `ChapterRangeDialog` — dialog for selecting a chapter range to batch-download. Volume-pivot arc replaces the chapter granularity with volume granularity; this dialog's fate is deletion or volume-equivalent replacement at arc execution time.

**Do not add new Tankoyomi-standalone UI here.** Everything new for the merged Comics + Tankoyomi experience goes into `src/ui/pages/comics/`. This directory only receives: (a) bugfixes to keep existing behavior working during the transition window, or (b) arc-execution teardown commits that delete files as the merger lands.

## Active arc context

See `src/ui/pages/comics/CLAUDE.md` for the full COMICS_TANKOYOMI_STREAM_MERGER shape, Hemanth verbatim quote, process gate (Rule 20), and Codex review-and-expand requirement. Not duplicated here.

Short version for this directory: when the arc executes, `ComicsTankoyomiDetailView` in the comics tree gets deleted, `ChapterRangeDialog` here likely gets deleted, and `ChapterDownloadIndicator`'s concept migrates to a volume-scale indicator in the comics tree.

## Load-bearing memories

- `feedback_stremio_for_manga_vibe.md` — volume is the only first-class unit; chapters are buried; these widgets are in the chapter-granularity world that the arc replaces
- `project_tankoyomi_volume_pivot_arc_2026-05-16.md` — the 13-phase plan; Phase 9 deletes `ComicsTankoyomiDetailView`; scope for this directory's files is set there
- `project_tankoyomi_continue_reading_shipped.md` — CR strip integration; `ChapterDownloadIndicator` drives the download state signal that feeds the CR tile
- `feedback_brainstorm_batches_of_four.md` — Agent 1's pacing for any new brainstorm work on the merged arc

## Build / MCP lane discipline (gov-v7)

Lane names: `mcp` for desktop interactions, `build` for `build_check.bat`. Run `build_check.bat` after edits; this tree's widgets link into the main app so a compile check is mandatory. Per Rules 19 + 22, acquire the lease before MCP-smoking.

## When this file activates

Auto-loads when any agent reads a file under `src/ui/pages/tankoyomi/`. This is a shrinking directory — read the transitional status above before touching anything here.

If you're Agent 1: you know this is the pre-merger holdover. Only touch for bugfixes or arc teardown.

If you're another brother: do not add functionality here. Cross-domain edits without Agent 1 sign-off are a Rule 14 violation.
