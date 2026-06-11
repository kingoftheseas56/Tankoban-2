# Theatre Downloads Overhaul — Design Spec

**Date:** 2026-06-11 · **Owner:** Agent 4 (Theatre/stream + Tankorent) · **Status:** Hemanth-approved design, spec for review
**Arc name:** `THEATRE_DOWNLOADS_OVERHAUL_V2`
**Brainstorm:** visual-companion session (`.superpowers/brainstorm/`), decisions recorded below verbatim.

---

## 1. Why (the four pains, Hemanth 2026-06-11)

Hemanth named all four download pains as real:

1. **Can't control them** — once started, no UI pause/cancel/resume/reprioritize (Downloads page is read-only v1).
2. **Can't see what's happening** — no at-a-glance speed/progress/queue/stuck view.
3. **Starting is clunky** — specifically: **season/batch flow is weak** and **no feedback at the moment of click**. (Per-episode pick-first flow is fine as shipped 2026-06-10.)
4. **The finish is invisible** — completion/failure discovered only by going back and looking.

## 2. Decision log (locked with Hemanth, 2026-06-11)

| Question | Decision |
|---|---|
| Scope | **Theatre only** (no Comics/Books unification) |
| Control depth | **Full power visible** (qBittorrent energy, Theatre skin) |
| Ambient awareness | **None** — no toasts, no topbar/sidebar indicators; the Downloads page is the truth |
| Downloads page vs Tankorent | **Downloads = Theatre command center** (show-shaped); **Tankorent = raw utility** (unchanged) |
| Row unit | **Episode-first, torrent expandable** |
| Page layout | **B — Master–Detail** (list left, deep-dive right) |
| Season flow | **A with C's packs — Season Checkout, pack-first** |
| Click feedback | **Button + row morph instantly** (no chip, no toast) |
| Left-list organization | **Status sections (Failed → Active → Queued → Completed), show-grouped inside** |
| Parallelism | **3 concurrent default**, knob on the page |
| Finish/fail on page | **Completed section with Play** + **Failed loud with one-click Retry** + **history auto-trims** |
| Speed graph tab | **Deferred** to a polish pass; v1 = numeric speed/ETA readout |

## 3. User-visible design

### 3.1 Downloads page — Master–Detail command center

Replaces the read-only `StreamDownloadsPage` content (same sidebar entry, Theatre mode).

- **Top strip:** total down-speed · active count · `Pause All` · `Resume All` · `Clear Done` · `Max active: [3 ▾]`.
- **Left column (list):** every Theatre download as an **episode row** — poster thumb, `Show · S01E12`, mini progress bar, status glyph. Organized in **status sections**: `Failed` → `Active` → `Queued` → `Completed`; inside each section, episodes cluster under their show header. Season-pack batches appear as one expandable batch row; its section follows this aggregate rule: **Active** while any child is downloading or queued · **Failed** when all children are terminal and at least one failed · **Completed** only when every child completed. (A batch with failures inside also shows a red child-count badge while still Active.)
- **Right pane (detail of the selected item):**
  - Header: episode/batch title, big progress bar, numeric **speed / ETA / peers / size**.
  - Controls: `Pause` / `Resume` / `Cancel` / `Retry` (failed) / `Bump to top` (queued) / **`Play`** (completed).
  - Tabs: **Files / Peers / Trackers** — reusing the Tankorent property widgets (`TorrentFilesTab` / `TorrentPeersTab` / `TorrentTrackersTab`) pointed at the item's underlying torrent.
  - Selecting a **batch** shows batch-level progress + per-episode breakdown list; selecting a child episode shows that episode against its carrying torrent.
- **Failed section:** top of list, red accent, `Retry` re-runs the source pick for that episode (same auto-pick path as a fresh download; the failed transfer is cleaned up first).
- **Completed section:** `Play` routes through the existing local-file playback gateway. **Auto-trim:** completed entries older than 30 days leave the list (record stays in `StreamDownloadIndex`; this is display trim, not data deletion). `Clear Done` clears the section immediately.

### 3.2 Season Checkout — pack-first

`Download Season N` (and `Download Selected`) opens a **checkout panel** instead of silently dispatching:

1. **Pack candidates on top:** best season-pack torrents (from the existing pack search machinery) with size, seeders, and **episode coverage** (e.g. "E01–E12 ✔ complete" / "E01–E10 partial"). Default-selected: best coverage·health pack.
2. **Gap rows below:** episodes the selected pack does NOT cover get per-episode auto-picked sources (existing `AutoSourcePicker` path), each row editable (`change` swaps to that episode's source list).
3. **Already-owned episodes** are greyed "already have" and excluded.
4. **Footer:** `N episodes · X GB total` + **`Queue all`**. Nothing downloads until this click.
5. Queued result lands on the Downloads page as **one batch unit** (the pack torrent + any gap episode transfers grouped).

### 3.3 Click-time feedback (all download entry points)

The instant any Download affordance is clicked: the button **morphs to `Queued ✓`** (disabled) and the episode row flips to the new **`Queued`** display state. This covers per-source Download buttons, episode-row download affordances, and checkout `Queue all` (every covered row flips).

### 3.4 Queue semantics

- Default **3 transfers actually running**; everything else `Queued`, feeding in as slots free.
- `Bump to top` promotes within the queue; `Pause All` / `Resume All` act globally.
- The knob (1 / 2 / 3 / 5 / unlimited) persists in settings.

## 4. Architecture grounding (what exists vs what's new)

### Reused as-is or lightly extended
- **`TransferQueue`** (`src/core/queue/`) — shipped engine: `enqueue / pauseCurrent / cancel / reorder / bumpToFront`, `laneChanged` / `itemStateChanged` signals. **Caveat (verified 2026-06-11):** it is built around **per-show lanes**; the global "max 3 active" needs a small global-concurrency scheduler layered on it (count running across lanes; gate lane starts). That is a queue-core change → TDD + review.
- **`EpisodeDisplayState`** (`src/core/stream/`) — already `NotDownloaded / Downloading / Paused / Failed / Downloaded`. **Add `Queued`** (+ `EpisodeStateInputs.queued`), slotting into the existing priority chain. Pure logic → extend the existing GoogleTests.
- **`StreamDownloadIndex` + `TorrentClient`** (`streamBulkGroups()` / `streamBulkSnapshotForImdbSeason` / per-episode progress, now off-GUI-thread per `bc179a1`) — the data feed, as the current read-only page already consumes it.
- **Tankorent property widgets** (`TorrentFilesTab` / `TorrentPeersTab` / `TorrentTrackersTab`) — re-parented into the right pane; they already take a torrent handle/snapshot.
- **Pack machinery** (`UnifiedPackSearchEngine`, `PackClassifier`, `BulkPackVerifier`, `StreamBulkPlan`, `BulkSourceCollector`) — drives checkout pack candidates + coverage math. `TheatreDownloadPanel` (1332 lines, currently unmounted since the streaming restore) is **mined for parts, not revived** — checkout is a new widget.
- **`AutoSourcePicker` + `finishAutoDownloadPick` download branch** — per-episode gap picks in checkout.
- **Metadata enrichment** — `fetchMetaItem(imdbId, "series")` poster/title pattern already proven on the read-only page.

### New components (each independently testable)
| Component | One purpose | Boundary |
|---|---|---|
| `DownloadsCommandModel` | Aggregate TorrentClient/StreamDownloadIndex/TransferQueue state into the status-sectioned, show-grouped episode list the page renders | in: signals from the three sources · out: a flat list of row structs + per-item detail struct |
| `DownloadsMasterDetailPage` | The page UI (top strip, left list, right pane shell) replacing StreamDownloadsPage's body | consumes `DownloadsCommandModel`; emits user intents (pause/cancel/retry/bump/play/clear) |
| `DownloadDetailPane` | Right pane: header, numeric stats, controls, hosts the three reused tabs | in: one item's detail struct + torrent handle ref |
| `SeasonCheckoutPanel` | The pack-first checkout (candidates, gap rows, owned-grey, total, Queue all) | in: imdbId+season(+preselected eps) · out: a `CheckoutPlan` {pack choice, gap picks} handed to dispatch |
| Global-concurrency scheduler (inside/atop `TransferQueue`) | Enforce max-N running across lanes | queue-core change; TDD |

### Behavioral changes to existing code
- `Download Season` / `Download Selected` handlers → open `SeasonCheckoutPanel` instead of silent bulk dispatch.
- All Download affordances → immediate `Queued ✓` morph + row-state flip on click.
- Retry (failed) → cleanup failed transfer, re-run auto-pick for that episode.
- Completed `Play` → existing `playLocalFileFromStreamRequested` → MainWindow gateway.

## 5. Data flow (one line each)

- **State in:** TorrentClient progress/alerts + StreamDownloadIndex records + TransferQueue lane state → `DownloadsCommandModel` (coalesced, GUI-thread-cheap per the `bc179a1` pattern) → page renders.
- **Intent out:** page emits pause/cancel/retry/bump/play → routed to TransferQueue / TorrentClient / playback gateway — page never touches libtorrent directly.
- **Checkout:** pack search + coverage → plan rows → `Queue all` → batch enqueue via existing bulk dispatch + TransferQueue.

## 6. Error handling

- Transfer error → `Failed` section + red row + Retry (already a display state; now it gets a surface).
- Retry failure (no sources found) → row stays Failed with "no sources found" subtitle — no loops, no popups.
- Pack search empty in checkout → checkout degrades to all-gap (per-episode) rows with a "no season packs found" note; still one `Queue all`.
- Engine/queue signal gaps (app restart mid-download) → model rebuilds from StreamDownloadIndex + TorrentClient snapshots on page open (same reconcile the read-only page does today).
- Mechanics (review C1, 2026-06-11): failure surfacing is driven by the INDEX state (`Entry::Failed`) plus the torrent-error path freeing the queue slot (`finishCurrent(Failed)` for the lane head, `cancel` for pre-start items) — lane items are erased on terminal states, so lanes never carry Failed.

## 7. Testing

- `EpisodeDisplayState` + `Queued`: extend existing pure-logic GoogleTests.
- Global-concurrency scheduler: TDD (the queue already has a per-method test suite to extend).
- `DownloadsCommandModel`: pure aggregation → unit tests with fake inputs.
- UI: tankoctl dev-bridge smoke (page snapshot commands) + Hemanth visual smoke per phase gate.

## 8. Out of scope (explicit)

- Speed **graph** tab (polish pass later; v1 is numeric readout).
- Toasts / topbar / sidebar ambient indicators (declined).
- Comics/Books downloads unification (Theatre-only).
- Tankorent page changes (stays the raw utility).
- Reviving `TheatreDownloadPanel` as-is (parts donor only).

## 9. Open items for the plan stage

- Exact placement/animation of the checkout panel (slide-over vs modal) — pick at implementation with the existing right-pane slide pattern as default.
- `Queued ✓` morph styling — match the gray/white palette rule (no color accents beyond the existing red-for-failed).
- Auto-trim constant (30 days) — settings-backed, named constant.
