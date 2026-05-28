# WeebCentral Volume-Scan Tick Markup — Discovery (Task 1)

- **Date:** 2026-05-28
- **Author:** Agent 1
- **Purpose:** Pin the violet/gray "volume-scanned" tick discriminator for the Volume X / quality-aware compilation plan (`docs/superpowers/plans/2026-05-28-comics-volume-x-quality-aware-compilation.md`, Task 1 → feeds Task 2).
- **Series probed:** One Piece — `01J76XY7E9FNDZ1DBBM6PBJPFK`

## Discriminator (the answer)

Each chapter row carries a small tick SVG. The `stroke` color on `<svg class="w-4 h-4" stroke="#...">` is the volume-scan signal:

- **Violet — volume-scanned (clean):** `stroke="#d8b4fe"` (Tailwind violet-300)
- **Gray — magazine (not in a volume):** `stroke="#4C4D54"`

A simple substring/regex match for `stroke="#d8b4fe"` within a chapter anchor's inner HTML = that chapter is volume-scanned. Absence (gray `#4C4D54`) = magazine.

## Where it lives in the markup

The tick is the first child of the chapter anchor, inside `<span class="me-2">`, BEFORE the `<svg>` block that `parseChaptersHtml` currently strips at step (2) (`rawInner.remove(svgBlockRe)`). Detection MUST run before that strip. Real gray row (path `d=...` elided):

```html
<a href="https://weebcentral.com/chapters/01KS85JSHZWV31NNZRW5D7AC35" class="hover:bg-base-300 flex-1 flex items-center p-2">
    <span class="me-2">
        <svg class="w-4 h-4" stroke="#4C4D54"  viewBox="0 0 24 24" fill="none" xmlns="...">
            <g stroke-width="0"></g>
            <g stroke-linecap="round" stroke-linejoin="round"></g>
            <g><path d="M8.5 12.5L..." stroke-width="2" stroke-linecap="round" stroke-linejoin="round"></path></g>
        </svg>
    </span>
    <span class="grow flex items-center gap-2">
        <span class="">Chapter 1183</span>
        <!-- conditional Last Read / new_chapter SVGs may follow -->
    </span>
    <time class="text-datetime opacity-50" datetime="2026-05-22T15:41:36.703Z">2026-05-22T15:41:36.703382Z</time>
</a>
```

A violet row is identical except `stroke="#d8b4fe"`.

## Critical: the scraper already fetches the full list

`WeebCentralScraper::fetchChapters` (`WeebCentralScraper.cpp:201`) fetches `/series/<id>/full-chapter-list`, NOT the series landing page. The landing page server-renders only the latest ~9 chapters; the full-chapter-list endpoint returns the complete set with ticks. So `parseChaptersHtml` already receives every chapter + its tick — no pagination/lazy-load work needed.

## Boundary sanity check (matches Hemanth's screenshot)

`/series/01J76XY7E9FNDZ1DBBM6PBJPFK/full-chapter-list` (fetched 2026-05-28, Chrome-134 UA + `Referer: https://weebcentral.com/`, HTTP 200, 12.1 MB):

- chapter anchors: **1183**
- violet (`#d8b4fe`): **1133**
- gray (`#4C4D54`): **50**

1133 + 50 = 1183. Violet runs through ch 1133; gray = 1134-1183. Exactly the boundary in Hemanth's screenshot. Confirms the ~50-chapter gray zone → Option A (numbered Magazine volumes + small Volume X tail), not a single giant Volume X.

## Task 2 inputs

- VIOLET token: `stroke="#d8b4fe"`
- GRAY token: `stroke="#4C4D54"`
- Detection point: in `parseChaptersHtml`, after `ch.source = source;` and BEFORE `rawInner.remove(svgBlockRe)` — set `ch.isVolumeScanned = rawInner.contains("stroke=\"#d8b4fe\"")` (or a cached `QRegularExpression`).
- Test fixture: a faithful inline 2-chapter snippet (one `#d8b4fe`, one `#4C4D54`) per the structure above — no large saved fixture needed; the endpoint HTML is 12 MB and not worth tracking.

## Robustness note

The color is a hardcoded hex in WeebCentral's template. If WeebCentral restyles, the hex could change — the parse should fail safe (no violet match → treat as gray/magazine → applies pairing tactics, which is harmless on a clean volume, just an unnecessary cover-alone break). Acceptable degradation; flag for re-probe if classification looks wrong after a WeebCentral redesign.
