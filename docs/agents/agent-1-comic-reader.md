# Agent 1: Comic Reader

You are Agent 1. You own the **Comic Reader** — the fullscreen CBZ page viewer.

## Your Mission
Enhance the existing Comic Reader MVP into a full-featured comic reading experience.

## Current State
The MVP exists at:
- `src/ui/readers/ComicReader.h/.cpp` — fullscreen overlay, single-page view, keyboard nav, auto-hide toolbar
- `src/core/ArchiveReader.h/.cpp` — static CBZ page listing/extraction via QZipReader
- `src/ui/pages/SeriesView.h/.cpp` — issue list (click issue → opens reader)
- `src/ui/pages/ComicsPage.h/.cpp` — tile grid with QStackedWidget swapping grid ↔ series view

The reader currently supports: single-page fit-to-window, arrow/Space/wheel navigation, page counter toolbar, Escape to close, next-page prefetch.

## Your Files (YOU OWN THESE)
- `src/ui/readers/ComicReader.h` / `ComicReader.cpp`
- `src/core/ArchiveReader.h` / `ArchiveReader.cpp`
- `src/ui/pages/SeriesView.h` / `SeriesView.cpp`
- Any new files under `src/ui/readers/` (e.g., overlays, widgets)

## DO NOT TOUCH
- `src/ui/MainWindow.h` / `MainWindow.cpp` — shared, Agent 1 coordination only
- `src/main.cpp` — shared
- `CMakeLists.txt` — shared (tell the user what to add, don't edit it yourself)
- `src/ui/pages/ComicsPage.h` / `ComicsPage.cpp` — only if absolutely needed for signal changes
- `src/core/CoreBridge.*` / `src/core/JsonStore.*` — shared data layer
- Anything under `src/ui/pages/Books*`, `src/ui/pages/Videos*`, `src/ui/pages/Tile*`
- Anything under `src/ui/player/`

## Feature Roadmap

### Phase 1: Core Features
1. **Progress persistence** — save current page per book via CoreBridge::saveProgress("comics", bookId, {page, pageCount}). Restore on reopen.
2. **Double-page mode** — side-by-side spread view. M key toggles. Detect spreads (aspect ratio > 1.08). Cover page always alone.
3. **Fit modes** — cycle through fit-width / fit-height / fit-page with F key
4. **Go-to-page** — Ctrl+G opens a small overlay to jump to a specific page number
5. **Volume navigation** — pass the series CBZ list to the reader, add prev/next volume buttons

### Phase 2: Polish
1. **Keyboard help overlay** (K key)
2. **End-of-volume overlay** — when reaching last page, prompt to open next issue
3. **Bookmarks** (B key to toggle, visual indicator on bookmarked pages)
4. **Reading direction** — LTR/RTL toggle (I key), affects page turn direction in double-page mode
5. **Scrub bar** — clickable page position bar at the bottom
6. **Auto-scroll** — timed page advance

### Phase 3: Advanced
1. **Image effects** — brightness, contrast, invert, grayscale, sepia
2. **Loupe magnifier** (L key) — zoom circle following cursor
3. **Gutter shadow** between pages in double-page mode
4. **LRU page cache** with memory budget (~512MB)

## Build System
- Qt6 at `C:\tools\qt6sdk\6.10.2\msvc2022_64`
- MSVC 2022 Build Tools
- `build_and_run.bat` compiles and launches
- `Qt6::CorePrivate` linked for QZipReader (use `#ifdef HAS_QT_ZIP`)
- Kill `Tankoban.exe` via tray before rebuilding (or `TASKKILL //F //IM Tankoban.exe`)

## Architecture Notes
- ComicReader is a child widget of MainWindow's root (overlay pattern, like RootFoldersOverlay)
- MainWindow calls `openComicReader(cbzPath)` and `closeComicReader()`
- Use CoreBridge for progress: `bridge->saveProgress("comics", itemId, data)` / `bridge->progress("comics", itemId)`
- Book IDs: use SHA1 hash of the CBZ file path for stable identification
- All styling uses the noir glass aesthetic — rgba backgrounds, subtle borders, Segoe UI font

## Reference
The original Python reader is at `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\readers\comic_reader.py` (4,787 lines). Use it as a reference for behavior, not for code structure.
