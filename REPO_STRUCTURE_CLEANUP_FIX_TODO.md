# REPO_STRUCTURE_CLEANUP FIX TODO

**Author:** Agent 0 (Opus 4.8)
**Date:** 2026-05-29
**Owner:** Agent 0 (coordinator) — see §6

---

## §1 — Strategic intent

The repo grew organically into a working app but the *presentation and navigability* have decayed. Symptoms hit hard this wake: a 2,261-file build-artifact junk commit (`263f438`, caught + reset pre-push), a `.gitignore` lane-dir gap (fixed `65cf7ab`), 18 `*_TODO.md` files at root, ~116 sprawling `docs/superpowers/` files with no map, a README that still sells 7 subsystems instead of the 3-mode story, and stale CI/release lines. None of this breaks the app — it's **maintainability + public presentation** debt.

Why now: the app's real end user is Hemanth's brother on **macOS** ([[project_macos_target_end_user]]), who will **clone this repo and build it himself**. A newcomer landing on the GitHub page today sees coordination debris before the product. Hemanth verbatim: *"we badly need to organise our repo, gitignore, folder structure, docs.. everything."*

Grounded in Agent 7 (Codex) audit `agents/audits/repo_structure_docs_github_2026-05-29.md` (committed `9239031`) + Agent 0 (Opus) reviewer pass. **This TODO is scoped to the SAFE, port-independent, high-value work** (the audit's passes 1–4). The audit's risky source-move passes (5–7) and the macOS `platform/` seam are explicitly **deferred + gated** — see §7 + §10.

---

## §2 — Phase breakdown

1. **P1** — Documentation truth pass (NO file moves): reframe README to Comics/Books/Theatre, fix stale CI + release lines, add `docs/README.md` map + a repo-structure doc — Owner: Agent 0 — Wakes: ~1
2. **P2** — Hygiene pass (low risk): untrack the stray generated `out/stremio_tune_ab_results.csv`, archive completed root `*_TODO.md` files to `agents/_archive/todos/`, sweep root scratch evidence into `agents/audits/smoke_evidence/` or ignored space — Owner: Agent 0 — Wakes: ~1
3. **P3** — CMake split (NO source moves): extract `cmake/TankobanSources.cmake` + `cmake/TankobanTests.cmake` + `cmake/TankobanRuntimeAssets.cmake` from root `CMakeLists.txt`; clean-from-scratch build of app + `tankoctl` + tests — Owner: Agent 0 (mechanical extraction may go Trigger-D) — Wakes: ~1–2
4. **P4** — Docs migration: sort `docs/superpowers/` into active / archive / raw / assets with indexes; classify by lifecycle (active / superseded / archived / raw) — Owner: Agent 0 — Wakes: ~1

---

## §3 — Deliverables per phase

### P1 deliverables
- `README.md` rewritten: one-sentence product line; **Comics / Books / Theatre** as the public model (Tankorent/Tankoyomi/TankoLibrary framed as source capabilities *beneath* the modes, not top-level identities); fix the release/installer ambiguity (`README.md:5` vs `:27`); fix the stale CI note (`README.md:64` — `build.yml` is already full Windows CI + `release.yml` exists); keep the source-build path concise.
- `docs/README.md` NEW — the canonical map, one screen, answering: "I'm a user" / "I'm a contributor" / "I'm an agent" / "I'm reading history."
- `docs/developer/repo-structure.md` NEW — names what's current vs historical vs internal vs public.
- Smoke: render README + docs/README on GitHub (or local preview); Hemanth eyeballs the landing.

### P2 deliverables
- `out/stremio_tune_ab_results.csv` untracked (`git rm --cached`) — it's a generated file still tracked.
- Completed root `*_TODO.md` files moved to `agents/_archive/todos/` (validate each is actually closed first; CLAUDE.md "Active Fix TODOs" table is the truth source for active vs closed).
- Root scratch PNGs / logs / one-off evidence relocated to `agents/audits/smoke_evidence/` or confirmed gitignored.
- Smoke: `git status` clean of stray tracked junk; root file list is product-first.

### P3 deliverables
- `cmake/TankobanSources.cmake`, `cmake/TankobanTests.cmake`, `cmake/TankobanRuntimeAssets.cmake` — the flat source/header/test/resource lists relocated into `include()`-d files; root `CMakeLists.txt` becomes a thin orchestrator.
- **No source files moved** — only the CMake lists relocate. Include paths unchanged.
- Smoke: clean-from-scratch `build_check.bat` BUILD OK (gov-v11 hard gate) + `tankoban_tests` builds + `tankoctl` builds.

### P4 deliverables
- `docs/superpowers/` reorganized: active plans/specs, archived plans/specs, mockups/assets, data, raw model output — each with an index (not date-only filenames).
- Raw model output (e.g. `*_gpt_raw.md`, `*_gemini_raw.md`) separated from curated human-facing docs.
- Smoke: a newcomer can tell which docs are current vs historical from the index alone.

---

## §4 — Acceptance criteria

- **P1:** README + docs/README render correctly; product framing matches Comics/Books/Theatre; no stale CI/release claims; Hemanth eyeballs the GitHub landing and approves.
- **P2:** `git status` shows no tracked generated files; root `*_TODO.md` count reflects only *active* TODOs; no scratch images/logs tracked at root.
- **P3:** clean-from-scratch build GREEN (app + tests + tankoctl) verified in a throwaway worktree per gov-v11. Zero behavior change (CMake-only refactor).
- **P4:** `docs/superpowers/` has a working index; lifecycle state visible per doc; raw output segregated.

---

## §5 — Decisions (resolved 2026-05-29)

Hemanth standing directive: *"always your rec"* — these resolve to the recommended answers, and future calls within Agent 0's authority proceed on the rec without a ratification pause ([[feedback_no_rhetorical_ratification_pause]]). Genuinely product/strategic/irreversible calls still get surfaced.

1. **Root docs placement — RESOLVED: keep at root.** `README.md` / `BUILD.md` / `ARCHITECTURE.md` / `CONTRIBUTING.md` / `LICENSE` stay at repo root (GitHub surfaces them natively); `docs/README.md` is the map that links into the deeper tree. (Hemanth-confirmed verbatim.)
2. **README screenshot — RESOLVED: defer** to after the next visual-stable point so the landing image doesn't immediately rot. P1 ships without it; revisit as a product/taste moment later.
3. **GitHub description + topics — RESOLVED: set in P1.** Description: `Windows + macOS Qt6 desktop media library for comics, books, and theatre.` Topics: `qt6`, `cpp20`, `windows`, `macos`, `desktop-app`, `media-library`, `comics`, `epub`, `video-player`, `ffmpeg`, `libtorrent`, `stremio`, `cmake`, `vcpkg`. (macOS added to the audit's Windows-only wording per the locked equal-priority decision.)

---

## §6 — Ownership

- **Primary owner:** Agent 0 (coordinator). Repo structure, docs, gitignore, CMake orchestration, and the GitHub page are Agent 0's domain (Rule 13 + hygiene tooling).
- **Cross-agent contributors:** P4 docs-sorting can fan to Agent N Jrs (Trigger E) if volume warrants. README product-wording should be sanity-checked against each mode-owner's framing (A1 Comics, A2 Books, A4 Theatre) via chat.md, not edited by them.
- **Codex / DeepSeek Trigger-D scope:** P3 CMake extraction is mechanical + well-specified → a good Trigger-D candidate (Codex or DeepSeek by quota) with a clean-build gate. P1/P2/P4 stay with Agent 0 (judgment + cross-domain wording).

---

## §7 — Dependencies

- **Blocked by:** nothing — safe to start immediately (P1/P2 are zero-risk).
- **Blocks:** nothing hard. The `MACOS_DUAL_BACKEND` arc *benefits* from a clean structure but is not blocked by this.
- **DEFERRED + GATED (NOT in this TODO):** the audit's source-move passes (5: move `BooksPage`/`ComicsPage`/`StreamPage` into `src/ui/modes/*`, 6: resource split, 7: sidecar internal split) **and** the macOS `platform/windows` ↔ `platform/mac` seam around the video layer. Rationale: those are risky restructures of the **hottest, most-contended subsystem** (the player/sidecar), and the platform seam is properly the *opening move of the MACOS_DUAL_BACKEND arc*, not repo hygiene. They gate on (a) a quiet-tree window with no active arcs in the affected files, and (b) the Mac-arc decision. See §8 risk 1.
- **Memory references:** [[project_macos_target_end_user]], [[project_repo_hygiene_sequencing]], [[feedback_fix_todo_authoring_shape]], [[feedback_cmakelists_multi_agent_collision]].

---

## §8 — Risks

1. **Multi-agent collision on source moves (THE risk).** Flat-on-master with 5+ agents holding dirty in-flight work; this wake alone, Agent 1 committed to `ComicsSeriesView.cpp` + `MangaDownloadIndex` and Agent 2/4 hold dirty Books/stream files. A source-move reorg relocating those exact files mid-arc = merge hell. **Mitigation: this TODO contains ZERO source moves.** Passes 5–7 are deferred + gated on a quiet-tree window (§7).
2. **CMake split breaks app / test / tankoctl builds.** **Mitigation:** relocate lists only, move no source, keep include paths identical; clean-from-scratch build verify after P3 (gov-v11) before declaring green.
3. **Doc moves break links.** **Mitigation:** leave root stubs or update all links; use relative links (GitHub guidance); add indexes rather than rely on filenames.
4. **Scope creep into the macOS port.** **Mitigation:** the `platform/` seam is explicitly the Mac arc's job, not this TODO's (§7). This TODO never touches platform code.
5. **Re-introducing the junk-commit / gitignore problem.** Already structurally fixed (`65cf7ab`: `out_agent*/` + `out_*/`). P2 finishes the job (untrack the one stray `out/` csv).

---

## §9 — Wake budget

- P1 ~1 wake · P2 ~1 wake · P3 ~1–2 wakes (CMake split + clean-build verify) · P4 ~1 wake.
- **Total: ~4–5 wakes.** Passes 5–7 + platform seam are NOT budgeted here (deferred to a quiet window / the Mac arc). No calendar dates — currency is wakes ([[feedback_no_human_days_in_agentic]]).

---

## §10 — Anti-patterns to avoid

1. **DO NOT move source files in this TODO.** Source moves are deferred passes 5–7, gated separately.
2. **DO NOT combine any move with a behavior refactor** (audit rule — e.g. don't split `StreamPage.cpp` while relocating it).
3. **DO NOT run the eventual source moves while agents hold dirty in-flight work in the affected files** — wait for a quiet tree or use worktree discipline.
4. **DO NOT glob CMake sources.** Keep explicit lists; just relocate them into `cmake/*.cmake` includes (preserves shared-edit clarity, avoids glob instability).
5. **DO NOT polish `agents/` for public consumption.** Point external readers to ignore it (CONTRIBUTING.md already does); don't sink effort making coordination logs pretty.
6. **DO NOT bundle the macOS platform seam here.** It's the Mac arc's opening move.
7. **DO NOT archive a root TODO without confirming it's actually closed** against the CLAUDE.md active table + git history.

---

## §11 — Evidence pointers

- Audit: `agents/audits/repo_structure_docs_github_2026-05-29.md` (Agent 7 / Codex, committed `9239031`).
- Agent 0 (Opus) reviewer pass: this wake's coordinator session (validated audit facts; added the sequencing-vs-Mac-port split, the value-gate on passes 5–7, the multi-agent collision hazard, and the CLAUDE.md + memory path-break omissions).
- gitignore fix: `65cf7ab`. Junk-commit recovery: reset off `263f438` → `0d6a065`.
- Memory: [[project_macos_target_end_user]] (locked architecture), [[project_repo_hygiene_sequencing]], [[feedback_cmakelists_multi_agent_collision]].

---

## §12 — Close criteria

P1–P4 complete, clean-from-scratch build GREEN after P3, and Hemanth has eyeballed the GitHub landing + README and approved the product framing. The deferred source-move passes (5–7) + platform seam are **explicitly outside this TODO's close** — they're a separate decision made when the tree is quiet and/or the Mac arc kicks off. Closing this TODO = "the repo reads clean and navigable; the risky structural moves remain a deliberate future choice."

---

## §13 — Standing contracts

Survive this TODO's close:
- **Lifecycle field on every new plan/spec:** declare one of `active` / `superseded` / `archived` / `raw`. Prevents `docs/superpowers/` re-rotting (audit's governance idea).
- **Root stays minimal:** `README` / `BUILD` / `ARCHITECTURE` / `CONTRIBUTING` / `LICENSE` + only *active* TODOs. Closed TODOs → `agents/_archive/todos/`.
- **CMake source lists live in `cmake/*.cmake`, explicit (never glob).**
- **`platform/windows` ↔ `platform/mac` seam convention** reserved for the video layer when the Mac arc lands (named here so it's consistent later; not built here).
- **Windows + macOS are equal first-class targets** in all public docs (description, README, badges) per [[project_macos_target_end_user]].

---

## §14 — Archive trigger

When this TODO closes, move to `agents/_archive/todos/REPO_STRUCTURE_CLEANUP_FIX_TODO.md` and update the CLAUDE.md "Active Fix TODOs" table to mark it CLOSED.
