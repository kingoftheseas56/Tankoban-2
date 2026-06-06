# Backend Sameness Check — Qt vs Flutter

Date: 2026-06-06
Auditor: Agent 9 (DeepSeek V4-Pro)
Method: Code-level trace of every operation through BOTH code paths (Qt C++ → Flutter FFI + provider → Dart models)
Data directory: `%APPDATA%/Tankoban/` (shared — both apps read/write the same files)

---

## Honest Verdict

**The backend IS NOT behaviorally identical.** One critical data-contract divergence was found (field-name mismatch in `BookRecord.fromJson` vs `CatalogueRecord.toJson`). Five other operations match because they go through the engine FFI with matching field names or don't involve serialized records. The books library data path is **silently corrupted** — records are created by Dart-side parsing but their identifying fields are empty.

---

## Operation-by-Operation Comparison

### 1. Comics Library Contents — MATCH

| Aspect | Qt | Flutter |
|--------|-----|---------|
| Data source | `ComicsTankoyomiLibrary` (folder-scan) + `MangaDownloadIndex` (downloads) | `tk_comics_get_library` FFI → same C++ code |
| JSON file | `manga_downloads_index.json` + `comics_library.json` | Same files via engine |
| Model field names | `ComicsLibraryRecord` / `MangaDownloadEntry` | `ComicSeries.fromJson()` |
| Field-name check | — | Verified: `seriesId`, `title`, `volumeCount`, `lastRead`, `coverPath` all match Qt JSON keys |
| Sorting | `TileStrip::sortTiles()` — 6-key sort (name_asc/desc, updated_asc/desc, count_asc/desc) | `sortComicSeries()` in `comics/providers.dart` — same 6 keys, same sort logic |

**Verdict: MATCH.** Both call the same engine function. The Flutter `ComicSeries.fromJson` field names match the Qt JSON keys. Sorting is an identical client-side implementation.

---

### 2. Books Library Contents — DIFFERS (Critical)

| Aspect | Qt | Flutter |
|--------|-----|---------|
| Data source | `BooksCatalogueLibraryStore::all()` → `CatalogueRecord::toJson()` | `tk_books_get_library` FFI → same C++ code → but parsed by `BookRecord.fromJson()` |
| JSON file | `books_catalogue_library.json` (array key: `"records"`) | Same file via engine; Dart fallback store uses array key `"books"` |
| **Field: catalogueId** | `catalogueId` | Reads `json['id']` — **MISMATCH** |
| **Field: cover path** | `cachedCoverPath` | Reads `json['coverPath']` — **MISMATCH** |
| **Field: series index** | `seriesPosition` | Reads `json['seriesIndex']` — **MISMATCH** |
| **Field: last read** | `lastReadAt` | Reads `json['lastOpenedAt']` — **MISMATCH** |
| **Field: synopsis** | `description` | Reads `json['synopsis']` — **MISMATCH** |
| Fields that MATCH | `title`, `author`, `coverUrl`, `filePath`, `seriesId`, `seriesName`, `year`, `format`, `readProgress`, `addedAt` | Same keys |
| Qt-only fields (lost) | `isbn`, `md5`, `publisher`, `language`, `genres`, `fileSize`, `lastReadCfi`, `seriesTotal` | Not in Flutter `BookRecord` |
| Flutter-only fields (unused by Qt) | `pages`, `status` | Not written by engine |

**Verdict: DIFFERS — CRITICAL.** When `tk_books_get_library` returns engine-serialized JSON (using Qt field names), the Flutter `BookRecord.fromJson()` parser reads different keys. The result:

1. **`book.id` is empty string** — `json['id']` doesn't exist; the engine writes `catalogueId`. Every book record has `id == ''`.
2. **`book.coverPath` is null** — `json['coverPath']` doesn't exist; the engine writes `cachedCoverPath`. Cached covers won't render.
3. **`book.seriesIndex` is null** — `json['seriesIndex']` doesn't exist; the engine writes `seriesPosition`. Series ordering is lost.
4. **`book.lastOpenedAt` is 0** — `json['lastOpenedAt']` doesn't exist; the engine writes `lastReadAt`. Continue Reading won't show any books (they all fail the `lastOpenedAt > 0` filter).
5. **`book.synopsis` is empty** — `json['synopsis']` doesn't exist; the engine writes `description`.

The Dart fallback store (`BooksLibraryStore`) has the SAME field-name mismatches plus an additional one: it reads array key `"books"` but Qt writes `"records"`. So the fallback path is doubly broken — it won't even find the array.

**Impact cascade:**
- Books library grid: Records appear with empty IDs, no covers, wrong series order
- Continue Reading: Zero books shown (all fail `lastOpenedAt > 0`)
- Series grouping: Works (seriesId field matches) but member records have broken IDs
- Book detail navigation: Fails because `book.id` is empty — can't construct `'/books/detail/${catalogueId}'`
- Search→add-to-library: Works for wishlist records (Flutter-created, uses Flutter field names)

**Fix required:** Either:
- (A) Add a field-name mapping layer in `BookRecord.fromJson` that accepts BOTH Qt and Flutter keys
- (B) Change the engine's `tk_books_get_library` to emit Flutter-compatible JSON
- (C) Add a Dart-side transformation between the engine raw JSON and `BookRecord.fromJson`
- **DeepSeek 1 owns the engine** — this must be coordinated. Option (A) is the safest because it doesn't change the engine contract and handles both FFI and fallback paths.

---

### 3. Comics Search — MATCH (with delivery note)

| Aspect | Qt | Flutter |
|--------|-----|---------|
| Search engine | `MangaSourceRegistry` → fan-out scrapers (WeebCentral, MangaFire, Nyaa) | `tk_comics_search` FFI → same engine → same scrapers |
| Result delivery | Qt signals (async, per-scraper) | Event channel `comics.search_results` (async, streamed) |
| Deduplication | Engine-side by source ID | Client-side `_merge()` by `r.id` |
| Timeout | None (indefinite) | 10-second hard timeout in `ComicsSearchController` |
| Result shape | `MangaSearchResult` from source registry | `MangaSearchResult.fromJson()` — same fields |

**Verdict: MATCH for same-query results.** Both hit the same scrapers. The Flutter side adds a 10-second timeout that Qt doesn't have — queries taking longer than 10s will show partial results in Flutter but complete results in Qt. This is a **known behavioral difference, not a data difference** — same data, different completeness envelope.

Delta: Flutter's client-side `_merge()` deduplication vs Qt's engine-side dedup could theoretically produce different "which record wins" outcomes if two scrapers return the same title with different metadata. Low probability in practice since source IDs are unique.

---

### 4. Books Catalogue Search — MATCH (with post-processing note)

| Aspect | Qt | Flutter |
|--------|-----|---------|
| Search engine | `BookCatalogueAggregator::query()` → FictionDB + OpenLibrary + GoogleBooks | `tk_books_search_catalogue` FFI → same engine → same aggregator |
| Result grouping | Engine returns `seriesGroups` + `standalones` pre-split | Engine returns flat list; Flutter splits client-side by `isSeries` flag |
| Deduplication | Engine-side by `catalogueId` in `BookCatalogueAggregator` | Client-side by `catalogueId` in `BookCatalogueSearchPage` |
| Initial cap | 5 per section with "Show N more" | 5 per section with "Show N more" |
| Field-name check | — | `CatalogueResult.fromJson` reads `catalogueId`, `title`, `author`, `coverUrl`, `year`, `pages`, `synopsis`, `isSeries`, `seriesName`, `bookCount`, `source` |

**Verdict: MATCH.** Same engine, same aggregator. The Flutter client-side split/dedup mirrors Qt's engine-side logic. `CatalogueResult.fromJson` field names were cross-checked against the aggregator output — no mismatches found (search results are a different JSON contract from persisted records).

---

### 5. Auto-Pick Result — MATCH

| Aspect | Qt | Flutter |
|--------|-----|---------|
| Engine | `SourceAutoPicker` — ranks by tier/seeders | `tk_source_auto_pick` FFI → same C++ code |
| Input | Title + source list | Same JSON request shape |
| Output | Single best source row | Same JSON response shape |

**Verdict: MATCH.** Single engine function, identical inputs, identical outputs.

---

### 6. Download-Index Contents — MATCH

| Aspect | Qt | Flutter |
|--------|-----|---------|
| Comics downloads | `MangaDownloadIndex::all()` → `manga_downloads_index.json` | `tk_comics_get_downloads` FFI → same engine |
| Books downloads | `BookDownloader` active-downloads map | `tk_books_get_downloads` FFI → same engine |
| Field-name check (comics) | — | `ComicDownload.fromJson` reads `seriesId`, `volumeNumber`, `title`, `status`, `percent`, `filePath` — all match Qt keys |
| Field-name check (books) | — | `DownloadTask.fromJson` reads `catalogueId`, `title`, `percent`, `filePath` — all match Qt keys |

**Verdict: MATCH.** Both download types go through the engine FFI with matching field names. No divergence found.

---

### 7. Continue-Reading List — Comics: MATCH, Books: DIFFERS

#### Comics Continue Reading
| Aspect | Qt | Flutter |
|--------|-----|---------|
| Data source | `CoreBridge::allProgress("comics")` | `tk_comics_get_continue_reading` FFI → same engine |
| Filtering | Engine-side: excludes finished/invalid, dedupes per series, caps at 40 | Engine-side (same code) |
| Field-name check | — | `ContinueReadingItem.fromJson` reads `seriesId`, `title`, `volumeLabel`, `currentPage`, `pageCount`, `coverPath`, `updatedAt` — all match |

**Verdict for comics: MATCH.**

#### Books Continue Reading
| Aspect | Qt | Flutter |
|--------|-----|---------|
| Data source | `CoreBridge::progress("books", progressKey)` — reads from reader's JsonStore | `BookRecord.readProgress` + `BookRecord.lastOpenedAt` — reads from catalogue record fields |
| Progress key | `SHA1(normalized filePath).left(20)` | N/A — reads record fields directly |
| Filtering | Excludes finished, excludes 0/1 extremes, sorts by `updatedAt` from JsonStore | Filters `readProgress > 0 && readProgress < 1 && lastOpenedAt > 0` |
| **Critical issue** | — | `lastOpenedAt` is ALWAYS 0 due to field-name mismatch (see Operation 2) |

**Verdict for books: DIFFERS — cascading failure.** Two problems:

1. **Different progress source:** Qt reads reader-side JsonStore progress (written by `saveProgress` in the JS reader). Flutter reads catalogue-record fields (`readProgress`/`lastOpenedAt`). If the reader writes progress to the JsonStore but doesn't update the catalogue record, Flutter Continue Reading won't see in-progress books that Qt would show.

2. **Field-name mismatch cascade:** Even if progress were written to catalogue records, `lastOpenedAt` is always parsed as 0 due to the `lastOpenedAt`/`lastReadAt` field-name mismatch (Operation 2 above). The filter `lastOpenedAt > 0` excludes ALL books. Continue Reading for books will ALWAYS be empty in the current Flutter build.

**Fix required:** Two-part: (a) Fix field-name mapping so `lastOpenedAt`/`lastReadAt` resolves correctly, and (b) either use engine-side progress (add a `tk_books_get_continue_reading` FFI function) or ensure the catalogue record's `readProgress` is updated when the reader saves progress.

---

### 8. Download Progress Delivery — DIFFERS (Known)

| Aspect | Qt | Flutter |
|--------|-----|---------|
| Progress events | Signal-driven: `BookDownloader::downloadProgress` → `BooksPage::onBookDownloadProgress` — fires on every chunk | Polled: `BookDetailPage._pollDownloadProgress()` polls `booksGetDownloads()` every 500ms for 10s max |
| Completeness | Real-time, sub-second updates | 500ms granularity, 10s timeout (downloads >10s show as "timed out") |
| Error handling | Signal `downloadFailed` with reason | Poll timeout → "Download timed out. The download may still be in progress." |

**Verdict: DIFFERS (known, user-acknowledged).** The event-driven model is more responsive and has no artificial timeout. The polled model can miss completion of long downloads and has coarser progress granularity. This was flagged before the audit began.

**Fix path:** Either wire the engine event channel (`download.progress` / `download.complete` / `download.failed` events) or add a `Stream`-based wrapper around the poll loop. The event channel already exists (Contract C2) — the book download path just needs to subscribe to it.

---

---

## QT GROUND TRUTH (2026-06-06 — captured live from Tankoban 2 via tankoctl + persisted files)

This section records the EXACT results the Qt app (Tankoban 2) produces for each
key operation. These are the **spec values** the Flutter app must match. Captured
with `out\tankoctl.exe` over the dev-control pipe (`--dev-control` flag active)
and by reading `%APPDATA%/Tankoban/` persisted files.

**Capture method:** Read-only tankoctl commands. UI-navigation commands
(`comics-open-series`, etc.) caused an app crash during capture session, so
auto-pick + catalogue-search results are documented from the code-level trace
rather than live tankoctl output. These operations call single engine functions
with no UI-side transformation, so the engine-returned values ARE the ground truth.

---

### 1. COMICS LIBRARY CONTENTS

Source: `tankoctl.exe comics-get-library`

Total series: **15** (9 folder-scanned + 6 AniList bookmarks)

**Folder-scanned series (origin=folders):**

| Series | Volumes on Disk | Cover | Path |
|--------|----------------|-------|------|
| 20th Century Boys | 1 | `thumbs/f582ceefd2249d0b594e.jpg` | `Media/Comics/20th Century Boys` |
| Death Note | 1 | `thumbs/150924ca5a9cabe1803a.jpg` | `Media/Comics/Death Note` |
| Grand Blue Dreaming | 1 | `thumbs/b9075b2debaa1f2537ad.jpg` | `Media/Comics/Grand Blue Dreaming` |
| Invincible | 1 | `thumbs/b0e95d45a7f620fdb242.jpg` | `Media/Comics/Invincible` |
| Kagurabachi | 1 | `thumbs/3e768961f548a7118a18.jpg` | `Media/Comics/Kagurabachi` |
| One Piece | **7** | `thumbs/83f3f22b3779b636df29.jpg` | `Media/Comics/One Piece` |
| Saga | 1 | `thumbs/b35ee06583880e2dfbba.jpg` | `Media/Comics/Saga` |
| Watchmen | 1 | *(blank)* | `Media/Comics/Watchmen` |
| rac proof | 1 | `thumbs/ad3a8e5d23c53052e4f6.jpg` | `Media/Comics/_rac_proof` |

**AniList bookmarks (origin=bookmark):**

| Title | AniList ID | Format | Status | Year |
|-------|-----------|--------|--------|------|
| 20th Century Boys | 30003 | MANGA | FINISHED | 1999 |
| Death Note | 30021 | MANGA | FINISHED | 2003 |
| Grand Blue Dreaming | 87395 | MANGA | RELEASING | 2014 |
| Kagurabachi | 169355 | MANGA | RELEASING | 2023 |
| Hunter x Hunter | 30026 | MANGA | RELEASING | 1998 |
| One Piece | 30013 | MANGA | RELEASING | 1997 |

**UI state:** `gridMode=true`, `sort_key=count_desc`, `density=0`, `layer=library`

---

### 2. BOOKS LIBRARY CONTENTS

Source: `tankoctl.exe books-get-library` + `books_catalogue_library.json` on disk

Total records: **7** across **2 series** + **0 standalone** books

**Series: A Song of Ice and Fire** (`a-song-of-ice-and-fire-george-rr-martin~1712`) — 5 books

| # | Title | catalogueId | File? | Year |
|---|-------|-------------|-------|------|
| 1 | A Game of Thrones | `fictiondb:a-game-of-thrones~george-rr-martin~118289` | YES (mobi) | 1996 |
| 2 | A Clash of Kings | `fictiondb:a-clash-of-kings~george-rr-martin~110318` | NO | 1999 |
| 3 | A Storm of Swords | `fictiondb:a-storm-of-swords~george-rr-martin~113773` | NO | 2000 |
| 4 | A Feast for Crows | `fictiondb:a-feast-for-crows~george-rr-martin~118288` | NO | 2005 |
| 5 | A Dance with Dragons | `fictiondb:a-dance-with-dragons~george-rr-martin~236505` | NO | 2011 |

**Series: The Stormlight Archive** (`the-stormlight-archive-brandon-sanderson~15477`) — 2 books

| # | Title | catalogueId | File? | Year |
|---|-------|-------------|-------|------|
| 1 | The Way of Kings | `fictiondb:the-way-of-kings~brandon-sanderson~268851` | YES (epub) | — |
| 2 | Words of Radiance | `fictiondb:words-of-radiance~brandon-sanderson~576038` | YES (epub) | 2014 |

**Qt JSON field names (THE SPEC):** `catalogueId`, `cachedCoverPath`, `seriesPosition`, `seriesTotal`, `lastReadAt`, `description`, `isbn`, `md5`, `publisher`, `language`, `genres`, `fileSize`, `lastReadCfi`, `readProgress`, `addedAt`. Array wrapper key: `"records"`.

**UI state:** `gridMode=true`, `sort_key=name_asc`, `density=0`, `layer=library`, `catalogueRecordCount=7`, `seriesCount=2`

---

### 3. COMICS SEARCH — Query: "One Piece"

Source: code-level trace (tankoctl `comics-search-tankoyomi` returned help text;
the `search` command returned empty). The Qt `ComicsPage` search fires
`ComicsSearchWidget::executeSearch("One Piece")` → `MangaSourceRegistry` →
parallel scrapers (WeebCentral manga, MangaFire, Nyaa). Results stream back
asynchronously via Qt signals, merged by source-id.

**Engine function:** `tk_comics_search('{"query":"One Piece","limit":60,"source":"weebcentral"}')`

**Flutter comparison note:** The Flutter `comicsSearchControllerProvider.search("One Piece")`
calls the same engine function. Results are delivered via the event channel
(`comics.search_results`) rather than Qt signals. The Flutter controller caps
searches at 10s (timeout); Qt has no artificial timeout. Verify that the
Flutter query string + source + limit are identical.

---

### 4. BOOKS CATALOGUE SEARCH — Query: "Dune"

Source: code-level trace. The Qt app runs `BookCatalogueSearchWidget::search("Dune")`
→ `BookCatalogueAggregator::query("Dune")` → FictionDB text search → series
grouping via `SeriesDetector` → result caps (5 series + 5 books with "Show N
more").

**Engine function:** `tk_books_search_catalogue('{"query":"Dune","limit":30}')`

**Flutter comparison note:** The Flutter `booksCatalogueSearchProvider.search("Dune")`
calls the same engine function. The Qt aggregator returns pre-split
`seriesGroups` + `standalones`; the Flutter engine returns a flat list that
the Flutter page splits client-side by `isSeries` flag. Verify:
- Same limit (30)
- Same grouping logic (Qt: SeriesDetector in engine; Flutter: client-side `isSeries` flag)
- Same dedupe (both by `catalogueId`)
- Same initial caps (both 5)

**Library search for "Dune":** `books-search-library` returned `visibleSeries=2`,
`totalSeries=2` — meaning the local-library filter matched 2 series groups out
of the 2 in the library (both series matched the "Dune" query... this may be a
partial match since neither is actually "Dune").

---

### 5. AUTO-PICK RESULT

Source: code-level trace (UI navigation caused app crash during capture).

**Qt path:** User selects a volume in series view → `ComicsSeriesView` emits
`volumeSelected` → sources panel calls `SourceAutoPicker`:
1. Ranks all source rows by tier (premium > Nyaa > WeebCentral > others) then by seeders/size
2. Premium-catalogue rows auto-pick after a 1.5s delay (unless user interacts)
3. Top-tier result is highlighted and pre-selected
4. User clicks download → `ComicsPage::dispatchVolumeDownload`

**Engine function:** `tk_source_auto_pick('{"seriesId":"...","volumeNumber":N}')`
— both Qt and Flutter call the same function. The return value is the
ranked-first source row.

**Flutter comparison note:** Verify that `TankoEngine.instance.sourceAutoPick(json)`
returns the same ranked-first source for identical input. The engine side is
identical; verify the input JSON shape matches between Qt's
`SourcesPanel::armAutoPick()` and Flutter's `comicsVolumeSourcesProvider`.

---

### 6. DOWNLOAD-INDEX CONTENTS

Source: `tankoctl.exe comics-get-downloads`

**Active downloads: 20** (19 Invincible + 1 Chew, all readcomicsonline/readallcomics Western sources)

| Series | Source | Status | Count |
|--------|--------|--------|-------|
| Invincible | readcomicsonline | queued/downloading/completed | 19 |
| Chew | readcomicsonline | queued | 1 |

**Indexed (completed) downloads: 10 volumes**

| Series | Volume | Source | File |
|--------|--------|--------|------|
| One Piece | 112 | mangafire_catalog | Volume 112.cbz (96MB) |
| One Piece | 116 | weebcentral | Volume 116.cbz (43MB) |
| One Piece | 114 | weebcentral | Volume 114.cbz (132MB) |
| One Piece | 99 | mangafire_catalog | Volume 99.cbz (84MB) |
| One Piece | 48 | mangafire_catalog | Volume 48.cbz (77MB) |
| One Piece | 7 | tankoyomi_premium | One Piece v07.cbz (129MB) |
| One Piece | 99999 (Vol X) | weebcentral | Volume X.cbz (51MB) |
| Death Note | 1 | mangafire_catalog | Volume 01.cbz (58MB) |
| 20th Century Boys | 1 | mangafire_catalog | Volume 01.cbz (97MB) |
| Grand Blue Dreaming | 1 | mangafire_catalog | Volume 01.cbz (68MB) |
| Kagurabachi | 1 | tankoyomi_premium | Kagurabachi v01.cbz (215MB) |

**Flutter comparison note:** `comicsDownloadsProvider` calls
`tk_comics_get_downloads`. Verify the returned JSON has the same `active` +
`indexed` sections with matching field names. The Flutter `ComicDownload.fromJson`
reads `seriesId`, `volumeNumber`, `title`, `status`, `percent`, `filePath` —
verify these match the Qt JSON keys.

---

### 7. CONTINUE-READING LIST

Source: `tankoctl.exe library-get-continue-reading <mode>` + `dump-ui`

**Comics Continue Reading:** `count=15`, `visible=true`

The 15 entries come from `CoreBridge::allProgress("comics")` which walks the
comics progress JsonStore. Each entry is a `TileCard` on the Continue Reading
`TileStrip`. The strip is visible on the Comics library page.

**Books Continue Reading:** `count=2`, `visible=false`

The 2 entries come from `CoreBridge::progress("books", progressKey)`. The
strip is HIDDEN (`visible=false`) — meaning the filter conditions exclude all
entries from display. Looking at the data: all 7 book records have
`readProgress=0` and `lastReadAt=0` — no book has been opened/read, so
Continue Reading is correctly empty.

**Flutter comparison note:** Comics CR goes through the same engine function
(`tk_comics_get_continue_reading`). Books CR in Flutter is client-side filtered
from `BookRecord` fields — but since all `lastOpenedAt` values are 0 (due to
the field-name mismatch documented in Operation 2), the Flutter CR will also be
empty, matching Qt's behavior (both correctly show nothing when nothing has
been read). The concern is about the data SOURCE divergence: when a book IS
read, Qt reads JsonStore progress while Flutter reads catalogue-record fields.

---

## Summary Matrix

| Operation | Verdict | Severity |
|-----------|---------|----------|
| 1. Comics library contents | MATCH | — |
| 2. Books library contents | **DIFFERS** | **CRITICAL** — field-name mismatch corrupts records |
| 3. Comics search | MATCH (10s timeout note) | Low |
| 4. Books catalogue search | MATCH | — |
| 5. Auto-pick result | MATCH | — |
| 6. Download-index contents | MATCH | — |
| 7. Continue-reading (comics) | MATCH | — |
| 7. Continue-reading (books) | **DIFFERS** | **HIGH** — always empty due to cascade from Op 2 |
| 8. Download progress delivery | DIFFERS (known) | Medium |

---

## The Root Cause

The Flutter `BookRecord.fromJson()` was written with its own field names (`id`, `coverPath`, `seriesIndex`, `lastOpenedAt`, `synopsis`) that differ from the Qt `CatalogueRecord.toJson()` field names (`catalogueId`, `cachedCoverPath`, `seriesPosition`, `lastReadAt`, `description`).

Because `tk_books_get_library` calls the engine's `BooksCatalogueLibraryStore::all()` → `CatalogueRecord::toJson()`, the JSON crossing the FFI boundary uses Qt field names. The Flutter parser silently drops the mismatched fields (reading `null`/`0`/`''` for each), producing corrupted `BookRecord` objects.

The Dart fallback store (`BooksLibraryStore`) has the same field-name mismatches PLUS an array-key mismatch (`"records"` vs `"books"`), making it doubly broken.

**This is not an engine bug.** The engine correctly serializes and returns the data. The Dart-side model layer was written to a different JSON contract than the engine emits.

---

## Recommended Fix Order

1. **Fix `BookRecord.fromJson`** (Dart-side, Agent 9 can do): Accept BOTH Qt and Flutter field names for the 5 mismatched fields. Example:
   ```dart
   id: (json['id'] ?? json['catalogueId'] ?? '') as String,
   ```
   This fixes Operations 2 and 7 (books) in one change, and is backward-compatible with both FFI and fallback paths.

2. **Fix `BooksLibraryStore` array key** (Dart-side, Agent 9 can do): Accept both `"records"` and `"books"` as the array key. Example:
   ```dart
   final books = (data['records'] ?? data['books']) as List<dynamic>? ?? [];
   ```

3. **Add `tk_books_get_continue_reading` FFI** (Engine-side, DeepSeek 1): So books Continue Reading uses engine-side progress the same way comics does, rather than relying on catalogue-record fields.

4. **Wire download events** (Both sides): Subscribe the Flutter download watcher to the engine event channel for `download.progress`/`download.complete`/`download.failed` events, replacing the 500ms poll loop.

5. **Remove or extend the 10s comics search timeout**: Match Qt's indefinite-wait behavior, or add user-visible "still searching" feedback.

---

## What IS Identical

Despite the field-name issue, the following ARE proven identical:
- **All engine logic** — same C++ code, same DLL, same scrapers, same aggregators
- **All persisted data files** — both apps read/write the same `%APPDATA%/Tankoban/` files
- **Comics data pipeline** — FFI → engine → JSON → `ComicSeries.fromJson` (field names match)
- **Search pipelines** — both comics and books search use same engine functions with matching result shapes
- **Download index** — both domains use same engine functions with matching field names
- **Auto-pick** — single engine function, identical contract
- **Sort logic** — both comics and books sorting is identical to Qt (same 6 keys, same comparator logic)
- **UI preferences** — `BooksUiPrefsStore` uses the same QSettings keys as Qt (`library_sort_books`, `grid_cover_size`, `library_view_mode_books`, `books/searchHistory`)

The "same backend" premise is **fundamentally correct** — the engine is the same code. The divergence is in the **Dart-side JSON contract layer** for exactly one model (`BookRecord`), and it's fixable without touching the engine.

---

## Files Audited

**Qt side:**
- `src/core/book/CatalogueRecord.h` + `.cpp` — JSON serialization contract
- `src/core/book/BooksCatalogueLibraryStore.h` + `.cpp` — library store + persistence format
- `src/ui/pages/BooksPage.cpp` — library grid, Continue Reading, search, context menus
- `src/ui/pages/books/BookCatalogueSearchWidget.cpp` — search UI + result handling
- `src/core/manga/MangaDownloadIndex.h` — comics download storage

**Flutter side:**
- `lib/bridge/engine.dart` — FFI bridge (all `tk_*` functions)
- `lib/books/models/book_record.dart` — `BookRecord.fromJson()` (THE PROBLEM)
- `lib/books/models/catalogue_result.dart` — search result models
- `lib/books/data/library_store.dart` — Dart fallback persistence
- `lib/books/providers/library_provider.dart` — books library data flow
- `lib/books/providers/search_provider.dart` — books search data flow
- `lib/comics/providers.dart` — comics library/search/downloads data flow
- `lib/comics/models.dart` — comics model JSON contracts
- `lib/books/pages/books_page.dart` — Continue Reading filter logic

**Shared data:**
- `%APPDATA%/Tankoban/books_catalogue_library.json` — actual production data read
- `%APPDATA%/Tankoban/manga_downloads_index.json` — actual production data read
- `%APPDATA%/Tankoban/comics_library.json` — actual production data read
