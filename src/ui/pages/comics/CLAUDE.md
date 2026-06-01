# Comics Domain — Agent 1

This file auto-loads when any agent reads a file under `src/ui/pages/comics/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). MVP of the `.claude/rules/`-equivalent path-scoped guidance migration (Phase 2 Item 4 of the CLAUDE_CODE_PRACTICES arc, 2026-05-21). Sibling file at `src/core/manga/CLAUDE.md` carries the manga-source side (not yet authored — pending this MVP's smoke).

## Domain owner

**Agent 1** (Comic Reader + Tankoyomi as source-side ingestion into Comics mode). Tankoyomi ownership inherited from Agent 4B on 2026-05-14. Standing polish mode on `agents/todos/COMIC_READER_FIX_TODO.md` Phase 6 — **no new UI/UX without explicit Hemanth ratification**.

## Active arc — COMICS_TANKOYOMI_STREAM_MERGER

Vision locked 2026-05-14, Agent 1 owns. Hemanth verbatim: *"I want to remove Tankoyomi and merge it with the Comics mode. The Comics mode will then take a lot of elements from Stream mode. ... Stream mode isn't just a reference; it's a blueprint."*

**Operative shape:** Tankoyomi dissolves as a standalone Source; Comics-mode search becomes "Search Tankoyomi" producing Stream-style result cards → Stream-style series view inside the Comics library → Netflix-style in-library downloads (downloading a chapter auto-adds the series); Tankoyomi-sourced series carry a badge and render the new Stream-blueprint series UI vs the existing folder-import UI.

**Process gate (Rule 20, gov-v4 — revised 2026-05-14):** Codex (Agent 7) reviews AND EXPANDS Agent 1's brainstorm-md **in place** (HTML-comment attribution markers per added/rewritten section) before `/superpowers:writing-plans` fires. Co-authorship, not audit. One Codex pass total — execution follows writing-plans directly with no second Codex review on the plan.

**Brainstorm pacing:** Agent 1 fires `/superpowers:brainstorming` in batches of 4 questions per `feedback_brainstorm_batches_of_four.md`. Brainstorm + Codex review-and-expand queued behind Hemanth's next Agent 1 wake.

## Reference apps

- **Mihon** — primary reference for comic reader Phase 6 polish work. See `reference_reader_codebases.md` for on-disk reference path.
- **YACReader** — secondary polish overlay reference. Same memory.
- **Stream mode (Stremio architecture)** — the BLUEPRINT for Comics-mode UI shape per the active arc above. Cards, series view, in-library downloads.

## Load-bearing memories (read when touching this domain)

- `project_comic_reader_identity.md` — Mihon primary + YACReader polish overlay
- `feedback_comic_reader_polish_mode.md` — post-Phase-6, no new UI/UX without ratification
- `project_tankoyomi_volume_pivot_arc_2026-05-16.md` — 13-phase volume-only Stremio-for-manga arc
- `feedback_stremio_for_manga_vibe.md` — volume is the only first-class UI unit; chapters are buried
- `feedback_brainstorm_batches_of_four.md` — Agent 1's brainstorm pacing preference
- `project_anilist_api_facts.md` — AniList GraphQL constraints (90/min, no auth for read, no per-chapter binding)
- `project_comics_sources_sidebar_shipped_2026-05-17.md` — Stremio-style Sources sidebar v1
- `project_empty_cbz_bug_fix.md` — MangaDownloader rejects zero-page scraper responses; 22-byte EOCD-only signature
- `project_weebcentral_71pct_downscale_confirmed.md` — WeebCentral downscales to ~71% linear / ~50% pixel count of master
- `project_tankoyomi_continue_reading_shipped.md` — CR strip surfaces Tankoyomi chapters on read-start
- `feedback_bigger_manga_covers.md` — Comics volume rows want bigger covers; locked at 110×150 BookWalker-native (2:3 manga ratio)

## Dev-bridge surface (Agent 1's commands)

Agent 1 owns the `comics-*` tankoctl prefix (v1.2 — 10 commands):
- `comics-get-state` / `comics-get-library` / `comics-get-series` / `comics-get-sources` / `comics-get-downloads`
- `comics-select-volume` / `comics-dispatch-volume` / `comics-open-series` / `comics-open-chapter`
- `comics-search-tankoyomi`

Plus cross-mode `library-*` commands for comics-mode tile state. Full catalog enumerable via `out\tankoctl.exe ping`.

## Build / MCP lane discipline (gov-v7)

When building or MCP-smoking this domain, use the lease registry per Rules 19 + 22. Lane names: `mcp` for desktop interactions, `build` for `build_check.bat` against shared `out/`. Per-lane build dirs (`TANKOBAN_BUILD_LANE=<lane>`) bypass the shared lock — use them when isolating from other brothers' concurrent builds.

## When this file activates

Auto-loads when any agent reads a file under `src/ui/pages/comics/`. Treat the content as ambient context: who owns this code, what arc is in flight, which memories are load-bearing, which dev-bridge commands belong to this domain.

If you're Agent 1: this is your home turf, redundant reminder.

If you're another brother doing a cross-domain audit / read: this orients you on whose hand you're reading + what taste discipline applies. Cross-domain edits without Agent 1 sign-off are a Rule 14 violation in this domain.
