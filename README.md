# Tankoban

A unified media library + player for Windows. Watch videos, read comics + books, stream from Stremio addons, and manage torrents — all in one Qt6 desktop app.

> **Status:** active development, pre-1.0. Built for Windows 11 + MSVC 2022 + Qt 6.10. Public Releases pipeline live (`scripts/version-bump.ps1 -Version 0.X.Y` + tag push triggers `Tankoban-Setup-vX.Y.Z.exe` build).

---

## What's in the box

- **Video player** — native Qt6 player backed by an FFmpeg sidecar process. libplacebo HDR + tone-mapping for HDR10/Dolby Vision content, libass-rendered subtitles, A/V sync clock with stall pause/resume, mpv-grade frame pacing.
- **Comic reader** — CBZ + CBR + folder-of-images. Double-page + scroll-strip modes. Mihon-grade UX with YACReader polish overlay.
- **Book reader** — EPUB rendering with Edge TTS audiobook narration, audiobook-to-ebook chapter pairing, and per-show resume position.
- **Stream mode** — Stremio addon protocol support. Browse catalogs from any Stremio addon, play torrents via the bundled Stremio `stream-server` runtime, continue-watching synced across modes.
- **Tankorent** — local torrent client. libtorrent-rasterbar 2.0 backend, magnet links, DHT, sequential download for in-progress streaming.
- **Tankoyomi** — manga discovery + download (WeebCentral, ReadComics).
- **TankoLibrary** — book discovery + download (LibGen with EPUB-only filter; Anna's Archive scraper available, currently captcha-gated).

The apps share one library scanner + one continue-watching store, so a video paused at 15:32 on the Videos page picks up at 15:32 from the Stream page (and vice versa).

---

## Getting it running

### End users

Pre-built `Tankoban-Setup.exe` ships in a future release. Until then, build from source — see below.

### Developers / from source

See [BUILD.md](BUILD.md) for the full guide. Short version:

1. Install Qt 6.10.2 + MSVC 2022 Build Tools (manual one-time prereqs).
2. Clone this repo.
3. Run `setup.bat` once — it auto-clones vcpkg if needed, installs libtorrent + Boost + OpenSSL via `vcpkg.json`, and runs the first cmake configure (~30 min first run; cached after).
4. After setup, normal dev cycle is `build_and_run.bat`.

---

## Architecture

High-level component map: [ARCHITECTURE.md](ARCHITECTURE.md). The TL;DR:

```
Tankoban.exe (Qt6 GUI) ──── stdin/stdout JSON ──── ffmpeg_sidecar.exe (decode + render)
                       │
                       ├── Stream mode  ─────────── stream-server.exe (Stremio runtime, subprocess)
                       │
                       └── libtorrent-rasterbar (in-process: Tankorent + Stream torrents)
```

A separate dev-control bridge (`tankoctl.exe`) is shipped for development smoke testing — gated behind a `--dev-control` flag, never advertised in production builds.

---

## Repository layout

```
src/                    Qt6 main app source
native_sidecar/         FFmpeg sidecar process (separate cmake build)
tools/                  Development tools (tankoctl console client)
scripts/                Build + smoke + lint helpers
resources/              Icons, QSS, embedded book/comic/video reader assets
.github/workflows/      CI (lint; full Windows-build CI lands with vcpkg phase)
agents/                 Internal multi-agent coordination scratchpad — see CONTRIBUTING.md for context
```

---

## Contributing

External contributors welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for code-style + PR conventions.

The `agents/` directory holds internal state for an LLM-agent-driven development workflow. It's not load-bearing for outside contributors and can be ignored when reading the source.

---

## License

MIT — see [LICENSE](LICENSE).

Tankoban depends on libraries with their own licenses (Qt under LGPL, libtorrent-rasterbar under BSD, FFmpeg under LGPL+GPL depending on build flags, Stremio's stream-server under MIT). Distributing pre-built binaries respects each upstream license; building from source is unrestricted.
