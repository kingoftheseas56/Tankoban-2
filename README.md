# Tankoban

A unified media library for **Comics, Books, and Theatre** — read manga and comics, read ebooks and listen to audiobooks, and download video to watch locally, all in one Qt6 desktop app.

> **Status:** active development, pre-1.0.
> **Windows** is the current build target (full build CI + a tag-driven installer pipeline are live).
> **macOS** is a planned equal first-class target — cross-platform support is on the roadmap; today the app builds and runs on Windows only.

---

## The three modes

Tankoban is organized around three modes. Each mode has a built-in reader/player and its own source-and-download capability beneath it.

- **Comics** — read manga and comics (CBZ, CBR, or folders of images) with double-page and scroll-strip modes. *Source:* **Tankoyomi** discovers and downloads series from manga sources (e.g. WeebCentral).
- **Books** — read EPUBs, with Edge-TTS audiobook narration and audiobook↔ebook chapter pairing. *Source:* **TankoLibrary** discovers and downloads titles from shadow-library sources (e.g. LibGen).
- **Theatre** — browse video catalogs (via Stremio addons) and **download** titles to watch locally — download-exclusive, the same shape as Comics and Books. Playback runs on a native Qt6 player backed by an FFmpeg sidecar (libplacebo HDR tone-mapping, libass subtitles, A/V-sync clock). *Source:* **Tankorent**, an in-process libtorrent-rasterbar 2.0 engine (magnets, DHT), plus the bundled Stremio `stream-server` runtime for catalog + source resolution.

All three modes share one library scanner and one continue-watching/-reading store, so progress follows you across the app — stop partway through and pick up later from the same spot.

---

## Getting it running

### End users

There is no public pre-built installer yet (pre-1.0). The release pipeline is live — a version tag builds `Tankoban-Setup-vX.Y.Z.exe` — but until a public release is cut, build from source (below). macOS builds are not available yet.

### Developers / from source (Windows)

See [BUILD.md](BUILD.md) for the full guide. Short version:

1. Install Qt 6.10.2 + MSVC 2022 Build Tools (manual one-time prereqs).
2. Clone this repo.
3. Run `setup.bat` once — it auto-clones vcpkg if needed, installs libtorrent + Boost + OpenSSL via `vcpkg.json`, and runs the first cmake configure (~30 min first run; cached after).
4. After setup, the normal dev cycle is `build_and_run.bat`.

---

## Architecture

High-level component map: [ARCHITECTURE.md](ARCHITECTURE.md). The TL;DR:

```
Tankoban.exe (Qt6 GUI) ──── stdin/stdout JSON ──── ffmpeg_sidecar.exe (decode + render)
                       │
                       ├── Theatre  ────────────── stream-server.exe (Stremio catalog/source runtime, subprocess)
                       │
                       └── libtorrent-rasterbar (in-process: Tankorent + Theatre torrents)
```

The ~90% of the app that is pure Qt/C++ (Comics, Books, Theatre business logic, scrapers, library) is platform-independent; the Windows-specific surface is concentrated in the video decode/render layer (sidecar + player), which is the focus of the planned macOS port.

A separate dev-control bridge (`tankoctl.exe`) ships for development smoke testing — gated behind a `--dev-control` flag, never advertised in production builds.

---

## Repository layout

```
src/                    Qt6 main app source (core/ + ui/ + devtools/)
native_sidecar/         FFmpeg sidecar process (separate cmake build)
tests/                  GoogleTest suite (opt-in via -DTANKOBAN_BUILD_TESTS=ON)
tools/                  Development tools (tankoctl console client)
scripts/                Build + smoke + lint helpers
resources/              Icons, QSS, embedded reader assets + runtime payloads
docs/                   Project documentation — start at docs/README.md
.github/workflows/      CI: build.yml (full Windows build), release.yml (installer), repo-consistency.yml (lint)
agents/                 Internal multi-agent coordination state — see CONTRIBUTING.md; external readers can ignore it
```

---

## Contributing

External contributors welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for code-style + PR conventions.

The `agents/` directory holds internal state for an LLM-agent-driven development workflow. It is not load-bearing for outside contributors and can be ignored when reading the source.

---

## License

MIT — see [LICENSE](LICENSE).

Tankoban depends on libraries with their own licenses (Qt under LGPL, libtorrent-rasterbar under BSD, FFmpeg under LGPL+GPL depending on build flags, Stremio's stream-server under MIT). Distributing pre-built binaries respects each upstream license; building from source is unrestricted.
