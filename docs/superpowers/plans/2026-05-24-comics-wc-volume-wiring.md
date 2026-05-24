# COMICS_WC_VOLUME_WIRING Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire WeebCentral chapter scraping as a per-volume source in ComicsSeriesView's Sources panel. When MangaFire says vol 1 = "ch 1-5", the new `MangaWeebCentralResolver` looks up WeebCentral's matching chapter IDs and either offers a "WeebCentral" row in the Stremio-style sources panel (Viable) or omits it (Skip). Click on the row hands the chapter IDs to the existing `WeebCentralVolumePacker` which packs them into a cbz next to torrent-packed vols.

**Architecture:** New `MangaWeebCentralResolver` bridge class in `src/core/manga/mangafire/`. Owns a PRIVATE `WeebCentralScraper` instance (the shared one is used by Tankoyomi search/detail/packing and has no request-id discipline). Lazy on first vol click. Generation-key guarded (`seriesId + volumeNumber + requestSerial`) so rapid vol clicks don't append late rows to the wrong vol. WeebCentral seriesId cached back into the MangaCatalog JSON via an atomic patch helper. UI parity with Stream's `StreamSourceCard`/`StreamSourceList`.

**Tech Stack:** Qt6, C++17, QNetworkAccessManager (shared via ComicsPage), GoogleTest (tankoban_tests). Same patterns as existing TANKOYOMI_VOLUME_PIVOT infrastructure.

**Spec:** `docs/superpowers/specs/2026-05-24-comics-wc-volume-wiring-design.md` (commit `c6dc3ef`).

---

## File Structure

**New files:**
- `src/core/manga/mangafire/MangaWeebCentralResolver.h` — public class surface
- `src/core/manga/mangafire/MangaWeebCentralResolver.cpp` — pipeline implementation
- `tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp` — pure-logic + generation-guard tests

**Modified files:**
- `src/core/manga/MangaCatalogTypes.h` — add `WeebCentralCacheBlock` struct + `MangaCatalog.weebCentral` field
- `src/core/manga/LocalMangaCatalogLoader.cpp` — deserialize `weebCentral` block
- `src/core/manga/mangafire/MangaFireCatalogClient.h` — declare `patchWeebCentralBlock` static helper
- `src/core/manga/mangafire/MangaFireCatalogClient.cpp` — implement `patchWeebCentralBlock`
- `src/ui/pages/comics/ComicsSourcesPanel.h` — declare `appendWeebCentralRow` slot
- `src/ui/pages/comics/ComicsSourcesPanel.cpp` — replace inline WC append with public slot; label "WeebCentral pack" → "WeebCentral"
- `src/ui/pages/comics/ComicsSeriesView.h` — declare `populateSourcesForVolume(int)` + `onWeebCentralViable` slot + signal
- `src/ui/pages/comics/ComicsSeriesView.cpp` — implement `populateSourcesForVolume`; emit resolve signal; handle viable/skip slots
- `src/ui/pages/ComicsPage.h` — declare `m_wcResolver` member + slot decls
- `src/ui/pages/ComicsPage.cpp` — instantiate resolver, wire signals end-to-end
- `CMakeLists.txt` — register new resolver source in main target + test source in tankoban_tests target

---

## Phase 1 — Data layer (schema + JSON patch)

### Task 1: Add `WeebCentralCacheBlock` struct + `MangaCatalog.weebCentral` field

**Files:**
- Modify: `src/core/manga/MangaCatalogTypes.h`

- [ ] **Step 1: Append the new struct after `MangaCatalog`**

Find the closing `};` of `struct MangaCatalog` (around line 81). Immediately BEFORE the closing namespace `}`, add:

```cpp
// COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Cache hint for the
// MangaWeebCentralResolver — stores the WC seriesId we resolved for this
// series so subsequent vol clicks don't re-run the searchByTitle round-trip.
// chaptersFetchedAt + volumeChapterIds are reserved for future on-disk
// caching of the chapter enumeration; v1 leaves them empty on disk and
// keeps the chapter list in the resolver's session-scoped memory cache.
struct WeebCentralCacheBlock {
    QString    seriesId;                       // WC seriesId; empty = unresolved
    QDateTime  chaptersFetchedAt;              // reserved; empty in v1
    QHash<int, QStringList> volumeChapterIds;  // reserved; empty in v1
    bool isEmpty() const { return seriesId.isEmpty(); }
};
```

- [ ] **Step 2: Add the field to `MangaCatalog`**

In `struct MangaCatalog`, after the `notes` field (around line 79) and before the closing `};`/`isValid()` member, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 — additive (no schema-version bump).
    WeebCentralCacheBlock weebCentral;
```

Reorder if needed so `bool isValid() const { ... }` stays last in the struct.

- [ ] **Step 3: Commit**

```bash
git add src/core/manga/MangaCatalogTypes.h
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 1 -- add WeebCentralCacheBlock to MangaCatalogTypes (additive, no schema-version bump)]"
```

---

### Task 2: Extend LocalMangaCatalogLoader to deserialize the `weebCentral` block

**Files:**
- Modify: `src/core/manga/LocalMangaCatalogLoader.cpp`

- [ ] **Step 1: Add the deserialize block**

In `LocalMangaCatalogLoader::loadFromFile`, after the existing `cat.notes = root.value(...).toString();` line (around line 101) and before the `// scrapedAt -> fetchedAt` block (around line 103-110), add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Additive: read the
    // weebCentral cache block if present. Absent key = empty block (the
    // resolver will run searchByTitle on first vol click for this series).
    if (root.contains(QStringLiteral("weebCentral"))) {
        const QJsonObject wc = root.value(QStringLiteral("weebCentral")).toObject();
        cat.weebCentral.seriesId = wc.value(QStringLiteral("seriesId")).toString();
        const QString fetchedAtRaw = wc.value(QStringLiteral("chaptersFetchedAt")).toString();
        if (!fetchedAtRaw.isEmpty()) {
            cat.weebCentral.chaptersFetchedAt =
                QDateTime::fromString(fetchedAtRaw, Qt::ISODate);
        }
        const QJsonObject volsObj = wc.value(QStringLiteral("volumeChapterIds")).toObject();
        for (auto it = volsObj.constBegin(); it != volsObj.constEnd(); ++it) {
            bool ok = false;
            const int volNum = it.key().toInt(&ok);
            if (!ok || volNum <= 0) continue;
            QStringList chs;
            const QJsonArray arr = it.value().toArray();
            for (const auto& v : arr) {
                const QString s = v.toString();
                if (!s.isEmpty()) chs.append(s);
            }
            if (!chs.isEmpty()) {
                cat.weebCentral.volumeChapterIds.insert(volNum, chs);
            }
        }
    }
```

- [ ] **Step 2: Commit**

```bash
git add src/core/manga/LocalMangaCatalogLoader.cpp
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 2 -- LocalMangaCatalogLoader deserializes weebCentral cache block when present (absent key = empty block)]"
```

---

### Task 3: Add `patchWeebCentralBlock` atomic JSON-patch helper to MangaFireCatalogClient

**Files:**
- Modify: `src/core/manga/mangafire/MangaFireCatalogClient.h`
- Modify: `src/core/manga/mangafire/MangaFireCatalogClient.cpp`

- [ ] **Step 1: Declare the static helper in the header**

In `MangaFireCatalogClient.h`, inside the class `public:` section after the `fetchByTitle` method declaration, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Atomic JSON-patch helper:
    // reads data/mangafire_catalog/<seriesId>.json, mutates ONLY the
    // "weebCentral" key, writes back. Does NOT re-serialize from
    // MangaCatalog — that would drop MangaFire-only fields not yet
    // mirrored to the C++ struct (e.g. seriesTitleAlt, genres, mangazine,
    // malScoreRaw). Returns the absolute path written, empty on failure.
    static QString patchWeebCentralBlock(
        const QString& seriesId,
        const tankoban::manga::WeebCentralCacheBlock& block);
```

- [ ] **Step 2: Implement the helper in the .cpp**

In `MangaFireCatalogClient.cpp`, after the existing `writeCatalogJson` function (in the anonymous namespace) and before the `} // namespace` closer, add an OUT-OF-namespace definition. Find the `MangaFireCatalogClient::~MangaFireCatalogClient()` line and insert this BEFORE it:

```cpp
QString MangaFireCatalogClient::patchWeebCentralBlock(
    const QString& seriesId,
    const tankoban::manga::WeebCentralCacheBlock& block)
{
    if (seriesId.isEmpty()) return {};
    const QString dir = LocalMangaCatalogLoader::canonicalDataDir();
    const QString path = QDir(dir).absoluteFilePath(seriesId + QStringLiteral(".json"));

    QFile in(path);
    if (!in.exists() || !in.open(QIODevice::ReadOnly)) return {};
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &err);
    in.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};

    QJsonObject root = doc.object();
    QJsonObject wc;
    wc.insert(QStringLiteral("seriesId"), block.seriesId);
    if (block.chaptersFetchedAt.isValid()) {
        wc.insert(QStringLiteral("chaptersFetchedAt"),
                  block.chaptersFetchedAt.toUTC().toString(Qt::ISODate));
    }
    if (!block.volumeChapterIds.isEmpty()) {
        QJsonObject volsObj;
        for (auto it = block.volumeChapterIds.constBegin();
             it != block.volumeChapterIds.constEnd(); ++it) {
            QJsonArray arr;
            for (const QString& s : it.value()) arr.append(s);
            volsObj.insert(QString::number(it.key()), arr);
        }
        wc.insert(QStringLiteral("volumeChapterIds"), volsObj);
    }
    root.insert(QStringLiteral("weebCentral"), wc);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
    return path;
}
```

- [ ] **Step 3: Add the include for `WeebCentralCacheBlock`**

Confirm `#include "core/manga/MangaCatalogTypes.h"` is present in `MangaFireCatalogClient.cpp` (it should be — it's pulled in by the LocalMangaCatalogLoader.h include). If `MangaFireCatalogClient.h` doesn't transitively include it, add `#include "core/manga/MangaCatalogTypes.h"` to the header.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/mangafire/MangaFireCatalogClient.h src/core/manga/mangafire/MangaFireCatalogClient.cpp
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 3 -- patchWeebCentralBlock atomic JSON-patch helper (mutates only weebCentral key, preserves all MangaFire-only fields)]"
```

---

## Phase 2 — Resolver class

### Task 4: Declare `MangaWeebCentralResolver` class

**Files:**
- Create: `src/core/manga/mangafire/MangaWeebCentralResolver.h`

- [ ] **Step 1: Write the header**

```cpp
// src/core/manga/mangafire/MangaWeebCentralResolver.h
//
// COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1).
//
// Bridge between MangaFire-cataloged volume metadata and WeebCentral's
// chapter scrape. When the user selects a volume in ComicsSeriesView,
// resolve(catalog, volNumber, key) runs a 3-step async pipeline:
//   (a) If catalog.weebCentral.seriesId is unset, runs searchByTitle
//       against a PRIVATE WeebCentralScraper instance, takes the top hit,
//       persists the resolved WC seriesId via
//       MangaFireCatalogClient::patchWeebCentralBlock (atomic JSON patch).
//   (b) If the in-memory chapter cache for this series is empty, runs
//       fetchDetail and caches the chapter list per session.
//   (c) Filters the chapter list to vol N's [chapterRangeStart, chapterRangeEnd]
//       integer range. Verifies every integer in the range is present.
//
// Outputs ONE of:
//   - viable(key, chapterIds): WC viable for this volume; chapterIds ready
//     to hand to WeebCentralVolumePacker::requestVolume.
//   - skip(key, reasonCode):   WC NOT viable; ComicsSourcesPanel omits the
//     WeebCentral row.
//
// Generation-key guard: every resolve() carries a ResolveKey
// (seriesId + volumeNumber + requestSerial). The receiver in ComicsPage
// compares the inbound key against the current selection and drops late
// results that arrive after the user has navigated to a different volume.
//
// PRIVATE scraper requirement: the shared WeebCentralScraper exposed via
// MangaSourceRegistry is also used by Tankoyomi search, ComicsSeriesView
// detail fetches, and WeebCentralVolumePacker. Its searchFinished and
// chaptersReady signals carry no request id; sharing it would cross-talk
// with concurrent flows. The resolver owns its own private instance built
// on the shared QNetworkAccessManager.

#pragma once

#include "core/manga/MangaCatalogTypes.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class WeebCentralScraper;
class QNetworkAccessManager;

namespace tankoban::manga::mangafire {

class MangaWeebCentralResolver : public QObject
{
    Q_OBJECT
public:
    struct ResolveKey {
        QString seriesId;       // MangaFire slug
        int     volumeNumber = 0;
        quint64 requestSerial = 0;
        bool operator==(const ResolveKey& other) const {
            return seriesId == other.seriesId
                && volumeNumber == other.volumeNumber
                && requestSerial == other.requestSerial;
        }
    };

    enum class SkipReason {
        NoSeriesMatch,
        NoChapterOverlap,
        IncompleteCoverage,
        NetworkError,
    };
    static QString reasonCode(SkipReason r);

    explicit MangaWeebCentralResolver(QNetworkAccessManager* nam,
                                       QObject* parent = nullptr);
    ~MangaWeebCentralResolver() override;

    // Fire a resolve. Emits exactly one of viable / skip per call (matched
    // by ResolveKey). Concurrent resolves for the same series share the
    // single in-flight WC seriesId search + chapter-list fetch (subsequent
    // callers queue behind the first; results dispatched to all).
    void resolve(const tankoban::manga::MangaCatalog& catalog,
                  int                                  volumeNumber,
                  const ResolveKey&                    key);

    // Pure-logic helper exposed for TDD. Given a chapter id list (in any
    // order) and an integer range, returns the chapter ids whose numeric
    // prefix falls in [start, end] sorted ascending. Returns empty list
    // when coverage is incomplete (any integer in range missing from list).
    //
    // chapterIds are matched by extracting their leading integer (e.g.
    // "chapter-5", "5", "ch-5" all match volume 5). Non-numeric chapters
    // are ignored. Pure function, no side effects.
    static QStringList filterChaptersToRange(const QStringList& chapterIds,
                                              int rangeStart,
                                              int rangeEnd,
                                              bool* outIncomplete = nullptr);

signals:
    void viable(tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
                QStringList chapterIds);
    void skip(tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
              QString reasonCode);

private:
    struct PendingResolve;
    using PendingResolvePtr = std::shared_ptr<PendingResolve>;

    // After WC seriesId is known (cached or just-resolved), drive the chapter
    // fetch + filter + emit chain.
    void stepFetchChapters(PendingResolvePtr pending);
    void filterAndEmit(PendingResolvePtr pending);
    void emitSkip(PendingResolvePtr pending, SkipReason reason);
    void emitViable(PendingResolvePtr pending, const QStringList& chapterIds);

    QPointer<QNetworkAccessManager> m_nam;
    WeebCentralScraper*             m_scraper = nullptr;  // owned, private instance

    // Per-series in-memory cache of fetched chapter ids. Populated on first
    // fetchDetail per series; reused for every subsequent vol click in the
    // same series (within a session). Cleared on resolver destruction.
    QHash<QString, QStringList>     m_chapterCache;

    // In-flight tracking: keyed by MangaFire seriesId, holds the queue of
    // pending resolves awaiting the same fetch.
    QHash<QString, QList<PendingResolvePtr>> m_pendingByMangafireSeriesId;
};

} // namespace tankoban::manga::mangafire

Q_DECLARE_METATYPE(tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey)
```

- [ ] **Step 2: Commit**

```bash
git add src/core/manga/mangafire/MangaWeebCentralResolver.h
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 4 -- MangaWeebCentralResolver.h: class declaration with ResolveKey, SkipReason enum, filterChaptersToRange pure-logic helper, private-scraper architecture]"
```

---

### Task 5: TDD red — pure-logic tests for `filterChaptersToRange`

**Files:**
- Create: `tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp`

- [ ] **Step 1: Write the failing test file**

```cpp
// tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp
//
// COMICS_WC_VOLUME_WIRING Task 5 — pure-logic tests for the chapter-range
// filter. Generation-guard tests live in a sibling file once the resolver
// implementation lands (Task 8 wires them in).

#include <gtest/gtest.h>
#include <QString>
#include <QStringList>

#include "core/manga/mangafire/MangaWeebCentralResolver.h"

using tankoban::manga::mangafire::MangaWeebCentralResolver;

TEST(MangaWeebCentralResolverFilter, FullCoverageSimple)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "4", "5" }, 1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, SubsetReturnsFiltered)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "4", "5", "6", "7" }, 3, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, OutOfOrderInputSortedAscending)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "5", "3", "1", "4", "2" }, 1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, PartialCoverageFlagged)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "5" }, 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, NoOverlapEmptyResult)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "10", "11", "12" }, 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, EmptyInputEmptyResult)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{}, 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, AlphanumericChapterIdsParseLeadingInt)
{
    // Real WeebCentral chapter ids often have prefixes (e.g.
    // "chapter-5", "ch-5", "5") — the filter extracts the leading integer.
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "chapter-1", "chapter-2", "chapter-3", "chapter-4", "chapter-5" },
        1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "chapter-1", "chapter-2", "chapter-3", "chapter-4", "chapter-5" }));
}

TEST(MangaWeebCentralResolverFilter, NonNumericChaptersIgnored)
{
    // Volume-cover-only entries and side-story chapters with no integer
    // prefix are ignored entirely (not counted as missing for coverage).
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "extra", "3", "side-story", "4", "5" },
        1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, SingleChapterRange)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "5" }, 5, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "5" }));
}

TEST(MangaWeebCentralResolverFilter, OutParamOptional)
{
    // Calling without outIncomplete must not crash.
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "4", "5" }, 1, 5, nullptr);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}
```

- [ ] **Step 2: Register the test in CMakeLists.txt**

In `CMakeLists.txt`, locate the `tankoban_tests` SOURCES list (around line 862 per the existing manga catalog sources block). Add:

```cmake
        tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp
```

Place it alphabetically among the existing test sources, or grouped with other `tests/core/manga/` entries.

Also add the resolver source to the test target so the static `filterChaptersToRange` symbol is linkable:

```cmake
        src/core/manga/mangafire/MangaWeebCentralResolver.cpp
```

(The .cpp doesn't exist yet — Task 6 creates it; for now the test will fail to link, which is the RED state.)

- [ ] **Step 3: Verify red**

The test must fail to LINK because `MangaWeebCentralResolver::filterChaptersToRange` symbol is unresolved. Do not build yet (Task 6 creates the .cpp).

- [ ] **Step 4: Commit (RED state)**

```bash
git add tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp CMakeLists.txt
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 5 RED -- pure-logic tests for filterChaptersToRange (10 test cases covering full/partial/empty/alphanumeric coverage; expected RED until Task 6 lands the .cpp)]"
```

---

### Task 6: TDD green — implement `filterChaptersToRange` + resolver skeleton

**Files:**
- Create: `src/core/manga/mangafire/MangaWeebCentralResolver.cpp`

- [ ] **Step 1: Write the minimal .cpp that satisfies the pure-logic tests**

```cpp
// src/core/manga/mangafire/MangaWeebCentralResolver.cpp
//
// COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Implementation notes:
//
//   - The async pipeline is wired in Task 7. This file lands the class
//     scaffolding + the pure-logic filterChaptersToRange helper that the
//     Task 5 tests depend on.
//   - Generation guards are intrinsic to the ResolveKey carried on every
//     viable/skip emission. Stale results are dropped at the receiver
//     (ComicsPage), not here.

#include "MangaWeebCentralResolver.h"

#include "core/manga/WeebCentralScraper.h"
#include "core/manga/mangafire/MangaFireCatalogClient.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QRegularExpression>

#include <algorithm>
#include <memory>

namespace tankoban::manga::mangafire {

namespace {

// Extract the leading integer from a chapter id string. Returns -1 when no
// leading integer is present. Matches "5", "chapter-5", "ch-5", "ch5", etc.
int leadingInt(const QString& chapterId) {
    static const QRegularExpression rx(QStringLiteral("(\\d+)"));
    const auto m = rx.match(chapterId);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int v = m.captured(1).toInt(&ok);
    return ok ? v : -1;
}

} // namespace

QString MangaWeebCentralResolver::reasonCode(SkipReason r) {
    switch (r) {
        case SkipReason::NoSeriesMatch:     return QStringLiteral("NoSeriesMatch");
        case SkipReason::NoChapterOverlap:  return QStringLiteral("NoChapterOverlap");
        case SkipReason::IncompleteCoverage: return QStringLiteral("IncompleteCoverage");
        case SkipReason::NetworkError:      return QStringLiteral("NetworkError");
    }
    return QStringLiteral("Unknown");
}

QStringList MangaWeebCentralResolver::filterChaptersToRange(
    const QStringList& chapterIds, int rangeStart, int rangeEnd,
    bool* outIncomplete)
{
    if (outIncomplete) *outIncomplete = false;

    if (chapterIds.isEmpty() || rangeEnd < rangeStart) {
        if (outIncomplete) *outIncomplete = true;
        return {};
    }

    // Map integer -> first chapterId we see for that integer. (Real WC data
    // has one chapter per integer; if a duplicate appears we take the first.)
    QHash<int, QString> byInt;
    for (const QString& id : chapterIds) {
        const int n = leadingInt(id);
        if (n < rangeStart || n > rangeEnd) continue;
        if (!byInt.contains(n)) byInt.insert(n, id);
    }

    // Coverage check: every integer in [start, end] must be present.
    for (int n = rangeStart; n <= rangeEnd; ++n) {
        if (!byInt.contains(n)) {
            if (outIncomplete) *outIncomplete = true;
            return {};
        }
    }

    // Emit in ascending integer order.
    QStringList out;
    out.reserve(rangeEnd - rangeStart + 1);
    for (int n = rangeStart; n <= rangeEnd; ++n) {
        out.append(byInt.value(n));
    }
    return out;
}

// Pipeline-related struct + member implementations land in Task 7.
struct MangaWeebCentralResolver::PendingResolve {
    ResolveKey key;
    // Snapshot of the per-volume range so we don't have to re-read the
    // MangaCatalog if it mutates mid-resolve.
    int chapterRangeStart = 0;
    int chapterRangeEnd = 0;
    // Caller-visible series title for the searchByTitle call (snapshot too).
    QString seriesTitle;
};

MangaWeebCentralResolver::MangaWeebCentralResolver(QNetworkAccessManager* nam,
                                                    QObject* parent)
    : QObject(parent), m_nam(nam), m_scraper(nullptr)
{
    // Task 7 wires up the private WeebCentralScraper instance + signal
    // connections. v0 (this task) leaves m_scraper null; resolve() emits
    // skip(NetworkError) when called before Task 7's wiring lands.
}

MangaWeebCentralResolver::~MangaWeebCentralResolver() = default;

void MangaWeebCentralResolver::resolve(
    const tankoban::manga::MangaCatalog& /*catalog*/,
    int /*volumeNumber*/,
    const ResolveKey& key)
{
    // Task 7 fills the pipeline. v0 returns NetworkError so the path is
    // testable end-to-end before the WC scraper wiring lands.
    emit skip(key, reasonCode(SkipReason::NetworkError));
}

void MangaWeebCentralResolver::stepFetchChapters(PendingResolvePtr /*pending*/) {}
void MangaWeebCentralResolver::filterAndEmit(PendingResolvePtr /*pending*/) {}
void MangaWeebCentralResolver::emitSkip(PendingResolvePtr /*pending*/, SkipReason /*r*/) {}
void MangaWeebCentralResolver::emitViable(PendingResolvePtr /*pending*/,
                                           const QStringList& /*chs*/) {}

} // namespace tankoban::manga::mangafire
```

- [ ] **Step 2: Register the .cpp in main app sources (already added to test target in Task 5)**

In `CMakeLists.txt`, locate the main `Tankoban` target SOURCES list (the one containing `src/core/manga/mangafire/MangaFireCatalogClient.cpp` from the previous arc, around line 177). Add:

```cmake
    src/core/manga/mangafire/MangaWeebCentralResolver.cpp
```

(Place it immediately after the existing `MangaFireCatalogClient.cpp` line.)

- [ ] **Step 3: Build the test target + verify all 10 tests pass**

Run:
```bash
TANKOBAN_AGENT_ID=agent-1 cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat\" x64 >nul 2>&1 && cmake --build out --target tankoban_tests && out\tankoban_tests.exe --gtest_filter=MangaWeebCentralResolverFilter.*"
```

Expected: 10 PASSED, 0 FAILED.

- [ ] **Step 4: Commit (GREEN state for pure-logic phase)**

```bash
git add src/core/manga/mangafire/MangaWeebCentralResolver.cpp CMakeLists.txt
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 6 GREEN -- filterChaptersToRange impl + class scaffolding; 10 pure-logic tests pass; pipeline stubbed to emit skip(NetworkError) until Task 7]"
```

---

### Task 7: Implement the async resolve pipeline

**Files:**
- Modify: `src/core/manga/mangafire/MangaWeebCentralResolver.cpp`

- [ ] **Step 1: Replace the constructor + stubbed methods with the full pipeline**

In the constructor, instantiate the PRIVATE WeebCentralScraper and connect its signals:

```cpp
MangaWeebCentralResolver::MangaWeebCentralResolver(QNetworkAccessManager* nam,
                                                    QObject* parent)
    : QObject(parent), m_nam(nam),
      m_scraper(new WeebCentralScraper(nam, this))
{
    // PRIVATE scraper instance. Do NOT swap to a shared one from
    // MangaSourceRegistry; those signals are used by Tankoyomi search,
    // ComicsSeriesView detail fetches, and WeebCentralVolumePacker, and
    // they carry no request id. Sharing causes cross-talk between flows.

    connect(m_scraper, &MangaScraper::searchFinished,
            this, [this](const QList<MangaResult>& results) {
        // Identify the pending resolve that triggered this search. Our
        // private scraper guarantees one search in flight at a time per
        // series; the matching pending entry has empty WC seriesId in the
        // catalog snapshot AND non-empty seriesTitle in pending.
        if (m_inflightSearch.isEmpty()) return;
        const QString mangaFireId = m_inflightSearch;
        m_inflightSearch.clear();

        if (results.isEmpty()) {
            // No WC match; emit skip(NoSeriesMatch) for every queued pending.
            const auto queued = m_pendingByMangafireSeriesId.take(mangaFireId);
            for (const auto& p : queued) {
                emitSkip(p, SkipReason::NoSeriesMatch);
            }
            return;
        }

        const QString wcSeriesId = results.first().id;
        // Persist the cache hint via atomic JSON patch. Failure to persist
        // is non-fatal — we'll just re-resolve next session.
        tankoban::manga::WeebCentralCacheBlock block;
        block.seriesId = wcSeriesId;
        block.chaptersFetchedAt = QDateTime::currentDateTimeUtc();
        MangaFireCatalogClient::patchWeebCentralBlock(mangaFireId, block);

        // Stamp every pending resolve's cached WC seriesId and proceed to
        // chapter fetch.
        auto& queued = m_pendingByMangafireSeriesId[mangaFireId];
        for (const auto& p : queued) {
            p->cachedWcSeriesId = wcSeriesId;
        }
        if (!queued.isEmpty()) {
            stepFetchChapters(queued.first());
        }
    });

    connect(m_scraper, &MangaScraper::detailReady,
            this, [this](const MangaSeriesDetail& detail) {
        // The detail carries chapter ids in detail.chapters; cache them and
        // drain every queued pending for the matching WC seriesId.
        if (m_inflightFetch.isEmpty()) return;
        const QString wcSeriesId = m_inflightFetch;
        m_inflightFetch.clear();

        QStringList chapterIds;
        chapterIds.reserve(detail.chapters.size());
        for (const auto& ch : detail.chapters) {
            if (!ch.id.isEmpty()) chapterIds.append(ch.id);
        }
        m_chapterCache.insert(wcSeriesId, chapterIds);

        // Drain every pending resolve whose cached WC seriesId matches.
        // Pending entries live keyed by MangaFire seriesId; we scan all
        // values for matches.
        QList<PendingResolvePtr> ready;
        for (auto it = m_pendingByMangafireSeriesId.begin();
             it != m_pendingByMangafireSeriesId.end(); ) {
            auto& queue = it.value();
            QList<PendingResolvePtr> stillPending;
            for (const auto& p : queue) {
                if (p->cachedWcSeriesId == wcSeriesId) {
                    ready.append(p);
                } else {
                    stillPending.append(p);
                }
            }
            if (stillPending.isEmpty()) {
                it = m_pendingByMangafireSeriesId.erase(it);
            } else {
                queue = stillPending;
                ++it;
            }
        }
        for (const auto& p : ready) {
            filterAndEmit(p);
        }
    });

    connect(m_scraper, &MangaScraper::scrapeFailed,
            this, [this](const QString& /*code*/, const QString& /*reason*/) {
        // Drop every in-flight pending with NetworkError on hard failure.
        // (Either search or fetchDetail failed; we treat both as terminal
        // for the pending generation.)
        const auto all = m_pendingByMangafireSeriesId;
        m_pendingByMangafireSeriesId.clear();
        m_inflightSearch.clear();
        m_inflightFetch.clear();
        for (const auto& queue : all) {
            for (const auto& p : queue) {
                emitSkip(p, SkipReason::NetworkError);
            }
        }
    });
}
```

- [ ] **Step 2: Add the two in-flight state members to the header**

In `MangaWeebCentralResolver.h`, in the `private:` section after `m_pendingByMangafireSeriesId`, add:

```cpp
    // Single-in-flight tracking per phase: at most one WC searchByTitle in
    // flight (m_inflightSearch holds the MangaFire seriesId that triggered
    // it); at most one fetchDetail in flight (m_inflightFetch holds the WC
    // seriesId). Per-series serialization avoids the shared scraper's
    // signal-id-less surface.
    QString m_inflightSearch;
    QString m_inflightFetch;
```

And in the pending-resolve struct, add the cached WC seriesId field:

In `MangaWeebCentralResolver.cpp`, update the PendingResolve struct definition:

```cpp
struct MangaWeebCentralResolver::PendingResolve {
    ResolveKey key;
    int chapterRangeStart = 0;
    int chapterRangeEnd = 0;
    QString seriesTitle;
    QString mangaFireSeriesId;
    QString cachedWcSeriesId;     // populated after step (a) finishes
};
```

- [ ] **Step 3: Implement `resolve` to drive the pipeline**

Replace the stubbed `resolve` with:

```cpp
void MangaWeebCentralResolver::resolve(
    const tankoban::manga::MangaCatalog& catalog,
    int volumeNumber,
    const ResolveKey& key)
{
    // Look up the volume's chapter range.
    int rangeStart = 0, rangeEnd = 0;
    for (const auto& v : catalog.volumes) {
        if (v.volumeNumber == volumeNumber) {
            rangeStart = v.chapterRangeStart;
            rangeEnd   = v.chapterRangeEnd;
            break;
        }
    }
    if (rangeStart <= 0 || rangeEnd < rangeStart) {
        emit skip(key, reasonCode(SkipReason::NoChapterOverlap));
        return;
    }

    auto pending = std::make_shared<PendingResolve>();
    pending->key = key;
    pending->chapterRangeStart = rangeStart;
    pending->chapterRangeEnd = rangeEnd;
    pending->seriesTitle = catalog.seriesTitle;
    pending->mangaFireSeriesId = catalog.seriesId;
    pending->cachedWcSeriesId = catalog.weebCentral.seriesId;

    // Fast path: WC seriesId AND chapter list both cached -> filter inline.
    if (!pending->cachedWcSeriesId.isEmpty()
     && m_chapterCache.contains(pending->cachedWcSeriesId)) {
        filterAndEmit(pending);
        return;
    }

    // Queue behind any in-flight resolve for this MangaFire series.
    m_pendingByMangafireSeriesId[catalog.seriesId].append(pending);
    if (m_pendingByMangafireSeriesId[catalog.seriesId].size() > 1) {
        return;  // first pending will drive the chain; later ones ride along
    }

    // Step (a) or (b): which phase do we need?
    if (pending->cachedWcSeriesId.isEmpty()) {
        // Need searchByTitle to resolve WC seriesId.
        if (!m_inflightSearch.isEmpty() || !m_scraper) {
            // Should not happen given single-resolve-per-series queueing;
            // be defensive.
            emitSkip(pending, SkipReason::NetworkError);
            m_pendingByMangafireSeriesId.remove(catalog.seriesId);
            return;
        }
        m_inflightSearch = catalog.seriesId;
        m_scraper->search(pending->seriesTitle, /*limit*/1);
    } else {
        // Have WC seriesId; need chapter list.
        stepFetchChapters(pending);
    }
}
```

- [ ] **Step 4: Implement `stepFetchChapters` + `filterAndEmit` + `emitSkip` + `emitViable`**

```cpp
void MangaWeebCentralResolver::stepFetchChapters(PendingResolvePtr pending)
{
    const QString wcSeriesId = pending->cachedWcSeriesId;
    if (wcSeriesId.isEmpty()) {
        emitSkip(pending, SkipReason::NoSeriesMatch);
        return;
    }

    // Already cached? Filter inline.
    if (m_chapterCache.contains(wcSeriesId)) {
        filterAndEmit(pending);
        return;
    }

    // Need fetchDetail. Build a minimal MangaResult to seed the call.
    if (!m_inflightFetch.isEmpty() || !m_scraper) {
        // Another fetch in flight (different WC seriesId) — defensive skip.
        emitSkip(pending, SkipReason::NetworkError);
        return;
    }
    m_inflightFetch = wcSeriesId;
    MangaResult seed;
    seed.id = wcSeriesId;
    seed.source = m_scraper->sourceId();  // "weebcentral"
    seed.title = pending->seriesTitle;
    m_scraper->fetchDetail(seed);
}

void MangaWeebCentralResolver::filterAndEmit(PendingResolvePtr pending)
{
    const auto it = m_chapterCache.constFind(pending->cachedWcSeriesId);
    if (it == m_chapterCache.constEnd()) {
        emitSkip(pending, SkipReason::NetworkError);
        return;
    }
    bool incomplete = false;
    const QStringList chs = filterChaptersToRange(
        it.value(), pending->chapterRangeStart, pending->chapterRangeEnd,
        &incomplete);
    if (incomplete) {
        emitSkip(pending, SkipReason::IncompleteCoverage);
        return;
    }
    emitViable(pending, chs);
}

void MangaWeebCentralResolver::emitSkip(PendingResolvePtr pending, SkipReason r)
{
    emit skip(pending->key, reasonCode(r));
}

void MangaWeebCentralResolver::emitViable(PendingResolvePtr pending,
                                           const QStringList& chapterIds)
{
    emit viable(pending->key, chapterIds);
}
```

- [ ] **Step 5: Confirm the includes**

The .cpp needs:
```cpp
#include "core/manga/MangaResult.h"
#include "core/manga/MangaSeriesDetail.h"
```
plus the already-added `WeebCentralScraper.h` and `MangaFireCatalogClient.h` (Task 6).

- [ ] **Step 6: Rebuild tests; the pure-logic suite still passes**

Run:
```bash
TANKOBAN_AGENT_ID=agent-1 cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat\" x64 >nul 2>&1 && cmake --build out --target tankoban_tests && out\tankoban_tests.exe --gtest_filter=MangaWeebCentralResolverFilter.*"
```

Expected: still 10 PASSED. The async pipeline isn't exercised by these tests but the file must still compile and link cleanly.

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/mangafire/MangaWeebCentralResolver.h src/core/manga/mangafire/MangaWeebCentralResolver.cpp
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 7 -- async pipeline (private WeebCentralScraper + per-series queueing + cache + atomic JSON-patch persist); pure-logic tests stay GREEN]"
```

---

## Phase 3 — UI wire-up

### Task 8: Add `populateSourcesForVolume(int volumeNumber)` to ComicsSeriesView

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h`
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1: Declare the new public slot in the header**

In `ComicsSeriesView.h`, in the `public slots:` (or `public:` if no slots section) section near the existing `populateSourcesForRow` declaration, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Catalog-tile-path
    // entry point. Looks up the row by volumeNumber in m_currentVolumeRows,
    // delegates to populateSourcesForRow if found, OR fabricates a minimal
    // VolumeRow from m_currentMangaCatalog when populateVolumeRowsFromCatalog
    // is the source (m_currentVolumeRows is empty in that path because the
    // catalog path bypasses the AniList VolumeRow list).
    void populateSourcesForVolume(int volumeNumber);
```

Also add a new signal:

```cpp
signals:
    // ... existing signals ...

    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Emitted whenever the
    // user selects a volume so ComicsPage can fire the WC resolve. Carries
    // the MangaFire seriesId from the catalog + the volume number. The
    // requestSerial is allocated by ComicsPage (not the view); the view
    // does not own the generation counter.
    void weebCentralResolveRequested(const QString& mangaFireSeriesId,
                                      int volumeNumber);
```

And a slot to receive the resolver's viable verdict:

```cpp
public slots:
    // ... existing slots ...

    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Fired by ComicsPage
    // when the WC resolver returns Viable. The receiver appends a
    // "WeebCentral" row to ComicsSourcesPanel with these chapter ids.
    // Stale-event guard lives in ComicsPage; if this slot fires, the
    // chapterIds are for the currently-displayed volume.
    void onWeebCentralViable(int volumeNumber, const QStringList& chapterIds);
```

- [ ] **Step 2: Implement `populateSourcesForVolume`**

In `ComicsSeriesView.cpp`, immediately after the existing `populateSourcesForRow` definition (around line 2028 — find the closing `}` of that function), add:

```cpp
void ComicsSeriesView::populateSourcesForVolume(int volumeNumber)
{
    if (volumeNumber <= 0) return;
    if (!m_sourcesPanel) return;

    // Path 1: AniList-driven series (m_currentVolumeRows populated by
    // populateVolumeRows). Find the row by volumeNumber.
    for (int i = 0; i < m_currentVolumeRows.size(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber == volumeNumber) {
            populateSourcesForRow(i);
            // Kick the WC resolve so the panel can append a WeebCentral row
            // when the resolver returns Viable.
            emit weebCentralResolveRequested(/*seriesId*/QString(), volumeNumber);
            return;
        }
    }

    // Path 2: MangaFire-catalog-driven series (m_currentVolumeRows is empty;
    // tiles were built from m_currentMangaCatalog). Fabricate a minimal
    // VolumeRow for populateSourcesForRow's existing contract.
    // m_currentMangaCatalog is set in populateVolumeRowsFromCatalog (Task 9
    // below adds the snapshot).
    if (m_currentMangaCatalog.isValid()) {
        for (const auto& vol : m_currentMangaCatalog.volumes) {
            if (vol.volumeNumber != volumeNumber) continue;
            anilist::VolumeRow stub;
            stub.volumeNumber = vol.volumeNumber;
            stub.isVolumeX = false;
            stub.chapterNumbers = QStringList();  // resolver supplies WC ids
            // populateSourcesForRow expects the row to live in
            // m_currentVolumeRows; temporarily append + restore.
            m_currentVolumeRows.append(stub);
            const int row = m_currentVolumeRows.size() - 1;
            populateSourcesForRow(row);
            m_currentVolumeRows.removeLast();

            emit weebCentralResolveRequested(m_currentMangaCatalog.seriesId,
                                              volumeNumber);
            return;
        }
    }
}

void ComicsSeriesView::onWeebCentralViable(int volumeNumber,
                                            const QStringList& chapterIds)
{
    if (!m_sourcesPanel) return;
    m_sourcesPanel->appendWeebCentralRow(volumeNumber, chapterIds);
}
```

- [ ] **Step 3: Add the m_currentMangaCatalog member + capture in populateVolumeRowsFromCatalog**

In `ComicsSeriesView.h`, in the `private:` data members section, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Snapshot of the
    // currently-displayed MangaFire catalog, captured at the top of
    // populateVolumeRowsFromCatalog. Used by populateSourcesForVolume to
    // look up the per-volume chapter range when m_currentVolumeRows is
    // empty (catalog-tile-driven open).
    tankoban::manga::MangaCatalog m_currentMangaCatalog;
```

In `ComicsSeriesView.cpp`, in `populateVolumeRowsFromCatalog` (around line 1234), after the existing `m_currentSeriesKey = QStringLiteral("mangafire:%1").arg(catalog.seriesId);` assignment, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 — snapshot for populateSourcesForVolume.
    m_currentMangaCatalog = catalog;
```

And in `clearView` (find it via search; the function teardown happens around lines 891+), add:

```cpp
    m_currentMangaCatalog = tankoban::manga::MangaCatalog{};
```

- [ ] **Step 4: Replace the catalog-tile `populateSourcesForRow(-1)` call site**

In `populateVolumeRowsFromCatalog`, find the `downloadRequested` lambda (around line 1294-1302):

```cpp
        connect(tile, &tankoban::ui::comics::VolumeTile::downloadRequested,
                this, [this](int /*vn*/) {
                    populateSourcesForRow(-1);
                });
```

Replace with:

```cpp
        connect(tile, &tankoban::ui::comics::VolumeTile::downloadRequested,
                this, [this](int vn) {
                    populateSourcesForVolume(vn);
                });
```

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 8 -- populateSourcesForVolume(int) replaces the legacy populateSourcesForRow(-1) catalog-tile call site; snapshots m_currentMangaCatalog; emits weebCentralResolveRequested for the resolver fan-out]"
```

---

### Task 9: ComicsSourcesPanel — relabel + drop inline WC row + add `appendWeebCentralRow` slot

**Files:**
- Modify: `src/ui/pages/comics/ComicsSourcesPanel.h`
- Modify: `src/ui/pages/comics/ComicsSourcesPanel.cpp`

- [ ] **Step 1: Declare the new public slot in the header**

In `ComicsSourcesPanel.h`, in the public `slots:` section, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Append a "WeebCentral"
    // row to the currently-displayed source list. Called by
    // ComicsSeriesView::onWeebCentralViable when the resolver returns
    // Viable. volumeNumber is used to guard against late results landing
    // after the user navigates to a different vol; if it does not match
    // the currently-displayed volume context, the call is a no-op.
    void appendWeebCentralRow(int volumeNumber, const QStringList& chapterIds);
```

Add a tracking member for the current vol context:

```cpp
private:
    // ... existing members ...

    int m_currentContextVolumeNumber = 0;
```

- [ ] **Step 2: Drop the inline WC row append**

In `ComicsSourcesPanel.cpp`, find the existing block (around lines 250-259):

```cpp
    if (!chapterIds.isEmpty()) {
        UnifiedSourceRow wcRow;
        wcRow.kind = UnifiedSourceRow::Kind::WeebCentralPacker;
        wcRow.tier = 99;
        wcRow.title = QStringLiteral("WeebCentral pack");
        wcRow.uploaderHint = wcSubtitle(chapterIds);
        wcRow.seeders = -1;
        wcRow.sizeBytes = 0;
        appendRow(wcRow);
    }
```

DELETE it. The WC row is now driven exclusively by the resolver via `appendWeebCentralRow`.

- [ ] **Step 3: Track the current vol context**

In `ComicsSourcesPanel::populate(...)` (the function containing the deleted block), after the function's preamble where it knows the current volume number, store it:

```cpp
    m_currentContextVolumeNumber = vol.volumeNumber;
```

In `setContext(int volumeNumber, const QString& volumeTitle)` (around line 280), also set:

```cpp
    m_currentContextVolumeNumber = volumeNumber;
```

- [ ] **Step 4: Implement `appendWeebCentralRow`**

At the end of `ComicsSourcesPanel.cpp` (before any closing `} // namespace` if present), add:

```cpp
void ComicsSourcesPanel::appendWeebCentralRow(int volumeNumber,
                                                const QStringList& chapterIds)
{
    // Stale-event guard: drop if the user has moved to a different volume.
    if (volumeNumber <= 0 || volumeNumber != m_currentContextVolumeNumber) {
        return;
    }
    if (chapterIds.isEmpty()) return;

    UnifiedSourceRow wcRow;
    wcRow.kind = UnifiedSourceRow::Kind::WeebCentralPacker;
    wcRow.tier = 99;                              // always last in the sort order
    wcRow.title = QStringLiteral("WeebCentral");  // anchor decision 2 — bare name
    wcRow.uploaderHint = QString();               // anchor decision 3 — no per-row aux
    wcRow.seeders = -1;
    wcRow.sizeBytes = 0;
    // chapterIds ride along on the row payload so the downstream dispatch
    // (ComicsPage routes WeebCentralPacker rows to
    // WeebCentralVolumePacker::requestVolume) has the right chapter list.
    wcRow.weebCentralChapterIds = chapterIds;
    appendRow(wcRow);

    // Re-render the panel with the updated row list.
    setSources(m_rows, /*nyaaInFlight*/false);
}
```

- [ ] **Step 5: Add the `weebCentralChapterIds` field to UnifiedSourceRow**

Find the `UnifiedSourceRow` struct (likely in `src/core/manga/comics/UnifiedSourceRow.h` or near the `ComicsSourcesPanel.h` includes). Add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Populated when
    // kind == WeebCentralPacker; empty for all other kinds. The dispatch
    // path forwards this list verbatim to
    // WeebCentralVolumePacker::requestVolume.
    QStringList weebCentralChapterIds;
```

If `UnifiedSourceRow` lives in a header you can't easily locate, grep the codebase:
```bash
grep -rn "struct UnifiedSourceRow" src/
```

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/comics/ComicsSourcesPanel.h src/ui/pages/comics/ComicsSourcesPanel.cpp src/core/manga/comics/UnifiedSourceRow.h
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 9 -- ComicsSourcesPanel: drop inline 'WeebCentral pack' append; add appendWeebCentralRow(volumeNumber, chapterIds) slot with stale-vol guard; rename to bare 'WeebCentral'; payload chapterIds ride on UnifiedSourceRow]"
```

---

### Task 10: ComicsPage — own and wire the resolver

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Forward-declare + declare members in the header**

In `ComicsPage.h`, in the `namespace tankoban::manga::mangafire { }` block (near where `MangaFireCatalogClient` is forward-declared), add:

```cpp
    namespace mangafire {
        class MangaFireCatalogClient;
        class MangaWeebCentralResolver;        // COMICS_WC_VOLUME_WIRING 2026-05-24
    }
```

In the `private slots:` section, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1).
    void onWcResolveRequested(const QString& mangaFireSeriesIdFromView,
                               int volumeNumber);
    void onWcResolverViable(
        tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
        QStringList chapterIds);
    void onWcResolverSkip(
        tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
        QString reasonCode);
```

In the `private:` data members section, near `m_mangafireClient`, add:

```cpp
    tankoban::manga::mangafire::MangaWeebCentralResolver* m_wcResolver = nullptr;

    // Monotonic counter for ResolveKey generation guard. Incremented on
    // every weebCentralResolveRequested fan-out so late resolver responses
    // are dropped at onWcResolverViable / onWcResolverSkip.
    quint64 m_wcResolveSerial = 0;

    // Last-fan-out key — stale-event guard. Inbound viable/skip whose key
    // does not match are discarded.
    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey
        m_currentWcResolveKey;
```

- [ ] **Step 2: Instantiate the resolver and connect its signals**

In `ComicsPage.cpp`, find the existing block that instantiates `m_mangafireClient` (around line 168). Immediately AFTER that block, add:

```cpp
    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). MangaWeebCentralResolver
    // bridges MangaFire's per-volume chapter ranges to WeebCentral's actual
    // chapter list. Lazy on first vol click; caches WC seriesId back to JSON.
    m_wcResolver = new tankoban::manga::mangafire::MangaWeebCentralResolver(
                       m_nam, this);
    connect(m_wcResolver,
            &tankoban::manga::mangafire::MangaWeebCentralResolver::viable,
            this, &ComicsPage::onWcResolverViable);
    connect(m_wcResolver,
            &tankoban::manga::mangafire::MangaWeebCentralResolver::skip,
            this, &ComicsPage::onWcResolverSkip);

    // ComicsSeriesView -> ComicsPage fan-out. The view emits when the user
    // selects a vol; this page allocates the ResolveKey serial + drives the
    // resolver.
    if (m_tyVolumeSeriesView) {
        connect(m_tyVolumeSeriesView,
                &tankoban::manga::comics::ComicsSeriesView::weebCentralResolveRequested,
                this, &ComicsPage::onWcResolveRequested);
    }
```

(If the m_tyVolumeSeriesView is constructed later than this point, move the connect block to the same place that other m_tyVolumeSeriesView signal connections happen — search the file for an existing connect on `m_tyVolumeSeriesView` to find the right spot.)

- [ ] **Step 3: Implement the three slot bodies**

At the end of `ComicsPage.cpp` (immediately before the final closing brace, or alongside the existing `onMangaFireCatalogReady` slot), add:

```cpp
// COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1).
void ComicsPage::onWcResolveRequested(const QString& mangaFireSeriesIdFromView,
                                        int volumeNumber)
{
    if (!m_wcResolver) return;
    if (volumeNumber <= 0) return;

    // Resolve the catalog data. The view-supplied seriesId is empty for the
    // AniList path (no MangaCatalog snapshot) — in that case we can't fire
    // the WC resolve because we don't have a guaranteed slug-to-WC-series
    // path. Catalog-driven series carry the slug.
    QString slug = mangaFireSeriesIdFromView;
    if (slug.isEmpty()) {
        // AniList path: fall back to the current series title via the local
        // catalog index (matches dispatchCatalogResolve's title-hint path).
        slug = m_localCatalogIndex.slugForSeriesTitle(m_currentDetailSeriesTitle);
    }
    if (slug.isEmpty()) {
        qInfo("ComicsPage::onWcResolveRequested: no MangaFire slug for vol=%d title=%s; skipping WC resolve",
              volumeNumber, qUtf8Printable(m_currentDetailSeriesTitle));
        return;
    }

    const QString jsonPath = m_localCatalogIndex.filePathForSlug(slug);
    if (jsonPath.isEmpty()) {
        qInfo("ComicsPage::onWcResolveRequested: no local catalog JSON for slug=%s",
              qUtf8Printable(slug));
        return;
    }
    const auto catalog = tankoban::manga::LocalMangaCatalogLoader::loadFromFile(jsonPath);
    if (!catalog.has_value()) {
        qWarning("ComicsPage::onWcResolveRequested: catalog load failed for %s",
                 qUtf8Printable(jsonPath));
        return;
    }

    // Allocate a fresh generation key + stash it.
    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key;
    key.seriesId      = catalog->seriesId;
    key.volumeNumber  = volumeNumber;
    key.requestSerial = ++m_wcResolveSerial;
    m_currentWcResolveKey = key;

    qInfo("ComicsPage::onWcResolveRequested: firing resolver for seriesId=%s vol=%d serial=%llu",
          qUtf8Printable(catalog->seriesId), volumeNumber,
          static_cast<unsigned long long>(key.requestSerial));
    m_wcResolver->resolve(*catalog, volumeNumber, key);
}

void ComicsPage::onWcResolverViable(
    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
    QStringList chapterIds)
{
    // Generation-guard: drop late results.
    if (!(key == m_currentWcResolveKey)) {
        qInfo("ComicsPage::onWcResolverViable: stale (key=%s/%d/%llu vs current=%s/%d/%llu); dropping",
              qUtf8Printable(key.seriesId), key.volumeNumber,
              static_cast<unsigned long long>(key.requestSerial),
              qUtf8Printable(m_currentWcResolveKey.seriesId),
              m_currentWcResolveKey.volumeNumber,
              static_cast<unsigned long long>(m_currentWcResolveKey.requestSerial));
        return;
    }
    if (!m_tyVolumeSeriesView) return;
    m_tyVolumeSeriesView->onWeebCentralViable(key.volumeNumber, chapterIds);
}

void ComicsPage::onWcResolverSkip(
    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
    QString reasonCode)
{
    if (!(key == m_currentWcResolveKey)) return;  // stale
    qInfo("ComicsPage::onWcResolverSkip: seriesId=%s vol=%d reason=%s",
          qUtf8Printable(key.seriesId), key.volumeNumber,
          qUtf8Printable(reasonCode));
    // No UI action — Sources panel just doesn't gain a WC row.
}
```

- [ ] **Step 4: Add the `#include` for the resolver header**

At the top of `ComicsPage.cpp`, alongside the existing `MangaFireCatalogClient.h` include, add:

```cpp
#include "core/manga/mangafire/MangaWeebCentralResolver.h"
```

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 10 -- ComicsPage owns m_wcResolver; allocates ResolveKey serial per vol fan-out; routes viable/skip back through ComicsSeriesView; stale-event guard drops late results]"
```

---

### Task 11: Route the WeebCentralPacker source-click to WeebCentralVolumePacker::requestVolume

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Locate the source-row click dispatch**

Grep for the slot that handles `downloadDispatchRequested` / the equivalent — `ComicsPage` already wires Nyaa torrent dispatch via `m_nyaaRuntime` + `TorrentVolumeProvider`. WeebCentral dispatch may exist already (TANKOYOMI_VOLUME_PIVOT Phase 5) — verify it's still wired to `m_weebCentralPacker`.

```bash
grep -n "WeebCentralPacker\|m_weebCentralPacker\|m_weebCentralVolumePacker" src/ui/pages/ComicsPage.cpp
```

- [ ] **Step 2: Confirm the dispatch carries chapterIds from the new payload**

The dispatch slot likely takes a `UnifiedSourceRow` and inspects `row.kind`. For `Kind::WeebCentralPacker`, ensure it builds the `VolumePackRequest` with:

```cpp
VolumePackRequest req;
req.seriesId        = catalog.seriesId;      // MangaFire slug — anchor decision 13
req.volumeNumber    = currentDetailVolumeNumber;
req.destinationPath = canonicalSeriesDestinationDir(catalog.seriesId);
req.chapterIds      = row.weebCentralChapterIds;  // from the new payload
m_weebCentralPacker->requestVolume(req);
```

**Critical:** the `req.seriesId` MUST be the MangaFire slug (`catalog.seriesId`), NOT `QStringLiteral("anilist_%1").arg(anilistId)`. Anchor decision 13 — otherwise the completed cbz won't light up the MangaFire vol tile via MangaDownloadIndex.

If the existing dispatch hardcoded the `anilist_%1` shape, replace it.

- [ ] **Step 3: Add a qInfo log on dispatch for smoke verification**

```cpp
qInfo("ComicsPage: dispatching WeebCentralPacker req seriesId=%s vol=%d chapters=%lld dest=%s",
      qUtf8Printable(req.seriesId), req.volumeNumber,
      static_cast<long long>(req.chapterIds.size()),
      qUtf8Printable(req.destinationPath));
```

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "[Agent 1, COMICS_WC_VOLUME_WIRING Task 11 -- WeebCentralPacker dispatch uses MangaFire seriesId (NOT anilist_<id>); chapterIds sourced from UnifiedSourceRow.weebCentralChapterIds payload]"
```

---

## Phase 4 — Build verification + smoke

### Task 12: Full build verification

**Files:** none (verification only)

- [ ] **Step 1: Acquire the build lease**

```bash
out\tankoctl.exe lease-acquire build --holder agent-1 --purpose "COMICS_WC_VOLUME_WIRING build_check" --ttl-sec 1800
```

If the lease is held by another agent, coordinate via chat.md.

- [ ] **Step 2: Kill any running Tankoban (Rule 1)**

```bash
powershell -NoProfile -Command "Get-Process Tankoban -ErrorAction SilentlyContinue | Stop-Process -Force"
```

- [ ] **Step 3: Build the main app target**

```bash
TANKOBAN_AGENT_ID=agent-1 cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat\" x64 >nul 2>&1 && cmake --build out --config Release --target Tankoban"
```

Expected: `Linking CXX executable Tankoban.exe` and no `FAILED` lines.

- [ ] **Step 4: Build the test target + run the new tests**

```bash
TANKOBAN_AGENT_ID=agent-1 cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat\" x64 >nul 2>&1 && cmake --build out --target tankoban_tests && out\tankoban_tests.exe --gtest_filter=MangaWeebCentralResolverFilter.*"
```

Expected: `[  PASSED  ] 10 tests.` Zero failures.

- [ ] **Step 5: Release the lease**

```bash
out\tankoctl.exe lease-release build --token <token-from-acquire>
```

(The token comes from step 1's acquire response.)

- [ ] **Step 6: Mark build verified**

If both builds GREEN and all 10 tests PASS, this task is done. Otherwise, fix the issue and rebuild before continuing.

---

### Task 13: Manual smoke (Hemanth-driven)

**Files:** none (smoke verification)

- [ ] **Step 1: Launch Tankoban with dev-control**

```bash
powershell -NoProfile -Command "Start-Process -FilePath 'out\Tankoban.exe' -ArgumentList '--dev-control' -WorkingDirectory 'out'"
```

Wait 5 seconds for the window to come up.

- [ ] **Step 2: Open a catalog-flagged series**

Pick a series that's likely to have WC coverage (e.g. Yu Yu Hakusho, Death Note, Berserk). The MangaFireCatalogClient lands the catalog JSON on-demand if it's not already present.

- [ ] **Step 3: Verify the WC row appears**

In ComicsSeriesView:
- Click Volume 1
- Sources panel header shows "Loading sources…" briefly
- Sources panel populates:
  - Nyaa rows (if any trusted-uploader torrent is indexed for this series)
  - "WeebCentral" row (assuming WC has full coverage of ch 1-5)
- The WC row title is literally "WeebCentral" — no "pack", no "compile"

Acceptable variations:
- Series with no WC coverage: WC row absent; only Nyaa rows visible (or empty state if no Nyaa either)
- Series with WC partial coverage: WC row absent (skip-if-incomplete)

- [ ] **Step 4: Verify dispatch + pack**

Click the WeebCentral row:
- Vol 1 tile state → "Downloading X%" with progress
- Pack completes
- Vol 1 tile flips to "Open"
- Click "Open" → opens the cbz in the comic reader

- [ ] **Step 5: Verify identity alignment (anchor decision 13)**

Re-open the series:
- Vol 1 tile shows "Open" instantly (state persisted via MangaDownloadIndex)
- This proves the registered seriesId matches the MangaFire slug, not `anilist_<id>`

- [ ] **Step 6: Verify generation-key guard**

In a fresh series:
- Click Vol 1
- Immediately click Vol 2 (before Vol 1's resolve finishes)
- Vol 2's WC row should appear
- Vol 1's late result should NOT append a duplicate row to Vol 2

- [ ] **Step 7: Verify JSON cache hit on second open**

Open the series, close it, re-open:
- The Sources panel "Loading sources…" phase should be shorter than the first time (WC seriesId now cached; only fetchDetail runs, not searchByTitle)
- Check the JSON: `data/mangafire_catalog/<slug>.json` should now have a top-level `weebCentral.seriesId` field

- [ ] **Step 8: Smoke complete — flag READY TO COMMIT in chat.md**

If all 7 steps pass, the wiring is verified end-to-end. Add a chat.md entry per gov-v7 contracts-v3:

```
## [TIMESTAMP] [Agent 1] READY TO COMMIT
[Files: src/core/manga/mangafire/MangaWeebCentralResolver.{h,cpp},
  src/core/manga/MangaCatalogTypes.h, src/core/manga/LocalMangaCatalogLoader.cpp,
  src/core/manga/mangafire/MangaFireCatalogClient.{h,cpp},
  src/ui/pages/comics/ComicsSeriesView.{h,cpp},
  src/ui/pages/comics/ComicsSourcesPanel.{h,cpp},
  src/core/manga/comics/UnifiedSourceRow.h,
  src/ui/pages/ComicsPage.{h,cpp},
  tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp,
  CMakeLists.txt
  -- COMICS_WC_VOLUME_WIRING Tasks 1-11 shipped; pure-logic 10/10 GREEN;
  smoke GREEN on Yu Yu Hakusho (Hemanth-verified end-to-end)]
Skills invoked: [superpowers:brainstorming, superpowers:writing-plans,
  superpowers:test-driven-development, superpowers:verification-before-completion]
```

---

## Self-Review

**1. Spec coverage:**

| Spec section | Implementing task |
|--------------|-------------------|
| Anchor decision 1 (Stremio-style row list) | Task 9 (ComicsSourcesPanel — already Stremio-style; just adds WC row driver) |
| Anchor decision 2 (name = "WeebCentral") | Task 9 step 4 |
| Anchor decision 3 (row visual = Stream parity) | Task 9 step 4 (uploaderHint empty; matches StreamSourceCard sparse-aux pattern) |
| Anchor decision 4 (1-to-1 chapter matching) | Task 6 step 1 (filterChaptersToRange impl) |
| Anchor decision 5 (skip-if-incomplete) | Task 5 + 6 (test case + impl) |
| Anchor decision 6 (lazy WC seriesId resolve) | Task 7 step 3 (resolve fires only when triggered, not pre-fetch) |
| Anchor decision 7 ("Loading sources…" UX) | Task 9 (existing setSources(nyaaInFlight=true) state) |
| Anchor decision 8 (no wrong-match recovery) | Out of scope; no task needed |
| Anchor decision 9 (already-downloaded Stream parity) | Existing infrastructure — MangaDownloadIndex flips tile state via volumeCompleted signal already wired |
| Anchor decision 10 (canonical dir) | Task 11 step 2 (req.destinationPath = canonicalSeriesDestinationDir) |
| Anchor decision 11 (private scraper) | Task 7 step 1 (m_scraper = new WeebCentralScraper(nam, this)) |
| Anchor decision 12 (generation-key guard) | Task 10 (ComicsPage owns m_wcResolveSerial + m_currentWcResolveKey + drops stale viable/skip) |
| Anchor decision 13 (MangaFire seriesId, NOT anilist_<id>) | Task 11 step 2 |
| Anchor decision 14 (additive schema, no version bump) | Task 1 + Task 2 |
| Anchor decision 15 (atomic JSON patch) | Task 3 |

All 15 anchor decisions have a task. No gaps.

**2. Placeholder scan:** No "TBD", "TODO", "fill in details" present. The "Task 11 step 1" instruction to grep for existing dispatch is real reconnaissance (the dispatch exists from Phase 5 of TANKOYOMI_VOLUME_PIVOT) — the executor confirms the existing wiring and adjusts if needed. The chapter-id shape on UnifiedSourceRow is added explicitly in Task 9 step 5.

**3. Type consistency:**

- `ResolveKey` definition (Task 4) matches all usages (Task 7, Task 10) — `{ seriesId: QString, volumeNumber: int, requestSerial: quint64 }` ✓
- `viable` signal signature `(ResolveKey, QStringList)` — Task 4 declares, Task 7 emits, Task 10 connects ✓
- `skip` signal signature `(ResolveKey, QString reasonCode)` — Task 4 declares, Task 7 emits, Task 10 connects ✓
- `filterChaptersToRange(chapterIds, rangeStart, rangeEnd, outIncomplete)` — Task 4 declares, Task 5 tests (matches signature), Task 6 implements (matches signature), Task 7 calls (matches signature) ✓
- `WeebCentralCacheBlock { seriesId, chaptersFetchedAt, volumeChapterIds }` — Task 1 defines, Task 2 deserializes, Task 3 serializes via patch ✓
- `populateSourcesForVolume(int volumeNumber)` — Task 8 declares + implements, Task 8 step 4 calls ✓
- `appendWeebCentralRow(int volumeNumber, const QStringList& chapterIds)` — Task 9 declares + implements, Task 8 calls via `m_sourcesPanel->appendWeebCentralRow(...)` ✓
- `weebCentralChapterIds` field on UnifiedSourceRow — Task 9 step 5 adds, Task 11 step 2 reads ✓

No drift.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-24-comics-wc-volume-wiring.md`.

**Per Hemanth's earlier directive: this arc executes via Codex Trigger D (Agent 7), not Claude subagents.** The plan is structured to be self-contained for Codex's cloud-env execution. The Trigger D brief (next step) bundles this plan + the spec + the necessary key-file pointers, and Codex returns with the diff + commits + smoke verdict.

If Hemanth instead prefers Claude execution:

**1. Subagent-Driven** — fresh subagent per task, two-stage review per task (spec compliance + code quality), commit between each. Best for iterative review.

**2. Inline Execution** — execute tasks in this session via superpowers:executing-plans, batched with checkpoints. Faster wall-time; main context fills faster.

Default per Hemanth's earlier ask: draft the **Codex Trigger D** prompt from this plan.
