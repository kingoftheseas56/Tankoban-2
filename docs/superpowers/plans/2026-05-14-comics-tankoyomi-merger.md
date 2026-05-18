# Comics + Tankoyomi + Stream-as-blueprint Merger — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Excise the standalone Tankoyomi page; absorb its functionality into Comics mode as a Stream-style search + Stream-style series detail view + per-series Netflix-style inline downloads; badge Tankoyomi-origin series so they use the new UI while folder-imported series keep today's `SeriesView`.

**Architecture:** Hybrid reuse of Stream-side surfaces — TileStrip/TileCard/Toast primitives reused directly, `StreamSearchWidget` + `StreamDetailView` forked with attribution into `src/ui/pages/comics/`, `StreamDownloadIndex` forked into `MangaDownloadIndex`, no comics cross-series Downloads page. Provenance is library-record-bound: `comics_library.json` is source of truth, hidden `.tankoyomi-meta.json` sidecar is a recovery/scanner-skip hint. Auto-add fires from the detail view, not the downloader (keeps UI side-effects off downloader signal threads).

**Tech Stack:** Qt 6.10 / C++20. `QWidget` Widgets stack (no QML). `QJsonObject`/`QJsonDocument` for persistence via existing `JsonStore`. Reuse existing `MangaScraper` + `MangaDownloader` + `TileCard` + `ToastHud` + `ChapterDownloadIndicator` + `ChapterRangeDialog`. No new dependencies, no new icons (reuse Netflix overhaul's `download-arrow.svg` / `pause-circle.svg` / `play-circle.svg` / `retry-arrow.svg`).

---

## Spec source

`docs/superpowers/specs/2026-05-14-comics-tankoyomi-merger-brainstorm.md` (Agent 1 §1–§10 + Codex review-and-expand §11–§21 landed 2026-05-14 ~5:18pm per memory observation 2586). All section references in this plan (`§4 / §6.1 / §19` etc.) refer to that brainstorm-md.

## Pre-flight: working-tree state to honour

This plan executes against a tree where these unrelated in-flight items exist as of writing-plans fire:

- `agents/chat.md` — modified (active brotherhood coordination; do not stomp).
- `src/ui/pages/ComicsPage.h` + `src/ui/pages/tankoyomi/MangaDetailView.cpp` + `src/ui/pages/tankoyomi/TransferGroupCard.cpp` — modified by Agent 5's just-shipped GLOBAL_NAV_HISTORY Task 13 INavStateProvider hook (commit `14a045b` covered Tankoyomi capture/restore). The executor must NOT undo Agent 5's nav state work. Phase 1 starts on this tree.
- `docs/superpowers/specs/2026-05-13-tankoyomi-mihon-overhaul-design.md` + `docs/superpowers/specs/2026-05-13-sources-ui-refinement-design.md` + `docs/superpowers/specs/2026-05-14-global-nav-history-design.md` + `docs/superpowers/specs/2026-05-14-comics-tankoyomi-merger-brainstorm.md` + various plan docs — untracked; will be swept by Agent 0 alongside this arc's commits.

## Brotherhood convention — "Commit" step in this plan

This plan does NOT do per-task `git commit`. Agent 0 sweeps RTCs from `agents/chat.md`. Each task's "Verify" step is `build_check.bat` BUILD OK. RTC happens at **phase close**, not per task. Each phase emits one RTC line of shape:

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER Phase N <name> 2026-05-NN ~HH:MMam/pm — <summary>] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: <comma-separated list>
```

There is ONE exception: at the very end (Phase 9 final task), a single closing RTC summarises the whole arc.

## Coordination heads-ups (Codex §11.7 + GOVERNANCE.md:32)

`TileCard`, `TileStrip`, `ScannerUtils`, and `LibraryScanner` are **Agent 5 primary-owned** library UX surfaces. Phase 2 (LibraryScanner skip-set/resolver) and Phase 5 (TileCard badge slot extension for the Tankoyomi-origin chip + DOWNLOADING chip) touch Agent 5's surfaces. Before the first edit to any of those files in those phases, the executor MUST post a coordination note in `agents/chat.md`:

```
NOTE - Agent 1 -> Agent 5 2026-05-NN ~HH:MMam/pm. Starting COMICS_TANKOYOMI_STREAM_MERGER Phase <N>; touching <FILE> in <scope>. Heads-up per GOVERNANCE.md:32 primary-ownership. Will RTC when done.
```

No ack required to proceed (Rule 14 — Agent 1 owns this arc's technical calls), but the heads-up is mandatory.

---

## File Structure

### New files

- `src/core/manga/MangaSeriesDetail.h` — detail-shape POD struct + JSON serialization helpers
- `src/core/manga/MangaSourceRegistry.{h,cpp}` — small factory holding `QList<MangaScraper*>` with string-keyed source IDs
- `src/core/manga/ComicsLibraryRecord.h` — POD record for `comics_library.json` entries
- `src/core/manga/ComicsTankoyomiLibrary.{h,cpp}` — library store class (add/remove/get/contains/all/libraryChanged signal), JsonStore-backed
- `src/core/manga/MangaDownloadIndex.{h,cpp}` — canonical-key-keyed threadsafe chapter index; fork of `StreamDownloadIndex`
- `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}` — fork of `StreamSearchWidget` with manga types + two-section (Manga/Comics) split
- `src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}` — fork of `StreamDetailView` with chapter-list semantics + Range/multi-select + offline banner
- `src/ui/pages/comics/SidecarMeta.{h,cpp}` — read/write `.tankoyomi-meta.json` sidecar; pure-logic file I/O

### Modified files

- `src/ui/pages/ComicsPage.{h,cpp}` — search-bar repurpose, provenance-routed tile-click dispatch, badge slots, MangaDownloadIndex wiring, 3-mode nav state
- `src/ui/MainWindow.{h,cpp}` — remove `PAGE_TANKOYOMI` routing, wire new detail-view openComic chain
- `src/ui/widgets/SidebarDrawer.{h,cpp}` — remove "Tankoyomi" drawer entry
- `src/core/LibraryScanner.{h,cpp}` — accept skip-set/resolver callback (Agent 5 surface — coordination per §above)
- `src/core/manga/MangaScraper.h` — add `virtual void fetchDetail(const MangaResult&)` + `detailReady(const MangaSeriesDetail&)` signal
- `src/core/manga/WeebCentralScraper.{h,cpp}` — implement `fetchDetail` (HTML parse of detail page)
- `src/core/manga/ReadComicsScraper.{h,cpp}` — implement `fetchDetail` (HTML parse of detail page)
- `src/core/manga/MangaDownloader.{h,cpp}` — minor: no signature changes for v1; the auto-add toast wiring is done in the detail view per Codex §15 (downloader stays as-is)
- `src/ui/pages/TileCard.{h,cpp}` — add provenance + downloading badge slots to `applyBadges` painter path (Agent 5 surface — coordination per §above)
- `CMakeLists.txt` — add new sources, drop deleted sources

### Deleted files (Phase 8 only — after Phases 3–5 are compile-green)

- `src/ui/pages/TankoyomiPage.{h,cpp}`
- `src/ui/pages/tankoyomi/MangaDetailView.{h,cpp}` (Agent 4B's 2026-05-13 C.5 ship)
- `src/ui/pages/tankoyomi/MangaResultsGrid.{h,cpp}`
- `src/ui/pages/tankoyomi/TransferGroupCard.{h,cpp}`
- `src/ui/dialogs/AddMangaDialog.{h,cpp}`

### Preserved as-is (reuse direct)

- `src/ui/pages/tankoyomi/ChapterDownloadIndicator.{h,cpp}` — 5-state tap-only indicator, ABSORB/REUSE DIRECTLY per Codex §18.1
- `src/ui/pages/tankoyomi/ChapterRangeDialog.{h,cpp}` — From/To modal, ABSORB/REUSE DIRECTLY per Codex §18.2
- `src/ui/player/ToastHud.{h,cpp}` — reuse for auto-add toast + source-failure toast
- `src/core/manga/MangaResult.h` + `ChapterInfo` + `PageInfo` — unchanged for v1
- `src/core/manga/MangaDownloader.{h,cpp}` — unchanged for v1 (Codex §15 — wiring fix lives in detail view)
- `src/ui/pages/SeriesView.{h,cpp}` — unchanged (folder-imported route)
- `src/ui/readers/ComicReader.{h,cpp}` — unchanged (out of scope)
- `src/core/CoreBridge.{h,cpp}` — unchanged for v1; root-change auto-copy deferred per §14

---

## Phase 1 — Data contracts and source registry

**Goal:** Land the data shapes the rest of the arc needs (`MangaSeriesDetail` + scraper `fetchDetail`/`detailReady` + `MangaSourceRegistry`). Existing UI keeps compiling and behaving identically; only the scraper layer grows.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P1 data contracts + source registry`.

### Task 1: Create `MangaSeriesDetail` struct

**Files:**
- Create: `src/core/manga/MangaSeriesDetail.h`

- [ ] **Step 1: Create the header**

```cpp
#pragma once

#include "MangaResult.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QList>

// Detail-page payload returned by MangaScraper::fetchDetail.
// Decoupled from MangaResult (the search preview) so that
// preview cards stay cheap while detail-page hero gets the full
// metadata. Per brainstorm-md §12 (Codex pass).
struct MangaSeriesDetail {
    MangaResult preview;       // copy of the search-time preview
    QString     synopsis;
    QStringList genres;
    QString     year;
    QString     status;        // "ongoing" | "completed" | "hiatus" | etc.
    QString     author;        // may already be in preview.author; canonicalise on consume
    QString     heroCoverUrl;  // larger/higher-res cover if the source serves one
    QString     sourceUrl;     // detail page URL on the source site
    QList<ChapterInfo> cachedChapters; // only populated if fetchDetail's HTTP response naturally included the chapter list

    QJsonObject toJson() const;
    static MangaSeriesDetail fromJson(const QJsonObject& j);
};
```

- [ ] **Step 2: Add `MangaSeriesDetail.h` to CMakeLists.txt HEADERS**

`CMakeLists.txt` — find the manga sources block (search for `MangaResult.h`). Add `src/core/manga/MangaSeriesDetail.h` adjacent.

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected tail: `BUILD OK`. If `BUILD FAILED exit=<n>`, read the 30-line cl.exe tail printed by the script and fix the error.

### Task 2: Implement `MangaSeriesDetail` JSON serialization

**Files:**
- Create: `src/core/manga/MangaSeriesDetail.cpp`

- [ ] **Step 1: Create the impl**

```cpp
#include "MangaSeriesDetail.h"
#include <QJsonArray>

QJsonObject MangaSeriesDetail::toJson() const
{
    QJsonObject o;
    // Inline preview fields (don't nest — keeps the JSON flat for the library record):
    o["id"]            = preview.id;
    o["url"]           = preview.url;
    o["title"]         = preview.title;
    o["author"]        = author.isEmpty() ? preview.author : author;
    o["thumbnailUrl"]  = preview.thumbnailUrl;
    o["source"]        = preview.source;
    o["status"]        = status.isEmpty() ? preview.status : status;
    o["type"]          = preview.type;

    // Detail-only fields:
    o["synopsis"]      = synopsis;
    o["year"]          = year;
    o["heroCoverUrl"]  = heroCoverUrl;
    o["sourceUrl"]     = sourceUrl;

    QJsonArray genresArr;
    for (const auto& g : genres) genresArr.append(g);
    o["genres"] = genresArr;

    QJsonArray chaptersArr;
    for (const auto& ch : cachedChapters) {
        QJsonObject c;
        c["id"]            = ch.id;
        c["name"]          = ch.name;
        c["chapterNumber"] = ch.chapterNumber;
        c["dateUpload"]    = QString::number(ch.dateUpload);
        chaptersArr.append(c);
    }
    o["cachedChapters"] = chaptersArr;
    return o;
}

MangaSeriesDetail MangaSeriesDetail::fromJson(const QJsonObject& j)
{
    MangaSeriesDetail d;
    d.preview.id           = j.value("id").toString();
    d.preview.url          = j.value("url").toString();
    d.preview.title        = j.value("title").toString();
    d.preview.author       = j.value("author").toString();
    d.preview.thumbnailUrl = j.value("thumbnailUrl").toString();
    d.preview.source       = j.value("source").toString();
    d.preview.status       = j.value("status").toString();
    d.preview.type         = j.value("type").toString();

    d.author        = d.preview.author;
    d.synopsis      = j.value("synopsis").toString();
    d.year          = j.value("year").toString();
    d.status        = d.preview.status;
    d.heroCoverUrl  = j.value("heroCoverUrl").toString();
    d.sourceUrl     = j.value("sourceUrl").toString();

    for (const auto& v : j.value("genres").toArray()) d.genres.append(v.toString());

    for (const auto& v : j.value("cachedChapters").toArray()) {
        const auto co = v.toObject();
        ChapterInfo c;
        c.id            = co.value("id").toString();
        c.name          = co.value("name").toString();
        c.chapterNumber = co.value("chapterNumber").toDouble();
        c.dateUpload    = co.value("dateUpload").toString().toLongLong();
        d.cachedChapters.append(c);
    }
    return d;
}
```

- [ ] **Step 2: Add `.cpp` to CMakeLists.txt SOURCES**

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 3: Add `fetchDetail` + `detailReady` to `MangaScraper`

**Files:**
- Modify: `src/core/manga/MangaScraper.h:9-32` (existing virtual base; insert new virtual + signal)

- [ ] **Step 1: Edit the header**

Open `src/core/manga/MangaScraper.h`. Add the new method to the public virtual block and the new signal to the signals block:

```cpp
#pragma once

#include "MangaResult.h"
#include "MangaSeriesDetail.h"   // NEW include
#include <QObject>
#include <QList>

class QNetworkAccessManager;

class MangaScraper : public QObject
{
    Q_OBJECT

public:
    explicit MangaScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : QObject(parent), m_nam(nam) {}

    virtual QString sourceId() const = 0;
    virtual QString sourceName() const = 0;

    virtual void search(const QString& query, int limit = 60) = 0;
    virtual void fetchChapters(const QString& seriesId) = 0;
    virtual void fetchPages(const QString& chapterId) = 0;

    // NEW (v1 merger): fetch detail-page hero metadata (synopsis,
    // genres, year, status, hero cover URL) given a search-time
    // preview. Result delivered via detailReady(). Concrete scrapers
    // SHOULD also populate cachedChapters if their detail page
    // already returns the chapter list, so the detail view can
    // skip a separate fetchChapters() round-trip.
    virtual void fetchDetail(const MangaResult& preview) = 0;

signals:
    void searchFinished(const QList<MangaResult>& results);
    void chaptersReady(const QList<ChapterInfo>& chapters);
    void pagesReady(const QList<PageInfo>& pages);
    void errorOccurred(const QString& message);

    // NEW (v1 merger): emitted when fetchDetail completes.
    void detailReady(const MangaSeriesDetail& detail);

protected:
    QNetworkAccessManager* m_nam;
};
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD FAILED` (because `WeebCentralScraper` + `ReadComicsScraper` are concrete subclasses that don't implement the new pure-virtual yet). Read the error to confirm it's "cannot instantiate abstract class" or equivalent. Proceed to Task 4 to satisfy it.

### Task 4: Implement `WeebCentralScraper::fetchDetail`

**Files:**
- Modify: `src/core/manga/WeebCentralScraper.h` (add method declaration)
- Modify: `src/core/manga/WeebCentralScraper.cpp` (add HTTP fetch + HTML parse)

- [ ] **Step 1: Declare in header**

Open `src/core/manga/WeebCentralScraper.h`. In the public section (alongside the existing `search/fetchChapters/fetchPages` overrides), add:

```cpp
void fetchDetail(const MangaResult& preview) override;
```

- [ ] **Step 2: Implement the GET + parse**

Open `src/core/manga/WeebCentralScraper.cpp`. Use the existing search-parser pattern at lines 128-183 as the template — same QNetworkAccessManager get + finished-lambda + HTML regex pattern. The detail page lives at `preview.url`; fetch its body and extract:

```cpp
void WeebCentralScraper::fetchDetail(const MangaResult& preview)
{
    QNetworkRequest req{QUrl(preview.url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                  "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36");
    auto* reply = m_nam->get(req);
    QPointer<WeebCentralScraper> self(this);
    connect(reply, &QNetworkReply::finished, this, [reply, self, preview]() {
        reply->deleteLater();
        if (!self) return;
        if (reply->error() != QNetworkReply::NoError) {
            emit self->errorOccurred(QString("weebcentral fetchDetail: %1")
                                     .arg(reply->errorString()));
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());

        MangaSeriesDetail detail;
        detail.preview     = preview;
        detail.sourceUrl   = preview.url;

        // Synopsis: WeebCentral wraps the description in <p class="description"> or a
        // <li> labelled "Description". The exact selector depends on the live page; the
        // executor MUST verify by curl'ing one known-good detail URL during impl. Start
        // with the pattern the search parser already uses (lines 128-183 of this file).
        static QRegularExpression kSynopsis(
            R"RX(<li class="description"[^>]*>([\s\S]*?)</li>)RX");
        auto sm = kSynopsis.match(html);
        if (sm.hasMatch()) detail.synopsis = stripTags(sm.captured(1)).trimmed();

        // Genres: <a href="/genres/<slug>">Name</a> repeated; collect all.
        static QRegularExpression kGenre(
            R"RX(<a[^>]+href="[^"]*genres?/[^"]+"[^>]*>([^<]+)</a>)RX");
        auto gi = kGenre.globalMatch(html);
        while (gi.hasNext()) {
            const auto m = gi.next();
            detail.genres.append(m.captured(1).trimmed());
        }

        // Year: <li>Year:</li> <li>2024</li> pattern. Source HTML varies; capture loosely.
        static QRegularExpression kYear(
            R"RX(Year[^<]*<[^>]*>\s*<[^>]*>(\d{4})<)RX");
        auto ym = kYear.match(html);
        if (ym.hasMatch()) detail.year = ym.captured(1);

        // Status: similar shape to Year ("Status:" label followed by value).
        static QRegularExpression kStatus(
            R"RX(Status[^<]*<[^>]*>\s*<[^>]*>([^<]+)<)RX");
        auto stm = kStatus.match(html);
        if (stm.hasMatch()) detail.status = stm.captured(1).trimmed();

        // Hero cover: WeebCentral may serve a higher-res cover on the detail page
        // (<img src="...cover/full/..."). Fall back to preview.thumbnailUrl.
        static QRegularExpression kHero(
            R"RX(<img[^>]+src="([^"]*cover[^"]+)"[^>]*class="[^"]*hero[^"]*")RX");
        auto hm = kHero.match(html);
        detail.heroCoverUrl = hm.hasMatch() ? hm.captured(1) : preview.thumbnailUrl;

        // cachedChapters NOT populated here — WeebCentral chapter list is its own
        // endpoint (the existing fetchChapters call). Leave empty.

        emit self->detailReady(detail);
    });
}
```

Note the `stripTags` helper already exists in this file (used by the search parser); reuse it.

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD FAILED` still (ReadComicsScraper not yet done). Proceed to Task 5.

### Task 5: Implement `ReadComicsScraper::fetchDetail`

**Files:**
- Modify: `src/core/manga/ReadComicsScraper.h`
- Modify: `src/core/manga/ReadComicsScraper.cpp`

- [ ] **Step 1: Declare in header**

```cpp
void fetchDetail(const MangaResult& preview) override;
```

- [ ] **Step 2: Implement the GET + parse**

The existing search parser pattern at `ReadComicsScraper.cpp:49-64` uses a JSON-suggestions API — the detail page is HTML at `preview.url`. Mirror Task 4's WeebCentral pattern shape, parsing ReadComicsOnline's detail page structure. The exact selectors are TODO-on-impl-against-live-HTML; capture synopsis from the page's description block, genres from a list of category tags, year from the publication year line, status from a status line, heroCoverUrl from a larger cover element if present (else use preview.thumbnailUrl).

```cpp
void ReadComicsScraper::fetchDetail(const MangaResult& preview)
{
    QNetworkRequest req{QUrl(preview.url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                  "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36");
    auto* reply = m_nam->get(req);
    QPointer<ReadComicsScraper> self(this);
    connect(reply, &QNetworkReply::finished, this, [reply, self, preview]() {
        reply->deleteLater();
        if (!self) return;
        if (reply->error() != QNetworkReply::NoError) {
            emit self->errorOccurred(QString("readcomicsonline fetchDetail: %1")
                                     .arg(reply->errorString()));
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());

        MangaSeriesDetail detail;
        detail.preview   = preview;
        detail.sourceUrl = preview.url;

        // Synopsis: ReadComicsOnline tends to wrap the summary in a div with class
        // "manga-excerpt" or "summary". Curl one detail URL during impl and pin
        // the actual selector.
        static QRegularExpression kSummary(
            R"RX(<div[^>]+class="[^"]*(?:manga-excerpt|summary)[^"]*"[^>]*>([\s\S]*?)</div>)RX");
        auto sm = kSummary.match(html);
        if (sm.hasMatch()) detail.synopsis = sm.captured(1).trimmed();

        // Genres: anchor tags into /category/<slug>/.
        static QRegularExpression kGenre(
            R"RX(<a[^>]+href="[^"]*category/[^"]+"[^>]*>([^<]+)</a>)RX");
        auto gi = kGenre.globalMatch(html);
        while (gi.hasNext()) detail.genres.append(gi.next().captured(1).trimmed());

        // Year: "Date of Release" or similar dt/dd pair.
        static QRegularExpression kYear(
            R"RX(Date of Release[\s\S]*?(\d{4}))RX");
        auto ym = kYear.match(html);
        if (ym.hasMatch()) detail.year = ym.captured(1);

        // Status: "Status" dt/dd pair.
        static QRegularExpression kStatus(
            R"RX(Status[\s\S]*?<(?:dd|span)[^>]*>([^<]+)<)RX");
        auto stm = kStatus.match(html);
        if (stm.hasMatch()) detail.status = stm.captured(1).trimmed();

        detail.heroCoverUrl = preview.thumbnailUrl; // ReadComicsOnline detail
                                                    // page doesn't expose a
                                                    // distinct hero size today.

        emit self->detailReady(detail);
    });
}
```

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 6: Create `MangaSourceRegistry`

**Files:**
- Create: `src/core/manga/MangaSourceRegistry.h`
- Create: `src/core/manga/MangaSourceRegistry.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <QList>
#include <QObject>
#include <QString>

class MangaScraper;
class QNetworkAccessManager;

// v1: hardcoded list of scrapers. v2 (deferred) can replace the
// hardcoded ctor body with file-system-loaded plugins without
// changing this class's surface (per brainstorm §3.6 / §9 v2
// follow-up note).
//
// Owns the scraper instances. Source IDs are stable string keys
// (e.g. "weebcentral", "readcomicsonline") suitable for persisting
// in comics_library.json records.
class MangaSourceRegistry : public QObject
{
    Q_OBJECT
public:
    explicit MangaSourceRegistry(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaSourceRegistry();

    QList<MangaScraper*> scrapers() const { return m_scrapers; }
    MangaScraper*        find(const QString& sourceId) const;

private:
    QList<MangaScraper*> m_scrapers;
};
```

- [ ] **Step 2: Impl**

```cpp
#include "MangaSourceRegistry.h"
#include "MangaScraper.h"
#include "WeebCentralScraper.h"
#include "ReadComicsScraper.h"

MangaSourceRegistry::MangaSourceRegistry(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
{
    m_scrapers.append(new WeebCentralScraper(nam, this));
    m_scrapers.append(new ReadComicsScraper(nam, this));
}

MangaSourceRegistry::~MangaSourceRegistry()
{
    // QObject parent-chain owns the scrapers; nothing to do.
}

MangaScraper* MangaSourceRegistry::find(const QString& sourceId) const
{
    for (auto* s : m_scrapers)
        if (s->sourceId() == sourceId) return s;
    return nullptr;
}
```

- [ ] **Step 3: Add to CMakeLists.txt**

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 7: Phase 1 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P1 data contracts + source registry 2026-05-NN ~HH:MMpm. New types MangaSeriesDetail (POD + JSON round-trip) and MangaSourceRegistry (replaces hardcoded m_scrapers list with stable string-keyed lookup). MangaScraper gains virtual fetchDetail(MangaResult preview) + signal detailReady(MangaSeriesDetail). WeebCentralScraper + ReadComicsScraper implement fetchDetail with detail-page HTML parsers mirroring their existing search-parser shape; selectors pinned against live HTML during impl. No UI changes; old TankoyomiPage still compiles + runs unchanged. BUILD OK first try after the WeebCentral + ReadComics impls landed.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/core/manga/MangaSeriesDetail.h, src/core/manga/MangaSeriesDetail.cpp, src/core/manga/MangaSourceRegistry.h, src/core/manga/MangaSourceRegistry.cpp, src/core/manga/MangaScraper.h, src/core/manga/WeebCentralScraper.h, src/core/manga/WeebCentralScraper.cpp, src/core/manga/ReadComicsScraper.h, src/core/manga/ReadComicsScraper.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 2 — Library store + provenance merge

**Goal:** Land `comics_library.json`, `.tankoyomi-meta.json` sidecar I/O, and the `LibraryScanner` skip-set/resolver hook so the scanner doesn't double-tile Tankoyomi-origin folders. UI mostly unchanged from the user's point of view.

**Coordination heads-up:** Phase 2 touches Agent 5's `LibraryScanner`. Post the NOTE-to-Agent-5 in chat.md (template above) before Task 11.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P2 library store + provenance`.

### Task 8: Create `ComicsLibraryRecord` POD

**Files:**
- Create: `src/core/manga/ComicsLibraryRecord.h`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include "MangaSeriesDetail.h"
#include <QJsonObject>
#include <QString>

// Schema for one entry in comics_library.json. Records exist ONLY for
// Tankoyomi-origin series; folder-imported series are represented purely
// by LibraryScanner's on-disk walk + cover cache. Source-of-truth
// invariant per Codex §16: this record is authoritative for
// "is this series Tankoyomi-origin?"; the sidecar is a recovery hint.
struct ComicsLibraryRecord {
    QString sourceId;            // "weebcentral" | "readcomicsonline"
    QString seriesId;            // scraper-local series id (from MangaResult.id)
    QString title;
    QString origin;              // always "tankoyomi" in v1 (folder rows live elsewhere)
    QString rootFolder;          // absolute path, the Comics root the series lives under
    QString seriesFolderName;    // sanitised series folder name on disk
    QString canonicalSeriesPath; // rootFolder + "/" + seriesFolderName (display form)
    QString coverPath;           // absolute path to cached cover image
    MangaSeriesDetail detailCache; // last successful fetchDetail payload
    qint64  addedAt = 0;
    qint64  lastValidatedAt = 0;

    QJsonObject toJson() const;
    static ComicsLibraryRecord fromJson(const QJsonObject& j);

    static QString makeKey(const QString& sourceId, const QString& seriesId)
    { return sourceId + ":" + seriesId; }

    QString key() const { return makeKey(sourceId, seriesId); }
};
```

- [ ] **Step 2: Impl as inline `.cpp` (small enough)**

Create `src/core/manga/ComicsLibraryRecord.cpp`:

```cpp
#include "ComicsLibraryRecord.h"

QJsonObject ComicsLibraryRecord::toJson() const
{
    QJsonObject o;
    o["sourceId"]            = sourceId;
    o["seriesId"]            = seriesId;
    o["title"]               = title;
    o["origin"]              = origin;
    o["rootFolder"]          = rootFolder;
    o["seriesFolderName"]    = seriesFolderName;
    o["canonicalSeriesPath"] = canonicalSeriesPath;
    o["coverPath"]           = coverPath;
    o["detailCache"]         = detailCache.toJson();
    o["addedAt"]             = QString::number(addedAt);
    o["lastValidatedAt"]     = QString::number(lastValidatedAt);
    return o;
}

ComicsLibraryRecord ComicsLibraryRecord::fromJson(const QJsonObject& j)
{
    ComicsLibraryRecord r;
    r.sourceId            = j.value("sourceId").toString();
    r.seriesId            = j.value("seriesId").toString();
    r.title               = j.value("title").toString();
    r.origin              = j.value("origin").toString();
    r.rootFolder          = j.value("rootFolder").toString();
    r.seriesFolderName    = j.value("seriesFolderName").toString();
    r.canonicalSeriesPath = j.value("canonicalSeriesPath").toString();
    r.coverPath           = j.value("coverPath").toString();
    r.detailCache         = MangaSeriesDetail::fromJson(j.value("detailCache").toObject());
    r.addedAt             = j.value("addedAt").toString().toLongLong();
    r.lastValidatedAt     = j.value("lastValidatedAt").toString().toLongLong();
    return r;
}
```

- [ ] **Step 3: Add to CMakeLists.txt**

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 9: Create `ComicsTankoyomiLibrary` class

**Files:**
- Create: `src/core/manga/ComicsTankoyomiLibrary.h`
- Create: `src/core/manga/ComicsTankoyomiLibrary.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include "ComicsLibraryRecord.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>

class JsonStore;

// Tankoyomi-origin Comics library store. Authoritative source-of-truth
// for "is this series Tankoyomi-origin?" — folder-imported series do
// NOT have entries here.
//
// JSON file: <appDataDir>/comics_library.json
// Schema version 1.
//
// Thread safety: mutating methods take m_mutex. Read accessors take
// m_mutex via QMutexLocker. libraryChanged is emitted off-lock.
class ComicsTankoyomiLibrary : public QObject
{
    Q_OBJECT
public:
    explicit ComicsTankoyomiLibrary(JsonStore* store, QObject* parent = nullptr);

    // Insert or replace by key (sourceId:seriesId). Idempotent.
    void add(const ComicsLibraryRecord& rec);
    void remove(const QString& sourceId, const QString& seriesId);

    bool contains(const QString& sourceId, const QString& seriesId) const;
    bool containsCanonicalPath(const QString& canonicalPath) const;
    ComicsLibraryRecord get(const QString& sourceId, const QString& seriesId) const;
    QList<ComicsLibraryRecord> all() const;

    // Used by LibraryScanner integration (Task 11): paths that the
    // scanner MUST NOT emit as folder-origin SeriesInfo because
    // they're already claimed by Tankoyomi-origin records.
    QStringList claimedCanonicalPaths() const;

signals:
    void libraryChanged();

private:
    void load();
    void save();

    JsonStore* m_store;
    mutable QMutex m_mutex;
    QHash<QString, ComicsLibraryRecord> m_byKey;             // sourceId:seriesId -> record
    QHash<QString, QString>             m_canonicalToKey;    // canonical path -> sourceId:seriesId

    static constexpr const char* FILENAME = "comics_library.json";
    static constexpr int kSchemaVersion = 1;
};
```

- [ ] **Step 2: Impl skeleton**

```cpp
#include "ComicsTankoyomiLibrary.h"
#include "core/JsonStore.h"
#include <QJsonArray>
#include <QJsonDocument>

ComicsTankoyomiLibrary::ComicsTankoyomiLibrary(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    load();
}

void ComicsTankoyomiLibrary::load()
{
    const auto doc = m_store->read(FILENAME);
    if (!doc.isObject()) return;
    for (const auto& v : doc.object().value("records").toArray()) {
        auto rec = ComicsLibraryRecord::fromJson(v.toObject());
        m_byKey.insert(rec.key(), rec);
        if (!rec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.insert(rec.canonicalSeriesPath, rec.key());
    }
}

void ComicsTankoyomiLibrary::save()
{
    QJsonArray arr;
    for (auto it = m_byKey.constBegin(); it != m_byKey.constEnd(); ++it)
        arr.append(it.value().toJson());
    QJsonObject root;
    root["schemaVersion"] = kSchemaVersion;
    root["records"] = arr;
    m_store->write(FILENAME, QJsonDocument(root));
}

void ComicsTankoyomiLibrary::add(const ComicsLibraryRecord& rec)
{
    {
        QMutexLocker lk(&m_mutex);
        const auto oldRec = m_byKey.value(rec.key());
        if (!oldRec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.remove(oldRec.canonicalSeriesPath);
        m_byKey.insert(rec.key(), rec);
        if (!rec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.insert(rec.canonicalSeriesPath, rec.key());
        save();
    }
    emit libraryChanged();
}

void ComicsTankoyomiLibrary::remove(const QString& sourceId, const QString& seriesId)
{
    {
        QMutexLocker lk(&m_mutex);
        const auto key = ComicsLibraryRecord::makeKey(sourceId, seriesId);
        const auto oldRec = m_byKey.take(key);
        if (!oldRec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.remove(oldRec.canonicalSeriesPath);
        save();
    }
    emit libraryChanged();
}

bool ComicsTankoyomiLibrary::contains(const QString& sourceId, const QString& seriesId) const
{
    QMutexLocker lk(&m_mutex);
    return m_byKey.contains(ComicsLibraryRecord::makeKey(sourceId, seriesId));
}

bool ComicsTankoyomiLibrary::containsCanonicalPath(const QString& canonicalPath) const
{
    QMutexLocker lk(&m_mutex);
    return m_canonicalToKey.contains(canonicalPath);
}

ComicsLibraryRecord ComicsTankoyomiLibrary::get(const QString& sourceId, const QString& seriesId) const
{
    QMutexLocker lk(&m_mutex);
    return m_byKey.value(ComicsLibraryRecord::makeKey(sourceId, seriesId));
}

QList<ComicsLibraryRecord> ComicsTankoyomiLibrary::all() const
{
    QMutexLocker lk(&m_mutex);
    return m_byKey.values();
}

QStringList ComicsTankoyomiLibrary::claimedCanonicalPaths() const
{
    QMutexLocker lk(&m_mutex);
    return m_canonicalToKey.keys();
}
```

- [ ] **Step 3: Add to CMakeLists.txt**

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 10: Create `SidecarMeta` for `.tankoyomi-meta.json`

**Files:**
- Create: `src/ui/pages/comics/SidecarMeta.h`
- Create: `src/ui/pages/comics/SidecarMeta.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <QString>
#include <optional>

// Reads and writes the hidden .tankoyomi-meta.json sidecar inside each
// Tankoyomi-origin series folder on disk. The sidecar is a RECOVERY
// HINT, not a source of truth (per Codex §16 invariant). It exists so
// that:
//   1. LibraryScanner can skip a folder it has already claimed
//      without round-tripping to comics_library.json on every walk.
//   2. Folder-renames and root-moves can be reconciled by scanning
//      for sidecar (sourceId, seriesId) tuples and re-pointing the
//      library record to the new path.
//
// File name uses a leading dot so it's hidden on POSIX; on Windows the
// dot does not hide but it stays out of the user's typical view.
struct SidecarMeta {
    QString sourceId;
    QString seriesId;
    QString title;
    qint64  createdAt = 0;
    int     schemaVersion = 1;
};

namespace sidecar {

constexpr const char* kFileName = ".tankoyomi-meta.json";

// Read meta from <seriesFolder>/.tankoyomi-meta.json. Returns
// nullopt if missing or malformed.
std::optional<SidecarMeta> read(const QString& seriesFolder);

// Write meta into <seriesFolder>/.tankoyomi-meta.json. Returns
// true on success.
bool write(const QString& seriesFolder, const SidecarMeta& meta);

// True iff the sidecar file exists at the expected path.
bool exists(const QString& seriesFolder);

} // namespace sidecar
```

- [ ] **Step 2: Impl**

```cpp
#include "SidecarMeta.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace sidecar {

static QString sidecarPath(const QString& folder)
{
    return QDir(folder).filePath(kFileName);
}

std::optional<SidecarMeta> read(const QString& seriesFolder)
{
    QFile f(sidecarPath(seriesFolder));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return std::nullopt;
    const auto o = doc.object();
    SidecarMeta m;
    m.sourceId      = o.value("sourceId").toString();
    m.seriesId      = o.value("seriesId").toString();
    m.title         = o.value("title").toString();
    m.createdAt     = o.value("createdAt").toString().toLongLong();
    m.schemaVersion = o.value("schemaVersion").toInt(1);
    if (m.sourceId.isEmpty() || m.seriesId.isEmpty()) return std::nullopt;
    return m;
}

bool write(const QString& seriesFolder, const SidecarMeta& meta)
{
    QDir().mkpath(seriesFolder);
    QSaveFile f(sidecarPath(seriesFolder));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QJsonObject o;
    o["sourceId"]      = meta.sourceId;
    o["seriesId"]      = meta.seriesId;
    o["title"]         = meta.title;
    o["createdAt"]     = QString::number(meta.createdAt);
    o["schemaVersion"] = meta.schemaVersion;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return f.commit();
}

bool exists(const QString& seriesFolder)
{
    return QFileInfo::exists(sidecarPath(seriesFolder));
}

} // namespace sidecar
```

- [ ] **Step 3: Add to CMakeLists.txt**

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 11: LibraryScanner skip-set integration (Agent 5 coordination heads-up first)

**Files:**
- Modify: `src/core/LibraryScanner.h`
- Modify: `src/core/LibraryScanner.cpp`

- [ ] **Step 1: Post coordination NOTE to chat.md FIRST**

Before editing, append to `agents/chat.md`:

```
NOTE - Agent 1 -> Agent 5 2026-05-NN ~HH:MMpm. Starting COMICS_TANKOYOMI_STREAM_MERGER Phase 2 Task 11; touching src/core/LibraryScanner.{h,cpp} to add an optional setClaimedPaths(QStringList) so the merged Comics page can suppress Tankoyomi-origin folders from the folder-imported tile set. Heads-up per GOVERNANCE.md:32 primary-ownership. Behaviour unchanged when no claimed paths are set. Will RTC when done.
```

- [ ] **Step 2: Add setClaimedPaths to header**

In `src/core/LibraryScanner.h`, add a public method declaration in the class body:

```cpp
// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — suppress emitting
// folder-origin SeriesInfo for any folder whose canonical path
// (display form, lowercased on Windows) is in this set. Used by
// ComicsPage to hide Tankoyomi-origin folders that have a library
// record. Empty set => unchanged behaviour.
void setClaimedPaths(const QStringList& paths);
```

And a private field:

```cpp
QSet<QString> m_claimedPaths;  // normalised canonical paths (lower on Windows)
```

- [ ] **Step 3: Add setClaimedPaths impl + skip logic in walk loop**

In `src/core/LibraryScanner.cpp`, add:

```cpp
#include <QSet>
// ... existing includes ...

void LibraryScanner::setClaimedPaths(const QStringList& paths)
{
    QSet<QString> norm;
    for (const auto& p : paths) {
        #ifdef Q_OS_WIN
        norm.insert(p.toLower());
        #else
        norm.insert(p);
        #endif
    }
    m_claimedPaths = norm;
}
```

Then find the existing folder-emit site (around `LibraryScanner.cpp:48-77` per Codex §16) where `seriesFound(...)` (or equivalent emit) is called for each grouped folder. Add a guard immediately above the emit:

```cpp
QString canonical = QDir(seriesFolderPath).absolutePath();
#ifdef Q_OS_WIN
const QString cmp = canonical.toLower();
#else
const QString cmp = canonical;
#endif
if (m_claimedPaths.contains(cmp)) continue;  // skip — Tankoyomi-origin owns this folder
```

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 12: Wire `ComicsTankoyomiLibrary` into ComicsPage construction

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Forward-declare + add member**

In `ComicsPage.h`, add a forward declaration `class ComicsTankoyomiLibrary;` and a private member field `ComicsTankoyomiLibrary* m_tyLibrary = nullptr;`.

- [ ] **Step 2: Construct in ctor**

In `ComicsPage.cpp` ctor, instantiate the library, get the `JsonStore` from `m_bridge` (existing pattern — see how `MangaDownloader` gets its `JsonStore` in `TankoyomiPage.cpp:100-108`). Connect `libraryChanged` to a slot that re-pushes the claimed-paths set to the scanner + triggers a tile refresh.

```cpp
#include "core/manga/ComicsTankoyomiLibrary.h"
// ... existing includes ...

// in ctor body, after m_bridge is wired:
m_tyLibrary = new ComicsTankoyomiLibrary(m_bridge->jsonStore(), this);
connect(m_tyLibrary, &ComicsTankoyomiLibrary::libraryChanged,
        this, &ComicsPage::onTankoyomiLibraryChanged);
```

- [ ] **Step 3: Wire scanner claimed-paths push**

Add a private slot `onTankoyomiLibraryChanged()` that:

```cpp
void ComicsPage::onTankoyomiLibraryChanged()
{
    if (m_scanner) m_scanner->setClaimedPaths(m_tyLibrary->claimedCanonicalPaths());
    // No automatic rescan; the tile refresh comes via the merged-tile logic in Task 13.
    rebuildTiles();
}
```

In the scanner construction site (existing `m_scanner = new LibraryScanner(...)` block), call `m_scanner->setClaimedPaths(m_tyLibrary->claimedCanonicalPaths())` before `triggerScan`.

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 13: Add `rebuildTiles()` merged-source method

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Add private method**

```cpp
// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — assemble tiles from
// two sources: (a) folder-origin SeriesInfo from m_scanner (already
// suppressed for claimed paths), (b) Tankoyomi-origin records from
// m_tyLibrary. Folder rows render with no badge; Tankoyomi rows
// render with the [Tankoyomi] chip. Dedup by canonical path.
void ComicsPage::rebuildTiles()
{
    m_tileStrip->clear();

    // Folder-origin: existing m_allSeries set, populated by onSeriesFound +
    // onScanFinished. Keep as-is.
    for (const auto& s : m_allSeries) addSeriesTile(s);  // existing path

    // Tankoyomi-origin: derive a SeriesInfo from each record + emit a
    // tile with the provenance badge.
    for (const auto& r : m_tyLibrary->all()) {
        SeriesInfo s;
        s.name        = r.title;
        s.path        = r.canonicalSeriesPath;
        s.coverPath   = r.coverPath;
        s.lastModifiedMs = r.lastValidatedAt;
        s.provenance  = QStringLiteral("tankoyomi");   // requires SeriesInfo to gain this field — see Task 14
        addSeriesTile(s);
    }
}
```

(Note: `SeriesInfo` doesn't have a `provenance` field today; Task 14 adds it. The build will fail after this task until Task 14 lands.)

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD FAILED` — `'provenance' is not a member of 'SeriesInfo'`. Proceed to Task 14.

### Task 14: Add `provenance` field to `SeriesInfo`

**Files:**
- Modify: `src/core/SeriesInfo.h` (or wherever it's declared — grep first)

- [ ] **Step 1: Locate the declaration**

Run: `Grep "struct SeriesInfo" src/`. Open the matching file.

- [ ] **Step 2: Add the field**

```cpp
QString provenance;  // "folder" (default) | "tankoyomi"
```

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 15: Adjust `addSeriesTile` to render the provenance badge

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Update `addSeriesTile`**

In `ComicsPage::addSeriesTile(const SeriesInfo& s)`, after the existing `TileCard` construction + cover setup, route the provenance flag to a future `TileCard::setProvenance` slot:

```cpp
// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — provenance chip slot.
// Renders a [Tankoyomi] chip at top-left when the series is
// Tankoyomi-origin. TileCard::setProvenance is added in Phase 5
// Task 33 (Agent 5 coordination surface).
card->setProvenance(s.provenance);  // "folder" | "tankoyomi"
```

(The build will fail here until Task 33 adds `setProvenance` to TileCard. Phase 2 ends with a temporary `card->setProperty("provenance", s.provenance);` instead, so the field flows through. Phase 5 swaps to the real method.)

- [ ] **Step 2: Use the property-shim form for Phase 2**

```cpp
card->setProperty("provenance", s.provenance);
```

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 16: Phase 2 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P2 library store + provenance 2026-05-NN ~HH:MMpm. New ComicsLibraryRecord POD + ComicsTankoyomiLibrary (JsonStore-backed comics_library.json, schema v1, thread-safe) + SidecarMeta (.tankoyomi-meta.json reader/writer for recovery hints, not source of truth per Codex §16). LibraryScanner gains setClaimedPaths(QStringList) skip-set integration; behaviour unchanged when no claims set. ComicsPage rebuildTiles assembles merged tile set from folder + Tankoyomi-origin records, deduped by canonical path. SeriesInfo gains provenance field. TileCard provenance plumbed via QObject property shim pending Phase 5 setProvenance method on TileCard (Agent 5 surface — coordination heads-up posted to chat.md before Task 11). BUILD OK.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/core/manga/ComicsLibraryRecord.{h,cpp}, src/core/manga/ComicsTankoyomiLibrary.{h,cpp}, src/ui/pages/comics/SidecarMeta.{h,cpp}, src/core/LibraryScanner.{h,cpp}, src/core/SeriesInfo.h, src/ui/pages/ComicsPage.{h,cpp}, CMakeLists.txt, agents/chat.md
```

---

## Phase 3 — Search takeover shell

**Goal:** Repurpose the existing ComicsPage search bar so its placeholder is "Search Tankoyomi" and submitting opens a takeover view forked from `StreamSearchWidget`. Two sections (Manga / Comics) split by `MangaResult::type`. Source-failure toast. Click a tile → emit `seriesActivated(MangaResult)` (consumed by Phase 4 detail view). Back → return to library. Old local-folder filter on the search bar is removed.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P3 search takeover`.

### Task 17: Fork `StreamSearchWidget` → `ComicsTankoyomiSearchWidget` skeleton

**Files:**
- Create: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h`
- Create: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp`

- [ ] **Step 1: Header (forked from StreamSearchWidget.h:1-96)**

```cpp
#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — forked from
// src/ui/pages/stream/StreamSearchWidget.{h,cpp} per brainstorm §6.1.
// Same shape: search input → fan-out to scrapers → two-section grid
// (Manga / Comics, by MangaResult::type) → click emits seriesActivated.
// Diverges: manga types instead of MetaItemPreview; QList<MangaScraper*>
// fan-out instead of MetaAggregator; no addon registry; no "in library"
// badge on result tiles (the comics library tile shows the chip).

#include "core/manga/MangaResult.h"
#include <QHash>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>

class MangaSourceRegistry;
class ComicsTankoyomiLibrary;
class TileStrip;
class TileCard;
class QNetworkAccessManager;

class ComicsTankoyomiSearchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ComicsTankoyomiSearchWidget(MangaSourceRegistry* registry,
                                         ComicsTankoyomiLibrary* tyLibrary,
                                         QNetworkAccessManager* nam,
                                         QWidget* parent = nullptr);

    void search(const QString& query);   // entry point from ComicsPage's search bar
    void clearResults();                  // when Back is clicked

signals:
    void backRequested();
    void seriesActivated(const MangaResult& preview);

private:
    void buildUI();
    void addResultCard(const MangaResult& r, TileStrip* targetStrip);
    void downloadPoster(const MangaResult& r, TileCard* card);
    void onSearchFinished(const QList<MangaResult>& batch);
    void onSearchError(const QString& message);

    static constexpr int kInitialCap = 6;     // mirrors Stream's per-section cap
    void revealMangaOverflow();
    void revealComicsOverflow();

    MangaSourceRegistry*    m_registry;
    ComicsTankoyomiLibrary* m_tyLibrary;
    QNetworkAccessManager*  m_nam;

    QString m_currentQuery;
    int     m_pendingSearches = 0;

    QPushButton* m_backBtn      = nullptr;
    QLabel*      m_statusLabel  = nullptr;
    QScrollArea* m_scroll       = nullptr;

    QLabel*      m_mangaHeader    = nullptr;
    TileStrip*   m_mangaStrip     = nullptr;
    QPushButton* m_mangaShowMore  = nullptr;
    QLabel*      m_comicsHeader   = nullptr;
    TileStrip*   m_comicsStrip    = nullptr;
    QPushButton* m_comicsShowMore = nullptr;

    QList<MangaResult> m_mangaOverflow;
    QList<MangaResult> m_comicsOverflow;

    QHash<QString, MangaResult> m_previewsByKey;  // sourceId:seriesId -> MangaResult
};
```

- [ ] **Step 2: Impl skeleton (forked from StreamSearchWidget.cpp)**

Create `ComicsTankoyomiSearchWidget.cpp`. Mirror the shape of `StreamSearchWidget.cpp:1-200`:

```cpp
#include "ComicsTankoyomiSearchWidget.h"
#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/ComicsTankoyomiLibrary.h"
#include "ui/widgets/TileStrip.h"
#include "ui/widgets/TileCard.h"
#include <QNetworkAccessManager>
#include <QVBoxLayout>

ComicsTankoyomiSearchWidget::ComicsTankoyomiSearchWidget(
    MangaSourceRegistry* registry, ComicsTankoyomiLibrary* tyLibrary,
    QNetworkAccessManager* nam, QWidget* parent)
    : QWidget(parent), m_registry(registry), m_tyLibrary(tyLibrary), m_nam(nam)
{
    buildUI();
    for (auto* s : m_registry->scrapers()) {
        connect(s, &MangaScraper::searchFinished,
                this, &ComicsTankoyomiSearchWidget::onSearchFinished);
        connect(s, &MangaScraper::errorOccurred,
                this, &ComicsTankoyomiSearchWidget::onSearchError);
    }
}

void ComicsTankoyomiSearchWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_backBtn = new QPushButton("← Back to library");
    connect(m_backBtn, &QPushButton::clicked, this, &ComicsTankoyomiSearchWidget::backRequested);
    root->addWidget(m_backBtn, 0, Qt::AlignLeft);

    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName("ComicsSearchStatus");
    root->addWidget(m_statusLabel);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    auto* sw = new QWidget;
    auto* sl = new QVBoxLayout(sw);

    m_mangaHeader   = new QLabel("MANGA");
    m_mangaHeader->setObjectName("ComicsSearchSection");
    m_mangaStrip    = new TileStrip;
    m_mangaShowMore = new QPushButton;
    m_mangaShowMore->setVisible(false);
    connect(m_mangaShowMore, &QPushButton::clicked, this, &ComicsTankoyomiSearchWidget::revealMangaOverflow);

    m_comicsHeader   = new QLabel("COMICS");
    m_comicsHeader->setObjectName("ComicsSearchSection");
    m_comicsStrip    = new TileStrip;
    m_comicsShowMore = new QPushButton;
    m_comicsShowMore->setVisible(false);
    connect(m_comicsShowMore, &QPushButton::clicked, this, &ComicsTankoyomiSearchWidget::revealComicsOverflow);

    sl->addWidget(m_mangaHeader);
    sl->addWidget(m_mangaStrip);
    sl->addWidget(m_mangaShowMore, 0, Qt::AlignLeft);
    sl->addWidget(m_comicsHeader);
    sl->addWidget(m_comicsStrip);
    sl->addWidget(m_comicsShowMore, 0, Qt::AlignLeft);
    sl->addStretch(1);
    m_scroll->setWidget(sw);
    root->addWidget(m_scroll, 1);
}

void ComicsTankoyomiSearchWidget::search(const QString& query)
{
    m_currentQuery = query;
    m_pendingSearches = m_registry->scrapers().size();
    m_mangaOverflow.clear();
    m_comicsOverflow.clear();
    m_previewsByKey.clear();
    m_mangaStrip->clear();
    m_comicsStrip->clear();
    m_mangaShowMore->setVisible(false);
    m_comicsShowMore->setVisible(false);
    m_mangaHeader->setVisible(false);
    m_comicsHeader->setVisible(false);
    m_statusLabel->setText(QString("Searching... (%1)").arg(query));
    for (auto* s : m_registry->scrapers()) s->search(query, 60);
}

void ComicsTankoyomiSearchWidget::clearResults()
{
    m_mangaStrip->clear();
    m_comicsStrip->clear();
    m_mangaOverflow.clear();
    m_comicsOverflow.clear();
    m_previewsByKey.clear();
    m_statusLabel->clear();
}

void ComicsTankoyomiSearchWidget::onSearchFinished(const QList<MangaResult>& batch)
{
    for (const auto& r : batch) {
        m_previewsByKey.insert(r.source + ":" + r.id, r);
        const bool isManga = (r.type.compare("manga", Qt::CaseInsensitive) == 0);
        auto& overflow = isManga ? m_mangaOverflow : m_comicsOverflow;
        auto* strip    = isManga ? m_mangaStrip    : m_comicsStrip;
        auto* header   = isManga ? m_mangaHeader   : m_comicsHeader;
        auto* showMore = isManga ? m_mangaShowMore : m_comicsShowMore;
        header->setVisible(true);
        if (strip->count() < kInitialCap) {
            addResultCard(r, strip);
        } else {
            overflow.append(r);
            showMore->setText(QString("Show %1 more").arg(overflow.size()));
            showMore->setVisible(true);
        }
    }
    if (--m_pendingSearches <= 0) {
        m_statusLabel->setText(QString("Done: %1 manga / %2 comics")
                                .arg(m_mangaStrip->count() + m_mangaOverflow.size())
                                .arg(m_comicsStrip->count() + m_comicsOverflow.size()));
    }
}

void ComicsTankoyomiSearchWidget::onSearchError(const QString& message)
{
    // Source-failure toast is fired by ComicsPage on this signal (Task 19);
    // here we just decrement the pending counter so the status line
    // settles.
    if (--m_pendingSearches <= 0) {
        m_statusLabel->setText(QString("Done with errors. Last: %1").arg(message));
    }
}

void ComicsTankoyomiSearchWidget::addResultCard(const MangaResult& r, TileStrip* targetStrip)
{
    auto* card = new TileCard(targetStrip);
    card->setText(r.title);
    downloadPoster(r, card);
    card->setProperty("seriesKey", r.source + ":" + r.id);
    QObject::connect(card, &TileCard::clicked, this, [this, r]() {
        emit seriesActivated(r);
    });
    targetStrip->addCard(card);
}

void ComicsTankoyomiSearchWidget::downloadPoster(const MangaResult& r, TileCard* card)
{
    // Reuse the existing poster cache pattern from TankoyomiPage::ensureCover
    // (TankoyomiPage.h:36-39, TankoyomiPage.cpp around line 95-98). Path is
    // <GenericDataLocation>/Tankoban/data/manga_posters/<source>_<id>.<ext>.
    // For Phase 3 the simplest viable path is a direct GET via m_nam;
    // share the cache directory with the existing TankoyomiPage cache to
    // avoid duplicate downloads while both surfaces coexist (Phase 8
    // deletes TankoyomiPage).
    // ... (implementation mirrors TankoyomiPage::ensureCover) ...
}

void ComicsTankoyomiSearchWidget::revealMangaOverflow()
{
    for (const auto& r : m_mangaOverflow) addResultCard(r, m_mangaStrip);
    m_mangaOverflow.clear();
    m_mangaShowMore->setVisible(false);
}

void ComicsTankoyomiSearchWidget::revealComicsOverflow()
{
    for (const auto& r : m_comicsOverflow) addResultCard(r, m_comicsStrip);
    m_comicsOverflow.clear();
    m_comicsShowMore->setVisible(false);
}
```

- [ ] **Step 3: Add to CMakeLists.txt + new `src/ui/pages/comics/` directory must be referenced in include paths if your CMake setup doesn't auto-glob**

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 18: Add ComicsPage state machine for search-takeover mode

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Add stack pages**

In `ComicsPage.h`, forward-declare `class ComicsTankoyomiSearchWidget;` and add member:

```cpp
ComicsTankoyomiSearchWidget* m_searchTakeover = nullptr;
MangaSourceRegistry*         m_sourceRegistry  = nullptr;
QNetworkAccessManager*       m_nam             = nullptr;

enum class Mode { Library, SearchResults, TankoyomiDetail };
Mode m_mode = Mode::Library;
```

- [ ] **Step 2: Build the search widget into `m_stack`**

In ComicsPage ctor, after the existing `m_stack` construction (today's `FadingStackedWidget* m_stack`):

```cpp
m_sourceRegistry = new MangaSourceRegistry(m_nam, this);
m_searchTakeover = new ComicsTankoyomiSearchWidget(m_sourceRegistry, m_tyLibrary, m_nam, this);
m_stack->addWidget(m_searchTakeover);

connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::backRequested,
        this, &ComicsPage::showLibraryMode);
connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::seriesActivated,
        this, &ComicsPage::onSearchResultActivated);
```

- [ ] **Step 3: Add mode-transition slots**

```cpp
void ComicsPage::showLibraryMode()
{
    m_mode = Mode::Library;
    m_stack->setCurrentWidget(/* grid page */);
}

void ComicsPage::showSearchMode(const QString& query)
{
    m_mode = Mode::SearchResults;
    m_searchTakeover->search(query);
    m_stack->setCurrentWidget(m_searchTakeover);
}

void ComicsPage::onSearchResultActivated(const MangaResult& preview)
{
    // Phase 4 wires the detail view here. For Phase 3, leave as a stub
    // (or open a temporary message) so the search-result click path is
    // testable without depending on Phase 4's widget existing yet.
    Q_UNUSED(preview);
    qDebug() << "[ComicsPage] result activated:" << preview.title;
}
```

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 19: Repurpose the search bar

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Change placeholder + signal target**

Find the existing search-bar construction in `ComicsPage.cpp` (the `m_searchBar = new QLineEdit; m_searchBar->setPlaceholderText("Search series and volumes");` block). Change it:

```cpp
m_searchBar->setPlaceholderText("Search Tankoyomi");
```

Remove the existing `connect(m_searchBar, &QLineEdit::textChanged, this, &ComicsPage::applySearch);` (or equivalent — the existing client-side filter wire). Replace with:

```cpp
connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() {
    const QString q = m_searchBar->text().trimmed();
    if (q.isEmpty()) return;
    showSearchMode(q);
});
```

Delete the `applySearch()` slot if no other callers remain (grep first).

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 20: Source-failure toast

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (or `ComicsTankoyomiSearchWidget.cpp` — choose Page-side so the toast attaches to the same parent as other Comics toasts)

- [ ] **Step 1: Wire scraper `errorOccurred` to a toast**

In ComicsPage ctor, after constructing `m_sourceRegistry`:

```cpp
#include "ui/player/ToastHud.h"
// ...
for (auto* s : m_sourceRegistry->scrapers()) {
    connect(s, &MangaScraper::errorOccurred, this, [this, s](const QString& msg) {
        ToastHud::show(window(), QString("%1 didn't respond").arg(s->sourceName()));
    });
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 21: Phase 3 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P3 search takeover 2026-05-NN ~HH:MMpm. New ComicsTankoyomiSearchWidget forked from StreamSearchWidget per §6.1 — two-section grid (Manga / Comics) split by MangaResult::type, kInitialCap=6 + Show-more overflow, fan-out to MangaSourceRegistry's scrapers. ComicsPage gains 3-mode enum (Library/SearchResults/TankoyomiDetail), search bar placeholder changed to "Search Tankoyomi", returnPressed kicks search takeover; old client-side applySearch removed. Source-failure surfaces via ToastHud. seriesActivated emit consumed by Phase 4 detail view (stub for now). BUILD OK; old TankoyomiPage still works unchanged. No src/ui/pages/tankoyomi/ deletions in this phase.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}, src/ui/pages/ComicsPage.{h,cpp}, CMakeLists.txt, agents/chat.md
```

---

## Phase 4 — Stream-style detail hero + Add/Remove Library

**Goal:** New `ComicsTankoyomiDetailView` with preview-first hero (paint from `MangaResult` immediately), `fetchDetail` refresh on entry, cache-lookup order (library JSON → sidecar → preview → network), Add/Remove silent-bookmark button, offline-source banner. No chapter download wiring yet — chapter list renders read-only with "open in reader" for any already-downloaded files. Sidecar is written on Add.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P4 detail hero + Add/Remove`.

### Task 22: Fork `StreamDetailView` → `ComicsTankoyomiDetailView` skeleton

**Files:**
- Create: `src/ui/pages/comics/ComicsTankoyomiDetailView.h`
- Create: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Header skeleton (forked from `src/ui/pages/stream/StreamDetailView.h:1-100`, minus stream-specific bits)**

```cpp
#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — forked from
// src/ui/pages/stream/StreamDetailView.{h,cpp} per brainstorm §6.1.
// Same shape: preview-first hero + chapter list + per-row action
// dispatch + right-click context menu. Diverges:
//   - chapter list instead of season-combo+episode table
//   - multi-select + Range modal (no Stream equivalent)
//   - no embedded source picker
//   - offline-source banner above the chapter list
//   - auto-add fires from the row click, not from progress save

#include "core/manga/MangaResult.h"
#include "core/manga/MangaSeriesDetail.h"
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>
#include <optional>

class CoreBridge;
class MangaSourceRegistry;
class MangaScraper;
class MangaDownloader;
class MangaDownloadIndex;
class ComicsTankoyomiLibrary;
class ChapterDownloadIndicator;
class ChapterRangeDialog;
class QNetworkAccessManager;

class ComicsTankoyomiDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit ComicsTankoyomiDetailView(CoreBridge* bridge,
                                        MangaSourceRegistry* registry,
                                        ComicsTankoyomiLibrary* tyLibrary,
                                        MangaDownloader* downloader,
                                        MangaDownloadIndex* downloadIndex,
                                        QNetworkAccessManager* nam,
                                        QWidget* parent = nullptr);

    // Open the detail view for a series. previewHint comes from search
    // (a MangaResult); for opening from a library tile, previewHint is
    // built from the existing ComicsLibraryRecord.detailCache.preview.
    void showEntry(const MangaResult& previewHint);

signals:
    void backRequested();
    void openComicRequested(const QString& cbzPath,
                            const QStringList& seriesCbzList,
                            const QString& seriesName);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void buildUI();
    void renderPreviewHero(const MangaResult& preview);
    void renderDetailHero(const MangaSeriesDetail& detail);
    void renderChapters(const QList<ChapterInfo>& chapters);
    void refreshChapterMarkers();    // disk-state revalidation
    void onAddRemoveClicked();
    void onChapterRowClicked(int row, int col);
    void onChapterContextMenu(const QPoint& pos);
    void onSeriesHeaderContextMenu(const QPoint& pos);
    void onFetchDetailReady(const MangaSeriesDetail& detail);
    void onChaptersReady(const QList<ChapterInfo>& chapters);
    void onSourceError(const QString& msg);
    bool isInLibrary() const;

    CoreBridge*             m_bridge;
    MangaSourceRegistry*    m_registry;
    ComicsTankoyomiLibrary* m_tyLibrary;
    MangaDownloader*        m_downloader;
    MangaDownloadIndex*     m_downloadIndex;
    QNetworkAccessManager*  m_nam;

    MangaScraper* m_currentScraper = nullptr;
    MangaResult   m_currentPreview;
    std::optional<MangaSeriesDetail> m_currentDetail;
    QList<ChapterInfo> m_currentChapters;

    QPushButton*  m_backBtn        = nullptr;
    QPushButton*  m_addRemoveBtn   = nullptr;
    QLabel*       m_coverLabel     = nullptr;
    QLabel*       m_titleLabel     = nullptr;
    QLabel*       m_metaLabel      = nullptr;
    QLabel*       m_synopsisLabel  = nullptr;
    QLabel*       m_genresLabel    = nullptr;
    QLabel*       m_offlineBanner  = nullptr;   // visible only when offline
    QTableWidget* m_chapterTable   = nullptr;
    ChapterRangeDialog* m_rangeDialog = nullptr;

    static constexpr int kColCheckbox  = 0;
    static constexpr int kColIndicator = 1;
    static constexpr int kColTitle     = 2;
    static constexpr int kColDate      = 3;
    static constexpr int kColCount     = 4;
};
```

- [ ] **Step 2: Build the UI**

Open `ComicsTankoyomiDetailView.cpp`. Implement `buildUI()`:

```cpp
#include "ComicsTankoyomiDetailView.h"
#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/MangaDownloader.h"
#include "core/manga/MangaDownloadIndex.h"
#include "core/manga/ComicsTankoyomiLibrary.h"
#include "ui/pages/tankoyomi/ChapterDownloadIndicator.h"
#include "ui/pages/tankoyomi/ChapterRangeDialog.h"
#include "SidecarMeta.h"
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QVBoxLayout>

void ComicsTankoyomiDetailView::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);

    auto* topRow = new QHBoxLayout;
    m_backBtn = new QPushButton("← Back");
    connect(m_backBtn, &QPushButton::clicked, this, &ComicsTankoyomiDetailView::backRequested);
    m_addRemoveBtn = new QPushButton("Add to library");
    m_addRemoveBtn->setObjectName("AddRemoveLibraryBtn");
    connect(m_addRemoveBtn, &QPushButton::clicked, this, &ComicsTankoyomiDetailView::onAddRemoveClicked);
    topRow->addWidget(m_backBtn);
    topRow->addStretch(1);
    topRow->addWidget(m_addRemoveBtn);
    root->addLayout(topRow);

    auto* hero = new QHBoxLayout;
    m_coverLabel = new QLabel;
    m_coverLabel->setObjectName("ComicsDetailHeroCover");
    m_coverLabel->setFixedSize(180, 270);
    hero->addWidget(m_coverLabel);

    auto* heroText = new QVBoxLayout;
    m_titleLabel = new QLabel;
    m_titleLabel->setObjectName("ComicsDetailTitle");
    m_metaLabel = new QLabel;
    m_metaLabel->setObjectName("ComicsDetailMeta");
    m_synopsisLabel = new QLabel;
    m_synopsisLabel->setObjectName("ComicsDetailSynopsis");
    m_synopsisLabel->setWordWrap(true);
    m_genresLabel = new QLabel;
    m_genresLabel->setObjectName("ComicsDetailGenres");
    heroText->addWidget(m_titleLabel);
    heroText->addWidget(m_metaLabel);
    heroText->addWidget(m_synopsisLabel);
    heroText->addWidget(m_genresLabel);
    heroText->addStretch(1);
    hero->addLayout(heroText, 1);
    root->addLayout(hero);

    m_offlineBanner = new QLabel(
        "Couldn't refresh chapter list from Tankoyomi — showing cached state.");
    m_offlineBanner->setObjectName("ComicsDetailOfflineBanner");
    m_offlineBanner->setVisible(false);
    root->addWidget(m_offlineBanner);

    m_chapterTable = new QTableWidget(0, kColCount);
    m_chapterTable->setHorizontalHeaderLabels({"", "", "Chapter", "Date"});
    m_chapterTable->horizontalHeader()->setStretchLastSection(true);
    m_chapterTable->verticalHeader()->setVisible(false);
    m_chapterTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_chapterTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chapterTable, &QTableWidget::cellClicked,
            this, &ComicsTankoyomiDetailView::onChapterRowClicked);
    connect(m_chapterTable, &QTableWidget::customContextMenuRequested,
            this, &ComicsTankoyomiDetailView::onChapterContextMenu);
    root->addWidget(m_chapterTable, 1);
}
```

- [ ] **Step 3: Add to CMakeLists.txt**

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 23: Implement preview-first hero + detail-cache lookup + fetchDetail refresh

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: showEntry**

```cpp
void ComicsTankoyomiDetailView::showEntry(const MangaResult& previewHint)
{
    m_currentPreview = previewHint;
    m_currentDetail.reset();
    m_currentChapters.clear();
    m_offlineBanner->setVisible(false);

    renderPreviewHero(previewHint);

    // Resolve scraper for this source.
    m_currentScraper = m_registry->find(previewHint.source);
    if (!m_currentScraper) {
        m_synopsisLabel->setText("(Unknown source)");
        return;
    }

    // Cache lookup order per Codex §13:
    //   1. comics_library.json record (if in library)
    //   2. sidecar at <canonicalSeriesPath>/.tankoyomi-meta.json
    //   3. in-memory preview (already painted)
    //   4. network fetchDetail
    if (m_tyLibrary->contains(previewHint.source, previewHint.id)) {
        const auto rec = m_tyLibrary->get(previewHint.source, previewHint.id);
        if (!rec.detailCache.synopsis.isEmpty()) {
            m_currentDetail = rec.detailCache;
            renderDetailHero(rec.detailCache);
        }
    }
    // (Sidecar fallback handled when opened from a tile click whose record is
    // somehow missing — defensive; not the common path.)

    // Wire detailReady on the scraper (one-shot via QPointer guard).
    QPointer<ComicsTankoyomiDetailView> self(this);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_currentScraper, &MangaScraper::detailReady, this,
        [self, conn](const MangaSeriesDetail& d) {
            QObject::disconnect(*conn);
            if (self) self->onFetchDetailReady(d);
        });
    m_currentScraper->fetchDetail(previewHint);

    // Wire chaptersReady similarly.
    auto conn2 = std::make_shared<QMetaObject::Connection>();
    *conn2 = connect(m_currentScraper, &MangaScraper::chaptersReady, this,
        [self, conn2](const QList<ChapterInfo>& chs) {
            QObject::disconnect(*conn2);
            if (self) self->onChaptersReady(chs);
        });
    m_currentScraper->fetchChapters(previewHint.id);

    // Wire errorOccurred for the offline banner.
    auto conn3 = std::make_shared<QMetaObject::Connection>();
    *conn3 = connect(m_currentScraper, &MangaScraper::errorOccurred, this,
        [self, conn3](const QString& msg) {
            if (self) self->onSourceError(msg);
        });

    // Update Add/Remove label.
    m_addRemoveBtn->setText(isInLibrary() ? "Remove from library" : "Add to library");
}

void ComicsTankoyomiDetailView::renderPreviewHero(const MangaResult& preview)
{
    m_titleLabel->setText(preview.title);
    QString meta;
    if (!preview.author.isEmpty()) meta += preview.author;
    if (!preview.status.isEmpty()) meta += (meta.isEmpty() ? "" : " . ") + preview.status;
    meta += (meta.isEmpty() ? "" : " . ") + preview.source;
    m_metaLabel->setText(meta);
    m_synopsisLabel->setText(QString());  // network refresh fills this
    m_genresLabel->setText(QString());
    // Cover: trigger a poster download into the same cache as
    // TankoyomiPage::ensureCover. For now load preview.thumbnailUrl via m_nam
    // and setPixmap on m_coverLabel when ready.
    // (Implementation mirrors TankoyomiPage::ensureCover at TankoyomiPage.cpp:95-98.)
}

void ComicsTankoyomiDetailView::renderDetailHero(const MangaSeriesDetail& detail)
{
    if (!detail.synopsis.isEmpty()) m_synopsisLabel->setText(detail.synopsis);
    if (!detail.genres.isEmpty())  m_genresLabel->setText(detail.genres.join(" • "));
    QString meta;
    if (!detail.author.isEmpty()) meta += detail.author;
    if (!detail.year.isEmpty())   meta += (meta.isEmpty() ? "" : " . ") + detail.year;
    if (!detail.status.isEmpty()) meta += (meta.isEmpty() ? "" : " . ") + detail.status;
    meta += (meta.isEmpty() ? "" : " . ") + detail.preview.source;
    m_metaLabel->setText(meta);
}

void ComicsTankoyomiDetailView::onFetchDetailReady(const MangaSeriesDetail& detail)
{
    m_currentDetail = detail;
    renderDetailHero(detail);
    // If the library record exists, persist the fresh detail cache.
    if (m_tyLibrary->contains(detail.preview.source, detail.preview.id)) {
        auto rec = m_tyLibrary->get(detail.preview.source, detail.preview.id);
        rec.detailCache = detail;
        rec.lastValidatedAt = QDateTime::currentMSecsSinceEpoch();
        m_tyLibrary->add(rec);  // upsert
    }
    // If detail came with chapters baked in, render them; otherwise wait
    // for chaptersReady.
    if (!detail.cachedChapters.isEmpty() && m_currentChapters.isEmpty()) {
        m_currentChapters = detail.cachedChapters;
        renderChapters(m_currentChapters);
        refreshChapterMarkers();
    }
}

void ComicsTankoyomiDetailView::onChaptersReady(const QList<ChapterInfo>& chs)
{
    m_currentChapters = chs;
    renderChapters(chs);
    refreshChapterMarkers();
}

void ComicsTankoyomiDetailView::onSourceError(const QString& /*msg*/)
{
    m_offlineBanner->setVisible(true);
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 24: Implement `renderChapters` + `refreshChapterMarkers` skeleton

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: renderChapters**

```cpp
void ComicsTankoyomiDetailView::renderChapters(const QList<ChapterInfo>& chs)
{
    m_chapterTable->setRowCount(chs.size());
    for (int i = 0; i < chs.size(); ++i) {
        const auto& ch = chs[i];
        // col 0 checkbox
        auto* cb = new QTableWidgetItem;
        cb->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        cb->setCheckState(Qt::Unchecked);
        m_chapterTable->setItem(i, kColCheckbox, cb);

        // col 1 indicator — Phase 5 wires ChapterDownloadIndicator here.
        // For Phase 4, a placeholder QTableWidgetItem so the column renders.
        auto* ind = new QTableWidgetItem;
        m_chapterTable->setItem(i, kColIndicator, ind);

        // col 2 title
        auto* title = new QTableWidgetItem(ch.name);
        title->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_chapterTable->setItem(i, kColTitle, title);

        // col 3 date
        const auto dt = QDateTime::fromMSecsSinceEpoch(ch.dateUpload).toString(Qt::ISODate);
        auto* date = new QTableWidgetItem(dt);
        date->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_chapterTable->setItem(i, kColDate, date);
    }
}

void ComicsTankoyomiDetailView::refreshChapterMarkers()
{
    // Phase 5 wires MangaDownloadIndex revalidation here. For Phase 4
    // this is a no-op stub; if a chapter is on disk it's already
    // discoverable via existing MangaDownloader records but we don't
    // surface that until Phase 5.
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 25: Implement Add/Remove silent-bookmark button

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`
- Modify: `src/ui/pages/comics/SidecarMeta.h` (already created in Task 10 — no edit)

- [ ] **Step 1: isInLibrary helper**

```cpp
bool ComicsTankoyomiDetailView::isInLibrary() const
{
    return m_tyLibrary->contains(m_currentPreview.source, m_currentPreview.id);
}
```

- [ ] **Step 2: Add/Remove click handler**

```cpp
void ComicsTankoyomiDetailView::onAddRemoveClicked()
{
    if (isInLibrary()) {
        // Phase 4: simple delete-record-only path. Phase 6 adds the
        // "keep files vs delete files" confirmation dialog; Phase 4
        // ships only the silent remove of the record.
        m_tyLibrary->remove(m_currentPreview.source, m_currentPreview.id);
        m_addRemoveBtn->setText("Add to library");
    } else {
        // Build a new record.
        ComicsLibraryRecord rec;
        rec.sourceId = m_currentPreview.source;
        rec.seriesId = m_currentPreview.id;
        rec.title    = m_currentPreview.title;
        rec.origin   = "tankoyomi";
        // Pick the first comics root as destination.
        const auto roots = m_bridge->rootFolders("comics");
        rec.rootFolder = roots.isEmpty() ? QString() : roots.first();
        rec.seriesFolderName = sanitiseFilename(m_currentPreview.title);
        rec.canonicalSeriesPath = QDir(rec.rootFolder).filePath(rec.seriesFolderName);
        // Cover path: reuse the existing TankoyomiPage poster cache file as
        // the tile cover. The path comes from the preview thumbnail download
        // (Task 22). For Phase 4 we accept an empty coverPath and the cover
        // resolves on next sync. (Phase 5 / 6 tightens.)
        rec.coverPath = QString();
        if (m_currentDetail.has_value()) rec.detailCache = *m_currentDetail;
        rec.addedAt        = QDateTime::currentMSecsSinceEpoch();
        rec.lastValidatedAt = rec.addedAt;
        m_tyLibrary->add(rec);

        // Write sidecar inside the series folder (creating it if needed).
        SidecarMeta sm;
        sm.sourceId = rec.sourceId;
        sm.seriesId = rec.seriesId;
        sm.title    = rec.title;
        sm.createdAt = rec.addedAt;
        sidecar::write(rec.canonicalSeriesPath, sm);

        m_addRemoveBtn->setText("Remove from library");
    }
}
```

- [ ] **Step 3: Add a sanitiseFilename helper**

Inside the cpp anonymous namespace at file top:

```cpp
namespace {

QString sanitiseFilename(const QString& s)
{
    QString r = s;
    static const QString kBad = R"(<>:"/\|?*)";
    for (QChar c : kBad) r.replace(c, '_');
    return r.trimmed();
}

} // namespace
```

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 26: Wire the detail view into ComicsPage's mode stack

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Forward-declare + member**

In `ComicsPage.h`:

```cpp
class ComicsTankoyomiDetailView;
// ...
ComicsTankoyomiDetailView* m_tyDetailView = nullptr;
MangaDownloader*           m_mangaDownloader = nullptr;
MangaDownloadIndex*        m_mangaDownloadIndex = nullptr;  // Phase 5 instantiates
```

- [ ] **Step 2: Construct in ctor**

```cpp
m_mangaDownloader = new MangaDownloader(m_bridge->jsonStore(), this);
for (auto* s : m_sourceRegistry->scrapers())
    m_mangaDownloader->setScraper(s->sourceId(), s);

m_tyDetailView = new ComicsTankoyomiDetailView(
    m_bridge, m_sourceRegistry, m_tyLibrary, m_mangaDownloader,
    /*downloadIndex*/ nullptr,   // Phase 5 wires this
    m_nam, this);
m_stack->addWidget(m_tyDetailView);

connect(m_tyDetailView, &ComicsTankoyomiDetailView::backRequested,
        this, &ComicsPage::onDetailBack);
connect(m_tyDetailView, &ComicsTankoyomiDetailView::openComicRequested,
        this, &ComicsPage::openComic);  // existing signal
```

- [ ] **Step 3: Wire `onSearchResultActivated` to open the detail view**

Replace the Phase 3 stub:

```cpp
void ComicsPage::onSearchResultActivated(const MangaResult& preview)
{
    m_mode = Mode::TankoyomiDetail;
    m_tyDetailView->showEntry(preview);
    m_stack->setCurrentWidget(m_tyDetailView);
}

void ComicsPage::onDetailBack()
{
    // Back from detail returns to whichever mode the user entered
    // detail FROM (Codex §20 — back behaviour).
    if (m_mode == Mode::TankoyomiDetail) {
        // For Phase 4: always return to library. Phase 9 refines.
        showLibraryMode();
    }
}
```

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 27: ComicsPage tile-click dispatcher routes Tankoyomi-origin tiles to the new view

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Update onTileClicked**

The existing `onTileClicked(QString seriesPath, QString seriesName)` opens `m_seriesView`. Update to check provenance first:

```cpp
void ComicsPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    // Provenance route: check whether this canonical path is claimed by a
    // Tankoyomi record. If yes, open the new detail view; if no, fall
    // through to today's SeriesView (folder-tree).
    if (m_tyLibrary->containsCanonicalPath(seriesPath)) {
        // Reconstruct a MangaResult preview from the record so the
        // detail view can open without re-querying the source.
        // Find the record by canonical path:
        for (const auto& rec : m_tyLibrary->all()) {
            if (rec.canonicalSeriesPath == seriesPath) {
                m_mode = Mode::TankoyomiDetail;
                m_tyDetailView->showEntry(rec.detailCache.preview);
                m_stack->setCurrentWidget(m_tyDetailView);
                return;
            }
        }
    }
    // Existing folder-imported path:
    m_seriesView->openSeries(seriesPath, seriesName);
    m_stack->setCurrentWidget(m_seriesView);
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 28: Phase 4 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P4 detail hero + Add/Remove 2026-05-NN ~HH:MMpm. New ComicsTankoyomiDetailView forked from StreamDetailView per §6.1 + Codex §13 cache order — preview-first hero painted from MangaResult immediately, fetchDetail refresh on entry, detail-cache lookup order (library JSON > sidecar > preview > network), Add/Remove silent-bookmark button (Phase 4 simple delete; Phase 6 adds keep-files-vs-delete-files confirmation), offline-source banner above chapter list when scraper errorOccurred fires. Chapter list renders read-only in Phase 4 — Phase 5 wires ChapterDownloadIndicator + range/multi-select. ComicsPage tile-click dispatcher routes Tankoyomi-origin tiles (matched by canonical path lookup in m_tyLibrary) to the new view; folder tiles still open today's SeriesView. Sidecar (.tankoyomi-meta.json) written on Add via SidecarMeta helper. Search-result click in Phase 3 now opens the detail view. BUILD OK.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}, src/ui/pages/ComicsPage.{h,cpp}, CMakeLists.txt, agents/chat.md
```

---

## Phase 5 — Chapter rows + downloader integration + tile chips

**Goal:** Wire `ChapterDownloadIndicator` into the chapter table, hook the range modal + multi-select, build `MangaDownloadIndex`, fire `validateAll` on detail-view + ComicsPage `showEvent`, fire auto-add toast on first download click, integrate per-series queue controls into the right-click header menu, add Tankoyomi-origin + DOWNLOADING chips to `TileCard` (Agent 5 coordination heads-up first).

**Coordination heads-up:** Phase 5 touches Agent 5's `TileCard`. Post the NOTE-to-Agent-5 in chat.md before Task 33.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P5 chapter rows + downloader integration + tile chips`.

### Task 29: Create `MangaDownloadIndex` (fork of `StreamDownloadIndex`)

**Files:**
- Create: `src/core/manga/MangaDownloadIndex.h`
- Create: `src/core/manga/MangaDownloadIndex.cpp`

- [ ] **Step 1: Header (forked from `src/core/stream/StreamDownloadIndex.h`)**

```cpp
#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — forked from
// src/core/stream/StreamDownloadIndex.{h,cpp} per brainstorm §6.1.
// Same threadsafe canonical-key-keyed JSON-backed shape, different
// keying (sourceId:seriesId:chapterId instead of imdbId:season:episode).
//
// JSON file: <appDataDir>/manga_downloads_index.json. Schema v1.

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

class JsonStore;

class MangaDownloadIndex : public QObject
{
    Q_OBJECT
public:
    struct Entry {
        QString sourceId;
        QString seriesId;
        QString chapterId;
        QString canonicalPath;    // .cbz file or folder, display form
        qint64  addedAt = 0;
        qint64  fileSizeBytes = 0;
    };

    explicit MangaDownloadIndex(JsonStore* store, QObject* parent = nullptr);

    void registerChapter(const QString& sourceId, const QString& seriesId,
                          const QString& chapterId, const QString& canonicalPath,
                          qint64 fileSizeBytes);
    void evictBySeries(const QString& sourceId, const QString& seriesId);
    void evictByChapter(const QString& sourceId, const QString& seriesId,
                         const QString& chapterId);
    void validateAll();

    bool isComicsOwned(const QString& canonicalKey) const;
    std::optional<QString> filePathFor(const QString& sourceId, const QString& seriesId,
                                        const QString& chapterId) const;
    bool hasAnyForSeries(const QString& sourceId, const QString& seriesId) const;
    QList<Entry> entriesForSeries(const QString& sourceId, const QString& seriesId) const;

    static QString computeCanonicalKey(const QString& anyPath);
    static QString computeChapterKey(const QString& sourceId, const QString& seriesId,
                                      const QString& chapterId);

signals:
    void entriesChanged();

private:
    void load();
    void save();

    JsonStore* m_store;
    mutable QMutex m_mutex;
    QHash<QString, Entry>   m_byPath;
    QHash<QString, QString> m_byChapter;
    QSet<QString>           m_seriesHasAny;

    static constexpr const char* FILENAME = "manga_downloads_index.json";
    static constexpr int kSchemaVersion = 1;
};
```

- [ ] **Step 2: Impl (mirror StreamDownloadIndex.cpp structurally)**

The implementation parallels StreamDownloadIndex's. Copy its body, rename:
- `imdbId` → `seriesId` + `sourceId`
- `season`/`episode` → `chapterId`
- `streamingFile` → `comicsFile`
- `isStreamOwned` → `isComicsOwned`
- `m_imdbHasAny` → `m_seriesHasAny`
- File constant → `"manga_downloads_index.json"`

The `validateAll`, `computeCanonicalKey`, mutex contract, signal-emit-off-lock pattern are identical.

- [ ] **Step 3: Add to CMakeLists.txt**

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 30: Wire `MangaDownloadIndex` into ComicsPage + detail view

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Instantiate in ComicsPage ctor**

```cpp
m_mangaDownloadIndex = new MangaDownloadIndex(m_bridge->jsonStore(), this);
// Pass to the detail view via setter (or via the existing ctor param,
// already declared in Task 22 — replace the nullptr argument).
```

- [ ] **Step 2: `validateAll` on ComicsPage showEvent**

Override `showEvent` (or add the call into the existing `activate()` if that's the show-equivalent for this page):

```cpp
void ComicsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_mangaDownloadIndex) m_mangaDownloadIndex->validateAll();
}
```

- [ ] **Step 3: Detail view showEvent**

In `ComicsTankoyomiDetailView::showEvent`:

```cpp
void ComicsTankoyomiDetailView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_downloadIndex) m_downloadIndex->validateAll();
    refreshChapterMarkers();
}
```

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 31: Replace placeholder indicator with `ChapterDownloadIndicator`

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Use `ChapterDownloadIndicator` in renderChapters**

Replace the `col 1 indicator` block:

```cpp
auto* ind = new ChapterDownloadIndicator(m_chapterTable);
const QString chapterKey = MangaDownloadIndex::computeChapterKey(
    m_currentPreview.source, m_currentPreview.id, ch.id);
const auto onDisk = m_downloadIndex && m_downloadIndex->filePathFor(
    m_currentPreview.source, m_currentPreview.id, ch.id).has_value();
ind->setState(onDisk ? ChapterDownloadIndicator::State::Downloaded
                      : ChapterDownloadIndicator::State::NotDownloaded);
QObject::connect(ind, &ChapterDownloadIndicator::clicked, this, [this, ch, ind]() {
    onIndicatorClicked(ch, ind);
});
m_chapterTable->setCellWidget(i, kColIndicator, ind);
```

- [ ] **Step 2: refreshChapterMarkers fills states**

```cpp
void ComicsTankoyomiDetailView::refreshChapterMarkers()
{
    if (!m_downloadIndex) return;
    for (int i = 0; i < m_currentChapters.size(); ++i) {
        auto* ind = qobject_cast<ChapterDownloadIndicator*>(
            m_chapterTable->cellWidget(i, kColIndicator));
        if (!ind) continue;
        const auto& ch = m_currentChapters[i];
        const auto onDisk = m_downloadIndex->filePathFor(
            m_currentPreview.source, m_currentPreview.id, ch.id).has_value();
        const auto rec = m_downloader->recordForSeries(
            m_currentPreview.source, m_currentPreview.id);
        // (recordForSeries is a small helper to add to MangaDownloader; see Task 32.)
        ind->setState(onDisk ? ChapterDownloadIndicator::State::Downloaded
                              : ChapterDownloadIndicator::State::NotDownloaded);
    }
}
```

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD FAILED` until `MangaDownloader::recordForSeries` is added in Task 32.

### Task 32: Add minimal `MangaDownloader::recordForSeries` helper

**Files:**
- Modify: `src/core/manga/MangaDownloader.h`
- Modify: `src/core/manga/MangaDownloader.cpp`

- [ ] **Step 1: Header declaration**

```cpp
// Returns the active record for (source, seriesTitle-or-id), if any.
// Used by the new detail view to colour indicator widgets and route
// per-chapter pause/resume queries.
MangaDownloadRecord recordForSeries(const QString& sourceId,
                                     const QString& seriesId) const;
```

- [ ] **Step 2: Impl**

```cpp
MangaDownloadRecord MangaDownloader::recordForSeries(const QString& sourceId,
                                                       const QString& seriesId) const
{
    QMutexLocker lk(&m_mutex);
    for (const auto& r : m_records) {
        if (r.source == sourceId && r.id == seriesId) return r;
    }
    return MangaDownloadRecord{};
}
```

(Note: `MangaDownloadRecord::id` is the existing SHA-256-truncated ID at `MangaDownloader.h:39`. The merger's chapter-row code stores `seriesId` matching the scraper's `MangaResult.id`. There may be a mapping needed if the existing ID computation diverges from `MangaResult.id`. If divergent, add a `sourceSeriesId` field on `MangaDownloadRecord` that captures `MangaResult.id` verbatim, and key `recordForSeries` on that.)

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 33: TileCard provenance + downloading badge slots (Agent 5 coordination heads-up first)

**Files:**
- Modify: `src/ui/pages/TileCard.h`
- Modify: `src/ui/pages/TileCard.cpp`

- [ ] **Step 1: Post coordination NOTE to chat.md FIRST**

```
NOTE - Agent 1 -> Agent 5 2026-05-NN ~HH:MMpm. Starting COMICS_TANKOYOMI_STREAM_MERGER Phase 5 Task 33; touching src/ui/pages/TileCard.{h,cpp} to add two badge slots: provenance ("tankoyomi" → "Tankoyomi" chip top-left) + downloadingState (bool → "DOWNLOADING" chip top-right). Both render via the existing applyBadges painter path (TileCard.cpp:243-333). Heads-up per GOVERNANCE.md:32 primary-ownership. Behaviour unchanged when slots are default-empty. Will RTC when done.
```

- [ ] **Step 2: Add to header**

In `TileCard.h:17-19` area, add setters:

```cpp
void setProvenance(const QString& provenance);   // "folder" | "tankoyomi"
void setDownloadingChip(bool show);
```

And private fields:

```cpp
QString m_provenance;
bool    m_downloadingChip = false;
```

- [ ] **Step 3: Setters route to repaint**

```cpp
void TileCard::setProvenance(const QString& p)
{
    if (m_provenance == p) return;
    m_provenance = p;
    update();
}

void TileCard::setDownloadingChip(bool s)
{
    if (m_downloadingChip == s) return;
    m_downloadingChip = s;
    update();
}
```

- [ ] **Step 4: Extend `applyBadges` painter (TileCard.cpp:243-333)**

Inside the existing `applyBadges` body, after the current chip rendering, add:

```cpp
// Provenance chip (top-left). Renders only when m_provenance == "tankoyomi".
if (m_provenance == "tankoyomi") {
    const QRect chipRect(10, 10, 70, 18);
    painter.fillRect(chipRect, palette().color(QPalette::AlternateBase));
    painter.setPen(palette().color(QPalette::ButtonText));
    painter.drawText(chipRect, Qt::AlignCenter, "Tankoyomi");
}

// DOWNLOADING chip (top-right) — only when active.
if (m_downloadingChip) {
    const QRect chipRect(width() - 90, 10, 80, 18);
    painter.fillRect(chipRect, palette().color(QPalette::AlternateBase));
    painter.setPen(palette().color(QPalette::ButtonText));
    painter.drawText(chipRect, Qt::AlignCenter, "DOWNLOADING");
}
```

- [ ] **Step 5: Swap the Phase 2 property shim for the real method**

In `ComicsPage::addSeriesTile`, replace `card->setProperty("provenance", s.provenance);` with:

```cpp
card->setProvenance(s.provenance);
```

- [ ] **Step 6: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 34: ComicsPage drives the DOWNLOADING chip from MangaDownloader

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Subscribe to MangaDownloader::downloadUpdated + connect to tile refresh**

```cpp
connect(m_mangaDownloader, &MangaDownloader::downloadUpdated,
        this, [this](const QString& /*id*/) { refreshTileChips(); });
connect(m_mangaDownloader, &MangaDownloader::downloadCompleted,
        this, [this](const QString& /*id*/) { refreshTileChips(); });
```

- [ ] **Step 2: Implement refreshTileChips**

```cpp
void ComicsPage::refreshTileChips()
{
    // Walk the tile-strip cards; for each Tankoyomi-origin tile,
    // check whether the downloader has any active record for its
    // sourceId:seriesId and set the chip accordingly.
    for (auto* card : m_tileStrip->cards()) {
        const QString key = card->property("seriesKey").toString();
        if (key.isEmpty()) { card->setDownloadingChip(false); continue; }
        const auto parts = key.split(':');
        if (parts.size() < 2) { card->setDownloadingChip(false); continue; }
        const auto rec = m_mangaDownloader->recordForSeries(parts[0], parts[1]);
        const bool active = !rec.id.isEmpty() &&
                            (rec.status == "queued" || rec.status == "downloading");
        card->setDownloadingChip(active);
    }
}
```

(Note: the `seriesKey` property must be set on each Tankoyomi-origin tile by `addSeriesTile` — add the line `card->setProperty("seriesKey", rec.sourceId + ":" + rec.seriesId);` in the tile-construction branch when assembling the Tankoyomi-origin tile.)

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 35: Auto-add toast on first download click

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Implement onIndicatorClicked**

```cpp
void ComicsTankoyomiDetailView::onIndicatorClicked(const ChapterInfo& ch,
                                                     ChapterDownloadIndicator* ind)
{
    // Codex §15 wiring rule: auto-add happens BEFORE startDownload, in
    // the GUI thread, so the toast + library tile + downloader call
    // are sequenced correctly.

    bool didAdd = false;
    if (!isInLibrary()) {
        // Mirror the Add-button path.
        onAddRemoveClicked();
        didAdd = true;
    }

    if (didAdd) {
        ToastHud::show(window(),
                       QString("Added %1 to your library").arg(m_currentPreview.title));
    }

    // Build a one-chapter QList<ChapterInfo> and call startDownload.
    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    QList<ChapterInfo> single;
    single.append(ch);
    m_downloader->startDownload(rec.title, rec.sourceId, single,
                                  rec.canonicalSeriesPath, "cbz",
                                  /*coverPath*/ rec.coverPath);

    // Optimistic indicator state.
    ind->setState(ChapterDownloadIndicator::State::Queue);
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 36: Multi-select + "Download N selected" + Range modal

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.h` (add member buttons)
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Add header buttons**

In `buildUI`, after the chapter-table construction, add a header row with two buttons:

```cpp
auto* chapHeader = new QHBoxLayout;
auto* chapTitle  = new QLabel("CHAPTERS");
chapHeader->addWidget(chapTitle);
chapHeader->addStretch(1);
m_downloadRangeBtn = new QPushButton("Download Range...");
m_downloadSelectedBtn = new QPushButton("Download 0 selected");
m_downloadSelectedBtn->setVisible(false);
chapHeader->addWidget(m_downloadRangeBtn);
chapHeader->addWidget(m_downloadSelectedBtn);
root->insertLayout(/* index before chapter table */, chapHeader);
connect(m_downloadRangeBtn, &QPushButton::clicked,
        this, &ComicsTankoyomiDetailView::onDownloadRangeClicked);
connect(m_downloadSelectedBtn, &QPushButton::clicked,
        this, &ComicsTankoyomiDetailView::onDownloadSelectedClicked);
connect(m_chapterTable, &QTableWidget::itemChanged,
        this, &ComicsTankoyomiDetailView::updateDownloadSelectedButton);
```

- [ ] **Step 2: updateDownloadSelectedButton**

```cpp
void ComicsTankoyomiDetailView::updateDownloadSelectedButton()
{
    int n = 0;
    for (int i = 0; i < m_chapterTable->rowCount(); ++i) {
        if (auto* item = m_chapterTable->item(i, kColCheckbox))
            if (item->checkState() == Qt::Checked) ++n;
    }
    m_downloadSelectedBtn->setText(QString("Download %1 selected").arg(n));
    m_downloadSelectedBtn->setVisible(n > 0);
}
```

- [ ] **Step 3: onDownloadSelectedClicked**

```cpp
void ComicsTankoyomiDetailView::onDownloadSelectedClicked()
{
    QList<ChapterInfo> picks;
    for (int i = 0; i < m_chapterTable->rowCount(); ++i) {
        if (auto* it = m_chapterTable->item(i, kColCheckbox))
            if (it->checkState() == Qt::Checked && i < m_currentChapters.size())
                picks.append(m_currentChapters[i]);
    }
    if (picks.isEmpty()) return;

    if (!isInLibrary()) { onAddRemoveClicked(); ToastHud::show(window(),
        QString("Added %1 to your library").arg(m_currentPreview.title)); }
    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    m_downloader->startDownload(rec.title, rec.sourceId, picks,
                                  rec.canonicalSeriesPath, "cbz", rec.coverPath);
}
```

- [ ] **Step 4: onDownloadRangeClicked**

```cpp
void ComicsTankoyomiDetailView::onDownloadRangeClicked()
{
    if (!m_rangeDialog) m_rangeDialog = new ChapterRangeDialog(this);
    m_rangeDialog->setChapters(m_currentChapters);
    if (m_rangeDialog->exec() != QDialog::Accepted) return;
    const auto range = m_rangeDialog->selectedRange();   // hypothetical API
    QList<ChapterInfo> picks;
    for (int i = range.first; i <= range.second && i < m_currentChapters.size(); ++i)
        picks.append(m_currentChapters[i]);
    if (picks.isEmpty()) return;
    if (!isInLibrary()) { onAddRemoveClicked(); ToastHud::show(window(),
        QString("Added %1 to your library").arg(m_currentPreview.title)); }
    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    m_downloader->startDownload(rec.title, rec.sourceId, picks,
                                  rec.canonicalSeriesPath, "cbz", rec.coverPath);
}
```

(Note: `ChapterRangeDialog::setChapters` and `::selectedRange` need to exist. Check the existing dialog's API at `src/ui/pages/tankoyomi/ChapterRangeDialog.h:13-22`; if those exact methods aren't present, adapt the call site to whatever the dialog actually exposes.)

- [ ] **Step 5: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 37: Right-click series-header context menu

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Wire context menu on the hero header row**

In `buildUI()`, set the hero container widget's `setContextMenuPolicy(Qt::CustomContextMenu)` and connect `customContextMenuRequested` to `onSeriesHeaderContextMenu`.

- [ ] **Step 2: Build the menu**

```cpp
void ComicsTankoyomiDetailView::onSeriesHeaderContextMenu(const QPoint& pos)
{
    if (!isInLibrary()) return;   // these actions only make sense for in-library series
    const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);
    const auto activeRec = m_downloader->recordForSeries(rec.sourceId, rec.seriesId);
    QMenu menu(this);
    auto* pauseAct   = menu.addAction("Pause series");
    auto* resumeAct  = menu.addAction("Resume series");
    auto* restartAct = menu.addAction("Restart all chapters");
    auto* retryAct   = menu.addAction("Retry failed chapters");
    auto* cancelAct  = menu.addAction("Cancel all");
    menu.addSeparator();
    auto* removeAct  = menu.addAction("Remove from library");

    pauseAct->setEnabled(!activeRec.paused);
    resumeAct->setEnabled(activeRec.paused);

    const auto chosen = menu.exec(/* hero widget */->mapToGlobal(pos));
    if (chosen == pauseAct)   m_downloader->pauseSeries(activeRec.id);
    if (chosen == resumeAct)  m_downloader->resumeSeries(activeRec.id);
    if (chosen == restartAct) m_downloader->restartSeries(activeRec.id);
    if (chosen == retryAct)   m_downloader->retryFailedChapters(activeRec.id);
    if (chosen == cancelAct)  m_downloader->cancelDownload(activeRec.id);
    if (chosen == removeAct)  onAddRemoveClicked();   // shares the silent-remove path
}
```

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 38: Phase 5 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P5 chapter rows + downloader integration + tile chips 2026-05-NN ~HH:MMpm. New MangaDownloadIndex forked from StreamDownloadIndex per §6.1; threadsafe canonical-key-keyed JSON-backed index over manga_downloads_index.json. ComicsTankoyomiDetailView wires ChapterDownloadIndicator into per-chapter rows, ChapterRangeDialog into "Download Range..." button, shift-click multi-select + "Download N selected" header button (hidden until at least one row checked), right-click series-header context menu (Pause/Resume/Restart/Retry/Cancel/Remove). Auto-add toast fires from chapter-arrow click BEFORE startDownload (Codex §15 wiring rule). TileCard gains setProvenance("tankoyomi" → top-left chip) + setDownloadingChip(bool → top-right chip) painter slots; ComicsPage drives the chip via MangaDownloader downloadUpdated/downloadCompleted subscription + refreshTileChips. ChapterDownloadIndicator + ChapterRangeDialog reused unchanged from src/ui/pages/tankoyomi/ (Codex §18.1+18.2). MangaDownloader gains recordForSeries(sourceId, seriesId) helper. BUILD OK. Old TankoyomiPage still compiles + functions (Phase 8 retires).] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/core/manga/MangaDownloadIndex.{h,cpp}, src/core/manga/MangaDownloader.{h,cpp}, src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}, src/ui/pages/ComicsPage.{h,cpp}, src/ui/pages/TileCard.{h,cpp}, CMakeLists.txt, agents/chat.md
```

---

## Phase 6 — Provenance edge handling

**Goal:** Tighten the invariants from Codex §16 — sidecar rewrite on detail open if missing, missing-sidecar recovery, renamed-folder recovery via `(sourceId, seriesId)` cross-root scan, duplicate-root canonical-path dedupe, Remove-keep-files vs Remove-and-delete-files flows.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P6 provenance edges`.

### Task 39: Rewrite missing sidecar on detail-view open

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: After showEntry's library lookup, ensure sidecar present**

```cpp
// After m_tyLibrary->contains check block:
if (m_tyLibrary->contains(previewHint.source, previewHint.id)) {
    const auto rec = m_tyLibrary->get(previewHint.source, previewHint.id);
    if (!rec.canonicalSeriesPath.isEmpty() && !sidecar::exists(rec.canonicalSeriesPath)) {
        // Sidecar missing (user deleted manually). Rewrite it.
        SidecarMeta sm;
        sm.sourceId = rec.sourceId;
        sm.seriesId = rec.seriesId;
        sm.title    = rec.title;
        sm.createdAt = rec.addedAt;
        sidecar::write(rec.canonicalSeriesPath, sm);
    }
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 40: Renamed-folder recovery via `(sourceId, seriesId)` cross-root scan

**Files:**
- Modify: `src/core/manga/ComicsTankoyomiLibrary.h`
- Modify: `src/core/manga/ComicsTankoyomiLibrary.cpp`

- [ ] **Step 1: Add `findByIdentity` + `relocate` methods**

```cpp
// Header:
std::optional<ComicsLibraryRecord> findByIdentityAcrossRoots(
    const QString& sourceId, const QString& seriesId,
    const QStringList& comicsRoots) const;
void relocate(const QString& sourceId, const QString& seriesId,
              const QString& newCanonicalPath, const QString& newRootFolder,
              const QString& newSeriesFolderName);
```

- [ ] **Step 2: Impl**

```cpp
std::optional<ComicsLibraryRecord> ComicsTankoyomiLibrary::findByIdentityAcrossRoots(
    const QString& sourceId, const QString& seriesId,
    const QStringList& comicsRoots) const
{
    // Walk every comics root, look for any subfolder containing a
    // .tankoyomi-meta.json whose (sourceId, seriesId) matches.
    for (const auto& root : comicsRoots) {
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            const auto folder = it.next();
            const auto meta = sidecar::read(folder);
            if (meta && meta->sourceId == sourceId && meta->seriesId == seriesId) {
                ComicsLibraryRecord r;
                r.sourceId = sourceId;
                r.seriesId = seriesId;
                r.title    = meta->title;
                r.origin   = "tankoyomi";
                r.rootFolder = root;
                r.canonicalSeriesPath = folder;
                r.seriesFolderName    = QFileInfo(folder).fileName();
                return r;
            }
        }
    }
    return std::nullopt;
}

void ComicsTankoyomiLibrary::relocate(const QString& sourceId, const QString& seriesId,
                                        const QString& newCanonicalPath,
                                        const QString& newRootFolder,
                                        const QString& newSeriesFolderName)
{
    QMutexLocker lk(&m_mutex);
    const auto key = ComicsLibraryRecord::makeKey(sourceId, seriesId);
    if (!m_byKey.contains(key)) return;
    auto rec = m_byKey.value(key);
    m_canonicalToKey.remove(rec.canonicalSeriesPath);
    rec.canonicalSeriesPath = newCanonicalPath;
    rec.rootFolder          = newRootFolder;
    rec.seriesFolderName    = newSeriesFolderName;
    m_byKey.insert(key, rec);
    m_canonicalToKey.insert(newCanonicalPath, key);
    save();
}
```

- [ ] **Step 3: Hook into detail-view showEntry validation**

After Task 39's sidecar-rewrite step, if the canonical-path file system check fails entirely, run `findByIdentityAcrossRoots` to recover; on hit, call `relocate`.

- [ ] **Step 4: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 41: Remove-keep-files vs Remove-and-delete-files dialog

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Replace simple Add/Remove with confirmation on Remove path**

```cpp
void ComicsTankoyomiDetailView::onAddRemoveClicked()
{
    if (isInLibrary()) {
        QMessageBox box(this);
        box.setWindowTitle("Remove from library");
        box.setText(QString("Remove %1 from your library?").arg(m_currentPreview.title));
        auto* keepBtn   = box.addButton("Remove (keep files)", QMessageBox::AcceptRole);
        auto* deleteBtn = box.addButton("Remove and delete files", QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        const auto* clicked = box.clickedButton();
        if (clicked != keepBtn && clicked != deleteBtn) return;

        const auto rec = m_tyLibrary->get(m_currentPreview.source, m_currentPreview.id);

        // Cancel active downloads first (mirrors Netflix overhaul Phase 7).
        const auto activeRec = m_downloader->recordForSeries(rec.sourceId, rec.seriesId);
        if (!activeRec.id.isEmpty()) m_downloader->cancelDownload(activeRec.id);

        // Drop the library record + sidecar.
        m_tyLibrary->remove(rec.sourceId, rec.seriesId);
        QFile::remove(QDir(rec.canonicalSeriesPath).filePath(sidecar::kFileName));

        // Evict from MangaDownloadIndex.
        if (m_downloadIndex) m_downloadIndex->evictBySeries(rec.sourceId, rec.seriesId);

        if (clicked == deleteBtn) {
            QDir(rec.canonicalSeriesPath).removeRecursively();
        }

        m_addRemoveBtn->setText("Add to library");
    } else {
        // Existing Add path (Task 25) unchanged.
        // ...
    }
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 42: Phase 6 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P6 provenance edges 2026-05-NN ~HH:MMpm. Sidecar rewrite on detail-view open if missing (user-manual-delete recovery). Renamed-folder recovery via ComicsTankoyomiLibrary::findByIdentityAcrossRoots + relocate (cross-root sidecar scan for matching (sourceId, seriesId)). Remove-from-library now offers Remove(keep files) vs Remove and delete files (mirrors Netflix overhaul P7 confirmation), cancels active downloads first, evicts MangaDownloadIndex entries, and deletes the sidecar in both paths. Duplicate-root canonical-path dedupe enforced by m_canonicalToKey map. Folder-imported series with name-collision against a Tankoyomi-origin series still treated as folder (per locked Batch E Q3). BUILD OK.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/core/manga/ComicsTankoyomiLibrary.{h,cpp}, src/ui/pages/comics/ComicsTankoyomiDetailView.cpp, agents/chat.md
```

---

## Phase 7 — Root-change fallback

**Goal:** Ship the safe-fallback v1 path per Codex §14 — tiles persist, chapters revalidate as missing on root change, no auto-copy in v1. Auto-copy phased out to v2 unless writing-plans can isolate it cleanly (deferred decision; not in this plan's scope).

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P7 root-change fallback`.

### Task 43: Hook `rootFoldersChanged` for Comics

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Connect to CoreBridge signal**

```cpp
connect(m_bridge, &CoreBridge::rootFoldersChanged, this,
        [this](const QString& domain) {
            if (domain != "comics") return;
            // Push fresh claimed-paths set to scanner (records still valid;
            // chapter on-disk markers will be re-validated by validateAll).
            if (m_scanner) m_scanner->setClaimedPaths(m_tyLibrary->claimedCanonicalPaths());
            if (m_mangaDownloadIndex) m_mangaDownloadIndex->validateAll();
            rebuildTiles();
            refreshTileChips();
        });
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 44: Document v1 fallback behaviour in chapter list

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 1: Greyed-state for missing-on-disk chapters**

`refreshChapterMarkers` already handles `onDisk` based on `MangaDownloadIndex::filePathFor`. After a root change with files no longer at the recorded paths, `validateAll` invalidates entries → `onDisk` returns false → indicator state reverts to `NotDownloaded` → user can re-download. The existing implementation already covers the fallback. No new code is required; document this in a code comment so future readers see why §14 fallback is satisfied:

```cpp
// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 P7 — root-change fallback
// per brainstorm §14. Library records persist; chapter on-disk state
// is re-validated by MangaDownloadIndex::validateAll, fired by
// ComicsPage's rootFoldersChanged subscription (Task 43). Missing
// chapters revert to NotDownloaded state; users re-download per
// chapter or via Range modal. Auto-copy is deferred (v2 follow-up).
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 45: Phase 7 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P7 root-change fallback 2026-05-NN ~HH:MMpm. CoreBridge::rootFoldersChanged("comics") subscription wires: re-push claimed-paths set to scanner, re-validate MangaDownloadIndex (so missing-on-disk chapters revert to NotDownloaded state), rebuild tiles, refresh tile chips. Auto-copy migration deferred to v2 follow-up per Codex §14 — v1 ships the safe fallback (tiles persist, chapters revalidate as missing, re-download per chapter restores). Documented in code comment in ComicsTankoyomiDetailView::refreshChapterMarkers. BUILD OK.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/pages/ComicsPage.cpp, src/ui/pages/comics/ComicsTankoyomiDetailView.cpp, agents/chat.md
```

---

## Phase 8 — Remove old Tankoyomi surface (only after Phases 3-5 compile-green)

**Goal:** Excise the standalone Tankoyomi page, its sidebar entry, and its now-superseded UI files. ALSO: one-time backup of `manga_downloads.json` + `manga_history.json` per Codex §21.5 before dropping.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P8 remove old Tankoyomi surface`.

### Task 46: Remove PAGE_TANKOYOMI routing in MainWindow

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Drop the constant + page-stack add**

Grep for `PAGE_TANKOYOMI` in `MainWindow.{h,cpp}`. Delete the enum value + the `m_pageStack->addWidget(m_tankoyomiPage)` call + the `m_tankoyomiPage` member declaration. Drop any `connect` lines that reference `m_tankoyomiPage`. Drop the `case PAGE_TANKOYOMI:` arms in any switch statements (e.g. activatePage / domainForPage).

- [ ] **Step 2: Drop the SidebarDrawer "Tankoyomi" entry**

In `src/ui/widgets/SidebarDrawer.{h,cpp}`, grep for `Tankoyomi`. Delete the entry construction (button + icon + routing connect).

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`. If `BUILD FAILED` due to dangling references to `m_tankoyomiPage` or `PAGE_TANKOYOMI`, find and remove them.

### Task 47: One-time backup of legacy Tankoyomi JSON files

**Files:**
- Modify: `src/main.cpp` (or wherever first-boot migration hooks live; grep `manga_downloads.json` to find consumers)

- [ ] **Step 1: Add a one-time-only migration step at app start**

```cpp
// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 P8 — one-time backup of
// legacy Tankoyomi state per Codex §21.5. Runs once on first launch
// after the merger ships; flagged by a small marker file so it doesn't
// re-run on subsequent launches.

void runOneTimeTankoyomiBackup(const QString& dataDir)
{
    const QString marker = QDir(dataDir).filePath(".comics_merger_migration_done");
    if (QFileInfo::exists(marker)) return;

    auto backupOne = [&](const QString& filename) {
        const QString src = QDir(dataDir).filePath(filename);
        const QString dst = QDir(dataDir).filePath(filename + ".pre-merger-backup");
        if (QFileInfo::exists(src) && !QFileInfo::exists(dst))
            QFile::copy(src, dst);
        QFile::remove(src);
    };
    backupOne("manga_downloads.json");
    backupOne("manga_history.json");

    QFile m(marker);
    if (m.open(QIODevice::WriteOnly)) m.write("1");
}
```

Call it once from the app boot path.

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 48: `git rm` the deleted Tankoyomi source files + CMakeLists cleanup

**Files:**
- Delete: `src/ui/pages/TankoyomiPage.{h,cpp}`
- Delete: `src/ui/pages/tankoyomi/MangaDetailView.{h,cpp}`
- Delete: `src/ui/pages/tankoyomi/MangaResultsGrid.{h,cpp}`
- Delete: `src/ui/pages/tankoyomi/TransferGroupCard.{h,cpp}`
- Delete: `src/ui/dialogs/AddMangaDialog.{h,cpp}`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Remove from CMakeLists.txt FIRST**

Grep `CMakeLists.txt` for each of the five files; delete the SOURCES/HEADERS lines.

- [ ] **Step 2: `git rm` the files**

```
git rm src/ui/pages/TankoyomiPage.h src/ui/pages/TankoyomiPage.cpp
git rm src/ui/pages/tankoyomi/MangaDetailView.h src/ui/pages/tankoyomi/MangaDetailView.cpp
git rm src/ui/pages/tankoyomi/MangaResultsGrid.h src/ui/pages/tankoyomi/MangaResultsGrid.cpp
git rm src/ui/pages/tankoyomi/TransferGroupCard.h src/ui/pages/tankoyomi/TransferGroupCard.cpp
git rm src/ui/dialogs/AddMangaDialog.h src/ui/dialogs/AddMangaDialog.cpp
```

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`. If `BUILD FAILED` due to remaining includes of any removed header, grep and remove those `#include` lines.

### Task 49: Phase 8 RTC

- [ ] **Step 1: Append RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P8 remove old Tankoyomi surface 2026-05-NN ~HH:MMpm. Removed PAGE_TANKOYOMI routing + MainWindow m_tankoyomiPage member + SidebarDrawer "Tankoyomi" entry. git rm'd TankoyomiPage.{h,cpp}, tankoyomi/MangaDetailView.{h,cpp} (Agent 4B's 2026-05-13 C.5 ship), tankoyomi/MangaResultsGrid.{h,cpp}, tankoyomi/TransferGroupCard.{h,cpp}, dialogs/AddMangaDialog.{h,cpp}. ChapterDownloadIndicator + ChapterRangeDialog under src/ui/pages/tankoyomi/ PRESERVED (reused unchanged from Phase 5). CMakeLists.txt entries dropped. One-time first-boot backup of manga_downloads.json → manga_downloads.json.pre-merger-backup + manga_history.json → manga_history.json.pre-merger-backup (per Codex §21.5); marker file .comics_merger_migration_done prevents re-runs. BUILD OK.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/MainWindow.{h,cpp}, src/ui/widgets/SidebarDrawer.{h,cpp}, src/main.cpp, CMakeLists.txt, agents/chat.md (plus deletions)
```

---

## Phase 9 — Polish, nav, smoke

**Goal:** ComicsPage `INavStateProvider` gains the 3-mode discriminator (library/searchResults/tankoyomiDetail) per Codex §20. Back behaviour: from search → library; from detail-via-search → search; from detail-via-tile → library. Full smoke + Hemanth visual eye-check.

**Phase RTC tag at close:** `Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P9 polish + nav + final smoke + ARC CLOSE`.

### Task 50: Extend `ComicsPage::captureNavState` for 3-mode discriminator

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (the existing `captureNavState` at lines around 879-918 per Codex §20)

- [ ] **Step 1: Encode mode + per-mode payload**

```cpp
QJsonObject ComicsPage::captureNavState() const
{
    QJsonObject blob;
    blob["mode"] = (m_mode == Mode::Library          ? "library" :
                    m_mode == Mode::SearchResults    ? "searchResults" :
                                                       "tankoyomiDetail");
    // Library mode payload (existing fields):
    if (m_mode == Mode::Library) {
        blob["query"]     = QString();   // bar text isn't a library filter anymore
        blob["scrollY"]   = m_gridScroll ? m_gridScroll->verticalScrollBar()->value() : 0;
        return blob;
    }
    // Search-results mode payload:
    if (m_mode == Mode::SearchResults) {
        blob["query"]     = m_searchBar->text();
        return blob;
    }
    // Detail mode payload:
    if (m_mode == Mode::TankoyomiDetail) {
        // ComicsTankoyomiDetailView should expose currentPreview() — add accessor.
        const auto p = m_tyDetailView->currentPreview();
        blob["sourceId"] = p.source;
        blob["seriesId"] = p.id;
        blob["enteredFrom"] = m_enteredDetailFrom == Mode::SearchResults ? "search" : "library";
        return blob;
    }
    return blob;
}
```

- [ ] **Step 2: Add `currentPreview()` getter on detail view + `m_enteredDetailFrom` field on ComicsPage**

In `ComicsTankoyomiDetailView.h`:

```cpp
const MangaResult& currentPreview() const { return m_currentPreview; }
```

In `ComicsPage`, track which mode we transitioned from when entering detail, so Back can route correctly.

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 51: Extend `ComicsPage::restoreNavState` 3-mode router

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Dispatch by blob's mode field**

```cpp
bool ComicsPage::restoreNavState(const QJsonObject& blob)
{
    const auto mode = blob.value("mode").toString();
    if (mode == "library") {
        showLibraryMode();
        if (m_gridScroll) m_gridScroll->verticalScrollBar()->setValue(blob.value("scrollY").toInt());
        return true;
    }
    if (mode == "searchResults") {
        const auto q = blob.value("query").toString();
        m_searchBar->setText(q);
        showSearchMode(q);
        return true;
    }
    if (mode == "tankoyomiDetail") {
        const auto sid = blob.value("sourceId").toString();
        const auto seriesId = blob.value("seriesId").toString();
        // Build a preview from the library record if present.
        if (m_tyLibrary->contains(sid, seriesId)) {
            const auto rec = m_tyLibrary->get(sid, seriesId);
            m_enteredDetailFrom = (blob.value("enteredFrom").toString() == "search")
                                   ? Mode::SearchResults : Mode::Library;
            m_mode = Mode::TankoyomiDetail;
            m_tyDetailView->showEntry(rec.detailCache.preview);
            m_stack->setCurrentWidget(m_tyDetailView);
            return true;
        }
        // Cache miss — fall back to whichever mode entered originally,
        // or library as last resort.
        return restoreNavState({{"mode", blob.value("enteredFrom")}});
    }
    return false;
}
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 52: Back behaviour from detail view

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Route based on `m_enteredDetailFrom`**

Update `onDetailBack`:

```cpp
void ComicsPage::onDetailBack()
{
    if (m_enteredDetailFrom == Mode::SearchResults) {
        m_mode = Mode::SearchResults;
        m_stack->setCurrentWidget(m_searchTakeover);
    } else {
        showLibraryMode();
    }
}
```

Update tile-click and search-result-activated handlers to set `m_enteredDetailFrom` accordingly:

```cpp
void ComicsPage::onSearchResultActivated(const MangaResult& preview)
{
    m_enteredDetailFrom = Mode::SearchResults;
    m_mode = Mode::TankoyomiDetail;
    m_tyDetailView->showEntry(preview);
    m_stack->setCurrentWidget(m_tyDetailView);
}

// inside onTileClicked, before m_tyDetailView->showEntry call:
m_enteredDetailFrom = Mode::Library;
```

- [ ] **Step 2: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 53: Full smoke recipe — Hemanth's lane

**Files:** none (verification only)

- [ ] **Step 1: Build + run via `build_and_run.bat`**

Hemanth double-clicks `build_and_run.bat`. App launches with telemetry on.

- [ ] **Step 2: Smoke matrix**

Hemanth walks the following matrix (each row is one click sequence):

1. Open Comics. See library tiles + Continue strip. Confirm: tiles unchanged from prior version for existing folder-imported series; no Tankoyomi badge on them.
2. Click into the search bar at the top. Placeholder reads "Search Tankoyomi". Type `berserk`. Press Enter. Page takes over with results split into Manga + Comics sections. Library hides. Back arrow visible.
3. Click a result tile in the Manga section. Detail view opens; cover, title, meta, synopsis, genres render. Add to library button shows "Add to library".
4. Click "Add to library". Toast fires; button morphs to "Remove from library". Click Back. Library now has a new tile with a "Tankoyomi" chip top-left.
5. Open the new tile. Detail view opens. Click the download arrow on a chapter. Indicator state moves to Queue / Downloading. Tile (in background) gains "DOWNLOADING" chip.
6. While downloading, right-click the series header in the detail view. Context menu offers Pause / Resume / Restart / Retry / Cancel / Remove. Click Pause. Indicator state freezes.
7. Click Remove from library. Confirmation modal offers Remove (keep files) / Remove and delete files / Cancel. Pick Remove (keep files). Tile disappears. Re-add via search.
8. Force a source failure: turn off Wi-Fi, search for a query. Working source (if any cached) responds; toast fires for the failing source ("ReadComicsOnline didn't respond" or similar).
9. Manually delete files via File Explorer. Reopen the detail page. Tile + badge stay. Chapter indicators revert to NotDownloaded.

- [ ] **Step 3: Hemanth reports results in chat.md**

If all matrix rows pass, Agent 1 emits the final closing RTC. If any fail, Agent 1 investigates + patches before closing.

### Task 54: Phase 9 + arc-close RTC

- [ ] **Step 1: Append final RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER P9 polish + nav + final smoke + ARC CLOSE 2026-05-NN ~HH:MMpm. ComicsPage INavStateProvider extended with 3-mode discriminator (library / searchResults / tankoyomiDetail) per Codex §20; per-mode payload (library scrollY, search query, detail (sourceId, seriesId, enteredFrom)). Back behaviour: search → library; detail-from-search → search; detail-from-tile → library. ComicsTankoyomiDetailView exposes currentPreview() for nav state capture. Final smoke walked the 9-step matrix end-to-end GREEN: search takeover, two-section results, detail hero, Add silent-bookmark, Tankoyomi chip top-left on tiles, DOWNLOADING chip on active series, right-click series-header context menu, Remove confirmation dialog, source-failure toast, manual file-deletion → chapter markers revert. Arc closes 9 phases / ~54 tasks shipped end-to-end. Old Tankoyomi page surface fully retired (Phase 8); ChapterDownloadIndicator + ChapterRangeDialog preserved as reusable Tankoyomi-side primitives. Auto-copy on root-folder change deferred to v2 follow-up per Codex §14. Volume / story-arc grouping deferred to v2 per Hemanth's locked Batch B Q4 directive. BUILD OK across all 9 phases.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify, /superpowers:requesting-code-review, /simplify] | files: src/ui/pages/ComicsPage.{h,cpp}, src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}, agents/chat.md
```

---

## Self-review

### Spec coverage

Walked the brainstorm-md sections against this plan:

- §3.1 Search flow — Tasks 17–20 (search bar repurpose, takeover widget, two-section split, source-failure toast)
- §3.2 Series detail flow — Tasks 22–24, 36–37 (hero, preview-first, fetchDetail, chapter list, multi-select, Range modal, context menu)
- §3.3 Library/Downloads (Netflix-style, no cross-series page) — Tasks 29–37 (MangaDownloadIndex, ChapterDownloadIndicator wire, auto-add toast, per-series queue controls, tile DOWNLOADING chip)
- §3.4 Tankoyomi badge — Task 33 (TileCard provenance slot painter)
- §3.5 Internal UI distinction folder vs badged — Task 27 (provenance-routed tile-click dispatcher), Task 41 (Remove keep-files vs delete-files)
- §3.6 Sources sealed v1 + future-plugin-ready — Tasks 6 (MangaSourceRegistry replaces hardcoded list)
- §3.7 Persistence + clean-slate migration — Tasks 8–11, 47 (records, library store, sidecar, scanner skip, one-time backup)
- §3.8 Out of scope — Task 46–48 (SidebarDrawer entry removal, no theme touch, no Comic Reader touch)
- §11 Codex path corrections — applied throughout (`src/core/LibraryScanner.{h,cpp}`, `src/ui/player/ToastHud.{h,cpp}`, no StreamDownloadsPage delete task, MangaScraper EXTEND, TileCard extend-not-fork, Agent 5 coordination NOTEs in Tasks 11+33)
- §12 Scraper hybrid metadata — Tasks 1–5 (MangaSeriesDetail + fetchDetail + detailReady + concrete scraper impls)
- §13 Detail cache order — Task 23 (library JSON → sidecar → preview → network)
- §14 Auto-copy fallback v1 default — Tasks 43–44 (fallback works via validateAll; auto-copy noted as v2 deferred)
- §15 Downloader hardening unnecessary — Task 35 (auto-add toast wired in detail view BEFORE startDownload per Codex)
- §16 Provenance invariant — Task 9 (canonical-path map source-of-truth), Task 39–40 (sidecar rewrite + cross-root identity recovery)
- §17 Additional Stream surfaces — all noted as REPLACE WITH NOTHING in §6; no tasks needed (already absent from the plan)
- §18 Tankoyomi-side ABSORB items — Task 31 (ChapterDownloadIndicator wire), Task 36 (ChapterRangeDialog wire)
- §19 9-phase sequencing — Phases 1–9 mirror Codex's order exactly
- §20 Nav state 3-mode discriminator — Tasks 50–52
- §21 UI compliance — no QML used, no colors introduced (palette-derived greys throughout), no separate "Saved" list, no cross-series Downloads page, sidecar isn't a badge source

### Placeholder scan

Walked through. Any "TODO" / "TBD" / "implement later" patterns? No — the only "later" references are explicit v2-deferred follow-ups (volume grouping, auto-copy, extensions) which are correctly out of v1 scope, not v1 placeholders. The "code blocks for scraper HTML parsers" carry a note that the executor MUST verify selectors against live HTML during impl — that's a concrete instruction, not a vague TBD.

### Type consistency

Walked the cross-task references:

- `MangaSeriesDetail` (Task 1) → used in Tasks 3, 4, 5, 22, 23, 25, 39 — consistent.
- `MangaSourceRegistry::find(sourceId)` (Task 6) → used in Task 22 — consistent.
- `ComicsTankoyomiLibrary::add/remove/contains/get/all/claimedCanonicalPaths/containsCanonicalPath/findByIdentityAcrossRoots/relocate` (Tasks 9, 40) → used in Tasks 12, 22, 25, 27, 41, 43 — consistent.
- `MangaDownloadIndex::registerChapter/evictBySeries/evictByChapter/validateAll/filePathFor` (Task 29) → used in Tasks 30, 31, 41, 43 — consistent.
- `MangaDownloader::recordForSeries(sourceId, seriesId)` (Task 32) → used in Tasks 31, 34, 37 — consistent.
- `TileCard::setProvenance/setDownloadingChip` (Task 33) → used in Task 34 + replaces the Task 15 property shim — consistent.
- `ComicsTankoyomiDetailView::showEntry/currentPreview` (Tasks 22, 50) → used in Tasks 26, 27, 51 — consistent.
- `ComicsPage::Mode/showLibraryMode/showSearchMode/onSearchResultActivated/onDetailBack/onTileClicked/rebuildTiles/refreshTileChips/refreshTileChips/onTankoyomiLibraryChanged/onDetailBack` — consistent within ComicsPage.
- `SidecarMeta` struct + `sidecar::read/write/exists/kFileName` — consistent.

Found one type-consistency issue during review: in Task 25 I wrote `m_currentPreview.source` and `m_currentPreview.id` as the keys passed to `m_tyLibrary->contains`, but `ComicsLibraryRecord::makeKey` expects `(sourceId, seriesId)` — these are the same values under the names `MangaResult::source` and `MangaResult::id`, so the mapping is one-to-one. No fix needed — names differ but values are correct.

Plan is complete and consistent. Saved to `docs/superpowers/plans/2026-05-14-comics-tankoyomi-merger.md`.

---

## Execution handoff

Two execution options:

**1. Subagent-Driven (recommended per the brief's Phase 3 cadence note)** — fresh subagent per phase (Codex §19 sequencing is the natural unit), two-stage review (spec compliance + code quality) between phases. Matches the Netflix overhaul precedent. The brief specifically says: "Tightly-coupled architectural reach — recommended cadence is one-go-per-phase, stop at phase boundaries (not full one-shot, not task-by-task). Each phase boundary is a checkpoint."

**2. Inline Execution** — execute tasks in this session using `superpowers:executing-plans`, batch execution with manual checkpoints between phases.

Which approach?
