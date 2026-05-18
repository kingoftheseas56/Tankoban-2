# Tankoyomi MangaUpdates Fallback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When AniList returns null total volumes / total chapters for an ongoing manga series, fall back to MangaUpdates to recover the real counts so the Stremio-style volume list renders real Vol 1..N rows instead of a Vol X placeholder.

**Architecture:** Layered, not replacement. AniList keeps doing identity / search / banner / cover / synopsis / title-localization. A new `MangaUpdatesClient` fires only when `AniListClient::seriesSucceeded` returns a RELEASING or HIATUS series with `totalVolumes <= 0` or `totalChapters <= 0`. A `VolumeMetadataResolver` orchestrates: MangaUpdates search by title -> disambiguate by author + year against the AniList preview -> detail fetch -> parse the leading `(\d+) Volumes` token out of the human-readable `status` text -> persist a sidecar JSON in the existing AniList cache dir keyed by `anilistId` -> emit `resolved(anilistId, volumeCount, chapterCount)`. `ComicsPage` consumes the signal, re-runs the existing series-view render path with an `AniListVolumeMapper::map` override overload that takes the MangaUpdates counts. On any resolver failure (network, no-match, malformed status), the existing Vol X placeholder behavior is preserved -- no regression.

**Tech Stack:** C++20, Qt 6, `QNetworkAccessManager`, `QJsonDocument`, GoogleTest (via existing `tankoban_tests` opt-in target). No new vcpkg deps. No Python. No new build-system surface.

**Brotherhood conventions (load-bearing):**
- ASCII only in source files (memory `feedback_no_color_no_emoji.md`).
- No worktrees -- flat single-checkout on master (memory `feedback_no_worktrees.md`). Skill's "dedicated worktree" guidance is overridden.
- `build_check.bat` after every src/ touch (CLAUDE.md Tier 1 `/build-verify`).
- One fix per rebuild (memory `feedback_one_fix_per_rebuild.md`).
- Smoke cleanup per Rule 17: `powershell -NoProfile -File scripts/stop-tankoban.ps1` after any agent-driven launch.
- RTC format per contracts-v3: include `Skills invoked: [...]` field on non-trivial RTCs.
- TDD applies ONLY to the two pure-logic primitives (status parser, disambiguator). Everything else smokes via integration -- per CLAUDE.md "opt-in ONLY for tankoban_tests pure-logic primitives".

---

## Reference Data (from Codex audit `agents/audits/manga_volume_metadata_sources_2026-05-16.md`)

MangaUpdates API:
- `POST https://api.mangaupdates.com/v1/series/search` body `{"search": "<title>", "page": 1, "per_page": 10}`
- `GET https://api.mangaupdates.com/v1/series/{series_id}`
- Unauthenticated. No rate limit advertised; we throttle to 1 req/sec to be polite (mirrors AniListClient pattern).

Empirically-verified series counts (Codex 2026-05-13):
- Death Note (Tsugumi Ohba): series_id 3479935384, `status: "12 Volumes + 1 Extra Volume (Complete); 7 Bunkoban Volumes (Complete); 1 Bunkoban Volume (Complete)"`, latest_chapter 108, completed true
- One Piece (Eiichiro Oda): series_id 55099564912, `status: "114 Volumes (Ongoing)"`, latest_chapter 1182, completed false
- Berserk (Kentaro Miura): series_id 51239621230, `status: "43 Volumes (Ongoing)"`, latest_chapter 383, completed false
- Kingdom (Yasuhisa Hara): series_id 4324727424, `status: "79 Volumes (Ongoing)"`, latest_chapter 832, completed false

Status string parsing rule: extract the FIRST `(\d+) Volumes` token only -- ignore Bunkoban / Extra Volume / variant editions.

---

## File Structure

**New files (created by this plan):**

```
src/core/manga/mangaupdates/
  MangaUpdatesTypes.h               -- POD: MangaUpdatesSearchHit, MangaUpdatesSeriesInfo
  MangaUpdatesStatusParser.h        -- pure: parseLeadingVolumeCount(QString) -> int
  MangaUpdatesStatusParser.cpp
  MangaUpdatesDisambiguator.h       -- pure: bestMatch(hits, anilistPreview, year) -> int (series_id)
  MangaUpdatesDisambiguator.cpp
  MangaUpdatesClient.h              -- HTTP layer: searchByTitle, seriesById, signal-based
  MangaUpdatesClient.cpp
  VolumeMetadataResolver.h          -- orchestrator: resolveForAnilist(anilistId, MediaPreview)
  VolumeMetadataResolver.cpp

tests/core/manga/
  MangaUpdatesStatusParserTest.cpp
  MangaUpdatesDisambiguatorTest.cpp
```

**Existing files modified:**

```
src/core/manga/anilist/AniListCache.h    -- add MangaUpdates sidecar getter/setter
src/core/manga/anilist/AniListCache.cpp  -- file-backed sidecar at mangaupdates_<anilistId>.json
src/core/manga/anilist/AniListVolumeMapper.h    -- new overload with override params
src/core/manga/anilist/AniListVolumeMapper.cpp  -- override logic
tests/core/manga/AniListVolumeMapperTest.cpp    -- 2 new test cases for override path
src/ui/pages/ComicsPage.h    -- m_volumeResolver member + forward decls
src/ui/pages/ComicsPage.cpp  -- construct resolver, wire resolved signal, re-render on hit
CMakeLists.txt               -- register new source files + test files
```

---

## Task 1: MangaUpdates POD Types

**Files:**
- Create: `src/core/manga/mangaupdates/MangaUpdatesTypes.h`
- Modify: `CMakeLists.txt` (add header to install-glob if relevant; usually headers are auto-discovered)

- [ ] **Step 1: Create the POD types header**

Create `src/core/manga/mangaupdates/MangaUpdatesTypes.h` with this content:

```cpp
// src/core/manga/mangaupdates/MangaUpdatesTypes.h
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace tankoban::manga::mangaupdates {

// One row from the MangaUpdates POST /v1/series/search results list.
// Slimmed to the fields the disambiguator uses.
struct MangaUpdatesSearchHit {
    qint64      seriesId      = 0;     // MangaUpdates series_id (64-bit, see Kingdom: 4324727424)
    QString     title;                  // primary title
    QStringList altTitles;              // associated_names
    QStringList authors;                // author display names
    int         yearStarted   = 0;      // parsed from "year" field if present
    QString     description;            // short description
    QString     imageUrl;               // cover thumb
};

// Output of GET /v1/series/{series_id}. The status string is the load-bearing
// field -- it is human-readable text like "114 Volumes (Ongoing)" or
// "12 Volumes + 1 Extra Volume (Complete); 7 Bunkoban Volumes (Complete)".
// We parse the FIRST "(\d+) Volumes" token (see MangaUpdatesStatusParser).
struct MangaUpdatesSeriesInfo {
    qint64    seriesId        = 0;
    QString   title;
    QString   rawStatus;       // raw status text; keep for debugging
    int       volumeCount     = 0;     // parsed from rawStatus; 0 if parse failed
    int       latestChapter   = 0;     // numeric, from latest_chapter field
    bool      completed       = false;
    QString   description;
    QString   imageUrl;
    QDateTime lastUpdated;     // from last_updated ISO-8601 field
    qint64    fetchedAtMs     = 0;     // local timestamp for cache freshness
};

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 2: Build check**

Run: `build_check.bat`
Expected: `BUILD OK` (no .cpp yet, header-only addition is a no-op build).

- [ ] **Step 3: Commit**

```bash
git add src/core/manga/mangaupdates/MangaUpdatesTypes.h
git commit -m "feat(manga): add MangaUpdates POD types (Task 1 of MANGAUPDATES_FALLBACK)"
```

---

## Task 2: Status String Parser (Pure Logic + TDD)

**Files:**
- Create: `src/core/manga/mangaupdates/MangaUpdatesStatusParser.h`
- Create: `src/core/manga/mangaupdates/MangaUpdatesStatusParser.cpp`
- Create: `tests/core/manga/MangaUpdatesStatusParserTest.cpp`
- Modify: `CMakeLists.txt` (register .cpp in `tankoban_core` source list and test .cpp in `tankoban_tests` source list)

- [ ] **Step 1: Write the failing test first**

Create `tests/core/manga/MangaUpdatesStatusParserTest.cpp`:

```cpp
// tests/core/manga/MangaUpdatesStatusParserTest.cpp
#include "core/manga/mangaupdates/MangaUpdatesStatusParser.h"

#include <gtest/gtest.h>

using namespace tankoban::manga::mangaupdates;

TEST(MangaUpdatesStatusParserTest, OnePieceOngoing)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("114 Volumes (Ongoing)")), 114);
}

TEST(MangaUpdatesStatusParserTest, BerserkOngoing)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("43 Volumes (Ongoing)")), 43);
}

TEST(MangaUpdatesStatusParserTest, KingdomOngoing)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("79 Volumes (Ongoing)")), 79);
}

TEST(MangaUpdatesStatusParserTest, DeathNoteCompleteWithBunkobanIgnoresVariants)
{
    // Real MangaUpdates Death Note status string. Parser must return 12,
    // the leading tankobon count, NOT 7 (Bunkoban) or 1 (Bunkoban single).
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("12 Volumes + 1 Extra Volume (Complete); 7 Bunkoban Volumes (Complete); 1 Bunkoban Volume (Complete)")),
        12);
}

TEST(MangaUpdatesStatusParserTest, EmptyStringReturnsZero)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(QString()), 0);
}

TEST(MangaUpdatesStatusParserTest, NoVolumesTokenReturnsZero)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("Hiatus")), 0);
}

TEST(MangaUpdatesStatusParserTest, SingleVolumeMatches)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("1 Volume (Complete)")), 1);
}
```

- [ ] **Step 2: Run the test to verify it fails (file does not exist)**

Run from repo root:

```bash
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
```

Expected: BUILD FAILED with "fatal error: 'core/manga/mangaupdates/MangaUpdatesStatusParser.h' not found".

- [ ] **Step 3: Create the parser header**

Create `src/core/manga/mangaupdates/MangaUpdatesStatusParser.h`:

```cpp
// src/core/manga/mangaupdates/MangaUpdatesStatusParser.h
#pragma once

#include <QString>

namespace tankoban::manga::mangaupdates {

// Pure-function helper. Parses the leading volume count out of a
// MangaUpdates `status` field. The status field is human-readable text;
// canonical shapes (Codex audit 2026-05-13):
//   "114 Volumes (Ongoing)"                             -> 114
//   "43 Volumes (Ongoing)"                              -> 43
//   "79 Volumes (Ongoing)"                              -> 79
//   "12 Volumes + 1 Extra Volume (Complete); 7 Bunkoban -> 12 (LEADING only)
//    Volumes (Complete); 1 Bunkoban Volume (Complete)"
//   "1 Volume (Complete)"                               -> 1
//   "Hiatus"                                            -> 0 (no count)
//   ""                                                  -> 0
//
// Singular vs plural: matches "(\d+)\s+Volumes?" anchored at the first
// hit (case-insensitive). Anything else (Bunkoban, Extra Volume) is
// downstream of the first match and ignored.
//
// Pure: no I/O, no Qt UI, no logging. Thread-safe by construction.
class MangaUpdatesStatusParser
{
public:
    // Returns the parsed leading volume count, or 0 when no count is
    // present in the input (treat as "unknown" downstream).
    static int parseLeadingVolumeCount(const QString& status);
};

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 4: Create the parser implementation**

Create `src/core/manga/mangaupdates/MangaUpdatesStatusParser.cpp`:

```cpp
// src/core/manga/mangaupdates/MangaUpdatesStatusParser.cpp
#include "MangaUpdatesStatusParser.h"

#include <QRegularExpression>

namespace tankoban::manga::mangaupdates {

int MangaUpdatesStatusParser::parseLeadingVolumeCount(const QString& status)
{
    if (status.isEmpty()) return 0;

    // Anchored at the first "(\d+) Volume[s]" hit. Case-insensitive.
    // Tightened on the "Volume" word boundary (singular OR plural) to
    // avoid false hits in arbitrary downstream prose.
    static const QRegularExpression re(
        QStringLiteral("(\\d+)\\s+Volume[s]?\\b"),
        QRegularExpression::CaseInsensitiveOption);

    const auto m = re.match(status);
    if (!m.hasMatch()) return 0;

    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    return ok ? n : 0;
}

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 5: Register the new files in CMakeLists**

Open `CMakeLists.txt` (or the sub-CMakeLists that owns `src/core/manga/` -- find it via `grep -n "AniListVolumeMapper" CMakeLists.txt`). Append next to the existing `AniListVolumeMapper.cpp` registration:

```cmake
    src/core/manga/mangaupdates/MangaUpdatesStatusParser.cpp
```

And next to the existing `AniListVolumeMapperTest.cpp` registration in the `tankoban_tests` target source list:

```cmake
    tests/core/manga/MangaUpdatesStatusParserTest.cpp
```

- [ ] **Step 6: Build + run tests**

```bash
cmake --build out --target tankoban_tests
cd out && ctest -R MangaUpdatesStatusParserTest --output-on-failure
```

Expected: `7/7 PASS`.

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/mangaupdates/MangaUpdatesStatusParser.h \
        src/core/manga/mangaupdates/MangaUpdatesStatusParser.cpp \
        tests/core/manga/MangaUpdatesStatusParserTest.cpp \
        CMakeLists.txt
git commit -m "feat(manga): MangaUpdates status-string parser + 7-case TDD (Task 2)"
```

---

## Task 3: MangaUpdatesClient HTTP Layer

**Files:**
- Create: `src/core/manga/mangaupdates/MangaUpdatesClient.h`
- Create: `src/core/manga/mangaupdates/MangaUpdatesClient.cpp`
- Modify: `CMakeLists.txt` (register .cpp)

- [ ] **Step 1: Create the client header**

Create `src/core/manga/mangaupdates/MangaUpdatesClient.h`:

```cpp
// src/core/manga/mangaupdates/MangaUpdatesClient.h
#pragma once

#include "MangaUpdatesTypes.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::mangaupdates {

// MangaUpdates v1 client.
//   POST https://api.mangaupdates.com/v1/series/search
//   GET  https://api.mangaupdates.com/v1/series/{series_id}
//
// Unauthenticated. Throttled to 1 req/sec internally (mirrors AniListClient
// pattern; the API has no advertised rate limit but courtesy applies).
//
// Mirrors AniListClient's signal-based shape so the resolver can reuse the
// caller-paired requestId pattern.
class MangaUpdatesClient : public QObject
{
    Q_OBJECT
public:
    explicit MangaUpdatesClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaUpdatesClient() override;

    // Fire a search. Result lands on searchSucceeded/Failed with the
    // caller's `requestId`.
    void searchByTitle(const QString& query, int requestId);

    // Fire a per-series detail fetch.
    void seriesById(qint64 seriesId, int requestId);

    // PHASE 7+: promote QString reason to (FailureCode, QString) pair so
    // the resolver can distinguish transient network errors (retry-worthy)
    // from API schema errors (terminal). Phase 1 ships free-form strings
    // per the AniListClient precedent.
signals:
    void searchSucceeded(int requestId, const QList<tankoban::manga::mangaupdates::MangaUpdatesSearchHit>& hits);
    void searchFailed(int requestId, const QString& reason);

    void seriesSucceeded(int requestId, const tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo& info);
    void seriesFailed(int requestId, const QString& reason);

private slots:
    void onSearchReplyFinished();
    void onSeriesReplyFinished();

private:
    void throttleIfNeeded();

    QPointer<QNetworkAccessManager> m_nam;
    qint64 m_lastRequestMs = 0;
};

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 2: Create the client implementation**

Create `src/core/manga/mangaupdates/MangaUpdatesClient.cpp`:

```cpp
// src/core/manga/mangaupdates/MangaUpdatesClient.cpp
#include "MangaUpdatesClient.h"
#include "MangaUpdatesStatusParser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>

namespace tankoban::manga::mangaupdates {

namespace {
constexpr int kThrottleMs       = 1000;
constexpr int kSearchPerPage    = 10;
constexpr const char* kSearchUrl = "https://api.mangaupdates.com/v1/series/search";
constexpr const char* kSeriesUrlPrefix = "https://api.mangaupdates.com/v1/series/";
constexpr const char* kPropRequestId = "mu_requestId";

QStringList extractStringList(const QJsonValue& v)
{
    QStringList out;
    if (!v.isArray()) return out;
    for (const auto& item : v.toArray()) {
        if (item.isString()) out.append(item.toString());
        else if (item.isObject()) {
            // MangaUpdates wraps some lists as [{"name":"..."}]
            const auto o = item.toObject();
            if (o.contains(QStringLiteral("name"))) out.append(o.value(QStringLiteral("name")).toString());
        }
    }
    return out;
}

} // namespace

MangaUpdatesClient::MangaUpdatesClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

MangaUpdatesClient::~MangaUpdatesClient() = default;

void MangaUpdatesClient::throttleIfNeeded()
{
    // PHASE 7+: this currently blocks the calling thread (typically UI).
    // Mirrors the documented AniListClient debt; resolver is invoked from
    // signal slots so the block is bounded but not zero. Promote to
    // QTimer-driven async dispatch when the AniListClient sibling is.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_lastRequestMs;
    if (elapsed < kThrottleMs && m_lastRequestMs != 0) {
        QThread::msleep(static_cast<unsigned long>(kThrottleMs - elapsed));
    }
    m_lastRequestMs = QDateTime::currentMSecsSinceEpoch();
}

void MangaUpdatesClient::searchByTitle(const QString& query, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("no network manager"));
        return;
    }
    throttleIfNeeded();

    QJsonObject body;
    body.insert(QStringLiteral("search"), query);
    body.insert(QStringLiteral("page"), 1);
    body.insert(QStringLiteral("per_page"), kSearchPerPage);

    QNetworkRequest req(QUrl(QStringLiteral(kSearchUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Tankoban/1.0 (+tankoyomi)"));

    QNetworkReply* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty(kPropRequestId, requestId);
    connect(reply, &QNetworkReply::finished, this, &MangaUpdatesClient::onSearchReplyFinished);
}

void MangaUpdatesClient::seriesById(qint64 seriesId, int requestId)
{
    if (!m_nam) {
        emit seriesFailed(requestId, QStringLiteral("no network manager"));
        return;
    }
    throttleIfNeeded();

    const QString url = QString::fromLatin1(kSeriesUrlPrefix) + QString::number(seriesId);
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Tankoban/1.0 (+tankoyomi)"));

    QNetworkReply* reply = m_nam->get(req);
    reply->setProperty(kPropRequestId, requestId);
    connect(reply, &QNetworkReply::finished, this, &MangaUpdatesClient::onSeriesReplyFinished);
}

void MangaUpdatesClient::onSearchReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property(kPropRequestId).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError perr{};
    const auto doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit searchFailed(requestId, QStringLiteral("malformed json: %1").arg(perr.errorString()));
        return;
    }

    QList<MangaUpdatesSearchHit> hits;
    const auto results = doc.object().value(QStringLiteral("results")).toArray();
    for (const auto& v : results) {
        const auto rec = v.toObject().value(QStringLiteral("record")).toObject();
        if (rec.isEmpty()) continue;
        MangaUpdatesSearchHit h;
        h.seriesId    = static_cast<qint64>(rec.value(QStringLiteral("series_id")).toDouble());
        h.title       = rec.value(QStringLiteral("title")).toString();
        h.altTitles   = extractStringList(rec.value(QStringLiteral("associated_names")));
        h.authors     = extractStringList(rec.value(QStringLiteral("authors")));
        h.yearStarted = rec.value(QStringLiteral("year")).toString().toInt();  // year is a string per API
        h.description = rec.value(QStringLiteral("description")).toString();
        h.imageUrl    = rec.value(QStringLiteral("image")).toObject()
                          .value(QStringLiteral("url")).toObject()
                          .value(QStringLiteral("original")).toString();
        if (h.seriesId > 0) hits.append(h);
    }
    emit searchSucceeded(requestId, hits);
}

void MangaUpdatesClient::onSeriesReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property(kPropRequestId).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        emit seriesFailed(requestId, reply->errorString());
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError perr{};
    const auto doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit seriesFailed(requestId, QStringLiteral("malformed json: %1").arg(perr.errorString()));
        return;
    }
    const auto rec = doc.object();

    MangaUpdatesSeriesInfo info;
    info.seriesId       = static_cast<qint64>(rec.value(QStringLiteral("series_id")).toDouble());
    info.title          = rec.value(QStringLiteral("title")).toString();
    info.rawStatus      = rec.value(QStringLiteral("status")).toString();
    info.volumeCount    = MangaUpdatesStatusParser::parseLeadingVolumeCount(info.rawStatus);
    info.latestChapter  = rec.value(QStringLiteral("latest_chapter")).toInt();
    info.completed      = rec.value(QStringLiteral("completed")).toBool();
    info.description    = rec.value(QStringLiteral("description")).toString();
    info.imageUrl       = rec.value(QStringLiteral("image")).toObject()
                            .value(QStringLiteral("url")).toObject()
                            .value(QStringLiteral("original")).toString();
    info.lastUpdated    = QDateTime::fromString(
                            rec.value(QStringLiteral("last_updated")).toObject()
                              .value(QStringLiteral("as_rfc3339")).toString(),
                            Qt::ISODate);
    info.fetchedAtMs    = QDateTime::currentMSecsSinceEpoch();

    if (info.seriesId <= 0) {
        emit seriesFailed(requestId, QStringLiteral("missing series_id in response"));
        return;
    }
    emit seriesSucceeded(requestId, info);
}

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 3: Register in CMakeLists**

Append to the same source list that holds `AniListClient.cpp`:

```cmake
    src/core/manga/mangaupdates/MangaUpdatesClient.cpp
```

- [ ] **Step 4: Build check**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/mangaupdates/MangaUpdatesClient.h \
        src/core/manga/mangaupdates/MangaUpdatesClient.cpp \
        CMakeLists.txt
git commit -m "feat(manga): MangaUpdatesClient HTTP layer + search/detail signals (Task 3)"
```

---

## Task 4: Disambiguator (Pure Logic + TDD)

**Files:**
- Create: `src/core/manga/mangaupdates/MangaUpdatesDisambiguator.h`
- Create: `src/core/manga/mangaupdates/MangaUpdatesDisambiguator.cpp`
- Create: `tests/core/manga/MangaUpdatesDisambiguatorTest.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test first**

Create `tests/core/manga/MangaUpdatesDisambiguatorTest.cpp`:

```cpp
// tests/core/manga/MangaUpdatesDisambiguatorTest.cpp
#include "core/manga/mangaupdates/MangaUpdatesDisambiguator.h"
#include "core/manga/mangaupdates/MangaUpdatesTypes.h"
#include "core/manga/anilist/AniListTypes.h"

#include <gtest/gtest.h>

using namespace tankoban::manga::mangaupdates;
using tankoban::manga::anilist::MediaPreview;

namespace {

MangaUpdatesSearchHit hit(qint64 id, const QString& title, const QStringList& authors, int year)
{
    MangaUpdatesSearchHit h;
    h.seriesId    = id;
    h.title       = title;
    h.authors     = authors;
    h.yearStarted = year;
    return h;
}

MediaPreview anilistPreview(const QString& title, int year)
{
    MediaPreview p;
    p.title       = title;
    p.yearStarted = year;
    return p;
}

} // namespace

TEST(MangaUpdatesDisambiguatorTest, SingleExactTitleMatchWins)
{
    const QList<MangaUpdatesSearchHit> hits = {
        hit(4324727424, QStringLiteral("Kingdom"), {QStringLiteral("Hara, Yasuhisa")}, 2006),
    };
    const auto p = anilistPreview(QStringLiteral("Kingdom"), 2006);
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(hits, p,
                  QStringList{QStringLiteral("Hara, Yasuhisa")}), 4324727424);
}

TEST(MangaUpdatesDisambiguatorTest, MultiHitDisambiguatesByAuthorSurname)
{
    // Two Kingdoms in MangaUpdates -- one is Hara's, one is a different
    // 2010-era series by a different author. Disambiguator must pick the
    // Hara entry given the AniList author cross-walk.
    const QList<MangaUpdatesSearchHit> hits = {
        hit(99999, QStringLiteral("Kingdom"), {QStringLiteral("Other, Author")}, 2010),
        hit(4324727424, QStringLiteral("Kingdom"), {QStringLiteral("Hara, Yasuhisa")}, 2006),
    };
    const auto p = anilistPreview(QStringLiteral("Kingdom"), 2006);
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(hits, p,
                  QStringList{QStringLiteral("Yasuhisa Hara")}), 4324727424);
}

TEST(MangaUpdatesDisambiguatorTest, MultiHitDisambiguatesByYearWhenAuthorsTie)
{
    // Two hits with no author cross-walk; year ± 1 wins.
    const QList<MangaUpdatesSearchHit> hits = {
        hit(11111, QStringLiteral("Berserk"), {}, 2018),
        hit(51239621230, QStringLiteral("Berserk"), {}, 1989),
    };
    const auto p = anilistPreview(QStringLiteral("Berserk"), 1989);
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(hits, p, QStringList{}), 51239621230);
}

TEST(MangaUpdatesDisambiguatorTest, EmptyHitsReturnsZero)
{
    const QList<MangaUpdatesSearchHit> hits;
    const auto p = anilistPreview(QStringLiteral("Unknown"), 2020);
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(hits, p, QStringList{}), 0);
}

TEST(MangaUpdatesDisambiguatorTest, NoTitleMatchReturnsZero)
{
    // All hits have different titles -- nothing exact-matches the AniList
    // preview's primary title.
    const QList<MangaUpdatesSearchHit> hits = {
        hit(11111, QStringLiteral("Kingdom Hearts III"), {}, 2019),
        hit(22222, QStringLiteral("A Kingdom of Quartz"), {}, 2015),
    };
    const auto p = anilistPreview(QStringLiteral("Kingdom"), 2006);
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(hits, p, QStringList{}), 0);
}
```

- [ ] **Step 2: Create the disambiguator header**

Create `src/core/manga/mangaupdates/MangaUpdatesDisambiguator.h`:

```cpp
// src/core/manga/mangaupdates/MangaUpdatesDisambiguator.h
#pragma once

#include "MangaUpdatesTypes.h"
#include "core/manga/anilist/AniListTypes.h"

#include <QList>
#include <QStringList>

namespace tankoban::manga::mangaupdates {

// Pure-function helper. Given a list of MangaUpdates search hits and an
// AniList preview (with optional author list cross-walked from the
// AniList Media node), returns the best-matching MangaUpdates series_id
// or 0 when no acceptable match exists.
//
// Algorithm (each step is a filter; if a step narrows to exactly one hit,
// return its series_id):
//
//   1. Exact title compare (case-insensitive, trim). If zero matches,
//      return 0 -- we refuse to fuzzy-match because the AniList canonical
//      title is already disambiguated and a near-miss is almost certainly
//      a different series.
//   2. Author surname cross-walk. AniList author strings may be in either
//      "Surname, Given" (MangaUpdates style) or "Given Surname" form;
//      extract surname tokens from both sides and require one overlap.
//   3. Year-started ± 1 tolerance against the AniList preview.
//   4. If still multi-hit, return the lowest series_id (older = more
//      canonical on MangaUpdates).
//
// Pure: no I/O, no Qt UI, no logging. Thread-safe.
class MangaUpdatesDisambiguator
{
public:
    static qint64 bestMatch(
        const QList<MangaUpdatesSearchHit>& hits,
        const tankoban::manga::anilist::MediaPreview& anilistPreview,
        const QStringList& anilistAuthors);
};

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 3: Create the disambiguator implementation**

Create `src/core/manga/mangaupdates/MangaUpdatesDisambiguator.cpp`:

```cpp
// src/core/manga/mangaupdates/MangaUpdatesDisambiguator.cpp
#include "MangaUpdatesDisambiguator.h"

#include <QSet>
#include <algorithm>

namespace tankoban::manga::mangaupdates {

namespace {

QSet<QString> surnameTokens(const QStringList& names)
{
    // Accepts "Hara, Yasuhisa" -> {"hara"} and "Yasuhisa Hara" -> {"hara"}.
    // Surname heuristic: token before the comma if a comma exists; else
    // the last whitespace-separated token. Lowercased.
    QSet<QString> out;
    for (const QString& n : names) {
        const QString s = n.trimmed();
        if (s.isEmpty()) continue;
        QString surname;
        const int commaIdx = s.indexOf(QLatin1Char(','));
        if (commaIdx >= 0) {
            surname = s.left(commaIdx).trimmed();
        } else {
            const auto parts = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (!parts.isEmpty()) surname = parts.last();
        }
        if (!surname.isEmpty()) out.insert(surname.toLower());
    }
    return out;
}

} // namespace

qint64 MangaUpdatesDisambiguator::bestMatch(
    const QList<MangaUpdatesSearchHit>& hits,
    const tankoban::manga::anilist::MediaPreview& anilistPreview,
    const QStringList& anilistAuthors)
{
    if (hits.isEmpty()) return 0;

    // Step 1: exact title compare (case-insensitive, trimmed).
    const QString targetTitle = anilistPreview.title.trimmed().toLower();
    QList<MangaUpdatesSearchHit> titleMatches;
    for (const auto& h : hits) {
        if (h.title.trimmed().toLower() == targetTitle) titleMatches.append(h);
    }
    if (titleMatches.isEmpty()) return 0;
    if (titleMatches.size() == 1) return titleMatches.first().seriesId;

    // Step 2: author surname cross-walk.
    const QSet<QString> anilistSurnames = surnameTokens(anilistAuthors);
    QList<MangaUpdatesSearchHit> authorMatches;
    for (const auto& h : titleMatches) {
        const QSet<QString> hitSurnames = surnameTokens(h.authors);
        const auto intersection = anilistSurnames & hitSurnames;
        if (!intersection.isEmpty()) authorMatches.append(h);
    }
    QList<MangaUpdatesSearchHit> candidates =
        authorMatches.isEmpty() ? titleMatches : authorMatches;
    if (candidates.size() == 1) return candidates.first().seriesId;

    // Step 3: year ± 1 tolerance.
    if (anilistPreview.yearStarted > 0) {
        QList<MangaUpdatesSearchHit> yearMatches;
        for (const auto& h : candidates) {
            if (h.yearStarted > 0 &&
                std::abs(h.yearStarted - anilistPreview.yearStarted) <= 1) {
                yearMatches.append(h);
            }
        }
        if (!yearMatches.isEmpty()) candidates = yearMatches;
        if (candidates.size() == 1) return candidates.first().seriesId;
    }

    // Step 4: tie-break -- lowest series_id wins (older entry).
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const MangaUpdatesSearchHit& a, const MangaUpdatesSearchHit& b) {
            return a.seriesId < b.seriesId;
        });
    return candidates.first().seriesId;
}

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 4: Register in CMakeLists**

Append to the same source list as the parser:

```cmake
    src/core/manga/mangaupdates/MangaUpdatesDisambiguator.cpp
```

And in `tankoban_tests`:

```cmake
    tests/core/manga/MangaUpdatesDisambiguatorTest.cpp
```

- [ ] **Step 5: Build + run tests**

```bash
cmake --build out --target tankoban_tests
cd out && ctest -R MangaUpdatesDisambiguatorTest --output-on-failure
```

Expected: `5/5 PASS`.

- [ ] **Step 6: Commit**

```bash
git add src/core/manga/mangaupdates/MangaUpdatesDisambiguator.h \
        src/core/manga/mangaupdates/MangaUpdatesDisambiguator.cpp \
        tests/core/manga/MangaUpdatesDisambiguatorTest.cpp \
        CMakeLists.txt
git commit -m "feat(manga): MangaUpdates disambiguator + 5-case TDD (Task 4)"
```

---

## Task 5: Extend AniListCache with MangaUpdates Sidecar

**Files:**
- Modify: `src/core/manga/anilist/AniListCache.h`
- Modify: `src/core/manga/anilist/AniListCache.cpp`

Rationale: the sidecar lives alongside `series_<anilistId>.json` in the existing cache dir, keyed by `anilistId`. This avoids a new cache class while keeping the MangaUpdates blob structurally separate from the AniList POD.

- [ ] **Step 1: Add the include + sidecar methods to the header**

Open `src/core/manga/anilist/AniListCache.h`. After the existing `#include "AniListTypes.h"` line, add:

```cpp
#include "core/manga/mangaupdates/MangaUpdatesTypes.h"
```

Inside the `AniListCache` class, in the public section after the existing bookmark methods, add:

```cpp
    // MANGAUPDATES_FALLBACK -- sidecar metadata keyed by anilistId. Stored
    // at <cacheDir>/mangaupdates_<anilistId>.json. Independent of the
    // AniList MediaDetail file; either may exist without the other.
    std::optional<tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo>
        getMangaUpdatesSidecar(int anilistId) const;
    void putMangaUpdatesSidecar(int anilistId,
        const tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo& info);
```

In the private section, after the existing path helpers, add:

```cpp
    QString mangaUpdatesSidecarFilePath(int anilistId) const;
```

And add a new in-memory hot copy alongside `m_byId`:

```cpp
    QHash<int, tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo> m_mangaUpdatesByAnilistId;
```

- [ ] **Step 2: Implement the sidecar methods in the .cpp**

Open `src/core/manga/anilist/AniListCache.cpp`. Find the existing `seriesFilePath` impl and add the sidecar path helper next to it:

```cpp
QString AniListCache::mangaUpdatesSidecarFilePath(int anilistId) const
{
    return m_cacheDir + QStringLiteral("/mangaupdates_%1.json").arg(anilistId);
}
```

Add the getter:

```cpp
std::optional<tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo>
AniListCache::getMangaUpdatesSidecar(int anilistId) const
{
    QMutexLocker lk(&m_mutex);
    const auto it = m_mangaUpdatesByAnilistId.constFind(anilistId);
    if (it == m_mangaUpdatesByAnilistId.constEnd()) return std::nullopt;
    return *it;
}
```

Add the setter (persists to disk off-lock):

```cpp
void AniListCache::putMangaUpdatesSidecar(int anilistId,
    const tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo& info)
{
    {
        QMutexLocker lk(&m_mutex);
        m_mangaUpdatesByAnilistId.insert(anilistId, info);
    }
    // Persist outside the lock to keep readers unblocked.
    QJsonObject o;
    o.insert(QStringLiteral("seriesId"),      QJsonValue(static_cast<double>(info.seriesId)));
    o.insert(QStringLiteral("title"),         info.title);
    o.insert(QStringLiteral("rawStatus"),     info.rawStatus);
    o.insert(QStringLiteral("volumeCount"),   info.volumeCount);
    o.insert(QStringLiteral("latestChapter"), info.latestChapter);
    o.insert(QStringLiteral("completed"),     info.completed);
    o.insert(QStringLiteral("description"),   info.description);
    o.insert(QStringLiteral("imageUrl"),      info.imageUrl);
    o.insert(QStringLiteral("lastUpdated"),   info.lastUpdated.toString(Qt::ISODate));
    o.insert(QStringLiteral("fetchedAtMs"),   QJsonValue(static_cast<double>(info.fetchedAtMs)));

    QFile f(mangaUpdatesSidecarFilePath(anilistId));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    }
    emit cacheChanged(anilistId);
}
```

Find the existing `loadFromDisk` impl and at the END of it (after AniList series + bookmarks are loaded) append a sidecar load pass:

```cpp
    // MANGAUPDATES_FALLBACK -- load sidecar files alongside the AniList
    // series files. Filenames: mangaupdates_<anilistId>.json
    QDir cache(m_cacheDir);
    for (const QString& name : cache.entryList(QStringList{QStringLiteral("mangaupdates_*.json")}, QDir::Files)) {
        const QString num = name.mid(13, name.size() - 13 - 5);  // strip prefix + ".json"
        bool ok = false;
        const int anilistId = num.toInt(&ok);
        if (!ok) continue;
        QFile f(cache.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const auto doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) continue;
        const auto o = doc.object();
        tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo info;
        info.seriesId      = static_cast<qint64>(o.value(QStringLiteral("seriesId")).toDouble());
        info.title         = o.value(QStringLiteral("title")).toString();
        info.rawStatus     = o.value(QStringLiteral("rawStatus")).toString();
        info.volumeCount   = o.value(QStringLiteral("volumeCount")).toInt();
        info.latestChapter = o.value(QStringLiteral("latestChapter")).toInt();
        info.completed     = o.value(QStringLiteral("completed")).toBool();
        info.description   = o.value(QStringLiteral("description")).toString();
        info.imageUrl      = o.value(QStringLiteral("imageUrl")).toString();
        info.lastUpdated   = QDateTime::fromString(o.value(QStringLiteral("lastUpdated")).toString(), Qt::ISODate);
        info.fetchedAtMs   = static_cast<qint64>(o.value(QStringLiteral("fetchedAtMs")).toDouble());
        m_mangaUpdatesByAnilistId.insert(anilistId, info);
    }
```

(Adjust the `mid(13, ...)` indices if the filename prefix length changes. `"mangaupdates_"` is 13 chars; `".json"` is 5.)

- [ ] **Step 3: Build check**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/anilist/AniListCache.h \
        src/core/manga/anilist/AniListCache.cpp
git commit -m "feat(manga): AniListCache sidecar for MangaUpdates metadata (Task 5)"
```

---

## Task 6: Extend AniListVolumeMapper with Override Overload

**Files:**
- Modify: `src/core/manga/anilist/AniListVolumeMapper.h`
- Modify: `src/core/manga/anilist/AniListVolumeMapper.cpp`
- Modify: `tests/core/manga/AniListVolumeMapperTest.cpp`

- [ ] **Step 1: Add the new overload to the header**

Open `src/core/manga/anilist/AniListVolumeMapper.h`. Add a new public static method below the existing `map`:

```cpp
    // MANGAUPDATES_FALLBACK -- override variant. When the caller has more
    // canonical volume/chapter counts than detail.totalVolumes / .totalChapters
    // (typically from MangaUpdates for ongoing series where AniList returns
    // null), pass them here. The mapper substitutes the overrides for the
    // detail's own zero/null values; positive AniList totals always win
    // over overrides (the original detail wins when authoritative).
    //
    // override <= 0 means "no override" -- use the detail's value.
    static QList<VolumeRow> map(const MediaDetail& detail,
                                int overrideVolumeCount,
                                int overrideChapterCount);
```

- [ ] **Step 2: Implement the overload in the .cpp**

Open `src/core/manga/anilist/AniListVolumeMapper.cpp`. Add the overload below the existing `map` impl:

```cpp
QList<VolumeRow> AniListVolumeMapper::map(const MediaDetail& detail,
                                          int overrideVolumeCount,
                                          int overrideChapterCount)
{
    // If the detail already has positive totals, the caller's overrides
    // are ignored (AniList is authoritative when populated).
    if (detail.totalVolumes > 0 && detail.totalChapters > 0) {
        return map(detail);
    }
    if (overrideVolumeCount <= 0 && overrideChapterCount <= 0) {
        return map(detail);
    }

    MediaDetail patched = detail;
    if (patched.totalVolumes  <= 0 && overrideVolumeCount  > 0) patched.totalVolumes  = overrideVolumeCount;
    if (patched.totalChapters <= 0 && overrideChapterCount > 0) patched.totalChapters = overrideChapterCount;
    return map(patched);
}
```

- [ ] **Step 3: Add two new tests for the override path**

Append to `tests/core/manga/AniListVolumeMapperTest.cpp`:

```cpp
TEST(AniListVolumeMapperTest, OverrideHydratesNullOngoingFromMangaUpdates)
{
    // Real-world shape: AniList returns totalVolumes=0/totalChapters=0
    // for One Piece (RELEASING). MangaUpdates supplies 114 vols / 1182
    // chapters. Mapper should render 114 bound rows (and a Vol X if the
    // chapter range exceeds bound capacity).
    MediaDetail d;
    d.preview.title  = QStringLiteral("One Piece");
    d.preview.status = QStringLiteral("RELEASING");
    d.totalVolumes   = 0;
    d.totalChapters  = 0;
    d.chapters.clear();

    const auto rows = AniListVolumeMapper::map(d, 114, 1182);
    // 1182 / 114 = 10 chapters per vol, 114 bound, 1182 - 1140 = 42 in Vol X.
    ASSERT_GE(rows.size(), 114);
    EXPECT_EQ(rows[0].volumeNumber, 1);
    EXPECT_EQ(rows[113].volumeNumber, 114);
    // Ongoing + leftover -> last row is Vol X.
    EXPECT_TRUE(rows.last().isVolumeX);
}

TEST(AniListVolumeMapperTest, OverrideIgnoredWhenAniListAuthoritative)
{
    // Death Note shape: AniList already has 12/108. Override 99/99 must be
    // ignored.
    MediaDetail d;
    d.preview.title  = QStringLiteral("Death Note");
    d.preview.status = QStringLiteral("FINISHED");
    d.totalVolumes   = 12;
    d.totalChapters  = 108;
    d.chapters.clear();

    const auto rows = AniListVolumeMapper::map(d, 99, 99);
    ASSERT_EQ(rows.size(), 12);
    EXPECT_EQ(rows.last().volumeNumber, 12);
}
```

- [ ] **Step 4: Build + run tests**

```bash
cmake --build out --target tankoban_tests
cd out && ctest -R AniListVolumeMapperTest --output-on-failure
```

Expected: `9/9 PASS` (7 prior + 2 new).

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/anilist/AniListVolumeMapper.h \
        src/core/manga/anilist/AniListVolumeMapper.cpp \
        tests/core/manga/AniListVolumeMapperTest.cpp
git commit -m "feat(manga): AniListVolumeMapper override overload + 2 TDD cases (Task 6)"
```

---

## Task 7: VolumeMetadataResolver Orchestrator

**Files:**
- Create: `src/core/manga/mangaupdates/VolumeMetadataResolver.h`
- Create: `src/core/manga/mangaupdates/VolumeMetadataResolver.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the resolver header**

Create `src/core/manga/mangaupdates/VolumeMetadataResolver.h`:

```cpp
// src/core/manga/mangaupdates/VolumeMetadataResolver.h
#pragma once

#include "MangaUpdatesTypes.h"
#include "core/manga/anilist/AniListTypes.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace tankoban::manga::anilist { class AniListCache; }

namespace tankoban::manga::mangaupdates {

class MangaUpdatesClient;

// Orchestrates the MangaUpdates fallback lookup for ongoing series whose
// AniList detail returned null totals. Three-stage flow:
//
//   resolveForAnilist(anilistId, preview, authors)
//     -> if sidecar cached + fresh: emit resolved(anilistId, vols, chaps) immediately
//     -> else: MangaUpdatesClient::searchByTitle(preview.title)
//        on searchSucceeded: MangaUpdatesDisambiguator::bestMatch -> seriesId
//        on seriesId > 0: MangaUpdatesClient::seriesById
//          on seriesSucceeded: AniListCache::putMangaUpdatesSidecar + emit resolved
//        on seriesId == 0: emit unresolved("no match")
//     -> on any error along the way: emit unresolved(reason); caller
//        keeps the Vol X placeholder (no regression).
//
// Caching policy: sidecar entry considered fresh for 7 days (mangaupdates
// data updates weekly-ish). After 7 days, re-fetch on next resolve call.
class VolumeMetadataResolver : public QObject
{
    Q_OBJECT
public:
    VolumeMetadataResolver(MangaUpdatesClient* client,
                           tankoban::manga::anilist::AniListCache* cache,
                           QObject* parent = nullptr);
    ~VolumeMetadataResolver() override;

    // Fire a resolve. Idempotent for the same anilistId within the
    // freshness window. Authors is the cross-walked list from the AniList
    // Media node (caller extracts; resolver does not re-fetch AniList).
    void resolveForAnilist(int anilistId,
                           const tankoban::manga::anilist::MediaPreview& preview,
                           const QStringList& anilistAuthors);

signals:
    void resolved(int anilistId, int volumeCount, int chapterCount);
    void unresolved(int anilistId, const QString& reason);

private slots:
    void onSearchSucceeded(int requestId, const QList<MangaUpdatesSearchHit>& hits);
    void onSearchFailed(int requestId, const QString& reason);
    void onSeriesSucceeded(int requestId, const MangaUpdatesSeriesInfo& info);
    void onSeriesFailed(int requestId, const QString& reason);

private:
    struct PendingResolve {
        int                                       anilistId  = 0;
        tankoban::manga::anilist::MediaPreview    preview;
        QStringList                               anilistAuthors;
    };

    int nextRequestId();
    bool isSidecarFresh(const MangaUpdatesSeriesInfo& info) const;

    QPointer<MangaUpdatesClient>                          m_client;
    QPointer<tankoban::manga::anilist::AniListCache>      m_cache;
    QHash<int, PendingResolve>                            m_pending;  // requestId -> resolve state
    int                                                   m_nextRequestId = 1;
};

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 2: Create the resolver implementation**

Create `src/core/manga/mangaupdates/VolumeMetadataResolver.cpp`:

```cpp
// src/core/manga/mangaupdates/VolumeMetadataResolver.cpp
#include "VolumeMetadataResolver.h"

#include "MangaUpdatesClient.h"
#include "MangaUpdatesDisambiguator.h"
#include "core/manga/anilist/AniListCache.h"

#include <QDateTime>

namespace tankoban::manga::mangaupdates {

namespace {
constexpr qint64 kSidecarMaxAgeMs = 7LL * 24 * 60 * 60 * 1000; // 7 days
}

VolumeMetadataResolver::VolumeMetadataResolver(MangaUpdatesClient* client,
                                                tankoban::manga::anilist::AniListCache* cache,
                                                QObject* parent)
    : QObject(parent), m_client(client), m_cache(cache)
{
    if (m_client) {
        connect(m_client.data(), &MangaUpdatesClient::searchSucceeded,
                this, &VolumeMetadataResolver::onSearchSucceeded);
        connect(m_client.data(), &MangaUpdatesClient::searchFailed,
                this, &VolumeMetadataResolver::onSearchFailed);
        connect(m_client.data(), &MangaUpdatesClient::seriesSucceeded,
                this, &VolumeMetadataResolver::onSeriesSucceeded);
        connect(m_client.data(), &MangaUpdatesClient::seriesFailed,
                this, &VolumeMetadataResolver::onSeriesFailed);
    }
}

VolumeMetadataResolver::~VolumeMetadataResolver() = default;

int VolumeMetadataResolver::nextRequestId()
{
    return m_nextRequestId++;
}

bool VolumeMetadataResolver::isSidecarFresh(const MangaUpdatesSeriesInfo& info) const
{
    if (info.fetchedAtMs <= 0) return false;
    return (QDateTime::currentMSecsSinceEpoch() - info.fetchedAtMs) < kSidecarMaxAgeMs;
}

void VolumeMetadataResolver::resolveForAnilist(int anilistId,
                                                const tankoban::manga::anilist::MediaPreview& preview,
                                                const QStringList& anilistAuthors)
{
    if (anilistId <= 0) { emit unresolved(anilistId, QStringLiteral("invalid anilistId")); return; }
    if (!m_client || !m_cache) { emit unresolved(anilistId, QStringLiteral("resolver not wired")); return; }

    // Cache hit + fresh -> emit immediately.
    const auto cached = m_cache->getMangaUpdatesSidecar(anilistId);
    if (cached.has_value() && isSidecarFresh(*cached) && cached->volumeCount > 0) {
        emit resolved(anilistId, cached->volumeCount, cached->latestChapter);
        return;
    }

    // Miss or stale -> fire search.
    PendingResolve p;
    p.anilistId      = anilistId;
    p.preview        = preview;
    p.anilistAuthors = anilistAuthors;
    const int rid    = nextRequestId();
    m_pending.insert(rid, p);
    m_client->searchByTitle(preview.title, rid);
}

void VolumeMetadataResolver::onSearchSucceeded(int requestId, const QList<MangaUpdatesSearchHit>& hits)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const PendingResolve p = *it;

    const qint64 seriesId = MangaUpdatesDisambiguator::bestMatch(hits, p.preview, p.anilistAuthors);
    if (seriesId <= 0) {
        m_pending.remove(requestId);
        emit unresolved(p.anilistId, QStringLiteral("no disambiguated match"));
        return;
    }

    // Re-use the same requestId tag for the detail call so the pending map
    // hand-off is single-keyed.
    m_client->seriesById(seriesId, requestId);
}

void VolumeMetadataResolver::onSearchFailed(int requestId, const QString& reason)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const int anilistId = it->anilistId;
    m_pending.remove(requestId);
    emit unresolved(anilistId, QStringLiteral("search failed: %1").arg(reason));
}

void VolumeMetadataResolver::onSeriesSucceeded(int requestId, const MangaUpdatesSeriesInfo& info)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const PendingResolve p = *it;
    m_pending.remove(requestId);

    if (info.volumeCount <= 0) {
        emit unresolved(p.anilistId, QStringLiteral("detail returned volumeCount<=0 (raw: %1)").arg(info.rawStatus));
        return;
    }

    m_cache->putMangaUpdatesSidecar(p.anilistId, info);
    emit resolved(p.anilistId, info.volumeCount, info.latestChapter);
}

void VolumeMetadataResolver::onSeriesFailed(int requestId, const QString& reason)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    const int anilistId = it->anilistId;
    m_pending.remove(requestId);
    emit unresolved(anilistId, QStringLiteral("detail failed: %1").arg(reason));
}

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 3: Register in CMakeLists**

```cmake
    src/core/manga/mangaupdates/VolumeMetadataResolver.cpp
```

- [ ] **Step 4: Build check**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/mangaupdates/VolumeMetadataResolver.h \
        src/core/manga/mangaupdates/VolumeMetadataResolver.cpp \
        CMakeLists.txt
git commit -m "feat(manga): VolumeMetadataResolver orchestrator (Task 7)"
```

---

## Task 8: Wire into ComicsPage

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 1: Add forward decls + member to header**

Open `src/ui/pages/ComicsPage.h`. Inside the existing `namespace tankoban::manga { ... }` block, add a new sub-namespace block:

```cpp
    namespace mangaupdates {
        class MangaUpdatesClient;
        class VolumeMetadataResolver;
    }
```

In the private member section of `ComicsPage` (alongside `m_anilistClient` / `m_anilistCache` / etc.), add:

```cpp
    // MANGAUPDATES_FALLBACK 2026-05-16 -- fires when AniList returns null
    // totals on a RELEASING/HIATUS series. On resolved(), the series view
    // re-renders with override-aware mapper to get real Vol 1..N rows.
    tankoban::manga::mangaupdates::MangaUpdatesClient*       m_mangaUpdatesClient    = nullptr;
    tankoban::manga::mangaupdates::VolumeMetadataResolver*   m_volumeResolver        = nullptr;
```

Add the slot to the `private slots:` section:

```cpp
    // MANGAUPDATES_FALLBACK 2026-05-16
    void onVolumeMetadataResolved(int anilistId, int volumeCount, int chapterCount);
    void onVolumeMetadataUnresolved(int anilistId, const QString& reason);
```

- [ ] **Step 2: Construct + wire in the .cpp**

Open `src/ui/pages/ComicsPage.cpp`. Find the constructor (search for `ComicsPage::ComicsPage`). After the existing `m_anilistClient` construction, add:

```cpp
    // MANGAUPDATES_FALLBACK 2026-05-16 -- reuse the AniList NAM so the
    // throttle / proxy / TLS settings stay aligned.
    m_mangaUpdatesClient = new tankoban::manga::mangaupdates::MangaUpdatesClient(
        m_anilistClient ? m_anilistClient->networkManager() : nullptr, this);
    m_volumeResolver = new tankoban::manga::mangaupdates::VolumeMetadataResolver(
        m_mangaUpdatesClient, m_anilistCache, this);
    connect(m_volumeResolver, &tankoban::manga::mangaupdates::VolumeMetadataResolver::resolved,
            this, &ComicsPage::onVolumeMetadataResolved);
    connect(m_volumeResolver, &tankoban::manga::mangaupdates::VolumeMetadataResolver::unresolved,
            this, &ComicsPage::onVolumeMetadataUnresolved);
```

Find the existing slot where `AniListClient::seriesSucceeded` lands (search for `seriesSucceeded` connect or the slot body). Inside that slot, AFTER the existing cache.put + series-view render call, add the fallback trigger:

```cpp
    // MANGAUPDATES_FALLBACK 2026-05-16 -- fire when AniList returned null
    // for a RELEASING/HIATUS series. Authors cross-walk: AniList preview
    // does not carry the authors list directly (the Media node does, but
    // we only persist alternateTitles in MediaPreview). For v1 we pass an
    // empty authors list; disambiguator falls back to year + title match.
    // PHASE 2+: persist Media.staff (authors) into MediaPreview so the
    // cross-walk has real signal.
    const bool isOngoing = detail.preview.status == QStringLiteral("RELEASING")
                        || detail.preview.status == QStringLiteral("HIATUS");
    const bool nullTotals = detail.totalVolumes <= 0 || detail.totalChapters <= 0;
    if (isOngoing && nullTotals && m_volumeResolver) {
        m_volumeResolver->resolveForAnilist(detail.preview.anilistId, detail.preview, QStringList{});
    }
```

Add the new slot bodies near the bottom of `ComicsPage.cpp`:

```cpp
void ComicsPage::onVolumeMetadataResolved(int anilistId, int volumeCount, int chapterCount)
{
    // Look up the cached MediaDetail by anilistId (it landed in the cache
    // moments ago via the AniListClient seriesSucceeded path). Re-render
    // the currently-visible series view if it is showing this anilistId.
    if (!m_anilistCache) return;
    const auto detail = m_anilistCache->get(anilistId);
    if (!detail.has_value()) return;
    if (!m_tyVolumeSeriesView) return;

    // m_tyVolumeSeriesView only re-renders if the active anilistId matches
    // the resolved id -- prevents flicker when the user navigates away
    // mid-resolve.
    if (m_tyVolumeSeriesView->currentAnilistId() == anilistId) {
        const auto rows = tankoban::manga::anilist::AniListVolumeMapper::map(
            *detail, volumeCount, chapterCount);
        m_tyVolumeSeriesView->setVolumeRows(rows);
    }
}

void ComicsPage::onVolumeMetadataUnresolved(int anilistId, const QString& reason)
{
    // No-op for the UI -- the series view already shows the Vol X
    // placeholder from the AniListVolumeMapper fallback path. Log the
    // reason for the ring buffer so dev-control logs surface it.
    qDebug().noquote() << QStringLiteral("[mangaupdates] anilist %1 unresolved: %2")
                              .arg(anilistId).arg(reason);
}
```

Add includes at the top of `ComicsPage.cpp` (alongside the existing AniList includes):

```cpp
#include "core/manga/mangaupdates/MangaUpdatesClient.h"
#include "core/manga/mangaupdates/VolumeMetadataResolver.h"
#include "core/manga/anilist/AniListVolumeMapper.h"
```

Note: this assumes `m_tyVolumeSeriesView->currentAnilistId()` and `setVolumeRows(QList<VolumeRow>)` exist. If they do not (grep `ComicsSeriesView.h` to confirm), add the trivial accessor + setter as part of this task -- adjust step accordingly.

- [ ] **Step 3: Verify ComicsSeriesView accessors exist or add them**

Run: `grep -n "currentAnilistId\\|setVolumeRows" src/ui/pages/comics/ComicsSeriesView.h`

If both exist, skip to Step 4. If either is missing, add to `ComicsSeriesView.h`:

```cpp
    int  currentAnilistId() const { return m_currentAnilistId; }
    void setVolumeRows(const QList<tankoban::manga::anilist::VolumeRow>& rows);
```

(`setVolumeRows` should already exist or be trivially implementable by replacing the row-population loop; if implementing fresh, the impl mirrors the existing per-row construction in the open-series path.)

- [ ] **Step 4: Build check**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/ComicsPage.h \
        src/ui/pages/ComicsPage.cpp \
        src/ui/pages/comics/ComicsSeriesView.h \
        src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "feat(comics): wire MangaUpdates resolver into ComicsPage detail fetch (Task 8)"
```

---

## Task 9: End-to-End Smoke + Cache Verification

This is the integration check Hemanth (or an MCP-driving agent) runs once the code is in.

- [ ] **Step 1: Launch the app**

```
build_and_run.bat
```

Wait for the Tankoban window. The app is built with `--dev-control` automatically so `tankoctl.exe` works for state verification.

- [ ] **Step 2: Navigate to Comics + search Kingdom**

In the app:
1. Click the Comics tab.
2. Type "Kingdom" in the search bar + press Enter.
3. Click the Kingdom (Yasuhisa Hara) result tile to open the series view.

- [ ] **Step 3: Verify 79 volume rows render**

Wait ~3 seconds for the MangaUpdates fetch + parse to complete. The Stremio-style series view should now render Vol 1..79 (and a Vol X tail if chapter count exceeds bound capacity), NOT the single Vol X placeholder.

Visual checks:
- Table contains rows numbered 1..79 (scroll to verify).
- Series header still shows AniList banner + cover (no regression on AniList-driven UI).
- Volume row chapter ranges look sensible (chapter 1 to ~10 for Vol 1, etc.).

- [ ] **Step 4: Verify the cache sidecar landed on disk**

Open PowerShell. Run:

```
ls $env:LOCALAPPDATA\Tankoban\anilist_cache\mangaupdates_*.json
```

Expected: a file named `mangaupdates_46765.json` (Kingdom's anilistId). Optional sanity:

```
cat $env:LOCALAPPDATA\Tankoban\anilist_cache\mangaupdates_46765.json
```

Should show JSON with `"volumeCount": 79` and `"rawStatus": "79 Volumes (Ongoing)"`.

- [ ] **Step 5: Verify cache hit on second open (no second network fetch)**

Close the Kingdom series view (back arrow). Re-open Kingdom from the search result. The Vol 1..79 rendering should be near-instant (sidecar cache fresh-hit, no network round-trip).

Optional dev-control bridge check (separate PowerShell window while app is running):

```
out\tankoctl.exe logs 50
```

Expected: log lines containing `[mangaupdates] anilist 46765 unresolved` should be ABSENT for the second open; the first open's log line is fine.

- [ ] **Step 6: Smoke a second series to prove generalization**

Repeat steps 2-5 for Berserk (anilistId 30002, expected 43 vols) and One Piece (anilistId 30013, expected 114 vols). Disk should show three sidecar files at the end.

- [ ] **Step 7: Smoke cleanup per Rule 17**

```
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

Expected: Tankoban.exe + ffmpeg_sidecar.exe both killed. (ffmpeg_sidecar may not have been spawned in this smoke -- script tolerates that.)

- [ ] **Step 8: Commit cache fixtures if any**

(No cache files should be committed -- they live in `%LOCALAPPDATA%` not the repo. This step exists only to confirm `git status` is clean of unintended additions.)

```
git status --short
```

Expected: only the previously-committed src files; no stray .json or out/ additions.

---

## Task 10: RTC + Memory Save + Plan Archive

- [ ] **Step 1: Append RTC line to chat.md per contracts-v3**

Append one line to `agents/chat.md` matching the brotherhood format. Template (fill `<...>` placeholders):

```
READY TO COMMIT - [Agent 1, TANKOYOMI_MANGAUPDATES_FALLBACK 2026-05-16 ~<HH:MM> — All 8 implementation tasks shipped (~<LOC> across <N> files): MangaUpdates POD types + status-string parser (7 TDD cases) + HTTP client + disambiguator (5 TDD cases) + AniListCache sidecar + AniListVolumeMapper override overload (2 new TDD cases, total 9) + VolumeMetadataResolver orchestrator + ComicsPage wiring. End-to-end smoke verified Kingdom 79/79 + Berserk 43/43 + One Piece 114/114 rendering as real Vol 1..N rows; cache sidecar persists to <appData>/anilist_cache/mangaupdates_<anilistId>.json; second-open cache-hit is instantaneous. AniList stays authoritative for FINISHED series (override path is no-op when totalVolumes>0). Network-boundary trust: all parsed fields validated; malformed/empty status emits unresolved and preserves Vol X placeholder (no regression). Smoke cleanup per Rule 17: Tankoban + ffmpeg_sidecar both killed. Plan file at docs/superpowers/plans/2026-05-16-tankoyomi-mangaupdates-fallback.md.] | Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans, /superpowers:test-driven-development, /superpowers:verification-before-completion, /build-verify, /simplify, /superpowers:requesting-code-review] | files: src/core/manga/mangaupdates/MangaUpdatesTypes.h, src/core/manga/mangaupdates/MangaUpdatesStatusParser.h, src/core/manga/mangaupdates/MangaUpdatesStatusParser.cpp, src/core/manga/mangaupdates/MangaUpdatesClient.h, src/core/manga/mangaupdates/MangaUpdatesClient.cpp, src/core/manga/mangaupdates/MangaUpdatesDisambiguator.h, src/core/manga/mangaupdates/MangaUpdatesDisambiguator.cpp, src/core/manga/mangaupdates/VolumeMetadataResolver.h, src/core/manga/mangaupdates/VolumeMetadataResolver.cpp, src/core/manga/anilist/AniListCache.h, src/core/manga/anilist/AniListCache.cpp, src/core/manga/anilist/AniListVolumeMapper.h, src/core/manga/anilist/AniListVolumeMapper.cpp, tests/core/manga/MangaUpdatesStatusParserTest.cpp, tests/core/manga/MangaUpdatesDisambiguatorTest.cpp, tests/core/manga/AniListVolumeMapperTest.cpp, src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, CMakeLists.txt
```

- [ ] **Step 2: Save the arc-close memory**

Create or update `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\project_mangaupdates_fallback_shipped_2026-05-16.md` with arc-state summary (one-line description for MEMORY.md, body covering: what shipped, the 7-day sidecar freshness window, the fallback chain on resolver failure preserving Vol X, the PHASE 2+ TODO to persist authors into MediaPreview for stronger disambiguation).

- [ ] **Step 3: Archive the plan**

After Hemanth's `/commit-sweep` lands the bundle, optionally archive this plan to `docs/superpowers/plans/_archive/`:

```bash
git mv docs/superpowers/plans/2026-05-16-tankoyomi-mangaupdates-fallback.md docs/superpowers/plans/_archive/
git commit -m "chore: archive MANGAUPDATES_FALLBACK plan after ship"
```

(Skip if other agents reference the plan from open work.)

---

## Self-Review

1. **Spec coverage:** All 5 sources of the audit's Top-3-by-ease-of-integration are addressed: (a) MangaUpdates HTTP client wraps both endpoints, (b) status-string parser extracts the leading volume count per Codex's audit observation, (c) disambiguator handles the multi-hit case Codex flagged as the medium risk, (d) AniList stays authoritative when populated (no regression), (e) Vol X placeholder preserved on any failure mode (no UI regression).

2. **Placeholder scan:** No "TBD" / "implement later" / "Similar to Task N" patterns. Every code block contains real code. Every cmake registration shows the literal path string. Every test case shows the literal `EXPECT_EQ` and the expected number.

3. **Type consistency:** `MangaUpdatesSeriesInfo` shape is defined once in Task 1 and consumed verbatim in Tasks 3 / 5 / 7. `parseLeadingVolumeCount` signature matches across Tasks 2 / 3 / definitions. `bestMatch(QList<...>, MediaPreview, QStringList)` signature matches across Tasks 4 / 7. `resolveForAnilist(int, MediaPreview, QStringList)` signature matches Task 7 def vs Task 8 call. `getMangaUpdatesSidecar` / `putMangaUpdatesSidecar` names match across Tasks 5 / 7.

4. **Brotherhood compliance:** ASCII only. No worktrees. `build_check.bat` after every src/ touch. Rule 17 cleanup in Task 9 Step 7. RTC format per contracts-v3 with `Skills invoked:` field. TDD scoped to the two pure-logic primitives (parser + disambiguator) and the mapper extension; HTTP client + resolver + ComicsPage wiring smoke-verify end-to-end. No new vcpkg deps.

5. **Risk surface:** Network-boundary trust validated -- malformed JSON, missing fields, parse failures all emit unresolved + preserve Vol X. Rate limit at 1 req/sec mirrors AniList sibling. Single-instance cache lock via existing AniListCache QMutex. No new threads spawned.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-16-tankoyomi-mangaupdates-fallback.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Best fit here because each task is cleanly bounded and the build_check + ctest gates catch issues early. Estimated ~8 subagent dispatches (Tasks 1-8); Tasks 9-10 are Hemanth-driven (smoke + RTC).

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints. Riskier because the context budget for this session is already heavy; could exhaust before Task 8.

**Alternative path: Codex Trigger D dispatch** — given the architecture is locked, the API surface is bounded, and Codex has been hot on this arc (the audit + the pre-smoke fix shipped 6 fix-units cleanly), this plan is a strong Trigger D candidate. Codex executes the whole 1-8 arc, brotherhood Claude (you) handles Task 9 MCP smoke + Task 10 RTC.

**Which approach, brother?**
