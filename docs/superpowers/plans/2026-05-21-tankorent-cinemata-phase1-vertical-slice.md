# TANKORENT_CINEMATA Phase 1 — Vertical Slice MVP

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline single-agent — single src/ file family per task, sequential discipline). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the minimum end-to-end Tankorent→Cinemata flow visible from the Theatre detail view: click `[Find sources for Season X]` → search fires with show identity baked in → top-ranked source auto-picks → download starts → on completion, that episode row shows ▶ Play.

**Architecture:** This is the spine of the full arc spec'd at `docs/superpowers/specs/2026-05-21-tankorent-cinemata-integration-design.md`. Phase 1 wires a vertical slice covering identity capture (via `TankorentSearchService` extension) + minimal source ranker (seeders + trust only — full formula deferred to Phase 2) + auto-pick handler + per-episode publish path verification + ShowView's `[Find sources]` button + episode-row state painter. **Out of Phase 1 scope:** season packs, filename inference for packs, full ranker formula, Downloads sidebar page, concurrency cap / Queued state, cascade-on-remove. Those land in Phases 2–5.

**Tech Stack:** Qt6 (QObject + signals/slots + QSqlDatabase), C++20, existing `TorrentRepository` SQLite + `StreamDownloadIndex` SQLite + `TankorentSearchService` (Phase 5 ship from this morning) + `TrustedUploaders` (Phase 8a ship), GoogleTest via FetchContent.

---

## File Structure

**New files:**
- `src/core/stream/SourceRanker.h` — pure-logic ranker class (header)
- `src/core/stream/SourceRanker.cpp` — ranker impl: `rank(QList<TorrentResult>) → ranked QList<TorrentResult>` + `pickTop(...) → std::optional<TorrentResult>` with confidence threshold
- `tests/core/stream/test_source_ranker.cpp` — 5 GoogleTest cases covering empty/single/multi/tied/below-threshold

**Modified files:**
- `src/core/TankorentSearchService.h` — extend `startSearch` to accept optional `CinemataIdentity{imdbId, season, episode}` struct + new signal `topResultPicked(handle, result)` for auto-pick
- `src/core/TankorentSearchService.cpp` — apply ranker to per-indexer results on `searchFinished`; emit `topResultPicked` when first indexer returns
- `src/ui/pages/ShowView.h` — add member `TankorentSearchService* m_searchService` + slot `onFindSourcesClicked` + state for current search handle
- `src/ui/pages/ShowView.cpp` — add `[⬇ Find sources for Season N]` button to season-picker row; wire click to fire search with identity; wire `topResultPicked` to call `m_torrentClient->addMagnetHeadless`; paint episode-row state by reading from `StreamDownloadIndex`
- `src/core/torrent/TorrentClient.cpp` — verify `addMagnetHeadless` writes `imdbId + season` into the repo row (not just into the magnet save path — into the actual columns)
- `CMakeLists.txt` — register `SourceRanker.{h,cpp}` in `SOURCES` + register test file in `tankoban_tests`

---

## Invariants to preserve

1. **No regression on existing Tankorent direct-search flow** — the existing `TankorentPage` Add button + StartSingleAddFlow paths continue working unchanged. This phase ADDS a new entry point; it doesn't modify the existing one.
2. **Identity must be baked in at search time, not download time** — `addMagnetHeadless(magnetUri, "videos", path)` is the existing entry; this phase extends it to accept `imdbId + season` and write them to the repo row's `imdb_id` + `season` columns at upsert time.
3. **`stream_downloads_index` is the single source of truth for "is this episode downloaded?"** — ShowView reads only from there + live `m_engine` status; never from `m_records` (deleted in P5.5).
4. **Auto-pick must respect the confidence threshold** — `SourceRanker::pickTop` returns `std::nullopt` when top score < 0.30; the UI must handle this gracefully (sources panel populates manually-pickable instead of auto-picking).

---

## Tasks

### Task 1: SourceRanker header + first failing test

**Files:**
- Create: `src/core/stream/SourceRanker.h`
- Create: `tests/core/stream/test_source_ranker.cpp`

- [ ] **Step 1: Write the header**

```cpp
// src/core/stream/SourceRanker.h
#pragma once

#include "core/TorrentResult.h"

#include <QList>
#include <QString>
#include <QSet>
#include <optional>

namespace tankoban::stream {

// Pure-logic ranker for Tankorent search results. Scores each result based on
// seeders + uploader trust + (Phase 2) quality + size sanity; returns the
// list re-sorted by descending score, and a pickTop convenience that returns
// nullopt when no result clears the confidence threshold.
//
// Phase 1 scope: seeders + trust only. Phase 2 adds quality + size + pack-
// bonus per the design spec.
class SourceRanker {
public:
    struct Scored {
        TorrentResult result;
        double score = 0.0;
    };

    // Confidence threshold below which pickTop returns nullopt (caller must
    // fall back to manual-pick mode).
    static constexpr double kConfidenceThreshold = 0.30;

    explicit SourceRanker(const QSet<QString>& trustedUploaders);

    // Returns the input list re-sorted descending by score. Original list
    // not mutated.
    QList<Scored> rank(const QList<TorrentResult>& results) const;

    // Returns the highest-scored result if its score ≥ kConfidenceThreshold,
    // otherwise nullopt.
    std::optional<TorrentResult> pickTop(const QList<TorrentResult>& results) const;

private:
    double scoreOne(const TorrentResult& r) const;

    QSet<QString> m_trustedUploaders;
};

} // namespace tankoban::stream
```

- [ ] **Step 2: Write the first failing test**

```cpp
// tests/core/stream/test_source_ranker.cpp
#include "core/stream/SourceRanker.h"
#include "core/TorrentResult.h"

#include <gtest/gtest.h>

#include <QSet>
#include <QString>

using tankoban::stream::SourceRanker;

namespace {
TorrentResult makeResult(const QString& title, int seeders, const QString& source = "piratebay") {
    TorrentResult r;
    r.title = title;
    r.seeders = seeders;
    r.sourceKey = source;
    return r;
}
}

TEST(SourceRankerTest, EmptyInputReturnsNullopt) {
    SourceRanker ranker({});
    EXPECT_FALSE(ranker.pickTop({}).has_value());
}
```

- [ ] **Step 3: Add files to CMakeLists**

Edit `CMakeLists.txt`:
- Add `src/core/stream/SourceRanker.h` to the HEADERS list (alphabetical, after `src/core/stream/QualityScorer.h` or similar)
- Add `src/core/stream/SourceRanker.cpp` to SOURCES (after `QualityScorer.cpp`)
- Add `tests/core/stream/test_source_ranker.cpp` to `tankoban_tests` target sources

Run: `build_check.bat`
Expected: `BUILD FAILED` because `SourceRanker.cpp` doesn't exist yet (good — confirms registration is wired).

- [ ] **Step 4: Commit the failing test scaffold**

```bash
git add src/core/stream/SourceRanker.h tests/core/stream/test_source_ranker.cpp CMakeLists.txt
git commit -m "TANKORENT_CINEMATA P1.T1: SourceRanker.h + first failing test"
```

### Task 2: SourceRanker.cpp minimal implementation (pass first test)

**Files:**
- Create: `src/core/stream/SourceRanker.cpp`

- [ ] **Step 1: Write the minimal impl**

```cpp
// src/core/stream/SourceRanker.cpp
#include "core/stream/SourceRanker.h"

#include <QtMath>
#include <algorithm>

namespace tankoban::stream {

SourceRanker::SourceRanker(const QSet<QString>& trustedUploaders)
    : m_trustedUploaders(trustedUploaders)
{
}

double SourceRanker::scoreOne(const TorrentResult& r) const
{
    // Phase 1 formula (subset of spec): 0.65 × seeder_score + 0.35 × trust_score.
    // Full formula (quality + size) added in Phase 2.
    const double seederScore = r.seeders > 0
        ? std::min(1.0, std::log10(r.seeders + 1) / 3.0)
        : 0.0;

    // Trust score: 1.0 if uploader tag (from title suffix) is in trusted set,
    // 0.5 otherwise; -0.5 for empty-seeder zombie torrents.
    double trustScore = 0.5;
    for (const QString& uploader : m_trustedUploaders) {
        if (r.title.contains(uploader, Qt::CaseInsensitive)) {
            trustScore = 1.0;
            break;
        }
    }
    if (r.seeders == 0) trustScore = std::min(trustScore, 0.0);

    return 0.65 * seederScore + 0.35 * trustScore;
}

QList<SourceRanker::Scored> SourceRanker::rank(const QList<TorrentResult>& results) const
{
    QList<Scored> scored;
    scored.reserve(results.size());
    for (const auto& r : results) {
        scored.append({r, scoreOne(r)});
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.score > b.score;
    });
    return scored;
}

std::optional<TorrentResult> SourceRanker::pickTop(const QList<TorrentResult>& results) const
{
    const auto ranked = rank(results);
    if (ranked.isEmpty()) return std::nullopt;
    if (ranked.first().score < kConfidenceThreshold) return std::nullopt;
    return ranked.first().result;
}

} // namespace tankoban::stream
```

- [ ] **Step 2: Build + run the empty test**

Run via PowerShell:
```
cmd /c "<vcvars> && cmake --build out --config Release --target tankoban_tests"
out\tankoban_tests.exe --gtest_filter='SourceRankerTest.*'
```
Expected: 1/1 PASS (EmptyInputReturnsNullopt).

- [ ] **Step 3: Commit**

```bash
git add src/core/stream/SourceRanker.cpp
git commit -m "TANKORENT_CINEMATA P1.T2: SourceRanker.cpp minimal impl (seeders + trust)"
```

### Task 3: Four more SourceRanker test cases

**Files:**
- Modify: `tests/core/stream/test_source_ranker.cpp`

- [ ] **Step 1: Add four tests**

Append to the test file:

```cpp
TEST(SourceRankerTest, SingleHighSeederResultPicked) {
    SourceRanker ranker({"NTb", "Joy"});
    auto r = makeResult("Community.S05.NTb", 500);
    auto picked = ranker.pickTop({r});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->title, QString("Community.S05.NTb"));
}

TEST(SourceRankerTest, TrustedUploaderOutranksHigherSeederUntrusted) {
    SourceRanker ranker({"NTb"});
    auto untrusted = makeResult("Community.S05.UNKNOWN", 800);
    auto trusted = makeResult("Community.S05.NTb", 300);
    auto ranked = ranker.rank({untrusted, trusted});
    EXPECT_EQ(ranked.at(0).result.title, QString("Community.S05.NTb"));
}

TEST(SourceRankerTest, ZombieZeroSeederBelowThreshold) {
    SourceRanker ranker({"NTb"});
    auto zombie = makeResult("Community.S05.NTb", 0);
    EXPECT_FALSE(ranker.pickTop({zombie}).has_value());
}

TEST(SourceRankerTest, RankPreservesOriginalListImmutable) {
    SourceRanker ranker({});
    auto r1 = makeResult("a", 10);
    auto r2 = makeResult("b", 100);
    QList<TorrentResult> input = {r1, r2};
    auto ranked = ranker.rank(input);
    EXPECT_EQ(input.at(0).title, QString("a"));  // original list unchanged
    EXPECT_EQ(input.at(1).title, QString("b"));
    EXPECT_EQ(ranked.at(0).result.title, QString("b"));  // higher seeders first
}
```

- [ ] **Step 2: Build + run**

Run: `out\tankoban_tests.exe --gtest_filter='SourceRankerTest.*'`
Expected: 5/5 PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/core/stream/test_source_ranker.cpp
git commit -m "TANKORENT_CINEMATA P1.T3: 4 more SourceRanker test cases"
```

### Task 4: Extend TankorentSearchService with CinemataIdentity

**Files:**
- Modify: `src/core/TankorentSearchService.h`
- Modify: `src/core/TankorentSearchService.cpp`

- [ ] **Step 1: Add CinemataIdentity struct + extend startSearch signature in header**

In `TankorentSearchService.h`, add inside the `public:` section (above `startSearch`):

```cpp
struct CinemataIdentity {
    QString imdbId;   // empty if no identity (legacy direct-search path)
    int     season = 0;  // 0 for movies
    int     episode = 0; // 0 = whole-season search, else specific episode
};
```

Replace existing `startSearch` decl with:

```cpp
QString startSearch(const QString& mediaType,
                    const QString& sourceFilter,
                    const QString& query,
                    int limit,
                    const QString& categoryId = {},
                    const CinemataIdentity& identity = {});
```

Add a getter for the identity tied to a handle (page-side state needs it for the addMagnetHeadless call):

```cpp
CinemataIdentity identityFor(const QString& handle) const;
```

Add to `private:` SearchContext struct:

```cpp
struct SearchContext {
    QList<TorrentIndexer*> activeIndexers;
    int pendingCount = 0;
    CinemataIdentity identity;  // bake-time identity for this search
};
```

- [ ] **Step 2: Update .cpp impl**

In `TankorentSearchService.cpp`, change `startSearch` to capture identity:

```cpp
QString TankorentSearchService::startSearch(const QString& mediaType,
                                            const QString& sourceFilter,
                                            const QString& query,
                                            int limit,
                                            const QString& categoryId,
                                            const CinemataIdentity& identity)
{
    QList<TorrentIndexer*> indexers = buildIndexersFor(mediaType, sourceFilter);
    if (indexers.isEmpty())
        return {};

    const QString handle = QStringLiteral("search-%1").arg(++m_handleSeq);
    SearchContext ctx;
    ctx.activeIndexers = indexers;
    ctx.pendingCount = indexers.size();
    ctx.identity = identity;
    m_contexts.insert(handle, ctx);

    /* … rest unchanged … */
}
```

Add the identityFor impl:

```cpp
TankorentSearchService::CinemataIdentity
TankorentSearchService::identityFor(const QString& handle) const
{
    auto it = m_contexts.find(handle);
    if (it == m_contexts.end()) return {};
    return it.value().identity;
}
```

- [ ] **Step 3: Build + run existing tests to verify no regression**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `out\tankoban_tests.exe --gtest_filter='TankorentSearchServiceTest.*'`
Expected: 6/6 PASS (existing tests use the default `CinemataIdentity{}` param).

- [ ] **Step 4: Commit**

```bash
git add src/core/TankorentSearchService.h src/core/TankorentSearchService.cpp
git commit -m "TANKORENT_CINEMATA P1.T4: TankorentSearchService accepts CinemataIdentity"
```

### Task 5: Add topResultPicked signal + auto-pick wiring

**Files:**
- Modify: `src/core/TankorentSearchService.h`
- Modify: `src/core/TankorentSearchService.cpp`

- [ ] **Step 1: Add signal + ranker member**

In `TankorentSearchService.h`, add to `signals:` block:

```cpp
// Emitted exactly once per handle when the first indexer returns AND its
// top-ranked result clears the confidence threshold. UI consumes this for
// auto-pick. Subsequent indexer completions still emit resultsReady for
// the [Pick different source] expansion list.
void topResultPicked(const QString& handle, const TorrentResult& result);
```

Add private member:

```cpp
// Set via setRanker(); ownership stays with caller (MainWindow / page).
// nullptr = no auto-pick (legacy direct-search path).
const tankoban::stream::SourceRanker* m_ranker = nullptr;
```

Add setter:

```cpp
void setRanker(const tankoban::stream::SourceRanker* ranker) { m_ranker = ranker; }
```

Forward-decl `SourceRanker` at top of header.

- [ ] **Step 2: Wire ranker into the per-indexer searchFinished lambda**

In `TankorentSearchService.cpp` `startSearch`, modify the connect lambda to also try auto-pick:

```cpp
connect(idx, &TorrentIndexer::searchFinished, this,
        [this, handle, indexerId](const QList<TorrentResult>& results) {
    if (!m_contexts.contains(handle)) return;

    emit resultsReady(handle, results);

    // Auto-pick on the first non-empty result set if ranker is wired AND
    // the search has CinemataIdentity (legacy searches don't auto-pick).
    SearchContext& ctx = m_contexts[handle];
    if (m_ranker && !ctx.identity.imdbId.isEmpty() && !ctx.autoPicked && !results.isEmpty()) {
        if (auto top = m_ranker->pickTop(results)) {
            ctx.autoPicked = true;
            emit topResultPicked(handle, *top);
        }
    }

    settleOne(handle);
});
```

Add `bool autoPicked = false;` to `SearchContext` struct.

- [ ] **Step 3: Build + run existing tests**

Run: `build_check.bat` → `BUILD OK`.
Run: `out\tankoban_tests.exe --gtest_filter='TankorentSearchServiceTest.*'`
Expected: 6/6 PASS (no ranker injected = no behavior change).

- [ ] **Step 4: Commit**

```bash
git add src/core/TankorentSearchService.h src/core/TankorentSearchService.cpp
git commit -m "TANKORENT_CINEMATA P1.T5: TankorentSearchService topResultPicked + auto-pick wiring"
```

### Task 6: Test the auto-pick path

**Files:**
- Modify: `tests/core/test_tankorent_search_service.cpp`

- [ ] **Step 1: Add an auto-pick test**

Add at end of file:

```cpp
TEST(TankorentSearchServiceTest, AutoPickFiresWhenRankerInjectedAndIdentityNonEmpty)
{
    TestableSearchService svc;
    auto* mock = new MockTorrentIndexer("piratebay");
    svc.setMockIndexers({mock});

    tankoban::stream::SourceRanker ranker({QStringLiteral("NTb")});
    svc.setRanker(&ranker);

    QSignalSpy topPickedSpy(&svc, &TankorentSearchService::topResultPicked);

    TankorentSearchService::CinemataIdentity id;
    id.imdbId = "tt1439629";
    id.season = 5;

    const QString handle = svc.startSearch("videos", "all", "Community S5", 30, {}, id);
    ASSERT_FALSE(handle.isEmpty());

    TorrentResult r;
    r.title = "Community.S05E03.1080p.NTb";
    r.magnetUri = "magnet:?xt=urn:btih:abc";
    r.seeders = 500;
    mock->triggerFinished({r});

    ASSERT_EQ(topPickedSpy.count(), 1);
    EXPECT_EQ(topPickedSpy.first().at(0).toString(), handle);
    auto picked = topPickedSpy.first().at(1).value<TorrentResult>();
    EXPECT_EQ(picked.title, QString("Community.S05E03.1080p.NTb"));
}

TEST(TankorentSearchServiceTest, NoAutoPickWhenIdentityEmpty)
{
    TestableSearchService svc;
    auto* mock = new MockTorrentIndexer("piratebay");
    svc.setMockIndexers({mock});

    tankoban::stream::SourceRanker ranker({QStringLiteral("NTb")});
    svc.setRanker(&ranker);

    QSignalSpy topPickedSpy(&svc, &TankorentSearchService::topResultPicked);

    // No identity passed (legacy direct-search shape)
    const QString handle = svc.startSearch("videos", "all", "Community", 30);
    TorrentResult r;
    r.title = "Community.S05E03.NTb";
    r.seeders = 500;
    mock->triggerFinished({r});

    EXPECT_EQ(topPickedSpy.count(), 0);  // legacy path doesn't auto-pick
}
```

Add `#include "core/stream/SourceRanker.h"` to the test file.

- [ ] **Step 2: Build + run**

Run: `cmake --build out --target tankoban_tests` then `out\tankoban_tests.exe --gtest_filter='TankorentSearchServiceTest.*'`
Expected: 8/8 PASS (6 existing + 2 new).

- [ ] **Step 3: Commit**

```bash
git add tests/core/test_tankorent_search_service.cpp
git commit -m "TANKORENT_CINEMATA P1.T6: TankorentSearchService auto-pick tests (8/8 green)"
```

### Task 7: Verify addMagnetHeadless writes imdbId + season to repo row

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp` (only if existing impl drops the fields)
- Modify: `src/core/torrent/TorrentClient.h` (extend `addMagnetHeadless` if needed)

- [ ] **Step 1: Inspect current addMagnetHeadless signature**

Run: `grep -nA 5 "QString TorrentClient::addMagnetHeadless" src/core/torrent/TorrentClient.cpp`

Current signature accepts `magnetUri + category + destinationPath`. Identity (imdbId + season) is NOT in the current parameter list.

- [ ] **Step 2: Extend the signature to accept identity**

In `TorrentClient.h`, replace:

```cpp
QString addMagnetHeadless(const QString& magnetUri,
                          const QString& category   = QString(),
                          const QString& destinationPath = QString());
```

with:

```cpp
QString addMagnetHeadless(const QString& magnetUri,
                          const QString& category   = QString(),
                          const QString& destinationPath = QString(),
                          const QString& imdbId = QString(),
                          int season = 0);
```

In `TorrentClient.cpp`, update the impl to write imdbId + season into the `AddTorrentConfig` before `startDownload`:

```cpp
QString TorrentClient::addMagnetHeadless(const QString& magnetUri,
                                          const QString& category,
                                          const QString& destinationPath,
                                          const QString& imdbId,
                                          int season)
{
    /* … existing resolveMetadata + dedup check + AddTorrentConfig construction … */
    AddTorrentConfig config;
    config.category = category.isEmpty() ? defaultCategory() : category;
    config.destinationPath = destinationPath.isEmpty()
        ? defaultPaths().value(config.category)
        : destinationPath;
    config.imdbId = imdbId;
    config.season = season;
    /* … rest unchanged … */
}
```

- [ ] **Step 3: Build + run existing tests**

Run: `build_check.bat` → `BUILD OK`.
Run: `out\tankoban_tests.exe` (full suite)
Expected: all-green (existing callers pass default empty imdbId / season=0 → no behavior change).

- [ ] **Step 4: Commit**

```bash
git add src/core/torrent/TorrentClient.h src/core/torrent/TorrentClient.cpp
git commit -m "TANKORENT_CINEMATA P1.T7: addMagnetHeadless accepts identity (imdbId + season)"
```

### Task 8: ShowView — add [Find sources for Season N] button + click handler

**Files:**
- Modify: `src/ui/pages/ShowView.h`
- Modify: `src/ui/pages/ShowView.cpp`

- [ ] **Step 1: Add header members**

In `ShowView.h`, add `#include` for `TankorentSearchService.h`. Add to private members:

```cpp
TankorentSearchService* m_searchService = nullptr;  // injected via setSearchService
QString m_currentSearchHandle;  // tracks the in-flight Find sources request

QPushButton* m_findSourcesBtn = nullptr;
QPushButton* m_findPackBtn = nullptr;  // ▤ icon — Phase 2 wiring
```

Add slots:

```cpp
private slots:
    void onFindSourcesClicked();
    void onSearchTopPicked(const QString& handle, const TorrentResult& result);
    void onSearchFinished(const QString& handle);  // for status text
```

Add setter:

```cpp
void setSearchService(TankorentSearchService* svc);
```

- [ ] **Step 2: Implement the setter + click handler**

In `ShowView.cpp`:

```cpp
void ShowView::setSearchService(TankorentSearchService* svc)
{
    m_searchService = svc;
    connect(m_searchService, &TankorentSearchService::topResultPicked,
            this, &ShowView::onSearchTopPicked);
    connect(m_searchService, &TankorentSearchService::searchFinished,
            this, &ShowView::onSearchFinished);
}

void ShowView::onFindSourcesClicked()
{
    if (!m_searchService || m_imdbId.isEmpty()) return;

    TankorentSearchService::CinemataIdentity id;
    id.imdbId = m_imdbId;
    id.season = m_currentSeason;
    id.episode = 0;  // season-level search

    const QString query = QStringLiteral("%1 S%2")
        .arg(m_showName)
        .arg(m_currentSeason, 2, 10, QChar('0'));

    m_currentSearchHandle = m_searchService->startSearch(
        QStringLiteral("videos"),
        QStringLiteral("all"),
        query,
        50,
        QString(),  // categoryId
        id);

    m_findSourcesBtn->setText("Searching…");
    m_findSourcesBtn->setEnabled(false);
}
```

- [ ] **Step 3: Wire the top-picked auto-download**

```cpp
void ShowView::onSearchTopPicked(const QString& handle, const TorrentResult& result)
{
    if (handle != m_currentSearchHandle) return;
    if (!m_torrentClient) return;

    const QString destination = m_torrentClient->defaultPaths().value("videos");
    m_torrentClient->addMagnetHeadless(
        result.magnetUri,
        QStringLiteral("videos"),
        destination,
        m_imdbId,
        m_currentSeason);
}

void ShowView::onSearchFinished(const QString& handle)
{
    if (handle != m_currentSearchHandle) return;
    m_currentSearchHandle.clear();
    m_findSourcesBtn->setText("⬇ Find sources for Season " + QString::number(m_currentSeason));
    m_findSourcesBtn->setEnabled(true);
}
```

- [ ] **Step 4: Construct the button in UI build path**

Find the season-picker row build code (search for where `m_seasonCombo` is added to a layout). Add right after:

```cpp
m_findSourcesBtn = new QPushButton(
    QStringLiteral("⬇ Find sources for Season %1").arg(m_currentSeason));
m_findSourcesBtn->setObjectName("ShowViewFindSourcesBtn");
m_findSourcesBtn->setStyleSheet(
    "QPushButton#ShowViewFindSourcesBtn {"
    "  background: #a78bfa; color: black; font-weight: 600;"
    "  padding: 7px 14px; font-size: 12px; border: none; border-radius: 4px;"
    "}"
    "QPushButton#ShowViewFindSourcesBtn:hover { background: #b89cff; }"
    "QPushButton#ShowViewFindSourcesBtn:disabled { background: #4a3b6a; color: #888; }");
seasonPickerRowLayout->addStretch();
seasonPickerRowLayout->addWidget(m_findSourcesBtn);
connect(m_findSourcesBtn, &QPushButton::clicked, this, &ShowView::onFindSourcesClicked);
```

(Adjust `seasonPickerRowLayout` to whatever the actual layout var is named.)

- [ ] **Step 5: Build + verify no regression**

Run: `build_check.bat` → `BUILD OK`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/ShowView.h src/ui/pages/ShowView.cpp
git commit -m "TANKORENT_CINEMATA P1.T8: ShowView [Find sources for Season N] button + auto-pick wire"
```

### Task 9: Wire SourceRanker + TankorentSearchService in MainWindow

**Files:**
- Modify: `src/ui/MainWindow.cpp` (or wherever Theatre/ShowView are constructed)

- [ ] **Step 1: Locate construction point**

Run: `grep -n "new ShowView\|new TankorentSearchService" src/ui/MainWindow.cpp src/ui/pages/*.cpp | head -10`

Find where ShowView is instantiated. Identify the existing `TankorentSearchService` instance (created during the TankorentPage extraction earlier this wake).

- [ ] **Step 2: Inject ranker into service + service into ShowView**

In the construction code (likely MainWindow ctor or a page-factory):

```cpp
// Inject the SourceRanker into the TankorentSearchService.
// The ranker is a long-lived singleton owned by MainWindow; service stores a
// non-owning pointer.
static const QSet<QString> kVideoTrustedUploaders = {
    QStringLiteral("NTb"),
    QStringLiteral("Joy"),
    QStringLiteral("ELiTE"),
    QStringLiteral("RARBG"),
    QStringLiteral("PSA"),
};
m_videoSourceRanker = new tankoban::stream::SourceRanker(kVideoTrustedUploaders);
m_tankorentSearchService->setRanker(m_videoSourceRanker);

// Inject the service into each ShowView (whenever a Theatre detail view opens).
showView->setSearchService(m_tankorentSearchService);
```

Add `tankoban::stream::SourceRanker* m_videoSourceRanker = nullptr;` to MainWindow.h.

- [ ] **Step 3: Build**

Run: `build_check.bat` → `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "TANKORENT_CINEMATA P1.T9: MainWindow wires SourceRanker + service into ShowView"
```

### Task 10: ShowView episode-row state painter (read StreamDownloadIndex)

**Files:**
- Modify: `src/ui/pages/ShowView.cpp`

- [ ] **Step 1: Locate episode-row paint code**

Run: `grep -n "populateEpisodes\|episodeRow\|m_episodesTable" src/ui/pages/ShowView.cpp | head -10`

Find the function that builds episode table rows.

- [ ] **Step 2: Add state-paint helper**

```cpp
ShowView::EpisodeRowState ShowView::resolveRowState(int episode) const
{
    if (!m_streamDownloadIndex) return EpisodeRowState::NotDownloaded;

    auto entry = m_streamDownloadIndex->lookupByImdbSeasonEpisode(
        m_imdbId, m_currentSeason, episode);

    if (!entry) return EpisodeRowState::NotDownloaded;

    if (entry->state == QStringLiteral("complete")) {
        return EpisodeRowState::Downloaded;
    }
    if (entry->state == QStringLiteral("downloading")) {
        return EpisodeRowState::Downloading;
    }
    return EpisodeRowState::NotDownloaded;
}
```

Add to `ShowView.h`:

```cpp
enum class EpisodeRowState { NotDownloaded, Downloading, Downloaded };
EpisodeRowState resolveRowState(int episode) const;
StreamDownloadIndex* m_streamDownloadIndex = nullptr;
void setStreamDownloadIndex(StreamDownloadIndex* idx) { m_streamDownloadIndex = idx; }
```

In MainWindow wire-up (Task 9), pass `m_streamDownloadIndex` to `showView->setStreamDownloadIndex(...)`.

- [ ] **Step 3: Apply the per-row state in the populate loop**

In the populateEpisodes loop, replace static "-" / "-" Progress + Status cells with state-conditional action button:

```cpp
const EpisodeRowState state = resolveRowState(episode.number);
QPushButton* actionBtn = nullptr;
if (state == EpisodeRowState::Downloaded) {
    actionBtn = new QPushButton("▶ Play");
    actionBtn->setStyleSheet("background:#5cb874; color:black; font-weight:600; padding:5px 12px; border:none; border-radius:4px;");
    connect(actionBtn, &QPushButton::clicked, this, [this, ep=episode.number]() { playEpisode(ep); });
} else if (state == EpisodeRowState::Downloading) {
    actionBtn = new QPushButton("✕ Cancel");
    actionBtn->setStyleSheet("background:#1a1a1a; color:#cfcfcf; padding:5px 8px; border:1px solid #2a2a2a; border-radius:4px;");
    connect(actionBtn, &QPushButton::clicked, this, [this, ep=episode.number]() { cancelEpisodeDownload(ep); });
    row->setStyleSheet(row->styleSheet() + "QFrame { opacity: 1.0; }");  // not dimmed
} else {
    actionBtn = new QPushButton("⬇ Download");
    actionBtn->setStyleSheet("background:#1a1a1a; color:#7a7a7a; padding:5px 12px; border:1px solid #2a2a2a; border-radius:4px;");
    connect(actionBtn, &QPushButton::clicked, this, [this, ep=episode.number]() { findSourcesForEpisode(ep); });
    row->setStyleSheet(row->styleSheet() + "QFrame { opacity: 0.55; }");  // dimmed
}
m_episodesTable->setCellWidget(rowIdx, actionColumn, actionBtn);
```

(`playEpisode` / `cancelEpisodeDownload` / `findSourcesForEpisode` are existing or stub methods; the latter two will be implemented in P1.T11+P1.T12.)

- [ ] **Step 4: Build**

Run: `build_check.bat` → `BUILD OK` (`playEpisode` etc. may stub-fail — declare them as no-op slots first).

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/ShowView.h src/ui/pages/ShowView.cpp
git commit -m "TANKORENT_CINEMATA P1.T10: ShowView episode-row state painter (StreamDownloadIndex read)"
```

### Task 11: Connect torrentCompleted signal → ShowView repaint

**Files:**
- Modify: `src/ui/pages/ShowView.cpp`

- [ ] **Step 1: Add the connect in setSearchService (or a sibling injection)**

Extend `ShowView::setTorrentClient` (or `setStreamDownloadIndex`) to also connect:

```cpp
connect(m_torrentClient, &TorrentClient::torrentCompleted,
        this, [this](const QString& /*infoHash*/) {
    // When ANY torrent completes, re-paint episodes — the publish path may have
    // just written new stream_downloads_index entries that affect this show.
    populateEpisodes();
});
```

Also connect `torrentUpdated` to repaint during in-flight progress (throttle if churn becomes an issue; not in Phase 1 scope).

- [ ] **Step 2: Build**

Run: `build_check.bat` → `BUILD OK`.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/ShowView.cpp
git commit -m "TANKORENT_CINEMATA P1.T11: ShowView repaints episodes on torrentCompleted"
```

### Task 12: Live smoke under BUILD LANE

**Files:** None (verification only)

- [ ] **Step 1: Claim BUILD LANE in chat.md**

Post:
```
## BUILD LANE — Agent 4 — TANKORENT_CINEMATA P1 vertical-slice smoke
Claimed 2026-05-21 ~<HH:MM>pm IST. Launching Tankoban; will test Find sources for Community S5 → auto-pick → download (Hemanth may need to leave running ~30 min to complete) → episode row paints ▶ Play. Expect ~10 min for setup + auto-pick verification.
```

- [ ] **Step 2: Launch Tankoban**

Run: `build_and_run.bat`.

- [ ] **Step 3: Add Community to library + open detail view**

Via Theatre tab → search "Community" → add to library → click into detail view.

- [ ] **Step 4: Verify button + click**

Verify the purple `[⬇ Find sources for Season 5]` button is present in the season-picker row.
Click it.
Verify the button text changes to "Searching…" and is disabled.

- [ ] **Step 5: Verify auto-pick fires within ~30s**

Tail `out/events.jsonl` or inspect tankoctl get-torrents:

```
out\tankoctl.exe get-torrents --active
```

Expected: one row with `imdb_id = tt1439629` + `season = 5`. This proves the identity capture path works.

- [ ] **Step 6: Verify episode rows repaint as in-flight (or completed if fast)**

Hemanth in-the-loop step: confirm the detail view's episode 1-N rows now show ✕ Cancel buttons with mini-progress on the thumbnail OR ▶ Play if download completed.

- [ ] **Step 7: Stop Tankoban + release BUILD LANE**

```
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

Post chat.md release + close-out RTC documenting the Phase 1 ship.

---

## Self-Review Checklist

1. ✅ **Spec coverage (Phase 1 subset):** Identity capture (T4, T7, T8) / minimal ranker (T1-T3) / auto-pick (T5, T6, T9) / episode-row state (T10, T11) / live smoke (T12). All Phase 1 spec requirements mapped to tasks.
2. ✅ **No placeholders:** Each task has concrete code blocks. References to `playEpisode` / `cancelEpisodeDownload` / `findSourcesForEpisode` in T10 are explicit stubs to be implemented in later phases (P2+) — flagged in the task body.
3. ✅ **Type consistency:** `CinemataIdentity { imdbId, season, episode }` used consistently across T4/T5/T6/T8/T9. `SourceRanker::pickTop` returns `std::optional<TorrentResult>` across header + impl + tests.

## Risks (Phase 1)

- **No regression on existing TankorentPage direct-search** — preserved by making the identity param optional (default `{}`). Existing callers don't pass it; the new ShowView callsite does.
- **`m_currentSeason` + `m_imdbId` may not be set in all ShowView code paths** — T8 has an early-return `if (m_imdbId.isEmpty()) return;` guard. If the season picker hasn't been wired to track the current season yet (Phase 0 of ShowView), T8 may need a small precursor task to add that state. Verify at execution time.
- **`StreamDownloadIndex::lookupByImdbSeasonEpisode` may not exist yet** — Phase 1's T10 assumes this method. If it doesn't, add a small precursor task before T10 to implement it (~10 LOC + 2 tests).
- **`MainWindow` wiring (T9) may differ in layout from this plan's assumption** — read MainWindow.cpp at execution time + adapt the injection points to actual structure.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-21-tankorent-cinemata-phase1-vertical-slice.md`.

REQUIRED SUB-SKILL on execution: `superpowers:executing-plans`. Inline single-agent (multi-file but sequential; the ShowView edits in T8-T11 are entangled and want serial discipline like the persistence-collapse P5 work).
