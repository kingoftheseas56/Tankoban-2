# BookWalker JP Per-Volume Covers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire BookWalker JP as the per-volume cover source for non-Premium Comics-mode series, surfaced through `ComicsSeriesView`'s existing col-2 QLabel-pixmap cells, with a blocking loading overlay until all data resolves.

**Architecture:** New `src/core/manga/bookwalker/` module (parallel to `anilist/` and `mangaupdates/`) containing two pure-logic primitives (`BookWalkerSeriesPageParser`, `VolumeCoverAlignment` — TDD-tested), one async HTTP client (`BookWalkerClient`), one disk cache (`BookWalkerCache` with TTL + count-drift invalidation), one orchestrator (`VolumeCoverResolver`) that runs the Premium short-circuit → AniList alt-title → MangaUpdates count → BookWalker search → BookWalker page parse → index-align → cache → emit chain. ComicsSeriesView calls the resolver on `openSeries` and paints rows from its result. Spec at `docs/superpowers/specs/2026-05-18-bookwalker-volume-covers-design.md` is authoritative for all decisions and rationale.

**Tech Stack:** C++20, Qt 6.10.2 (QObject + QNetworkAccessManager + QRegularExpression + QJsonDocument + QPixmapCache), MSVC2022, CMake + Ninja. GoogleTest via FetchContent for unit tests on the two pure-logic primitives (opt-in via `-DTANKOBAN_BUILD_TESTS=ON`).

---

## File Structure

**New files (`src/core/manga/bookwalker/`)**
- `BookWalkerTypes.h` — POD structs (`BookWalkerSearchHit`, `BookWalkerCoverEntry`, `BookWalkerCacheRecord`).
- `BookWalkerSeriesPageParser.h` + `BookWalkerSeriesPageParser.cpp` — pure-logic HTML parsers (search-results extraction + series-page cover extraction). Stateless, no Qt I/O. Unit-tested.
- `VolumeCoverAlignment.h` + `VolumeCoverAlignment.cpp` — pure-logic function aligning N BookWalker URLs to M MangaUpdates canonical count. Unit-tested.
- `BookWalkerClient.h` + `BookWalkerClient.cpp` — async HTTP client following `MangaUpdatesClient` conventions.
- `BookWalkerCache.h` + `BookWalkerCache.cpp` — disk-cache layer (one JSON per series-by-AniList-ID).
- `VolumeCoverResolver.h` + `VolumeCoverResolver.cpp` — orchestrator that runs the full chain.

**New test files**
- `tests/core/manga/bookwalker/test_bookwalker_series_page_parser.cpp`
- `tests/core/manga/bookwalker/test_volume_cover_alignment.cpp`
- `tests/fixtures/bookwalker/berserk_series_page.html` — frozen 2026-05-18 capture.
- `tests/fixtures/bookwalker/berserk_search_results.html` — frozen 2026-05-18 capture.

**Modified files**
- `CMakeLists.txt` — register the 6 new source files; register the 2 new test files under `tankoban_tests`.
- `src/ui/pages/comics/ComicsSeriesView.h` — add `VolumeCoverResolver*` member, loading state, slot signatures, 10s safety timer.
- `src/ui/pages/comics/ComicsSeriesView.cpp` — wire `openSeries` → loading overlay → resolver → success/failure handlers.
- `src/ui/widgets/ComicsSeriesViewLoadingOverlay.h` + `.cpp` (NEW) OR move-and-share existing `src/ui/player/LoadingOverlay.{h,cpp}` — decision in Task 17.

---

## Phase 1 — Pure-logic primitives (TDD)

### Task 1: Add `BookWalkerTypes.h`

**Files:**
- Create: `src/core/manga/bookwalker/BookWalkerTypes.h`

- [ ] **Step 1: Create the header**

```cpp
#pragma once

#include <QList>
#include <QString>
#include <QDateTime>

namespace tankoban::manga::bookwalker {

struct BookWalkerSearchHit {
    QString seriesId;
    QString title;
};

struct BookWalkerCoverEntry {
    int volume = 0;
    QString url;
};

struct BookWalkerCacheRecord {
    int schemaVersion = 1;
    QDateTime fetchedAt;
    int canonicalCount = 0;
    QString bookwalkerSeriesId;
    QList<BookWalkerCoverEntry> volumes;
};

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 2: Commit**

```bash
git add src/core/manga/bookwalker/BookWalkerTypes.h
git commit -m "feat(manga/bookwalker): add BookWalkerTypes POD structs"
```

---

### Task 2: Snapshot Berserk series-page HTML fixture

**Files:**
- Create: `tests/fixtures/bookwalker/berserk_series_page.html`

- [ ] **Step 1: Fetch and save the live HTML (must run from project root, PowerShell)**

```powershell
New-Item -ItemType Directory -Force tests/fixtures/bookwalker | Out-Null
Invoke-WebRequest -Uri "https://bookwalker.jp/series/16664/list/" -UserAgent "Mozilla/5.0" -UseBasicParsing |
    Select-Object -ExpandProperty Content |
    Out-File -Encoding utf8 tests/fixtures/bookwalker/berserk_series_page.html
```

- [ ] **Step 2: Verify the fixture contains data-original attributes**

```powershell
(Get-Content tests/fixtures/bookwalker/berserk_series_page.html -Raw | Select-String -Pattern 'data-original="https://rimg\.bookwalker\.jp/' -AllMatches).Matches.Count
```

Expected: 30 or more matches (live probe 2026-05-18 returned 30+ in head-30; real count likely 43–57 including omnibus editions).

- [ ] **Step 3: Commit**

```bash
git add tests/fixtures/bookwalker/berserk_series_page.html
git commit -m "test(manga/bookwalker): freeze Berserk series-page HTML fixture (2026-05-18)"
```

---

### Task 3: Snapshot Berserk search-results HTML fixture

**Files:**
- Create: `tests/fixtures/bookwalker/berserk_search_results.html`

- [ ] **Step 1: Fetch and save the live HTML**

```powershell
Invoke-WebRequest -Uri "https://bookwalker.jp/search/?word=%E3%83%99%E3%83%AB%E3%82%BB%E3%83%AB%E3%82%AF" -UserAgent "Mozilla/5.0" -UseBasicParsing |
    Select-Object -ExpandProperty Content |
    Out-File -Encoding utf8 tests/fixtures/bookwalker/berserk_search_results.html
```

- [ ] **Step 2: Verify the fixture contains Berserk-proper at series-id 16664**

```powershell
Select-String -Path tests/fixtures/bookwalker/berserk_search_results.html -Pattern 'data-series-id="16664"' -Quiet
```

Expected: `True`

- [ ] **Step 3: Commit**

```bash
git add tests/fixtures/bookwalker/berserk_search_results.html
git commit -m "test(manga/bookwalker): freeze Berserk search-results HTML fixture (2026-05-18)"
```

---

### Task 4: TDD `BookWalkerSeriesPageParser` — series page cover extraction

**Files:**
- Create: `src/core/manga/bookwalker/BookWalkerSeriesPageParser.h`
- Create: `src/core/manga/bookwalker/BookWalkerSeriesPageParser.cpp`
- Create: `tests/core/manga/bookwalker/test_bookwalker_series_page_parser.cpp`

- [ ] **Step 1: Write the failing test for series-page extraction**

```cpp
// tests/core/manga/bookwalker/test_bookwalker_series_page_parser.cpp
#include "core/manga/bookwalker/BookWalkerSeriesPageParser.h"

#include <gtest/gtest.h>
#include <QFile>
#include <QString>

using tankoban::manga::bookwalker::BookWalkerSeriesPageParser;
using tankoban::manga::bookwalker::BookWalkerSearchHit;

namespace {

QString loadFixture(const QString& relPath) {
    QFile f(QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

} // namespace

TEST(BookWalkerSeriesPageParser, ExtractsCoverUrlsFromBerserkFixture) {
    QString html = loadFixture(QStringLiteral("bookwalker/berserk_series_page.html"));
    ASSERT_FALSE(html.isEmpty()) << "Fixture missing";

    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);

    EXPECT_GE(urls.size(), 30) << "Live probe 2026-05-18 found 30+ covers; regression if fewer";
    for (const QString& url : urls) {
        EXPECT_TRUE(url.startsWith(QStringLiteral("https://rimg.bookwalker.jp/"))) << url.toStdString();
        EXPECT_TRUE(url.endsWith(QStringLiteral(".jpg")) || url.endsWith(QStringLiteral(".png")) || url.endsWith(QStringLiteral(".webp")));
    }
}

TEST(BookWalkerSeriesPageParser, DeduplicatesCoverUrls) {
    QString html = QStringLiteral(
        R"(<img data-original="https://rimg.bookwalker.jp/AAA.jpg">)"
        R"(<img data-original="https://rimg.bookwalker.jp/BBB.jpg">)"
        R"(<img data-original="https://rimg.bookwalker.jp/AAA.jpg">)"
    );
    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);
    ASSERT_EQ(urls.size(), 2);
    EXPECT_EQ(urls[0], QStringLiteral("https://rimg.bookwalker.jp/AAA.jpg"));
    EXPECT_EQ(urls[1], QStringLiteral("https://rimg.bookwalker.jp/BBB.jpg"));
}

TEST(BookWalkerSeriesPageParser, ReturnsEmptyOnNoMatch) {
    QString html = QStringLiteral("<html><body><img src='unrelated.jpg'/></body></html>");
    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);
    EXPECT_TRUE(urls.isEmpty());
}
```

- [ ] **Step 2: Run the test to verify it fails (no header yet)**

```bash
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
```

Expected: compile failure on missing `BookWalkerSeriesPageParser.h`.

- [ ] **Step 3: Write the header**

```cpp
// src/core/manga/bookwalker/BookWalkerSeriesPageParser.h
#pragma once

#include "BookWalkerTypes.h"

#include <QList>
#include <QString>

namespace tankoban::manga::bookwalker {

class BookWalkerSeriesPageParser
{
public:
    // Series-page parsing: extract ordered, deduplicated cover URLs from data-original attrs.
    static QList<QString> extractCoverUrls(const QString& html);

    // Search-results parsing: extract (series-id, title) pairs from data-series-id + img alt attrs.
    static QList<BookWalkerSearchHit> extractSearchHits(const QString& html);

    // Disambiguation: pick the series-id whose title (after stripping parenthetical
    // publisher suffix like "（ヤングアニマル）") equals targetJapaneseTitle exactly.
    // Returns empty QString if no exact match.
    static QString pickSeriesIdByTitle(const QList<BookWalkerSearchHit>& hits,
                                       const QString& targetJapaneseTitle);
};

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 4: Write the implementation**

```cpp
// src/core/manga/bookwalker/BookWalkerSeriesPageParser.cpp
#include "BookWalkerSeriesPageParser.h"

#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QSet>

namespace tankoban::manga::bookwalker {

QList<QString> BookWalkerSeriesPageParser::extractCoverUrls(const QString& html)
{
    static const QRegularExpression re(
        QStringLiteral(R"(data-original=["'](https://rimg\.bookwalker\.jp/[^"']+\.(?:jpg|png|webp))["'])"),
        QRegularExpression::CaseInsensitiveOption);

    QList<QString> out;
    QSet<QString> seen;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const QString url = it.next().captured(1);
        if (!seen.contains(url)) {
            seen.insert(url);
            out.append(url);
        }
    }
    return out;
}

QList<BookWalkerSearchHit> BookWalkerSeriesPageParser::extractSearchHits(const QString& html)
{
    // Anchor pattern: <a ... data-series-id="<id>" ...> ... <img ... alt="<title>" ... />
    static const QRegularExpression re(
        QStringLiteral(R"(<a[^>]*\bdata-series-id=["'](\d+)["'][^>]*>[\s\S]*?<img[^>]*\balt=["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);

    QList<BookWalkerSearchHit> out;
    QSet<QString> seenIds;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        const QString id = m.captured(1);
        if (seenIds.contains(id)) continue;
        seenIds.insert(id);
        BookWalkerSearchHit hit;
        hit.seriesId = id;
        hit.title = m.captured(2);
        out.append(hit);
    }
    return out;
}

QString BookWalkerSeriesPageParser::pickSeriesIdByTitle(const QList<BookWalkerSearchHit>& hits,
                                                       const QString& targetJapaneseTitle)
{
    static const QRegularExpression stripParens(
        QStringLiteral(R"([\(（][^\)）]*[\)）])"));

    const QString needle = targetJapaneseTitle.trimmed();
    for (const auto& h : hits) {
        QString normalized = h.title;
        normalized.replace(stripParens, QString());
        normalized = normalized.trimmed();
        if (normalized == needle) {
            return h.seriesId;
        }
    }
    return QString();
}

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 5: Add tests for the search-hit extraction + disambiguation**

Append to `tests/core/manga/bookwalker/test_bookwalker_series_page_parser.cpp`:

```cpp
TEST(BookWalkerSeriesPageParser, ExtractsSearchHitsFromFixture) {
    QString html = loadFixture(QStringLiteral("bookwalker/berserk_search_results.html"));
    ASSERT_FALSE(html.isEmpty());

    auto hits = BookWalkerSeriesPageParser::extractSearchHits(html);
    ASSERT_FALSE(hits.isEmpty());

    // Live probe 2026-05-18 found id 16664 with title "ベルセルク（ヤングアニマル）".
    bool found = false;
    for (const auto& h : hits) {
        if (h.seriesId == QStringLiteral("16664")) {
            found = true;
            EXPECT_TRUE(h.title.contains(QString::fromUtf8("ベルセルク")));
            break;
        }
    }
    EXPECT_TRUE(found) << "Berserk-proper (id 16664) missing from search hits";
}

TEST(BookWalkerSeriesPageParser, PicksSeriesIdByExactTitleAfterStrippingParens) {
    QList<BookWalkerSearchHit> hits = {
        {QStringLiteral("16664"), QString::fromUtf8("ベルセルク（ヤングアニマル）")},
        {QStringLiteral("175790"), QString::fromUtf8("暴食のベルセルク")},
        {QStringLiteral("139162"), QString::fromUtf8("ベルセルク アナリストブック")},
    };
    QString id = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, QString::fromUtf8("ベルセルク"));
    EXPECT_EQ(id, QStringLiteral("16664"));
}

TEST(BookWalkerSeriesPageParser, ReturnsEmptyIdOnNoExactMatch) {
    QList<BookWalkerSearchHit> hits = {
        {QStringLiteral("175790"), QString::fromUtf8("暴食のベルセルク")},
    };
    QString id = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, QString::fromUtf8("ベルセルク"));
    EXPECT_TRUE(id.isEmpty());
}
```

- [ ] **Step 6: Run all parser tests, expect green**

```bash
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R BookWalkerSeriesPageParser
```

Expected: all tests PASS (6 cases).

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/bookwalker/BookWalkerSeriesPageParser.h \
        src/core/manga/bookwalker/BookWalkerSeriesPageParser.cpp \
        tests/core/manga/bookwalker/test_bookwalker_series_page_parser.cpp
git commit -m "feat(manga/bookwalker): add BookWalkerSeriesPageParser with TDD"
```

---

### Task 5: TDD `VolumeCoverAlignment`

**Files:**
- Create: `src/core/manga/bookwalker/VolumeCoverAlignment.h`
- Create: `src/core/manga/bookwalker/VolumeCoverAlignment.cpp`
- Create: `tests/core/manga/bookwalker/test_volume_cover_alignment.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/core/manga/bookwalker/test_volume_cover_alignment.cpp
#include "core/manga/bookwalker/VolumeCoverAlignment.h"

#include <gtest/gtest.h>
#include <QString>
#include <QList>

using tankoban::manga::bookwalker::VolumeCoverAlignment;

namespace {

QList<QString> mkUrls(int n) {
    QList<QString> out;
    for (int i = 1; i <= n; ++i) {
        out.append(QStringLiteral("https://rimg.bookwalker.jp/v%1.jpg").arg(i, 7, 10, QChar('0')));
    }
    return out;
}

} // namespace

TEST(VolumeCoverAlignment, ExactCountAlignsOneToOne) {
    auto urls = mkUrls(43);
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/43);
    ASSERT_EQ(m.size(), 43);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[43], urls[42]);
}

TEST(VolumeCoverAlignment, OverflowDropsTail) {
    auto urls = mkUrls(60); // 43 regular + 17 omnibus/special
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/43);
    ASSERT_EQ(m.size(), 43);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[43], urls[42]);
    EXPECT_FALSE(m.contains(44));
}

TEST(VolumeCoverAlignment, ShortfallMapsWhatWeHave) {
    auto urls = mkUrls(30); // BookWalker missing some
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/43);
    ASSERT_EQ(m.size(), 30);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[30], urls[29]);
    EXPECT_FALSE(m.contains(31));
}

TEST(VolumeCoverAlignment, ZeroCanonicalReturnsAllAsIs) {
    // Degraded path: MangaUpdates count unavailable, use BookWalker raw count.
    auto urls = mkUrls(5);
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/0);
    ASSERT_EQ(m.size(), 5);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[5], urls[4]);
}

TEST(VolumeCoverAlignment, EmptyInputReturnsEmpty) {
    auto m = VolumeCoverAlignment::align(QList<QString>{}, 10);
    EXPECT_TRUE(m.isEmpty());
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build out --target tankoban_tests
```

Expected: compile failure on missing header.

- [ ] **Step 3: Write the header**

```cpp
// src/core/manga/bookwalker/VolumeCoverAlignment.h
#pragma once

#include <QList>
#include <QMap>
#include <QString>

namespace tankoban::manga::bookwalker {

class VolumeCoverAlignment
{
public:
    // Map BookWalker URLs to canonical volume indices [1..N].
    // - canonicalCount > 0: take first canonicalCount URLs (drop overflow); shortfall is honest.
    // - canonicalCount == 0: degraded mode — map every URL as-is at indices [1..rawCount].
    // - urls empty: returns empty map.
    static QMap<int, QString> align(const QList<QString>& orderedCoverUrls,
                                    int canonicalCount);
};

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 4: Write the implementation**

```cpp
// src/core/manga/bookwalker/VolumeCoverAlignment.cpp
#include "VolumeCoverAlignment.h"

namespace tankoban::manga::bookwalker {

QMap<int, QString> VolumeCoverAlignment::align(const QList<QString>& orderedCoverUrls,
                                               int canonicalCount)
{
    QMap<int, QString> out;
    const int rawCount = orderedCoverUrls.size();
    if (rawCount == 0) return out;

    const int take = (canonicalCount > 0)
        ? qMin(canonicalCount, rawCount)
        : rawCount;

    for (int i = 0; i < take; ++i) {
        out.insert(i + 1, orderedCoverUrls[i]);
    }
    return out;
}

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 5: Run tests, expect green**

```bash
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R VolumeCoverAlignment
```

Expected: all 5 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/manga/bookwalker/VolumeCoverAlignment.h \
        src/core/manga/bookwalker/VolumeCoverAlignment.cpp \
        tests/core/manga/bookwalker/test_volume_cover_alignment.cpp
git commit -m "feat(manga/bookwalker): add VolumeCoverAlignment with TDD"
```

---

### Task 6: Wire test sources into `CMakeLists.txt`

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Locate the `if(TANKOBAN_BUILD_TESTS)` block**

Search for: `tankoban_tests`. There should be an existing `target_sources(tankoban_tests PRIVATE ...)` block listing other test sources.

- [ ] **Step 2: Append the two new test sources and the fixture-dir define**

Add inside the existing `target_sources(tankoban_tests PRIVATE ...)` list:
```cmake
        tests/core/manga/bookwalker/test_bookwalker_series_page_parser.cpp
        tests/core/manga/bookwalker/test_volume_cover_alignment.cpp
```

Add after the existing tests target_compile_definitions (or create one if missing):
```cmake
target_compile_definitions(tankoban_tests PRIVATE
    TANKOBAN_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
```

(If `TANKOBAN_TEST_FIXTURE_DIR` already exists, do not duplicate.)

- [ ] **Step 3: Add the two new module sources to the main app**

Find the existing manga source registration block (search `MangaUpdatesClient.cpp` in `CMakeLists.txt`). Append next to it:

```cmake
        src/core/manga/bookwalker/BookWalkerSeriesPageParser.cpp
        src/core/manga/bookwalker/VolumeCoverAlignment.cpp
```

- [ ] **Step 4: Configure + build + run tests, expect green**

```bash
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R "BookWalker|VolumeCover"
```

Expected: 11 tests PASS (6 parser + 5 alignment).

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(manga/bookwalker): register parser/alignment sources + tests in CMake"
```

---

## Phase 2 — HTTP client (`BookWalkerClient`)

### Task 7: Author `BookWalkerClient.h`

**Files:**
- Create: `src/core/manga/bookwalker/BookWalkerClient.h`

- [ ] **Step 1: Write the header following `MangaUpdatesClient` shape**

```cpp
// src/core/manga/bookwalker/BookWalkerClient.h
#pragma once

#include "BookWalkerTypes.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::bookwalker {

// Async HTTP client for bookwalker.jp series-search and series-page parsing.
// Pattern mirrors MangaUpdatesClient: caller-allocated NAM, requestId correlation,
// signal/slot completion, soft throttling.
class BookWalkerClient : public QObject
{
    Q_OBJECT
public:
    explicit BookWalkerClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~BookWalkerClient() override;

    // GET bookwalker.jp/search/?word=<japaneseTitle>, extract all search hits.
    void searchSeries(const QString& japaneseTitle, int requestId);

    // GET bookwalker.jp/series/<seriesId>/list/, extract ordered cover URLs.
    void fetchSeriesCovers(const QString& bookwalkerSeriesId, int requestId);

signals:
    void searchSucceeded(int requestId, const QList<BookWalkerSearchHit>& hits);
    void searchFailed(int requestId, const QString& reason);

    void coversSucceeded(int requestId, const QList<QString>& orderedCoverUrls);
    void coversFailed(int requestId, const QString& reason);

private slots:
    void onSearchReplyFinished();
    void onCoversReplyFinished();

private:
    void throttleIfNeeded();

    QPointer<QNetworkAccessManager> m_nam;
    qint64 m_lastRequestMs = 0;
};

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 2: Commit**

```bash
git add src/core/manga/bookwalker/BookWalkerClient.h
git commit -m "feat(manga/bookwalker): add BookWalkerClient header"
```

---

### Task 8: Implement `BookWalkerClient.cpp`

**Files:**
- Create: `src/core/manga/bookwalker/BookWalkerClient.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
// src/core/manga/bookwalker/BookWalkerClient.cpp
#include "BookWalkerClient.h"
#include "BookWalkerSeriesPageParser.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

namespace tankoban::manga::bookwalker {

namespace {
constexpr int kHttpTimeoutMs = 10'000;
constexpr int kThrottleGapMs = 250;   // soft inter-request gap
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Tankoban/1.0 (manga-bookwalker)";

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    req.setTransferTimeout(kHttpTimeoutMs);
    return req;
}
} // namespace

BookWalkerClient::BookWalkerClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

BookWalkerClient::~BookWalkerClient() = default;

void BookWalkerClient::throttleIfNeeded()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 sinceLast = now - m_lastRequestMs;
    if (m_lastRequestMs != 0 && sinceLast < kThrottleGapMs) {
        QThread::msleep(static_cast<unsigned long>(kThrottleGapMs - sinceLast));
    }
    m_lastRequestMs = QDateTime::currentMSecsSinceEpoch();
}

void BookWalkerClient::searchSeries(const QString& japaneseTitle, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("nam-null"));
        return;
    }
    throttleIfNeeded();
    QUrl url(QStringLiteral("https://bookwalker.jp/search/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("word"), japaneseTitle);
    url.setQuery(q);

    QNetworkReply* reply = m_nam->get(makeRequest(url));
    reply->setProperty("requestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &BookWalkerClient::onSearchReplyFinished);
}

void BookWalkerClient::fetchSeriesCovers(const QString& bookwalkerSeriesId, int requestId)
{
    if (!m_nam) {
        emit coversFailed(requestId, QStringLiteral("nam-null"));
        return;
    }
    if (bookwalkerSeriesId.isEmpty()) {
        emit coversFailed(requestId, QStringLiteral("empty-series-id"));
        return;
    }
    throttleIfNeeded();
    const QUrl url(QStringLiteral("https://bookwalker.jp/series/%1/list/").arg(bookwalkerSeriesId));

    QNetworkReply* reply = m_nam->get(makeRequest(url));
    reply->setProperty("requestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &BookWalkerClient::onCoversReplyFinished);
}

void BookWalkerClient::onSearchReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const int requestId = reply->property("requestId").toInt();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    auto hits = BookWalkerSeriesPageParser::extractSearchHits(html);
    emit searchSucceeded(requestId, hits);
}

void BookWalkerClient::onCoversReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const int requestId = reply->property("requestId").toInt();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit coversFailed(requestId, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);
    if (urls.isEmpty()) {
        emit coversFailed(requestId, QStringLiteral("parse-failed-zero-data-original"));
        return;
    }
    emit coversSucceeded(requestId, urls);
}

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 2: Register the source in `CMakeLists.txt`**

Append next to the existing `BookWalkerSeriesPageParser.cpp` line added in Task 6:

```cmake
        src/core/manga/bookwalker/BookWalkerClient.cpp
```

- [ ] **Step 3: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/bookwalker/BookWalkerClient.cpp CMakeLists.txt
git commit -m "feat(manga/bookwalker): implement BookWalkerClient async HTTP"
```

---

## Phase 3 — Cache layer (`BookWalkerCache`)

### Task 9: Author `BookWalkerCache.h`

**Files:**
- Create: `src/core/manga/bookwalker/BookWalkerCache.h`

- [ ] **Step 1: Write the header**

```cpp
// src/core/manga/bookwalker/BookWalkerCache.h
#pragma once

#include "BookWalkerTypes.h"

#include <QString>
#include <optional>

namespace tankoban::manga::bookwalker {

class BookWalkerCache
{
public:
    // Default TTL = 7 days (per spec Decision #6).
    static constexpr qint64 kDefaultTtlSeconds = 7 * 24 * 60 * 60;

    // Storage path: <AppDataLocation>/cache/bookwalker_covers/<anilistId>.json
    static QString cacheFilePath(int anilistId);

    // Load + validate. Returns nullopt if:
    //  - file missing / unreadable / malformed
    //  - now - fetchedAt > ttlSeconds
    //  - record.canonicalCount != currentCanonicalCount (drift)
    // Pass currentCanonicalCount == 0 to skip the drift check (degraded MangaUpdates path).
    static std::optional<BookWalkerCacheRecord> load(int anilistId,
                                                     int currentCanonicalCount,
                                                     qint64 ttlSeconds = kDefaultTtlSeconds);

    // Atomic write (tmp + rename). Returns true on success.
    static bool store(int anilistId, const BookWalkerCacheRecord& record);
};

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 2: Commit**

```bash
git add src/core/manga/bookwalker/BookWalkerCache.h
git commit -m "feat(manga/bookwalker): add BookWalkerCache header"
```

---

### Task 10: Implement `BookWalkerCache.cpp`

**Files:**
- Create: `src/core/manga/bookwalker/BookWalkerCache.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
// src/core/manga/bookwalker/BookWalkerCache.cpp
#include "BookWalkerCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace tankoban::manga::bookwalker {

QString BookWalkerCache::cacheFilePath(int anilistId)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QStringLiteral("%1/cache/bookwalker_covers/%2.json").arg(root).arg(anilistId);
}

std::optional<BookWalkerCacheRecord> BookWalkerCache::load(int anilistId,
                                                          int currentCanonicalCount,
                                                          qint64 ttlSeconds)
{
    const QString path = cacheFilePath(anilistId);
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("schemaVersion")).toInt() != 1) return std::nullopt;

    BookWalkerCacheRecord rec;
    rec.schemaVersion = 1;
    rec.fetchedAt = QDateTime::fromString(obj.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    if (!rec.fetchedAt.isValid()) return std::nullopt;
    rec.canonicalCount = obj.value(QStringLiteral("canonicalCount")).toInt();
    rec.bookwalkerSeriesId = obj.value(QStringLiteral("bookwalkerSeriesId")).toString();

    // TTL check.
    const qint64 ageSeconds = rec.fetchedAt.secsTo(QDateTime::currentDateTimeUtc());
    if (ageSeconds > ttlSeconds) return std::nullopt;

    // Drift check (skip if currentCanonicalCount == 0, degraded path).
    if (currentCanonicalCount > 0 && rec.canonicalCount != currentCanonicalCount) {
        return std::nullopt;
    }

    const QJsonArray vols = obj.value(QStringLiteral("volumes")).toArray();
    for (const auto& v : vols) {
        const QJsonObject vo = v.toObject();
        BookWalkerCoverEntry e;
        e.volume = vo.value(QStringLiteral("vol")).toInt();
        e.url = vo.value(QStringLiteral("url")).toString();
        if (e.volume > 0 && !e.url.isEmpty()) rec.volumes.append(e);
    }
    return rec;
}

bool BookWalkerCache::store(int anilistId, const BookWalkerCacheRecord& record)
{
    const QString path = cacheFilePath(anilistId);
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 1);
    obj.insert(QStringLiteral("fetchedAt"),
               (record.fetchedAt.isValid() ? record.fetchedAt : QDateTime::currentDateTimeUtc())
                   .toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("canonicalCount"), record.canonicalCount);
    obj.insert(QStringLiteral("bookwalkerSeriesId"), record.bookwalkerSeriesId);

    QJsonArray arr;
    for (const auto& e : record.volumes) {
        QJsonObject vo;
        vo.insert(QStringLiteral("vol"), e.volume);
        vo.insert(QStringLiteral("url"), e.url);
        arr.append(vo);
    }
    obj.insert(QStringLiteral("volumes"), arr);

    QSaveFile sf(path);
    if (!sf.open(QIODevice::WriteOnly)) return false;
    sf.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return sf.commit();
}

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 2: Register the source in `CMakeLists.txt`**

Append:
```cmake
        src/core/manga/bookwalker/BookWalkerCache.cpp
```

- [ ] **Step 3: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/bookwalker/BookWalkerCache.cpp CMakeLists.txt
git commit -m "feat(manga/bookwalker): implement BookWalkerCache with TTL + drift check"
```

---

## Phase 4 — Orchestrator (`VolumeCoverResolver`)

### Task 11: Author `VolumeCoverResolver.h`

**Files:**
- Create: `src/core/manga/bookwalker/VolumeCoverResolver.h`

- [ ] **Step 1: Write the header**

```cpp
// src/core/manga/bookwalker/VolumeCoverResolver.h
#pragma once

#include "BookWalkerTypes.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

namespace tankoban::manga::anilist { class AniListCache; }
namespace tankoban::manga::mangaupdates { class MangaUpdatesClient; }

namespace tankoban::manga {
class PremiumCatalog;
}

namespace tankoban::manga::bookwalker {

class BookWalkerClient;

// Orchestrates the full chain per spec §5:
//   Premium short-circuit → AniList alt-title → MangaUpdates count →
//   cache check → BookWalker search → BookWalker page parse →
//   index align → cache write → emit.
class VolumeCoverResolver : public QObject
{
    Q_OBJECT
public:
    VolumeCoverResolver(BookWalkerClient* bwClient,
                        tankoban::manga::anilist::AniListCache* anilistCache,
                        tankoban::manga::mangaupdates::MangaUpdatesClient* muClient,
                        tankoban::manga::PremiumCatalog* premium,
                        QObject* parent = nullptr);
    ~VolumeCoverResolver() override;

    // Entry point. Resolves per-volume covers for the given AniList ID.
    // Emits resolved(...) or unresolved(...) when complete.
    void resolveForAnilist(int anilistId);

signals:
    void resolved(int anilistId, const QMap<int, QString>& volumeToCoverUrl);
    void unresolved(int anilistId, const QString& reason);

private slots:
    void onSearchSucceeded(int requestId, const QList<BookWalkerSearchHit>& hits);
    void onSearchFailed(int requestId, const QString& reason);
    void onCoversSucceeded(int requestId, const QList<QString>& orderedCoverUrls);
    void onCoversFailed(int requestId, const QString& reason);

private:
    struct PendingResolve {
        int anilistId = 0;
        QString japaneseTitle;
        int canonicalCount = 0;
        QString bookwalkerSeriesId;
    };

    int nextRequestId();
    void serveCachedOrFallback(int anilistId, int canonicalCount,
                               const QString& failureReason);
    void emitFromCache(int anilistId, const BookWalkerCacheRecord& rec);

    QPointer<BookWalkerClient> m_bwClient;
    QPointer<tankoban::manga::anilist::AniListCache> m_anilistCache;
    QPointer<tankoban::manga::mangaupdates::MangaUpdatesClient> m_muClient;
    QPointer<tankoban::manga::PremiumCatalog> m_premium;

    QHash<int, PendingResolve> m_pending;
    int m_nextRequestId = 1;
};

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 2: Commit**

```bash
git add src/core/manga/bookwalker/VolumeCoverResolver.h
git commit -m "feat(manga/bookwalker): add VolumeCoverResolver header"
```

---

### Task 12: Implement `VolumeCoverResolver.cpp`

**Files:**
- Create: `src/core/manga/bookwalker/VolumeCoverResolver.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
// src/core/manga/bookwalker/VolumeCoverResolver.cpp
#include "VolumeCoverResolver.h"

#include "BookWalkerCache.h"
#include "BookWalkerClient.h"
#include "BookWalkerSeriesPageParser.h"
#include "VolumeCoverAlignment.h"

#include "core/manga/PremiumCatalog.h"
#include "core/manga/anilist/AniListCache.h"
#include "core/manga/mangaupdates/MangaUpdatesClient.h"

#include <QDateTime>

namespace tankoban::manga::bookwalker {

VolumeCoverResolver::VolumeCoverResolver(
        BookWalkerClient* bwClient,
        tankoban::manga::anilist::AniListCache* anilistCache,
        tankoban::manga::mangaupdates::MangaUpdatesClient* muClient,
        tankoban::manga::PremiumCatalog* premium,
        QObject* parent)
    : QObject(parent), m_bwClient(bwClient), m_anilistCache(anilistCache),
      m_muClient(muClient), m_premium(premium)
{
    if (m_bwClient) {
        connect(m_bwClient.data(), &BookWalkerClient::searchSucceeded,
                this, &VolumeCoverResolver::onSearchSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::searchFailed,
                this, &VolumeCoverResolver::onSearchFailed);
        connect(m_bwClient.data(), &BookWalkerClient::coversSucceeded,
                this, &VolumeCoverResolver::onCoversSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::coversFailed,
                this, &VolumeCoverResolver::onCoversFailed);
    }
}

VolumeCoverResolver::~VolumeCoverResolver() = default;

int VolumeCoverResolver::nextRequestId() { return m_nextRequestId++; }

void VolumeCoverResolver::emitFromCache(int anilistId, const BookWalkerCacheRecord& rec)
{
    QMap<int, QString> m;
    for (const auto& e : rec.volumes) m.insert(e.volume, e.url);
    emit resolved(anilistId, m);
}

void VolumeCoverResolver::serveCachedOrFallback(int anilistId, int canonicalCount,
                                                const QString& failureReason)
{
    // Degradation per spec §6: AniList/MangaUpdates unreachable BUT fresh cache exists
    // → serve cached covers, skip drift check.
    auto cached = BookWalkerCache::load(anilistId, /*currentCanonicalCount=*/0);
    if (cached) {
        emitFromCache(anilistId, *cached);
        return;
    }
    emit unresolved(anilistId, failureReason);
}

void VolumeCoverResolver::resolveForAnilist(int anilistId)
{
    // Step 1: Premium short-circuit.
    if (m_premium && m_premium->hasEntry(anilistId)) {
        // Premium curated covers come via PremiumCoverExtractor in the existing pipeline.
        // BookWalker explicitly does not run for Premium series (spec Decision #5).
        emit unresolved(anilistId, QStringLiteral("premium-short-circuit"));
        return;
    }

    // Step 2: AniList alt-title.
    QString japaneseTitle;
    if (m_anilistCache) japaneseTitle = m_anilistCache->japaneseTitleFor(anilistId);
    if (japaneseTitle.isEmpty()) {
        serveCachedOrFallback(anilistId, /*canonicalCount=*/0,
                              QStringLiteral("no-japanese-title"));
        return;
    }

    // Step 3: MangaUpdates count.
    int canonicalCount = 0;
    if (m_muClient) canonicalCount = m_muClient->cachedVolumeCount(anilistId);
    // canonicalCount may be 0 here — that's the degraded path; alignment handles it.

    // Step 4: Cache check (TTL + drift).
    auto cached = BookWalkerCache::load(anilistId, canonicalCount);
    if (cached) {
        emitFromCache(anilistId, *cached);
        return;
    }

    // Step 5: BookWalker search.
    if (!m_bwClient) {
        emit unresolved(anilistId, QStringLiteral("bookwalker-client-null"));
        return;
    }
    PendingResolve p;
    p.anilistId = anilistId;
    p.japaneseTitle = japaneseTitle;
    p.canonicalCount = canonicalCount;
    const int reqId = nextRequestId();
    m_pending.insert(reqId, p);
    m_bwClient->searchSeries(japaneseTitle, reqId);
}

void VolumeCoverResolver::onSearchSucceeded(int requestId,
                                            const QList<BookWalkerSearchHit>& hits)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    PendingResolve p = it.value();

    const QString seriesId = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, p.japaneseTitle);
    if (seriesId.isEmpty()) {
        m_pending.erase(it);
        emit unresolved(p.anilistId, QStringLiteral("series-not-on-bookwalker"));
        return;
    }
    p.bookwalkerSeriesId = seriesId;
    it.value() = p;
    m_bwClient->fetchSeriesCovers(seriesId, requestId);
}

void VolumeCoverResolver::onSearchFailed(int requestId, const QString& reason)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const PendingResolve p = it.value();
    m_pending.erase(it);
    emit unresolved(p.anilistId, QStringLiteral("search-failed: ") + reason);
}

void VolumeCoverResolver::onCoversSucceeded(int requestId,
                                            const QList<QString>& orderedCoverUrls)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const PendingResolve p = it.value();
    m_pending.erase(it);

    QMap<int, QString> aligned = VolumeCoverAlignment::align(
        orderedCoverUrls, p.canonicalCount);
    if (aligned.isEmpty()) {
        emit unresolved(p.anilistId, QStringLiteral("alignment-empty"));
        return;
    }

    // Cache write.
    BookWalkerCacheRecord rec;
    rec.schemaVersion = 1;
    rec.fetchedAt = QDateTime::currentDateTimeUtc();
    rec.canonicalCount = (p.canonicalCount > 0 ? p.canonicalCount : aligned.size());
    rec.bookwalkerSeriesId = p.bookwalkerSeriesId;
    for (auto k = aligned.constBegin(); k != aligned.constEnd(); ++k) {
        BookWalkerCoverEntry e;
        e.volume = k.key();
        e.url = k.value();
        rec.volumes.append(e);
    }
    (void)BookWalkerCache::store(p.anilistId, rec); // disk-write failure is a soft warning, not fatal

    emit resolved(p.anilistId, aligned);
}

void VolumeCoverResolver::onCoversFailed(int requestId, const QString& reason)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const PendingResolve p = it.value();
    m_pending.erase(it);
    emit unresolved(p.anilistId, QStringLiteral("covers-failed: ") + reason);
}

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 2: Verify expected accessor methods exist on dependencies**

The implementation references three methods on existing classes — verify their signatures BEFORE building. If any are missing, add minimal accessors as part of this task.

```bash
grep -rn "japaneseTitleFor" src/core/manga/anilist/
grep -rn "cachedVolumeCount" src/core/manga/mangaupdates/
grep -rn "hasEntry" src/core/manga/PremiumCatalog.h
```

If any returns no match:
- For `AniListCache::japaneseTitleFor(int anilistId) const` — add as a public method on `AniListCache` returning the `native` title from the cached `MediaPreview`.
- For `MangaUpdatesClient::cachedVolumeCount(int anilistId) const` — add as a public method reading the most-recent VolumeMetadataResolver result for that ID; if not tracked yet, return 0.
- For `PremiumCatalog::hasEntry(int anilistId) const` — verify the existing API; if named differently (e.g., `contains`, `entryFor`), update the resolver call site accordingly. Do not rename existing methods.

Commit any necessary accessor additions as a separate commit per Tankoban one-fix-per-rebuild convention before continuing.

- [ ] **Step 3: Register the source in `CMakeLists.txt`**

Append:
```cmake
        src/core/manga/bookwalker/VolumeCoverResolver.cpp
```

- [ ] **Step 4: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/bookwalker/VolumeCoverResolver.cpp CMakeLists.txt
git commit -m "feat(manga/bookwalker): implement VolumeCoverResolver orchestrator"
```

---

## Phase 5 — UI integration

### Task 13: Loading-overlay decision and lightweight `ComicsSeriesViewLoadingOverlay`

**Files:**
- Create: `src/ui/widgets/ComicsSeriesViewLoadingOverlay.h`
- Create: `src/ui/widgets/ComicsSeriesViewLoadingOverlay.cpp`

**Decision rationale:** The existing `src/ui/player/LoadingOverlay.cpp` is player-domain (knows about video state, sidecar phases, buffering text). Reuse would require a namespace move + a generalize-the-message-API refactor — bigger blast radius than warranted for a single-purpose comics overlay. Going with option (b) from spec §4.3: lightweight purpose-built widget. ~50 LOC, clean isolation, no risk to player domain.

- [ ] **Step 1: Write the header**

```cpp
// src/ui/widgets/ComicsSeriesViewLoadingOverlay.h
#pragma once

#include <QWidget>

class QLabel;

namespace tankoban::ui::widgets {

class ComicsSeriesViewLoadingOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit ComicsSeriesViewLoadingOverlay(QWidget* parent = nullptr);
    ~ComicsSeriesViewLoadingOverlay() override;

    void setMessage(const QString& text);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    QLabel* m_label = nullptr;
};

} // namespace tankoban::ui::widgets
```

- [ ] **Step 2: Write the implementation**

```cpp
// src/ui/widgets/ComicsSeriesViewLoadingOverlay.cpp
#include "ComicsSeriesViewLoadingOverlay.h"

#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace tankoban::ui::widgets {

ComicsSeriesViewLoadingOverlay::ComicsSeriesViewLoadingOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("ComicsSeriesViewLoadingOverlay"));

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("ComicsSeriesViewLoadingOverlay_Label"));
    m_label->setText(QStringLiteral("Loading volume covers…"));
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(QStringLiteral(
        "QLabel#ComicsSeriesViewLoadingOverlay_Label { color: #d0d0d0; font-size: 14px; }"));

    auto* layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(m_label, 0, Qt::AlignCenter);
    layout->addStretch();
}

ComicsSeriesViewLoadingOverlay::~ComicsSeriesViewLoadingOverlay() = default;

void ComicsSeriesViewLoadingOverlay::setMessage(const QString& text)
{
    if (m_label) m_label->setText(text);
}

void ComicsSeriesViewLoadingOverlay::paintEvent(QPaintEvent* /*ev*/)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 200));
}

void ComicsSeriesViewLoadingOverlay::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
}

} // namespace tankoban::ui::widgets
```

- [ ] **Step 3: Register in `CMakeLists.txt`**

Find the main app's UI sources block (search for an existing `src/ui/widgets/` entry OR add the `src/ui/widgets/` directory if it's the first widget there). Append:

```cmake
        src/ui/widgets/ComicsSeriesViewLoadingOverlay.h
        src/ui/widgets/ComicsSeriesViewLoadingOverlay.cpp
```

- [ ] **Step 4: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/widgets/ComicsSeriesViewLoadingOverlay.h \
        src/ui/widgets/ComicsSeriesViewLoadingOverlay.cpp \
        CMakeLists.txt
git commit -m "feat(ui/widgets): add ComicsSeriesViewLoadingOverlay"
```

---

### Task 14: Wire `VolumeCoverResolver` + overlay into `ComicsSeriesView`

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h`
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1: Add the new members and slots in the header**

In `ComicsSeriesView.h`, inside the class declaration, add:

```cpp
// Forward declarations near the top of the file
namespace tankoban::manga::bookwalker { class VolumeCoverResolver; }
namespace tankoban::ui::widgets { class ComicsSeriesViewLoadingOverlay; }

class QTimer;
```

Inside the class, add:
```cpp
public:
    // Inject collaborators (typically from MainWindow during construction).
    void setVolumeCoverResolver(tankoban::manga::bookwalker::VolumeCoverResolver* resolver);

private slots:
    void onCoverResolverResolved(int anilistId, const QMap<int, QString>& volumeToCoverUrl);
    void onCoverResolverUnresolved(int anilistId, const QString& reason);
    void onCoverResolverSafetyTimeout();

private:
    void showLoadingOverlay();
    void hideLoadingOverlay();
    void paintVolumeCovers(const QMap<int, QString>& volumeToCoverUrl);
    void paintVolumeCoversAsFallback(); // series-level cover applied to every row

    QPointer<tankoban::manga::bookwalker::VolumeCoverResolver> m_coverResolver;
    tankoban::ui::widgets::ComicsSeriesViewLoadingOverlay* m_loadingOverlay = nullptr;
    QTimer* m_loadingSafetyTimer = nullptr;
    int m_currentResolvingAnilistId = 0;
```

- [ ] **Step 2: Implement the new methods in the .cpp**

In `ComicsSeriesView.cpp`, add these include lines near the top:
```cpp
#include "core/manga/bookwalker/VolumeCoverResolver.h"
#include "ui/widgets/ComicsSeriesViewLoadingOverlay.h"

#include <QTimer>
```

Find `buildUi()` (line ~205 per recap), append at its end:
```cpp
m_loadingOverlay = new tankoban::ui::widgets::ComicsSeriesViewLoadingOverlay(this);
m_loadingOverlay->hide();

m_loadingSafetyTimer = new QTimer(this);
m_loadingSafetyTimer->setSingleShot(true);
m_loadingSafetyTimer->setInterval(10'000); // 10 s hard cap per spec §5
connect(m_loadingSafetyTimer, &QTimer::timeout,
        this, &ComicsSeriesView::onCoverResolverSafetyTimeout);
```

Add the setter and slot implementations (near the bottom of the file or grouped with related logic):
```cpp
void ComicsSeriesView::setVolumeCoverResolver(
        tankoban::manga::bookwalker::VolumeCoverResolver* resolver)
{
    if (m_coverResolver) {
        disconnect(m_coverResolver.data(), nullptr, this, nullptr);
    }
    m_coverResolver = resolver;
    if (m_coverResolver) {
        connect(m_coverResolver.data(),
                &tankoban::manga::bookwalker::VolumeCoverResolver::resolved,
                this, &ComicsSeriesView::onCoverResolverResolved);
        connect(m_coverResolver.data(),
                &tankoban::manga::bookwalker::VolumeCoverResolver::unresolved,
                this, &ComicsSeriesView::onCoverResolverUnresolved);
    }
}

void ComicsSeriesView::showLoadingOverlay()
{
    if (!m_loadingOverlay) return;
    m_loadingOverlay->setGeometry(rect());
    m_loadingOverlay->raise();
    m_loadingOverlay->show();
}

void ComicsSeriesView::hideLoadingOverlay()
{
    if (m_loadingOverlay) m_loadingOverlay->hide();
    if (m_loadingSafetyTimer) m_loadingSafetyTimer->stop();
}

void ComicsSeriesView::onCoverResolverResolved(
        int anilistId, const QMap<int, QString>& volumeToCoverUrl)
{
    if (anilistId != m_currentResolvingAnilistId) return; // stale
    paintVolumeCovers(volumeToCoverUrl);
    hideLoadingOverlay();
}

void ComicsSeriesView::onCoverResolverUnresolved(int anilistId, const QString& /*reason*/)
{
    if (anilistId != m_currentResolvingAnilistId) return; // stale
    paintVolumeCoversAsFallback();
    hideLoadingOverlay();
}

void ComicsSeriesView::onCoverResolverSafetyTimeout()
{
    // 10s expired. Force fallback and bring the user the series view.
    paintVolumeCoversAsFallback();
    hideLoadingOverlay();
}
```

- [ ] **Step 3: Hook into the existing `openSeries` (or equivalent entry point)**

Find the existing entry point that loads a series for display (likely a method called from `ComicsPage::onSeriesActivated` — recap mentions `loadBannerUrl` near line 1055, and the series-open flow runs through `renderPreview`). At the START of that method, BEFORE the existing populate path:

```cpp
m_currentResolvingAnilistId = id.anilistId; // or whatever identity field exists
showLoadingOverlay();
if (m_loadingSafetyTimer) m_loadingSafetyTimer->start();
if (m_coverResolver) {
    m_coverResolver->resolveForAnilist(id.anilistId);
} else {
    // Defensive: no resolver wired → fallback immediately.
    paintVolumeCoversAsFallback();
    hideLoadingOverlay();
}
```

The existing `populateVolumeRows` call should remain — `paintVolumeCovers` and `paintVolumeCoversAsFallback` operate ON the rows that `populateVolumeRows` builds. The integration is: rows are built (text + checkbox + status), then the col-2 pixmap source is set by the cover-resolver callback.

- [ ] **Step 4: Implement the paint helpers**

`paintVolumeCovers`: for each `(volume, url)` in the map, find the row at index `volume - 1` and replace the col-2 cellWidget's `QLabel` pixmap source. Use `MangaPosterCache` for the URL → QPixmap step (existing pattern; mirror the call shape used today for the series-level cover in `populateVolumeRows`).

`paintVolumeCoversAsFallback`: for every row, set the col-2 cellWidget's `QLabel` pixmap to the series-level cover (the current default behavior — preserve whatever call shape `populateVolumeRows` uses today).

Concrete code shape (adjust the cellWidget lookup pattern to match what `populateVolumeRows` uses today):
```cpp
void ComicsSeriesView::paintVolumeCovers(const QMap<int, QString>& volumeToCoverUrl)
{
    for (auto it = volumeToCoverUrl.constBegin(); it != volumeToCoverUrl.constEnd(); ++it) {
        const int rowIdx = it.key() - 1; // volume is 1-based, table row is 0-based
        if (rowIdx < 0 || rowIdx >= m_volumeTable->rowCount()) continue;
        auto* labelWidget = qobject_cast<QLabel*>(m_volumeTable->cellWidget(rowIdx, kColCover));
        if (!labelWidget) continue;
        // Hand off to the existing pixmap-from-url helper used by populateVolumeRows.
        applyCoverPixmap(labelWidget, it.value()); // existing helper, name may differ — grep populateVolumeRows
    }
}

void ComicsSeriesView::paintVolumeCoversAsFallback()
{
    const QString seriesUrl = m_currentSeries.coverUrl; // existing field used by populateVolumeRows
    for (int rowIdx = 0; rowIdx < m_volumeTable->rowCount(); ++rowIdx) {
        auto* labelWidget = qobject_cast<QLabel*>(m_volumeTable->cellWidget(rowIdx, kColCover));
        if (!labelWidget) continue;
        applyCoverPixmap(labelWidget, seriesUrl);
    }
}
```

If `kColCover` / `applyCoverPixmap` / `m_volumeTable` / `m_currentSeries` have different names in the actual file, adjust to the existing names — but use whatever the current code uses for the series-level cover plumb.

- [ ] **Step 5: Wire the resolver in MainWindow (construction site)**

Find where `ComicsSeriesView` is constructed (likely in `ComicsPage::ComicsPage` or `MainWindow::buildComicsPage`). Right after construction, before adding to the layout:

```cpp
// Construct or obtain the resolver. If MainWindow already owns BookWalkerClient + AniListCache
// + MangaUpdatesClient + PremiumCatalog (it should, per existing patterns), reuse them. Otherwise
// construct here and own under MainWindow.
auto* bwClient = new tankoban::manga::bookwalker::BookWalkerClient(m_nam, this);
auto* coverResolver = new tankoban::manga::bookwalker::VolumeCoverResolver(
    bwClient, m_anilistCache, m_muClient, m_premiumCatalog, this);
seriesView->setVolumeCoverResolver(coverResolver);
```

Adjust the constructor-site location and naming to match the actual existing MainWindow/ComicsPage wiring. The principle: BookWalkerClient + VolumeCoverResolver are owned alongside other manga clients.

- [ ] **Step 6: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`. If any references (`kColCover`, `applyCoverPixmap`, etc.) don't compile, find the actual names in `ComicsSeriesView.cpp` via grep and adjust the references in Step 4 + Step 5 before re-running.

- [ ] **Step 7: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h \
        src/ui/pages/comics/ComicsSeriesView.cpp \
        src/ui/pages/ComicsPage.cpp \
        src/ui/MainWindow.cpp
git commit -m "feat(ui/comics): wire VolumeCoverResolver + loading overlay into ComicsSeriesView"
```

---

## Phase 6 — tankoctl schema verification

### Task 15: Confirm `tankoctl comics-get-series` surfaces `coverUrl` per row

**Files:**
- Verify (no edits unless gap found): `src/ui/MainWindow.cpp`, `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1: Inspect existing JSON schema returned by `comics-get-series`**

```bash
grep -n "volumeRows\|coverUrl\|comics-get-series" src/ui/MainWindow.cpp src/ui/pages/comics/ComicsSeriesView.cpp
```

If `coverUrl` is already a field on each `volumeRows[i]` entry: NO CHANGES NEEDED.

If it's missing (rows only contain `vol`, `chapterRange`, `downloaded`, `selected`): add a `coverUrl` field to the `devSnapshot()` method on `ComicsSeriesView` that returns the currently-applied per-row pixmap source URL. This is essential for the smoke matrix to verify covers programmatically.

- [ ] **Step 2: If a change was needed, build_check + commit**

```bash
build_check.bat
git add src/ui/pages/comics/ComicsSeriesView.cpp src/ui/pages/comics/ComicsSeriesView.h
git commit -m "feat(devtools): expose volumeRows[i].coverUrl in comics-get-series snapshot"
```

If no change needed, skip.

---

## Phase 7 — Smoke verification

### Task 16: Full smoke matrix on the canonical quartet

**Prerequisite:** Tankoban running with `--dev-control` (auto-set by `build_and_run.bat`).

- [ ] **Step 1: Launch Tankoban**

```bash
build_and_run.bat
```

Wait for the window to appear, mode-pill bar visible.

- [ ] **Step 2: Open Comics page + open Death Note via tankoctl**

```bash
out\tankoctl.exe open-page comics
out\tankoctl.exe comics-search-tankoyomi "Death Note" --timeout 8000
out\tankoctl.exe comics-open-series 30021
```

Expected: loading overlay paints briefly, then series view renders. Visual verification: each volume row shows a different cover image.

- [ ] **Step 3: Programmatic verification — Death Note**

```bash
out\tankoctl.exe comics-get-series
```

Expected JSON keys: `volumeRows[*].coverUrl` populated. Each `coverUrl` starts with `https://rimg.bookwalker.jp/`. Row count: 12 (Death Note completed).

- [ ] **Step 4: Berserk smoke (anilistId 30002)**

```bash
out\tankoctl.exe comics-open-series 30002
out\tankoctl.exe comics-get-series
```

Expected: 43 rows, each `coverUrl` from `rimg.bookwalker.jp`. Confirms baseline match with live probe 2026-05-18.

- [ ] **Step 5: One Piece smoke (anilistId 30013)**

```bash
out\tankoctl.exe comics-open-series 30013
out\tankoctl.exe comics-get-series
```

Expected: 114+ rows; first row coverUrl from BookWalker.

- [ ] **Step 6: Kingdom smoke (anilistId 46765)**

```bash
out\tankoctl.exe comics-open-series 46765
out\tankoctl.exe comics-get-series
```

Expected: 79 rows. This is the historically-gap-prone case (MangaDex failed at 11/79). BookWalker JP should not fail similarly. If it does, log finding and continue — Kingdom may not be on BookWalker for the audit's reason (publisher mismatch worth investigating).

- [ ] **Step 7: Cache-hit verification (re-open Death Note within session)**

```bash
out\tankoctl.exe comics-open-series 30021
out\tankoctl.exe logs --limit 50 | grep -i "bookwalker\|cache"
```

Expected: log lines indicate cache-hit. No new HTTP request to bookwalker.jp.

- [ ] **Step 8: Cache invalidation smoke**

```bash
powershell -c "Remove-Item -Force '$env:APPDATA\Tankoban\cache\bookwalker_covers\30021.json'"
out\tankoctl.exe comics-open-series 30021
out\tankoctl.exe logs --limit 50 | grep -i "bookwalker\|cache"
```

Expected: cache miss → BookWalker re-fetch → new cache file written.

- [ ] **Step 9: Niche-series fallback smoke**

Pick an obscure manga that AniList knows but BookWalker doesn't index. Candidate: any indie/doujinshi series with low publisher footprint. Open via `comics-open-series`. Expected: series view renders with series-level covers on every row (current-behavior fallback). No hang, no infinite loading overlay.

- [ ] **Step 10: Premium short-circuit verification**

Pick a Premium-catalog series (one already in the curated catalog). Open it. Expected: per the logs, NO request to bookwalker.jp fires. Premium-curated covers paint via the existing PremiumCoverExtractor path.

```bash
out\tankoctl.exe comics-open-series <premium-series-anilist-id>
out\tankoctl.exe logs --limit 50 | grep -i "bookwalker"
```

Expected: zero matches.

- [ ] **Step 11: Stop Tankoban + cleanup**

```bash
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

---

### Task 17: Flag READY TO COMMIT in `agents/chat.md`

- [ ] **Step 1: Append the RTC line**

Add at the end of `agents/chat.md`:

```
## Agent 1 - BOOKWALKER_VOLUME_COVERS_TODO all phases shipped + smoked
Files: src/core/manga/bookwalker/BookWalkerTypes.h, src/core/manga/bookwalker/BookWalkerSeriesPageParser.{h,cpp}, src/core/manga/bookwalker/VolumeCoverAlignment.{h,cpp}, src/core/manga/bookwalker/BookWalkerClient.{h,cpp}, src/core/manga/bookwalker/BookWalkerCache.{h,cpp}, src/core/manga/bookwalker/VolumeCoverResolver.{h,cpp}, src/ui/widgets/ComicsSeriesViewLoadingOverlay.{h,cpp}, src/ui/pages/comics/ComicsSeriesView.{h,cpp}, src/ui/pages/ComicsPage.cpp, src/ui/MainWindow.cpp, tests/core/manga/bookwalker/test_bookwalker_series_page_parser.cpp, tests/core/manga/bookwalker/test_volume_cover_alignment.cpp, tests/fixtures/bookwalker/berserk_series_page.html, tests/fixtures/bookwalker/berserk_search_results.html, CMakeLists.txt
Skills invoked: [/superpowers:executing-plans OR /superpowers:subagent-driven-development, /superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /simplify]
BookWalker JP per-volume covers shipped end-to-end. Pure-logic primitives (BookWalkerSeriesPageParser + VolumeCoverAlignment) TDD-tested with frozen Berserk fixtures: 11 unit tests green. HTTP client + disk cache (7-day TTL + count-drift invalidation) + VolumeCoverResolver orchestrator follow MangaUpdatesClient/VolumeMetadataResolver patterns. ComicsSeriesView wired with blocking loading overlay (10s hard cap), Premium short-circuit (BookWalker never fires for Premium series), series-level fallback on resolver-failed. Smoke matrix: Death Note ✓ / Berserk ✓ / One Piece ✓ / Kingdom ✓ / niche fallback ✓ / cache-hit ✓ / cache-invalidation ✓ / Premium-skip ✓. Spec: docs/superpowers/specs/2026-05-18-bookwalker-volume-covers-design.md. Plan: docs/superpowers/plans/2026-05-18-bookwalker-volume-covers.md.
READY TO COMMIT
```

- [ ] **Step 2: Commit the chat.md update**

```bash
git add agents/chat.md
git commit -m "chat: flag BOOKWALKER_VOLUME_COVERS shipment for Agent 0 sweep"
```

---

## Self-Review Notes

**Spec coverage check:**
- §3 Decision #1 (covers-only scope) → Tasks 4-12 only handle covers; no title/date/ISBN parsing anywhere ✓
- §3 Decision #2 (AniList alt-titles cascade) → Task 12 Step 1 calls `m_anilistCache->japaneseTitleFor(anilistId)` ✓
- §3 Decision #3 (cross-reference MangaUpdates count) → Task 5 (`VolumeCoverAlignment`) + Task 12 (`canonicalCount` from `MangaUpdatesClient::cachedVolumeCount`) ✓
- §3 Decision #4 (series-level fallback) → Task 14 Step 4 (`paintVolumeCoversAsFallback`) ✓
- §3 Decision #5 (Premium curated wins) → Task 12 Step 1 first branch (`m_premium->hasEntry`) ✓
- §3 Decision #6 (7-day TTL + count-drift invalidation) → Task 9 (header constants) + Task 10 (load with TTL + drift checks) ✓
- §3 Decision #7 (blocking loading overlay) → Task 13 (overlay widget) + Task 14 (showLoadingOverlay in openSeries) ✓
- §5 data-flow chain (steps 1-9) → Task 12 implements all 9 in order ✓
- §6 error handling matrix → Task 12 (`serveCachedOrFallback` for AniList/MU failure with cache) + Task 14 (safety-timeout for 10s cap) ✓
- §8.1 smoke matrix → Task 16 (all 4 canonical series + niche + cache-hit + cache-invalidation + Premium-skip) ✓
- §8.2 unit-level primitives → Task 4 + Task 5 (TDD) ✓

**Placeholder scan:** All code blocks contain compilable C++. No "TBD", "TODO" inside implementation code. Task 12 Step 2 explicitly calls out the need to verify three accessor methods exist before continuing; this is real and not a placeholder (the existing classes may or may not have these methods, and skipping the check would silently fail at compile).

**Type-consistency check:**
- `BookWalkerSearchHit` defined in Types (Task 1), consumed in parser (Task 4) and client (Tasks 7-8) ✓
- `BookWalkerCoverEntry` defined in Types, consumed in cache (Task 10) and resolver (Task 12) ✓
- `BookWalkerCacheRecord` defined in Types, consumed in cache + resolver ✓
- `extractCoverUrls` / `extractSearchHits` / `pickSeriesIdByTitle` signatures match between parser header (Task 4 Step 3) and parser tests (Task 4 Step 1 + Step 5) ✓
- `align()` signature consistent in header (Task 5 Step 3) + tests (Task 5 Step 1) + resolver call (Task 12 Step 1) ✓
- `resolved` / `unresolved` signal signatures match between resolver header (Task 11) and view slot signatures (Task 14 Step 1) ✓
