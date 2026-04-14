# Cross-Agent Contracts

Append-only. Never delete entries — mark `[DEPRECATED]` if superseded. These are the interface specs that caused real build breaks when violated.

---

## Progress Save Payloads

All readers save progress via `CoreBridge::saveProgress(domain, key, data)`.

### Comics (Agent 1 → CoreBridge → Agent 5)
```cpp
domain: "comics"
key:    SHA1(absoluteFilePath).toHex().left(20)

QJsonObject data;
data["page"]      = currentPage;       // int, 0-based
data["pageCount"] = totalPages;        // int
data["path"]      = m_cbzPath;         // QString, absolute file path — REQUIRED for continue strip
```

### Books (Agent 2 → CoreBridge → Agent 5)
```cpp
domain: "books"
key:    SHA1(absoluteFilePath).toHex().left(20)

QJsonObject data;
data["chapter"]        = currentChapter;   // int, 0-based
data["chapterCount"]   = totalChapters;    // int
data["scrollFraction"] = scrollFraction;   // double 0.0–1.0
data["percent"]        = percentRead;      // double 0.0–100.0
data["finished"]       = isFinished;       // bool
data["path"]           = filePath;         // QString, absolute — REQUIRED for continue strip
data["bookmarks"]      = bookmarksArray;   // QJsonArray of chapter indices
```

### Videos (Agent 3 → CoreBridge → Agent 5)
```cpp
domain: "videos"
key:    SHA1((absoluteFilePath + "::" + fileSize + "::" + lastModifiedMs).toUtf8()).toHex()
        // full hex string — NOT .left(20)

QJsonObject data;
data["positionSec"] = positionSec;    // double
data["durationSec"] = durationSec;   // double
data["path"]        = currentFilePath; // QString, absolute — REQUIRED for continue strip
```

The `path` field is non-negotiable in all three domains. Without it, Agent 5's continue strips cannot map a progress hash back to a file.

---

## Thumbnail Cache Key Format

Used by LibraryScanner (Agent 5) when generating per-file cover thumbnails. Any agent that needs to look up a cover thumbnail must use this format:

```cpp
// Per-file cover (used by continue strips, SeriesView, BookSeriesView)
QString key = QCryptographicHash::hash(
    (absoluteFilePath + "::" + fileSize + "::" + lastModifiedMs).toUtf8(),
    QCryptographicHash::Sha1
).toHex().left(20);
// Cache path: QStandardPaths::AppLocalDataLocation + "/data/thumbs/" + key + ".jpg"
```

Thumbnail dimensions: 240x369 px (ratio 0.65 — matches groundwork `cover_ratio`).

---

## Constructor Signatures (Breaking Changes History)

These signatures caused real `C2512` linker errors when callers used the wrong form. Reference before calling.

### SeriesView (Agent 5)
```cpp
// Header: src/ui/pages/SeriesView.h
SeriesView(CoreBridge* bridge, QWidget* parent = nullptr);
// showSeries() signature:
void showSeries(const QStringList& cbzPaths, const QString& seriesName,
                const QString& coverThumbPath = QString());
// Signals:
void backRequested();
void issueSelected(const QString& cbzPath, const QStringList& seriesCbzList,
                   const QString& seriesName);
```

### ShowView (Agent 5)
```cpp
// Header: src/ui/pages/ShowView.h
ShowView(CoreBridge* bridge, QWidget* parent = nullptr);
// showFolder() signature:
void showFolder(const QString& folderPath, const QString& coverThumbPath = QString());
// Signals:
void backRequested();
void episodeSelected(const QString& filePath);
```

### TankorentPage (Agent 4)
```cpp
// Header: src/ui/pages/TankorentPage.h
TankorentPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent = nullptr);
```

### SourcesPage (Agent 4)
```cpp
// Header: src/ui/pages/SourcesPage.h
SourcesPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent = nullptr);
```

### BookSeriesView (Agent 2)
```cpp
// Header: src/ui/pages/BookSeriesView.h
// showSeries() signature (optional coverThumbPath added by Agent 5):
void showSeries(const QStringList& filePaths, const QString& seriesName,
                const QString& coverThumbPath = QString());
```

---

## Cross-Agent Signals

Signals that cross subsystem boundaries — both sides must agree on the signature.

### VideosPage → MainWindow (Agent 5 → Agent 3)
```cpp
// Declared in VideosPage.h
signals:
    void playVideo(const QString& filePath);
// Connected in MainWindow.cpp to VideoPlayer::openVideo()
```

### ComicsPage → MainWindow (Agent 1 → Agent 0)
```cpp
// Declared in ComicsPage.h
signals:
    void fullscreenRequested(bool enter);
// Connected in MainWindow.cpp (additive, Agent 1 wired it)
```

### BooksPage → MainWindow (Agent 2 → Agent 0)
```cpp
// Declared in BooksPage.h
signals:
    void openBook(const QString& filePath);
// Connected in MainWindow.cpp to BookReader::openBook()
```

### TorrentClient → LibraryScanner (Agent 4 → Agent 5)
When a torrent download completes, Agent 4 should emit `rootFoldersChanged` so Agent 5's scanners auto-trigger. This signal is owned by CoreBridge. Agent 4 calls `m_bridge->emitRootFoldersChanged()` (or equivalent) on torrent completion. Agent 5 scanners listen to it.

---

## Shared File Touch Rules

These files require coordination before editing. See GOVERNANCE.md for the announce-before-touch rule.

| File | Rule |
|------|------|
| `CMakeLists.txt` | Post exact lines added in chat.md. No silent additions. |
| `src/ui/MainWindow.h` | Additive only. No removal of existing members. |
| `src/ui/MainWindow.cpp` | Additive only. Announce before editing. |
| `resources/resources.qrc` | Additive only. |

---

## Build Verification Rule

**Agents must NOT attempt to run builds from bash to verify their work.**

The Windows MSVC build environment (vcvarsall.bat, Ninja, cl.exe) does not work reliably from the bash shell agents run in. Attempting to self-verify causes: zombie cl.exe/ninja.exe processes, 30+ minute hangs, false failures, and wasted session time.

**The rule:**
- Write your code changes
- Post your completion summary in chat.md
- Stop. Do not run cmake, ninja, build_and_run.bat, or any build command.

**Who builds:**
- Hemanth runs `build_and_run.bat` natively to verify
- Agent 3 is the build gate for congress component integration — they run the build after wiring each component

If a build break is discovered, Hemanth or Agent 0 will route it back to the responsible agent. You do not need to verify it yourself.

---

## Build Configuration

- **Build type:** Release (required when libtorrent is enabled — it is MSVC Release-built)
- **libtorrent guard:** Wrapped in `if(CMAKE_BUILD_TYPE STREQUAL "Release")` — Debug builds skip it cleanly
- **Build directory:** `out/` is the canonical build. `out2/` and `out_test/` are for experiments only.
- **Qt modules in use:** `Qt6::Core`, `Qt6::Widgets`, `Qt6::Gui`, `Qt6::Network`, `Qt6::Svg`, `Qt6::OpenGL`, `Qt6::OpenGLWidgets`, `Qt6::GuiPrivate` (for QRhiWidget)
