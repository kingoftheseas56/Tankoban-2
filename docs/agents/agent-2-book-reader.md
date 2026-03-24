# Agent 2: Book Reader

You are Agent 2. You own the **Book Reader** — EPUB/PDF/TXT rendering.

## Your Mission
Build a book reader that can open and display EPUB, PDF, and plain text files from the Books library page.

## Current State
- `src/ui/pages/BooksPage.h/.cpp` — tile grid showing book series with covers
- `src/core/BooksScanner.h/.cpp` — scans for epub/pdf/mobi/fb2/txt files, extracts EPUB covers
- Books page has no click-to-open behavior yet (tiles don't do anything when clicked)
- No reader exists yet — you're building from scratch

## Your Files (YOU OWN THESE)
- `src/ui/readers/BookReader.h` / `BookReader.cpp` (create these)
- Any new files under `src/ui/readers/` prefixed with `Book` or `Epub` or `Pdf`
- You may create `src/ui/pages/BookSeriesView.h/.cpp` if you need an issue list (similar to SeriesView for comics)

## DO NOT TOUCH
- `src/ui/MainWindow.h` / `MainWindow.cpp` — shared (tell the user what signals to wire)
- `src/main.cpp` — shared
- `CMakeLists.txt` — shared (tell the user what to add)
- `src/core/CoreBridge.*` / `src/core/JsonStore.*` — shared data layer (read-only usage)
- `src/ui/readers/ComicReader.*` — Agent 1's territory
- `src/ui/pages/ComicsPage.*` / `src/ui/pages/Videos*`
- Anything under `src/ui/player/`

## Approach

### EPUB Reader
EPUBs are ZIP archives containing HTML/CSS/images. The best Qt6 approach:
- Use `QWebEngineView` to render EPUB HTML content (requires `Qt6::WebEngineWidgets`)
- The user's Qt6 install may NOT have WebEngineWidgets — check `#ifdef HAS_WEBENGINE`
- **Fallback if no WebEngine**: extract text content and display in a `QTextBrowser`
- Use the existing `ArchiveReader` (or QZipReader directly) to extract EPUB contents

### PDF Reader
- Qt6 does not have a built-in PDF renderer for widgets
- Options: use `QPdfDocument` + `QPdfView` (Qt6::Pdf module), or shell out to an external viewer
- Check if `Qt6::Pdf` is available, otherwise show a "Open in external viewer" button

### TXT Reader
- Simple `QTextBrowser` or `QPlainTextEdit` with the noir stylesheet applied
- File loaded with `QFile::readAll()`

### Reader UI
- Fullscreen overlay (same pattern as ComicReader — child of MainWindow root widget)
- Dark background matching the app theme
- Toolbar: back button, chapter nav, page/progress indicator
- TOC sidebar (for EPUB)
- Escape to close

## Feature Roadmap

### Phase 1: Basic EPUB
1. Click a book tile → open EPUB in fullscreen reader
2. Render EPUB HTML via WebEngine (or fallback QTextBrowser)
3. Chapter navigation (prev/next)
4. TOC sidebar
5. Progress persistence via CoreBridge

### Phase 2: PDF + TXT
1. PDF rendering (QPdfView or external)
2. TXT rendering (QTextBrowser)
3. Route by file extension on open

### Phase 3: Polish
1. Font size control
2. Search in book
3. Bookmarks
4. Night/sepia reading themes

## Build System
- Qt6 at `C:\tools\qt6sdk\6.10.2\msvc2022_64`
- MSVC 2022 Build Tools
- `build_and_run.bat` compiles and launches
- WebEngine status: check `#ifdef HAS_WEBENGINE` — may not be available
- Kill `Tankoban.exe` before rebuilding

## Integration Points
When ready to wire into the app, tell the user to:
1. Add your files to `CMakeLists.txt`
2. Add `#include "readers/BookReader.h"` in `MainWindow.cpp`
3. Create `m_bookReader = new BookReader(root)` in MainWindow constructor
4. Connect `BooksPage::openBook(filePath)` signal → `MainWindow::openBookReader(filePath)`
5. Add resize handling in `MainWindow::resizeEvent()`

## Reference
Original Python book reader resources at `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\resources\book_reader\` (HTML/JS/CSS). The Python reader uses a WebView to render foliate.js — a JS EPUB engine.
