# TankoLibrary UI — Agent 2

This file auto-loads when any agent reads a file under `src/ui/pages/tankolibrary/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). Part of the path-scoped guidance migration (Phase 2 Item 4 of the CLAUDE_CODE_PRACTICES arc, 2026-05-21). Sibling for the data layer lives at `src/core/book/CLAUDE.md`.

## Domain owner

**Agent 2** (Book Reader + TankoLibrary). TankoLibrary ownership inherited from Agent 4B on 2026-05-20 at 4B's brotherhood departure. This subtree is the source-side UI layer — the search widget, results grid, and transfers view that let users discover + download books via TankoLibrary, feeding the Books library the same way Tankoyomi feeds Comics.

Note: `TankoLibraryPage.{h,cpp}` currently lives at `src/ui/pages/` (flat), not under this subtree — it predates the tankolibrary/ directory. Treat both as Agent 2 territory.

## Active arc — BOOKS_STREMIO_PIVOT (mid-flight)

The UI shape this subtree is growing toward: a Stremio-style source panel (search takeover → result cards → detail view with download picker) inside Books mode. Think of how Tankoyomi gives Comics its source panel — TankoLibrary does the same for Books.

**Phase cursor (as of 2026-05-21):** Phases 1–4 SHIPPED (data model + catalogue clients + scraper layer + downloader). Phases 5–8 queued:
- Phase 5: `BooksTankoLibrarySearchWidget` — search-takeover view, forked from `StreamSearchWidget`
- Phase 6: source picker (which scraper to search: LibGen, Tankorent; AA deferred to v1.1)
- Phase 7: detail view (cover + metadata + download button + format chip)
- Phase 8: bookshelf integration (downloaded book auto-added to library)
- Phase 9: integration + smoke (inline, arc close)

Phases 5–8 are likely Trigger E (Agent 2 Jrs in parallel tabs) per `feedback_trigger_e_agent_n_jrs.md`. Phase 9 inline.

## File map — what lives here

- `BookResultsGrid.{h,cpp}` — results grid widget (displays `BookResult` cards after a search; Agent 4B-authored, Agent 2-inherited)
- `TransfersView.{h,cpp}` — in-progress downloads view (mirrors Stream mode's transfers panel shape; Agent 4B-authored, Agent 2-inherited)

Phase 5+ will add more widgets here. The search widget, picker, and detail view will land in this subtree as they ship.

## Reference apps

- **Stream mode (Stremio architecture)** — the BLUEPRINT. `StreamSearchWidget` is the direct template for `BooksTankoLibrarySearchWidget`. Cards, source picker, in-library auto-add.
- **Comics Sources sidebar** (`src/ui/pages/comics/`) — already shipped Stremio-style sidebar; the comics version is the nearest same-generation reference for the pattern we're replicating here.

## Load-bearing memories (read when touching this domain)

- `project_books_stremio_pivot_2026-05-20.md` — full arc shape + pattern parity table
- `project_comics_sources_sidebar_shipped_2026-05-17.md` — the comics sidebar is the same pattern; read for shape continuity before authoring Phase 5+
- `feedback_mode_pill_resets_to_root.md` — standing contract: topbar mode pills are hard resets to mode root; every deep view must expose `resetToRoot()` + emit `navigationRequested`
- `reference_libgen_url_params.md` — v1 primary scraper (LibGen); format-narrowing is client-side only; cover URL requires a `/ads.php` round-trip
- `project_agent4b_departure_2026-05-20.md` — Agent 4B's inheritance ledger; honor the UI hand you're reading

## Dev-bridge surface

Agent 2 owns `books-*` (24 commands, v1.3) + inherited `sources-*` (20 commands, v1.5) — full listings in `src/core/book/CLAUDE.md`. For this subtree specifically:
- `sources-get-tankolibrary-state` / `sources-get-tankolibrary-results` / `sources-set-tankolibrary-filters` / `sources-open-tankolibrary-detail` / `sources-download-tankolibrary-selected` for the UI-state reads most relevant here.
- `books-refresh-library` / `books-search-library` for post-download library state verification.

Full catalog enumerable via `out\tankoctl.exe ping`.

## Build / MCP lane discipline (gov-v7)

Use the lease registry per Rules 19 + 22 (gov-v7 landed 2026-05-21). `out\tankoctl.exe lease-get <lane>` is machine-truth; `## BUILD LANE` / `## MCP LANE` chat.md headings are the human narrative. Lane names: `mcp` for desktop interactions, `build` for `build_check.bat`. Per-lane build dirs (`TANKOBAN_BUILD_LANE=<lane>`) bypass the shared lock for isolated parallel builds.

## When this file activates

Auto-loads when any agent reads a file under `src/ui/pages/tankolibrary/`. Treat it as ambient context: who owns this code, what arc is mid-flight, which memories are load-bearing.

If you're Agent 2: home turf, redundant reminder.

If you're another brother doing a cross-domain read: edits without Agent 2 sign-off are a Rule 14 violation in this domain.
