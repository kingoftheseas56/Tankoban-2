# BOOKS_STREMIO_PIVOT v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild Books mode as a Stremio-style catalogue → source → reader pipeline, mirroring Comics mode + Stream mode, with TankoLibrary as the source layer and Open Library + Google Books as the metadata catalogue.

**Architecture:** Three concentric layers + the existing untouched reader. Catalogue layer (new — Open Library + Google Books HTTP clients + aggregator). Source layer (mostly inherited from Agent 4B's TankoLibrary work — LibGen + Anna's Archive + Tankorent, fanned out in parallel on every [Search for downloads]). Library layer (rewrite — catalogue-record store replaces folder-scan as the source of library truth; `BookSeriesView` deleted; `BooksScanner` simplifies to file-existence validation). UI layer (forked from Stream blueprint: search-takeover view + movie-shape detail page + series-shape detail page + parallel-fan-out picker). The reader (`BookReader` + `BookBridge`) is untouched.

**Tech Stack:** C++20, Qt 6.10.2, MSVC2022. Qt6Core + Qt6Network + Qt6Widgets. GoogleTest for pure-logic primitives (FetchContent). JsonStore (existing) for persistence. CMake + Ninja build system.

**Reference docs:**
- Spec: `docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md`
- Mockup: `docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html`
- Stream blueprint: `src/ui/pages/stream/StreamSearchWidget.{h,cpp}`, `src/ui/pages/stream/StreamDetailView.{h,cpp}`, `src/core/stream/StreamDownloadIndex.{h,cpp}`
- Inherited TankoLibrary surface (Agent 4B → Agent 2 transfer 2026-05-20): `src/core/book/*`, `src/ui/pages/TankoLibraryPage.{h,cpp}`

**Cross-agent coordination:** Phase 4 (Tankorent integration) requires HELP-request to Agent 4 (Tankorent owner post-4B-departure) for book-category query filter wiring + magnet→Books-library-path shim. Phase 5+6+7 do not need cross-agent coordination.

---

## File Structure

### NEW files

| File | Responsibility |
|---|---|
| `src/core/book/BookCatalogueResult.h` | POD for a single catalogue search result (cover, title, author, ISBN, series, etc.). Parallels `BookResult` but for catalogue side (not source side). |
| `src/core/book/CatalogueRecord.h` | POD for the v1 library entity. Wraps one downloaded file with catalogue metadata + read progress + (for series) series-position. |
| `src/core/book/BooksCatalogueLibraryStore.h/.cpp` | JSON-backed thread-safe store. `catalogueId → CatalogueRecord` + `seriesId → list<catalogueId>` derived map + `filePath → catalogueId` reverse-lookup. `validateAll()` mirrors `StreamDownloadIndex` pattern. |
| `src/core/book/OpenLibraryClient.h/.cpp` | HTTP client + JSON parser for Open Library search + work + author endpoints. |
| `src/core/book/GoogleBooksClient.h/.cpp` | HTTP client + JSON parser for Google Books volumes endpoint (API key required; fallback when Open Library misses). |
| `src/core/book/BookCatalogueAggregator.h/.cpp` | Catalogue fan-out — queries both clients in parallel, merges + dedupes by ISBN + title-author fuzzy match, decides series-shape vs movie-shape. |
| `src/core/book/BookSearchAggregator.h/.cpp` | Source-side picker engine. Given a `BookCatalogueResult`, fans out parallel queries to LibGen + AA + Tankorent. Each source streams independently. |
| `src/ui/pages/books/BooksTankoLibrarySearchWidget.h/.cpp` | Forked from `StreamSearchWidget`. Two-section layout (Series first, then Books), `kInitialCap = 6`, "Show N more" overflow reveal. |
| `src/ui/pages/books/BooksTankoLibraryDetailView.h/.cpp` | Movie-shape detail page. Forked subset of `StreamDetailView`. Hero + meta + [Search for downloads] + synopsis + tags + author scroller. |
| `src/ui/pages/books/BooksTankoLibrarySeriesDetailView.h/.cpp` | Series-shape detail page. Full fork of `StreamDetailView`. Hero + bulk button + per-book table with 5 row states + context menu + author scroller. |
| `src/ui/pages/books/BookSourcePicker.h/.cpp` | Parallel fan-out picker. Three vertical source sections, per-source spinner, results stream in independently. Each row carries quality signals. |
| `tests/core/book/test_catalogue_record.cpp` | GoogleTest for CatalogueRecord ser/des + round-trip. |
| `tests/core/book/test_books_catalogue_library_store.cpp` | GoogleTest for store register / evict / validateAll / by-series aggregation. |
| `tests/core/book/test_open_library_client_parser.cpp` | GoogleTest for OpenLibraryClient JSON parsing against frozen fixtures. |
| `tests/core/book/test_google_books_client_parser.cpp` | GoogleTest for GoogleBooksClient JSON parsing. |
| `tests/core/book/test_book_catalogue_aggregator.cpp` | GoogleTest for aggregator dedup + series-shape detection. |
| `tests/fixtures/book_catalogue/openlib_search_stormlight.json` | Frozen Open Library response for "stormlight archive" query. |
| `tests/fixtures/book_catalogue/openlib_search_project_hail_mary.json` | Frozen Open Library response for "project hail mary" query. |
| `tests/fixtures/book_catalogue/googlebooks_search_stormlight.json` | Frozen Google Books response. |

### MODIFIED files

| File | Changes |
|---|---|
| `src/ui/pages/BooksPage.h/.cpp` | Rewire: search bar fires catalogue takeover; library grid driven by `BooksCatalogueLibraryStore`; Continue strip becomes series-aware + auto-resume; `m_seriesView` field + routing deleted; empty-state copy. |
| `src/core/BooksScanner.h/.cpp` | Simplify: drop folder-walk discovery; new job is `validateAll()` against `BooksCatalogueLibraryStore` records (file-existence check). |
| `src/ui/MainWindow.h/.cpp` | Replace `BookSeriesView`-targeted routing with movie-shape vs series-shape detail-view routing decided by catalogue record's `seriesId`. |
| `src/ui/readers/BookBridge.h/.cpp` | Books-bridge v1.3 `devSnapshot()` — strip the `BookSeriesView.devSnapshot()` reference (BookSeriesView is being deleted). |
| `src/core/book/AnnaArchiveScraper.h/.cpp` | Re-enable AA from disabled-at-construction state; integrate captcha-solving sub-task. |
| `src/core/book/BookDownloader.h/.cpp` | Add magnet-source variant (or thin shim against `TorrentClient::addTorrent` → extraction → Books library path). |
| `src/ui/pages/TankoLibraryPage.h/.cpp` | Audiobooks-tab stays compiled; Books-mode catalogue layer does not surface ABB results (out of v1 scope per spec §3.1). No changes to TankoLibraryPage UI itself. |
| `CMakeLists.txt` | Register all new files in `SOURCES` + `HEADERS` lists; add test binaries to `tankoban_tests` target. |

### DELETED files

| File | Reason |
|---|---|
| `src/ui/pages/BookSeriesView.h` | Folder-tree archive view obsoleted by burn-the-ships call (spec §3.8). |
| `src/ui/pages/BookSeriesView.cpp` | Same as above. |

---

## Phase 1 — Data model foundation

Pure-logic, TDD-friendly. Three new POD-or-near-POD types + the catalogue-records store. All MSVC-compiled, GoogleTest-tested. No Qt UI dependencies in this phase (BooksCatalogueLibraryStore uses QObject + QMutex + JsonStore but no widgets).

This phase must be GREEN before any UI work begins. The store is the new source of library truth — everything downstream reads from it.

### Task 1.1: BookCatalogueResult POD

**Files:**
- Create: `src/core/book/BookCatalogueResult.h`
- Modify: `CMakeLists.txt` — add to `HEADERS` list (line ~248 per project convention)

- [ ] **Step 1: Write the failing test**

Create `tests/core/book/test_book_catalogue_result.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/book/BookCatalogueResult.h"

TEST(BookCatalogueResultTest, DefaultConstructionLeavesEmptyFields) {
    BookCatalogueResult r;
    EXPECT_TRUE(r.catalogueId.isEmpty());
    EXPECT_TRUE(r.title.isEmpty());
    EXPECT_TRUE(r.author.isEmpty());
    EXPECT_TRUE(r.isSeries == false);
    EXPECT_EQ(r.seriesPosition, 0);
}

TEST(BookCatalogueResultTest, SeriesShapeFieldsPopulate) {
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("openlib:OL14868682W");
    r.title = QStringLiteral("Stormlight Archive");
    r.author = QStringLiteral("Brandon Sanderson");
    r.isSeries = true;
    r.seriesName = QStringLiteral("Stormlight Archive");
    r.seriesTotal = 5;
    r.genres = QStringList{QStringLiteral("epic fantasy"), QStringLiteral("cosmere")};
    EXPECT_TRUE(r.isSeries);
    EXPECT_EQ(r.seriesTotal, 5);
    EXPECT_EQ(r.genres.size(), 2);
}

TEST(BookCatalogueResultTest, MoviesShapeFieldsPopulate) {
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("openlib:OL27448W");
    r.title = QStringLiteral("Project Hail Mary");
    r.author = QStringLiteral("Andy Weir");
    r.isbn = QStringLiteral("9780593135204");
    r.year = QStringLiteral("2021");
    r.publisher = QStringLiteral("Ballantine");
    r.pages = QStringLiteral("480");
    r.language = QStringLiteral("English");
    r.isSeries = false;
    EXPECT_FALSE(r.isSeries);
    EXPECT_EQ(r.year, QStringLiteral("2021"));
}
```

- [ ] **Step 2: Run test to verify it fails (header doesn't exist yet)**

Run:
```
cmake --build out --target tankoban_tests
```
Expected: FAIL with "cannot open source file 'core/book/BookCatalogueResult.h'".

- [ ] **Step 3: Write the POD header**

Create `src/core/book/BookCatalogueResult.h`:

```cpp
#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>

// Catalogue-side search result for Books mode (Open Library + Google Books).
// Parallel to BookResult under src/core/book/ but represents the metadata
// catalogue layer, not the source layer. A BookCatalogueResult flows from
// the catalogue aggregator into the search-takeover view; the user clicks
// it to land on a detail page; the detail page's [Search for downloads]
// fans out to the source layer (which returns BookResult rows in the picker).
//
// Series-shape (isSeries=true) -> opens BooksTankoLibrarySeriesDetailView
//   with the book list table populated by sibling catalogue results.
// Movie-shape (isSeries=false) -> opens BooksTankoLibraryDetailView
//   with a single [Search for downloads] action.
struct BookCatalogueResult {
    // Identity
    QString catalogueId;        // "openlib:OL27448W" | "googlebooks:abc123"
    QString isbn;               // when known (multi-ISBN joined with ',')
    QString workId;             // Open Library work key (OL...W), groups editions

    // Display
    QString title;
    QString author;             // multi-author joined with " & "
    QString publisher;
    QString year;
    QString language;
    QString description;        // synopsis (may be HTML in Google Books; plain in OL)
    QStringList genres;         // Open Library subjects / Google Books categories
    QString coverUrl;           // absolute URL; remote, lazy-fetched

    // Series shape
    bool    isSeries = false;
    QString seriesId;           // catalogueId of the SERIES record (== self.catalogueId
                                //   for a series tile, or the parent series for a book)
    QString seriesName;
    int     seriesPosition = 0; // 1-indexed; 0 if standalone or unknown
    int     seriesTotal = 0;    // when known; 0 if unknown

    // Physical (when known from catalogue side)
    QString pages;              // string — sometimes "pp." suffix in source data
};
Q_DECLARE_METATYPE(BookCatalogueResult)
Q_DECLARE_METATYPE(QList<BookCatalogueResult>)
```

- [ ] **Step 4: Add new files to CMakeLists.txt + tests**

Edit `CMakeLists.txt`:
- Add `src/core/book/BookCatalogueResult.h` to the `HEADERS` list (around line 248, in alphabetical order under `src/core/book/`).
- Add `tests/core/book/test_book_catalogue_result.cpp` to the `tankoban_tests` source list (search for `test_legacy_importer` in CMakeLists.txt — same block).

- [ ] **Step 5: Run test to verify it passes**

Run:
```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R BookCatalogueResultTest
```
Expected: 3 tests pass.

- [ ] **Step 6: Commit**

```
git add src/core/book/BookCatalogueResult.h tests/core/book/test_book_catalogue_result.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P1.1: BookCatalogueResult POD + 3 tests"
```

---

### Task 1.2: CatalogueRecord POD (the v1 library entity)

**Files:**
- Create: `src/core/book/CatalogueRecord.h`
- Create: `tests/core/book/test_catalogue_record.cpp`
- Modify: `CMakeLists.txt` — add to HEADERS + tests

- [ ] **Step 1: Write the failing test**

Create `tests/core/book/test_catalogue_record.cpp`:

```cpp
#include <gtest/gtest.h>
#include <QJsonObject>
#include <QJsonDocument>
#include "core/book/CatalogueRecord.h"

TEST(CatalogueRecordTest, DefaultsAreSafe) {
    CatalogueRecord r;
    EXPECT_TRUE(r.catalogueId.isEmpty());
    EXPECT_TRUE(r.filePath.isEmpty());
    EXPECT_DOUBLE_EQ(r.readProgress, 0.0);
    EXPECT_EQ(r.seriesPosition, 0);
}

TEST(CatalogueRecordTest, RoundTripsThroughJson) {
    CatalogueRecord r;
    r.catalogueId = QStringLiteral("openlib:OL27448W");
    r.isbn = QStringLiteral("9780593135204");
    r.md5 = QStringLiteral("aabbccdd11223344");
    r.title = QStringLiteral("Project Hail Mary");
    r.author = QStringLiteral("Andy Weir");
    r.publisher = QStringLiteral("Ballantine");
    r.year = QStringLiteral("2021");
    r.language = QStringLiteral("English");
    r.description = QStringLiteral("Ryland Grace is the sole survivor on a desperate, last-chance mission.");
    r.genres = QStringList{QStringLiteral("hard sci-fi"), QStringLiteral("first contact")};
    r.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/12345-L.jpg");
    r.filePath = QStringLiteral("Project Hail Mary.epub");
    r.format = QStringLiteral("epub");
    r.fileSize = QStringLiteral("4.2 MB");
    r.addedAt = 1716100000;
    r.readProgress = 0.42;
    r.lastReadAt = 1716200000;
    r.lastReadCfi = QStringLiteral("epubcfi(/6/8!/4/2/12)");

    QJsonObject json = r.toJson();
    CatalogueRecord back = CatalogueRecord::fromJson(json);

    EXPECT_EQ(back.catalogueId, r.catalogueId);
    EXPECT_EQ(back.isbn, r.isbn);
    EXPECT_EQ(back.md5, r.md5);
    EXPECT_EQ(back.title, r.title);
    EXPECT_EQ(back.author, r.author);
    EXPECT_EQ(back.description, r.description);
    EXPECT_EQ(back.genres, r.genres);
    EXPECT_EQ(back.filePath, r.filePath);
    EXPECT_EQ(back.format, r.format);
    EXPECT_EQ(back.addedAt, r.addedAt);
    EXPECT_DOUBLE_EQ(back.readProgress, r.readProgress);
    EXPECT_EQ(back.lastReadCfi, r.lastReadCfi);
}

TEST(CatalogueRecordTest, SeriesFieldsRoundTrip) {
    CatalogueRecord r;
    r.catalogueId = QStringLiteral("openlib:OL27448W:3");
    r.seriesId = QStringLiteral("openlib:OL14868682W");
    r.seriesName = QStringLiteral("Stormlight Archive");
    r.seriesPosition = 3;
    r.seriesTotal = 5;

    QJsonObject json = r.toJson();
    CatalogueRecord back = CatalogueRecord::fromJson(json);

    EXPECT_EQ(back.seriesId, r.seriesId);
    EXPECT_EQ(back.seriesName, r.seriesName);
    EXPECT_EQ(back.seriesPosition, 3);
    EXPECT_EQ(back.seriesTotal, 5);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```
cmake --build out --target tankoban_tests
```
Expected: FAIL with "cannot open source file 'core/book/CatalogueRecord.h'".

- [ ] **Step 3: Write the header + implementation**

Create `src/core/book/CatalogueRecord.h`:

```cpp
#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaType>

// The v1 library entity for Books mode after BOOKS_STREMIO_PIVOT.
// Replaces the folder-scan-driven model (where on-disk files defined the
// library) with a catalogue-record-driven model: a book is in the library
// iff a CatalogueRecord wraps it. See spec §3.8 (burn the ships).
//
// A series of N books = N CatalogueRecords sharing the same seriesId.
// The library grid renders the series tile from the most-progressed or
// most-recently-touched record in the group.
struct CatalogueRecord {
    // Identity
    QString catalogueId;        // "openlib:OL27448W" | "googlebooks:abc123" | for-series-books: "openlib:OL27448W:3" (work + position)
    QString isbn;               // when known (multi-ISBN joined with ',')
    QString md5;                // BookResult.md5 of the downloaded file; cross-source dedup

    // Display metadata (from catalogue layer)
    QString title;
    QString author;
    QString publisher;
    QString year;
    QString language;
    QString description;        // synopsis
    QStringList genres;         // Open Library subjects
    QString coverUrl;           // remote (lazy-fetched into cache)
    QString cachedCoverPath;    // local cached cover for offline render

    // Series (empty seriesId means movie-shape standalone)
    QString seriesId;
    QString seriesName;
    int     seriesPosition = 0; // 1-indexed; 0 if standalone or unknown
    int     seriesTotal = 0;

    // File (from source layer + downloader)
    QString filePath;           // canonical relative path under Books root
    QString format;             // "epub" | "pdf" | "mobi" | "azw3" | "djvu"
    QString fileSize;           // human-readable display

    // State (from app runtime)
    qint64  addedAt = 0;        // epoch seconds; 0 == not added
    double  readProgress = 0.0; // 0.0..1.0
    qint64  lastReadAt = 0;     // epoch seconds; 0 == never opened
    QString lastReadCfi;        // EPUB CFI position for resume

    QJsonObject toJson() const;
    static CatalogueRecord fromJson(const QJsonObject& obj);
};
Q_DECLARE_METATYPE(CatalogueRecord)
```

Create `src/core/book/CatalogueRecord.cpp`:

```cpp
#include "CatalogueRecord.h"

QJsonObject CatalogueRecord::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("catalogueId")]   = catalogueId;
    o[QStringLiteral("isbn")]          = isbn;
    o[QStringLiteral("md5")]           = md5;
    o[QStringLiteral("title")]         = title;
    o[QStringLiteral("author")]        = author;
    o[QStringLiteral("publisher")]     = publisher;
    o[QStringLiteral("year")]          = year;
    o[QStringLiteral("language")]      = language;
    o[QStringLiteral("description")]   = description;
    QJsonArray g; for (const auto& s : genres) g.append(s);
    o[QStringLiteral("genres")]        = g;
    o[QStringLiteral("coverUrl")]      = coverUrl;
    o[QStringLiteral("cachedCoverPath")] = cachedCoverPath;
    if (!seriesId.isEmpty()) {
        o[QStringLiteral("seriesId")]       = seriesId;
        o[QStringLiteral("seriesName")]     = seriesName;
        o[QStringLiteral("seriesPosition")] = seriesPosition;
        o[QStringLiteral("seriesTotal")]    = seriesTotal;
    }
    o[QStringLiteral("filePath")]      = filePath;
    o[QStringLiteral("format")]        = format;
    o[QStringLiteral("fileSize")]      = fileSize;
    o[QStringLiteral("addedAt")]       = addedAt;
    o[QStringLiteral("readProgress")]  = readProgress;
    o[QStringLiteral("lastReadAt")]    = lastReadAt;
    o[QStringLiteral("lastReadCfi")]   = lastReadCfi;
    return o;
}

CatalogueRecord CatalogueRecord::fromJson(const QJsonObject& o)
{
    CatalogueRecord r;
    r.catalogueId     = o.value(QStringLiteral("catalogueId")).toString();
    r.isbn            = o.value(QStringLiteral("isbn")).toString();
    r.md5             = o.value(QStringLiteral("md5")).toString();
    r.title           = o.value(QStringLiteral("title")).toString();
    r.author          = o.value(QStringLiteral("author")).toString();
    r.publisher       = o.value(QStringLiteral("publisher")).toString();
    r.year            = o.value(QStringLiteral("year")).toString();
    r.language        = o.value(QStringLiteral("language")).toString();
    r.description     = o.value(QStringLiteral("description")).toString();
    QJsonArray g      = o.value(QStringLiteral("genres")).toArray();
    for (const auto& v : g) r.genres << v.toString();
    r.coverUrl        = o.value(QStringLiteral("coverUrl")).toString();
    r.cachedCoverPath = o.value(QStringLiteral("cachedCoverPath")).toString();
    r.seriesId        = o.value(QStringLiteral("seriesId")).toString();
    r.seriesName      = o.value(QStringLiteral("seriesName")).toString();
    r.seriesPosition  = o.value(QStringLiteral("seriesPosition")).toInt(0);
    r.seriesTotal     = o.value(QStringLiteral("seriesTotal")).toInt(0);
    r.filePath        = o.value(QStringLiteral("filePath")).toString();
    r.format          = o.value(QStringLiteral("format")).toString();
    r.fileSize        = o.value(QStringLiteral("fileSize")).toString();
    r.addedAt         = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble(0));
    r.readProgress    = o.value(QStringLiteral("readProgress")).toDouble(0.0);
    r.lastReadAt      = static_cast<qint64>(o.value(QStringLiteral("lastReadAt")).toDouble(0));
    r.lastReadCfi     = o.value(QStringLiteral("lastReadCfi")).toString();
    return r;
}
```

- [ ] **Step 4: Add to CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/core/book/CatalogueRecord.h` to `HEADERS`.
- Add `src/core/book/CatalogueRecord.cpp` to `SOURCES`.
- Add `tests/core/book/test_catalogue_record.cpp` to `tankoban_tests` sources.

- [ ] **Step 5: Run test to verify it passes**

Run:
```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R CatalogueRecordTest
```
Expected: 3 tests pass.

- [ ] **Step 6: Commit**

```
git add src/core/book/CatalogueRecord.h src/core/book/CatalogueRecord.cpp tests/core/book/test_catalogue_record.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P1.2: CatalogueRecord POD + JSON round-trip + 3 tests"
```

---

### Task 1.3: BooksCatalogueLibraryStore — class skeleton + load/save

**Files:**
- Create: `src/core/book/BooksCatalogueLibraryStore.h`
- Create: `src/core/book/BooksCatalogueLibraryStore.cpp`
- Create: `tests/core/book/test_books_catalogue_library_store.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/core/book/test_books_catalogue_library_store.cpp`:

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"

namespace {
CatalogueRecord makeMovieRecord(const QString& id, const QString& title) {
    CatalogueRecord r;
    r.catalogueId = id;
    r.title = title;
    r.author = QStringLiteral("Test Author");
    r.filePath = title + QStringLiteral(".epub");
    r.format = QStringLiteral("epub");
    r.addedAt = 1716100000;
    return r;
}

CatalogueRecord makeSeriesBookRecord(const QString& id, const QString& seriesId,
                                     const QString& seriesName,
                                     int pos, const QString& title) {
    CatalogueRecord r;
    r.catalogueId = id;
    r.title = title;
    r.author = QStringLiteral("Test Author");
    r.seriesId = seriesId;
    r.seriesName = seriesName;
    r.seriesPosition = pos;
    r.seriesTotal = 5;
    r.filePath = title + QStringLiteral(".epub");
    r.format = QStringLiteral("epub");
    r.addedAt = 1716100000 + pos;
    return r;
}
} // namespace

TEST(BooksCatalogueLibraryStoreTest, EmptyStoreReportsEmpty) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    EXPECT_EQ(store.all().size(), 0);
    EXPECT_FALSE(store.hasRecord(QStringLiteral("openlib:nonexistent")));
}

TEST(BooksCatalogueLibraryStoreTest, RegisterAndLookupRoundTrips) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    auto r = makeMovieRecord(QStringLiteral("openlib:OL27448W"),
                             QStringLiteral("Project Hail Mary"));
    store.upsertRecord(r);
    EXPECT_TRUE(store.hasRecord(QStringLiteral("openlib:OL27448W")));
    auto opt = store.recordFor(QStringLiteral("openlib:OL27448W"));
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->title, QStringLiteral("Project Hail Mary"));
}

TEST(BooksCatalogueLibraryStoreTest, EvictRemovesRecord) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    store.upsertRecord(makeMovieRecord(QStringLiteral("a"), QStringLiteral("A")));
    store.upsertRecord(makeMovieRecord(QStringLiteral("b"), QStringLiteral("B")));
    EXPECT_EQ(store.all().size(), 2);
    store.evictByCatalogueId(QStringLiteral("a"));
    EXPECT_EQ(store.all().size(), 1);
    EXPECT_FALSE(store.hasRecord(QStringLiteral("a")));
    EXPECT_TRUE(store.hasRecord(QStringLiteral("b")));
}

TEST(BooksCatalogueLibraryStoreTest, BySeriesAggregation) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    const QString sid = QStringLiteral("openlib:OL14868682W");
    store.upsertRecord(makeSeriesBookRecord(QStringLiteral("k1"), sid,
                                            QStringLiteral("Stormlight Archive"), 1,
                                            QStringLiteral("The Way of Kings")));
    store.upsertRecord(makeSeriesBookRecord(QStringLiteral("k2"), sid,
                                            QStringLiteral("Stormlight Archive"), 2,
                                            QStringLiteral("Words of Radiance")));
    store.upsertRecord(makeMovieRecord(QStringLiteral("phm"),
                                       QStringLiteral("Project Hail Mary")));
    auto ids = store.catalogueIdsForSeries(sid);
    EXPECT_EQ(ids.size(), 2);
    EXPECT_TRUE(ids.contains(QStringLiteral("k1")));
    EXPECT_TRUE(ids.contains(QStringLiteral("k2")));
    auto allSeries = store.allSeriesIds();
    EXPECT_EQ(allSeries.size(), 1);
    EXPECT_TRUE(allSeries.contains(sid));
}

TEST(BooksCatalogueLibraryStoreTest, FilePathReverseLookup) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    auto r = makeMovieRecord(QStringLiteral("openlib:OL27448W"),
                             QStringLiteral("Project Hail Mary"));
    store.upsertRecord(r);
    auto opt = store.catalogueIdForFile(QStringLiteral("Project Hail Mary.epub"));
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, QStringLiteral("openlib:OL27448W"));
    EXPECT_FALSE(store.catalogueIdForFile(QStringLiteral("nonexistent.epub")).has_value());
}

TEST(BooksCatalogueLibraryStoreTest, PersistAndReload) {
    QTemporaryDir tmp;
    {
        BooksCatalogueLibraryStore store(tmp.path());
        store.upsertRecord(makeMovieRecord(QStringLiteral("phm"),
                                           QStringLiteral("Project Hail Mary")));
        store.save();
    }
    {
        BooksCatalogueLibraryStore store(tmp.path());
        store.load();
        EXPECT_EQ(store.all().size(), 1);
        EXPECT_TRUE(store.hasRecord(QStringLiteral("phm")));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run:
```
cmake --build out --target tankoban_tests
```
Expected: FAIL with "cannot open source file 'core/book/BooksCatalogueLibraryStore.h'".

- [ ] **Step 3: Write the header**

Create `src/core/book/BooksCatalogueLibraryStore.h`:

```cpp
#pragma once

#include <QHash>
#include <QSet>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <optional>

#include "CatalogueRecord.h"

// BOOKS_STREMIO_PIVOT 2026-05-20 — persistent catalogue-records library for
// Books mode after the burn-the-ships migration (spec §3.8).
//
// Replaces the folder-scan-driven model: a book is in the library iff a
// CatalogueRecord wraps it. Files on disk without a matching record are
// ignored (we never delete user data, but we don't surface them either).
//
// Owns three in-memory lookup maps derived from a single sibling JSON file
// (<dataDir>/books_catalogue_library.json), patterned after StreamDownloadIndex
// (src/core/stream/StreamDownloadIndex.h:21):
//   - m_byId       : catalogueId -> CatalogueRecord (primary)
//   - m_bySeries   : seriesId -> set<catalogueId> (series aggregation for grid)
//   - m_byFilePath : filePath -> catalogueId (reverse lookup for validate)
//
// Threadsafe — BooksScanner reads from a worker thread via mutex-guarded
// const APIs. Mutating methods (upsertRecord / evict / validateAll /
// updateReadProgress) execute synchronously on the calling thread, acquire
// m_mutex around map mutations, then call save() and emit recordsChanged()
// OFF the lock.
class BooksCatalogueLibraryStore : public QObject
{
    Q_OBJECT

public:
    // dataDir is the folder under which books_catalogue_library.json lives.
    // Production callsite passes CoreBridge::dataDir(); tests pass a QTemporaryDir.
    explicit BooksCatalogueLibraryStore(const QString& dataDir, QObject* parent = nullptr);

    // ── Mutate ────────────────────────────────────────────────────────────
    // Upsert (insert or replace by catalogueId). Updates all three derived maps
    // and persists. Emits recordsChanged() after save returns.
    void upsertRecord(const CatalogueRecord& r);

    // Drop a record by catalogueId. File on disk is NOT deleted. If the
    // catalogueId was the last entry for its seriesId, the series goes off
    // the seriesId map. Emits recordsChanged().
    void evictByCatalogueId(const QString& catalogueId);

    // Drop all records whose filePath does not exist on disk anymore.
    // Mirror of StreamDownloadIndex::validateAll. Called on BooksPage::showEvent.
    void validateAll();

    // Update per-record read state. Persists. Emits recordReadStateChanged.
    void updateReadProgress(const QString& catalogueId,
                            double readProgress,
                            qint64 lastReadAt,
                            const QString& lastReadCfi);

    // ── Read (const, mutex-guarded) ───────────────────────────────────────
    bool hasRecord(const QString& catalogueId) const;
    std::optional<CatalogueRecord> recordFor(const QString& catalogueId) const;
    std::optional<QString> catalogueIdForFile(const QString& filePath) const;
    QList<CatalogueRecord> all() const;

    // Series-aware reads
    QList<QString> catalogueIdsForSeries(const QString& seriesId) const;
    QSet<QString> allSeriesIds() const;

    // ── Persistence (explicit for testability) ────────────────────────────
    void load();
    void save();

    static constexpr const char* FILENAME = "books_catalogue_library.json";
    static constexpr int kSchemaVersion = 1;

signals:
    void recordsChanged();
    // Granular: fires when a single record's read progress or lastReadAt updates.
    void recordReadStateChanged(const QString& catalogueId);

private:
    void rebuildDerivedMapsLocked();

    QString m_dataDir;
    mutable QMutex m_mutex;
    QHash<QString, CatalogueRecord> m_byId;
    QHash<QString, QSet<QString>>   m_bySeries;       // seriesId -> {catalogueId}
    QHash<QString, QString>         m_byFilePath;     // filePath -> catalogueId
};
```

- [ ] **Step 4: Write the implementation**

Create `src/core/book/BooksCatalogueLibraryStore.cpp`:

```cpp
#include "BooksCatalogueLibraryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>

BooksCatalogueLibraryStore::BooksCatalogueLibraryStore(const QString& dataDir,
                                                       QObject* parent)
    : QObject(parent), m_dataDir(dataDir)
{
    // Lazy load so tests can construct before calling load().
}

// ── Mutate ──────────────────────────────────────────────────────────────────

void BooksCatalogueLibraryStore::upsertRecord(const CatalogueRecord& r)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        // Replace existing record's derived-map entries before re-inserting.
        auto existing = m_byId.find(r.catalogueId);
        if (existing != m_byId.end()) {
            const auto& old = existing.value();
            if (!old.seriesId.isEmpty()) {
                auto sit = m_bySeries.find(old.seriesId);
                if (sit != m_bySeries.end()) {
                    sit.value().remove(old.catalogueId);
                    if (sit.value().isEmpty()) m_bySeries.erase(sit);
                }
            }
            if (!old.filePath.isEmpty()) m_byFilePath.remove(old.filePath);
        }
        m_byId.insert(r.catalogueId, r);
        if (!r.seriesId.isEmpty()) m_bySeries[r.seriesId].insert(r.catalogueId);
        if (!r.filePath.isEmpty()) m_byFilePath.insert(r.filePath, r.catalogueId);
        changed = true;
    }
    if (changed) {
        save();
        emit recordsChanged();
    }
}

void BooksCatalogueLibraryStore::evictByCatalogueId(const QString& catalogueId)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        auto it = m_byId.find(catalogueId);
        if (it == m_byId.end()) return;
        const auto rec = it.value();
        m_byId.erase(it);
        if (!rec.seriesId.isEmpty()) {
            auto sit = m_bySeries.find(rec.seriesId);
            if (sit != m_bySeries.end()) {
                sit.value().remove(catalogueId);
                if (sit.value().isEmpty()) m_bySeries.erase(sit);
            }
        }
        if (!rec.filePath.isEmpty()) m_byFilePath.remove(rec.filePath);
        changed = true;
    }
    if (changed) {
        save();
        emit recordsChanged();
    }
}

void BooksCatalogueLibraryStore::validateAll()
{
    QList<QString> toEvict;
    {
        QMutexLocker lk(&m_mutex);
        for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
            const auto& rec = it.value();
            if (rec.filePath.isEmpty()) continue;
            const QString abs = QDir(m_dataDir).absoluteFilePath(rec.filePath);
            if (!QFileInfo::exists(abs) &&
                !QFileInfo::exists(rec.filePath)) {
                toEvict.append(it.key());
            }
        }
    }
    for (const auto& id : toEvict) evictByCatalogueId(id);
}

void BooksCatalogueLibraryStore::updateReadProgress(const QString& catalogueId,
                                                    double readProgress,
                                                    qint64 lastReadAt,
                                                    const QString& lastReadCfi)
{
    {
        QMutexLocker lk(&m_mutex);
        auto it = m_byId.find(catalogueId);
        if (it == m_byId.end()) return;
        it.value().readProgress = readProgress;
        it.value().lastReadAt = lastReadAt;
        it.value().lastReadCfi = lastReadCfi;
    }
    save();
    emit recordReadStateChanged(catalogueId);
}

// ── Read ────────────────────────────────────────────────────────────────────

bool BooksCatalogueLibraryStore::hasRecord(const QString& catalogueId) const
{
    QMutexLocker lk(&m_mutex);
    return m_byId.contains(catalogueId);
}

std::optional<CatalogueRecord>
BooksCatalogueLibraryStore::recordFor(const QString& catalogueId) const
{
    QMutexLocker lk(&m_mutex);
    auto it = m_byId.constFind(catalogueId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

std::optional<QString>
BooksCatalogueLibraryStore::catalogueIdForFile(const QString& filePath) const
{
    QMutexLocker lk(&m_mutex);
    auto it = m_byFilePath.constFind(filePath);
    if (it == m_byFilePath.constEnd()) return std::nullopt;
    return it.value();
}

QList<CatalogueRecord> BooksCatalogueLibraryStore::all() const
{
    QMutexLocker lk(&m_mutex);
    return m_byId.values();
}

QList<QString>
BooksCatalogueLibraryStore::catalogueIdsForSeries(const QString& seriesId) const
{
    QMutexLocker lk(&m_mutex);
    auto it = m_bySeries.constFind(seriesId);
    if (it == m_bySeries.constEnd()) return {};
    return it.value().values();
}

QSet<QString> BooksCatalogueLibraryStore::allSeriesIds() const
{
    QMutexLocker lk(&m_mutex);
    return QSet<QString>(m_bySeries.keyBegin(), m_bySeries.keyEnd());
}

// ── Persistence ─────────────────────────────────────────────────────────────

void BooksCatalogueLibraryStore::load()
{
    const QString path = QDir(m_dataDir).absoluteFilePath(QString::fromLatin1(FILENAME));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonArray records = root.value(QStringLiteral("records")).toArray();

    QMutexLocker lk(&m_mutex);
    m_byId.clear();
    m_bySeries.clear();
    m_byFilePath.clear();
    for (const auto& v : records) {
        if (!v.isObject()) continue;
        auto rec = CatalogueRecord::fromJson(v.toObject());
        if (rec.catalogueId.isEmpty()) continue;
        m_byId.insert(rec.catalogueId, rec);
        if (!rec.seriesId.isEmpty()) m_bySeries[rec.seriesId].insert(rec.catalogueId);
        if (!rec.filePath.isEmpty()) m_byFilePath.insert(rec.filePath, rec.catalogueId);
    }
}

void BooksCatalogueLibraryStore::save()
{
    QJsonArray records;
    {
        QMutexLocker lk(&m_mutex);
        for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
            records.append(it.value().toJson());
        }
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = kSchemaVersion;
    root[QStringLiteral("records")] = records;

    QDir().mkpath(m_dataDir);
    const QString path = QDir(m_dataDir).absoluteFilePath(QString::fromLatin1(FILENAME));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

void BooksCatalogueLibraryStore::rebuildDerivedMapsLocked()
{
    m_bySeries.clear();
    m_byFilePath.clear();
    for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
        const auto& r = it.value();
        if (!r.seriesId.isEmpty()) m_bySeries[r.seriesId].insert(r.catalogueId);
        if (!r.filePath.isEmpty()) m_byFilePath.insert(r.filePath, r.catalogueId);
    }
}
```

- [ ] **Step 5: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/core/book/BooksCatalogueLibraryStore.h` to `HEADERS`.
- Add `src/core/book/BooksCatalogueLibraryStore.cpp` to `SOURCES`.
- Add `tests/core/book/test_books_catalogue_library_store.cpp` to `tankoban_tests` sources.

- [ ] **Step 6: Run test to verify it passes**

Run:
```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R BooksCatalogueLibraryStoreTest
```
Expected: 6 tests pass.

- [ ] **Step 7: Commit**

```
git add src/core/book/BooksCatalogueLibraryStore.h src/core/book/BooksCatalogueLibraryStore.cpp tests/core/book/test_books_catalogue_library_store.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P1.3: BooksCatalogueLibraryStore + 6 tests (CRUD + by-series + reverse-lookup + persist)"
```

---

**End of Phase 1.** At this point you have:
- `BookCatalogueResult` POD (catalogue-side search result shape)
- `CatalogueRecord` POD with JSON round-trip (the v1 library entity)
- `BooksCatalogueLibraryStore` threadsafe JSON-backed store with all CRUD + by-series aggregation + reverse-lookup + persistence (10 tests total across Phase 1)

Nothing UI-facing yet, nothing wired into BooksPage yet. Build is GREEN; tests are GREEN; main app continues to behave exactly as before this phase.

---

## Phase 2 — Catalogue HTTP clients

Two HTTP-fetching clients with pure-logic JSON parsers underneath. Parsers are static + frozen-fixture-tested (GoogleTest). Network-fetching is a thin Qt wrapper around `QNetworkAccessManager`. Open Library is primary (no API key, richer series metadata, author endpoint powers the "Other books by author" scroller). Google Books is fallback (broader catalog, requires API key — Hemanth provides at writing-plans completion).

### Task 2.1: OpenLibraryClient — search parser (pure-logic)

**Files:**
- Create: `src/core/book/OpenLibraryClient.h`
- Create: `src/core/book/OpenLibraryClient.cpp`
- Create: `tests/core/book/test_open_library_client_parser.cpp`
- Create: `tests/fixtures/book_catalogue/openlib_search_stormlight.json`
- Create: `tests/fixtures/book_catalogue/openlib_search_project_hail_mary.json`
- Create: `tests/fixtures/book_catalogue/openlib_search_empty.json`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Capture three frozen Open Library fixtures**

Create `tests/fixtures/book_catalogue/openlib_search_stormlight.json` (this is a realistic Open Library search response shape for the query `q=stormlight+archive`; minimal but covers series-shape detection):

```json
{
  "numFound": 3,
  "start": 0,
  "docs": [
    {
      "key": "/works/OL14868682W",
      "title": "The Way of Kings",
      "author_name": ["Brandon Sanderson"],
      "author_key": ["OL34184A"],
      "first_publish_year": 2010,
      "isbn": ["9780765326355", "076532635X"],
      "subject": ["Epic fantasy", "High fantasy", "Magic systems", "Cosmere"],
      "cover_i": 6979861,
      "publisher": ["Tor Books"],
      "language": ["eng"],
      "number_of_pages_median": 1007
    },
    {
      "key": "/works/OL15843427W",
      "title": "Words of Radiance",
      "author_name": ["Brandon Sanderson"],
      "author_key": ["OL34184A"],
      "first_publish_year": 2014,
      "isbn": ["9780765326362"],
      "subject": ["Epic fantasy", "High fantasy", "Cosmere"],
      "cover_i": 7898123,
      "publisher": ["Tor Books"],
      "language": ["eng"],
      "number_of_pages_median": 1087
    },
    {
      "key": "/works/OL17893215W",
      "title": "Oathbringer",
      "author_name": ["Brandon Sanderson"],
      "author_key": ["OL34184A"],
      "first_publish_year": 2017,
      "isbn": ["9780765326379"],
      "subject": ["Epic fantasy", "High fantasy", "Cosmere"],
      "cover_i": 8123456,
      "publisher": ["Tor Books"],
      "language": ["eng"],
      "number_of_pages_median": 1248
    }
  ]
}
```

Create `tests/fixtures/book_catalogue/openlib_search_project_hail_mary.json`:

```json
{
  "numFound": 1,
  "start": 0,
  "docs": [
    {
      "key": "/works/OL27448W",
      "title": "Project Hail Mary",
      "author_name": ["Andy Weir"],
      "author_key": ["OL7173552A"],
      "first_publish_year": 2021,
      "isbn": ["9780593135204", "0593135202"],
      "subject": ["Hard science fiction", "First contact", "Space opera"],
      "cover_i": 10458823,
      "publisher": ["Ballantine Books"],
      "language": ["eng"],
      "number_of_pages_median": 480
    }
  ]
}
```

Create `tests/fixtures/book_catalogue/openlib_search_empty.json`:

```json
{
  "numFound": 0,
  "start": 0,
  "docs": []
}
```

- [ ] **Step 2: Write the failing test**

Create `tests/core/book/test_open_library_client_parser.cpp`:

```cpp
#include <gtest/gtest.h>
#include <QFile>
#include <QByteArray>
#include "core/book/OpenLibraryClient.h"
#include "core/book/BookCatalogueResult.h"

namespace {
QByteArray loadFixture(const char* relPath) {
    // tankoban_tests runs from out/; fixtures resolved relative to project root.
    // CMake sets TANKOBAN_TESTS_FIXTURE_DIR via add_compile_definitions.
    const QString base = QStringLiteral(TANKOBAN_TESTS_FIXTURE_DIR);
    QFile f(base + QLatin1Char('/') + QString::fromLatin1(relPath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
} // namespace

TEST(OpenLibraryClientParserTest, EmptyResponseReturnsZeroResults) {
    auto bytes = loadFixture("book_catalogue/openlib_search_empty.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    EXPECT_EQ(results.size(), 0);
}

TEST(OpenLibraryClientParserTest, ParsesSingleStandaloneBook) {
    auto bytes = loadFixture("book_catalogue/openlib_search_project_hail_mary.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    const auto& r = results.first();
    EXPECT_EQ(r.catalogueId, QStringLiteral("openlib:/works/OL27448W"));
    EXPECT_EQ(r.title, QStringLiteral("Project Hail Mary"));
    EXPECT_EQ(r.author, QStringLiteral("Andy Weir"));
    EXPECT_EQ(r.year, QStringLiteral("2021"));
    EXPECT_EQ(r.publisher, QStringLiteral("Ballantine Books"));
    EXPECT_EQ(r.pages, QStringLiteral("480"));
    EXPECT_EQ(r.language, QStringLiteral("eng"));
    EXPECT_TRUE(r.isbn.contains(QStringLiteral("9780593135204")));
    EXPECT_TRUE(r.genres.contains(QStringLiteral("Hard science fiction")));
    EXPECT_FALSE(r.coverUrl.isEmpty());
    EXPECT_FALSE(r.isSeries);
}

TEST(OpenLibraryClientParserTest, ParsesMultipleBooks) {
    auto bytes = loadFixture("book_catalogue/openlib_search_stormlight.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].title, QStringLiteral("The Way of Kings"));
    EXPECT_EQ(results[1].title, QStringLiteral("Words of Radiance"));
    EXPECT_EQ(results[2].title, QStringLiteral("Oathbringer"));
    // All share the same author key — used by the aggregator to group
    // candidate series by author + title-suffix heuristic in Phase 3.
    for (const auto& r : results) {
        EXPECT_EQ(r.author, QStringLiteral("Brandon Sanderson"));
    }
}

TEST(OpenLibraryClientParserTest, MultipleAuthorsJoinWithAmpersand) {
    QByteArray bytes = R"({
        "numFound": 1, "start": 0,
        "docs": [{
            "key": "/works/OL999W",
            "title": "Good Omens",
            "author_name": ["Neil Gaiman", "Terry Pratchett"],
            "first_publish_year": 1990,
            "subject": ["Fantasy"]
        }]
    })";
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].author, QStringLiteral("Neil Gaiman & Terry Pratchett"));
}

TEST(OpenLibraryClientParserTest, MissingFieldsLeaveEmpty) {
    QByteArray bytes = R"({
        "numFound": 1, "start": 0,
        "docs": [{
            "key": "/works/OL_minimal_W",
            "title": "Minimal Book"
        }]
    })";
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].title, QStringLiteral("Minimal Book"));
    EXPECT_TRUE(results[0].author.isEmpty());
    EXPECT_TRUE(results[0].year.isEmpty());
    EXPECT_TRUE(results[0].isbn.isEmpty());
    EXPECT_TRUE(results[0].coverUrl.isEmpty());
}

TEST(OpenLibraryClientParserTest, CoverUrlUsesIdEndpoint) {
    QByteArray bytes = R"({
        "numFound": 1, "start": 0,
        "docs": [{
            "key": "/works/OL_cover_W",
            "title": "Covered Book",
            "cover_i": 12345678
        }]
    })";
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    // L-size endpoint per Open Library convention.
    EXPECT_EQ(results[0].coverUrl,
              QStringLiteral("https://covers.openlibrary.org/b/id/12345678-L.jpg"));
}

TEST(OpenLibraryClientParserTest, GarbageJsonReturnsEmpty) {
    auto results = OpenLibraryClient::parseSearchResponse(QByteArray("not json"));
    EXPECT_EQ(results.size(), 0);
}
```

- [ ] **Step 3: Run to verify it fails**

Run:
```
cmake --build out --target tankoban_tests
```
Expected: FAIL with "cannot open source file 'core/book/OpenLibraryClient.h'".

- [ ] **Step 4: Write the header**

Create `src/core/book/OpenLibraryClient.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include "BookCatalogueResult.h"

class QNetworkAccessManager;
class QNetworkReply;

// HTTP client + JSON parser for Open Library (https://openlibrary.org).
// Primary catalogue source for BOOKS_STREMIO_PIVOT — no API key, richer
// series metadata, author endpoint powers the "Other books by author" scroller.
//
// API endpoints (subset used by v1):
//   - GET /search.json?q=<query>             -> book search
//   - GET /authors/<OLnA>/works.json         -> author's works (for scroller)
//   - GET /works/<OLnW>.json                 -> work detail (synopsis if not on search)
//
// Parsers are static + pure (no network), tested against frozen fixtures.
// Network-fetching is a thin wrapper around QNetworkAccessManager.
class OpenLibraryClient : public QObject
{
    Q_OBJECT

public:
    explicit OpenLibraryClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // ── Pure parsers (frozen-fixture testable) ────────────────────────────
    static QList<BookCatalogueResult> parseSearchResponse(const QByteArray& json);
    static QList<BookCatalogueResult> parseAuthorWorksResponse(const QByteArray& json,
                                                               const QString& authorName);

    // Detail-page synopsis is not in the search response. fetchWorkDetail
    // pulls /works/<OLnW>.json and parses its description field; this is the
    // pure version that operates on the response body.
    static QString parseWorkDescription(const QByteArray& json);

    // ── Network methods (signal-based) ────────────────────────────────────
    void search(const QString& query);          // fires searchResults / searchFailed
    void fetchAuthorWorks(const QString& authorKey, const QString& authorName);
    void fetchWorkDetail(const QString& workKey);

signals:
    void searchResults(const QList<BookCatalogueResult>& results);
    void searchFailed(const QString& error);
    void authorWorksResults(const QString& authorKey,
                            const QList<BookCatalogueResult>& results);
    void authorWorksFailed(const QString& authorKey, const QString& error);
    void workDetailReady(const QString& workKey, const QString& description);
    void workDetailFailed(const QString& workKey, const QString& error);

private:
    void onSearchReply();
    void onAuthorWorksReply();
    void onWorkDetailReply();

    QNetworkAccessManager* m_nam;
};
```

- [ ] **Step 5: Write the implementation**

Create `src/core/book/OpenLibraryClient.cpp`:

```cpp
#include "OpenLibraryClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString joinStringArray(const QJsonArray& arr, const QString& sep)
{
    QStringList parts;
    for (const auto& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) parts << s;
    }
    return parts.join(sep);
}

QString firstString(const QJsonValue& v)
{
    if (v.isArray()) {
        auto a = v.toArray();
        if (a.isEmpty()) return {};
        return a.first().toString();
    }
    if (v.isString()) return v.toString();
    return {};
}

QStringList toStringList(const QJsonArray& arr)
{
    QStringList out;
    for (const auto& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) out << s;
    }
    return out;
}

BookCatalogueResult parseDoc(const QJsonObject& doc)
{
    BookCatalogueResult r;
    const QString workKey = doc.value(QStringLiteral("key")).toString();
    r.catalogueId = QStringLiteral("openlib:") + workKey;
    r.workId = workKey;

    r.title = doc.value(QStringLiteral("title")).toString();

    const auto authorArr = doc.value(QStringLiteral("author_name")).toArray();
    r.author = joinStringArray(authorArr, QStringLiteral(" & "));

    if (doc.contains(QStringLiteral("first_publish_year"))) {
        r.year = QString::number(doc.value(QStringLiteral("first_publish_year")).toInt());
    }
    r.publisher = firstString(doc.value(QStringLiteral("publisher")));
    r.language  = firstString(doc.value(QStringLiteral("language")));

    const auto isbnArr = doc.value(QStringLiteral("isbn")).toArray();
    r.isbn = joinStringArray(isbnArr, QStringLiteral(","));

    r.genres = toStringList(doc.value(QStringLiteral("subject")).toArray());

    if (doc.contains(QStringLiteral("cover_i"))) {
        const int coverId = doc.value(QStringLiteral("cover_i")).toInt();
        if (coverId > 0) {
            r.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/%1-L.jpg")
                             .arg(coverId);
        }
    }

    if (doc.contains(QStringLiteral("number_of_pages_median"))) {
        const int p = doc.value(QStringLiteral("number_of_pages_median")).toInt();
        if (p > 0) r.pages = QString::number(p);
    }

    // Series field is patchy in Open Library; v1 uses author + title-suffix
    // heuristic in Phase 3 (BookCatalogueAggregator) instead of trusting this.
    // Surface here for diagnostic value only.
    const auto seriesArr = doc.value(QStringLiteral("series")).toArray();
    if (!seriesArr.isEmpty()) {
        r.seriesName = firstString(doc.value(QStringLiteral("series")));
    }
    r.isSeries = false; // Aggregator decides; do not infer here.

    return r;
}

} // namespace

// ── Pure parsers ────────────────────────────────────────────────────────────

QList<BookCatalogueResult> OpenLibraryClient::parseSearchResponse(const QByteArray& json)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    QList<BookCatalogueResult> results;
    const auto arr = doc.object().value(QStringLiteral("docs")).toArray();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        results.append(parseDoc(v.toObject()));
    }
    return results;
}

QList<BookCatalogueResult>
OpenLibraryClient::parseAuthorWorksResponse(const QByteArray& json,
                                            const QString& authorName)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    QList<BookCatalogueResult> results;
    const auto arr = doc.object().value(QStringLiteral("entries")).toArray();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        auto obj = v.toObject();
        BookCatalogueResult r;
        const QString workKey = obj.value(QStringLiteral("key")).toString();
        r.catalogueId = QStringLiteral("openlib:") + workKey;
        r.workId = workKey;
        r.title = obj.value(QStringLiteral("title")).toString();
        r.author = authorName; // /authors/<id>/works.json doesn't include author_name per row
        // covers can be array of ints in author/works response
        const auto covers = obj.value(QStringLiteral("covers")).toArray();
        if (!covers.isEmpty()) {
            const int coverId = covers.first().toInt();
            if (coverId > 0) {
                r.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/%1-L.jpg")
                                 .arg(coverId);
            }
        }
        if (r.title.isEmpty()) continue;
        results.append(r);
    }
    return results;
}

QString OpenLibraryClient::parseWorkDescription(const QByteArray& json)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QJsonValue v = doc.object().value(QStringLiteral("description"));
    // Open Library work descriptions are sometimes a string, sometimes
    // an object {"type":"/type/text","value":"..."}.
    if (v.isString()) return v.toString();
    if (v.isObject()) return v.toObject().value(QStringLiteral("value")).toString();
    return {};
}

// ── Network ─────────────────────────────────────────────────────────────────

OpenLibraryClient::OpenLibraryClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

void OpenLibraryClient::search(const QString& query)
{
    QUrl url(QStringLiteral("https://openlibrary.org/search.json"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("fields"),
                   QStringLiteral("key,title,author_name,author_key,first_publish_year,"
                                  "isbn,subject,cover_i,publisher,language,"
                                  "number_of_pages_median,series"));
    url.setQuery(q);
    auto* reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, &OpenLibraryClient::onSearchReply);
}

void OpenLibraryClient::fetchAuthorWorks(const QString& authorKey,
                                         const QString& authorName)
{
    QUrl url(QStringLiteral("https://openlibrary.org/authors/%1/works.json").arg(authorKey));
    auto* reply = m_nam->get(QNetworkRequest(url));
    reply->setProperty("authorKey", authorKey);
    reply->setProperty("authorName", authorName);
    connect(reply, &QNetworkReply::finished, this, &OpenLibraryClient::onAuthorWorksReply);
}

void OpenLibraryClient::fetchWorkDetail(const QString& workKey)
{
    // workKey is "/works/OL27448W"; URL is "https://openlibrary.org/works/OL27448W.json".
    const QString suffix = workKey.startsWith(QLatin1Char('/'))
                               ? workKey.mid(1) : workKey;
    QUrl url(QStringLiteral("https://openlibrary.org/%1.json").arg(suffix));
    auto* reply = m_nam->get(QNetworkRequest(url));
    reply->setProperty("workKey", workKey);
    connect(reply, &QNetworkReply::finished, this, &OpenLibraryClient::onWorkDetailReply);
}

void OpenLibraryClient::onSearchReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(reply->errorString());
        return;
    }
    emit searchResults(parseSearchResponse(reply->readAll()));
}

void OpenLibraryClient::onAuthorWorksReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString authorKey = reply->property("authorKey").toString();
    const QString authorName = reply->property("authorName").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit authorWorksFailed(authorKey, reply->errorString());
        return;
    }
    emit authorWorksResults(authorKey,
                            parseAuthorWorksResponse(reply->readAll(), authorName));
}

void OpenLibraryClient::onWorkDetailReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString workKey = reply->property("workKey").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit workDetailFailed(workKey, reply->errorString());
        return;
    }
    emit workDetailReady(workKey, parseWorkDescription(reply->readAll()));
}
```

- [ ] **Step 6: Register in CMakeLists.txt + set fixture-dir define**

Edit `CMakeLists.txt`:
- Add `src/core/book/OpenLibraryClient.h` to `HEADERS`.
- Add `src/core/book/OpenLibraryClient.cpp` to `SOURCES`.
- Add `tests/core/book/test_open_library_client_parser.cpp` to `tankoban_tests` sources.
- Add the fixture-dir compile definition for `tankoban_tests` (one-time setup that subsequent fixture-using tests reuse):

```cmake
target_compile_definitions(tankoban_tests PRIVATE
    TANKOBAN_TESTS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
```

If a `target_compile_definitions(tankoban_tests ...)` block already exists, add the `TANKOBAN_TESTS_FIXTURE_DIR=...` to it instead of creating a new block.

- [ ] **Step 7: Run test to verify it passes**

Run:
```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R OpenLibraryClientParserTest
```
Expected: 7 tests pass.

- [ ] **Step 8: Commit**

```
git add src/core/book/OpenLibraryClient.h src/core/book/OpenLibraryClient.cpp tests/core/book/test_open_library_client_parser.cpp tests/fixtures/book_catalogue/openlib_search_stormlight.json tests/fixtures/book_catalogue/openlib_search_project_hail_mary.json tests/fixtures/book_catalogue/openlib_search_empty.json CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P2.1: OpenLibraryClient parsers + 7 tests + 3 frozen fixtures"
```

---

### Task 2.2: GoogleBooksClient — parser + skeleton (fallback)

**Files:**
- Create: `src/core/book/GoogleBooksClient.h`
- Create: `src/core/book/GoogleBooksClient.cpp`
- Create: `tests/core/book/test_google_books_client_parser.cpp`
- Create: `tests/fixtures/book_catalogue/googlebooks_search_stormlight.json`
- Create: `tests/fixtures/book_catalogue/googlebooks_search_empty.json`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Capture frozen Google Books fixtures**

Create `tests/fixtures/book_catalogue/googlebooks_search_stormlight.json`:

```json
{
  "kind": "books#volumes",
  "totalItems": 2,
  "items": [
    {
      "kind": "books#volume",
      "id": "qE9SBgAAQBAJ",
      "volumeInfo": {
        "title": "The Way of Kings",
        "authors": ["Brandon Sanderson"],
        "publisher": "Tor Books",
        "publishedDate": "2010-08-31",
        "description": "Roshar is a world of stone and storms.",
        "industryIdentifiers": [
          { "type": "ISBN_13", "identifier": "9780765326355" },
          { "type": "ISBN_10", "identifier": "076532635X" }
        ],
        "pageCount": 1007,
        "categories": ["Fiction / Fantasy / Epic"],
        "language": "en",
        "imageLinks": {
          "smallThumbnail": "http://books.google.com/books/content?id=qE9SBgAAQBAJ&printsec=frontcover&img=1&zoom=5&edge=curl&source=gbs_api",
          "thumbnail": "http://books.google.com/books/content?id=qE9SBgAAQBAJ&printsec=frontcover&img=1&zoom=1&edge=curl&source=gbs_api"
        }
      }
    },
    {
      "kind": "books#volume",
      "id": "TGtnDAAAQBAJ",
      "volumeInfo": {
        "title": "Words of Radiance",
        "authors": ["Brandon Sanderson"],
        "publisher": "Tor Books",
        "publishedDate": "2014-03-04",
        "description": "Six years ago, the Assassin in White killed Gavilar Kholin.",
        "industryIdentifiers": [
          { "type": "ISBN_13", "identifier": "9780765326362" }
        ],
        "pageCount": 1087,
        "categories": ["Fiction / Fantasy / Epic"],
        "language": "en",
        "imageLinks": {
          "thumbnail": "http://books.google.com/books/content?id=TGtnDAAAQBAJ&img=1&zoom=1"
        }
      }
    }
  ]
}
```

Create `tests/fixtures/book_catalogue/googlebooks_search_empty.json`:

```json
{
  "kind": "books#volumes",
  "totalItems": 0
}
```

- [ ] **Step 2: Write the failing test**

Create `tests/core/book/test_google_books_client_parser.cpp`:

```cpp
#include <gtest/gtest.h>
#include <QFile>
#include "core/book/GoogleBooksClient.h"

namespace {
QByteArray loadFixture(const char* relPath) {
    const QString base = QStringLiteral(TANKOBAN_TESTS_FIXTURE_DIR);
    QFile f(base + QLatin1Char('/') + QString::fromLatin1(relPath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
} // namespace

TEST(GoogleBooksClientParserTest, EmptyResponseReturnsZeroResults) {
    auto bytes = loadFixture("book_catalogue/googlebooks_search_empty.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    EXPECT_EQ(results.size(), 0);
}

TEST(GoogleBooksClientParserTest, ParsesTwoBooks) {
    auto bytes = loadFixture("book_catalogue/googlebooks_search_stormlight.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 2);

    const auto& r0 = results[0];
    EXPECT_EQ(r0.catalogueId, QStringLiteral("googlebooks:qE9SBgAAQBAJ"));
    EXPECT_EQ(r0.title, QStringLiteral("The Way of Kings"));
    EXPECT_EQ(r0.author, QStringLiteral("Brandon Sanderson"));
    EXPECT_EQ(r0.publisher, QStringLiteral("Tor Books"));
    EXPECT_EQ(r0.year, QStringLiteral("2010"));
    EXPECT_EQ(r0.pages, QStringLiteral("1007"));
    EXPECT_EQ(r0.language, QStringLiteral("en"));
    EXPECT_TRUE(r0.isbn.contains(QStringLiteral("9780765326355")));
    EXPECT_TRUE(r0.genres.contains(QStringLiteral("Fiction / Fantasy / Epic")));
    EXPECT_FALSE(r0.coverUrl.isEmpty());
    EXPECT_FALSE(r0.description.isEmpty());

    EXPECT_EQ(results[1].title, QStringLiteral("Words of Radiance"));
}

TEST(GoogleBooksClientParserTest, MultipleAuthorsJoinWithAmpersand) {
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":1,
        "items":[{"id":"x","volumeInfo":{"title":"X","authors":["A","B"]}}]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].author, QStringLiteral("A & B"));
}

TEST(GoogleBooksClientParserTest, MissingVolumeInfoSkipsItem) {
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":2,
        "items":[
            {"id":"x"},
            {"id":"y","volumeInfo":{"title":"Y"}}
        ]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].title, QStringLiteral("Y"));
}

TEST(GoogleBooksClientParserTest, GarbageJsonReturnsEmpty) {
    auto results = GoogleBooksClient::parseVolumesResponse(QByteArray("not json"));
    EXPECT_EQ(results.size(), 0);
}

TEST(GoogleBooksClientParserTest, PublishedDateYearOnly) {
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":1,
        "items":[{"id":"z","volumeInfo":{"title":"Z","publishedDate":"2024"}}]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].year, QStringLiteral("2024"));
}
```

- [ ] **Step 3: Run to verify it fails**

Run:
```
cmake --build out --target tankoban_tests
```
Expected: FAIL with "cannot open source file 'core/book/GoogleBooksClient.h'".

- [ ] **Step 4: Write the header**

Create `src/core/book/GoogleBooksClient.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include "BookCatalogueResult.h"

class QNetworkAccessManager;

// HTTP client + JSON parser for Google Books (https://www.googleapis.com/books/v1).
// Fallback catalogue source — broader catalog than Open Library for newer
// releases + non-English titles; requires API key (TANKOBAN_GOOGLE_BOOKS_KEY
// env var, set in build_and_run.bat per writing-plans coordination).
//
// API endpoints used by v1:
//   - GET /volumes?q=<query>&key=<KEY>   -> volume search
class GoogleBooksClient : public QObject
{
    Q_OBJECT

public:
    explicit GoogleBooksClient(QNetworkAccessManager* nam,
                               const QString& apiKey,
                               QObject* parent = nullptr);

    static QList<BookCatalogueResult> parseVolumesResponse(const QByteArray& json);

    void search(const QString& query);

signals:
    void searchResults(const QList<BookCatalogueResult>& results);
    void searchFailed(const QString& error);

private:
    void onSearchReply();

    QNetworkAccessManager* m_nam;
    QString m_apiKey;
};
```

- [ ] **Step 5: Write the implementation**

Create `src/core/book/GoogleBooksClient.cpp`:

```cpp
#include "GoogleBooksClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString joinStringArray(const QJsonArray& arr, const QString& sep)
{
    QStringList parts;
    for (const auto& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) parts << s;
    }
    return parts.join(sep);
}

QStringList toStringList(const QJsonArray& arr)
{
    QStringList out;
    for (const auto& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) out << s;
    }
    return out;
}

QString yearOnly(const QString& publishedDate)
{
    // Google's publishedDate is "YYYY" | "YYYY-MM" | "YYYY-MM-DD"; truncate.
    if (publishedDate.size() >= 4) return publishedDate.left(4);
    return publishedDate;
}

BookCatalogueResult parseItem(const QJsonObject& item)
{
    BookCatalogueResult r;
    const QString volumeId = item.value(QStringLiteral("id")).toString();
    r.catalogueId = QStringLiteral("googlebooks:") + volumeId;

    const auto info = item.value(QStringLiteral("volumeInfo")).toObject();
    r.title = info.value(QStringLiteral("title")).toString();
    r.author = joinStringArray(info.value(QStringLiteral("authors")).toArray(),
                               QStringLiteral(" & "));
    r.publisher = info.value(QStringLiteral("publisher")).toString();
    r.year = yearOnly(info.value(QStringLiteral("publishedDate")).toString());
    r.description = info.value(QStringLiteral("description")).toString();
    r.language = info.value(QStringLiteral("language")).toString();
    r.genres = toStringList(info.value(QStringLiteral("categories")).toArray());

    if (info.contains(QStringLiteral("pageCount"))) {
        const int p = info.value(QStringLiteral("pageCount")).toInt();
        if (p > 0) r.pages = QString::number(p);
    }

    // industryIdentifiers — collect ISBN_13 + ISBN_10 entries, join by comma.
    QStringList isbns;
    const auto ids = info.value(QStringLiteral("industryIdentifiers")).toArray();
    for (const auto& v : ids) {
        if (!v.isObject()) continue;
        auto o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ISBN_13") || type == QStringLiteral("ISBN_10")) {
            const QString id = o.value(QStringLiteral("identifier")).toString();
            if (!id.isEmpty()) isbns << id;
        }
    }
    r.isbn = isbns.join(QStringLiteral(","));

    // imageLinks.thumbnail preferred; small fallback. Google returns http://
    // URLs that work over https; rewrite to https for QtWebEngine + Qt6 strict.
    const auto img = info.value(QStringLiteral("imageLinks")).toObject();
    QString cover = img.value(QStringLiteral("thumbnail")).toString();
    if (cover.isEmpty()) cover = img.value(QStringLiteral("smallThumbnail")).toString();
    if (cover.startsWith(QStringLiteral("http://"))) {
        cover.replace(0, 7, QStringLiteral("https://"));
    }
    r.coverUrl = cover;

    return r;
}

} // namespace

QList<BookCatalogueResult>
GoogleBooksClient::parseVolumesResponse(const QByteArray& json)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    QList<BookCatalogueResult> results;
    const auto arr = doc.object().value(QStringLiteral("items")).toArray();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        auto item = v.toObject();
        if (!item.contains(QStringLiteral("volumeInfo"))) continue;
        if (item.value(QStringLiteral("id")).toString().isEmpty()) continue;
        results.append(parseItem(item));
    }
    return results;
}

GoogleBooksClient::GoogleBooksClient(QNetworkAccessManager* nam,
                                     const QString& apiKey,
                                     QObject* parent)
    : QObject(parent), m_nam(nam), m_apiKey(apiKey) {}

void GoogleBooksClient::search(const QString& query)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/books/v1/volumes"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("20"));
    if (!m_apiKey.isEmpty()) {
        q.addQueryItem(QStringLiteral("key"), m_apiKey);
    }
    url.setQuery(q);
    auto* reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, &GoogleBooksClient::onSearchReply);
}

void GoogleBooksClient::onSearchReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(reply->errorString());
        return;
    }
    emit searchResults(parseVolumesResponse(reply->readAll()));
}
```

- [ ] **Step 6: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/core/book/GoogleBooksClient.h` to `HEADERS`.
- Add `src/core/book/GoogleBooksClient.cpp` to `SOURCES`.
- Add `tests/core/book/test_google_books_client_parser.cpp` to `tankoban_tests` sources.

- [ ] **Step 7: Run test to verify it passes**

Run:
```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R GoogleBooksClientParserTest
```
Expected: 6 tests pass.

- [ ] **Step 8: Commit**

```
git add src/core/book/GoogleBooksClient.h src/core/book/GoogleBooksClient.cpp tests/core/book/test_google_books_client_parser.cpp tests/fixtures/book_catalogue/googlebooks_search_stormlight.json tests/fixtures/book_catalogue/googlebooks_search_empty.json CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P2.2: GoogleBooksClient parsers + 6 tests + 2 frozen fixtures"
```

---

**End of Phase 2.** At this point you have:
- `OpenLibraryClient` (search + author-works + work-detail parsers + network thin wrappers; 7 tests against 3 fixtures)
- `GoogleBooksClient` (volumes parser + network thin wrapper; 6 tests against 2 fixtures)

Main app build still GREEN. Nothing wired into BooksPage yet. The two clients are independently exercised; Phase 3 builds the aggregator on top.

---

## Phase 3 — BookCatalogueAggregator

The aggregator is the brain of the catalogue layer. It fans queries out to OpenLibraryClient + GoogleBooksClient in parallel, merges results, dedupes by ISBN-or-fuzzy-title-author, and runs the series-shape detection heuristic that decides which results become a series tile vs a standalone book tile.

The series-detection heuristic is the meat of this phase and is pure-logic + TDD-friendly. Open Library's `series` field is patchy, so the heuristic falls back to title-suffix pattern matching (per spec §3.2 + §6.3 Rule-14 implementation call).

### Task 3.1: Series-detection heuristic (pure logic)

**Files:**
- Create: `src/core/book/SeriesDetector.h`
- Create: `src/core/book/SeriesDetector.cpp`
- Create: `tests/core/book/test_series_detector.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/core/book/test_series_detector.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/book/SeriesDetector.h"
#include "core/book/BookCatalogueResult.h"

namespace {
BookCatalogueResult mk(const QString& title, const QString& author,
                      const QString& year = QString()) {
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("openlib:/works/OL_%1_W").arg(title);
    r.workId = QStringLiteral("/works/OL_%1_W").arg(title);
    r.title = title;
    r.author = author;
    r.year = year;
    return r;
}
} // namespace

TEST(SeriesDetectorTest, SingleStandaloneNotSeries) {
    QList<BookCatalogueResult> input{
        mk("Project Hail Mary", "Andy Weir", "2021"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.standalones.size(), 1);
    EXPECT_EQ(out.standalones[0].title, QStringLiteral("Project Hail Mary"));
    EXPECT_FALSE(out.standalones[0].isSeries);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
}

TEST(SeriesDetectorTest, MultipleBooksSameAuthorNoSeriesPatternNotGrouped) {
    QList<BookCatalogueResult> input{
        mk("The Martian", "Andy Weir", "2014"),
        mk("Project Hail Mary", "Andy Weir", "2021"),
        mk("Artemis", "Andy Weir", "2017"),
    };
    auto out = SeriesDetector::detect(input);
    EXPECT_EQ(out.standalones.size(), 3);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
}

TEST(SeriesDetectorTest, CommonSeriesNamePrefixGroupsAsSeries) {
    QList<BookCatalogueResult> input{
        mk("The Stormlight Archive #1: The Way of Kings", "Brandon Sanderson", "2010"),
        mk("The Stormlight Archive #2: Words of Radiance", "Brandon Sanderson", "2014"),
        mk("The Stormlight Archive #3: Oathbringer", "Brandon Sanderson", "2017"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    const auto& g = out.seriesGroups.first();
    EXPECT_EQ(g.seriesName, QStringLiteral("The Stormlight Archive"));
    EXPECT_EQ(g.books.size(), 3);
    EXPECT_EQ(g.books[0].seriesPosition, 1);
    EXPECT_EQ(g.books[1].seriesPosition, 2);
    EXPECT_EQ(g.books[2].seriesPosition, 3);
}

TEST(SeriesDetectorTest, ColonStyleSeriesNameGrouped) {
    QList<BookCatalogueResult> input{
        mk("Mistborn: The Final Empire", "Brandon Sanderson", "2006"),
        mk("Mistborn: The Well of Ascension", "Brandon Sanderson", "2007"),
        mk("Mistborn: The Hero of Ages", "Brandon Sanderson", "2008"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Mistborn"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 3);
}

TEST(SeriesDetectorTest, BookNumberPatternGrouped) {
    QList<BookCatalogueResult> input{
        mk("Wheel of Time, Book 1: The Eye of the World", "Robert Jordan", "1990"),
        mk("Wheel of Time, Book 2: The Great Hunt", "Robert Jordan", "1990"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Wheel of Time"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 2);
    EXPECT_EQ(out.seriesGroups.first().books[0].seriesPosition, 1);
    EXPECT_EQ(out.seriesGroups.first().books[1].seriesPosition, 2);
}

TEST(SeriesDetectorTest, RomanNumeralPatternGrouped) {
    QList<BookCatalogueResult> input{
        mk("Dune I", "Frank Herbert", "1965"),
        mk("Dune II: Dune Messiah", "Frank Herbert", "1969"),
        mk("Dune III: Children of Dune", "Frank Herbert", "1976"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Dune"));
    EXPECT_EQ(out.seriesGroups.first().books[0].seriesPosition, 1);
    EXPECT_EQ(out.seriesGroups.first().books[1].seriesPosition, 2);
    EXPECT_EQ(out.seriesGroups.first().books[2].seriesPosition, 3);
}

TEST(SeriesDetectorTest, ParensPositionPatternGrouped) {
    QList<BookCatalogueResult> input{
        mk("Discworld (1): The Colour of Magic", "Terry Pratchett", "1983"),
        mk("Discworld (2): The Light Fantastic", "Terry Pratchett", "1986"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Discworld"));
}

TEST(SeriesDetectorTest, OpenLibrarySeriesFieldUsedWhenPresent) {
    auto a = mk("The Way of Kings", "Brandon Sanderson", "2010");
    auto b = mk("Words of Radiance", "Brandon Sanderson", "2014");
    a.seriesName = QStringLiteral("The Stormlight Archive");
    b.seriesName = QStringLiteral("The Stormlight Archive");
    QList<BookCatalogueResult> input{a, b};
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName,
              QStringLiteral("The Stormlight Archive"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 2);
}

TEST(SeriesDetectorTest, DifferentAuthorsSameTitlePatternNotGrouped) {
    // If two books have the same title-suffix pattern but different authors,
    // do not group — series are author-bound.
    QList<BookCatalogueResult> input{
        mk("Foo Book 1", "Author A", "2020"),
        mk("Foo Book 2", "Author B", "2021"),
    };
    auto out = SeriesDetector::detect(input);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
    EXPECT_EQ(out.standalones.size(), 2);
}

TEST(SeriesDetectorTest, SingleBookWithSeriesFieldStaysStandalone) {
    // A single book with a series field hint but no sibling is not a series
    // (movie-shape fallback per spec — safer than wrong-grouping).
    auto a = mk("The Way of Kings", "Brandon Sanderson", "2010");
    a.seriesName = QStringLiteral("The Stormlight Archive");
    QList<BookCatalogueResult> input{a};
    auto out = SeriesDetector::detect(input);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
    ASSERT_EQ(out.standalones.size(), 1);
    EXPECT_FALSE(out.standalones[0].isSeries);
}

TEST(SeriesDetectorTest, MixedSeriesAndStandaloneInOneAuthorGroup) {
    QList<BookCatalogueResult> input{
        mk("Mistborn: The Final Empire", "Brandon Sanderson", "2006"),
        mk("Mistborn: The Well of Ascension", "Brandon Sanderson", "2007"),
        mk("Elantris", "Brandon Sanderson", "2005"),
        mk("Warbreaker", "Brandon Sanderson", "2009"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Mistborn"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 2);
    EXPECT_EQ(out.standalones.size(), 2);
    QStringList standaloneTitles;
    for (const auto& s : out.standalones) standaloneTitles << s.title;
    EXPECT_TRUE(standaloneTitles.contains(QStringLiteral("Elantris")));
    EXPECT_TRUE(standaloneTitles.contains(QStringLiteral("Warbreaker")));
}
```

- [ ] **Step 2: Run to verify it fails**

Run:
```
cmake --build out --target tankoban_tests
```
Expected: FAIL with "cannot open source file 'core/book/SeriesDetector.h'".

- [ ] **Step 3: Write the header**

Create `src/core/book/SeriesDetector.h`:

```cpp
#pragma once

#include <QList>
#include <QString>

#include "BookCatalogueResult.h"

// Series-shape detection for catalogue results.
//
// Given a list of BookCatalogueResult (typically the merged output of
// OpenLibraryClient + GoogleBooksClient parsers), group them into series
// when a clear series-shape signal exists. Otherwise leave them as
// standalones — the safer fallback per spec §3.2 (a wrong-singleton is
// recoverable; a wrong-grouping is jarring).
//
// Signals used, in priority order:
//   1. Open Library / GoogleBooks `seriesName` field (when populated)
//      AND at least one sibling under the same (author, seriesName).
//   2. Title pattern: extract base + position from
//        "<base> #N", "<base>, Book N", "<base>: ...", "<base> N",
//        "<base> (N)", "<base> I/II/III/IV/V/VI/VII/VIII/IX/X"
//      AND at least one sibling under the same (author, base) extracted.
//
// All grouping is author-bound — different authors with similar title
// patterns do NOT merge into one series.
//
// Single books that match a pattern alone (no siblings) stay standalone.
class SeriesDetector
{
public:
    struct SeriesGroup {
        QString seriesName;
        QString author;
        QList<BookCatalogueResult> books;   // sorted by seriesPosition asc
    };

    struct DetectionResult {
        QList<SeriesGroup> seriesGroups;
        QList<BookCatalogueResult> standalones;
    };

    // Pure function: takes a flat list, returns the partition.
    // Mutates each grouped book's isSeries / seriesName / seriesPosition fields.
    static DetectionResult detect(const QList<BookCatalogueResult>& flatResults);

    // ── Exposed for unit-testing the title parsing primitive ─────────────
    struct TitleParse {
        bool matched = false;
        QString base;       // "The Stormlight Archive"
        int     position = 0; // 1-indexed; 0 if no positional signal
    };
    static TitleParse parseSeriesTitlePattern(const QString& title);

    // Roman numeral 1-10 → int; returns 0 on no match.
    static int romanToInt(const QString& roman);
};
```

- [ ] **Step 4: Write the implementation**

Create `src/core/book/SeriesDetector.cpp`:

```cpp
#include "SeriesDetector.h"

#include <QHash>
#include <QRegularExpression>

namespace {

constexpr int kMinSeriesSize = 2; // Need 2+ siblings to call it a series.

QString trimTitleEdges(const QString& s)
{
    QString t = s;
    while (!t.isEmpty() && (t.endsWith(QLatin1Char(':')) ||
                            t.endsWith(QLatin1Char(',')) ||
                            t.endsWith(QLatin1Char('-')) ||
                            t.endsWith(QLatin1Char(' ')))) {
        t.chop(1);
    }
    return t.trimmed();
}

} // namespace

int SeriesDetector::romanToInt(const QString& s)
{
    static const QHash<QString, int> table = {
        {QStringLiteral("I"),    1}, {QStringLiteral("II"),   2},
        {QStringLiteral("III"),  3}, {QStringLiteral("IV"),   4},
        {QStringLiteral("V"),    5}, {QStringLiteral("VI"),   6},
        {QStringLiteral("VII"),  7}, {QStringLiteral("VIII"), 8},
        {QStringLiteral("IX"),   9}, {QStringLiteral("X"),   10},
    };
    auto it = table.constFind(s.toUpper());
    return it == table.constEnd() ? 0 : it.value();
}

SeriesDetector::TitleParse SeriesDetector::parseSeriesTitlePattern(const QString& title)
{
    TitleParse p;

    // Patterns ordered from most-specific to least, all using greedy base capture.
    // Stop at the first match.

    // 1. "<base> #N[: subtitle]"
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s*#(?<n>\d+)(?:\s*[:\-].*)?$)");
        auto m = re.match(title);
        if (m.hasMatch()) {
            p.matched = true;
            p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
            p.position = m.captured(QStringLiteral("n")).toInt();
            return p;
        }
    }

    // 2. "<base>, Book N[: subtitle]" / "<base> Book N[: subtitle]"
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s*,?\s*Book\s+(?<n>\d+)(?:\s*[:\-].*)?$)",
            QRegularExpression::CaseInsensitiveOption);
        auto m = re.match(title);
        if (m.hasMatch()) {
            p.matched = true;
            p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
            p.position = m.captured(QStringLiteral("n")).toInt();
            return p;
        }
    }

    // 3. "<base> (N)[: subtitle]"
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s*\((?<n>\d+)\)(?:\s*[:\-].*)?$)");
        auto m = re.match(title);
        if (m.hasMatch()) {
            p.matched = true;
            p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
            p.position = m.captured(QStringLiteral("n")).toInt();
            return p;
        }
    }

    // 4. "<base> <ROMAN>[: subtitle]" (I..X)
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s+(?<r>I{1,3}|IV|V|VI{0,3}|IX|X)(?:\s*[:\-].*)?$)");
        auto m = re.match(title);
        if (m.hasMatch()) {
            const int n = romanToInt(m.captured(QStringLiteral("r")));
            if (n > 0) {
                p.matched = true;
                p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
                p.position = n;
                return p;
            }
        }
    }

    // 5. "<base>: <subtitle>" — base is everything before the FIRST colon.
    //    No position signal; only useful when a sibling has the same base.
    //    Position stays 0; grouping fills it in arrival order.
    {
        const int colon = title.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            p.matched = true;
            p.base = trimTitleEdges(title.left(colon));
            p.position = 0; // unknown — sibling-ordered fill at grouping time
            return p;
        }
    }

    p.matched = false;
    return p;
}

SeriesDetector::DetectionResult
SeriesDetector::detect(const QList<BookCatalogueResult>& flatResults)
{
    DetectionResult out;

    // Step 1: Bucket by (author, candidate-series-name).
    //   - First try Open Library `seriesName` field (high-confidence).
    //   - Else try title-pattern parse.
    //   - Else: standalone (author alone is not enough to group).
    using GroupKey = std::pair<QString, QString>;
    struct Bucket {
        QList<BookCatalogueResult> books;
        bool fromSeriesField = false; // priority signal
    };

    auto keyHash = [](const GroupKey& k) {
        return qHash(k.first) ^ qHash(k.second);
    };
    // QHash needs an explicit qHash overload for std::pair; use QString concat as key.
    QHash<QString, Bucket> buckets;
    QList<BookCatalogueResult> unbucketed;

    for (const auto& r : flatResults) {
        if (r.author.isEmpty()) {
            unbucketed.append(r);
            continue;
        }
        // High-priority: explicit seriesName field present.
        if (!r.seriesName.isEmpty()) {
            const QString k = r.author + QLatin1Char('\x1f') + r.seriesName;
            buckets[k].books.append(r);
            buckets[k].fromSeriesField = true;
            continue;
        }
        // Title-pattern parse.
        auto tp = parseSeriesTitlePattern(r.title);
        if (tp.matched && !tp.base.isEmpty()) {
            const QString k = r.author + QLatin1Char('\x1f') + tp.base;
            auto enriched = r;
            enriched.seriesPosition = tp.position;
            if (enriched.seriesName.isEmpty()) enriched.seriesName = tp.base;
            buckets[k].books.append(enriched);
            continue;
        }
        unbucketed.append(r);
    }

    // Step 2: Promote buckets with >= kMinSeriesSize siblings to SeriesGroup.
    //         Singletons fall back to standalone.
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        if (it.value().books.size() < kMinSeriesSize) {
            for (const auto& r : it.value().books) {
                // Strip the speculative series fields when promoting back to standalone.
                auto bareback = r;
                bareback.isSeries = false;
                bareback.seriesName.clear();
                bareback.seriesPosition = 0;
                bareback.seriesTotal = 0;
                out.standalones.append(bareback);
            }
            continue;
        }
        SeriesGroup g;
        g.books = it.value().books;
        g.author = g.books.first().author;
        g.seriesName = g.books.first().seriesName.isEmpty()
                           ? it.key().section(QLatin1Char('\x1f'), 1, 1)
                           : g.books.first().seriesName;

        // If positions are missing (0), fill in arrival order (1-indexed).
        // Otherwise sort by position.
        bool anyMissing = false;
        for (const auto& b : g.books) {
            if (b.seriesPosition == 0) { anyMissing = true; break; }
        }
        if (anyMissing) {
            int pos = 1;
            for (auto& b : g.books) b.seriesPosition = pos++;
        } else {
            std::sort(g.books.begin(), g.books.end(),
                      [](const BookCatalogueResult& a, const BookCatalogueResult& b) {
                          return a.seriesPosition < b.seriesPosition;
                      });
        }

        // Stamp isSeries + seriesName + seriesTotal on each book.
        const int total = g.books.size();
        for (auto& b : g.books) {
            b.isSeries = true;
            b.seriesName = g.seriesName;
            b.seriesTotal = total;
        }
        out.seriesGroups.append(g);
    }

    // Step 3: Unbucketed go to standalones.
    for (const auto& r : unbucketed) {
        auto clean = r;
        clean.isSeries = false;
        out.standalones.append(clean);
    }

    return out;
}
```

- [ ] **Step 5: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/core/book/SeriesDetector.h` to `HEADERS`.
- Add `src/core/book/SeriesDetector.cpp` to `SOURCES`.
- Add `tests/core/book/test_series_detector.cpp` to `tankoban_tests` sources.

- [ ] **Step 6: Run test to verify it passes**

Run:
```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R SeriesDetectorTest
```
Expected: 11 tests pass.

- [ ] **Step 7: Commit**

```
git add src/core/book/SeriesDetector.h src/core/book/SeriesDetector.cpp tests/core/book/test_series_detector.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P3.1: SeriesDetector heuristic + 11 tests (5 title patterns + series-field signal + edge cases)"
```

---

### Task 3.2: BookCatalogueAggregator — cross-source dedup (pure logic)

**Files:**
- Create: `src/core/book/CatalogueDeduper.h`
- Create: `src/core/book/CatalogueDeduper.cpp`
- Create: `tests/core/book/test_catalogue_deduper.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/core/book/test_catalogue_deduper.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/book/CatalogueDeduper.h"
#include "core/book/BookCatalogueResult.h"

namespace {
BookCatalogueResult mk(const QString& source, const QString& id,
                      const QString& title, const QString& author,
                      const QString& isbn = QString()) {
    BookCatalogueResult r;
    r.catalogueId = source + QStringLiteral(":") + id;
    r.title = title;
    r.author = author;
    r.isbn = isbn;
    return r;
}
} // namespace

TEST(CatalogueDeduperTest, EmptyInputReturnsEmpty) {
    QList<BookCatalogueResult> openlib, googlebooks;
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 0);
}

TEST(CatalogueDeduperTest, NonOverlappingResultsAllPreserved) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Klara and the Sun", "Kazuo Ishiguro", "9780593318171"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 2);
}

TEST(CatalogueDeduperTest, ExactIsbnMatchDedupes) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    ASSERT_EQ(out.size(), 1);
    // OpenLibrary wins as primary source.
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
}

TEST(CatalogueDeduperTest, IsbnSubsetMatchDedupes) {
    // OpenLib has multi-ISBN ",9780593135204,0593135202"; Google has just 9780593135204.
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir",
           "9780593135204,0593135202"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    ASSERT_EQ(out.size(), 1);
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
}

TEST(CatalogueDeduperTest, FuzzyTitleAuthorMatchDedupesWhenNoIsbn) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Project Hail Mary", "Andy Weir"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    ASSERT_EQ(out.size(), 1);
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
}

TEST(CatalogueDeduperTest, FuzzyTitleNormalizesCaseAndPunctuation) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "a", "The Way of Kings", "Brandon Sanderson"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "b", "the way of kings.", "BRANDON SANDERSON"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 1);
}

TEST(CatalogueDeduperTest, DifferentAuthorSameTitleNotDeduped) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "a", "Foundation", "Isaac Asimov"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "b", "Foundation", "Frank Herbert"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 2);
}

TEST(CatalogueDeduperTest, MergePreservesOpenLibraryDescriptionAndCover) {
    // If OpenLibrary lacks description and Google has it, the merge should
    // copy missing fields from the Google record onto the OpenLibrary winner.
    BookCatalogueResult openlib_book;
    openlib_book.catalogueId = QStringLiteral("openlib:OL27448W");
    openlib_book.title = QStringLiteral("Project Hail Mary");
    openlib_book.author = QStringLiteral("Andy Weir");
    openlib_book.isbn = QStringLiteral("9780593135204");
    openlib_book.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/123-L.jpg");
    // No description on the OpenLibrary side.

    BookCatalogueResult google_book;
    google_book.catalogueId = QStringLiteral("googlebooks:xyz");
    google_book.title = QStringLiteral("Project Hail Mary");
    google_book.author = QStringLiteral("Andy Weir");
    google_book.isbn = QStringLiteral("9780593135204");
    google_book.description = QStringLiteral("Ryland Grace, sole survivor...");

    auto out = CatalogueDeduper::merge({openlib_book}, {google_book});
    ASSERT_EQ(out.size(), 1);
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
    EXPECT_EQ(out[0].description, QStringLiteral("Ryland Grace, sole survivor..."));
    EXPECT_TRUE(out[0].coverUrl.startsWith(QStringLiteral("https://covers.openlibrary.org")));
}
```

- [ ] **Step 2: Run to verify it fails**

Run:
```
cmake --build out --target tankoban_tests
```
Expected: FAIL.

- [ ] **Step 3: Write the header**

Create `src/core/book/CatalogueDeduper.h`:

```cpp
#pragma once

#include <QList>

#include "BookCatalogueResult.h"

// Cross-source dedup for catalogue results. Merges two source lists
// (OpenLibrary + GoogleBooks) into a single ordered list with duplicates
// removed and missing fields cross-filled.
//
// Dedup signals, in priority order:
//   1. Any shared ISBN between the two records (most reliable).
//   2. Fuzzy title + author equality (normalized: lowercased, punctuation-stripped).
//
// Winner policy: OpenLibrary wins when both have the same book (primary source).
// Missing fields on the winner get filled from the loser (description, coverUrl, etc.).
class CatalogueDeduper
{
public:
    static QList<BookCatalogueResult> merge(
        const QList<BookCatalogueResult>& openlib,
        const QList<BookCatalogueResult>& googlebooks);

    // Exposed for unit-testing: normalize a string for fuzzy compare.
    static QString normalize(const QString& s);
};
```

- [ ] **Step 4: Write the implementation**

Create `src/core/book/CatalogueDeduper.cpp`:

```cpp
#include "CatalogueDeduper.h"

#include <QSet>
#include <QStringList>

namespace {

QSet<QString> isbnsOf(const BookCatalogueResult& r)
{
    QSet<QString> out;
    const auto parts = r.isbn.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const auto& p : parts) {
        const QString clean = p.trimmed();
        if (!clean.isEmpty()) out.insert(clean);
    }
    return out;
}

bool isbnsOverlap(const BookCatalogueResult& a, const BookCatalogueResult& b)
{
    const auto sa = isbnsOf(a);
    if (sa.isEmpty()) return false;
    const auto sb = isbnsOf(b);
    if (sb.isEmpty()) return false;
    for (const auto& v : sa) {
        if (sb.contains(v)) return true;
    }
    return false;
}

bool fuzzyTitleAuthorEqual(const BookCatalogueResult& a, const BookCatalogueResult& b)
{
    return CatalogueDeduper::normalize(a.title)  == CatalogueDeduper::normalize(b.title)
        && CatalogueDeduper::normalize(a.author) == CatalogueDeduper::normalize(b.author);
}

void fillMissingFromLoser(BookCatalogueResult& winner, const BookCatalogueResult& loser)
{
    if (winner.description.isEmpty()) winner.description = loser.description;
    if (winner.coverUrl.isEmpty())    winner.coverUrl    = loser.coverUrl;
    if (winner.publisher.isEmpty())   winner.publisher   = loser.publisher;
    if (winner.year.isEmpty())        winner.year        = loser.year;
    if (winner.pages.isEmpty())       winner.pages       = loser.pages;
    if (winner.language.isEmpty())    winner.language    = loser.language;
    if (winner.genres.isEmpty())      winner.genres      = loser.genres;
    if (winner.isbn.isEmpty())        winner.isbn        = loser.isbn;
}

} // namespace

QString CatalogueDeduper::normalize(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c.isLetterOrNumber()) out.append(c.toLower());
        else if (c.isSpace())     out.append(QLatin1Char(' '));
        // punctuation discarded
    }
    // Collapse runs of spaces.
    QString collapsed;
    bool prevSpace = false;
    for (const QChar c : out) {
        if (c == QLatin1Char(' ')) {
            if (!prevSpace) collapsed.append(c);
            prevSpace = true;
        } else {
            collapsed.append(c);
            prevSpace = false;
        }
    }
    return collapsed.trimmed();
}

QList<BookCatalogueResult> CatalogueDeduper::merge(
    const QList<BookCatalogueResult>& openlib,
    const QList<BookCatalogueResult>& googlebooks)
{
    QList<BookCatalogueResult> out = openlib;
    QList<bool> consumed(googlebooks.size(), false);

    // Pass 1: For each OpenLibrary record, look for a Google match.
    for (auto& winner : out) {
        for (int j = 0; j < googlebooks.size(); ++j) {
            if (consumed[j]) continue;
            const auto& loser = googlebooks[j];
            if (isbnsOverlap(winner, loser) || fuzzyTitleAuthorEqual(winner, loser)) {
                fillMissingFromLoser(winner, loser);
                consumed[j] = true;
                break;
            }
        }
    }

    // Pass 2: Append non-consumed Google records.
    for (int j = 0; j < googlebooks.size(); ++j) {
        if (!consumed[j]) out.append(googlebooks[j]);
    }

    return out;
}
```

- [ ] **Step 5: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/core/book/CatalogueDeduper.h` to `HEADERS`.
- Add `src/core/book/CatalogueDeduper.cpp` to `SOURCES`.
- Add `tests/core/book/test_catalogue_deduper.cpp` to `tankoban_tests` sources.

- [ ] **Step 6: Run test to verify it passes**

Run:
```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R CatalogueDeduperTest
```
Expected: 8 tests pass.

- [ ] **Step 7: Commit**

```
git add src/core/book/CatalogueDeduper.h src/core/book/CatalogueDeduper.cpp tests/core/book/test_catalogue_deduper.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P3.2: CatalogueDeduper ISBN + fuzzy-title merge + 8 tests"
```

---

### Task 3.3: BookCatalogueAggregator — orchestrator (fan-out + signal pipeline)

**Files:**
- Create: `src/core/book/BookCatalogueAggregator.h`
- Create: `src/core/book/BookCatalogueAggregator.cpp`
- Modify: `CMakeLists.txt`

This task wires the pieces from Phase 2 + 3.1 + 3.2 into a single QObject that the BooksTankoLibrarySearchWidget consumes. It has no GoogleTest coverage by itself (pure signal-orchestration; covered by integration smoke in Phase 9).

- [ ] **Step 1: Write the header**

Create `src/core/book/BookCatalogueAggregator.h`:

```cpp
#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "BookCatalogueResult.h"
#include "SeriesDetector.h"

class QNetworkAccessManager;
class OpenLibraryClient;
class GoogleBooksClient;

// Top-of-the-catalogue layer for BOOKS_STREMIO_PIVOT.
//
// Fires OpenLibraryClient + GoogleBooksClient queries in parallel, waits for
// BOTH to return (or timeout/fail), dedupes with CatalogueDeduper, runs
// SeriesDetector, and emits TWO ordered lists for the search-takeover view:
//   - seriesGroups (multi-book series tiles)
//   - standalones (movie-shape book tiles)
//
// Per-source failures are non-fatal — if Google Books fails, results from
// OpenLibrary still flow through. Both-failure cases emit aggregateFailed.
//
// Lifecycle: one Aggregator instance owned by BooksPage; new query()
// supersedes any pending in-flight query (cheaper than tracking generation IDs
// for a 2-user app — last-fire-wins is acceptable).
class BookCatalogueAggregator : public QObject
{
    Q_OBJECT

public:
    explicit BookCatalogueAggregator(QNetworkAccessManager* nam,
                                     const QString& googleBooksApiKey,
                                     QObject* parent = nullptr);

    void query(const QString& q);

    // For the "Other books by author" scroller on detail pages.
    void fetchAuthorWorks(const QString& openLibraryAuthorKey, const QString& authorName);

signals:
    // Fires once both sources have replied (or one replied + the other failed).
    // seriesGroups + standalones are partitioned via SeriesDetector.
    void aggregateReady(const QString& query,
                        const QList<SeriesDetector::SeriesGroup>& seriesGroups,
                        const QList<BookCatalogueResult>& standalones);

    void aggregateFailed(const QString& query, const QString& error);

    // Per-author works for the scroller.
    void authorWorksReady(const QString& authorKey,
                          const QList<BookCatalogueResult>& works);

private:
    void tryEmitAggregate();

    QNetworkAccessManager* m_nam;
    OpenLibraryClient*     m_openlib;
    GoogleBooksClient*     m_googlebooks;

    QString m_currentQuery;

    // Pending state for the current query — both must complete (success or
    // failure) before tryEmitAggregate() fires.
    bool m_openlibPending = false;
    bool m_googlebooksPending = false;
    QList<BookCatalogueResult> m_openlibResults;
    QList<BookCatalogueResult> m_googlebooksResults;
    bool m_openlibSucceeded = false;
    bool m_googlebooksSucceeded = false;
    QString m_lastOpenlibError;
    QString m_lastGooglebooksError;
};
```

- [ ] **Step 2: Write the implementation**

Create `src/core/book/BookCatalogueAggregator.cpp`:

```cpp
#include "BookCatalogueAggregator.h"

#include "OpenLibraryClient.h"
#include "GoogleBooksClient.h"
#include "CatalogueDeduper.h"

BookCatalogueAggregator::BookCatalogueAggregator(QNetworkAccessManager* nam,
                                                 const QString& googleBooksApiKey,
                                                 QObject* parent)
    : QObject(parent),
      m_nam(nam),
      m_openlib(new OpenLibraryClient(nam, this)),
      m_googlebooks(new GoogleBooksClient(nam, googleBooksApiKey, this))
{
    connect(m_openlib, &OpenLibraryClient::searchResults,
            this, [this](const QList<BookCatalogueResult>& results) {
                m_openlibResults = results;
                m_openlibSucceeded = true;
                m_openlibPending = false;
                tryEmitAggregate();
            });
    connect(m_openlib, &OpenLibraryClient::searchFailed,
            this, [this](const QString& err) {
                m_lastOpenlibError = err;
                m_openlibSucceeded = false;
                m_openlibPending = false;
                tryEmitAggregate();
            });

    connect(m_googlebooks, &GoogleBooksClient::searchResults,
            this, [this](const QList<BookCatalogueResult>& results) {
                m_googlebooksResults = results;
                m_googlebooksSucceeded = true;
                m_googlebooksPending = false;
                tryEmitAggregate();
            });
    connect(m_googlebooks, &GoogleBooksClient::searchFailed,
            this, [this](const QString& err) {
                m_lastGooglebooksError = err;
                m_googlebooksSucceeded = false;
                m_googlebooksPending = false;
                tryEmitAggregate();
            });

    connect(m_openlib, &OpenLibraryClient::authorWorksResults,
            this, [this](const QString& authorKey,
                         const QList<BookCatalogueResult>& results) {
                emit authorWorksReady(authorKey, results);
            });
}

void BookCatalogueAggregator::query(const QString& q)
{
    m_currentQuery = q;
    m_openlibResults.clear();
    m_googlebooksResults.clear();
    m_openlibSucceeded = false;
    m_googlebooksSucceeded = false;
    m_openlibPending = true;
    m_googlebooksPending = true;

    m_openlib->search(q);
    m_googlebooks->search(q);
}

void BookCatalogueAggregator::fetchAuthorWorks(const QString& authorKey,
                                               const QString& authorName)
{
    m_openlib->fetchAuthorWorks(authorKey, authorName);
}

void BookCatalogueAggregator::tryEmitAggregate()
{
    if (m_openlibPending || m_googlebooksPending) return;

    if (!m_openlibSucceeded && !m_googlebooksSucceeded) {
        emit aggregateFailed(m_currentQuery,
            QStringLiteral("OpenLibrary: %1; GoogleBooks: %2")
                .arg(m_lastOpenlibError, m_lastGooglebooksError));
        return;
    }

    auto merged = CatalogueDeduper::merge(m_openlibResults, m_googlebooksResults);
    auto detection = SeriesDetector::detect(merged);
    emit aggregateReady(m_currentQuery, detection.seriesGroups, detection.standalones);
}
```

- [ ] **Step 3: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/core/book/BookCatalogueAggregator.h` to `HEADERS`.
- Add `src/core/book/BookCatalogueAggregator.cpp` to `SOURCES`.

- [ ] **Step 4: Build to verify no compile errors**

Run:
```
build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```
git add src/core/book/BookCatalogueAggregator.h src/core/book/BookCatalogueAggregator.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P3.3: BookCatalogueAggregator orchestrator (fan-out + dedup + series-detect pipeline)"
```

---

**End of Phase 3.** At this point you have:
- `SeriesDetector` — pure-logic series-shape detection with 5 title-pattern matchers + Open Library `seriesName` signal (11 tests)
- `CatalogueDeduper` — ISBN-or-fuzzy-title merge across two catalogue sources (8 tests)
- `BookCatalogueAggregator` — orchestrator wiring all three layers (clients + deduper + series detector) into one signal-pipe consumable by the search widget

Total tests added in Phases 1-3: 36 (3 + 3 + 6 + 7 + 6 + 11 + 8 + skeleton compile = 44 across `tankoban_tests`).
Main app build still GREEN. Nothing UI-wired yet.

---

## Phase 4 — Source layer expansion

This phase has two **blocking external dependencies** that must be resolved before the code tasks run:

1. **Anna's Archive captcha-solving approach** (Task 4.1, blocking AA re-enable).
2. **Agent 4 sign-off + cross-coordination** on Tankorent book-category wiring + magnet→Books-library-path shim (Task 4.2, blocking Tankorent integration).

Phase 4 also adds the picker-side engine `BookSearchAggregator` (parallel fan-out across LibGen + AA + Tankorent) and the magnet-source variant of `BookDownloader`.

### Task 4.1: AA captcha-solving approach — investigation + decision

**Files:**
- Create: `agents/audits/aa_captcha_investigation_2026-05-20.md` (decision-record only; not source code)

This is a process task. It outputs a decision recorded in a brief audit-style markdown file; subsequent tasks branch on its outcome.

- [ ] **Step 1: Probe whether Anna's Archive offers a programmatic API key**

Visit https://annas-archive.org/donate and https://annas-archive.org/account (manually, in browser; the agent records what's offered). Check whether AA exposes a "Fast Download API" or "Member API" with a token-based authentication path that bypasses the captcha-gated interstitial. Note: as of 2026-04-22 (when 4B last probed), AA offered Member-tier accounts with "Fast Download" privilege but no documented API key. Verify whether this has changed.

Record findings in `agents/audits/aa_captcha_investigation_2026-05-20.md` under §1 "API token path."

- [ ] **Step 2: Probe whether `CloudflareCookieHarvester` extends to AA captcha-stage-(a)**

Read `src/core/indexers/CloudflareCookieHarvester.{h,cpp}` and check whether its existing harvest logic (Tankorent's anti-bot path) generalizes to AA's `cf_clearance` interstitial. The cookie-harvest pattern works against Tankorent indexers; AA's interstitial may use the same Cloudflare gate or a custom CAPTCHA above it.

Record findings under §2 "Cookie harvest path."

- [ ] **Step 3: Pick the approach and document in the audit file**

Choose one of three paths, recording rationale in `agents/audits/aa_captcha_investigation_2026-05-20.md` under §3 "Decision":

- **Path A: AA API token.** If AA offers a documented token-based API, wire `AnnaArchiveScraper` to authenticate via the token. Cleanest approach. AA token stored in `TANKOBAN_AA_TOKEN` env var (set by user, not committed).
- **Path B: Extended cookie harvester.** If `CloudflareCookieHarvester` can be extended to handle AA's CAPTCHA stage, do so. The harvester runs once on first AA query of a session, caches the cookie, reuses it for subsequent queries.
- **Path C: Defer AA to v1.1.** If neither path is feasible without major investment, keep AA disabled-at-construction in v1 (today's state); plan AA re-enable for v1.1 follow-on. Note: this reduces v1 source coverage (LibGen + Tankorent only); document in spec §8 deferred table.

Path C is the safe fallback. Path A is the dream. Path B is between.

- [ ] **Step 4: Commit the audit file**

```
git add agents/audits/aa_captcha_investigation_2026-05-20.md
git commit -m "BOOKS_STREMIO_PIVOT P4.1: AA captcha investigation + approach decision"
```

- [ ] **Step 5: Post a chat.md observation flagging the decision**

Append to `agents/chat.md`:
```
Agent 2 → brotherhood (2026-05-20 ~XX:XXpm): AA captcha investigation closed.
Picked Path [A/B/C] for v1. Rationale: <one sentence>. Affects subsequent Phase
4 tasks: <task list>. Full decision record at
agents/audits/aa_captcha_investigation_2026-05-20.md.
```

---

### Task 4.2: HELP-request to Agent 4 — Tankorent cross-coordination

**Files:**
- Modify: `agents/HELP.md` (open new HELP request)

This is a process task. It opens a HELP request to Agent 4 (Tankorent owner post-4B-departure) for two specific items needed by Phase 4 + Phase 8.

- [ ] **Step 1: Compose the HELP request**

Edit `agents/HELP.md`. Replace the empty template's `STATUS: NO OPEN REQUEST` block with:

```markdown
## HELP REQUEST — STATUS: OPEN
From: Agent 2 (Book Reader + TankoLibrary)
To: Agent 4 (Stream + Tankorent)

**Context:** BOOKS_STREMIO_PIVOT arc (spec at docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md, plan at docs/superpowers/plans/2026-05-20-books-stremio-pivot.md). Spec locks Tankorent as one of three v1 sources for [Search for downloads]. Hemanth-verbatim 2026-05-20: "Tankorent search (especially piratesbay) produces all kinds of book results."

**Asks (both gated by your sign-off):**

1. **Book-category query filter on Tankorent search.** When TankoLibrary fires a query against the Tankorent search wrapper, we want results filtered to the "Books" category (and equivalent across other indexers — Pirate Bay has a Books category, ExtraTorrents has Books, RuTracker has dedicated forums). What's the cleanest API surface for passing a category filter through your existing search call? Add a parameter to the existing search method, or a new category-aware variant?

2. **Magnet→Books-library-path handoff.** When the picker (Phase 8) selects a Tankorent torrent for a book, we need the torrent to download, extract the EPUB/PDF/MOBI file inside, and move it to the Books root folder so BooksScanner.validateAll() picks it up via the catalogue record's filePath. Options I've considered:
    - (a) Extend BookDownloader with a magnet-source variant that uses TorrentClient::addTorrent → completion-watch → file-extraction-from-archive → move-to-Books-root.
    - (b) New helper class TankorentBookDownloader that owns the magnet→file pipeline, BookDownloader unchanged.
    - (c) Some pattern you'd prefer that respects TorrentClient's ownership invariants.

**What I'd like from you:**
- A short reply naming your preferred API surface for (1) and your preferred shim pattern for (2). Both could land in the same PR if convenient; I can do the actual wiring once you've signed off the shape.
- If you'd rather not get pinged on this during your current TORRENT_PERSISTENCE_COLLAPSE work, set the priority and I'll wait.

**Why I'm asking instead of just shipping:** Tankorent is your domain post-4B-departure, the magnet→file pipeline touches TorrentClient internals you own, and I want to honor your "respect the substrate" discipline.

— Agent 2 (Book Reader + TankoLibrary), 2026-05-20
```

- [ ] **Step 2: Commit the HELP request**

```
git add agents/HELP.md
git commit -m "BOOKS_STREMIO_PIVOT P4.2: HELP request to Agent 4 for Tankorent cross-coordination"
```

- [ ] **Step 3: Post a chat.md observation**

Append to `agents/chat.md`:
```
Agent 2 → Agent 4 (HELP request opened, 2026-05-20 ~XX:XXpm): BOOKS_STREMIO_PIVOT
needs your sign-off on (1) book-category query filter API and (2) magnet→Books-
library-path shim pattern before I can wire Tankorent into the [Search for
downloads] picker. Full request at agents/HELP.md. No urgency — slot it after
TORRENT_PERSISTENCE_COLLAPSE if that's where your head is. — Agent 2
```

- [ ] **Step 4: Pause / continue downstream Phase 4 tasks pending response**

If Agent 4 responds promptly (within the same wake), continue with Tasks 4.4-4.6 using their picked shapes. If Agent 4 is unavailable, Agent 2 may proceed with Task 4.3 (AA re-enable, independent of Tankorent), Task 4.5 (BookSearchAggregator skeleton, can be skeleton-only until Tankorent shape is known), and skip Tasks 4.4 + 4.6 until Agent 4 weighs in.

---

### Task 4.3: Re-enable Anna's Archive (per Path picked in Task 4.1)

**Files:**
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (ctor — re-enable AA scraper push)
- Modify: `src/core/book/AnnaArchiveScraper.{h,cpp}` (apply chosen captcha approach)

Branches on the Task 4.1 decision.

**If Path A (API token) picked:**

- [ ] **Step 1: Add `TANKOBAN_AA_TOKEN` env-var lookup**

Edit `src/core/book/AnnaArchiveScraper.cpp`. In the constructor (or class-static init), read the env var:

```cpp
m_apiToken = qEnvironmentVariable("TANKOBAN_AA_TOKEN");
```

Update the search HTTP-builder to attach the token as an `Authorization` header on outgoing requests:

```cpp
QNetworkRequest req(searchUrl);
if (!m_apiToken.isEmpty()) {
    req.setRawHeader("Authorization",
                     ("Bearer " + m_apiToken).toUtf8());
}
```

- [ ] **Step 2: Re-enable AA push in TankoLibraryPage ctor**

Edit `src/ui/pages/TankoLibraryPage.cpp`. Locate the constructor's scraper list initialization (search for `m_scrapersBooks.push_back`). Today AA is commented out per M2.2 captcha finding. Uncomment + add:

```cpp
m_scrapersBooks.push_back(new AnnaArchiveScraper(this));
m_scrapersBooks.push_back(new LibGenScraper(this));
```

- [ ] **Step 3: Build verification**

Run `build_check.bat`. Expected: `BUILD OK`. Smoke-launch with `TANKOBAN_AA_TOKEN=<test-token>` set in build_and_run.bat; verify TankoLibraryPage now queries AA on a test search.

- [ ] **Step 4: Commit**

```
git add src/core/book/AnnaArchiveScraper.h src/core/book/AnnaArchiveScraper.cpp src/ui/pages/TankoLibraryPage.cpp
git commit -m "BOOKS_STREMIO_PIVOT P4.3 (Path A): AA token-authed; re-enable in TankoLibrary"
```

**If Path B (extended cookie harvester) picked:**

- [ ] **Step 1: Extend `CloudflareCookieHarvester` to handle AA's captcha stage**

Edit `src/core/indexers/CloudflareCookieHarvester.{h,cpp}`. Add a new method:

```cpp
QString harvestForAnnasArchive(const QUrl& aaUrl);
```

Internally it walks the same Cloudflare interstitial pattern used today for Tankorent, but starts at `https://annas-archive.org/` instead of an indexer base URL. Cache the resulting `cf_clearance` cookie per-session.

The harvest may fail (CAPTCHA can't be auto-solved); on failure, log + return empty + downstream `AnnaArchiveScraper` treats the source as offline for the session.

- [ ] **Step 2: Wire `AnnaArchiveScraper` to the harvester on first query**

Edit `src/core/book/AnnaArchiveScraper.cpp`. Before the first network request per session, call `CloudflareCookieHarvester::harvestForAnnasArchive(...)`. Cache the cookie in the scraper's `QNetworkAccessManager` cookie jar; subsequent requests reuse it.

- [ ] **Step 3 + 4: Same as Path A.**

**If Path C (defer to v1.1) picked:**

- [ ] **Step 1: Document the defer in the spec**

Edit `docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md` §8 (Deferred table). Add a new row:

```
| **Re-enable Anna's Archive** | Captcha approach not feasible for v1 (per agents/audits/aa_captcha_investigation_2026-05-20.md §3). | Path A/B becomes feasible OR Hemanth ratifies a manual user-token approach. |
```

- [ ] **Step 2: Update Phase 4 + Phase 8 to skip AA**

Edit `docs/superpowers/plans/2026-05-20-books-stremio-pivot.md` Tasks 4.5 + Phase 8 picker tasks to note "AA section hidden / disabled when Path C in effect."

- [ ] **Step 3: Commit**

```
git add docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md docs/superpowers/plans/2026-05-20-books-stremio-pivot.md agents/audits/aa_captcha_investigation_2026-05-20.md
git commit -m "BOOKS_STREMIO_PIVOT P4.3 (Path C): AA captcha defer to v1.1; spec + plan updated"
```

---

### Task 4.4: Tankorent book-category query filter (after Agent 4 sign-off)

**Blocking precondition:** Task 4.2 HELP request resolved with Agent 4's chosen API shape for the book-category filter.

**Files (placeholders — final paths depend on Agent 4's API surface):**
- Modify: `src/core/torrent/TorrentClient.{h,cpp}` OR `src/core/indexers/TorrentIndexer.{h,cpp}` per Agent 4's pick
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (push Tankorent scraper into `m_scrapersBooks`)
- Create: `src/core/book/TankorentBookScraper.{h,cpp}` — `BookScraper` implementation that delegates to the Tankorent search infrastructure

- [ ] **Step 1: Implement Agent 4's signed-off API shape**

Apply the API surface change Agent 4 specified in their HELP.md reply. If Agent 4 picked "add `category` parameter to existing search method": modify the relevant TorrentClient / TorrentIndexer signature + propagate the category through all callsites that currently pass nothing.

- [ ] **Step 2: Write `TankorentBookScraper`**

Create `src/core/book/TankorentBookScraper.h`:

```cpp
#pragma once

#include "BookScraper.h"

class TorrentClient;

// BookScraper implementation that fans out queries to Tankorent's indexer
// stack filtered to the "Books" category. Returns one BookResult per torrent
// hit, with format inferred from filename (if available in the indexer's
// search response).
//
// Tankorent owner (Agent 4) signed off on the API surface in Phase 4 HELP
// resolution (chat.md ~2026-05-20).
class TankorentBookScraper : public BookScraper
{
    Q_OBJECT
public:
    explicit TankorentBookScraper(TorrentClient* client, QObject* parent = nullptr);

    QString sourceId() const override { return QStringLiteral("tankorent"); }
    QString sourceName() const override { return QStringLiteral("Tankorent (torrents)"); }
    void search(const QString& query, int limit) override;
    void resolveDownload(const QString& torrentId) override; // returns magnet URI

signals:
    void resolveSucceeded(const QString& torrentId, const QString& magnetUri);
    void resolveFailed(const QString& torrentId, const QString& error);

private:
    TorrentClient* m_client;
};
```

Create `src/core/book/TankorentBookScraper.cpp` with the search wrapper. The exact implementation depends on Agent 4's signed-off API; placeholder structure:

```cpp
#include "TankorentBookScraper.h"
#include "core/torrent/TorrentClient.h"  // exact path TBD by Agent 4

TankorentBookScraper::TankorentBookScraper(TorrentClient* client, QObject* parent)
    : BookScraper(parent), m_client(client) {}

void TankorentBookScraper::search(const QString& query, int limit)
{
    // Delegate to TorrentClient's search with category="Books" filter.
    // Agent 4's signed-off API determines the exact call shape.
    m_client->searchWithCategory(query, QStringLiteral("Books"), limit);

    // Wire TorrentClient's results back through BookScraper's searchFinished
    // signal — map torrent results to BookResult rows. Format inferred from
    // filename ('.epub' / '.pdf' / '.mobi' suffix). seeders + leechers stashed
    // into BookResult.fileSize as "X seeders · Y MB" (or a dedicated field
    // added to BookResult if Agent 4 prefers explicit modeling).
}

void TankorentBookScraper::resolveDownload(const QString& torrentId)
{
    // Resolve magnet URI; emit resolveSucceeded(torrentId, magnet).
}
```

- [ ] **Step 3: Push the scraper into TankoLibrary's BookScraper list**

Edit `src/ui/pages/TankoLibraryPage.cpp` ctor. After the LibGen/AA pushes from Task 4.3, add:

```cpp
m_scrapersBooks.push_back(new TankorentBookScraper(m_torrentClient, this));
```

- [ ] **Step 4: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```
git add src/core/book/TankorentBookScraper.h src/core/book/TankorentBookScraper.cpp src/ui/pages/TankoLibraryPage.cpp src/core/torrent/TorrentClient.h src/core/torrent/TorrentClient.cpp
git commit -m "BOOKS_STREMIO_PIVOT P4.4: TankorentBookScraper + book-category filter (Agent 4 API per HELP)"
```

---

### Task 4.5: `BookDownloader` magnet-source variant

**Files:**
- Modify: `src/core/book/BookDownloader.{h,cpp}`

The existing `BookDownloader` HTTP-streams files from LibGen / AA direct URLs. Tankorent's results are magnet URIs — needs a separate path: add magnet → TorrentClient::addTorrent → completion-watch → file-extraction-from-archive (often torrents wrap the EPUB in a folder) → move to Books root folder. The exact shape depends on Agent 4's Task 4.2 sign-off.

- [ ] **Step 1: Add the magnet-variant method to the header**

Edit `src/core/book/BookDownloader.h`. Add (alongside the existing HTTP-streaming method):

```cpp
// Magnet-source variant — used by TankorentBookScraper's resolveDownload path.
// Adds the magnet to TorrentClient, watches for completion, extracts the
// book file (EPUB/PDF/MOBI) from the downloaded payload, and moves it to
// the Books root folder. Emits the same downloadComplete / downloadFailed
// signals as the HTTP variant.
//
// Per Agent 4's Task 4.2 sign-off: <fill in API per Agent 4's reply>.
void downloadFromMagnet(const QString& md5, const QString& magnetUri,
                        const QString& expectedFormat);
```

- [ ] **Step 2: Implement the magnet variant**

Edit `src/core/book/BookDownloader.cpp`. Add the method body:

```cpp
void BookDownloader::downloadFromMagnet(const QString& md5,
                                        const QString& magnetUri,
                                        const QString& expectedFormat)
{
    // Hand off to TorrentClient. Connect to its completion signal scoped
    // to this magnet's infoHash. On completion: locate file matching
    // expectedFormat in the payload tree; move it to the Books root.
    auto* handle = m_torrentClient->addTorrent(magnetUri, m_booksRootDir);
    connect(handle, &TorrentHandle::completed,
            this, [this, md5, expectedFormat, handle]() {
                const QString matchedFile = locateBookFile(handle->files(),
                                                           expectedFormat);
                if (matchedFile.isEmpty()) {
                    emit downloadFailed(md5,
                        QStringLiteral("No %1 file in payload").arg(expectedFormat));
                    return;
                }
                QString finalPath = moveToBooksRoot(matchedFile);
                emit downloadComplete(md5, finalPath);
            });
    connect(handle, &TorrentHandle::failed,
            this, [this, md5](const QString& err) {
                emit downloadFailed(md5, err);
            });
}
```

Add helper methods `locateBookFile` (walks payload tree, returns first matching-format file) and `moveToBooksRoot` (atomic rename to Books root, returns final path).

- [ ] **Step 3: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```
git add src/core/book/BookDownloader.h src/core/book/BookDownloader.cpp
git commit -m "BOOKS_STREMIO_PIVOT P4.5: BookDownloader magnet-source variant"
```

---

### Task 4.6: `BookSearchAggregator` — parallel fan-out across all three sources

**Files:**
- Create: `src/core/book/BookSearchAggregator.h`
- Create: `src/core/book/BookSearchAggregator.cpp`
- Modify: `CMakeLists.txt`

The picker-side engine. Owned by the picker widget (Phase 8). Given a `BookCatalogueResult`, queries LibGen + AA + Tankorent in parallel, streams results to the picker as each source returns.

- [ ] **Step 1: Write the header**

Create `src/core/book/BookSearchAggregator.h`:

```cpp
#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "BookResult.h"
#include "BookCatalogueResult.h"

class BookScraper;

// Picker-side aggregator. Given a BookCatalogueResult (the user's pick from
// the catalogue), fans out parallel queries to LibGen + Anna's Archive +
// Tankorent. Each source's results stream into the picker independently —
// the picker UI shows three vertical sections with per-source spinners that
// stop independently as each source returns (or fails).
//
// Search query strategy: probe by ISBN first (when available, most precise);
// fall back to "title author" string concatenation if no ISBN on the catalogue
// record. Per-source query strings are identical (the source-side scrapers
// handle their own escaping / encoding).
class BookSearchAggregator : public QObject
{
    Q_OBJECT
public:
    explicit BookSearchAggregator(const QList<BookScraper*>& scrapers,
                                  QObject* parent = nullptr);

    // Fires queries against every scraper in m_scrapers. Each scraper's
    // results flow back as sourceResultsReady or sourceFailed signals.
    void searchFor(const BookCatalogueResult& target);

signals:
    // Per-source results (the picker handles streaming UI per section).
    void sourceResultsReady(const QString& sourceId,
                            const QList<BookResult>& results);
    void sourceFailed(const QString& sourceId, const QString& error);

    // Convenience: fires when EVERY source has completed (success or fail).
    // Picker can use this to enable a "no sources have any results — close
    // picker?" banner if all sections came back empty.
    void allSourcesCompleted();

private:
    void onScraperSearchFinished(const QString& sourceId,
                                 const QList<BookResult>& results);
    void onScraperError(const QString& sourceId, const QString& error);
    void checkAllCompleted();

    QList<BookScraper*> m_scrapers;
    QSet<QString>       m_pending; // sourceIds with in-flight queries
};
```

- [ ] **Step 2: Write the implementation**

Create `src/core/book/BookSearchAggregator.cpp`:

```cpp
#include "BookSearchAggregator.h"

#include "BookScraper.h"

BookSearchAggregator::BookSearchAggregator(const QList<BookScraper*>& scrapers,
                                           QObject* parent)
    : QObject(parent), m_scrapers(scrapers)
{
    for (auto* s : m_scrapers) {
        connect(s, &BookScraper::searchFinished,
                this, [this, s](const QList<BookResult>& results) {
                    onScraperSearchFinished(s->sourceId(), results);
                });
        connect(s, &BookScraper::errorOccurred,
                this, [this, s](const QString& err) {
                    onScraperError(s->sourceId(), err);
                });
    }
}

void BookSearchAggregator::searchFor(const BookCatalogueResult& target)
{
    m_pending.clear();
    QString query;
    if (!target.isbn.isEmpty()) {
        // Use first ISBN (multi-ISBN joined with comma in BookCatalogueResult).
        query = target.isbn.section(QLatin1Char(','), 0, 0).trimmed();
    } else {
        query = target.title + QLatin1Char(' ') + target.author;
    }

    constexpr int kPerSourceLimit = 10;
    for (auto* s : m_scrapers) {
        m_pending.insert(s->sourceId());
        s->search(query, kPerSourceLimit);
    }
}

void BookSearchAggregator::onScraperSearchFinished(const QString& sourceId,
                                                   const QList<BookResult>& results)
{
    m_pending.remove(sourceId);
    emit sourceResultsReady(sourceId, results);
    checkAllCompleted();
}

void BookSearchAggregator::onScraperError(const QString& sourceId, const QString& error)
{
    m_pending.remove(sourceId);
    emit sourceFailed(sourceId, error);
    checkAllCompleted();
}

void BookSearchAggregator::checkAllCompleted()
{
    if (m_pending.isEmpty()) emit allSourcesCompleted();
}
```

- [ ] **Step 3: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/core/book/BookSearchAggregator.h` to `HEADERS`.
- Add `src/core/book/BookSearchAggregator.cpp` to `SOURCES`.

- [ ] **Step 4: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```
git add src/core/book/BookSearchAggregator.h src/core/book/BookSearchAggregator.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P4.6: BookSearchAggregator (parallel fan-out picker engine)"
```

---

**End of Phase 4.** At this point you have:
- AA captcha approach picked + documented + applied (Path A/B/C per Task 4.1)
- Tankorent integration signed off by Agent 4 + applied (Task 4.4) — or skipped if Agent 4 blocked
- `BookDownloader::downloadFromMagnet` variant (Task 4.5)
- `BookSearchAggregator` — picker-side parallel fan-out engine (Task 4.6)

Main app build still GREEN. Source layer now exposes a unified picker-side API consumable by Phase 8 (BookSourcePicker).

---

## Phase 5 — BooksTankoLibrarySearchWidget (search-takeover view)

Forked from `StreamSearchWidget` (`src/ui/pages/stream/StreamSearchWidget.h`, 95 lines). The substitution map:

| Stream concept | Books-mode equivalent |
|---|---|
| `MetaItemPreview` | `BookCatalogueResult` |
| `MetaAggregator` | `BookCatalogueAggregator` |
| `StreamLibrary` | `BooksCatalogueLibraryStore` |
| `metaActivated(MetaItemPreview)` signal | `resultActivated(BookCatalogueResult)` signal |
| Two sections "Movies / Series" | Two sections **Series / Books** (Series first — multi-book is the more compelling unit; spec §3.5) |
| `m_moviesStrip` + `m_seriesStrip` | `m_seriesStrip` + `m_booksStrip` |
| `m_moviesOverflow` + `m_seriesOverflow` | `m_seriesOverflow` + `m_booksOverflow` |
| Poster download via QNAM | Same; lazy-fetch from `BookCatalogueResult::coverUrl` |
| `kInitialCap = 6` | Same |
| `m_previewsById` cache | `m_resultsByCatalogueId` cache |

### Task 5.1: Class header

**Files:**
- Create: `src/ui/pages/books/BooksTankoLibrarySearchWidget.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/books/BooksTankoLibrarySearchWidget.h`:

```cpp
#pragma once

#include <QHash>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>

#include "core/book/BookCatalogueResult.h"
#include "core/book/SeriesDetector.h"

class BookCatalogueAggregator;
class BooksCatalogueLibraryStore;
class TileStrip;
class TileCard;
class QNetworkAccessManager;

// Search-takeover view for Books mode.
//
// Forked from src/ui/pages/stream/StreamSearchWidget.h (Stream blueprint).
// Two sections, Series first then Books, per spec §3.5 — Series is the more
// compelling unit when both are present (the user typed a series query like
// "stormlight archive" and wants the series tile up top).
//
// Result flow:
//   1. BooksPage shows this widget when user types into search bar + hits Enter.
//   2. searchFor(query) called → BookCatalogueAggregator::query(query) fires.
//   3. aggregator emits aggregateReady(seriesGroups, standalones) → populate
//      m_seriesStrip + m_booksStrip; download covers lazily.
//   4. User clicks a tile → emit resultActivated(BookCatalogueResult).
//   5. BooksPage routes to BooksTankoLibrarySeriesDetailView (if isSeries)
//      or BooksTankoLibraryDetailView (movie-shape).
class BooksTankoLibrarySearchWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BooksTankoLibrarySearchWidget(BookCatalogueAggregator* aggregator,
                                           BooksCatalogueLibraryStore* libraryStore,
                                           QWidget* parent = nullptr);

    void searchFor(const QString& query);

signals:
    void backRequested();
    void libraryChanged();
    // The tile click handler emits with the full BookCatalogueResult so the
    // routing layer can decide series vs movie shape via result.isSeries
    // without re-querying the catalogue.
    void resultActivated(const BookCatalogueResult& result);

private:
    void buildUI();
    void clearResults();
    void onAggregateReady(const QString& query,
                          const QList<SeriesDetector::SeriesGroup>& seriesGroups,
                          const QList<BookCatalogueResult>& standalones);
    void onAggregateFailed(const QString& query, const QString& error);

    void addSeriesCard(const SeriesDetector::SeriesGroup& group);
    void addBookCard(const BookCatalogueResult& result);
    void downloadCover(const QString& catalogueId, const QString& coverUrl, TileCard* card);
    void updateInLibraryBadge(TileCard* card);
    void refreshAllBadges();

    void revealSeriesOverflow();
    void revealBooksOverflow();

    static constexpr int kInitialCap = 6;

    BookCatalogueAggregator*    m_aggregator;
    BooksCatalogueLibraryStore* m_libraryStore;
    QNetworkAccessManager*      m_nam;

    // UI
    QPushButton* m_backBtn      = nullptr;
    QLabel*      m_statusLabel  = nullptr;
    QScrollArea* m_scroll       = nullptr;
    QLabel*      m_seriesHeader   = nullptr;
    TileStrip*   m_seriesStrip    = nullptr;
    QPushButton* m_seriesShowMore = nullptr;
    QLabel*      m_booksHeader    = nullptr;
    TileStrip*   m_booksStrip     = nullptr;
    QPushButton* m_booksShowMore  = nullptr;
    QString      m_currentQuery;

    QList<SeriesDetector::SeriesGroup> m_seriesOverflow;
    QList<BookCatalogueResult>         m_booksOverflow;

    QString m_posterCacheDir;

    QHash<QString, BookCatalogueResult> m_resultsByCatalogueId;
    QList<TileCard*> m_tiles;
};
```

- [ ] **Step 2: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/ui/pages/books/BooksTankoLibrarySearchWidget.h` to `HEADERS`.

- [ ] **Step 3: Build to verify header compiles**

This is a header-only step. `BUILD` won't link successfully until the .cpp lands in Task 5.2, but configure should not error.

Run:
```
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release
```
Expected: configures clean.

- [ ] **Step 4: Commit**

```
git add src/ui/pages/books/BooksTankoLibrarySearchWidget.h CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P5.1: BooksTankoLibrarySearchWidget header (forked from StreamSearchWidget)"
```

---

### Task 5.2: Class implementation — buildUI + buildSection wiring

**Files:**
- Create: `src/ui/pages/books/BooksTankoLibrarySearchWidget.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the implementation skeleton (buildUI + state holders)**

Create `src/ui/pages/books/BooksTankoLibrarySearchWidget.cpp`:

```cpp
#include "BooksTankoLibrarySearchWidget.h"

#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QScrollArea>
#include <QStandardPaths>
#include <QDir>
#include <QVBoxLayout>

#include "core/book/BookCatalogueAggregator.h"
#include "core/book/BooksCatalogueLibraryStore.h"
#include "ui/widgets/TileStrip.h"
#include "ui/widgets/TileCard.h"

BooksTankoLibrarySearchWidget::BooksTankoLibrarySearchWidget(
    BookCatalogueAggregator* aggregator,
    BooksCatalogueLibraryStore* libraryStore,
    QWidget* parent)
    : QWidget(parent),
      m_aggregator(aggregator),
      m_libraryStore(libraryStore),
      m_nam(new QNetworkAccessManager(this))
{
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                       + QStringLiteral("/book_catalogue_covers");
    QDir().mkpath(m_posterCacheDir);

    buildUI();

    connect(m_aggregator, &BookCatalogueAggregator::aggregateReady,
            this, &BooksTankoLibrarySearchWidget::onAggregateReady);
    connect(m_aggregator, &BookCatalogueAggregator::aggregateFailed,
            this, &BooksTankoLibrarySearchWidget::onAggregateFailed);
    connect(m_libraryStore, &BooksCatalogueLibraryStore::recordsChanged,
            this, &BooksTankoLibrarySearchWidget::refreshAllBadges);
}

void BooksTankoLibrarySearchWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 16, 24, 16);
    root->setSpacing(12);

    // Back row.
    auto* backRow = new QHBoxLayout;
    m_backBtn = new QPushButton(QStringLiteral("← Back"), this);
    m_backBtn->setFlat(true);
    connect(m_backBtn, &QPushButton::clicked,
            this, &BooksTankoLibrarySearchWidget::backRequested);
    backRow->addWidget(m_backBtn);
    backRow->addStretch();
    root->addLayout(backRow);

    // Status label (loading / no results / error message).
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #9090a0; font-size: 13px;"));
    root->addWidget(m_statusLabel);

    // Scroll area containing the two sections.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    auto* scrollContent = new QWidget;
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(20);

    // Series section.
    m_seriesHeader = new QLabel(QStringLiteral("Series"), scrollContent);
    m_seriesHeader->setStyleSheet(
        QStringLiteral("color:#e0e0e8; font-size:15px; text-transform:uppercase; letter-spacing:0.5px;"));
    m_seriesHeader->hide();
    scrollLayout->addWidget(m_seriesHeader);
    m_seriesStrip = new TileStrip(scrollContent);
    m_seriesStrip->hide();
    scrollLayout->addWidget(m_seriesStrip);
    m_seriesShowMore = new QPushButton(QStringLiteral("Show all series"), scrollContent);
    m_seriesShowMore->hide();
    connect(m_seriesShowMore, &QPushButton::clicked,
            this, &BooksTankoLibrarySearchWidget::revealSeriesOverflow);
    scrollLayout->addWidget(m_seriesShowMore);

    // Books section.
    m_booksHeader = new QLabel(QStringLiteral("Books"), scrollContent);
    m_booksHeader->setStyleSheet(m_seriesHeader->styleSheet());
    m_booksHeader->hide();
    scrollLayout->addWidget(m_booksHeader);
    m_booksStrip = new TileStrip(scrollContent);
    m_booksStrip->hide();
    scrollLayout->addWidget(m_booksStrip);
    m_booksShowMore = new QPushButton(QStringLiteral("Show all books"), scrollContent);
    m_booksShowMore->hide();
    connect(m_booksShowMore, &QPushButton::clicked,
            this, &BooksTankoLibrarySearchWidget::revealBooksOverflow);
    scrollLayout->addWidget(m_booksShowMore);

    scrollLayout->addStretch();
    m_scroll->setWidget(scrollContent);
    root->addWidget(m_scroll, /*stretch*/ 1);
}
```

- [ ] **Step 2: Add the search + clearResults + onAggregateReady methods**

Append to `src/ui/pages/books/BooksTankoLibrarySearchWidget.cpp`:

```cpp
void BooksTankoLibrarySearchWidget::searchFor(const QString& query)
{
    if (query.trimmed().isEmpty()) return;
    m_currentQuery = query;
    clearResults();
    m_statusLabel->setText(QStringLiteral("Searching for \"%1\"...").arg(query));
    m_aggregator->query(query);
}

void BooksTankoLibrarySearchWidget::clearResults()
{
    m_seriesStrip->clear();
    m_booksStrip->clear();
    m_seriesHeader->hide();
    m_booksHeader->hide();
    m_seriesStrip->hide();
    m_booksStrip->hide();
    m_seriesShowMore->hide();
    m_booksShowMore->hide();
    m_seriesOverflow.clear();
    m_booksOverflow.clear();
    m_resultsByCatalogueId.clear();
    m_tiles.clear();
}

void BooksTankoLibrarySearchWidget::onAggregateReady(
    const QString& query,
    const QList<SeriesDetector::SeriesGroup>& seriesGroups,
    const QList<BookCatalogueResult>& standalones)
{
    if (query != m_currentQuery) return; // stale; user typed something newer

    if (seriesGroups.isEmpty() && standalones.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No results for \"%1\".").arg(query));
        return;
    }
    m_statusLabel->clear();

    // Populate Series section.
    if (!seriesGroups.isEmpty()) {
        m_seriesHeader->show();
        m_seriesStrip->show();
        int shown = 0;
        for (const auto& g : seriesGroups) {
            if (shown < kInitialCap) {
                addSeriesCard(g);
                ++shown;
            } else {
                m_seriesOverflow.append(g);
            }
        }
        if (!m_seriesOverflow.isEmpty()) {
            m_seriesShowMore->setText(
                QStringLiteral("Show %1 more series").arg(m_seriesOverflow.size()));
            m_seriesShowMore->show();
        }
    }

    // Populate Books section.
    if (!standalones.isEmpty()) {
        m_booksHeader->show();
        m_booksStrip->show();
        int shown = 0;
        for (const auto& r : standalones) {
            if (shown < kInitialCap) {
                addBookCard(r);
                ++shown;
            } else {
                m_booksOverflow.append(r);
            }
        }
        if (!m_booksOverflow.isEmpty()) {
            m_booksShowMore->setText(
                QStringLiteral("Show %1 more books").arg(m_booksOverflow.size()));
            m_booksShowMore->show();
        }
    }
}

void BooksTankoLibrarySearchWidget::onAggregateFailed(const QString& query,
                                                      const QString& error)
{
    if (query != m_currentQuery) return;
    m_statusLabel->setText(
        QStringLiteral("Search failed: %1").arg(error));
}

void BooksTankoLibrarySearchWidget::revealSeriesOverflow()
{
    for (const auto& g : m_seriesOverflow) addSeriesCard(g);
    m_seriesOverflow.clear();
    m_seriesShowMore->hide();
}

void BooksTankoLibrarySearchWidget::revealBooksOverflow()
{
    for (const auto& r : m_booksOverflow) addBookCard(r);
    m_booksOverflow.clear();
    m_booksShowMore->hide();
}
```

- [ ] **Step 3: Add the tile-add methods (series + book) + cover download + badge refresh**

Append:

```cpp
void BooksTankoLibrarySearchWidget::addSeriesCard(
    const SeriesDetector::SeriesGroup& group)
{
    if (group.books.isEmpty()) return;
    // Synthesize a series-tile BookCatalogueResult — the cover is book 1's cover.
    BookCatalogueResult synth = group.books.first();
    synth.title = group.seriesName;
    synth.isSeries = true;
    synth.seriesName = group.seriesName;
    synth.seriesPosition = 0; // 0 == the series itself, not a member
    synth.seriesTotal = group.books.size();
    // CatalogueId for the series tile uses the first member's workId-based id
    // with a ":series" suffix so the routing layer can recognize "this is a
    // series tile, open the series detail view" vs "this is a book tile."
    synth.catalogueId = synth.catalogueId + QStringLiteral(":series");

    auto* card = new TileCard(m_seriesStrip);
    card->setTitle(group.seriesName);
    card->setSubtitle(QStringLiteral("%1 books · %2")
                          .arg(group.books.size()).arg(group.author));

    m_resultsByCatalogueId.insert(synth.catalogueId, synth);
    m_tiles.append(card);
    m_seriesStrip->addCard(card);

    if (!synth.coverUrl.isEmpty()) {
        downloadCover(synth.catalogueId, synth.coverUrl, card);
    }
    // Click → emit resultActivated with the synthesized series-tile result.
    connect(card, &TileCard::clicked, this, [this, synth]() {
        emit resultActivated(synth);
    });
    updateInLibraryBadge(card);
}

void BooksTankoLibrarySearchWidget::addBookCard(const BookCatalogueResult& result)
{
    auto* card = new TileCard(m_booksStrip);
    card->setTitle(result.title);
    card->setSubtitle(result.author);

    m_resultsByCatalogueId.insert(result.catalogueId, result);
    m_tiles.append(card);
    m_booksStrip->addCard(card);

    if (!result.coverUrl.isEmpty()) {
        downloadCover(result.catalogueId, result.coverUrl, card);
    }
    connect(card, &TileCard::clicked, this, [this, result]() {
        emit resultActivated(result);
    });
    updateInLibraryBadge(card);
}

void BooksTankoLibrarySearchWidget::downloadCover(const QString& catalogueId,
                                                  const QString& coverUrl,
                                                  TileCard* card)
{
    // Check on-disk cache first.
    const QString cachePath = m_posterCacheDir + QLatin1Char('/')
                              + catalogueId.toUtf8().toBase64(QByteArray::OmitTrailingEquals)
                              + QStringLiteral(".jpg");
    if (QFileInfo::exists(cachePath)) {
        QPixmap pix(cachePath);
        if (!pix.isNull()) {
            card->setCover(pix);
            return;
        }
    }

    // Network fetch.
    QNetworkRequest req((QUrl(coverUrl)));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    auto* reply = m_nam->get(req);
    QPointer<TileCard> cardGuard(card);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, cachePath, cardGuard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        QPixmap pix;
        if (!pix.loadFromData(data)) return;
        QFile out(cachePath);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(data);
            out.close();
        }
        if (cardGuard) cardGuard->setCover(pix);
    });
}

void BooksTankoLibrarySearchWidget::updateInLibraryBadge(TileCard* card)
{
    if (!card) return;
    // For a series tile, the badge lights up if the library has ANY book
    // from the series. For a movie-shape tile, the badge lights up if the
    // library has the catalogueId.
    const QString catalogueId = card->property("catalogueId").toString();
    auto resIt = m_resultsByCatalogueId.constFind(catalogueId);
    if (resIt == m_resultsByCatalogueId.constEnd()) return;
    const auto& r = resIt.value();
    bool inLibrary = false;
    if (r.isSeries) {
        // Strip the ":series" suffix to get the workId-equivalent seriesId.
        const QString seriesId = r.workId; // workId already stripped of suffix
        inLibrary = !m_libraryStore->catalogueIdsForSeries(seriesId).isEmpty();
    } else {
        inLibrary = m_libraryStore->hasRecord(r.catalogueId);
    }
    card->setBadgeVisible(inLibrary);
}

void BooksTankoLibrarySearchWidget::refreshAllBadges()
{
    for (auto* t : m_tiles) updateInLibraryBadge(t);
}
```

- [ ] **Step 4: Register .cpp in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/ui/pages/books/BooksTankoLibrarySearchWidget.cpp` to `SOURCES`.

- [ ] **Step 5: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`. Linker may warn if `TileCard::clicked` isn't a `Q_SIGNAL` in the existing TileCard — verify by reading `src/ui/widgets/TileCard.h` and adapt connect-syntax to whatever signal name the existing primitive exposes.

- [ ] **Step 6: Commit**

```
git add src/ui/pages/books/BooksTankoLibrarySearchWidget.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P5.2: BooksTankoLibrarySearchWidget impl (buildUI + aggregate flow + tile-add + cover cache + badge refresh)"
```

---

**End of Phase 5.** Search-takeover view ships, parses Series + Books sections from `BookCatalogueAggregator`'s output, lazy-fetches covers from Open Library / Google Books URLs into a local cache, and emits `resultActivated(BookCatalogueResult)` on tile click. The widget is fully built but not yet wired into BooksPage's search bar; that happens in Phase 9.

Main app build still GREEN. Search widget can be instantiated standalone for a manual smoke test even before Phase 9 wires it in.

---

## Phase 6 — BooksTankoLibraryDetailView (movie-shape)

Opens when the user clicks a standalone-book tile in the search-takeover view. Lighter fork of `StreamDetailView` — no episode table, no season combo, no bulk-download. Just hero + meta + one action button + synopsis + tags + "Other books by author" scroller.

Layout per the Hemanth-approved spec §5.2 (no mockup needed; Stream-blueprint port suffices).

### Task 6.1: Class header

**Files:**
- Create: `src/ui/pages/books/BooksTankoLibraryDetailView.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/books/BooksTankoLibraryDetailView.h`:

```cpp
#pragma once

#include <QWidget>
#include <QHash>
#include <QString>

#include "core/book/BookCatalogueResult.h"
#include "core/book/CatalogueRecord.h"
#include "core/book/BookResult.h"

class BookCatalogueAggregator;
class BookSearchAggregator;
class BookDownloader;
class BooksCatalogueLibraryStore;
class BookSourcePicker;
class QLabel;
class QPushButton;
class QProgressBar;
class QScrollArea;
class QNetworkAccessManager;

// Detail page for a movie-shape book (standalone novel).
//
// Layout top-to-bottom (per spec §5.2):
//   - Back arrow row
//   - Hero row: cover (160x240) on left + title (large) + author + meta strip
//     (publisher · year · language · pages)
//   - Big purple [Search for downloads] action button. Morphs to progress bar
//     when downloading, then to [Read] when file on disk.
//   - Synopsis paragraph (5-line truncate + "Read more" expand)
//   - Genre / subject tag chips
//   - "Other books by <author>" horizontal scroller (lazy-loaded)
//
// State machine on the action button:
//   Idle:        [Search for downloads ↓]  — click opens picker
//   Searching:   spinner + "Checking sources..."
//   Picking:     picker modal visible; button hidden
//   Downloading: progress bar + percentage + Cancel
//   Done:        [Read] (filled purple)
//
// If the library already has a CatalogueRecord for this catalogueId on open,
// jump straight to Done state.
class BooksTankoLibraryDetailView : public QWidget
{
    Q_OBJECT
public:
    explicit BooksTankoLibraryDetailView(BookCatalogueAggregator* catalogueAggregator,
                                         BookSearchAggregator* searchAggregator,
                                         BookDownloader* downloader,
                                         BooksCatalogueLibraryStore* libraryStore,
                                         QWidget* parent = nullptr);

    void showResult(const BookCatalogueResult& result);

signals:
    void backRequested();
    void readRequested(const QString& catalogueId); // → MainWindow opens BookReader

private:
    enum class ActionState { Idle, Searching, Picking, Downloading, Done };

    void buildUI();
    void paintHero(const BookCatalogueResult& r);
    void paintMetaStrip(const BookCatalogueResult& r);
    void paintSynopsis(const BookCatalogueResult& r);
    void paintGenreTags(const QStringList& genres);
    void clearAuthorScroller();
    void fetchAuthorWorks(const BookCatalogueResult& r);

    void setActionState(ActionState state);
    void onActionClicked();
    void onPickerCancelled();
    void onPickerSelected(const BookResult& selected);
    void onDownloadProgress(const QString& md5, qint64 received, qint64 total);
    void onDownloadComplete(const QString& md5, const QString& filePath);
    void onDownloadFailed(const QString& md5, const QString& reason);

    void onAuthorWorksReady(const QString& authorKey,
                            const QList<BookCatalogueResult>& works);

    BookCatalogueAggregator*    m_catalogueAggregator;
    BookSearchAggregator*       m_searchAggregator;
    BookDownloader*             m_downloader;
    BooksCatalogueLibraryStore* m_libraryStore;
    QNetworkAccessManager*      m_nam;

    BookCatalogueResult m_currentResult;
    ActionState         m_actionState = ActionState::Idle;
    BookSourcePicker*   m_picker = nullptr;

    // UI
    QPushButton*  m_backBtn        = nullptr;
    QLabel*       m_coverLabel     = nullptr;
    QLabel*       m_titleLabel     = nullptr;
    QLabel*       m_authorLabel    = nullptr;
    QLabel*       m_metaLabel      = nullptr;
    QPushButton*  m_actionBtn      = nullptr;
    QProgressBar* m_progressBar    = nullptr;
    QPushButton*  m_cancelBtn      = nullptr;
    QLabel*       m_synopsisLabel  = nullptr;
    QPushButton*  m_readMoreBtn    = nullptr;
    QWidget*      m_tagsRow        = nullptr;
    QScrollArea*  m_authorScroller = nullptr;
    QWidget*      m_authorContent  = nullptr;
    QString       m_currentDownloadMd5;
    bool          m_synopsisExpanded = false;
};
```

- [ ] **Step 2: Register in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/ui/pages/books/BooksTankoLibraryDetailView.h` to `HEADERS`.

- [ ] **Step 3: Commit**

```
git add src/ui/pages/books/BooksTankoLibraryDetailView.h CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P6.1: BooksTankoLibraryDetailView header (movie-shape)"
```

---

### Task 6.2: Class implementation — buildUI + paint methods

**Files:**
- Create: `src/ui/pages/books/BooksTankoLibraryDetailView.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write ctor + buildUI**

Create `src/ui/pages/books/BooksTankoLibraryDetailView.cpp`:

```cpp
#include "BooksTankoLibraryDetailView.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QDir>
#include <QVBoxLayout>

#include "core/book/BookCatalogueAggregator.h"
#include "core/book/BookSearchAggregator.h"
#include "core/book/BookDownloader.h"
#include "core/book/BooksCatalogueLibraryStore.h"
#include "ui/pages/books/BookSourcePicker.h"
#include "ui/widgets/TileStrip.h"
#include "ui/widgets/TileCard.h"

BooksTankoLibraryDetailView::BooksTankoLibraryDetailView(
    BookCatalogueAggregator* catalogueAggregator,
    BookSearchAggregator* searchAggregator,
    BookDownloader* downloader,
    BooksCatalogueLibraryStore* libraryStore,
    QWidget* parent)
    : QWidget(parent),
      m_catalogueAggregator(catalogueAggregator),
      m_searchAggregator(searchAggregator),
      m_downloader(downloader),
      m_libraryStore(libraryStore),
      m_nam(new QNetworkAccessManager(this))
{
    buildUI();

    connect(m_catalogueAggregator, &BookCatalogueAggregator::authorWorksReady,
            this, &BooksTankoLibraryDetailView::onAuthorWorksReady);
    connect(m_downloader, &BookDownloader::downloadProgress,
            this, &BooksTankoLibraryDetailView::onDownloadProgress);
    connect(m_downloader, &BookDownloader::downloadComplete,
            this, &BooksTankoLibraryDetailView::onDownloadComplete);
    connect(m_downloader, &BookDownloader::downloadFailed,
            this, &BooksTankoLibraryDetailView::onDownloadFailed);
}

void BooksTankoLibraryDetailView::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 18, 28, 24);
    root->setSpacing(0);

    // Back row.
    m_backBtn = new QPushButton(QStringLiteral("← Back"), this);
    m_backBtn->setFlat(true);
    m_backBtn->setStyleSheet(QStringLiteral("color: #9090a0; font-size: 13px;"));
    connect(m_backBtn, &QPushButton::clicked,
            this, &BooksTankoLibraryDetailView::backRequested);
    root->addWidget(m_backBtn, 0, Qt::AlignLeft);
    root->addSpacing(18);

    // Hero row: cover + meta column.
    auto* heroRow = new QHBoxLayout;
    heroRow->setSpacing(28);

    m_coverLabel = new QLabel(this);
    m_coverLabel->setFixedSize(160, 240);
    m_coverLabel->setStyleSheet(
        QStringLiteral("background: linear-gradient(160deg,#2d3a55 0%,#5e3a8b 100%); border-radius: 8px;"));
    heroRow->addWidget(m_coverLabel, 0, Qt::AlignTop);

    auto* metaCol = new QVBoxLayout;
    metaCol->setSpacing(6);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet(QStringLiteral("color:#fff; font-size:28px; font-weight:600;"));
    m_titleLabel->setWordWrap(true);
    metaCol->addWidget(m_titleLabel);

    m_authorLabel = new QLabel(this);
    m_authorLabel->setStyleSheet(QStringLiteral("color:#c0a0ff; font-size:15px;"));
    metaCol->addWidget(m_authorLabel);
    metaCol->addSpacing(10);

    m_metaLabel = new QLabel(this);
    m_metaLabel->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:13px;"));
    metaCol->addWidget(m_metaLabel);
    metaCol->addSpacing(16);

    // Action row container (button | progress | cancel — only one visible at a time).
    auto* actionRow = new QHBoxLayout;
    m_actionBtn = new QPushButton(QStringLiteral("Search for downloads ↓"), this);
    m_actionBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #5e3a8b, stop:1 #7b4dba); color:#fff; padding: 12px 22px; "
        "border-radius: 6px; font-size:14px; font-weight:600; }"
        "QPushButton:disabled { opacity: 0.5; }"));
    connect(m_actionBtn, &QPushButton::clicked,
            this, &BooksTankoLibraryDetailView::onActionClicked);
    actionRow->addWidget(m_actionBtn);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setVisible(false);
    actionRow->addWidget(m_progressBar);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelBtn->setFlat(true);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_actionState == ActionState::Downloading && !m_currentDownloadMd5.isEmpty()) {
            m_downloader->cancel(m_currentDownloadMd5);
            setActionState(ActionState::Idle);
        }
    });
    actionRow->addWidget(m_cancelBtn);
    actionRow->addStretch();
    metaCol->addLayout(actionRow);

    heroRow->addLayout(metaCol, 1);
    root->addLayout(heroRow);
    root->addSpacing(24);

    // Synopsis paragraph + Read more.
    m_synopsisLabel = new QLabel(this);
    m_synopsisLabel->setWordWrap(true);
    m_synopsisLabel->setStyleSheet(QStringLiteral("color:#c8c8d0; font-size:14px; line-height:1.55;"));
    root->addWidget(m_synopsisLabel);

    m_readMoreBtn = new QPushButton(QStringLiteral("Read more"), this);
    m_readMoreBtn->setFlat(true);
    m_readMoreBtn->setStyleSheet(QStringLiteral("color:#c0a0ff; text-align:left; padding: 0;"));
    m_readMoreBtn->setVisible(false);
    connect(m_readMoreBtn, &QPushButton::clicked, this, [this]() {
        m_synopsisExpanded = !m_synopsisExpanded;
        m_synopsisLabel->setText(m_currentResult.description);
        m_readMoreBtn->setText(m_synopsisExpanded
                                   ? QStringLiteral("Show less")
                                   : QStringLiteral("Read more"));
        if (!m_synopsisExpanded) paintSynopsis(m_currentResult);
    });
    root->addWidget(m_readMoreBtn, 0, Qt::AlignLeft);
    root->addSpacing(14);

    // Genre tag chips.
    m_tagsRow = new QWidget(this);
    auto* tagsLayout = new QHBoxLayout(m_tagsRow);
    tagsLayout->setContentsMargins(0, 0, 0, 0);
    tagsLayout->setSpacing(8);
    root->addWidget(m_tagsRow);
    root->addSpacing(24);

    // Other books by author scroller.
    auto* authorHeader = new QLabel(QStringLiteral("Other books by author"), this);
    authorHeader->setStyleSheet(QStringLiteral(
        "color:#e0e0e8; font-size:13px; text-transform:uppercase; letter-spacing:0.5px;"));
    root->addWidget(authorHeader);
    m_authorScroller = new QScrollArea(this);
    m_authorScroller->setWidgetResizable(true);
    m_authorScroller->setFrameShape(QFrame::NoFrame);
    m_authorScroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_authorScroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_authorScroller->setFixedHeight(240);
    m_authorContent = new QWidget(m_authorScroller);
    auto* authorLayout = new QHBoxLayout(m_authorContent);
    authorLayout->setContentsMargins(0, 0, 0, 0);
    authorLayout->setSpacing(14);
    m_authorScroller->setWidget(m_authorContent);
    root->addWidget(m_authorScroller);
}
```

- [ ] **Step 2: Add paint methods + showResult entry point**

Append:

```cpp
void BooksTankoLibraryDetailView::showResult(const BookCatalogueResult& result)
{
    m_currentResult = result;
    m_synopsisExpanded = false;

    paintHero(result);
    paintMetaStrip(result);
    paintSynopsis(result);
    paintGenreTags(result.genres);
    clearAuthorScroller();

    // If the library already has this book, jump to Done state immediately.
    if (m_libraryStore->hasRecord(result.catalogueId)) {
        setActionState(ActionState::Done);
    } else {
        setActionState(ActionState::Idle);
    }

    // Fire off author-works fetch for the scroller. Open Library author key
    // is in BookCatalogueResult.workId's parent — we don't track it directly
    // on the POD; for v1, query by author name instead (Open Library accepts
    // /search.json?author=<name> and returns works by that author).
    fetchAuthorWorks(result);
}

void BooksTankoLibraryDetailView::paintHero(const BookCatalogueResult& r)
{
    m_titleLabel->setText(r.title);
    m_authorLabel->setText(r.author);

    if (r.coverUrl.isEmpty()) {
        m_coverLabel->clear();
    } else {
        // Lazy fetch (same pattern as SearchWidget; could share a CoverCache).
        QNetworkRequest req((QUrl(r.coverUrl)));
        req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        auto* reply = m_nam->get(req);
        QPointer<QLabel> guard(m_coverLabel);
        connect(reply, &QNetworkReply::finished, this, [reply, guard]() {
            reply->deleteLater();
            if (!guard) return;
            if (reply->error() != QNetworkReply::NoError) return;
            QPixmap pix;
            if (pix.loadFromData(reply->readAll())) {
                guard->setPixmap(pix.scaled(160, 240,
                                            Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation));
            }
        });
    }
}

void BooksTankoLibraryDetailView::paintMetaStrip(const BookCatalogueResult& r)
{
    QStringList parts;
    if (!r.publisher.isEmpty()) parts << r.publisher;
    if (!r.year.isEmpty())      parts << r.year;
    if (!r.language.isEmpty())  parts << r.language;
    if (!r.pages.isEmpty())     parts << QStringLiteral("%1 pages").arg(r.pages);
    m_metaLabel->setText(parts.join(QStringLiteral(" · ")));
}

void BooksTankoLibraryDetailView::paintSynopsis(const BookCatalogueResult& r)
{
    if (r.description.isEmpty()) {
        m_synopsisLabel->setText(QString());
        m_readMoreBtn->setVisible(false);
        return;
    }
    // Truncate to ~5 lines: roughly 600 chars. If longer, show "Read more".
    constexpr int kTruncateAt = 600;
    if (r.description.size() > kTruncateAt && !m_synopsisExpanded) {
        QString truncated = r.description.left(kTruncateAt).trimmed();
        truncated += QStringLiteral("...");
        m_synopsisLabel->setText(truncated);
        m_readMoreBtn->setVisible(true);
    } else {
        m_synopsisLabel->setText(r.description);
        m_readMoreBtn->setVisible(false);
    }
}

void BooksTankoLibraryDetailView::paintGenreTags(const QStringList& genres)
{
    // Clear existing chips.
    if (auto* lay = m_tagsRow->layout()) {
        while (auto* item = lay->takeAt(0)) {
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
    auto* lay = qobject_cast<QHBoxLayout*>(m_tagsRow->layout());
    if (!lay) return;
    for (const auto& g : genres) {
        auto* chip = new QLabel(g, m_tagsRow);
        chip->setStyleSheet(QStringLiteral(
            "background:#1c1c22; border:1px solid #2a2a32; color:#a8a8b4; "
            "font-size:12px; padding: 4px 11px; border-radius: 999px;"));
        lay->addWidget(chip);
    }
    lay->addStretch();
}

void BooksTankoLibraryDetailView::clearAuthorScroller()
{
    if (auto* lay = m_authorContent->layout()) {
        while (auto* item = lay->takeAt(0)) {
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
}

void BooksTankoLibraryDetailView::fetchAuthorWorks(const BookCatalogueResult& r)
{
    if (r.author.isEmpty()) return;
    // Use the catalogue aggregator's author-works fetch wrapping OpenLibrary's
    // /authors/<id>/works.json. For v1, since we don't store the author key on
    // the BookCatalogueResult, we issue a name-based search and filter results
    // to the same author + isSeries=false on receipt.
    m_catalogueAggregator->fetchAuthorWorks(QString(), r.author);
}

void BooksTankoLibraryDetailView::onAuthorWorksReady(const QString&,
    const QList<BookCatalogueResult>& works)
{
    auto* lay = qobject_cast<QHBoxLayout*>(m_authorContent->layout());
    if (!lay) return;
    int added = 0;
    for (const auto& w : works) {
        if (added >= 10) break;
        if (w.catalogueId == m_currentResult.catalogueId) continue; // skip self
        auto* card = new TileCard(m_authorContent);
        card->setTitle(w.title);
        card->setFixedSize(130, 230);
        // Lazy cover fetch (same pattern).
        if (!w.coverUrl.isEmpty()) {
            QNetworkRequest req((QUrl(w.coverUrl)));
            req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
            auto* reply = m_nam->get(req);
            QPointer<TileCard> guard(card);
            connect(reply, &QNetworkReply::finished, this, [reply, guard]() {
                reply->deleteLater();
                if (!guard) return;
                if (reply->error() != QNetworkReply::NoError) return;
                QPixmap pix;
                if (pix.loadFromData(reply->readAll())) guard->setCover(pix);
            });
        }
        lay->addWidget(card);
        ++added;
    }
    lay->addStretch();
}
```

- [ ] **Step 3: Add action state machine + picker handling + download progress**

Append:

```cpp
void BooksTankoLibraryDetailView::setActionState(ActionState state)
{
    m_actionState = state;
    switch (state) {
    case ActionState::Idle:
        m_actionBtn->setText(QStringLiteral("Search for downloads ↓"));
        m_actionBtn->setEnabled(true);
        m_actionBtn->setVisible(true);
        m_progressBar->setVisible(false);
        m_cancelBtn->setVisible(false);
        break;
    case ActionState::Searching:
        m_actionBtn->setText(QStringLiteral("Checking sources..."));
        m_actionBtn->setEnabled(false);
        m_actionBtn->setVisible(true);
        m_progressBar->setVisible(false);
        m_cancelBtn->setVisible(false);
        break;
    case ActionState::Picking:
        m_actionBtn->setVisible(false);
        m_progressBar->setVisible(false);
        m_cancelBtn->setVisible(false);
        break;
    case ActionState::Downloading:
        m_actionBtn->setVisible(false);
        m_progressBar->setValue(0);
        m_progressBar->setVisible(true);
        m_cancelBtn->setVisible(true);
        break;
    case ActionState::Done:
        m_actionBtn->setText(QStringLiteral("Read"));
        m_actionBtn->setEnabled(true);
        m_actionBtn->setVisible(true);
        m_progressBar->setVisible(false);
        m_cancelBtn->setVisible(false);
        break;
    }
}

void BooksTankoLibraryDetailView::onActionClicked()
{
    if (m_actionState == ActionState::Idle) {
        // Open the picker.
        if (!m_picker) {
            m_picker = new BookSourcePicker(m_searchAggregator, this);
            connect(m_picker, &BookSourcePicker::resultSelected,
                    this, &BooksTankoLibraryDetailView::onPickerSelected);
            connect(m_picker, &BookSourcePicker::cancelled,
                    this, &BooksTankoLibraryDetailView::onPickerCancelled);
        }
        setActionState(ActionState::Picking);
        m_picker->openFor(m_currentResult);
    } else if (m_actionState == ActionState::Done) {
        emit readRequested(m_currentResult.catalogueId);
    }
}

void BooksTankoLibraryDetailView::onPickerCancelled()
{
    setActionState(ActionState::Idle);
}

void BooksTankoLibraryDetailView::onPickerSelected(const BookResult& selected)
{
    m_currentDownloadMd5 = selected.md5;
    setActionState(ActionState::Downloading);
    // Magnet vs HTTP path branches inside BookDownloader. Phase 4.5 wired both.
    if (selected.source == QStringLiteral("tankorent")) {
        m_downloader->downloadFromMagnet(selected.md5, selected.downloadUrl,
                                         selected.format);
    } else {
        m_downloader->downloadFromUrl(selected.md5, selected.downloadUrl,
                                      selected.format);
    }
}

void BooksTankoLibraryDetailView::onDownloadProgress(const QString& md5,
                                                     qint64 received,
                                                     qint64 total)
{
    if (md5 != m_currentDownloadMd5) return;
    if (total > 0) {
        m_progressBar->setValue(int((received * 100) / total));
    }
}

void BooksTankoLibraryDetailView::onDownloadComplete(const QString& md5,
                                                     const QString& filePath)
{
    if (md5 != m_currentDownloadMd5) return;
    m_currentDownloadMd5.clear();
    // Build the CatalogueRecord and persist.
    CatalogueRecord rec;
    rec.catalogueId  = m_currentResult.catalogueId;
    rec.isbn         = m_currentResult.isbn;
    rec.md5          = md5;
    rec.title        = m_currentResult.title;
    rec.author       = m_currentResult.author;
    rec.publisher    = m_currentResult.publisher;
    rec.year         = m_currentResult.year;
    rec.language     = m_currentResult.language;
    rec.description  = m_currentResult.description;
    rec.genres       = m_currentResult.genres;
    rec.coverUrl     = m_currentResult.coverUrl;
    rec.filePath     = filePath;
    rec.format       = QFileInfo(filePath).suffix().toLower();
    rec.addedAt      = QDateTime::currentSecsSinceEpoch();
    m_libraryStore->upsertRecord(rec);
    setActionState(ActionState::Done);
}

void BooksTankoLibraryDetailView::onDownloadFailed(const QString& md5,
                                                   const QString& reason)
{
    if (md5 != m_currentDownloadMd5) return;
    m_currentDownloadMd5.clear();
    setActionState(ActionState::Idle);
    // Show error inline via the picker re-opened (user can pick alternate source).
    // For v1 ship: simple status surface — picker re-opens with an error banner.
    onActionClicked();
}
```

- [ ] **Step 4: Register .cpp in CMakeLists.txt**

Edit `CMakeLists.txt`:
- Add `src/ui/pages/books/BooksTankoLibraryDetailView.cpp` to `SOURCES`.

- [ ] **Step 5: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`. `BookSourcePicker` is forward-declared but defined in Phase 8 — if you're building between phases, comment out the picker-related lines temporarily OR run Phase 8 first if the build needs to be green at every commit.

- [ ] **Step 6: Commit**

```
git add src/ui/pages/books/BooksTankoLibraryDetailView.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P6.2: BooksTankoLibraryDetailView impl (hero + meta + action state-machine + synopsis + tags + author scroller)"
```

---

**End of Phase 6.** Movie-shape detail view fully built. State machine works through Idle → Picking → Downloading → Done with the action button morphing. Library record upsert happens on successful download. The picker dep (`BookSourcePicker`) is forward-declared; Phase 8 ships it.

Main app build is GREEN once Phase 8 lands (or with `BookSourcePicker` stubbed temporarily for incremental commits).

---

## Phase 7 — BooksTankoLibrarySeriesDetailView (series-shape)

The bigger of the two detail views. Matches the Hemanth-approved mockup at `docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html`.

Layout: hero (series cover + title + author + meta) + bulk [Search for downloads — entire series] button + last-run progress strip + synopsis + tag chips + books-in-this-series table (5 row states) + context menu (⋯ dots on section header) + "Other books by author" scroller.

### Task 7.1: Class header

**Files:**
- Create: `src/ui/pages/books/BooksTankoLibrarySeriesDetailView.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/books/BooksTankoLibrarySeriesDetailView.h`:

```cpp
#pragma once

#include <QWidget>
#include <QHash>
#include <QString>
#include <QList>

#include "core/book/BookCatalogueResult.h"
#include "core/book/CatalogueRecord.h"
#include "core/book/BookResult.h"
#include "core/book/SeriesDetector.h"

class BookCatalogueAggregator;
class BookSearchAggregator;
class BookDownloader;
class BooksCatalogueLibraryStore;
class BookSourcePicker;
class QLabel;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QTableWidgetItem;
class QScrollArea;
class QMenu;
class QNetworkAccessManager;

// Series-shape detail page (multi-book series like Stormlight Archive).
//
// Layout per spec §5.3 + mockup at
// docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html
//
//   - Back arrow row
//   - Hero: series cover (book 1's) + series title + author + meta
//     ("5 books · sci-fi/fantasy · ongoing · English")
//   - Bulk [Search for downloads — entire series ↓] action button +
//     "Last run: X/Y found" progress-strip line below
//   - Synopsis (5-line truncate + "Read more")
//   - Genre tag chips
//   - "Books in this series" section header + ⋯ context menu dots
//   - Per-book table: 5 columns (cover thumb, book#/title, year, progress, action)
//   - Each row's action button morphs by per-book state:
//       Idle/NotInLibrary  → [Search for downloads ↓]
//       Downloading        → spinner + "Downloading"
//       Downloaded/Unread  → [Read]
//       Downloaded/Reading → [Read] (progress bar in progress column)
//       NoSourceYet        → italic "no source yet" (greyed)
//   - "Other books by author" horizontal scroller at the bottom
//
// Per-book state derives from BooksCatalogueLibraryStore::hasRecord(bookId)
// plus per-book download state held in m_bookStateById.
class BooksTankoLibrarySeriesDetailView : public QWidget
{
    Q_OBJECT
public:
    explicit BooksTankoLibrarySeriesDetailView(
        BookCatalogueAggregator* catalogueAggregator,
        BookSearchAggregator* searchAggregator,
        BookDownloader* downloader,
        BooksCatalogueLibraryStore* libraryStore,
        QWidget* parent = nullptr);

    void showSeries(const BookCatalogueResult& seriesTile,
                    const SeriesDetector::SeriesGroup& group);

signals:
    void backRequested();
    void readRequested(const QString& catalogueId);

private:
    enum class RowState {
        NotInLibrary,
        Searching,
        Downloading,
        Downloaded,
        NoSourceYet
    };

    struct BookRowState {
        RowState state = RowState::NotInLibrary;
        int      progressPct = 0;       // 0..100 for Downloading, or read% for Downloaded
        QString  pendingDownloadMd5;    // populated during Downloading
    };

    void buildUI();
    void paintHero(const BookCatalogueResult& seriesTile, int bookCount);
    void paintSynopsis(const BookCatalogueResult& seriesTile);
    void paintGenreTags(const QStringList& genres);
    void populateBookTable(const SeriesDetector::SeriesGroup& group);
    void refreshRowAction(int row);
    void refreshAllRows();
    void clearAuthorScroller();
    void fetchAuthorWorks(const QString& author);

    // Bulk download.
    void onBulkActionClicked();
    void runBulkProbeNext();          // pump method: probe one book at a time

    // Per-book actions.
    void onRowActionClicked(int row);
    void onRowPickerSelected(int row, const BookResult& selected);
    void onRowDownloadComplete(int row, const QString& filePath);
    void onRowDownloadFailed(int row, const QString& reason);

    // Library-store signal handlers.
    void onLibraryStoreChanged();

    // Context menus.
    void showSeriesContextMenu();
    void showBookRowContextMenu(int row);

    BookCatalogueAggregator*    m_catalogueAggregator;
    BookSearchAggregator*       m_searchAggregator;
    BookDownloader*             m_downloader;
    BooksCatalogueLibraryStore* m_libraryStore;
    QNetworkAccessManager*      m_nam;

    BookCatalogueResult         m_currentSeriesTile;
    SeriesDetector::SeriesGroup m_currentGroup;
    QList<BookRowState>         m_rowStates;
    bool                        m_bulkInFlight = false;
    int                         m_bulkCursor = 0;     // index of next book to probe in bulk run
    int                         m_bulkFound = 0;
    int                         m_bulkTotal = 0;

    // UI
    QPushButton*  m_backBtn       = nullptr;
    QLabel*       m_coverLabel    = nullptr;
    QLabel*       m_titleLabel    = nullptr;
    QLabel*       m_authorLabel   = nullptr;
    QLabel*       m_metaLabel     = nullptr;
    QPushButton*  m_bulkBtn       = nullptr;
    QLabel*       m_bulkStripLbl  = nullptr;
    QLabel*       m_synopsisLabel = nullptr;
    QPushButton*  m_readMoreBtn   = nullptr;
    QWidget*      m_tagsRow       = nullptr;
    QLabel*       m_sectionLbl    = nullptr;
    QPushButton*  m_ctxDotsBtn    = nullptr;
    QTableWidget* m_bookTable     = nullptr;
    QScrollArea*  m_authorScroller= nullptr;
    QWidget*      m_authorContent = nullptr;

    bool          m_synopsisExpanded = false;
};
```

- [ ] **Step 2: Register in CMakeLists.txt + commit**

Edit `CMakeLists.txt`: add the header.

```
git add src/ui/pages/books/BooksTankoLibrarySeriesDetailView.h CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P7.1: BooksTankoLibrarySeriesDetailView header (series-shape)"
```

---

### Task 7.2: Implementation — buildUI + hero + synopsis + tags

**Files:**
- Create: `src/ui/pages/books/BooksTankoLibrarySeriesDetailView.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write ctor + buildUI**

Create `src/ui/pages/books/BooksTankoLibrarySeriesDetailView.cpp`:

```cpp
#include "BooksTankoLibrarySeriesDetailView.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "core/book/BookCatalogueAggregator.h"
#include "core/book/BookSearchAggregator.h"
#include "core/book/BookDownloader.h"
#include "core/book/BooksCatalogueLibraryStore.h"
#include "ui/pages/books/BookSourcePicker.h"
#include "ui/widgets/TileCard.h"

BooksTankoLibrarySeriesDetailView::BooksTankoLibrarySeriesDetailView(
    BookCatalogueAggregator* catalogueAggregator,
    BookSearchAggregator* searchAggregator,
    BookDownloader* downloader,
    BooksCatalogueLibraryStore* libraryStore,
    QWidget* parent)
    : QWidget(parent),
      m_catalogueAggregator(catalogueAggregator),
      m_searchAggregator(searchAggregator),
      m_downloader(downloader),
      m_libraryStore(libraryStore),
      m_nam(new QNetworkAccessManager(this))
{
    buildUI();

    connect(m_catalogueAggregator, &BookCatalogueAggregator::authorWorksReady,
            this, [this](const QString&, const QList<BookCatalogueResult>& works) {
                // Same author-works rendering as movie-shape detail (copy of method).
                clearAuthorScroller();
                auto* lay = qobject_cast<QHBoxLayout*>(m_authorContent->layout());
                if (!lay) return;
                int added = 0;
                for (const auto& w : works) {
                    if (added >= 10) break;
                    auto* card = new TileCard(m_authorContent);
                    card->setTitle(w.title);
                    card->setFixedSize(130, 230);
                    if (!w.coverUrl.isEmpty()) {
                        QNetworkRequest req((QUrl(w.coverUrl)));
                        auto* reply = m_nam->get(req);
                        QPointer<TileCard> guard(card);
                        connect(reply, &QNetworkReply::finished,
                                this, [reply, guard]() {
                            reply->deleteLater();
                            if (!guard) return;
                            if (reply->error() != QNetworkReply::NoError) return;
                            QPixmap pix;
                            if (pix.loadFromData(reply->readAll())) guard->setCover(pix);
                        });
                    }
                    lay->addWidget(card);
                    ++added;
                }
                lay->addStretch();
            });
    connect(m_libraryStore, &BooksCatalogueLibraryStore::recordsChanged,
            this, &BooksTankoLibrarySeriesDetailView::onLibraryStoreChanged);
    connect(m_downloader, &BookDownloader::downloadProgress,
            this, [this](const QString& md5, qint64 received, qint64 total) {
                for (int i = 0; i < m_rowStates.size(); ++i) {
                    if (m_rowStates[i].pendingDownloadMd5 == md5) {
                        if (total > 0) {
                            m_rowStates[i].progressPct = int((received * 100) / total);
                            refreshRowAction(i);
                        }
                        break;
                    }
                }
            });
    connect(m_downloader, &BookDownloader::downloadComplete,
            this, [this](const QString& md5, const QString& filePath) {
                for (int i = 0; i < m_rowStates.size(); ++i) {
                    if (m_rowStates[i].pendingDownloadMd5 == md5) {
                        onRowDownloadComplete(i, filePath);
                        break;
                    }
                }
            });
    connect(m_downloader, &BookDownloader::downloadFailed,
            this, [this](const QString& md5, const QString& reason) {
                for (int i = 0; i < m_rowStates.size(); ++i) {
                    if (m_rowStates[i].pendingDownloadMd5 == md5) {
                        onRowDownloadFailed(i, reason);
                        break;
                    }
                }
            });
}

void BooksTankoLibrarySeriesDetailView::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 18, 28, 24);
    root->setSpacing(0);

    // Back row.
    m_backBtn = new QPushButton(QStringLiteral("← Back"), this);
    m_backBtn->setFlat(true);
    m_backBtn->setStyleSheet(QStringLiteral("color:#9090a0; font-size:13px;"));
    connect(m_backBtn, &QPushButton::clicked,
            this, &BooksTankoLibrarySeriesDetailView::backRequested);
    root->addWidget(m_backBtn, 0, Qt::AlignLeft);
    root->addSpacing(18);

    // Hero row (cover + meta column).
    auto* heroRow = new QHBoxLayout;
    heroRow->setSpacing(28);
    m_coverLabel = new QLabel(this);
    m_coverLabel->setFixedSize(160, 240);
    m_coverLabel->setStyleSheet(QStringLiteral(
        "background: linear-gradient(160deg, #2d3a55 0%, #5e3a8b 100%); border-radius: 8px;"));
    heroRow->addWidget(m_coverLabel, 0, Qt::AlignTop);

    auto* metaCol = new QVBoxLayout;
    metaCol->setSpacing(6);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet(QStringLiteral("color:#fff; font-size:28px; font-weight:600;"));
    m_titleLabel->setWordWrap(true);
    metaCol->addWidget(m_titleLabel);
    m_authorLabel = new QLabel(this);
    m_authorLabel->setStyleSheet(QStringLiteral("color:#c0a0ff; font-size:15px;"));
    metaCol->addWidget(m_authorLabel);
    metaCol->addSpacing(10);
    m_metaLabel = new QLabel(this);
    m_metaLabel->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:13px;"));
    metaCol->addWidget(m_metaLabel);
    metaCol->addSpacing(16);

    // Bulk action row.
    m_bulkBtn = new QPushButton(QStringLiteral("Search for downloads — entire series ↓"), this);
    m_bulkBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #5e3a8b, stop:1 #7b4dba); color:#fff; padding: 12px 22px; "
        "border-radius: 6px; font-size:14px; font-weight:600; }"));
    connect(m_bulkBtn, &QPushButton::clicked,
            this, &BooksTankoLibrarySeriesDetailView::onBulkActionClicked);
    metaCol->addWidget(m_bulkBtn, 0, Qt::AlignLeft);

    m_bulkStripLbl = new QLabel(this);
    m_bulkStripLbl->setStyleSheet(QStringLiteral("color:#b0b0bc; font-size:12px;"));
    metaCol->addWidget(m_bulkStripLbl, 0, Qt::AlignLeft);
    metaCol->addStretch();

    heroRow->addLayout(metaCol, 1);
    root->addLayout(heroRow);
    root->addSpacing(24);

    // Synopsis + read more.
    m_synopsisLabel = new QLabel(this);
    m_synopsisLabel->setWordWrap(true);
    m_synopsisLabel->setStyleSheet(QStringLiteral(
        "color:#c8c8d0; font-size:14px; line-height:1.55;"));
    root->addWidget(m_synopsisLabel);
    m_readMoreBtn = new QPushButton(QStringLiteral("Read more"), this);
    m_readMoreBtn->setFlat(true);
    m_readMoreBtn->setStyleSheet(QStringLiteral("color:#c0a0ff; padding:0;"));
    m_readMoreBtn->setVisible(false);
    connect(m_readMoreBtn, &QPushButton::clicked, this, [this]() {
        m_synopsisExpanded = !m_synopsisExpanded;
        if (m_synopsisExpanded) {
            m_synopsisLabel->setText(m_currentSeriesTile.description);
            m_readMoreBtn->setText(QStringLiteral("Show less"));
        } else {
            paintSynopsis(m_currentSeriesTile);
            m_readMoreBtn->setText(QStringLiteral("Read more"));
        }
    });
    root->addWidget(m_readMoreBtn, 0, Qt::AlignLeft);
    root->addSpacing(14);

    // Tag chips.
    m_tagsRow = new QWidget(this);
    auto* tagsLayout = new QHBoxLayout(m_tagsRow);
    tagsLayout->setContentsMargins(0, 0, 0, 0);
    tagsLayout->setSpacing(8);
    root->addWidget(m_tagsRow);
    root->addSpacing(24);

    // Section header + context dots.
    auto* secRow = new QHBoxLayout;
    m_sectionLbl = new QLabel(QStringLiteral("Books in this series"), this);
    m_sectionLbl->setStyleSheet(QStringLiteral(
        "color:#e0e0e8; font-size:15px; text-transform:uppercase; letter-spacing:0.5px;"));
    secRow->addWidget(m_sectionLbl);
    secRow->addStretch();
    m_ctxDotsBtn = new QPushButton(QStringLiteral("⋯"), this);
    m_ctxDotsBtn->setFlat(true);
    m_ctxDotsBtn->setStyleSheet(QStringLiteral("color:#6a6a76; font-size:18px;"));
    connect(m_ctxDotsBtn, &QPushButton::clicked,
            this, &BooksTankoLibrarySeriesDetailView::showSeriesContextMenu);
    secRow->addWidget(m_ctxDotsBtn);
    root->addLayout(secRow);
    root->addSpacing(8);

    // Book table.
    m_bookTable = new QTableWidget(this);
    m_bookTable->setColumnCount(5);
    m_bookTable->setHorizontalHeaderLabels({QString(), QString(),
                                            QStringLiteral("Year"),
                                            QStringLiteral("Progress"),
                                            QString()});
    m_bookTable->horizontalHeader()->setStretchLastSection(false);
    m_bookTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_bookTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_bookTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_bookTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_bookTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_bookTable->setColumnWidth(0, 56);
    m_bookTable->setColumnWidth(2, 80);
    m_bookTable->setColumnWidth(3, 140);
    m_bookTable->setColumnWidth(4, 140);
    m_bookTable->verticalHeader()->setDefaultSectionSize(80);
    m_bookTable->verticalHeader()->setVisible(false);
    m_bookTable->horizontalHeader()->setVisible(false);
    m_bookTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_bookTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bookTable->setShowGrid(false);
    m_bookTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bookTable, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                int row = m_bookTable->rowAt(pos.y());
                if (row >= 0) showBookRowContextMenu(row);
            });
    root->addWidget(m_bookTable);

    // Author scroller (same pattern as movie-shape).
    auto* authorHeader = new QLabel(QStringLiteral("Other books by author"), this);
    authorHeader->setStyleSheet(QStringLiteral(
        "color:#e0e0e8; font-size:13px; text-transform:uppercase; letter-spacing:0.5px;"));
    root->addSpacing(16);
    root->addWidget(authorHeader);
    m_authorScroller = new QScrollArea(this);
    m_authorScroller->setWidgetResizable(true);
    m_authorScroller->setFrameShape(QFrame::NoFrame);
    m_authorScroller->setFixedHeight(240);
    m_authorContent = new QWidget(m_authorScroller);
    auto* authorLayout = new QHBoxLayout(m_authorContent);
    authorLayout->setContentsMargins(0, 0, 0, 0);
    authorLayout->setSpacing(14);
    m_authorScroller->setWidget(m_authorContent);
    root->addWidget(m_authorScroller);
}
```

- [ ] **Step 2: Add showSeries + paint methods**

Append:

```cpp
void BooksTankoLibrarySeriesDetailView::showSeries(
    const BookCatalogueResult& seriesTile,
    const SeriesDetector::SeriesGroup& group)
{
    m_currentSeriesTile = seriesTile;
    m_currentGroup = group;
    m_synopsisExpanded = false;
    m_bulkInFlight = false;
    m_bulkCursor = 0;
    m_bulkFound = 0;
    m_bulkTotal = group.books.size();

    paintHero(seriesTile, group.books.size());
    paintSynopsis(seriesTile);
    paintGenreTags(seriesTile.genres);
    populateBookTable(group);
    clearAuthorScroller();
    fetchAuthorWorks(seriesTile.author);
    m_bulkStripLbl->clear();
}

void BooksTankoLibrarySeriesDetailView::paintHero(const BookCatalogueResult& t,
                                                  int bookCount)
{
    m_titleLabel->setText(t.seriesName.isEmpty() ? t.title : t.seriesName);
    m_authorLabel->setText(t.author);

    QStringList parts;
    parts << QStringLiteral("%1 books").arg(bookCount);
    if (!t.genres.isEmpty()) parts << t.genres.first();
    parts << QStringLiteral("ongoing");
    if (!t.language.isEmpty()) parts << t.language;
    m_metaLabel->setText(parts.join(QStringLiteral(" · ")));

    if (!t.coverUrl.isEmpty()) {
        QNetworkRequest req((QUrl(t.coverUrl)));
        auto* reply = m_nam->get(req);
        QPointer<QLabel> guard(m_coverLabel);
        connect(reply, &QNetworkReply::finished, this, [reply, guard]() {
            reply->deleteLater();
            if (!guard) return;
            if (reply->error() != QNetworkReply::NoError) return;
            QPixmap pix;
            if (pix.loadFromData(reply->readAll())) {
                guard->setPixmap(pix.scaled(160, 240,
                                            Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation));
            }
        });
    }
}

void BooksTankoLibrarySeriesDetailView::paintSynopsis(const BookCatalogueResult& t)
{
    if (t.description.isEmpty()) {
        m_synopsisLabel->clear();
        m_readMoreBtn->setVisible(false);
        return;
    }
    constexpr int kTruncateAt = 600;
    if (t.description.size() > kTruncateAt && !m_synopsisExpanded) {
        QString truncated = t.description.left(kTruncateAt).trimmed() + QStringLiteral("...");
        m_synopsisLabel->setText(truncated);
        m_readMoreBtn->setVisible(true);
    } else {
        m_synopsisLabel->setText(t.description);
        m_readMoreBtn->setVisible(false);
    }
}

void BooksTankoLibrarySeriesDetailView::paintGenreTags(const QStringList& genres)
{
    if (auto* lay = m_tagsRow->layout()) {
        while (auto* item = lay->takeAt(0)) {
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
    auto* lay = qobject_cast<QHBoxLayout*>(m_tagsRow->layout());
    if (!lay) return;
    for (const auto& g : genres) {
        auto* chip = new QLabel(g, m_tagsRow);
        chip->setStyleSheet(QStringLiteral(
            "background:#1c1c22; border:1px solid #2a2a32; color:#a8a8b4; "
            "font-size:12px; padding:4px 11px; border-radius:999px;"));
        lay->addWidget(chip);
    }
    lay->addStretch();
}

void BooksTankoLibrarySeriesDetailView::clearAuthorScroller()
{
    if (auto* lay = m_authorContent->layout()) {
        while (auto* item = lay->takeAt(0)) {
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
}

void BooksTankoLibrarySeriesDetailView::fetchAuthorWorks(const QString& author)
{
    if (author.isEmpty()) return;
    m_catalogueAggregator->fetchAuthorWorks(QString(), author);
}
```

- [ ] **Step 3: Add populateBookTable + refreshRowAction**

Append:

```cpp
void BooksTankoLibrarySeriesDetailView::populateBookTable(
    const SeriesDetector::SeriesGroup& group)
{
    m_rowStates.clear();
    m_bookTable->setRowCount(group.books.size());
    for (int i = 0; i < group.books.size(); ++i) {
        const auto& book = group.books[i];

        // Col 0: cover thumbnail (48x64 placeholder; populate after fetch).
        auto* thumb = new QLabel;
        thumb->setFixedSize(48, 64);
        thumb->setStyleSheet(QStringLiteral(
            "background: linear-gradient(160deg, #2d3a55 0%, #5e3a8b 100%); border-radius:4px;"));
        m_bookTable->setCellWidget(i, 0, thumb);
        if (!book.coverUrl.isEmpty()) {
            QNetworkRequest req((QUrl(book.coverUrl)));
            auto* reply = m_nam->get(req);
            QPointer<QLabel> guard(thumb);
            connect(reply, &QNetworkReply::finished, this, [reply, guard]() {
                reply->deleteLater();
                if (!guard) return;
                if (reply->error() != QNetworkReply::NoError) return;
                QPixmap pix;
                if (pix.loadFromData(reply->readAll())) {
                    guard->setPixmap(pix.scaled(48, 64,
                                                Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation));
                }
            });
        }

        // Col 1: book# + title.
        auto* titleCell = new QWidget;
        auto* titleLayout = new QVBoxLayout(titleCell);
        titleLayout->setContentsMargins(0, 4, 0, 4);
        titleLayout->setSpacing(2);
        auto* numLbl = new QLabel(QStringLiteral("Book %1").arg(book.seriesPosition));
        numLbl->setStyleSheet(QStringLiteral(
            "color:#8b8b95; font-size:11px; text-transform:uppercase; letter-spacing:0.4px;"));
        auto* titleLbl = new QLabel(book.title);
        titleLbl->setStyleSheet(QStringLiteral(
            "color:#f0f0f4; font-size:15px; font-style:italic;"));
        titleLayout->addWidget(numLbl);
        titleLayout->addWidget(titleLbl);
        m_bookTable->setCellWidget(i, 1, titleCell);

        // Col 2: year.
        auto* yearLbl = new QLabel(book.year);
        yearLbl->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:13px;"));
        m_bookTable->setCellWidget(i, 2, yearLbl);

        // Col 3 + 4: progress + action (populated by refreshRowAction).

        BookRowState rs;
        // Initial state: check library store.
        auto rec = m_libraryStore->recordFor(book.catalogueId);
        if (rec.has_value()) {
            rs.state = RowState::Downloaded;
            rs.progressPct = int(rec->readProgress * 100);
        } else {
            rs.state = RowState::NotInLibrary;
        }
        m_rowStates.append(rs);
        refreshRowAction(i);
    }
}

void BooksTankoLibrarySeriesDetailView::refreshRowAction(int row)
{
    if (row < 0 || row >= m_rowStates.size()) return;
    const auto& rs = m_rowStates[row];

    // Col 3: progress widget.
    auto* progCell = new QWidget;
    auto* progLayout = new QHBoxLayout(progCell);
    progLayout->setContentsMargins(0, 0, 0, 0);
    progLayout->setSpacing(8);
    auto* bar = new QProgressBar;
    bar->setFixedHeight(4);
    bar->setTextVisible(false);
    bar->setRange(0, 100);
    auto* pctLbl = new QLabel;
    pctLbl->setStyleSheet(QStringLiteral("color:#b0b0bc; font-size:11px;"));
    progLayout->addWidget(bar, 1);
    progLayout->addWidget(pctLbl);

    // Col 4: action button.
    auto* actionBtn = new QPushButton;
    actionBtn->setProperty("row", row);
    connect(actionBtn, &QPushButton::clicked, this, [this, row]() {
        onRowActionClicked(row);
    });

    switch (rs.state) {
    case RowState::NotInLibrary:
        bar->setValue(0);
        pctLbl->setText(QStringLiteral("—"));
        actionBtn->setText(QStringLiteral("Search for downloads ↓"));
        actionBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background:#1c1c22; color:#c0a0ff; "
            "border:1px solid #2d2d35; padding: 7px 14px; border-radius: 5px; "
            "font-size: 12px; font-weight: 500; }"));
        break;
    case RowState::Searching:
        bar->setValue(0);
        pctLbl->setText(QStringLiteral("—"));
        actionBtn->setText(QStringLiteral("Checking..."));
        actionBtn->setEnabled(false);
        break;
    case RowState::Downloading:
        bar->setValue(rs.progressPct);
        pctLbl->setText(QStringLiteral("%1%").arg(rs.progressPct));
        actionBtn->setText(QStringLiteral("Downloading"));
        actionBtn->setEnabled(false);
        actionBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background:#1c1c22; color:#b0b0bc; "
            "border:1px solid #2d2d35; padding:7px 14px; border-radius:5px; }"));
        break;
    case RowState::Downloaded:
        bar->setValue(rs.progressPct);
        pctLbl->setText(rs.progressPct == 100
                            ? QStringLiteral("100%")
                            : QStringLiteral("%1%").arg(rs.progressPct));
        actionBtn->setText(QStringLiteral("Read"));
        actionBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 #5e3a8b, stop:1 #7b4dba); color:#fff; "
            "padding: 7px 14px; border-radius:5px; font-size:12px; font-weight:500; }"));
        break;
    case RowState::NoSourceYet:
        bar->setValue(0);
        pctLbl->setText(QStringLiteral("—"));
        actionBtn->setText(QStringLiteral("no source yet"));
        actionBtn->setEnabled(false);
        actionBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; color:#6a6a76; "
            "font-style: italic; }"));
        break;
    }

    m_bookTable->setCellWidget(row, 3, progCell);
    m_bookTable->setCellWidget(row, 4, actionBtn);
}

void BooksTankoLibrarySeriesDetailView::refreshAllRows()
{
    for (int i = 0; i < m_rowStates.size(); ++i) refreshRowAction(i);
}
```

- [ ] **Step 4: Add bulk-download + per-row action handlers**

Append:

```cpp
void BooksTankoLibrarySeriesDetailView::onBulkActionClicked()
{
    if (m_bulkInFlight) return;
    m_bulkInFlight = true;
    m_bulkCursor = 0;
    m_bulkFound = 0;
    m_bulkStripLbl->setText(QStringLiteral("Searching..."));
    runBulkProbeNext();
}

void BooksTankoLibrarySeriesDetailView::runBulkProbeNext()
{
    // Find the next NotInLibrary book to probe.
    while (m_bulkCursor < m_currentGroup.books.size()) {
        if (m_rowStates[m_bulkCursor].state == RowState::NotInLibrary) {
            onRowActionClicked(m_bulkCursor);
            ++m_bulkCursor;
            return;
        }
        if (m_rowStates[m_bulkCursor].state == RowState::Downloaded) {
            ++m_bulkFound;
        }
        ++m_bulkCursor;
    }
    // All processed.
    m_bulkInFlight = false;
    m_bulkStripLbl->setText(
        QStringLiteral("Last run: %1 / %2 found").arg(m_bulkFound).arg(m_bulkTotal));
}

void BooksTankoLibrarySeriesDetailView::onRowActionClicked(int row)
{
    if (row < 0 || row >= m_rowStates.size()) return;
    auto& rs = m_rowStates[row];
    if (rs.state == RowState::Downloaded) {
        emit readRequested(m_currentGroup.books[row].catalogueId);
        return;
    }
    if (rs.state != RowState::NotInLibrary) return;

    // Open the picker for this row.
    rs.state = RowState::Searching;
    refreshRowAction(row);

    auto* picker = new BookSourcePicker(m_searchAggregator, this);
    connect(picker, &BookSourcePicker::resultSelected, this,
            [this, row, picker](const BookResult& selected) {
                onRowPickerSelected(row, selected);
                picker->deleteLater();
            });
    connect(picker, &BookSourcePicker::cancelled, this, [this, row, picker]() {
        m_rowStates[row].state = RowState::NotInLibrary;
        refreshRowAction(row);
        picker->deleteLater();
    });
    picker->openFor(m_currentGroup.books[row]);
}

void BooksTankoLibrarySeriesDetailView::onRowPickerSelected(int row,
                                                            const BookResult& selected)
{
    auto& rs = m_rowStates[row];
    rs.state = RowState::Downloading;
    rs.progressPct = 0;
    rs.pendingDownloadMd5 = selected.md5;
    refreshRowAction(row);

    if (selected.source == QStringLiteral("tankorent")) {
        m_downloader->downloadFromMagnet(selected.md5, selected.downloadUrl,
                                         selected.format);
    } else {
        m_downloader->downloadFromUrl(selected.md5, selected.downloadUrl,
                                      selected.format);
    }
}

void BooksTankoLibrarySeriesDetailView::onRowDownloadComplete(int row,
                                                              const QString& filePath)
{
    auto& rs = m_rowStates[row];
    rs.state = RowState::Downloaded;
    rs.progressPct = 0; // read-progress starts at 0
    rs.pendingDownloadMd5.clear();

    // Build CatalogueRecord for this book + persist.
    const auto& book = m_currentGroup.books[row];
    CatalogueRecord rec;
    rec.catalogueId    = book.catalogueId;
    rec.isbn           = book.isbn;
    rec.title          = book.title;
    rec.author         = book.author;
    rec.publisher      = book.publisher;
    rec.year           = book.year;
    rec.language       = book.language;
    rec.description    = book.description;
    rec.genres         = book.genres;
    rec.coverUrl       = book.coverUrl;
    rec.seriesId       = m_currentSeriesTile.workId; // series record id
    rec.seriesName     = m_currentSeriesTile.seriesName;
    rec.seriesPosition = book.seriesPosition;
    rec.seriesTotal    = m_currentGroup.books.size();
    rec.filePath       = filePath;
    rec.format         = QFileInfo(filePath).suffix().toLower();
    rec.addedAt        = QDateTime::currentSecsSinceEpoch();
    m_libraryStore->upsertRecord(rec);
    refreshRowAction(row);

    if (m_bulkInFlight) {
        runBulkProbeNext();
    }
}

void BooksTankoLibrarySeriesDetailView::onRowDownloadFailed(int row,
                                                            const QString& /*reason*/)
{
    auto& rs = m_rowStates[row];
    rs.state = RowState::NoSourceYet;
    rs.pendingDownloadMd5.clear();
    refreshRowAction(row);
    if (m_bulkInFlight) {
        runBulkProbeNext();
    }
}

void BooksTankoLibrarySeriesDetailView::onLibraryStoreChanged()
{
    // Re-derive each row's state from store + any in-flight pending download.
    for (int i = 0; i < m_currentGroup.books.size(); ++i) {
        const auto& book = m_currentGroup.books[i];
        if (m_rowStates[i].state == RowState::Downloading) continue;
        auto rec = m_libraryStore->recordFor(book.catalogueId);
        m_rowStates[i].state = rec.has_value() ? RowState::Downloaded
                                                : RowState::NotInLibrary;
        m_rowStates[i].progressPct = rec.has_value()
            ? int(rec->readProgress * 100) : 0;
        refreshRowAction(i);
    }
}

void BooksTankoLibrarySeriesDetailView::showSeriesContextMenu()
{
    QMenu menu(this);
    menu.addAction(QStringLiteral("Cancel all downloads"), this, [this]() {
        // Cancel any in-flight downloads in this series.
        for (auto& rs : m_rowStates) {
            if (!rs.pendingDownloadMd5.isEmpty()) {
                m_downloader->cancel(rs.pendingDownloadMd5);
                rs.pendingDownloadMd5.clear();
                rs.state = RowState::NotInLibrary;
            }
        }
        refreshAllRows();
    });
    menu.addAction(QStringLiteral("Remove series from library"), this, [this]() {
        for (const auto& book : m_currentGroup.books) {
            m_libraryStore->evictByCatalogueId(book.catalogueId);
        }
        emit backRequested();
    });
    menu.addAction(QStringLiteral("Show series on Open Library"), this, [this]() {
        QDesktopServices::openUrl(QUrl(
            QStringLiteral("https://openlibrary.org") + m_currentSeriesTile.workId));
    });
    menu.exec(QCursor::pos());
}

void BooksTankoLibrarySeriesDetailView::showBookRowContextMenu(int row)
{
    if (row < 0 || row >= m_rowStates.size()) return;
    const auto& book = m_currentGroup.books[row];
    QMenu menu(this);
    if (m_rowStates[row].state == RowState::Downloaded) {
        menu.addAction(QStringLiteral("Open in reader"), this,
                       [this, book]() { emit readRequested(book.catalogueId); });
        menu.addAction(QStringLiteral("Remove from library"), this,
                       [this, book]() {
                           m_libraryStore->evictByCatalogueId(book.catalogueId);
                       });
    } else if (m_rowStates[row].state == RowState::Downloading) {
        menu.addAction(QStringLiteral("Cancel"), this, [this, row]() {
            auto& rs = m_rowStates[row];
            if (!rs.pendingDownloadMd5.isEmpty()) {
                m_downloader->cancel(rs.pendingDownloadMd5);
                rs.pendingDownloadMd5.clear();
                rs.state = RowState::NotInLibrary;
                refreshRowAction(row);
            }
        });
    } else {
        menu.addAction(QStringLiteral("Search for downloads"), this,
                       [this, row]() { onRowActionClicked(row); });
    }
    menu.exec(QCursor::pos());
}
```

- [ ] **Step 5: Register .cpp + commit**

Edit `CMakeLists.txt`: add the .cpp.

```
git add src/ui/pages/books/BooksTankoLibrarySeriesDetailView.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P7.2: BooksTankoLibrarySeriesDetailView impl (hero + bulk + per-book table + 5 row states + context menus)"
```

---

**End of Phase 7.** Series-shape detail view ships with the full 5-row-state vocabulary from the mockup, bulk download fan-out, per-book download paths, library-store sync on changes, and context menus on series header + book rows.

Main app build is GREEN once Phase 8 (`BookSourcePicker`) lands or with picker temporarily stubbed.

---

## Phase 8 — BookSourcePicker (parallel fan-out picker)

The picker is a modal dialog that opens when the user clicks `[Search for downloads]`. Per spec §3.7: three vertical source sections (LibGen / Anna's Archive / Tankorent) each loading independently with its own spinner; results stream in as each source returns. Each row shows quality signals: format, file size, source, filename hint, plus seeders + leechers on Tankorent rows.

Hemanth-locked rationale 2026-05-20: *"there are a lot of innacurate or misleading books masqeurading as other books or books that are still what we searched for but low quality pages and watermarks. This is where having options would beneift us."*

### Task 8.1: Class header

**Files:**
- Create: `src/ui/pages/books/BookSourcePicker.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/books/BookSourcePicker.h`:

```cpp
#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

#include "core/book/BookCatalogueResult.h"
#include "core/book/BookResult.h"

class BookSearchAggregator;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;

// Modal picker for [Search for downloads]. Three vertical sections —
// LibGen / Anna's Archive / Tankorent — each loading independently. Per spec
// §3.7 + Hemanth's "separate the sources to reduce search time" framing.
//
// Lifecycle:
//   1. Detail view (movie- or series-shape) instantiates a BookSourcePicker.
//   2. Calls openFor(catalogueResult). Picker shows up + queries fire in
//      parallel via BookSearchAggregator.
//   3. Each source's results populate its section independently as the
//      aggregator emits sourceResultsReady(sourceId, results).
//   4. Each source's spinner stops + section header updates ("LibGen — 3 results"
//      or "Anna's Archive — No results").
//   5. User clicks a row → picker emits resultSelected(BookResult); closes.
//   6. User clicks Cancel → picker emits cancelled; closes.
//   7. All sections finished + zero results → "polite empty" state, picker stays
//      open with a Close button (per spec §3.11).
class BookSourcePicker : public QDialog
{
    Q_OBJECT
public:
    explicit BookSourcePicker(BookSearchAggregator* aggregator,
                              QWidget* parent = nullptr);

    void openFor(const BookCatalogueResult& target);

signals:
    void resultSelected(const BookResult& result);
    void cancelled();

private:
    struct SourceSection {
        QString    sourceId;
        QLabel*    header = nullptr;
        QWidget*   spinner = nullptr;
        QWidget*   rowsContainer = nullptr;
        QVBoxLayout* rowsLayout = nullptr;
        bool       completed = false;
        int        rowCount = 0;
    };

    void buildUI();
    SourceSection makeSection(const QString& sourceId, const QString& displayName);
    QWidget* makeResultRow(const QString& sourceId, const BookResult& r);

    void onSourceResultsReady(const QString& sourceId,
                              const QList<BookResult>& results);
    void onSourceFailed(const QString& sourceId, const QString& error);
    void onAllSourcesCompleted();

    BookSearchAggregator* m_aggregator;
    BookCatalogueResult   m_currentTarget;

    QVBoxLayout* m_sectionsLayout = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QLabel*      m_emptyStateLabel = nullptr;

    QHash<QString, SourceSection> m_sections;
};
```

- [ ] **Step 2: Register + commit**

Edit `CMakeLists.txt`: add the header.

```
git add src/ui/pages/books/BookSourcePicker.h CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P8.1: BookSourcePicker header"
```

---

### Task 8.2: Implementation

**Files:**
- Create: `src/ui/pages/books/BookSourcePicker.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write ctor + buildUI**

Create `src/ui/pages/books/BookSourcePicker.cpp`:

```cpp
#include "BookSourcePicker.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/book/BookSearchAggregator.h"

BookSourcePicker::BookSourcePicker(BookSearchAggregator* aggregator,
                                   QWidget* parent)
    : QDialog(parent), m_aggregator(aggregator)
{
    setWindowTitle(QStringLiteral("Search for downloads"));
    setModal(true);
    resize(720, 520);
    buildUI();

    connect(m_aggregator, &BookSearchAggregator::sourceResultsReady,
            this, &BookSourcePicker::onSourceResultsReady);
    connect(m_aggregator, &BookSearchAggregator::sourceFailed,
            this, &BookSourcePicker::onSourceFailed);
    connect(m_aggregator, &BookSearchAggregator::allSourcesCompleted,
            this, &BookSourcePicker::onAllSourcesCompleted);
}

void BookSourcePicker::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 18, 24, 18);
    root->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Search for downloads"), this);
    title->setStyleSheet(QStringLiteral("color:#fff; font-size:18px; font-weight:600;"));
    root->addWidget(title);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* scrollContent = new QWidget;
    m_sectionsLayout = new QVBoxLayout(scrollContent);
    m_sectionsLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionsLayout->setSpacing(20);

    // Create the three sections in fixed order.
    auto add = [this](const QString& id, const QString& name) {
        auto sec = makeSection(id, name);
        m_sections.insert(id, sec);
    };
    add(QStringLiteral("libgen"),         QStringLiteral("LibGen"));
    add(QStringLiteral("annas-archive"),  QStringLiteral("Anna's Archive"));
    add(QStringLiteral("tankorent"),      QStringLiteral("Tankorent"));

    m_sectionsLayout->addStretch();
    scroll->setWidget(scrollContent);
    root->addWidget(scroll, /*stretch*/ 1);

    m_emptyStateLabel = new QLabel(this);
    m_emptyStateLabel->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:13px;"));
    m_emptyStateLabel->hide();
    root->addWidget(m_emptyStateLabel);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit cancelled();
        accept();
    });
    btnRow->addWidget(m_closeBtn);
    root->addLayout(btnRow);
}

BookSourcePicker::SourceSection
BookSourcePicker::makeSection(const QString& sourceId, const QString& displayName)
{
    SourceSection sec;
    sec.sourceId = sourceId;

    sec.header = new QLabel(displayName, this);
    sec.header->setStyleSheet(QStringLiteral(
        "color:#e0e0e8; font-size:14px; font-weight:600; "
        "border-bottom: 1px solid #2a2a32; padding-bottom: 4px;"));
    m_sectionsLayout->addWidget(sec.header);

    // Spinner: a simple "Searching..." label for v1 (animated spinner widget
    // can be a Phase 9 polish if Hemanth wants).
    sec.spinner = new QLabel(QStringLiteral("Searching..."), this);
    sec.spinner->setStyleSheet(QStringLiteral("color:#b0b0bc; font-size:12px;"));
    m_sectionsLayout->addWidget(sec.spinner);

    sec.rowsContainer = new QWidget(this);
    sec.rowsLayout = new QVBoxLayout(sec.rowsContainer);
    sec.rowsLayout->setContentsMargins(0, 0, 0, 0);
    sec.rowsLayout->setSpacing(4);
    m_sectionsLayout->addWidget(sec.rowsContainer);

    return sec;
}

void BookSourcePicker::openFor(const BookCatalogueResult& target)
{
    m_currentTarget = target;

    // Reset section states.
    for (auto& sec : m_sections) {
        sec.completed = false;
        sec.rowCount = 0;
        sec.spinner->setVisible(true);
        sec.header->setText(QStringLiteral("%1").arg(
            sec.sourceId == QStringLiteral("libgen") ? QStringLiteral("LibGen") :
            sec.sourceId == QStringLiteral("annas-archive") ? QStringLiteral("Anna's Archive") :
            QStringLiteral("Tankorent")));
        // Clear existing rows.
        if (sec.rowsLayout) {
            while (auto* item = sec.rowsLayout->takeAt(0)) {
                if (auto* w = item->widget()) w->deleteLater();
                delete item;
            }
        }
    }
    m_emptyStateLabel->hide();

    m_aggregator->searchFor(target);
    show();
}
```

- [ ] **Step 2: Add row construction + source-results handler**

Append:

```cpp
QWidget* BookSourcePicker::makeResultRow(const QString& sourceId,
                                         const BookResult& r)
{
    auto* row = new QFrame;
    row->setStyleSheet(QStringLiteral(
        "QFrame { background:#1c1c22; border:1px solid #2a2a32; border-radius:6px; }"
        "QFrame:hover { border-color: #c0a0ff; }"));
    row->setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(14);

    // Format chip.
    auto* fmt = new QLabel(r.format.toUpper());
    fmt->setStyleSheet(QStringLiteral(
        "background:#2d2d35; color:#c0a0ff; padding: 3px 10px; "
        "border-radius: 999px; font-size: 11px; font-weight:600;"));
    layout->addWidget(fmt);

    // Title + filename hint.
    auto* col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    auto* tl = new QLabel(r.title.isEmpty() ? QStringLiteral("(no title)") : r.title);
    tl->setStyleSheet(QStringLiteral("color:#f0f0f4; font-size:13px;"));
    col->addWidget(tl);
    if (!r.author.isEmpty()) {
        auto* a = new QLabel(r.author);
        a->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:11px;"));
        col->addWidget(a);
    }
    layout->addLayout(col, /*stretch*/ 1);

    // Source-specific signal: seeders/leechers (Tankorent), or year (LibGen/AA).
    if (sourceId == QStringLiteral("tankorent")) {
        // BookResult.fileSize is repurposed by TankorentBookScraper to carry
        // "<size> · <seeders>S · <leechers>L". For v1, surface fileSize as-is
        // — it's the picker's quality signal column.
        auto* signal = new QLabel(r.fileSize);
        signal->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:11px;"));
        layout->addWidget(signal);
    } else {
        auto* size = new QLabel(r.fileSize);
        size->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:11px;"));
        layout->addWidget(size);
        if (!r.year.isEmpty()) {
            auto* yr = new QLabel(r.year);
            yr->setStyleSheet(QStringLiteral("color:#8b8b95; font-size:11px;"));
            layout->addWidget(yr);
        }
    }

    // Click anywhere on the row → select.
    row->installEventFilter(new class : public QObject {
    public:
        BookSourcePicker* picker;
        BookResult        result;
        bool eventFilter(QObject*, QEvent* ev) override {
            if (ev->type() == QEvent::MouseButtonRelease) {
                emit picker->resultSelected(result);
                picker->accept();
                return true;
            }
            return false;
        }
    });
    // Note: lambda-as-filter pattern above is illustrative; replace with
    // a proper named QObject subclass (e.g. BookSourcePickerRowEventFilter)
    // at implementation time. For brevity in the plan, the row's clicked
    // semantic is captured.

    return row;
}

void BookSourcePicker::onSourceResultsReady(const QString& sourceId,
                                            const QList<BookResult>& results)
{
    auto it = m_sections.find(sourceId);
    if (it == m_sections.end()) return;
    auto& sec = it.value();
    sec.completed = true;
    sec.spinner->setVisible(false);
    sec.rowCount = results.size();

    if (results.isEmpty()) {
        sec.header->setText(QStringLiteral("%1 — No results").arg(sec.header->text()));
    } else {
        sec.header->setText(QStringLiteral("%1 — %2 result(s)")
                                .arg(sec.header->text()).arg(results.size()));
        for (const auto& r : results) {
            auto* row = makeResultRow(sourceId, r);
            sec.rowsLayout->addWidget(row);
        }
    }
}

void BookSourcePicker::onSourceFailed(const QString& sourceId, const QString& error)
{
    auto it = m_sections.find(sourceId);
    if (it == m_sections.end()) return;
    auto& sec = it.value();
    sec.completed = true;
    sec.spinner->setVisible(false);
    sec.header->setText(QStringLiteral("%1 — Source failed: %2")
                            .arg(sec.header->text(), error));
}

void BookSourcePicker::onAllSourcesCompleted()
{
    int totalResults = 0;
    for (const auto& sec : m_sections) totalResults += sec.rowCount;
    if (totalResults == 0) {
        m_emptyStateLabel->setText(
            QStringLiteral("No sources have %1 available right now.")
                .arg(m_currentTarget.title));
        m_emptyStateLabel->setVisible(true);
    }
}
```

- [ ] **Step 3: Register .cpp in CMakeLists.txt + commit**

Edit `CMakeLists.txt`: add the .cpp.

```
git add src/ui/pages/books/BookSourcePicker.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P8.2: BookSourcePicker impl (three-section parallel picker with quality signals + polite-empty state)"
```

- [ ] **Step 4: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`. With Phases 6 + 7 already in, the forward-declared `BookSourcePicker` resolves and link succeeds.

---

**End of Phase 8.** Picker ships with three-section parallel layout, per-source streaming results, quality signals (format/size/seeders), polite-empty state when all sources return zero results, and a Close button for cancel.

Main app build GREEN (modulo the row-click event-filter cleanup noted inline — convert to a proper named `QObject` subclass at execution time).

---

## Phase 9 — BooksPage rewire + integration + end-to-end smoke

The integration phase. Ties Phases 1-8 together: BooksPage's search bar fires the catalogue takeover, the library grid is driven by `BooksCatalogueLibraryStore`, the Continue strip becomes series-aware, `BookSeriesView` is deleted, `BooksScanner` simplifies, `MainWindow` routing updates. Closes with an end-to-end smoke against real Open Library + LibGen.

### Task 9.1: Delete BookSeriesView + strip books-bridge v1.3 devSnapshot reference

**Files:**
- Delete: `src/ui/pages/BookSeriesView.h`
- Delete: `src/ui/pages/BookSeriesView.cpp`
- Modify: `src/ui/readers/BookBridge.h/cpp` — strip `m_seriesView` devSnapshot reference (introduced in v1.3 books bridge 2026-05-19)
- Modify: `src/ui/pages/BooksPage.h/cpp` — strip `m_seriesView` field + `showSeries` slot + `m_stack` usage
- Modify: `src/ui/MainWindow.h/cpp` — strip BookSeriesView-targeted routing
- Modify: `CMakeLists.txt` — remove from SOURCES + HEADERS

- [ ] **Step 1: Locate the BookSeriesView devSnapshot reference in BookBridge**

Run:
```
grep -n "BookSeriesView" src/ui/readers/BookBridge.h src/ui/readers/BookBridge.cpp
```

Expected: one or more lines in `BookBridge.cpp::devSnapshot()` or `dispatchDevCommand()`. Strip those references — they were introduced by Phase D.1 v1.3 books bridge (chat.md 2026-05-19) for the `dump-ui books` command. The relevant lines invoke `m_seriesView->devSnapshot()`; replace with a `"seriesView": "deleted-in-BOOKS_STREMIO_PIVOT"` placeholder OR remove the key entirely. The v1.3 bridge is dev-tooling, not user-surface — agents can adapt.

- [ ] **Step 2: Strip BookSeriesView from BooksPage**

Edit `src/ui/pages/BooksPage.h`:
- Remove `class BookSeriesView;` forward declaration.
- Remove the `m_seriesView` field.
- Remove the `showSeries` slot.
- Remove `m_stack` if it was used only to switch between grid and BookSeriesView.

Edit `src/ui/pages/BooksPage.cpp`:
- Remove `#include "BookSeriesView.h"`.
- Remove BookSeriesView construction in ctor.
- Remove `showSeries` method body.
- Remove any `openBook` connection routing through BookSeriesView.

- [ ] **Step 3: Strip from MainWindow routing**

Edit `src/ui/MainWindow.cpp`:
- Find the routing block that connects `BookSeriesView::bookSelected` to `BookReader::open` (search: `BookSeriesView`).
- Remove those connections.

- [ ] **Step 4: Remove from CMakeLists.txt**

Edit `CMakeLists.txt`:
- Remove `src/ui/pages/BookSeriesView.h` from `HEADERS`.
- Remove `src/ui/pages/BookSeriesView.cpp` from `SOURCES`.

- [ ] **Step 5: Delete the files**

```
git rm src/ui/pages/BookSeriesView.h src/ui/pages/BookSeriesView.cpp
```

- [ ] **Step 6: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`. Any compile errors here mean a stale `BookSeriesView` reference somewhere; grep + fix.

- [ ] **Step 7: Commit**

```
git add src/ui/pages/BooksPage.h src/ui/pages/BooksPage.cpp src/ui/readers/BookBridge.h src/ui/readers/BookBridge.cpp src/ui/MainWindow.h src/ui/MainWindow.cpp CMakeLists.txt
git commit -m "BOOKS_STREMIO_PIVOT P9.1: Delete BookSeriesView + strip routing + adapt v1.3 books-bridge devSnapshot"
```

---

### Task 9.2: Simplify BooksScanner — drop folder walk

**Files:**
- Modify: `src/core/BooksScanner.h`
- Modify: `src/core/BooksScanner.cpp`

The existing BooksScanner walks bookRoots for `*.epub/pdf/mobi/...` and groups files into `BookSeriesInfo` structs. After the pivot, library content is defined by `BooksCatalogueLibraryStore`, not by what's on disk. BooksScanner's new job is to validate that the files behind catalogue records still exist + emit a signal to BooksPage on file-disappearance so the tile gets evicted.

- [ ] **Step 1: Rewrite the header**

Replace `src/core/BooksScanner.h` with:

```cpp
#pragma once

#include <QObject>
#include <QString>

class BooksCatalogueLibraryStore;

// BOOKS_STREMIO_PIVOT 2026-05-20 — BooksScanner simplifies dramatically.
//
// Pre-pivot: walked Books root folder for *.epub/pdf/mobi/azw3/djvu/txt files
// + grouped them into BookSeriesInfo tiles. Catalogue-records-as-truth makes
// that walk unnecessary; user-dropped files outside catalogue downloads do
// not enter the library (per spec §3.8 burn-the-ships).
//
// Post-pivot: only validates that files behind catalogue records still exist
// on disk. Files have NOT been deleted (we never delete user data), but they
// may have moved or been removed externally — the validateAll() pass emits
// missingFile() for each orphaned record so BooksPage can evict + re-render.
class BooksScanner : public QObject
{
    Q_OBJECT
public:
    explicit BooksScanner(BooksCatalogueLibraryStore* store,
                          QObject* parent = nullptr);

public slots:
    // Walks every record in the store. For each record whose filePath does
    // not resolve to an extant file, emits missingFile(catalogueId). Caller
    // (BooksPage on showEvent) then decides whether to evict.
    void validateAll();

signals:
    void missingFile(const QString& catalogueId);
    void validateFinished();

private:
    BooksCatalogueLibraryStore* m_store;
};
```

- [ ] **Step 2: Rewrite the implementation**

Replace `src/core/BooksScanner.cpp` with:

```cpp
#include "BooksScanner.h"

#include <QFileInfo>
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"

BooksScanner::BooksScanner(BooksCatalogueLibraryStore* store, QObject* parent)
    : QObject(parent), m_store(store) {}

void BooksScanner::validateAll()
{
    const auto records = m_store->all();
    for (const auto& r : records) {
        if (r.filePath.isEmpty()) continue;
        // filePath in CatalogueRecord is canonical-relative under Books root;
        // BooksCatalogueLibraryStore.cpp handles both relative + absolute
        // resolution in its own validateAll(). For the scanner's signaling
        // role, we just inform the page of orphans without evicting here
        // (store's validateAll evicts; the scanner is a signal-bridge in case
        // BooksPage wants different policy in the future).
        if (!QFileInfo::exists(r.filePath)) {
            emit missingFile(r.catalogueId);
        }
    }
    emit validateFinished();
}
```

- [ ] **Step 3: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`. Any leftover callers of the old `scan(QStringList)` slot or `bookSeriesFound`/`scanFinished` signals will fail to compile — grep + remove (those callsites all lived in the now-deleted folder-walk code path).

- [ ] **Step 4: Commit**

```
git add src/core/BooksScanner.h src/core/BooksScanner.cpp
git commit -m "BOOKS_STREMIO_PIVOT P9.2: BooksScanner simplifies to catalogue-record file validation only"
```

---

### Task 9.3: BooksPage rewire — search bar takeover + detail-view routing

**Files:**
- Modify: `src/ui/pages/BooksPage.h`
- Modify: `src/ui/pages/BooksPage.cpp`
- Modify: `src/ui/MainWindow.h/cpp` — instantiate aggregator + libraryStore + downloader + searchAggregator and pass into BooksPage ctor

- [ ] **Step 1: Add the new fields + dependencies to BooksPage header**

Edit `src/ui/pages/BooksPage.h`. Add includes:

```cpp
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"
```

Add forward declarations:

```cpp
class BookCatalogueAggregator;
class BookSearchAggregator;
class BookDownloader;
class BooksTankoLibrarySearchWidget;
class BooksTankoLibraryDetailView;
class BooksTankoLibrarySeriesDetailView;
class QStackedWidget;
```

Update ctor signature:

```cpp
explicit BooksPage(CoreBridge* bridge,
                   BooksCatalogueLibraryStore* libraryStore,
                   BookCatalogueAggregator* catalogueAggregator,
                   BookSearchAggregator* searchAggregator,
                   BookDownloader* downloader,
                   QWidget* parent = nullptr);
```

Add fields (in the private: section):

```cpp
BooksCatalogueLibraryStore*       m_libraryStore = nullptr;
BookCatalogueAggregator*          m_catalogueAggregator = nullptr;
BookSearchAggregator*             m_searchAggregator = nullptr;
BookDownloader*                   m_downloader = nullptr;

QStackedWidget*                   m_pageStack = nullptr;
QWidget*                          m_libraryPage = nullptr;
BooksTankoLibrarySearchWidget*    m_searchWidget = nullptr;
BooksTankoLibraryDetailView*      m_movieDetailView = nullptr;
BooksTankoLibrarySeriesDetailView*m_seriesDetailView = nullptr;

QLabel*                           m_emptyStateLabel = nullptr; // "Search for books to add to library"
```

- [ ] **Step 2: Rewire BooksPage ctor + buildUI**

Edit `src/ui/pages/BooksPage.cpp` ctor + buildUI to:
1. Construct a `QStackedWidget` as the new root widget.
2. Add four pages to the stack:
   - `m_libraryPage` — the existing Continue strip + tile grid + empty state.
   - `m_searchWidget = new BooksTankoLibrarySearchWidget(m_catalogueAggregator, m_libraryStore, this)`.
   - `m_movieDetailView = new BooksTankoLibraryDetailView(m_catalogueAggregator, m_searchAggregator, m_downloader, m_libraryStore, this)`.
   - `m_seriesDetailView = new BooksTankoLibrarySeriesDetailView(m_catalogueAggregator, m_searchAggregator, m_downloader, m_libraryStore, this)`.
3. Connect search bar `returnPressed` → show `m_searchWidget` + call `m_searchWidget->searchFor(m_searchBar->text())`.
4. Connect `m_searchWidget::resultActivated` → routing logic that picks movie-shape vs series-shape detail view based on `result.isSeries`.
5. Connect `m_searchWidget::backRequested` → switch stack back to `m_libraryPage`.
6. Connect both detail views' `backRequested` → switch stack back to `m_searchWidget` (or `m_libraryPage` if user came from the library).
7. Connect both detail views' `readRequested(catalogueId)` → emit `openBook(filePath)` after looking up the file path in `m_libraryStore`.
8. Wire `m_libraryStore::recordsChanged` → re-render the library grid from current records.

Concrete code snippet for the routing handler:

```cpp
void BooksPage::buildUI()
{
    // ... existing search bar + sort combo + view toggle + density slider build ...

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_pageStack = new QStackedWidget(this);

    // Library page (Continue + grid + empty state).
    m_libraryPage = new QWidget;
    auto* libLayout = new QVBoxLayout(m_libraryPage);
    // ... add m_searchBar, sort combo, view toggle, density slider, continue strip,
    //     m_bookStrip, m_listView, m_emptyStateLabel ...
    m_emptyStateLabel = new QLabel(
        QStringLiteral("Search for books to add to library"), m_libraryPage);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setStyleSheet(QStringLiteral("color:#9090a0; font-size:14px;"));
    libLayout->addWidget(m_emptyStateLabel);
    m_pageStack->addWidget(m_libraryPage);

    m_searchWidget = new BooksTankoLibrarySearchWidget(
        m_catalogueAggregator, m_libraryStore, this);
    m_pageStack->addWidget(m_searchWidget);

    m_movieDetailView = new BooksTankoLibraryDetailView(
        m_catalogueAggregator, m_searchAggregator, m_downloader, m_libraryStore, this);
    m_pageStack->addWidget(m_movieDetailView);

    m_seriesDetailView = new BooksTankoLibrarySeriesDetailView(
        m_catalogueAggregator, m_searchAggregator, m_downloader, m_libraryStore, this);
    m_pageStack->addWidget(m_seriesDetailView);

    root->addWidget(m_pageStack);

    // Routing wiring.
    connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() {
        const QString q = m_searchBar->text().trimmed();
        if (q.isEmpty()) return;
        m_pageStack->setCurrentWidget(m_searchWidget);
        m_searchWidget->searchFor(q);
    });
    connect(m_searchWidget, &BooksTankoLibrarySearchWidget::backRequested,
            this, [this]() { m_pageStack->setCurrentWidget(m_libraryPage); });
    connect(m_searchWidget, &BooksTankoLibrarySearchWidget::resultActivated,
            this, [this](const BookCatalogueResult& r) {
                if (r.isSeries) {
                    // For a series tile, we need the SeriesGroup carrying the books.
                    // The search widget cached this; expose via aggregator state OR
                    // re-derive by querying the aggregator for the series' work key.
                    // For v1 ship: re-query aggregator with the series tile's seriesName
                    // and pull the matching group from the next aggregateReady. Simpler:
                    // SearchWidget stores the SeriesGroup keyed by catalogueId and exposes
                    // it via a getter.
                    auto group = m_searchWidget->seriesGroupFor(r.catalogueId);
                    m_seriesDetailView->showSeries(r, group);
                    m_pageStack->setCurrentWidget(m_seriesDetailView);
                } else {
                    m_movieDetailView->showResult(r);
                    m_pageStack->setCurrentWidget(m_movieDetailView);
                }
            });
    connect(m_movieDetailView, &BooksTankoLibraryDetailView::backRequested,
            this, [this]() { m_pageStack->setCurrentWidget(m_searchWidget); });
    connect(m_seriesDetailView, &BooksTankoLibrarySeriesDetailView::backRequested,
            this, [this]() { m_pageStack->setCurrentWidget(m_searchWidget); });

    // Read requests → forward to MainWindow via existing openBook signal.
    connect(m_movieDetailView, &BooksTankoLibraryDetailView::readRequested,
            this, [this](const QString& catalogueId) {
                auto rec = m_libraryStore->recordFor(catalogueId);
                if (rec) emit openBook(rec->filePath);
            });
    connect(m_seriesDetailView, &BooksTankoLibrarySeriesDetailView::readRequested,
            this, [this](const QString& catalogueId) {
                auto rec = m_libraryStore->recordFor(catalogueId);
                if (rec) emit openBook(rec->filePath);
            });

    // Library refresh on store changes.
    connect(m_libraryStore, &BooksCatalogueLibraryStore::recordsChanged,
            this, &BooksPage::refreshLibraryGrid);
}
```

- [ ] **Step 3: Expose `seriesGroupFor` on the search widget**

Edit `src/ui/pages/books/BooksTankoLibrarySearchWidget.{h,cpp}` to cache the most recent `aggregateReady` payload + expose a getter:

In header (private fields):

```cpp
QHash<QString, SeriesDetector::SeriesGroup> m_seriesGroupByCatalogueId;
```

In header (public methods):

```cpp
SeriesDetector::SeriesGroup seriesGroupFor(const QString& seriesCatalogueId) const {
    return m_seriesGroupByCatalogueId.value(seriesCatalogueId);
}
```

In `.cpp::onAggregateReady`, populate the cache: for each group, key by the synthesized series-tile catalogueId (which is `group.books.first().catalogueId + ":series"`).

- [ ] **Step 4: Add MainWindow wiring**

Edit `src/ui/MainWindow.cpp` ctor (where Books mode page is instantiated). Replace the existing `BooksPage(m_bridge, this)` with:

```cpp
auto* libraryStore = new BooksCatalogueLibraryStore(m_bridge->dataDir(), this);
libraryStore->load();
auto* nam = new QNetworkAccessManager(this);
auto* catalogueAggregator = new BookCatalogueAggregator(
    nam,
    qEnvironmentVariable("TANKOBAN_GOOGLE_BOOKS_KEY"),
    this);
auto* searchAggregator = new BookSearchAggregator(
    {/* scrapers from TankoLibraryPage::activeScrapers() — pass list */}, this);
auto* downloader = new BookDownloader(
    m_torrentClient, m_bridge->booksRootDir(), this);

m_booksPage = new BooksPage(m_bridge, libraryStore, catalogueAggregator,
                            searchAggregator, downloader, this);
```

Note: the `searchAggregator` needs the same `BookScraper*` list TankoLibraryPage uses. For v1, instantiate the scrapers once at MainWindow scope + share between BooksPage + TankoLibraryPage (refactor TankoLibraryPage's ctor to accept a passed-in list rather than constructing its own).

- [ ] **Step 5: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 6: Commit**

```
git add src/ui/pages/BooksPage.h src/ui/pages/BooksPage.cpp src/ui/pages/books/BooksTankoLibrarySearchWidget.h src/ui/pages/books/BooksTankoLibrarySearchWidget.cpp src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "BOOKS_STREMIO_PIVOT P9.3: BooksPage rewire — QStackedWidget + search-takeover + movie/series routing"
```

---

### Task 9.4: BooksPage library grid driven by catalogue records (series-aware tile aggregation)

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` — add `refreshLibraryGrid` method
- Modify: `src/ui/pages/BooksPage.h` — declare slot

- [ ] **Step 1: Implement series-aware tile aggregation**

Edit `src/ui/pages/BooksPage.h` (private slots):

```cpp
void refreshLibraryGrid();
void refreshContinueStrip();
```

Edit `src/ui/pages/BooksPage.cpp`:

```cpp
void BooksPage::refreshLibraryGrid()
{
    m_bookStrip->clear();
    if (m_listView) m_listView->clear();

    const auto allRecords = m_libraryStore->all();
    if (allRecords.isEmpty()) {
        m_bookStrip->hide();
        if (m_listView) m_listView->hide();
        m_emptyStateLabel->show();
        m_continueSection->hide();
        return;
    }
    m_bookStrip->show();
    if (m_listView) m_listView->show();
    m_emptyStateLabel->hide();

    // Group by seriesId for series-aware tiles.
    // Standalone (empty seriesId) records become one tile each.
    // Records sharing a seriesId aggregate into a single series tile,
    // rendered from the most-recently-touched record.
    QHash<QString, QList<CatalogueRecord>> bySeries;
    QList<CatalogueRecord> standalone;
    for (const auto& r : allRecords) {
        if (r.seriesId.isEmpty()) {
            standalone.append(r);
        } else {
            bySeries[r.seriesId].append(r);
        }
    }

    // Render series tiles.
    for (auto it = bySeries.constBegin(); it != bySeries.constEnd(); ++it) {
        auto records = it.value();
        if (records.isEmpty()) continue;
        // Pick canonical record = most-recently-touched (lastReadAt or addedAt).
        std::sort(records.begin(), records.end(),
                  [](const CatalogueRecord& a, const CatalogueRecord& b) {
                      const qint64 ta = a.lastReadAt > 0 ? a.lastReadAt : a.addedAt;
                      const qint64 tb = b.lastReadAt > 0 ? b.lastReadAt : b.addedAt;
                      return ta > tb;
                  });
        const auto& canonical = records.first();
        auto* card = new TileCard(m_bookStrip);
        card->setTitle(canonical.seriesName);
        card->setSubtitle(QStringLiteral("%1 books in library").arg(records.size()));
        if (!canonical.cachedCoverPath.isEmpty()) {
            card->setCover(QPixmap(canonical.cachedCoverPath));
        }
        // Click → re-open the series detail view by re-querying catalogue.
        connect(card, &TileCard::clicked, this, [this, canonical]() {
            // Re-query the aggregator with the series name to get the full
            // SeriesGroup. For library-side clicks where the user is opening
            // an already-known series, this is a small UX latency cost.
            m_pageStack->setCurrentWidget(m_searchWidget);
            m_searchWidget->searchFor(canonical.seriesName);
        });
        m_bookStrip->addCard(card);
    }

    // Render standalone (movie-shape) tiles.
    for (const auto& r : standalone) {
        auto* card = new TileCard(m_bookStrip);
        card->setTitle(r.title);
        card->setSubtitle(r.author);
        if (!r.cachedCoverPath.isEmpty()) {
            card->setCover(QPixmap(r.cachedCoverPath));
        }
        connect(card, &TileCard::clicked, this, [this, r]() {
            // Synthesize a BookCatalogueResult from the record + show movie-shape detail.
            BookCatalogueResult fauxResult;
            fauxResult.catalogueId = r.catalogueId;
            fauxResult.title = r.title;
            fauxResult.author = r.author;
            fauxResult.publisher = r.publisher;
            fauxResult.year = r.year;
            fauxResult.language = r.language;
            fauxResult.description = r.description;
            fauxResult.genres = r.genres;
            fauxResult.coverUrl = r.coverUrl;
            fauxResult.isbn = r.isbn;
            fauxResult.isSeries = false;
            m_movieDetailView->showResult(fauxResult);
            m_pageStack->setCurrentWidget(m_movieDetailView);
        });
        m_bookStrip->addCard(card);
    }

    refreshContinueStrip();
}

void BooksPage::refreshContinueStrip()
{
    m_continueStrip->clear();
    const auto allRecords = m_libraryStore->all();
    QList<CatalogueRecord> inProgress;
    for (const auto& r : allRecords) {
        if (r.readProgress > 0.0 && r.readProgress < 1.0) inProgress.append(r);
    }
    if (inProgress.isEmpty()) {
        m_continueSection->hide();
        return;
    }
    m_continueSection->show();
    // Sort by lastReadAt descending.
    std::sort(inProgress.begin(), inProgress.end(),
              [](const CatalogueRecord& a, const CatalogueRecord& b) {
                  return a.lastReadAt > b.lastReadAt;
              });
    for (const auto& r : inProgress) {
        auto* card = new TileCard(m_continueStrip);
        // Series-aware subscript per spec §3.10.
        QString subtitle;
        if (!r.seriesName.isEmpty()) {
            subtitle = QStringLiteral("%1 · Reading %2 · %3%")
                            .arg(r.seriesName, r.title).arg(int(r.readProgress * 100));
            card->setTitle(r.seriesName);
        } else {
            subtitle = QStringLiteral("%1 · %2%").arg(r.title).arg(int(r.readProgress * 100));
            card->setTitle(r.title);
        }
        card->setSubtitle(subtitle);
        if (!r.cachedCoverPath.isEmpty()) {
            card->setCover(QPixmap(r.cachedCoverPath));
        }
        // Click → open the SPECIFIC book at last-read CFI (not the series page).
        connect(card, &TileCard::clicked, this, [this, r]() {
            emit openBook(r.filePath);
        });
        m_continueStrip->addCard(card);
    }
}
```

- [ ] **Step 2: Wire `BooksScanner::missingFile` to evict orphan records**

Edit `src/ui/pages/BooksPage.cpp` ctor wiring:

```cpp
connect(m_scanner, &BooksScanner::missingFile, this,
        [this](const QString& catalogueId) {
            m_libraryStore->evictByCatalogueId(catalogueId);
        });
```

- [ ] **Step 3: Trigger refresh on showEvent + validateAll on each show**

Edit `src/ui/pages/BooksPage.cpp` showEvent override:

```cpp
void BooksPage::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    m_libraryStore->validateAll();
    refreshLibraryGrid();
}
```

- [ ] **Step 4: Build verification**

```
build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```
git add src/ui/pages/BooksPage.h src/ui/pages/BooksPage.cpp
git commit -m "BOOKS_STREMIO_PIVOT P9.4: BooksPage library grid catalogue-driven + series-aware tile aggregation + continue-strip with series subscript"
```

---

### Task 9.5: End-to-end smoke

**Files:**
- Modify: `agents/audits/evidence_books_stremio_pivot_v1_smoke_<HHMMSS>.json` (smoke evidence)
- Modify: `agents/chat.md` (smoke verdict)

The full happy-path smoke + a no-source-found smoke. Driven via tankoctl dev-bridge + (optional) pywinauto-mcp for visual verification.

- [ ] **Step 1: Pre-smoke — burn the library**

Before launching, ensure the books library is empty (the burn-the-ships state for the v1 ship).

```
rm <dataDir>/books_catalogue_library.json
```

`<dataDir>` is per-user; resolve via `tankoctl app-get-data-dir` or check `%LOCALAPPDATA%\TankobanTeam\Tankoban\`.

- [ ] **Step 2: Launch**

```
build_and_run.bat
```
Wait for Tankoban to launch with `--dev-control` flag (auto-set by build_and_run).

- [ ] **Step 3: Dev-bridge smoke — verify empty state**

```
out\tankoctl.exe ping
out\tankoctl.exe open-page books
out\tankoctl.exe books-get-library
```
Expected: `books-get-library` returns an empty `records` array. UI shows "Search for books to add to library".

- [ ] **Step 4: Movie-shape happy-path smoke (Project Hail Mary)**

Driven via pywinauto-mcp (focus the search bar, type "project hail mary", press Enter):

```python
mcp__pywinauto-mcp__automation_keyboard(action="send", text="project hail mary")
mcp__pywinauto-mcp__automation_keyboard(action="send", text="{ENTER}")
```

Wait ~3-5s for catalogue aggregator to complete. Verify search-takeover view shows results (Books section populated with at least one *Project Hail Mary* tile).

Click the tile (via pywinauto). Verify detail page renders: hero, cover (Andy Weir cover), synopsis, "Other books by Andy Weir" scroller populates within ~2s.

Click [Search for downloads]. Verify picker opens with three sections. LibGen should return EPUB rows within ~1s. Click the first LibGen EPUB row.

Verify the download progress bar fills, completes within ~5-10s, and the detail page's action button morphs to [Read].

Run:
```
out\tankoctl.exe books-get-library
```
Expected: `records` array now has 1 entry for *Project Hail Mary* with `format: "epub"`, `readProgress: 0.0`, valid `filePath`.

- [ ] **Step 5: Series-shape happy-path smoke (Stormlight Archive)**

Back to BooksPage (click back arrow). Search "stormlight archive". Verify Series section populates with the *Stormlight Archive* series tile + Books section populates with individual book results.

Click the series tile. Verify series detail page renders with the per-book table (5 rows initially in "Available not-started" state).

Click [Search for downloads — entire series]. Verify the progress strip fires sequentially ("Searching..." then a stream of state transitions on each row).

Verify within ~30-60s the strip settles to "Last run: X/Y found" where X is the number of books LibGen + AA + Tankorent collectively found (likely 4/5 with *Wind and Truth* italic "no source yet" matching the mockup).

```
out\tankoctl.exe books-get-library
```
Expected: `records` now has 4 additional entries (the found Stormlight books) with valid `seriesId`, `seriesName: "The Stormlight Archive"`, `seriesPosition: 1..5` (skipping the missing one).

- [ ] **Step 6: No-source-found smoke**

Search for an obscure title with no LibGen / AA / Tankorent presence (Hemanth picks; suggestion: a recent academic monograph or self-published indie title).

Click the result. Click [Search for downloads]. Wait for all three source spinners to stop. Verify all three show "No results" and the polite-empty banner shows ("No sources have <title> available right now.").

Click Close. Verify no library record was created — `books-get-library` count unchanged.

- [ ] **Step 7: Continue-reading smoke**

Open *Project Hail Mary* via the Read button. Read ~10 pages. Close. Return to BooksPage. Verify Continue strip shows a single tile with *"Project Hail Mary · X%"* subscript. Click it → reader opens directly at the last-read CFI position, not a detail-page detour.

For a series, open *The Way of Kings* (Book 1 of Stormlight). Read a bit. Close. Verify Continue strip shows *"The Stormlight Archive · Reading The Way of Kings · Y%"* subscript. Click it → opens *The Way of Kings* at the last-read CFI.

- [ ] **Step 8: Empty-state recovery smoke**

Delete one of the catalogue records via tankoctl:
```
out\tankoctl.exe books-evict <catalogueId-of-Project-Hail-Mary>
```
Verify the tile disappears immediately from the library grid (recordsChanged → refreshLibraryGrid). Verify the file on disk was NOT deleted (we never delete user data).

Burn all records:
```
out\tankoctl.exe books-evict-all
```
Verify BooksPage returns to the quiet-empty state with "Search for books to add to library".

- [ ] **Step 9: Capture evidence**

Save the captured tankoctl outputs + pywinauto screenshots to `agents/audits/evidence_books_stremio_pivot_v1_smoke_<HHMMSS>.json`.

- [ ] **Step 10: Stop the app + post the verdict**

```
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

Append to `agents/chat.md`:
```
Agent 2 → brotherhood (smoke verdict, 2026-05-DD ~XX:XXpm): BOOKS_STREMIO_PIVOT v1
end-to-end smoke GREEN across all 8 smoke scenarios — empty-state, movie-shape
happy-path, series-shape happy-path, no-source-found polite-empty, continue-
reading auto-resume (movie + series), empty-state recovery on evict. Evidence
bundle at agents/audits/evidence_books_stremio_pivot_v1_smoke_<HHMMSS>.json.
[Hemanth visual-quality smoke gate pending — book covers + synopsis layout +
picker-row spacing + Stormlight series detail visual fidelity vs the mockup.]
— Agent 2
```

- [ ] **Step 11: Commit smoke evidence**

```
git add agents/audits/evidence_books_stremio_pivot_v1_smoke_<HHMMSS>.json
git commit -m "BOOKS_STREMIO_PIVOT P9.5: End-to-end smoke evidence — 8 scenarios GREEN"
```

---

**End of Phase 9. End of v1 ship.**

You now have:
- Books mode rebuilt Stremio-style: catalogue → source → reader pipeline matching Stream + Comics.
- Catalogue layer (Open Library + Google Books, dedup + series detection) fully tested with 36 unit tests.
- Source layer extended (AA re-enabled per Path A/B/C; Tankorent integration per Agent 4's signed-off API; BookDownloader magnet-source variant).
- Library = catalogue-records-only (BookSeriesView deleted, BooksScanner simplified, burn-the-ships honored).
- Search-takeover view + movie-shape detail + series-shape detail + parallel-fan-out picker, all forked from Stream blueprint with attribution.
- Continue Reading strip is series-aware with auto-resume on click.
- Empty-state shows the single-line copy Hemanth approved.
- End-to-end smoke GREEN across happy path, no-source-found, continue-reading, and empty-state recovery.

**Pending follow-ons (already documented in spec §8 deferred):**
- Catalogue button + Stremio-style discovery board (v2, after Comics catalogue ships).
- "Notify me when available" (parked indefinitely).
- Search within library (v2, when library scales).
- Standalone audiobook player (separate arc).
- Book-focused indexer expansion to Tankorent (v1.x).
- Hemanth's final visual-quality smoke (cover quality, synopsis layout, picker-row spacing, series-detail fidelity vs mockup).

---

## Self-Review — plan vs spec

**Spec coverage check** — every section in `docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md`:

- §3.1 Audiobooks out of scope → Captured in scope statement + spec §7 reference; no audiobook code in this plan. ✓
- §3.2 Movie / series shape → Phase 6 (movie-shape detail) + Phase 7 (series-shape detail) + Phase 3.1 (SeriesDetector) ✓
- §3.3 No [+ Add to library] → Phase 6 + 7 + 8 implement only [Search for downloads] as the entry; no Add button anywhere. ✓
- §3.4 Search bar = global catalogue takeover → Phase 9.3 wires `m_searchBar::returnPressed` → search-takeover ✓
- §3.5 Two sections Series + Books → Phase 5 implements ✓
- §3.6 + §3.7 Sources LibGen + AA + Tankorent in parallel-fan-out picker → Phases 4 + 8 ✓
- §3.8 Library = catalogue-records-only, burn ships → Phases 1.3 (store) + 9.1 (delete BookSeriesView) + 9.2 (BooksScanner simplify) + 9.5 step 1 (burn library on smoke) ✓
- §3.9 Empty-state copy → Phase 9.3 + 9.4 ✓
- §3.10 Continue strip series-aware + auto-resume → Phase 9.4 `refreshContinueStrip` ✓
- §3.11 No-source-found polite empty → Phase 8.2 `onAllSourcesCompleted` ✓
- §3.12 Catalogue button v2 deferred → Not in plan; tracked in spec §8 deferred ✓
- §4.1 Reuse-vs-fork map → Each phase honors the map's REUSE / FORK / DELETE / REWRITE columns ✓
- §6.1 CatalogueRecord → Phase 1.2 ✓
- §6.2 BooksCatalogueLibraryStore → Phase 1.3 ✓
- §6.3 BookCatalogueAggregator → Phase 3.3 ✓
- §6.4 BookSearchAggregator → Phase 4.6 ✓
- §9 Cross-agent coordination — Agent 4 HELP request → Phase 4.2 ✓
- §9 Agent 1 directive relay (Catalogue button v2) → already done in chat.md mid-brainstorm; no plan task needed.
- §10 Mockup reference → Phase 7's spec callout cites the file path ✓

**Placeholder scan** — no "TBD" / "implement later" / "fill in details" found in plan text. Two acceptable "implementation chooses" markers (Task 4.1 path A/B/C branch — the path itself IS the implementation choice; that's the task's purpose; and Task 8.2 step 2 inline note about the event-filter cleanup which is mechanical at execution time).

**Type consistency** — verified across phases:
- `BookCatalogueResult` defined in 1.1, used in 1.2 / 3.1 / 3.2 / 3.3 / 5 / 6 / 7 / 8 — consistent shape (catalogueId, isbn, workId, title, author, publisher, year, language, description, genres, coverUrl, isSeries, seriesId, seriesName, seriesPosition, seriesTotal, pages).
- `CatalogueRecord` defined in 1.2, used in 1.3 / 6 / 7 / 9 — consistent shape.
- `BooksCatalogueLibraryStore` API: `upsertRecord` / `evictByCatalogueId` / `validateAll` / `updateReadProgress` / `hasRecord` / `recordFor` / `catalogueIdForFile` / `all` / `catalogueIdsForSeries` / `allSeriesIds` / `load` / `save` — all consistent across callsites.
- `BookCatalogueAggregator::aggregateReady` signal signature `(QString query, QList<SeriesDetector::SeriesGroup>, QList<BookCatalogueResult>)` — consistent in 3.3 + 5.2 + (implicit in author-works fetch).
- `BookSearchAggregator::sourceResultsReady` signal — consistent across 4.6 + 8.

Plan is internally consistent. Self-review passes.

---

**Plan written. 9 phases, 38 tasks, ~50 tests, full TDD discipline through the data + parser layers, fork-and-substitute attribution through the UI layers, end-to-end smoke gate at the close.**

