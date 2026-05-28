# Books FictionDB Catalogue — Local Series Index + Two-Track Search Implementation Plan

> **⚠️ SUPERSEDED (2026-05-28, mid-build) — the series-track design below is dead.**
> The local-series-index approach (Phase 1 `BookSeriesIndex` + A–Z crawl) was built and
> then abandoned: the real crawl returned 7,248 series but only FictionDB's *indie long
> tail* — Dune/Stormlight and other major franchises are structurally absent from the
> author-series directory. **As-built reality: the aggregator's series track is Top-N
> resolution** (free-text `search` → fetch the top ~8 result book pages → group by their
> self-declared series link). `BookSeriesIndex`/`BookSeriesIndexBuilder` + the
> `parseSeriesIndexPage`/`fetchSeriesIndexPage` client methods are **dormant** (candidate
> persistent-cache layer or removal). Series detail metadata (covers/synopsis/year) is
> fetched eagerly per-book on series open and cached — see `BookSeriesDetailView`.
> Treat Phases 1–2's series-index sections as historical; everything else (storefront,
> series detail view, routing, §5.2 reuse) shipped as written.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Books-mode search clean and series-first by building a local series index from FictionDB's A–Z series directory (instant series results), keeping live FictionDB search for standalone books (re-ranked locally), and adding a series-shape detail view whose per-book rows reuse the existing §5.2 download flow.

**Architecture:** Hybrid (Approach 3). Reuse the data model (`BookCatalogueResult`/`CatalogueRecord`), the library store, the two-section storefront, and the §5.2 movie-shape detail view + download chain. New: `BookSeriesIndex` (local, JSON-backed series catalogue) + series-index parsing on `FictionDbClient` + a two-track `BookCatalogueAggregator` + `BookSeriesDetailView`. Retire OpenLibrary/GoogleBooks + `SeriesDetector` heuristics. NO catalog/browse board this arc (search is the discovery surface).

**Tech Stack:** C++17, Qt6 (QNetworkAccessManager, QObject signals/slots, QJsonDocument), GoogleTest (pure-logic tests with frozen HTML fixtures). FictionDB scraped via Chrome-UA `QNetworkRequest`. Spec: `docs/superpowers/specs/2026-05-28-books-fictiondb-catalogue-design.md`.

**Verification:** `build_check.bat` BUILD OK after each phase (kill `Tankoban.exe` first — Rule 1). Pure-logic GoogleTests via `_build_tests.bat` + `out/tankoban_tests.exe --gtest_filter=...`. Final live smoke via `build_and_run.bat` + `out/tankoctl.exe`.

**Flat-on-master:** no worktree/branch (`feedback_no_worktrees`).

---

## Ground truth (verified signatures, 2026-05-28)

- `FictionDbClient` (`src/core/book/FictionDbClient.{h,cpp}`, shipped Phase 1 last wake): `search(q)`→`searchResults(QString,QList<BookCatalogueResult>)`; `fetchBook(id)`→`bookReady`; `fetchSeries(id)`→`seriesReady(QString seriesId,QString seriesName,QList<BookCatalogueResult> books)`; static `parseSearchPage`/`parseBookPage`/`parseSeriesPage`/`slugFromHref`. `kBase="https://www.fictiondb.com"`. Search endpoint: `/search/searchresults.htm?srchtxt=<q>&styp=5`.
- `BookCatalogueResult` (POD): `catalogueId,isbn,workId,title,author,publisher,year,language,description,genres,coverUrl,isSeries,seriesId,seriesName,seriesPosition,seriesTotal,pages`.
- `BookCatalogueAggregator` (`src/core/book/BookCatalogueAggregator.{h,cpp}`): ctor `(QNetworkAccessManager*, const QString& googleBooksApiKey, QObject*)`; `query(q)`; signal `aggregateReady(QString query, QList<SeriesDetector::SeriesGroup>, QList<BookCatalogueResult>)` + `aggregateFailed`. Constructed at `BooksPage.cpp:58`.
- `SeriesDetector::SeriesGroup` (`src/core/book/SeriesDetector.h`): `{ QString seriesName; QString author; QList<BookCatalogueResult> books; }`.
- `BookCatalogueSearchWidget` (`src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}`): ctor `(BookCatalogueAggregator*, QNetworkAccessManager*, const QString& coverCacheDir, QWidget*)`; `search(q)`; signals `backRequested`, `bookPicked(BookCatalogueResult, QString coverPath)`; slot `onCatalogueResult(QString, QList<SeriesGroup>, QList<BookCatalogueResult>)`; `addSeriesCard(SeriesGroup)`, `addBookCard(BookCatalogueResult)`; `m_seriesStrip`/`m_booksStrip` (`TileStrip`), `m_resultsById`.
- `BookCatalogueDetailView` (`src/ui/pages/books/BookCatalogueDetailView.{h,cpp}`): `showBook(BookCatalogueResult, coverPath)`, `setCatalogueStore`, `notifyDownloadStarted/Progress/Complete/Failed(handle,...)`, signals `downloadRequested(sourceId,BookResult,QStringList urls,BookCatalogueResult,coverPath)` + `readRequested(catalogueId,filePath)`.
- `BooksCatalogueLibraryStore`: `hasRecord(id)`, `recordFor(id)→optional<CatalogueRecord>`, `all()`, `catalogueIdsForSeries(seriesId)`, `upsertRecord`, `validateAll`, signals `recordsChanged()`/`recordReadStateChanged(id)`.
- `BooksPage` (`src/ui/pages/BooksPage.{cpp,h}`): `m_stack` (`FadingStackedWidget`), `m_catalogueDetailView`, `m_catalogueSearchView`, `m_catalogueAggregator`, `m_catalogueStore`, `m_catalogueNam`, `m_catalogueCoverDir`, `m_bookDownloader` (lazy), `m_activeDownloads`. §5.2 slots: `onCatalogueDownloadRequested(...)`, `onBookDownloadProgress/Complete/Failed(...)`, `onCatalogueReadRequested(catalogueId,filePath)`. Detail-view wiring at `BooksPage.cpp:~90`.

---

## File Structure

| File | Responsibility | Disposition |
|------|----------------|-------------|
| `tests/fixtures/book_catalogue/fictiondb_author_series_a.html` | Frozen real A-series-index page | CREATE |
| `src/core/book/FictionDbClient.{h,cpp}` | Add `SeriesIndexEntry`, `parseSeriesIndexPage`, `fetchSeriesIndexPage` | MODIFY |
| `src/core/book/BookSeriesIndex.{h,cpp}` | Local series catalogue: build/load/store(JSON)/query | CREATE |
| `tests/core/book/test_fictiondb_client_parser.cpp` | + series-index-page parser test | MODIFY |
| `tests/core/book/test_book_series_index.cpp` | Index query + ranking tests | CREATE |
| `src/core/book/SeriesDetector.h` | Gut to types-only (keep `SeriesGroup`; drop `detect`/heuristics) | MODIFY |
| `src/core/book/SeriesDetector.cpp` | Delete (heuristics retired) | DELETE |
| `src/core/book/BookCatalogueAggregator.{h,cpp}` | Two-track rework (index + live), drop OL/GB | MODIFY |
| `tests/core/book/test_catalogue_rerank.cpp` | Books-track re-rank scoring test | CREATE |
| `src/core/book/OpenLibraryClient.{h,cpp}`, `GoogleBooksClient.{h,cpp}`, `CatalogueDeduper.{h,cpp}` | Drop from pipeline; delete if unreferenced | DELETE (conditional) |
| `src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}` | Add `seriesPicked`; `kInitialCap=6` + "Show N more" | MODIFY |
| `src/ui/pages/books/BookSeriesDetailView.{h,cpp}` | Series-shape detail: hero + ordered rows + per-row CTA | CREATE |
| `src/ui/pages/BooksPage.{cpp,h}` | Construct index + series view; routing; wire downloads | MODIFY |
| `resources/book_series_index.json.gz` | Bundled pre-built series index (gzipped) | CREATE |
| `CMakeLists.txt` | Register new sources + tests + bundled resource | MODIFY |

---

## Phase 1 — Series index (the data engine)

### Task 1.1: Capture the A-series-index fixture + parse it

**Files:**
- Create: `tests/fixtures/book_catalogue/fictiondb_author_series_a.html`
- Modify: `src/core/book/FictionDbClient.h`, `src/core/book/FictionDbClient.cpp`, `tests/core/book/test_fictiondb_client_parser.cpp`

- [ ] **Step 1: Capture the real fixture (browser UA)**

```bash
UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"
curl -s -A "$UA" -H "Accept: text/html" \
  "https://www.fictiondb.com/series/author-series~a.htm" \
  -o tests/fixtures/book_catalogue/fictiondb_author_series_a.html
# sanity: should be ~200KB+ with many /series/ links and a next-page "»" link
grep -coE '/series/[a-z0-9~._-]+~[0-9]+\.htm' tests/fixtures/book_catalogue/fictiondb_author_series_a.html
```
Expected: a few hundred series links (letter "A" page 1).

- [ ] **Step 2: Add `SeriesIndexEntry` + `parseSeriesIndexPage` declaration to `FictionDbClient.h`**

```cpp
// in FictionDbClient.h, above the class:
struct SeriesIndexEntry {
    QString seriesId;    // FictionDB series slug, e.g. "dune-chronicles-frank-herbert~3735"
    QString seriesName;  // "Dune Chronicles"
    QString author;      // "Frank Herbert" (from row text or slug)
    QString genre;       // optional; "" if absent
};

// in the class public section:
static QList<SeriesIndexEntry> parseSeriesIndexPage(const QString& html, bool* hasNextPage);
void fetchSeriesIndexPage(const QString& letter, int page);
// in signals:
void seriesIndexPageReady(const QString& letter, int page,
                          const QList<SeriesIndexEntry>& entries, bool hasNextPage);
void seriesIndexPageFailed(const QString& letter, int page, const QString& error);
// in private slots:
void onSeriesIndexReply();
```

- [ ] **Step 3: Write the failing test**

```cpp
// tests/core/book/test_fictiondb_client_parser.cpp (append)
TEST(FictionDbClientParser, ParsesAuthorSeriesIndexPage) {
    const QString html = loadFixture("fictiondb_author_series_a.html");
    bool hasNext = false;
    auto entries = FictionDbClient::parseSeriesIndexPage(html, &hasNext);
    ASSERT_GT(entries.size(), 50);                 // letter "A" page 1 is dense
    EXPECT_TRUE(hasNext);                           // page 1 of many → "»" present
    for (const auto& e : entries) {
        EXPECT_FALSE(e.seriesId.isEmpty());
        EXPECT_TRUE(e.seriesId.contains(QChar('~')));   // slug carries ~id
        EXPECT_FALSE(e.seriesName.isEmpty());
    }
}
```

- [ ] **Step 4: Run to confirm fail** — `_build_tests.bat` then `out/tankoban_tests.exe --gtest_filter=FictionDbClientParser.ParsesAuthorSeriesIndexPage`. Expected: FAIL (undefined symbol).

- [ ] **Step 5: Implement `parseSeriesIndexPage` in `FictionDbClient.cpp`**

Each row links a series: `href="../series/<slug>~<id>.htm">Series Name</a>`. Reuse the `slugFromHref(href,"series")` helper. Author derives from the slug tail or an adjacent cell; genre from an adjacent cell if present (else ""). The next-page link is an anchor whose text/title is "»"/"Next" pointing at `author-series~<letter>~<page>.htm`.

```cpp
QList<SeriesIndexEntry> FictionDbClient::parseSeriesIndexPage(const QString& html, bool* hasNextPage) {
    QList<SeriesIndexEntry> out;
    QRegularExpression rowRe(
        QStringLiteral("href=\"\\.\\./series/([a-z0-9~._-]+~\\d+)\\.htm\"[^>]*>\\s*([^<]+?)\\s*</a>"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = rowRe.globalMatch(html);
    QSet<QString> seen;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString slug = m.captured(1);
        // skip the pagination/nav author-series~<letter>~<n> links (no ~<digits> book/series id form they share;
        // filter those whose slug starts with "author-series")
        if (slug.startsWith(QLatin1String("author-series"))) continue;
        if (seen.contains(slug)) continue;
        seen.insert(slug);
        SeriesIndexEntry e;
        e.seriesId   = slug;
        e.seriesName = m.captured(2).trimmed();
        // author: trailing "<...>-<first>-<last>~<id>"; finalize against fixture (slug-derived fallback).
        out.append(e);
    }
    if (hasNextPage) {
        QRegularExpression nextRe(QStringLiteral("author-series~[a-z]~\\d+\\.htm"),
                                  QRegularExpression::CaseInsensitiveOption);
        *hasNextPage = nextRe.match(html).hasMatch();
    }
    return out;
}
```

**Note for implementer:** the row/author/genre selectors are best-effort starting points — open the committed `fictiondb_author_series_a.html` fixture, confirm the real `<tr>` shape, and tighten until the test passes (TDD fixture-first). Filter out the alphabetical-nav `author-series~*` links (they match `/series/` loosely).

- [ ] **Step 6: Run to confirm pass.** Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add tests/fixtures/book_catalogue/fictiondb_author_series_a.html src/core/book/FictionDbClient.{h,cpp} tests/core/book/test_fictiondb_client_parser.cpp
git commit -m "feat(books): FictionDbClient series-index-page parser + fixture (TDD)"
```

### Task 1.2: `fetchSeriesIndexPage` network method

**Files:** Modify `src/core/book/FictionDbClient.cpp`

- [ ] **Step 1: Implement the network method + reply slot** (mirror `fetchSeries`)

```cpp
void FictionDbClient::fetchSeriesIndexPage(const QString& letter, int page) {
    const QString path = page <= 1
        ? QStringLiteral("%1/series/author-series~%2.htm").arg(kBase, letter)
        : QStringLiteral("%1/series/author-series~%2~%3.htm").arg(kBase, letter).arg(page);
    auto* reply = m_nam->get(makeRequest(QUrl(path)));
    reply->setProperty("letter", letter);
    reply->setProperty("page", page);
    connect(reply, &QNetworkReply::finished, this, &FictionDbClient::onSeriesIndexReply);
}

void FictionDbClient::onSeriesIndexReply() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString letter = reply->property("letter").toString();
    const int page = reply->property("page").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit seriesIndexPageFailed(letter, page, reply->errorString());
        return;
    }
    bool hasNext = false;
    const QString html = QString::fromUtf8(reply->readAll());
    emit seriesIndexPageReady(letter, page, parseSeriesIndexPage(html, &hasNext), hasNext);
}
```

- [ ] **Step 2: build_check** — `taskkill //F //IM Tankoban.exe 2>/dev/null; ./build_check.bat`. Expected: BUILD OK.
- [ ] **Step 3: Commit** — `git commit -am "feat(books): FictionDbClient series-index-page fetch + reply routing"`

### Task 1.3: `BookSeriesIndex` — load / store / query (TDD the ranking)

**Files:**
- Create: `src/core/book/BookSeriesIndex.{h,cpp}`, `tests/core/book/test_book_series_index.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include "core/book/FictionDbClient.h"   // SeriesIndexEntry

// Local series catalogue for Books mode (BOOKS_FICTIONDB_CATALOGUE §4.2).
// Built from FictionDB's A–Z series directory; answers series search instantly,
// offline, series-first. JSON-backed (sibling of BooksCatalogueLibraryStore).
class BookSeriesIndex : public QObject {
    Q_OBJECT
public:
    explicit BookSeriesIndex(const QString& dataDir, QObject* parent = nullptr);

    // Load order: data-dir refresh copy if present + schema-valid, else bundled
    // resource (resources/book_series_index.json.gz). Sets m_entries + m_builtAt.
    void load(const QString& bundledResourcePath);

    // Case-insensitive ranked match on name (+author). exact > prefix > contains.
    QList<SeriesIndexEntry> query(const QString& text, int limit = 24) const;

    int size() const { return m_entries.size(); }
    qint64 builtAt() const { return m_builtAt; }

    // Used by the builder (Task 1.4) to persist a freshly-walked index.
    void setEntries(const QList<SeriesIndexEntry>& entries, qint64 builtAt);
    void save() const;   // writes <dataDir>/book_series_index.json

    static constexpr const char* FILENAME = "book_series_index.json";
    static constexpr int kSchemaVersion = 1;

    // Pure ranking primitive — exposed for tests.
    static int matchScore(const QString& query, const SeriesIndexEntry& e);

private:
    QString m_dataDir;
    QList<SeriesIndexEntry> m_entries;
    qint64 m_builtAt = 0;
};
```

- [ ] **Step 2: Write the failing ranking test**

```cpp
// tests/core/book/test_book_series_index.cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include "core/book/BookSeriesIndex.h"

static SeriesIndexEntry mk(const QString& name, const QString& author) {
    SeriesIndexEntry e; e.seriesId = name.toLower()+"~1"; e.seriesName = name; e.author = author; return e;
}

TEST(BookSeriesIndex, RanksExactBeforePrefixBeforeContains) {
    QTemporaryDir dir;
    BookSeriesIndex idx(dir.path());
    idx.setEntries({ mk("The Stormlight Archive","Brandon Sanderson"),
                     mk("Stormlight Shorts","Someone Else"),
                     mk("Light of the Storm","Other") }, 1);
    auto r = idx.query("stormlight", 10);
    ASSERT_GE(r.size(), 2);
    // "Stormlight Shorts" (prefix) should outrank "Light of the Storm" (contains)
    int idxShorts = -1, idxContains = -1;
    for (int i=0;i<r.size();++i){ if(r[i].seriesName=="Stormlight Shorts") idxShorts=i;
                                  if(r[i].seriesName=="Light of the Storm") idxContains=i; }
    EXPECT_GE(idxContains, 0); EXPECT_GE(idxShorts, 0);
    EXPECT_LT(idxShorts, idxContains);
}

TEST(BookSeriesIndex, RoundTripsThroughJson) {
    QTemporaryDir dir;
    { BookSeriesIndex a(dir.path()); a.setEntries({ mk("Dune Chronicles","Frank Herbert") }, 42); a.save(); }
    BookSeriesIndex b(dir.path());
    b.load(/*bundled*/ "");   // data-dir copy exists → loads it
    ASSERT_EQ(b.size(), 1);
    EXPECT_EQ(b.builtAt(), 42);
    EXPECT_EQ(b.query("dune", 5).size(), 1);
}
```

- [ ] **Step 3: Run to confirm fail.** Expected: FAIL (no symbols).

- [ ] **Step 4: Implement `BookSeriesIndex.cpp`** — `matchScore` (exact name=300, name prefix=200, name contains=100, author contains=+25; 0 = no match); `query` filters score>0, stable-sorts by score desc then name; `save`/`load` via `QJsonDocument` (array of `{id,name,author,genre}` + `{schemaVersion,builtAt}` header); `load` prefers data-dir copy (schema match) else decompresses+reads the bundled `.gz`.

- [ ] **Step 5: Register in CMakeLists** — add `src/core/book/BookSeriesIndex.cpp` to main `SOURCES`; add `tests/core/book/test_book_series_index.cpp` to `tankoban_tests`.

- [ ] **Step 6: Run to confirm pass** — `_build_tests.bat` + `out/tankoban_tests.exe --gtest_filter=BookSeriesIndex.*`. Expected: PASS.

- [ ] **Step 7: Commit** — `git add ...; git commit -m "feat(books): BookSeriesIndex JSON store + ranked query (TDD)"`

### Task 1.4: Index builder (walk A–Z) + bundled-resource generation

**Files:** Modify `src/core/book/BookSeriesIndex.{h,cpp}` (add a builder driver) + `src/ui/pages/BooksPage.cpp` (a dev-control trigger)

- [ ] **Step 1: Add a builder driver** — an object (or method on a small `BookSeriesIndexBuilder`) that, given a `FictionDbClient*`, walks `letter a..z`, page 1..N (following `hasNextPage`), accumulates `SeriesIndexEntry`s across `seriesIndexPageReady`, throttles ~1 req/sec, treats `seriesIndexPageFailed` as skip-and-continue, and on completion calls `BookSeriesIndex::setEntries(...) + save()` and emits `buildFinished(int count)`.

- [ ] **Step 2: Add a dev-control trigger** in `BooksPage::dispatchDevCommand`:

```cpp
if (cmd == QLatin1String("books_build_series_index")) {
    startSeriesIndexBuild();   // kicks the builder in background
    return replyOk(reply, {{"started", true}});
}
if (cmd == QLatin1String("books_series_index_status"))
    return replyOk(reply, {{"size", m_seriesIndex ? m_seriesIndex->size() : 0},
                           {"builtAt", m_seriesIndex ? (double)m_seriesIndex->builtAt() : 0}});
```

- [ ] **Step 3: build_check** — Expected: BUILD OK.
- [ ] **Step 4: Generate the bundled resource (one-time, during execution).** Launch the app, run the builder, gzip the result into the repo:

```bash
taskkill //F //IM Tankoban.exe 2>/dev/null; ./build_and_run.bat &   # background
# wait for dev-bridge, then:
out/tankoctl.exe books-build-series-index           # ~5 min, ~300 polite fetches
# poll until size stabilizes:
out/tankoctl.exe books-series-index-status
# copy the produced <dataDir>/book_series_index.json → repo, gzip it:
gzip -c "<dataDir>/book_series_index.json" > resources/book_series_index.json.gz
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

- [ ] **Step 5: Commit** — `git add src/core/book/BookSeriesIndex.* src/ui/pages/BooksPage.cpp resources/book_series_index.json.gz CMakeLists.txt; git commit -m "feat(books): series-index builder + bundled pre-built index resource"`

**Note:** the bundled `.gz` keeps the committed artifact ~1MB (vs ~6MB raw). If repo-health flags it, that's the size/no-first-launch-wait tradeoff from spec §4.2 — keep it gzipped.

### Task 1.5: Load the bundled index on launch + register the resource

**Files:** Modify `src/ui/pages/BooksPage.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Deploy the resource** — add `resources/book_series_index.json.gz` to the asset-deploy step (mirror how `resources/` assets reach the app dir in `build_and_run.bat`/CMake). Resolve its runtime path the same way other bundled resources resolve.
- [ ] **Step 2: Construct + load the index in `BooksPage`** (done structurally in Task 5.1; here just confirm `m_seriesIndex->load(bundledPath)` runs at construction so search has data immediately).
- [ ] **Step 3: build_check** — Expected: BUILD OK.
- [ ] **Step 4: Commit** — `git commit -am "feat(books): load bundled series index on launch (no first-run wait)"`

---

## Phase 2 — Aggregator two-track rework

### Task 2.1: Gut `SeriesDetector` to a types-only header

**Files:** Modify `src/core/book/SeriesDetector.h`; Delete `src/core/book/SeriesDetector.cpp`; Modify `CMakeLists.txt`, `tests/...` (drop the SeriesDetector test if present)

- [ ] **Step 1:** Reduce `SeriesDetector.h` to just the `SeriesGroup` struct (and `DetectionResult` if other code references it). Remove `detect()`, `parseSeriesTitlePattern()`, `romanToInt()` declarations. Keep the type name `SeriesDetector::SeriesGroup` so storefront/aggregator references don't change.

```cpp
#pragma once
#include <QList>
#include <QString>
#include "BookCatalogueResult.h"
// Wire-type for the catalogue series/standalone split. (Heuristic detection
// retired 2026-05-28 — series now come from FictionDB's explicit series index.)
class SeriesDetector {
public:
    struct SeriesGroup { QString seriesName; QString author; QList<BookCatalogueResult> books; };
};
```

- [ ] **Step 2:** `git rm src/core/book/SeriesDetector.cpp`; remove it (+ any `test_series_detector.cpp`) from `CMakeLists.txt`.
- [ ] **Step 3: build_check** — Expected: BUILD OK (aggregator still calls `detect()` → compile error until Task 2.3; if so, proceed to 2.2/2.3 before re-checking). Commit after Task 2.3 builds green.

### Task 2.2: Repoint `BookCatalogueAggregator` members + construction

**Files:** Modify `src/core/book/BookCatalogueAggregator.{h,cpp}`, `src/ui/pages/BooksPage.cpp`

- [ ] **Step 1:** In `BookCatalogueAggregator.h`: drop `OpenLibraryClient* m_openlib`, `GoogleBooksClient* m_googlebooks`, `m_openlibResults`/`m_googlebooksResults`/pending/succeeded flags + the `googleBooksApiKey` ctor param + `fetchAuthorWorks`. Add `FictionDbClient* m_fictiondb` + `BookSeriesIndex* m_index` (injected). New ctor: `BookCatalogueAggregator(FictionDbClient* fictiondb, BookSeriesIndex* index, QObject* parent)`. Keep `aggregateReady`/`aggregateFailed` signatures unchanged.
- [ ] **Step 2:** Update construction at `BooksPage.cpp:58` — remove the `googleKey` lines; construct `m_fictiondb` + `m_seriesIndex` first, then `new BookCatalogueAggregator(m_fictiondb, m_seriesIndex, this)`.
- [ ] **Step 3:** (build verified in Task 2.3.)

### Task 2.3: Two-track `query()` + re-rank (TDD the scorer)

**Files:** Modify `src/core/book/BookCatalogueAggregator.cpp`; Create `tests/core/book/test_catalogue_rerank.cpp`; Modify `CMakeLists.txt`

- [ ] **Step 1: Write the failing re-rank test** (pure static scorer)

```cpp
// tests/core/book/test_catalogue_rerank.cpp
#include <gtest/gtest.h>
#include "core/book/BookCatalogueAggregator.h"
TEST(CatalogueRerank, ExactTitleBeatsContains) {
    QList<BookCatalogueResult> in;
    BookCatalogueResult a; a.title="The Godfather's Revenge"; a.author="Mark Winegardner";
    BookCatalogueResult b; b.title="The Godfather"; b.author="Mario Puzo";
    in << a << b;
    auto out = BookCatalogueAggregator::rerankBooks("the godfather", in);
    EXPECT_EQ(out.first().title.toStdString(), "The Godfather");   // exact floats to #1
}
```

- [ ] **Step 2: Run to confirm fail.** Expected: FAIL.

- [ ] **Step 3: Implement** — add `static QList<BookCatalogueResult> rerankBooks(const QString& query, QList<BookCatalogueResult>)` (score exact-title > prefix-title > contains, +author bonus; stable sort desc; keep all). Rewrite `query(q)`:

```cpp
void BookCatalogueAggregator::query(const QString& q) {
    const int gen = ++m_generation;
    m_currentQuery = q;
    // Series track — instant, from the local index:
    QList<SeriesDetector::SeriesGroup> groups;
    for (const auto& e : m_index->query(q)) {
        BookCatalogueResult stub;
        stub.isSeries   = true;
        stub.catalogueId= QStringLiteral("fictiondb:%1").arg(e.seriesId);
        stub.seriesId   = e.seriesId;          // FictionDB series slug → fetchSeries on click
        stub.seriesName = e.seriesName;
        stub.title      = e.seriesName;
        stub.author     = e.author;
        groups.append({ e.seriesName, e.author, { stub } });
    }
    emit aggregateReady(q, groups, {});         // paint Series section now
    m_pendingGroups = groups;                   // retain for the second emit
    // Books track — live, async:
    m_fictiondb->search(q);                     // → onFictionSearch(gen)
}
```

Connect `FictionDbClient::searchResults` once in the ctor: if `q != m_currentQuery` (stale) drop; else `emit aggregateReady(q, m_pendingGroups, rerankBooks(q, books))`. On `FictionDbClient::searchFailed`, `emit aggregateReady(q, m_pendingGroups, {})` (series still show) — series track never fails.

- [ ] **Step 4: Run to confirm pass** + register the test in CMake. Expected: PASS.
- [ ] **Step 5: build_check** — Expected: BUILD OK (SeriesDetector::detect no longer called).
- [ ] **Step 6: Commit** — `git add ...; git commit -m "refactor(books): two-track aggregator (index series + live re-ranked books); retire SeriesDetector heuristics"`

### Task 2.4: Remove OL/GB/CatalogueDeduper from the build

**Files:** Modify `CMakeLists.txt`; Delete the now-unreferenced clients

- [ ] **Step 1:** `grep -rn "OpenLibraryClient\|GoogleBooksClient\|CatalogueDeduper" src/ tests/` — confirm only the now-removed aggregator referenced them.
- [ ] **Step 2:** `git rm` `OpenLibraryClient.{h,cpp}`, `GoogleBooksClient.{h,cpp}`, `CatalogueDeduper.{h,cpp}` + their tests; drop from `CMakeLists.txt`. (Spec §4.6 lean-delete; fiction-only is locked.)
- [ ] **Step 3: build_check + tests** — Expected: BUILD OK; `out/tankoban_tests.exe` green.
- [ ] **Step 4: Commit** — `git commit -am "chore(books): delete OpenLibrary/GoogleBooks/CatalogueDeduper (fiction-only, FictionDB sole source)"`

---

## Phase 3 — Storefront wiring + overflow cap

### Task 3.1: Add `seriesPicked` + route series-tile clicks

**Files:** Modify `src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}`

- [ ] **Step 1:** Add signal `void seriesPicked(const BookCatalogueResult& series);` In `addSeriesCard(group)`, wire the created `TileCard`'s click to `emit seriesPicked(group.books.first())` (the stub carrying `seriesId`). (Standalone `addBookCard` keeps emitting `bookPicked`.)
- [ ] **Step 2: build_check** — Expected: BUILD OK.
- [ ] **Step 3: Commit** — `git commit -am "feat(books): storefront emits seriesPicked for series tiles"`

### Task 3.2: `kInitialCap = 6` + "Show N more" per section

**Files:** Modify `src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}`

- [ ] **Step 1:** In `onCatalogueResult`, render at most `kInitialCap = 6` tiles per `TileStrip`; if a section has more, append a "Show N more" affordance that reveals the remainder (retain the full lists in members). Apply to both Series and Books sections.
- [ ] **Step 2: build_check** — Expected: BUILD OK.
- [ ] **Step 3: Commit** — `git commit -am "feat(books): storefront 6-tile cap + Show-more per section (§3.5)"`

---

## Phase 4 — `BookSeriesDetailView` (series-shape detail)

> **§4.4 realization (flagged for Hemanth at handoff):** per-book rows do NOT reimplement the §5.2 source picker inline. A row shows **[Read]** when the book is on disk (→ opens reader), else **[Get]** which routes to the existing movie-shape `BookCatalogueDetailView` for that specific book (where the full §5.2 source-search + picker + download already lives). DRY; lazy per D3 (no search until a row is acted on). On return, the row reflects the new library state via `recordsChanged`.

### Task 4.1: Skeleton — hero + ordered book rows

**Files:** Create `src/ui/pages/books/BookSeriesDetailView.{h,cpp}`; Modify `CMakeLists.txt`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <QWidget>
#include <QList>
#include <QHash>
#include "core/book/BookCatalogueResult.h"
class BooksCatalogueLibraryStore;
class QLabel; class QVBoxLayout; class QPushButton;

// Series-shape detail (BOOKS_FICTIONDB_CATALOGUE §4.4). Hero (series cover +
// name + author + count) + books in reading order, each row a per-book CTA.
class BookSeriesDetailView : public QWidget {
    Q_OBJECT
public:
    explicit BookSeriesDetailView(QWidget* parent = nullptr);
    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    void showSeries(const QString& seriesName, const QString& author,
                    const QString& coverPath, const QList<BookCatalogueResult>& books);
signals:
    void backRequested();
    void bookOpenRequested(const BookCatalogueResult& book); // → movie detail view (§5.2)
    void bookReadRequested(const QString& catalogueId, const QString& filePath);
private:
    void buildUi();
    void rebuildRows();
    BooksCatalogueLibraryStore* m_store = nullptr;
    QString m_seriesName, m_author;
    QList<BookCatalogueResult> m_books;     // ordered by seriesPosition
    QVBoxLayout* m_rowsLayout = nullptr;
    QLabel* m_heroTitle = nullptr; QLabel* m_heroMeta = nullptr; QLabel* m_heroCover = nullptr;
};
```

- [ ] **Step 2: Implement** `buildUi` (back button + hero labels + a scroll area whose content has `m_rowsLayout`), `showSeries` (set hero from name/author/cover + count, store `m_books`, call `rebuildRows`), and `rebuildRows` (clear + one row per book: cover thumb + "N. Title" + a per-book button). Button state per row from `m_store->hasRecord(book.catalogueId)`: record present → `[Read]` (click → `bookReadRequested(book.catalogueId, recordFor(...).filePath)`); else `[Get]` (click → `bookOpenRequested(book)`).
- [ ] **Step 3: Register in CMake + build_check** — Expected: BUILD OK.
- [ ] **Step 4: Commit** — `git commit -m "feat(books): BookSeriesDetailView skeleton (hero + ordered rows + per-book CTA)"`

### Task 4.2: Live per-row state on `recordsChanged`

**Files:** Modify `src/ui/pages/books/BookSeriesDetailView.cpp`

- [ ] **Step 1:** In `setCatalogueStore`, `connect(store, &BooksCatalogueLibraryStore::recordsChanged, this, [this]{ rebuildRows(); })` so a freshly-downloaded book's row flips to `[Read]` when the user returns. Guard `rebuildRows` against empty `m_books`.
- [ ] **Step 2: build_check** — Expected: BUILD OK.
- [ ] **Step 3: Commit** — `git commit -am "feat(books): series rows re-derive Read/Get on recordsChanged"`

---

## Phase 5 — `BooksPage` routing

### Task 5.1: Construct index + series view; load index

**Files:** Modify `src/ui/pages/BooksPage.{cpp,h}`

- [ ] **Step 1:** In `BooksPage.h`: add members `FictionDbClient* m_fictiondb`, `BookSeriesIndex* m_seriesIndex`, `BookSeriesDetailView* m_seriesDetailView`; forward-declare the classes.
- [ ] **Step 2:** In the ctor (near `BooksPage.cpp:56-75`): construct `m_fictiondb = new FictionDbClient(m_catalogueNam, this)`, `m_seriesIndex = new BookSeriesIndex(m_bridge->dataDir(), this)`, `m_seriesIndex->load(<bundled resource path>)`; pass both to the aggregator (Task 2.2); construct `m_seriesDetailView = new BookSeriesDetailView(this)`, `setCatalogueStore(m_catalogueStore)`, `m_stack->addWidget(m_seriesDetailView)`.
- [ ] **Step 3: build_check** — Expected: BUILD OK.
- [ ] **Step 4: Commit** — `git commit -am "feat(books): BooksPage constructs FictionDbClient + series index + series detail view"`

### Task 5.2: Wire routing + downloads

**Files:** Modify `src/ui/pages/BooksPage.cpp`

- [ ] **Step 1: Route series clicks** — `connect(m_catalogueSearchView, &BookCatalogueSearchWidget::seriesPicked, this, [this](const BookCatalogueResult& s){ m_fictiondb->fetchSeries(s.seriesId); });` and `connect(m_fictiondb, &FictionDbClient::seriesReady, this, [this](const QString&, const QString& name, const QList<BookCatalogueResult>& books){ const QString cover = books.isEmpty()? QString() : coverPathOrFetch(books.first()); m_seriesDetailView->showSeries(name, books.isEmpty()?QString():books.first().author, cover, books); m_stack->setCurrentWidget(m_seriesDetailView); });`
- [ ] **Step 2: Wire series-view downloads into §5.2** — `connect(m_seriesDetailView, &BookSeriesDetailView::bookReadRequested, this, &BooksPage::onCatalogueReadRequested);` and `connect(m_seriesDetailView, &BookSeriesDetailView::bookOpenRequested, this, [this](const BookCatalogueResult& b){ m_catalogueDetailReturnToSearch = false; m_catalogueDetailView->showBook(b, /*coverPath*/{}); m_stack->setCurrentWidget(m_catalogueDetailView); });` (the movie detail view then runs the existing §5.2 download flow for that book).
- [ ] **Step 3: Forward download lifecycle to BOTH detail views** — in `onBookDownloadProgress/Complete/Failed`, call the matching `notify*` on `m_catalogueDetailView` (already wired). The series view refreshes via `recordsChanged` after `onBookDownloadComplete` upserts the record — no extra forward needed. Confirm `backRequested` from the series view returns to the storefront/grid.
- [ ] **Step 4: build_check** — Expected: BUILD OK.
- [ ] **Step 5: Commit** — `git commit -am "feat(books): route series tile→series view→per-book movie detail; reuse §5.2 downloads"`

---

## Phase 6 — Build, smoke, RTC

### Task 6.1: Full build + live smoke

- [ ] **Step 1: Clean build** — `taskkill //F //IM Tankoban.exe 2>/dev/null; ./build_check.bat`. Expected: BUILD OK. Run `out/tankoban_tests.exe` — all green.
- [ ] **Step 2: Live smoke** via `build_and_run.bat` + tankoctl:

```bash
out/tankoctl.exe open-page books
out/tankoctl.exe books-series-index-status          # size > 0 (bundled index loaded, no wait)
out/tankoctl.exe books-search-library stormlight    # Series section instant: "The Stormlight Archive"
# drill into the series tile → series detail shows the books in order
# click book 1 [Get] → routes to movie detail → click a LibGen row → downloads → reader
out/tankoctl.exe books-get-library                  # confirm a record landed post-download
out/tankoctl.exe books-search-library "the godfather" # Books section re-ranked: Puzo near top
```
Expected: instant series tile from the index; standalone Books section re-ranked (Puzo floats up); per-book download via the movie detail view creates a record + opens the reader; the series row flips to `[Read]`.

- [ ] **Step 3: Capture evidence** — `out/tankoctl.exe dump-ui books > agents/audits/smoke_evidence/books_fictiondb_catalogue_$(date +%Y-%m-%d_%H%M%S).json` + a PNG via pywinauto if a visual beat needs eyes.
- [ ] **Step 4: Cleanup + RTC** — `powershell -NoProfile -File scripts/stop-tankoban.ps1`; post the RTC line in `agents/chat.md` (contracts-v3, `Skills invoked:` field) for Agent 0 sweep.

---

## Self-review

**Spec coverage:**
- D1 (drop OL+GB) → Task 2.4. ✓
- D2 (full series treatment) → Phase 4 + Phase 5. ✓
- D3 (lazy per-book) → §4.4 note + Task 4.1 (no search until a row's [Get] is clicked). ✓
- D4 (per-book only, no bulk) → no bulk button in Phase 4. ✓
- D5 (FictionDB covers) → series cover via `books.first()` cover (Task 5.2); book covers via existing cache. ✓
- D6 (hybrid) → reuse storefront/store/§5.2/movie-detail; new client-index/series-view. ✓
- D7 (local index / Option 2) → Phase 1. ✓
- D8 (two-track / Option A) → Task 2.3. ✓
- D9 (no dupes + re-rank) → Task 2.3 `rerankBooks` + test. ✓
- D10 (no catalog board) → not in scope; nothing builds genre rows. ✓
- §4.2 bundled index + refresh → Task 1.4/1.5 (refresh-cadence threshold = open call). ✓

**Placeholder scan:** Parser "finalize against fixture" notes (Task 1.1) are the deliberate TDD fixture-first pattern (og:/row DOM needs the real fixture), not placeholders — the surrounding structure + tests are concrete. No TBD/TODO elsewhere.

**Type consistency:** `SeriesIndexEntry` fields (`seriesId/seriesName/author/genre`) consistent across FictionDbClient ↔ BookSeriesIndex ↔ aggregator. `aggregateReady(QString, QList<SeriesDetector::SeriesGroup>, QList<BookCatalogueResult>)` signature preserved end-to-end (storefront slot unchanged). `seriesPicked(BookCatalogueResult)` carries `seriesId` → `fetchSeries(seriesId)` → `seriesReady(...)`. `bookOpenRequested(BookCatalogueResult)` → `showBook`. Re-rank scorer name `rerankBooks` used in both test and `query()`.

**Open implementation calls (Agent-2 Rule-14, at execution):** index JSON schema + bundled-resource runtime path resolution; refresh-cadence threshold; matchScore/rerank exact weights; whether `SeriesGroup` needs DetectionResult kept; series-detail cover fetch helper (`coverPathOrFetch`).
