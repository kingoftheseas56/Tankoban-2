# Bridge D.0 — Dispatcher Delegation Refactor Commission Spec

**Commissioned by:** Agent 0 (Codex)
**Date:** 2026-05-19
**Schema:** stays v1.2 (no bump — pure structural refactor)
**Pre-requisite for:** D.1-D.6 (must land before any new bridge layer)

## Strategic intent

The `MainWindow::handleDevCommand()` if/else chain at v1.2 is already ~270 lines. Adding ~120-135 new commands across v1.3-v1.8 without refactoring would push it past 600 lines — unreadable, unmaintainable, prone to merge conflicts when concurrent Codex commissions touch the dispatcher.

This refactor introduces per-domain delegation: `MainWindow::handleDevCommand()` keeps inline implementations for the v1.0-v1.2 commands (preserve diff scope — don't move what already works), but adds a `forwardToDispatch(QObject* page, ...)` private method that routes v1.3+ commands (prefixed by domain: `books_`, `player_` for v1.4 deeper, `sources_`, `library_`, `ui_`, `system_`/`app_`/`settings_`/`jsonstore_`/`cache_`/`log_`/`network_`/`theme_`/`perf_`/`dev_`) to the per-domain page's `dispatchDevCommand(cmd, payload, reply)` method.

Each per-domain page (StreamPage, ComicsPage, BooksPage, VideosPage, TankorentPage, TankoLibraryPage) gets a stub `dispatchDevCommand` returning `false` (unknown command). The actual command bodies land in D.1-D.6.

## Files to touch (exact list)

- `src/devtools/DevControlServer.h` — doc comment update only (no API change)
- `src/ui/MainWindow.h` — add `bool forwardToDispatch(QObject* page, const QString& cmd, const QJsonObject& payload, QJsonObject& reply)` private method declaration
- `src/ui/MainWindow.cpp` — refactor `handleDevCommand`:
  - Existing v1.0-v1.2 commands STAY inline (no behavior change)
  - Add prefix-routing chain at the bottom for v1.3+ commands: detect prefix (`books_`, `comics_`, etc), look up the corresponding page via existing private member, call `forwardToDispatch`
  - If `forwardToDispatch` returns true, reply is populated; if false, fall through to existing `UNKNOWN_COMMAND` error
- `src/ui/pages/StreamPage.{h,cpp}` — add `bool dispatchDevCommand(const QString& cmd, const QJsonObject& payload, QJsonObject& reply)` returning false stub
- `src/ui/pages/ComicsPage.{h,cpp}` — same stub
- `src/ui/pages/BooksPage.{h,cpp}` — same stub
- `src/ui/pages/VideosPage.{h,cpp}` — same stub
- `src/ui/pages/TankorentPage.{h,cpp}` — same stub
- `src/ui/pages/TankoLibraryPage.{h,cpp}` — same stub

## Constraints

- **Additive only.** Existing v1.0-v1.2 commands keep their inline implementations exactly as-is. No reordering, no behavior changes, no consolidation of duplicated logic.
- **Schema string unchanged.** Schema in `ping` reply stays `"tankoban.dev.v1.2"` after this refactor. The schema bumps to v1.3 only when D.1 lands the first new command.
- **No new commands.** This refactor adds plumbing, not commands. `ping.commands` list is unchanged.
- **Build_check.bat = BUILD OK** before commit.
- **No tests.** Per Codex #4 Stage 3a, structural refactors of UI dispatch don't need GoogleTest scaffolding (the test would be running tankoctl smokes, which D.1+ handles).
- **No memory or CLAUDE.md edits.** Phase E1/E2 handles surface integration separately.
- **No worktrees.** Per `feedback_no_worktrees.md`, work on master directly.

## Verification (run before posting RTC)

1. `build_check.bat` = BUILD OK
2. Existing v1.0-v1.2 commands still respond correctly via tankoctl:
   - `tankoctl ping` returns schema `"tankoban.dev.v1.2"`
   - `tankoctl get-state` returns the existing state snapshot unchanged
   - `tankoctl comics-get-state` returns the existing comics state snapshot unchanged
   - `tankoctl get-torrents` returns the existing torrent list unchanged
   - At least 3 v1.0 + 3 v1.1 + 3 v1.2 commands smoke-passed
3. New unknown command (e.g. `tankoctl books-test-stub`) returns `UNKNOWN_COMMAND` error (stub returns false, fall-through preserved)
4. Scoped `git diff --check` clean (no whitespace warnings)

## RTC attribution

When done, post an RTC in agents/chat.md:

```
READY TO COMMIT - [Agent 0 (Codex), bridge dispatcher delegation refactor (D.0 of SKILL_AUGMENTATION_ARC)]: forwardToDispatch + per-page dispatchDevCommand stubs land before D.1-D.6 bridge layers fire | Skills invoked: [/superpowers:verification-before-completion, /simplify, /build-verify, /superpowers:requesting-code-review] | files: <full list>
```

## Memory pointers

- `project_dev_control_bridge.md` — bridge architecture + naming convention + extension procedure
- `project_codex_substrate_live.md` — Codex Trigger A/B/C/D pattern
- `docs/superpowers/specs/2026-05-19-brotherhood-skill-augmentation-design.md` — main spec, §Detailed catalog Track B + §Anti-patterns #4 (dispatcher delegation)
