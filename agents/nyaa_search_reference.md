# Nyaa Search Results — UI/UX Reference

Source: `https://nyaa.si/?f=0&c=0_0&q=one+piece` — saved HTML at `C:\Users\Suprabha\Downloads\one piece __ Nyaa_files\one piece __ Nyaa.html`. Captured 2026-04-14 by Hemanth as the visual + interaction model to appropriate for Tankorent's search engine.

This file describes Nyaa as it ships, not what we will build. Porting decisions live in a future plan.

---

## Page anatomy (top → bottom)

1. **Top navbar** (dark, full-width)
   - Brand: `Nyaa`
   - Left links: `Upload` · `Info ▾` (Rules / Help) · `RSS` · `Twitter` · `Fap`
   - Right: search controls (see §Search controls) and a `Guest ▾` user menu (Login / Register)
2. **Per-search info banner** *(conditional)*
   - Blue alert: `Click here to see only results uploaded by {user}` — appears when the query string matches a known username, links to `/user/{name}?q=...`
3. **Results table** (`<table class="torrent-list">`) — see §Columns
4. **Pagination bar** (centered): `«  1  2  3  4  5  …  13  14  »` plus `Displaying results 1-75 out of 1000 results.` line above
5. **Footer**: tiny `Dark Mode: Toggle` link

The table is the entire content area below the navbar. No sidebars, no facets, no hero image — everything is data-dense.

---

## Search controls (in navbar, right-aligned)

Three side-by-side widgets followed by a search button:

| Widget | Query param | Options |
|--------|-------------|---------|
| **Filter** dropdown | `f` | `0` No filter (default) · `1` No remakes · `2` Trusted only |
| **Category** dropdown | `c` | Hierarchical: `0_0` All · `1_0` Anime (`1_1` AMV / `1_2` English / `1_3` Non-English / `1_4` Raw) · `2_0` Audio (Lossless / Lossy) · `3_0` Literature (English / Non-English / Raw) · `4_0` Live Action (English / Idol-PV / Non-English / Raw) · `5_0` Pictures (Graphics / Photos) · `6_0` Software (Apps / Games) |
| **Text input** | `q` | Free-form query |
| **Search button** | — | Submits the form (GET to `/`) |

All controls are persisted in the URL — every navigation is bookmarkable. Form submission is a regular GET; no client-side AJAX.

Mobile/narrow viewport: the same three widgets stack vertically inside a collapsed navbar drawer.

---

## Columns (in display order)

| # | Header | Width | Content | Sortable? |
|---|--------|-------|---------|-----------|
| 1 | **Category** | 80 px | Category icon (`<img>` referencing `1_3.png` etc.); icon links to that category's filter view | No |
| 2 | **Name** | flex (auto) | Title text + tooltip with full name. If the row has comments, a small `💬N` link appears inline before the title | No |
| 3 | (comments icon header) | 50 px | Empty in body; column 2 absorbs the comment indicator | Yes (by comment count) |
| 4 | **Link** | 70 px | Two icons side-by-side: `📥` `.torrent` download, `🧲` magnet URI | No |
| 5 | **Size** | 100 px | Human-readable (`1.3 GiB`, `520.6 MiB`, `7.8 MiB`) | Yes |
| 6 | **Date** | 140 px | Local-time string `YYYY-MM-DD HH:MM`. `<td title="...">` shows relative time on hover (`"4 hours 57 minutes ago"`); `data-timestamp` attribute carries epoch seconds | Yes (default sort, descending) |
| 7 | **Seeders** | 50 px | Integer, header is `↑` | Yes |
| 8 | **Leechers** | 50 px | Integer, header is `↓` | Yes |
| 9 | **Downloads** | 50 px | Integer (completed download count), header is `✓` | Yes |

The **Name** column eats the comment-count column (`<td colspan="2">`) — visually it's one wide cell that may or may not contain a comment badge. The header still reserves the slot so column widths line up.

---

## Row classes (visual trust signal)

The `<tr>` element carries a Bootstrap context class that tints the row background:

| Class | Meaning | Example in the One Piece sample |
|-------|---------|---------------------------------|
| `default` | Anonymous upload | Most rows |
| `success` (green) | **Trusted uploader** | `[SubsPlease]`, `[Erai-raws]`, `[Kaerizaki-Fansub]` |
| `danger` (red) | **Remake / flagged** (e.g. duplicate of a trusted release, DMCA-prone) | `[ASW] One Piece - 1157` |
| `warning` (yellow, not present in sample) | Used elsewhere (anonymous report flagged) | — |

The class is server-decided based on uploader status and report state — clients don't compute it.

---

## Sorting behaviour

- Sortable headers carry one of two CSS classes:
  - `sorting` — sortable, currently inactive
  - `sorting_desc` / `sorting_asc` — currently the active sort, direction shown
- Each header is wrapped in an `<a>` tag whose `href` flips the `s` (sort key) and `o` (order) query params.
  - Sort keys observed: `comments`, `size`, `id` (used for date — newest first via `id desc`), `seeders`, `leechers`, `downloads`.
  - Default landing: sorted by `id desc` (newest upload first), shown as `sorting_desc` on the Date header.
- Clicking a header sets that key with `o=desc`. Clicking the *same* active header would normally toggle to `asc` — Nyaa's pattern: the link always points at the toggled direction relative to current state.
- Sorting is server-side via URL params. No client-side reorder; full page reload.

---

## Pagination

- 75 results per page (hard-coded server-side in the One Piece sample).
- Footer block: numeric pagination (`«  1  2  3  4  5  …  13  14  »`) with current page in `.active` state and prev/next disabled at boundaries.
- `«` and `»` use `class="previous"` / `class="next"`.
- `…` ellipsis is a separate disabled `<li>` between page 5 and 13 — large gaps elide.
- URL param: `p={n}` (1-indexed). Other params (`q`, `c`, `f`, `s`, `o`) carry across page changes.
- An info line above the pagination reports `Displaying results A-B out of N results. Please refine your search results if you can't find what you were looking for.` Total `N` capped at 1000 (Nyaa's known ceiling).

---

## Row-level interactions

- **Click the title** → navigates to `/view/{id}` (torrent detail page; out of scope for this doc).
- **Click `📥`** → downloads the `.torrent` file at `/download/{id}.torrent`.
- **Click `🧲`** → opens the magnet URI in the registered handler; magnet has the BTIH, dn (display name), and tracker list pre-baked.
- **Click the category icon** → filters to that category (`?c=1_3` etc.).
- **Click the comment badge `💬N`** → jumps to the detail page's comments anchor.
- **Hover the Date cell** → tooltip shows relative time.

No row-level multi-select, no bulk actions, no per-row context menu (right-click yields the browser default).

---

## Visual / styling cues

- Row striping (`table-striped`) — alternating subtle backgrounds on top of the trust-class tint.
- Hover row highlight (`table-hover`) — entire row brightens on mouseover.
- Bordered cells (`table-bordered`) — thin separators on every side of every cell.
- Text alignment: left for Category/Name, centered for everything else.
- Dark mode is a single CSS swap (alternate stylesheet, persisted in `localStorage` under key `theme`).

---

## State persisted to the URL

Every meaningful interaction is reflected in query params; nothing is held in browser state alone. Reloadable, shareable, bookmarkable.

| Param | Source | Example |
|-------|--------|---------|
| `q` | search input | `q=one+piece` |
| `c` | category dropdown | `c=1_2` (Anime - English) |
| `f` | filter dropdown | `f=2` (Trusted only) |
| `s` | sort key | `s=seeders` |
| `o` | sort order | `o=desc` |
| `p` | page index | `p=3` |

RSS feed is the same query with `page=rss` — `?page=rss&q=one+piece&c=0_0&f=0`. Useful aside: every search is also a feed.

---

## Implicit behaviours worth noting

- **No infinite scroll.** Classic paginated table.
- **No client-side filtering after load.** Re-filter = re-submit.
- **No live updates.** Result counts are at-load snapshots.
- **No cover art / thumbnails.** Pure text + small category glyphs.
- **No detail expansion in-place.** Detail lives on a separate page (`/view/{id}`).
- **Magnet URIs include trackers inline** — no separate "fetch magnet metadata" round trip.

---

## What this means for Tankorent (forward-looking notes — not a plan)

Just observations to anchor the future appropriation:

- Tankorent already aggregates results across multiple indexers (Nyaa, 1337x, PirateBay, EZTV, ExtTorrents, TorrentsCsv, YTS). The Nyaa pattern is single-source per page. We'd be building a **multi-source-per-table** UI on top of the same column shape.
- Sortable column headers are universal in Tankorent's domain — Tankorent currently uses an explicit Sort combo. The Nyaa pattern is more discoverable.
- Trust-class row tinting maps cleanly to our existing seeder-health concept (we already paint a green/yellow/red dot in the Seeders column). Either move the colour to the row tint, or keep both.
- The comment-count badge has no analogue in our scrapers — most don't expose comments. Drop the column.
- "Trusted only" filter would correspond to a Tankorent-side concept we don't have today. Either omit, or repurpose for "verified uploaders" if we ever add an allowlist.
- Magnet + .torrent dual-action per row matches Tankorent's existing Add flow but presented as two icons rather than a dialog.

End of reference.
