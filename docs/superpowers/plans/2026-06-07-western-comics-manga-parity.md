# Western Comics manga-parity — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Western comics mode work like Manga — a real My Library (only added/downloaded series), its own Continue Reading strip, and live readallcomics search — without leaking western into the Manga strips.

**Architecture:** A new per-user `WesternLibrary` JsonStore-backed store (slug-keyed) becomes the source of truth for "my western series," replacing the catalogue-dir dump. The Western page (`buildWesternScreen`) grows a Continue Reading strip + a My Library grid driven by that store. A shared header-only `isWesternIssueCbz()` classifier is the single marker that Manga *excludes* and Western *includes*. Search repoints from the dead `readcomicsonline` source to live `readallcomics`.

**Tech Stack:** C++17, Qt6 (Widgets/Core/Network), GoogleTest, JsonStore (atomic async JSON), existing `ComicsPage`/`ComicsSeriesView`/`TileStrip` infra.

**Spec:** `docs/superpowers/specs/2026-06-07-western-comics-manga-parity-design.md`

**Reference patterns (read before starting):**
- Store to mirror: `src/core/manga/ComicsTankoyomiLibrary.{h,cpp}` (JsonStore + QMutex, `load()`/`save()`, `nullptr`-store tolerance like `MangaDownloadIndex`).
- Test style + registration: `tests/core/manga/test_manga_download_index.cpp`; source list in `cmake/TankobanTests.cmake:120-140`.
- Progress-key helper precedent: `src/ui/readers/comic_progress_key.h` (header-only free function).
- Catalog types: `src/core/manga/MangaCatalogTypes.h` (`MangaCatalog.seriesId/seriesTitle/seriesCover/seriesSynopsis`).
- TileStrip API: `src/ui/pages/TileStrip.h` (`clear()`, `addTile(TileCard*)`, `setMode(QString)`, `setDensity(int)`, `totalCount()`, signals `tileSingleClicked`/`tileDoubleClicked`).

**Build commands:**
- Compile-only (agent-safe): `.\build_check.bat` (prints `BUILD OK` / `BUILD FAILED`). Kill `Tankoban.exe` first (Rule 1).
- Tests: `cmake --build out --target tankoban_tests` then `cd out; ctest --output-on-failure -R tankoban_tests`.
- App smoke: `build_and_run.bat`; dev-bridge via `out\tankoctl.exe`.

**Invariant (regression guard, do NOT break):** the June-6 manga filters stay —
`refreshLibraryStrips()` keeps `if (e.sourceId == "readallcomics") continue;` and
`refreshContinueStrip()` keeps the western-issue skip. Western surfaces are the mirror.

---

## Phase 1 — My Library (real added-only library)

### Task 1: `isWesternIssueCbz` shared classifier (header-only, TDD)

A western issue cbz is named `<Series> #<N>.cbz`. This single helper is what Manga excludes and Western includes — extracting it kills the duplicated regex and makes it testable.

**Files:**
- Create: `src/core/manga/WesternIssueKey.h`
- Test: `tests/core/manga/WesternIssueKeyTest.cpp`
- Modify: `cmake/TankobanTests.cmake` (register test + no new .cpp — header-only)

- [ ] **Step 1: Write the failing test**

```cpp
// tests/core/manga/WesternIssueKeyTest.cpp
#include <gtest/gtest.h>
#include "core/manga/WesternIssueKey.h"

using tankoban::manga::isWesternIssueCbz;
using tankoban::manga::westernIssueNumber;

TEST(WesternIssueKey, MatchesIssueNamedCbz) {
    EXPECT_TRUE(isWesternIssueCbz("Invincible #1"));
    EXPECT_TRUE(isWesternIssueCbz("The Walking Dead #144"));
    EXPECT_TRUE(isWesternIssueCbz("Saga #0"));
}

TEST(WesternIssueKey, RejectsMangaVolumeNames) {
    EXPECT_FALSE(isWesternIssueCbz("One Piece v114"));
    EXPECT_FALSE(isWesternIssueCbz("Volume X"));
    EXPECT_FALSE(isWesternIssueCbz("Death Note Volume 1"));
    EXPECT_FALSE(isWesternIssueCbz("Chapter 5"));
}

TEST(WesternIssueKey, ParsesIssueNumber) {
    EXPECT_EQ(westernIssueNumber("Invincible #1"), 1);
    EXPECT_EQ(westernIssueNumber("The Walking Dead #144"), 144);
    EXPECT_EQ(westernIssueNumber("Saga #0"), 0);
    EXPECT_EQ(westernIssueNumber("One Piece v114"), -1); // not a western issue
}
```

- [ ] **Step 2: Register the test, build, verify it FAILS**

In `cmake/TankobanTests.cmake`, add after the `WesternCatalogLoaderTest.cpp` line (~:139):
```cmake
        tests/core/manga/WesternIssueKeyTest.cpp
```
Run: `cmake --build out --target tankoban_tests`
Expected: FAIL — `Cannot open include file: 'core/manga/WesternIssueKey.h'`.

- [ ] **Step 3: Write the header**

```cpp
// src/core/manga/WesternIssueKey.h
#pragma once
#include <QString>
#include <QRegularExpression>

namespace tankoban::manga {

// A western (readallcomics) issue cbz is named "<Series> #<N>". Manga cbzs use
// "Volume N" / "vNN" and never this " #<digit>" form, so this is the single
// safe marker that the Manga strips EXCLUDE and the Western strips INCLUDE.
// `basename` is the cbz completeBaseName (no extension, no path).
inline bool isWesternIssueCbz(const QString& basename) {
    static const QRegularExpression re(QStringLiteral(" #\\d"));
    return re.match(basename).hasMatch();
}

// Returns the issue number from a western issue basename, or -1 if it isn't one.
inline int westernIssueNumber(const QString& basename) {
    static const QRegularExpression re(QStringLiteral(" #(\\d+)"));
    const auto m = re.match(basename);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

} // namespace tankoban::manga
```

- [ ] **Step 4: Build + run the test, verify it PASSES**

Run: `cmake --build out --target tankoban_tests; cd out; ctest --output-on-failure -R WesternIssueKey`
Expected: PASS (3/3).

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/WesternIssueKey.h tests/core/manga/WesternIssueKeyTest.cpp cmake/TankobanTests.cmake
git commit -m "feat(comics): WesternIssueKey classifier — shared western-issue cbz marker (WESTERN_PARITY P1 T1)"
```

---

### Task 2: `WesternLibrary` store + record (TDD)

Per-user store of "my western series," slug-keyed, JsonStore-backed, `nullptr`-store tolerant for unit tests (mirror `MangaDownloadIndex(nullptr)`).

**Files:**
- Create: `src/core/manga/WesternLibrary.h`, `src/core/manga/WesternLibrary.cpp`
- Test: `tests/core/manga/WesternLibraryTest.cpp`
- Modify: `cmake/TankobanTests.cmake` (register .cpp + test), `CMakeLists.txt` (add `WesternLibrary.cpp` to the `Tankoban` target source list)

- [ ] **Step 1: Write the failing test**

```cpp
// tests/core/manga/WesternLibraryTest.cpp
#include <gtest/gtest.h>
#include "core/manga/WesternLibrary.h"

using tankoban::manga::WesternLibrary;
using tankoban::manga::WesternLibraryRecord;

static WesternLibraryRecord rec(const QString& id, const QString& title) {
    WesternLibraryRecord r;
    r.seriesId = id; r.title = title;
    r.coverUrl = "http://x/" + id + ".jpg"; r.addedAt = 1000;
    return r;
}

TEST(WesternLibrary, AddThenContainsAndGet) {
    WesternLibrary lib(nullptr);
    lib.addOrUpdate(rec("invincible", "Invincible"));
    EXPECT_TRUE(lib.contains("invincible"));
    EXPECT_FALSE(lib.contains("saga"));
    ASSERT_TRUE(lib.get("invincible").has_value());
    EXPECT_EQ(lib.get("invincible")->title.toStdString(), "Invincible");
}

TEST(WesternLibrary, AddOrUpdateIsIdempotentBySeriesId) {
    WesternLibrary lib(nullptr);
    lib.addOrUpdate(rec("invincible", "Invincible"));
    lib.addOrUpdate(rec("invincible", "Invincible (Image)"));
    EXPECT_EQ(lib.all().size(), 1);
    EXPECT_EQ(lib.get("invincible")->title.toStdString(), "Invincible (Image)");
}

TEST(WesternLibrary, RemoveDropsOnlyThatSeries) {
    WesternLibrary lib(nullptr);
    lib.addOrUpdate(rec("invincible", "Invincible"));
    lib.addOrUpdate(rec("saga", "Saga"));
    lib.remove("invincible");
    EXPECT_FALSE(lib.contains("invincible"));
    EXPECT_TRUE(lib.contains("saga"));
    EXPECT_EQ(lib.all().size(), 1);
}

TEST(WesternLibrary, RecordJsonRoundTrip) {
    const auto r = rec("invincible", "Invincible");
    const auto back = WesternLibraryRecord::fromJson(r.toJson());
    EXPECT_EQ(back.seriesId.toStdString(), "invincible");
    EXPECT_EQ(back.title.toStdString(), "Invincible");
    EXPECT_EQ(back.coverUrl.toStdString(), "http://x/invincible.jpg");
    EXPECT_EQ(back.addedAt, 1000);
}
```

- [ ] **Step 2: Register + build + verify FAIL**

In `cmake/TankobanTests.cmake` add (next to the other manga sources, ~:138):
```cmake
        src/core/manga/WesternLibrary.cpp
        tests/core/manga/WesternLibraryTest.cpp
```
Run: `cmake --build out --target tankoban_tests`
Expected: FAIL — `WesternLibrary.h` not found.

- [ ] **Step 3: Write the header**

```cpp
// src/core/manga/WesternLibrary.h
#pragma once
#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QJsonObject>
#include <QList>
#include <optional>

class JsonStore;

namespace tankoban::manga {

// One "my western series" record. Slug-keyed (readallcomics/RCO slug).
struct WesternLibraryRecord {
    QString seriesId;   // e.g. "invincible"
    QString title;      // "Invincible"
    QString coverUrl;   // remote cover URL (may be empty)
    qint64  addedAt = 0; // epoch ms; set by caller (keeps the store deterministic)

    QJsonObject toJson() const;
    static WesternLibraryRecord fromJson(const QJsonObject& o);
};

// Per-user store of added western series. Authoritative answer to "is this
// western series in my library?" — distinct from the shipped read-only
// catalogue (data/western_catalogue/). JsonStore file: western_library.json.
// nullptr store => in-memory only (unit tests), mirrors MangaDownloadIndex.
class WesternLibrary : public QObject {
    Q_OBJECT
public:
    explicit WesternLibrary(JsonStore* store, QObject* parent = nullptr);

    void addOrUpdate(const WesternLibraryRecord& rec); // idempotent by seriesId
    void remove(const QString& seriesId);
    bool contains(const QString& seriesId) const;
    std::optional<WesternLibraryRecord> get(const QString& seriesId) const;
    QList<WesternLibraryRecord> all() const;

signals:
    void libraryChanged();

private:
    void load();
    void save();

    JsonStore* m_store;
    mutable QMutex m_mutex;
    QHash<QString, WesternLibraryRecord> m_byId; // seriesId -> record

    static constexpr const char* FILENAME = "western_library.json";
    static constexpr int kSchemaVersion = 1;
};

} // namespace tankoban::manga
```

- [ ] **Step 4: Write the implementation**

```cpp
// src/core/manga/WesternLibrary.cpp
#include "WesternLibrary.h"
#include "core/JsonStore.h"
#include <QJsonArray>

namespace tankoban::manga {

QJsonObject WesternLibraryRecord::toJson() const {
    QJsonObject o;
    o["seriesId"] = seriesId;
    o["title"]    = title;
    o["coverUrl"] = coverUrl;
    o["addedAt"]  = addedAt;
    return o;
}

WesternLibraryRecord WesternLibraryRecord::fromJson(const QJsonObject& o) {
    WesternLibraryRecord r;
    r.seriesId = o.value("seriesId").toString();
    r.title    = o.value("title").toString();
    r.coverUrl = o.value("coverUrl").toString();
    r.addedAt  = o.value("addedAt").toVariant().toLongLong();
    return r;
}

WesternLibrary::WesternLibrary(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store) { load(); }

void WesternLibrary::load() {
    if (!m_store) return;
    const QJsonObject root = m_store->read(FILENAME);
    if (root.isEmpty()) return;
    const QJsonArray records = root.value("records").toArray();
    for (const auto& v : records) {
        const auto r = WesternLibraryRecord::fromJson(v.toObject());
        if (!r.seriesId.isEmpty()) m_byId.insert(r.seriesId, r);
    }
}

void WesternLibrary::save() {
    if (!m_store) return;
    QJsonArray arr;
    {
        QMutexLocker lk(&m_mutex);
        for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it)
            arr.append(it.value().toJson());
    }
    QJsonObject root;
    root["schemaVersion"] = kSchemaVersion;
    root["records"]       = arr;
    m_store->write(FILENAME, root);
}

void WesternLibrary::addOrUpdate(const WesternLibraryRecord& rec) {
    if (rec.seriesId.isEmpty()) return;
    {
        QMutexLocker lk(&m_mutex);
        m_byId.insert(rec.seriesId, rec); // insert replaces by key
    }
    save();
    emit libraryChanged();
}

void WesternLibrary::remove(const QString& seriesId) {
    {
        QMutexLocker lk(&m_mutex);
        m_byId.remove(seriesId);
    }
    save();
    emit libraryChanged();
}

bool WesternLibrary::contains(const QString& seriesId) const {
    QMutexLocker lk(&m_mutex);
    return m_byId.contains(seriesId);
}

std::optional<WesternLibraryRecord> WesternLibrary::get(const QString& seriesId) const {
    QMutexLocker lk(&m_mutex);
    const auto it = m_byId.constFind(seriesId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

QList<WesternLibraryRecord> WesternLibrary::all() const {
    QMutexLocker lk(&m_mutex);
    return m_byId.values();
}

} // namespace tankoban::manga
```

- [ ] **Step 5: Add to the main app target**

In `CMakeLists.txt`, find the `qt_add_executable(Tankoban` source list (~:48) and add near the other `src/core/manga/*.cpp` entries:
```cmake
    src/core/manga/WesternLibrary.cpp
```

- [ ] **Step 6: Build + run tests, verify PASS**

Run: `cmake --build out --target tankoban_tests; cd out; ctest --output-on-failure -R WesternLibrary`
Expected: PASS (4/4).

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/WesternLibrary.h src/core/manga/WesternLibrary.cpp tests/core/manga/WesternLibraryTest.cpp cmake/TankobanTests.cmake CMakeLists.txt
git commit -m "feat(comics): WesternLibrary store — per-user added-series source of truth (WESTERN_PARITY P1 T2)"
```

---

### Task 3: Construct `WesternLibrary` in ComicsPage + My Library render

Replace the catalogue dump with a store-driven My Library grid + bare empty state.

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (member + method decls), `src/ui/pages/ComicsPage.cpp` (construct store; `refreshWesternLibrary()`; repoint `buildWesternScreen`/callers)

- [ ] **Step 1: Add the member + include**

In `ComicsPage.h`, near the other manga forward-decls / members:
```cpp
// forward-decl block (namespace tankoban::manga):
    class WesternLibrary;
```
Add a member (near `m_tyLibrary`):
```cpp
    tankoban::manga::WesternLibrary* m_westernLibrary = nullptr;
    QLabel* m_westernEmptyLabel = nullptr;
```
Add method decls (near `refreshWesternGrid`):
```cpp
    void refreshWesternLibrary();
```
Add include at top of `ComicsPage.cpp`:
```cpp
#include "core/manga/WesternLibrary.h"
```

- [ ] **Step 2: Construct the store**

In the ComicsPage ctor, near where `m_mangaDownloadIndex = new MangaDownloadIndex(&m_bridge->store(), this);` is created (`ComicsPage.cpp:384`), add:
```cpp
    m_westernLibrary = new tankoban::manga::WesternLibrary(&m_bridge->store(), this);
    connect(m_westernLibrary, &tankoban::manga::WesternLibrary::libraryChanged,
            this, [this]() { refreshWesternLibrary(); });
```

- [ ] **Step 3: Add the empty-state label in `buildWesternScreen`**

In `buildWesternScreen` (`ComicsPage.cpp:2860`), after the search row is added and before `m_westernGrid` is added, insert:
```cpp
    m_westernEmptyLabel = new QLabel(tr("Search to find comics"), page);
    m_westernEmptyLabel->setAlignment(Qt::AlignCenter);
    m_westernEmptyLabel->setStyleSheet("color: rgba(255,255,255,0.45); font-size: 14px; padding: 40px;");
    m_westernEmptyLabel->hide();
    v->addWidget(m_westernEmptyLabel);
```

- [ ] **Step 4: Write `refreshWesternLibrary()` (replaces refreshWesternGrid's data source)**

Add a new method (place next to `refreshWesternGrid`, ~:2904):
```cpp
void ComicsPage::refreshWesternLibrary()
{
    if (!m_westernGrid) return;
    m_westernGrid->clear();

    const auto records = m_westernLibrary ? m_westernLibrary->all()
                                          : QList<tankoban::manga::WesternLibraryRecord>{};
    for (const auto& r : records) {
        auto* card = new TileCard(QString(), r.title, tr("Western"));
        const QString jsonPath =
            QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                .absoluteFilePath(r.seriesId + QStringLiteral(".json"));
        card->setProperty("westernJsonPath", jsonPath); // curated enrichment if present
        card->setProperty("westernSeriesId", r.seriesId);
        card->setProperty("seriesName", r.title);
        m_westernGrid->addTile(card);
        if (!r.coverUrl.isEmpty()) fetchPosterForTile(card, 0, r.coverUrl);
    }

    const bool empty = records.isEmpty();
    if (m_westernEmptyLabel) m_westernEmptyLabel->setVisible(empty);
    m_westernGrid->setVisible(!empty);
}
```

- [ ] **Step 5: Repoint every `refreshWesternGrid()` call to `refreshWesternLibrary()`**

Replace the 4 call sites (`ComicsPage.cpp:565, 1092, 2996`, and any others — grep `refreshWesternGrid()`), AND the single-click handler in `buildWesternScreen` that opens by `westernJsonPath`. Keep `openWesternSeriesFromJson` for curated, but the My Library tile may have no curated json — so the click must fall back to opening by stored record. Update the single-click handler (`ComicsPage.cpp:2895`):
```cpp
    connect(m_westernGrid, &TileStrip::tileSingleClicked, this, [this](TileCard* card) {
        if (!card) return;
        const QString jsonPath = card->property("westernJsonPath").toString();
        if (!jsonPath.isEmpty() && QFile::exists(jsonPath)) {
            openWesternSeriesFromJson(jsonPath);   // curated enrichment path
            return;
        }
        // No curated json — open from the stored library record (live issues).
        const QString seriesId = card->property("westernSeriesId").toString();
        if (!seriesId.isEmpty()) openWesternSeriesFromLibrary(seriesId);
    });
```
Leave `refreshWesternGrid()` defined for now (Task 5 removes it) but stop calling it.

- [ ] **Step 6: Add `openWesternSeriesFromLibrary` (header-only catalog from the record)**

`MangaCatalog` needs `seriesId`/`seriesTitle`/`seriesCover` to drive the header; issues come live. Add:
```cpp
void ComicsPage::openWesternSeriesFromLibrary(const QString& seriesId)
{
    if (!m_westernLibrary) return;
    const auto recOpt = m_westernLibrary->get(seriesId);
    if (!recOpt) return;
    tankoban::manga::MangaCatalog cat;
    cat.seriesId    = recOpt->seriesId;
    cat.seriesTitle = recOpt->title;
    cat.seriesCover = recOpt->coverUrl;
    m_pendingWesternJson     = {};          // no baked json; live issues only
    m_pendingWesternSeriesId = recOpt->seriesId;
    openWesternSeriesFromCatalog(cat, QString(), /*onShelf*/true);
}
```
Declare it in `ComicsPage.h` next to `openWesternSeriesFromJson`.

- [ ] **Step 7: Build (compile-only) + dev-bridge smoke**

Run: `.\build_check.bat` → Expected: `BUILD OK`.
Then `build_and_run.bat`; with a CLEAN western library (no downloads yet):
`out\tankoctl.exe comics-get-state` after opening Comics→Western — Expected: My Library empty, empty-label visible, NO 14-series dump.

- [ ] **Step 8: Commit**

```bash
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): Western My Library grid from WesternLibrary store + empty state (WESTERN_PARITY P1 T3)"
```

---

### Task 4: Auto-add on download

When a readallcomics issue completes, add its series to the library.

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (`onProviderVolumeCompleted`, WesternGetComics branch, ~:2450-2490)

- [ ] **Step 1: Add the library write in the WesternGetComics branch**

In `onProviderVolumeCompleted`, after the `registerVolume(...)` call and inside the `WesternGetComics` handling, add:
```cpp
    if (kind == PendingVolumeSourceKind::WesternGetComics && m_westernLibrary
        && !m_pendingWesternSeriesId.isEmpty()) {
        tankoban::manga::WesternLibraryRecord r;
        r.seriesId = m_pendingWesternSeriesId;
        r.title    = m_currentDetailSeriesTitle.isEmpty()
                       ? m_pendingWesternSeriesId : m_currentDetailSeriesTitle;
        r.coverUrl = m_currentWesternSeriesCover; // see Step 2
        r.addedAt  = QDateTime::currentMSecsSinceEpoch();
        m_westernLibrary->addOrUpdate(r); // fires libraryChanged → refreshWesternLibrary
    }
```

- [ ] **Step 2: Track the open series' cover**

`m_currentWesternSeriesCover` must hold the cover of the series currently open. In `openWesternSeriesFromCatalog` (`ComicsPage.cpp:2908`), set it from the catalog:
```cpp
    m_currentWesternSeriesCover = catalog.seriesCover;
```
Declare `QString m_currentWesternSeriesCover;` in `ComicsPage.h` near `m_pendingWesternSeriesId`.

- [ ] **Step 3: Build + smoke (auto-add)**

Run: `.\build_check.bat` → `BUILD OK`. Then `build_and_run.bat`:
- Comics→Western→search "Invincible"→open→download issue #1.
- `out\tankoctl.exe comics-get-downloads` shows a `readallcomics` completed record.
- Return to Western tab → **Invincible appears in My Library; the other 13 do NOT.**

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): download a western issue auto-adds its series to My Library (WESTERN_PARITY P1 T4)"
```

---

### Task 5: `+ Add to Library` button repoint + retire catalogue dump

Repoint the explicit add to write the store (not bake a catalogue json), and make `onShelf` read the store. Then delete the now-dead `refreshWesternGrid`.

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (`addWesternToLibraryRequested` handler ~:516-565; `onShelf` derivation ~:319; remove `refreshWesternGrid`)

- [ ] **Step 1: Replace the add-to-shelf handler body**

Replace the lambda body connected to `addWesternToLibraryRequested` (the whole `data/western_catalogue` write block, `ComicsPage.cpp:516-565`) with:
```cpp
    connect(m_tyVolumeSeriesView,
            &tankoban::manga::comics::ComicsSeriesView::addWesternToLibraryRequested,
            this, [this]() {
        if (m_pendingWesternSeriesId.isEmpty() || !m_westernLibrary) return;
        tankoban::manga::WesternLibraryRecord r;
        r.seriesId = m_pendingWesternSeriesId;
        r.title    = m_currentDetailSeriesTitle.isEmpty()
                       ? m_pendingWesternSeriesId : m_currentDetailSeriesTitle;
        r.coverUrl = m_currentWesternSeriesCover;
        r.addedAt  = QDateTime::currentMSecsSinceEpoch();
        m_westernLibrary->addOrUpdate(r);
        if (m_tyVolumeSeriesView) m_tyVolumeSeriesView->setWesternOnShelf(true);
    });
```

- [ ] **Step 2: Repoint the `onShelf` derivation**

At `ComicsPage.cpp:317-321`, replace the "file exists in catalogue dir" check:
```cpp
            const bool onShelf = m_westernLibrary
                                 && m_westernLibrary->contains(catalog.seriesId);
            openWesternSeriesFromCatalog(catalog, QString(), onShelf);
```
Do the same for the other `QFile::exists(shelfPath)` shelf checks for western (grep `shelfPath`).

- [ ] **Step 3: Delete the dead `refreshWesternGrid()`**

Remove the `refreshWesternGrid()` definition (`ComicsPage.cpp:2904-2942`) and its declaration in `ComicsPage.h`. Confirm no remaining callers: `grep -n "refreshWesternGrid" src/ui/pages/ComicsPage.cpp` → no hits.

- [ ] **Step 4: Build + smoke (+Add without download)**

Run: `.\build_check.bat` → `BUILD OK`. Then `build_and_run.bat`:
- Western→search "Saga"→open→click **+ Add to Library** (do NOT download).
- Return to Western → **Saga is in My Library**; button reads on-shelf when reopened.

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): +Add writes WesternLibrary, onShelf reads store, retire catalogue dump (WESTERN_PARITY P1 T5)"
```

---

### Task 6: Phase-1 reconcile — back-fill orphaned downloads

A series downloaded before this arc has a `readallcomics` `MangaDownloadIndex` entry but no library record. Back-fill on refresh so it isn't orphaned.

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (`refreshWesternLibrary`)

- [ ] **Step 1: Add the back-fill pass at the top of `refreshWesternLibrary()`**

Before reading `m_westernLibrary->all()`:
```cpp
    // Back-fill: any readallcomics download lacking a library record gets one
    // (handles series downloaded before this arc existed).
    if (m_westernLibrary && m_mangaDownloadIndex) {
        for (const auto& e : m_mangaDownloadIndex->entriesForAllSeries()) {
            if (e.sourceId != QLatin1String("readallcomics")) continue;
            if (m_westernLibrary->contains(e.seriesId)) continue;
            tankoban::manga::WesternLibraryRecord r;
            r.seriesId = e.seriesId;
            r.title    = resolveDisplayTitle(e.sourceId, e.seriesId);
            if (r.title.isEmpty()) r.title = humanizeSlug(e.seriesId);
            r.addedAt  = QDateTime::currentMSecsSinceEpoch();
            m_westernLibrary->addOrUpdate(r);
        }
    }
```
(Guard against recursion: `addOrUpdate` emits `libraryChanged` → `refreshWesternLibrary`; the `contains` check makes the second pass a no-op, so it converges in one extra call.)

- [ ] **Step 2: Build + smoke**

Run: `.\build_check.bat` → `BUILD OK`. `build_and_run.bat` with an existing readallcomics download present → that series shows in My Library after one refresh.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): reconcile orphaned readallcomics downloads into My Library (WESTERN_PARITY P1 T6)"
```

---

## Phase 2 — Continue Reading (western, own strip)

### Task 7: Western Continue Reading strip

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (members + decls), `src/ui/pages/ComicsPage.cpp` (`buildWesternScreen`; new `refreshWesternContinueStrip()`; `ensureWesternIssueInMap`; call on open + on reader-close)

- [ ] **Step 1: Add members**

In `ComicsPage.h`:
```cpp
    QWidget*   m_westernContinueSection = nullptr;
    TileStrip* m_westernContinueStrip   = nullptr;
    QMap<QString, FileRef> m_westernProgressKeyMap; // western issue progress-key -> file
```
Decls:
```cpp
    void refreshWesternContinueStrip();
    void ensureWesternIssueInMap(const QString& cbzPath);
```

- [ ] **Step 2: Build the section in `buildWesternScreen`**

Insert ABOVE the empty label + grid (so order is: search, CONTINUE READING, MY LIBRARY):
```cpp
    m_westernContinueSection = new QWidget(page);
    auto* wcLayout = new QVBoxLayout(m_westernContinueSection);
    wcLayout->setContentsMargins(0, 0, 0, 0);
    wcLayout->setSpacing(8);
    auto* wcLabel = new QLabel(tr("CONTINUE READING"), m_westernContinueSection);
    wcLabel->setStyleSheet("color: rgba(255,255,255,0.6); font-size: 12px; font-weight: 600; letter-spacing: 1px;");
    wcLayout->addWidget(wcLabel);
    m_westernContinueStrip = new TileStrip(m_westernContinueSection);
    m_westernContinueStrip->setMode(QStringLiteral("continue"));
    wcLayout->addWidget(m_westernContinueStrip);
    m_westernContinueSection->hide();
    v->addWidget(m_westernContinueSection);
```

- [ ] **Step 3: Write `refreshWesternContinueStrip()`**

Mirror `refreshContinueStrip` but INCLUDE only western issues (use `isWesternIssueCbz`), dedup per series, cap 40. Add `#include "core/manga/WesternIssueKey.h"`.
```cpp
void ComicsPage::refreshWesternContinueStrip()
{
    if (!m_westernContinueStrip) return;
    m_westernContinueStrip->clear();

    const QJsonObject allProg = m_bridge->allProgress("comics");
    struct WItem { qint64 updatedAt; QString filePath, seriesPath, title, subtitle, coverUrl; };
    QList<WItem> items;
    for (auto it = allProg.begin(); it != allProg.end(); ++it) {
        const QJsonObject prog = it.value().toObject();
        if (prog.value("finished").toBool()) continue;
        const int page = prog.value("page").toInt(0);
        if (page < 0) continue;
        const auto ref = m_westernProgressKeyMap.find(it.key());
        if (ref == m_westernProgressKeyMap.end()) continue;
        const QString base = QFileInfo(ref->filePath).completeBaseName();
        if (!tankoban::manga::isWesternIssueCbz(base)) continue;
        const int issueNo = tankoban::manga::westernIssueNumber(base);
        const int pageCount = prog.value("pageCount").toInt(0);
        const QString seriesName = QDir(ref->seriesPath).dirName();
        const QString pageLabel = pageCount > 0
            ? QStringLiteral("Page %1/%2").arg(page + 1).arg(pageCount)
            : QStringLiteral("Page %1").arg(page + 1);
        items.append({ prog.value("updatedAt").toVariant().toLongLong(),
                       ref->filePath, ref->seriesPath, seriesName,
                       QStringLiteral("Issue %1 · %2").arg(issueNo).arg(pageLabel),
                       ref->coverPath });
    }
    if (items.isEmpty()) { m_westernContinueSection->hide(); return; }

    QMap<QString, int> bestPerSeries; // seriesPath -> index (most recent)
    for (int i = 0; i < items.size(); ++i) {
        auto it = bestPerSeries.find(items[i].seriesPath);
        if (it == bestPerSeries.end() || items[i].updatedAt > items[it.value()].updatedAt)
            bestPerSeries[items[i].seriesPath] = i;
    }
    QList<WItem> deduped;
    for (int idx : bestPerSeries) deduped.append(items[idx]);
    std::sort(deduped.begin(), deduped.end(),
              [](const WItem& a, const WItem& b){ return a.updatedAt > b.updatedAt; });
    if (deduped.size() > 40) deduped = deduped.mid(0, 40);

    for (const auto& w : deduped) {
        auto* card = new TileCard(QString(), w.title, w.subtitle);
        card->setProperty("filePath", w.filePath);
        card->setProperty("seriesPath", w.seriesPath);
        card->setProperty("seriesName", w.title);
        connect(card, &TileCard::clicked, this, [this, card]() {
            const QString path = card->property("filePath").toString();
            const QString seriesPath = card->property("seriesPath").toString();
            const QString seriesName = card->property("seriesName").toString();
            QDir dir(seriesPath);
            QStringList files = dir.entryList(COMIC_EXTS, QDir::Files);
            QCollator col; col.setNumericMode(true);
            std::sort(files.begin(), files.end(),
                      [&col](const QString& a, const QString& b){ return col.compare(a,b) < 0; });
            QStringList cbzList;
            for (const auto& f : files) cbzList.append(dir.absoluteFilePath(f));
            emit openComic(path, cbzList, seriesName);
        });
        m_westernContinueStrip->addTile(card);
        if (!w.coverUrl.isEmpty()) fetchPosterForTile(card, 0, w.coverUrl);
    }
    m_westernContinueSection->show();
}
```

- [ ] **Step 4: Write `ensureWesternIssueInMap` + register on open**

```cpp
void ComicsPage::ensureWesternIssueInMap(const QString& cbzPath)
{
    if (cbzPath.isEmpty()) return;
    const QString key = comicProgressKeyForPath(cbzPath);
    if (m_westernProgressKeyMap.contains(key)) return;
    const QString parentDir = QFileInfo(cbzPath).absolutePath();
    QString coverUrl;
    if (m_westernLibrary && !m_pendingWesternSeriesId.isEmpty()) {
        if (const auto r = m_westernLibrary->get(m_pendingWesternSeriesId))
            coverUrl = r->coverUrl;
    }
    m_westernProgressKeyMap[key] = { cbzPath, parentDir, coverUrl };
}
```
In the western download/open path where the issue cbz is opened to read — the `downloadWesternEditionRequested` completion already opens to read, and the issue-tile click path emits `openComic`. Register right before those `emit openComic(...)` for western. Simplest: in the `tileSingleClicked`/issue-open handler for the western series view, call `ensureWesternIssueInMap(cbzPath)` before `emit openComic`. Add `#include "ui/readers/comic_progress_key.h"` if not present.

- [ ] **Step 5: Refresh western continue when the reader returns + on series open**

`refreshContinueStrip()` is already public and called on reader-close (MainWindow). Add a sibling call. In `ComicsPage.h` make `refreshWesternContinueStrip()` reachable, and in `refreshContinueStrip()` body (or the reader-return path) also call `refreshWesternContinueStrip();`. Also call it at the end of `buildWesternScreen` and when switching to the Western tab (`showWesternMode`).

- [ ] **Step 6: Build + smoke**

Run: `.\build_check.bat` → `BUILD OK`. `build_and_run.bat`:
- Download + read an Invincible issue → back out → **Western Continue Reading shows Invincible · Issue N · page**.
- Switch to Manga tab → **Manga Continue Reading has NO western issues.**

- [ ] **Step 7: Commit**

```bash
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): Western Continue Reading strip (own, manga stays clean) (WESTERN_PARITY P2 T7)"
```

---

## Phase 3 — Live search + curated enrichment

### Task 8: Repoint western search to live readallcomics

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (`buildWesternScreen` search sourceId ~:2832); verify `showSearchMode` routing

- [ ] **Step 1: Trace the search routing (verify before changing)**

Run: `grep -n "setActiveSourceId\|showSearchMode\|m_searchTakeover\|readallcomics\|readcomicsonline" src/ui/pages/ComicsPage.cpp src/ui/pages/comics/*.cpp`
Confirm how `setActiveSourceId(sid)` selects the scraper. Expected: the takeover resolves the scraper by sourceId from `m_sourceRegistry`. If `"readallcomics"` is registered (it is — `ComicsPage.cpp:292`), changing the sid is sufficient. Document the finding inline in the commit.

- [ ] **Step 2: Change the western search source**

In `buildWesternScreen` (`ComicsPage.cpp:2829-2833`):
```cpp
        QWidget* westernSearchRow = buildSearchRow(
            m_westernSearchBar, m_westernSearchBusy, m_westernSearchBtn,
            QStringLiteral("Search Comics"),
            QStringLiteral("readallcomics"));   // was "readcomicsonline" (CF-locked, dead)
```

- [ ] **Step 3: Build + smoke (live search)**

Run: `.\build_check.bat` → `BUILD OK`. `build_and_run.bat`:
- Western→type "Saga"→Enter → results from live readallcomics (not empty/dead).
- Open a result → issues render → download → lands in My Library.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): Western search hits live readallcomics (was dead readcomicsonline) (WESTERN_PARITY P3 T8)"
```

---

### Task 9: Curated enrichment on open

When opening a searched series whose slug matches a shipped curated file, use the richer curated cover/synopsis.

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (`openWesternSeriesFromCatalog` or `openWesternSeriesFromLibrary`)

- [ ] **Step 1: Add the enrichment merge**

In `openWesternSeriesFromLibrary` (and the search-open path that builds a catalog from readallcomics metadata), before `openWesternSeriesFromCatalog`, merge curated art if present:
```cpp
    // Curated enrichment: richer cover/synopsis for one of the shipped 14.
    const QString curatedPath =
        QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
            .absoluteFilePath(cat.seriesId + QStringLiteral(".json"));
    if (QFile::exists(curatedPath)) {
        if (const auto curated = tankoban::manga::WesternCatalogLoader::loadFromFile(curatedPath)) {
            if (!curated->seriesCover.isEmpty())    cat.seriesCover    = curated->seriesCover;
            if (!curated->seriesSynopsis.isEmpty()) cat.seriesSynopsis = curated->seriesSynopsis;
        }
    }
```

- [ ] **Step 2: Build + smoke (enrichment)**

Run: `.\build_check.bat` → `BUILD OK`. `build_and_run.bat`:
- Open Invincible (curated) → hero uses the curated cover/synopsis.
- Open a non-curated searched series → uses readallcomics metadata, no crash.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "feat(comics): curated cover/synopsis enrichment when opening a shipped western series (WESTERN_PARITY P3 T9)"
```

---

### Task 10: Regression-guard test (manga stays clean)

Assert western issues never enter the manga strips — locks the bug I introduced + reverted this session.

**Files:**
- Create: `tests/core/manga/WesternMangaIsolationTest.cpp`
- Modify: `cmake/TankobanTests.cmake`

- [ ] **Step 1: Write the test (classifier-level invariant)**

```cpp
// tests/core/manga/WesternMangaIsolationTest.cpp
#include <gtest/gtest.h>
#include "core/manga/WesternIssueKey.h"

using tankoban::manga::isWesternIssueCbz;

// The manga Continue strip EXCLUDES western issues; the western strip INCLUDES
// them. Both branch on this one predicate, so its contract IS the isolation.
TEST(WesternMangaIsolation, MangaVolumesNeverClassifiedWestern) {
    for (const char* manga : {"One Piece v114", "Death Note Volume 1",
                              "Volume X", "Berserk v40", "Naruto vol 72"})
        EXPECT_FALSE(isWesternIssueCbz(manga)) << manga;
}
TEST(WesternMangaIsolation, WesternIssuesNeverClassifiedManga) {
    for (const char* w : {"Invincible #1", "Saga #54", "Watchmen #12"})
        EXPECT_TRUE(isWesternIssueCbz(w)) << w;
}
```

- [ ] **Step 2: Register + build + run, verify PASS**

Add to `cmake/TankobanTests.cmake`:
```cmake
        tests/core/manga/WesternMangaIsolationTest.cpp
```
Run: `cmake --build out --target tankoban_tests; cd out; ctest --output-on-failure -R WesternMangaIsolation`
Expected: PASS (2/2).

- [ ] **Step 3: Commit**

```bash
git add tests/core/manga/WesternMangaIsolationTest.cpp cmake/TankobanTests.cmake
git commit -m "test(comics): western/manga strip isolation invariant (WESTERN_PARITY P3 T10)"
```

---

## Final verification (Definition of Done)

- [ ] `cd out; ctest --output-on-failure -R "WesternLibrary|WesternIssueKey|WesternMangaIsolation"` → all PASS.
- [ ] `.\build_check.bat` → `BUILD OK`.
- [ ] **Cross-engine review** (producer ≠ reviewer): route the ComicsPage diff through `/codex-review` against this plan's DoD before declaring done.
- [ ] **Hemanth visual smoke** (the gate): empty Western tab shows "Search to find comics"; search "Invincible" live → download #1 → **only Invincible** in My Library; read it → Western Continue Reading shows it; Manga tab clean; **+ Add** shelves without downloading.

## Self-review notes
- **Spec coverage:** My Library (T3/T5), auto-add (T4), +Add (T5), reconcile (T6, spec edge-handling), Continue Reading (T7), live search (T8), enrichment (T9), manga isolation (T10 + preserved filters), empty state (T3). All spec sections mapped.
- **Type consistency:** `WesternLibraryRecord{seriesId,title,coverUrl,addedAt}` and `WesternLibrary::{addOrUpdate,remove,contains,get,all}` used identically across T2–T9. `isWesternIssueCbz`/`westernIssueNumber` signatures match T1↔T7↔T10.
- **Open risk flagged for executor:** T8 Step 1 is a *verify-then-change* — if `showSearchMode` does NOT resolve the scraper by sourceId, the executor must wire `"readallcomics"` into the takeover before Step 2 (don't assume).
