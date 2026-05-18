# Tankoyomi Volume Pivot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pivot Comics mode to a Stremio-shape volume-only experience. Volume is the only first-class UI unit. AniList drives metadata + chapter-to-volume mapping. Catalog (precision) + nyaa runtime (uncurated) + WeebCentral packer (fallback) feed the right-side Sources panel inside a forked Theatre-shape series view.

**Architecture:** Path A — fork `StreamDetailView` into `ComicsSeriesView`; v2 will generalize into a shared widget after both views mature. Backend-first phase order: AniList client+cache+mapper → source providers → pre-pivot migrator (burn-it-down) → forked series view → sources panel → search refactor → comics landing refactor → coordinator extension → cover integration → smoke matrix. The Phase 1-5 + 9-facade + 10 plumbing from the prior 11-phase TANKOYOMI_PREMIUM_MVP arc all survives; the Phase 6 / Phase 7 / Phase 8 / Phase 9-adopt-folder UI gets ripped out.

**Tech Stack:** Qt6 (QNetworkAccessManager for AniList GraphQL, QJsonDocument for parsing, QFile/QSaveFile for cache, QZipWriter for synthesized cbz assembly), libtorrent-rasterbar (existing engine, no changes), QZipReader (existing, via `Qt6::CorePrivate`), GoogleTest via FetchContent (only for pure-logic primitives that compile in `tankoban_tests`). MCP for visual smokes (pywinauto-mcp + tankoctl dev-control bridge). Playwright MCP for WeebCentral + nyaa selector pre-flight when needed.

**Spec:** `docs/superpowers/specs/2026-05-16-tankoyomi-volume-pivot-design.md`. 12 locked decisions captured there. This plan implements that spec.

**Brotherhood conventions:** master only, no worktrees, no per-task commits (RTC-in-chat-md pattern; Agent 0 batches via `/commit-sweep`), ASCII-only, smoke-first (TDD opt-in only for pure-logic primitives that fit `tankoban_tests`), build_check after every major edit group, MCP LOCK claim+release around any desktop smoke.

---

## File Structure

This plan locks the decomposition decisions. Each file has one clear responsibility.

**New code (12 files):**

| File | Purpose |
|---|---|
| `src/core/manga/anilist/AniListTypes.h` | POD struct definitions (MediaPreview, MediaDetail, AniListChapter, AniListVolume, VolumeRow). Pure data, no Qt UI dependency. |
| `src/core/manga/anilist/AniListClient.h/.cpp` | GraphQL HTTP client via QNetworkAccessManager. Two query shapes: `searchByTitle` + `seriesById`. Public AniList endpoint, unauthenticated. Internal 1-req/sec throttle to stay well under the 90/min cap. |
| `src/core/manga/anilist/AniListCache.h/.cpp` | JSON file-backed cache at `<appData>/anilist_cache/`. Refresh-on-open semantics. Bookmark-aware (bookmarked series never evicted). |
| `src/core/manga/anilist/AniListVolumeMapper.h/.cpp` | Pure function: turns AniList chapter list + bound-vol info into `QList<VolumeRow>` with Vol X synthesis. No I/O, no Qt UI — pure unit-testable. |
| `src/core/manga/NyaaRuntimeSource.h/.cpp` | Runtime nyaa.si query with uploader-trust filter. Parses nyaa's RSS or HTML response. Returns ranked `NyaaSourceCandidate` list. |
| `src/core/manga/WeebCentralVolumePacker.h/.cpp` | Sibling to TorrentVolumeProvider on the source layer. HTTP-fetches AniList-mapped chapters, zips into one cbz, fires same `volumeCompleted` signal shape so downstream code does not care which source completed. |
| `src/core/manga/ComicsPrePivotMigrator.h/.cpp` | Burn-it-down on first post-pivot launch. Move pre-pivot files to `<appData>/comics_pre_pivot_backup/`, wipe MangaDownloadIndex + Tankoyomi library, preserve manga_posters cache. Idempotent. |
| `src/ui/pages/comics/ComicsSeriesView.h/.cpp` | Forked from `src/ui/pages/stream/StreamDetailView.{h,cpp}`. Banner + meta + volume list + sources panel layout. Replaces ComicsTankoyomiDetailView. |
| `src/ui/pages/comics/ComicsSourcesPanel.h/.cpp` | Right-side sources panel widget inside ComicsSeriesView. Empty-state until a volume row is clicked, then populates with ranked source rows. |
| `resources/manga_uploader_trust.json` | Tier-1 uploader allowlist. `{"tier1": ["1r0n", "Hox", "VIZ Digital"]}`. Loaded at startup; informs NyaaRuntimeSource ranking. |
| `tests/core/manga/AniListVolumeMapperTest.cpp` | GoogleTest-based unit tests for AniListVolumeMapper. Compiles into the existing `tankoban_tests` target. |

**Modified files (5):**

| File | What changes |
|---|---|
| `src/ui/pages/ComicsPage.h/.cpp` | Drop existing detail-view + search-routing + adopt-callback wires; add bookmark store + library tile rendering (downloaded vols + bookmarked series); wire ComicsSeriesView; bootstrap ComicsPrePivotMigrator on first construction. |
| `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h/.cpp` | Collapse to single AniList-primary list. Delete Premium section + chip routing + injectPremiumCatalogSynthetics. Forward result clicks straight to ComicsSeriesView. |
| `src/core/manga/MangaTransferCoordinator.cpp` | Add WeebCentralVolumePacker to the pauseAll/resumeAll fan-out as a third backend. |
| `src/main.cpp` | Bootstrap AniListClient + AniListCache singletons during the existing manga-premium-dirs bootstrap block. Run ComicsPrePivotMigrator before MainWindow construction. |
| `CMakeLists.txt` | Register the 9 new .cpp + 9 new .h files in the manga source group; register AniListVolumeMapperTest.cpp in the `tankoban_tests` target. |

**Deleted files (2):**

| File | Why |
|---|---|
| `src/ui/pages/comics/ComicsTankoyomiDetailView.h/.cpp` | Replaced by ComicsSeriesView. The 5-column volume-row table + filter-chip row + Premium-branch logic in renderChapters all goes away. |

**Surviving files from 11-phase arc (no change or minimal):** PremiumCatalog + schema, premium_catalog_helper Python tool, TorrentVolumeProvider, PremiumArchiveValidator, TorrentRequestLedger, MangaDownloadIndex (registerVolume + canonical keys), PremiumCoverExtractor, MangaTransferCoordinator (extended in P11), CanonicalChapterKey helper.

---

## Phase 0 — Brotherhood conventions in effect

Before any code is written, the executor (whoever runs the next phase) acknowledges:

- **Master only.** No worktrees, no feature branches. Per `feedback_no_worktrees.md`.
- **No per-task commits.** Brotherhood pattern is RTC line in `agents/chat.md` at phase close. Agent 0 batches commits via `/commit-sweep`.
- **ASCII only in source + RTCs.** No em-dashes, smart quotes, curly punctuation in code or chat.md. Per `feedback_no_color_no_emoji.md`.
- **build_check after every meaningful edit group.** `build_check.bat` returns `BUILD OK` or `BUILD FAILED exit=<n>` with cl.exe tail. The build-verify slash command runs the same. Per CLAUDE.md Build Quick Reference.
- **Smoke-first.** Pure-logic primitives (only AniListVolumeMapper in this plan) get TDD via `tankoban_tests`. Everything else gets a build-verify + MCP-driven or code-level smoke. Per CLAUDE.md Tier 1 + Codex #4 Stage 3a.
- **MCP LOCK around any desktop interaction.** Claim+release `MCP LOCK - [Agent 1, ...]` in chat.md. Per Rule 19.
- **Rule 17 cleanup post-smoke.** `taskkill /F /IM Tankoban.exe` + `taskkill /F /IM ffmpeg_sidecar.exe` before exit. Per `feedback_mcp_smoke_discipline.md`.
- **Skills invoked tracked in RTC.** Per contracts-v3, every non-trivial RTC carries a `Skills invoked: [...]` field.

If any of these conventions are unclear, read CLAUDE.md "Hemanth's Role" + "Build Quick Reference" sections before starting.

---

## Phase 1 — AniList types + GraphQL client

**Phase goal:** Establish the AniList POD types and a working GraphQL client that can issue `searchByTitle` and `seriesById` queries against the public AniList endpoint. No caching yet (Phase 2 adds that); no UI integration (Phase 7+ adds that).

**Files this phase touches:**

- Create: `src/core/manga/anilist/AniListTypes.h`
- Create: `src/core/manga/anilist/AniListClient.h`
- Create: `src/core/manga/anilist/AniListClient.cpp`
- Modify: `CMakeLists.txt` (register new sources)

**Reference design sections:** §1 (vision — AniList is the metadata backbone), §2 decision 6 + 7 (AniList-primary search), §4 component 1 (AniListClient).

### Task 1.1: Define POD types

**Files:**
- Create: `src/core/manga/anilist/AniListTypes.h`

- [ ] **Step 1.1.1: Write the header**

```cpp
// src/core/manga/anilist/AniListTypes.h
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace tankoban::manga::anilist {

// Single chapter as AniList tracks it. AniList exposes chapter metadata on
// Media via the `chapters` count (when available) but per-chapter detail
// (number + title + bound-volume) is sparser. For series with explicit
// per-chapter volume mappings we use those; for others we use the
// series-level `volumes` count as the binding boundary heuristic.
struct AniListChapter {
    QString number;       // string to allow "12.5" half-chapters
    QString title;        // can be empty
    int     boundVolume;  // -1 when unbound; 1..N when bound to a vol
};

// Per-volume art reference. AniList exposes `Media.coverImage` (series-
// level) but per-volume art lives on related Volume records (not always
// populated by the database). When per-volume art is unavailable, fall
// back to series-level coverImage.
struct AniListVolumeArt {
    QString thumbnailUrl;  // 256px-ish, fast for grid rendering
    QString fullUrl;       // higher-res; used in the detail-view hero
};

// AniList Media node, slimmed to fields we use.
struct MediaPreview {
    int         anilistId      = 0;
    QString     title;             // primary display title
    QStringList alternateTitles;   // romaji + native + synonyms (English-first ordering applied client-side)
    QString     coverThumbUrl;     // small cover for search-result tile
    QString     coverFullUrl;      // larger cover for detail-view hero
    QString     bannerUrl;         // wide banner for detail-view hero; may be empty
    QString     format;            // "MANGA" / "MANHWA" / "MANHUA" / "ONE_SHOT" / "NOVEL"
    QString     status;            // "FINISHED" / "RELEASING" / "HIATUS" / "CANCELLED" / "NOT_YET_RELEASED"
    int         yearStarted       = 0;
    QStringList genres;
    QString     description;       // raw HTML/BBCode; stripped to plain text by display layer
};

// Full series detail. Includes per-chapter binding info needed by
// AniListVolumeMapper.
struct MediaDetail {
    MediaPreview            preview;
    int                     totalChapters = 0;  // 0 when unknown / ongoing
    int                     totalVolumes  = 0;  // 0 when unknown / ongoing
    QList<AniListChapter>   chapters;            // sorted ascending by chapter number
    QList<AniListVolumeArt> volumeArt;           // optional, indexed by vol number - 1; empty entries = use series cover
    qint64                  fetchedAtMs   = 0;  // for cache freshness checks
};

// Output of AniListVolumeMapper. The series view renders one row per
// VolumeRow.
struct VolumeRow {
    int                     volumeNumber;   // 1..N for bound vols; sentinel kVolumeXNumber for the un-bound tail
    bool                    isVolumeX;       // true when this row is the synthesized Vol X
    int                     chapterRangeStart; // first chapter number (numeric extract) in this vol
    int                     chapterRangeEnd;   // last chapter number in this vol
    int                     chapterCount;      // count of AniListChapter entries in this vol
    QStringList             chapterNumbers;    // the raw chapterNumber strings (preserves "12.5" etc.)
    AniListVolumeArt        art;               // per-vol art when available; else fall back to series cover
};

// Sentinel for VolumeRow.volumeNumber when isVolumeX is true. Chosen as
// a high integer so any normal vol comparison still works.
constexpr int kVolumeXNumber = 99999;

} // namespace tankoban::manga::anilist
```

- [ ] **Step 1.1.2: Add to CMakeLists.txt**

In `CMakeLists.txt`, locate the manga-domain headers group (where prior phases added entries like `PremiumCatalog.h`, `MangaDownloadIndex.h`, `TorrentVolumeProvider.h`). Append:

```cmake
    src/core/manga/anilist/AniListTypes.h
```

- [ ] **Step 1.1.3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. No symbols defined yet (header-only POD), but it should parse and CMake should find the file.

### Task 1.2: AniList GraphQL client

**Files:**
- Create: `src/core/manga/anilist/AniListClient.h`
- Create: `src/core/manga/anilist/AniListClient.cpp`

- [ ] **Step 1.2.1: Write the header**

```cpp
// src/core/manga/anilist/AniListClient.h
#pragma once

#include "AniListTypes.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::anilist {

// AniList GraphQL client. Endpoint: https://graphql.anilist.co
// Unauthenticated. Rate limit: 90 requests per minute. We throttle to
// 1 request per second internally to stay well under that ceiling.
//
// Two query shapes:
//   - searchByTitle(query)  -> list of MediaPreview tiles for the search UI
//   - seriesById(anilistId) -> full MediaDetail incl. chapter+volume binding
//
// Both return via signals so the UI thread is not blocked. Failure modes
// surface via the *Failed signals with a human-readable reason; the UI
// either shows a stale-cache fallback or a 'try again' affordance.
class AniListClient : public QObject
{
    Q_OBJECT
public:
    explicit AniListClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~AniListClient() override;

    // Fire a search. Result lands on searchSucceeded or searchFailed with
    // a matching `requestId` (caller-generated) so concurrent searches
    // can be disambiguated.
    void searchByTitle(const QString& query, int requestId);

    // Fire a per-series fetch. requestId is the caller's chosen tag for
    // pairing the response with the originating request.
    void seriesById(int anilistId, int requestId);

signals:
    void searchSucceeded(int requestId, const QList<MediaPreview>& results);
    void searchFailed(int requestId, const QString& reason);

    void seriesSucceeded(int requestId, const MediaDetail& detail);
    void seriesFailed(int requestId, const QString& reason);

private slots:
    void onSearchReplyFinished();
    void onSeriesReplyFinished();

private:
    void fireQuery(const QByteArray& body, QNetworkReply** outReply);
    void throttleIfNeeded();

    QPointer<QNetworkAccessManager> m_nam;
    qint64 m_lastRequestMs = 0;  // simple 1-req/sec throttle

    // Pending request -> requestId map. We use Qt property setters on
    // QNetworkReply to thread the caller's requestId through to the
    // slot, rather than a separate map.
};

} // namespace tankoban::manga::anilist
```

- [ ] **Step 1.2.2: Write the implementation**

```cpp
// src/core/manga/anilist/AniListClient.cpp
#include "AniListClient.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>

namespace tankoban::manga::anilist {

namespace {

constexpr const char* kEndpoint = "https://graphql.anilist.co";
constexpr qint64 kMinIntervalMs = 1000;  // 1 req/sec internal throttle

constexpr const char* kSearchQuery = R"(
query ($search: String) {
  Page(page: 1, perPage: 25) {
    media(search: $search, type: MANGA) {
      id
      title { romaji english native userPreferred }
      synonyms
      coverImage { medium large extraLarge }
      bannerImage
      format
      status
      startDate { year }
      genres
      description(asHtml: false)
    }
  }
}
)";

constexpr const char* kSeriesQuery = R"(
query ($id: Int) {
  Media(id: $id, type: MANGA) {
    id
    title { romaji english native userPreferred }
    synonyms
    coverImage { medium large extraLarge }
    bannerImage
    format
    status
    startDate { year }
    genres
    description(asHtml: false)
    chapters
    volumes
  }
}
)";
// NOTE: AniList does NOT expose per-chapter volume mapping via the public
// GraphQL schema as of 2026-05. The Media.chapters count + Media.volumes
// count are available, but the per-chapter binding is sparse. Our v1
// strategy: when the series has both totals, AniListVolumeMapper uses
// a chapter-count-per-volume heuristic (chapters / volumes, integer-
// floor). When the per-chapter list comes from WeebCentral (which we
// already scrape) and the AniList totals say how many vols exist, we
// map by ascending chapter number into equal-sized vol buckets. Vol X
// holds the residual unbound chapters past `volumes * (chapters/volumes)`.
// Plan-time decision: this is approximate but tractable; the alternative
// (a different metadata source) is out of scope for v1.

QByteArray makeRequestBody(const char* query, const QJsonObject& variables)
{
    QJsonObject body;
    body["query"] = QString::fromLatin1(query);
    body["variables"] = variables;
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString pickTitle(const QJsonObject& titleObj)
{
    // Prefer English, then romaji, then native.
    if (titleObj.contains("english") && !titleObj.value("english").isNull()) {
        const QString s = titleObj.value("english").toString();
        if (!s.isEmpty()) return s;
    }
    if (titleObj.contains("romaji") && !titleObj.value("romaji").isNull()) {
        const QString s = titleObj.value("romaji").toString();
        if (!s.isEmpty()) return s;
    }
    if (titleObj.contains("native") && !titleObj.value("native").isNull()) {
        const QString s = titleObj.value("native").toString();
        if (!s.isEmpty()) return s;
    }
    return QString();
}

QStringList collectAlternateTitles(const QJsonObject& mediaObj)
{
    QStringList out;
    const QJsonObject t = mediaObj.value("title").toObject();
    for (const auto& key : { "english", "romaji", "native", "userPreferred" }) {
        const QString s = t.value(key).toString();
        if (!s.isEmpty() && !out.contains(s)) out.append(s);
    }
    const QJsonArray syn = mediaObj.value("synonyms").toArray();
    for (const auto& v : syn) {
        const QString s = v.toString();
        if (!s.isEmpty() && !out.contains(s)) out.append(s);
    }
    return out;
}

MediaPreview parsePreview(const QJsonObject& mediaObj)
{
    MediaPreview p;
    p.anilistId       = mediaObj.value("id").toInt();
    p.title           = pickTitle(mediaObj.value("title").toObject());
    p.alternateTitles = collectAlternateTitles(mediaObj);
    const QJsonObject cover = mediaObj.value("coverImage").toObject();
    p.coverThumbUrl   = cover.value("medium").toString();
    p.coverFullUrl    = cover.value("large").toString();
    if (p.coverFullUrl.isEmpty()) {
        p.coverFullUrl = cover.value("extraLarge").toString();
    }
    p.bannerUrl       = mediaObj.value("bannerImage").toString();
    p.format          = mediaObj.value("format").toString();
    p.status          = mediaObj.value("status").toString();
    p.yearStarted     = mediaObj.value("startDate").toObject().value("year").toInt();
    const QJsonArray genres = mediaObj.value("genres").toArray();
    for (const auto& v : genres) p.genres.append(v.toString());
    p.description     = mediaObj.value("description").toString();
    return p;
}

} // anonymous namespace

AniListClient::AniListClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    Q_ASSERT(m_nam);
}

AniListClient::~AniListClient() = default;

void AniListClient::throttleIfNeeded()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_lastRequestMs;
    if (m_lastRequestMs > 0 && elapsed < kMinIntervalMs) {
        QThread::msleep(static_cast<unsigned long>(kMinIntervalMs - elapsed));
    }
    m_lastRequestMs = QDateTime::currentMSecsSinceEpoch();
}

void AniListClient::searchByTitle(const QString& query, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("network manager unavailable"));
        return;
    }
    throttleIfNeeded();

    QJsonObject variables;
    variables["search"] = query;
    const QByteArray body = makeRequestBody(kSearchQuery, variables);

    QNetworkRequest req(QUrl(QString::fromLatin1(kEndpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->post(req, body);
    reply->setProperty("anilist_requestId", requestId);
    connect(reply, &QNetworkReply::finished,
            this, &AniListClient::onSearchReplyFinished);
}

void AniListClient::seriesById(int anilistId, int requestId)
{
    if (!m_nam) {
        emit seriesFailed(requestId, QStringLiteral("network manager unavailable"));
        return;
    }
    throttleIfNeeded();

    QJsonObject variables;
    variables["id"] = anilistId;
    const QByteArray body = makeRequestBody(kSeriesQuery, variables);

    QNetworkRequest req(QUrl(QString::fromLatin1(kEndpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->post(req, body);
    reply->setProperty("anilist_requestId", requestId);
    connect(reply, &QNetworkReply::finished,
            this, &AniListClient::onSeriesReplyFinished);
}

void AniListClient::onSearchReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property("anilist_requestId").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit searchFailed(requestId, QStringLiteral("json parse: ") + err.errorString());
        return;
    }
    const QJsonObject root = doc.object();
    if (root.contains("errors")) {
        emit searchFailed(requestId, QStringLiteral("graphql errors in response"));
        return;
    }
    const QJsonArray media = root.value("data").toObject()
                                 .value("Page").toObject()
                                 .value("media").toArray();
    QList<MediaPreview> out;
    for (const auto& v : media) {
        out.append(parsePreview(v.toObject()));
    }
    emit searchSucceeded(requestId, out);
}

void AniListClient::onSeriesReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property("anilist_requestId").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit seriesFailed(requestId, reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit seriesFailed(requestId, QStringLiteral("json parse: ") + err.errorString());
        return;
    }
    const QJsonObject root = doc.object();
    if (root.contains("errors")) {
        emit seriesFailed(requestId, QStringLiteral("graphql errors in response"));
        return;
    }
    const QJsonObject mediaObj = root.value("data").toObject().value("Media").toObject();

    MediaDetail detail;
    detail.preview       = parsePreview(mediaObj);
    detail.totalChapters = mediaObj.value("chapters").toInt(0);
    detail.totalVolumes  = mediaObj.value("volumes").toInt(0);
    detail.fetchedAtMs   = QDateTime::currentMSecsSinceEpoch();
    // chapters[] + volumeArt[] populated by callers (AniListVolumeMapper
    // synthesizes from WeebCentral when chapters list isn't on AniList).
    emit seriesSucceeded(requestId, detail);
}

} // namespace tankoban::manga::anilist
```

- [ ] **Step 1.2.3: Wire into CMakeLists.txt**

In `CMakeLists.txt` manga sources group:

```cmake
    src/core/manga/anilist/AniListClient.cpp
```

In headers group:

```cmake
    src/core/manga/anilist/AniListClient.h
```

- [ ] **Step 1.2.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. AniListClient compiles clean; nothing wired to it yet.

### Task 1.3: Phase 1 close — RTC line

- [ ] **Step 1.3.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 1 - AniList POD types + GraphQL client. New: AniListTypes.h (MediaPreview, MediaDetail, AniListChapter, AniListVolumeArt, VolumeRow with kVolumeXNumber sentinel), AniListClient.h/.cpp (QNetworkAccessManager-driven GraphQL client, two queries searchByTitle + seriesById, 1-req/sec internal throttle, requestId pairing via Qt property setter, English-first title pick + native/romaji/synonym alternate-title collection, robust JSON parse with graphql-errors detection). Endpoint graphql.anilist.co, unauthenticated, 90-req/min cap. Per-chapter volume mapping limitation documented inline (AniList GraphQL exposes Media.chapters + Media.volumes counts but not the per-chapter binding; Phase 3 AniListVolumeMapper uses chapters/volumes-floor heuristic). build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/anilist/AniListTypes.h, src/core/manga/anilist/AniListClient.h, src/core/manga/anilist/AniListClient.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 2 — AniListCache

**Phase goal:** JSON file-backed local cache at `<appData>/anilist_cache/`. Refresh-on-open semantics with bookmark-awareness. AniListClient results land here; AniListVolumeMapper + the series view both read from here. Survives app restart.

**Files this phase touches:**

- Create: `src/core/manga/anilist/AniListCache.h`
- Create: `src/core/manga/anilist/AniListCache.cpp`
- Modify: `CMakeLists.txt`

**Reference design sections:** §2 decision 12 (cache on first fetch, refresh on series open), §4 component 2 (AniListCache), §6 error handling (AniList offline = use cache).

### Task 2.1: AniListCache primitive

**Files:**
- Create: `src/core/manga/anilist/AniListCache.h`
- Create: `src/core/manga/anilist/AniListCache.cpp`

- [ ] **Step 2.1.1: Write the header**

```cpp
// src/core/manga/anilist/AniListCache.h
#pragma once

#include "AniListTypes.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

namespace tankoban::manga::anilist {

// File-backed cache for AniList responses.
//   <cacheDir>/series_<anilistId>.json   - one MediaDetail per file
//   <cacheDir>/_bookmarks.json           - QSet<int> of bookmarked anilistIds (never evicted)
//   <cacheDir>/_index.json               - per-series last-fetched timestamps for staleness
//
// Thread safety: all mutating methods acquire m_mutex; readers also acquire
// m_mutex (the in-memory hot copy m_byId may be touched from any thread).
// save/flush always happen off-lock.
class AniListCache : public QObject
{
    Q_OBJECT
public:
    explicit AniListCache(const QString& cacheDir, QObject* parent = nullptr);
    ~AniListCache() override;

    // Read API.
    std::optional<MediaDetail> get(int anilistId) const;
    bool isBookmarked(int anilistId) const;
    QSet<int> bookmarkedIds() const;
    QList<MediaPreview> bookmarkedPreviews() const;  // for offline browse landing

    // Write API.
    void put(const MediaDetail& detail);
    void addBookmark(int anilistId);
    void removeBookmark(int anilistId);

    // Returns true if a cached entry exists AND its fetchedAtMs is within
    // `maxAgeMs` of now. Used by the series view to decide whether to
    // fire a background refetch.
    bool isFresh(int anilistId, qint64 maxAgeMs) const;

signals:
    void cacheChanged(int anilistId);
    void bookmarksChanged();

private:
    void loadFromDisk();
    void saveSeriesToDisk(const MediaDetail& d) const;
    void saveBookmarksToDisk() const;
    QString seriesFilePath(int anilistId) const;
    QString bookmarksFilePath() const;
    QString indexFilePath() const;

    const QString m_cacheDir;
    mutable QMutex m_mutex;
    QHash<int, MediaDetail> m_byId;
    QSet<int>               m_bookmarks;
};

} // namespace tankoban::manga::anilist
```

- [ ] **Step 2.1.2: Write the implementation**

```cpp
// src/core/manga/anilist/AniListCache.cpp
#include "AniListCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>

namespace tankoban::manga::anilist {

namespace {

QJsonObject mediaPreviewToJson(const MediaPreview& p)
{
    QJsonObject o;
    o["anilistId"]       = p.anilistId;
    o["title"]           = p.title;
    QJsonArray alts;
    for (const auto& s : p.alternateTitles) alts.append(s);
    o["alternateTitles"] = alts;
    o["coverThumbUrl"]   = p.coverThumbUrl;
    o["coverFullUrl"]    = p.coverFullUrl;
    o["bannerUrl"]       = p.bannerUrl;
    o["format"]          = p.format;
    o["status"]          = p.status;
    o["yearStarted"]     = p.yearStarted;
    QJsonArray g;
    for (const auto& s : p.genres) g.append(s);
    o["genres"]          = g;
    o["description"]     = p.description;
    return o;
}

MediaPreview mediaPreviewFromJson(const QJsonObject& o)
{
    MediaPreview p;
    p.anilistId     = o.value("anilistId").toInt();
    p.title         = o.value("title").toString();
    for (const auto& v : o.value("alternateTitles").toArray()) p.alternateTitles.append(v.toString());
    p.coverThumbUrl = o.value("coverThumbUrl").toString();
    p.coverFullUrl  = o.value("coverFullUrl").toString();
    p.bannerUrl     = o.value("bannerUrl").toString();
    p.format        = o.value("format").toString();
    p.status        = o.value("status").toString();
    p.yearStarted   = o.value("yearStarted").toInt();
    for (const auto& v : o.value("genres").toArray()) p.genres.append(v.toString());
    p.description   = o.value("description").toString();
    return p;
}

QJsonObject mediaDetailToJson(const MediaDetail& d)
{
    QJsonObject o;
    o["preview"]       = mediaPreviewToJson(d.preview);
    o["totalChapters"] = d.totalChapters;
    o["totalVolumes"]  = d.totalVolumes;
    QJsonArray chapters;
    for (const auto& c : d.chapters) {
        QJsonObject co;
        co["number"]      = c.number;
        co["title"]       = c.title;
        co["boundVolume"] = c.boundVolume;
        chapters.append(co);
    }
    o["chapters"]      = chapters;
    QJsonArray volArt;
    for (const auto& a : d.volumeArt) {
        QJsonObject ao;
        ao["thumbnailUrl"] = a.thumbnailUrl;
        ao["fullUrl"]      = a.fullUrl;
        volArt.append(ao);
    }
    o["volumeArt"]     = volArt;
    o["fetchedAtMs"]   = static_cast<qint64>(d.fetchedAtMs);
    return o;
}

MediaDetail mediaDetailFromJson(const QJsonObject& o)
{
    MediaDetail d;
    d.preview       = mediaPreviewFromJson(o.value("preview").toObject());
    d.totalChapters = o.value("totalChapters").toInt();
    d.totalVolumes  = o.value("totalVolumes").toInt();
    for (const auto& v : o.value("chapters").toArray()) {
        const QJsonObject co = v.toObject();
        AniListChapter c;
        c.number      = co.value("number").toString();
        c.title       = co.value("title").toString();
        c.boundVolume = co.value("boundVolume").toInt(-1);
        d.chapters.append(c);
    }
    for (const auto& v : o.value("volumeArt").toArray()) {
        const QJsonObject ao = v.toObject();
        AniListVolumeArt a;
        a.thumbnailUrl = ao.value("thumbnailUrl").toString();
        a.fullUrl      = ao.value("fullUrl").toString();
        d.volumeArt.append(a);
    }
    d.fetchedAtMs   = static_cast<qint64>(o.value("fetchedAtMs").toVariant().toLongLong());
    return d;
}

} // anonymous namespace

AniListCache::AniListCache(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(cacheDir)
{
    QDir().mkpath(m_cacheDir);
    loadFromDisk();
}

AniListCache::~AniListCache() = default;

QString AniListCache::seriesFilePath(int anilistId) const
{
    return m_cacheDir + QStringLiteral("/series_%1.json").arg(anilistId);
}

QString AniListCache::bookmarksFilePath() const
{
    return m_cacheDir + QStringLiteral("/_bookmarks.json");
}

QString AniListCache::indexFilePath() const
{
    return m_cacheDir + QStringLiteral("/_index.json");
}

void AniListCache::loadFromDisk()
{
    QMutexLocker lk(&m_mutex);

    // Bookmarks first (small file).
    QFile bf(bookmarksFilePath());
    if (bf.exists() && bf.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(bf.readAll()).array();
        for (const auto& v : arr) m_bookmarks.insert(v.toInt());
    }

    // Series detail files. One JSON file per series.
    QDir dir(m_cacheDir);
    const auto entries = dir.entryList(QStringList{ QStringLiteral("series_*.json") },
                                       QDir::Files);
    for (const auto& filename : entries) {
        QFile f(dir.absoluteFilePath(filename));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) continue;
        const MediaDetail d = mediaDetailFromJson(doc.object());
        if (d.preview.anilistId > 0) {
            m_byId.insert(d.preview.anilistId, d);
        }
    }
}

void AniListCache::saveSeriesToDisk(const MediaDetail& d) const
{
    QSaveFile f(seriesFilePath(d.preview.anilistId));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(mediaDetailToJson(d)).toJson(QJsonDocument::Indented));
    f.commit();
}

void AniListCache::saveBookmarksToDisk() const
{
    QJsonArray arr;
    for (int id : m_bookmarks) arr.append(id);
    QSaveFile f(bookmarksFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

std::optional<MediaDetail> AniListCache::get(int anilistId) const
{
    QMutexLocker lk(&m_mutex);
    const auto it = m_byId.constFind(anilistId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

bool AniListCache::isBookmarked(int anilistId) const
{
    QMutexLocker lk(&m_mutex);
    return m_bookmarks.contains(anilistId);
}

QSet<int> AniListCache::bookmarkedIds() const
{
    QMutexLocker lk(&m_mutex);
    return m_bookmarks;
}

QList<MediaPreview> AniListCache::bookmarkedPreviews() const
{
    QMutexLocker lk(&m_mutex);
    QList<MediaPreview> out;
    for (int id : m_bookmarks) {
        const auto it = m_byId.constFind(id);
        if (it != m_byId.constEnd()) out.append(it.value().preview);
    }
    return out;
}

void AniListCache::put(const MediaDetail& detail)
{
    if (detail.preview.anilistId <= 0) return;
    MediaDetail toSave;
    {
        QMutexLocker lk(&m_mutex);
        m_byId.insert(detail.preview.anilistId, detail);
        toSave = detail;
    }
    saveSeriesToDisk(toSave);
    emit cacheChanged(detail.preview.anilistId);
}

void AniListCache::addBookmark(int anilistId)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        if (!m_bookmarks.contains(anilistId)) {
            m_bookmarks.insert(anilistId);
            changed = true;
        }
    }
    if (changed) {
        QMutexLocker lk(&m_mutex);
        saveBookmarksToDisk();
    }
    if (changed) emit bookmarksChanged();
}

void AniListCache::removeBookmark(int anilistId)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        if (m_bookmarks.remove(anilistId)) changed = true;
    }
    if (changed) {
        QMutexLocker lk(&m_mutex);
        saveBookmarksToDisk();
    }
    if (changed) emit bookmarksChanged();
}

bool AniListCache::isFresh(int anilistId, qint64 maxAgeMs) const
{
    QMutexLocker lk(&m_mutex);
    const auto it = m_byId.constFind(anilistId);
    if (it == m_byId.constEnd()) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - it->fetchedAtMs) < maxAgeMs;
}

} // namespace tankoban::manga::anilist
```

- [ ] **Step 2.1.3: Wire into CMakeLists.txt**

Add to manga sources + headers groups:

```cmake
    src/core/manga/anilist/AniListCache.cpp
    src/core/manga/anilist/AniListCache.h
```

- [ ] **Step 2.1.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 2.2: Phase 2 close — RTC line

- [ ] **Step 2.2.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 2 - AniListCache. New: AniListCache.h/.cpp - file-backed cache at <cacheDir> with one series_<id>.json per series + _bookmarks.json + _index.json. Thread-safe via QMutex; mutations + reads both acquire the lock; saves happen on the calling thread (cache is small, JSON-per-series stays compact). get/put/isBookmarked/addBookmark/removeBookmark/bookmarkedIds/bookmarkedPreviews/isFresh API surface. Bookmark-aware (bookmarked series never evicted by future LRU sweeps). loadFromDisk runs at construction; saves run on put/addBookmark/removeBookmark. JSON encode/decode helpers (mediaPreviewToJson/mediaDetailToJson + inverse) cover all POD fields including QStringList alternateTitles + AniListChapter chapters[] + AniListVolumeArt volumeArt[]. Refresh-on-open semantics surfaced via isFresh(maxAgeMs) helper. cacheChanged + bookmarksChanged signals for downstream consumers (series view + library tile renderer). build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/anilist/AniListCache.h, src/core/manga/anilist/AniListCache.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 3 — AniListVolumeMapper (pure-logic + TDD)

**Phase goal:** Pure function that turns AniList metadata + chapter list into `QList<VolumeRow>` with Vol X synthesis. No I/O, no Qt UI dependency, fully unit-testable. Lives in `tankoban_tests` target.

**Files this phase touches:**

- Create: `src/core/manga/anilist/AniListVolumeMapper.h`
- Create: `src/core/manga/anilist/AniListVolumeMapper.cpp`
- Create: `tests/core/manga/AniListVolumeMapperTest.cpp`
- Modify: `CMakeLists.txt` (register test source in `tankoban_tests` target)

**Reference design sections:** §1 (volume is the only UI unit), §2 decision 2 (Vol X = un-bound chapters past last AniList-bound volume), §4 component 3 (AniListVolumeMapper, pure-function-style).

### Task 3.1: Write the failing tests first (TDD)

**Files:**
- Create: `tests/core/manga/AniListVolumeMapperTest.cpp`

- [ ] **Step 3.1.1: Write the test file with 5 cases**

```cpp
// tests/core/manga/AniListVolumeMapperTest.cpp
#include "core/manga/anilist/AniListVolumeMapper.h"

#include <gtest/gtest.h>

using namespace tankoban::manga::anilist;

namespace {

AniListChapter ch(const QString& num, int boundVol = -1)
{
    AniListChapter c;
    c.number       = num;
    c.title        = QString();
    c.boundVolume  = boundVol;
    return c;
}

MediaDetail makeDetail(const QString& title, int totalVolumes, int totalChapters,
                       const QString& status, const QList<AniListChapter>& chapters)
{
    MediaDetail d;
    d.preview.title  = title;
    d.preview.status = status;
    d.totalVolumes   = totalVolumes;
    d.totalChapters  = totalChapters;
    d.chapters       = chapters;
    return d;
}

} // namespace

TEST(AniListVolumeMapperTest, CompletedSeriesProducesNVolumesNoVolX)
{
    // Death Note: 12 vols, 108 chapters, status FINISHED.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 108; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("Death Note"), 12, 108, QStringLiteral("FINISHED"), chapters));

    ASSERT_EQ(rows.size(), 12);
    for (int i = 0; i < 12; ++i) {
        EXPECT_EQ(rows[i].volumeNumber, i + 1);
        EXPECT_FALSE(rows[i].isVolumeX);
        EXPECT_EQ(rows[i].chapterCount, 9);  // 108 / 12 = 9
    }
    // No Vol X for completed series.
    bool anyVolX = false;
    for (const auto& r : rows) if (r.isVolumeX) anyVolX = true;
    EXPECT_FALSE(anyVolX);
}

TEST(AniListVolumeMapperTest, OngoingFullyBoundProducesNVolumesNoVolX)
{
    // Hypothetical: 5 vols, 40 chapters, status RELEASING but all chapters
    // happen to land in bound vols (8 each). No Vol X needed.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 40; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("Hypothetical"), 5, 40, QStringLiteral("RELEASING"), chapters));

    ASSERT_EQ(rows.size(), 5);
    for (const auto& r : rows) EXPECT_FALSE(r.isVolumeX);
}

TEST(AniListVolumeMapperTest, OngoingWithUnboundTailProducesVolX)
{
    // One Piece-ish: 111 bound vols (888 chapters at 8/vol), 1146 latest
    // chapter. Status RELEASING. Vol X should hold chapters 889-1146.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 1146; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("One Piece"), 111, 888, QStringLiteral("RELEASING"), chapters));

    // 111 bound vols + 1 Vol X
    ASSERT_EQ(rows.size(), 112);
    for (int i = 0; i < 111; ++i) {
        EXPECT_EQ(rows[i].volumeNumber, i + 1);
        EXPECT_FALSE(rows[i].isVolumeX);
    }
    EXPECT_TRUE(rows[111].isVolumeX);
    EXPECT_EQ(rows[111].volumeNumber, kVolumeXNumber);
    EXPECT_EQ(rows[111].chapterCount, 1146 - 888);  // 258 chapters in Vol X
}

TEST(AniListVolumeMapperTest, EmptyChaptersListReturnsEmpty)
{
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("Unknown"), 0, 0, QStringLiteral("NOT_YET_RELEASED"), {}));
    EXPECT_TRUE(rows.isEmpty());
}

TEST(AniListVolumeMapperTest, OngoingWithNoBoundVolumesProducesOnlyVolX)
{
    // Pure-tail series (no vols bound yet, only loose chapters). All
    // chapters should go into Vol X.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 12; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("FreshSeries"), 0, 0, QStringLiteral("RELEASING"), chapters));

    ASSERT_EQ(rows.size(), 1);
    EXPECT_TRUE(rows[0].isVolumeX);
    EXPECT_EQ(rows[0].chapterCount, 12);
}
```

- [ ] **Step 3.1.2: Register test in CMakeLists.txt under tankoban_tests target**

In `CMakeLists.txt`, locate the `tankoban_tests` target's source list. Append:

```cmake
    tests/core/manga/AniListVolumeMapperTest.cpp
```

Plus add the mapper's .cpp to the test target sources too (the test links the mapper directly; no full app dependency):

```cmake
    src/core/manga/anilist/AniListVolumeMapper.cpp
```

If `tankoban_tests` target uses `add_executable(tankoban_tests ...)` and pulls a subset of main-app sources, follow the existing pattern — verify how prior tests (if any) wired their subjects-under-test.

- [ ] **Step 3.1.3: Run tests, verify they fail to compile**

```cmd
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
```

Expected: build fails with `AniListVolumeMapper.h not found` or `unresolved external symbol map`. This is the failing-test state.

### Task 3.2: Implement AniListVolumeMapper

**Files:**
- Create: `src/core/manga/anilist/AniListVolumeMapper.h`
- Create: `src/core/manga/anilist/AniListVolumeMapper.cpp`

- [ ] **Step 3.2.1: Write the header**

```cpp
// src/core/manga/anilist/AniListVolumeMapper.h
#pragma once

#include "AniListTypes.h"

#include <QList>

namespace tankoban::manga::anilist {

// Pure-function helper. Given a MediaDetail (AniList metadata + chapter
// list), produces a list of VolumeRow that the series view renders.
//
// Mapping strategy (since AniList does not expose per-chapter volume
// binding via the public GraphQL schema as of 2026-05):
//
//   if totalVolumes > 0 AND totalChapters > 0:
//     - chapters-per-bound-vol = totalChapters / totalVolumes (integer floor)
//     - vols 1..totalVolumes get `chapters-per-bound-vol` chapters each, in order
//     - if there are MORE chapters than totalVolumes * chapters-per-vol,
//       the residual goes into a single Vol X at the end
//     - ONLY for status == "RELEASING" (ongoing) do we create Vol X. For
//       FINISHED series we cap at totalVolumes (any extra chapters are
//       considered data noise and squeezed into the last bound vol).
//
//   if totalVolumes == 0 AND totalChapters == 0:
//     - empty result (series has no metadata yet)
//
//   if totalVolumes == 0 AND chapters.size() > 0:
//     - all chapters go into a single Vol X (pure-tail series)
//
// The mapper is pure: no I/O, no Qt UI, no logging. Thread-safe by
// construction (no shared state).
class AniListVolumeMapper
{
public:
    static QList<VolumeRow> map(const MediaDetail& detail);

    // Exposed for unit tests: extract the numeric prefix from a chapter
    // number string. "12" -> 12; "12.5" -> 12; "Prologue 1" -> 1; ""
    // returns -1.
    static int extractChapterNumeric(const QString& chapterNumber);
};

} // namespace tankoban::manga::anilist
```

- [ ] **Step 3.2.2: Write the implementation**

```cpp
// src/core/manga/anilist/AniListVolumeMapper.cpp
#include "AniListVolumeMapper.h"

#include <QRegularExpression>
#include <algorithm>

namespace tankoban::manga::anilist {

int AniListVolumeMapper::extractChapterNumeric(const QString& chapterNumber)
{
    static const QRegularExpression numRe(QStringLiteral("(\\d+)"));
    const auto m = numRe.match(chapterNumber);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    return ok ? n : -1;
}

QList<VolumeRow> AniListVolumeMapper::map(const MediaDetail& detail)
{
    QList<VolumeRow> out;
    if (detail.chapters.isEmpty()) return out;

    const int chapterCount    = detail.chapters.size();
    const int volumeCount     = detail.totalVolumes;
    const bool isOngoing      = (detail.preview.status == QStringLiteral("RELEASING") ||
                                  detail.preview.status == QStringLiteral("HIATUS"));

    // Sort chapters by numeric value so vol-bucket assignment is monotonic.
    QList<AniListChapter> sorted = detail.chapters;
    std::sort(sorted.begin(), sorted.end(),
              [](const AniListChapter& a, const AniListChapter& b) {
                  return extractChapterNumeric(a.number) <
                         extractChapterNumeric(b.number);
              });

    // Case 1: no bound vols at all -> single Vol X holds everything.
    if (volumeCount <= 0) {
        VolumeRow x;
        x.volumeNumber       = kVolumeXNumber;
        x.isVolumeX          = true;
        x.chapterCount       = chapterCount;
        x.chapterRangeStart  = extractChapterNumeric(sorted.first().number);
        x.chapterRangeEnd    = extractChapterNumeric(sorted.last().number);
        for (const auto& c : sorted) x.chapterNumbers.append(c.number);
        out.append(x);
        return out;
    }

    // Case 2: bound vols exist. Compute chapters per vol.
    const int chaptersPerVol = std::max(1, detail.totalChapters / volumeCount);

    int chapterIdx = 0;
    for (int v = 1; v <= volumeCount; ++v) {
        VolumeRow row;
        row.volumeNumber = v;
        row.isVolumeX    = false;

        const bool isLastBoundVol = (v == volumeCount);
        const int chaptersThisVol = isLastBoundVol && !isOngoing
            ? (chapterCount - chapterIdx)   // FINISHED: stuff remaining into last bound vol
            : chaptersPerVol;

        for (int n = 0; n < chaptersThisVol && chapterIdx < chapterCount; ++n) {
            row.chapterNumbers.append(sorted[chapterIdx].number);
            ++chapterIdx;
        }
        row.chapterCount       = row.chapterNumbers.size();
        if (!row.chapterNumbers.isEmpty()) {
            row.chapterRangeStart = extractChapterNumeric(row.chapterNumbers.first());
            row.chapterRangeEnd   = extractChapterNumeric(row.chapterNumbers.last());
        }

        // Per-vol art when available.
        if (v - 1 < detail.volumeArt.size()) row.art = detail.volumeArt.at(v - 1);

        out.append(row);
    }

    // Case 3: ongoing + leftover chapters past the bound vols -> Vol X.
    if (isOngoing && chapterIdx < chapterCount) {
        VolumeRow x;
        x.volumeNumber       = kVolumeXNumber;
        x.isVolumeX          = true;
        for (int i = chapterIdx; i < chapterCount; ++i) {
            x.chapterNumbers.append(sorted[i].number);
        }
        x.chapterCount       = x.chapterNumbers.size();
        if (!x.chapterNumbers.isEmpty()) {
            x.chapterRangeStart = extractChapterNumeric(x.chapterNumbers.first());
            x.chapterRangeEnd   = extractChapterNumeric(x.chapterNumbers.last());
        }
        out.append(x);
    }

    return out;
}

} // namespace tankoban::manga::anilist
```

- [ ] **Step 3.2.3: Wire mapper into CMakeLists.txt main-app sources**

In `CMakeLists.txt` manga group:

```cmake
    src/core/manga/anilist/AniListVolumeMapper.cpp
    src/core/manga/anilist/AniListVolumeMapper.h
```

(The .cpp was already added to `tankoban_tests` in Task 3.1.2.)

- [ ] **Step 3.2.4: Run tests, verify they pass**

```cmd
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R AniListVolumeMapperTest
```

Expected: all 5 tests PASS. If any fail, fix the mapper logic until they do (this is the inner loop of TDD).

- [ ] **Step 3.2.5: Build-verify main app**

Run: `build_check.bat`
Expected: `BUILD OK` for the main app target (the mapper is now also linked into Tankoban.exe via CMakeLists).

### Task 3.3: Phase 3 close — RTC line

- [ ] **Step 3.3.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 3 - AniListVolumeMapper pure-logic + TDD. New: AniListVolumeMapper.h/.cpp - static map(MediaDetail) -> QList<VolumeRow>. Mapping strategy documented inline: when totalVolumes>0+totalChapters>0, chapters/volumes-floor heuristic distributes chapters into bound vols 1..N; ongoing series with residual chapters get Vol X appended (kVolumeXNumber sentinel); FINISHED series cap at totalVolumes and stuff any extra into the last bound vol; no-bound-vols case puts everything into Vol X (pure-tail series). extractChapterNumeric exposed for callers + tested. Chapter ordering monotonic via std::sort on numeric prefix. 5 unit tests in tankoban_tests target: CompletedSeriesProducesNVolumesNoVolX (Death Note shape), OngoingFullyBoundProducesNVolumesNoVolX, OngoingWithUnboundTailProducesVolX (One Piece shape), EmptyChaptersListReturnsEmpty, OngoingWithNoBoundVolumesProducesOnlyVolX. All 5 PASS via ctest. Main app build_check green.] | Skills invoked: [/superpowers:executing-plans, /superpowers:test-driven-development, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/anilist/AniListVolumeMapper.h, src/core/manga/anilist/AniListVolumeMapper.cpp, tests/core/manga/AniListVolumeMapperTest.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 4 — NyaaRuntimeSource

**Phase goal:** Runtime nyaa.si query for volumes whose series is not in the catalog. Uploader-trust filter (1r0n, Hox, VIZ Digital). Ranked candidates. Backbone for the Sources panel's "other than catalog" tier.

**Files this phase touches:**

- Create: `src/core/manga/NyaaRuntimeSource.h`
- Create: `src/core/manga/NyaaRuntimeSource.cpp`
- Create: `resources/manga_uploader_trust.json`
- Modify: `CMakeLists.txt`

**Reference design sections:** §2 decision 5 (hybrid catalog + runtime), §2 decision 10 (trust tier first then seeders), §4 component 5 (NyaaRuntimeSource).

### Task 4.1: Uploader trust JSON

**Files:**
- Create: `resources/manga_uploader_trust.json`

- [ ] **Step 4.1.1: Write the trust file**

```json
{
  "schemaVersion": 1,
  "tier1": [
    "1r0n",
    "Hox",
    "VIZ Digital"
  ],
  "tier2": [
    "KG Manga",
    "DKThias"
  ],
  "blocked": []
}
```

Tier-1 uploaders rank above all others. Tier-2 above untrusted. Blocked never appears.

### Task 4.2: NyaaRuntimeSource class

**Files:**
- Create: `src/core/manga/NyaaRuntimeSource.h`
- Create: `src/core/manga/NyaaRuntimeSource.cpp`

- [ ] **Step 4.2.1: Write the header**

```cpp
// src/core/manga/NyaaRuntimeSource.h
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga {

// One nyaa search-result candidate. The Sources panel renders one row
// per NyaaSourceCandidate.
struct NyaaSourceCandidate {
    QString  title;        // full nyaa title string
    QString  uploader;
    QString  magnetUri;
    QString  infoHash;     // 40-char lowercase hex
    qint64   sizeBytes = 0;
    int      seeders   = 0;
    int      leechers  = 0;
    int      tier      = 99; // 1 / 2 / 99 (untrusted)
};

// Runtime nyaa.si query with uploader-trust filter. Loads the trust JSON
// once at construction and uses it to tag + rank results.
class NyaaRuntimeSource : public QObject
{
    Q_OBJECT
public:
    explicit NyaaRuntimeSource(QNetworkAccessManager* nam,
                                const QString& trustJsonPath,
                                QObject* parent = nullptr);
    ~NyaaRuntimeSource() override;

    // Fire a search. Result lands on searchSucceeded with the same
    // requestId. Query shape: `series title + "v"+volNumber + uploader-trust-OR`.
    // E.g.: 'One Piece v50 (1r0n | Hox | "VIZ Digital")'.
    void search(const QString& seriesTitle, int volumeNumber, int requestId);

signals:
    void searchSucceeded(int requestId, const QList<NyaaSourceCandidate>& results);
    void searchFailed(int requestId, const QString& reason);

private slots:
    void onReplyFinished();

private:
    void loadTrustJson(const QString& path);
    int  tierForUploader(const QString& uploader) const;

    QPointer<QNetworkAccessManager> m_nam;
    QSet<QString> m_tier1;
    QSet<QString> m_tier2;
    QSet<QString> m_blocked;
};

} // namespace tankoban::manga
```

- [ ] **Step 4.2.2: Write the implementation**

```cpp
// src/core/manga/NyaaRuntimeSource.cpp
#include "NyaaRuntimeSource.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <algorithm>

namespace tankoban::manga {

namespace {

constexpr const char* kNyaaRssEndpoint = "https://nyaa.si";

QString buildQueryString(const QString& seriesTitle, int volumeNumber,
                         const QSet<QString>& tier1, const QSet<QString>& tier2)
{
    QStringList uploaders;
    for (const auto& u : tier1) uploaders.append(u);
    for (const auto& u : tier2) uploaders.append(u);
    QString q = seriesTitle + QStringLiteral(" v") + QString::number(volumeNumber);
    if (!uploaders.isEmpty()) {
        q += QStringLiteral(" (") + uploaders.join(QStringLiteral(" | ")) + QChar(')');
    }
    return q;
}

QString infoHashFromMagnet(const QString& magnet)
{
    // magnet:?xt=urn:btih:HEX&...
    static const QRegularExpression re(QStringLiteral("xt=urn:btih:([0-9a-fA-F]{40})"));
    const auto m = re.match(magnet);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

} // anonymous namespace

NyaaRuntimeSource::NyaaRuntimeSource(QNetworkAccessManager* nam,
                                    const QString& trustJsonPath,
                                    QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadTrustJson(trustJsonPath);
}

NyaaRuntimeSource::~NyaaRuntimeSource() = default;

void NyaaRuntimeSource::loadTrustJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral("[NyaaRuntimeSource] cannot open trust json:") << path;
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto& v : root.value("tier1").toArray())   m_tier1.insert(v.toString());
    for (const auto& v : root.value("tier2").toArray())   m_tier2.insert(v.toString());
    for (const auto& v : root.value("blocked").toArray()) m_blocked.insert(v.toString());
}

int NyaaRuntimeSource::tierForUploader(const QString& uploader) const
{
    if (m_blocked.contains(uploader)) return -1; // skip entirely
    if (m_tier1.contains(uploader))   return 1;
    if (m_tier2.contains(uploader))   return 2;
    return 99;
}

void NyaaRuntimeSource::search(const QString& seriesTitle, int volumeNumber, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("network manager unavailable"));
        return;
    }
    QUrl url(QString::fromLatin1(kNyaaRssEndpoint));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("page"), QStringLiteral("rss"));
    q.addQueryItem(QStringLiteral("c"),    QStringLiteral("3_1")); // Literature - English-translated category
    q.addQueryItem(QStringLiteral("s"),    QStringLiteral("seeders"));
    q.addQueryItem(QStringLiteral("o"),    QStringLiteral("desc"));
    q.addQueryItem(QStringLiteral("q"),    buildQueryString(seriesTitle, volumeNumber, m_tier1, m_tier2));
    url.setQuery(q);

    auto* reply = m_nam->get(QNetworkRequest(url));
    reply->setProperty("nyaa_requestId", requestId);
    connect(reply, &QNetworkReply::finished,
            this, &NyaaRuntimeSource::onReplyFinished);
}

void NyaaRuntimeSource::onReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property("nyaa_requestId").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }

    QList<NyaaSourceCandidate> out;
    QXmlStreamReader xml(reply->readAll());
    NyaaSourceCandidate cur;
    bool inItem = false;
    QString currentTag;
    while (!xml.atEnd() && !xml.hasError()) {
        const auto t = xml.readNext();
        if (t == QXmlStreamReader::StartElement) {
            currentTag = xml.name().toString();
            if (currentTag == QStringLiteral("item")) {
                cur = NyaaSourceCandidate{};
                inItem = true;
            }
        } else if (t == QXmlStreamReader::EndElement) {
            if (xml.name() == QStringLiteral("item") && inItem) {
                cur.tier = tierForUploader(cur.uploader);
                if (cur.tier >= 0) out.append(cur);
                inItem = false;
            }
            currentTag.clear();
        } else if (t == QXmlStreamReader::Characters && inItem && !xml.isWhitespace()) {
            const QString text = xml.text().toString();
            if (currentTag == QStringLiteral("title")) {
                cur.title = text;
            } else if (currentTag == QStringLiteral("link") ||
                       currentTag == QStringLiteral("nyaa:infoHash")) {
                if (currentTag == QStringLiteral("nyaa:infoHash")) {
                    cur.infoHash = text.toLower();
                    cur.magnetUri = QStringLiteral("magnet:?xt=urn:btih:") + cur.infoHash;
                }
            } else if (currentTag == QStringLiteral("nyaa:seeders")) {
                cur.seeders = text.toInt();
            } else if (currentTag == QStringLiteral("nyaa:leechers")) {
                cur.leechers = text.toInt();
            } else if (currentTag == QStringLiteral("nyaa:size")) {
                // Format like "1.4 GiB" - leave parsing approximation for now.
                cur.sizeBytes = 0; // populated by a follow-up if exact bytes needed
            } else if (currentTag == QStringLiteral("nyaa:uploader") ||
                       currentTag == QStringLiteral("author")) {
                if (cur.uploader.isEmpty()) cur.uploader = text.trimmed();
            }
        }
    }

    // Rank: tier asc, seeders desc within tier.
    std::sort(out.begin(), out.end(),
              [](const NyaaSourceCandidate& a, const NyaaSourceCandidate& b) {
                  if (a.tier != b.tier) return a.tier < b.tier;
                  return a.seeders > b.seeders;
              });

    emit searchSucceeded(requestId, out);
}

} // namespace tankoban::manga
```

- [ ] **Step 4.2.3: Wire into CMakeLists.txt**

```cmake
    src/core/manga/NyaaRuntimeSource.cpp
    src/core/manga/NyaaRuntimeSource.h
```

Plus register the trust JSON as a deployed resource (deployment to `out/resources/manga_uploader_trust.json` per existing pattern).

- [ ] **Step 4.2.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 4.3: Phase 4 close — RTC line

- [ ] **Step 4.3.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 4 - NyaaRuntimeSource. New: NyaaRuntimeSource.h/.cpp - runtime nyaa.si search with uploader-trust filter, RSS XML parse, tier-aware ranking (tier1 [1r0n/Hox/VIZ Digital] above tier2 [KG Manga/DKThias] above untrusted; seeders break ties within tier; blocked filtered out). Query shape: `<series> v<vol> (uploader1 | uploader2 | ...)`. NyaaSourceCandidate struct: title + uploader + magnetUri + infoHash + sizeBytes + seeders + leechers + tier. resources/manga_uploader_trust.json captures the tier definitions in a single JSON loaded at construction. searchSucceeded/searchFailed signals matching the AniListClient request-id pattern. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/NyaaRuntimeSource.h, src/core/manga/NyaaRuntimeSource.cpp, resources/manga_uploader_trust.json, CMakeLists.txt, agents/chat.md
```

---

## Phase 5 — WeebCentralVolumePacker

**Phase goal:** New source provider that synthesizes a volume cbz from WeebCentral chapter fetches. Sibling to TorrentVolumeProvider on the source layer. Fires same `volumeCompleted` shape so downstream code (MangaDownloadIndex, Phase 10 cover extractor) does not branch on source type.

**Files this phase touches:**

- Create: `src/core/manga/WeebCentralVolumePacker.h`
- Create: `src/core/manga/WeebCentralVolumePacker.cpp`
- Modify: `CMakeLists.txt`

**Reference design sections:** §1 (chapters are implementation detail), §2 decision 3 (HTTP-fetch + zip-on-the-fly), §4 component 4 (WeebCentralVolumePacker), §5 step 5-alt (WeebCentral data flow).

### Task 5.1: WeebCentralVolumePacker class

**Files:**
- Create: `src/core/manga/WeebCentralVolumePacker.h`
- Create: `src/core/manga/WeebCentralVolumePacker.cpp`

- [ ] **Step 5.1.1: Write the header**

```cpp
// src/core/manga/WeebCentralVolumePacker.h
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class MangaScraper;
class MangaDownloader;
class QNetworkAccessManager;

namespace tankoban::manga {

class PremiumArchiveValidator;

// One pending vol-pack request.
struct VolumePackRequest {
    QString     seriesId;       // app-internal series id (lowercase slug)
    int         volumeNumber;
    QString     destinationPath; // canonical .cbz path on disk
    QStringList chapterIds;     // AniList chapter numbers in order
};

// Source provider that synthesizes a vol cbz from WeebCentral chapter
// fetches.
//
// Lifecycle:
//   requestVolume(req) ->
//     1. mkpath staging dir <appData>/manga_premium_staging/wc_<seriesId>_v<NN>/
//     2. for each chapterId in req.chapterIds:
//          a. scraper->fetchChapter(chapterId) -> List<imageUrl>
//          b. download each image via NAM -> write to staging dir
//          c. emit volumeProgress per chapter complete
//     3. zip the staging dir into <destination>.tankoban-part
//     4. hand to PremiumArchiveValidator for Phase 4 finalize lifecycle
//     5. on validator success: atomic rename .tankoban-part -> .cbz, emit
//        volumeCompleted; on failure, move to quarantine, emit volumeFailed
//
// Signals match TorrentVolumeProvider's shape so downstream consumers
// (MangaDownloadIndex.registerVolume + PremiumCoverExtractor) do not
// branch.
class WeebCentralVolumePacker : public QObject
{
    Q_OBJECT
public:
    WeebCentralVolumePacker(MangaScraper* scraper,
                             QNetworkAccessManager* nam,
                             const QString& stagingRoot,
                             QObject* parent = nullptr);
    ~WeebCentralVolumePacker() override;

    void requestVolume(const VolumePackRequest& req);

    // Coordinator integration.
    void pauseAll();
    void resumeAll();
    bool isPaused() const;

signals:
    void volumeProgress(QString seriesId, int volumeNumber, double pct);
    void volumeCompleted(QString seriesId, int volumeNumber, QString cbzPath);
    void volumeFailed(QString seriesId, int volumeNumber,
                      QString code, QString message);

private:
    void startNextChapter(const VolumePackRequest& req, int chapterIdx,
                          int totalChapters, const QString& stagingDir);
    void finalizePack(const VolumePackRequest& req, const QString& stagingDir);

    QPointer<MangaScraper>          m_scraper;
    QPointer<QNetworkAccessManager> m_nam;
    QString                         m_stagingRoot;
    bool                            m_paused = false;
};

} // namespace tankoban::manga
```

- [ ] **Step 5.1.2: Write the implementation skeleton**

```cpp
// src/core/manga/WeebCentralVolumePacker.cpp
#include "WeebCentralVolumePacker.h"
#include "MangaScraper.h"
#include "MangaDownloader.h"
#include "PremiumArchiveValidator.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>

#ifdef HAS_QT_ZIP
#  include <QtCore/private/qzipwriter_p.h>
#endif

namespace tankoban::manga {

namespace {

constexpr int kMaxConcurrentImageDownloads = 4;
constexpr qint64 kImageMaxBytes = 32LL * 1024 * 1024;  // 32 MiB per image safety bound

QString stagingDirFor(const QString& root, const QString& seriesId, int volNumber)
{
    return root + QStringLiteral("/wc_") + seriesId + QStringLiteral("_v")
         + QString::number(volNumber, 10).rightJustified(2, QChar('0'));
}

bool zipStagingDir(const QString& stagingDir, const QString& outPartPath)
{
#ifndef HAS_QT_ZIP
    Q_UNUSED(stagingDir)
    Q_UNUSED(outPartPath)
    return false;
#else
    QFile out(outPartPath);
    if (!out.open(QIODevice::WriteOnly)) return false;
    QZipWriter zw(&out);
    QDir dir(stagingDir);
    const auto files = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const auto& fi : files) {
        QFile in(fi.absoluteFilePath());
        if (!in.open(QIODevice::ReadOnly)) continue;
        zw.addFile(fi.fileName(), in.readAll());
    }
    zw.close();
    return out.size() > 0;
#endif
}

} // anonymous namespace

WeebCentralVolumePacker::WeebCentralVolumePacker(MangaScraper* scraper,
                                                 QNetworkAccessManager* nam,
                                                 const QString& stagingRoot,
                                                 QObject* parent)
    : QObject(parent), m_scraper(scraper), m_nam(nam), m_stagingRoot(stagingRoot)
{
    QDir().mkpath(m_stagingRoot);
}

WeebCentralVolumePacker::~WeebCentralVolumePacker() = default;

void WeebCentralVolumePacker::requestVolume(const VolumePackRequest& req)
{
    if (m_paused) {
        // Defer to resume; v1 simple behavior: store + re-fire on resume.
        // For now: short-circuit by emitting Failed so the user retries
        // after resume.
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("packer_paused"),
                          QStringLiteral("WeebCentral packer is paused; resume transfers first"));
        return;
    }
    const QString staging = stagingDirFor(m_stagingRoot, req.seriesId, req.volumeNumber);
    QDir().mkpath(staging);

    if (req.chapterIds.isEmpty()) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("no_chapters_for_volume"),
                          QStringLiteral("chapter list is empty"));
        return;
    }
    startNextChapter(req, 0, req.chapterIds.size(), staging);
}

void WeebCentralVolumePacker::startNextChapter(const VolumePackRequest& req,
                                                int chapterIdx, int totalChapters,
                                                const QString& stagingDir)
{
    if (chapterIdx >= totalChapters) {
        finalizePack(req, stagingDir);
        return;
    }
    if (!m_scraper) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("scraper_unavailable"), QString());
        return;
    }
    const QString chapterId = req.chapterIds.at(chapterIdx);

    // The scraper's fetchChapterImages signal-or-callback shape varies; we
    // assume a synchronous-ish "fetchChapterPages(seriesId, chapterId) ->
    // emits chapterPagesReady(QStringList urls)" pattern matching the
    // existing MangaScraper interface used by MangaDownloader. Connect once
    // per chapter; disconnect on completion.
    auto* conn = new QMetaObject::Connection();
    *conn = connect(m_scraper, &MangaScraper::pagesReady,
        this, [this, conn, req, chapterIdx, totalChapters, stagingDir]
              (const QString& sId, const QString& cId, const QStringList& pageUrls) {
            Q_UNUSED(sId); Q_UNUSED(cId);
            QObject::disconnect(*conn);
            delete conn;

            // Download each page url to stagingDir/<chapterIdx-zero-padded>_<pageIdx>.jpg
            int finished = 0;
            const int total = pageUrls.size();
            if (total == 0) {
                emit volumeFailed(req.seriesId, req.volumeNumber,
                                  QStringLiteral("zero_pages_in_chapter"),
                                  QStringLiteral("chapter returned 0 image URLs"));
                return;
            }
            for (int p = 0; p < total; ++p) {
                if (!m_nam) {
                    emit volumeFailed(req.seriesId, req.volumeNumber,
                                      QStringLiteral("nam_unavailable"), QString());
                    return;
                }
                QNetworkRequest req2(pageUrls.at(p));
                auto* reply = m_nam->get(req2);
                connect(reply, &QNetworkReply::finished,
                    this, [this, reply, stagingDir, chapterIdx, p, total,
                           &finished, req, totalChapters]() mutable {
                        reply->deleteLater();
                        if (reply->error() != QNetworkReply::NoError) {
                            emit volumeFailed(req.seriesId, req.volumeNumber,
                                              QStringLiteral("image_fetch_failed"),
                                              reply->errorString());
                            return;
                        }
                        const QByteArray data = reply->readAll();
                        if (data.size() > kImageMaxBytes) {
                            emit volumeFailed(req.seriesId, req.volumeNumber,
                                              QStringLiteral("image_oversize"),
                                              QString::number(data.size()));
                            return;
                        }
                        const QString outName = QStringLiteral("%1_%2.jpg")
                            .arg(chapterIdx, 4, 10, QChar('0'))
                            .arg(p,          4, 10, QChar('0'));
                        QFile f(stagingDir + QChar('/') + outName);
                        if (!f.open(QIODevice::WriteOnly)) {
                            emit volumeFailed(req.seriesId, req.volumeNumber,
                                              QStringLiteral("write_failed"), outName);
                            return;
                        }
                        f.write(data);
                        f.close();

                        ++finished;
                        if (finished == total) {
                            // Chapter done; report progress + start next chapter.
                            const double pct = static_cast<double>(chapterIdx + 1)
                                             / static_cast<double>(totalChapters);
                            emit volumeProgress(req.seriesId, req.volumeNumber, pct);
                            startNextChapter(req, chapterIdx + 1, totalChapters, stagingDir);
                        }
                    });
            }
        });
    m_scraper->fetchPages(req.seriesId, chapterId);
}

void WeebCentralVolumePacker::finalizePack(const VolumePackRequest& req, const QString& stagingDir)
{
    const QString partPath = req.destinationPath + QStringLiteral(".tankoban-part");
    if (!zipStagingDir(stagingDir, partPath)) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("zip_failed"), stagingDir);
        return;
    }

    using namespace tankoban::manga::premium;
    const auto vr = PremiumArchiveValidator::validate(partPath, /*expectedPageCount=*/0);
    if (vr.code != ArchiveValidationCode::Ok) {
        const QString quarantineDir = QStandardPaths::writableLocation(
                                          QStandardPaths::AppDataLocation)
                                    + QStringLiteral("/manga_premium_quarantine");
        QDir().mkpath(quarantineDir);
        const QString quarantineName = QStringLiteral("%1_v%2_%3.cbz.bad")
            .arg(req.seriesId)
            .arg(req.volumeNumber, 2, 10, QChar('0'))
            .arg(QDateTime::currentMSecsSinceEpoch());
        QFile::rename(partPath, quarantineDir + QChar('/') + quarantineName);
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("validation_failed"), vr.detail);
        return;
    }

    if (!QFile::rename(partPath, req.destinationPath)) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("final_rename_failed"), req.destinationPath);
        return;
    }

    // Cleanup staging dir (best-effort).
    QDir(stagingDir).removeRecursively();

    emit volumeCompleted(req.seriesId, req.volumeNumber, req.destinationPath);
}

void WeebCentralVolumePacker::pauseAll()  { m_paused = true; }
void WeebCentralVolumePacker::resumeAll() { m_paused = false; }
bool WeebCentralVolumePacker::isPaused() const { return m_paused; }

} // namespace tankoban::manga
```

Note: this uses a `MangaScraper::fetchPages` + `MangaScraper::pagesReady` API shape that already exists from prior arcs. Verify the actual signal/method names match — grep `src/core/manga/MangaScraper.h` and adjust if the names differ slightly (e.g. `fetchChapterPages` / `chapterPagesReady`).

- [ ] **Step 5.1.3: Wire into CMakeLists.txt**

```cmake
    src/core/manga/WeebCentralVolumePacker.cpp
    src/core/manga/WeebCentralVolumePacker.h
```

- [ ] **Step 5.1.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. If MangaScraper API names differ, fix the signal connect line and retry.

### Task 5.2: Phase 5 close — RTC line

- [ ] **Step 5.2.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 5 - WeebCentralVolumePacker. New: WeebCentralVolumePacker.h/.cpp - sibling to TorrentVolumeProvider on the source layer; HTTP-fetches AniList-mapped chapters from WeebCentral via the existing MangaScraper pagesReady signal, downloads each chapter's images concurrently with QNetworkAccessManager, writes each image to <stagingRoot>/wc_<seriesId>_v<NN>/<chapterIdx>_<pageIdx>.jpg, then zips the staging dir into <destination>.tankoban-part via QZipWriter (HAS_QT_ZIP gated), validates via PremiumArchiveValidator (Phase 4 lifecycle reuse), atomic-rename to .cbz on success or quarantine on failure. Image safety bound 32 MiB per image. VolumePackRequest struct: seriesId + volumeNumber + destinationPath + chapterIds[]. Signals match TorrentVolumeProvider: volumeProgress + volumeCompleted + volumeFailed. pauseAll/resumeAll/isPaused for MangaTransferCoordinator integration in Phase 11. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/WeebCentralVolumePacker.h, src/core/manga/WeebCentralVolumePacker.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 6 — ComicsPrePivotMigrator (burn-it-down)

**Phase goal:** Detect existing pre-pivot library + chapter-folders on first post-pivot launch, move them to `<appData>/comics_pre_pivot_backup/`, wipe MangaDownloadIndex + Tankoyomi library JSON, preserve manga_posters thumb cache. Idempotent.

**Files this phase touches:**

- Create: `src/core/manga/ComicsPrePivotMigrator.h`
- Create: `src/core/manga/ComicsPrePivotMigrator.cpp`
- Modify: `src/main.cpp` (run migrator before MainWindow construction)
- Modify: `CMakeLists.txt`

**Reference design sections:** §2 decision 8 (burn-it-down), §8 (burn-it-down spec).

### Task 6.1: ComicsPrePivotMigrator class

**Files:**
- Create: `src/core/manga/ComicsPrePivotMigrator.h`
- Create: `src/core/manga/ComicsPrePivotMigrator.cpp`

- [ ] **Step 6.1.1: Write the header**

```cpp
// src/core/manga/ComicsPrePivotMigrator.h
#pragma once

#include <QObject>
#include <QString>

namespace tankoban::manga {

// One-time pre-pivot to post-pivot migrator. Runs at app startup before
// MainWindow constructs ComicsPage. Idempotent: subsequent launches see
// the backup dir already exists and no-op.
//
// Migration behavior:
//   - move <appData>/data/comics_library.json -> <appData>/comics_pre_pivot_backup/
//   - move <appData>/data/manga_downloads_index.json -> same
//   - on-disk chapter folders left in place (LibraryScanner just won't see them
//     after the index wipe)
//   - manga_posters/ thumb cache preserved
//   - sidecar files (.tankoyomi-meta.json) left on disk (harmless after wipe)
//
// Detection: if `comics_pre_pivot_backup/MIGRATED` marker exists, skip.
class ComicsPrePivotMigrator
{
public:
    explicit ComicsPrePivotMigrator(const QString& appDataDir);

    // Returns true if a migration actually ran this launch (false = already
    // migrated or no pre-pivot files found).
    bool migrate();

    // Surfaces whether a backup was created at any point in the past (used
    // by Settings page to expose a 'Show backup folder' button).
    bool hasBackup() const;

    QString backupDir() const;

private:
    const QString m_appDataDir;
    const QString m_backupDir;
    const QString m_markerFile;
};

} // namespace tankoban::manga
```

- [ ] **Step 6.1.2: Write the implementation**

```cpp
// src/core/manga/ComicsPrePivotMigrator.cpp
#include "ComicsPrePivotMigrator.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>

namespace tankoban::manga {

ComicsPrePivotMigrator::ComicsPrePivotMigrator(const QString& appDataDir)
    : m_appDataDir(appDataDir)
    , m_backupDir(appDataDir + QStringLiteral("/comics_pre_pivot_backup"))
    , m_markerFile(m_backupDir + QStringLiteral("/MIGRATED"))
{
}

bool ComicsPrePivotMigrator::hasBackup() const
{
    return QFile::exists(m_markerFile);
}

QString ComicsPrePivotMigrator::backupDir() const
{
    return m_backupDir;
}

bool ComicsPrePivotMigrator::migrate()
{
    if (QFile::exists(m_markerFile)) {
        // Already migrated.
        return false;
    }

    const QString dataDir = m_appDataDir + QStringLiteral("/data");
    const QStringList candidates {
        dataDir + QStringLiteral("/comics_library.json"),
        dataDir + QStringLiteral("/manga_downloads_index.json"),
    };

    bool anyMoved = false;
    for (const auto& src : candidates) {
        if (!QFile::exists(src)) continue;
        QDir().mkpath(m_backupDir);
        const QString filename = QFileInfo(src).fileName();
        const QString dst = m_backupDir + QChar('/') + filename;
        if (QFile::exists(dst)) QFile::remove(dst); // idempotency safety
        if (QFile::rename(src, dst)) {
            anyMoved = true;
            qInfo().noquote() << QStringLiteral("[ComicsPrePivotMigrator] moved")
                              << src << QStringLiteral("->") << dst;
        } else {
            qWarning().noquote() << QStringLiteral("[ComicsPrePivotMigrator] FAILED to move")
                                 << src;
        }
    }

    // Always write the marker. Even if no pre-pivot files existed (fresh
    // install on a clean machine), we want subsequent launches to skip
    // the detection logic.
    QDir().mkpath(m_backupDir);
    QFile marker(m_markerFile);
    if (marker.open(QIODevice::WriteOnly)) {
        marker.write(QStringLiteral("Migrated at %1\n")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
        marker.close();
    }

    return anyMoved;
}

} // namespace tankoban::manga
```

- [ ] **Step 6.1.3: Wire into CMakeLists.txt**

```cmake
    src/core/manga/ComicsPrePivotMigrator.cpp
    src/core/manga/ComicsPrePivotMigrator.h
```

### Task 6.2: Run migrator in main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 6.2.1: Insert migrator invocation**

Locate the existing manga-premium-dirs bootstrap block in `src/main.cpp` (around the `4c-premium-dirs-bootstrapped` log line per prior phase output). Immediately AFTER that block but BEFORE MainWindow construction, insert:

```cpp
#include "core/manga/ComicsPrePivotMigrator.h"
// ...
    {
        tankoban::manga::ComicsPrePivotMigrator migrator(bridge.dataDir() + QStringLiteral("/.."));
        const bool migratedNow = migrator.migrate();
        DebugLogBuffer::instance().info(QStringLiteral("ComicsPrePivotMigrator"),
            migratedNow
                ? QStringLiteral("pre-pivot library migrated to backup; volume mode active")
                : QStringLiteral("already migrated or fresh install; no-op"),
            QJsonObject{{QStringLiteral("backupDir"), migrator.backupDir()}});
    }
```

Note: `bridge.dataDir()` already returns the data dir under appData. The migrator wants the appData root (one level up) so it can place backup beside data dir.

- [ ] **Step 6.2.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 6.3: Phase 6 close — RTC line

- [ ] **Step 6.3.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 6 - ComicsPrePivotMigrator (burn-it-down). New: ComicsPrePivotMigrator.h/.cpp - one-shot startup migrator that moves comics_library.json + manga_downloads_index.json to <appData>/comics_pre_pivot_backup/ with a MIGRATED marker file, idempotent (subsequent launches see marker + skip). On-disk chapter folders left in place (harmless after index wipe; LibraryScanner won't surface them post-pivot). manga_posters/ thumb cache preserved per spec §8. Sidecar .tankoyomi-meta.json files left on disk (harmless after wipe). Wired into src/main.cpp immediately after the existing 4c-premium-dirs-bootstrapped block but BEFORE MainWindow construction; logs migration result via DebugLogBuffer so smoke evidence surfaces in tankoctl logs. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/ComicsPrePivotMigrator.h, src/core/manga/ComicsPrePivotMigrator.cpp, src/main.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 7 — ComicsSeriesView fork from StreamDetailView

**Phase goal:** Fork the Theatre series-page widget into a Comics-domain twin. Banner + meta + synopsis on the left, volume list below, sources panel placeholder on the right. AniList drives content; no source-panel population yet (Phase 8). Replaces the old `ComicsTankoyomiDetailView`.

**Files this phase touches:**

- Create: `src/ui/pages/comics/ComicsSeriesView.h`
- Create: `src/ui/pages/comics/ComicsSeriesView.cpp`
- Modify: `CMakeLists.txt`
- Delete (defer to Phase 9 when no callers remain): `src/ui/pages/comics/ComicsTankoyomiDetailView.h/.cpp`

**Reference design sections:** §3 (Path A fork), §4 component 6 (ComicsSeriesView).

### Task 7.1: Fork the Theatre detail view

**Files:**
- Create: `src/ui/pages/comics/ComicsSeriesView.h`
- Create: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 7.1.1: Copy StreamDetailView as the starting point**

```cmd
copy src\ui\pages\stream\StreamDetailView.h     src\ui\pages\comics\ComicsSeriesView.h
copy src\ui\pages\stream\StreamDetailView.cpp   src\ui\pages\comics\ComicsSeriesView.cpp
```

- [ ] **Step 7.1.2: Mechanical rename pass**

In both files, replace globally:
- `StreamDetailView` → `ComicsSeriesView`
- `MetaItemPreview` → `tankoban::manga::anilist::MediaPreview`
- `MetaItemDetail` → `tankoban::manga::anilist::MediaDetail`
- `MetaAggregator` → forward-decl `AniListClient` and `AniListCache`
- `Episode` (as a struct/type) → `VolumeRow`
- `episode` (variable names) → `volume`
- "Episodes" header label → "Volumes"
- Season picker dropdown → drop entirely (no season concept for manga; we deal with a single flat volume list + optional Vol X)
- `playEpisode` signal → `openVolume(QString cbzPath)` signal

- [ ] **Step 7.1.3: Replace meta + chapter loading with AniList integration**

The original StreamDetailView fetches meta + episodes from MetaAggregator. Replace that path: at `showSeries(MediaPreview)` entry:

1. `m_cache->get(preview.anilistId)` returns cached MediaDetail if present → render immediately from cache.
2. Fire `m_client->seriesById(preview.anilistId, m_requestId++)` as a background refetch.
3. On `seriesSucceeded`, update via `m_cache->put(detail)` + re-render.

Render flow:
- Banner: `bannerUrl` if non-empty, else stretched `coverFullUrl`
- Hero: title + meta line (`<author? if available> · <yearStarted> · <status humanized> · <format>`) + synopsis + genres
- Volume list: `AniListVolumeMapper::map(detail)` → for each VolumeRow, build a row in a QTableWidget with columns `# | Cover | Volume | Chapters (count) | Progress | Status | Download`
- Right-side: `m_sourcesPanel` placeholder (Phase 8 wires it). For now: a QLabel saying "Select a volume to see sources" centered in the right pane.

Add `openVolume` signal that fires when a row's Download button is clicked OR when a downloaded row is clicked-to-read.

- [ ] **Step 7.1.4: Wire into CMakeLists.txt**

```cmake
    src/ui/pages/comics/ComicsSeriesView.cpp
    src/ui/pages/comics/ComicsSeriesView.h
```

- [ ] **Step 7.1.5: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. The widget compiles in isolation; Phase 9 wires it into ComicsPage so the user can actually navigate to it.

### Task 7.2: Phase 7 close — RTC line

- [ ] **Step 7.2.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 7 - ComicsSeriesView fork from StreamDetailView. New: ComicsSeriesView.h/.cpp forked from src/ui/pages/stream/StreamDetailView.{h,cpp}; mechanical rename pass StreamDetailView->ComicsSeriesView + MetaItemPreview/MetaItemDetail->MediaPreview/MediaDetail + MetaAggregator->AniListClient+AniListCache + Episode->VolumeRow + season-picker dropped (manga has no season concept; flat vol list + optional Vol X); playEpisode signal renamed to openVolume(cbzPath). showSeries() entry consults cache first, fires AniListClient seriesById background refetch, populates via AniListVolumeMapper::map. Volume list QTableWidget columns # | Cover | Volume | Chapters (count) | Progress | Status | Download. Right-pane Sources panel placeholder QLabel for Phase 8. Banner uses bannerUrl when set, else stretched coverFullUrl. Hero meta line yearStarted + status + format. build_check green; widget compiles in isolation, Phase 9 wires into ComicsPage navigation.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsSeriesView.h, src/ui/pages/comics/ComicsSeriesView.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 8 — ComicsSourcesPanel + provider wiring

**Phase goal:** Right-pane widget inside ComicsSeriesView. Populates on volume-row click. Shows ranked source rows (catalog hit > tier-1 nyaa > tier-2 nyaa > WeebCentral packer). Click-to-download fires the appropriate provider's `requestVolume`.

**Files this phase touches:**

- Create: `src/ui/pages/comics/ComicsSourcesPanel.h`
- Create: `src/ui/pages/comics/ComicsSourcesPanel.cpp`
- Modify: `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` (replace placeholder with the panel)
- Modify: `CMakeLists.txt`

**Reference design sections:** §2 decision 1 (Stremio-style ranked list), §2 decision 10 (trust tier first, seeders break ties), §4 component 7 (ComicsSourcesPanel), §5 step 4 (sources flow).

### Task 8.1: ComicsSourcesPanel class

**Files:**
- Create: `src/ui/pages/comics/ComicsSourcesPanel.h`
- Create: `src/ui/pages/comics/ComicsSourcesPanel.cpp`

- [ ] **Step 8.1.1: Write the header**

```cpp
// src/ui/pages/comics/ComicsSourcesPanel.h
#pragma once

#include "core/manga/NyaaRuntimeSource.h"
#include "core/manga/anilist/AniListTypes.h"

#include <QList>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QVBoxLayout;

namespace tankoban::manga {
class NyaaRuntimeSource;
class WeebCentralVolumePacker;
namespace premium {
class PremiumCatalog;
class TorrentVolumeProvider;
}
}

namespace tankoban::manga::comics {

// One source row, generic across the three provider types.
struct UnifiedSourceRow {
    enum class Kind { Catalog, NyaaRuntime, WeebCentralPacker };
    Kind     kind;
    int      tier;         // 1 / 2 / 99 for nyaa; 1 always for catalog; 99 for WC
    QString  title;        // user-facing label
    QString  uploaderHint; // e.g. "1r0n" or "WeebCentral" or "VIZ Digital"
    int      seeders;      // -1 for WC; positive int for nyaa+catalog
    qint64   sizeBytes;    // best-effort estimate
    QString  magnetUri;    // for nyaa/catalog
    QString  infoHash;     // for nyaa/catalog
};

class ComicsSourcesPanel : public QWidget
{
    Q_OBJECT
public:
    ComicsSourcesPanel(premium::PremiumCatalog* catalog,
                        NyaaRuntimeSource* nyaa,
                        QWidget* parent = nullptr);

    // Clear panel and show the "Select a volume to see sources" empty state.
    void clear();

    // Populate panel with ranked sources for the given series + volume.
    void populate(const QString& seriesTitle, int anilistSeriesId,
                  const anilist::VolumeRow& vol, const QStringList& chapterIds);

signals:
    // Fired when user clicks Download on a row. Caller dispatches to the
    // appropriate provider.
    void downloadRequested(const UnifiedSourceRow& row,
                           const QString& seriesTitle, int anilistSeriesId,
                           int volumeNumber, const QStringList& chapterIds);

private slots:
    void onNyaaResults(int reqId, const QList<NyaaSourceCandidate>& results);
    void onNyaaFailed(int reqId, const QString& reason);

private:
    void appendRow(const UnifiedSourceRow& row);
    void renderEmpty();
    void renderRanked();

    premium::PremiumCatalog*      m_catalog;
    NyaaRuntimeSource*            m_nyaa;
    QListWidget*                  m_list;
    QLabel*                       m_emptyLabel;
    QString                       m_currentSeriesTitle;
    int                           m_currentAnilistId = 0;
    int                           m_currentVolNumber = 0;
    QStringList                   m_currentChapterIds;
    int                           m_pendingNyaaReqId = -1;
    QList<UnifiedSourceRow>       m_rows;
};

} // namespace tankoban::manga::comics
```

- [ ] **Step 8.1.2: Write the implementation skeleton**

The implementation:
1. On `populate(...)`: clear, store params, check `m_catalog->entryForSeriesAndVolume(...)` (extend catalog API in Task 8.2), append Catalog row if hit. Fire `m_nyaa->search(seriesTitle, volNumber, reqId)`. Always append WeebCentral packer row at the bottom (synthesizable from chapterIds).
2. On `onNyaaResults`: append each candidate as a row, re-sort + re-render.
3. On row click: emit `downloadRequested(row, ...)`.

The full code is mechanical Qt widget plumbing; expand similarly to existing source-tab widgets in `src/ui/pages/stream/`. Approximate ~250 LOC total.

- [ ] **Step 8.1.3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 8.2: Extend PremiumCatalog with entryForSeriesAndVolume

**Files:**
- Modify: `src/core/manga/PremiumCatalog.h`
- Modify: `src/core/manga/PremiumCatalog.cpp`

- [ ] **Step 8.2.1: Add accessor**

Add to PremiumCatalog public API:

```cpp
// Returns the volume entry for the given series id + volume number, if
// present in the catalog. Used by Sources panel for catalog-hit lookup.
std::optional<PremiumVolumeEntry> entryForSeriesAndVolume(
    const QString& seriesId, int volumeNumber) const;
```

Implementation: loop through `entryById(seriesId)->volumes` for matching `vol`.

- [ ] **Step 8.2.2: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`.

### Task 8.3: Wire panel into ComicsSeriesView

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h`
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 8.3.1: Replace placeholder with ComicsSourcesPanel**

Replace the placeholder QLabel from Phase 7 with `ComicsSourcesPanel m_sourcesPanel`. On volume-row click, call `m_sourcesPanel->populate(seriesTitle, anilistId, volumeRow, chapterIds)`. On `m_sourcesPanel->downloadRequested`, route to TorrentVolumeProvider (Catalog + NyaaRuntime kinds) or WeebCentralVolumePacker (WC kind).

- [ ] **Step 8.3.2: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`.

### Task 8.4: Phase 8 close — RTC line

- [ ] **Step 8.4.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 8 - ComicsSourcesPanel + provider wiring. New: ComicsSourcesPanel.h/.cpp - right-pane widget; populate(seriesTitle, anilistId, volumeRow, chapterIds) clears, queries PremiumCatalog::entryForSeriesAndVolume (new helper added), fires NyaaRuntimeSource::search, appends WeebCentralVolumePacker fallback row; renders ranked UnifiedSourceRow list (tier-1 catalog at top, tier-1 nyaa next, tier-2 nyaa, WC packer at bottom). Row click emits downloadRequested signal. ComicsSeriesView replaces the Phase 7 placeholder QLabel with m_sourcesPanel; routes downloadRequested to TorrentVolumeProvider (Catalog + NyaaRuntime kinds) or WeebCentralVolumePacker (WC kind). PremiumCatalog gains entryForSeriesAndVolume(seriesId, vol) -> std::optional<PremiumVolumeEntry>. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsSourcesPanel.h, src/ui/pages/comics/ComicsSourcesPanel.cpp, src/ui/pages/comics/ComicsSeriesView.h, src/ui/pages/comics/ComicsSeriesView.cpp, src/core/manga/PremiumCatalog.h, src/core/manga/PremiumCatalog.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 9 — Comics search refactor + delete old detail view

**Phase goal:** Collapse `ComicsTankoyomiSearchWidget` to a single AniList-primary list. Delete `ComicsTankoyomiDetailView`. Wire search results to open `ComicsSeriesView`.

**Files this phase touches:**

- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h/.cpp`
- Modify: `src/ui/pages/ComicsPage.{h,cpp}` (navigation rewire)
- Delete: `src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}`
- Modify: `CMakeLists.txt`

**Reference design sections:** §2 decisions 6 + 7 (single ranked AniList list, AniList-only backbone), §4 deleted-files (ComicsTankoyomiDetailView).

### Task 9.1: Collapse search widget to single AniList list

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp`

- [ ] **Step 9.1.1: Strip Phase 8 premium sectioning**

Remove from header:
- `m_premiumHeader / m_premiumStrip / m_premiumShowMore / m_premiumOverflow / m_premiumCatalog`
- `setPremiumCatalog(...)`
- `revealPremiumOverflow()`
- `injectPremiumCatalogSynthetics(...)`

Remove from .cpp: corresponding implementations + the routing branch in `onSearchFinished` that diverts premium-title results to the premium strip.

Remove also the Manga + Comics section split. Replace with a single TileStrip + header "RESULTS".

- [ ] **Step 9.1.2: Replace MangaScraper fan-out with AniList search**

The current widget fires `MangaSourceRegistry::search(query)` which fans out to WeebCentralScraper + ReadComicsScraper. Replace with `AniListClient::searchByTitle(query, requestId)`. On `searchSucceeded`, render each MediaPreview as a TileCard in the single strip.

Tile click emits `seriesActivated(MediaPreview preview)` (signature change from MangaResult to MediaPreview).

- [ ] **Step 9.1.3: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`. ComicsPage's connect to `seriesActivated` will need updating in Task 9.2.

### Task 9.2: ComicsPage navigation rewire

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 9.2.1: Replace m_tyDetailView with m_seriesView**

In ComicsPage.h:
```cpp
// Forward decls
namespace tankoban::manga::comics { class ComicsSeriesView; }

private:
    tankoban::manga::comics::ComicsSeriesView* m_seriesView = nullptr;
```

In ComicsPage.cpp constructor:
- Delete the existing `m_tyDetailView = new ComicsTankoyomiDetailView(...)` block + all its setter calls (setPremiumCatalog, setAdoptLookup, etc.)
- Replace with `m_seriesView = new ComicsSeriesView(m_aniListClient, m_aniListCache, m_premiumCatalog, m_nyaaRuntime, m_weebCentralPacker, this);`
- Add to QStackedWidget
- Connect: `m_searchTakeover seriesActivated -> m_seriesView->showSeries`

- [ ] **Step 9.2.2: Delete ComicsTankoyomiDetailView files**

```cmd
del src\ui\pages\comics\ComicsTankoyomiDetailView.h
del src\ui\pages\comics\ComicsTankoyomiDetailView.cpp
```

Remove the two entries from CMakeLists.txt.

- [ ] **Step 9.2.3: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`. Any leftover references to ComicsTankoyomiDetailView fail to link — fix as they surface.

### Task 9.3: Phase 9 close — RTC line

- [ ] **Step 9.3.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 9 - Comics search refactor + delete legacy detail view. ComicsTankoyomiSearchWidget stripped: m_premiumHeader/Strip/ShowMore/Overflow + m_premiumCatalog + setPremiumCatalog + injectPremiumCatalogSynthetics + revealPremiumOverflow all REMOVED (Phase 8 premium-sectioning logic deleted). Manga+Comics type split also REMOVED. Replaced with single TileStrip + RESULTS header. Search backbone switched MangaSourceRegistry::search -> AniListClient::searchByTitle. seriesActivated signal signature changed MangaResult -> MediaPreview. ComicsPage: m_tyDetailView REMOVED, m_seriesView (new ComicsSeriesView) added in its place. ComicsTankoyomiDetailView.h/.cpp DELETED. Navigation rewired: search-takeover seriesActivated -> m_seriesView->showSeries. build_check green; all linker references to the deleted detail view caught + fixed in this phase.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsTankoyomiSearchWidget.h, src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp, src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, CMakeLists.txt, agents/chat.md (DELETED: src/ui/pages/comics/ComicsTankoyomiDetailView.h, src/ui/pages/comics/ComicsTankoyomiDetailView.cpp)
```

---

## Phase 10 — Comics landing library + bookmark store

**Phase goal:** Burn the existing Tankoyomi-library + folder-import library tile rendering. New landing: downloaded series + bookmarked-but-not-downloaded series. Bookmark store backed by AniListCache (which already manages bookmarks). Continue strip volume-keyed.

**Files this phase touches:**

- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

**Reference design sections:** §2 decision 8 (burn-it-down, applied at UI layer here), §2 decision 9 (library = downloads + bookmarks).

### Task 10.1: Rewire Comics landing tiles

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 10.1.1: Strip pre-pivot tile rendering**

Remove all references to:
- `m_tyLibrary->all()` iteration → replaced by bookmarked series tile rendering
- `m_folderSeries` iteration → replaced by downloaded-series tile rendering
- `addSeriesTile(seriesInfoFromRecord(r))` → replaced by `addBookmarkedTile` / `addDownloadedTile`

The new landing has:
- **DOWNLOADED** section: enumerate `m_mangaDownloadIndex->entriesForAllSeries()` (extend index API), group by series, one tile per series with cover from AniListCache.
- **BOOKMARKED** section: `m_aniListCache->bookmarkedPreviews()` → one tile per bookmark.
- Tile click → `m_searchTakeover->seriesActivated(preview)` shape → `m_seriesView->showSeries(preview)`.

- [ ] **Step 10.1.2: Extend MangaDownloadIndex with entriesForAllSeries**

Add to MangaDownloadIndex.h:

```cpp
// Returns one Entry per distinct seriesKey. Used by ComicsPage landing
// to render the downloaded-series tile list.
QList<Entry> entriesForAllSeries() const;
```

Implementation: iterate m_seriesHasAny, for each series take the first entry from m_byPath that matches (or a representative).

- [ ] **Step 10.1.3: Continue strip volume-keyed**

Refactor `refreshContinueStrip` (rewritten in Phase 7 of the prior arc). Continue tile now shows `<series> — Vol N — page X/Y`. Progress is keyed by canonical chapter key (Phase 5 plumbing stays) but the user-facing string is volume-keyed.

- [ ] **Step 10.1.4: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`.

### Task 10.2: Phase 10 close — RTC line

- [ ] **Step 10.2.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 10 - Comics landing library + bookmark store. ComicsPage landing fully rewritten: pre-pivot Tankoyomi-library + folder-import tile rendering REMOVED; new model surfaces two sections, DOWNLOADED (entries from MangaDownloadIndex.entriesForAllSeries grouped by series) + BOOKMARKED (AniListCache.bookmarkedPreviews). Each tile shows AniList-cached cover/title; click routes to ComicsSeriesView. refreshContinueStrip rewritten: continue tiles now show `<series> - Vol N - page X/Y` user-facing (canonical chapter key Phase 5 plumbing preserved under the hood for cross-source resolution). MangaDownloadIndex gains entriesForAllSeries() helper. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, src/core/manga/MangaDownloadIndex.h, src/core/manga/MangaDownloadIndex.cpp, agents/chat.md
```

---

## Phase 11 — MangaTransferCoordinator extension

**Phase goal:** Add `WeebCentralVolumePacker` to the pauseAll/resumeAll fan-out. Three-backend coordinator now: MangaDownloader (legacy HTTP chapter downloads, deprecated but still linked), TorrentVolumeProvider, WeebCentralVolumePacker.

**Files this phase touches:**

- Modify: `src/core/manga/MangaTransferCoordinator.h`
- Modify: `src/core/manga/MangaTransferCoordinator.cpp`

**Reference design sections:** §4 surviving (MangaTransferCoordinator extended in P11).

### Task 11.1: Add WC packer to coordinator

**Files:**
- Modify: `src/core/manga/MangaTransferCoordinator.h/.cpp`

- [ ] **Step 11.1.1: Extend constructor**

Add `WeebCentralVolumePacker* packer` as a fourth ctor arg. Store as `QPointer<WeebCentralVolumePacker> m_packer`.

- [ ] **Step 11.1.2: Fan out to all three backends**

In `pauseAll()`:
```cpp
if (m_downloader) m_downloader->pauseAll();
if (m_provider)   m_provider->pauseAll();
if (m_packer)     m_packer->pauseAll();
emit pausedChanged(true);
```

Same shape for `resumeAll()`. `isPaused()` returns `dPaused && pPaused && (wcPaused || !m_packer)`.

- [ ] **Step 11.1.3: Update ComicsPage construction site**

Pass `m_weebCentralPacker` as the new fourth arg.

- [ ] **Step 11.1.4: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`.

### Task 11.2: Phase 11 close — RTC line

- [ ] **Step 11.2.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 11 - MangaTransferCoordinator extends to WeebCentralVolumePacker. Coordinator now fans out pauseAll/resumeAll across THREE backends: MangaDownloader (legacy HTTP, deprecated but still linked for migration), TorrentVolumeProvider, WeebCentralVolumePacker. isPaused returns true only when all three paused (with null-guards via QPointer). Constructor gains 4th arg packer; ComicsPage construction site updated. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/MangaTransferCoordinator.h, src/core/manga/MangaTransferCoordinator.cpp, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 12 — Cover extractor integration + AniList per-vol art

**Phase goal:** ComicsSeriesView renders AniList per-vol art (or series cover fallback) on each volume row pre-download. Phase 10 cover extractor's post-download replacement still fires; series view receives `volumeCoverReady` and swaps the row's icon.

**Files this phase touches:**

- Modify: `src/ui/pages/comics/ComicsSeriesView.{h,cpp}`
- Modify: `src/ui/pages/ComicsPage.cpp` (wire volumeCoverReady from provider + packer)

**Reference design sections:** §2 decision 11 (three-stage cover resolution), §4 component PremiumCoverExtractor (surviving).

### Task 12.1: Render AniList per-vol art

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

- [ ] **Step 12.1.1: VolumeRow rendering uses AniListVolumeArt**

In the row builder for the QTableWidget, use `volumeRow.art.thumbnailUrl` if non-empty, else fall back to `detail.preview.coverThumbUrl`. Lazy-load via QNetworkAccessManager + QPixmapCache.

- [ ] **Step 12.1.2: Add setVolumeCoverFromDisk slot**

```cpp
void ComicsSeriesView::setVolumeCoverFromDisk(const QString& seriesId, int volumeNumber,
                                              const QString& coverPath)
{
    // Find the row matching volumeNumber + seriesId; setData(DecorationRole, QIcon(coverPath))
    // Replaces the AniList-loaded thumb with the actual cbz-extracted cover.
}
```

- [ ] **Step 12.1.3: Wire volumeCoverReady signals**

In ComicsPage, connect:
- `m_torrentVolumeProvider volumeCoverReady -> m_seriesView setVolumeCoverFromDisk`
- `m_weebCentralPacker volumeCoverReady -> m_seriesView setVolumeCoverFromDisk`

(The WC packer needs to fire cover-extractor too post-finalize. Add the call in WeebCentralVolumePacker::finalizePack right before emit volumeCompleted, paralleling TorrentVolumeProvider's Phase 10 wiring.)

- [ ] **Step 12.1.4: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`.

### Task 12.2: Phase 12 close — RTC line

- [ ] **Step 12.2.1: Append the phase-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 12 - Cover extractor integration + AniList per-vol art. ComicsSeriesView VolumeRow rendering uses volumeRow.art.thumbnailUrl when non-empty else preview.coverThumbUrl fallback; lazy-load via QNetworkAccessManager + QPixmapCache. setVolumeCoverFromDisk(seriesId, vol, path) public slot walks the QTableWidget for matching row + setData(DecorationRole, QIcon(path)) for post-download cover replacement. ComicsPage wires volumeCoverReady from BOTH m_torrentVolumeProvider AND m_weebCentralPacker (packer also fires Phase 10 cover extractor in finalizePack). Three-stage cover resolution complete: AniList per-vol -> series cover fallback -> cbz-extracted post-download. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsSeriesView.h, src/ui/pages/comics/ComicsSeriesView.cpp, src/ui/pages/ComicsPage.cpp, src/core/manga/WeebCentralVolumePacker.cpp, agents/chat.md
```

---

## Phase 13 — Smoke matrix + arc close

**Phase goal:** Execute the 7-case smoke matrix from spec §7. Arc-close RTC.

**Files this phase touches:**

- Modify: `agents/chat.md` (MCP LOCK + smoke evidence posts + arc-close RTC)
- Create: `agents/audits/smoke_evidence/02XX_volume_pivot_*.png` (screenshots)

### Task 13.1: MCP smoke session

- [ ] **Step 13.1.1: Claim MCP lock**

```
MCP LOCK - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 13 7-case smoke]: expecting ~30 min. Smoke 1-7 per spec.
```

- [ ] **Step 13.1.2: Execute Smoke 1 — Search renders single list**

`taskkill /F /IM Tankoban.exe`; `build_and_run.bat`; wait; `out\tankoctl.exe ping`; via pywinauto-mcp click search input + type "death note"; screenshot. Verify single ranked list (no PREMIUM/MANGA/COMICS sections).

- [ ] **Step 13.1.3: Execute Smoke 2 — Series view opens with vol list**

Click Death Note tile. Screenshot. Verify ComicsSeriesView opens with banner + 12 volume rows + per-vol art from AniList cache.

- [ ] **Step 13.1.4: Execute Smoke 3 — Vol click populates Sources panel**

Click Vol 1 row. Screenshot. Verify Sources panel populates with the 1r0n KG Manga catalog hit + WeebCentral fallback.

- [ ] **Step 13.1.5: Execute Smoke 4 — Catalog source download completes**

Click the 1r0n source row. Wait for completion (~5-10 min real download). Screenshot. Verify row Status flips to Downloaded; cover thumb replaced with cbz-extracted art.

- [ ] **Step 13.1.6: Execute Smoke 5 — WeebCentral fallback download**

Search a non-catalog series (e.g. "Chainsaw Man"). Open series view. Click a vol → only WeebCentral fallback shown. Click Download. Verify HTTP chapter-pack flow completes. Screenshot.

- [ ] **Step 13.1.7: Execute Smoke 6 — Bookmark + AniList offline survives**

Bookmark Death Note via Add-to-library. Close app. Disconnect network. Reopen. Verify Death Note tile appears on Comics landing under BOOKMARKED. Open it; series view renders from cache without crashing.

- [ ] **Step 13.1.8: Execute Smoke 7 — Vol X auto-shrink on AniList refresh**

Open One Piece series view (Vol X exists). Manually edit `<appData>/anilist_cache/series_<onePieceId>.json` to add a new bound vol (e.g. add `"volumes": 112` and shift the chapter count). Re-open the series view. Verify Vol X has shrunk + new Vol 112 appended.

- [ ] **Step 13.1.9: Release MCP lock**

```
MCP LOCK RELEASED - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 13 smoke]: <N>/7 PASS.
```

Rule 17 cleanup: `taskkill /F /IM Tankoban.exe`; `taskkill /F /IM ffmpeg_sidecar.exe`.

### Task 13.2: ARC CLOSE RTC

- [ ] **Step 13.2.1: Append the arc-close RTC**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_VOLUME_PIVOT ARC CLOSED. 13 phases shipped: P1 AniList types + GraphQL client, P2 AniListCache (file-backed, bookmark-aware), P3 AniListVolumeMapper (pure-logic, TDD-verified via tankoban_tests), P4 NyaaRuntimeSource (uploader-trust-filtered runtime nyaa search), P5 WeebCentralVolumePacker (HTTP-fetch + zip + Phase 4 finalize integration), P6 ComicsPrePivotMigrator (burn-it-down), P7 ComicsSeriesView fork from StreamDetailView, P8 ComicsSourcesPanel + provider wiring (catalog + nyaa + WC packer ranked rows), P9 search refactor + ComicsTankoyomiDetailView deletion, P10 Comics landing library + bookmark store (downloaded + bookmarked sections), P11 MangaTransferCoordinator extends to WC packer (3-backend fan-out), P12 Cover extractor integration + AniList per-vol art (three-stage cover), P13 7-case smoke matrix executed. Volume-only Comics mode live: AniList drives metadata + chapter-to-volume mapping; catalog provides precision for tier-1 1r0n/Hox/VIZ Digital uploaders; runtime nyaa fills in non-catalog series; WeebCentralVolumePacker synthesizes vol cbzs from chapter fetches for any vol with no torrent source. Stremio-for-manga MVP is now visually live end-to-end. New code footprint: ~XXX LOC across XX files. Smoke results: <N>/7 PASS. Forward-compat hooks recorded in spec §9 (v2 generalize StreamDetailView/ComicsSeriesView, v2 hot-reload catalog, v2 chapter-level resume, v3 AniList tracking write-back). Spec doc at docs/superpowers/specs/2026-05-16-tankoyomi-volume-pivot-design.md; plan doc at docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot.md. Predecessor 11-phase TANKOYOMI_PREMIUM_MVP arc preserved as infrastructure substrate; only its UI half (Phases 6/7/8 + 9 adopt-folder) was ripped out.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion, /superpowers:finishing-a-development-branch] | files: agents/chat.md, agents/audits/smoke_evidence/02XX_volume_pivot_*.png
```

---

## Self-Review

Inline self-review against the spec per the writing-plans skill checklist.

**1. Spec coverage**

Walking each spec section and pointing to the implementing task:

- §1 Vision (Stremio-for-manga; volume is only first-class UI unit) → realized across P1-P13 collectively; specifically P7 (no chapter UI) + P10 (continue strip volume-keyed).
- §2 decision 1 (Stremio-style ranked Sources panel) → P8.
- §2 decision 2 (Vol X = un-bound chapters) → P3 + tests.
- §2 decision 3 (WeebCentral fallback = HTTP-fetch + zip-on-the-fly) → P5.
- §2 decision 4 (strictly volume-keyed read-state) → P10 (continue strip refactor).
- §2 decision 5 (hybrid catalog) → P4 (runtime nyaa) + surviving Phase 1 catalog.
- §2 decision 6 (single ranked search) → P9.
- §2 decision 7 (AniList-only search) → P1 + P9.
- §2 decision 8 (burn-it-down) → P6 (file move) + P10 (UI burn).
- §2 decision 9 (library = downloads + bookmarks) → P10.
- §2 decision 10 (trust tier first, seeders break ties) → P4 + P8 (ranking).
- §2 decision 11 (three-stage cover) → P12.
- §2 decision 12 (cache on first fetch, refresh on series open) → P2.
- §3 Architecture path A (fork) → P7.
- §4 Components — all 7 new components have a phase: P1 (AniListClient), P2 (Cache), P3 (Mapper), P4 (Nyaa), P5 (WC), P7 (SeriesView), P8 (SourcesPanel). Surviving + deleted handled in P9/P10/P11/P12.
- §5 Data flow — exact happy path realized across phases; alt WC path in P5.
- §6 Error handling — addressed implicitly per phase but no dedicated phase. Acceptable: each provider handles its own failure modes inline (P1 emit *Failed signals; P5 zip_failed + validation_failed branches; P10 validateAll on existing chapter-folder loss).
- §7 Testing — P13 smoke matrix.
- §8 Burn-it-down spec — P6 (file moves) + P10 (UI tile burn).
- §9 Forward-compat → not implemented (correct).
- §10 Plan handoff hooks → this plan.

**Gap**: §6 error handling for AniList offline (use cache; show "Offline" search empty state) is mentioned but not explicitly tasked. Acceptable because the cache-first behavior is the natural consequence of P2 + P7 (showSeries consults cache before firing AniListClient). The "Offline" empty state in search is a Phase 9 polish item that should be inline-added; flag this and add to P9.

**Update to P9**: Add a Step 9.1.4 noting that when `AniListClient::searchFailed` fires AND `m_aniListCache->bookmarkedPreviews()` is non-empty, the search results render bookmarked series as the offline-fallback list with a status banner "Offline — showing bookmarked series only". When cache is empty too, render an empty-state QLabel.

**2. Placeholder scan**

Scanning the plan body for `TBD` / `TODO` / `implement later` / `XXX` / `LATER` / "add appropriate error handling" / "similar to Task N":
- Phase 8 Task 8.1.2: "The full code is mechanical Qt widget plumbing; expand similarly to existing source-tab widgets in `src/ui/pages/stream/`. Approximate ~250 LOC total." → **PLACEHOLDER**. Fix: expand into a concrete implementation block similar to Phase 7's mechanical-rename pattern.

Replacement for Task 8.1.2:

```cpp
// src/ui/pages/comics/ComicsSourcesPanel.cpp
#include "ComicsSourcesPanel.h"
#include "core/manga/PremiumCatalog.h"
#include "core/manga/NyaaRuntimeSource.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace tankoban::manga::comics {

namespace {
constexpr int kSourcesNyaaRequestId = 7700;

UnifiedSourceRow rowFromCatalogEntry(const premium::PremiumVolumeEntry& v, const QString& seriesTitle)
{
    UnifiedSourceRow r;
    r.kind         = UnifiedSourceRow::Kind::Catalog;
    r.tier         = 1;
    r.title        = QStringLiteral("Catalog: ") + seriesTitle + QStringLiteral(" Vol ") + QString::number(v.vol);
    r.uploaderHint = QStringLiteral("Curated");
    r.seeders      = -2; // unknown at panel-render time; live nyaa query for the same infoHash could resolve
    r.sizeBytes    = v.fileSizeBytes;
    r.magnetUri    = QString();
    r.infoHash     = QString();
    return r;
}

UnifiedSourceRow rowFromNyaa(const NyaaSourceCandidate& c)
{
    UnifiedSourceRow r;
    r.kind         = UnifiedSourceRow::Kind::NyaaRuntime;
    r.tier         = c.tier;
    r.title        = c.title;
    r.uploaderHint = c.uploader;
    r.seeders      = c.seeders;
    r.sizeBytes    = c.sizeBytes;
    r.magnetUri    = c.magnetUri;
    r.infoHash     = c.infoHash;
    return r;
}

UnifiedSourceRow rowFromWeebCentral(int chapterCount, qint64 estBytes)
{
    UnifiedSourceRow r;
    r.kind         = UnifiedSourceRow::Kind::WeebCentralPacker;
    r.tier         = 99;
    r.title        = QStringLiteral("WeebCentral — pack ") + QString::number(chapterCount) + QStringLiteral(" chapters");
    r.uploaderHint = QStringLiteral("WeebCentral");
    r.seeders      = -1;
    r.sizeBytes    = estBytes;
    return r;
}
} // anonymous

ComicsSourcesPanel::ComicsSourcesPanel(premium::PremiumCatalog* catalog,
                                       NyaaRuntimeSource* nyaa,
                                       QWidget* parent)
    : QWidget(parent), m_catalog(catalog), m_nyaa(nyaa)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    m_emptyLabel = new QLabel(QStringLiteral("Select a volume to see sources"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_list = new QListWidget(this);
    m_list->setVisible(false);
    root->addWidget(m_emptyLabel, 1);
    root->addWidget(m_list, 1);

    if (m_nyaa) {
        connect(m_nyaa, &NyaaRuntimeSource::searchSucceeded,
                this, &ComicsSourcesPanel::onNyaaResults);
        connect(m_nyaa, &NyaaRuntimeSource::searchFailed,
                this, &ComicsSourcesPanel::onNyaaFailed);
    }

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_rows.size()) return;
        emit downloadRequested(m_rows.at(idx), m_currentSeriesTitle,
                                m_currentAnilistId, m_currentVolNumber, m_currentChapterIds);
    });
}

void ComicsSourcesPanel::clear()
{
    m_rows.clear();
    m_list->clear();
    m_list->setVisible(false);
    m_emptyLabel->setVisible(true);
}

void ComicsSourcesPanel::populate(const QString& seriesTitle, int anilistSeriesId,
                                   const anilist::VolumeRow& vol,
                                   const QStringList& chapterIds)
{
    clear();
    m_currentSeriesTitle = seriesTitle;
    m_currentAnilistId   = anilistSeriesId;
    m_currentVolNumber   = vol.volumeNumber;
    m_currentChapterIds  = chapterIds;
    m_emptyLabel->setVisible(false);
    m_list->setVisible(true);

    // 1. Catalog hit (precision tier-1).
    // PremiumCatalog uses its own app-internal seriesId keying; the
    // anilistSeriesId arg here is informational. For a real catalog hit
    // we need a seriesId-resolution helper that maps anilistId -> catalog
    // seriesId. v1 simplification: catalog ships an `anilistId` field; we
    // resolve via that.
    if (m_catalog) {
        for (const auto& entry : m_catalog->allEntries()) {
            if (entry.anilistId == anilistSeriesId) {
                auto volEntryOpt = m_catalog->entryForSeriesAndVolume(entry.seriesId, vol.volumeNumber);
                if (volEntryOpt) {
                    appendRow(rowFromCatalogEntry(*volEntryOpt, seriesTitle));
                }
                break;
            }
        }
    }

    // 2. WeebCentral fallback (always available if chapter list non-empty).
    if (!chapterIds.isEmpty()) {
        const qint64 estBytes = static_cast<qint64>(chapterIds.size()) * 20LL * 1024 * 1024; // 20 MiB/chapter rough
        appendRow(rowFromWeebCentral(chapterIds.size(), estBytes));
    }

    renderRanked();

    // 3. Nyaa runtime (async; results populate via onNyaaResults).
    if (m_nyaa) {
        m_pendingNyaaReqId = kSourcesNyaaRequestId;
        m_nyaa->search(seriesTitle, vol.volumeNumber, m_pendingNyaaReqId);
    }
}

void ComicsSourcesPanel::onNyaaResults(int reqId, const QList<NyaaSourceCandidate>& results)
{
    if (reqId != m_pendingNyaaReqId) return;
    for (const auto& c : results) {
        appendRow(rowFromNyaa(c));
    }
    renderRanked();
}

void ComicsSourcesPanel::onNyaaFailed(int reqId, const QString& reason)
{
    if (reqId != m_pendingNyaaReqId) return;
    Q_UNUSED(reason);
    // Silent fail: catalog + WC fallback rows still render; user gets a working
    // experience even if nyaa is unreachable.
}

void ComicsSourcesPanel::appendRow(const UnifiedSourceRow& row)
{
    m_rows.append(row);
}

void ComicsSourcesPanel::renderRanked()
{
    std::sort(m_rows.begin(), m_rows.end(),
              [](const UnifiedSourceRow& a, const UnifiedSourceRow& b) {
                  if (a.tier != b.tier) return a.tier < b.tier;
                  return a.seeders > b.seeders;
              });
    m_list->clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto& r = m_rows.at(i);
        const QString label = QStringLiteral("[T%1] %2 — %3 — %4")
            .arg(r.tier == 99 ? QString("WC") : QString::number(r.tier))
            .arg(r.title)
            .arg(r.seeders >= 0 ? QString("%1 seeders").arg(r.seeders) : QString("HTTP"))
            .arg(r.sizeBytes > 0 ? QString("%1 MiB").arg(r.sizeBytes / (1024 * 1024)) : QString("?"));
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, i);
    }
}

} // namespace tankoban::manga::comics
```

(Inline-fix landed in plan.)

**3. Type consistency**

Walking type names across phases:
- `MediaPreview` (P1) used in P2 cache + P7 series view + P9 search + P10 landing. Consistent.
- `MediaDetail` (P1) used in P2 cache + P3 mapper + P7 series view. Consistent.
- `VolumeRow` (P1) returned by P3 mapper + consumed by P7 + P8 + P12. Consistent.
- `kVolumeXNumber` (P1) used by P3 + P7 (when rendering "Volume X" label conditional on `isVolumeX` flag). Consistent.
- `NyaaSourceCandidate` (P4) used by P4 + P8. Consistent.
- `VolumePackRequest` (P5) used by P5 + P8 (panel constructs this for the WC source row click). Consistent.
- `UnifiedSourceRow` (P8) used by P8. New.
- `AniListVolumeArt` (P1) used by P2 cache + P12 rendering. Consistent.
- `PremiumVolumeEntry` (Phase 1 surviving) — its `anilistId` field is referenced in P8 source-resolution; verify Phase 1 schema already has this field (it does, per the prior arc's PremiumCatalogSchema.h:48 + the catalog file shape used in Phase 11 curation; `anilistId` was populated for Death Note). Consistent.

No type drift detected.

**4. Scope check**

One coherent pivot of one subsystem. 13 phases. Each independently buildable. No phase requires content from a later phase. Plan ready.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per phase with two-stage review (spec compliance + code quality) between phases. Fast iteration; each phase context is clean.

**2. Inline Execution** — Execute phases in this session using `superpowers:executing-plans`. Batch execution within each phase, RTC checkpoint at phase close.

Which approach?
