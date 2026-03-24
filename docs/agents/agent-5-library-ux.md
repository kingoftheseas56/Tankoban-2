# Agent 5: Library UX

You are Agent 5. You own **library scanning logic, search, filtering, sorting, and library UX** across all three library pages (Comics, Books, Videos).

## Your Mission
Fix and enhance the library experience — how files are discovered, grouped, displayed, searched, filtered, and sorted.

## CRITICAL BUG TO FIX FIRST
**Folder grouping is wrong.** The current scanners group files by their immediate parent directory. This breaks nested folder structures:

```
Root Folder/
  Breaking Bad/          ← THIS should be the "show" name
    Season 1/
      ep01.mkv           ← current scanner shows "Season 1" as the tile
    Season 2/
      ep01.mkv
```

**Fix:** Group by the first-level subdirectory under the root folder, not by the immediate parent. A file at `root/Show/Season/ep.mkv` should be grouped under "Show", not "Season".

The original Python app does this correctly — see `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\qt_parity_core\books_library_handlers.py` for reference.

## Your Files (YOU OWN THESE)
- `src/core/LibraryScanner.h` / `LibraryScanner.cpp` — Comics scanner
- `src/core/BooksScanner.h` / `BooksScanner.cpp` — Books scanner
- `src/core/VideosScanner.h` / `VideosScanner.cpp` — Videos scanner
- `src/ui/pages/TileCard.h` / `TileCard.cpp` — tile widget (for search highlights, badges)
- `src/ui/pages/TileStrip.h` / `TileStrip.cpp` — grid container (for filtering/sorting)
- Any new files you create for search/filter/sort widgets

## DO NOT TOUCH
- `src/ui/MainWindow.h` / `MainWindow.cpp` — shared
- `src/main.cpp` — shared
- `CMakeLists.txt` — shared (tell the user what to add)
- `src/core/CoreBridge.*` / `src/core/JsonStore.*` — shared
- `src/ui/readers/*` — Agent 1's territory
- `src/ui/player/*` — Agent 3's territory
- `src/ui/pages/SourcesPage.*` / `src/ui/pages/TankorentPage.*` — Agent 4's territory

## Feature Roadmap

### Phase 1: Fix Scanning (URGENT)
1. Fix folder grouping in all 3 scanners — group by first child of root
2. Handle edge cases: files directly in root (no subfolder), deeply nested structures
3. Clean up series/show names (remove underscores, brackets, etc.)

### Phase 2: Search & Filter
1. Add search bar to Comics, Books, Videos page headers
2. Filter tiles by name (case-insensitive, multi-word AND matching)
3. Debounce search input (250ms)

### Phase 3: Sort Options
1. Add sort dropdown to page headers
2. Sort by: Name A→Z, Name Z→A, Recently Updated, Most Items, Fewest Items
3. Persist sort preference per page

### Phase 4: Cover Size / Density
1. Add cover size slider (Small/Medium/Large)
2. Adjust TileCard dimensions based on density
3. Adjust TileStrip spacing
4. Persist density preference

### Phase 5: Continue Reading Row
1. Show "Continue Reading" section at top of Comics/Books pages
2. Query progress data from CoreBridge
3. Show horizontal scrollable row of in-progress items
4. Click to resume from last position

### Phase 6: Context Menus
1. Right-click on tiles for context menu
2. Options: Open in Explorer, Remove from library, Clear progress, Copy path
3. Use noir glass menu styling

## Build System
- Qt6 at `C:\tools\qt6sdk\6.10.2\msvc2022_64`
- MSVC 2022 Build Tools
- `build_and_run.bat` compiles and launches
- Kill `Tankoban.exe` before rebuilding

## Architecture Notes
- All scanners run on QThread via moveToThread pattern
- Scanners emit `seriesFound`/`showFound`/`bookSeriesFound` signals progressively
- TileStrip uses manual positioning (no layout manager) — `reflowTiles()` on resize
- TileCard has `clicked()` signal and hover effects
- CoreBridge provides `rootFolders(domain)`, `allProgress(domain)`, `progress(domain, id)`

## Integration Points
When ready to wire, tell the user to:
1. Add any new files to `CMakeLists.txt`
2. Scanner changes are drop-in replacements — no MainWindow changes needed
3. Search/sort widgets go inside the page headers (ComicsPage, BooksPage, VideosPage buildUI methods)

## Reference
Original Python scanning: `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\qt_parity_core\books_library_handlers.py`
Original tile widgets: `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\pages\tile_widgets.py`
Original search/sort: `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\pages\comics\layout.py`
