# Comics Western Downloads & Read — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax. Flat-on-master (no worktrees, gov-v13); commit per task, stage only the named files (shared tree).

**Goal:** Make a Western collected edition downloadable in place and readable — click an edition → resolve its GetComics post live → download (magnet via the existing torrent engine, or a direct link via a new in-app downloader) → register like a manga download → read in the existing comic reader.

**Architecture:** Three new bricks — `GetComicsParse` (pure: parse/match), `GetComicsResolver` (live HTTP search→match→post), `HttpFileDownloader` (stream a URL to disk), `WesternVolumeDownloader` (provider: resolve → magnet|DDL → emit `volumeCompleted`). Everything else reuses the manga download pipeline: `TorrentClient::addMagnetHeadless`, `MangaDownloadIndex`, the VolumeTile download state, and `ComicReader` (zero reader change).

**Tech Stack:** C++17, Qt6 (Core + Network; QtGui/Widgets only at the UI tasks), GoogleTest (`tankoban_tests`). NetSeam for QNAM.

**Spec:** `docs/superpowers/specs/2026-06-02-comics-western-downloads-design.md`

---

## File Structure

**Create:**
- `src/core/manga/GetComicsParse.h` / `.cpp` — pure parse + match (port of `scripts/comics_catalogue/getcomics_resolve.py` + the new search/match). No network, no UI.
- `tests/core/manga/GetComicsParseTest.cpp` — GoogleTest.
- `src/core/manga/GetComicsResolver.h` / `.cpp` — live HTTP (search → match → fetch post → emit `resolved(EditionDownload)`). Mirrors `ReadComicsScraper`.
- `src/core/net/HttpFileDownloader.h` / `.cpp` — stream a URL to a file, follow redirects.
- `src/core/manga/WesternVolumeDownloader.h` / `.cpp` — provider tying resolve + magnet/DDL together; manga-provider signal shape.
- `tests/core/manga/HttpFileDownloaderTest.cpp` — small redirect/write test (optional; smoke otherwise).

**Modify:**
- `cmake/TankobanSources.cmake` / `cmake/TankobanTests.cmake` — register new sources + tests.
- `src/core/manga/MangaCatalogTypes.h` — add a per-edition cover field to `MangaVolume`.
- `src/core/manga/WesternCatalogLoader.cpp` — read `editions[].cover`.
- `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` — un-gate Western edition click → download; render per-edition cover + download state on the tile.
- `src/ui/pages/ComicsPage.{h,cpp}` — own `WesternVolumeDownloader`; wire `volumeCompleted/Progress/Failed`; auto-add series on download.

---

## Task 1: GetComicsParse — pure parse + match unit (TDD)

Ports the proven `getcomics_resolve.py` (download-anchor extraction + priority) and adds the new search-result match scorer. Pure QtCore; fully unit-tested.

**Files:**
- Create: `src/core/manga/GetComicsParse.h`, `src/core/manga/GetComicsParse.cpp`
- Test: `tests/core/manga/GetComicsParseTest.cpp`
- Modify: `cmake/TankobanSources.cmake`, `cmake/TankobanTests.cmake`

- [ ] **Step 1: Header**

```cpp
// src/core/manga/GetComicsParse.h
#pragma once

#include <QString>
#include <QList>

// Pure parse/match helpers for GetComics (getcomics.org). C++ port of
// scripts/comics_catalogue/getcomics_resolve.py (extract_downloads + pick_best)
// plus the search-result match scorer that file did NOT have. No network, no UI
// — unit-tested in tankoban_tests. The live HTTP lives in GetComicsResolver.
namespace tankoban::manga::getcomics {

struct DownloadLink {
    QString kind;   // "magnet" | "main_server" | "pixeldrain" | "mediafire" | "mega"
    QString url;
};

struct SearchResult {
    QString title;     // candidate post title
    QString postUrl;   // absolute getcomics.org post URL
};

// Every real download anchor on a post, as {kind,url}. Ad links + non-download
// anchors dropped (kept only if magnet: scheme OR contains "getcomics.org/dls/").
QList<DownloadLink> extractDownloads(const QString& postHtml);

// Best link by priority magnet > main_server > pixeldrain > mediafire > mega.
// Returns an empty (url.isEmpty()) DownloadLink if none.
DownloadLink pickBest(const QList<DownloadLink>& links);

// The post's per-edition cover from <meta property="og:image" ...>, or "".
QString parsePostCover(const QString& postHtml);

// Parse a getcomics.org/?s= results page into {title, postUrl}, first-seen order.
QList<SearchResult> parseSearchResults(const QString& searchHtml);

// Confidence score (>=0) for a candidate post title vs the wanted edition.
// Higher = better. 0 means no meaningful overlap.
int scoreMatch(const QString& editionTitle, int year,
               const QString& tierLabel, const QString& candidateTitle);

// The best result whose score clears the confidence floor, or an empty
// SearchResult (postUrl.isEmpty()) if none is confident enough. FAIL SAFE:
// when unsure, returns empty rather than a wrong post.
SearchResult pickBestMatch(const QString& editionTitle, int year,
                           const QString& tierLabel,
                           const QList<SearchResult>& results);

} // namespace tankoban::manga::getcomics
```

- [ ] **Step 2: Failing test**

```cpp
// tests/core/manga/GetComicsParseTest.cpp
#include <gtest/gtest.h>
#include "core/manga/GetComicsParse.h"

using namespace tankoban::manga::getcomics;

TEST(GetComicsParse, ExtractDownloadsFiltersAndKinds) {
    const QString html = R"HTML(
        <a href="magnet:?xt=urn:btih:ABC">MAGNET Link</a>
        <a href="https://getcomics.org/dls/tok1">Main Server</a>
        <a href="https://getcomics.org/dls/tok2">Pixeldrain</a>
        <a href="https://craveu.example/ad">Hot Singles</a>
        <a href="https://getcomics.org/dls/tok3">MEGA</a>
    )HTML";
    const auto dls = extractDownloads(html);
    ASSERT_EQ(dls.size(), 4);                 // ad link dropped
    EXPECT_EQ(dls[0].kind, "magnet");
    EXPECT_EQ(dls[1].kind, "main_server");
    EXPECT_EQ(dls[2].kind, "pixeldrain");
    EXPECT_EQ(dls[3].kind, "mega");
}

TEST(GetComicsParse, PickBestPriority) {
    QList<DownloadLink> links = {
        {"mega", "u-mega"}, {"pixeldrain", "u-pd"}, {"magnet", "u-mag"}, {"main_server", "u-ms"}};
    EXPECT_EQ(pickBest(links).kind, "magnet");
    QList<DownloadLink> noMagnet = {{"mega", "u-mega"}, {"pixeldrain", "u-pd"}, {"main_server", "u-ms"}};
    EXPECT_EQ(pickBest(noMagnet).kind, "main_server");
    EXPECT_TRUE(pickBest({}).url.isEmpty());
}

TEST(GetComicsParse, ParsePostCover) {
    const QString html = R"HTML(<meta property="og:image" content="https://getcomics.org/x/Invincible-Compendium-1.jpg" />)HTML";
    EXPECT_EQ(parsePostCover(html), "https://getcomics.org/x/Invincible-Compendium-1.jpg");
    EXPECT_EQ(parsePostCover("<html>no og</html>"), "");
}

TEST(GetComicsParse, ScoreMatchPrefersRightTitleYearTier) {
    const QString wanted = "Invincible Compendium Vol. 1";
    const int year = 2011;
    const QString tier = "Compendium";
    const int good = scoreMatch(wanted, year, tier, "Invincible Compendium Vol 1 (2011)");
    const int wrongSeries = scoreMatch(wanted, year, tier, "Saga Compendium One (2019)");
    const int wrongTier = scoreMatch(wanted, year, tier, "Invincible TPB Vol 1 (2003)");
    EXPECT_GT(good, wrongSeries);
    EXPECT_GT(good, wrongTier);
    EXPECT_EQ(scoreMatch(wanted, year, tier, "Batman Year One"), 0);  // no overlap
}

TEST(GetComicsParse, PickBestMatchFailSafe) {
    QList<SearchResult> results = {
        {"Invincible Compendium Vol 1 (2011)", "https://getcomics.org/p/inv-comp-1"},
        {"Saga Compendium One", "https://getcomics.org/p/saga"}};
    EXPECT_EQ(pickBestMatch("Invincible Compendium Vol. 1", 2011, "Compendium", results).postUrl,
              "https://getcomics.org/p/inv-comp-1");
    // No confident match -> empty (fail safe).
    QList<SearchResult> junk = {{"Batman Year One", "u1"}, {"Spawn Origins", "u2"}};
    EXPECT_TRUE(pickBestMatch("Invincible Compendium Vol. 1", 2011, "Compendium", junk).postUrl.isEmpty());
}
```

- [ ] **Step 3: Register in CMake (both targets)**

In `cmake/TankobanSources.cmake`, next to the other `src/core/manga/*.cpp`:
```cmake
    src/core/manga/GetComicsParse.cpp
```
In `cmake/TankobanTests.cmake`, in the `add_executable(tankoban_tests ...)` list (near `WesternSeriesParse.cpp`):
```cmake
        src/core/manga/GetComicsParse.cpp
        tests/core/manga/GetComicsParseTest.cpp
```

- [ ] **Step 4: Run test → fail**

Run: `cmake -S . -B out -DTANKOBAN_BUILD_TESTS=ON && cmake --build out --target tankoban_tests`
Expected: FAIL (link error — no definitions yet).

- [ ] **Step 5: Implementation**

```cpp
// src/core/manga/GetComicsParse.cpp
#include "GetComicsParse.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace tankoban::manga::getcomics {
namespace {

QString classifyKind(const QString& href, const QString& text) {
    QString t = text;
    t.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    t = t.trimmed().toLower();
    if (href.startsWith(QLatin1String("magnet:"))) return QStringLiteral("magnet");
    if (t.contains(QLatin1String("main server"))) return QStringLiteral("main_server");
    if (t.contains(QLatin1String("pixeldrain")))  return QStringLiteral("pixeldrain");
    if (t.contains(QLatin1String("mediafire")))   return QStringLiteral("mediafire");
    if (t.contains(QLatin1String("mega")))        return QStringLiteral("mega");
    return QString();
}

// Lowercase alnum tokens, dropping common edition-noise words so the series
// name carries the match (year handled separately).
QStringList tokens(const QString& s) {
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QSet<QString> noise = {
        QStringLiteral("vol"), QStringLiteral("volume"), QStringLiteral("the"),
        QStringLiteral("edition"), QStringLiteral("collection")};
    QStringList out;
    for (const QString& w : s.toLower().split(nonAlnum, Qt::SkipEmptyParts))
        if (!noise.contains(w)) out.push_back(w);
    return out;
}

const QStringList& priority() {
    static const QStringList p = {QStringLiteral("magnet"), QStringLiteral("main_server"),
                                  QStringLiteral("pixeldrain"), QStringLiteral("mediafire"),
                                  QStringLiteral("mega")};
    return p;
}
} // namespace

QList<DownloadLink> extractDownloads(const QString& postHtml) {
    static const QRegularExpression anchor(
        QStringLiteral(R"(<a\s+[^>]*?href="([^"]+)"[^>]*>(.*?)</a>)"),
        QRegularExpression::DotMatchesEverythingOption);
    QList<DownloadLink> out;
    auto it = anchor.globalMatch(postHtml);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString href = m.captured(1);
        const QString kind = classifyKind(href, m.captured(2));
        if (kind.isEmpty()) continue;
        if (href.startsWith(QLatin1String("magnet:")) ||
            href.contains(QLatin1String("getcomics.org/dls/")))
            out.push_back({kind, href});
    }
    return out;
}

DownloadLink pickBest(const QList<DownloadLink>& links) {
    for (const QString& k : priority())
        for (const auto& d : links)
            if (d.kind == k) return d;
    return {};
}

QString parsePostCover(const QString& postHtml) {
    static const QRegularExpression og(
        QStringLiteral(R"(<meta\s+property="og:image"\s+content="([^"]+)")"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = og.match(postHtml);
    return m.hasMatch() ? m.captured(1) : QString();
}

QList<SearchResult> parseSearchResults(const QString& searchHtml) {
    // GetComics search results: each post is an <article> whose title links the
    // post: <h1 class="post-title"><a href="<postUrl>">Title</a></h1>. The exact
    // class is pinned against a captured fixture in Task 2 — adjust there if the
    // live markup differs. Deduped by postUrl, first-seen order.
    static const QRegularExpression row(
        QStringLiteral(R"(<h1[^>]*class="[^"]*post-title[^"]*"[^>]*>\s*<a\s+href="([^"]+)"[^>]*>(.*?)</a>)"),
        QRegularExpression::DotMatchesEverythingOption);
    QList<SearchResult> out;
    QSet<QString> seen;
    auto it = row.globalMatch(searchHtml);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString url = m.captured(1);
        if (seen.contains(url)) continue;
        seen.insert(url);
        QString title = m.captured(2);
        title.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
        out.push_back({title.trimmed(), url});
    }
    return out;
}

int scoreMatch(const QString& editionTitle, int year,
               const QString& tierLabel, const QString& candidateTitle) {
    const QStringList want = tokens(editionTitle);
    if (want.isEmpty()) return 0;
    const QString cand = candidateTitle.toLower();
    const QStringList candToks = tokens(candidateTitle);
    const QSet<QString> candSet(candToks.begin(), candToks.end());

    int shared = 0;
    for (const QString& w : want)
        if (candSet.contains(w)) ++shared;
    if (shared == 0) return 0;                       // no series overlap -> reject

    int score = shared * 10;
    if (year > 0 && cand.contains(QString::number(year))) score += 8;   // year match
    if (!tierLabel.isEmpty() && cand.contains(tierLabel.toLower())) score += 6;  // tier match
    return score;
}

SearchResult pickBestMatch(const QString& editionTitle, int year,
                           const QString& tierLabel,
                           const QList<SearchResult>& results) {
    // Confidence floor: at least half the edition's significant tokens must be
    // shared (encoded as score >= ceil(half)*10). Fail safe — empty if unsure.
    const int wantCount = tokens(editionTitle).size();
    if (wantCount == 0) return {};
    const int minShared = (wantCount + 1) / 2;
    const int floor = minShared * 10;

    SearchResult best;
    int bestScore = 0;
    for (const auto& r : results) {
        const int s = scoreMatch(editionTitle, year, tierLabel, r.title);
        if (s > bestScore) { bestScore = s; best = r; }
    }
    return (bestScore >= floor) ? best : SearchResult{};
}

} // namespace tankoban::manga::getcomics
```

- [ ] **Step 6: Run test → pass**

Run: `cmake --build out --target tankoban_tests && out\tankoban_tests.exe --gtest_filter=GetComicsParse.*`
Expected: all 5 cases PASS. READ the pass line.

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/GetComicsParse.h src/core/manga/GetComicsParse.cpp tests/core/manga/GetComicsParseTest.cpp cmake/TankobanSources.cmake cmake/TankobanTests.cmake
git commit -m "feat(comics): GetComicsParse pure unit (download extract + match scorer, TDD)"
```

---

## Task 2: GetComicsResolver — live search → match → post (HTTP)

Fetches live: search GetComics → `pickBestMatch` → fetch the post → assemble `EditionDownload {magnet/ddl links, cover}`. Mirrors `ReadComicsScraper` (QNAM + the file-local `makeRequest`). The `parseSearchResults` regex from Task 1 is verified against a captured fixture in Step 1.

**Files:**
- Create: `src/core/manga/GetComicsResolver.h`, `src/core/manga/GetComicsResolver.cpp`
- Modify: `cmake/TankobanSources.cmake`

- [ ] **Step 1: Capture a live search fixture + verify the results regex**

Fetch a real search page and a real post to pin the regex (the RCO scraper was built this way):
```bash
curl -sL "https://getcomics.org/?s=invincible+compendium" -o /tmp/gc_search.html
curl -sL "<one result post url>" -o /tmp/gc_post.html
```
Open `/tmp/gc_search.html`; confirm the post-title anchor markup matches Task 1's `parseSearchResults` regex (`<h1 class="post-title"><a href=...>`). If the live class differs (e.g. `entry-title`, or an `<article><a rel="bookmark">`), update the regex in `GetComicsParse.cpp` + its test fixture and re-run `--gtest_filter=GetComicsParse.*` until green. Commit any regex correction with Task 1's message amended note. (Do NOT guess — pin it to the captured HTML.)

- [ ] **Step 2: Header**

```cpp
// src/core/manga/GetComicsResolver.h
#pragma once

#include "GetComicsParse.h"
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace tankoban::manga {

// One resolved edition's downloads + cover, ready for WesternVolumeDownloader.
struct EditionDownload {
    QString postUrl;
    QString coverUrl;                       // per-edition og:image (may be empty)
    QList<getcomics::DownloadLink> links;   // ordered as found; pickBest applied by caller
    getcomics::DownloadLink best;           // pickBest(links)
};

// Live GetComics resolver: search -> fuzzy-match -> fetch post -> emit downloads.
// QNAM is created via NetSeam (non-owning here; caller passes the shared one).
class GetComicsResolver : public QObject {
    Q_OBJECT
public:
    explicit GetComicsResolver(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // editionTitle/year/tierLabel come from the Western edition (label + catalog
    // year + grouping tier). Emits resolved() on a confident match with a usable
    // download, else resolveFailed() (fail safe).
    void resolve(const QString& editionTitle, int year, const QString& tierLabel);

signals:
    void resolved(const EditionDownload& dl);
    void resolveFailed(const QString& reason);

private:
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga
```

- [ ] **Step 3: Implementation (mirror ReadComicsScraper's QNAM + lifetime-safe replies)**

Implement `resolve()`:
1. GET `https://getcomics.org/?s=<percent-encoded editionTitle>` (a `makeRequest`-style request with a desktop User-Agent, mirroring `ReadComicsScraper::makeRequest`). On `finished`:
2. `const auto results = getcomics::parseSearchResults(html);`
3. `const auto match = getcomics::pickBestMatch(editionTitle, year, tierLabel, results);`
4. If `match.postUrl.isEmpty()` → `emit resolveFailed("no confident match")`.
5. Else GET `match.postUrl`. On `finished`:
6. `EditionDownload dl; dl.postUrl = match.postUrl; dl.coverUrl = getcomics::parsePostCover(postHtml); dl.links = getcomics::extractDownloads(postHtml); dl.best = getcomics::pickBest(dl.links);`
7. If `dl.best.url.isEmpty()` → `emit resolveFailed("no usable download in post")`. Else `emit resolved(dl)`.

CRITICAL (Codex caught this in the search arc): on each reply, add a lifetime-independent cleanup so the reply can't leak if the resolver is destroyed mid-flight:
```cpp
auto* reply = m_nam->get(makeRequest(url));
connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);   // lifetime-independent
connect(reply, &QNetworkReply::finished, this, [this, reply, ...]() { ... });
```
Watch raw-string delimiters: any regex literal containing `)"` must use `R"rx(...)rx"` (bit Task 1 of the search arc). Avoid non-ASCII in any literal.

- [ ] **Step 4: Register source + build-verify**

Add `src/core/manga/GetComicsResolver.cpp` to `cmake/TankobanSources.cmake`. Run `build_check.bat` → confirm `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/GetComicsResolver.h src/core/manga/GetComicsResolver.cpp cmake/TankobanSources.cmake
git commit -m "feat(comics): GetComicsResolver — live search -> match -> post downloads"
```

---

## Task 3: HttpFileDownloader — stream a URL to disk

Generic streaming downloader for direct (DDL) links, following redirects (incl. the `getcomics.org/dls/<token>` gate). QNAM via NetSeam.

**Files:**
- Create: `src/core/net/HttpFileDownloader.h`, `src/core/net/HttpFileDownloader.cpp`
- Modify: `cmake/TankobanSources.cmake`

- [ ] **Step 1: Header**

```cpp
// src/core/net/HttpFileDownloader.h
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace tankoban::net {

// Streams an HTTP(S) URL to a file on disk, following redirects (incl. the
// getcomics.org/dls/<token> gate). Atomic: writes to <dest>.part then renames
// on success. One download per instance call; emits progress/finished/failed.
class HttpFileDownloader : public QObject {
    Q_OBJECT
public:
    explicit HttpFileDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    void start(const QString& url, const QString& destPath);
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void finished(const QString& path);
    void failed(const QString& reason);

private:
    QNetworkAccessManager* m_nam = nullptr;
    // reply/file held during an in-flight download.
};
```

- [ ] **Step 2: Implementation**

- Open `<destPath>.part` (QFile, WriteOnly|Truncate); fail → `emit failed`.
- `QNetworkRequest` with `setAttribute(RedirectPolicyAttribute, NoLessSafeRedirectPolicy)` (follows the dls redirect) + desktop UA.
- `reply->readyRead()` → append `reply->readAll()` to the file (streaming; don't buffer the whole file in memory).
- `reply->downloadProgress(recv,total)` → `emit progress`.
- `reply->finished()`: on error → remove `.part`, `emit failed(errorString)`. On success → close, atomically rename `.part` → `destPath` (remove existing dest first on Windows), `emit finished(destPath)`.
- `cancel()` → `reply->abort()` + remove `.part`.
- Lifetime: hold `reply` + `QFile` as members; `reply->deleteLater()` in the finished handler.

- [ ] **Step 3: Register + build-verify**

Add `src/core/net/HttpFileDownloader.cpp` to `cmake/TankobanSources.cmake`. Ensure `src/core/net/` is on the include path (it is — `${CMAKE_SOURCE_DIR}/src`). Run `build_check.bat` → `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/net/HttpFileDownloader.h src/core/net/HttpFileDownloader.cpp cmake/TankobanSources.cmake
git commit -m "feat(net): HttpFileDownloader — streaming URL->disk with redirect follow"
```

---

## Task 4: WesternVolumeDownloader — the provider

Ties resolve + download together; emits the manga-provider signal shape so `ComicsPage` wiring is reused.

**Files:**
- Create: `src/core/manga/WesternVolumeDownloader.h`, `src/core/manga/WesternVolumeDownloader.cpp`
- Modify: `cmake/TankobanSources.cmake`

- [ ] **Step 1: Header**

```cpp
// src/core/manga/WesternVolumeDownloader.h
#pragma once

#include "GetComicsResolver.h"
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class TorrentClient;

namespace tankoban::net { class HttpFileDownloader; }

namespace tankoban::manga {

// Western edition download provider. Mirrors TorrentVolumeProvider's signal
// shape so ComicsPage::onProviderVolumeCompleted reuses it verbatim.
class WesternVolumeDownloader : public QObject {
    Q_OBJECT
public:
    WesternVolumeDownloader(QNetworkAccessManager* nam, TorrentClient* torrent,
                            QObject* parent = nullptr);

    // editionTitle/year/tier come from the Western edition; destinationPath is
    // the per-series comics folder. coverUrl from resolve is forwarded via
    // coverReady so the UI can paint the per-edition cover.
    void requestVolume(const QString& seriesId, int volumeNumber,
                       const QString& editionTitle, int year,
                       const QString& tierLabel, const QString& destinationPath);

signals:
    void volumeProgress(const QString& seriesId, int volumeNumber, int percent);
    void volumeCompleted(const QString& seriesId, int volumeNumber, const QString& cbzPath);
    void volumeFailed(const QString& seriesId, int volumeNumber, const QString& reason);
    void coverReady(const QString& seriesId, int volumeNumber, const QString& coverUrl);

private:
    QNetworkAccessManager* m_nam = nullptr;
    TorrentClient* m_torrent = nullptr;
    GetComicsResolver m_resolver;
    // per-request state (seriesId, volumeNumber, destinationPath, infoHash) keyed
    // so resolve/download callbacks can correlate.
};

} // namespace tankoban::manga
```

- [ ] **Step 2: Implementation**

`requestVolume`:
1. `emit volumeProgress(seriesId, vol, 0)` ("Finding download…" state on the tile).
2. `m_resolver.resolve(editionTitle, year, tierLabel)` (one resolver; correlate via a per-request struct keyed by (seriesId,vol)).
3. On `resolveFailed` → `emit volumeFailed(seriesId, vol, reason)`.
4. On `resolved(dl)`:
   - `emit coverReady(seriesId, vol, dl.coverUrl)` if non-empty.
   - If `dl.best.kind == "magnet"` and `m_torrent`: `const QString infoHash = m_torrent->addMagnetHeadless(dl.best.url, "comics", destinationPath);` — then track completion via `TorrentClient` progress/complete signals for that infoHash; on complete, locate the `.cbz`/`.cbr` in `destinationPath` (split paths on `[\\/]`), `emit volumeCompleted(seriesId, vol, cbzPath)`. Wire `downloadProgress` → `emit volumeProgress`.
   - Else (no magnet) walk `dl.links` in `pickBest` priority order (skip already-tried), each through an `HttpFileDownloader` writing to `destinationPath/<sanitised edition>.cbz`; on `finished(path)` → `emit volumeCompleted`; on `failed` → try next link; all fail → `emit volumeFailed`.

Consult Task 13 of the spec / the ground-truth for the exact `TorrentClient` completion signals (`listActive()` / a per-infoHash completion signal). If `TorrentClient` exposes no per-infoHash "completed" signal, poll `downloadProgress(folderPath)` to 1.0 then locate the file. **Coordinate the exact signal with Agent 4 (spec §10) before finalizing this step.**

- [ ] **Step 3: Register + build-verify**

Add to `cmake/TankobanSources.cmake`. `build_check.bat` → `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/WesternVolumeDownloader.h src/core/manga/WesternVolumeDownloader.cpp cmake/TankobanSources.cmake
git commit -m "feat(comics): WesternVolumeDownloader — resolve + magnet/DDL provider"
```

---

## Task 5: Per-edition cover field (schema + loader + render)

Editions currently share the series hero cover. Add a per-edition cover that the resolver/harvester can fill.

**Files:**
- Modify: `src/core/manga/MangaCatalogTypes.h`, `src/core/manga/WesternCatalogLoader.cpp`, `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1:** In `MangaCatalogTypes.h`, add to `MangaVolume` an additive field `QString coverUrlEdition;` (per-edition cover; empty falls back to the shared series cover).
- [ ] **Step 2:** In `WesternCatalogLoader::loadFromJsonObject`, inside the editions loop, read `eo.value("cover").toString()` into `vol.coverUrlEdition` (absolutise a host-relative `/...` against `https://getcomics.org` the same way `seriesCover` is absolutised).
- [ ] **Step 3:** In `ComicsSeriesView::populateVolumeRowsFromCatalog`, set the VolumeTile cover to `vol.coverUrlEdition` when non-empty, else the existing `vol.coverUrlJapanese` (shared). (One line at the `data.coverUrl = ...` assignment.)
- [ ] **Step 4:** `build_check.bat` → `BUILD OK`. Commit: `feat(comics): per-edition cover field (editions[].cover) + render`.

---

## Task 6: Wire the download into ComicsPage + un-gate the Western click

**Files:**
- Modify: `src/ui/pages/ComicsPage.{h,cpp}`, `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 1:** In `ComicsPage.h`, add a `WesternVolumeDownloader* m_westernDownloader = nullptr;` member; construct it in the ctor with the shared NetSeam QNAM + the `TorrentClient` (set via the existing `setTorrentClient`; if the torrent client arrives later, construct/wire on first non-null `setTorrentClient`, mirroring the existing `TorrentVolumeProvider` lazy-wire).
- [ ] **Step 2:** Replace the Western gate in `ComicsSeriesView::populateSourcesForVolume` (the `showComingSoon()` early-return added in the search arc): for `source=="rco"`, instead emit a new `downloadWesternEditionRequested(volumeNumber, editionTitle, tierLabel)` signal (the edition title = the volume's `titleEnglish`, tier = `groupingLabel`; the catalog year is on `m_currentMangaCatalog.publishedYearStart`). Keep the tile-selection highlight.
- [ ] **Step 3:** In `ComicsPage`, connect `downloadWesternEditionRequested` → a slot that calls `m_westernDownloader->requestVolume(seriesId, vol, editionTitle, year, tier, destPath)` where `destPath = <comics-root>/<sanitised series title>/`. **Auto-add the series to the shelf first** (spec §8) by reusing the §6 add-to-shelf persistence (`m_pendingWesternJson` write) if not already shelved.
- [ ] **Step 4:** Wire the provider signals (mirror the existing manga provider wiring ~ComicsPage.cpp:408/941): `volumeCompleted` → `onProviderVolumeCompleted(seriesId, vol, cbzPath, /*sourceId*/"getcomics")`; `volumeProgress` → `ComicsSeriesView::setVolumeDownloadState`/the tile progress; `volumeFailed` → tile "No download found"/"Download failed"; `coverReady` → paint the per-edition cover on the tile + persist into the shelf JSON.
- [ ] **Step 5:** `build_check.bat` → `BUILD OK`. Commit: `feat(comics): wire Western edition download (click -> resolve -> download -> read)`.

---

## Task 7: End-to-end smoke

Agent-driven where mechanical; Hemanth visual gate for the in-place download UX + read. NOT a Hemanth-only task.

- [ ] **Step 1:** `build_and_run.bat` (kill running Tankoban first; verify exe mtime advanced).
- [ ] **Step 2: Happy path** — Comics → Western → search a title with collected editions (Invincible) → open → click an edition's **Download** → tile shows Finding → Downloading % → **Read** → click Read → ComicReader opens the pages. Capture a screenshot.
- [ ] **Step 3: Magnet + DDL** — verify at least one edition downloads via magnet (torrent) and one via DDL (no-magnet post) end-to-end.
- [ ] **Step 4: Fail-safe edges** — an edition with no confident GetComics match → "No download found" (no wrong file); an editionless series (Walking Dead) → no Download affordance / nothing to download. Auto-add: download a live-searched (unshelved) series → it appears on the Western shelf.
- [ ] **Step 5:** Cleanup `scripts/stop-tankoban.ps1`. Record smoke result (smoke-report skill).

---

## Self-Review

**Spec coverage:**
- §3.1 live-at-click → Task 2 `resolve()`. ✔
- §3.2 magnet + in-app DDL → Task 4 (magnet→TorrentClient; DDL→HttpFileDownloader Task 3). ✔
- §3.3 in-place Netflix UX → Task 6 (tile state, reuse onProviderVolumeCompleted + ComicReader). ✔
- §3.4 per-edition covers → Task 5 + Task 6 Step 4 (coverReady). ✔
- §5 matching fail-safe → Task 1 `pickBestMatch` floor + Task 2 resolveFailed. ✔
- §8 auto-add on download → Task 6 Step 3. ✔ edge cases (no match, DDL fallback, idempotent) → Tasks 4 + 7. ✔
- §6 reuse (index/reader/tile) → Task 6 Step 4. ✔
- §10 Agent 4 coordination → Task 4 Step 2 note (confirm completion signal + "comics" category). ✔

**Placeholder scan:** Task 4 Step 2 defers the exact `TorrentClient` completion-signal name to ground-truth + Agent 4 — this is a real cross-domain dependency, flagged explicitly (not a logic placeholder); the implementer reads `TorrentClient.h` + confirms with Agent 4. Task 2 Step 1 captures the search fixture before pinning `parseSearchResults` (the live markup is the source of truth) — a deliberate verify step, not a guess.

**Type consistency:** `EditionDownload`/`DownloadLink`/`SearchResult` defined Task 1/2, used Tasks 2/4. `getcomics::pickBestMatch`/`extractDownloads`/`pickBest`/`parsePostCover`/`parseSearchResults`/`scoreMatch` defined Task 1, used Task 2. `WesternVolumeDownloader::volumeCompleted` (Task 4) consumed by `onProviderVolumeCompleted` (existing). `coverUrlEdition` (Task 5) used Task 6. `sourceId="getcomics"` consistent. ✔

**Engine routing:** Task 1 (pure TDD) + Tasks 3/5 (templated) are DeepSeek/Agent 9-shaped; Task 1 match scorer + Task 4 provider (disk writes + shared torrent engine) get Codex review; Task 4's torrent signal reviewed WITH Agent 4. Opus owns plan + matching-heuristic tuning + final merge. Reviewer pass before master.
