# RCO Reader Descramble → Western Download-to-CBZ Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Western (readcomicsonline / rcostation.xyz) comics actually downloadable+readable by un-obfuscating RCO's reader page-image URLs and routing the Western edition download through the existing manga `MangaDownloader` page→cbz pipeline.

**Architecture:** RCO's reader scrambles its blogspot-hosted page-image URLs (a JS array + `fDRrmAS0JMc` transform in `rguard.min.js` v1.5.8). gallery-dl's proven `baeu()` descramble recovers them (verified live 2026-06-03, 3/3 real JPEGs — see `agents/audits/rco_descramble_spike.py`). We port that descramble into `ReadComicsScraper::parsePagesHtml` (currently dead → returns empty), then wire the Western edition "download" action to `MangaDownloader::startDownload(..., "readcomicsonline", ...)` — a complete, RCO-aware, but currently UNWIRED pipeline that calls `fetchPages` → downloads each `PageInfo.imageUrl` → packs a `.cbz`. Status surfaces through the live Sources-panel UX shipped in `4f9d221`. The GetComics path (`WesternVolumeDownloader`) is superseded for the actual fetch and left in place (dormant) for now.

**Tech Stack:** C++17 / Qt6 (QString, QRegularExpression, QByteArray base64), GoogleTest (`tankoban_tests`), existing `MangaDownloader` (QZipWriter cbz packer) + `MangaDownloadIndex`.

---

## File Structure

- **Create** `src/core/manga/ReadComicsPageParse.h` / `.cpp` — pure, network-free descramble: `parseReaderPages(html) -> QList<PageInfo>` and the `baeu()` URL-descramble. Mirrors the `GetComicsParse` pure-logic pattern so it is unit-testable. One responsibility: turn a reader-page HTML string into ordered page-image URLs.
- **Modify** `src/core/manga/ReadComicsScraper.cpp` — `parsePagesHtml()` delegates to `ReadComicsPageParse::parseReaderPages()` instead of the dead legacy `var pages=[...]` regex.
- **Modify** `src/core/manga/MangaDownloader.cpp` — fix the hardcoded RCO Referer (`readcomicsonline.ru` → `rcostation.xyz`); harmless if Referer is ignored, correct if checked.
- **Modify** `src/ui/pages/comics/ComicsSeriesView.h` / `.cpp` — carry the edition `sourceHref` on `downloadWesternEditionRequested` so the trigger knows the RCO chapterId.
- **Modify** `src/ui/pages/ComicsPage.cpp` — `downloadWesternEditionRequested` handler routes to `m_mangaDownloader->startDownload(...)` (RCO source) instead of `m_westernDownloader->requestVolume(...)`; wire `MangaDownloader` signals → Sources-panel status + tile + `MangaDownloadIndex`.
- **Create** `tests/core/manga/ReadComicsPageParseTest.cpp` — unit tests for `baeu()` + `parseReaderPages()` against a frozen live-captured vector + a synthetic fixture. Register in `cmake/TankobanTests.cmake`.
- **Modify** `cmake/TankobanTests.cmake` — add `src/core/manga/ReadComicsPageParse.cpp` + the new test to `tankoban_tests`.
- **Reference (read, do not edit):** `src/core/manga/MangaResult.h` (`PageInfo{int index; QString imageUrl; int pageGroup=-1}`, `ChapterInfo{QString id,url,name; double chapterNumber; qint64 dateUpload; QString source; bool isVolumeScanned}`), `src/core/manga/MangaDownloader.h` (`startDownload(seriesTitle, source, QList<ChapterInfo>, destinationPath, format, coverPath="")`), `agents/audits/rco_descramble_spike.py` (the proven algorithm).

---

## Task 1: Pure descramble unit — `ReadComicsPageParse`

**Files:**
- Create: `src/core/manga/ReadComicsPageParse.h`
- Create: `src/core/manga/ReadComicsPageParse.cpp`
- Test: `tests/core/manga/ReadComicsPageParseTest.cpp`
- Modify: `cmake/TankobanTests.cmake`

- [ ] **Step 1: Capture a frozen live test vector**

The descramble's correctness was proven live, but the magic offsets are version-specific, so freeze a real input→output pair as a regression anchor. Run (from repo root):

```bash
cd agents/audits && python - <<'PY'
import re, urllib.request
UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36"
PAGE="https://rcostation.xyz/Comic/Invincible/TPB-1-Family-matters"
req=urllib.request.Request(PAGE, headers={"User-Agent":UA})
page=urllib.request.urlopen(req,timeout=30).read().decode("utf-8","replace")
root=(re.search(r"return baeu\(l, '([^']*)'", page) or [None,""])[1]
var=re.search(r"var pth = '[^']*';\s*var (\w+)\s*=\s*'", page).group(1)
repls=re.findall(r"l = l\.replace\(/([^/]+)/g, [\"']([^\"']*)[\"']\)", page)
raw=re.search(r"= '([^']*)'", page.split(var)[2]).group(1)
print("ROOT=",repr(root)); print("VAR=",repr(var)); print("REPLS=",repls); print("RAW_TOKEN_0=",repr(raw))
PY
```

Record `RAW_TOKEN_0`, `REPLS`, `ROOT` from the output, and the expected descrambled URL (the spike already prints these — `[0] -> https://2.bp.blogspot.com/...=s1600?rhlupa=...`). These become the test constants in Step 3.

- [ ] **Step 2: Write the failing test**

```cpp
// tests/core/manga/ReadComicsPageParseTest.cpp
#include <gtest/gtest.h>
#include "core/manga/ReadComicsPageParse.h"

using namespace tankoban::manga::readcomics;

// baeu() reproduces gallery-dl's readcomiconline descramble (rguard v1.5.8).
// Vector frozen from a live rcostation reader page (Task 1 Step 1).
TEST(ReadComicsPageParse, BaeuDescramblesToBlogspot) {
    // RAW_TOKEN_0 is the post-`= '...'` string BEFORE the page replacements;
    // baeu() applies the page replacements then the transpose+base64 steps.
    const QString root = "";  // empty -> default https://2.bp.blogspot.com
    const QList<QPair<QString,QString>> repls = {
        {"fF__R8BcQ4_", "g"}, {"b", "pw_.g28x"}, {"h", "d2pr.x_27"}};
    const QString rawToken = /* RAW_TOKEN_0 from Step 1 */ "";
    const QString url = baeu(applyReplacements(rawToken, repls), root);
    EXPECT_TRUE(url.startsWith("https://2.bp.blogspot.com/"));
    EXPECT_TRUE(url.contains("=s"));   // size suffix preserved
}

// parseReaderPages() extracts ordered page URLs from a reader HTML payload.
TEST(ReadComicsPageParse, ParseReaderPagesExtractsOrderedUrls) {
    // Minimal synthetic fixture mirroring rcostation's inline reader shape.
    const QString html = R"HTML(
        <script>
        var pth = '';
        var _ZZ = '';
        l = l.replace(/fF__R8BcQ4_/g, 'g');
        l = l.replace(/b/g, 'pw_.g28x');
        l = l.replace(/h/g, 'd2pr.x_27');
        return baeu(l, '');
        _ZZ = 'https://2.bp.blogspot.com/already-plain=s1600?rhlupa=x';
        _ZZ = 'https://2.bp.blogspot.com/second-plain=s1600?rhlupa=y';
        </script>
    )HTML";
    const auto pages = parseReaderPages(html);
    ASSERT_EQ(pages.size(), 2);
    EXPECT_EQ(pages[0].index, 0);
    EXPECT_TRUE(pages[0].imageUrl.startsWith("https://2.bp.blogspot.com/"));
    EXPECT_EQ(pages[1].index, 1);
}

// Dead/obfuscation-changed page (no var/baeu markers) -> empty, never crashes.
TEST(ReadComicsPageParse, ParseReaderPagesFailSafeEmpty) {
    EXPECT_TRUE(parseReaderPages("<html>no reader here</html>").isEmpty());
}
```

- [ ] **Step 3: Fill the frozen vector** — paste `RAW_TOKEN_0` from Step 1 into `rawToken` in the first test. (The synthetic fixture in test 2 uses already-`https` tokens so it does not depend on the transpose offsets.)

- [ ] **Step 4: Run the test, verify it fails to compile/link**

Run: `scripts\build_tests_agent1.bat "ReadComicsPageParse.*"`
Expected: FAIL — `ReadComicsPageParse.h` not found / `baeu` undefined.

- [ ] **Step 5: Write the header**

```cpp
// src/core/manga/ReadComicsPageParse.h
#pragma once
#include "MangaResult.h"      // PageInfo
#include <QString>
#include <QList>
#include <QPair>

// Pure parse/descramble for the readcomicsonline / rcostation.xyz reader. No
// network, no UI — unit-tested in tankoban_tests. RCO scrambles its blogspot
// page-image URLs (rguard.min.js v1.5.8); this recovers them. Port of
// gallery-dl readcomiconline.py baeu()+images() (proven live 2026-06-03,
// agents/audits/rco_descramble_spike.py). FRAGILE: the transpose offsets are
// rguard-version-specific — mirror gallery-dl if rcostation bumps the scheme.
namespace tankoban::manga::readcomics {

// Apply the page's ordered junk-token replacements (l.replace(/X/g,'Y')) to a
// raw token, in order.
QString applyReplacements(const QString& token,
                          const QList<QPair<QString,QString>>& replacements);

// Descramble one (already-replacement-applied) token into a real blogspot URL.
// `root` empty -> https://2.bp.blogspot.com. Mirrors gallery-dl baeu().
QString baeu(const QString& url, const QString& root);

// Parse a reader-page HTML payload into ordered page-image URLs. Extracts the
// root, the array var name, and the replacements from the page, then descrambles
// each token. Returns empty (never throws) if the markers are absent.
QList<PageInfo> parseReaderPages(const QString& html);

} // namespace tankoban::manga::readcomics
```

- [ ] **Step 6: Write the implementation**

```cpp
// src/core/manga/ReadComicsPageParse.cpp
#include "ReadComicsPageParse.h"
#include <QRegularExpression>
#include <QByteArray>

namespace tankoban::manga::readcomics {

QString applyReplacements(const QString& token,
                          const QList<QPair<QString,QString>>& replacements)
{
    QString s = token;
    for (const auto& r : replacements) s.replace(r.first, r.second);
    return s;
}

QString baeu(const QString& urlIn, const QString& rootIn)
{
    static const QString kBlogspot = QStringLiteral("https://2.bp.blogspot.com");
    const QString root = rootIn.isEmpty() ? kBlogspot : rootIn;

    QString url = urlIn;
    url.replace(QStringLiteral("pw_.g28x"), QStringLiteral("b"));
    url.replace(QStringLiteral("d2pr.x_27"), QStringLiteral("h"));

    if (url.startsWith(QLatin1String("https")))
        return QString(url).replace(kBlogspot, root);

    const int q = url.indexOf(QLatin1Char('?'));
    QString path  = (q < 0) ? url : url.left(q);
    const QString sep   = (q < 0) ? QString() : QStringLiteral("?");
    const QString query = (q < 0) ? QString() : url.mid(q + 1);

    const bool s0 = path.contains(QLatin1String("=s0"));
    path.chop(s0 ? 3 : 6);                       // strip =s0 / =s1600
    path = path.mid(15, 33 - 15) + path.mid(50); // step1: [15:33] + [50:]
    path = path.left(path.size() - 11) + path.right(2); // step2: [:-11] + [-2:]
    path = QString::fromUtf8(QByteArray::fromBase64(path.toUtf8())); // atob
    path = path.left(13) + path.mid(17);         // [:13] + [17:]
    path = path.left(path.size() - 2) + (s0 ? QStringLiteral("=s0")
                                            : QStringLiteral("=s1600"));
    return root + QStringLiteral("/") + path + sep + query;
}

QList<PageInfo> parseReaderPages(const QString& html)
{
    QList<PageInfo> pages;

    // root passed to baeu(l, '<root>')
    static const QRegularExpression rootRe(QStringLiteral(R"rx(return baeu\(l, '([^']*)')rx"));
    const auto rootM = rootRe.match(html);
    if (!rootM.hasMatch()) return pages;            // not the obfuscated reader -> empty
    const QString root = rootM.captured(1);

    // array var name: after `var pth = '...';` comes `var <NAME> = '`
    static const QRegularExpression varRe(QStringLiteral(R"rx(var pth = '[^']*';\s*var (\w+)\s*=\s*')rx"));
    const auto varM = varRe.match(html);
    if (!varM.hasMatch()) return pages;
    const QString var = varM.captured(1);

    // junk-token replacements: l = l.replace(/X/g, 'Y')
    QList<QPair<QString,QString>> repls;
    static const QRegularExpression replRe(
        QStringLiteral(R"rx(l = l\.replace\(/([^/]+)/g, ["']([^"']*)["']\))rx"));
    auto it = replRe.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        repls.append({m.captured(1), m.captured(2)});
    }

    // each token: page.split(var)[2:], take the `= '...'` string
    static const QRegularExpression tokRe(QStringLiteral(R"rx(= '([^']*)')rx"));
    const QStringList parts = html.split(var);
    int idx = 0;
    for (int i = 2; i < parts.size(); ++i) {
        const auto m = tokRe.match(parts.at(i));
        if (!m.hasMatch()) continue;
        const QString url = baeu(applyReplacements(m.captured(1), repls), root);
        if (!url.startsWith(QLatin1String("https://"))) continue;   // skip junk
        PageInfo p;
        p.index    = idx++;
        p.imageUrl = url;
        pages.append(p);
    }
    return pages;
}

} // namespace tankoban::manga::readcomics
```

- [ ] **Step 7: Register sources in CMake** — in `cmake/TankobanTests.cmake`, add to the `add_executable(tankoban_tests ...)` list:

```cmake
        src/core/manga/ReadComicsPageParse.cpp
        tests/core/manga/ReadComicsPageParseTest.cpp
```

(`ReadComicsPageParse.cpp` must also be in the main app target — add it to the manga sources list in `cmake/TankobanSources.cmake` next to `ReadComicsScraper.cpp`.)

- [ ] **Step 8: Run the tests, verify they pass**

Run: `scripts\build_tests_agent1.bat "ReadComicsPageParse.*"`
Expected: PASS — 3/3.

- [ ] **Step 9: Commit**

```bash
git add src/core/manga/ReadComicsPageParse.h src/core/manga/ReadComicsPageParse.cpp tests/core/manga/ReadComicsPageParseTest.cpp cmake/TankobanTests.cmake cmake/TankobanSources.cmake
git commit -m "feat(comics): RCO reader descramble (ReadComicsPageParse) — recover blogspot page URLs"
```

---

## Task 2: Wire the descramble into `ReadComicsScraper::parsePagesHtml`

**Files:**
- Modify: `src/core/manga/ReadComicsScraper.cpp:222-250` (`parsePagesHtml`)

- [ ] **Step 1: Replace the dead legacy parse**

Replace the body of `parsePagesHtml` (the `var pages=[...]` regex that returns empty on rcostation) with a delegation to the new pure unit. The `slug`/`issue` args are no longer needed for URL construction (the descrambled URLs are absolute blogspot URLs):

```cpp
QList<PageInfo> ReadComicsScraper::parsePagesHtml(const QString& html,
                                                  const QString& /*slug*/,
                                                  const QString& /*issue*/)
{
    // rcostation obfuscates the page-image list; recover it via the ported
    // gallery-dl descramble (ReadComicsPageParse). Legacy `var pages=[...]`
    // path is gone — it always returned empty on the live host.
    return tankoban::manga::readcomics::parseReaderPages(html);
}
```

Add the include at the top of `ReadComicsScraper.cpp`:

```cpp
#include "ReadComicsPageParse.h"
```

- [ ] **Step 2: Build-verify the main app**

Run: `set TANKOBAN_BUILD_LANE=agent1 && build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 3: Commit**

```bash
git add src/core/manga/ReadComicsScraper.cpp
git commit -m "feat(comics): ReadComicsScraper.parsePagesHtml uses RCO descramble (was dead)"
```

---

## Task 3: Route the Western edition download through `MangaDownloader`

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` (signal signature)
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp:2535-2556` (emit `sourceHref`)
- Modify: `src/ui/pages/ComicsPage.cpp:1003-1093` (`downloadWesternEditionRequested` handler)

- [ ] **Step 1: Add `sourceHref` to the download signal**

In `ComicsSeriesView.h`, change the signal:

```cpp
    void downloadWesternEditionRequested(int volumeNumber,
                                         const QString& editionTitle,
                                         const QString& tierLabel,
                                         const QString& sourceHref);
```

- [ ] **Step 2: Emit the edition href**

In `ComicsSeriesView.cpp` `populateSourcesForVolume()`, the rco branch already looks up the matching catalog volume — extend it to also grab `vol.sourceHref` and pass it:

```cpp
        QString editionTitle;
        QString tierLabel;
        QString sourceHref;
        for (const tankoban::manga::MangaVolume& vol : m_currentMangaCatalog.volumes) {
            if (vol.volumeNumber == volumeNumber) {
                editionTitle = vol.titleEnglish;
                tierLabel    = vol.groupingLabel;
                sourceHref   = vol.sourceHref;   // "/Comic/<slug>/<item>"
                break;
            }
        }
        if (m_sourcesPanel)
            m_sourcesPanel->setWesternDownloadStatus(QString(), tr("Finding..."));
        emit downloadWesternEditionRequested(volumeNumber, editionTitle, tierLabel, sourceHref);
        return;
```

- [ ] **Step 3: Route the handler to MangaDownloader**

In `ComicsPage.cpp` `wireWesternDownloader()`, change the `downloadWesternEditionRequested` lambda signature to accept `sourceHref` and replace the `m_westernDownloader->requestVolume(...)` call (keep the shelf-write + destPath logic above it) with a `MangaDownloader` dispatch. The chapterId is `sourceHref` with the leading `/Comic/` stripped:

```cpp
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::downloadWesternEditionRequested,
            this,
            [this](int volumeNumber, const QString& editionTitle,
                   const QString& tierLabel, const QString& sourceHref) {
        Q_UNUSED(tierLabel);
        if (!m_mangaDownloader || m_pendingWesternSeriesId.isEmpty() || sourceHref.isEmpty()) {
            qInfo("ComicsPage: Western download ignored — no downloader/series/href");
            if (m_tyVolumeSeriesView)
                m_tyVolumeSeriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
            return;
        }
        const QString seriesTitle = m_currentDetailSeriesTitle;
        const QJsonObject seriesJson = m_pendingWesternJson;
        const QString seriesId = m_pendingWesternSeriesId;

        // destPath: comics root + sanitized series (same as before)
        QString comicsRoot;
        if (m_torrentClient)
            comicsRoot = m_torrentClient->defaultPaths().value(QStringLiteral("comics"));
        if (comicsRoot.isEmpty())
            comicsRoot = QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                             .absoluteFilePath(QStringLiteral("../western_downloads"));
        QString safeTitle = seriesTitle;
        static const QRegularExpression kUnsafe(QStringLiteral("[\\\\/:*?\"<>|]"));
        safeTitle.replace(kUnsafe, QStringLiteral("_"));
        safeTitle = safeTitle.trimmed();
        if (safeTitle.isEmpty()) safeTitle = seriesId;
        const QString destPath = QDir(comicsRoot).absoluteFilePath(safeTitle);
        QDir().mkpath(destPath);

        // Auto-add to shelf (unchanged from 4f9d221).
        const QString dir = tankoban::manga::WesternCatalogLoader::canonicalDataDir();
        const QString shelfPath = QDir(dir).absoluteFilePath(seriesId + QStringLiteral(".json"));
        if (!seriesJson.isEmpty() && !QFile::exists(shelfPath)) {
            QDir().mkpath(dir);
            const QByteArray bytes = QJsonDocument(seriesJson).toJson(QJsonDocument::Indented);
            QSaveFile sf(shelfPath);
            if (sf.open(QIODevice::WriteOnly) && sf.write(bytes) == bytes.size() && sf.commit()) {
                if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->setWesternOnShelf(true);
                refreshWesternGrid();
            }
        }

        // chapterId = sourceHref minus "/Comic/"
        QString chapterId = sourceHref;
        if (chapterId.startsWith(QLatin1String("/Comic/")))
            chapterId.remove(0, QStringLiteral("/Comic/").size());

        tankoban::manga::ChapterInfo ch;
        ch.id     = chapterId;                 // "<slug>/<item>"
        ch.name   = editionTitle;
        ch.source = QStringLiteral("readcomicsonline");

        m_westernDownloadEdition = editionTitle;
        if (m_tyVolumeSeriesView)
            m_tyVolumeSeriesView->updateWesternDownloadStatus(editionTitle, tr("Downloading..."));

        qInfo("ComicsPage: RCO Western download — series=%s chapterId=%s dest=%s",
              qUtf8Printable(seriesId), qUtf8Printable(chapterId), qUtf8Printable(destPath));

        m_pendingWesternDownloadVolume = volumeNumber;   // for status routing (Step 4)
        m_mangaDownloader->startDownload(seriesTitle,
                                         QStringLiteral("readcomicsonline"),
                                         { ch }, destPath, QStringLiteral("cbz"));
    });
```

Add `int m_pendingWesternDownloadVolume = 0;` near `m_westernDownloadEdition` in `ComicsPage.h`.

- [ ] **Step 4: Build-verify**

Run: `set TANKOBAN_BUILD_LANE=agent1 && build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h src/ui/pages/comics/ComicsSeriesView.cpp src/ui/pages/ComicsPage.cpp src/ui/pages/ComicsPage.h
git commit -m "feat(comics): route Western edition download through MangaDownloader (RCO pages->cbz)"
```

---

## Task 4: Surface MangaDownloader status on the Western Sources panel + tile + index

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (`MangaDownloader` signal connects at ~349-353)

- [ ] **Step 1: Route download progress/completion to the Western UI**

The `MangaDownloader` signals (`downloadUpdated`, `downloadCompleted`) already connect to existing slots (lines 349-353). Extend the handlers so that when the active record's source is `"readcomicsonline"` and it matches the pending Western series, the Sources-panel status + volume tile update, and a completed cbz registers in `MangaDownloadIndex` (so the tile flips to **Read**). Add to the existing `downloadUpdated`/`downloadCompleted` lambdas (or the slots they call):

```cpp
    connect(m_mangaDownloader, &tankoban::manga::MangaDownloader::downloadUpdated, this,
            [this](const QString& recordId) {
        if (!m_mangaDownloader || !m_tyVolumeSeriesView) return;
        const auto recs = m_mangaDownloader->listActive();
        for (const auto& rec : recs) {
            if (rec.id != recordId || rec.source != QLatin1String("readcomicsonline")) continue;
            if (rec.seriesTitle != m_currentDetailSeriesTitle) return;
            // overall percent across the (single) chapter
            int pct = 0;
            if (!rec.chapters.isEmpty() && rec.chapters[0].totalImages > 0)
                pct = (rec.chapters[0].downloadedImages * 100) / rec.chapters[0].totalImages;
            const QString line = pct <= 0 ? tr("Finding...") : tr("Downloading %1%").arg(pct);
            m_tyVolumeSeriesView->updateWesternDownloadStatus(m_westernDownloadEdition, line);
            if (m_pendingWesternDownloadVolume > 0)
                m_tyVolumeSeriesView->setVolumeStatusText(m_pendingWesternDownloadVolume,
                    pct <= 0 ? QStringLiteral("Finding...") : QStringLiteral("%1%").arg(pct));
            return;
        }
    });

    connect(m_mangaDownloader, &tankoban::manga::MangaDownloader::downloadCompleted, this,
            [this](const QString& recordId) {
        if (!m_mangaDownloader || !m_tyVolumeSeriesView) return;
        for (const auto& rec : m_mangaDownloader->listActive()) {
            if (rec.id != recordId || rec.source != QLatin1String("readcomicsonline")) continue;
            if (rec.seriesTitle != m_currentDetailSeriesTitle) return;
            const QString cbz = rec.chapters.isEmpty() ? QString() : rec.chapters[0].finalPath;
            if (!cbz.isEmpty() && m_pendingWesternDownloadVolume > 0) {
                onProviderVolumeCompleted(m_pendingWesternSeriesId, m_pendingWesternDownloadVolume,
                    cbz, static_cast<int>(PendingVolumeSourceKind::WesternGetComics));
                m_tyVolumeSeriesView->updateWesternDownloadStatus(
                    m_westernDownloadEdition, tr("Downloaded - open to read"));
            } else {
                m_tyVolumeSeriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
            }
            return;
        }
    });
```

> NOTE: if `downloadUpdated`/`downloadCompleted` already have connected lambdas at lines 349-353, MERGE this logic into them rather than adding duplicate `connect`s (double-connect would double-fire). Read those lines first and fold the RCO branch in.

- [ ] **Step 2: Fix the RCO image Referer**

In `src/core/manga/MangaDownloader.cpp` (~line 647), the RCO Referer is hardcoded to the dead domain. Update it:

```cpp
        else if (source == "readcomicsonline")
            req.setRawHeader("Referer", "https://rcostation.xyz/");
```

(The descrambled blogspot URLs fetched in the spike with the reader-page referer; this aligns the Referer with the live host. If a smoke shows images fetch fine regardless, this is still harmless.)

- [ ] **Step 3: Build-verify**

Run: `set TANKOBAN_BUILD_LANE=agent1 && build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/ComicsPage.cpp src/core/manga/MangaDownloader.cpp
git commit -m "feat(comics): Western download status (RCO) -> Sources panel + tile + Read index"
```

---

## Task 5: End-to-end tankoctl smoke

**Files:** none (verification only).

- [ ] **Step 1: Launch the build with dev-control**

```
set TANKOBAN_BUILD_LANE=agent1
build_and_run.bat
```

- [ ] **Step 2: Drive the download (tankoctl needs Qt bin on PATH)**

```powershell
$env:PATH = "C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;" + $env:PATH
$tc = "$PWD\out_agent1\tankoctl.exe"
& $tc comics-open-western-series invincible
& $tc log-mark RCO_DL_SMOKE
& $tc comics-download-western-edition 1
# poll until terminal:
& $tc comics-get-western-download-state 1
```

- [ ] **Step 3: Verify the cbz is REAL (not HTML / not empty)**

Locate the downloaded `.cbz` under the comics dest folder and confirm it is a valid ZIP with multiple image entries:

```bash
f=$(find "/c/Users/Suprabha/Desktop/Media/Comics" -name "*.cbz" -newermt "-5 min" | head -1)
xxd "$f" | head -1          # expect "PK" (50 4b)
unzip -l "$f" | tail -3     # expect many .jpg entries, MBs total
```

Expected: `state` reaches `downloaded:true` with a real multi-MB `.cbz` containing the page JPEGs; the tile flips to Read; the Sources panel shows "Downloaded - open to read". Acceptance: opening the volume in the reader shows the actual comic pages.

- [ ] **Step 4: Stop the app**

Run: `scripts\stop-tankoban.ps1` (or `Stop-Process -Name Tankoban -Force`).

---

## Notes / Out of Scope

- **GetComics path** (`WesternVolumeDownloader`, `GetComicsResolver`, `GetComicsParse`) is now superseded for the actual fetch. Left in the tree (committed `4f9d221`) dormant; a later cleanup can retire it once RCO-as-source is smoked and trusted.
- **Fragility:** the descramble offsets are tied to `rguard.min.js` v1.5.8. If rcostation bumps the scheme, the frozen test vector (Task 1) will fail first — re-derive offsets from gallery-dl's `readcomiconline.py`. `readallcomics.com` (plain blogspot `<img>` URLs, no descramble) is the documented fallback source if this becomes untenable.
- **Multi-page editions:** a TPB/Compendium is one `ChapterInfo` → one `.cbz`. The collected-edition matching work in `4f9d221` (series-title search) is not needed for RCO (we use the exact edition href), so it stays GetComics-only.
