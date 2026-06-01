# TankoLibrary AudioBookBay Fix TODO

**Owner:** Agent 4B (Sources — Tankorent + Tankoyomi + TankoLibrary).
**Authored:** 2026-04-22 by Agent 4B, after 5-min curl-only reachability + DOM probe per Hemanth greenlight "yes" to "Want me to kick off the probe?". Self-authored because I own both halves of the plumbing (scraper side in TankoLibrary + torrent handoff side in Tankorent/TorrentEngine).
**Status:** AUTHORED; M1 queued pending Hemanth final word.
**Reference probe:** [agents/prototypes/tankolibrary_abb_probe_2026-04-22/FINDINGS.md](agents/prototypes/tankolibrary_abb_probe_2026-04-22/FINDINGS.md) + 4 artifact files (home.html, search_rhythm.html, detail_rhythm.html, main.js).

---

## 1. Context

`TANKOLIBRARY_FIX_TODO.md` (Agent 0 authored 2026-04-21) shipped Track A main path for EPUB books end-to-end by 2026-04-22 midday:
- M1: scaffold + AnnaArchiveScraper search (`c8052ee`-era)
- M2.1: AA detail page fetch + QStackedWidget detail view
- M2.2: AaSlowDownloadWaitHandler + scaffolded resolveDownload (captcha-blocked on live AA)
- M2.3: LibGen-first pivot — added LibGenScraper, AA stays compiled but captcha-gated
- M2.4: BookDownloader + real Download button + end-to-end LibGen → file-on-disk → BooksPage rescan
- Track B batch 1 (2026-04-22 afternoon): novels-only URL filter + AA default-disabled + EPUB-only checkbox

Scope today is novel/book EPUBs only. **Audiobooks are a missing category** — ABB (AudioBookBay) is the widest-coverage index site, Hemanth flagged it unprompted, and it's architecturally aligned with our existing stack (torrent magnet handoff to TorrentEngine, which we already own).

## 2. Objective

Add **AudioBookBay** as a new book-source feeding an **Audiobooks tab** sibling to the existing Books tab in TankoLibraryPage. Downloads resolve to magnet URIs (not HTTP — probe confirmed all ABB HTTP download paths are filehost ad-walls with captcha + wait timers + throttled speeds, same class of pain as AA's /slow_download/ that forced the LibGen-first pivot). Magnets hand off to the existing `TorrentEngine::addMagnet`. Resulting `.m4b` / `.mp3`-folder torrents land in the configured books root folder (`Media\Books\` per `feedback_quality_standard` and current Agent 2 audiobook library location); `BooksScanner`'s wrapper-flatten walker (shipped `d1cfb10` this same session) picks them up as audiobook tiles.

On completion of Track A (M1 + M2):
1. TankoLibraryPage gains a segmented "Books / Audiobooks" switcher at the top of the results region.
2. Books tab behaves unchanged (LibGen-first, AA commented out, EPUB-only checkbox).
3. Audiobooks tab runs AbbScraper against ABB with rich metadata rendering (cover / format / bitrate / size / posted date).
4. Download button on an audiobook row constructs the magnet URI client-side (replicating ABB's own `main.js` logic verbatim), calls `TorrentEngine::addMagnet(uri, booksRoot)`, and the torrent progress surfaces through the existing TransfersView inline tab.

On completion of Track B: filter chips on Audiobooks tab (format = M4B / MP3 / Mixed, bitrate minimum, unabridged-only), cover image cache, optional separate `Media\Audiobooks\` root if Hemanth later wants organizational split.

## 3. Non-goals

- **No HTTP download path from ABB.** All three ABB HTTP surfaces (Torrent Free Downloads / Direct Download / Secured Download) route through filehost ad-walls. Magnet-only from day one.
- **No ABB account / login / member features.** ABB's member system offers forum posting, request queue, and donation — none required for search or detail or magnet construction (probe verified unauthenticated access works fully).
- **No in-app BitTorrent-client reinvention.** TorrentEngine (libtorrent-rasterbar via libtorrent-sys FFI) is the one shipped torrent substrate and handles magnets today. Zero new BT code.
- **No M4B-chapter-metadata reader support in this TODO.** A single 1.5 GB `.m4b` audiobook (the common ABB shape) has internal chapter markers in MP4 metadata. Agent 2's current `AudiobookDetailView` assumes `chapters = files` (walks directory for MP3s, one row per file). For single-M4B audiobooks it'd render one row showing full 60h duration, losing chapter navigation. **This is a real post-download UX gap** — but it's Agent 2's reader-side domain. I flag it in §11 for Agent 2's next wake; my scraper ships whether or not that lands. Until Agent 2 extends the walker, M4B audiobooks just play as single-track with no chapter nav.
- **No cross-source unified feed.** Books tab = LibGen (future: AA). Audiobooks tab = ABB (future: possibly AA's audiobook corpus if the captcha-block ever lifts). No mixed results.
- **No search history or saved-book persistence.** Session-scoped like every other source surface today.
- **No Agent 7 audit.** Probe was 5 min of curl; full evidence is at `agents/prototypes/tankolibrary_abb_probe_2026-04-22/`. If a post-ship comparative audit against a reference audiobook client (e.g., Cagliostro, Audiobookshelf) becomes useful, that's a separate Trigger-C ask.

## 4. Agent ownership

**Primary + only:** Agent 4B.

**Cross-agent touches expected — none hard:**
- **Agent 2 (Book Reader)** — informational flag only in §11: Agent 2 may want to extend `BooksScanner` + `AudiobookDetailView` to handle single-M4B-with-internal-chapters (ffprobe chapter extraction). Not a blocker; my scope ships regardless.
- **Agent 5 (Library UX)** — no touches. TankoLibraryPage rendering stays self-contained per the M1/M2 LibGen work.
- **Agent 0** — CLAUDE.md dashboard row at authoring (if Agent 0 wakes in time; otherwise I post an own-dashboard-patch line in chat.md per rotation discipline). Commit sweep at each milestone exit.

## 5. Architecture deltas

**New files:**
```
src/core/book/
    AbbScraper.h             ~80 LOC  — BookScraper interface impl + magnet ctor helper
    AbbScraper.cpp          ~420 LOC  — QNetworkAccessManager search/detail/magnet paths
```

**Modified files:**
```
src/core/book/BookResult.h                   — optional audio-specific fields
src/ui/pages/TankoLibraryPage.h              — tab state + per-tab scraper lists
src/ui/pages/TankoLibraryPage.cpp            — segmented-control UI + per-tab dispatch + download-routing-by-sourceId
CMakeLists.txt                               — +2 entries for AbbScraper.{h,cpp}
```

**No API changes to existing shipped files:**
- `TorrentEngine::addMagnet(const QString& uri, const QString& savePath)` — already exists, used by Tankorent today, same signature honored.
- `TorrentEngine::torrentStateUpdated(infoHash, state)` — already exists, TransfersView consumes it today, same signature honored.
- `BookScraper` base class — stays as-is (search / fetchDetail / resolveDownload signals already-hoisted per M2.3 refactor).
- `BookDownloader` — stays untouched; download flow branches BEFORE BookDownloader is called, routing via `sourceId == "abb"` to TorrentEngine instead.

## 6. BookResult field strategy

Option A (additive new fields):
```cpp
struct BookResult {
    // ... existing fields unchanged ...
    QString audioFormat;    // "M4B" | "MP3" | "Mixed" | ""
    QString audioBitrate;   // "64 Kbps" | "256 Kbps" | "?" | ""
    QString audioPosted;    // "17 Nov 2020" | ""
    QString audioUploader;  // uploader username | ""
    QString magnetUri;      // set when sourceId == "abb" and Info Hash extracted
};
```

Option B (stuff into existing slots): put format in `format` (already exists, "EPUB"/"PDF"/etc — add "M4B"/"MP3"/"Mixed"), posted date in `year` (loose — year field is currently "2015" style), bitrate nowhere.

**Decision landed at authoring:** Option A. Bitrate + uploader have no existing slots; adding 5 fields is cheaper than cross-type overloading. Wake-entry Rule-14 call — revisit if in-batch shape pressure argues otherwise.

## 7. UI — segmented control

**Layout:** row 0 of search-controls (above the query input) gets a 2-button segmented QButtonGroup:

```
[ Books ] [ Audiobooks ]
         <search input>       [ Search ]
         [ ] EPUB only                      <- Books tab only
         [ ] Unabridged only   [ Format v ] <- Audiobooks tab only (Track B)
```

State:
- `QButtonGroup* m_sourceTabGroup`, `QButton* m_tabBooks`, `QButton* m_tabAudiobooks`
- `enum class Tab { Books, Audiobooks }; Tab m_currentTab = Tab::Books;`
- Per-tab scraper lists: `QList<BookScraper*> m_scrapersBooks;` (LibGen today, AA re-enable-possible), `QList<BookScraper*> m_scrapersAudiobooks;` (ABB today).
- Per-tab persisted settings: `QSettings("tankolibrary/current_tab")`, `QSettings("tankolibrary/audiobooks/format_filter")`, etc.
- Tab switch clears grid + resets status label + dispatches a fresh search IF query is non-empty (matches LibGen's refresh-on-filter-toggle pattern).

**Styling:** no color, grayscale segmented-control. Active tab has `background: #2a2a2a; color: #e0e0e0;`, inactive `background: transparent; color: #909090;`. Matches `feedback_no_color_no_emoji` + existing TankoLibrary aesthetic.

**Filter chips per tab:**
- Books tab: existing "EPUB only" checkbox (Track B batch 1 behavior preserved).
- Audiobooks tab: deferred to Track B.

## 8. Track A — milestones

### M1 — Tab scaffold + AbbScraper search + grid population

**Scope:**
1. Segmented control "Books | Audiobooks" at top of TankoLibraryPage results area. Books tab = existing behavior unchanged. Audiobooks tab = empty-state on entry, query-input routes to `m_scrapersAudiobooks`.
2. `src/core/book/AbbScraper.{h,cpp}`:
   - `search(QString query)` → `GET https://audiobookbay.lu/?s=<urlencoded>`, parses all `<div class="post">` blocks (NOT `post re-ab` honeypots), emits `resultsReady(sourceId="abb", QList<BookResult>)`.
   - Honeypot filter: use `QRegularExpression(R"RX(<div class="post">)RX")` literal, NOT class-contains. Verified in probe §3.
   - Per-row extract: title + detailUrl + category + language + keywords + posted-date + format (M4B/MP3) + bitrate + file-size + cover-URL + uploader. All 11 fields land in the BookResult shape (extended per §6).
   - No detail-fetch, no magnet-construct at this milestone.
3. TankoLibraryPage wiring: when `m_currentTab == Audiobooks`, parallel-dispatch search via `m_scrapersAudiobooks` (currently just AbbScraper instance). Per-source status line pattern mirrored from M2.3 ("Done: 20 from AudioBookBay").
4. Grid rendering: existing `BookResultsGrid` consumes `BookResult` list unchanged. Rich metadata (bitrate / posted-date / uploader) renders via the same card shape. `m_audiobooksOnly` grid-mode flag optional if card layout needs audiobook-specific fields surfaced (bitrate instead of pages, etc.); decide at batch entry.

**Exit criteria:**
- Typing "rhythm of war" on Audiobooks tab returns ~10 rows (page 1 of ABB search).
- Each row shows title + author (from title or keywords) + format (M4B/MP3) + file size + cover image.
- No honeypot rows in the grid (verified by search result count matching `class="post">` exact-match count).
- Switching Books↔Audiobooks preserves query + clears grid cleanly.
- `build_check.bat` BUILD OK.
- MCP self-drive smoke captures full flow: type query → hit Enter → 10 rows render → screenshot.

### M2 — Detail page + magnet construction + TorrentEngine handoff end-to-end

**Scope:**
1. AbbScraper detail flow: `fetchDetail(QString detailUrl)` fetches `/abss/<slug>/`, extracts Info Hash via `R"RX(<td>Info Hash:</td>\s*<td>([0-9a-fA-F]{40})</td>)RX"`, extracts file-list for preview, emits `detailReady(BookResult enriched)`.
2. AbbScraper magnet construction: `QString constructMagnet(QString infoHash, QString title)` returns magnet URI matching ABB's `main.js` format (hash + 7 hardcoded trackers, verbatim including the `:69691337` typo — libtorrent ignores bad trackers gracefully).
3. AbbScraper `resolveDownload(detailUrl)` returns magnet URI (not a list of HTTP URLs like LibGen). Signal shape: reuse `downloadResolved(md5, QStringList urls)` with `urls = {magnetUri}` — single-element list, same contract shape as LibGen, consumer differentiates by `sourceId == "abb"`.
4. TankoLibraryPage.cpp download routing — modify `onDownloadClicked`:
   ```cpp
   if (row.sourceId == QStringLiteral("abb")) {
       const QString magnet = row.magnetUri;  // constructed at fetchDetail time
       const QString dest = m_bridge->rootFolders(QStringLiteral("books")).value(0);
       m_bridge->torrentEngine()->addMagnet(magnet, dest);
       updateStatusLabel(tr("Starting torrent download..."));
       // No progress bar wiring — progress surfaces via TransfersView inline tab
   } else {
       // existing BookDownloader path for LibGen/AA
   }
   ```
5. Status messaging: post-magnet-add, status label shows "Torrent added — track progress in Transfers tab" and the Download button rearms immediately (unlike BookDownloader which locks button during HTTP stream).

**Exit criteria:**
- Type "rhythm of war" → Audiobooks tab renders 10 rows → double-click row 1 → detail view with Info Hash + announce + file list visible.
- Click Download → torrent starts in Tankorent backend (verify via `out/torrent_trace.log` or TransfersView row appearance).
- `.m4b` lands in `Media\Books\` configured root once enough pieces download (doesn't need to complete in smoke — just needs to start + show active torrent state).
- BooksPage rescan picks up the arriving folder (may need manual "Refresh" click in smoke if rescan cadence is slow).
- `build_check.bat` BUILD OK.
- MCP self-drive smoke: captures search → detail → download-click → TransfersView progress row.

## 9. Track B — polish (after M2 ships, capacity-gated)

**Batch B1 — Format/bitrate filter chips on Audiobooks tab:**
- "Format" QComboBox: All / M4B only / MP3 only. Persisted to `QSettings("tankolibrary/audiobooks/format_filter")`.
- "Unabridged only" QCheckBox: filter rows where title or keywords contain "unabridged" OR category includes "Unabridged" (ABB has dedicated Unabridged category tag). Default ON. Persisted.
- Client-side filter over cached `m_results`, same pattern as Books-tab EPUB-only checkbox.

**Batch B2 — Cover image fetch + cache:**
- Download cover URL from search row → cache to `%LOCALAPPDATA%/Tankoban/cache/abb_covers/<md5_of_url>.jpg` → grid renders from cache.
- 24h TTL on cache, LRU eviction at 500 entries.

**Batch B3 — Optional separate Audiobooks root:**
- `LibraryConfig` gains optional `audiobooksRoot` (separate from `booksRoot`).
- If set, ABB downloads land there instead of `booksRoot`. If not set, fallback to `booksRoot`.
- Settings-page UI exposes it; defaults to empty (match current behavior).
- Requires Agent 5 coordination if LibraryConfig UI work is in their domain (quick check before implementing).

## 10. Rule-14 decisions landed at authoring

1. **Two-tab UX, not unified feed.** Filter vocabulary diverges too much between EPUB books and audiobooks to share one surface. Tabs give clean mental model + independent filter state per tab.
2. **Same `Media\Books\` destination as EPUBs.** Simplest; matches where Hemanth's existing audiobook library already lives per Agent 2's scanner evidence. Separate `Media\Audiobooks\` root is Track B batch 3 polish if organization concerns surface later.
3. **ABB-only on Audiobooks tab at launch.** Mirrors the LibGen-only Books-tab launch pattern from M2.3. AA's audiobook corpus is captcha-blocked same as AA's book corpus; no point adding a second scraper that can't complete.
4. **Magnet-only, never HTTP.** All three ABB HTTP paths are filehost ad-walls (probe §7). Magnet construction is 100% replicable server-side from info hash + 7-tracker list in `main.js`. Zero JS execution, zero webview.
5. **BookResult field additive (Option A §6).** Cross-type overloading (stuffing bitrate into unrelated fields) would confuse consumers.
6. **Single-M4B-chapter-nav is NOT a blocker.** File plays as one-track-full-duration on first ship; Agent 2 extends later for chapter nav.
7. **No BookScraper interface changes.** Magnet URI flows through existing `downloadResolved(md5, QStringList)` signal as single-element list.

## 11. Deferrals and follow-ons

- **Agent 2 follow-on (not blocking this TODO):** `BooksScanner` + `AudiobookDetailView` extension to ffprobe-chapter-extract single-M4B audiobooks. Without it, ABB-downloaded M4B audiobooks play as single-track. Flagged for Agent 2's next wake by this TODO — Agent 2 decides when to pick up.
- **AA audiobook corpus:** blocked by same `/books/` captcha gate from M2.2. Re-evaluate if/when an AA-side captcha solution lands.
- **Z-Library audiobook corpus:** same stateful-source deferred cluster as the parent TankoLibrary TODO's §3. No earlier.
- **Post-ship audit vs reference audiobook client:** if Hemanth wants comparative audit against Cagliostro / Audiobookshelf / Plex audiobook handling, that's a separate Trigger-C ask to Agent 7.
- **Pagination UI:** M1 ships with page-1-only. Pagination control (Next / Prev / jump-to-page) deferred to Track B batch if users want it; ABB search returns ~12 pages for popular queries, so surface pressure likely exists.
- **Sort control:** ABB search has no visible sort surface (default is freshness). If sort-by-size / sort-by-bitrate becomes wanted, client-side re-sort over cached `m_results`.

## 12. Dependencies / HELP needs

**None.** All substrate already shipped and battle-tested:
- `TorrentEngine::addMagnet` — proven daily by Tankorent users (Hemanth's primary use case).
- `TorrentEngine::torrentStateUpdated` — consumed today by TransfersView inline tab.
- `BookScraper` base class — stable, no signature changes.
- `BooksScanner` wrapper-flatten walker — shipped `d1cfb10` this session, handles multi-file audiobook folders.
- `build_check.bat` build path — shipped, no env changes needed.

## 13. Test / smoke plan per milestone

**M1 exit smoke (MCP self-drive):**
1. `build_check.bat` → BUILD OK
2. Launch via `out/Tankoban.exe` (env already baked from `build_and_run.bat` contract)
3. Sources → TankoLibrary → segmented control visible with "Books | Audiobooks"
4. Click Audiobooks → query empty-state
5. Type "rhythm of war" → Enter → 10 rows render with covers, format, bitrate, size
6. Screenshot confirms no visible honeypot cells (manual count match against `class="post">` count in DOM)
7. Click Books tab → EPUB-only checkbox visible, LibGen query works unchanged (non-regression)
8. Click Audiobooks tab → query preserved, grid re-populates from cache
9. Rule 17 cleanup: `scripts/stop-tankoban.ps1`

**M2 exit smoke:**
1. Build + launch
2. Audiobooks tab → "rhythm of war" → 10 rows
3. Double-click row 1 → detail view with Info Hash + announce URL + file list
4. Click Download → status flips to "Torrent added — track progress in Transfers tab"
5. Navigate to Transfers inline tab → row visible with "Rhythm of War" + active state + some piece progress
6. Wait 30s → some pieces downloaded, piece count > 0
7. Kill Tankoban at ~30s mark (don't need full 1.5 GB download in smoke)
8. Restart Tankoban → Transfers tab still shows torrent in paused/queued state (state persistence)
9. Resume → continues downloading
10. Inspect `Media\Books\` directory → `.part` or partial folder visible for the torrent
11. Rule 17 cleanup

## 14. Changelog

| Date | Author | Change |
|------|--------|--------|
| 2026-04-22 | Agent 4B | Authored after 5-min curl probe of audiobookbay.lu; Track A scoped 2 milestones; Track B scoped 3 batches; all Rule-14 calls landed at authoring per §10. |
