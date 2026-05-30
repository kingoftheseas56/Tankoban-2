# Manga Catalog Engine — Congress 9 Experiment #1 (Reliable Catalog at Scale)

**Date:** 2026-05-30
**Author:** Agent 1 (Leader) + Hemanth (brainstorm) — recon by 6 research agents
**Status:** BLUEPRINT — the pre-planned arc the Office Congress executes. Per Congress 9 §5, batches are defined here BEFORE the room opens.
**Chair:** Agent 0 · **Leader:** Agent 1 · **Rides on:** The Office (live bus) + Congress 9 orchestration spec.

---

## §1 — Purpose & non-goals

**Purpose, in priority order:**
1. **Prove the Congress machine** — the Office assembly line: handoffs, batons, fluid roster, line-never-stops. This is Experiment #1; the orchestration is the real subject.
2. **Map what the proven scraper can't reach** — every dead-end logged to a `gaps` file. That file seeds the *next* arc (reliable long-tail scraping).
3. **Deliver 20 series, flawlessly complete** — the evidence the engine works.

**Non-goals (explicit):**
- NOT a thousands-deep discovery catalog — reach is *library completion*.
- NOT solving reliable scraping for the long tail — that's the follow-on arc this pilot scopes.
- NOT the easy automated per-volume synopsis for licensed series — Agent 1 runs that solo, off-Congress (it's a one-command chore).

## §2 — End product

The manga catalog stops being hand-curated and becomes **self-completing**: a **routing brain** + **source arms** that assemble a complete, accurate, per-volume record for any series — and keep doing it as the library grows. The pilot proves the engine on 20 deliberately-varied series; the same engine then runs the rest of the library itself.

## §3 — The record (per series)

- **Series spine:** HQ series cover, author/staff, genres/tags, status, publication year, ratings, alternative titles.
- **Per-volume array**, each entry: volume number, title, **cover image**, **plot synopsis**, English ISBN, release date.
- Every field **source-tagged** (provenance) so the routing decisions are always auditable.

## §4 — Source map & calibrated routing chains (from Phase-0 recon)

| Field | Chain (primary → fallbacks) |
|---|---|
| Series spine (cover/author/genres/status) | AniList → MangaUpdates |
| Series synopsis / ratings / alt-titles | **MangaUpdates** (clean keyless API, 100%) |
| Volume list + per-volume ISBNs + dates | **Wikipedia** ({{Graphic novel list}}) → OpenLibrary / AniList-count cross-check |
| Per-volume covers | **BookWalker** (clean CDN, 90%+) → VIZ.com / OpenLibrary (Urasawa gap) → Fandom (rehost) → scan-source (reject if possible) |
| Per-volume synopsis | **B&N by ISBN** + boilerplate-gate → Google Books (keyed) → Fandom blurb (CC-BY-SA, filter by heading) → MangaUpdates series-level (last resort, labeled "series overview") |

**Recon verdicts:** AniList ✅ · MangaUpdates ✅ (better than expected — keyless API, 100%) · Wikipedia ✅ (100% on licensed; two patterns + naming-resolution quirks) · BookWalker ✅ (gap: Monster + 20th Century Boys = Urasawa JP-digital-only) · B&N ⚠️ (90% hit / ~65% truly per-volume; boilerplate the rest) · Google Books ⚠️ (needs key, 1k/day) · Fandom ⚠️ (covers ~40% / synopsis ~50% real-world; CC-BY-SA; no universal wiki pattern) · **Simon & Schuster back-cover IMAGE ❌ CUT from v1** (anti-bot + VIZ-only; the text route wins).

## §5 — The routing brain (per-field waterfall + quality gates)

For each field: walk the chain top-to-bottom, take the first source that **passes its quality gate**, else fall through, else log a gap. Quality gates:
- **Synopsis boilerplate gate:** hash-compare vol-1 vs vol-2 (and majority-prefix across the series); if a volume's text is the shared franchise/anniversary intro, strip it or reject and fall through. (The Bleach/Grand Blue lesson, codified.)
- **Cover gate:** reject downscaled scan-thumbnails; require poster-grade resolution.
- **Per-volume-real gate:** synopsis must be volume-specific, not a series blurb reused across volumes.

## §6 — Congress mechanics (roster + pipeline)

**Roster:**
- **Agent 0 — Chair:** sequences handoffs, merges green work, watches the bus, declares done.
- **Agent 1 — Leader:** routing brain (stitch) + quality-gate (data judgment). Integration core.
- **Agent 9 — Stage 1 "Skeleton":** AniList + MangaUpdates + Wikipedia → per-series skeleton (spine + volume list + ISBNs). Feeds the line.
- **Agent 2 — Stage 2a "Synopsis":** per-volume blurb off Agent 9's ISBNs + boilerplate-gate. (Book-metadata/ISBN domain fit.)
- **Agent 3 — Stage 2b "Covers":** per-volume cover art (BookWalker → fallbacks, rehosted).
- **Agent 4 — Stage 3 "Smoke":** dev-bridge → open app per batch, screenshot volume pages, confirm render.
- **Agent 8 — Comms/briefing:** crafts arm-briefs + summons, keeps the Office thread + gaps-log clean.

**The line:** Stage 1 (Agent 9 skeleton) → Stage 2 (Agent 2 synopsis ∥ Agent 3 covers) → Leader stitch + gate → Stage 3 (Agent 4 smoke). **Batches of 5, pipelined** — Agent 9 builds batch N+1's skeleton while batch N is downstream. **Fluid roster** — tap in when your stage is live, tap out when it's downstream (Congress 9 §4).

**Handoffs:** direct + addressed on the bus (`@AgentB, batch 1 skeleton is up at data/manga_enrichment/_congress/batch1/, your turn`). Chair watches, doesn't switchboard.

## §7 — The 20-series pilot (4 batches of 5)

Deliberately spread across publishers + difficulty so the routing brain hits every decision:

- **Batch 1 — warm-up / known-good (validate the line):** One Piece, Death Note, Bleach, Grand Blue Dreaming, Frieren: Beyond Journey's End.
- **Batch 2 — Urasawa + cover-gap torture:** Monster, 20th Century Boys, Vagabond, Vinland Saga, Beastars. *(Monster + 20CB stress the BookWalker→VIZ/OpenLibrary cover fallback; 20CB stresses Fandom-barren synopsis.)*
- **Batch 3 — niche / publisher variety:** Made in Abyss (Seven Seas), Blame! (Kodansha, omnibus edge), Nichijou (Vertical), Dorohedoro (VIZ), Golden Kamuy (VIZ).
- **Batch 4 — mixed / newer / edge:** Naruto, Uzumaki (Junji Ito), Delicious in Dungeon (Yen Press), Spy × Family, Berserk (Dark Horse).

Publisher spread: VIZ, Kodansha, Seven Seas, Vertical, Yen Press, Dark Horse — exercises the S&S-VIZ-only limit, the Urasawa BookWalker gap, omnibus editions, and Dark Horse.

## §8 — Dead-ends → the gaps-log

Any field that fails its whole chain → one line in `data/manga_enrichment/_congress/gaps.jsonl`: `{series, volume, field, sources_tried, reason}`. The line **keeps moving** (skip + log). This file is a **first-class deliverable** — it is the input to the next arc (reliable long-tail scraping). Agent 8 keeps it clean.

## §9 — Output & integration

Each series → `data/manga_enrichment/<slug>.volumes.json` — the **existing enrichment-overlay format** the Comics detail page already renders. The engine produces data; the page already on screen renders it. No new UI.

## §10 — Success criteria (measure the MACHINE first)

- **Orchestration:** Did the line pipeline without stalling? Did batons flow peer-to-peer without the Chair switchboarding? Did the fluid roster (tap-in/out per stage) work?
- **Output:** 20 series, every chosen field complete + per-volume, smoke-verified rendering in the app.
- **Map:** a populated `gaps.jsonl` scoping the long-tail problem.
- **Verdict for the next run:** keep stage-parallel, or switch to slice-parallel for the full library?

## §11 — Legal posture

Personal, non-commercial use (Hemanth + brother). Full pragmatic scraping; scrapers stay *technically* polite (rate-limited, images rehosted not hotlinked) for reliability, not legality.

---

*Phase-0 recon transcripts: 6 research agents, 2026-05-30 (MangaUpdates / BookWalker / Simon & Schuster / B&N+Google Books / Wikipedia / Fandom).*
