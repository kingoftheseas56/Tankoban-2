# Downloads Page Redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the raw torrent filenames on the Downloads page with clean title-only rows + a show poster, in the locked side-poster layout, keeping the Active + History split.

**Architecture:** Read-time metadata enrichment. The page renders immediately with cleaned-filename placeholders, then asynchronously enriches each show group via `MetaAggregator::fetchMetaItem(imdbId, "series")` — one call yields both the poster (`MetaItem::preview.poster`) and per-episode titles (`MetaItem::videos[].title` keyed by `seriesInfo{season,episode}`). Posters reuse the existing on-disk cache at `…/Tankoban/data/stream_posters/<imdbId>`. Nothing is persisted to `StreamDownloadIndex` — enrichment is cosmetic and backfills existing history uniformly.

**Tech Stack:** C++17, Qt6 (QFrame/QLabel/QVBoxLayout/QHBoxLayout/QNetworkAccessManager/QPixmap), existing `MetaAggregator` + `StreamDownloadIndex`.

**Scope:** This plan covers the **Theatre** Downloads page (`StreamDownloadsPage`, Agent 4 domain). The **Comics** Downloads page (`ComicsDownloadsPage`) is Agent 1 domain (`src/ui/pages/comics/`, standing polish-mode, Rule 14) — handed off as a mirror brief in the final section, not executed here.

**Reference design:** spec `docs/superpowers/specs/2026-05-29-downloads-page-redesign-design.md`; mock-up `docs/superpowers/mockups/2026-05-29-downloads-page-redesign/downloads-redesign.html` (side-poster tab).

**Verification model:** This is Qt UI — no `tankoban_tests` unit coverage (those are pure-logic only). Each phase gates on `build_check.bat` (BUILD OK) + a targeted smoke via the running app / `tankoctl stream-get-downloads`. Commit per phase (RTC line for Agent 0 sweep on shared master, per Rule 11).

---

## File Structure

| File | Responsibility | Action |
|------|----------------|--------|
| `src/ui/pages/StreamPage.h` / `.cpp` | Owns `m_metaAggregator`; expose it to MainWindow | Modify — add `metaAggregator()` getter |
| `src/ui/MainWindow.cpp` | Constructs + wires `m_streamDownloadsPage` | Modify — inject meta aggregator |
| `src/ui/pages/stream/StreamDownloadsPage.h` / `.cpp` | The Downloads page UI | Modify — meta injection, side-poster cards, enrichment |

No new files. All work lands in the three existing units above.

---

## Task 0: Plumb the metadata provider into the Downloads page

**Files:**
- Modify: `src/ui/pages/StreamPage.h` (add getter declaration)
- Modify: `src/ui/pages/StreamPage.cpp` (define getter; `m_metaAggregator` created at line 298)
- Modify: `src/ui/pages/stream/StreamDownloadsPage.h` (forward-declare + member + setter)
- Modify: `src/ui/pages/stream/StreamDownloadsPage.cpp` (define setter)
- Modify: `src/ui/MainWindow.cpp:903-915` (inject after the existing `setStreamDownloadIndex` wire-up)

- [ ] **Step 1: Add the getter to StreamPage.** In `StreamPage.h`, in the public section near other accessors (e.g. alongside `streamDownloadIndex()`), add:

```cpp
tankostream::stream::MetaAggregator* metaAggregator() const { return m_metaAggregator; }
```

- [ ] **Step 2: Add the injection point to StreamDownloadsPage.h.** Add the forward declaration next to the existing ones (line 22-23) and the setter + member:

```cpp
// forward declarations (with QLabel etc.)
namespace tankostream::stream { class MetaAggregator; }
```
```cpp
// public, after setStreamDownloadIndex(...)
void setMetaAggregator(tankostream::stream::MetaAggregator* agg);
```
```cpp
// private members, after m_streamDownloadIndex
tankostream::stream::MetaAggregator* m_metaAggregator = nullptr;
```

- [ ] **Step 3: Define the setter in StreamDownloadsPage.cpp.** Add near `setStreamDownloadIndex` (line ~202). Connect `metaItemReady` ONCE here (handler added in Task 3; for now an empty lambda stub so it compiles):

```cpp
void StreamDownloadsPage::setMetaAggregator(tankostream::stream::MetaAggregator* agg)
{
    if (m_metaAggregator == agg)
        return;
    if (m_metaAggregator)
        disconnect(m_metaAggregator, nullptr, this, nullptr);
    m_metaAggregator = agg;
    // metaItemReady handler wired in Task 3.
}
```
Add `#include "core/stream/MetaAggregator.h"` to the .cpp includes.

- [ ] **Step 4: Wire it in MainWindow.** After `StreamDownloadsPage.cpp` line 907 (`setStreamDownloadIndex(...)`), inside the existing `if (m_streamPage)` block, add:

```cpp
        m_streamDownloadsPage->setMetaAggregator(m_streamPage->metaAggregator());
```

- [ ] **Step 5: Confirm `fetchMetaItem` is reentrant.** Read `MetaAggregator::fetchMetaItem` in `MetaAggregator.cpp`. Verify it uses its own transport/state and does NOT touch the `m_seriesResolved` / `m_seriesPendingImdb` single-shot state machine used by `fetchSeriesMeta` (so driving it from the Downloads page can't corrupt an in-flight detail-view fetch). If it DOES share that state, stop and note it — Task 3 will instead read only `MetaAggregator`'s warm series cache and skip on-demand fetch. (Expected: `fetchMetaItem` is independent, like `searchByTitle`.)

- [ ] **Step 6: Build + commit.** Run `build_check.bat` → expect `BUILD OK`. Commit: `feat(stream-downloads): plumb MetaAggregator into Downloads page` (or RTC line).

---

## Task 1: Side-poster card skeleton + title-only History rows (placeholders, no meta yet)

Replace the current filename rows ([StreamDownloadsPage.cpp:405-464](../../../src/ui/pages/stream/StreamDownloadsPage.cpp#L405-L464)) with the side-poster card. Use placeholder titles (the existing `prettifyFilenameTitle`) so this task is independently verifiable before enrichment lands.

**Files:** Modify `src/ui/pages/stream/StreamDownloadsPage.cpp`.

- [ ] **Step 1: Add a per-imdb card-handle struct + map** to track widgets for later async repaint. At file scope (top of the .cpp, after the anon namespace) or as a private member type:

```cpp
struct DownloadCardRefs {
    QFrame*  card        = nullptr;
    QWidget* posterWidget= nullptr;   // QLabel painting the poster / placeholder
    QLabel*  titleLabel  = nullptr;   // show title
    QHash<QString, QLabel*> rowTitleByKey;  // key = "<season>:<episode>" -> episode-title label
};
```
Add member: `QHash<QString, DownloadCardRefs> m_historyCards;` (and clear it at the top of `refreshHistory`, before the layout-clear loop).

- [ ] **Step 2: Add a poster-widget factory** (placeholder now; real pixmap in Task 2). Returns a fixed 96×144 QLabel:

```cpp
QWidget* StreamDownloadsPage::makePosterWidget(const QString& imdbId, const QString& title)
{
    auto* pl = new QLabel(m_historyBody);
    pl->setObjectName("StreamDownloadsPoster");
    pl->setFixedSize(96, 144);
    pl->setScaledContents(true);
    pl->setAlignment(Qt::AlignCenter);
    pl->setStyleSheet(
        "QLabel#StreamDownloadsPoster {"
        "  border-top-left-radius:12px; border-bottom-left-radius:12px;"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 rgba(255,255,255,0.10), stop:1 rgba(255,255,255,0.03));"
        "  color:rgba(255,255,255,0.45); font-size:9pt; padding:6px; }");
    pl->setText(title);  // placeholder: show title text until art loads
    return pl;
}
```
Declare it private in the .h: `QWidget* makePosterWidget(const QString& imdbId, const QString& title);`

- [ ] **Step 3: Rewrite the History card builder** inside `refreshHistory`'s per-imdb loop. Replace the current single-column `QFrame` (lines 405-460) with a horizontal poster+right-column card. Show title via `bestTitleFromEntries(entries, imdbId)` (existing). Rows are title-only: `S%1E%2 · <title>` where `<title>` starts as `prettifyFilenameTitle(e.canonicalPath)` and the episode code uses the existing `arg(e.season,2,10,'0')` formatting. Keep the existing click → `playLocalFileRequested(...)` connection verbatim. Store each row's title QLabel in `refs.rowTitleByKey["<season>:<episode>"]`, the poster in `refs.posterWidget`, the show label in `refs.titleLabel`, then `m_historyCards.insert(imdbId, refs)`.

  Card structure (QHBoxLayout: poster | right QVBoxLayout):
```cpp
auto* card = new QFrame(m_historyBody);
card->setObjectName("StreamDownloadsHistoryCard");
card->setStyleSheet("QFrame#StreamDownloadsHistoryCard{background:rgba(255,255,255,0.038);"
                    "border:1px solid rgba(255,255,255,0.08);border-radius:12px;}");
auto* h = new QHBoxLayout(card);
h->setContentsMargins(0,0,0,0); h->setSpacing(0);
DownloadCardRefs refs; refs.card = card;
refs.posterWidget = makePosterWidget(imdbId, showTitle);
h->addWidget(refs.posterWidget, 0, Qt::AlignTop);
auto* right = new QVBoxLayout(); right->setContentsMargins(16,13,16,11); right->setSpacing(3);
refs.titleLabel = new QLabel(showTitle, card);
refs.titleLabel->setStyleSheet("color:#ededed;font-size:15px;font-weight:600;");
right->addWidget(refs.titleLabel);
auto* sub = new QLabel(tr("%n episode(s)", "", entries.size()), card);
sub->setStyleSheet("color:rgba(255,255,255,0.55);font-size:12px;");
right->addWidget(sub);
// ... per-episode rows appended to `right`, each stored in refs.rowTitleByKey ...
h->addLayout(right, 1);
m_historyCards.insert(imdbId, refs);
```
  (Keep the existing movie-vs-series branch: movies get a single row, no `SxxExx` code.)

- [ ] **Step 4: Build + smoke.** `build_check.bat` → `BUILD OK`. Launch `build_and_run.bat`, open Downloads → cards now show a poster slot (grey, with the show title text) on the left and `S02E01 · <cleaned title>` rows on the right. No raw filenames. `tankoctl stream-get-downloads` group count unchanged.

- [ ] **Step 5: Commit.** `feat(stream-downloads): side-poster card + title-only rows (placeholder meta)`.

---

## Task 2: Real posters from disk cache + async fetch

**Files:** Modify `src/ui/pages/stream/StreamDownloadsPage.cpp` (+ `.h` for the NAM member + helper decls).

- [ ] **Step 1: Add a poster cache-dir helper + NAM member.** In the .h: `QNetworkAccessManager* m_posterNam = nullptr;`. In the .cpp, add (matching [StreamLibraryLayout.cpp:30-31](../../../src/ui/pages/stream/StreamLibraryLayout.cpp#L30-L31) and [StreamDetailView.cpp:2624](../../../src/ui/pages/stream/StreamDetailView.cpp#L2624)):

```cpp
static QString posterCachePath(const QString& imdbId)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/Tankoban/data/stream_posters/") + imdbId;
}
```
Includes: `<QStandardPaths>`, `<QFile>`, `<QPixmap>`, `<QNetworkAccessManager>`, `<QNetworkReply>`, `<QNetworkRequest>`, `<QDir>`.

- [ ] **Step 2: Paint cached poster in `makePosterWidget`.** Before falling back to the title-text placeholder, if `QFile::exists(posterCachePath(imdbId))`, load it into a QPixmap and `pl->setPixmap(pm)` (clear the text). This makes already-browsed shows show art instantly with zero network.

- [ ] **Step 3: Add `ensurePoster(imdbId)`** — called from the `metaItemReady` handler in Task 3 when the disk cache is missing. Downloads `preview.poster`, scales to 96×144, saves to `posterCachePath`, and repaints `m_historyCards[imdbId].posterWidget`:

```cpp
void StreamDownloadsPage::savePosterFrom(const QString& imdbId, const QUrl& posterUrl)
{
    if (posterUrl.isEmpty() || QFile::exists(posterCachePath(imdbId))) return;
    if (!m_posterNam) m_posterNam = new QNetworkAccessManager(this);
    QNetworkRequest req(posterUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_posterNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm; if (!pm.loadFromData(reply->readAll())) return;
        QDir().mkpath(QFileInfo(posterCachePath(imdbId)).absolutePath());
        pm.save(posterCachePath(imdbId), "PNG");
        auto it = m_historyCards.constFind(imdbId);
        if (it != m_historyCards.constEnd() && it->posterWidget) {
            auto* lbl = qobject_cast<QLabel*>(it->posterWidget);
            if (lbl) { lbl->setText(QString()); lbl->setPixmap(pm.scaled(96,144,
                          Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)); }
        }
    });
}
```
Declare `savePosterFrom` private in the .h.

- [ ] **Step 4: Build + commit.** `build_check.bat` → `BUILD OK`. Commit: `feat(stream-downloads): real posters from disk cache + async fetch`. (Visual smoke folds into Task 3.)

---

## Task 3: Episode-title + show-title enrichment via metaItemReady

**Files:** Modify `src/ui/pages/stream/StreamDownloadsPage.cpp` (+ `.h` for the handler decl).

- [ ] **Step 1: Trigger enrichment per show in `refreshHistory`.** After building all cards, for each distinct `imdbId` in `m_historyCards`, call `m_metaAggregator->fetchMetaItem(imdbId, QStringLiteral("series"))` (guard `if (m_metaAggregator)`). Dedup is automatic — one card per imdbId.

- [ ] **Step 2: Add the `metaItemReady` handler** and connect it in `setMetaAggregator` (replace the Task 0 stub). It filters by `item.preview.id`, updates the show title + poster, and rewrites matching row labels from `videos[]`:

```cpp
void StreamDownloadsPage::onMetaItemReady(const tankostream::addon::MetaItem& item)
{
    const QString imdbId = item.preview.id;
    auto it = m_historyCards.find(imdbId);
    if (it == m_historyCards.end()) return;
    DownloadCardRefs& refs = it.value();
    if (!item.preview.name.isEmpty() && refs.titleLabel)
        refs.titleLabel->setText(item.preview.name);
    savePosterFrom(imdbId, item.preview.poster);          // Task 2
    for (const tankostream::addon::Video& v : item.videos) {
        if (!v.seriesInfo.has_value() || v.title.isEmpty()) continue;
        const QString key = QStringLiteral("%1:%2")
            .arg(v.seriesInfo->season).arg(v.seriesInfo->episode);
        auto rit = refs.rowTitleByKey.constFind(key);
        if (rit != refs.rowTitleByKey.constEnd() && rit.value())
            rit.value()->setText(v.title);
    }
}
```
Wire in `setMetaAggregator`:
```cpp
connect(m_metaAggregator, &tankostream::stream::MetaAggregator::metaItemReady,
        this, &StreamDownloadsPage::onMetaItemReady, Qt::UniqueConnection);
```
Declare `onMetaItemReady` as a private slot; add `#include "core/stream/addon/MetaItem.h"`.

- [ ] **Step 3: Row label keying.** Confirm Task 1 stored each episode row's *title* QLabel (not the whole row widget) under the same `"<season>:<episode>"` key used here. The row visual is `[ecode][etitle]`; only the `etitle` label is swapped on enrichment, leaving `S02E01` intact.

- [ ] **Step 4: Build + smoke (the headline smoke).** `build_check.bat` → `BUILD OK`. `build_and_run.bat`, open Downloads:
  - Daredevil: Born Again → poster art + rows `S02E01 · <real episode title>` (titles arrive a beat after open, replacing the cleaned placeholder). No filename ever shown.
  - Community → same; titles match the catalog.
  - Toggle network off, fresh-boot a never-opened show → cleaned title + grey poster, never the raw filename.
  - Click a row → still plays (unchanged contract).

- [ ] **Step 5: Commit.** `feat(stream-downloads): enrich rows with real episode + show titles`.

---

## Task 4: Active section parity (poster + progress, same card shape)

**Files:** Modify `src/ui/pages/stream/StreamDownloadsPage.cpp`.

- [ ] **Step 1: Apply the side-poster card to `refreshActive`** ([lines 230-344](../../../src/ui/pages/stream/StreamDownloadsPage.cpp#L230-L344)). Reuse `makePosterWidget` + the same QHBoxLayout(poster|right) structure. Keep the existing per-season aggregation (`done/active/pending/failed`) but render it as: show title, a thin grayscale progress bar (`done/total` width), and the existing state line (`%1 active`, `%1 queued`, `%1 stuck`). Use a parallel `QHash<QString,DownloadCardRefs> m_activeCards` and trigger `fetchMetaItem` for active imdbIds too, so active cards get posters + real titles. The `onMetaItemReady` handler must also check `m_activeCards` (generalize it to update whichever map contains the imdbId).

```cpp
// progress bar
auto* bar = new QFrame(card); bar->setFixedHeight(4);
bar->setStyleSheet("background:rgba(255,255,255,0.12);border-radius:2px;");
auto* fill = new QFrame(bar);
fill->setStyleSheet("background:rgba(255,255,255,0.55);border-radius:2px;");
fill->setGeometry(0,0, bar->width()*done/std::max(1,total), 4);  // or a layout-driven fill
```

- [ ] **Step 2: Build + smoke.** `build_check.bat` → `BUILD OK`. With a show mid-download, the Active card shows poster + title + progress bar + state line.

- [ ] **Step 3: Commit.** `feat(stream-downloads): active section poster + progress parity`.

---

## Task 5: Final review + reviewer pass + smoke evidence

- [ ] **Step 1:** Run `/simplify` on the full diff (reuse/efficiency cleanup).
- [ ] **Step 2:** Run `/superpowers:requesting-code-review` self-primer; for a non-trivial UI+network diff also consider a `/security-review` (network fetch of poster URLs — validate they're https, no path traversal in `imdbId` used as a filename: `imdbId` is a `tt…` id, but assert it matches `^[A-Za-z0-9:_-]+$` before using it as a cache filename).
- [ ] **Step 3:** Capture a smoke screenshot of the redesigned page (Daredevil + Community) to `agents/audits/smoke_evidence/`.
- [ ] **Step 4:** Reviewer pass before master (gov-v11). Post RTC line for Agent 0 sweep.

---

## Self-Review (plan vs spec)

- **Spec goal 1 (title-only rows, no filenames):** Task 1 (placeholder) + Task 3 (real titles). ✓
- **Spec goal 2 (poster per group):** Task 2. ✓
- **Spec goal 3 (Active + History split):** Tasks 1+4. ✓
- **Spec goal 4 (Theatre + Comics):** Theatre = Tasks 0-5; Comics = handoff below. ✓ (split per skill scope-check + Rule 14)
- **Spec goal 5 (never regress to filename / offline fallback):** `prettifyFilenameTitle` placeholder is the floor (Task 1); enrichment only upgrades (Task 3). ✓
- **Type consistency:** `DownloadCardRefs`, `makePosterWidget`, `savePosterFrom`, `onMetaItemReady`, key format `"<season>:<episode>"` used consistently across Tasks 1-4. ✓
- **Placeholder scan:** one genuine open verification (Task 0 Step 5: `fetchMetaItem` reentrancy) — gated with an explicit fallback, not a hand-wave. ✓
- **Security:** `imdbId`-as-filename validated in Task 5 Step 2. ✓

---

## Comics track — handoff brief for Agent 1 (NOT executed by this plan)

`ComicsDownloadsPage` ([src/ui/pages/comics/ComicsDownloadsPage.cpp](../../../src/ui/pages/comics/ComicsDownloadsPage.cpp)) already groups completed downloads by canonical series via the **display-projection pipeline** (`setComicsPage()` + `MangaDownloadIndex::entriesForAllSeries()/entriesForSeries()`). The redesign there is a **visual mirror** of the Theatre side-poster card:

- **Layout:** same poster-left / title-only-rows card. **Cover size: 110×150** (BookWalker-native 2:3), per `feedback_bigger_manga_covers` — NOT the Theatre 96×144.
- **Rows:** `Vol. N · <volume title>` (volume title from the existing projection; fall back to `Volume N`). Volume is the first-class unit (`feedback_stremio_for_manga_vibe`).
- **Cover source:** local catalog via the existing `ComicsPage` projection + the `ComicsSeriesView` `QPixmapCache`-keyed-by-URL cover loader ([ComicsSeriesView.cpp:1822-2065](../../../src/ui/pages/comics/ComicsSeriesView.cpp#L1822-L2065)). No network needed — Comics metadata is local.
- **Active section:** `MangaDownloadIndex` tracks completed only (no progress aggregation API) — Comics keeps its single "Downloaded" section unless Agent 1 adds in-progress tracking.

**Governance:** `src/ui/pages/comics/` is Agent 1 domain, standing polish-mode. Hemanth has ratified this redesign (covers the polish-mode gate), but the edits require **Agent 1 sign-off (Rule 14)**. Recommend this becomes its own short plan executed by Agent 1, mirroring Tasks 1-4 above against the Comics data layer.
