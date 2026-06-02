# Spec — Comics Western Downloads & Read

**Status:** DRAFT (spec) · **Author:** Agent 1 (Opus) · **Date:** 2026-06-02
**Arc:** COMICS_WESTERN — arc 3 of 3 (richness ✔ → search ✔ → **downloads (this)**)
**Predecessor reading:** `2026-06-02-comics-western-search-design.md` (search) · `2026-06-01-comics-western-richness-design.md` (richness) · `scripts/comics_catalogue/RICHNESS_RECON.md` (download sources + matching problem)

---

## 1. The one-sentence shape

Make a Western collected edition **downloadable in place and readable** — click an edition → resolve its GetComics post live → download (magnet via the existing torrent engine, or a direct link via a new in-app downloader) → register it like a manga download → read it in the existing comic reader.

## 2. The problem this solves (in Hemanth's words)

- *"the real stuff.. actually being able to search, add comics and download them and read them."* — this arc is the **download → read** half of that loop. Search + add shipped (arc 2). Reading itself is essentially free (the comic reader already opens any CBZ/CBR); the real work is **getting the file**.

## 3. Ratified decisions (Hemanth, 2026-06-02)

1. **Live at click.** Resolve the GetComics post the moment the user clicks Download — works for ANY searched series (incl. live-searched ones like The Walking Dead), always-fresh links. Higher ceiling than baking links at harvest. The new build is a GetComics **search + fuzzy-match** in C++ (mirrors the RCO scraper already shipped).
2. **Magnet + in-app direct downloader.** Magnet → the existing headless torrent API. Direct links (TeraBox/Pixeldrain/Mega/getcomics main-server) → a NEW in-app HTTP downloader (handles the `getcomics.org/dls/<token>` redirect gate). Fully in-app for every edition; magnet preferred, direct-link fallback.
3. **Netflix-style, in-library, in-place.** Click an edition on the series page → downloads in place → progress on that edition's row → flips to **Read** → opens in the comic reader. Reuses manga's `MangaDownloadIndex` + `ComicReader` (zero new reader work). Realizes the COMICS_TANKOYOMI_STREAM_MERGER "Netflix-style in-library downloads" vision for Western.
4. **Grab per-edition covers.** The GetComics post carries a per-edition `og:image` cover; pull it on the same fetch so each TPB/Compendium shows its own art (vs the single shared series cover today).

## 4. User-facing flow

On a Western series page each collected edition (Compendium / Omnibus / TPB / Deluxe) has a **Download**. Click → "Finding download…" → the GetComics post is resolved live → the edition downloads in place (magnet to the torrent engine, or a direct link via the in-app downloader) → progress shows on that edition's row → on completion the row flips to **Read** → opens in the same comic reader everything else uses. The downloaded edition is tracked in the library exactly like a manga download and shows its own per-edition cover. Nothing leaves the app.

## 5. The crux — matching (stated honestly)

The one genuinely-new, *uncertain* piece is **matching**: given an edition label like *"Invincible Compendium Vol. 1"*, find the *right* GetComics post. We search GetComics (`getcomics.org/?s=<query>`), fuzzy-match the results (title + year + edition-type/tier), and take the best **confident** match. Matching is inherently imperfect, so the rule is **fail safe**:

- If no result clears the confidence threshold, or the matched post has no usable download, the edition shows **"No download found"** — never grab the wrong file.
- The heuristic will be tuned against real edition titles (same iterative approach as the richness arc's Wikidata matching). Expect this to be the part that needs the most iteration; it is isolated in one pure-ish function so it is unit-testable.

## 6. Architecture — three new bricks, the rest reuse

### 6.1 What already exists (reused, mostly unchanged)
- **Magnet download:** `TorrentClient::addMagnetHeadless(magnetUri, category, destinationPath)` → infoHash (`src/core/torrent/TorrentClient.h:173`). `listActive()` / `downloadProgress(folderPath)` for progress. **Agent 4's domain** — see §10.
- **Download tracking:** `MangaDownloadIndex::registerVolume(sourceId, seriesId, volumeNumber, canonicalPath, fileSizeBytes, chapterIds)` (`src/core/manga/MangaDownloadIndex.h`) — source-agnostic; Western uses `sourceId = "getcomics"`.
- **Provider→state wiring:** `ComicsPage::onProviderVolumeCompleted(seriesId, volNum, cbzPath, kind)` (`ComicsPage.cpp:~1986`) → registers + calls `ComicsSeriesView::setVolumeDownloadState(volumeNumber, cbzPath, true)`. The provider signal wiring (`ComicsPage.cpp:~408`/`~941`) is polymorphic — a new provider plugs in identically.
- **Edition tile states:** `ComicsSeriesView` Complete/Read rendering + `openVolume(volumeNumber, cbzPath)` → `ComicsPage::onComicsSeriesOpenVolume` → `emit openComic(cbzPath, seriesCbzList, seriesName)`.
- **Reader (zero change):** `ComicReader::openBook(cbzPath, seriesCbzList, seriesName)` (`src/ui/readers/ComicReader.cpp:941`); `MainWindow::openComicReader` (`MainWindow.cpp:1372`, wired `:267`). Formats CBZ/CBR/RAR via `ArchiveReader` (`src/core/ArchiveReader.*`).
- **Offline reference to port (NOT runtime):** `scripts/comics_catalogue/getcomics_resolve.py` — `extract_downloads(html)` + `pick_best()` priority **magnet → main_server → pixeldrain → mediafire → mega**.
- **Scraper pattern to mirror:** `src/core/manga/ReadComicsScraper.cpp` (QNAM + regex live HTTP).

### 6.2 The three new bricks
1. **`GetComicsResolver`** (`src/core/manga/GetComicsResolver.{h,cpp}`) — QObject + NetSeam-created QNAM.
   - `resolve(const QString& editionTitle, int year, const QString& tierLabel)` → searches `getcomics.org/?s=`, parses result list, **fuzzy-matches** to the best confident post, fetches that post HTML, parses **{magnet, ddlLinks[] (ordered), perEditionCoverUrl}** (port `getcomics_resolve.py`), emits `resolved(EditionDownload)` or `resolveFailed(reason)`.
   - The fuzzy-match scorer is a pure function (`scoreMatch(editionTitle, year, tier, candidateTitle) → int/confidence`) — unit-tested.
2. **`HttpFileDownloader`** (`src/core/net/HttpFileDownloader.{h,cpp}`) — QObject + NetSeam QNAM. Streams a URL to a file on disk, follows redirects (incl. the `getcomics.org/dls/<token>` gate), emits `progress(received,total)` / `finished(path)` / `failed(reason)`. Reusable beyond comics.
3. **`WesternVolumeDownloader`** (`src/core/manga/WesternVolumeDownloader.{h,cpp}`) — the provider that ties it together, mirroring `TorrentVolumeProvider`'s signal shape.
   - `requestVolume(seriesId, volumeNumber, editionTitle, year, tierLabel, destinationPath)`:
     1. `GetComicsResolver::resolve(...)`.
     2. On `resolved`: if a magnet exists → `TorrentClient::addMagnetHeadless(magnet, "comics", destinationPath)`; else walk `ddlLinks` in priority order through `HttpFileDownloader` until one succeeds.
     3. Locate the downloaded readable artifact (a `.cbz`/`.cbr` file) in the destination.
     4. Emit `volumeCompleted(seriesId, volumeNumber, cbzPath)` / `volumeProgress(...)` / `volumeFailed(...)` — same shape as the manga providers.

### 6.3 UI wiring
- **No source-picking for Western — click downloads directly.** Western has ONE source (GetComics), so unlike manga (which opens the Sources panel to pick among sources) a Western edition click **starts the download immediately**. The search arc gated Western clicks to `ComicsSourcesPanel::showComingSoon()`; this arc replaces that gate in `onVolumeRowActivated` (source=="rco") with a `WesternVolumeDownloader::requestVolume(...)` call.
- **Progress renders on the edition TILE, not the panel.** Reuse the exact manga mechanism: `volumeProgress` → the `VolumeTile`'s download-progress state (**Finding download… → Downloading N% → Read**), via the same `setVolumeDownloadState` / VolumeTile rendering manga uses. The Sources panel is not needed for the Western single-source path (it stays unused/hidden for Western, consistent with the §6 layout polish that already hides it for editionless series).
- **ComicsPage** owns a `WesternVolumeDownloader` (constructed with the shared `TorrentClient` via the existing `setTorrentClient` wiring + a `NetSeam` QNAM). Wire `volumeCompleted` → `onProviderVolumeCompleted(..., sourceId="getcomics")`; `volumeProgress`/`volumeFailed` → the edition tile state.
- **Per-edition covers:** `GetComicsResolver` returns `perEditionCoverUrl`; persist it onto the edition (new `editions[].cover` field in the western_catalogue JSON + a per-edition cover on `MangaVolume`) and render it on the edition tile (currently editions share the series hero cover).

## 7. Data flow

```
click Download on a Western edition
  └─ ComicsPage (source=="rco") -> WesternVolumeDownloader::requestVolume(edition)
       └─ GetComicsResolver::resolve(title,year,tier)            [NEW]
            └─ GET getcomics.org/?s=<title>  -> fuzzy-match       [NEW: scorer, unit-tested]
            └─ GET <best post>  -> {magnet, ddlLinks, cover}      [NEW: port getcomics_resolve.py]
       └─ magnet? -> TorrentClient::addMagnetHeadless(...,"comics") [EXISTS]
          else    -> HttpFileDownloader(ddlLinks in order)         [NEW]
       └─ locate .cbz/.cbr -> emit volumeCompleted(seriesId,vol,cbzPath)
  └─ ComicsPage::onProviderVolumeCompleted(sourceId="getcomics")  [EXISTS]
       └─ MangaDownloadIndex::registerVolume(...)                  [EXISTS]
       └─ ComicsSeriesView::setVolumeDownloadState(vol,path,true)  [EXISTS]
click Read
  └─ openVolume -> ComicsPage::openComic -> MainWindow::openComicReader -> ComicReader::openBook  [EXISTS, zero change]
```

## 8. Error handling / edge cases

- **No confident match / post has no usable download** → edition shows "No download found" (fail safe, §5). Never auto-grab a low-confidence match.
- **Magnet stalls / no peers** → existing torrent states surface; the edition row reflects failure/stall, user can retry. (Firewall gotcha per `feedback_stream_server_firewall_gotcha` applies — peers>0 + 0 speed = blocked.)
- **DDL host dead / redirect fails** → walk to the next `ddlLink` in priority order; if all fail → "Download failed, try again".
- **Downloaded artifact is a folder-of-images, not an archive** → v1 targets posts that deliver a `.cbz`/`.cbr` file (the common GetComics shape). If only a folder lands, surface "Unsupported download format" for now (zip-on-import is a follow-up). Flagged, not silently broken.
- **Download for a live (unsaved) series** → downloading **auto-adds the series to the Western shelf** first (a download must belong to a shelved series so it's tracked + reopenable). Reuses the §6 add-to-shelf persistence.
- **Re-download / already downloaded** → if `MangaDownloadIndex` already has the edition, the row shows **Read**, not Download (idempotent; no duplicate fetch).
- **Backslash path handling** (`feedback_libtorrent_windows_backslash_separator`) — split on `[\\/]` when locating the downloaded file.

## 9. Testing

- **Match scorer** = pure logic → **TDD** (GoogleTest, `tankoban_tests`): representative edition titles vs GetComics candidate titles → assert the right post wins / a bad set yields no-confident-match. Mirrors the richness Wikidata-match cases.
- **GetComics post parser** = fixture test over a captured GetComics post HTML (reuse `scripts/comics_catalogue/tests/fixtures/getcomics_post_capamerica.html` shape) → assert magnet + ddl links + cover extracted in priority order.
- **HttpFileDownloader** = a small redirect+write test (or smoke) — network, mostly smoke.
- **Everything else = smoke** in the running app (agent-driven where mechanical; Hemanth visual gate for the in-place download UX + read): search a comic → open → Download an edition → progress → Read → reader opens the pages.

## 10. Cross-domain coordination (Agent 4)

`TorrentClient` / the libtorrent engine is **Agent 4's domain** (Stream + Tankorent). This arc reuses the **headless** `addMagnetHeadless(...)` API (built for exactly this — non-interactive, category-routed). Coordination, before the build phase:
- Heads-up to Agent 4 (Office) that comics downloads will call `addMagnetHeadless` with category `"comics"`.
- Confirm the `"comics"` category resolves to the comics downloads root in `defaultPaths()`.
- No conflict with his per-show download lanes (TANKORENT_QUALITY_AND_QUEUE) — comics magnets are standalone (no `imdbId`/show binding).

## 11. Out of scope (deferred)

- **Per-TPB plot synopsis** — the free-source ceiling (no free source carries per-volume plot; RICHNESS_RECON). Still deferred.
- **Folder-of-images downloads** → "unsupported" for v1 (zip-on-import later).
- **Download queue/management page** — in-place per-edition is the v1 UX (Netflix-style); a dedicated downloads manager is not this arc.
- Marquee slug-discovery — already solved by live search.

## 12. Engine routing (build phase)

Design/deliberation stayed on Opus (this spec). Build phase, on Agent 1's lane:
- **GetComicsResolver + HttpFileDownloader + WesternVolumeDownloader** are scoped, well-templated src/ (mirror the RCO scraper + the manga provider pattern) — DeepSeek/Agent 9-shaped, with a reviewer pass before master.
- **Load-bearing reviews (Codex, cross-model):** the match scorer (correctness — wrong match = wrong download) and the download/file-locate path (writes to disk, hands magnets to a shared engine). Same gate that caught real bugs in the search arc.
- The torrent-engine reuse is reviewed *with Agent 4* (his domain).
- Opus (me) owns the plan, the matching-heuristic tuning, and final review/merge.

## 13. File pointers

- Reuse: `src/core/torrent/TorrentClient.h:173` (addMagnetHeadless), `src/core/manga/MangaDownloadIndex.h` (registerVolume), `src/ui/pages/ComicsPage.cpp:~1986` (onProviderVolumeCompleted) + `~408`/`~941` (provider wiring), `src/ui/pages/comics/ComicsSeriesView.cpp` (setVolumeDownloadState, openVolume), `src/ui/readers/ComicReader.cpp:941` (openBook), `src/core/ArchiveReader.*`.
- Un-gate: `ComicsSeriesView::populateSourcesForVolume` rco gate + `ComicsSourcesPanel::showComingSoon()` (added in the search arc — replaced for Western here).
- Reference to port: `scripts/comics_catalogue/getcomics_resolve.py`, `scripts/comics_catalogue/tests/fixtures/getcomics_post_capamerica.html`, `scripts/comics_catalogue/RICHNESS_RECON.md`.
- Mirror: `src/core/manga/ReadComicsScraper.cpp` (live HTTP scraper pattern), `src/core/manga/TorrentVolumeProvider.h` (provider signal shape).
- Edition data: `src/core/manga/WesternCatalogLoader.cpp` (add `editions[].cover`), `src/core/manga/MangaCatalogTypes.h` (`MangaVolume` per-edition cover, `MangaCatalog::seriesCover` already added).
- NetSeam QNAM factory: `NetSeam::createManager` (per the NetSeam migration — all QNAM route through it).
