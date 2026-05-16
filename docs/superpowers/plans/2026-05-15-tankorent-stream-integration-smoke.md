# TANKORENT_STREAM_INTEGRATION smoke recipe

**Status:** initial smoke executed 2026-05-15 ~10:30pm (Agent 4 via MCP). Hemanth visual-verify pending — see end of doc.

**Source plan:** [docs/superpowers/plans/2026-05-15-tankorent-stream-integration.md](2026-05-15-tankorent-stream-integration.md) (Phase G).

## Preconditions

- `build_check.bat` returns BUILD OK on current HEAD (latest verified: 2026-05-15 ~10:00pm post-Codex Phase F ship)
- `build_and_run.bat` launches Tankoban with `--dev-control` (sets `TANKOBAN_DEV_CONTROL=1` so `out\tankoctl.exe` can connect)
- Theatre tab visible in topbar (Stream → Theatre rename shipped Phase E3)
- No Videos sidebar entry (E5 shipped)
- Tankorent indexers reachable from the host network (at least one indexer enabled in Sources panel)
- For full end-to-end smoke: a Cinemeta-known free-to-distribute test torrent that has live seeders. **Big Buck Bunny (movie)** is the canonical choice — Cinemeta has it under `tt1254207`. If indexers don't return it, fall back to any Cinemeta-known series with seeded packs in your test indexer set.

## Phase G mapping

- **G1 (StreamDownloadIndex → TorrentClient wiring at construction)**: ✅ ALREADY SHIPPED. Verified at [src/ui/MainWindow.cpp:673](../../../src/ui/MainWindow.cpp#L673) (`torrentClient->setStreamDownloadIndex(m_streamDownloadIndex)`). Landed back in the STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 2 wiring; the new Phase A4 `publishTankorentItemsForTorrent` consumes the same already-wired pointer. No action needed.
- **G2 (Integration smoke + Hemanth visual verify)**: this doc + the agent-driven smoke + the Hemanth visual-verify ask.

## Agent-driven UI-flow smoke (Phase 1 — what the agent can verify mechanically)

This portion verifies the wiring + UI flow up to the moment the user commits to a real download. It deliberately STOPS at "AddTorrentDialog opens" — torrent completion + LOCAL-chip-lights-up are deferred to the Hemanth visual-verify portion (Phase 2 below) because real-download wall-clock latency (5–30 min) doesn't belong inside the agent loop.

### Pre-smoke

```
taskkill /F /IM Tankoban.exe       :: clean slate (Rule 1)
build_and_run.bat                  :: launch with --dev-control
```

Wait ~10–30s for app to be ready, then verify:

```
out\tankoctl.exe ping              :: expect schema=tankoban.dev.v1 + ok=true
out\tankoctl.exe get-state         :: confirm app responsive=true
```

Claim MCP LOCK in `agents/chat.md` per Rule 19 before any pywinauto-mcp UI clicks.

### Recipe (UI flow only)

1. `out\tankoctl.exe ping` — assert schema reply
2. `out\tankoctl.exe open-page theatre` — assert `activePageId` flips to `stream` internally (page-id mapping E3 alias)
3. `out\tankoctl.exe get-state` — capture nav button states; confirm "Theatre" label visible, "Videos" label absent (E5)
4. `pywinauto-mcp automation_visual` — full-window screenshot for evidence
5. `pywinauto-mcp` search-bar focus + type a Cinemeta-known title (e.g. "Big Buck Bunny" / "Sopranos") + Enter
6. Wait ~3–5s for Cinemeta dispatch + result tiles to render
7. `pywinauto-mcp` click the matching show/movie tile
8. Wait for detail-view paint; screenshot
9. For series: assert `m_seasonRow` is visible and contains both "Download Season" + "Download via Tankorent" buttons
   For movies: assert `m_movieActionRow` is visible and contains both "Stream"/"Play"-equivalent and "Download via Tankorent" button (H2)
10. `pywinauto-mcp` click "Download via Tankorent"
11. Wait for `TorrentPackPicker` modal to appear; screenshot
12. Wait ~30s for indexer fan-out (status label morphs from "Searching N queries..." to "N packs (sorted by quality x seeders)")
13. Verify list is non-empty (or document indexer-down-no-results gracefully)
14. Verify multi-season "Complete Series" packs are pinned at top with "[WHOLE SHOW]" suffix (D4)
15. `pywinauto-mcp` select the smallest test pack (lowest seeders+size combo); click Download button
16. Wait for `AddTorrentDialog` to appear; screenshot
17. Verify imdbId + season fields are pre-filled (via Phase A1 ctor variant — visible in dialog header or via tankoctl introspection if exposed)
18. **Click Cancel** to abort. This concludes the UI-flow smoke without committing to a download.
19. `pywinauto-mcp` back-nav to Theatre library

### Post-smoke

```
powershell -NoProfile -File scripts/stop-tankoban.ps1   :: Rule 17
```

Release MCP LOCK in `agents/chat.md`.

## Hemanth visual-verify (Phase 2 — what only eyes can confirm)

@Hemanth — TANKORENT_STREAM_INTEGRATION v1 is wired end-to-end. The agent-driven smoke above proves the UI flow up to AddTorrentDialog opening; the actually-downloads-something + LOCAL-chip-lights-up portion needs a real session.

**Recipe (one open, one wait, one click):**

1. Open Tankoban via `build_and_run.bat`.
2. Theatre tab → search for a show you actually want (e.g. "Sopranos" if you have it on your indexer list; or any other Cinemeta-known series).
3. Click the show → click "Download via Tankorent" on a season header (e.g. Season 1).
4. Pick a small-enough pack to download in <15 min. Click Download.
5. Wait for completion. The torrent will appear in the Tankorent tab too (it's still a Tankorent download).
6. Once done, navigate back to the Theatre show-view.
7. Confirm: the downloaded episodes now display a ✓ in the action column (the kColAction column — per Hemanth's 2026-05-12 NETFLIX_OVERHAUL P3 "tick-as-action" UX). Hover shows "Downloaded — options".
8. Click one of those downloaded episodes. Confirm: VideoPlayer opens with the LOCAL file (not a magnet stream). `tankoctl get-player` should show the local file path, not a magnet URL.
9. After playing for 30+ seconds, close player. Reopen show-view, hover the same episode. Resume position should persist (Phase F UnifiedProgressStore via the (imdbId, season, episode) key).

**Things to look out for** (these are the unknowns that smoke can't easily catch):

- **Missing ✓ marker.** If downloaded episodes don't show the action ✓, the `publishTankorentItemsForTorrent` route didn't fire OR the BulkPackVerifier filename-regex didn't match the downloaded files' real names. Capture tankoctl logs.
- **Wrong file binding** (e.g. clicking S6E3 plays S6E4). Indicates a filename-parsing bug in BulkPackVerifier::matchEpisodeFileForSeason for the specific naming convention the pack used.
- **Pack picker sorting feels off.** D4's `combinedScore` uses default weights (`qualityWeight=0.6`, `healthWeight=0.4`). If a 720p high-seed pack beats a 1080p low-seed pack and that feels wrong, the slider is in Settings → Theatre.
- **Settings slider not affecting sort.** D5 is referenced but not in any shipped phase (D5 didn't ship); if you want to tune the weight without code, it'd be a follow-up task.
- **Movie path (H2)**: if you click a movie show-view → "Download via Tankorent" → small movie pack → after download, the LOCAL chip should appear in the movie action row. Same UnifiedProgressStore resume semantics, identity-keyed by `(imdbId, 0, 0)`.

If anything looks off, screenshot + post to chat. We iterate.

## Cleanup

- `scripts/stop-tankoban.ps1` always runs after agent-driven smoke (Rule 17)
- ffmpeg_sidecar.exe also killed if it spawned during episode playback
- No tankoctl session left dangling

## Arc state after this smoke

- All phases A → F shipped src/-side
- G1 verified already-shipped at MainWindow.cpp:673
- G2 Phase 1 (agent-driven UI-flow smoke) — see findings in `agents/audits/tankorent_stream_integration_smoke_2026-05-15.md`
- G2 Phase 2 (Hemanth visual-verify) — pending

The arc closes when Hemanth's Phase 2 smoke verdict is GREEN. If it lands clean: archive `TANKORENT_STREAM_INTEGRATION_FIX_TODO.md` (if one is authored) + this smoke doc to `agents/_archive/`.
