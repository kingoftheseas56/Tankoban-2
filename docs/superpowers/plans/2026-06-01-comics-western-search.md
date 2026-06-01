# Comics Western Live Search & Add — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Flat-on-master (no worktrees, gov-v13).

**Goal:** Let a user type a comic title on the Western shelf, see live RCO results, open a real series page, and **Add** it as a permanent shelf fixture — mirroring manga mode.

**Architecture:** Reuse the live RCO scraper (`ReadComicsScraper`) and the existing search bar + Western series view. Three new pieces: (1) a pure `WesternSeriesParse` unit that ports the proven Python harvester logic (edition-tier classifier + RCO item/summary/cover parsers) to C++; (2) `ReadComicsScraper::fetchWesternSeries` that fetches a series page live and assembles the schema-v2 JSON in memory; (3) mode-aware search routing + add-to-shelf persistence in the UI. "Lean now, full later" — author/publisher/genre (Wikidata) deferred; live-add leaves those fields empty.

**Tech Stack:** C++17, Qt6 (Core for the pure unit; Network for the scraper; Widgets for the UI), GoogleTest (`tankoban_tests`).

**Spec:** `docs/superpowers/specs/2026-06-01-comics-western-search-design.md`

---

## File Structure

**Create:**
- `src/core/manga/WesternSeriesParse.h` — pure parse/classify API (no network, no UI).
- `src/core/manga/WesternSeriesParse.cpp` — implementation (ports `edition_classify.py` + `parse_rco.py`).
- `tests/core/manga/WesternSeriesParseTest.cpp` — GoogleTest for the pure unit.

**Modify:**
- `cmake/TankobanSources.cmake` — add `WesternSeriesParse.cpp` to the app sources.
- `cmake/TankobanTests.cmake` — add `WesternSeriesParse.cpp` + `WesternSeriesParseTest.cpp` to `tankoban_tests`.
- `src/core/manga/WesternCatalogLoader.{h,cpp}` — extract `loadFromJsonObject` so live JSON reuses the same mapping as on-disk JSON.
- `src/core/manga/ReadComicsScraper.{h,cpp}` — add `fetchWesternSeries(slug,title,cover)` + `westernSeriesReady(QJsonObject)` signal.
- `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}` — add `setActiveSourceId` so the bar is mode-aware.
- `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` — Western add affordance: `setWesternOnShelf(bool)` + `addWesternToLibraryRequested()`.
- `src/ui/pages/ComicsPage.cpp` (+ `.h`) — set search source on Western enter/leave; route Western picks through the live path; persist on add.

---

## Task 1: WesternSeriesParse — pure classify + parse unit (TDD)

Ports the proven harvester logic so live-fetched series classify *identically* to the baked 13. Pure: only QtCore types, no network, no UI — fully unit-testable.

**Files:**
- Create: `src/core/manga/WesternSeriesParse.h`
- Create: `src/core/manga/WesternSeriesParse.cpp`
- Test: `tests/core/manga/WesternSeriesParseTest.cpp`
- Modify: `cmake/TankobanSources.cmake`, `cmake/TankobanTests.cmake`

- [ ] **Step 1: Write the header**

```cpp
// src/core/manga/WesternSeriesParse.h
#pragma once

#include <QString>
#include <QList>
#include <QJsonArray>

// Pure parse/classify helpers for RCO (rcostation.xyz) Western series pages.
// C++ port of the offline harvester: scripts/comics_catalogue/edition_classify.py
// + parse_rco.py. No network, no UI — keep it that way (it is unit-tested in
// tankoban_tests, which links QtCore only). The live path (ReadComicsScraper)
// and the offline Python harvester MUST stay behaviourally identical so a
// searched-and-added series matches the baked 13.
namespace tankoban::manga::western {

struct SeriesItem {
    QString label;   // human label, e.g. "TPB 25 The End"
    QString href;    // "/Comic/<Series>/<Item>" (query stripped)
};

// Tier: 0 Compendium / 1 Omnibus / 2 TPB / 3 Deluxe / 4 Vol / 99 single-issue|unknown.
// Lower = stronger collected-edition signal. First rule that matches wins.
int  editionTier(const QString& label);

// True if the label denotes a collected edition worth a tile. Strong tiers
// (<=3) always count; the soft Vol tier counts only when no single-issue
// marker ("Issue" / "#N") is present.
bool isCollected(const QString& label);

// Last path segment of an href, dash/whitespace runs -> single spaces.
QString slugToLabel(const QString& href);

// Deduped {label, href} for every item link on a series page, first-seen order.
QList<SeriesItem> parseSeriesItems(const QString& html);

// The one series-hero cover path (host-relative, e.g. "/Uploads/.../x.jpg"), or "".
QString parseSeriesCover(const QString& html);

// The RCO "Summary:" prose block, tags stripped + entities unescaped + collapsed, or "".
QString parseSeriesSummary(const QString& html);

// True when an RCO summary is too thin to trust (< 120 chars) and a fallback
// (Wikipedia) should be attempted.
bool needsSummaryFallback(const QString& summary);

// schema-v2 editions[] from raw items: keep collected only, stable-sort by tier
// ascending (first-seen order preserved within a tier). Each entry:
//   { "label": <slugToLabel>, "href": <item href>, "formatTier": <editionTier> }
QJsonArray buildEditions(const QList<SeriesItem>& items);

} // namespace tankoban::manga::western
```

- [ ] **Step 2: Write the failing test**

```cpp
// tests/core/manga/WesternSeriesParseTest.cpp
#include <gtest/gtest.h>
#include "core/manga/WesternSeriesParse.h"

using namespace tankoban::manga::western;

TEST(WesternSeriesParse, EditionTierByKeyword) {
    EXPECT_EQ(editionTier("_The Lost Year Compendium"), 0);
    EXPECT_EQ(editionTier("Omnibus Vol 1"), 1);        // omnibus rule before vol
    EXPECT_EQ(editionTier("_TPB 25 The End"), 2);
    EXPECT_EQ(editionTier("Trade Paperback"), 2);
    EXPECT_EQ(editionTier("Complete Collection"), 2);
    EXPECT_EQ(editionTier("Absolute Vol 1"), 3);       // absolute before vol
    EXPECT_EQ(editionTier("Library Edition"), 3);
    EXPECT_EQ(editionTier("Vol. 3"), 4);
    EXPECT_EQ(editionTier("Issue 144"), 99);
    EXPECT_EQ(editionTier("Invincible (2003) #32"), 99);
}

TEST(WesternSeriesParse, IsCollected) {
    EXPECT_TRUE(isCollected("_TPB 25 The End"));
    EXPECT_TRUE(isCollected("Compendium One"));
    EXPECT_TRUE(isCollected("Vol. 3"));               // soft Vol, no issue marker
    EXPECT_FALSE(isCollected("Vol 2 Issue #5"));      // Vol + issue marker -> single issue
    EXPECT_FALSE(isCollected("Issue #144"));
    EXPECT_FALSE(isCollected("Invincible (2003) #32"));
}

TEST(WesternSeriesParse, SlugToLabel) {
    EXPECT_EQ(slugToLabel("/Comic/Invincible/TPB-25-The-End"), "TPB 25 The End");
    EXPECT_EQ(slugToLabel("/Comic/Saga/Compendium-One/"), "Compendium One");
}

TEST(WesternSeriesParse, ParseSeriesItemsDedupes) {
    const QString html = R"(
        <a href="/Comic/Invincible/TPB-25-The-End?id=1"><img/></a>
        <a href="/Comic/Invincible/TPB-25-The-End?id=1">TPB 25</a>
        <a href="/Comic/Invincible/Issue-144?id=2">Issue 144</a>
    )";
    const auto items = parseSeriesItems(html);
    ASSERT_EQ(items.size(), 2);
    EXPECT_EQ(items[0].href, "/Comic/Invincible/TPB-25-The-End");
    EXPECT_EQ(items[0].label, "TPB 25 The End");
    EXPECT_EQ(items[1].href, "/Comic/Invincible/Issue-144");
}

TEST(WesternSeriesParse, ParseSeriesCover) {
    const QString html = R"(<link rel="image_src" href="/Uploads/Etc/3-25-2016/42392826.jpg">)";
    EXPECT_EQ(parseSeriesCover(html), "/Uploads/Etc/3-25-2016/42392826.jpg");
    EXPECT_EQ(parseSeriesCover("<html>no cover</html>"), "");
}

TEST(WesternSeriesParse, ParseSeriesSummary) {
    const QString html =
        R"(<span class="info">Summary:</span> <p>A man named <b>Mark</b> &amp; his dad.</p>)";
    EXPECT_EQ(parseSeriesSummary(html), "A man named Mark & his dad.");
    EXPECT_EQ(parseSeriesSummary("<html>no summary</html>"), "");
}

TEST(WesternSeriesParse, NeedsSummaryFallback) {
    EXPECT_TRUE(needsSummaryFallback("too short"));
    EXPECT_FALSE(needsSummaryFallback(QString(200, 'x')));
}

TEST(WesternSeriesParse, BuildEditionsFiltersAndSorts) {
    QList<SeriesItem> items = {
        {"Vol. 1",            "/Comic/X/Vol-1"},
        {"Issue 5",           "/Comic/X/Issue-5"},     // dropped (single issue)
        {"_TPB 25 The End",   "/Comic/X/TPB-25"},
        {"Compendium One",    "/Comic/X/Compendium-One"},
    };
    const QJsonArray eds = buildEditions(items);
    ASSERT_EQ(eds.size(), 3);                          // Issue 5 filtered out
    EXPECT_EQ(eds[0].toObject()["formatTier"].toInt(), 0);  // Compendium first
    EXPECT_EQ(eds[1].toObject()["formatTier"].toInt(), 2);  // TPB
    EXPECT_EQ(eds[2].toObject()["formatTier"].toInt(), 4);  // Vol last
    EXPECT_EQ(eds[0].toObject()["label"].toString(), "Compendium One");
}
```

- [ ] **Step 3: Register the new files in CMake (both targets)**

In `cmake/TankobanSources.cmake`, add next to the other `src/core/manga/*.cpp` entries:
```cmake
    src/core/manga/WesternSeriesParse.cpp
```
In `cmake/TankobanTests.cmake`, inside the `add_executable(tankoban_tests ...)` source list (alongside `tests/core/manga/WeebCentralPairedParseTest.cpp` at line ~128), add BOTH the unit-under-test and its test:
```cmake
        src/core/manga/WesternSeriesParse.cpp
        tests/core/manga/WesternSeriesParseTest.cpp
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `cmake -S . -B out -DTANKOBAN_BUILD_TESTS=ON && cmake --build out --target tankoban_tests`
Expected: FAIL — link/compile error, `WesternSeriesParse.cpp` has no definitions yet.

- [ ] **Step 5: Write the implementation**

```cpp
// src/core/manga/WesternSeriesParse.cpp
#include "WesternSeriesParse.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace tankoban::manga::western {

namespace {
// Ported verbatim from edition_classify.py. Non-letter lookarounds (NOT \b):
// RCO prefixes collected editions with '_' which is a word char, so \b would
// not fire before it; [a-z] lookarounds (case-insensitive) match regardless.
struct Rule { QRegularExpression rx; int tier; };

const QList<Rule>& rules() {
    static const QList<Rule> r = {
        { QRegularExpression(R"((?<![a-z])compendium(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 0 },
        { QRegularExpression(R"((?<![a-z])omnibus(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 1 },
        { QRegularExpression(R"((?<![a-z])(?:tpb|trade paperback|complete collection)(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 2 },
        { QRegularExpression(R"((?<![a-z])(?:deluxe|absolute|library edition)(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 3 },
        { QRegularExpression(R"((?<![a-z])(?:vol\.?|volume)(?![a-z]))",
              QRegularExpression::CaseInsensitiveOption), 4 },
    };
    return r;
}

const QRegularExpression& issueRe() {
    static const QRegularExpression re(R"((?<![a-z])issue(?![a-z])|#\s*\d)",
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString unescapeEntities(QString s) {
    s.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
     .replace("&quot;", "\"").replace("&#39;", "'").replace("&nbsp;", " ");
    return s;
}
} // namespace

int editionTier(const QString& label) {
    for (const auto& r : rules())
        if (r.rx.match(label).hasMatch())
            return r.tier;
    return 99;
}

bool isCollected(const QString& label) {
    const int t = editionTier(label);
    if (t <= 3) return true;
    if (t == 4 && !issueRe().match(label).hasMatch()) return true;
    return false;
}

QString slugToLabel(const QString& href) {
    QString s = href;
    while (s.endsWith('/')) s.chop(1);
    const QString seg = s.section('/', -1);
    static const QRegularExpression sep(R"([\s \-\x{2010}-\x{2015}]+)");
    return seg.split(sep, Qt::SkipEmptyParts).join(' ').trimmed();
}

QList<SeriesItem> parseSeriesItems(const QString& html) {
    // /Comic/<Series>/<Item> with optional ?query (query excluded from capture).
    static const QRegularExpression itemRe(R"(href="(/Comic/[^"/]+/[^"?]+)(?:\?[^"]*)?")");
    QList<SeriesItem> items;
    QSet<QString> seen;
    auto it = itemRe.globalMatch(html);
    while (it.hasNext()) {
        const QString href = it.next().captured(1);
        if (seen.contains(href)) continue;
        seen.insert(href);
        items.push_back({ slugToLabel(href), href });
    }
    return items;
}

QString parseSeriesCover(const QString& html) {
    static const QRegularExpression coverRe(
        R"(<link\s+rel="image_src"\s+href="([^"]+)")",
        QRegularExpression::CaseInsensitiveOption);
    const auto m = coverRe.match(html);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString parseSeriesSummary(const QString& html) {
    static const QRegularExpression sumRe(
        R"(<span class="info">\s*Summary:\s*</span>\s*<p>(.*?)</p>)",
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    const auto m = sumRe.match(html);
    if (!m.hasMatch()) return QString();
    static const QRegularExpression tagRe(R"(<[^>]+>)");
    static const QRegularExpression wsRe(R"(\s+)");
    QString text = m.captured(1);
    text.replace(tagRe, " ");
    text = unescapeEntities(text);
    return text.replace(wsRe, " ").trimmed();
}

bool needsSummaryFallback(const QString& summary) {
    return summary.trimmed().size() < 120;
}

QJsonArray buildEditions(const QList<SeriesItem>& items) {
    QList<SeriesItem> collected;
    for (const auto& it : items)
        if (isCollected(it.label))
            collected.push_back(it);
    std::stable_sort(collected.begin(), collected.end(),
                     [](const SeriesItem& a, const SeriesItem& b) {
                         return editionTier(a.label) < editionTier(b.label);
                     });
    QJsonArray out;
    for (const auto& it : collected) {
        QJsonObject e;
        e["label"] = it.label;
        e["href"] = it.href;
        e["formatTier"] = editionTier(it.label);
        out.push_back(e);
    }
    return out;
}

} // namespace tankoban::manga::western
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R WesternSeriesParse`
Expected: PASS — all 8 cases green. READ the pass/fail line (do not assume).

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/WesternSeriesParse.h src/core/manga/WesternSeriesParse.cpp tests/core/manga/WesternSeriesParseTest.cpp cmake/TankobanSources.cmake cmake/TankobanTests.cmake
git commit -m "feat(comics): WesternSeriesParse pure unit (RCO classify+parse port, TDD)"
```

---

## Task 2: WesternCatalogLoader::loadFromJsonObject (refactor for reuse)

The live path produces an in-memory `QJsonObject`; the disk path reads a file. Both must map to `MangaCatalog` identically. Extract the existing mapping so there is one code path.

**Files:**
- Modify: `src/core/manga/WesternCatalogLoader.h`
- Modify: `src/core/manga/WesternCatalogLoader.cpp`

- [ ] **Step 1: Add the new declaration to the header**

In `WesternCatalogLoader.h`, alongside the existing `static MangaCatalog loadFromFile(const QString& path);`, add:
```cpp
    // Maps a schema-v2 Western JSON object -> MangaCatalog. Shared by loadFromFile
    // (disk) and the live search path (in-memory). cat.seriesId empty => invalid.
    static MangaCatalog loadFromJsonObject(const QJsonObject& obj);
```

- [ ] **Step 2: Move the mapping body verbatim into the new function**

In `WesternCatalogLoader.cpp`: take the existing field-mapping body of `loadFromFile` (everything that reads the parsed `QJsonObject` and fills `MangaCatalog` — currently `WesternCatalogLoader.cpp:38-102`, including the relative-cover absolutization and the `tierLabel()` grouping) and move it **unchanged** into a new `loadFromJsonObject(const QJsonObject& obj)`. Then rewrite `loadFromFile` to be a thin reader:
```cpp
MangaCatalog WesternCatalogLoader::loadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo("WesternCatalogLoader::loadFromFile: open failed for %s", qUtf8Printable(path));
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return {};
    return loadFromJsonObject(doc.object());
}
```
Do not change any field-mapping logic — this is a pure extraction.

- [ ] **Step 3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/WesternCatalogLoader.h src/core/manga/WesternCatalogLoader.cpp
git commit -m "refactor(comics): extract WesternCatalogLoader::loadFromJsonObject for live reuse"
```

---

## Task 3: ReadComicsScraper::fetchWesternSeries (live page -> schema-v2 JSON)

Fetch a series page live, classify its editions, pull its synopsis (RCO Summary; one Wikipedia "(comics)" attempt if thin — lean), and emit the assembled schema-v2 JSON. Metadata fields (author/publisher/genres/year) intentionally left empty ("full later").

**Files:**
- Modify: `src/core/manga/ReadComicsScraper.h`
- Modify: `src/core/manga/ReadComicsScraper.cpp`

- [ ] **Step 1: Declare the method + signal in the header**

In `ReadComicsScraper.h`, add to the public section:
```cpp
    // Live Western-catalogue fetch: GET /Comic/<slug>, classify collected
    // editions + pull synopsis, assemble a schema-v2 Western JSON object, emit
    // westernSeriesReady. title + coverFromSearch come from the search result
    // (used as fallbacks when the page parse is thin).
    void fetchWesternSeries(const QString& seriesSlug,
                            const QString& title,
                            const QString& coverFromSearch);

signals:
    void westernSeriesReady(const QJsonObject& seriesJson);
```
Add `#include <QJsonObject>` to the header includes.

- [ ] **Step 2: Implement fetch + assemble in the .cpp**

Add to `ReadComicsScraper.cpp` (uses the file-local `BASE`, `makeRequest`, and `western::*` from Task 1; add `#include "WesternSeriesParse.h"`, `#include <QJsonArray>`, `#include <QUrl>`):
```cpp
#include "WesternSeriesParse.h"

void ReadComicsScraper::fetchWesternSeries(const QString& seriesSlug,
                                           const QString& title,
                                           const QString& coverFromSearch)
{
    QUrl url(BASE + "/Comic/" + seriesSlug);
    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, seriesSlug, title, coverFromSearch]() {
        reply->deleteLater();

        QJsonObject obj;
        // seriesId: lowercase slug, '/'+space -> '-' (matches baked file naming).
        QString id = seriesSlug.toLower();
        id.replace(QRegularExpression(R"([\s/]+)"), "-");
        obj["seriesId"]      = id;
        obj["seriesTitle"]   = title.isEmpty() ? western::slugToLabel("/x/" + seriesSlug) : title;
        obj["source"]        = QStringLiteral("rco");
        obj["schemaVersion"] = 2;

        if (reply->error() != QNetworkReply::NoError) {
            // Degrade: still emit a minimal record (cover from search, no editions)
            // so the UI shows the series rather than a hung overlay.
            obj["seriesCover"] = coverFromSearch;
            obj["synopsis"]    = QString();
            obj["editions"]    = QJsonArray();
            emit westernSeriesReady(obj);
            return;
        }

        const QString html = QString::fromUtf8(reply->readAll());
        const QString cover = western::parseSeriesCover(html);
        obj["seriesCover"] = cover.isEmpty() ? coverFromSearch : cover;
        obj["editions"]    = western::buildEditions(western::parseSeriesItems(html));

        const QString summary = western::parseSeriesSummary(html);
        if (!western::needsSummaryFallback(summary)) {
            obj["synopsis"] = summary;
            emit westernSeriesReady(obj);
            return;
        }
        // Lean fallback: a single Wikipedia "(comics)" attempt. Full suffix-ladder
        // + Wikidata metadata are the later richness pass (out of scope here).
        const QString wikiTitle = (title.isEmpty() ? id : title) + " (comics)";
        QUrl wikiUrl("https://en.wikipedia.org/api/rest_v1/page/summary/"
                     + QString::fromUtf8(QUrl::toPercentEncoding(wikiTitle)));
        auto* wreply = m_nam->get(makeRequest(wikiUrl));
        connect(wreply, &QNetworkReply::finished, this,
                [this, wreply, obj, summary]() mutable {
            wreply->deleteLater();
            QString chosen = summary;
            if (wreply->error() == QNetworkReply::NoError) {
                const auto doc = QJsonDocument::fromJson(wreply->readAll());
                const QString extract = doc.object().value("extract").toString();
                const QString type    = doc.object().value("type").toString();
                if (!extract.isEmpty() && type != QLatin1String("disambiguation"))
                    chosen = extract;
            }
            obj["synopsis"] = chosen;
            emit westernSeriesReady(obj);
        });
    });
}
```

- [ ] **Step 3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/ReadComicsScraper.h src/core/manga/ReadComicsScraper.cpp
git commit -m "feat(comics): ReadComicsScraper::fetchWesternSeries — live RCO page -> schema-v2 JSON"
```

---

## Task 4: Mode-aware search source in ComicsTankoyomiSearchWidget

So the existing top search bar searches comics (RCO) on the Western shelf and manga (WeebCentral) elsewhere.

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp`

- [ ] **Step 1: Add the setter + member to the header**

In `ComicsTankoyomiSearchWidget.h`, in the public section after `void search(const QString& query);`:
```cpp
    // Mode-aware source. ComicsPage sets "readcomicsonline" on the Western shelf,
    // "weebcentral" (default) on the manga shelf. Both sources' searchFinished
    // are connected in the ctor; this selects which one search() dispatches to.
    void setActiveSourceId(const QString& sourceId);
```
And in the private members, after `QString m_currentQuery;`:
```cpp
    QString m_activeSourceId = QStringLiteral("weebcentral");
```

- [ ] **Step 2: Connect both sources in the ctor; route search() through the active source**

In `ComicsTankoyomiSearchWidget.cpp` ctor (currently connects only `find("weebcentral")` at line ~39), connect `searchFinished` for BOTH sources so whichever is active routes to `onSearchFinished`:
```cpp
    if (m_sourceRegistry) {
        for (const QString& id : { QStringLiteral("weebcentral"),
                                   QStringLiteral("readcomicsonline") }) {
            if (auto* scraper = m_sourceRegistry->find(id)) {
                connect(scraper, &MangaScraper::searchFinished,
                        this,    &ComicsTankoyomiSearchWidget::onSearchFinished,
                        Qt::UniqueConnection);
            }
        }
    }
```
Add the setter (anywhere in the .cpp):
```cpp
void ComicsTankoyomiSearchWidget::setActiveSourceId(const QString& sourceId)
{
    m_activeSourceId = sourceId;
}
```
In `search()`, replace the hardcoded lookup (line ~109 `m_sourceRegistry->find(QStringLiteral("weebcentral"))`) with:
```cpp
    auto* scraper = m_sourceRegistry->find(m_activeSourceId);
    if (!scraper) {
        qWarning() << "[ComicsTankoyomiSearchWidget] scraper not found:" << m_activeSourceId;
        m_statusLabel->setText(QStringLiteral("Search unavailable"));
        return;
    }
```

- [ ] **Step 3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/comics/ComicsTankoyomiSearchWidget.h src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp
git commit -m "feat(comics): mode-aware search source in ComicsTankoyomiSearchWidget"
```

---

## Task 5: ComicsPage — route Western picks through the live path

Set the search source when entering/leaving the Western shelf, and open a picked Western result via the live fetch (in memory, no disk yet).

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Add the in-memory open + state to the header**

In `ComicsPage.h`, add private members:
```cpp
    QJsonObject m_pendingWesternJson;   // last live-fetched Western series (for Add persistence)
    void openWesternSeriesFromCatalog(const tankoban::manga::MangaCatalog& cat,
                                      const QJsonObject& sourceJson);
```
(Add `#include <QJsonObject>` if not present.)

- [ ] **Step 2: Extract the show logic so disk + live share it**

In `ComicsPage.cpp`, refactor `openWesternSeriesFromJson(const QString& jsonPath)` (line ~2315): keep it as the disk entry, but move everything *after* `loadFromFile` (the part that takes the resulting `MangaCatalog` and shows it in the series view, sets `m_detailEnteredFromWestern = true`, etc.) into the new `openWesternSeriesFromCatalog(const MangaCatalog& cat, const QJsonObject& sourceJson)`. Then:
```cpp
void ComicsPage::openWesternSeriesFromJson(const QString& jsonPath)
{
    const auto catalog = tankoban::manga::WesternCatalogLoader::loadFromFile(jsonPath);
    if (catalog.seriesId.isEmpty()) {
        qInfo("ComicsPage::openWesternSeriesFromJson: loadFromFile failed for %s",
              qUtf8Printable(jsonPath));
        return;
    }
    // Disk path: series is already on the shelf.
    if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->setWesternOnShelf(true);
    openWesternSeriesFromCatalog(catalog, QJsonObject());
}
```
Inside `openWesternSeriesFromCatalog`, store `m_pendingWesternJson = sourceJson;` near the top (empty for disk opens, populated for live opens) and keep the existing show/`m_detailEnteredFromWestern = true` logic.

- [ ] **Step 3: Set the search source on Western enter/leave**

In `showWesternMode()` (line ~2362), after the grid refresh / stack switch, add:
```cpp
    if (m_searchTakeover) m_searchTakeover->setActiveSourceId(QStringLiteral("readcomicsonline"));
```
In the manga-mode entry (`showMangaMode` / wherever the manga shelf is selected — the sibling of `showWesternMode`), add:
```cpp
    if (m_searchTakeover) m_searchTakeover->setActiveSourceId(QStringLiteral("weebcentral"));
```
(`m_searchTakeover` is the `ComicsTankoyomiSearchWidget*` per ComicsPage.cpp:2978. Use that exact member name.)

- [ ] **Step 4: Route a Western pick through the live fetch**

Locate the RCO scraper once in the ComicsPage ctor and connect its ready signal (place near the other source-registry wiring, ComicsPage.cpp ctor ~line 185):
```cpp
    if (auto* rco = qobject_cast<ReadComicsScraper*>(
            m_sourceRegistry->find(QStringLiteral("readcomicsonline")))) {
        connect(rco, &ReadComicsScraper::westernSeriesReady, this,
                [this](const QJsonObject& json) {
            const auto cat = tankoban::manga::WesternCatalogLoader::loadFromJsonObject(json);
            if (cat.seriesId.isEmpty()) return;
            // Live open: "on shelf" iff a file already exists for this seriesId.
            const QString path = QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                                     .filePath(json.value("seriesId").toString() + ".json");
            if (m_tyVolumeSeriesView)
                m_tyVolumeSeriesView->setWesternOnShelf(QFileInfo::exists(path));
            openWesternSeriesFromCatalog(cat, json);
        });
    }
```
Add `#include "core/manga/ReadComicsScraper.h"`, `#include <QDir>`, `#include <QFileInfo>` to ComicsPage.cpp if not present.

In `onSearchResultActivated(const MangaResult& result)` (line ~2982), branch at the very top so Western results skip the AniList/manga path:
```cpp
    if (result.source == QLatin1String("readcomicsonline")) {
        if (auto* rco = qobject_cast<ReadComicsScraper*>(
                m_sourceRegistry->find(QStringLiteral("readcomicsonline")))) {
            // Switch to the series view + show loading overlay (mirror the manga
            // branch below), then fetch live.
            if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->showLoadingOverlay();
            rco->fetchWesternSeries(result.id, result.title, result.thumbnailUrl);
        }
        return;
    }
```
(Use the existing loading-overlay call the manga branch uses; match its exact method name. If the series-view stack-switch is done by the caller, mirror that here before `fetchWesternSeries`.)

- [ ] **Step 5: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): route Western search picks through live fetchWesternSeries"
```

---

## Task 6: Add-to-shelf persistence (the "Add" button)

Adding a live Western series writes its JSON to the shelf folder (skip-if-present) and refreshes the grid; the button reflects on-shelf state.

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h`
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Add Western add affordance to ComicsSeriesView**

In `ComicsSeriesView.h`, add:
```cpp
public:
    // Western live series: true => this series is already on the shelf, so the
    // library button reads "On shelf" and is inert. False => "Add to Library".
    void setWesternOnShelf(bool onShelf);
signals:
    void addWesternToLibraryRequested();
private:
    bool m_westernOnShelf = false;
```

- [ ] **Step 2: Wire the library button's Western branch**

In `ComicsSeriesView.cpp`, implement the setter:
```cpp
void ComicsSeriesView::setWesternOnShelf(bool onShelf)
{
    m_westernOnShelf = onShelf;
    refreshLibraryButton();   // reuse existing button-label refresh
}
```
In `onLibraryButtonClicked()` (line ~1724), at the top, add a Western branch that fires before the AniList logic. Western series are the `source=="rco"` ones (the same gate used for the about-block):
```cpp
    if (m_currentSource == QLatin1String("rco")) {   // Western series
        if (!m_westernOnShelf) {
            emit addWesternToLibraryRequested();
            m_westernOnShelf = true;
            refreshLibraryButton();
        }
        return;
    }
```
In `refreshLibraryButton()`, honor the Western state — when `m_currentSource == "rco"`, set the label to `m_westernOnShelf ? tr("On shelf") : tr("Add to Library")` and enable only when not on shelf. (Use the existing source-tracking member; if the view does not already store the current source string, add `QString m_currentSource;` set in `showSeries`/`populate*` from the catalog's `source` field.)

- [ ] **Step 3: Persist on add, in ComicsPage**

In `ComicsPage.cpp`, where `m_tyVolumeSeriesView` is constructed/wired, connect:
```cpp
    connect(m_tyVolumeSeriesView, &ComicsSeriesView::addWesternToLibraryRequested,
            this, [this]() {
        if (m_pendingWesternJson.isEmpty()) return;
        const QString id = m_pendingWesternJson.value("seriesId").toString();
        if (id.isEmpty()) return;
        const QString dir = tankoban::manga::WesternCatalogLoader::canonicalDataDir();
        QDir().mkpath(dir);
        const QString path = QDir(dir).filePath(id + ".json");
        if (QFileInfo::exists(path)) return;   // skip-if-present (locked default)
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(m_pendingWesternJson).toJson(QJsonDocument::Indented));
            f.close();
        }
        refreshWesternGrid();
    });
```
Add `#include <QJsonDocument>` to ComicsPage.cpp if not present.

- [ ] **Step 4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h src/ui/pages/comics/ComicsSeriesView.cpp src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): Western Add-to-shelf persistence (skip-if-present) + on-shelf button state"
```

---

## Task 7: End-to-end smoke + empty/error states

No new logic — verify the loop in the running app and confirm the empty/error states from spec §8 behave. Agent-driven smoke (tankoctl primary, pywinauto for visual), NOT a Hemanth task.

**Files:** none (verification only).

- [ ] **Step 1: Launch the app**

Run: `build_and_run.bat` (kill any running `Tankoban.exe` first per Rule 1; verify `out/Tankoban.exe` mtime advanced).

- [ ] **Step 2: Happy path**

- Open Comics → Western shelf (the 13 paint).
- Type `Saga` in the top search bar → results strip paints RCO covers.
- Click Saga → series page shows cover + synopsis + collected-editions list (Compendium/Omnibus/TPB grouped, single issues absent).
- Click **Add to Library** → button flips to "On shelf"; tile appears on the Western grid.
- Relaunch the app → Saga tile is still on the Western shelf (persisted).
Expected: each step as described. Capture a screenshot of the series page for the visual "looks like the 13" gate.

- [ ] **Step 3: Edge cases (spec §8)**

- Search a nonsense string → "no results" empty state, no hung spinner.
- Re-open an already-added series (search Saga again, open it) → button reads "On shelf"; clicking does nothing; no duplicate tile.
- A marquee whose primary slug is single-issues-only → series page shows synopsis with an empty/"no collected editions" state, not a broken page.
Expected: graceful in all three.

- [ ] **Step 4: Cleanup**

Run: `powershell -NoProfile -File scripts/stop-tankoban.ps1` (Rule 17).

- [ ] **Step 5: Record smoke result** in the RTC / chat.md (skill: smoke-report).

---

## Self-Review

**Spec coverage:**
- §3.1 Lean now → Task 3 leaves metadata empty, single Wikipedia attempt. ✔
- §3.2 Permanent on shelf → Task 6 writes to `data/western_catalogue/`. ✔
- §3.3 Same bar, mode-aware → Task 4 + Task 5 Step 3. ✔
- §3.4 Search solves marquee → live search (Task 3) reaches any RCO-indexed slug; Task 7 Step 3 verifies the single-issue-only edge. ✔
- §3.5 No badge → live tiles use the same `WesternCatalogLoader`/grid path; nothing adds a badge. ✔
- §5 download→read dependency → out of scope by construction (no reader work in any task); editions render but page-reading waits for the downloads arc. ✔
- §6.2 three new bits → Task 1 (classifier+synopsis), Task 4/5 (mode-aware routing), Task 6 (persistence). ✔
- §8 edge cases → Task 7 Step 3 + Task 3 error-degrade branch + Task 6 skip-if-present. ✔
- §9 testing → Task 1 is TDD GoogleTest; rest is smoke. ✔

**Placeholder scan:** No "TBD"/"implement later". Two steps say "match the exact existing method name" for `showLoadingOverlay`/`refreshLibraryButton`/manga-branch overlay — these are real existing methods the executor confirms by reading the surrounding code; the behavior is fully specified. Acceptable (integration into existing code), not a logic placeholder.

**Type consistency:** `westernSeriesReady(QJsonObject)` emitted in Task 3, consumed in Task 5. `loadFromJsonObject` defined Task 2, used Task 5. `setWesternOnShelf`/`addWesternToLibraryRequested` defined Task 6 Step 1, used Tasks 5 + 6. `m_pendingWesternJson` set in Task 5 Step 2, read in Task 6 Step 3. `m_searchTakeover` / `m_tyVolumeSeriesView` / `onSearchResultActivated` / `openWesternSeriesFromJson` / `refreshWesternGrid` / `canonicalDataDir` are confirmed real names from ground-truth. ✔

**Engine routing (build):** Task 1 (pure TDD port) and Tasks 4/6 (templated wiring) are DeepSeek/Agent 9-shaped grunt; Task 3 (live assemble) + Task 6 persistence write are the load-bearing diffs → Codex sign-off; Agent 1 (Opus) owns plan + final review/merge. Reviewer pass mandatory before master.
