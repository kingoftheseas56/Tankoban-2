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

## After-refactor (2026-06-02)

| Thing | Before | After |
|---|---|---|
| `CLAUDE.md` kernel | 185 lines / 28K | **108 lines** (churn + reference moved out) |
| `agents/_archive/` | 120 MB | **3.9 MB** (stray 115MB libmpv binary reclaimed) |
| memory freshness | 0/183 typed | **183 OK, 0 missing, 0 stale** |
| dangling kernel pointers | — | **0** |

**New files created:** `agents/STATUS.md` (live state, 38L) · `agents/ACTIVE_TODOS.md` (24L) · `agents/BUILD.md` (full tooling, 19L) · `agents/routes.yml` (owner+task→files router, 36L) · `scripts/memory_lint.py` (freshness linter).

**What moved where:** the mutable 30-Second Dashboard → STATUS.md; Active Fix TODOs table → ACTIVE_TODOS.md; full tiered skills catalog → GOVERNANCE.md (Tier-1 kept in kernel); full Build Quick Reference → BUILD.md (3 essentials kept). The load-bearing HEMANTH'S ROLE block stayed in the kernel intact.

**Deliberately NOT done:** `.git` history bloat (would need shared-history rewrite — too risky); the 156MB of untracked smoke PNGs (brotherhood evidence, left in place, harmless to repo); compiled-context/task-packs (Codex's higher-build idea — revisit after this proves out).

**Backups (off-git, recoverable):** `~/tankoban_context_backup_2026-06-02/{audits,_archive,memory_predfreshness}`.
