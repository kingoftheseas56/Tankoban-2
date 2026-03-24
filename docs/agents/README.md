# Tankoban 2 — Multi-Agent Workflow

## How It Works
Each agent owns a feature area and its files. They work independently.
The user (Hemanth) merges shared files (MainWindow, CMakeLists) after each agent finishes.

## Agents

| Agent | Area | Brief | Status |
|-------|------|-------|--------|
| 1 | Comic Reader | [agent-1-comic-reader.md](agent-1-comic-reader.md) | Active — MVP done, enhancing |
| 2 | Book Reader | [agent-2-book-reader.md](agent-2-book-reader.md) | Not started |
| 3 | Video Player | [agent-3-video-player.md](agent-3-video-player.md) | Not started |
| 4 | Stream & Sources | [agent-4-stream-sources.md](agent-4-stream-sources.md) | Not started |

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
├── core/               # Data layer (shared)
│   ├── CoreBridge.*
│   ├── JsonStore.*
│   ├── ArchiveReader.*  # Agent 1
│   ├── LibraryScanner.* # Shared (comics scanning)
│   ├── BooksScanner.*   # Shared (books scanning)
│   └── VideosScanner.*  # Shared (videos scanning)
├── ui/
│   ├── MainWindow.*     # SHARED — coordination only
│   ├── GlassBackground.*
│   ├── RootFoldersOverlay.*
│   ├── pages/
│   │   ├── ComicsPage.* # Agent 1 (minor changes only)
│   │   ├── BooksPage.*  # Agent 2 (minor changes only)
│   │   ├── VideosPage.* # Agent 3 (minor changes only)
│   │   ├── SeriesView.* # Agent 1
│   │   ├── TileCard.*   # Shared widget
│   │   └── TileStrip.*  # Shared widget
│   ├── readers/         # Agent 1 & 2
│   │   ├── ComicReader.* # Agent 1
│   │   └── BookReader.*  # Agent 2
│   └── player/          # Agent 3
│       ├── VideoPlayer.*
│       └── MpvWidget.*
```
