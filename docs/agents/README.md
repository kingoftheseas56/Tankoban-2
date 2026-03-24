# Tankoban 2 — Multi-Agent Workflow

## How It Works
Each agent owns a feature area and its files. They work independently.
The user (Hemanth) merges shared files (MainWindow, CMakeLists) after each agent finishes.

## Agents

| Agent | Area | Brief | Status |
|-------|------|-------|--------|
| 1 | Comic Reader | [agent-1-comic-reader.md](agent-1-comic-reader.md) | Active — Phase A done, working on Phase B |
| 2 | Book Reader | [agent-2-book-reader.md](agent-2-book-reader.md) | MVP done |
| 3 | Video Player | [agent-3-video-player.md](agent-3-video-player.md) | MVP done |
| 4 | Stream & Sources | [agent-4-stream-sources.md](agent-4-stream-sources.md) | SourcesPage + TankorentPage scaffolded |
| 5 | Library UX | [agent-5-library-ux.md](agent-5-library-ux.md) | Not started — URGENT: fix folder grouping bug |

## Shared Files (NO AGENT TOUCHES THESE)
These files are modified only by the user or by explicit coordination:
- `src/ui/MainWindow.h` / `MainWindow.cpp`
- `src/main.cpp`
- `CMakeLists.txt`
- `src/core/CoreBridge.h` / `CoreBridge.cpp`
- `src/core/JsonStore.h` / `JsonStore.cpp`

## How to Start an Agent
Open a new Claude Code conversation and say:
> You're Agent [N]. Read `docs/agents/agent-[N]-[name].md` for your brief.

The agent will read its brief and know exactly what to build, what files to own, and what to stay away from.

## Merging
After an agent finishes a phase:
1. They tell you what files they created
2. They tell you what lines to add to `CMakeLists.txt`
3. They tell you what to wire in `MainWindow.cpp`
4. You make those shared-file changes (or ask Agent 1 to do it since that's this conversation)

## Project Layout
```
src/
├── core/               # Data layer
│   ├── CoreBridge.*     # SHARED
│   ├── JsonStore.*      # SHARED
│   ├── ArchiveReader.*  # Agent 1
│   ├── LibraryScanner.* # Agent 5 (comics scanning)
│   ├── BooksScanner.*   # Agent 5 (books scanning)
│   └── VideosScanner.*  # Agent 5 (videos scanning)
├── ui/
│   ├── MainWindow.*     # SHARED
│   ├── GlassBackground.*
│   ├── RootFoldersOverlay.*
│   ├── pages/
│   │   ├── ComicsPage.*       # Agent 1 (minor changes only)
│   │   ├── BooksPage.*        # Agent 2 (minor changes only)
│   │   ├── VideosPage.*       # Agent 3 (minor changes only)
│   │   ├── SeriesView.*       # Agent 1
│   │   ├── BookSeriesView.*   # Agent 2
│   │   ├── SourcesPage.*      # Agent 4
│   │   ├── TankorentPage.*    # Agent 4
│   │   ├── TileCard.*         # Agent 5
│   │   └── TileStrip.*        # Agent 5
│   ├── readers/               # Agent 1 & 2
│   │   ├── ComicReader.*      # Agent 1
│   │   ├── PageCache.*        # Agent 1
│   │   ├── DecodeTask.*       # Agent 1
│   │   ├── SmoothScrollArea.* # Agent 1
│   │   ├── BookReader.*       # Agent 2
│   │   └── BookBridge.*       # Agent 2
│   └── player/                # Agent 3
│       ├── VideoPlayer.*
│       ├── FrameCanvas.*
│       └── FfmpegDecoder.*
```
