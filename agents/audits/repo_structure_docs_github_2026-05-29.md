# Audit - repo structure, docs, and GitHub presentation - 2026-05-29

By Agent 7 (Codex). For Agent 0 / Hemanth.
Reference comparison: current Tankoban tree, GitHub README / repository metadata docs, and GitHub license guidance.
Scope: Structure, documentation layout, and public repository presentation only. No files were moved or edited outside this audit. This does not assess the future macOS port; recommendations only prefer boundaries that keep platform-specific player / sidecar code isolated as normal engineering practice.

## Observed behavior (in our codebase)

### Source and build layout

- The repo already has a recognizable application shape: `src/`, `native_sidecar/`, `tests/`, `resources/`, `scripts/`, `tools/`, `.github/workflows/`, and root build docs. README documents this shape at `README.md:56-66`.
- The main Qt app source is separated into `src/core`, `src/ui`, and `src/devtools`; current directories include domain subtrees such as `src/core/book`, `src/core/manga`, `src/core/stream`, `src/core/torrent`, `src/ui/pages/books`, `src/ui/pages/comics`, `src/ui/pages/stream`, `src/ui/player`, and `src/ui/readers` from the live tree inventory.
- The main CMake file owns one large flat source list beginning at `CMakeLists.txt:41`, with the stream entries continuing through `CMakeLists.txt:270`; headers are listed in a second flat block from `CMakeLists.txt:273-479`. The executable consumes those variables at `CMakeLists.txt:487-491`.
- The main target includes only `${CMAKE_SOURCE_DIR}/src` at `CMakeLists.txt:493-495`. This makes includes stable while files remain somewhere under `src/`, but moving a header to a path whose relative include changes will break any include that names the old subpath.
- Tests are opt-in and also wired through the root CMake file: `enable_testing()` appears at `CMakeLists.txt:806`, and `tankoban_tests` manually lists test and production source files at `CMakeLists.txt:816-922`. Moving production files without updating the test target will break test builds even when the app target is updated.
- `native_sidecar/` is a separate CMake project. It declares `project(ffmpeg_sidecar ...)` at `native_sidecar/CMakeLists.txt:2`, uses hardcoded Windows dependency roots at `native_sidecar/CMakeLists.txt:10-20`, builds `ffmpeg_sidecar` from `native_sidecar/src/*` at `native_sidecar/CMakeLists.txt:105-123`, and links Windows / DirectX / audio libraries at `native_sidecar/CMakeLists.txt:199`.
- Sidecar docs say the sidecar is a subprocess of the main app and communicates over stdin/stdout JSON at `native_sidecar/README.md:3`; the README also lists component responsibilities at `native_sidecar/README.md:27-37`. That is a useful boundary today.
- Runtime assets are mixed under `resources/`: embedded Qt assets are listed in `resources/resources.qrc:3-54`, while other runtime payloads include `resources/book_reader`, `resources/stream_server`, `resources/ffmpeg_sidecar`, `resources/fandom_manifests`, and data JSON. The build copies book-reader and fandom-manifest resources in `build_and_run.bat:89-102`, and CMake hardcodes stream-server payload paths at `CMakeLists.txt:709-727`.
- The largest source files are concentrated in UI orchestration surfaces: `VideoPlayer.cpp` 4525 lines, `StreamPage.cpp` 4281, `ComicsPage.cpp` 4013, `ComicReader.cpp` 3770, `TorrentClient.cpp` 3377, `TankorentPage.cpp` 2934, `MainWindow.cpp` 2572, and `TankoLibraryPage.cpp` 2039 from the line-count inventory. This is not a folder-structure bug by itself, but it means moves should not be combined with behavior refactors.
- Build output is intended to be ignored: `.gitignore` covers `out/`, `out_agent*/`, `out_*/`, `native_sidecar/build/`, `vcpkg_installed/`, root logs, root scratch PNGs, and `.cc-history/` at `.gitignore:1-31` and `.gitignore:60-108`. Current `git status --short` still shows one tracked generated file deletion under `out/stremio_tune_ab_results.csv`, and the root working directory contains many local logs / scratch files.

### Docs layout

- Public-facing docs already exist at repo root: `README.md`, `BUILD.md`, `ARCHITECTURE.md`, `CONTRIBUTING.md`, and `LICENSE`. README points readers to build, architecture, contribution, and license material at `README.md:31-42`, `README.md:70-80`.
- `docs/` currently contains `docs/agents/` and `docs/superpowers/`, but no `docs/README.md` index from the live tree inventory.
- `docs/superpowers/` currently contains 116 files: 64 `plans`, 46 `specs`, 3 `mockups`, 2 `data`, and 1 `audits` file from the file inventory. File names are date-based and mostly arc-specific.
- `agents/` is substantially larger and more operational than `docs/`: tracked file inventory shows 454 files under `agents/`, including 359 under `agents/audits`, 32 under `agents/prototypes`, 26 under `agents/review_archive`, and multiple chat / congress / status archives.
- `agents/ONBOARDING.md` gives an internal 15-minute orientation path and points agents to `agents/GOVERNANCE.md`, `agents/STATUS.md`, subsystem TODOs, `agents/chat.md`, and `agents/CONTRACTS.md` at `agents/ONBOARDING.md:3-26`. This is useful internal documentation but not a public docs entry point.
- Root TODO files are still first-viewport repo artifacts: current root includes many active and historical `*_TODO.md` files, including `BOOK_READER_FIX_TODO.md`, `COMIC_READER_FIX_TODO.md`, `STREAM_SERVER_PIVOT_TODO.md`, `TANKORENT_FIX_TODO.md`, `REPO_HYGIENE_FIX_TODO.md`, and others from the root inventory.

### GitHub presentation

- README has a good public start: it states the product in one sentence at `README.md:3`, declares active pre-1.0 Windows status at `README.md:5`, lists the product surfaces at `README.md:9-19`, and gives a short source-build path at `README.md:31-36`.
- README currently describes more than the user-facing three-mode story. It lists Video player, Comic reader, Book reader, Stream mode, Tankorent, Tankoyomi, and TankoLibrary separately at `README.md:11-17`. That is accurate to subsystems but less clean than "Comics, Books, Theatre" as the public landing model.
- README says "Pre-built `Tankoban-Setup.exe` ships in a future release" at `README.md:27`, while the status line says the public release pipeline is live at `README.md:5`. This is confusing: the repo can say releases are pipeline-backed while still warning that pre-1.0 binary availability may lag.
- README says `.github/workflows/` is "CI (lint; full Windows-build CI lands with vcpkg phase)" at `README.md:64`. Current workflows show `build.yml` is already full Windows-build CI (`.github/workflows/build.yml:1-18`, `.github/workflows/build.yml:69-72`), and `release.yml` exists (`.github/workflows/release.yml:16-24`, `.github/workflows/release.yml:77-83`).
- BUILD.md is detailed but still has stale troubleshooting around manually built libtorrent at `BUILD.md:167-169`, while vcpkg is the current main dependency path at `BUILD.md:28-46` and `vcpkg.json` lists `libtorrent`, Boost system/filesystem, and OpenSSL.
- CONTRIBUTING.md already explains that `agents/` is internal and ignorable for external contributors at `CONTRIBUTING.md:80-90`, which is the right public posture.

## Reference behavior

- GitHub's README guidance says a README is commonly the first repository item visitors see and should cover what the project does, why it is useful, how to start, where to get help, and who maintains it. Source: https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes lines 175-183.
- GitHub recommends relative links for repository files because they keep working across branches and clones. Source: https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes lines 198-211.
- GitHub's README guidance says long-form documentation is better outside the README, while the README should keep the getting-started path concise. Source: https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes lines 214-215.
- GitHub repository topics exist to help people find projects by purpose, subject, community, or language. Topic names should be lowercase / hyphenated, 50 characters or less, and no more than 20 topics. Source: https://github.com/github/docs/blob/main/content/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/classifying-your-repository-with-topics.md lines 244-283.
- GitHub surfaces a detectable license on the repository page. Source: https://github.com/github/docs/blob/main/content/communities/setting-up-your-project-for-healthy-contributions/adding-a-license-to-a-repository.md lines 242-265.

## Gaps (ranked P0 / P1 / P2)

**P0 (user-blocking or severely degrading):**

- None for this audit scope. The current structure can still build and support active development; the risk is maintainability / presentation, not immediate user breakage.

**P1 (repo-maintenance or public-presentation shortfall):**

- Root is overloaded with operational artifacts. Observed: root contains active docs, many TODOs, local logs, scratch images, build output directories, and one tracked generated `out/` file; `.gitignore` already tries to prevent most of this at `.gitignore:1-31` and `.gitignore:60-108`. Reference: GitHub README docs emphasize a concise first path for visitors. Impact: newcomers see coordination debris before the product shape.
- Source movement is currently high-risk because root CMake is the source-of-truth for almost every app, test, resource, and tool path. Observed: source and header lists at `CMakeLists.txt:41-479`, app target at `CMakeLists.txt:487-505`, tests at `CMakeLists.txt:806-922`, resources at `CMakeLists.txt:709-727`. Impact: a structural cleanup can compile-break in multiple targets unless CMake is split or updated in lockstep.
- Docs have no canonical map. Observed: root docs exist, `docs/superpowers/` has 116 files, and `agents/` has hundreds of operational docs / audits, but `docs/README.md` is absent. Reference: GitHub supports relative links and favors moving long-form docs out of README. Impact: a newcomer cannot tell which docs are current, historical, internal, or public.
- README is slightly stale and presents the app as seven surfaces rather than three primary modes. Observed: subsystem list at `README.md:11-17`, release/installer ambiguity at `README.md:5` and `README.md:27`, CI ambiguity at `README.md:64`. Impact: GitHub landing does not match the current "Comics, Books, Theatre" framing and undersells the build/release maturity already in `.github/workflows/`.

**P2 (polish / cleanup):**

- `src/ui/pages` still mixes domain roots (`ComicsPage.cpp`, `BooksPage.cpp`, `StreamPage.cpp`, `TankorentPage.cpp`, `TankoLibraryPage.cpp`) with extracted subdirectories (`pages/comics`, `pages/books`, `pages/stream`, etc.). Observed in live tree and CMake entries at `CMakeLists.txt:71-80`, `CMakeLists.txt:210-225`, and `CMakeLists.txt:253-270`. Impact: not broken, but the module boundary is visually inconsistent.
- Runtime payloads and embedded assets are both under `resources/`. Observed: `resources/resources.qrc:3-54` is embedded Qt content, while `build_and_run.bat:89-102` copies runtime folders and CMake hardcodes stream-server binaries at `CMakeLists.txt:709-727`. Impact: packaging and source review are noisier than they need to be.
- `agents/audits/` contains both markdown audit reports and smoke evidence / raw model output / working directories. Observed: inventory has 166 files under `agents/audits/smoke_evidence`, raw files like `*_gpt_raw.md`, and work dirs such as `_vlc_aspect_crop_work`. Impact: useful history, but hard to scan for authoritative audit documents.
- Public docs contain useful but duplicated status. Observed: README, BUILD, ARCHITECTURE, CONTRIBUTING, CLAUDE, ONBOARDING, TODOs, and workflow comments all restate pieces of build / state / architecture. Impact: drift is already visible in README and BUILD.

## Hypothesized root causes (Agent 0 to validate)

- **Hypothesis -** The root TODO sprawl exists because the brotherhood uses root markdown files as active planning surfaces, and completed arcs were not consistently migrated into `agents/_archive/todos/` or `docs/archive/`. **Agent 0 to validate.**
- **Hypothesis -** The flat root CMake source list was chosen to avoid glob instability and to make shared-file edits explicit, but the project has now outgrown a single manually edited list. **Agent 0 to validate.**
- **Hypothesis -** `docs/superpowers/` is functioning as a session-output inbox rather than a curated documentation tree, so the folder naturally accumulated specs, plans, mockups, audits, and data without a lifecycle gate. **Agent 0 to validate.**
- **Hypothesis -** README drift happened because Phase 7 public docs shipped before later build/release phases fully landed, and no follow-up pass reconciled the README with `.github/workflows/build.yml` / `release.yml`. **Agent 0 to validate.**

## Recommended follow-ups (advisory)

### Target source structure

Recommended end state:

```text
src/
  app/                         # main.cpp, app bootstrap, app-wide settings
  core/
    common/                    # JsonStore, CoreBridge, DebugLogBuffer, scanner primitives
    library/                   # cross-mode library scanner/category/poster code
    comics/                    # manga/catalog/download/library backend
    books/                     # epub/catalog/download/TTS backend
    theatre/                   # stream metadata, downloads, torrent-facing theatre services
    torrent/                   # libtorrent wrapper and repositories
  ui/
    shell/                     # MainWindow, navigation, overlays, app chrome
    common/                    # TileCard, TileStrip, ContextMenuHelper, shared widgets
    modes/
      comics/
      books/
      theatre/
    readers/
    player/
  devtools/
native_sidecar/
  src/
    ipc/
    media/
    render/
    platform/windows/
tests/
  core/
  ui/
  fixtures/
resources/
  qrc/                         # embedded icons/shaders and resources.qrc
  runtime/                     # copied/deployed payloads: book_reader, stream_server
  data/                        # small checked-in seed/catalog data
tools/
scripts/
cmake/
  TankobanSources.cmake
  TankobanTests.cmake
  TankobanRuntimeAssets.cmake
```

Rationale:

- Keep the sidecar separate. It is already a subprocess / separate CMake project; that is the correct boundary for platform-specific decode/render/audio code.
- Collapse public product structure around three modes: `comics`, `books`, `theatre`. Treat Tankorent / Tankoyomi / TankoLibrary as source/download capabilities under those modes rather than public top-level app identities.
- Move CMake lists into `cmake/*.cmake` before moving source files. This reduces merge pressure on `CMakeLists.txt` and makes later path updates reviewable by module.
- Do not combine file moves with large-file decomposition. For example, move `StreamPage.cpp` in one commit with no behavior change, then split it later if needed.

Paths that would break if moved carelessly:

- Any source/header in `CMakeLists.txt:41-479` or `CMakeLists.txt:816-922` must be updated in both app and test target lists.
- Any runtime resource path in `build_and_run.bat:89-102`, `CMakeLists.txt:709-727`, `BUILD.md:57`, `BUILD.md:118`, `native_sidecar/README.md:21-23`, `src/ui/readers/BookReader.cpp:54`, `src/ui/player/SidecarProcess.cpp:32-50`, and `src/core/stream/stremio/StreamServerProcess.cpp:48-98` must move as a coordinated resource-path change.
- Any embedded Qt asset path in `resources/resources.qrc:3-54` must preserve the `:/icons/...` and `:/shaders/...` virtual paths or update all `QIcon(":/icons/...")` and `QFile(":/shaders/...")` callsites.
- Any header moved outside the current subpath must update includes because the only broad include root is `${CMAKE_SOURCE_DIR}/src` at `CMakeLists.txt:493-495`.

### Target docs structure

Recommended end state:

```text
docs/
  README.md                    # canonical map: start here
  user/
    install.md
    modes-comics.md
    modes-books.md
    modes-theatre.md
  developer/
    build.md                   # either move BUILD.md here or keep root + link
    contributing.md            # either move CONTRIBUTING.md here or keep root + link
    testing-and-smoke.md
    repo-structure.md
  architecture/
    overview.md                # current ARCHITECTURE.md content
    sidecar.md
    stream-server.md
    persistence.md
  decisions/
    2026-04-26-repo-hygiene.md
    2026-05-20-source-ownership.md
  plans/
    active/
    archive/
  audits/
    curated/
    raw/
  assets/
    mockups/
    data/
agents/
  README.md                    # internal workflow entry point
  GOVERNANCE.md
  STATUS.md
  CONTRACTS.md
  chat.md
  audits/                      # current operational audit output remains allowed
  _archive/
```

Recommended policy:

- Keep root docs minimal: `README.md`, `BUILD.md`, `ARCHITECTURE.md`, `CONTRIBUTING.md`, `LICENSE` are acceptable at root for GitHub conventions. If BUILD / ARCHITECTURE / CONTRIBUTING move under `docs/`, leave root stubs or update all links.
- Add `docs/README.md` first. It should answer: "I am a user", "I am a contributor", "I am an agent", "I am reading history." One screen.
- Classify `docs/superpowers/` into lifecycle states: `active`, `reference`, `archive`, `raw`. Do not leave raw model output beside final human-facing plans without an index.
- Move completed root TODOs into `agents/_archive/todos/` or `docs/plans/archive/` depending on audience. Active TODOs can remain root only while they are operationally active; otherwise they should not dominate the GitHub file list.
- Keep `agents/` as internal coordination. Do not try to make every agent artifact public-doc polished; instead make `CONTRIBUTING.md` and `docs/README.md` clearly tell external readers they can ignore it.

### Target GitHub presentation

Recommended README structure:

```text
# Tankoban
One-sentence product line: Windows desktop media library for Comics, Books, and Theatre.

Status badges:
  build, repo-consistency, release, license

Screenshot or short GIF:
  first screen should show the app, not architecture.

## What it does
  Comics
  Books
  Theatre
  Source/download capabilities beneath those modes

## Install / Run
  Releases when available
  Source build path: setup.bat -> build_and_run.bat

## Developer Quick Start
  prerequisites summary
  build_check.bat
  tests

## Architecture
  5-line process model + link to ARCHITECTURE.md

## Repository Layout
  concise, current, no stale CI note

## Contributing
  link to CONTRIBUTING.md

## License
```

Recommended repository metadata:

- Description: `Windows Qt6 desktop media library for comics, books, and theatre playback.`
- Topics: `qt6`, `cpp20`, `windows`, `desktop-app`, `media-library`, `comics`, `epub`, `video-player`, `ffmpeg`, `libtorrent`, `stremio`, `cmake`, `vcpkg`. These fit GitHub's topic guidance on purpose / subject / language and stay under the 20-topic cap.
- Add a screenshot under `docs/assets/screenshots/` and reference it with a relative path from README, matching GitHub's relative-link guidance.
- Add badges only for workflows that are actually useful to outsiders: `build`, `repo-consistency`, `release`, and `MIT`.
- Reword internal-agent content to one paragraph: "This repo uses file-coordinated LLM agents internally; external contributors can ignore `agents/`." `CONTRIBUTING.md:80-90` already has the right basis.

### Prioritized migration path

1. Documentation truth pass, no moves: update README stale CI/release lines, align public product wording to Comics / Books / Theatre, add `docs/README.md`, and add a short repo-structure doc that names active vs archived docs.
2. Hygiene pass, low risk: untrack or archive the tracked generated `out/stremio_tune_ab_results.csv`, move completed root TODOs to the existing archive location, and move root scratch evidence into `agents/audits/smoke_evidence/` or local-only ignored space.
3. CMake preparation pass: split root `CMakeLists.txt` into included `cmake/TankobanSources.cmake`, `cmake/TankobanTests.cmake`, and `cmake/TankobanRuntimeAssets.cmake` without moving source. Verify app, `tankoctl`, and tests.
4. Docs migration pass: sort `docs/superpowers/` into active plans/specs, archived plans/specs, mockups/assets, data, and raw outputs. Add indexes rather than relying on date-only filenames.
5. Source move pass, one mode at a time: start with leaf / low-risk UI subtrees, then move `BooksPage`, `ComicsPage`, and `StreamPage` into `src/ui/modes/*` in separate commits. Update CMake and includes in the same commit as each move.
6. Resource split pass: separate embedded QRC assets from runtime payloads. Preserve `:/icons` and `:/shaders` virtual paths unless the whole icon-loading surface is changed deliberately.
7. Sidecar internal split pass: keep `native_sidecar/` as the process boundary, then subdivide its `src/` into `ipc`, `media`, `render`, and `platform/windows` after the main repo has stabilized.

## Recommended follow-ups (advisory)

- Consider treating this audit as the objective for a dedicated `REPO_STRUCTURE_CLEANUP_TODO.md` rather than mixing it into active feature arcs.
- Consider requiring every new plan/spec to declare one of: active, superseded, archived, raw. That single field would prevent `docs/superpowers/` from becoming ambiguous again.
- Consider doing a README screenshot pass only after the next visual stable point, so the GitHub landing image does not immediately rot.
