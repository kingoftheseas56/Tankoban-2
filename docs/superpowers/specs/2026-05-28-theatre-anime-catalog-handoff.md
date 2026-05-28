# THEATRE_ANIME_CATALOG — next-wake handoff + ground-truth seed

**Status:** NOT a spec yet. Ground-truth seed + the questions a proper brainstorm must answer. Hemanth chose this direction 2026-05-28; agreed to start it fresh next wake with **ground-truth before brainstorm** (the lesson from the TANKORENT_QUALITY_AND_QUEUE arc — see [[feedback_ground_truth_before_brainstorm]]).

**Owner:** Agent 4 (Stream + Tankorent).

## The confirmed diagnosis (don't re-derive)

One Piece in Tankoban shows **23 seasons**; in Hemanth's Stremio it shows **one continuous absolute-numbered list (1163+ episodes)**. Root cause, fully proven this wake:

- Our metadata is **not broken**. Live-curl of `https://v3-cinemeta.strem.io/meta/series/tt0388629.json` (the exact endpoint our AddonRegistry uses) returns **23 seasons** (season 0–23, 1230 videos). Cinemeta itself splits One Piece into TMDB-style seasons. Tankoban faithfully reflects that.
- Hemanth's Stremio differs because he runs **Anime Kitsu v0.0.10** (+ One Pace Catalog v0.3.0) addons. Anime Kitsu overrides Cinemeta for anime, presenting Kitsu.io absolute numbering. **We simply don't have that anime catalog layer.**

So this arc = **add an anime metadata catalog to Theatre**, not fix a bug.

## Ground-truth already gathered (verified in code this wake)

1. **Override hook exists.** `MetaAggregator` (src/core/stream/MetaAggregator.cpp:290-296) picks the series-meta source via a `std::stable_sort` that prefers `com.linvo.cinemeta`. Making an anime addon win for anime shows is a change to *that sort* — not new plumbing.
2. **Addon layer already knows about Kitsu + anime.** `AddonRegistry` already declares `idPrefixes = {"tt", "kitsu"}` and an `"anime"` resource type (src/core/stream/addon/AddonRegistry.cpp:694-716). Kitsu-ID awareness is partially scaffolded.
3. **Season structure comes straight from `meta.videos[].season`.** `MetaAggregator::parseSeriesEpisodes` (cpp:167-198) groups videos by their `season` field. An absolute-numbering anime source would feed this differently (likely all season=1 with high absolute episode numbers, or a single flat list).
4. **AniList assets already in the Comics tree** — `src/core/manga/anilist/AniListClient.cpp`, `AniListCache.cpp`, `AniListParser.cpp`, `AniListVolumeMapper.cpp`. Comics/Tankoyomi already does AniList metadata. Reuse potential is real but unverified for the Theatre/series shape.

## The hard problem (where the brainstorm must focus)

**The ID bridge.** Our pack search + Torrentio key on `imdb:tt…`. Anime catalogs key on `kitsu:…` / AniList IDs. The whole integration hinges on: given an anime, how do we (a) get its absolute-numbered episode list AND (b) still find torrents/streams for it? Torrentio *does* accept kitsu IDs for anime streams (the `idPrefixes` tt+kitsu hint at this), so the stream side may already be reachable — but the Tankorent indexer pack-search (Nyaa et al.) keys on title strings, and anime is packaged by arc / absolute-range / batch, never "Season N." This connects to the still-open download-search defect: even with correct metadata, "Season N pack" queries fail for anime megaseries.

## Ground-truth still owed BEFORE brainstorming next wake

- [ ] Fetch Anime Kitsu's actual manifest + a sample `/meta` response (`kitsu:` ID) — confirm its episode/numbering shape and how it maps to a tt-id (does it provide an imdb mapping?).
- [ ] Trace how Torrentio resolves a `kitsu:` ID to streams in our stream-server path — is the stream side already anime-capable?
- [ ] Read the Comics `AniList*` assets to judge real reuse vs Theatre-specific need.
- [ ] Decide the numbering model for downloads: absolute episode ranges vs arc-based (Hemanth runs One Pace = arc-based; that's a tell for how he thinks about One Piece packaging).

## Open product questions for the brainstorm (Hemanth's calls)

- Match Stremio exactly (Kitsu absolute numbering) or a Tankoban-native anime model?
- Anime detection signal — where does "this show is anime → use the anime catalog" live?
- Download UX for a seasonless megaseries — arc packs? absolute-range batches? full-series? (the existing season/multi-season chips are meaningless for these shows and should hide).

## Carryover from the TANKORENT_QUALITY_AND_QUEUE arc (this wake)

- **Phase 1** (per-show sequential download queue) — shipped, correctly wired to TheatreDownloadPanel's Download path. Pending Hemanth smoke.
- **Defect 1** (stale rows on panel reopen) — fixed `020e2ef`. Pending smoke.
- **Defect 2** (source dropdown re-fires search, complaint #3) — fixed `0a88511` with the StreamAggregator epoch guard. Pending smoke.
- **Finding 3** (pack-search 25/indexer cap, the real Nyaa-parity surface — NOT the standalone tab Phase 2 touched) — untouched; fold into this arc's download-search work since anime parity is the same surface.
- Full audit: `agents/audits/tankorent_download_panel_audit_2026-05-27.md`.
