# Comics-Western Prototype — Design Spec (Foundation)

**Date:** 2026-06-04 · **Author:** Agent 1 (Comics) · **Companion mockups:** `.superpowers/brainstorm/723-1780587859/`
**Status:** **FOUNDATION LOCKED.** Discovery/home, download loop, cover-fallback, reader reuse, and first-slice scope are **OPEN — resume brainstorm next session** (see §6). Not yet ready for `writing-plans`.

---

## 1. Goal & context

Replicate the manga stack for **Western comics**, "one last time, systematically" (Hemanth, 2026-06-04). The manga stack that works: **one metadata brain (AniList) + high-quality packaged sources (Nyaa volume torrents) + a scrape fallback (WeebCentral) + a single first-class unit (the volume).** Mihon's per-site-adapter model was explicitly rejected as below our quality bar.

This session ran a systematic research pass (5 engines: Claude harness, Grok, ChatGPT, Gemini deep-research, + live system-access probing) and a real-file quality bake-off to settle every comics equivalent. Evidence:
- `agents/audits/comic_metadata_brain_verdict_2026-06-04.md` — the brain verdict (5-source convergence + live verification).
- `agents/audits/comic_source_quality_bakeoff_2026-06-04.md` — the source quality bake-off (real Saga files, measured).

## 2. The four pillars — manga → comics

| Pillar | Manga | Comics (decided this session) |
|---|---|---|
| Brain (catalogue/metadata) | AniList (free API) | **GCD** (live API + optional local dump) + **Open Library** (covers by ISBN) |
| High-quality source | Nyaa volume torrents | **GetComics** true-digital (DRM-stripped) |
| Fallback | WeebCentral scrape | **ReadComicOnline** (read-online, via headless browser) |
| Unit | Tankōbon volume | **TPB = the volume** (uniform) |

## 3. Locked decisions

### 3.1 Metadata brain — GCD + Open Library (no signup)
- **GCD** (`comics.org`): public REST API works with **no key** (live-verified: `/api/series/name/<q>/?format=json`, `/api/issue/<id>/?format=json`). ~2.1M issues, 212K series, all major publishers. Models collected editions as first-class series with ISBNs (live-verified: "Invincible Compendium" ISBN 978-1-60706-411-4; Saga TPB volumes). CC BY-SA 4.0 → cacheable/redistributable with attribution. Optional **full DB dump** (MySQL ~2.4 GB / SQLite ~4.1 GB) shippable locally for instant offline queries.
- **Open Library**: covers by ISBN — `https://covers.openlibrary.org/b/isbn/{ISBN}-L.jpg`, no key (live-verified: real JPEGs 50–66 KB). CC0. Fills GCD's gap (GCD ships metadata only, its own cover CDN is Cloudflare-blocked to us).
- **The chain:** GCD series → TPB volume + ISBN → Open Library cover. Entirely no-signup.
- **Known gap (→ §6):** OL cover coverage is partial (Saga missing vols 3,4,7–10 in testing). A **cover-fallback** is required.

### 3.2 High-quality source — GetComics true-digital
- Bake-off on real Saga #72 files (measured):
  - **GetComics TPB** (Zone-Empire): **2560×3936** (~10 MP), ~1.5 MB/page — **best/sharpest, genuine native detail**.
  - **GetComics single issue** (Zone-Empire): 1988×3056 (~6 MP), ~2.3 MB/page — least compressed at its size.
  - **RCO HQ** (rcostation): 1988×3056 (~6 MP), ~0.5 MB/page — full-res (NOT downscaled, overturns the WeebCentral assumption) but ~4× more compressed.
  - **Compendium** (EJGriffin): 1988×3056, ~1.0 MB/page — lowest tier.
- **GetComics ships both single issues AND TPBs, all true-digital.** It is the "Nyaa of comics."
- **RCO = read-online fallback.** Pullable reliably by driving a real browser (Edge `--headless --dump-dom` runs RCO's own JS → resolves the blogspot image URLs; version-proof, beats hand-cracking their daily-changing scramble). comic-dl (Xonshiz) is stale — do not use.
- **Known engineering gap (→ §6):** GetComics serves `.cbz/.cbr` through an ad-redirect/file-host layer. Automating that fetch is the one piece to productize (done manually this session to obtain test files).

### 3.3 Unit — TPB = the volume (uniform reading medium)
- **The TPB is the unit.** Best quality (above), full coverage (TPBs keep pace with publication — a new volume every ~6 issues), and a **uniform numbered sequence** (Vol 1…N) identical in shape to manga tankōbon.
- **Compendium dropped** — lowest quality tier AND it lags publication (orphan-issue problem). Hemanth: "TPB is king… uniform medium for reading."
- **No "Volume X" synthesis.** Recent issues are covered by real TPBs (e.g. Saga #67–72 = TPB v12), which are higher quality than a synthesized bundle would be. Synthesis would only ever apply to truly-uncollected bleeding-edge singles — deferred.
- Every series in the library reads as the same unit type — no per-series format variation.

### 3.4 Series page — mirror `ComicsSeriesView` exactly
Replicate the existing manga series view layout (`src/ui/pages/comics/ComicsSeriesView.cpp`):
- **Full-width banner** (`m_heroBannerLabel`, fixed height **170px**), hidden until painted.
- **Two-column `contentRow`** (spacing 16): `leftCol` **stretch 3** : `m_sourcesPanel` **stretch 2**.
- **leftCol:**
  - **Hero block** (`m_heroBlock`, HBox spacing 22): hero cover **90×135** (`kHeroCoverSize`) + text stack (title · creators byline · meta line · synopsis · show-more).
  - **Volume scroll** (`m_volumesScroll` → QVBoxLayout of `VolumeTile` rows, spacing 0): vertical list of volume rows.
  - **Download Selected** button (bottom-right).
- **`VolumeTile` row** (fixed height `kVolumeRowHeight` **124px**): `[checkbox 20×20] [number label w32] [cover 76×108] [title 13px/700 + synopsis 11px/0.54α] [state icon 28×28] [optional upgrade button]`.
- **Sources panel** (right): for the selected volume, lists sources — **GetComics digital (primary)**, **RCO read-online (fallback)**.

## 4. Build on existing code (refound, don't rebuild)
The Stremio spine already exists from the Tankoyomi volume-pivot + Western arc. Reuse it; swap the data/source layer to GCD+OL+GetComics:
- `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` — series page (above).
- `src/ui/pages/comics/VolumeTile.{h,cpp}` — the volume row unit.
- `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` + `ComicsSourceCard.{h,cpp}` — Sources sidebar.
- `src/ui/pages/comics/ComicsDownloadsPage.{h,cpp}` — downloads surface.
- `src/ui/pages/ComicsPage.{h,cpp}` — Western screen + `WesternCatalogLoader` (shipped Jun 1–2).

## 5. Architecture principle
Fat app shell + thin data/source adapters. The brain (GCD+OL) and source (GetComics/RCO) sit behind interfaces; the UI (series view, volume tile, sources panel) is unit-agnostic and already built. Adding a future source = one adapter, not a rewrite.

## 6. OPEN — resume brainstorm next session
These are intentionally undecided; this spec is the foundation, not the full design:
1. **Discovery / home** — how the user finds & adds a series. Candidate: mirror the manga "search-to-add" + library grid + continue-reading strip. (GCD search → result cards → series page → add to library.)
2. **Download loop** — tap volume → GetComics search → resolve through ad-redirect → `.cbz` → MangaDownloadIndex → reader. Includes the GetComics-redirect automation (§3.2 gap).
3. **Cover-fallback** — when Open Library has no cover: generate a title-card cover / scrape GCD or GetComics / neutral placeholder (§3.1 gap).
4. **Reader reuse** — confirm the existing comic reader (COMIC_READER_FIX_TODO Phase 6) is reused as-is for TPB `.cbz`.
5. **Scope / first vertical slice** — the first end-to-end flow to prove (candidate: Saga — search → series page → download Vol 1 → read).

## 7. Process gate (gov-v14)
Per `src/ui/pages/comics/CLAUDE.md`: before `/superpowers:writing-plans`, summon **Codex** (via `scripts/engines/`) to review-and-expand this brainstorm in place (orthogonal-model review). One pass, then writing-plans. The remaining OPEN items (§6) must be brainstormed and folded in first.

## 8. References
- `agents/audits/comic_metadata_brain_verdict_2026-06-04.md`, `agents/audits/comic_source_quality_bakeoff_2026-06-04.md`
- Memories: `project_comics_catalog_arc`, `project_western_richness_no_signup_ladder`, `feedback_stremio_for_manga_vibe`, `project_tankoyomi_volume_pivot_arc_2026-05-16`, `feedback_bigger_manga_covers`
- Domain: `src/ui/pages/comics/CLAUDE.md`
