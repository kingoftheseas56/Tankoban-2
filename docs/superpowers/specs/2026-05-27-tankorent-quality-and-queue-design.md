# TANKORENT_QUALITY_AND_QUEUE — design spec

**Date:** 2026-05-27
**Owner:** Agent 4 (Stream + Tankorent)
**Scope:** Theatre only this arc. Comics + Books are future extensions owned by Agent 1 / Agent 2.

---

## Problems we are solving

Four pains in the current Tankorent + Theatre downloads flow:

1. **Season pack recognition.** Tankorent search results show full-season packs, multi-season bundles, and single episodes as visually identical rows. No way to tell at a glance which is which, no way to filter by kind.
2. **Source parity.** A search for the same term on Nyaa's website returns more (and often better) results than Tankorent's Nyaa indexer. Tankorent is silently dropping rows the user wants to see. Same gap likely exists on the other six indexers.
3. **Re-search after source change.** When the first batch of results is unsatisfying and the user switches to a different source, nothing re-fires. The only way to refresh is to retype the query.
4. **Parallel-by-default downloads.** Adding multiple downloads at once runs them in parallel, starving each one. Within a single show, the user wants episodes to download one at a time in episode order.

---

## Scope this arc

- Theatre series view + standalone Tankorent tab + Theatre Downloads sidebar page.
- Phase 1 lane queue covers Tankorent direct adds and Stream-server downloads (both Agent 4's domain).
- Comics + Books integration into the lane queue are **out of scope this arc** — owning agents (1 + 2) plug in on their own schedule via a published lane-queue interface.
- Standalone Tankorent top-level tab is preserved for power-user raw search; series-view sidebar becomes the primary path.

---

## Locked decisions

### Search behavior
- **Click-only.** No input change (typing, source switch, indexer switch, filter chip, sort) auto-fires a search. The Search button is the sole trigger.
- **Source switch clears.** Changing the indexer inside Tankorent immediately clears the result list and shows "Click Search to load." Forces explicit re-fire, removes ambiguity about which source produced which row.
- **Filter chip defaults to All.** No remembered state across queries.

### Season classification
- **Title heuristics only.** Regex over the torrent title detects multi-season bundles (`S01-S05`, `Complete Series`), single-season packs (`Season 2`, `S02`, `S02 Complete`), and individual episodes (`S02E01`, `S2E1`, etc.).
- **Three classes:** `MULTI_SEASON`, `SEASON_PACK`, `EPISODE`. Anything that does not match any of these stays unclassified and renders without a badge.
- **Pack episodes detected post-add.** When a pack-tagged torrent finishes adding to the engine and file metadata is available, each video file inside is parsed for episode number; queue lane orders them by that number (E01 → E02 → ... → fallback alphabetical if parse fails).

### Source parity audit
- **Nyaa first.** Strip all server-side and client-side filtering on the Nyaa path: seeder threshold, trust filter, any row-drop logic. Keep dedupe only. Re-sort by seeders descending. Result count returned to the user must match what Nyaa's own results page returns for the same query.
- **"N results from Nyaa" disclosure.** Visible above the result list. Honest count, no hidden filtering.
- **Other six indexers later.** Same pattern applied source-by-source after Nyaa proves out. Each one is a separate phase.

### Sequential download lanes
- **Per-show, not global.** The queue runs one transfer per show at a time. Across shows, lanes run in parallel.
- **Lane key = show identity.** IMDb ID for live-action, AniList ID for anime, book ID for books. Items resolved to the same ID share a lane. Items with no resolved ID get their own one-item lane and start immediately.
- **Packs occupy one lane.** A pack-tagged torrent takes one slot in its show's lane; the files inside download in detected episode order, never in parallel.
- **Queue controls:** pause current episode (lane does not advance until resumed), cancel queued or current item (lane advances if current was canceled), drag-reorder queued items, bump-to-front button on each queued item.

### Tankorent as a source-addon
- **Lives inside the Sources sidebar of the series view** (Theatre's show-detail page), alongside Torrentio and any future addons.
- **Expandable section.** Click the Tankorent header to expand; controls (indexer dropdown, filter chips, Search button) and results render inline within the addon's section.
- **Implicit query.** The show's title + season context drives the search; no big query input. Users override the implicit query via a small "edit query" affordance inside the addon panel.
- **Standalone Tankorent tab survives.** Reachable from the topbar for ad-hoc raw magnet searches with no show context.

### Theatre Downloads page (Netflix-clean revision)
- **One card per active show.** Card shows: poster, official show title, season + episode count, current episode's progress + speed, queued episode list ("Episode 2 · Queued / Episode 3 · Queued / +N more"), Pause + Cancel buttons.
- **No torrent metadata on this page.** Filenames, release groups, codec strings, quality tags are stripped. The card surfaces show-level + episode-level identity only.
- **Completed shows go below.** Leaner row: poster thumbnail, title, season summary. No file list.
- **Tankorent search page keeps raw titles.** That is where the user judges release quality; the cleanup is a Downloads-page concern, not a search-page concern.

### No cross-mode topbar indicator
- Downloads page is reached via each mode's sidebar entry. No persistent topbar chip showing active-download count. Removes UI noise; keeps the topbar focused on mode navigation.

---

## What the user sees

**Searching for a show.** Open Theatre, pick a show, the series view loads. The Sources sidebar on the right lists Torrentio and Tankorent stacked. Expand Tankorent. The indexer dropdown defaults to Nyaa. The filter chip is on All. The Search button is the trigger. Click it; results appear with `S1-S3` / `S2` / `EP` badges and a "142 results from Nyaa" honest count above the list.

**Switching indexer.** Click the indexer dropdown, change to PirateBay. The result list immediately clears and shows "Click Search to load." Click Search; PB results appear with badges and the new honest count.

**Filtering to packs.** Click the Packs chip. The list clears and shows "Click Search to load." Click Search; only `S1-S3` and `S2` badged results appear.

**Queuing downloads.** Click Download on a Season 2 pack of Daredevil. Click Download on a Season 4 pack of Invincible. Open the Theatre Downloads page from the sidebar. Two cards visible side by side — Daredevil card showing "Episode 1 · 47%" with Episode 2 / Episode 3 queued underneath, Invincible card showing "Episode 1 · 12%" with its own queue. Both progressing in parallel. Inside each card, episodes download strictly one at a time.

**Reordering.** On the Invincible card, drag Episode 3 above Episode 2 in the queue list. Episode 3 will run next when Episode 1 finishes. Or click Bump on a single queued item to promote it to position 2 instantly.

**Pausing.** Click Pause on Daredevil's current episode. The download halts; the lane does not advance to Episode 2. Click Resume to continue.

---

## How we know it works

- **Parity verified by count.** A query on Nyaa.si and the same query in Tankorent's Nyaa indexer return the same row count and (after re-sort by seeders) the same top results.
- **Lane discipline verified by smoke.** Queue Daredevil S02 pack + Invincible S04 pack. Two cards appear in Downloads. Both show one episode actively downloading; no episode-pair inside the same lane downloads concurrently. Cancel Daredevil; its card disappears, Invincible keeps running unaffected.
- **Pack badge accuracy verified by sample.** Run 50 sample queries across known shows; check badge classification against ground truth (a Complete Series torrent should always classify MULTI_SEASON, a single-episode torrent should always classify EPISODE).
- **Click-only verified by interaction.** Switch indexer, change filter, change sort — confirm result list never refreshes without a Search click.

---

## Rollout phases

1. **Per-show lane queue infrastructure** — `TransferQueue` keyed by show ID, with lane semantics + pause/cancel/reorder/bump. Tankorent direct adds + Stream-server downloads wire in. Comics + Books left untouched (their parallel behavior preserved until owning agents plug in).
2. **Nyaa parity** — strip filtering on Nyaa indexer; re-sort by seeders; honest count disclosure; smoke against Nyaa.si for parity.
3. **Tankorent as source-addon inside Theatre series view** — new addon panel beside Torrentio; indexer dropdown + filter chip + Search button + result list; implicit query from show context.
4. **Pack detection + badges + filter chip** — title-heuristic classifier; badge rendering; filter chip wiring; pack-internal episode ordering on add.
5. **Theatre Downloads page Netflix revision** — strip torrent metadata; one-card-per-show layout; episode-level queue inside each card; pause/cancel/reorder/bump controls.
6. **Other six indexer parity** — separate phase per indexer (PirateBay, 1337x, EZTV, YTS, TorrentsCSV, ExtTorrents) following the Nyaa pattern.

---

## Out of scope this arc

- Comics series-view Tankorent addon (future Agent 1 arc).
- Books series-view Tankorent addon (future Agent 2 arc).
- Cross-mode topbar download indicator.
- Pack detection signals beyond title heuristics (metadata-based detection, post-download verification).
- Smart filter chip defaults (auto-pick based on query content).
- Source-side metadata integration on indexers (e.g. Nyaa category tags) — title heuristics only, applied uniformly.

---

## Open items for the planning phase

- Concrete TransferQueue API surface — what does the lane interface look like? Likely a small class with `enqueue(showId, transfer)`, `cancel(transferId)`, `reorder(showId, oldIdx, newIdx)`, plus signals for lane state changes.
- Persistence shape — should the queue survive app restart? (Lean toward yes; restart should resume the lane at the same position.)
- Pack episode-order parsing — what regex covers the common cases (`S02E01`, `S2E01`, `02x01`, `Ep01`, `Episode 1`)? Bench against sample torrent file names.
- Nyaa parity probe methodology — what's the smoke harness that fetches the Nyaa page directly and compares row counts? Likely a small one-off script, not shipped.

These are planning-phase concerns, not brainstorm-phase concerns. The writing-plans skill will handle them.
