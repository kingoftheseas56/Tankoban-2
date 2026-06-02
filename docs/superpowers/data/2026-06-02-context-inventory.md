# Context footprint baseline — 2026-06-02 (pre-refactor)

Measured before the memory/context-architecture refactor
(`docs/superpowers/plans/2026-06-02-memory-context-architecture.md`).
Off-git safety backup of the dead-weight dirs: `~/tankoban_context_backup_2026-06-02/`.

## Always-loaded at every wake
| File | Lines | Size |
|---|---|---|
| `CLAUDE.md` (root kernel) | 185 | 28K |
| `MEMORY.md` (memory index) | 203 | 28K |

## Subtree CLAUDE.md (load only on dir reads — domain-scoped, KEEP)
11 files, 42–108 lines each: native_sidecar, src/core/{book,manga,stream,torrent},
src/ui/pages/{comics,stream,tankolibrary,tankorent,tankoyomi}, src/ui/player.

## agents/ governance + coordination
| File | Lines | Size |
|---|---|---|
| `agents/GOVERNANCE.md` | 575 | 72K |
| `agents/CONTRACTS.md` | 268 | 16K |
| `agents/VERSIONS.md` | 33 | 16K |
| `agents/ONBOARDING.md` | 70 | 8K |
| `agents/chat.md` | 1711 | 220K |

## agents/ history (the dead weight)
| Dir | Files | Size |
|---|---|---|
| `agents/audits/` | 364 | **163 MB** |
| `agents/_archive/` | 64 | **120 MB** |
| `agents/congress_archive/` | 10 | 232K |
| `agents/todos/` | 17 | 404K |
| `agents/chat_archive/` | 8 | 6.2M |

## Long tail
| Class | Count | Size |
|---|---|---|
| memory `*.md` | 184 | 1.4M |
| memory/_archive | 53 | — |
| recaps | 59 | 880K |
| docs/superpowers/specs | 62 | 1.4M |
| docs/superpowers/plans | 81 | 4.2M |

## Read (post-refactor numbers appended in Task 8)
_(to be filled after execution)_
