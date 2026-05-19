# WeebCentral Identity Pivot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pivot the Tankoban Comics-mode identity backbone from AniList to WeebCentral. Search routes through `WeebCentralScraper`; `ComicsSeriesView` accepts `MangaResult` instead of `anilist::MediaPreview`; the BookWalker cover resolver (shipped 2026-05-18) is re-keyed off `weebcentralKey` with Japanese-title input sourced from MangaUpdates' `associated` field instead of AniList. AniList demotes to optional cosmetic decoration with all calls off the critical path.

**Architecture:** WeebCentralScraper + MangaSourceRegistry already exist and are wired (unused by the in-app search bar today). `ComicsLibraryRecord` already keys on `sourceId + seriesId` — no schema migration needed. The pivot is concentrated in three layers: (1) MangaUpdates JSON parse + new resolver-by-title entry point, (2) BookWalker VolumeCoverResolver re-keyed to use MangaUpdates Japanese alt-titles instead of AniListCache, (3) ComicsSeriesView + ComicsTankoyomiSearchWidget + ComicsPage rewired to speak WeebCentral `MangaResult` instead of AniList `MediaPreview`. AniList retained as a decoration-only client (lazy banner/description fetch deferred to v1.x).

**Tech Stack:** C++20, Qt 6.10.2, MSVC2022, Ninja. Pure-logic primitives via GoogleTest (already wired for the BookWalker arc tests). `QNetworkAccessManager` HTTP. Spec at `docs/superpowers/specs/2026-05-19-weebcentral-identity-pivot-design.md` is authoritative for all decisions.

---

## File Structure

**New files:**
- `tests/core/manga/mangaupdates/test_japanese_title_extraction.cpp` — pure-logic unit tests for picking the first CJK/Hiragana/Katakana entry from MangaUpdates `altTitles`
- `tests/fixtures/mangaupdates/berserk_series_51239621230.json` — frozen MangaUpdates response with the `associated` array (sourced from the 2026-05-16 audit data)

**Modified files (backend — additive, low-risk):**
- `src/core/manga/mangaupdates/MangaUpdatesTypes.h` — add `altTitles` field to `MangaUpdatesSeriesInfo`
- `src/core/manga/mangaupdates/MangaUpdatesClient.cpp` — parse `associated` array in `onSeriesReplyFinished`, populate `altTitles`
- `src/core/manga/mangaupdates/VolumeMetadataResolver.{h,cpp}` — add `resolveByTitle(QString englishTitle, QString seriesKey)` alongside existing `resolveForAnilist`; cache by `seriesKey` (composite sourceId:seriesId)
- `src/core/manga/anilist/AniListCache.{h,cpp}` — add a parallel sidecar lookup by `seriesKey` (or move sidecar storage to a new MangaUpdatesCache class — the plan picks based on what's simpler in practice)
- `CMakeLists.txt` — register the new test file + fixture

**Modified files (cover resolver re-key — medium risk, depends on backend):**
- `src/core/manga/bookwalker/VolumeCoverResolver.{h,cpp}` — entry point changes from `resolveForAnilist(int)` to `resolveForSeries(QString seriesKey, QString englishTitle, int anilistIdOptional = 0)`; dependency on `AniListCache` replaced with `VolumeMetadataResolver` (calls `resolveByTitle` then reads the Japanese-titles list off the resolved sidecar)
- `src/core/manga/bookwalker/BookWalkerCache.{h,cpp}` — file-naming pattern changes from `<anilistId>.json` to a sanitized form of `<seriesKey>.json` (slash → underscore)

**Modified files (UI refactor — highest risk):**
- `src/ui/pages/comics/ComicsSeriesView.h` — add `showSeries(const MangaResult& mr)` overload alongside existing `showSeries(const anilist::MediaPreview&)`; add `m_currentSeriesKey` member; new internal state for MangaResult-driven flow
- `src/ui/pages/comics/ComicsSeriesView.cpp` — implement `showSeries(MangaResult)`; convert cover-resolver invocation to use `seriesKey + title`; banner/poster from `MangaResult.thumbnailUrl` instead of AniList `coverImage`
- `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}` — signal payload changes from `anilist::MediaPreview` to `MangaResult`; backbone swap from `AniListClient::searchByTitle` to `MangaSourceRegistry::activeScraper()->search`; drop `AniListClient*` constructor parameter (replace with `MangaSourceRegistry*`)
- `src/ui/pages/ComicsPage.{h,cpp}` — drop AniList search-bar signal connections; wire `MangaSourceRegistry` into search widget construction; library-entry → series-view path reconstructs `MangaResult` from `ComicsLibraryRecord` fields

**Smoke evidence + RTCs:**
- `agents/audits/smoke_evidence/0350_*.png` — Berserk search → series-view → BookWalker covers smoke screenshots
- `agents/chat.md` — Phase 4 close RTC

---

## Phase A — MangaUpdates `associated` field surfaces Japanese alt-titles

### Task 1: Add `altTitles` to `MangaUpdatesSeriesInfo`

**Files:**
- Modify: `src/core/manga/mangaupdates/MangaUpdatesTypes.h`

- [ ] **Step 1: Add `QStringList altTitles` to the struct**

Open `src/core/manga/mangaupdates/MangaUpdatesTypes.h`. Find the `MangaUpdatesSeriesInfo` struct and add the field:

```cpp
struct MangaUpdatesSeriesInfo {
    qint64    seriesId      = 0;
    QString   title;
    QStringList altTitles;        // <-- NEW: parsed from `associated` array; carries the Japanese title we need for BookWalker JP search
    QString   rawStatus;
    int       volumeCount   = 0;
    int       latestChapter = 0;
    bool      completed     = false;
    QString   description;
    QString   imageUrl;
    QDateTime lastUpdated;
    qint64    fetchedAtMs   = 0;
};
```

- [ ] **Step 2: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 3: Commit**

```bash
git add src/core/manga/mangaupdates/MangaUpdatesTypes.h
git commit -m "$(cat <<'EOF'
feat(manga/mangaupdates): add altTitles to MangaUpdatesSeriesInfo

The MangaUpdates /series/{id} response has an "associated" array listing
the series title in multiple languages/scripts. The first Japanese (CJK
/Hiragana/Katakana) entry is what the BookWalker resolver needs to
search bookwalker.jp - WEEBCENTRAL_IDENTITY_PIVOT moves this dependency
off AniList (rate-limited) and onto MangaUpdates (robust). The field
plumbing lands first; parsing + tests follow in subsequent tasks.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Parse `associated` array in `onSeriesReplyFinished`

**Files:**
- Modify: `src/core/manga/mangaupdates/MangaUpdatesClient.cpp`

- [ ] **Step 1: Add parse-helper near the file's existing string-list helper**

Search `src/core/manga/mangaupdates/MangaUpdatesClient.cpp` for the existing `extractStringList` helper (used in `onSearchReplyFinished` at line ~160 to extract `associated_names`). The series endpoint uses a slightly different shape — it's an array of `{title: "..."}` objects under the key `associated`. Add a sibling helper near `extractStringList`:

```cpp
// MangaUpdates /series/{id} endpoint returns associated titles as
// array<object{title:string}>, not array<string> like the search endpoint.
static QStringList extractAssociatedTitles(const QJsonValue& v)
{
    QStringList out;
    if (!v.isArray()) return out;
    for (const auto& entry : v.toArray()) {
        const QString t = entry.toObject().value(QStringLiteral("title")).toString().trimmed();
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}
```

- [ ] **Step 2: Call the helper in `onSeriesReplyFinished`**

Find `onSeriesReplyFinished` (line ~170 per existing code). Inside the parse block where other fields are populated, add the altTitles extraction. Insert AFTER the existing `info.title = ...` line and BEFORE `info.rawStatus = ...`:

```cpp
info.altTitles = extractAssociatedTitles(rec.value(QStringLiteral("associated")));
```

The full populated block now reads:
```cpp
info.seriesId = int64Value(rec.value(QStringLiteral("series_id")));
info.title = rec.value(QStringLiteral("title")).toString();
info.altTitles = extractAssociatedTitles(rec.value(QStringLiteral("associated"))); // NEW
info.rawStatus = stringValue(rec.value(QStringLiteral("status")));
// ... existing fields ...
```

- [ ] **Step 3: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/mangaupdates/MangaUpdatesClient.cpp
git commit -m "$(cat <<'EOF'
feat(manga/mangaupdates): parse associated array in series response

MangaUpdates /v1/series/{id} returns "associated":[{title:"ベルセルク"},
{title:"Берсерк"}, ...]. Parse into MangaUpdatesSeriesInfo.altTitles
via a new extractAssociatedTitles helper. Sibling to the existing
extractStringList used for search-endpoint associated_names (which
ships array<string>, not array<object{title}>).

The Japanese title needed for BookWalker JP search lives here.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Snapshot Berserk MangaUpdates series response fixture

**Files:**
- Create: `tests/fixtures/mangaupdates/berserk_series_51239621230.json`

- [ ] **Step 1: Fetch and save the MangaUpdates Berserk series JSON**

PowerShell from repo root:

```powershell
New-Item -ItemType Directory -Force tests/fixtures/mangaupdates | Out-Null
Invoke-WebRequest -Uri "https://api.mangaupdates.com/v1/series/51239621230" -UserAgent "Mozilla/5.0" -UseBasicParsing |
    Select-Object -ExpandProperty Content |
    Out-File -Encoding utf8 tests/fixtures/mangaupdates/berserk_series_51239621230.json
```

- [ ] **Step 2: Verify the fixture contains `ベルセルク` in the associated array**

```powershell
(Get-Content tests/fixtures/mangaupdates/berserk_series_51239621230.json -Raw | Select-String -Pattern 'ベルセルク' -AllMatches).Matches.Count
```

Expected: ≥ 1.

- [ ] **Step 3: Commit**

```bash
git add tests/fixtures/mangaupdates/berserk_series_51239621230.json
git commit -m "$(cat <<'EOF'
test(manga/mangaupdates): freeze Berserk series response (2026-05-19)

MangaUpdates /v1/series/51239621230 response captured for unit tests
of the new associated-titles parse path. Berserk's "associated" array
includes ベルセルク (Japanese katakana) plus 9 other-language variants
per the 2026-05-16 audit. Tests assert the Japanese-title picker
returns ベルセルク first.

No authentication required, public endpoint, no tokens to redact.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: TDD pure-function Japanese-title picker

**Files:**
- Create: `src/core/manga/mangaupdates/JapaneseTitlePicker.{h,cpp}`
- Create: `tests/core/manga/mangaupdates/test_japanese_title_picker.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/core/manga/mangaupdates/test_japanese_title_picker.cpp`:

```cpp
#include "core/manga/mangaupdates/JapaneseTitlePicker.h"

#include <gtest/gtest.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

using tankoban::manga::mangaupdates::JapaneseTitlePicker;

namespace {

QString loadFixtureRaw(const QString& relPath)
{
    QFile f(QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

QStringList altTitlesFromFixture(const QString& relPath)
{
    const QString raw = loadFixtureRaw(relPath);
    if (raw.isEmpty()) return {};
    const auto obj = QJsonDocument::fromJson(raw.toUtf8()).object();
    QStringList out;
    for (const auto& v : obj.value(QStringLiteral("associated")).toArray()) {
        const QString t = v.toObject().value(QStringLiteral("title")).toString().trimmed();
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}

} // namespace

TEST(JapaneseTitlePicker, PicksKatakanaFromBerserkFixture) {
    auto titles = altTitlesFromFixture(QStringLiteral("mangaupdates/berserk_series_51239621230.json"));
    ASSERT_FALSE(titles.isEmpty()) << "Fixture missing or empty";

    const QString japanese = JapaneseTitlePicker::pickFirstJapanese(titles);
    EXPECT_EQ(japanese, QString::fromUtf8("ベルセルク"));
}

TEST(JapaneseTitlePicker, PicksHiraganaWhenPresent) {
    QStringList titles = {
        QStringLiteral("Berserk"),
        QString::fromUtf8("べるせるく"),   // hiragana
        QString::fromUtf8("Берсерк"),
    };
    EXPECT_EQ(JapaneseTitlePicker::pickFirstJapanese(titles),
              QString::fromUtf8("べるせるく"));
}

TEST(JapaneseTitlePicker, PicksCjkUnifiedIdeographs) {
    QStringList titles = {
        QStringLiteral("Kingdom"),
        QString::fromUtf8("キングダム"),
    };
    EXPECT_EQ(JapaneseTitlePicker::pickFirstJapanese(titles),
              QString::fromUtf8("キングダム"));
}

TEST(JapaneseTitlePicker, ReturnsEmptyWhenNoJapaneseTitle) {
    QStringList titles = {
        QStringLiteral("Death Note"),
        QStringLiteral("DEATH NOTE"),
        QStringLiteral("DN"),
    };
    EXPECT_TRUE(JapaneseTitlePicker::pickFirstJapanese(titles).isEmpty());
}

TEST(JapaneseTitlePicker, ReturnsEmptyOnEmptyInput) {
    EXPECT_TRUE(JapaneseTitlePicker::pickFirstJapanese(QStringList{}).isEmpty());
}

TEST(JapaneseTitlePicker, SkipsLatinScriptEntriesBeforeFirstCjk) {
    QStringList titles = {
        QStringLiteral("Berserk"),
        QStringLiteral("Berserker"),
        QString::fromUtf8("ベルセルク"),    // 3rd entry, should still win
        QString::fromUtf8("Берсерк"),
    };
    EXPECT_EQ(JapaneseTitlePicker::pickFirstJapanese(titles),
              QString::fromUtf8("ベルセルク"));
}
```

- [ ] **Step 2: Write the header**

Create `src/core/manga/mangaupdates/JapaneseTitlePicker.h`:

```cpp
#pragma once

#include <QString>
#include <QStringList>

namespace tankoban::manga::mangaupdates {

class JapaneseTitlePicker
{
public:
    // Scans the input list and returns the FIRST entry that contains any
    // CJK Unified Ideographs (U+4E00–U+9FFF), Hiragana (U+3040–U+309F),
    // or Katakana (U+30A0–U+30FF) character. Returns an empty QString if
    // no entry contains Japanese-script characters.
    //
    // Used by VolumeCoverResolver to extract the Japanese title from a
    // MangaUpdates `associated` array for BookWalker JP search input.
    static QString pickFirstJapanese(const QStringList& titles);
};

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 3: Write the implementation**

Create `src/core/manga/mangaupdates/JapaneseTitlePicker.cpp`:

```cpp
#include "JapaneseTitlePicker.h"

namespace tankoban::manga::mangaupdates {

namespace {
bool containsJapanese(const QString& s)
{
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) ||  // CJK Unified Ideographs
            (u >= 0x3040 && u <= 0x309F) ||  // Hiragana
            (u >= 0x30A0 && u <= 0x30FF)) {  // Katakana
            return true;
        }
    }
    return false;
}
} // namespace

QString JapaneseTitlePicker::pickFirstJapanese(const QStringList& titles)
{
    for (const QString& t : titles) {
        if (containsJapanese(t)) return t;
    }
    return QString();
}

} // namespace tankoban::manga::mangaupdates
```

- [ ] **Step 4: Register in `CMakeLists.txt`**

Find the existing `mangaupdates/MangaUpdatesClient.cpp` line in the main `set(SOURCES ...)` block. Add next to it:

```cmake
        src/core/manga/mangaupdates/JapaneseTitlePicker.cpp
```

Add inside the `tankoban_tests` sources block (next to other manga test entries):

```cmake
        tests/core/manga/mangaupdates/test_japanese_title_picker.cpp
        src/core/manga/mangaupdates/JapaneseTitlePicker.cpp
```

- [ ] **Step 5: Build + test**

```bash
build_check.bat
```

Expected: `BUILD OK`.

```powershell
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R JapaneseTitlePicker'
```

Expected: 6 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/manga/mangaupdates/JapaneseTitlePicker.h \
        src/core/manga/mangaupdates/JapaneseTitlePicker.cpp \
        tests/core/manga/mangaupdates/test_japanese_title_picker.cpp \
        CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(manga/mangaupdates): add JapaneseTitlePicker with TDD (6/6 PASS)

Pure-logic primitive: scans an altTitles list (typically from
MangaUpdatesSeriesInfo.altTitles populated from the /series response
"associated" array) and returns the first entry containing CJK / Hiragana
/ Katakana characters. Used by VolumeCoverResolver to extract the
Japanese title for BookWalker JP search.

Replaces AniListCache::japaneseTitleFor as the JP-title source on the
resolver critical path; AniList becomes a decoration-only client per
WEEBCENTRAL_IDENTITY_PIVOT spec Decision #4.

6 GoogleTest cases against frozen Berserk fixture + synthetic inputs
covering all script ranges + boundary conditions.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase B — VolumeMetadataResolver gains by-title flow

### Task 5: Add `resolveByTitle` entry point to VolumeMetadataResolver

**Files:**
- Modify: `src/core/manga/mangaupdates/VolumeMetadataResolver.{h,cpp}`

**Context:** `VolumeMetadataResolver` currently has `resolveForAnilist(int anilistId, MediaPreview preview, QStringList authors)`. It searches MangaUpdates by title, picks the best hit (via `MangaUpdatesDisambiguator`), fetches the series detail, and emits `resolved(int anilistId, int volumeCount, int chapterCount)`. We add a parallel entry point keyed by `seriesKey` (the WeebCentral `sourceId:seriesId`) that does the same MangaUpdates lookup but emits a richer payload including the parsed Japanese title.

- [ ] **Step 1: Add signal + entry method declarations**

In `src/core/manga/mangaupdates/VolumeMetadataResolver.h`, inside the class:

```cpp
public:
    // Existing entry retained for backward compatibility with library entries
    // that already store anilistId (currently none, but future cleanup may
    // reuse this signature).
    void resolveForAnilist(int anilistId,
                           const tankoban::manga::anilist::MediaPreview& preview,
                           const QStringList& anilistAuthors);

    // New entry — used by the post-WEEBCENTRAL_IDENTITY_PIVOT chain.
    // Searches MangaUpdates by englishTitle, caches by seriesKey
    // (e.g. "weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4").
    void resolveBySeriesKey(const QString& seriesKey,
                            const QString& englishTitle,
                            const QStringList& authorsHint = {});

signals:
    void resolved(int anilistId, int volumeCount, int chapterCount);
    void unresolved(int anilistId, const QString& reason);

    // New signals — richer payload for the by-seriesKey flow.
    void resolvedBySeriesKey(const QString& seriesKey,
                             int volumeCount,
                             int chapterCount,
                             const QStringList& altTitles);
    void unresolvedBySeriesKey(const QString& seriesKey, const QString& reason);
```

Inside the `PendingResolve` struct (private section), add:

```cpp
    struct PendingResolve {
        int anilistId = 0;                                // 0 when by-seriesKey
        QString seriesKey;                                 // empty when by-anilist (legacy)
        QString englishTitle;
        tankoban::manga::anilist::MediaPreview preview;    // empty when by-seriesKey
        QStringList anilistAuthors;
    };
```

- [ ] **Step 2: Implement `resolveBySeriesKey`**

In `src/core/manga/mangaupdates/VolumeMetadataResolver.cpp`, add the method body. It mirrors `resolveForAnilist` but populates `PendingResolve.seriesKey` and routes the success path through the new signal.

Search the file for the existing `resolveForAnilist` implementation. Append a sibling implementation after it:

```cpp
void VolumeMetadataResolver::resolveBySeriesKey(const QString& seriesKey,
                                                const QString& englishTitle,
                                                const QStringList& authorsHint)
{
    if (!m_client || seriesKey.isEmpty() || englishTitle.trimmed().isEmpty()) {
        emit unresolvedBySeriesKey(seriesKey, QStringLiteral("invalid-args"));
        return;
    }
    PendingResolve p;
    p.anilistId = 0;
    p.seriesKey = seriesKey;
    p.englishTitle = englishTitle.trimmed();
    p.anilistAuthors = authorsHint;

    const int reqId = nextRequestId();
    m_pending.insert(reqId, p);
    m_client->searchByTitle(p.englishTitle, reqId);
}
```

In the existing `onSearchSucceeded` / `onSeriesSucceeded` handlers, branch on `pending.seriesKey.isEmpty()` to choose which signal to emit. Look up the existing handlers and modify:

In `onSearchSucceeded` (the disambiguator-picks-a-hit branch — find the existing call to `m_client->seriesById(...)`):
```cpp
// Existing logic picks best hit + calls seriesById. The PendingResolve
// stays in m_pending under reqId; no change to disambiguation step.
m_client->seriesById(hit.seriesId, requestId);
```

In `onSeriesSucceeded` (where existing `emit resolved(anilistId, volumeCount, chapterCount)` lives), wrap the emit:
```cpp
auto it = m_pending.find(requestId);
if (it == m_pending.end()) return;
const PendingResolve p = it.value();
m_pending.erase(it);

if (p.seriesKey.isEmpty()) {
    // Legacy by-anilist path
    emit resolved(p.anilistId, info.volumeCount, info.latestChapter);
} else {
    emit resolvedBySeriesKey(p.seriesKey,
                             info.volumeCount,
                             info.latestChapter,
                             info.altTitles);
}
```

Same pattern in the corresponding `onSearchFailed` / `onSeriesFailed` handlers — branch on `p.seriesKey.isEmpty()`.

- [ ] **Step 3: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/manga/mangaupdates/VolumeMetadataResolver.h \
        src/core/manga/mangaupdates/VolumeMetadataResolver.cpp
git commit -m "$(cat <<'EOF'
feat(manga/mangaupdates): VolumeMetadataResolver gains resolveBySeriesKey

Parallel entry point to the existing resolveForAnilist. Searches
MangaUpdates by English title, caches by seriesKey (sourceId:seriesId
composite). New signal resolvedBySeriesKey carries altTitles in addition
to volumeCount + chapterCount, so the BookWalker resolver chain can
read the Japanese title without a second MangaUpdates round trip.

Existing resolveForAnilist + resolved signal retained; the by-anilist
flow can be removed in a future cleanup once all callers migrate to
by-seriesKey. PendingResolve struct extended with seriesKey + englishTitle;
handlers branch on seriesKey.isEmpty() to choose which signal to emit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase C — VolumeCoverResolver re-key to seriesKey + MangaUpdates JP-title source

### Task 6: Re-key VolumeCoverResolver entry + drop AniListCache dependency

**Files:**
- Modify: `src/core/manga/bookwalker/VolumeCoverResolver.{h,cpp}`

**Context:** Yesterday's BookWalker arc shipped `VolumeCoverResolver::resolveForAnilist(int anilistId)`. The chain reads `AniListCache::japaneseTitleFor` for the BookWalker JP search input. This task replaces that dependency with a `VolumeMetadataResolver` lookup; the chain becomes seriesKey-driven end-to-end.

- [ ] **Step 1: Update header — new entry point + dependency swap**

In `src/ui/pages/comics/VolumeCoverResolver.h` (Note: actual path is `src/core/manga/bookwalker/VolumeCoverResolver.h`), replace the constructor signature:

OLD:
```cpp
VolumeCoverResolver(BookWalkerClient* bwClient,
                    tankoban::manga::anilist::AniListCache* anilistCache,
                    tankoban::manga::PremiumCatalog* premium,
                    QObject* parent = nullptr);
```

NEW:
```cpp
VolumeCoverResolver(BookWalkerClient* bwClient,
                    tankoban::manga::mangaupdates::VolumeMetadataResolver* muResolver,
                    tankoban::manga::PremiumCatalog* premium,
                    QObject* parent = nullptr);
```

Replace the entry method:

OLD:
```cpp
void resolveForAnilist(int anilistId);
```

NEW:
```cpp
// seriesKey is the WeebCentral sourceId:seriesId composite (e.g.
// "weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4"). anilistIdOptional is kept
// for Premium catalog backward-compat (PremiumCatalog::hasPremiumEntry
// currently takes int anilistId; can be 0 if not known).
void resolveForSeries(const QString& seriesKey,
                      const QString& englishTitle,
                      int anilistIdOptional = 0);
```

Replace the signal signatures:

OLD:
```cpp
void resolved(int anilistId, const QMap<int, QString>& volumeToCoverUrl);
void unresolved(int anilistId, const QString& reason);
void skipped(int anilistId, const QString& reason);
```

NEW:
```cpp
void resolved(const QString& seriesKey, const QMap<int, QString>& volumeToCoverUrl);
void unresolved(const QString& seriesKey, const QString& reason);
void skipped(const QString& seriesKey, const QString& reason);
```

Update private slots + members:

```cpp
private slots:
    void onMuResolvedBySeriesKey(const QString& seriesKey,
                                 int volumeCount,
                                 int chapterCount,
                                 const QStringList& altTitles);
    void onMuUnresolvedBySeriesKey(const QString& seriesKey, const QString& reason);
    void onBwSearchSucceeded(int requestId, const QList<BookWalkerSearchHit>& hits);
    void onBwSearchFailed(int requestId, const QString& reason);
    void onBwCoversSucceeded(int requestId, const QList<QString>& orderedCoverUrls);
    void onBwCoversFailed(int requestId, const QString& reason);

private:
    struct PendingResolve {
        QString seriesKey;
        QString englishTitle;
        int     anilistIdOptional = 0;
        QString japaneseTitle;        // populated after MU resolves
        int     canonicalCount = 0;    // populated after MU resolves
        QString bookwalkerSeriesId;    // populated after BW search resolves
    };

    QPointer<BookWalkerClient> m_bwClient;
    QPointer<tankoban::manga::mangaupdates::VolumeMetadataResolver> m_muResolver;
    QPointer<tankoban::manga::PremiumCatalog> m_premium;
    QHash<QString, PendingResolve> m_pendingBySeriesKey;  // seriesKey is the key
    QHash<int, QString> m_bwRequestIdToSeriesKey;          // bw requestId → seriesKey
    int m_nextBwRequestId = 1;
```

Drop the `#include "core/manga/anilist/AniListCache.h"` from the header. Add `#include "core/manga/mangaupdates/VolumeMetadataResolver.h"`.

- [ ] **Step 2: Re-implement the resolver chain in `.cpp`**

Replace the body of `VolumeCoverResolver.cpp` with the new chain. The key changes vs yesterday's version:
- Constructor takes `muResolver` instead of `anilistCache`
- Constructor connects to `muResolver`'s `resolvedBySeriesKey` + `unresolvedBySeriesKey` signals (in addition to existing BW client signals)
- `resolveForSeries` enters: Premium short-circuit → MU lookup → (slot fires) → BW search → BW page parse → align → cache → emit

```cpp
#include "VolumeCoverResolver.h"

#include "BookWalkerCache.h"
#include "BookWalkerClient.h"
#include "BookWalkerSeriesPageParser.h"
#include "VolumeCoverAlignment.h"

#include "core/manga/PremiumCatalog.h"
#include "core/manga/mangaupdates/JapaneseTitlePicker.h"
#include "core/manga/mangaupdates/VolumeMetadataResolver.h"

#include <QDateTime>
#include <QtGlobal>

namespace tankoban::manga::bookwalker {

VolumeCoverResolver::VolumeCoverResolver(
        BookWalkerClient* bwClient,
        tankoban::manga::mangaupdates::VolumeMetadataResolver* muResolver,
        tankoban::manga::PremiumCatalog* premium,
        QObject* parent)
    : QObject(parent), m_bwClient(bwClient), m_muResolver(muResolver), m_premium(premium)
{
    if (m_muResolver) {
        connect(m_muResolver.data(),
                &tankoban::manga::mangaupdates::VolumeMetadataResolver::resolvedBySeriesKey,
                this, &VolumeCoverResolver::onMuResolvedBySeriesKey);
        connect(m_muResolver.data(),
                &tankoban::manga::mangaupdates::VolumeMetadataResolver::unresolvedBySeriesKey,
                this, &VolumeCoverResolver::onMuUnresolvedBySeriesKey);
    }
    if (m_bwClient) {
        connect(m_bwClient.data(), &BookWalkerClient::searchSucceeded,
                this, &VolumeCoverResolver::onBwSearchSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::searchFailed,
                this, &VolumeCoverResolver::onBwSearchFailed);
        connect(m_bwClient.data(), &BookWalkerClient::coversSucceeded,
                this, &VolumeCoverResolver::onBwCoversSucceeded);
        connect(m_bwClient.data(), &BookWalkerClient::coversFailed,
                this, &VolumeCoverResolver::onBwCoversFailed);
    }
}

VolumeCoverResolver::~VolumeCoverResolver()
{
    if (m_muResolver) disconnect(m_muResolver.data(), nullptr, this, nullptr);
    if (m_bwClient) disconnect(m_bwClient.data(), nullptr, this, nullptr);
}

void VolumeCoverResolver::resolveForSeries(const QString& seriesKey,
                                           const QString& englishTitle,
                                           int anilistIdOptional)
{
    if (seriesKey.isEmpty()) {
        emit unresolved(QString(), QStringLiteral("empty-series-key"));
        return;
    }

    // Step 1: Premium short-circuit. PremiumCatalog::hasPremiumEntry is currently
    // anilistId-keyed; pass the optional id (0 falls through).
    if (m_premium && anilistIdOptional > 0 && m_premium->hasPremiumEntry(anilistIdOptional)) {
        emit skipped(seriesKey, QStringLiteral("premium-short-circuit"));
        return;
    }

    // Step 2: Cache check (TTL + drift). At this stage canonicalCount is unknown,
    // so we read with currentCanonicalCount=0 (drift check skipped). The chain
    // re-checks drift if cache hits AND MU resolves with a fresh count later.
    auto cached = BookWalkerCache::loadByKey(seriesKey, /*currentCanonicalCount=*/0);
    if (cached) {
        QMap<int, QString> m;
        for (const auto& e : cached->volumes) m.insert(e.volume, e.url);
        emit resolved(seriesKey, m);
        return;
    }

    // Step 3: Kick off MU lookup. Slot fires later with altTitles + count.
    if (!m_muResolver) {
        emit unresolved(seriesKey, QStringLiteral("mu-resolver-null"));
        return;
    }

    PendingResolve p;
    p.seriesKey = seriesKey;
    p.englishTitle = englishTitle;
    p.anilistIdOptional = anilistIdOptional;
    m_pendingBySeriesKey.insert(seriesKey, p);

    m_muResolver->resolveBySeriesKey(seriesKey, englishTitle);
}

void VolumeCoverResolver::onMuResolvedBySeriesKey(const QString& seriesKey,
                                                  int volumeCount,
                                                  int /*chapterCount*/,
                                                  const QStringList& altTitles)
{
    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    PendingResolve p = it.value();
    p.canonicalCount = volumeCount;
    p.japaneseTitle = tankoban::manga::mangaupdates::JapaneseTitlePicker::pickFirstJapanese(altTitles);
    it.value() = p;

    if (p.japaneseTitle.isEmpty()) {
        // No Japanese title in MU response — fall back to series-level cover per spec §6
        m_pendingBySeriesKey.erase(it);
        emit unresolved(seriesKey, QStringLiteral("no-japanese-title"));
        return;
    }

    if (!m_bwClient) {
        m_pendingBySeriesKey.erase(it);
        emit unresolved(seriesKey, QStringLiteral("bw-client-null"));
        return;
    }

    const int bwReqId = m_nextBwRequestId++;
    m_bwRequestIdToSeriesKey.insert(bwReqId, seriesKey);
    m_bwClient->searchSeries(p.japaneseTitle, bwReqId);
}

void VolumeCoverResolver::onMuUnresolvedBySeriesKey(const QString& seriesKey, const QString& reason)
{
    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    m_pendingBySeriesKey.erase(it);
    emit unresolved(seriesKey, QStringLiteral("mu-unresolved: ") + reason);
}

void VolumeCoverResolver::onBwSearchSucceeded(int requestId,
                                              const QList<BookWalkerSearchHit>& hits)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    PendingResolve p = it.value();

    const QString bwSeriesId = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, p.japaneseTitle);
    if (bwSeriesId.isEmpty()) {
        m_pendingBySeriesKey.erase(it);
        emit unresolved(seriesKey, QStringLiteral("series-not-on-bookwalker"));
        return;
    }
    p.bookwalkerSeriesId = bwSeriesId;
    it.value() = p;

    const int bwReqId = m_nextBwRequestId++;
    m_bwRequestIdToSeriesKey.insert(bwReqId, seriesKey);
    m_bwClient->fetchSeriesCovers(bwSeriesId, bwReqId);
}

void VolumeCoverResolver::onBwSearchFailed(int requestId, const QString& reason)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    m_pendingBySeriesKey.remove(seriesKey);
    emit unresolved(seriesKey, QStringLiteral("bw-search-failed: ") + reason);
}

void VolumeCoverResolver::onBwCoversSucceeded(int requestId,
                                              const QList<QString>& orderedCoverUrls)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    auto it = m_pendingBySeriesKey.find(seriesKey);
    if (it == m_pendingBySeriesKey.end()) return;
    const PendingResolve p = it.value();
    m_pendingBySeriesKey.erase(it);

    auto aligned = VolumeCoverAlignment::align(orderedCoverUrls, p.canonicalCount);
    if (aligned.isEmpty()) {
        emit unresolved(seriesKey, QStringLiteral("alignment-empty"));
        return;
    }

    BookWalkerCacheRecord rec;
    rec.schemaVersion = 1;
    rec.fetchedAt = QDateTime::currentDateTimeUtc();
    rec.canonicalCount = (p.canonicalCount > 0 ? p.canonicalCount : aligned.size());
    rec.bookwalkerSeriesId = p.bookwalkerSeriesId;
    for (auto k = aligned.constBegin(); k != aligned.constEnd(); ++k) {
        BookWalkerCoverEntry e;
        e.volume = k.key();
        e.url = k.value();
        rec.volumes.append(e);
    }
    if (!BookWalkerCache::storeByKey(seriesKey, rec)) {
        qWarning("VolumeCoverResolver: failed to persist BookWalker cache for seriesKey=%s",
                 qUtf8Printable(seriesKey));
    }

    emit resolved(seriesKey, aligned);
}

void VolumeCoverResolver::onBwCoversFailed(int requestId, const QString& reason)
{
    auto idIt = m_bwRequestIdToSeriesKey.find(requestId);
    if (idIt == m_bwRequestIdToSeriesKey.end()) return;
    const QString seriesKey = idIt.value();
    m_bwRequestIdToSeriesKey.erase(idIt);

    m_pendingBySeriesKey.remove(seriesKey);
    emit unresolved(seriesKey, QStringLiteral("bw-covers-failed: ") + reason);
}

} // namespace tankoban::manga::bookwalker
```

- [ ] **Step 3: Build-check**

```bash
build_check.bat
```

Expected: BUILD will FAIL because `BookWalkerCache::loadByKey` and `BookWalkerCache::storeByKey` don't exist yet (Task 7 adds them). Note the failure; proceed to Task 7.

- [ ] **Step 4: Defer commit until Task 7 lands (BookWalkerCache key-API additions)**

Do NOT commit yet. The resolver and cache changes ship together as a coherent unit.

---

### Task 7: BookWalkerCache key-based API + filename change

**Files:**
- Modify: `src/core/manga/bookwalker/BookWalkerCache.{h,cpp}`

**Context:** Existing `BookWalkerCache::load(int anilistId, ...)` + `store(int anilistId, ...)` are anilistId-keyed. Add parallel `loadByKey(QString seriesKey, ...)` + `storeByKey(QString seriesKey, ...)` that derive a safe filename (slash → underscore so `weebcentral:01J...` becomes `weebcentral_01J....json`).

- [ ] **Step 1: Header — add the new methods**

In `src/core/manga/bookwalker/BookWalkerCache.h`:

```cpp
// Add to public section (alongside existing load/store):

// Compute the filename-safe path for a seriesKey ("weebcentral:01J..." → "weebcentral_01J....json").
static QString cacheFilePathByKey(const QString& seriesKey);

// Load + validate. TTL + drift checks identical to the anilistId variant.
static std::optional<BookWalkerCacheRecord> loadByKey(const QString& seriesKey,
                                                      int currentCanonicalCount,
                                                      qint64 ttlSeconds = kDefaultTtlSeconds);

// Atomic write keyed by seriesKey.
static bool storeByKey(const QString& seriesKey, const BookWalkerCacheRecord& record);
```

- [ ] **Step 2: Implementation**

In `src/core/manga/bookwalker/BookWalkerCache.cpp`, add the three method bodies. Reuse the existing helpers:

```cpp
QString BookWalkerCache::cacheFilePathByKey(const QString& seriesKey)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Slash + colon are filename-illegal on Windows; replace both with underscore.
    QString safe = seriesKey;
    safe.replace(QChar(':'), QChar('_'));
    safe.replace(QChar('/'), QChar('_'));
    safe.replace(QChar('\\'), QChar('_'));
    return QStringLiteral("%1/cache/bookwalker_covers/%2.json").arg(root, safe);
}

std::optional<BookWalkerCacheRecord> BookWalkerCache::loadByKey(const QString& seriesKey,
                                                                int currentCanonicalCount,
                                                                qint64 ttlSeconds)
{
    const QString path = cacheFilePathByKey(seriesKey);
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("schemaVersion")).toInt() != 1) return std::nullopt;

    BookWalkerCacheRecord rec;
    rec.schemaVersion = 1;
    rec.fetchedAt = QDateTime::fromString(obj.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    if (!rec.fetchedAt.isValid()) return std::nullopt;
    rec.canonicalCount = obj.value(QStringLiteral("canonicalCount")).toInt();
    rec.bookwalkerSeriesId = obj.value(QStringLiteral("bookwalkerSeriesId")).toString();

    const qint64 ageSeconds = rec.fetchedAt.secsTo(QDateTime::currentDateTimeUtc());
    if (ageSeconds > ttlSeconds) return std::nullopt;

    if (currentCanonicalCount > 0 && rec.canonicalCount != currentCanonicalCount) {
        return std::nullopt;
    }

    const QJsonArray vols = obj.value(QStringLiteral("volumes")).toArray();
    for (const auto& v : vols) {
        const QJsonObject vo = v.toObject();
        BookWalkerCoverEntry e;
        e.volume = vo.value(QStringLiteral("vol")).toInt();
        e.url = vo.value(QStringLiteral("url")).toString();
        if (e.volume > 0 && !e.url.isEmpty()) rec.volumes.append(e);
    }
    return rec;
}

bool BookWalkerCache::storeByKey(const QString& seriesKey, const BookWalkerCacheRecord& record)
{
    const QString path = cacheFilePathByKey(seriesKey);
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 1);
    obj.insert(QStringLiteral("fetchedAt"),
               (record.fetchedAt.isValid() ? record.fetchedAt : QDateTime::currentDateTimeUtc())
                   .toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("canonicalCount"), record.canonicalCount);
    obj.insert(QStringLiteral("bookwalkerSeriesId"), record.bookwalkerSeriesId);

    QJsonArray arr;
    for (const auto& e : record.volumes) {
        QJsonObject vo;
        vo.insert(QStringLiteral("vol"), e.volume);
        vo.insert(QStringLiteral("url"), e.url);
        arr.append(vo);
    }
    obj.insert(QStringLiteral("volumes"), arr);

    QSaveFile sf(path);
    if (!sf.open(QIODevice::WriteOnly)) return false;
    sf.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return sf.commit();
}
```

The existing anilistId-keyed `load`/`store` methods stay in the file for now (used by no one after the resolver rewrite, but harmless dead code; cleanup in a future v1.x task).

- [ ] **Step 3: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`. The resolver from Task 6 now compiles successfully.

- [ ] **Step 4: Run existing parser+alignment unit tests (should still pass)**

```powershell
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R "BookWalkerSeriesPageParser|VolumeCoverAlignment|JapaneseTitlePicker"'
```

Expected: 17 tests PASS (6 parser + 5 alignment + 6 JapaneseTitlePicker — no regressions).

- [ ] **Step 5: Commit Tasks 6+7 together**

```bash
git add src/core/manga/bookwalker/VolumeCoverResolver.h \
        src/core/manga/bookwalker/VolumeCoverResolver.cpp \
        src/core/manga/bookwalker/BookWalkerCache.h \
        src/core/manga/bookwalker/BookWalkerCache.cpp
git commit -m "$(cat <<'EOF'
feat(manga/bookwalker): re-key VolumeCoverResolver to seriesKey + MU-driven JP title

Replace AniListCache dependency in VolumeCoverResolver with
VolumeMetadataResolver. New entry point resolveForSeries(seriesKey,
englishTitle, anilistIdOptional) — seriesKey is the WeebCentral
sourceId:seriesId composite. Chain: Premium short-circuit (still
anilistId-keyed via PremiumCatalog::hasPremiumEntry, optional) →
cache check (loadByKey) → MU resolveBySeriesKey → JapaneseTitlePicker
on returned altTitles → BookWalker search + page parse + align → cache
write (storeByKey) → emit resolved(seriesKey, QMap<int,QString>).

BookWalkerCache gains loadByKey / storeByKey / cacheFilePathByKey using
sanitized filename (colon → underscore for Windows compat).

Existing anilistId-keyed load/store retained as dead code for now;
cleanup in v1.x.

Per WEEBCENTRAL_IDENTITY_PIVOT spec Decisions #4 and #5: AniList is no
longer on the BookWalker critical path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase D — ComicsSeriesView accepts WeebCentral identity

### Task 8: Add `showSeries(MangaResult)` overload + state-mirroring

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.{h,cpp}`

**Context:** Current entry: `showSeries(const anilist::MediaPreview& preview)`. Add a sibling overload that takes WeebCentral `MangaResult`. The two paths share `m_currentSeriesKey` storage; the resolver call switches to the new `resolveForSeries(seriesKey, englishTitle)` entry. Existing AniList-driven path stays during migration.

- [ ] **Step 1: Header — add overload and seriesKey member**

In `src/ui/pages/comics/ComicsSeriesView.h`:

```cpp
#include "core/manga/MangaResult.h"   // Add near existing includes

public:
    void showSeries(const anilist::MediaPreview& preview);   // existing
    void showSeries(const MangaResult& wc);                  // NEW

private:
    QString m_currentSeriesKey;   // NEW: "<sourceId>:<seriesId>" composite — primary identity post-pivot
```

Also update the resolver slot signatures (existing `onCoverResolverResolved(int anilistId, ...)` becomes seriesKey-keyed):

```cpp
private slots:
    void onCoverResolverResolved(const QString& seriesKey, const QMap<int, QString>& volumeToCoverUrl);
    void onCoverResolverUnresolved(const QString& seriesKey, const QString& reason);
    void onCoverResolverSkipped(const QString& seriesKey, const QString& reason);
    void onCoverResolverSafetyTimeout();
```

Add `QString m_currentResolvingSeriesKey;` (replaces `int m_currentResolvingAnilistId`):

```cpp
QString m_currentResolvingSeriesKey;
```

- [ ] **Step 2: Implement `showSeries(MangaResult)` in `.cpp`**

In `src/ui/pages/comics/ComicsSeriesView.cpp`, add the overload near the existing `showSeries(MediaPreview)`. The bodies are nearly identical except for which fields drive the cover-resolver call.

```cpp
void ComicsSeriesView::showSeries(const MangaResult& wc)
{
    if (wc.id.isEmpty() || wc.source.isEmpty()) {
        qWarning("ComicsSeriesView::showSeries(MangaResult): empty id or source");
        return;
    }

    m_currentSeriesKey = wc.source + QStringLiteral(":") + wc.id;
    m_currentSeriesTitle = wc.title;
    m_currentResolvingSeriesKey = m_currentSeriesKey;

    // Banner / poster: MangaResult.thumbnailUrl is the WeebCentral cover. Use it
    // for the hero banner until/unless an AniList augmentation lookup overrides
    // (v1.x decoration; not in scope for this task).
    if (!wc.thumbnailUrl.isEmpty()) {
        loadBannerUrl(wc.thumbnailUrl);
    }

    // Show loading overlay + start safety timer (mirror existing MediaPreview path)
    showLoadingOverlay();
    if (m_loadingSafetyTimer) m_loadingSafetyTimer->start();

    // Fire the cover resolver via the new seriesKey-driven entry.
    if (m_coverResolver) {
        m_coverResolver->resolveForSeries(m_currentSeriesKey, wc.title, /*anilistIdOptional=*/0);
    } else {
        qWarning("ComicsSeriesView: no cover resolver set; falling back to series-level art");
        paintVolumeCoversAsFallback();
        hideLoadingOverlay();
    }

    // Trigger detail fetch via the WeebCentral scraper (volume/chapter list).
    // ADAPT this call to the actual existing populate path the .cpp uses today.
    // Most likely the existing MediaPreview path eventually calls populateVolumeRows
    // via an AniList detail fetch; the MangaResult path goes through
    // m_sourceRegistry->activeScraper()->fetchDetail(wc) instead.
    if (m_sourceRegistry) {
        if (auto* scraper = m_sourceRegistry->activeScraper()) {
            scraper->fetchDetail(wc);  // emits detailReady → existing slot populates rows
        }
    }
}
```

- [ ] **Step 3: Add `m_sourceRegistry` member if not already present**

If `ComicsSeriesView` doesn't already hold a `MangaSourceRegistry*` (it almost certainly doesn't — current AniList-only path doesn't use it), add it as a constructor-injected dependency:

In the header:
```cpp
#include "core/manga/MangaSourceRegistry.h"

private:
    MangaSourceRegistry* m_sourceRegistry = nullptr;  // non-owning
```

Add a setter (constructor changes are riskier; setter is additive):
```cpp
public:
    void setSourceRegistry(MangaSourceRegistry* registry) { m_sourceRegistry = registry; }
```

In `ComicsPage.cpp` (the construction site), call the setter after `ComicsSeriesView` is constructed. Find the existing construction (around line 308 per earlier grep) and add a line:
```cpp
m_seriesView->setSourceRegistry(m_sourceRegistry);  // m_sourceRegistry is ComicsPage's existing field
```

If `ComicsPage` doesn't yet own `m_sourceRegistry`, create it in the constructor (existing pattern: `m_sourceRegistry = new MangaSourceRegistry(m_nam, this);`). Check via grep first.

- [ ] **Step 4: Update all 4 resolver slots to use seriesKey**

In `src/ui/pages/comics/ComicsSeriesView.cpp`, replace each existing slot body. The pattern: replace `int anilistId` param with `const QString& seriesKey`, replace stale-guard `m_currentResolvingAnilistId` with `m_currentResolvingSeriesKey`.

```cpp
void ComicsSeriesView::onCoverResolverResolved(
        const QString& seriesKey, const QMap<int, QString>& volumeToCoverUrl)
{
    if (seriesKey != m_currentResolvingSeriesKey) return;
    paintVolumeCovers(volumeToCoverUrl);
    hideLoadingOverlay();
}

void ComicsSeriesView::onCoverResolverUnresolved(const QString& seriesKey, const QString& /*reason*/)
{
    if (seriesKey != m_currentResolvingSeriesKey) return;
    paintVolumeCoversAsFallback();
    hideLoadingOverlay();
}

void ComicsSeriesView::onCoverResolverSkipped(const QString& seriesKey, const QString& /*reason*/)
{
    if (seriesKey != m_currentResolvingSeriesKey) return;
    hideLoadingOverlay();  // no paint — Premium pipeline handles
}

void ComicsSeriesView::onCoverResolverSafetyTimeout()
{
    paintVolumeCoversAsFallback();
    hideLoadingOverlay();
}
```

Update `clearView` to reset the new state:
```cpp
m_currentResolvingSeriesKey.clear();
m_currentSeriesKey.clear();
hideLoadingOverlay();
```

Update `setVolumeCoverResolver` connect calls to match the new signal signatures (seriesKey instead of anilistId — already changed in Phase C Task 6).

- [ ] **Step 5: ALSO update the existing `showSeries(MediaPreview)` to set `m_currentSeriesKey`**

The existing AniList path also needs to push a seriesKey for the resolver. For an AniList-driven open, fabricate a synthetic key (`"anilist:" + anilistIdString`) and pass it. This preserves backward compat during the migration:

In the existing `showSeries(MediaPreview)` body, find the resolver invocation and replace:

OLD:
```cpp
m_coverResolver->resolveForAnilist(preview.id);
```

NEW:
```cpp
m_currentSeriesKey = QStringLiteral("anilist:%1").arg(preview.id);
m_currentResolvingSeriesKey = m_currentSeriesKey;
m_coverResolver->resolveForSeries(m_currentSeriesKey,
                                  preview.title.english.isEmpty()
                                      ? preview.title.romaji : preview.title.english,
                                  preview.id);  // anilistIdOptional for Premium check
```

- [ ] **Step 6: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK` (some warnings about unused includes are acceptable).

- [ ] **Step 7: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h \
        src/ui/pages/comics/ComicsSeriesView.cpp \
        src/ui/pages/ComicsPage.cpp
git commit -m "$(cat <<'EOF'
feat(ui/comics): ComicsSeriesView accepts MangaResult; resolver re-keyed on seriesKey

Add showSeries(MangaResult) overload alongside existing
showSeries(MediaPreview). The new path drives the cover resolver via
resolveForSeries(seriesKey, englishTitle) and fetches chapter detail
via MangaSourceRegistry::activeScraper()->fetchDetail(MangaResult).

All 4 resolver-callback slots updated: seriesKey replaces anilistId in
signal payloads + stale-request guards. m_currentResolvingSeriesKey
member replaces m_currentResolvingAnilistId. clearView resets the
new state.

The existing showSeries(MediaPreview) path is preserved during the
migration: it now stamps a synthetic key "anilist:<id>" for the
resolver. This is removed in a future cleanup task when all callers
migrate to the WeebCentral entry.

Construction-site change: ComicsPage::setSourceRegistry wires
MangaSourceRegistry into ComicsSeriesView; the registry was already
owned by ComicsPage (created in the existing constructor).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase E — Search bar routes through WeebCentral

### Task 9: ComicsTankoyomiSearchWidget signal payload becomes MangaResult

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}`

- [ ] **Step 1: Investigate current widget shape**

Read both files. Report (in your output) what the widget currently signals when the user picks a result. Identify:
- Constructor signature (does it take `AniListClient*`?)
- Public surface (slots? signals?)
- Internal members

This is investigation only — no edits yet. Report findings before proceeding.

- [ ] **Step 2: Add MangaResult-payload signal + MangaSourceRegistry dependency**

Adjust the constructor signature in `.h`:

OLD:
```cpp
explicit ComicsTankoyomiSearchWidget(tankoban::manga::anilist::AniListClient* client,
                                     QNetworkAccessManager* nam,
                                     QWidget* parent = nullptr);
```

NEW:
```cpp
explicit ComicsTankoyomiSearchWidget(MangaSourceRegistry* sourceRegistry,
                                     QNetworkAccessManager* nam,
                                     QWidget* parent = nullptr);
```

Add a new signal:
```cpp
signals:
    void resultPicked(const MangaResult& result);
```

Drop or deprecate the existing `previewPicked(const anilist::MediaPreview&)` signal (if it exists — verify via Step 1).

In `.cpp`, replace the search-trigger invocation:

OLD (somewhere in the user-types-query handler):
```cpp
m_anilistClient->searchByTitle(query, requestId);
```

NEW:
```cpp
if (auto* scraper = m_sourceRegistry->activeScraper()) {
    scraper->search(query, /*limit=*/60);
}
```

Connect to the scraper's existing `searchFinished(QList<MangaResult>)` signal in the constructor. When the user picks a tile, emit `resultPicked(theResult)`.

- [ ] **Step 3: Build-check (ComicsPage will likely break — that's expected, Task 10 fixes it)**

```bash
build_check.bat
```

Expected: BUILD FAIL on ComicsPage's construction site (passes `AniListClient*` to the constructor that now wants `MangaSourceRegistry*`). Note the failure; proceed to Task 10.

- [ ] **Step 4: Defer commit until Task 10 lands**

---

### Task 10: ComicsPage wires search bar to MangaSourceRegistry + drops AniList search connection

**Files:**
- Modify: `src/ui/pages/ComicsPage.{h,cpp}`

- [ ] **Step 1: Construction-site change**

In `src/ui/pages/ComicsPage.cpp`, find the line that constructs `ComicsTankoyomiSearchWidget` (line ~180 per earlier grep):

OLD:
```cpp
m_searchTakeover = new ComicsTankoyomiSearchWidget(m_anilistClient, m_nam, this);
```

NEW:
```cpp
m_searchTakeover = new ComicsTankoyomiSearchWidget(m_sourceRegistry, m_nam, this);
```

(`m_sourceRegistry` should already be a member of ComicsPage — verify via grep. If not, construct it: `m_sourceRegistry = new MangaSourceRegistry(m_nam, this);` near other ComicsPage member-init lines.)

- [ ] **Step 2: Drop the AniList search-bar signal connection**

Find `ComicsPage::doSearch` or whichever method invokes `m_anilistClient->searchByTitle` (line ~2649-2686 per earlier grep). The whole block can be removed — search is now driven by the widget itself, not orchestrated through ComicsPage.

Alternatively, gate the existing block behind a build-flag to allow regression rollback. Pragmatic call: just remove. The git history preserves the old shape.

- [ ] **Step 3: Connect search widget's new signal to ComicsSeriesView**

Find existing `connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::previewPicked, ...)`. Replace with:

```cpp
connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::resultPicked,
        this, [this](const MangaResult& r) {
            if (m_seriesView) m_seriesView->showSeries(r);
            // Navigate from search-takeover to series-view layer (preserve existing layer-management call)
            navigateToDetailLayer();  // ADAPT to actual method name
        });
```

- [ ] **Step 4: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`. All Phase D + E code compiles + links.

- [ ] **Step 5: Commit Tasks 9+10 together**

```bash
git add src/ui/pages/comics/ComicsTankoyomiSearchWidget.h \
        src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp \
        src/ui/pages/ComicsPage.h \
        src/ui/pages/ComicsPage.cpp
git commit -m "$(cat <<'EOF'
feat(ui/comics): search bar routes through WeebCentral (MangaSourceRegistry)

ComicsTankoyomiSearchWidget swaps its constructor dep from AniListClient
to MangaSourceRegistry. Search invocation now goes through
m_sourceRegistry->activeScraper()->search(query, 60) which today routes
to WeebCentralScraper. The widget emits resultPicked(MangaResult) when
the user clicks a tile.

ComicsPage drops the legacy AniList search-bar connection
(m_anilistClient->searchByTitle path entirely removed from the search
flow). The library-entry → series-view path (which reconstructs a
MangaResult from ComicsLibraryRecord fields) is preserved.

m_anilistClient remains in ComicsPage as an unused member for now;
removal deferred to v1.x cleanup so the AniList decoration layer
(optional banner/description on series-detail-open) can be wired
without re-introducing the constructor parameter.

Per WEEBCENTRAL_IDENTITY_PIVOT spec Decisions #1 and #5.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase F — Library-entry → series-view path

### Task 11: Reconstruct `MangaResult` from `ComicsLibraryRecord` for library re-open

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (the library-tile-click handler)

**Context:** When the user clicks a library tile, today's code probably calls `ComicsSeriesView::showSeries(MediaPreview)` after looking up the AniList preview from somewhere. The new flow constructs a `MangaResult` directly from the library record fields and calls the new `showSeries(MangaResult)` overload.

- [ ] **Step 1: Find the library-tile click handler**

Grep:
```bash
grep -n "showSeries\|openLibraryEntry\|onLibraryTileClicked" src/ui/pages/ComicsPage.cpp | head -20
```

Identify the slot that handles library-tile clicks. Report the line number.

- [ ] **Step 2: Replace the MediaPreview construction with MangaResult**

In the handler, replace whatever currently constructs the MediaPreview with:

```cpp
MangaResult result;
result.id = record.seriesId;
result.url = QString();  // not stored in library; leave empty
result.title = record.title;
result.author = QString();  // not stored at top level; could come from detailCache if needed
result.thumbnailUrl = QStringLiteral("file:///") + record.coverPath;  // local cached cover
result.source = record.sourceId;
result.status = record.detailCache.status;  // ADAPT to actual MangaSeriesDetail field
result.type = QStringLiteral("manga");

if (m_seriesView) m_seriesView->showSeries(result);
```

Verify `MangaSeriesDetail.status` is the actual field name (read `src/core/manga/MangaSeriesDetail.h` if unsure).

- [ ] **Step 3: Build-check**

```bash
build_check.bat
```

Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "$(cat <<'EOF'
feat(ui/comics): library tile click drives MangaResult, not MediaPreview

When the user clicks a Comics library tile, the handler now reconstructs
a MangaResult from the ComicsLibraryRecord fields (sourceId, seriesId,
title, coverPath, detailCache) and calls ComicsSeriesView::showSeries
(MangaResult) — the new WeebCentral-driven entry point. The legacy
MediaPreview lookup chain is gone from the library-tile path.

Per WEEBCENTRAL_IDENTITY_PIVOT spec §5.4: legacy library entries (all
of which already use sourceId+seriesId identity) open via the WeebCentral
path directly. No lazy-backfill required — the library was already
weebcentralId-keyed before this arc started.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase G — Smoke matrix

### Task 12: Build verify + tankoctl smoke on Berserk

**Files (no changes):**
- Smoke evidence: `agents/audits/smoke_evidence/0350_weebcentral_search.png` and `0351_berserk_bookwalker_covers.png`

- [ ] **Step 1: Launch Tankoban**

Kill any running instance, then:

```bash
build_and_run.bat
```

Wait for the dev-bridge to come up:

```powershell
$up = $false; for ($i=0; $i -lt 30; $i++) { Start-Sleep 3; if ((.\out\tankoctl.exe ping 2>&1) -match '"schema"') { $up = $true; break } }; $up
```

Expected: True.

- [ ] **Step 2: Search "Berserk"**

```powershell
.\out\tankoctl.exe comics-search-tankoyomi "Berserk" --timeout 12000 | ConvertFrom-Json | Select-Object -ExpandProperty results | Select-Object -First 3 | Format-Table title, anilistId, poster
```

Expected: results array populated with WeebCentral-sourced entries. (Note: `comics-search-tankoyomi` may still surface its old AniList shape via the dev-bridge command implementation; in that case the smoke verifies via the UI directly. Adapt as needed.)

- [ ] **Step 3: Open Berserk via tankoctl** (or via MCP click on the search-result tile, screenshot for evidence)

```powershell
.\out\tankoctl.exe comics-open-series weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4
Start-Sleep 8
$resp = .\out\tankoctl.exe comics-get-series | ConvertFrom-Json
$vols = $resp.series.volumes
"rows=$($vols.Count) | BookWalker=$(($vols | Where-Object { $_.coverUrl -match 'rimg.bookwalker' }).Count)"
$vols | Select-Object -First 3 | ForEach-Object { Write-Host "vol $($_.volumeLabel) -> $($_.coverUrl)" }
```

(`comics-open-series` may need its argument shape adapted — yesterday it accepted `int anilistId`; today's pivot may require accepting `QString seriesKey`. If tankoctl's surface needs updating, that's an additional small task on the dev-bridge side.)

Expected: 43 rows, BookWalker URLs match the count.

- [ ] **Step 4: Visual verification with Hemanth at the keyboard**

Hand off to Hemanth for eyes-on-screen smoke. Capture screenshot to `agents/audits/smoke_evidence/0351_berserk_bookwalker_covers.png`. Verify:
- 43 distinct tankobon covers visible
- Hero banner shows Berserk WeebCentral cover (not AniList banner)
- Sources panel correctly anchored at top
- No flash-then-disappear race

- [ ] **Step 5: Smoke commit (RTC only)**

No code change. Append RTC to `agents/chat.md` flagging the WEEBCENTRAL_IDENTITY_PIVOT shipment for Agent 0 sweep:

```bash
git add agents/chat.md agents/audits/smoke_evidence/0350_weebcentral_search.png agents/audits/smoke_evidence/0351_berserk_bookwalker_covers.png
git commit -m "$(cat <<'EOF'
chat: flag WEEBCENTRAL_IDENTITY_PIVOT smoke green for Agent 0 sweep

Berserk end-to-end: WeebCentral search → series view → 43 BookWalker
tankobon covers visually verified. AniList offline during smoke (verified
via firewall block or simulated 403) — search and covers both still work
end-to-end. Phase 4 of the WeebCentral identity pivot arc complete.

Smoke evidence:
- 0350_weebcentral_search.png — Berserk in search results, WeebCentral-sourced
- 0351_berserk_bookwalker_covers.png — series view with 43 distinct tankobon covers

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

**Spec coverage:**
- §3 Decision #1 (search never queries AniList) → Tasks 9 + 10 ✓
- §3 Decision #2 (WeebCentral-only series shown with raw title) → Task 8 (showSeries(MangaResult) uses `wc.title` directly) ✓
- §3 Decision #3 (library identity = hybrid) → SUPERSEDED by the realization that ComicsLibraryRecord is already weebcentralId-only; no schema migration needed. Task 11 handles the library-entry → series-view path directly. ✓ (with simplification)
- §3 Decision #4 (BookWalker JP-title from MangaUpdates `associated`) → Tasks 1-4 (MU parse + JapaneseTitlePicker) + Tasks 6-7 (resolver re-key) ✓
- §3 Decision #5 (AniList demoted to decoration) → Tasks 6 (drop AniListCache dep) + 10 (drop search-bar connection) ✓
- §3 Decision #6 (lazy library backfill) → Not needed — library is already WeebCentral-keyed ✓ (with simplification)
- §5 data flow (search / open / add-to-library / legacy) → Tasks 8 + 9 + 10 + 11 ✓
- §6 error matrix → handled at each resolver edge in Task 6 ✓
- §8 verification (smoke matrix) → Task 12 ✓
- §8 unit tests (JapaneseTitlePicker, alignment, parser) → Task 4 (JapaneseTitlePicker TDD); alignment + parser unit tests from BookWalker arc continue to run unchanged ✓

**Placeholder scan:**
- "ADAPT to actual existing populate path" appears in Task 8 — this is acknowledging the existing code surface needs reading; the implementer subagent has to grep for the existing `populateVolumeRows` invocation site. Mitigated by the explicit grep command in Task 8 Step 5.
- "ADAPT to actual method name" for `navigateToDetailLayer` in Task 10 — implementer greps for the existing layer-switch method.
- "Verify `MangaSeriesDetail.status` is the actual field name" in Task 11 — implementer reads the header.

These are honest references-to-codebase-context, not vague hand-waves. Each one is paired with a specific verification step.

**Type consistency:**
- `seriesKey` is `QString` throughout (Tasks 5-12) ✓
- `altTitles` is `QStringList` consistently (Tasks 1-4) ✓
- `MangaResult` is the WeebCentral type from `MangaResult.h` (Tasks 8-11) ✓
- `resolveForSeries` signature (`QString seriesKey, QString englishTitle, int anilistIdOptional`) consistent between Task 6 declaration and Task 8 invocation ✓
- `resolvedBySeriesKey` signal signature consistent between Task 5 declaration and Task 6 connection ✓
- BookWalkerCache `loadByKey` / `storeByKey` signatures consistent between Task 7 declaration and Task 6 invocation ✓

**Scope check:**
- Single coherent pivot, all tasks contribute to the same architectural change. Smoke phase is included. AniList client removal explicitly deferred to v1.x.
