# TANKORENT_STREAM_INTEGRATION agent-driven smoke — findings

**Date:** 2026-05-15 ~10:35pm–10:45pm GMT+5:30
**Agent:** Agent 4 (Stream mode)
**Build:** post-Codex Phase F ship; `build_check.bat` returned BUILD OK ~10:00pm
**MCP LOCK:** claimed at chat.md (line ~3963) for the duration; released at end of this audit
**App PID:** 24772 (Tankoban), launched via `build_and_run.bat` with `--dev-control`
**Smoke recipe doc:** [docs/superpowers/plans/2026-05-15-tankorent-stream-integration-smoke.md](../../docs/superpowers/plans/2026-05-15-tankorent-stream-integration-smoke.md)
**Evidence:** 10 screenshots at `agents/audits/smoke_evidence/g2_01_*.png` through `g2_10_*.png`

## What was verified

### ✅ E5 — Videos sidebar entry removed
- `out\tankoctl.exe get-state` returns `navButtons: [comics, books, stream]` — Videos gone.
- Visual confirm in [g2_01_theatre_landing.png](smoke_evidence/g2_01_theatre_landing.png): topbar shows only Comics / Books / Theatre tabs.

### ✅ E3 — Theatre rename (user-facing)
- `out\tankoctl.exe open-page theatre` returns `activePageId: "stream"` — the theatre→stream alias in tankoctl.cpp works.
- Sidebar label visually reads "Theatre" not "Stream" (g2_01).
- Internal pageId stays "stream" (confirmed by get-state) — class names and IDs untouched, only user-visible strings flipped per the design.

### ✅ Library tile click → detail-view nav
- Click on the second tile in SHOWS & MOVIES row (native coords (300, 570)) opened the Star Wars: Maul - Shadow Lord detail view ([g2_05_library_tile_click.png](smoke_evidence/g2_05_library_tile_click.png)).
- Detail view renders: poster banner, title, meta line ("2026 · pilot · 24 min · Animation Action Adventure · Series · IMDb 6.1"), description, cast row, season selector, episode table, sources panel on the right with 2 visible torrent results from PirateBay + 1337x.
- "Remove from Library" button visible in top-right — show is library-resident.

### ✅ E1 — "Download via Tankorent" button on season header
- Visually confirmed in [g2_05](smoke_evidence/g2_05_library_tile_click.png): season row shows BOTH the new "Download via Tankorent" button AND the existing "Download Season" button.
- UIA `automation_elements rect` returned `left=634, top=573, right=902, bottom=618` — button is a real Qt QPushButton with proper AutomationId.
- Both buttons styled consistently (matching dimensions, padding, hover affordance via cursor).

### ✅ D1 + D2 — TorrentPackPicker opens with correct identity
- Click on "Download via Tankorent" → modal `TorrentPackPicker` opened ([g2_07_picker_opening.png](smoke_evidence/g2_07_picker_opening.png)).
- Window title reads: **"Download via Tankorent — Star Wars: Maul - Shadow Lord Season 1"** — proves the (imdbId, showName, season) tuple was correctly threaded from `StreamDetailView::onDownloadViaTankorentClicked` → `TorrentPackPicker` constructor.
- Status label: "**2 packs (sorted by quality x seeders)**" — indexer fan-out fired both query variations ("Season 1" + "S01") and the rerankAndRender path executed (D4's combined-score sort).
- Cancel + Download buttons present + correctly positioned.
- Cancel button click closed the modal cleanly and returned to detail view ([g2_08_post_cancel.png](smoke_evidence/g2_08_post_cancel.png)).

### ✅ Back navigation works
- Top-left "Back" button (UIA rect: 78/24/120/60) clicked → returned to Theatre library landing ([g2_09_back_to_library.png](smoke_evidence/g2_09_back_to_library.png)).

### ✅ Existing-arc preserved features still work
- Continue Watching strip populated with 5 in-progress items (Daredevil, Star Wars Maul, Invincible at 77%, Aang Movie, etc.). Logs confirm `STREAM_CONTINUE_TRACE` accepted 5 candidates from `allProgress` size=6 (1 skipped because not-in-library).
- DOWNLOADED chips visible on a couple of SHOWS & MOVIES tiles — bulk-cohort StreamDownloadIndex registrations from prior wakes survive.
- Phase F UnifiedProgressStore routing: the `STREAM_CONTINUE_TRACE` epKeys (`stream:tt6741278:s1:e1`, `stream:tt18923754:s2:e4`, etc.) are exactly the shape `parseStreamProgressKey` consumes → progress was read through the unified store at app boot.

## Findings

### F1 (Important) — Picker renders "No results returned" entries as fake torrent packs

**Symptom:** When the indexer fan-out returns zero real results, the picker still renders rows showing `No results returned · 0 seeders · 0 MB · score 12` ([g2_07_picker_opening.png](smoke_evidence/g2_07_picker_opening.png)). The status counter says "2 packs" — those are the bogus rows.

**Hypothesis:** TorrentPackPicker's `onIndexerResults` slot is being invoked with payloads whose `TorrentResult.title` is literally `"No results returned"` — most likely the empty-response branch of one of the indexers (PirateBay / 1337x / etc.) being parsed as a real result row. Each of the 2 query variations ("Star Wars: Maul - Shadow Lord Season 1" and "Star Wars: Maul - Shadow Lord S01") returned 1 such row → 2 fake packs.

**Confounders:**
- Star Wars: Maul - Shadow Lord is a 2026 show — possibly too new for real torrents to exist, making this an "indexers genuinely returned nothing" case where the picker's rendering of the empty-result message is at fault, not the indexers.
- Couldn't test with a second well-indexed show because the app exited mid-flight (see F2). The 2-pack count fingerprint suggests it's a per-query empty-result branch (2 queries → 2 fake rows), but a second show test is needed to confirm.

**Recommended fix surface:** In `TorrentPackPicker::onIndexerResults` (or wherever the indexer client's empty-result payload is shaped into `TorrentResult` records), filter out results with empty `magnetUri` or `infoHash`, and/or skip results whose `title` matches a no-results sentinel. The status label's "N packs" count should reflect only real packs after that filter.

**Severity:** Important, not Critical. The picker UX is still navigable — user can click Cancel and move on. No data corruption; no crash. But the rendering is misleading: a user sees "2 packs" and clicks a row → AddTorrentDialog opens with a magnet-less / unusable torrent. Worth fixing before any user-facing release.

### F2 (Process — non-blocking) — App exited during second-show probe; cross-app focus cascade suspected

**Sequence:**
1. After Cancel + Back, library re-shown ([g2_09](smoke_evidence/g2_09_back_to_library.png)).
2. Click at native (640, 570) intended for SHOWS & MOVIES 4th tile → focus landed on WhatsApp instead ([g2_10_second_show.png](smoke_evidence/g2_10_second_show.png)).
3. pywinauto-mcp `automation_windows focus` returned `(-2147220991, 'An event was unable to invoke any of the subscribers')` — same family of pywinauto internal subscriber-list error that the `automation_visual screenshot window_handle=...` call hit earlier.
4. By the time I'd composed the next probe via Win32 `SetForegroundWindow` fallback, the Tankoban PID 24772 was gone (`Get-Process -Id 24772` → not found).

**Best-guess cause:** The (640, 570) click landed on WhatsApp content (in the middle of a chat conversation, per the screenshot). That shouldn't have killed Tankoban. The two pywinauto focus errors suggest some kind of Win32 message-pump corruption during the focus-steal recovery attempt. Possible Tankoban crash; possible inadvertent close-via-keyboard; possible mediated by pywinauto's internal subscriber-list state. **Not reproducibly identified.**

**No crash artifact found:** `out\_player_debug.txt` / `sidecar_debug_live.log` not modified; no minidumps in `out\`. Either a clean exit triggered by stray click or a silent crash without a coredump.

**Severity:** Not a code finding — environmental / smoke-process issue. Worth flagging for future MCP-driven smokes: do `Win32::SetForegroundWindow` BEFORE every click as a defensive measure, since pywinauto-mcp's `automation_windows focus` is unreliable on this machine (see also the `automation_visual screenshot` SetForegroundWindow import error encountered earlier).

## What couldn't be verified (deferred to Hemanth visual-verify)

The agent-driven smoke deliberately stopped before committing to a real torrent download. The following still need eyes-on-screen confirmation:

- **D4 sorting behavior with non-zero real results.** Multi-season "Complete Series" packs pinned at top + per-pack `[WHOLE SHOW]` suffix + combined-score descending order. The picker rendered for a 2026 brand-new show with no real torrents, so D4's sorting couldn't be observed end-to-end.
- **AddTorrentDialog imdbId+season pre-fill.** Phase A1 added the 6-arg AddTorrentDialog constructor (`preFilledImdbId`, `preFilledSeason`). E1's slot constructs the dialog via this ctor on `packChosen`. Smoke didn't exercise this because no real pack was selectable.
- **Tankorent-bound download → onTorrentFinished → publishTankorentItemsForTorrent → StreamDownloadIndex registration → kColAction ✓ light-up.** This is the actually-load-bearing end-to-end test for the whole arc. Needs ~5–30 min of wall-clock for a real download.
- **UnifiedProgressStore resume after Tankorent-bound playback.** Open a downloaded local episode via the show-view, play 30s, close, reopen the same show-view, hover the episode — confirm the resume position persists by identity (imdbId, season, episode) not just by path.
- **Movie path (H2).** Movie show-view → "Download via Tankorent" button on movieActionRow → picker with season=0 sentinel → registerMovie via the largest-video fallback in publishTankorentItemsForTorrent.
- **Local files row (E4).** Whether the new "Local files" row at the bottom of the Theatre library landing actually surfaces folder tiles (visible scrolling-down would show this), and whether click → OS file manager works.

## Cleanup

- App PID 24772 already exited (cleanup not needed)
- No ffmpeg_sidecar.exe found in process list (verified post-exit)
- `scripts/stop-tankoban.ps1` ran in dry mode (no-op since nothing to kill)
- MCP LOCK released in chat.md after final RTC posts

## Arc state after this smoke

- All TANKORENT_STREAM_INTEGRATION src/ phases (A–F) shipped + verified at the unit-test + build levels
- G1 confirmed already-wired (MainWindow.cpp:673)
- G2 agent-driven UI-flow smoke partially completed — 7 of the 14 recipe steps confirmed; steps 7–14 (real download + completion + LOCAL chip light-up + playback) deferred to Hemanth visual-verify
- 1 Important finding logged (F1: empty-result rendering) — recommend a same-arc or near-arc follow-up to filter empty/bogus indexer rows
- 1 Process finding logged (F2: focus-cascade exit) — non-blocking, environmental

The arc closes when Hemanth's visual-verify lands GREEN. If F1 is judged a blocker, a fix-and-re-smoke pass is needed before close.
