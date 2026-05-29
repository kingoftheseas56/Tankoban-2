# Downloads Page Redesign — Design Spec

- **Date:** 2026-05-29
- **Owner:** Agent 4 (Stream + Tankorent); Comics half coordinated with Agent 1
- **Status:** Design — visual locked by Hemanth via mock-up; awaiting spec review → plan
- **Mock-up:** `docs/superpowers/mockups/2026-05-29-downloads-page-redesign/downloads-redesign.html` (side-poster tab is the chosen look)

## Problem

The Downloads page lists each finished episode by its **raw torrent filename**
(`Daredevil.Born.Again.S02E01_1080p_RHS.mkv`). It carries no show poster and no
real episode titles. It reads like a folder listing, not a library.

Root cause is a data gap, not a rendering bug: `StreamDownloadIndex::Entry`
([src/core/stream/StreamDownloadIndex.h:31-44](../../../src/core/stream/StreamDownloadIndex.h#L31-L44))
persists only `imdbId`, `type`, `season`, `episode`, `canonicalPath`, `addedAt`,
`fileSizeBytes`, `state`, `progressPct`. There is **no stored show title, poster,
or episode title** — so the page falls back to the filename
([StreamDownloadsPage.cpp:442-447](../../../src/ui/pages/stream/StreamDownloadsPage.cpp#L442-L447)).
The same gap exists on the Comics downloads view.

## Goals

1. Replace torrent filenames with clean, title-only rows:
   - Theatre: `S02E01 · Heaven's Half Hour`
   - Comics: `Vol. 1 · The Black Swordsman` (volume label; volume title if the catalog has one, else `Volume N`)
2. Show the **show/series poster** per group.
3. Keep the **Active** (in-progress) + **History** (finished) split.
4. Apply the same look to **Theatre and Comics** downloads pages.
5. Never regress: if metadata isn't available yet (or offline), the row degrades
   to a cleaned-up title — never the raw filename — with a placeholder cover.

## Non-goals

- Books downloads (out of scope this round).
- Episode thumbnail stills (title-only chosen).
- Big-banner layout (side-poster chosen; banner mock-up retained for reference only).
- Changing dispatch/download mechanics. This is a presentation + metadata-sourcing change.

## Locked visual design (side-poster)

Per-group card, grouped by show/series (one card per `imdbId` / manga series):

```
┌──────────┬──────────────────────────────────────────────┐
│          │  Daredevil: Born Again            (show title) │
│  poster  │  Season 2 · 8 episodes               (sub)     │
│  2:3     │  ── [Active only: progress bar + state] ──     │
│  ~96×144 │  S02E01 · Heaven's Half Hour          (row)    │
│          │  S02E02 · The Hollow of His Hand      (row)    │
│          │  + 3 more episodes                    (more)   │
└──────────┴──────────────────────────────────────────────┘
```

- Poster: 2:3, left-aligned, fills card height; rounded on the card's left edge.
- Rows: `code · title`, hover highlight, click = play (Theatre) / open reader (Comics) — unchanged click contract.
- Active cards add a thin grayscale progress bar + state line (`2 downloading · 1 queued`), reusing today's `refreshActive` aggregation.
- Palette/identity: existing dark theme, grayscale chrome, no colored text or emoji
  (per `feedback_no_color_no_emoji`). Poster artwork is the only color, and it is real image data.
- Long lists: cap visible rows per card (e.g. 5) with a `+ N more` affordance to keep the page scannable; expand on click. (Exact cap is a plan-phase detail.)

## Metadata sourcing — read-time enrichment with cache (chosen)

The page enriches each group **on open**, keyed by `imdbId` (Theatre) or series id (Comics):

**Theatre**
- **Poster:** read `…/Tankoban/data/stream_posters/<imdbId>` (the existing disk cache
  populated by the library/search widgets — see
  [StreamDetailView.cpp:2622-2640](../../../src/ui/pages/stream/StreamDetailView.cpp#L2622-L2640)).
  On miss, fetch the poster URL from series meta, write it to that same cache, repaint.
- **Show title + episode titles:** from series meta (Cinemeta `meta.name` +
  `videos[].title`, the same data `parseSeriesEpisodes` already reads —
  [MetaAggregator.cpp:176-214](../../../src/core/stream/MetaAggregator.cpp#L176-L214)).
  Look up by `imdbId`; reuse `MetaAggregator`'s `m_seriesCache` when warm, fetch
  on miss, cache an `imdbId → {showTitle, season+episode → episodeTitle}` map, repaint.
- **Wiring:** `StreamPage` owns both `m_metaAggregator` and the downloads page, so it
  injects the meta provider — same ownership used for the T5.2a anime-flag wiring.

**Comics**
- The local manga catalog already holds series cover + per-volume titles (the series
  view renders both via `QPixmapCache` keyed by URL —
  [ComicsSeriesView.cpp:1822-2065](../../../src/ui/pages/comics/ComicsSeriesView.cpp#L1822-L2065)).
  Enrichment is a **pure local lookup** by series id — no network. Reuse the existing
  cover loader; map downloaded volumes to their catalog titles.

**Fallback / offline (both modes)**
- While meta is unresolved or unreachable: title = `prettifyFilenameTitle(canonicalPath)`
  (already implemented, [StreamDownloadsPage.cpp:24-53](../../../src/ui/pages/stream/StreamDownloadsPage.cpp#L24-L53)),
  poster = grayscale placeholder. Never the raw filename. Snaps to real data when meta lands.
- Enrichment is async and idempotent; a repaint replaces placeholders in place.

### Approach considered & rejected: persist titles/poster at download time

Storing `showTitle` / `episodeTitle` / poster in `StreamDownloadIndex` at registration
time (when the dispatch flow already has the metadata) would make the page pure-offline.
**Rejected** because: (a) it does not backfill the user's *existing* history (Daredevil,
Community are already stored with no titles) — those would still need read-time enrichment,
so we'd build both paths; (b) it duplicates metadata the catalog/meta layer already owns;
(c) it forces a SQLite schema migration on `TorrentRepository`'s `stream_downloads_index`
table for cosmetic fields. Read-time enrichment backfills old + new uniformly and reuses
the poster disk cache that already exists.

## Components touched

- `StreamDownloadsPage` ([.cpp](../../../src/ui/pages/stream/StreamDownloadsPage.cpp)) —
  card layout rewrite (poster + title-only rows), meta-provider injection, async repaint.
- `StreamPage` — inject `MetaAggregator` (or a thin meta-resolver) into the downloads page.
- Comics downloads view (`ComicsPage` downloads tab / projection from commit `345f2c1`) —
  same card shape, local-catalog enrichment. Coordinate with Agent 1 (Comics owner).
- Possible small shared helper for the `imdbId → episode-title map` cache (plan decides
  whether it lives in `MetaAggregator` or a focused resolver).

## Testing / smoke

- Theatre: open Downloads after a fresh boot → Daredevil + Community show poster + real
  titles, no filenames. Offline boot → cleaned titles + placeholder cover (no filename).
- Active section: a show mid-download shows the progress bar + state line.
- Comics: open Downloads → series cover + `Vol. N · title` rows, no network needed.
- Click a row → plays / opens (unchanged contract).
- `tankoctl stream-get-downloads` snapshot still matches the rendered groups.

## Open items for the plan

- Exact home for the episode-title cache (MetaAggregator vs new resolver).
- Visible-row cap + expand behavior.
- Comics ownership boundary: how much Agent 4 implements vs hands to Agent 1.
