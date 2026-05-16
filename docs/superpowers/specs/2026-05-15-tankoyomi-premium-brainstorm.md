# Tankoyomi-Premium — Brainstorm

- **Date:** 2026-05-15
- **Author:** Agent 1 (Comic Reader + Tankoyomi, scope owner since the 2026-05-14 BROTHERHOOD_RESTRUCTURE bundle `1466a79`)
- **Arc tag:** `TANKOYOMI_PREMIUM_MVP`
- **Status:** Phase 1 brainstorm — pending Codex (Agent 7) review-AND-EXPAND per gov-v4 Rule 20 (revised 2026-05-14 ~5:35pm at chat.md:3640+). After Codex's expansion lands inline with HTML-comment attribution markers, Hemanth fires `/superpowers:writing-plans` directly; no second Codex pass.
- **Phase 2 (writing-plans, separate Hemanth fire):** `docs/superpowers/plans/2026-05-15-tankoyomi-premium.md` (or 2026-05-16 if the writing-plans pass fires tomorrow)
- **Phase 3 (executing-plans, separate Hemanth fire):** multi-summon arc, recommended cadence one-go-per-phase
- **Pre-brainstorm memory entries:** [[project-tankoyomi-premium-mvp-brainstorm-prelock]] (the 8 starting-point decisions captured 2026-05-15 noon), [[project-weebcentral-71pct-downscale-confirmed]] (the empirical motivation), [[feedback-brainstorm-skill-for-big-arcs]] (why this formal pass), [[feedback-brainstorm-batches-of-four]] (Hemanth's AskUserQuestion cadence)
- **Coordination boundaries:** Theme system (Agent 5) untouched; Player (Agent 3) untouched; Books / Videos / Stream untouched; Tankorent + TankoLibrary (Agent 4B's narrowed lane) untouched. `MangaDownloader` unchanged. `TorrentEngine` consumed via existing public surface only.
- **Skills invoked (Phase 1):** `/brief`, `/superpowers:brainstorming`, `/superpowers:verification-before-completion` (before writing this doc)

---

## §1 Concept & motivation

A **Stremio-for-manga** layer inside Comics mode. For ~30 hand-curated series, the app downloads pristine master scans from trusted-uploader nyaa torrents (1r0n, VIZ Media Digital, etc.) by issuing **single-volume libtorrent file-priority requests** against series-pack torrents. For everything outside the catalog — and for chapters past the catalog's coverage gap on ongoing series — WeebCentral remains the default, exactly as today.

The motivation is empirical and dated 2026-05-15 ~1:00pm: a side-by-side A/B on One Piece chapter 1000's "STRAW HAT LUFFY" splash ([[project-weebcentral-71pct-downscale-confirmed]]) confirmed that WeebCentral's scans are **3000×2250 → 2133×1600 (71% linear, ~50% pixel count, 2.2× lower bytes/pixel)** vs. the 1r0n nyaa master. Day-to-day reading on a 1080p/1440p monitor barely registers the difference because the monitor's pixel pitch is the bottleneck — but zoom-in on hatching-heavy art (Berserk, Vagabond) shows clear softening and screentone moiré on the WeebCentral version.

This MVP gets pristine quality on the series Hemanth actually reads, keeps the existing clean Mihon-style Tankoyomi UX for everything else, and resolves the earlier "Tankoyomi-exclusive Comics-mode pivot" dilemma cleanly: Premium handles the canon, WeebCentral handles the long tail.

Hemanth's verbatim pitch from 2026-05-15 ~12:00pm noon:

> "Stremio for manga. We curate trusted-uploader nyaa torrents per series. The user clicks 'download volume 17' and we use libtorrent file-priority to only grab that volume's pieces. WeebCentral stays as the fallback for everything not in the catalog."

---

## §2 Reference surfaces (file:line cites)

### §2.1 Comics-mode surface this MVP touches

- `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}` — the existing "Search Tankoyomi" search input + tile grid. EXTEND with Premium-section header + Premium chip rendering + per-series dedup so a Premium hit subsumes its WeebCentral duplicate (§4 below).
- `src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}` — the detail view used for Tankoyomi-origin series. ADD a volume-row variant that the existing chapter-row layout switches to when the rendered series has a Premium catalog entry. Volume row = cover thumbnail + volume label + chapter-range + per-volume `[Download]` button + post-download `[Read]` button. Loose-tail chapters appended below in a `── Latest chapters (WeebCentral) ──` section for ongoing series.
- `src/ui/pages/ComicsPage.{h,cpp}` — owns instantiation of MangaDownloader. Adds a sibling instantiation of `TorrentVolumeProvider` (§3.2), wires its signals to refresh the visible detail view, and (via the slot already wired in the TANKOYOMI_CONTINUE_READING arc that shipped this morning) updates the Continue strip when the first chapter of a downloaded volume is opened.
- `src/core/manga/MangaScraper.h` + `MangaSourceRegistry.{h,cpp}` — UNCHANGED. Premium is not a scraper; it doesn't fit the HTTP image-fetch interface. The catalog loader is its own pipeline.
- `src/core/manga/MangaDownloadIndex.{h,cpp}` — used directly by `TorrentVolumeProvider` to register the completed volume's cbz so the Comics library sees it. The existing `registerChapter` primitive handles per-cbz registration; whether we extend it with a `registerVolume` cousin or just call it with the volume's constituent chapter IDs is a §3.2 implementation detail Codex can rule on.
- `src/core/manga/MangaDownloader.{h,cpp}` — **completely unchanged** by this MVP. Architecture A (§3.1) keeps the torrent-orchestration concern out of MangaDownloader's chapter-HTTP-loop machinery.

### §2.2 Stream-side / torrent-side surface this MVP consumes (no changes)

- `src/core/torrent/TorrentEngine.h` — consumed via existing public methods:
  - `:111  QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused = true);`
  - `:113  void setFilePriorities(const QString& infoHash, const QVector<int>& priorities);`
  - `:115  void setSequentialDownload(const QString& infoHash, bool sequential);` (probably useful for the cover-page-first behavior — Codex to weigh in)
  - `:124  void moveStorage(const QString& infoHash, const QString& newSavePath);` (probably not needed; we copy the finished cbz instead)
  - `:166  int peersWithPiece(const QString& infoHash, int pieceIdx) const;` (the swarm-quality probe for the "waiting for peers" UX)
  - Existing `torrentMetadataReady` / `torrentDetails` / piece-finished signals as appropriate.
- The 12-method API freeze (Congress 6, preserved through STREAM_SERVER_PIVOT) is honored throughout. No additions, no shape changes.

### §2.3 Library / persistence

- `src/core/manga/ComicsLibraryRecord.{h,cpp}` — extend the existing record's `origin` field to recognize a new origin value `tankoyomi-premium` alongside the existing `tankoyomi` (which today maps to WeebCentral/ReadComicsOnline). `canonicalSeriesPath` derivation is unchanged. Cover path field reused for the page-1 extraction output.
- `src/core/scan/LibraryScanner.{h,cpp}` — UNCHANGED. The completed Premium cbz lands in `<canonicalSeriesPath>` and the next scan picks it up exactly like any other cbz.
- `src/core/manga/MangaPosterCache.{h,cpp}` — reused for per-volume cover storage. New path convention: `manga_posters/premium_<seriesId>_v<NN>.jpg` for extracted volume covers, alongside the existing series-level `manga_posters/weebcentral_<id>.jpg`.

---

## §3 Architecture

### §3.1 Architecture pick: separate orchestrator (Option A)

Three candidate architectures were weighed in the brainstorm session; **Option A** was picked. The rejected alternatives are recorded for the spec audit trail:

- **(A) Separate `TorrentVolumeProvider` service** — picked. Lives at `src/core/manga/TorrentVolumeProvider.{h,cpp}`, sibling to `MangaDownloader`. ~250 LOC. Talks to `TorrentEngine` for the actual fetching, `MangaDownloadIndex` for library registration. `MangaDownloader` is untouched. Clean separation of HTTP-image-loop semantics (existing) from torrent-file-priority semantics (new).
- **(B) Extend `MangaDownloader` with a `"premium-nyaa"` source branch** — rejected. `MangaDownloader::startDownload`'s `source` switch would grow a third branch with completely different semantics (no per-image progress, no per-image retry, no per-image failure taxonomy). The class would carry two distinct internal worlds glued together by `if (source == "...")`.
- **(C) Parallel `PremiumMangaDownloader` class** — rejected. Would mirror the existing downloader's queue/pause/resume/cancel surface but with totally different innards. Duplicates plumbing for no benefit, since the work units (torrent files, not HTTP-image loops) are honestly different.

### §3.2 `TorrentVolumeProvider` surface

Public interface (sketch, Codex may refine):

```cpp
class TorrentVolumeProvider : public QObject {
public:
    TorrentVolumeProvider(TorrentEngine* engine,
                          MangaDownloadIndex* index,
                          QObject* parent);

    // Kicks off the fetch. Idempotent: if the torrent is already running,
    // just bumps file-priority for the new volume.
    void requestVolume(const PremiumCatalogEntry& entry,
                       int volumeNum,
                       const QString& destinationPath);

    // Cancel/pause one in-flight volume request. Cancels the volume's
    // file-priority slot; does NOT remove the torrent if other volumes are
    // still being requested for that series.
    void cancelVolume(const QString& seriesId, int volumeNum);

signals:
    void volumeProgress(QString seriesId, int volumeNum, float pct);
    void volumeCompleted(QString seriesId, int volumeNum, QString cbzPath);
    void volumeFailed(QString seriesId, int volumeNum, QString errorClass);
    void swarmStatus(QString seriesId, int volumeNum, int piecePeersOnline);
};
```

Per-call internal flow:

1. **Resolve torrent.** If the torrent for this series isn't yet added, call `TorrentEngine::addMagnet(magnetUri, tempSavePath, paused=false)` and wait for `torrentMetadataReady`. The `tempSavePath` is a per-app-session staging directory under `appData/manga_premium_staging/<infoHash>/`. Cleanup on app shutdown.
2. **Set file priorities — strict single-vol.** Build a `QVector<int> priorities` sized to the torrent's file count where only the target volume's `fileIndex` is `7` (highest) and everything else is `0`. Call `TorrentEngine::setFilePriorities(infoHash, priorities)`. If a subsequent `requestVolume` call bumps another volume from the same series, OR that volume's slot to also be `7` (incremental bump, no churn).
3. **Watch for completion.** Poll `torrentDetails(infoHash)` once per second; check whether the target file's bytes-done equals its total size. (Alternative: subscribe to a file-completion signal from `TorrentEngine` if one exists or can be cheaply added — Codex to confirm.) When complete, the cbz exists at `tempSavePath/<cbzFileName>`.
4. **Surface peer status.** Every 2 seconds, sample `peersWithPiece(infoHash, pieceIdx)` for one of the volume's piece indices. If `0` for ≥30s, emit `swarmStatus(seriesId, vol, 0)` → UI shows a small "Waiting for peers" indicator on the volume row. Per the brainstorm Batch 1 decision: no prompting, no auto-pivot — strict single-vol or wait.
5. **Hand off the cbz.** Move (`QFile::rename`) the completed cbz from `tempSavePath/<cbzFileName>` to `<destinationPath>/<cbzFileName>`. `destinationPath` is the same `canonicalSeriesPath` the COMICS_TANKOYOMI_STREAM_MERGER arc settled.
6. **Extract cover.** Open the cbz, decode page 1 with `QImage`, scale to standard poster dimensions, save to `manga_posters/premium_<seriesId>_v<NN>.jpg`. ~50ms one-time per volume.
7. **Register with the library index.** Call `MangaDownloadIndex::registerChapter` (or the proposed `registerVolume` cousin — Codex to rule) so the visible detail view repaints via the existing `entriesChanged` signal that the merger arc wired this week. `LibraryScanner` will also pick the file up on the next periodic scan as a defense-in-depth path.
8. **Emit completion.** `volumeCompleted(seriesId, vol, finalCbzPath)`. The volume row in the detail view swaps from `[Download]` to `[Read]`.

`TorrentVolumeProvider` owns no UI. `ComicsPage` instantiates it once at app start, connects to its signals, drives `requestVolume` from the volume-row `[Download]` button in `ComicsTankoyomiDetailView`.

### §3.3 `PremiumCatalog` loader

New file: `src/core/manga/PremiumCatalog.{h,cpp}` — ~80 LOC.

Loader scans `resources/manga_premium_catalogs/*.json` at app startup, merges all entries into one in-memory `QHash<QString, PremiumCatalogEntry>` keyed by `seriesId`. v1 ships **one** file (`tankoyomi_premium_2026-05.json`). Door left open: dropping a second `.json` into the folder later (a josei catalog, a friend's catalog) just works without code changes.

`PremiumCatalogEntry` carries the fields documented in §5.

Lookup surface:

```cpp
class PremiumCatalog : public QObject {
public:
    explicit PremiumCatalog(const QString& catalogsDir, QObject* parent = nullptr);

    bool isPremiumSeries(const QString& titleLowercased) const;
    std::optional<PremiumCatalogEntry> entryForTitle(const QString& titleLowercased) const;
    QList<PremiumCatalogEntry> allEntries() const;
};
```

`ComicsPage` holds the loader. `ComicsTankoyomiSearchWidget` queries `isPremiumSeries` on each search result to decide chip rendering + dedup. `ComicsTankoyomiDetailView` queries `entryForTitle` to decide whether to render volume-row vs. existing chapter-row.

---

## §4 UI integration

### §4.1 Search results

Search UI (`ComicsTankoyomiSearchWidget`) renders results in two ordered sections:

1. **Premium** (gold header text, top of the results region) — series for which `PremiumCatalog::isPremiumSeries(...)` returns true. Each tile has two chips: `[Tankoyomi]` (existing) + `[Premium]` (new, gold). Subtitle text: *"also on WeebCentral"* when the WeebCentral search also returned this series (informational only; the WC duplicate tile is suppressed).
2. **Tankoyomi (WeebCentral / ReadComicsOnline)** (existing section, unchanged label) — non-catalog series only. Catalog duplicates are deduped out so the user sees ONE Berserk tile (the Premium one), not two.

Empty state: if the user's query returns Premium-only hits (no WeebCentral matches), the Tankoyomi section is suppressed entirely rather than rendered empty.

### §4.2 Detail view layout

`ComicsTankoyomiDetailView` switches its row layout based on whether `PremiumCatalog::entryForTitle(...)` returns an entry:

- **Premium series, completed (e.g. Berserk, Death Note):** rows are volumes only. Each row carries cover thumbnail (placeholder before download, page-1 extraction post-download), volume label ("Volume 27"), chapter range ("Chs 234–242"), chapter count badge ("9 chapters"), per-volume `[Download]` or `[Read]` button.
- **Premium series, ongoing (e.g. One Piece, Sakamoto Days):** volume rows at top, then a dim section break `── Latest chapters (WeebCentral) ──`, then chapters 1146 → 1110 below, sourced from WeebCentral via the existing scraper path. The boundary is computed from `PremiumCatalogEntry::postCoverageFallback.startsAfterVolume`.
- **Non-Premium series:** today's chapter-row layout, unchanged.

ASCII shape (also produced during the brainstorm preview for Q2):

```
┌─────────────────────────────────────────────┐
│ Volume 99 — Chs 989–995    [Download]       │
│ Volume 98 — Chs 980–988    [Read]           │
│ ...                                         │
│ Volume 1  — Chs 1–8        [Read]           │
│ ── Latest chapters (WeebCentral) ──         │
│ Chapter 1145               [Download]       │
│ Chapter 1144               [Download]       │
│ ...                                         │
│ Chapter 996                [Download]       │
└─────────────────────────────────────────────┘
```

### §4.3 Premium chip styling

New chip widget reused across search-result tiles, library tiles (if the existing library tile rendering surfaces source chips — Codex to confirm), and the detail view header. Gold/amber tint (theme-aware: warmer gold in light mode, deeper amber in dark mode; aligned with the project's existing chip palette in `src/ui/widgets/`). Text: `Premium`. Renders alongside the existing `Tankoyomi` chip, not in place of it — both chips visible together. Closest reference: the Stream-mode `DOWNLOADED` and `DOWNLOADING` chips that landed in the Netflix overhaul Phase 4 (file:line cite to live in the writing-plans pass).

### §4.4 Read-state propagation

Chapter-level read-state remains the primitive (existing). Volume read-state is **derived, not stored**: a volume row's "X/N chapters read" badge is computed from the existing chapter progress map. Finishing the last unread chapter in a volume causes the volume to render as complete automatically. Opening a volume from the detail view jumps to the first unread chapter in that volume. The Continue strip on Comics page picks up the next unread volume's first chapter via the TANKOYOMI_CONTINUE_READING wiring shipped earlier today.

No new persistence schema for volume-level state.

---

## §5 Catalog file shape

JSON file at `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json`. One top-level array; each element is one series.

Example entry:

```json
{
  "seriesId": "berserk",
  "title": "Berserk",
  "alternateTitles": ["ベルセルク"],
  "anilistId": 30002,
  "status": "ongoing",
  "magnetUri": "magnet:?xt=urn:btih:...&dn=Berserk+%5B1r0n%5D",
  "trustedUploader": "1r0n",
  "format": "one-cbz-per-volume",
  "volumes": [
    { "vol": 1,  "fileIndex": 0,
      "cbzFileName": "Berserk v01 [1r0n].cbz",
      "chapters": [
        {"num": "1", "title": "The Black Swordsman"},
        {"num": "2", "title": "..."}
      ]
    },
    { "vol": 42, "fileIndex": 41,
      "cbzFileName": "Berserk v42 [1r0n].cbz",
      "chapters": [...]
    }
  ],
  "postCoverageFallback": {
    "weebcentralSlug": "berserk",
    "startsAfterVolume": 42
  }
}
```

Field rules:

- `seriesId` — stable lowercase slug; used as the lookup key + cover filename prefix. Never changes once a catalog ships.
- `title` / `alternateTitles` — used for the search-dedup match against WeebCentral results. Case-insensitive comparison.
- `anilistId` — optional, reserved for future cross-reference / metadata enrichment. Not consumed by v1 code.
- `status` — `"completed"` | `"ongoing"`. Drives detail-view layout (whether the loose-tail section renders).
- `magnetUri` — the trusted-uploader torrent for this series.
- `trustedUploader` — informational only in v1; surfaces in a debug tooltip if useful.
- `format` — `"one-cbz-per-volume"` is the only value v1 supports. The catalog curation gate is *"verify the torrent is one-cbz-per-volume format"* — series whose only available torrents are chapter-pack or raw-image-folder format are NOT included in v1.
- `volumes[].fileIndex` — what we pass to `setFilePriorities`. Captured at catalog curation time by inspecting the torrent's file listing.
- `volumes[].chapters[]` — the volume→chapter mapping, sourced from mangareader.to (per the pre-lock decision) at catalog curation time, hand-verified. Drives the read-state derivation in §4.4 and the chapter-range label in §4.2.
- `postCoverageFallback` — only present on `"ongoing"` entries. Names the WeebCentral slug to scrape for chapters beyond `startsAfterVolume`. Reused via the existing `WeebCentralScraper` path.

Validation: the loader rejects entries with missing required fields (silently logs a warning, doesn't crash). Codex to weigh in on whether stricter JSON schema validation is worth the dependency.

---

## §6 Catalog scope for v1

30 series, focused on the manga Hemanth actually reads (per the brainstorm Batch 3 Q on genre breadth). No josei/shoujo broadening for v1 — the catalog is honestly his list, not a tour-of-the-canon.

**Completed (20) — safest MVP cases, no chapter-vs-volume drift:**

1. Berserk (42 vols)
2. Vagabond (37 vols)
3. Monster (18 vols)
4. 20th Century Boys (22 vols)
5. Pluto (8 vols)
6. Slam Dunk (31 vols)
7. Fullmetal Alchemist (27 vols)
8. Death Note (12 vols)
9. Hunter x Hunter (37 vols) — effectively-completed via hiatus
10. Attack on Titan (34 vols)
11. Demon Slayer (23 vols)
12. Tokyo Ghoul + :re (30 vols total, single catalog entry)
13. Dr. Stone (26 vols)
14. The Promised Neverland (20 vols)
15. Steel Ball Run / JoJo Part 7 (24 vols)
16. Goodnight Punpun / Oyasumi Punpun (13 vols)
17. Blame! (10 vols)
18. Made in Abyss (13 vols)
19. Spy × Family (14 vols)
20. Frieren: Beyond Journey's End (14 vols)

**Ongoing (10) — exercises the post-coverage fallback path:**

21. One Piece (109+ vols, 1100+ chapters) — must-include, largest test
22. Jujutsu Kaisen (30 vols)
23. Chainsaw Man (18 vols, Part 2 ongoing)
24. Vinland Saga (28 vols, slow publishing)
25. Kingdom (73+ vols, ongoing)
26. Black Clover (36 vols, late stages)
27. Dandadan (18+ vols)
28. Sakamoto Days (19+ vols)
29. Dorohedoro (23 vols, completed in fact — re-classify when curating)
30. Bleach + TYBW (74 base + 4 new) — tests the "completed-then-resumed" edge

Catalog curation effort: ~10–15 hours of focused work. Per series: pick a trusted-uploader torrent → verify it's one-cbz-per-volume format → capture each volume's `fileIndex` from the torrent file listing → scrape mangareader.to for chapter→volume mapping → hand-verify the mapping against a known volume's spine. Codex may want to weigh in on whether this curation should be tooled (a small CLI helper that takes a magnet URI + a mangareader URL and emits the JSON entry) vs. fully manual; current plan is manual.

---

## §7 Edge cases & failure modes

### §7.1 Slow swarm
Volume request stays at 0% for an extended period. Volume row shows a small *"Waiting for peers (0 piece-holders online)"* indicator. Torrent keeps running. No prompts, no auto-pivot. User is informed and can leave it. When peers materialize, the download proceeds normally.

### §7.2 Dead torrent
v1 ships no auto-rescue mechanism. The catalog itself is the recovery path: if a series's torrent fully evaporates, the next Tankoban release ships an updated catalog with a swapped magnet URI. Acceptable cost for a 30-series catalog where periodic health-checking is tractable.

### §7.3 Series not in Premium catalog
Identical to today's behavior. Search returns WeebCentral / ReadComicsOnline results, no Premium tile, no Premium chip, no volume-row layout. The Premium MVP is strictly additive — nothing about the existing non-catalog flow changes.

### §7.4 Ongoing series past coverage
Detail view renders v1–v(N) from the catalog at top, then a section break `── Latest chapters (WeebCentral) ──`, then chapters past `startsAfterVolume` below sourced from WeebCentral. Mixed-origin within one series is acceptable here because the section break makes the quality boundary visible. The download buttons on the loose-tail chapters use the existing `MangaDownloader` HTTP-image-fetch path; the download buttons on the volume rows use `TorrentVolumeProvider`. Both deliver cbzs into the same `<canonicalSeriesPath>`.

### §7.5 Pre-existing folder import
User has already manually folder-imported Berserk volumes 1–10 from before this MVP existed. Now clicks `[Add Berserk]` from the Premium catalog. Two library entries result — the old folder-imported Berserk and the new Premium Berserk — coexisting as two tiles. v1 ships no merge / migration surface. Dedup/migration is a v1.1 problem; getting it wrong (deleting user data) is materially worse than letting them have two tiles for one series.

### §7.6 Re-click on in-flight volume
Idempotent. Second `[Download]` click on a volume already being fetched is a no-op (the volume's file-priority is already set to `7`). No second toast, no confusing state.

### §7.7 Series removed from library
The cbz files stay on disk in `<canonicalSeriesPath>` (existing behavior — `LibraryScanner` will not re-add them as long as the user is the one who removed the record, but they're physically still there if the user wants them). The torrent is removed from `TorrentEngine` (no continued seeding by default in v1). The library record is deleted. Re-adding the series later works as new.

### §7.8 App killed mid-download
Torrent's libtorrent resume data is persisted via the existing `TorrentEngine` resume mechanism. On next launch, the torrent re-attaches and resumes where it left off. The staging directory `appData/manga_premium_staging/<infoHash>/` is per-session — Codex to confirm whether resume actually wants this, vs. a persistent staging location.

---

## §8 Explicitly out of scope for MVP

- **Live nyaa.si search.** The catalog is the curation. v1 does NOT scrape nyaa at runtime for new torrents.
- **Multi-torrent stitching for one series.** One curated torrent per series. If volume 50 of Vagabond needs a different torrent than volume 1 (re-uploads, archive restorations), that series gets two catalog entries OR doesn't ship in v1.
- **Auto-catalog refresh from the internet.** Catalog ships with the app binary. Updates ride with Tankoban releases.
- **Genre / popularity / "browse top 10 seinen" view.** Search-only for v1.
- **User-editable trusted-uploader allowlist UI.** The catalog itself is the allowlist.
- **Cross-source dedup / migration of existing folder-imported series.** Two-tile coexistence is v1; merge/migrate is v1.1+.
- **Quality-grading sub-tiers.** Premium is one flat tier in v1; no "HQ vs. SHQ vs. lossless" badging.
- **Per-series catalog metadata enrichment (descriptions, ratings, ongoing-or-not derived from the catalog).** v1 uses what the existing Tankoyomi detail-view path already pulls from WeebCentral/AniList.
- **Continued seeding after a download completes.** v1 stops the torrent on series-remove and doesn't ship a "keep seeding" setting. Defer the seeder-ethics question to v1.1.
- **A "download all volumes of this series" bulk action.** v1 is single-volume-per-click. Bulk action belongs in v1.1.
- **Tooling for catalog curation (CLI helper).** v1 catalog is hand-built. If curation pain becomes real after the first 30 series, build the tool then.

---

## §9 Forward-compatibility doors left open

### §9.1 Multi-catalog file folder
Loader reads `resources/manga_premium_catalogs/*.json` (plural directory). v1 ships one file. Dropping a second `.json` later is a zero-code-change extension. Closest analog: Stremio's addon model, intentionally.

### §9.2 Chapter-pack-format torrent support
v1 supports only `format: "one-cbz-per-volume"`. The `format` field is structured so a future `"chapter-pack-per-volume"` (where one volume = multiple cbz files inside the torrent) or `"raw-images-per-volume"` (where one volume = a folder of images that needs zipping post-download) is an additive change to the catalog schema + `TorrentVolumeProvider` post-processing. v1's strict format gate is the cost.

### §9.3 `registerVolume` cousin
If Codex's review concludes that `MangaDownloadIndex::registerChapter` is the wrong primitive for a volume-as-one-cbz unit (e.g. because chapter-vs-volume read-state derivation gets cleaner with an explicit volume registration), a `registerVolume(seriesId, vol, cbzPath, constituentChapterIds)` addition is small and additive. v1's plan defaults to reusing `registerChapter` per-chapter-id derived from the catalog's `volumes[].chapters[]`; Codex may overrule.

### §9.4 Catalog signing / integrity
v1 trusts whatever JSON sits in `resources/manga_premium_catalogs/`. If we ever want community-submitted catalogs without trusting random submitters' magnet URIs, a signed-catalog format (catalog + signature, signature verified against a known public key) is a natural extension. v1 doesn't ship this because there's only one curator (Hemanth) and the JSON is bundled with the binary.

---

## §10 Smoke matrix

End-to-end validation corpus: **Berserk + Death Note + One Piece**.

| # | Series | Validates |
|---|--------|-----------|
| 1 | Berserk | Premium chip in search + volume-row UI in detail view + single-vol file-priority round-trip + page-1 cover extraction + visual A/B vs. the WeebCentral version |
| 2 | Death Note (12 vols) | Small enough to iterate end-to-end repeatedly; volume-row UI with shorter catalog; read-state propagation chapter → volume → series |
| 3 | One Piece (v1–v109 + chapters 996+ from WC) | Ongoing-series rendering: volumes + loose-tail section break + WeebCentral chapter-download path coexists with the Premium volume-download path |
| 4 | Any catalog series | Slow-swarm UX: throttle global DL rate to ~5 KB/s mid-download; verify "Waiting for peers" indicator renders cleanly without modal nag |
| 5 | Berserk | Pre-existing-import coexistence: import a Berserk folder via the existing folder-import path, then add Premium Berserk, confirm two tiles exist, neither breaks |
| 6 | Any non-catalog series (e.g. Yotsuba&!) | Existing WeebCentral path is unaffected — non-catalog search renders exactly as today |
| 7 | Death Note | App killed mid-download recovery: kill Tankoban while v3 is at ~30%, relaunch, verify torrent resumes and completes correctly |
| 8 | One Piece | Re-click idempotency: click `[Download]` on v50 twice in quick succession, verify no second toast and no confused state |

Smoke fires per Tier-2 conditional under CLAUDE.md ("Smoke when the work is feature-shaped"). Per smoke memory [[feedback-mcp-skies-clear]]: agent drives smokes via pywinauto-mcp + tankoctl; Hemanth's role is visual-quality + taste judgment only.

---

## §11 Estimated footprint

**New code: ~400–500 LOC C++ across:**

- `src/core/manga/PremiumCatalog.{h,cpp}` — JSON loader, in-memory model (~80 LOC)
- `src/core/manga/TorrentVolumeProvider.{h,cpp}` — orchestrator service (~250 LOC)
- `src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}` — volume-row variant additions (~80 LOC)
- `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}` — Premium chip rendering + section header + dedup (~50 LOC)
- `src/ui/pages/ComicsPage.{h,cpp}` — `TorrentVolumeProvider` instantiation + signal wiring (~20 LOC)

**No changes to:** `MangaDownloader`, `TorrentEngine`, `LibraryScanner`, Comic Reader, persistence-shared infrastructure, sidecar.

**Catalog curation effort:** ~10–15 hours of focused work. Per series: pick torrent → verify one-cbz-per-volume format → capture per-volume `fileIndex` → scrape mangareader.to chapter→volume mapping → hand-verify. Dominates the wall-clock budget.

**Wall-clock estimate:** ~2–3 weeks of focused work, mostly catalog curation, NOT code. Code-side is roughly one solid week of phased agent work.

---

## §12 Workflow shape (Rule 20)

1. **/superpowers:brainstorming** — completed (this doc, 2026-05-15). AskUserQuestion in batches of 4 per [[feedback-brainstorm-batches-of-four]]. 20 decisions locked across 3 batches + 1 reframe + 1 architecture pick.
2. **Codex (Agent 7) review-and-expand IN PLACE** on this brainstorm-md, with HTML-comment attribution markers per added/rewritten section. Co-authorship, not audit. One Codex pass total. Hemanth will craft the Codex prompt under plan-mode separately.
3. **/superpowers:writing-plans** — produces `docs/superpowers/plans/2026-05-15-tankoyomi-premium.md` (or 2026-05-16). Phased plan; Codex's expanded brainstorm-md is the input.
4. **/superpowers:executing-plans** — multi-summon arc, one-go-per-phase cadence preferred. Recommend `/superpowers:subagent-driven-development` for the independent phases (catalog loader + UI variant + chip styling are independent of each other; `TorrentVolumeProvider` is the load-bearing dependency).

---

## §13 Decisions log (the 20 locked in this brainstorm)

| # | Question | Decision |
|---|----------|----------|
| 1 | Catalog scope | 30 series, Hemanth's taste, no genre broadening |
| 2 | UI integration | Extends existing Tankoyomi search; Premium section header above WC |
| 3 | Volume rendering | Volumes default; loose chapters appended below for ongoing |
| 4 | Download UX | Selective single-volume via libtorrent file-priority |
| 5 | Volume→chapter mapping | Scrape mangareader.to at catalog-build time, hand-verified |
| 6 | Catalog refresh | Ship-with-binary only, no in-app refresh |
| 7 | Premium visual mark | New `Premium` chip, gold, alongside existing `Tankoyomi` chip |
| 8 | Fallback for non-catalog | Existing WeebCentral path unchanged |
| 9 | Slow-swarm UX | Strict single-vol only; "Waiting for peers" indicator; no prompts |
| 10 | Ongoing-series rendering shape | Volumes 1–N (catalog) + loose chapters appended below (WeebCentral) |
| 11 | Search dedup | One tile, Premium subsumes WC duplicate with "also on WeebCentral" subtitle |
| 12 | Premium chip text + color | `Premium`, gold/amber, theme-aware |
| 13 | Volume cover source | Extract page 1 of cbz at download time; per-volume cached |
| 14 | Dead-torrent recovery | Manual catalog rotation only (v1); no auto-rescue |
| 15 | Catalog forward-compat | Loader reads from `*.json` folder, merges all (door left open) |
| 16 | Catalog refresh cadence | Ship-with-binary only (re-confirmed against forward-compat option) |
| 17 | Read-state granularity | Chapter-level primitive (existing); volume state derived |
| 18 | Genre coverage | Hemanth's taste only, no broadening |
| 19 | Pre-existing folder-import collision | Coexist as separate library entries; merge is v1.1 |
| 20 | Smoke corpus | Berserk + Death Note + One Piece |
| 21 | Architecture | (A) Separate `TorrentVolumeProvider` service, `MangaDownloader` unchanged |

---

## §14 Open questions for Codex review-and-expand

Areas where Codex should bring his A-game per Hemanth's intent for the Phase 2 review pass:

- **Torrent file-completion signal granularity.** §3.2 step 3 punts on whether `TorrentEngine` exposes a per-file completion signal vs. requires once-per-second polling. Codex to inspect `TorrentEngine.cpp` and confirm the cleanest mechanism, OR propose a small additive signal if neither exists.
- **`registerVolume` vs. per-chapter `registerChapter` calls.** §3.2 step 7 leans toward calling `registerChapter` once per chapter in the volume's `chapters[]` array. Codex may overrule if there's a cleaner volume-aware primitive worth adding to `MangaDownloadIndex`.
- **Staging directory persistence model.** §3.2 step 1 + §7.8: is per-session `appData/manga_premium_staging/<infoHash>/` correct, or should the staging directory be persistent so torrent resume data finds its files where it expects them?
- **`PremiumCatalogEntry` JSON schema validation strictness.** §5 logs warnings on malformed entries silently. Codex to weigh whether stricter validation (rejecting the whole file, surfacing a startup-time error) is worth the failure-mode visibility.
- **Catalog curation tooling.** §6: should there be a small CLI helper that ingests `magnet:?xt=...` + a mangareader.to URL and emits a JSON entry skeleton, vs. fully manual curation? Affects curation effort estimate.
- **Cover extraction performance + lifecycle.** §3.2 step 6: ~50ms per volume is acceptable but blocks the cbz hand-off finalization. Should it be moved off-thread? Codex to rule.
- **Mixed-origin volume read-state.** §7.4: an ongoing series has Premium-quality volumes + WeebCentral-quality post-coverage chapters. Read-state is fine because chapters are the primitive, but is there a UX clarity issue Codex spots?
- **Theme palette for the gold/amber chip.** §4.3: Codex to inspect the existing chip palette in `src/ui/widgets/` and propose specific HSL values for light + dark modes.
- **Anything we're missing.** This is the open-ended catch-all. Bring the A-game.

---

## §15 Self-review block

Run inline before declaring this doc ready for Codex review-and-expand:

1. **Placeholder scan.** No TBDs / TODOs / vague-requirements in this doc. All numbered decisions resolved. Where Codex needs to weigh in, the open question is explicit in §14, not buried as a placeholder.
2. **Internal consistency.** Architecture choice (§3.1) matches the file:line cite plan (§2) matches the LOC estimate (§11) matches the workflow shape (§12). Cover-extraction lifecycle is described consistently in §3.2 step 6 + §4.2 + §11. The "ongoing-series" handling is described consistently in §4.2 + §7.4 + §5 (`postCoverageFallback`).
3. **Scope check.** Focused enough for one writing-plans pass to produce one phased plan. Out-of-scope list in §8 is explicit; v1.1 deferrals are named, not hand-waved.
4. **Ambiguity check.** Every UI decision has an ASCII shape or a verbatim chip-label. Every architecture decision names a file path. Every catalog field has a usage rule in §5. Where genuine uncertainty exists (cover extraction threading, staging directory persistence, JSON schema strictness), it's surfaced in §14 for Codex, not left ambiguous in the spec body.

Spec self-review passes. Doc ready for Codex review-and-expand per Rule 20.

---

## §16 Doc lineage

- **Brainstorm session date:** 2026-05-15, conducted under `/superpowers:brainstorming` skill
- **Pre-brainstorm capture:** session 2026-05-15 ~12:00pm noon (decisions via AskUserQuestion batches but no formal brainstorm-md produced; flagged by Hemanth → triggered this formal pass per [[feedback-brainstorm-skill-for-big-arcs]])
- **Formal brainstorm session:** 2026-05-15 ~2:36pm–3:05pm, Agent 1 wake
- **Cadence:** AskUserQuestion in batches of 4 per [[feedback-brainstorm-batches-of-four]] — 3 batches (4 + 4 + 4) + 1 single-Q reframe + 1 architecture pick
- **Hemanth's trust statement on the code sections (verbatim 2026-05-15 ~3:00pm):** *"yeah I'm not looking at that code bro, I might as will be reading latin. But I trust you. We can move to the next sections"*
- **Phase 2 prompt to Codex:** to be crafted by Hemanth under plan-mode, separately from this doc

<!-- Codex 2026-05-15: Adds the pass verdict and superseding corrections for writing-plans. -->
## Section 17 Codex pass verdict and superseding corrections

The core product shape is sound: a curated Premium catalog belongs beside the existing Tankoyomi/Comics flow, not inside `MangaDownloader`, and the first implementation should stay catalog-driven rather than opening a runtime nyaa search surface. The draft is strongest where it keeps Premium additive: non-catalog series stay on the existing scraper path, ongoing series keep a visible quality boundary, and `TorrentEngine` is consumed through its existing torrent primitives.

Writing-plans should treat these corrections as superseding earlier wording where they conflict:

1. **Path correction.** The live scanner is `src/core/LibraryScanner.{h,cpp}`, not `src/core/scan/LibraryScanner.{h,cpp}`. The scanner groups Comics archives via `ScannerUtils::groupByFirstLevelSubdir(...)` at `src/core/LibraryScanner.cpp:61-66`, skips Tankoyomi-claimed paths at `src/core/LibraryScanner.cpp:87-99`, and extracts covers with a `cover.*` / `folder.*` heuristic at `src/core/LibraryScanner.cpp:192-250`.
2. **Staging correction.** Supersedes Section 3.2 step 1's "per-app-session staging directory." Premium staging must be persistent, not per-session. Use a stable app-data path such as `<appData>/manga_premium_staging/<infoHash>/` so libtorrent resume data can find the same files after a crash. Delete staging only when the volume request is completed, cancelled with delete, or the torrent is explicitly evicted.
3. **Magnet add correction.** For magnet metadata resolution, the current `TorrentEngine::addMagnet(..., paused=true)` does not actually pause; it keeps the torrent active in upload-only mode so metadata can arrive without content download (`src/core/torrent/TorrentEngine.cpp:610-633`). Premium should use this shape, wait for `metadataReady`, set file priorities, then call `startTorrent(...)` to clear upload mode and resume content download (`src/core/torrent/TorrentEngine.cpp:804-820`). Calling `addMagnet(..., paused=false)` before priorities are known risks an all-files download window.
4. **Theme correction.** Supersedes the "gold/amber chip" wording as a hardcoded palette decision. The current theme system is single-axis Mode, with Dark/Nord/Solarized/Gruvbox/Catppuccin and no Light mode (`src/ui/Theme.h:69-120`). Premium chip styling should use `Theme::current().accent` / `accentSoft` / `accentLine`, not a new hardcoded amber. In Dark, that resolves to `#c7a76b`; in Solarized it resolves to `#b58900`; in other modes it tracks that mode's accent. This keeps the app inside the theme system and avoids another color island.
5. **Index primitive correction.** Reusing `MangaDownloadIndex::registerChapter` once per chapter for a single volume cbz is unsafe as the long-term primitive. Today `registerChapter` keys `m_byPath` by canonical path and `m_byChapter` by `source:series:chapter` (`src/core/manga/MangaDownloadIndex.cpp:143-200`). If ten chapter keys all point at one volume cbz path, evicting one chapter can remove the shared path entry and strand other chapter keys. Writing-plans should add a volume-aware registration path or refactor the index entry shape before Premium ships.
6. **Library scale correction.** The Comics library grid must remain series-level in v1. Do not render 600 volume tiles in the main library. Volume rows belong inside the Premium detail view; the library tile represents the series. The existing Continue strip is already deduped per series at `src/ui/pages/ComicsPage.cpp:1136-1173`, so Premium should feed it one "best current read" item per series, not per volume.

Research references used for this pass:

- libtorrent file and piece priority reference: https://www.libtorrent.org/single-page-ref.html
- libtorrent streaming scheduler reference: https://www.libtorrent.org/streaming.html
- local libtorrent source: `C:\tools\libtorrent-source\src\torrent.cpp`
- Stremio addon manifest reference: https://stremio.github.io/stremio-addon-sdk/api/responses/manifest.html
- AniList Media object reference: https://anilist.gitbook.io/anilist-apiv2-docs/docs/reference/object/media
- Mihon tracking/library docs: https://mihon.app/docs/guides/tracking and https://mihon.app/docs/faq/library
- Komga books/read-progress docs: https://komga.org/docs/openapi/books/
- mangareader.to terms page: https://mangareader.to/terms

<!-- Codex 2026-05-15: Validates Option A and adds the provider concurrency contract. -->
## Section 18 Architecture and concurrency contract

Option A remains the right architecture, with one addition: `TorrentVolumeProvider` should be a single long-lived service owned by `ComicsPage`, but its persistent state should be independent of UI lifetime. It is not a second `MangaDownloader`; it is a torrent request coordinator with a request ledger.

Required state model:

1. `PremiumCatalog` is loaded first and becomes the source of truth for `seriesId -> magnetUri -> expectedInfoHash -> volume fileIndex`.
2. `TorrentVolumeProvider` loads a small persisted request ledger before reattaching torrents. Suggested file: `<appData>/manga_premium_requests.json`.
3. Each request row is keyed by `(catalogId, seriesId, volumeNumber)` and stores `expectedInfoHash`, `fileIndex`, `cbzFileName`, `stagingPath`, `canonicalDestinationPath`, `status`, and `createdAt`.
4. Multiple volume requests for the same torrent merge into one file-priority vector. Multiple series use independent torrents and independent staging subdirectories.
5. Completion is serialized per requested volume: first make the final file durable, then register it in the index, then emit the UI completion signal. `volumeCompleted` must not fire before the index mutation has saved and emitted its own `entriesChanged`.

Threading contract:

- `TorrentEngine` emits alerts from its alert worker thread. Every connection from `TorrentVolumeProvider` to `TorrentEngine` should be `Qt::QueuedConnection` unless the receiver is intentionally thread-local.
- `MangaDownloadIndex` is mutex-protected and its mutating methods save and emit off-lock (`src/core/manga/MangaDownloadIndex.h:28-39`, `src/core/manga/MangaDownloadIndex.cpp:97-104`, `src/core/manga/MangaDownloadIndex.cpp:199-200`). The provider can call it from a worker, but UI receivers should still connect queued.
- `MangaDownloader` already owns global and per-series pause/resume for HTTP chapters (`src/core/manga/MangaDownloader.cpp:784-841`). Premium should not be forced into that class, but the UI needs one shared "Transfers paused" state. Writing-plans should introduce a tiny `MangaTransferCoordinator` or equivalent facade in `ComicsPage` that fans out pause/resume/cancel-all to both `MangaDownloader` and `TorrentVolumeProvider`.
- HTTP and Premium downloads for the same series can coexist because they write different work units into the same canonical series folder. The collision guard is filename-level: the provider must never overwrite an existing cbz unless the catalog entry explicitly marks it as a replacement for a prior Premium version.

The crash-resume case should be treated as normal, not exceptional. On startup, the provider reads the request ledger, re-adds or reuses the torrent by expected infoHash, reapplies the union file-priority vector after metadata, and resumes incomplete requests. If the catalog entry disappeared since the request was created, the request remains recoverable from its persisted fields; mark it `catalogMissing` in UI but allow completion or cancellation.

<!-- Codex 2026-05-15: Corrects selective-download assumptions using libtorrent file-to-piece behavior. -->
## Section 19 libtorrent selective-download realism

The "single-volume file-priority" idea is technically viable, but it is not byte-perfect single-file isolation. libtorrent file priorities are converted into piece priorities. In the RC_2_0 source, `file_to_piece_prio(...)` initializes all pieces to `dont_download`, walks every file, maps each non-zero-priority file to its inclusive piece range, and raises each covered piece to the maximum overlapping file priority (`C:\tools\libtorrent-source\src\torrent.cpp:154-188`). The official reference states the same API relationship: `prioritize_files()` sets priorities for all pieces based on the file vector, and changing file priorities resets affected piece priorities (https://www.libtorrent.org/single-page-ref.html).

Practical consequence: if a cbz begins or ends in the middle of a torrent piece, the boundary piece may contain bytes from the adjacent volume. Downloading Volume 17 can incidentally fetch small edge fragments of Volume 16 or 18. This is expected BitTorrent behavior, not a bug. The catalog should therefore record:

- `fileIndex`
- `fileSizeBytes`
- `pieceStart`
- `pieceEnd`
- `boundaryPolicy`: `"allow-piece-overlap"` for v1

The UI and product copy should say "downloads only the selected volume file" rather than promising "downloads zero bytes from other volumes." The file written to the canonical library remains only the target cbz; incidental piece bytes stay in libtorrent's staging/part-file world and are not surfaced as library content.

File completion should not be implemented as a blind once-per-second poll if we can avoid it. The current `TorrentEngine` already exposes `pieceFinished(infoHash, pieceIndex)` (`src/core/torrent/TorrentEngine.h:315-323`, emitted at `src/core/torrent/TorrentEngine.cpp:200-203`) and `fileByteRangesOfHavePieces(...)` (`src/core/torrent/TorrentEngine.h:282-289`, implemented at `src/core/torrent/TorrentEngine.cpp:1479-1554`). Recommended mechanism:

1. After metadata, compute the target file's piece range with the existing `pieceRangeForFileOffset(...)`.
2. Connect to `pieceFinished`.
3. On a piece in the target file's range, debounce a check of `fileByteRangesOfHavePieces(infoHash, fileIndex)`.
4. Treat the file as complete only when the merged have-ranges cover `[0, fileSizeBytes)`.
5. Call `flushCache(infoHash)` before moving or copying the cbz (`src/core/torrent/TorrentEngine.cpp:1325-1330`).

No new `TorrentEngine` signal is required for v1. If writing-plans wants cleaner ergonomics, the additive signal should be `fileCompleted(infoHash, fileIndex)`, implemented inside `TorrentEngine` from piece-finished alerts, but the existing API is enough.

Do not use `setSequentialDownload(true)` for normal volume downloads. libtorrent's own streaming document says sequential mode is simpler and can keep bandwidth saturated, but is sub-optimal for time-critical behavior compared with deadline scheduling. For Premium volumes, we are not streaming page 1 while downloading the rest; we want the selected file to finish robustly. Use file priority 7 for requested files, priority 0 for the rest, and optionally use `setPieceDeadlines` only for a "cover preview soon" enhancement later. Do not mix ad hoc piece priorities with file priorities unless writing-plans explicitly re-applies them after every `setFilePriorities` call, because libtorrent warns that file-priority changes reset piece priorities.

Peer behavior: BitTorrent peers serve requested pieces, not "chapters" or "volumes." I found no evidence in libtorrent docs that qBittorrent/BiglyBT seeders penalize a requester merely because its requested pieces are non-sequential. The real risks are (a) low availability for the selected pieces, (b) boundary-piece spillover, and (c) metadata latency before priorities can be applied. The existing `peersWithPiece(infoHash, pieceIdx)` probe is useful for (a), but sample several pieces across the target file, not just one boundary piece.

<!-- Codex 2026-05-15: Expands catalog schema, validation severity, and trust fields. -->
## Section 20 Catalog schema and validation

The Section 5 schema is a good skeleton but under-specifies auditability, integrity, and future migration. Add these fields before v1:

Top-level catalog file fields:

- `catalogId`: stable slug, e.g. `tankoyomi_premium_2026_05`.
- `schemaVersion`: integer, start at 1.
- `createdAt`: ISO-8601 timestamp.
- `curator`: short string, e.g. `Hemanth`.
- `signature`: optional for v1 bundled catalogs, required before community catalogs.
- `sourcePolicy`: object documenting whether mangareader-derived mapping was hand-entered, scraped by helper, or imported from another metadata source.

Series fields:

- `expectedInfoHash`: required. The app should reject the entry if the magnet resolves to a different infoHash. This is the cheapest protection against a swapped magnet URI.
- `magnetUri`: required for v1, but treated as an address for the expected infoHash, not as the identity itself.
- `publisherLanguage`: e.g. `en-digital`, `jp-digital`, `en-print-scan`, `fan-scanlation`.
- `releaseEdition`: free string for VIZ Digital, Kodansha Digital, 1r0n pack, etc.
- `riskClass`: `bundled-curated`, `license-sensitive`, `community-untrusted`, or `blocked`. v1 should ship only `bundled-curated` and `license-sensitive`; `blocked` entries are never loaded.
- `catalogEntryCreatedAt` and `catalogEntryUpdatedAt`.
- `curatorNotes`: optional, not shown in normal UI.
- `aliases`: keep `alternateTitles`, but also add normalized title keys used for dedup so future title changes are explicit.

Volume fields:

- `fileIndex`: required.
- `cbzFileName`: required.
- `fileSizeBytes`: required after metadata capture.
- `pieceStart` / `pieceEnd`: required after metadata capture.
- `sha256`: optional for v1 if curation time makes it expensive, but strongly recommended before community catalogs.
- `pageCount`: required if known; warn if missing, reject if non-integer or <= 0.
- `coverPageHint`: optional object, e.g. `{ "mode": "auto" }`, `{ "entryName": "cover.jpg" }`, or `{ "pageIndex": 1 }`.
- `publicationDate`: optional date; useful when `vol` is not enough for specials, omnibuses, or resumed series.
- `chapters[].canonicalChapterKey`: required. This is the cross-origin identity used when a loose WeebCentral chapter later migrates into a Premium volume.

Validation severity:

- **Reject catalog file:** invalid JSON, unsupported `schemaVersion`, duplicate `catalogId`, duplicate `(seriesId, volumeNumber)`, malformed magnet, missing `expectedInfoHash`, duplicate `fileIndex` inside one series, or unsupported `format`.
- **Reject series entry:** missing required series fields, expected infoHash mismatch, `format` not `one-cbz-per-volume`, `postCoverageFallback` missing on an ongoing series, or volume file indices out of metadata range.
- **Reject volume:** missing `fileIndex`, missing `cbzFileName`, duplicate chapter keys, invalid `pageCount`, or file extension not `.cbz`.
- **Warn but load:** missing `anilistId`, missing `publicationDate`, missing `sha256` in v1, missing `coverPageHint`, missing `curatorNotes`.

Comparable metadata surfaces support these additions. AniList's Media object exposes volumes, chapters, country of origin, licensing, cover image, genres, synonyms, updatedAt, and adult flags. Stremio addon manifests include identity/version/contact/behavior hints, including a `p2p` hint for BitTorrent sources. Komga exposes book-level operations, duplicate-file listing, metadata refresh, and read-progress endpoints. Premium should not copy any one schema wholesale, but these references support treating identity, version, volume count, read progress, and P2P trust as first-class metadata rather than loose comments.

<!-- Codex 2026-05-15: Adds the cover extraction and archive-integrity lifecycle. -->
## Section 21 Cover extraction and archive integrity

Supersedes Section 3.2 step 6's simple "page 1 extraction" rule. The live `LibraryScanner` already uses a better heuristic: prefer image entries whose basename starts with `cover.` or `folder.`, otherwise fall back to the first image (`src/core/LibraryScanner.cpp:192-250`). Premium should reuse that heuristic in a shared helper rather than inventing a second cover extractor.

Recommended lifecycle:

1. Torrent target file reaches complete byte coverage.
2. `TorrentVolumeProvider` calls `flushCache(infoHash)`.
3. Provider copies or renames from staging to a same-folder temp name under the canonical series path, e.g. `<cbzFileName>.tankoban-part`.
4. Provider validates the archive before final rename:
   - archive opens;
   - all entries used for reading are images;
   - at least one image exists;
   - page count is within a reasonable bound and matches catalog `pageCount` if provided;
   - decompressed first-image size is bounded to avoid zip-bomb behavior.
5. Provider extracts cover off the UI thread. If extraction succeeds, write `manga_posters/premium_<seriesId>_v<NN>.jpg`.
6. Provider atomically renames `.tankoban-part` to `.cbz` in the canonical folder.
7. Provider registers the volume in `MangaDownloadIndex`.
8. Provider emits `volumeCompleted`.

The `.tankoban-part` discipline matters because `LibraryScanner` scans `*.cbz`, `*.cbr`, and `*.rar` directly (`src/core/LibraryScanner.cpp:61-66`). If the final filename appears while bytes are still being written, the scanner can see a partial archive, count zero pages, or cache a bad thumbnail. A temp suffix outside the scanner glob prevents that.

Cover fallback:

- First choice: catalog `coverPageHint.entryName` if present and found.
- Second choice: `cover.*` or `folder.*` basename.
- Third choice: first naturally sorted image entry after filtering junk names such as `credit`, `scan`, `blank`, `ad`, `back`, and `spread` only when there is a later plausible cover.
- Fourth choice: series-level poster from existing Tankoyomi/AniList cache.
- Last choice: neutral placeholder, plus retry-on-demand the next time the detail view opens.

Do not block final registration on cover extraction. A volume with a valid cbz and no cover is readable; a volume with an invalid archive is not. Archive validation gates completion. Cover generation can finish after completion and repaint the row.

<!-- Codex 2026-05-15: Reframes cross-source dedup from v1.1-only into a v1 minimum viable merge. -->
## Section 22 Cross-source dedup and minimum viable merge

Section 7.5's "two tiles" deferral is safer than deleting user data, but it has real user-facing cost: duplicate library hits, duplicate Continue entries if both copies are read, duplicated disk usage, and search ambiguity. The current code already has enough machinery to avoid the worst of this without building a destructive merge UI. `LibraryScanner` can skip paths claimed by a Tankoyomi-origin library record (`src/core/LibraryScanner.cpp:87-99`), and `ComicsLibraryRecord` already stores `canonicalSeriesPath` (`src/core/manga/ComicsLibraryRecord.h`).

Recommended v1 mitigation: **adopt, do not migrate.**

When the user adds a Premium series:

1. Normalize the Premium title and aliases.
2. Scan existing Comics folder-import series for an exact normalized title match.
3. If exactly one match exists, create the Premium/Tankoyomi library record pointing at that existing folder instead of creating a second folder. Write the sidecar into that folder. Do not move, delete, or rename any existing files.
4. Future Premium volume downloads land in that adopted folder.
5. Because the folder is now claimed, `LibraryScanner` suppresses the folder-origin duplicate tile. The user sees one series tile.
6. If zero or multiple matches exist, fall back to new Premium folder creation and leave a v1.1 merge task.

This gives v1 a minimum viable merge surface without destructive migration. It accepts mixed-quality contents inside one folder, but the Premium detail view can still show per-volume source state:

- `Premium`: catalog volume downloaded from torrent.
- `Local`: existing cbz in the adopted folder with no catalog match.
- `WeebCentral`: loose tail chapter downloaded through `MangaDownloader`.

Continue strip rule: one item per canonical series path. If two physical files represent the same canonical chapter, prefer the Premium volume path once present, but preserve the older progress until the user opens the Premium version.

Disk rule: never delete the pre-existing folder-import cbz automatically. Any quality replacement/delete flow belongs in v1.1 because it is user-data destructive.

<!-- Codex 2026-05-15: Adds ongoing-series migration rules for loose chapters becoming covered by volumes. -->
## Section 23 Ongoing-series gap lifecycle

The draft underweights the long-running lifecycle where a loose WeebCentral chapter later receives official volume attribution and becomes part of the Premium catalog. This must be designed now because it affects identity and read-state keys.

Required invariant: every catalog chapter gets a stable `canonicalChapterKey`, independent of source URL and independent of the file that currently contains it. Example shape: `one_piece:ch_1146`. The Premium volume entry and the loose-tail WeebCentral entry must resolve to the same canonical key once the catalog knows they are the same chapter.

Catalog update behavior:

1. New catalog version says Volume 110 contains chapters 1146-1155.
2. Detail view moves those chapters out of "Latest chapters (WeebCentral)" and into the Volume 110 row.
3. Existing WeebCentral cbz files are not auto-deleted.
4. If the Premium volume is not downloaded, loose downloaded chapters remain readable through their existing path and the detail view can show them as `Local loose` under the volume row.
5. If the Premium volume is downloaded, the preferred read path for those canonical keys becomes the Premium cbz. The loose cbz is hidden from primary rows but remains on disk.
6. Continue strip resolves by canonical key first, then preferred path. This prevents a chapter from jumping backward in apparent reading order just because it moved from loose tail to a volume.

Read-state preservation:

- The existing progress system is file/path oriented through the Comics progress key map. Writing-plans should add a Premium read-state alias layer keyed by `canonicalChapterKey` before implementing ongoing migration.
- When a loose chapter becomes covered by a Premium volume, copy `read`, `lastPage`, `pageCount`, and `updatedAt` from the loose chapter key to the canonical key if the canonical key has no newer progress.
- Do not overwrite newer Premium progress with older loose progress.

This is v1.x behavior, but the schema support must land in v1. Without canonical chapter keys in the catalog, later migration becomes guesswork based on title strings and chapter numbers.

<!-- Codex 2026-05-15: Adds the community-catalog threat model and signing gate. -->
## Section 24 Trust, safety, and community-catalog threat model

The v1 product can be one-curator and bundled-only, but the schema should not leave a dangerous community door half-open. A community JSON can point to a malicious or illegal payload. The threat model includes: non-cbz files disguised as cbz, archive bombs, archives with non-image executable payloads, swapped magnets, and illegal content that the app must not surface.

V1 requirements:

1. Catalog includes `expectedInfoHash`; app rejects a magnet that resolves differently.
2. App accepts only `.cbz` files for `one-cbz-per-volume`.
3. App validates the archive before final registration.
4. App rejects archives with executable/script entries or nested archives.
5. App bounds decompression for cover extraction and page counting.
6. App keeps community catalogs disabled. Bundled catalogs are the only enabled source.

Forward-compat gate before any community catalog:

1. Signed catalogs. Hemanth or a Tankoban release key signs the catalog; the app verifies before loading.
2. Explicit manifest fields inspired by Stremio's addon manifest shape: `id`, `name`, `version`, `description`, `contact`, `behaviorHints.p2p`, and `behaviorHints.adult` where relevant. Stremio's manifest explicitly models P2P behavior hints, which is the right precedent for warning users about torrent-backed sources.
3. Curator review outside the app. Do not allow arbitrary remote catalogs to auto-install by URL in v1.x.
4. Quarantine failure mode. If validation fails, the file remains in staging/quarantine and is never registered into Comics library or opened in Comic Reader.

This is not overengineering. The cost to add `expectedInfoHash`, schema version, and signature slots now is low. Retrofitting trust identity after users share catalogs is expensive and risky.

<!-- Codex 2026-05-15: Re-estimates curation work and recommends a one-shot helper. -->
## Section 25 Curation tooling and research discipline

The 10-15 hour estimate is optimistic for 30 series. Manual curation is likely closer to 30-50 hours if done carefully.

Per-series time estimate:

- Pick candidate torrent and trusted uploader: 10-25 minutes.
- Fetch metadata and verify one-cbz-per-volume format: 5-15 minutes.
- Capture fileIndex, file size, piece range, and names: 5-20 minutes depending on volume count.
- Build chapter-to-volume mapping from mangareader.to/AniList/MangaUpdates/manual references: 15-60 minutes.
- Hand-verify a sample of volume boundaries and special chapters: 10-30 minutes.
- Fill schema, normalize aliases, page-count smoke one volume: 10-20 minutes.

Small completed series such as Death Note may take 30-45 minutes. One Piece or Kingdom can take 2-4 hours. That puts the realistic first 30-series catalog at 30-50 hours, not 10-15.

Recommendation: build a curation helper before hand-curating all 30. It can be a local Python or C++ one-shot tool; it does not ship in app UI.

Tool shape:

```text
premium-catalog-draft --magnet "magnet:?xt=urn:btih:..." --series-id one_piece --title "One Piece" --mapping-source mapping.csv --out one_piece.draft.json
```

Minimum helper duties:

1. Resolve torrent metadata using libtorrent or a metadata-only path.
2. Emit file list with index, path, size, inferred volume number, pieceStart, pieceEnd.
3. Flag non-cbz files and multi-file-per-volume patterns.
4. Accept a manually prepared chapter mapping CSV.
5. Emit a JSON skeleton with validation warnings.
6. Optionally compute page count and first cover candidate for a sampled subset.

Do not build a runtime mangareader scraper into Tankoban for v1. mangareader.to's terms page says access is for temporary personal, non-commercial viewing and prohibits copying/modifying materials. For curation, keep any helper local, rate-limited, and preferably driven by manually supplied mapping data rather than broad scraping. The app itself should consume only the derived catalog bundled with Tankoban.

Nyaa/uploader trust should also be handled as curation metadata, not runtime discovery. Record the uploader and release edition, but do not rely on uploader name alone as security identity. The infoHash is the identity.

<!-- Codex 2026-05-15: Expands the UX scale model and smoke matrix. -->
## Section 26 UX scale and smoke expansion

The main Comics library does not need a new "series-collapsed" view in v1 because it is already series-level. Premium should add one tile per series, not one tile per volume. The scale problem lives inside the detail view: One Piece has 100+ volume rows plus loose chapters. A plain `QTableWidget` can handle that count, but writing-plans should still specify:

- sticky section headers: `Volumes` and `Latest chapters (WeebCentral)`;
- row filtering inside detail: `All`, `Downloaded`, `Unread`, `Premium`, `Loose`;
- sort fixed to descending by default for ongoing series and ascending optional via existing sort affordance;
- no grid of volume covers in v1 unless performance is measured;
- Continue strip remains chapter-level in text but series-level in dedupe: title is series, subtitle is `Volume N - Page X/Y` or `Chapter N - Page X/Y`.

Smoke matrix additions:

| # | Case | Validates |
|---|------|-----------|
| 9 | Metadata-first add with `paused=true` path | No all-files download window before file priorities are applied |
| 10 | Target cbz boundary shares pieces with adjacent file | App completes target volume and does not register adjacent volume |
| 11 | Two concurrent volumes in same series | Union file-priority vector, no cancellation of the first request |
| 12 | Two concurrent volumes in different series | Cross-torrent state isolation |
| 13 | Kill app after request ledger write but before metadata | Restart reattaches and still knows destination path |
| 14 | Kill app after file complete but before final rename | `.tankoban-part` recovers or is cleaned; no scanner ghost |
| 15 | Low disk during staging | Provider surfaces disk error, does not register partial cbz |
| 16 | Network drop mid-download | libtorrent reconnects; UI shows stalled/waiting without modal nag |
| 17 | External delete of completed Premium cbz | `MangaDownloadIndex::validateAll()` evicts stale mapping and row returns to Download |
| 18 | Adopt existing folder-import series | One library tile, no duplicate Continue item, no file deletion |
| 19 | Catalog version moves loose chapter into new volume | Read-state survives and detail order stays coherent |
| 20 | Malformed catalog entry | Reject/warn severity matches Section 20; app continues loading other valid entries |
| 21 | Archive with no images or suspicious non-image payload | Stays quarantined; no reader open; no library registration |

Catalog reload decision: v1 should be restart-required. No hot reload. If a catalog file changes while downloads are in flight, the provider keeps using the persisted request snapshot for those requests and the new catalog is read on next launch. Hot reload can be v1.1 after the request ledger and validation model are proven.

Phase sequencing recommendation:

1. Schema + validator + curation helper.
2. Provider request ledger + persistent staging + metadata/file-priority round trip.
3. Volume-aware index registration.
4. Detail view volume rows using fake/local catalog fixtures.
5. Provider-to-detail progress/completion wiring.
6. Search dedup + Premium chip using theme accent.
7. Adopt-existing-folder mitigation.
8. Ongoing-series canonical chapter key migration.
9. Hardening smoke: crash, low disk, archive validation, boundary pieces.

Do not start with UI chrome. The hardest bugs are identity, persistence, and file completion. UI should consume stable states after those are shaped.

<!-- Codex 2026-05-15: Resolves every Section 14 open question with concrete recommendations. -->
## Section 27 Section 14 resolved recommendations

1. **Torrent file-completion signal granularity.** Recommendation: do not add a new engine signal for v1. Use existing `pieceFinished`, `pieceRangeForFileOffset`, `fileByteRangesOfHavePieces`, and `flushCache`. Add `fileCompleted(infoHash, fileIndex)` only if provider code becomes noisy during implementation.
2. **`registerVolume` vs per-chapter `registerChapter`.** Recommendation: add a volume-aware primitive or refactor index entries to support one path mapped to many chapter keys. Do not call `registerChapter` repeatedly with the same cbz path as the final design.
3. **Staging directory persistence model.** Recommendation: persistent app-data staging keyed by infoHash. Per-session staging is wrong for crash resume.
4. **JSON schema validation strictness.** Recommendation: strict validator with reject-file, reject-series, reject-volume, and warn severities as listed in Section 20. Startup should log and surface a compact "Premium catalog has invalid entries" diagnostic in debug/dev builds; user release builds can silently skip invalid entries unless all catalogs fail.
5. **Catalog curation tooling.** Recommendation: build a local one-shot helper before curating all 30 series. Manual-only is likely 30-50 hours and will produce avoidable fileIndex/chapter-boundary mistakes.
6. **Cover extraction performance and lifecycle.** Recommendation: validate archive before completion, extract cover off-thread, reuse `LibraryScanner`'s cover/folder heuristic, and allow readable completion before cover generation finishes.
7. **Mixed-origin volume read-state.** Recommendation: add canonical chapter keys and an alias layer before ongoing-series migration. Use Premium path as preferred once available, but never delete loose WeebCentral cbz automatically.
8. **Theme palette for Premium chip.** Recommendation: no hardcoded gold/amber chip. Use `Theme::current().accent` plus neutral text/background. In Dark that is `#c7a76b`; in current named modes it follows the mode registry. Light HSL values are out of scope because Light is currently removed from the theme system.
9. **Anything missing.** The missing load-bearing items are persistent request ledger, atomic `.tankoban-part` finalization, expected infoHash validation, archive quarantine, adopt-existing-folder mitigation, canonical chapter keys for ongoing migration, and restart-only catalog reload semantics.
