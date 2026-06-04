# Comic Metadata Brain — Final Verdict (Western Comics)
**Date:** 2026-06-04 · **Agent:** 1 (Comics) · **Scope:** Pillar-1 (metadata brain) only — alternatives to ComicVine/Metron under a hard no-end-user-signup constraint.

## The question
Find the best FREE, no-end-user-setup metadata brain for Western comics (Marvel/DC/Image/indie),
to replace the manga stack's AniList. End user does zero setup; a one-time **developer** account to
fetch a public dump is allowed. Judged on: access, breadth, unit-modelling (collected editions /
ISBNs), richness, licensing (offline cache/redistribute), tooling.

## Method — 5 research assets, converged
1. **Live probes (Agent 1, system access)** — actually hit the APIs, didn't just read about them.
2. **Grok deep research** — ranked GCD #1, OL #2.
3. **ChatGPT deep research** — GCD lead, OL supplement.
4. **Claude harness (deep-research workflow, 98 agents)** — GCD via dump, OL+GoogleBooks ISBN layer.
5. **Gemini deep research** — GCD SQLite-dump core + OL Covers API + pHash for floppies; richest architecture.

**All five converge: GCD = brain, Open Library = covers + collected editions.**

## VERDICT
**Grand Comics Database (GCD) as the brain, shipped as a filtered local dump (live API as freshness/fallback),
+ Open Library for covers-by-ISBN, + perceptual-hash for floppy covers.** This is the AniList-grade,
no-signup brain we couldn't find before. Maps 1:1 onto the manga model.

| Manga stack | Comics equivalent (this verdict) |
|---|---|
| AniList (live metadata API) | **GCD** — local SQLite dump (primary) + public `/api/` (fallback/freshness) |
| Volume (clean unit) | **Collected edition / compendium**, resolved by ISBN |
| WeebCentral (offline-ish completeness) | GCD dump shipped local = full offline brain |
| Cover art | **Open Library Covers API by ISBN** (collected) + pHash (floppies) |

## LIVE-VERIFIED FACTS (Agent 1, 2026-06-04 — these override the reports where they disagree)
- **GCD public API WORKS with no key** (two reports wrongly said "no API"; live evidence wins):
  - Root `https://www.comics.org/api/` → HTTP 200.
  - Search: `https://www.comics.org/api/series/name/<query>/?format=json` → filtered results.
    (NOTE: `?name=<q>` query-param does NOT filter — returns all ~230k series. Use the `/name/<q>/` path.)
  - Issue: `https://www.comics.org/api/issue/<id>/?format=json` → series_name, number, title,
    publication_date, **isbn**, **barcode**, page_count, story_set (per-story creators + cover entries).
- **GCD models collected editions WITH ISBNs** (one report wrongly downplayed this):
  - "Invincible Compendium (2011 series)" issue 1124186 = "Compendium One", **ISBN 978-1-60706-411-4**,
    barcode present, page_count 1024. First-class series, proper ISBN.
- **GCD cover images are NOT fetchable by us** — cover CDN `files1.comics.org/...` → HTTP 403 (Cloudflare),
  and the dump ships metadata only (no image binaries). → covers must come from elsewhere.
- **Open Library covers ARE cleanly fetchable, no key** — solves GCD's cover gap for collected editions:
  - By OLID: `https://covers.openlibrary.org/b/id/9030048-L.jpg` → 200, image/jpeg, 50,209 bytes.
  - **By ISBN directly**: `https://covers.openlibrary.org/b/isbn/9781607064114-L.jpg` → 200, image/jpeg, 65,597 bytes.
  - So: GCD gives the ISBN → OL turns the ISBN into a cover. Both no-signup. Chain verified end-to-end.
- **Open Library search/data API works no-key** — `openlibrary.org/search.json` returns Invincible
  Compendium One+Two, Saga, Batman: Hush collected editions with ISBNs + cover ids.
- **Google Books keyless is now dead** — keyless request → HTTP 429, quota 0. Needs a dev API key
  (acceptable as dev-side, but OL already covers covers, so GoogleBooks is optional backfill only).
- **ComicK** — Cloudflare-fronted + manga-only → disqualified as a Western brain.

## GCD facts (from reports, consistent across sources)
- Breadth: ~2.11M issues, ~212k series, ~16.8k publishers, ~4.1M stories (Mar 2025). Largest open Western catalog.
- Dump: bi-weekly full DB. MySQL ~2.4GB / **SQLite ~4.1GB**. Download at `comics.org/download/` → "Private Use"
  → "Download MySQL Dump" (free dev account, one-time). No-signup stale snapshot mirror on Internet Archive (2021, ~1.6GB).
- Licensing: **CC BY-SA 4.0** (schema + data). Cache + redistribute offline WITH attribution + share-alike. (Cover scans separately copyrighted — but we don't use GCD covers anyway.)
- Richness: full creator credits per story, dates, story titles, characters, variant_of_id linkage. No native "story arc" field.

## Recommended architecture (Gemini's, validated by community precedent + my live probes)
1. **Core engine:** dev fetches GCD dump once → build-step filters to `series, issue, story, publisher, brand_group`
   → ship a compressed read-only **SQLite (<1GB achievable)** inside the installer (or silent first-run download).
   Instant offline queries, immune to API outages/rate-limits. (Proven by r/comicrackusers offline tagger:
   1000-comic job 269min→32min vs live API; CLU `clu-comics` queries GCD MySQL with fuzzy title match.)
2. **Collected editions:** local SQLite already carries ISBN/barcode → resolve compendium/TPB/omnibus by ISBN.
3. **Covers (collected):** `covers.openlibrary.org/b/isbn/{ISBN}-L.jpg`, no key. Inject a
   `User-Agent: Tankoban (contact@…)` to lift OL limits to 3 req/sec (covers: 100/IP/5min).
4. **Covers (floppies, no ISBN):** perceptual hash (pHash/aHash + MD5) match against a local cover-hash set,
   or construct/scrape the GCD public issue-page cover URL on demand + cache.
5. **Freshness/fallback:** the live GCD `/api/` (verified no-key) for titles newer than the shipped dump.

## BOUNDARY — what this does NOT solve
This is the **brain** (catalogue + unit + metadata + covers). It is NOT the **page scans** — the actual
readable comic pages. That remains the separate pillar-3 decision: GetComics (whole-archive .cbz/.cbr) vs
RCO/rcostation (per-page scans behind the rguard scramble lock, cracked in `rco_*` artifacts this week).

## Disqualified (for the record)
ComicVine, Metron, Marvel API, Google Books (key), League of Comic Geeks (closed), ComicBookRealm (closed),
ComicK (manga-only), Inducks (Disney-only). All fail no-signup OR Western-breadth OR offline-license.
