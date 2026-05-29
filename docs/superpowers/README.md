# `docs/superpowers/` — Design Artifacts

Point-in-time **design records** for Tankoban's feature arcs — the spec and plan a feature was built from. This is *history and rationale*, not live status. (For what's **actively being worked**, see the **Active Fix TODOs** table in [`/CLAUDE.md`](../../CLAUDE.md) at the repo root.)

## What's here

Each arc typically has a **spec → plan** pair, date-prefixed by when it was authored:

- **`specs/`** — the *design*: what the feature is, why, the user-facing shape, and how we'll know it works. Often produced via the brainstorming flow. Files: `YYYY-MM-DD-<arc>-design.md` (and `-brainstorm.md` for the exploratory pass).
- **`plans/`** — the *implementation*: bite-sized, ordered tasks the spec was built from. Files: `YYYY-MM-DD-<arc>.md` (and `-smoke.md` for the smoke-test pass that followed).
- **`mockups/`** — visual design mockups (HTML/static) for UI-heavy arcs.
- **`data/`** — small checked-in seed/catalog data referenced by a design (e.g. candidate lists).
- **`audits/`** — arc-specific audit *prompts* (the brief handed to a reviewer). Note: comparative audits + raw model output live separately under [`/agents/audits/`](../../agents/audits/), not here.

## How to read it

- **Naming = chronology + arc.** `2026-05-16-tankoyomi-volume-pivot-design.md` is the spec; `2026-05-16-tankoyomi-volume-pivot.md` is its plan. Same date-prefix + arc-name = the same body of work.
- **A spec/plan here does NOT mean the arc is active.** Many describe arcs that have long since shipped (or were superseded). Treat them as the *record of how something was designed*, not a to-do list. The canonical "what's active" is the `/CLAUDE.md` arc table.
- **Superseded designs stay.** When an arc pivots (e.g. a streaming design later replaced by download-only), the old spec is kept as history — newer ones note the supersession.

## Lifecycle (repo-cleanup convention, 2026-05-29)

New specs/plans should declare a status (`active` / `superseded` / `archived`) so this folder stays navigable. Older artifacts are being classified active-vs-archived with their owning agents (tracked under `REPO_STRUCTURE_CLEANUP_FIX_TODO`); until then, default to "historical reference unless the `/CLAUDE.md` arc table says otherwise."
