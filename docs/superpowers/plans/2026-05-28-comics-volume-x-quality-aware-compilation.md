# Comics Volume X / Quality-Aware Volume Compilation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Comics volumes quality-aware — clean English-volume scans read full-quality and untagged, magazine-quality chapters group into "RAW SCAN"-tagged volumes (chapter-paired), and the bleeding-edge tail lands in a single Volume X.

**Architecture:** Compose two signals — MangaFire catalog volume→chapter boundaries (existing) × WeebCentral per-chapter violet/gray quality tick (new). A pure-logic classifier turns those into per-volume verdicts (Clean / Magazine / Volume X). Magazine + Volume X compilations get the `.volx` sidecar so the already-built reader chapter-pairing engine fires; clean volumes stitch as today.

**Tech Stack:** C++17 / Qt6, GoogleTest (`tankoban_tests` target), libarchive/QZip (cbz), existing `WeebCentralScraper` / `WeebCentralVolumePacker` / `ComicsSeriesView` / `VolumeTile` / `ComicReader`.

**Spec:** `docs/superpowers/specs/2026-05-28-comics-volume-x-quality-aware-compilation-design.md`

---

## Brotherhood execution notes

- **No worktree** — flat-on-master per `feedback_no_worktrees`.
- **Build-verify:** `build_check.bat` on an isolated lane (`$env:TANKOBAN_BUILD_LANE="agent1"`). Link step ~915s; batch edits per task before verifying.
- **Pairing/test runs:** the `tankoban_tests` target needs a tests-on configure. Run via: `cmake -B out_agent1 -DTANKOBAN_BUILD_TESTS=ON` then `cmake --build out_agent1 --config Release --target tankoban_tests`, then run `out_agent1\tankoban_tests.exe --gtest_filter=<Suite>.*` with Qt's bin on PATH (`C:\tools\qt6sdk\6.10.2\msvc2022_64\bin`). (CMake's auto test-discovery fails without Qt DLLs on PATH — run the exe directly.)
- **Commits:** per Rule 11, do NOT `git commit` directly. At each phase boundary, post a `READY TO COMMIT - [Agent 1, <TAG>]: ...` line to `agents/chat.md` (with `Skills invoked:` provenance) for Agent 0's sweep.
- **`taskkill //F //IM Tankoban.exe`** before any rebuild (Rule 1).

---

## File map

| File | Responsibility | Action |
|------|----------------|--------|
| `src/core/manga/MangaResult.h` | `ChapterInfo` POD | Modify — add `bool isVolumeScanned` |
| `src/core/manga/WeebCentralScraper.cpp` | chapter-list HTML parse | Modify — detect tick before stripping `<svg>` |
| `tests/core/manga/test_weebcentral_chapter_quality.cpp` | scraper tick-parse tests | Create |
| `src/core/manga/VolumeQualityClassifier.h/.cpp` | pure-logic Clean/Magazine/Volume X classifier | Create |
| `tests/core/manga/test_volume_quality_classifier.cpp` | classifier tests | Create |
| `src/core/manga/WeebCentralVolumePacker.h/.cpp` | `.volx` write trigger | Modify — broaden to `needsChapterPairing` flag |
| `src/ui/pages/comics/VolumeTile.h/.cpp` | volume row widget | Modify — "RAW SCAN" tag |
| `src/ui/pages/comics/ComicsSeriesView.cpp` | volume rows + classification wiring + Volume X row + upgrade offer | Modify |
| `src/ui/pages/ComicsPage.cpp` | dispatch sets `needsChapterPairing` | Modify |
| `CMakeLists.txt` | wire new sources + test files | Modify |

---

## Task 1: WeebCentral tick markup discovery

**Files:**
- Create: `agents/audits/weebcentral_volume_tick_markup_2026-05-28.md` (findings doc)
- Create: `tests/fixtures/weebcentral/one_piece_chapters_sample.html` (real HTML sample)

- [ ] **Step 1: Capture a real WeebCentral chapter-list HTML sample**

The series-page chapter list is fetched by `WeebCentralScraper::fetchChapters`. Capture the raw HTML for One Piece (`weebcentral.com/series/01J76XY7E9FNDZ1DBBM6PBJPFK/One-Piece`) — use the running app's existing fetch path with a temporary debug dump, OR a one-off authenticated fetch matching the scraper's headers (`User-Agent: Mozilla/5.0 … Chrome/134`, `Referer: https://weebcentral.com/`). Save the chapter-list section to the fixture path.

- [ ] **Step 2: Identify the violet vs gray discriminator**

Inspect the per-chapter `<svg>` (or wrapping element) inside each `<a href="/chapters/…">` block. The violet (volume-scanned) chapters differ from gray ones by exactly one of: an SVG `fill`/`stroke` color value, a CSS class on the tick element, or a `data-*` attribute. Record the precise discriminator (e.g. `fill="#a78bfa"` vs `fill="#…gray"`, or `class="… text-violet-…"`) in the findings doc. **This token is the input to Task 2.**

- [ ] **Step 3: Confirm boundary against known data**

Cross-check: the violet/gray boundary in the sample should sit at the last volume-scanned chapter (per Hemanth's screenshot, ~ch 1133 for One Piece as of 2026-02). Record the observed boundary chapter in the findings doc as a sanity anchor for Task 2 tests.

---

## Task 2: ChapterInfo carries volume-scan quality

**Files:**
- Modify: `src/core/manga/MangaResult.h:18-26`
- Modify: `src/core/manga/WeebCentralScraper.cpp:215-302` (`parseChaptersHtml`)
- Create: `tests/core/manga/test_weebcentral_chapter_quality.cpp`
- Modify: `CMakeLists.txt` (add test file under `TANKOBAN_BUILD_TESTS`)

- [ ] **Step 1: Add the field to ChapterInfo**

In `MangaResult.h`, inside `struct ChapterInfo`:

```cpp
struct ChapterInfo {
    QString id;
    QString url;
    QString name;
    double  chapterNumber = 0.0;
    qint64  dateUpload    = 0;   // ms epoch
    QString source;
    bool    isVolumeScanned = false;  // WeebCentral violet tick: chapter is from a volume scan
};
```

- [ ] **Step 2: Write the failing scraper test**

Use a minimal two-chapter HTML snippet matching the discriminator found in Task 1. Replace `VIOLET_TOKEN` / `GRAY_TOKEN` with the exact tokens from the Task 1 findings doc.

```cpp
// tests/core/manga/test_weebcentral_chapter_quality.cpp
#include <gtest/gtest.h>
#include "core/manga/WeebCentralScraper.h"
#include "core/manga/MangaResult.h"

// parseChaptersHtml is private static; expose via a friend or a thin test shim.
// This plan adds a public static `parseChaptersHtmlForTest` delegating to it.
TEST(WeebCentralChapterQuality, VioletTickMarksVolumeScanned)
{
    const QString html = R"HTML(
      <a href="/chapters/AAA"><span>Chapter 1133</span><svg VIOLET_TOKEN></svg></a>
      <a href="/chapters/BBB"><span>Chapter 1134</span><svg GRAY_TOKEN></svg></a>
    )HTML";
    const auto chapters = WeebCentralScraper::parseChaptersHtmlForTest(html, "weebcentral");
    ASSERT_EQ(chapters.size(), 2);
    // chapters sort ascending by number → [1133, 1134]
    EXPECT_DOUBLE_EQ(chapters[0].chapterNumber, 1133.0);
    EXPECT_TRUE(chapters[0].isVolumeScanned);
    EXPECT_DOUBLE_EQ(chapters[1].chapterNumber, 1134.0);
    EXPECT_FALSE(chapters[1].isVolumeScanned);
}
```

- [ ] **Step 3: Add the test shim + parse the tick before stripping `<svg>`**

In `WeebCentralScraper.h`, add a public delegator so the test can reach the private parser:

```cpp
public:
    static QList<ChapterInfo> parseChaptersHtmlForTest(const QString& html, const QString& source)
    { return parseChaptersHtml(html, source); }
```

In `WeebCentralScraper.cpp` `parseChaptersHtml`, BEFORE step (2) `rawInner.remove(svgBlockRe)` (line ~282), detect the tick. Use the discriminator from Task 1 — example shown for a violet `fill` color; substitute the real token:

```cpp
        // VOLUME_X_QUALITY 2026-05-28 (Agent 1). The volume-scanned tick is a
        // violet SVG inside the chapter anchor; magazine chapters render it
        // gray. Detect BEFORE the <svg> block is stripped below. Discriminator
        // confirmed in agents/audits/weebcentral_volume_tick_markup_2026-05-28.md.
        static QRegularExpression violetTickRe(
            QStringLiteral(R"RX(VIOLET_TOKEN)RX"),
            QRegularExpression::CaseInsensitiveOption);
        ch.isVolumeScanned = violetTickRe.match(rawInner).hasMatch();
```

(Place this assignment after `ch.source = source;` and before the `<svg>` removal.)

- [ ] **Step 4: Wire the test file into CMake**

In `CMakeLists.txt` under the `if(TANKOBAN_BUILD_TESTS)` block, alongside the other `tests/core/manga/*` entries:

```cmake
        tests/core/manga/test_weebcentral_chapter_quality.cpp
        src/core/manga/WeebCentralScraper.cpp
```

(Add `WeebCentralScraper.cpp` to the test SOURCES only if not already present.)

- [ ] **Step 5: Build + run the test**

```
cmake -B out_agent1 -DTANKOBAN_BUILD_TESTS=ON
cmake --build out_agent1 --config Release --target tankoban_tests
out_agent1\tankoban_tests.exe --gtest_filter=WeebCentralChapterQuality.*
```
Expected: `[  PASSED  ] 1 test.` (Qt bin on PATH per the execution notes.)

---

## Task 3: VolumeQualityClassifier (pure logic)

**Files:**
- Create: `src/core/manga/VolumeQualityClassifier.h`
- Create: `src/core/manga/VolumeQualityClassifier.cpp`
- Create: `tests/core/manga/test_volume_quality_classifier.cpp`
- Modify: `CMakeLists.txt` (SOURCES + test wiring)

- [ ] **Step 1: Define the types + interface**

```cpp
// src/core/manga/VolumeQualityClassifier.h
#pragma once
#include <QList>
#include "MangaCatalogTypes.h"   // MangaVolume (volumeNumber, chapterRangeStart/End)
#include "MangaResult.h"         // ChapterInfo
#include "anilist/AniListTypes.h" // kVolumeXNumber

namespace tankoban::manga {

enum class VolumeQuality { Clean, Magazine };

struct ClassifiedVolume {
    int           volumeNumber = 0;     // catalog number; kVolumeXNumber for the tail bucket
    bool          isVolumeX    = false;
    VolumeQuality quality      = VolumeQuality::Clean;
    QList<double> chapterNumbers;        // member chapters, ascending (for compilation)
};

class VolumeQualityClassifier {
public:
    // catalogVolumes: MangaCatalog volumes (number + chapterRangeStart/End).
    // chapters: scraper chapters (chapterNumber + isVolumeScanned), any order.
    // Returns: one ClassifiedVolume per catalog volume that has >=1 member
    // chapter, ascending by volumeNumber, plus a trailing Volume X (isVolumeX,
    // volumeNumber=kVolumeXNumber) if any chapters fall past the last catalog
    // volume's chapterRangeEnd. Clean iff ALL member chapters are volume-scanned.
    static QList<ClassifiedVolume> classify(
        const QList<MangaVolume>& catalogVolumes,
        const QList<ChapterInfo>& chapters);
};

} // namespace tankoban::manga
```

- [ ] **Step 2: Write failing tests**

```cpp
// tests/core/manga/test_volume_quality_classifier.cpp
#include <gtest/gtest.h>
#include "core/manga/VolumeQualityClassifier.h"

using namespace tankoban::manga;

namespace {
MangaVolume vol(int n, int start, int end) {
    MangaVolume v; v.volumeNumber = n;
    v.chapterRangeStart = start; v.chapterRangeEnd = end; return v;
}
ChapterInfo chap(double n, bool scanned) {
    ChapterInfo c; c.chapterNumber = n; c.isVolumeScanned = scanned; return c;
}
} // namespace

TEST(VolumeQualityClassifier, AllVioletVolumeIsClean) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(1, 1, 8) },
        { chap(1, true), chap(5, true), chap(8, true) });
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out[0].volumeNumber, 1);
    EXPECT_FALSE(out[0].isVolumeX);
    EXPECT_EQ(out[0].quality, VolumeQuality::Clean);
}

TEST(VolumeQualityClassifier, AnyGrayChapterMakesVolumeMagazine) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(110, 1100, 1109) },
        { chap(1100, true), chap(1105, false), chap(1109, true) });
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out[0].quality, VolumeQuality::Magazine);
}

TEST(VolumeQualityClassifier, ChaptersPastLastCatalogVolumeBecomeVolumeX) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(110, 1100, 1109) },
        { chap(1100, true), chap(1110, false), chap(1111, false) });
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out[0].volumeNumber, 110);
    EXPECT_TRUE(out[1].isVolumeX);
    EXPECT_EQ(out[1].volumeNumber, tankoban::manga::anilist::kVolumeXNumber);
    EXPECT_EQ(out[1].quality, VolumeQuality::Magazine);
    ASSERT_EQ(out[1].chapterNumbers.size(), 2);
    EXPECT_DOUBLE_EQ(out[1].chapterNumbers[0], 1110.0);
    EXPECT_DOUBLE_EQ(out[1].chapterNumbers[1], 1111.0);
}

TEST(VolumeQualityClassifier, VolumeWithNoMemberChaptersIsOmitted) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(1, 1, 8), vol(2, 9, 17) },
        { chap(1, true), chap(8, true) });   // no ch in vol 2 range
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out[0].volumeNumber, 1);
}

TEST(VolumeQualityClassifier, NoChaptersYieldsEmpty) {
    const auto out = VolumeQualityClassifier::classify({ vol(1, 1, 8) }, {});
    EXPECT_TRUE(out.isEmpty());
}
```

- [ ] **Step 3: Run tests to verify they fail**

```
out_agent1\tankoban_tests.exe --gtest_filter=VolumeQualityClassifier.*
```
Expected: FAIL / link error (`classify` undefined).

- [ ] **Step 4: Implement the classifier**

```cpp
// src/core/manga/VolumeQualityClassifier.cpp
#include "VolumeQualityClassifier.h"
#include <algorithm>

namespace tankoban::manga {

QList<ClassifiedVolume> VolumeQualityClassifier::classify(
    const QList<MangaVolume>& catalogVolumes,
    const QList<ChapterInfo>& chapters)
{
    QList<ClassifiedVolume> result;
    if (chapters.isEmpty()) return result;

    // Sort catalog volumes ascending by number; track the max chapter covered.
    QList<MangaVolume> vols = catalogVolumes;
    std::sort(vols.begin(), vols.end(), [](const MangaVolume& a, const MangaVolume& b) {
        return a.volumeNumber < b.volumeNumber;
    });
    int lastCatalogChapterEnd = 0;
    for (const auto& v : vols)
        lastCatalogChapterEnd = std::max(lastCatalogChapterEnd, v.chapterRangeEnd);

    // One classified volume per catalog volume that has member chapters.
    for (const auto& v : vols) {
        ClassifiedVolume cv;
        cv.volumeNumber = v.volumeNumber;
        bool allViolet = true;
        for (const auto& ch : chapters) {
            const int c = static_cast<int>(ch.chapterNumber);
            if (c >= v.chapterRangeStart && c <= v.chapterRangeEnd) {
                cv.chapterNumbers.append(ch.chapterNumber);
                if (!ch.isVolumeScanned) allViolet = false;
            }
        }
        if (cv.chapterNumbers.isEmpty()) continue;  // omit empty volumes
        std::sort(cv.chapterNumbers.begin(), cv.chapterNumbers.end());
        cv.quality = allViolet ? VolumeQuality::Clean : VolumeQuality::Magazine;
        result.append(cv);
    }

    // Volume X: chapters past the last catalog volume's coverage.
    ClassifiedVolume volX;
    volX.isVolumeX = true;
    volX.volumeNumber = tankoban::manga::anilist::kVolumeXNumber;
    volX.quality = VolumeQuality::Magazine;   // bleeding-edge is always magazine
    for (const auto& ch : chapters) {
        if (static_cast<int>(ch.chapterNumber) > lastCatalogChapterEnd)
            volX.chapterNumbers.append(ch.chapterNumber);
    }
    if (!volX.chapterNumbers.isEmpty()) {
        std::sort(volX.chapterNumbers.begin(), volX.chapterNumbers.end());
        result.append(volX);
    }

    return result;
}

} // namespace tankoban::manga
```

- [ ] **Step 5: Wire CMake (SOURCES + test)**

In `CMakeLists.txt` `set(SOURCES …)`:
```cmake
    src/core/manga/VolumeQualityClassifier.cpp
```
Under `if(TANKOBAN_BUILD_TESTS)`:
```cmake
        tests/core/manga/test_volume_quality_classifier.cpp
        src/core/manga/VolumeQualityClassifier.cpp
```

- [ ] **Step 6: Build + run tests green**

```
cmake -B out_agent1 -DTANKOBAN_BUILD_TESTS=ON
cmake --build out_agent1 --config Release --target tankoban_tests
out_agent1\tankoban_tests.exe --gtest_filter=VolumeQualityClassifier.*
```
Expected: `[  PASSED  ] 5 tests.`

- [ ] **Step 7: Phase boundary — RTC**

Post to `agents/chat.md`:
```
READY TO COMMIT - [Agent 1, VOLUME_X_QUALITY_P1_CLASSIFIER]: ChapterInfo.isVolumeScanned (WeebCentral violet tick parse) + VolumeQualityClassifier pure-logic Clean/Magazine/Volume X bucketing. Tests: WeebCentralChapterQuality + VolumeQualityClassifier (6 cases) green on agent1 lane. Skills invoked: [/superpowers:writing-plans, /superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion]. | files: src/core/manga/MangaResult.h, WeebCentralScraper.{h,cpp}, VolumeQualityClassifier.{h,cpp}, tests/core/manga/test_weebcentral_chapter_quality.cpp, test_volume_quality_classifier.cpp, CMakeLists.txt
```

---

## Task 4: Packer `.volx` trigger broadening

**Files:**
- Modify: `src/core/manga/WeebCentralVolumePacker.h` (`VolumePackRequest`)
- Modify: `src/core/manga/WeebCentralVolumePacker.cpp` (`finalizePack`)

- [ ] **Step 1: Add the request flag**

In `WeebCentralVolumePacker.h`, on `struct VolumePackRequest`, add:

```cpp
    // VOLUME_X_QUALITY 2026-05-28 (Agent 1). True when this volume is stitched
    // from magazine (gray) chapters — Magazine volumes AND Volume X. Drives the
    // .volx sidecar so the reader applies chapter-boundary pairing. Clean
    // (all-violet) volumes leave it false and stitch with normal global pairing.
    bool needsChapterPairing = false;
```

- [ ] **Step 2: Broaden the marker write**

In `WeebCentralVolumePacker.cpp` `finalizePack`, replace the existing Volume-X-only guard:

```cpp
    if (req.volumeNumber == tankoban::manga::anilist::kVolumeXNumber) {
```
with:
```cpp
    if (req.needsChapterPairing) {
```
(The marker body — writing `req.destinationPath + ".volx"` — is unchanged. The `kVolumeXNumber` include can stay; it's still referenced by the classifier path.)

- [ ] **Step 3: Build-verify**

```
$env:TANKOBAN_BUILD_LANE="agent1"; .\build_check.bat
```
Expected: `BUILD OK`.

---

## Task 5: Dispatch sets the pairing flag from classification

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (WeebCentralPacker dispatch branch, ~line 3012-3064)

- [ ] **Step 1: Thread the magazine verdict into the request**

At the WeebCentral dispatch branch where `VolumePackRequest req;` is built (~line 3055), set the new flag from the classification verdict for the volume being dispatched. The verdict is available via the `ClassifiedVolume` for this `volumeNumber` (the series view holds the classified list — see Task 6 which stores it; expose a lookup `ComicsSeriesView::volumeQualityFor(int volumeNumber) -> bool isMagazine`, or pass the verdict through the existing dispatch signal payload).

```cpp
        VolumePackRequest req;
        req.seriesId        = mangaFireSeriesId;
        req.volumeNumber    = volumeNumber;
        req.destinationPath = destinationPath;
        req.chapterIds      = wcChapterIds;
        // VOLUME_X_QUALITY 2026-05-28 (Agent 1). Magazine + Volume X volumes
        // need chapter-boundary pairing; clean volumes do not.
        req.needsChapterPairing = isMagazineSourcedVolume;  // from classification
```

`isMagazineSourcedVolume` is the bool carried on the dispatch path from the row's `ClassifiedVolume::quality == VolumeQuality::Magazine` (Volume X is always Magazine). Wire it through the same signal/struct that already carries `volumeNumber` + `chapterIds` into this dispatch.

- [ ] **Step 2: Build-verify**

```
$env:TANKOBAN_BUILD_LANE="agent1"; .\build_check.bat
```
Expected: `BUILD OK`.

---

## Task 6: ComicsSeriesView — classification wiring + Volume X row + RAW tag

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp` (`populateVolumeRowsFromCatalog`, ~line 1337)
- Modify: `src/ui/pages/comics/VolumeTile.h/.cpp` (RAW tag rendering)

- [ ] **Step 1: Add a RAW tag to VolumeTile**

In `VolumeTile.h`, add to `VolumeTileData`:
```cpp
    bool isRawScan = false;   // magazine-quality / Volume X → show RAW SCAN badge
```
In `VolumeTile.cpp` `buildUi`/`applyState`, when `m_data.isRawScan`, render a small badge label (text "RAW SCAN", muted amber) on the row. Follow the existing state-icon/label pattern in the file (the download-state icon paint path is the closest sibling). The badge is text-only; no new asset.

- [ ] **Step 2: Run classification in populateVolumeRowsFromCatalog**

In `ComicsSeriesView::populateVolumeRowsFromCatalog`, after `m_currentMangaCatalog = catalog;` (line ~1351) and once the WeebCentral chapter list for the series is available (the scraper's `chaptersReady` map — store it as `m_currentChapters` when it arrives), build the classification:

```cpp
    const auto classified = tankoban::manga::VolumeQualityClassifier::classify(
        catalog.volumes, m_currentChapters);
```

Iterate `classified` instead of `catalog.volumes` for the rows. For each `ClassifiedVolume`:
- `data.volumeNumber = cv.volumeNumber;` (kVolumeXNumber → the existing isVolumeX render path already shows "Volume X").
- `data.isRawScan = (cv.quality == VolumeQuality::Magazine);`
- Carry `cv.quality`/`cv.chapterNumbers` into `m_currentVolumeRows` so the dispatch (Task 5) and Sources panel can read the magazine verdict + chapter list.

Keep the existing per-volume cover/title/synopsis/chapterRange assignment for non-Volume-X rows from the matching `catalog.volumes` entry.

- [ ] **Step 3: Store the chapter list when the scraper returns it**

Add `QList<ChapterInfo> m_currentChapters;` to `ComicsSeriesView`. Populate it from the existing WeebCentral `chaptersReady`/resolve path that already feeds this view (the resolver that produces `weebCentral.volumeChapterIds`). Re-run `populateVolumeRowsFromCatalog` (or just the classification + row refresh) when both catalog and chapters are present.

- [ ] **Step 4: Build-verify + visual smoke**

```
$env:TANKOBAN_BUILD_LANE="agent1"; .\build_check.bat
```
Expected: `BUILD OK`. Then Hemanth smoke: open a series with gray chapters → clean volumes untagged, magazine volumes + Volume X show "RAW SCAN", Volume X at list end.

---

## Task 7: Upgrade lifecycle — offer re-download when a volume goes clean

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`
- Modify: `src/ui/pages/comics/VolumeTile.h/.cpp` (upgrade affordance)

- [ ] **Step 1: Detect the magazine→clean transition on refresh**

On series refresh (re-scrape of the tick + re-classification), compare the new verdict for each already-downloaded volume against its stored quality. If a volume that was downloaded as Magazine now classifies Clean, mark it upgrade-available. Store the prior quality alongside the `MangaDownloadIndex` entry (or derive from a stored `.volx` presence: a downloaded volume that has a `.volx` sidecar but now classifies Clean = upgradeable).

- [ ] **Step 2: Surface a per-volume upgrade affordance**

In `VolumeTile`, when `data.upgradeAvailable`, show an "Update available" affordance (small button/icon) on the downloaded row. Clicking it re-dispatches the volume download (now classified Clean → no `.volx`, clean stitch). Reuse the existing download dispatch path; the re-download overwrites the cbz + drops the stale `.volx`.

- [ ] **Step 3: Build-verify + smoke**

```
$env:TANKOBAN_BUILD_LANE="agent1"; .\build_check.bat
```
Expected: `BUILD OK`. Smoke: a downloaded magazine volume whose chapters later go violet shows the upgrade affordance; clicking re-downloads the clean scan.

- [ ] **Step 4: Phase boundary — RTC**

Post to `agents/chat.md`:
```
READY TO COMMIT - [Agent 1, VOLUME_X_QUALITY_P2_WIRING]: packer .volx trigger broadened to needsChapterPairing; ComicsPage dispatch sets it from classification; ComicsSeriesView runs VolumeQualityClassifier → RAW SCAN tags + Volume X row + upgrade-on-clean affordance; VolumeTile RAW badge + upgrade affordance. BUILD OK agent1 lane; Hemanth visual smoke [pending/passed]. Skills invoked: [/superpowers:writing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review]. | files: src/core/manga/WeebCentralVolumePacker.{h,cpp}, src/ui/pages/ComicsPage.cpp, src/ui/pages/comics/ComicsSeriesView.cpp, VolumeTile.{h,cpp}
```

---

## Already built (no task — slots in as-is)

- `buildTwoPagePairs` chapter-local parity + `TwoPagePairingPage::isChapterStart` (`ComicReader.h`) — 12/12 `ComicReaderPairing` tests green.
- Reader `.volx` detection (`m_isVolumeX` in `openBook`) + chapter-start tagging from `<chapter>_<page>` filenames (`pairingPages()`).
- The `.volx` sidecar write in `WeebCentralVolumePacker` (Task 4 only changes the *condition* under which it fires).

## Regression guard

After all tasks, re-run the full pairing suite to confirm no regression:
```
out_agent1\tankoban_tests.exe --gtest_filter=ComicReaderPairing.*
```
Expected: `[  PASSED  ] 12 tests.`
