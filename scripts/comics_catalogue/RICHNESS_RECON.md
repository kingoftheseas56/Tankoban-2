# Western Catalogue — Free-Source Richness Recon (2026-06-01)

**Question:** can we reach manga-level richness (per-edition covers + synopses +
series metadata) for Western collected editions **with zero signup** (hard line,
[[project_western_richness_no_signup_ladder]])? Probed GetComics, RCO
(rcostation.xyz), Wikipedia REST. Fandom available (clients exist) but not yet
probed — series-level lore bonus.

## What each free source actually delivers (probed, not assumed)

| Field | GetComics post | RCO series page | Wikipedia REST | Verdict |
|---|---|---|---|---|
| **Per-edition cover** | ✅ `og:image`, specific to the TPB/compendium (e.g. `Invincible-Compendium-Vol.-1...jpg`) | series hero only (one `image_src`) | series infobox image | **✅ GetComics = per-edition covers, free** |
| **Series synopsis** | ❌ body is download-focused; `og:description` = generic SEO ("FREE Comics Download...") | ✅ **real "Summary:" block** ("Girls, acne, homework, supervillains... Mark Grayson...") right on the page we already scrape | ✅ clean `extract` for every series tested (Invincible/Saga/Chew/Spawn) | **✅ two independent free sources** |
| **Per-edition (per-TPB) synopsis** | ❌ no plot blurb | ❌ series-level only | ❌ series-level only | **⚠️ THE CEILING — see below** |
| **Series metadata** (author/publisher/year/genre) | year + image-format + size + language | partial | ✅ author/publisher/year/genre | **✅ Wikipedia primary** |
| **Download** | DDL (TeraBox/Pixeldrain/Mega/WeTransfer) + `/dls/` tokens; **magnet NOT universal** (Invincible Compendium post had 0 magnets) | n/a (catalogue list only) | n/a | resolver already has DDL fallback; don't assume magnet |

## The one real ceiling — per-edition plot synopsis

No free source carries "what happens in TPB 5 *specifically*." GetComics/RCO/
Wikipedia are all **series-level**. The signup-gated DBs (ComicVine/Metron) are
where per-volume descriptions live — but those are the LAST rung.

**Possible no-signup path (brainstorm avenue, not proven):** per-edition synopsis
via **retailer-description scrape** (Amazon / publisher TPB pages) — exactly how
manga gets per-volume synopsis today (BookWalker description scrape, see
[[project_manga_synopsis_source_decision]]). Lower-yield + a matching problem,
but it's the free analog. Alternative: apply the series synopsis to all editions
(honest, less rich).

## Matching problem (shared with Task 8 downloads)

To attach a GetComics per-edition cover (or DDL) to an RCO edition
("TPB 5 The Facts of Life"), we must match RCO-edition ↔ GetComics-post. This is
the SAME discovery work the download wiring (Task 8) needs — richness + downloads
ride the same GetComics fetch. Design them together.

## Bottom line for the brainstorm

"Best shot, no signup" can realistically deliver: **per-edition covers (GetComics)
+ series synopsis (RCO Summary block / Wikipedia) + series metadata (Wikipedia)**
— a big jump from today's bare grid, and close to manga's *feel*. The honest gap
vs manga is **per-TPB plot synopsis**; closing it free means a retailer-scrape
experiment, and if that fails → Agent 7 perspective → signup as last resort.
