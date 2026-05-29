# Comics — MangaPlus Spread Stitching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build magazine-compiled comic volumes (WeebCentral-packed: numbered magazine volumes AND Volume X) so that double-page spreads stay intact, by fetching pages in WeebCentral's MangaPlus pairing mode and stitching each paired set of half-pages into one wide image at download time.

**Architecture:** WeebCentral's `reading_style=double_page_v2` ("Double Page (MangaPlus)") endpoint returns pages already grouped into `page === N` facing-pairs, with the cover alone and the correct right-to-left offset baked in. We fetch in that mode, carry the group number on each `PageInfo`, and the packer stitches each 2-image group into a single wide image before zipping. The reader needs **no changes** — a wide image is already displayed alone as a full spread in the existing double-page default, so stitched spreads render correctly and the fragile chapter-boundary "blank" heuristic becomes harmless (it only ever affected narrow-page pairing, and every spread is now a wide single).

**Tech Stack:** C++20, Qt 6.10 (QImage/QPainter for compositing, QRegularExpression for HTML parse), GoogleTest (`tankoban_tests`), MSVC + Ninja.

---

## Background / why this works (read once)

Empirically verified against live WeebCentral on 2026-05-29 (One Piece, series `01J76XY7E9FNDZ1DBBM6PBJPFK`):

- `GET /chapters/<id>/images?reading_style=double_page_v2` returns HTML with repeated `<div x-show="page === N">` blocks, each holding one `<img src=...>`. Pages sharing the same `N` are one facing-pair.
- First chapter, `double_page_v2`: group 1 = `[0001-001.png]` (cover, **alone**); group 2 = `[0001-003.png, 0001-002.png]`; group 3 = `[0001-005.png, 0001-004.png]`. So the cover is solo and every later page is paired with its true partner.
- Natively-wide spread pages (source already serves them combined, ~1568px wide) appear as their **own 1-image group** — so they pass through unstitched and the reader shows them as a spread anyway.
- Plain `reading_style=double_page` (no `_v2`) pairs from page 1 with **no** cover-alone — wrong offset, do not use.
- `long_strip` (current code) returns a flat list with no pairing info.

The reader's spread detector is purely aspect-ratio (`width/height > 1.08`, `SPREAD_RATIO` in `ComicReader.cpp` / `ScrollStripCanvas`), and a spread page is rendered alone spanning the view. A stitched pair (two ~784-wide halves → one ~1568-wide image) is therefore auto-detected and shown correctly with zero reader changes.

**Scope note — reader is intentionally untouched.** The existing `.volx` marker + book-mode default stay as-is. The chapter-boundary "show first page of chapter alone" logic in `ComicReader::pairingPages()` is left in place: after stitching, every real spread is a wide single (shown alone regardless of pairing), so that heuristic can no longer split a spread. Removing it is a separate optional cleanup, explicitly **out of scope** here.

**Migration:** existing volumes already on disk are in the old split format. They are fixed by **deleting + re-downloading** (the delete-frees-for-redownload fix landed 2026-05-29, commit `d098559`). No automatic on-disk migration is in scope.

---

## File Structure

- `src/core/manga/MangaResult.h` — add `int pageGroup` to `PageInfo` (the MangaPlus facing-pair number; `-1` when ungrouped).
- `src/core/manga/MangaScraper.h` — add a non-pure virtual `fetchPagesPaired(chapterId)` defaulting to `fetchPages(chapterId)` so only WeebCentral overrides it (ReadComics unaffected).
- `src/core/manga/WeebCentralScraper.{h,cpp}` — implement `fetchPagesPaired` (fetch `double_page_v2`), add `parsePagesPairedHtml` (grouped parse) + a static test hook.
- `src/core/manga/WeebCentralVolumePacker.cpp` — restructure the chapter image handler to download all images, group by `pageGroup`, stitch 2-image groups into one wide image, save one file per group.
- `tests/manga/WeebCentralPairedParseTest.cpp` — new GoogleTest for the grouped parser (pure logic).
- `tests/CMakeLists.txt` (or the `tankoban_tests` target's source list) — register the new test.

---

### Task 1: Carry the facing-pair group number on `PageInfo`

**Files:**
- Modify: `src/core/manga/MangaResult.h:29-33`

- [ ] **Step 1: Add the field**

In `src/core/manga/MangaResult.h`, change the `PageInfo` struct:

```cpp
struct PageInfo {
    int     index = 0;
    QString imageUrl;
    // MangaPlus (double_page_v2) facing-pair number. Pages sharing a pageGroup
    // are one two-page view; the packer stitches a 2-image group into one wide
    // spread. -1 = ungrouped (long_strip / flat fetch path).
    int     pageGroup = -1;
};
```

- [ ] **Step 2: Build-check (compile only)**

Run: `build_check.bat`
Expected: `BUILD OK` (PageInfo is widely included; this is an additive field with a default, so nothing breaks).

- [ ] **Step 3: Commit**

```bash
git add src/core/manga/MangaResult.h
git commit -m "[Agent 1, COMICS_SPREAD_STITCH]: add pageGroup to PageInfo for MangaPlus pairing"
```

---

### Task 2: Grouped page parser + `fetchPagesPaired` in the scraper

**Files:**
- Modify: `src/core/manga/MangaScraper.h:23` (add virtual)
- Modify: `src/core/manga/WeebCentralScraper.h:16-27`
- Modify: `src/core/manga/WeebCentralScraper.cpp` (add `fetchPagesPaired` + `parsePagesPairedHtml`)
- Test: `tests/manga/WeebCentralPairedParseTest.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/manga/WeebCentralPairedParseTest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/manga/WeebCentralScraper.h"
#include <QString>

// Mirrors the live double_page_v2 markup: repeated <div x-show="page === N">
// blocks each wrapping one <img src=...>. Cover is alone (group 1, one img);
// later groups carry two halves.
static const char* kV2Html = R"HTML(
<section>
  <div x-show="page === 1" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-001.png" decoding="async" alt="" />
  </div>
  <div x-show="page === 2" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-003.png" decoding="async" alt="" />
  </div>
  <div x-show="page === 2" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-002.png" decoding="async" alt="" />
  </div>
  <div x-show="page === 3" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-005.png" decoding="async" alt="" />
  </div>
</section>
)HTML";

TEST(WeebCentralPairedParse, GroupsCoverAloneAndPairsInOrder)
{
    const QList<PageInfo> pages =
        WeebCentralScraper::parsePagesPairedHtmlForTest(QString::fromUtf8(kV2Html));

    ASSERT_EQ(pages.size(), 4);

    // Group 1: cover, alone.
    EXPECT_EQ(pages[0].pageGroup, 1);
    EXPECT_TRUE(pages[0].imageUrl.endsWith("0001-001.png"));

    // Group 2: two halves, preserved in document (left-to-right visual) order.
    EXPECT_EQ(pages[1].pageGroup, 2);
    EXPECT_TRUE(pages[1].imageUrl.endsWith("0001-003.png"));
    EXPECT_EQ(pages[2].pageGroup, 2);
    EXPECT_TRUE(pages[2].imageUrl.endsWith("0001-002.png"));

    // Group 3: single (e.g. a natively-wide spread).
    EXPECT_EQ(pages[3].pageGroup, 3);
    EXPECT_TRUE(pages[3].imageUrl.endsWith("0001-005.png"));
}

TEST(WeebCentralPairedParse, SkipsBrokenImagePlaceholder)
{
    const QString html = QStringLiteral(
        "<div x-show=\"page === 1\"><img src=\"https://cdn/x/broken_image.jpg\"/></div>"
        "<div x-show=\"page === 1\"><img src=\"https://cdn/x/0001-001.png\"/></div>");
    const QList<PageInfo> pages =
        WeebCentralScraper::parsePagesPairedHtmlForTest(html);
    ASSERT_EQ(pages.size(), 1);
    EXPECT_TRUE(pages[0].imageUrl.endsWith("0001-001.png"));
    EXPECT_EQ(pages[0].pageGroup, 1);
}
```

- [ ] **Step 2: Register the test in the `tankoban_tests` target**

Find where existing manga tests are listed (search the test CMake for an existing manga test, e.g. `grep -rn "VolumeQualityClassifier\|AniListVolumeMapper" tests/CMakeLists.txt CMakeLists.txt`). Add `tests/manga/WeebCentralPairedParseTest.cpp` to the same `tankoban_tests` source list, alongside the file that already compiles `WeebCentralScraper.cpp` into the test target. If `WeebCentralScraper.cpp` is not already in the test target, add it too (the parser is static, no Qt-network needed at link beyond QtCore/QtNetwork already linked by the test target).

- [ ] **Step 3: Run the test target build + the new test, verify it FAILS to compile**

Run: `_build_tests.bat` then `set PATH=C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;%PATH% && out\tankoban_tests.exe --gtest_filter=WeebCentralPairedParse.*`
Expected: COMPILE FAIL — `parsePagesPairedHtmlForTest` is not a member of `WeebCentralScraper`.

- [ ] **Step 4: Add the declarations**

In `src/core/manga/MangaScraper.h`, after the `fetchPages` pure virtual (line 23), add a non-pure virtual:

```cpp
    virtual void fetchPages(const QString& chapterId) = 0;

    // Fetch pages already grouped into MangaPlus facing-pairs (PageInfo.pageGroup
    // set). Default falls back to the flat fetchPages for scrapers that have no
    // paired endpoint (e.g. ReadComics). Result still arrives via pagesReady().
    virtual void fetchPagesPaired(const QString& chapterId) { fetchPages(chapterId); }
```

In `src/core/manga/WeebCentralScraper.h`, add to the `public:` section (after line 19) and `private:` section (after line 27):

```cpp
    // public:
    void fetchPagesPaired(const QString& chapterId) override;

    static QList<PageInfo> parsePagesPairedHtmlForTest(const QString& html)
    { return parsePagesPairedHtml(html); }
```

```cpp
    // private:
    static QList<PageInfo> parsePagesPairedHtml(const QString& html);
```

- [ ] **Step 5: Implement `fetchPagesPaired` + `parsePagesPairedHtml`**

In `src/core/manga/WeebCentralScraper.cpp`, after `parsePagesHtml` (ends ~line 364), add:

```cpp
// ── Pages (MangaPlus paired) ──────────────────────────────────────────────
void WeebCentralScraper::fetchPagesPaired(const QString& chapterId)
{
    QUrl url(BASE + "/chapters/" + chapterId + "/images");
    QUrlQuery q;
    q.addQueryItem("is_prev", "False");
    q.addQueryItem("current_page", "1");
    // double_page_v2 = "Double Page (MangaPlus)": cover-alone + correct
    // right-to-left facing pairs, verified 2026-05-29.
    q.addQueryItem("reading_style", "double_page_v2");
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch paired pages: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit pagesReady(parsePagesPairedHtml(html));
    });
}

QList<PageInfo> WeebCentralScraper::parsePagesPairedHtml(const QString& html)
{
    QList<PageInfo> pages;

    // double_page_v2 markup: repeated `... page === N ...> <img src="...">`.
    // Each match is one image tagged with its facing-pair group N. Document
    // order is the visual left-to-right order WeebCentral renders.
    static const QRegularExpression groupImgRe(
        QStringLiteral(R"RE(page === (\d+)[^>]*>\s*<img\b[^>]*\bsrc="(https?://[^"]+\.(?:png|jpe?g|webp)(?:\?[^"]*)?)")RE"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    auto it = groupImgRe.globalMatch(html);
    int idx = 0;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString url = m.captured(2);
        if (url.contains(QLatin1String("/broken_image.")))
            continue;
        PageInfo p;
        p.index     = idx++;
        p.pageGroup = m.captured(1).toInt();
        p.imageUrl  = url;
        pages.append(p);
    }
    return pages;
}
```

- [ ] **Step 6: Build the test target + run, verify PASS**

Run: `_build_tests.bat` then `set PATH=C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;%PATH% && out\tankoban_tests.exe --gtest_filter=WeebCentralPairedParse.*`
Expected: `[  PASSED  ] 2 tests.`

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/MangaScraper.h src/core/manga/WeebCentralScraper.h src/core/manga/WeebCentralScraper.cpp tests/manga/WeebCentralPairedParseTest.cpp tests/CMakeLists.txt
git commit -m "[Agent 1, COMICS_SPREAD_STITCH]: WeebCentral MangaPlus paired-page fetch + grouped parser (TDD)"
```

---

### Task 3: Stitch paired groups into wide images in the packer

**Files:**
- Modify: `src/core/manga/WeebCentralVolumePacker.cpp` (includes + `startNextChapter`)

**Approach:** Call `fetchPagesPaired` instead of `fetchPages`. On `pagesReady`, download every image into an in-memory buffer keyed by page index; once all of the chapter's images have arrived, walk the groups in order — a 1-image group is written straight to disk, a 2-image group is decoded, composited side-by-side (document order = visual left-to-right), and the combined image saved as one file. One output file per group keeps the existing `<chapterIdx>_<seq>.jpg` natural-sort ordering.

- [ ] **Step 1: Add image includes**

In `src/core/manga/WeebCentralVolumePacker.cpp`, in the include block (after line 24 `#include <QUrl>`), add:

```cpp
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QMap>
```

- [ ] **Step 2: Switch the scraper call to the paired endpoint**

In `startNextChapter`, change the final line (currently `m_scraper->fetchPages(chapterId);`, ~line 256) to:

```cpp
    // Plan adaptation: fetch in MangaPlus paired mode so PageInfo.pageGroup is
    // populated and we can stitch facing-pairs into one spread image.
    m_scraper->fetchPagesPaired(chapterId);
```

- [ ] **Step 3: Replace the per-image save loop with download-all-then-stitch-by-group**

In `startNextChapter`, replace the body of the `pagesReady` lambda (the block from `const int total = pages.size();` through the per-image `for (int p = 0; p < total; ++p) { ... }` loop, i.e. current lines ~172-252) with the following. Keep the surrounding `connect(... &MangaScraper::pagesReady ...)` wrapper, the `QObject::disconnect(*conn); delete conn;` lines, and the `m_scraper->fetchPagesPaired(chapterId);` call unchanged.

```cpp
            const int total = pages.size();
            if (total == 0) {
                emit volumeFailed(req.seriesId, req.volumeNumber,
                                  QStringLiteral("zero_pages_in_chapter"),
                                  QStringLiteral("chapter returned 0 image URLs"));
                return;
            }

            // Buffer every downloaded image by its flat index; stitch per group
            // once all have arrived. shared_ptr gives heap lifetime across the
            // async per-image finished lambdas.
            auto bytesByIndex = std::make_shared<QMap<int, QByteArray>>();
            auto finished = std::make_shared<int>(0);
            auto aborted  = std::make_shared<bool>(false);

            for (int p = 0; p < total; ++p) {
                if (!m_nam) {
                    if (!*aborted) {
                        *aborted = true;
                        emit volumeFailed(req.seriesId, req.volumeNumber,
                                          QStringLiteral("nam_unavailable"), QString());
                    }
                    return;
                }
                QNetworkRequest httpReq(QUrl(pages.at(p).imageUrl));
                httpReq.setRawHeader("User-Agent", kUserAgent);
                httpReq.setRawHeader("Referer", "https://weebcentral.com/");
                httpReq.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8");
                auto* reply = m_nam->get(httpReq);
                connect(reply, &QNetworkReply::finished,
                    this, [this, reply, p, total, pages, stagingDir, chapterIdx,
                           totalChapters, bytesByIndex, finished, aborted, req]() {
                        reply->deleteLater();
                        if (*aborted) return;
                        if (reply->error() != QNetworkReply::NoError) {
                            *aborted = true;
                            emit volumeFailed(req.seriesId, req.volumeNumber,
                                              QStringLiteral("image_fetch_failed"),
                                              reply->errorString());
                            return;
                        }
                        const QByteArray data = reply->readAll();
                        if (data.size() > kImageMaxBytes) {
                            *aborted = true;
                            emit volumeFailed(req.seriesId, req.volumeNumber,
                                              QStringLiteral("image_oversize"),
                                              QString::number(data.size()));
                            return;
                        }
                        bytesByIndex->insert(p, data);

                        if (++(*finished) != total) return;

                        // All images in; stitch per facing-pair group, in
                        // document order. One output file per group.
                        QMap<int, QList<int>> groupToIndices;  // pageGroup -> flat indices (ordered)
                        QList<int> groupOrder;                 // first-seen order of groups
                        for (int i = 0; i < pages.size(); ++i) {
                            const int g = pages.at(i).pageGroup >= 0
                                              ? pages.at(i).pageGroup
                                              : i;  // ungrouped: each its own group
                            if (!groupToIndices.contains(g)) groupOrder.append(g);
                            groupToIndices[g].append(i);
                        }

                        int seq = 0;
                        for (const int g : groupOrder) {
                            const QList<int>& members = groupToIndices[g];
                            const QString outName = QStringLiteral("%1_%2.jpg")
                                .arg(chapterIdx, 4, 10, QChar('0'))
                                .arg(seq++,      4, 10, QChar('0'));
                            const QString outPath = stagingDir + QChar('/') + outName;

                            if (members.size() == 1) {
                                // Single (cover / natively-wide spread): write bytes as-is.
                                QFile f(outPath);
                                if (!f.open(QIODevice::WriteOnly)) {
                                    if (!*aborted) { *aborted = true;
                                        emit volumeFailed(req.seriesId, req.volumeNumber,
                                                          QStringLiteral("write_failed"), outName); }
                                    return;
                                }
                                f.write(bytesByIndex->value(members.first()));
                                f.close();
                                continue;
                            }

                            // Pair: decode both halves and composite left-to-right
                            // in document order (WeebCentral renders the group in
                            // visual L->R order for the reader's RTL layout).
                            QImage left, right;
                            left.loadFromData(bytesByIndex->value(members.at(0)));
                            right.loadFromData(bytesByIndex->value(members.at(1)));
                            if (left.isNull() || right.isNull()) {
                                // Fallback: a half failed to decode — write the
                                // decodable one, or the raw first half, rather
                                // than abort the whole volume.
                                QFile f(outPath);
                                if (f.open(QIODevice::WriteOnly)) {
                                    f.write(bytesByIndex->value(members.first()));
                                    f.close();
                                }
                                continue;
                            }
                            const int h = qMax(left.height(), right.height());
                            const int w = left.width() + right.width();
                            QImage combined(w, h, QImage::Format_RGB32);
                            combined.fill(Qt::white);
                            QPainter painter(&combined);
                            painter.drawImage(QPoint(0, (h - left.height()) / 2), left);
                            painter.drawImage(QPoint(left.width(), (h - right.height()) / 2), right);
                            painter.end();
                            if (!combined.save(outPath, "JPEG", 92)) {
                                if (!*aborted) { *aborted = true;
                                    emit volumeFailed(req.seriesId, req.volumeNumber,
                                                      QStringLiteral("stitch_save_failed"), outName); }
                                return;
                            }
                        }

                        const double pct = static_cast<double>(chapterIdx + 1)
                                         / static_cast<double>(totalChapters);
                        emit volumeProgress(req.seriesId, req.volumeNumber, pct);
                        startNextChapter(req, chapterIdx + 1, totalChapters, stagingDir);
                    });
            }
```

- [ ] **Step 4: Build-check**

Run: `taskkill //F //IM Tankoban.exe` (Rule 1) then `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/WeebCentralVolumePacker.cpp
git commit -m "[Agent 1, COMICS_SPREAD_STITCH]: stitch MangaPlus facing-pairs into one spread image at pack time"
```

---

### Task 4: Smoke — acceptance gate (eyes on the running app)

> Green build + green unit test prove the parser and that it compiles — **not** that spreads render correctly. The acceptance gate is opening a freshly-stitched volume and seeing intact spreads. This is non-negotiable (the whole arc exists because eyes-on-app caught what tests couldn't).

**Files:** none (verification only)

- [ ] **Step 1: Launch the rebuilt app**

Run: `build_and_run.bat` (or launch `out\Tankoban.exe --dev-control` if already built). Wait for the dev pipe: `out\tankoctl.exe ping`.

- [ ] **Step 2: Delete the old split-format Vol 114, then re-download it**

In the app: open One Piece → right-click Vol 114 → Delete (file too) → confirm the tile reverts to undownloaded → click it → Sources panel → download the WeebCentral source. (This exercises the new paired-fetch + stitch path end-to-end.)

- [ ] **Step 3: Verify the stitched file on disk**

Run (adjust path):
```
python - <<PY
import zipfile, struct
z=zipfile.ZipFile(r"C:/Users/Suprabha/Desktop/Media/Comics/One Piece/Volume 114.cbz")
def dims(b):
    if b[:8]==b'\x89PNG\r\n\x1a\n': import struct; w,h=struct.unpack('>II',b[16:24]); return w,h
    if b[:2]==b'\xff\xd8':
        i=2
        while i<len(b):
            if b[i]!=0xFF: i+=1; continue
            m=b[i+1]
            if m in (0xC0,0xC1,0xC2,0xC3): h,w=struct.unpack('>HH',b[i+5:i+9]); return w,h
            if m in (0xD8,0xD9) or 0xD0<=m<=0xD7: i+=2; continue
            i+=2+struct.unpack('>H',b[i+2:i+4])[0]
n=[x for x in z.namelist() if x.lower().endswith(('.jpg','.png'))]; n.sort()
wide=sum(1 for x in n for d in [dims(z.read(x)[:65536])] if d and d[0]>d[1])
print("pages",len(n),"wide(stitched/spread)",wide)
PY
```
Expected: a meaningful count of `wide` images (the stitched facing-pairs) — i.e. most non-cover pages are now wide, not 784-wide halves.

- [ ] **Step 4: Hemanth visual confirm (the real gate)**

Open Vol 114 in the reader. Confirm with Hemanth:
- The chapter-2 opening spread (the one that was split in the bug report) now renders as **one continuous spread**.
- The cover sits alone, no stray blank mid-chapter splitting a spread.
- Flipping through, double-page spreads stay whole.

**Orientation check:** if any stitched spread reads **mirrored** (right half on the left), reverse the paint order in Task 3 Step 3 — swap `members.at(0)`/`members.at(1)` in the two `drawImage` calls — rebuild, re-download, re-confirm. (Document order was taken as visual left-to-right; this step verifies that assumption against real art.)

- [ ] **Step 5: Cleanup + close**

Run: `powershell -NoProfile -File scripts/stop-tankoban.ps1` (Rule 17). Post `READY TO MERGE` / RTC per the commit protocol in force.

---

## Self-Review

**Spec coverage:**
- Fetch in MangaPlus mode → Task 2 (`fetchPagesPaired` uses `double_page_v2`). ✓
- Carry pairing info → Task 1 (`PageInfo.pageGroup`). ✓
- Stitch pairs into one spread image → Task 3. ✓
- Cover/natively-wide singles pass through → Task 3 (1-image group writes bytes as-is). ✓
- Reader renders stitched spreads → no code (existing aspect-ratio spread detection); documented in Background. ✓
- Existing volumes fixed by re-download → Task 4 Step 2; no migration (documented). ✓
- Acceptance = eyes-on smoke → Task 4. ✓

**Placeholder scan:** No TBD/TODO/"handle errors" — error paths are concrete (`zero_pages_in_chapter`, `image_fetch_failed`, `image_oversize`, `write_failed`, `stitch_save_failed`, decode fallback). Test code is complete. ✓

**Type consistency:** `pageGroup` (Task 1) is read in Task 3. `fetchPagesPaired` declared in `MangaScraper.h` + overridden in `WeebCentralScraper.h` (Task 2), called in Task 3. `parsePagesPairedHtml` / `parsePagesPairedHtmlForTest` names match between Task 2 declaration, implementation, and test. ✓

**Known assumption flagged for smoke:** stitch L/R order (document order = visual left-to-right) is verified in Task 4 Step 4 with an explicit swap-and-retry instruction if mirrored.

**Out of scope (intentional):** removing the now-harmless `.volx` chapter-break heuristic from the reader; auto-migrating existing on-disk volumes; applying paired-fetch to ReadComics (its `fetchPagesPaired` falls back to flat `fetchPages`).
