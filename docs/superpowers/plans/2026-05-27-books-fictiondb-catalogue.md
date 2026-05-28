# Books FictionDB Catalogue Swap — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Open Library + Google Books with FictionDB as the sole Books-mode catalogue source, and build the series-shape detail view so multi-book series render as one tile → ordered book list with per-book download.

**Architecture:** Hybrid (Approach 3 from the spec). New `FictionDbClient` scraper replaces the OL+GB fan-out inside `BookCatalogueAggregator`. `SeriesDetector` heuristics retire in favour of FictionDB's explicit series pages. Existing data model (`BookCatalogueResult`/`CatalogueRecord`), library store, storefront widget, and §5.2 download flow are reused unchanged. New `BookSeriesDetailView` ports `StreamDetailView`'s table pattern. Standalone novels keep the existing movie-shape `BookCatalogueDetailView`.

**Tech Stack:** C++17, Qt6 (QNetworkAccessManager, QObject signals/slots), GoogleTest (pure-logic parser tests with frozen HTML fixtures). FictionDB scraped via Chrome-UA `QNetworkRequest` (Cloudflare-passive at moderate rate). Spec: `docs/superpowers/specs/2026-05-27-books-fictiondb-catalogue-design.md`.

**Verification:** `build_check.bat` BUILD OK after each phase; parser GoogleTests against frozen fixtures; final live smoke (search "dune" → series tile → series detail → per-book download → reader) via tankoctl.

---

## File Structure

| File | Responsibility | Disposition |
|------|----------------|-------------|
| `src/core/book/FictionDbClient.{h,cpp}` | Scrape FictionDB search/book/series/author pages → `BookCatalogueResult`s | CREATE |
| `tests/core/book/test_fictiondb_client_parser.cpp` | Pure-logic parser tests against frozen FictionDB HTML fixtures | CREATE |
| `tests/core/book/fixtures/fictiondb_dune_book.html` | Frozen real Dune book page | CREATE |
| `tests/core/book/fixtures/fictiondb_dune_series.html` | Frozen real Dune Chronicles series page | CREATE |
| `tests/core/book/fixtures/fictiondb_search_dune.html` | Frozen real search-results page for "dune" | CREATE |
| `src/core/book/BookCatalogueAggregator.{h,cpp}` | Drop OL+GB fan-out; FictionDB sole source | MODIFY |
| `src/ui/pages/books/BookSeriesDetailView.{h,cpp}` | Series-shape detail view (hero + ordered book table + per-book download) | CREATE |
| `src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}` | Add `kInitialCap=6` + "Show N more"; route series vs standalone clicks | MODIFY |
| `src/ui/pages/BooksPage.{cpp,h}` | Route series tile → series view, standalone → movie view; wire series-view download signals | MODIFY |
| `CMakeLists.txt` | Add FictionDbClient.cpp + BookSeriesDetailView.cpp to SOURCES; add test to tankoban_tests | MODIFY |

---

## Phase 1 — FictionDbClient scraper (TDD against frozen fixtures)

### Task 1.1: Capture frozen FictionDB fixtures

**Files:**
- Create: `tests/core/book/fixtures/fictiondb_dune_book.html`, `fictiondb_dune_series.html`, `fictiondb_search_dune.html`

- [ ] **Step 1: Fetch + save the three real pages with a browser UA**

```bash
UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"
mkdir -p tests/core/book/fixtures
curl -s -A "$UA" "https://www.fictiondb.com/title/dune~frank-herbert~99723.htm" -o tests/core/book/fixtures/fictiondb_dune_book.html
curl -s -A "$UA" "https://www.fictiondb.com/series/dune-chronicles-frank-herbert~3735.htm" -o tests/core/book/fixtures/fictiondb_dune_series.html
curl -s -A "$UA" "https://www.fictiondb.com/search-book.htm?q=dune" -o tests/core/book/fixtures/fictiondb_search_dune.html
```

- [ ] **Step 2: Verify each fixture is real content (not a Cloudflare challenge)**

```bash
for f in tests/core/book/fixtures/fictiondb_*.html; do
  echo "$f: $(wc -c < "$f") bytes, just-a-moment:$(grep -ci 'just a moment' "$f")"
done
```
Expected: each file is >20KB and `just-a-moment:0`. If any shows a challenge, retry with a pause; if persistent, the search URL may differ — open the real site, find the search endpoint, and adjust.

- [ ] **Step 3: Confirm the og: tags + key fields are present in the book fixture**

```bash
grep -oE '<meta property="og:[^"]*" content="[^"]{0,60}' tests/core/book/fixtures/fictiondb_dune_book.html | head -6
```
Expected: `og:title`, `og:isbn`, `og:type`, `og:image`, `og:url` present.

- [ ] **Step 4: Commit the fixtures**

```bash
git add tests/core/book/fixtures/fictiondb_*.html
git commit -m "test(books): freeze FictionDB Dune book/series/search fixtures for parser tests"
```

### Task 1.2: FictionDbClient header + book-page parser (the og:-tag core)

**Files:**
- Create: `src/core/book/FictionDbClient.h`
- Create: `src/core/book/FictionDbClient.cpp`
- Create: `tests/core/book/test_fictiondb_client_parser.cpp`

The parser is a free function so it's unit-testable without a live network (mirrors `OpenLibraryClient`'s `parseDoc` pattern). Signatures mirror `OpenLibraryClient` (`src/core/book/OpenLibraryClient.h:29-58`) and produce `BookCatalogueResult` (`src/core/book/BookCatalogueResult.h`: catalogueId/isbn/workId/title/author/publisher/year/language/description/genres/coverUrl/isSeries/seriesId/seriesName/seriesPosition/seriesTotal/pages).

- [ ] **Step 1: Write the failing test for the book-page parser**

```cpp
// tests/core/book/test_fictiondb_client_parser.cpp
#include <gtest/gtest.h>
#include <QFile>
#include "core/book/FictionDbClient.h"

static QString loadFixture(const QString& name) {
    QFile f(QStringLiteral(FICTIONDB_FIXTURE_DIR) + "/" + name);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QString::fromUtf8(f.readAll());
}

TEST(FictionDbClientParser, ParsesDuneBookPage) {
    const QString html = loadFixture("fictiondb_dune_book.html");
    BookCatalogueResult r = FictionDbClient::parseBookPage(html,
        "dune~frank-herbert~99723");
    EXPECT_EQ(r.catalogueId.toStdString(), "fictiondb:dune~frank-herbert~99723");
    EXPECT_EQ(r.title.toStdString(), "Dune");
    EXPECT_EQ(r.author.toStdString(), "Frank Herbert");
    EXPECT_EQ(r.isbn.toStdString(), "9780441172719");
    EXPECT_FALSE(r.coverUrl.isEmpty());
    EXPECT_FALSE(r.description.isEmpty());
    EXPECT_EQ(r.year.toStdString(), "1965");
    EXPECT_FALSE(r.isSeries);  // a book page is a single book, not the series record
}
```

- [ ] **Step 2: Run to confirm it fails (no FictionDbClient yet)**

Run: `cmake --build out --target tankoban_tests && cd out && ctest -R FictionDbClientParser --output-on-failure`
Expected: FAIL — `FictionDbClient.h` not found / `parseBookPage` undefined.

- [ ] **Step 3: Write FictionDbClient.h**

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include "core/book/BookCatalogueResult.h"

class QNetworkAccessManager;
class QNetworkReply;

// Scrapes FictionDB (fictiondb.com) — the fiction-only catalogue source that
// replaced Open Library + Google Books (BOOKS_FICTIONDB_CATALOGUE, 2026-05-27).
// Server-rendered HTML, parsed primarily via og: meta tags. Chrome-UA request
// passes FictionDB's passive Cloudflare. Parse functions are static + pure so
// they unit-test against frozen fixtures without a live network.
class FictionDbClient : public QObject {
    Q_OBJECT
public:
    explicit FictionDbClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    void search(const QString& query);
    void fetchSeries(const QString& seriesId);   // seriesId = FictionDB series slug
    void fetchBook(const QString& bookId);        // bookId   = FictionDB title slug
    void fetchAuthorWorks(const QString& authorId);

    // Pure parsers — exposed for unit tests.
    static BookCatalogueResult parseBookPage(const QString& html, const QString& bookId);
    static QList<BookCatalogueResult> parseSeriesPage(const QString& html, const QString& seriesId);
    struct SearchSplit { QList<BookCatalogueResult> series; QList<BookCatalogueResult> standalone; };
    static SearchSplit parseSearchPage(const QString& html);

signals:
    void searchResults(const QString& query,
                       const QList<BookCatalogueResult>& series,
                       const QList<BookCatalogueResult>& standalone);
    void searchFailed(const QString& query, const QString& error);
    void seriesReady(const QString& seriesId, const QString& seriesName,
                     const QString& author, const QList<BookCatalogueResult>& books);
    void seriesFailed(const QString& seriesId, const QString& error);
    void bookReady(const BookCatalogueResult& book);
    void bookFailed(const QString& bookId, const QString& error);
    void authorWorksReady(const QString& authorId, const QList<BookCatalogueResult>& works);

private:
    void onSearchReply();
    void onSeriesReply();
    void onBookReply();
    void onAuthorReply();
    QNetworkAccessManager* m_nam = nullptr;
    static constexpr const char* kBase = "https://www.fictiondb.com";
};
```

- [ ] **Step 4: Write the parseBookPage implementation in FictionDbClient.cpp**

```cpp
#include "core/book/FictionDbClient.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {
QNetworkRequest makeRequest(const QUrl& url) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0 Safari/537.36");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml");
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

// Extract the content attribute of an og: meta tag.
QString ogTag(const QString& html, const QString& prop) {
    QRegularExpression re(
        QStringLiteral("<meta\\s+property=\"og:%1\"\\s+content=\"([^\"]*)\"").arg(prop),
        QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(html);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}
}  // namespace

BookCatalogueResult FictionDbClient::parseBookPage(const QString& html, const QString& bookId) {
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("fictiondb:%1").arg(bookId);
    r.isSeries = false;

    // og:title is "Dune by Frank Herbert" — split on the last " by ".
    const QString ogTitle = ogTag(html, "title");
    const int byIdx = ogTitle.lastIndexOf(QStringLiteral(" by "));
    if (byIdx > 0) {
        r.title = ogTitle.left(byIdx).trimmed();
        r.author = ogTitle.mid(byIdx + 4).trimmed();
    } else {
        r.title = ogTitle;
    }
    r.isbn = ogTag(html, "isbn");
    r.coverUrl = ogTag(html, "image");

    // Description: og:description if present, else the synopsis div.
    r.description = ogTag(html, "description");
    if (r.description.isEmpty()) {
        QRegularExpression syn(QStringLiteral("id=\"synopsis\"[^>]*>(.*?)</div>"),
            QRegularExpression::DotMatchesEverythingOption);
        auto m = syn.match(html);
        if (m.hasMatch()) r.description = m.captured(1).remove(QRegularExpression("<[^>]+>")).trimmed();
    }

    // Year: first 4-digit run near "Published" / a (YYYY) token. Finalize against fixture.
    QRegularExpression yr(QStringLiteral("\\b(1[5-9]\\d{2}|20\\d{2})\\b"));
    auto ym = yr.match(html);
    if (ym.hasMatch()) r.year = ym.captured(1);

    return r;
}
```

**Note for implementer:** the year/synopsis/genre selectors above are best-effort starting points. Open the committed `fictiondb_dune_book.html` fixture, confirm the real DOM shape, and tighten the regexes so the Step-1 test passes against the actual content. The og: tags (title/isbn/image) are verified-present; the inline-HTML fields need fixture confirmation.

- [ ] **Step 5: Run the test to confirm it passes**

Run: `cmake --build out --target tankoban_tests && cd out && ctest -R FictionDbClientParser --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Register the test + fixture dir in CMakeLists + commit**

Add `test_fictiondb_client_parser.cpp` to the `tankoban_tests` sources and define `FICTIONDB_FIXTURE_DIR` pointing at `tests/core/book/fixtures`. Add `src/core/book/FictionDbClient.cpp` to the main `SOURCES`.

```bash
git add src/core/book/FictionDbClient.{h,cpp} tests/core/book/test_fictiondb_client_parser.cpp CMakeLists.txt
git commit -m "feat(books): FictionDbClient book-page parser (og:-tag core) + test"
```

### Task 1.3: parseSeriesPage — ordered book list

**Files:**
- Modify: `src/core/book/FictionDbClient.cpp`, `tests/core/book/test_fictiondb_client_parser.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST(FictionDbClientParser, ParsesDuneSeriesPage) {
    const QString html = loadFixture("fictiondb_dune_series.html");
    auto books = FictionDbClient::parseSeriesPage(html, "dune-chronicles-frank-herbert~3735");
    ASSERT_GE(books.size(), 6);  // Dune Chronicles has 6 Frank Herbert books
    EXPECT_EQ(books[0].title.toStdString(), "Dune");
    EXPECT_EQ(books[0].seriesPosition, 1);
    EXPECT_EQ(books[1].title.toStdString(), "Dune Messiah");
    EXPECT_EQ(books[1].seriesPosition, 2);
    for (const auto& b : books) {
        EXPECT_TRUE(b.seriesId.contains("dune-chronicles"));
        EXPECT_FALSE(b.title.isEmpty());
    }
}
```

- [ ] **Step 2: Run to confirm fail** — `ctest -R FictionDbClientParser`. Expected: FAIL (parseSeriesPage returns empty).

- [ ] **Step 3: Implement parseSeriesPage**

```cpp
QList<BookCatalogueResult> FictionDbClient::parseSeriesPage(
        const QString& html, const QString& seriesId) {
    QList<BookCatalogueResult> books;
    // FictionDB series pages list books in a table; each row links to
    // /title/<slug>.htm with the book title + position. Iterate the rows.
    // Selector finalized against fixture — starting pattern:
    QRegularExpression rowRe(
        QStringLiteral("href=\"/title/([^\"]+)\\.htm\"[^>]*>([^<]+)</a>"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = rowRe.globalMatch(html);
    int pos = 1;
    while (it.hasNext()) {
        auto m = it.next();
        BookCatalogueResult b;
        b.catalogueId = QStringLiteral("fictiondb:%1").arg(m.captured(1));
        b.title = m.captured(2).trimmed();
        b.isSeries = false;
        b.seriesId = QStringLiteral("fictiondb-series:%1").arg(seriesId);
        b.seriesPosition = pos++;
        books.append(b);
    }
    for (auto& b : books) b.seriesTotal = books.size();
    return books;
}
```

**Note for implementer:** confirm the row selector against `fictiondb_dune_series.html` — the real series-table markup may wrap titles differently (the regex above is the starting hypothesis). Tighten until the test passes; filter out non-book links (author links, nav) that match the loose pattern.

- [ ] **Step 4: Run to confirm pass** — `ctest -R FictionDbClientParser`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/book/FictionDbClient.cpp tests/core/book/test_fictiondb_client_parser.cpp
git commit -m "feat(books): FictionDbClient series-page parser (ordered book list)"
```

### Task 1.4: parseSearchPage — series/standalone split

**Files:**
- Modify: `src/core/book/FictionDbClient.cpp`, `tests/core/book/test_fictiondb_client_parser.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST(FictionDbClientParser, SplitsSearchResultsIntoSeriesAndStandalone) {
    const QString html = loadFixture("fictiondb_search_dune.html");
    auto split = FictionDbClient::parseSearchPage(html);
    // "dune" search should surface the Dune Chronicles series at least once
    bool foundSeries = false;
    for (const auto& s : split.series)
        if (s.isSeries && s.title.contains("Dune", Qt::CaseInsensitive)) foundSeries = true;
    EXPECT_TRUE(foundSeries);
    // standalone list entries are not series records
    for (const auto& b : split.standalone) EXPECT_FALSE(b.isSeries);
}
```

- [ ] **Step 2: Run to confirm fail.** Expected: FAIL.

- [ ] **Step 3: Implement parseSearchPage** — parse the search results, classify each hit as a series (links to `/series/...`) vs a standalone book (links to `/title/...` with no series), populate `isSeries` + `seriesId`/`seriesName` for series rows. Finalize selectors against the fixture.

- [ ] **Step 4: Run to confirm pass.** Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git commit -am "feat(books): FictionDbClient search-page parser (series/standalone split)"
```

### Task 1.5: Wire the network methods (search/fetchSeries/fetchBook/fetchAuthorWorks)

**Files:**
- Modify: `src/core/book/FictionDbClient.cpp`

- [ ] **Step 1: Implement the four network methods + reply slots**

Each method builds the FictionDB URL, fires `m_nam->get(makeRequest(url))`, connects `finished` to the matching `on*Reply` slot, and the slot reads the body, calls the matching pure parser, and emits the result signal (or the `*Failed` signal on HTTP error / empty parse). Pattern mirrors `OpenLibraryClient.cpp`'s `search()` + `onSearchReply()`.

```cpp
void FictionDbClient::search(const QString& query) {
    QUrl url(QStringLiteral("%1/search-book.htm").arg(kBase));
    QUrlQuery q; q.addQueryItem("q", query); url.setQuery(q);
    auto* reply = m_nam->get(makeRequest(url));
    reply->setProperty("query", query);
    connect(reply, &QNetworkReply::finished, this, &FictionDbClient::onSearchReply);
}

void FictionDbClient::onSearchReply() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString query = reply->property("query").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(query, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    auto split = parseSearchPage(html);
    emit searchResults(query, split.series, split.standalone);
}
```

(Implement `fetchSeries`/`onSeriesReply`, `fetchBook`/`onBookReply`, `fetchAuthorWorks`/`onAuthorReply` analogously, calling `parseSeriesPage`/`parseBookPage` and emitting `seriesReady`/`bookReady`/`authorWorksReady`.)

- [ ] **Step 2: build_check** — `TANKOBAN_BUILD_LANE=agent2 ./build_check.bat` (or shared lane if free). Expected: BUILD OK.

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(books): FictionDbClient network methods + reply routing"
```

---

## Phase 2 — Aggregator rework (FictionDB sole source)

### Task 2.1: Repoint BookCatalogueAggregator at FictionDbClient

**Files:**
- Modify: `src/core/book/BookCatalogueAggregator.{h,cpp}`

- [ ] **Step 1: Swap the members**

Replace the `OpenLibraryClient* m_openlib` + `GoogleBooksClient* m_googlebooks` members (and `m_openlibResults`/`m_googlebooksResults` at `BookCatalogueAggregator.h:78-79`) with `FictionDbClient* m_fictiondb`. Drop `SeriesDetector` from the pipeline (series come from FictionDB directly now). The `tryEmitAggregate()` merge collapses to "forward FictionDB's series/standalone split."

- [ ] **Step 2: Update `query()` + the aggregate signal**

`query(q)` → `m_fictiondb->search(q)`. The aggregator's `aggregateReady` signal now forwards FictionDB's `(series, standalone)` split. (If the storefront currently consumes a flat list, extend the signal to carry the split — see Task 3.1.)

- [ ] **Step 3: build_check** — Expected: BUILD OK (may surface unused OL/GB includes — leave the files, just unwire).

- [ ] **Step 4: Commit**

```bash
git commit -am "refactor(books): BookCatalogueAggregator → FictionDB sole source; retire SeriesDetector from pipeline"
```

---

## Phase 3 — Storefront routing + overflow cap

### Task 3.1: Storefront consumes the series/standalone split + kInitialCap

**Files:**
- Modify: `src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}`

- [ ] **Step 1: Render series hits in the Series section, standalone in the Books section**

The widget already has two sections (§3.5). Wire its result-handler to the aggregator's split signal: series list → Series section (each tile click → `seriesPicked(BookCatalogueResult)`), standalone list → Books section (each tile click → `bookPicked(BookCatalogueResult, coverPath)` — the existing signal).

- [ ] **Step 2: Add `kInitialCap = 6` + "Show N more" per section**

Cap each section's initial render at 6 tiles; if more, render a "Show N more" button that reveals the rest. (§3.5 polish, folded into this arc.)

- [ ] **Step 3: build_check** — Expected: BUILD OK.

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(books): storefront renders series/standalone split + 6-tile overflow cap"
```

---

## Phase 4 — BookSeriesDetailView (series-shape detail)

### Task 4.1: BookSeriesDetailView skeleton (hero + book table)

**Files:**
- Create: `src/ui/pages/books/BookSeriesDetailView.{h,cpp}`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Header — hero + ordered book rows + per-book action signal**

```cpp
#pragma once
#include <QWidget>
#include <QList>
#include "core/book/BookCatalogueResult.h"

class BooksCatalogueLibraryStore;
class QLabel; class QVBoxLayout; class QPushButton;

// Series-shape detail view (BOOKS_FICTIONDB_CATALOGUE §4.4). Ports
// StreamDetailView's table pattern + the 2026-05-20 Stormlight mockup.
// Hero (series cover + name + author + meta) + ordered book rows, each with
// a per-book [Search for downloads] → "Downloading XX%" → [Read] button.
// Lazy: a row searches sources only when its button is clicked (D3).
class BookSeriesDetailView : public QWidget {
    Q_OBJECT
public:
    explicit BookSeriesDetailView(QWidget* parent = nullptr);
    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    void showSeries(const QString& seriesName, const QString& author,
                    const QString& coverPath, const QList<BookCatalogueResult>& books);
    void notifyDownloadStarted(const QString& catalogueId, const QString& handle);
    void notifyDownloadProgress(const QString& handle, int pct);
    void notifyDownloadComplete(const QString& handle, const QString& filePath);
    void notifyDownloadFailed(const QString& handle, const QString& reason);
signals:
    void backRequested();
    void bookDownloadRequested(const BookCatalogueResult& book);
    void bookReadRequested(const QString& catalogueId, const QString& filePath);
private:
    void buildUi();
    void rebuildRows();
    BooksCatalogueLibraryStore* m_store = nullptr;
    QList<BookCatalogueResult> m_books;
    // ... hero labels, rows container, per-row button registry
};
```

- [ ] **Step 2: Implement buildUi + showSeries + rebuildRows**

Hero from the series metadata; one row per book in `m_books` (already ordered by `seriesPosition`). Each row: cover thumb + title + position + a per-book action button whose state derives from `m_store->hasRecord(book.catalogueId)` → `[Read]` else `[Search for downloads]`. Button click emits `bookDownloadRequested(book)` or `bookReadRequested(...)`. Reuse the `ClickableRow`/CTA-state patterns from `BookCatalogueDetailView`.

- [ ] **Step 3: Add to CMakeLists SOURCES + build_check** — Expected: BUILD OK.

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(books): BookSeriesDetailView skeleton (hero + ordered book table + per-book CTA)"
```

### Task 4.2: Per-book download state machine

**Files:**
- Modify: `src/ui/pages/books/BookSeriesDetailView.cpp`

- [ ] **Step 1: Implement the notify* methods + per-row button morphing**

Each `notify*` finds the row by `handle`/`catalogueId` and updates that row's button text (`Downloading XX%` / `Read`). On `recordsChanged` from the store, re-derive every row's state. Mirror `BookCatalogueDetailView::refreshPrimaryCta` but per-row.

- [ ] **Step 2: build_check** — Expected: BUILD OK.

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(books): BookSeriesDetailView per-book download state machine"
```

---

## Phase 5 — BooksPage routing

### Task 5.1: Route series vs standalone + wire series-view downloads

**Files:**
- Modify: `src/ui/pages/BooksPage.{cpp,h}`

- [ ] **Step 1: Construct + stack the BookSeriesDetailView**

Add `BookSeriesDetailView* m_seriesDetailView` member, construct it, `m_stack->addWidget()` it, `setCatalogueStore(m_catalogueStore)`.

- [ ] **Step 2: Route storefront clicks**

Connect storefront `seriesPicked` → `m_fictiondb->fetchSeries(seriesId)` → on `seriesReady`, `m_seriesDetailView->showSeries(...)` + `m_stack->setCurrentWidget(m_seriesDetailView)`. Standalone `bookPicked` → existing movie-shape `m_catalogueDetailView` path (unchanged).

- [ ] **Step 3: Wire series-view download signals into the existing §5.2 slots**

Connect `m_seriesDetailView::bookDownloadRequested` → reuse `onCatalogueDownloadRequested` (the §5.2 slot already builds the record + opens the reader on completion). Connect `bookReadRequested` → `onCatalogueReadRequested`. Forward `BookDownloader` progress/complete/failed to BOTH detail views (movie + series) — the one showing the active book reflects it.

- [ ] **Step 4: build_check** — Expected: BUILD OK.

- [ ] **Step 5: Commit**

```bash
git commit -am "feat(books): BooksPage routes series→series-view, standalone→movie-view; series downloads reuse §5.2"
```

---

## Phase 6 — Build, smoke, RTC

### Task 6.1: Full build + live smoke

- [ ] **Step 1: Clean build** — `./build_check.bat` (shared lane). Expected: BUILD OK.
- [ ] **Step 2: Launch + drive smoke via tankoctl**

```bash
./build_and_run.bat   # background
# wait for dev-bridge, then:
out/tankoctl.exe open-page books
out/tankoctl.exe books-search-library dune
# confirm series section shows Dune Chronicles; drill in; trigger a per-book download
out/tankoctl.exe books-get-library   # confirm a record lands post-download
```
Expected: search "dune" → Series section has Dune Chronicles → series detail shows 6 ordered books → clicking book 1's download fires the §5.2 flow → record created → reader opens.

- [ ] **Step 3: Capture smoke evidence**

```bash
out/tankoctl.exe dump-ui books > agents/audits/smoke_evidence/books_fictiondb_catalogue_$(date +%Y-%m-%d_%H%M%S).json
```

- [ ] **Step 4: Cleanup + RTC**

```bash
powershell -NoProfile -File scripts/stop-tankoban.ps1
```
Post the RTC line in `agents/chat.md` for Agent 0 sweep.

---

## Self-review

**Spec coverage:**
- D1 (drop OL+GB) → Phase 2 (aggregator repoint; OL/GB unwired). ✓
- D2 (full series treatment) → Phase 4 (BookSeriesDetailView) + Phase 5 (routing). ✓
- D3 (lazy per-book) → Task 4.1 Step 2 (row searches only on button click). ✓
- D4 (per-book only, no bulk) → no bulk button in Task 4.1. ✓
- D5 (FictionDB covers) → parseBookPage takes `og:image`; cover-cache reuse in Phase 3/5. ✓
- D6 (hybrid) → reuse data model + storefront + §5.2; new client + series view. ✓
- §4.1 FictionDbClient → Phase 1. §4.2 aggregator → Phase 2. §4.3 storefront → Phase 3. §4.4 series view → Phase 4. §4.6 routing → Phase 5. ✓
- §3.5 kInitialCap polish → Task 3.1 Step 2. ✓

**Placeholder scan:** The parser selector "finalize against fixture" notes are deliberate — FictionDB's exact inline-HTML DOM (beyond verified og: tags) requires the real fixture, captured in Task 1.1 before any parser is written. This is the TDD fixture-first pattern, not a placeholder; the og:-tag core (title/isbn/cover) has concrete code. Network methods + signal shapes are fully specified.

**Type consistency:** `BookCatalogueResult` fields used match `BookCatalogueResult.h` (catalogueId/isbn/title/author/year/coverUrl/description/isSeries/seriesId/seriesName/seriesPosition/seriesTotal). `catalogueId` prefix `fictiondb:` for books, `fictiondb-series:` for series membership — consistent across parseBookPage/parseSeriesPage. Signal names consistent: `searchResults`/`seriesReady`/`bookReady`. §5.2 reuse slots named per the shipped BooksPage (`onCatalogueDownloadRequested`/`onCatalogueReadRequested`).

**Open implementation calls (Rule 14, decided during execution):**
- Exact FictionDB search endpoint URL (`/search-book.htm?q=` is the hypothesis; confirm against the live site in Task 1.1).
- ISBN-13 → ISBN-10 conversion for cover URL if `og:image` ever absent (og:image is verified-present, so likely unneeded).
- Whether to fully delete vs keep-dormant OpenLibraryClient/GoogleBooksClient (default keep-dormant per spec §3.3).
