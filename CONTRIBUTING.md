# Contributing to Tankoban

External contributors are welcome. This file covers what you need to know.

---

## Before you start

- **Open an issue first** for non-trivial changes (new features, refactors, anything touching the player or stream pipeline). For typo fixes or obvious bugs, a PR straight to master is fine.
- **Build from source** following [BUILD.md](BUILD.md). If you can't get a clean build, that's a bug — file an issue with your environment details.
- **Read [ARCHITECTURE.md](ARCHITECTURE.md)** for the high-level component map. It's short.

---

## What's in scope for contributions

- **Bug fixes** — anywhere.
- **Performance improvements** — backed by measurements (before/after numbers).
- **New stream-mode addons / source scrapers** — follow the pattern in `src/core/stream/addon/` (Stremio addons) or `src/core/manga/` + `src/core/book/` (direct scrapers).
- **Subtitle / audio codec support** — sidecar-side; opens against `native_sidecar/src/`.
- **UI polish** — see [src/ui/Theme.h](src/ui/Theme.h) for the theme system. The 5-mode picker is settled; new modes need a UX rationale.
- **Documentation** — README, BUILD, ARCHITECTURE always welcome refinements.

## What's out of scope

- **Linux / macOS support** — Windows-only by design (MSVC + libtorrent + DirectX-tied native sidecar). Cross-platform is a future-future thing.
- **Account systems / cloud sync** — Tankoban is a single-user single-machine app. Sync against external services (Trakt, etc.) could be added but is opt-in.
- **Large architectural rewrites without prior discussion** — the audit-driven repo-hygiene phases (`REPO_HYGIENE_FIX_TODO.md`) are the structured improvement track. Open an issue before authoring a sweeping refactor.

---

## Code style

- **C++20**. The codebase uses `auto`, structured bindings, range-for, `std::optional`, `std::filesystem`, `concepts` where they help.
- **Qt6 idioms**. `QObject` parents for memory management, signals/slots over std::function for async callbacks, `QStringLiteral` over raw strings for static QStrings, queued connections across thread boundaries.
- **Indent 4 spaces.** No tabs.
- **Braces on same line for control flow, new line for functions/classes.**
- **No emojis or colored ANSI in source / output / commit messages.** This is a settled convention — see `agents/` for context if curious.
- **Comments**: explain *why*, not *what*. Reference an audit finding or a fix-TODO when the rationale isn't obvious from context.
- **Header comments on classes/functions**: brief, mostly when behavior is non-obvious or when a callsite is documented elsewhere.

---

## Testing your change

### Compile-only check
```cmd
build_check.bat
```

### Static lint (matches what CI runs)
```bash
bash scripts/repo-consistency.sh
```

This catches the patterns the audit flagged: hardcoded developer paths, missing CMakeLists entries, bare debug-log filenames in non-comment source. Runs in ~1s.

### Runtime smoke
```cmd
build_and_run.bat
```

Exercise the surface you changed. There are no comprehensive integration tests for the player or stream pipeline — manual smoke testing is the rule.

### Unit tests (opt-in)
See [BUILD.md](BUILD.md) § Tests. Unit tests cover pure-logic primitives (scanner utils, persistence helpers). Player pipeline + sidecar IPC are smoke-only.

---

## Pull request conventions

- **One concern per PR.** A bug fix + an unrelated cleanup belong in two PRs.
- **Title format**: `[<area>] <what changed>` (e.g. `[player] Fix subtitle position when source has explicit MarginV`). Areas: `player`, `comic`, `book`, `stream`, `tankorent`, `library`, `theme`, `build`, `docs`.
- **Body**: the *why* — what bug this fixes, what symptom the user saw, how you verified it. Reference the issue if there is one.
- **Diff size**: prefer small. If your PR is over ~500 LOC, justify it in the body.
- **No commits without a build**: every commit on the PR branch should compile (`build_check.bat` green).

---

## On the `agents/` directory

This repo uses an LLM-agent-driven development workflow internally — multiple Claude Code sessions (one per domain: comic, book, video, stream, library, sources) coordinate via files in `agents/`: a chat log (`agents/chat.md`), per-agent status (`agents/STATUS.md`), governance rules (`agents/GOVERNANCE.md`), audit reports, and so on.

For external contributors, none of this is load-bearing. You can:

- Read it if you're curious (governance is documented).
- Ignore it entirely when reading source — `agents/` doesn't affect what the code does.
- Add `:!agents/**` to your git diff filter if it's noise.

The only inputs to the codebase from the agent workflow are the standard ones: commits, source edits, build verification. Contributing as an external party doesn't require participating in the agent coordination protocol.

---

## License

By contributing, you agree your contribution is licensed under the same MIT license the project uses (see [LICENSE](LICENSE)).

---

## Code of conduct

Be respectful. Disagreements about technical direction are fine — disagreements about whether someone deserves to be treated decently are not. If a maintainer asks you to take a tone down, take it down.

That's the whole policy.
