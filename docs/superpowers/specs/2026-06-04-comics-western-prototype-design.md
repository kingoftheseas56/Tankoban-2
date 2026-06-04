# Comics-Western Prototype — Design Spec

**Date:** 2026-06-04 (rev 2026-06-05) · **Author:** Agent 1 (Comics) · **Companion mockups:** `.superpowers/brainstorm/`
**Status:** **DESIGN COMPLETE.** UI mirrors the manga screens 1:1; the data/source layer (GCD + Open Library + GetComics) and the two comics-specific gaps (cover-fallback, GetComics fetch) are decided (§6). Ready for the Codex review gate (§7) → `writing-plans`.

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

## 6. Discovery, loop, reader, cover-fallback, scope (DECIDED 2026-06-05)
Governing principle (Hemanth): **comics mirrors manga screen-for-screen; only the data/source layer and the comics-specific gaps below differ.**

### 6.1 Discovery / home — same as manga
Identical to the manga home (`ComicsPage` `FadingStackedWidget`): home (index 0) = library grid + Continue-reading strip; search bar → search takeover → result cards → series page; ＋Library adds a series (downloading any volume auto-adds it). The existing Western browse-grid screen (`m_westernStackIndex`) is reused. No comics-specific divergence.

### 6.2 Reader — same as manga (reuse as-is)
Reuse the existing comic reader (`COMIC_READER_FIX_TODO` Phase 6) unchanged to read the TPB `.cbz`. No new reader.

### 6.3 Download loop — same as manga UX, GetComics as the source
UX identical to manga: tap volume → progress in the `VolumeTile` row → opens in the reader on completion; `MangaDownloadIndex` tracks state. The one new mechanism (Agent 1-owned engineering, no UX change): fetch from **GetComics** — resolve the post for `<series> <volume>` → follow the download/safelink through the ad-redirect/file-host layer (headless-browser fallback if JS-gated) → land the `.cbz` on the existing download/index path. **RCO read-online** stays the fallback (headless-browser image pull, §3.2). **De-risk gate (§9): this GetComics fetch is the single biggest feasibility risk and MUST be proven in a standalone downloader spike BEFORE any UI work.**

### 6.4 Cover-fallback — generated title-card
Open Library cover coverage is partial. When a volume has no OL cover, **generate a title-card cover locally** (series-art tint + "SERIES · Vol N"). Deterministic, instant, no extra network round-trip. (Chosen over cover-scraping to keep it offline/instant.)

### 6.5 Scope — first vertical slice = Saga, end-to-end
Prove ONE series fully before widening: **Saga** — search → series page (GCD volumes + OL covers + title-card fallback) → download Vol 1 (GetComics) → read (existing reader). Thin but complete loop. Widen to more series once the slice is green.

## 7. Process gate (gov-v14)
Per `src/ui/pages/comics/CLAUDE.md`: the design is now complete. Before `/superpowers:writing-plans`, summon **Codex** (via `scripts/engines/`) to review-and-expand this brainstorm in place (orthogonal-model review). One pass, then writing-plans.

## 8. References
- `agents/audits/comic_metadata_brain_verdict_2026-06-04.md`, `agents/audits/comic_source_quality_bakeoff_2026-06-04.md`
- Memories: `project_comics_catalog_arc`, `project_western_richness_no_signup_ladder`, `feedback_stremio_for_manga_vibe`, `project_tankoyomi_volume_pivot_arc_2026-05-16`, `feedback_bigger_manga_covers`
- Domain: `src/ui/pages/comics/CLAUDE.md`

## 9. Codex review — risks, gaps & required additions (2026-06-05)
<!-- Codex (gpt-5.5) orthogonal-model review via scripts/engines (gov-v14 gate). Folded in by Agent 1. -->
**Verdict:** sound for a *prototype*, not yet a shippable app. Architecture is coherent; the dominant risk is **source automation** (GetComics fetch + RCO scrape), not metadata or UI.

**Build-sequencing change — de-risk first:**
- **GetComics downloader SPIKE before any UI work.** Prove Saga Vol 1 resolves + downloads from GetComics with zero manual steps, repeatably (several runs), surviving the ad-redirect / safelink / file-host layer (changing redirects, host captchas, anti-bot, expiring links, ad/popup flows). If it can't be made reliable, the acquisition model changes — find out before building on it.

**New components to spec (the implementation plan operationalizes these):**
- **`SourceAdapter` interface** — search / resolve / download / validate / failure-codes / retry-policy / source-provenance. GetComics + RCO are adapters behind it; treat them as unstable.
- **`VolumeIdentity` model** — GCD series id, issue range, collected-edition id, ISBN(s), display volume number, edition type, source aliases.
- **Matching / ranking algorithm** — normalize title, publisher, year, volume number, ISBN, issue range, filename tokens; **require user confirmation when confidence is low** (prevents wrong-file downloads).
- **Post-download validation** — archive opens, page count > 0, image dimensions sane, store file hash, reject corrupt/spoofed files (guard extension spoofing + malware-adjacent redirects).
- **Fallback hierarchy (explicit, with degraded states)** — GCD metadata → local cache → OL cover by ISBN → generated title-card → RCO read-online → clear "unavailable" state.
- **Edition policy** — paperback / hardcover / deluxe / omnibus / digital TPB: which edition the unit prefers; how variants, reprints, and missing/mismatched ISBNs are handled (OL ISBN mismatch = bad cover = lost trust).
- **Storage / library model** — library tables, source provenance, downloaded file path, metadata cache, cover cache (incl. generated), read progress, failed-source attempts.
- **Error + offline states** — missing GCD match, missing ISBN/cover, broken GetComics link, host captcha, RCO blocked, partial/corrupt download; what stays browsable offline + cache expiry.

**Legal / compliance (pre-ship, not prototype-blocking):** GCD CC BY-SA attribution + license display + source links + share-alike clarity on transformed local metadata; Open Library attribution; a scraping/source disclaimer; no redistribution of metadata dumps unless cleared. Flag for legal review.

**First-slice acceptance test (Saga):** search Saga → select correct series → show Vol 1 row → fetch cover or title-card → download CBZ from GetComics with no manual steps → validate archive → import into library → open in the existing reader → persist read progress.
