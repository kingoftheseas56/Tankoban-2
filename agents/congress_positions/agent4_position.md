# Agent 4 (Stream & Sources) — Congress Position
# First Congress: Comic Reader Acceleration

**Your lane: File loading, format support, archive handling, error states, integration behavior**

---

## Step 1: Read these files IN THIS ORDER before writing anything

**Primary — Tankoban Max (JavaScript):**
- Any file in `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\` that handles file opening, archive reading, image loading, loading states, or errors
- Look for: how Max opens a CBZ, how it reads entries, how it handles missing/corrupt pages, what it shows while images load

**Secondary — Groundwork (PyQt6):**
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\readers\comic_reader.py`
  Focus on: `openVolume`, `loadPage`, any QThread or async loading logic, error handling, loading placeholder behavior, any format support beyond CBZ

**Current implementation:**
- `src\ui\readers\ComicReader.h`
- `src\ui\readers\ComicReader.cpp`
- Any `ArchiveReader`, `PageCache`, or `DecodeTask` files in `src\ui\readers\`

Do not write a single word of your position until you have read all of the above.

---

## Step 2: Your assignment

You built the download pipeline. You know exactly what comes out of it — CBZ files with gaps, mixed formats, long paths, single-page chapters. Your job is to read how Max and Groundwork handle file loading and format support — compare to Agent 1's current implementation — and write specific fixes and solutions in real C++.

**Part A — Rough edges already implemented (find the gaps and fix them)**

For each file-loading/format feature already in ComicReader, compare to Max and Groundwork. Where behavior is incomplete or fragile, document it and write the fix.

Areas to check:
- Loading state — what does Max show while a page is decoding? A spinner? A placeholder? A blank canvas? What do we show? Should we show something better?
- Corrupt/missing page handling — how does Max behave when a page entry in the archive is zero bytes or unreadable? Does it skip it, show an error tile, or crash? What do we do?
- Page count accuracy — Max reads actual archive entry count. Do we? Or do we assume a contiguous sequence anywhere?
- CBZ open failure — if `ArchiveReader::open()` returns false, what does the reader show the user? Is there an error state in the UI, or does the reader just open blank?
- Format detection — does Max detect image format by extension, by magic bytes, or both? What does Groundwork do? What do we do? Are we handling `.webp` correctly?
- Async decode pipeline — how does Max queue and cancel page decode requests? Compare to our `DecodeTask` / `PageCache` approach. Is there a behavioral gap the user would notice?

For each gap, use this format:

```
## Rough Edge: [name]
What Max does: ...
What Groundwork does: ...
What we have: ...
The gap: ...
Fix:
[C++ code — specific, compilable, with file and approximate line reference]
```

**Part B — Unimplemented loading/format features (what Max and Groundwork have that we don't)**

Look at Max and Groundwork for file handling behaviors not yet in our reader. For each:
- Describe what it does
- Decide: worth implementing?
- If yes: write the C++ approach

Things to look for (not exhaustive — read the code):
- Folder-based reading (raw image folder instead of CBZ) — does Max support it? Does Groundwork?
- CBR/RAR support — does Max or Groundwork read RAR archives? Should we?
- PDF support — does either reference support PDF as a comic format?
- Any preload or prefetch behavior in Max that is more sophisticated than what we have

Use this format:

```
## Unimplemented Feature: [name]
What Max does: ...
What Groundwork does: ...
Implement? Yes / No / Modified
C++ approach:
[C++ code or detailed spec]
```

---

## Step 3: When you are done

Post ONE LINE in `agents/chat.md`:

`Agent 4 congress position complete — [N] rough-edge fixes, [M] unimplemented features. See congress_positions/agent4_position.md.`

Then stop. Do not build anything. Do not touch Agent 1's files. Do not post anywhere else.

---

## Your Specific Checklist (from master_checklist.md)

These are your assigned items. Address every one. Use the master checklist for Max/Groundwork file references.

**Rough Edges:**
- R1: Natural sort for archive entries — "page10" must sort after "page9". Max: naturalCompare() in open.js line 209. Groundwork: natural_sort_key() lines 95–96 using re.split on digits. Does our ArchiveReader sort naturally or lexicographically?
- R2: Format detection by content — Groundwork uses QImageReader.setDecideFormatFromContent(True) so a JPEG served with a .png extension still decodes correctly. Do we detect by content or only by extension?
- R3: EXIF rotation — Groundwork uses QImageReader.setAutoTransform(True) to apply EXIF orientation metadata on decode. Do we apply EXIF rotation?
- R4: Stale decode detection — if the user navigates to a new volume while a decode is in-flight for the old volume, the result must be discarded. Max snapshots a volume token at queue-entry time. Groundwork uses an `inflight: set[int]`. Does our DecodeTask discard stale results correctly?
- R5: Loading status label — Groundwork shows "Indexing pages..." QLabel in the reader area while IndexerThread runs, removed immediately when indexed_fast fires. Do we show any loading indicator, or does the reader just open blank?
- R6: LRU cache with MB budget — Max/Groundwork both evict by memory cost (width * height * 4 bytes), not by entry count. Budget: 512MB normal, 256MB memory saver. Does our PageCache evict by cost or count?

**Missing Features:**
- M1: CBR/RAR archive support — optional, needs a C++ RAR extraction library (libarchive covers both ZIP and RAR). Max: cbrOpen IPC path. Groundwork: rarfile + 7-zip backend. Assess feasibility and give the C++ approach using libarchive or equivalent.
- M2: Fast dimension parsing from header bytes — parse PNG/JPEG/WebP image dimensions from the first few KB of file data without full decode. Used to pre-size page widgets and build Two-Page Scroll row layout before decode completes. Groundwork: parse_image_dimensions() lines 213–346. Give the C++ equivalent.
- M3: Memory saver mode — user-togglable setting that drops the cache budget from 512MB to 256MB. Max: PAGE_CACHE_BUDGET_MEMORY_SAVER. Simple boolean in settings; changes the eviction threshold in PageCache.

---

*(Write your position below this line)*

---

## Rough Edge: R1 — Natural sort for archive entries

**What Max does:** Reads CBZ central directory in raw ZIP order; the renderer sorts page names before display using `naturalCompare()`.

**What Groundwork does:** `natural_sort_key()` line 95–96 splits on `(\d+)` regex and compares digit tokens numerically — "page9" sorts before "page10".

**What we have:** `ArchiveReader::pageList()` uses `QCollator` with `setNumericMode(true)` — QCollator's built-in natural/numeric sort. "page10" sorts after "page9" correctly.

**The gap:** None. `QCollator` with `setNumericMode(true)` is equivalent to, and more robust than, `natural_sort_key()`. No fix needed.

---

## Rough Edge: R2 — Format detection by content

**What Max does:** Passes raw byte buffers to Chromium's native decoder — content-based by definition (no file extension available).

**What Groundwork does:** `decode_image()` line 192–203 uses `QImageReader` with `setDecideFormatFromContent(True)` — explicitly bypasses extension, detects by magic bytes.

**What we have:** `DecodeTask::run()` calls `QPixmap::loadFromData(data)` with no format argument. When format is `nullptr`, Qt creates a `QImageReader` from a `QBuffer` (no file path, no extension to mislead it) and probes by magic bytes. Functionally equivalent.

**The gap:** Minimal in practice — byte array input means no extension to mislead Qt. Fixing R3 requires switching to `QImageReader` anyway, which locks in explicit content-detection simultaneously. No standalone fix needed here.

---

## Rough Edge: R3 — EXIF rotation

**What Max does:** Chromium's image decoder applies EXIF orientation automatically.

**What Groundwork does:** `decode_image()` line 200: `reader.setAutoTransform(True)` — `QImageReader` applies EXIF orientation metadata. A portrait photo tagged landscape decodes correctly.

**What we have:** `DecodeTask::run()` uses `QPixmap::loadFromData(data)` — no `setAutoTransform` equivalent exists on `QPixmap`. EXIF orientation is **ignored**.

**The gap:** CBZ files from mobile cameras and some scan workflows embed EXIF orientation tags. Pages display rotated.

**Fix** — replace `QPixmap::loadFromData` in `src/ui/readers/DecodeTask.cpp` with `QImageReader`:

```cpp
// DecodeTask.cpp — rewrite run() — add #include <QImageReader>, <QBuffer>
void DecodeTask::run()
{
    QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageName);
    if (data.isEmpty()) {
        emit notifier.failed(m_pageIndex);
        return;
    }

    QBuffer buf(&data);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);       // apply EXIF orientation

    QImage image = reader.read();
    if (image.isNull()) {
        emit notifier.failed(m_pageIndex);
        return;
    }

    QPixmap pix = QPixmap::fromImage(std::move(image));
    emit notifier.decoded(m_pageIndex, pix, pix.width(), pix.height());
}
```

One-file change inside `src/ui/readers/DecodeTask.cpp`. No header changes. No signal changes.

---

## Rough Edge: R4 — Stale decode detection

**What Max does:** Snapshots a `volumeToken` (unique string per open) at queue-entry time. Result is dropped if the token no longer matches.

**What Groundwork does:** `inflight: set[int]` is cleared when `openVolume` is called. Old `DecodeTask` results check if their index is still in `inflight`; since it was cleared, stale results are dropped before any state mutation.

**What we have:** `m_inflightDecodes` tracks in-flight indices. When `openBook()` is called for a new volume:
1. `m_cache.clear()` runs — correct
2. `m_inflightDecodes` is **not cleared** — old tasks are still "tracked"
3. Old `DecodeTask` completes, fires `onPageDecoded(pageIndex, pixmap)`
4. `m_cache.insert(pageIndex, pixmap)` — **inserts old-volume page into new volume's cache**
5. If `pageIndex == m_currentPage`, the old pixmap is displayed as the new volume's first page

**The gap:** Fast volume switch (next volume clicked before decode completes) displays an old page in the new volume. Cache is polluted with wrong-volume data.

**Fix** — add `m_volumeId` (int) to `ComicReader`, thread it through `DecodeTask`, guard in `onPageDecoded`:

```cpp
// DecodeTask.h — add volumeId to constructor and signal
class DecodeTaskSignals : public QObject {
    Q_OBJECT
signals:
    void decoded(int pageIndex, const QPixmap& pixmap, int w, int h, int volumeId);
    void failed(int pageIndex);
};

class DecodeTask : public QRunnable {
public:
    DecodeTask(int pageIndex, const QString& cbzPath,
               const QString& pageName, int volumeId);
    void run() override;
    DecodeTaskSignals notifier;
private:
    int     m_pageIndex;
    QString m_cbzPath;
    QString m_pageName;
    int     m_volumeId;
};
```

```cpp
// DecodeTask.cpp — store and emit volumeId
DecodeTask::DecodeTask(int pageIndex, const QString& cbzPath,
                       const QString& pageName, int volumeId)
    : m_pageIndex(pageIndex), m_cbzPath(cbzPath)
    , m_pageName(pageName), m_volumeId(volumeId)
{ setAutoDelete(true); }

void DecodeTask::run()
{
    // ... decode logic from R3 fix ...
    emit notifier.decoded(m_pageIndex, pix, pix.width(), pix.height(), m_volumeId);
}
```

```cpp
// ComicReader.h — add member
int m_volumeId = 0;

// ComicReader.cpp — openBook(): increment + clear
void ComicReader::openBook(...)
{
    ++m_volumeId;
    m_inflightDecodes.clear();
    // ... rest of openBook ...
}

// requestDecode(): pass volumeId
auto* task = new DecodeTask(pageIndex, m_cbzPath, m_pageNames[pageIndex], m_volumeId);

// onPageDecoded(): add parameter + guard
void ComicReader::onPageDecoded(int pageIndex, const QPixmap& pixmap,
                                int w, int h, int volumeId)
{
    if (volumeId != m_volumeId) return;   // stale — discard
    m_inflightDecodes.remove(pageIndex);
    m_cache.insert(pageIndex, pixmap);
    // ... rest unchanged ...
}
```

`Qt::QueuedConnection` ensures the result arrives on the main thread after `openBook()` has already incremented `m_volumeId`, so the guard fires correctly in all race conditions.

---

## Rough Edge: R5 — Loading status label

**What Max does:** Reader shows a loading state while the archive index is fetched from the main process. Pages appear only after the index returns.

**What Groundwork does:** Line 2154: `self.status = QLabel("Indexing pages...")` added to the layout before `start_indexing()`. `IndexerThread` runs `archive.list_images()` in a background `QThread`. On `indexed_fast`, the label is removed and page widgets are created. On failure, label text updates to the error message.

**What we have:** `openBook()` calls `ArchiveReader::pageList(cbzPath)` **synchronously on the main thread**. UI freezes during this call. No loading indicator is shown. Error handling conflates "empty archive" with "file not found" — both return `{}` from `pageList()`.

**The gap:** User sees a blank/stale screen with no feedback. Errors have no distinction.

**Fix (minimal — no async rewrite required):**

```cpp
// ComicReader.h — add member
QLabel* m_statusLabel = nullptr;

// ComicReader::buildUI() — create label (hidden)
m_statusLabel = new QLabel(this);
m_statusLabel->setAlignment(Qt::AlignCenter);
m_statusLabel->setStyleSheet(
    "color: rgba(255,255,255,0.72); font-size: 13px; background: transparent;");
m_statusLabel->hide();

// ComicReader::openBook() — bracket the synchronous call
++m_volumeId;
m_inflightDecodes.clear();
m_cache.clear();
m_currentPixmap = QPixmap();
m_secondPixmap  = QPixmap();
m_imageLabel->clear();

m_statusLabel->setText("Loading...");
m_statusLabel->show();
m_statusLabel->raise();
QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

m_cbzPath = cbzPath;
m_pageNames = ArchiveReader::pageList(cbzPath);

m_statusLabel->hide();

if (m_pageNames.isEmpty()) {
    QFileInfo fi(cbzPath);
    m_imageLabel->setText(fi.exists()
        ? "No image pages found in this archive"
        : "File not found: " + fi.fileName());
    updatePageLabel();
    return;
}
// ... rest of openBook ...
```

**Note for Agent 1:** The proper long-term fix is an async `IndexerThread` (background `QThread` running `pageList()`, emitting a signal with the page names). That matches the Groundwork architecture exactly and eliminates the main-thread block. The minimal fix above covers the visible symptom. Agent 1 decides which path to take.

---

## Rough Edge: R6 — LRU cache with MB budget

**What Max does:** Tracks cost as `width * height * 4` bytes. Evicts LRU entries when budget exceeded (512MB normal, 256MB memory saver).

**What Groundwork does:** `image_memory_size()` line 186–190: `width * height * 4`. Same eviction logic, same budgets, `memory_saver` boolean toggle.

**What we have:** `PageCache::pixmapBytes()` returns `width * height * 4`. Default budget 512MB. `evict()` evicts LRU non-pinned entries by cost. **Already correct.**

**The gap:** None for the core tracking. Memory Saver toggle is missing — filed under M3 as a missing feature.

---

## Unimplemented Feature: M1 — CBR/RAR archive support

**What Max does:** Full CBR support via `node-unrar-js`. `cbrOpen()` / `cbrReadEntry()` / `cbrClose()` IPC handlers with LRU session eviction.

**What Groundwork does:** `ArchiveReader.__enter__()` detects `.cbr`/`.rar` and opens via `rarfile.RarFile` (requires 7-Zip at `C:\Program Files\7-Zip\7z.exe`).

**Implement?** Yes — conditional on library availability.

**Recommended: libarchive** (handles ZIP, RAR, RAR5, 7z in one library; MSVC pre-built available via vcpkg `libarchive`; no 7-Zip dependency):

```cpp
// ArchiveReader.cpp — extension dispatch, no API change

#ifdef HAS_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>

static QStringList pageListLibArchive(const QString& path) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    QStringList pages;
    if (archive_read_open_filename(a, path.toLocal8Bit().constData(), 10240) == ARCHIVE_OK) {
        struct archive_entry* entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            if (archive_entry_filetype(entry) == AE_IFREG) {
                QString name = QString::fromUtf8(archive_entry_pathname(entry));
                if (isImageFile(name) && !QFileInfo(name).fileName().startsWith('.'))
                    pages.append(name);
            }
            archive_read_data_skip(a);
        }
    }
    archive_read_free(a);
    QCollator col; col.setNumericMode(true);
    std::sort(pages.begin(), pages.end(),
        [&col](const QString& a, const QString& b){ return col.compare(a,b) < 0; });
    return pages;
}

static QByteArray pageDataLibArchive(const QString& path, const QString& pageName) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    QByteArray result;
    if (archive_read_open_filename(a, path.toLocal8Bit().constData(), 10240) == ARCHIVE_OK) {
        struct archive_entry* entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            if (QString::fromUtf8(archive_entry_pathname(entry)) == pageName) {
                qint64 size = archive_entry_size(entry);
                if (size > 0) { result.resize(size); archive_read_data(a, result.data(), size); }
                break;
            }
            archive_read_data_skip(a);
        }
    }
    archive_read_free(a);
    return result;
}
#endif

static bool isCbr(const QString& path) {
    return path.endsWith(".cbr", Qt::CaseInsensitive)
        || path.endsWith(".rar", Qt::CaseInsensitive);
}

QStringList ArchiveReader::pageList(const QString& cbzPath) {
#ifdef HAS_LIBARCHIVE
    if (isCbr(cbzPath)) return pageListLibArchive(cbzPath);
#endif
    // ... existing QZipReader path ...
}

QByteArray ArchiveReader::pageData(const QString& cbzPath, const QString& pageName) {
#ifdef HAS_LIBARCHIVE
    if (isCbr(cbzPath)) return pageDataLibArchive(cbzPath, pageName);
#endif
    // ... existing QZipReader path ...
}
```

**CMakeLists.txt addition (conditional, post-announcement in chat.md):**
```cmake
find_library(LIBARCHIVE_LIB archive PATHS "C:/tools/libarchive/lib" NO_DEFAULT_PATH)
find_path(LIBARCHIVE_INC archive.h PATHS "C:/tools/libarchive/include" NO_DEFAULT_PATH)
if(LIBARCHIVE_LIB AND LIBARCHIVE_INC)
    target_compile_definitions(Tankoban PRIVATE HAS_LIBARCHIVE)
    target_include_directories(Tankoban PRIVATE ${LIBARCHIVE_INC})
    target_link_libraries(Tankoban PRIVATE ${LIBARCHIVE_LIB})
    message(STATUS "libarchive found — CBR/RAR support enabled")
else()
    message(STATUS "libarchive NOT found — CBR/RAR disabled (CBZ only)")
endif()
```

Zero API change to `ArchiveReader.h`. Agent 1 and Agent 5 callers are unaffected. Recommend Phase C implementation.

---

## Unimplemented Feature: M2 — Fast dimension parsing from header bytes

**What Max does:** Custom ZIP parser reads image header bytes from local headers without full extraction; dimension parsers pre-size page areas before decode completes.

**What Groundwork does:** `parse_image_dimensions()` lines 213–346 parses PNG, JPEG, GIF, WebP, BMP from first bytes. `IndexerThread` pre-populates `PageMeta.width/height` before decode; page widgets get real dimensions immediately, no layout jump.

**What we have:** `ArchiveReader::parseImageDimensions()` is **already implemented** (PNG, JPEG, WebP, GIF, BMP — all 5 formats, correct byte-level parsing). It is **not called anywhere in the decode pipeline**. `m_pageMeta[i].width/height` stay 0 until `onPageDecoded` fires. `ScrollStripCanvas` uses a hardcoded default aspect until real dims arrive, causing layout jumps.

**The gap:** The parser exists and is correct. It is disconnected from the pipeline.

**Integration plan** — emit `dimensionsReady` from `DecodeTask` before the full decode, connect in `ComicReader::requestDecode()`:

```cpp
// DecodeTaskSignals — add fast signal (alongside R4's volumeId changes)
signals:
    void dimensionsReady(int pageIndex, int w, int h, int volumeId);
    void decoded(int pageIndex, const QPixmap& pixmap, int w, int h, int volumeId);
    void failed(int pageIndex);

// DecodeTask::run() — header parse before full decode
void DecodeTask::run()
{
    QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageName);
    if (data.isEmpty()) { emit notifier.failed(m_pageIndex); return; }

    // Fast path: parse dims from first 4KB, emit immediately
    QSize dims = ArchiveReader::parseImageDimensions(data.left(4096));
    if (dims.isValid())
        emit notifier.dimensionsReady(m_pageIndex, dims.width(), dims.height(), m_volumeId);

    // Full decode path (with R3 fix: QImageReader + autoTransform)
    QBuffer buf(&data);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) { emit notifier.failed(m_pageIndex); return; }

    QPixmap pix = QPixmap::fromImage(std::move(image));
    emit notifier.decoded(m_pageIndex, pix, pix.width(), pix.height(), m_volumeId);
}
```

```cpp
// ComicReader::requestDecode() — connect dimensionsReady
connect(&task->notifier, &DecodeTaskSignals::dimensionsReady,
        this, [this](int idx, int w, int h, int vid) {
    if (vid != m_volumeId || idx < 0 || idx >= m_pageMeta.size()) return;
    m_pageMeta[idx].width   = w;
    m_pageMeta[idx].height  = h;
    m_pageMeta[idx].isSpread = (h > 0 && static_cast<double>(w) / h > SPREAD_RATIO);
    if (m_isScrollStrip && m_stripCanvas)
        m_stripCanvas->updatePageDimensions(idx, w, h);
}, Qt::QueuedConnection);
```

**Note for Agent 1:** `ScrollStripCanvas` needs `updatePageDimensions(int index, int w, int h)` — recalculate page height for that index, trigger a partial reflow. This eliminates the layout jump that currently appears when pages decode in scroll strip mode.

---

## Unimplemented Feature: M3 — Memory saver mode

**What Max does:** `PAGE_CACHE_BUDGET_MEMORY_SAVER` constant. Toggle in settings switches budget between 512MB and 256MB.

**What Groundwork does:** `memory_saver: bool = False`. MegaSettingsOverlay button toggles it. On toggle: cache evicts immediately with new budget. Toast shown.

**Implement?** Yes — `PageCache::setBudget()` already exists. UI wiring only:

```cpp
// ComicReader.h — add member
bool m_memorySaver = false;

// ComicReader.cpp
void ComicReader::toggleMemorySaver()
{
    m_memorySaver = !m_memorySaver;
    m_cache.setBudget(m_memorySaver ? 256LL * 1024 * 1024 : 512LL * 1024 * 1024);
    showToast(m_memorySaver ? "Memory saver on (256 MB)" : "Memory saver off (512 MB)");
}
```

Wire into the right-click settings menu (already exists in `contextMenuEvent`). Persist via `QSettings("reader_memory_saver")`, restore in constructor. Three lines of logic.

---

## Summary

| Item | Status | Action for Agent 1 |
|------|--------|--------------------|
| R1 Natural sort | No gap — QCollator numeric mode is correct | None |
| R2 Format detection | No standalone gap — covered by R3 fix | None |
| R3 EXIF rotation | Gap — QPixmap skips autoTransform | Rewrite DecodeTask::run() with QImageReader |
| R4 Stale decode | Gap — old-volume pixmaps pollute new-volume cache | Add m_volumeId to ComicReader + DecodeTask |
| R5 Loading indicator | Gap — no feedback + main thread blocks | Add m_statusLabel in openBook(); full async preferred |
| R6 LRU MB budget | No gap — already byte-budget LRU | None |
| M1 CBR/RAR | Not implemented | libarchive conditional compile in ArchiveReader |
| M2 Fast dimensions | Parser exists, not wired | Emit dimensionsReady from DecodeTask; add updatePageDimensions to ScrollStripCanvas |
| M3 Memory saver | Not implemented | toggleMemorySaver() + setBudget() — 3 lines |
