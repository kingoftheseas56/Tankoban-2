# Comics Context Menus — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add right-click context menus across Comics mode — primarily to delete downloads — on five surfaces: series-view volume tiles, library grid series tiles, the Continue Reading strip, the Comic Reader, and the Downloads page.

**Architecture:** Flat, per-surface handlers (no menu framework), mirroring the proven Continue Reading menu at `ComicsPage.cpp:1076`. Two shared primitives only: a generic 3-way remove dialog added to `ContextMenuHelper`, and a new `MangaDownloadIndex::evictByVolume` for single-volume removal. Each surface owns its own menu + actions.

**Tech Stack:** C++17, Qt6 (QMenu / QMessageBox / signals), GoogleTest (`tankoban_tests`), `ContextMenuHelper` (`src/ui/ContextMenuHelper.h`), `MangaDownloadIndex`.

**Spec:** `docs/superpowers/specs/2026-05-28-comics-context-menu-design.md` (decisions D1-D8).

---

## Verified ground-truth (do not re-derive)

- **`ContextMenuHelper`** (`src/ui/ContextMenuHelper.h`): `QMenu* createMenu(QWidget*)`, `QAction* addDangerAction(QMenu*, QString)`, `void revealInExplorer(QString)`, `void copyToClipboard(QString)`, `bool confirmRemove(parent,title,msg)` (2-way). The 3-way dialog does NOT exist yet — Task 1 adds it here.
- **`MangaDownloadIndex`** (`src/core/manga/MangaDownloadIndex.h`): `Entry{ sourceId, seriesId, chapterId, int volumeNumber, QString canonicalPath, addedAt, fileSizeBytes, QSet<QString> servedChapterKeys }`. Methods: `evictBySeries(sourceId,seriesId)`, `evictByChapter(sourceId,seriesId,chapterId)`, `std::optional<Entry> entryForSeriesAndVolume(sourceId,seriesId,volumeNumber)`, `QList<Entry> entriesForSeries(sourceId,seriesId)`, `validateAll()`. Signal `entriesChanged()`. Mutex-guarded; mutators run synchronously on the calling thread. NO per-volume evict — Task 2 adds it.
- **Source id constant:** `kWeebCentralSourceId == "weebcentral"` (`ComicsSeriesView.cpp:83`).
- **`VolumeTile`** (`src/ui/pages/comics/VolumeTile.h`): `int volumeNumber()`, `VolumeTileState volumeState()` where `VolumeTileState{ State state; QString cbzPath; }` — downloaded ⟺ `state == Complete && !cbzPath.isEmpty()`. Signal `rowClicked(int)`.
- **`ComicsSeriesView`**: volume tiles live in `m_volumeTiles` (`QList<VolumeTile*>`), laid out in `m_volumesLayout` inside `m_volumesHost` (`ComicsSeriesView.cpp:667`). `m_downloadIndex` is the non-owning `MangaDownloadIndex*`. `m_currentMangaCatalog.seriesId` is the series id.
- **`ComicsPage`**: existing Continue Reading menu handler at `ComicsPage.cpp:1076-1170` (`m_continueStrip`, a `TileStrip`, `Qt::CustomContextMenu` + `customContextMenuRequested` lambda; uses `tileAt(pos)`, `mapToGlobal`). The main library grid is `m_tileStrip` (`TileStrip`) — no menu yet.
- **`ComicReader`** (`src/ui/readers/ComicReader.cpp:3561`): `contextMenuEvent` already builds a rich menu (Settings, Page Thumbnails, spread toggle, Gutter Shadow, Side Padding, Split Wide Pages) and ends with `menu->exec(...)`. Has `QString m_cbzPath`. Append actions just before the `exec`.
- **`ComicsDownloadsPage`** (`src/ui/pages/comics/ComicsDownloadsPage.{cpp,h}`): the Downloads sidebar page.

---

## File Structure

- `src/ui/ContextMenuHelper.{h,cpp}` — add `confirmRemoveWithFile` (generic 3-way remove dialog).
- `src/core/manga/MangaDownloadIndex.{h,cpp}` — add `evictByVolume`.
- `tests/core/manga/test_manga_download_index*.cpp` (existing suite; confirm exact filename) — test `evictByVolume`.
- `src/ui/pages/comics/ComicsSeriesView.{cpp,h}` — volume-tile context menu.
- `src/ui/pages/ComicsPage.cpp` — library-grid series menu (new) + Continue Reading menu (simplify to one action).
- `src/ui/readers/ComicReader.cpp` — append Reveal + Copy to existing menu.
- `src/ui/pages/comics/ComicsDownloadsPage.cpp` — per-row menu.

---

### Task 1: Generic 3-way remove dialog in ContextMenuHelper

**Files:**
- Modify: `src/ui/ContextMenuHelper.h`
- Modify: `src/ui/ContextMenuHelper.cpp`

- [ ] **Step 1: Declare the enum + function**

In `ContextMenuHelper.h`, inside the `ContextMenuHelper` namespace, after `confirmRemove`:

```cpp
// 3-way remove dialog for items backed by a file on disk. Returns the choice.
enum class RemoveChoice { Cancel, RemoveFromLibrary, DeleteFile };
RemoveChoice confirmRemoveWithFile(QWidget* parent,
                                   const QString& title,
                                   const QString& message);
```

- [ ] **Step 2: Implement**

In `ContextMenuHelper.cpp` (mirror the existing `confirmRemove` `QMessageBox` styling):

```cpp
RemoveChoice confirmRemoveWithFile(QWidget* parent,
                                   const QString& title,
                                   const QString& message)
{
    QMessageBox box(parent);
    box.setWindowTitle(title);
    box.setText(message);
    box.setIcon(QMessageBox::Warning);
    QPushButton* libBtn  = box.addButton(QObject::tr("Remove from library only"),
                                         QMessageBox::AcceptRole);
    QPushButton* fileBtn = box.addButton(QObject::tr("Delete the file too"),
                                         QMessageBox::DestructiveRole);
    QPushButton* cancel  = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    // Reuse the existing dark QSS the other ContextMenuHelper dialogs apply
    // (copy the stylesheet block from confirmRemove's QMessageBox here).
    box.exec();
    if (box.clickedButton() == libBtn)  return RemoveChoice::RemoveFromLibrary;
    if (box.clickedButton() == fileBtn) return RemoveChoice::DeleteFile;
    return RemoveChoice::Cancel;
}
```

Add `#include <QPushButton>` to the .cpp if absent.

- [ ] **Step 3: Build-check**

Kill `Tankoban.exe`, run `build_check.bat`. Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ContextMenuHelper.h src/ui/ContextMenuHelper.cpp
git commit -m "[Agent 1, COMICS_CONTEXT_MENU]: ContextMenuHelper 3-way confirmRemoveWithFile"
```

---

### Task 2: `MangaDownloadIndex::evictByVolume` (TDD)

**Files:**
- Modify: `src/core/manga/MangaDownloadIndex.h` (declare)
- Modify: `src/core/manga/MangaDownloadIndex.cpp` (implement, against the existing `evictByChapter`/`entryForSeriesAndVolume` internals)
- Test: existing download-index test file (confirm exact path with `ls tests/core/manga/`)

- [ ] **Step 1: Write the failing test**

Append to the download-index test file (mirror its existing `registerVolume` + `entryForSeriesAndVolume` test setup):

```cpp
TEST(MangaDownloadIndex, EvictByVolumeRemovesOnlyThatVolume) {
    MangaDownloadIndex idx(/* same ctor args the other tests use */);
    idx.registerVolume("weebcentral", "one-piece", 1, "/tmp/v1.cbz", 100, {"c1","c2"});
    idx.registerVolume("weebcentral", "one-piece", 2, "/tmp/v2.cbz", 100, {"c3","c4"});

    idx.evictByVolume("weebcentral", "one-piece", 1);

    EXPECT_FALSE(idx.entryForSeriesAndVolume("weebcentral", "one-piece", 1).has_value());
    EXPECT_TRUE (idx.entryForSeriesAndVolume("weebcentral", "one-piece", 2).has_value());
}
```

- [ ] **Step 2: Build the test target + run to confirm FAIL**

```
_build_tests.bat   (then run tankoban_tests.exe --gtest_filter=MangaDownloadIndex.EvictByVolume*)
```
Expected: FAIL — `evictByVolume` not declared.

- [ ] **Step 3: Declare + implement**

In `MangaDownloadIndex.h`, next to `evictByChapter`:

```cpp
// Evict a single volume's entry (and its served chapter keys). File on disk
// is NOT touched — caller removes the file for the "delete file too" path.
void evictByVolume(const QString& sourceId, const QString& seriesId, int volumeNumber);
```

In `MangaDownloadIndex.cpp`, implement by **reading the existing `evictByChapter` + `entryForSeriesAndVolume` bodies** and matching their locking + `m_byPath` cleanup. The shape: under `m_mutex`, find the volume's entry, remove its `servedChapterKeys` rows and its `m_byPath[canonicalPath]` entry (exactly as `evictByChapter` does for the last chapter), then `save()` + `emit entriesChanged()` OFF the lock (the file's header comment at line 49 documents this ordering). Do NOT guess the internal key formats — copy `evictByChapter`'s exact map operations.

- [ ] **Step 4: Run test to confirm PASS**

Expected: PASS. Run the full `MangaDownloadIndex.*` filter to confirm no regressions.

- [ ] **Step 5: Commit**

```bash
git add src/core/manga/MangaDownloadIndex.h src/core/manga/MangaDownloadIndex.cpp tests/core/manga/<test_file>.cpp
git commit -m "[Agent 1, COMICS_CONTEXT_MENU]: MangaDownloadIndex evictByVolume (TDD)"
```

---

### Task 3: Volume-tile context menu (the core surface)

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`
- Modify: `src/ui/pages/comics/ComicsSeriesView.h` (declare the handler slot + a private remove helper)

- [ ] **Step 1: Wire the context-menu policy on each volume tile**

In `populateVolumeRowsFromCatalog` (and the Volume X tile branch), after each `VolumeTile* tile` is created and added to `m_volumeTiles`, set:

```cpp
tile->setContextMenuPolicy(Qt::CustomContextMenu);
connect(tile, &QWidget::customContextMenuRequested, this,
        [this, tile](const QPoint& pos) { showVolumeTileMenu(tile, pos); });
```

- [ ] **Step 2: Declare the handler + helper in the header**

In `ComicsSeriesView.h` (private):

```cpp
void showVolumeTileMenu(tankoban::ui::comics::VolumeTile* tile, const QPoint& pos);
void deleteVolumeDownload(int volumeNumber, const QString& cbzPath);
```

- [ ] **Step 3: Implement the menu**

In `ComicsSeriesView.cpp` (include `"ui/ContextMenuHelper.h"`, `<QMenu>`, `<QFile>`):

```cpp
void ComicsSeriesView::showVolumeTileMenu(tankoban::ui::comics::VolumeTile* tile,
                                          const QPoint& pos)
{
    if (!tile) return;
    const auto st = tile->volumeState();
    const bool downloaded =
        (st.state == tankoban::ui::comics::VolumeTileState::Complete)
        && !st.cbzPath.isEmpty();
    if (!downloaded) return;  // D3: no menu for non-downloaded tiles

    QMenu* menu = ContextMenuHelper::createMenu(this);
    QAction* del    = ContextMenuHelper::addDangerAction(menu, tr("Delete…"));
    QAction* reveal = menu->addAction(tr("Reveal in File Explorer"));
    QAction* copy   = menu->addAction(tr("Copy path"));
    const bool fileExists = QFile::exists(st.cbzPath);
    reveal->setEnabled(fileExists);
    copy->setEnabled(fileExists);

    QAction* chosen = menu->exec(tile->mapToGlobal(pos));
    if (chosen == del)         deleteVolumeDownload(tile->volumeNumber(), st.cbzPath);
    else if (chosen == reveal) ContextMenuHelper::revealInExplorer(st.cbzPath);
    else if (chosen == copy)   ContextMenuHelper::copyToClipboard(st.cbzPath);
    menu->deleteLater();
}

void ComicsSeriesView::deleteVolumeDownload(int volumeNumber, const QString& cbzPath)
{
    if (!m_downloadIndex) return;
    const QString seriesId = m_currentMangaCatalog.seriesId;
    const QString src = QString::fromLatin1(kWeebCentralSourceId);

    const auto choice = ContextMenuHelper::confirmRemoveWithFile(
        this, tr("Delete volume"),
        tr("Remove this volume from your library?"));
    if (choice == ContextMenuHelper::RemoveChoice::Cancel) return;

    m_downloadIndex->evictByVolume(src, seriesId, volumeNumber);
    if (choice == ContextMenuHelper::RemoveChoice::DeleteFile && !cbzPath.isEmpty()) {
        QFile::remove(cbzPath);
        QFile::remove(cbzPath + QStringLiteral(".volx"));  // pairing sidecar, if present
    }
    // entriesChanged() (emitted by evictByVolume) re-renders the tile to undownloaded.
}
```

- [ ] **Step 4: Build-check** — `build_check.bat` → `BUILD OK`.

- [ ] **Step 5: Smoke** — launch, open a series with a downloaded volume, right-click it → Delete (file too) → tile reverts to undownloaded, `.cbz` gone; re-click → Sources panel offers re-download. Right-click a non-downloaded tile → no menu.

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.cpp src/ui/pages/comics/ComicsSeriesView.h
git commit -m "[Agent 1, COMICS_CONTEXT_MENU]: volume-tile right-click — delete/reveal/copy"
```

---

### Task 4: Library grid series-tile menu

**Files:** Modify `src/ui/pages/ComicsPage.cpp`.

**Pattern source:** copy the structure of the existing `m_continueStrip` handler at `ComicsPage.cpp:1076-1170` (policy + `customContextMenuRequested` + `tileAt(pos)` + `menu->exec(mapToGlobal(pos))`), retargeted to `m_tileStrip`.

- [ ] **Step 1: Add the handler** after `m_tileStrip` is constructed. Read the tile's `seriesId`/`seriesName`/`sourceId` properties (set during the grid rebuild — confirm the exact property names by reading the rebuild method). Actions (D4), in order:
  - **Open series** → the same call the left-click activation makes (find it in the existing tile-activation slot).
  - **Mark as read / Mark as unread** → toggle the series `finished` flag via `m_bridge` progress store (mirror the Continue Reading handler's mark logic at `:1076-1170`).
  - **Rename…** → `QInputDialog::getText`; rename the series folder (`QDir().rename`) AND update `canonicalPath` on every `m_mangaDownloadIndex->entriesForSeries(src, seriesId)` entry (re-register at the new path, or add a rename helper — confirm against the index API). On `rename`==false → `QMessageBox::warning`.
  - **Remove series…** → `ContextMenuHelper::confirmRemoveWithFile`; on `RemoveFromLibrary` → `m_mangaDownloadIndex->evictBySeries(src, seriesId)`; on `DeleteFile` → also `QFile::remove` each `entriesForSeries` `canonicalPath` (+ `.volx`), then `evictBySeries`.
  - **Refresh metadata** → call the existing catalog/AniList resolve entry-point for the series (find it near `dispatchCatalogResolve`).
  - **Reveal in File Explorer** / **Copy path** → series folder path (parent dir of the first entry's `canonicalPath`); disabled if none.

- [ ] **Step 2: Build-check** → `BUILD OK`.
- [ ] **Step 3: Smoke** — right-click a series card → each action behaves; Remove series (both semantics) clears all volumes; grid auto-refreshes via `entriesChanged()`.
- [ ] **Step 4: Commit** `[Agent 1, COMICS_CONTEXT_MENU]: library-grid series-tile menu`

---

### Task 5: Continue Reading strip → single action

**Files:** Modify `src/ui/pages/ComicsPage.cpp` (existing handler at `:1076-1170`).

**Per D5 (Hemanth verbatim "just have one"):** replace the body of the `m_continueStrip` `customContextMenuRequested` lambda so the menu has exactly one action.

- [ ] **Step 1:** Inside the existing lambda, after `auto* card = m_continueStrip->tileAt(pos); if (!card) return;`, replace the multi-action menu construction with:

```cpp
QMenu* menu = ContextMenuHelper::createMenu(this);
QAction* removeCr = menu->addAction(tr("Remove from Continue Reading"));
if (menu->exec(m_continueStrip->mapToGlobal(pos)) == removeCr) {
    // Clear the continue/progress entry for this card so it leaves the strip.
    // Reuse the SAME progress-clear the existing handler used for its remove
    // path (read :1131-1170 for the exact key + clear call), then:
    refreshContinueStrip();
}
menu->deleteLater();
```

Confirm the exact progress-key + clear call from the existing handler (it already resolves `filePath`/key for the card). Do NOT delete files here — this only removes from the strip.

- [ ] **Step 2: Build-check** → `BUILD OK`.
- [ ] **Step 3: Smoke** — right-click a Continue Reading card → "Remove from Continue Reading" → card leaves the strip; file + library untouched.
- [ ] **Step 4: Commit** `[Agent 1, COMICS_CONTEXT_MENU]: Continue Reading menu simplified to single remove action`

---

### Task 6: Comic Reader — append Reveal + Copy

**Files:** Modify `src/ui/readers/ComicReader.cpp` (`contextMenuEvent`, `:3561`).

- [ ] **Step 1:** Just before the existing `menu->exec(...)` at the end of `contextMenuEvent`, append:

```cpp
menu->addSeparator();
QAction* revealAct = menu->addAction(tr("Reveal in File Explorer"));
QAction* copyAct   = menu->addAction(tr("Copy volume path"));
const bool hasPath = !m_cbzPath.isEmpty() && QFile::exists(m_cbzPath);
revealAct->setEnabled(hasPath);
copyAct->setEnabled(!m_cbzPath.isEmpty());
```

Then in the action-dispatch (where the existing `exec` result is handled — or add handling): `if (chosen == revealAct) ContextMenuHelper::revealInExplorer(m_cbzPath); else if (chosen == copyAct) ContextMenuHelper::copyToClipboard(m_cbzPath);`. If the existing menu uses per-action `connect` rather than an `exec` return switch, use `connect(revealAct, &QAction::triggered, this, [this]{ ContextMenuHelper::revealInExplorer(m_cbzPath); });` to match the file's style. Add `<QFile>` include if absent (`ContextMenuHelper.h` already included).

- [ ] **Step 2: Build-check** → `BUILD OK`.
- [ ] **Step 3: Smoke** — open a volume in the reader, right-click → Reveal opens Explorer at the `.cbz`; Copy path pastes it.
- [ ] **Step 4: Commit** `[Agent 1, COMICS_CONTEXT_MENU]: reader menu gains Reveal + Copy volume path`

---

### Task 7: Downloads page row menu

**Files:** Modify `src/ui/pages/comics/ComicsDownloadsPage.cpp`.

- [ ] **Step 1:** Read the page to find the per-row widget + how a row carries its `sourceId`/`seriesId`/`volumeNumber`/`canonicalPath` (it consumes `MangaDownloadIndex` already). Add `Qt::CustomContextMenu` + a handler whose menu is: **Open series** (navigate to the series view — find the existing row-open path) · **Delete…** (`confirmRemoveWithFile` → `evictByVolume` + optional `QFile::remove`) · **Reveal** · **Copy path** (disabled when file missing).
- [ ] **Step 2: Build-check** → `BUILD OK`.
- [ ] **Step 3: Smoke** — right-click a Downloads row → Open navigates; Delete removes per chosen semantics; Reveal/Copy work.
- [ ] **Step 4: Commit** `[Agent 1, COMICS_CONTEXT_MENU]: Downloads page row menu`

---

## Self-Review

**Spec coverage:** D1 flat/no-framework → all tasks. D2 3-way delete → Task 1 (dialog) + used in 3,4,7. D3 lean volume menu + bail-on-undownloaded → Task 3. D4 rich series menu → Task 4. D5 CR single action → Task 5. D6 reader append → Task 6. D7 downloads → Task 7. D8 reuse → `ContextMenuHelper` extended (not duplicated), `evictByVolume` added once. **Gap addressed:** single-volume "remove from library only" had no index method → Task 2 adds `evictByVolume`.

**Placeholder scan:** Tasks 1-3 carry complete code. Tasks 4-7 intentionally point to the in-repo pattern (the `m_continueStrip` handler at `:1076-1170`, the reader `contextMenuEvent`, the Downloads page) and require the implementer to read 2-3 specific cited locations for exact property names / progress-clear call / row structure — these are concrete pointers to existing code, not hand-waving. Each lists exact actions + exact API calls (`evictBySeries`/`evictByVolume`/`confirmRemoveWithFile`/`revealInExplorer`).

**Type consistency:** `confirmRemoveWithFile → RemoveChoice{Cancel,RemoveFromLibrary,DeleteFile}` defined Task 1, used Tasks 3/4/7. `evictByVolume(sourceId,seriesId,volumeNumber)` defined Task 2, used Tasks 3/7. `kWeebCentralSourceId` as the sourceId throughout. `VolumeTileState::Complete` + `cbzPath` as the downloaded test.

**Note for executor:** Tasks 4-7 each open with a short read of the cited existing handler before editing — that's deliberate, since those surfaces mirror proven in-repo code rather than introducing new patterns. Task 3 (the core delete surface) and Tasks 1-2 (the shared primitives) are fully specified and should land first.
