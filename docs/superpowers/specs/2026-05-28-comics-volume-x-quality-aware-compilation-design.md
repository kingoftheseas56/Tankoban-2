# Comics Volume X / Quality-Aware Volume Compilation — Design

- **Date:** 2026-05-28
- **Author:** Agent 1 (Comic Reader + Tankoyomi)
- **Status:** Design approved (Hemanth, 2026-05-28); ready for implementation plan
- **Domain:** `src/core/manga/`, `src/ui/pages/comics/`, `src/ui/readers/`
- **Related:** TANKOYOMI_VOLUME_PIVOT arc; `feedback_stremio_for_manga_vibe` (volume-is-first-class)

---

## 1. Problem

WeebCentral serves manga as individual chapters. Some chapters are sourced from pristine English **volume scans** (uploaders like 1r0n / danke-empire compile a released tankobon and split it into chapter files); the rest are **magazine-quality** scans of chapters not yet collected into any volume. WeebCentral marks the volume-sourced chapters with a **violet tick**; magazine chapters get a **gray tick**.

Today the Comics series view renders volume rows straight from the MangaFire catalog (`populateVolumeRowsFromCatalog`) and has no concept of:

- which chapters are clean volume scans vs magazine scans,
- how to present the ~50+ chapters that exist past the last collected volume (currently invisible / unhandled),
- the fact that stitching magazine chapters into a synthetic volume breaks two-page pairing at chapter boundaries (the chapters were never laid out as a cohesive volume).

This spec defines a quality-aware volume system that uses the WeebCentral tick + MangaFire catalog boundaries to produce clean volumes, magazine volumes, and a single bleeding-edge **Volume X** — each correctly paired and honestly labeled.

## 2. Goal (user-end)

When you open a Comics series:

- Every volume available as a proper English-volume scan reads at full quality, no markings.
- Chapters not yet in a volume but with a known volume boundary group into navigable, volume-sized buckets, each tagged **"RAW SCAN"**.
- The newest chapters past all volume boundaries land in a single **"Volume X"** at the end of the list, also tagged RAW.
- All of it is readable. Synthetic (magazine) volumes display each chapter's first page alone (cover-style) so reading isn't visually jumbled at chapter boundaries.
- When a rough volume later gets a clean scan, you're offered a one-click re-download to upgrade.

## 3. Two data sources, two dimensions

The design composes two independent signals:

1. **Volume boundaries — MangaFire catalog** (already in use). Each catalog volume carries `chapterStart`/`chapterEnd` (Japanese tankobon structure). Defines which chapters belong to Volume N. Loaded by `LocalMangaCatalogLoader`; rendered by `ComicsSeriesView::populateVolumeRowsFromCatalog`.
2. **Quality — WeebCentral violet/gray tick** (new). Each chapter row on the WeebCentral series page carries a tick whose markup/CSS class distinguishes volume-scanned (violet) from magazine (gray). Captured by `WeebCentralScraper` as a per-chapter `isVolumeScanned` flag. **Parsed from the chapter-row markup (class/attribute), never pixel color.**

These compose: a *boundary* (which chapters form a volume) × a *quality verdict per chapter* (clean vs magazine).

## 4. Volume classification (core logic)

For a given series, after loading the MangaFire catalog volumes and the WeebCentral per-chapter quality map:

- **For each catalog volume**, inspect the quality of its member chapters:
  - **All member chapters violet** → **Clean volume.** Real English-volume scan. Stitches cleanly. No pairing tactics. No badge.
  - **Any member chapter gray** → **Magazine volume.** "RAW SCAN" tag. Chapter-pairing tactics applied.
- **Chapters past the last catalog volume's `chapterEnd`** (no tankobon boundary exists yet), up to the latest chapter WeebCentral lists → **Volume X.** Single bucket, labeled "Volume X", placed at the end of the volume list. "RAW SCAN" tag. Chapter-pairing tactics applied.

Decision: a catalog volume with a **mix** of violet + gray chapters is classified **Magazine** (any gray → RAW + pairing). "Clean" requires *all* member chapters violet.

## 5. Compilation & pairing

Compilation continues to flow through `WeebCentralVolumePacker` (stitch chapter image-files → cbz).

- **Clean volumes:** stitch → cbz. **No `.volx` marker.** Read with normal global pairing.
- **Magazine volumes + Volume X:** stitch → cbz **+ write the `.volx` sidecar marker.** The reader detects the marker and applies chapter-boundary pairing (each chapter's first page shows alone, cover-style; the rest of the chapter pairs fresh).

**Trigger broadening (the one change to already-built work):** the `.volx` marker currently fires only when `volumeNumber == kVolumeXNumber`. It must now fire for **any gray-sourced compilation** — i.e., any Magazine volume *or* Volume X. The packer needs to know, per request, whether the volume being packed is gray-sourced. This is carried on `VolumePackRequest` (e.g., a `bool isMagazineSourced` / `bool needsChapterPairing` flag set by the dispatch based on the classification in §4).

The chapter-pairing engine itself (`buildTwoPagePairs` + `TwoPagePairingPage::isChapterStart` in `ComicReader.h`, plus the reader's `.volx` detection + `<chapter>_<page>` filename parsing in `pairingPages()`) is **already built and verified** (12/12 `ComicReaderPairing` unit tests). No change needed to the engine; only the *trigger* (when the marker is written) broadens.

## 6. UI

In `ComicsSeriesView` volume rows:

- **"RAW SCAN" tag** on Magazine volumes and Volume X. Small text badge on the row. Clean volumes render untagged.
- **Volume X** labeled "Volume X", placed at the end of the volume list (follows the existing `isVolumeX` / kVolumeXNumber convention in `AniListVolumeMapper`).
- Clean volumes render exactly as today.

## 7. Upgrade lifecycle

On series refresh (series-open + manual refresh), the violet/gray tick map is re-scraped:

- If a volume previously classified **Magazine** is now **all-violet** (a clean scan dropped), surface a per-volume **"update available"** affordance offering a re-download to the clean scan.
- The upgrade is **offered, not silent** — the user clicks to re-download. No automatic background replacement.

## 8. Scope

Full end-to-end, one spec: WeebCentral tick scrape → quality map → volume classification → compilation with broadened pairing trigger → "RAW SCAN" tag → Volume X bucket → upgrade-offer affordance.

## 9. Caching & refresh

The violet/gray quality map is **cached per series** (alongside or within the existing catalog cache), **refreshed on series-open and on manual refresh**. Not re-fetched on every render. A stale map degrades gracefully: a chapter mis-classified as gray gets unnecessary (but harmless) pairing tactics; mis-classified as violet skips pairing (the pre-existing behavior).

## 10. Already built (execution layer)

- `buildTwoPagePairs` chapter-local parity + `TwoPagePairingPage::isChapterStart` (`ComicReader.h`).
- Reader `.volx` detection (`m_isVolumeX` in `openBook`) + chapter-start tagging from `<chapter>_<page>` filenames (`pairingPages()`).
- `WeebCentralVolumePacker` `.volx` sidecar write (currently gated on `kVolumeXNumber`; this spec broadens the gate).
- 12 passing `ComicReaderPairing` unit tests (8 original + 4 chapter-boundary).

## 11. Acceptance criteria

1. WeebCentralScraper exposes a per-chapter `isVolumeScanned` flag parsed from chapter-row markup.
2. A series whose catalog volumes are all-violet shows clean volumes, no RAW tag, no `.volx`, normal pairing.
3. A series with gray chapters shows: clean volumes (untagged) + Magazine volumes (RAW tag, `.volx`, chapter-paired) + a single Volume X (RAW tag, `.volx`, chapter-paired) for chapters past the last catalog volume.
4. Opening a Magazine volume / Volume X in double-page mode shows each chapter's first page alone (cover-style); a color-spread chapter cover shows alone as a spread; clean volumes read unchanged.
5. After a magazine volume gains a full violet set on refresh, a per-volume upgrade affordance appears and re-downloads the clean scan on click.
6. All existing `ComicReaderPairing` tests stay green; new tests cover the classification logic (clean / magazine / Volume X bucketing) as pure logic where feasible.

## 12. Verify during build (not blockers)

- MangaFire catalog volume-list currency vs the gray zone's Japanese volumes. If the catalog lags, gray-but-Japanese-collected chapters fall into Volume X rather than numbered Magazine volumes — degraded granularity, not broken.
- Exact WeebCentral chapter-row markup for the violet tick (class/attribute name).

## 13. Out of scope (YAGNI)

- Japanese-vs-English volume *numbering reconciliation* beyond using whatever number the catalog provides (no separate "English numbering" overlay).
- Image-quality heuristics (we trust WeebCentral's tick, not pixel inspection).
- Multi-source quality arbitration (WeebCentral tick is the single source of the quality verdict for v1).
- Auto/background re-download (upgrade is offer-only).
