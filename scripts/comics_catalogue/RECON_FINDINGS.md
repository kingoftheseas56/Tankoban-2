# Western Comics Catalogue — Phase 0 Recon Findings

**Date:** 2026-05-31 · **By:** Agent 1 · **Verdict: GO (no-brain, magnet-download)**

Live probes settled the three feasibility unknowns. The arc came out simpler than the brain-heavy v1 spec: two no-auth sources, no metadata brain, no signup, mirroring the manga stack.

---

## Verdict 1 — RCO catalogue LIST: ✅ GO (plain HTTP)

- `rcostation.xyz` returns HTTP 200 on plain requests, **no Cloudflare challenge** for page fetches.
- URL scheme: `https://rcostation.xyz/Comic/<Series-Name>` (capital **C**). Item links: `/Comic/<Series>/<Item>?id=N`.
- Invincible series page = **170 unique item slugs** (Issue 0–144 + TPB 1–25). Each item appears twice on the page (cover thumbnail link + text link) → parser dedupes by href.
- Collected editions ARE surfaced, but as flat uploader-typed label strings mixed into the issue list (no structured `format` field) — confirmed by two deep-research reports. We type them by label heuristic (blockbuster-clean).
- NOTE: a series' Compendium/Omnibus can be a SEPARATE series page (e.g. `/Comic/Invincible-Universe-Compendium`), not an item under the main series. The parser returns only two-segment item links.
- Fixture: `tests/fixtures/rco_series_invincible.html`.

## Verdict 2 — RCO reader SCANS: ❌ blocked (obfuscated)

- Reader pages (`/Comic/<Series>/<Item>`) return 200 but the image list is NOT in the HTML — injected at runtime by an external obfuscation script (`21wiz.com/s.js`). Every plain-HTTP image-array pattern returned zero.
- → RCO is a **catalogue-LIST source, NOT a scan source.** Its reader-image path is dead on plain HTTP and unused.

## Verdict 3 — GetComics: ✅ GO (download source, with its own magnet)

- `getcomics.org` reachable on plain HTTP. Search: `/?s=<query>`. Posts at publisher-pathed slugs, e.g. `/other-comics/invincible-compendium-vol-1-2013-2019/`, `/marvel/captain-america-omnibus-vol-1-5-fan-made-2007-2015/`.
- Each book on a post has a normalized labelled download block: **MAGNET Link / Main Server / Mega / Mediafire / Pixeldrain**. DDL options route through `getcomics.org/dls/<token>`; magnet is a real `magnet:?xt=urn:btih:...`.
- Verified on the Captain America omnibus post: a real magnet present (`941ec611...`). Fixture: `tests/fixtures/getcomics_post_capamerica.html`.
- Footer ad links (craveu/crushon/juicychat) filtered by requiring `magnet:` or `getcomics.org/dls/`.

## Verdict 4 — Metadata brain: ❌ DROPPED (no signup)

- The brain (Metron/ComicVine) was mandatory in v1 ONLY to power a synth-from-issues ladder (needs issue→collection mapping). GetComics delivers whole collected editions directly → no synth → no mapping → **no brain.** Both Metron + ComicVine require a free account/key — ruled out (app principle: everything already there, zero accounts).

## Verdict 5 — Torrents: GetComics' OWN magnet → our libtorrent

- We do NOT need a comics-specialized tracker or a new indexer. GetComics posts carry a magnet button → feed it to the **libtorrent client we already built** (TorrentClient/TorrentEngine).
- Nyaa Literature category probed directly = 0 Western collected editions; TPB(apibay)=0; 1337x untestable from this network. So the app's existing trackers don't carry comics — GetComics' own magnet is the path.

---

## Locked architecture

| Layer | Source |
|---|---|
| Browse / catalogue | RCO `/Comic/<Name>` list + covers (plain HTTP) |
| Edition typing | RCO label heuristic (compendium>omnibus>TPB>deluxe>vol) |
| Download | GetComics → **magnet → libtorrent** (preferred), Main Server DDL fallback, then Mega/Mediafire/Pixeldrain |
| Metadata brain | none |

**Download priority:** magnet → our libtorrent client; else Main Server `/dls/`; else other file hosts. Posture = **browse-first, download best-effort** (an edition with no resolvable download = browse-only). Curated blockbuster-first v1 scope.

## Honesty note (git process, 2026-05-31)

Phase 1 + Phase 2 commits reached origin (`5478d0a`, `7583452`) with "green" claims while tests were actually red (test expectations written from assumption, not the fixture; fixture not committed). Corrected forward — this findings file + the fixtures were re-banked, test expectations fixed to the real 170-item shape, suite verified 18/18 green. Root cause: committing before reading the test result. Owned.
