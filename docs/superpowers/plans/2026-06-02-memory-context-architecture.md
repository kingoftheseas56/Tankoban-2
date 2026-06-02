# Memory & Context Architecture — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline, with checkpoints) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax. This is a FILE-RESTRUCTURING plan (markdown / YAML / small scripts), not a TDD code plan — "verification" steps are grep/line-count/load checks, not unit tests.

**Goal:** Make every brother boot light and think fast — cut the always-loaded context to a true kernel, route to detail on demand, give memory facts a freshness story, and clear the dead-weight raw dumps — without changing anything about the app or breaking how a brother works mid-task.

**Architecture:** Four independently-converged sources (two deep-research runs, an external report, and Codex/Agent 7) agree: *less always-loaded English + a kernel boot file + a routing index + typed facts with invalidation + just-in-time retrieval + archived narrative.* No new "AI language", no vector DB (agentic grep/file-read wins for code). We keep rich markdown as the source of truth; we stop force-feeding all of it at every wake.

**Tech Stack:** Markdown, YAML frontmatter, a tiny Python validator (stdlib only), `git`, grep. No new runtime deps.

**Ground rules (NON-NEGOTIABLE):**
- **`agents/` is fenced** (Hemanth's "world-class repo, never at the brotherhood's cost"). Restructure *with* care, incrementally — never a big-bang that breaks a brother's wake.
- **Live shared tree.** Other brothers are actively committing (Agent 1 on comics). Touch shared files (CLAUDE.md, GOVERNANCE.md, chat.md) in small, self-contained commits; never hold them dirty across phases; `git status` before each shared-file edit.
- **Reversible.** One commit per task. Nothing destructive without an off-git backup first.
- **No history rewrite on master** — disk-hygiene removes files from the *working tree* only; `.git` history bloat is out of scope (would need a shared-history rewrite = too risky).
- **Additive-first.** Prefer creating the new (STATUS.md, routes.yml) and pointing at it before deleting the old.

**Inventory (ground truth, measured 2026-06-02):**
- Always-loaded: `CLAUDE.md` 185 lines/28K + `MEMORY.md` 203 lines/28K.
- Subtree `CLAUDE.md` ×11 (42–108 lines) — already domain-scoped, load only on dir reads. KEEP.
- `agents/GOVERNANCE.md` 575 lines/72K, `CONTRACTS.md` 268, `VERSIONS.md` 33, `ONBOARDING.md` 70, `chat.md` 1711 lines/220K.
- `agents/audits/` **364 files / 163 MB**, `agents/_archive/` **64 files / 120 MB** ← the dead weight.
- memory: 184 `*.md` (1.4M) + 53 archived. recaps: 59 (880K). docs/superpowers: specs 62, plans 81.

---

## File Structure (what gets created / changed)

| File | Responsibility | Change |
|---|---|---|
| `CLAUDE.md` (root) | **Kernel** — roles/ownership, hard rules pointer, how-to-route, index pointer, build/verify cmds. <100 lines. | Slim (extract state) |
| `agents/STATUS.md` | **Live mutable state** — the 30-Second Dashboard, moved out of the kernel. Tiny, current. | Create |
| `agents/routes.yml` | **Context router** — task/domain keywords → exact files to read (+ owners, validation cmds). | Create |
| `MEMORY.md` | Memory **index** only — lean one-line pointers (already is; verify + trim). | Verify/trim |
| `memory/*.md` | Typed facts — add `status` / `last_verified` / `supersedes` frontmatter (freshness story). | Schema + backfill |
| `scripts/memory_lint.py` | Validator — flags memory files missing freshness fields / past staleness. | Create |
| `.gitignore` | Stop new raw-dump bloat. | Append |
| `agents/audits/`, `agents/_archive/` | Working-tree dead weight — raw dumps removed from tree (off-git backup kept). | Prune |

---

### Task 1: Record the baseline + safety backup

**Files:**
- Create: `docs/superpowers/data/2026-06-02-context-inventory.md` (the measured baseline)

- [ ] **Step 1: Write the inventory baseline** (so we can prove before/after)

Create `docs/superpowers/data/2026-06-02-context-inventory.md` containing the inventory table from this plan's header (the measured numbers), titled "Context footprint baseline 2026-06-02 (pre-refactor)".

- [ ] **Step 2: Off-git safety backup of the dead-weight dirs** (before any prune)

Run:
```bash
cd "/c/Users/Suprabha/Desktop/Tankoban 2"
mkdir -p /c/Users/Suprabha/tankoban_context_backup_2026-06-02
cp -r agents/audits agents/_archive /c/Users/Suprabha/tankoban_context_backup_2026-06-02/
du -sh /c/Users/Suprabha/tankoban_context_backup_2026-06-02/*
```
Expected: two dirs copied (~163M + ~120M) — proof the originals are recoverable before we touch the tree.

- [ ] **Step 3: Commit the baseline**
```bash
git add docs/superpowers/data/2026-06-02-context-inventory.md
git commit -m "docs(context): record pre-refactor context footprint baseline"
```

---

### Task 2: Disk hygiene — prune raw dumps from the working tree (Phase 1, safe, biggest size win)

**Files:**
- Modify: `.gitignore`
- Remove from tree: the large raw-dump files under `agents/audits/` (and any oversized `agents/_archive/` raw dumps)

- [ ] **Step 1: Enumerate the biggest offenders (discovery)**

Run:
```bash
cd "/c/Users/Suprabha/Desktop/Tankoban 2"
echo "=== biggest files in agents/audits + _archive ==="
find agents/audits agents/_archive -type f -printf '%s\t%p\n' | sort -rn | head -40 | awk '{printf "%.1fMB\t%s\n", $1/1048576, $2}'
echo "=== raw-dump pattern count + size ==="
find agents/audits agents/_archive -type f \( -name '*_gemini_raw*' -o -name '*_gpt_raw*' -o -name '*_raw.md' -o -name '*.jsonl' \) -printf '%s\n' | awk '{s+=$1} END {printf "%d files, %.1fMB\n", NR, s/1048576}'
```
Expected: a ranked list; confirm the bulk is raw model-dump files, not human-authored audit summaries.

- [ ] **Step 2: Gitignore future raw dumps**

Append to `.gitignore`:
```
# Raw model-output dumps under agents/ — keep human-authored summaries, not raw dumps.
# (Off-git backup at ~/tankoban_context_backup_*; prune from tree to keep grep/explore fast.)
agents/audits/*_gemini_raw*
agents/audits/*_gpt_raw*
agents/**/*_raw.md
```

- [ ] **Step 3: Remove the raw dumps from the working tree (history retained; backup exists)**

Run (only the raw-dump patterns — NEVER the human-authored `*_audit_*.md` / summary files):
```bash
cd "/c/Users/Suprabha/Desktop/Tankoban 2"
git rm --cached $(find agents/audits agents/_archive -type f \( -name '*_gemini_raw*' -o -name '*_gpt_raw*' \) 2>/dev/null) 2>/dev/null
find agents/audits agents/_archive -type f \( -name '*_gemini_raw*' -o -name '*_gpt_raw*' \) -delete
echo "=== audits/_archive size after prune ==="
du -sh agents/audits agents/_archive
```
Expected: sizes drop sharply; only summary/human `.md` remain in tree.

- [ ] **Step 4: Verify nothing human-authored was lost**

Run: `find agents/audits -name '*_audit_*.md' -o -name '*.md' | grep -vE '_raw|gemini|gpt' | wc -l`
Expected: the human audit summaries are all still present (count > 0, matches pre-prune summary count). Cross-check against the backup if unsure.

- [ ] **Step 5: Commit**
```bash
git add .gitignore agents/audits agents/_archive
git commit -m "chore(agents): prune raw model-dumps from working tree (off-git backup kept); gitignore future dumps"
```

---

### Task 3: Kernel-ify CLAUDE.md — extract live state to STATUS.md (Phase 2, highest ROI)

**Files:**
- Create: `agents/STATUS.md`
- Modify: `CLAUDE.md` (remove the "30-Second State Dashboard" + active-TODO table + skills detail; leave a pointer)

- [ ] **Step 1: `git status` gate (shared file — don't clobber a brother)**

Run: `git status --short CLAUDE.md`
Expected: clean (not modified by another brother). If dirty, STOP and coordinate before editing.

- [ ] **Step 2: Move the mutable dashboard into STATUS.md**

Create `agents/STATUS.md` and CUT into it (verbatim) these sections currently in `CLAUDE.md`: the "30-Second State Dashboard", "Active agents", "READY TO COMMIT backlog", "Open congresses/HELP/Blocked", "Last successful smoke", "Engine/quota status". Header it:
```markdown
# Tankoban 2 — Live State (STATUS.md)
> Mutable state, refreshed by Agent 0 at phase boundaries (Rule 13). The boot kernel (CLAUDE.md) points here; read this when you need current who/what/where.
```

- [ ] **Step 3: Replace those sections in CLAUDE.md with a one-line pointer**

In `CLAUDE.md`, replace the cut block with:
```markdown
## Live state
Current who/what/where (dashboard, active agents, backlog, smoke status) lives in **`agents/STATUS.md`** — read it when you need the current picture. This kernel stays stable; STATUS churns.
```

- [ ] **Step 4: Verify the kernel shrank and STATUS holds the state**

Run:
```bash
wc -l CLAUDE.md agents/STATUS.md
grep -c "30-Second State Dashboard" CLAUDE.md   # expect 0
grep -c "30-Second State Dashboard" agents/STATUS.md  # expect 1
```
Expected: `CLAUDE.md` materially smaller (target trending toward <120 lines this task; <100 after Task 4); STATUS.md holds the dashboard.

- [ ] **Step 5: Commit**
```bash
git add CLAUDE.md agents/STATUS.md
git commit -m "refactor(context): kernel-ify CLAUDE.md — live dashboard moves to agents/STATUS.md"
```

---

### Task 4: Finish the kernel — move TODO tables + skills detail to pointed-to files

**Files:**
- Create: `agents/ACTIVE_TODOS.md` (the active fix-TODO table)
- Modify: `CLAUDE.md` (the "Active Fix TODOs" table + "Required Skills & Protocols" detail → pointers)

- [ ] **Step 1: Move the Active Fix TODOs table**

Cut the full "Active Fix TODOs" table from `CLAUDE.md` into a new `agents/ACTIVE_TODOS.md` (verbatim, with its intro line). In `CLAUDE.md` leave:
```markdown
## Active work
Active fix-TODOs + phase cursors: **`agents/ACTIVE_TODOS.md`**. Per-domain detail auto-loads from the subtree `CLAUDE.md` when you read files in that domain.
```

- [ ] **Step 2: Collapse the Skills section to the Tier-1 list + a pointer**

In `CLAUDE.md`, replace the long tiered skills catalog with the **Tier-1 list only** (the ~8 always-relevant skills as bare names) plus:
```markdown
Full tiered skill catalog + rationale: `agents/GOVERNANCE.md` (Skills section) / memory `feedback_plugin_skills_adopted`.
```

- [ ] **Step 3: Verify kernel is now <100 lines**

Run: `wc -l CLAUDE.md`
Expected: **< 100 lines.** (If still over, identify the next-largest static block and point it out instead of inlining it.)

- [ ] **Step 4: Sanity — the kernel still answers the 5 kernel questions**

Run: `grep -iE "own|rule|route|status|build|verify" CLAUDE.md | head`
Expected: the kernel still covers — who owns what, hard rules (pointer), how to route, where live state is, how to build/verify. (Codex's kernel checklist.)

- [ ] **Step 5: Commit**
```bash
git add CLAUDE.md agents/ACTIVE_TODOS.md
git commit -m "refactor(context): CLAUDE.md is now a <100-line kernel; TODOs + skills detail moved out"
```

---

### Task 5: The routing index — routes.yml

**Files:**
- Create: `agents/routes.yml`
- Modify: `CLAUDE.md` (point the kernel at the router)

- [ ] **Step 1: Write the router**

Create `agents/routes.yml` mapping domains/tasks → the exact files to read. Seed it from the real domain structure (the 11 subtree CLAUDE.md + owners + TODOs):
```yaml
# Context router — "for THIS kind of task, read THESE files; ignore the rest unless blocked."
# Owners + validation per domain. Keep entries to real, current files.
domains:
  comics:
    owner: agent1
    read: [src/ui/pages/comics/CLAUDE.md, src/core/manga/CLAUDE.md, src/ui/pages/tankoyomi/CLAUDE.md]
    todos: [agents/todos/COMIC_READER_FIX_TODO.md]
    verify: build_check.bat
  books:
    owner: agent2
    read: [src/core/book/CLAUDE.md, src/ui/pages/tankolibrary/CLAUDE.md]
    todos: [agents/todos/BOOK_READER_FIX_TODO.md, agents/todos/TANKOLIBRARY_FIX_TODO.md]
    verify: build_check.bat
  player:
    owner: agent3
    read: [src/ui/player/CLAUDE.md, native_sidecar/CLAUDE.md]
    verify: build_check.bat
  stream:
    owner: agent4
    read: [src/ui/pages/stream/CLAUDE.md, src/core/stream/CLAUDE.md, src/core/torrent/CLAUDE.md, src/ui/pages/tankorent/CLAUDE.md]
    todos: [agents/todos/STREAM_SERVER_PIVOT_TODO.md, agents/todos/TANKORENT_QUALITY_AND_QUEUE_TODO.md]
    verify: build_check.bat
  governance:
    owner: agent0
    read: [agents/GOVERNANCE.md, agents/VERSIONS.md, agents/CONTRACTS.md]
  state:
    owner: agent0
    read: [agents/STATUS.md, agents/ACTIVE_TODOS.md]
```

- [ ] **Step 2: Point the kernel at it**

In `CLAUDE.md` "Reading Order" area add:
```markdown
**Routing:** `agents/routes.yml` maps your task's domain to the few files worth reading. Read your domain's entries; skip the rest unless blocked.
```

- [ ] **Step 3: Verify the router only references files that exist**

Run:
```bash
cd "/c/Users/Suprabha/Desktop/Tankoban 2"
python -c "import yaml,sys,os; d=yaml.safe_load(open('agents/routes.yml')); miss=[f for v in d['domains'].values() for k in ('read','todos') for f in (v.get(k) or []) if not os.path.exists(f)]; print('MISSING:', miss or 'none')"
```
Expected: `MISSING: none`. (If yaml import fails, the validator in Task 6 installs nothing new — use a json fallback or skip; routes.yml is human-read too.)

- [ ] **Step 4: Commit**
```bash
git add agents/routes.yml CLAUDE.md
git commit -m "feat(context): add agents/routes.yml task→files router; kernel points at it"
```

---

### Task 6: Typed memory facts + freshness + a linter (Phase 4, the "stale truths" fix)

**Files:**
- Create: `scripts/memory_lint.py`
- Modify: memory frontmatter convention (documented) + backfill `last_verified` on existing files

- [ ] **Step 1: Write the memory linter**

Create `scripts/memory_lint.py`:
```python
#!/usr/bin/env python3
"""Lint Tankoban memory files for a freshness story.
Flags files missing status/last_verified, and facts older than STALE_DAYS not re-verified.
Read-only: reports, never edits. Run: python scripts/memory_lint.py [memory_dir]"""
import os, sys, re, datetime

STALE_DAYS = 120
DEFAULT_DIR = os.path.expanduser(
    "~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory")


def parse_frontmatter(text):
    m = re.match(r"^---\n(.*?)\n---\n", text, re.DOTALL)
    if not m:
        return {}
    fm = {}
    for line in m.group(1).splitlines():
        if ":" in line and not line.strip().startswith("#"):
            k, _, v = line.partition(":")
            fm[k.strip()] = v.strip()
    return fm


def main(argv):
    d = argv[0] if argv else DEFAULT_DIR
    today = datetime.date.today()
    missing, stale, ok = [], [], 0
    for name in sorted(os.listdir(d)):
        if not name.endswith(".md") or name == "MEMORY.md":
            continue
        fm = parse_frontmatter(open(os.path.join(d, name), encoding="utf-8").read())
        lv = fm.get("last_verified", "")
        if "status" not in fm or not lv:
            missing.append(name)
            continue
        try:
            age = (today - datetime.date.fromisoformat(lv)).days
            (stale if age > STALE_DAYS else None) and stale.append((name, age))
            ok += 1
        except ValueError:
            missing.append(name)
    print(f"OK: {ok}  |  missing freshness fields: {len(missing)}  |  stale(>{STALE_DAYS}d): {len(stale)}")
    for n in missing[:40]:
        print("  MISSING:", n)
    for n, a in sorted(stale, key=lambda x: -x[1])[:40]:
        print(f"  STALE {a}d:", n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 2: Run it to see the current gap**

Run: `python scripts/memory_lint.py`
Expected: a large "missing freshness fields" count (most of the 184 files lack `status`/`last_verified`). This is the baseline.

- [ ] **Step 3: Document the typed-fact schema in the memory-write convention**

Append to the memory frontmatter convention (in `CLAUDE.md` Memory section / the memory-write skill note): new optional-but-encouraged fields:
```markdown
metadata: type | status (active/superseded) | last_verified (YYYY-MM-DD) | supersedes / invalidated_by (slug)
```
Rationale line: "Most memory rot is old truths looking current — give every fact a freshness story (Codex, 2026-06-02)."

- [ ] **Step 4: Backfill `last_verified` + `status: active` on existing memory files (batch, conservative)**

Run a one-time backfill that adds `last_verified` = file mtime date + `status: active` ONLY to files that lack them (never overwrite existing fields):
```bash
cd "/c/Users/Suprabha/Desktop/Tankoban 2"
python - <<'PY'
import os, re, datetime
d=os.path.expanduser("~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory")
for n in os.listdir(d):
    if not n.endswith(".md") or n=="MEMORY.md": continue
    p=os.path.join(d,n); t=open(p,encoding="utf-8").read()
    m=re.match(r"^---\n(.*?)\n---\n",t,re.DOTALL)
    if not m: continue
    fm=m.group(1)
    if "last_verified" in fm and "status" in fm: continue
    mtime=datetime.date.fromtimestamp(os.path.getmtime(p)).isoformat()
    add=""
    if "status" not in fm: add+="\nstatus: active"
    if "last_verified" not in fm: add+=f"\nlast_verified: {mtime}"
    new=t.replace(m.group(0), f"---\n{fm}{add}\n---\n",1)
    open(p,"w",encoding="utf-8").write(new)
print("backfill done")
PY
python scripts/memory_lint.py
```
Expected: "missing freshness fields" drops to ~0; some files flagged STALE (>120d) — that's the linter doing its job, surfacing what to re-verify.

- [ ] **Step 5: Commit (the linter + convention; memory files are off-git, no commit needed for those)**
```bash
git add scripts/memory_lint.py CLAUDE.md
git commit -m "feat(memory): freshness schema + memory_lint.py (flag stale/untyped facts)"
```

---

### Task 7: Confirm narrative is archival + chat.md rotation check (Phase 5)

**Files:**
- Possibly: `agents/chat.md` (rotate if over threshold) via `/rotate-chat`

- [ ] **Step 1: Check chat.md against rotation threshold**

Run: `wc -l agents/chat.md` (threshold: 3000 lines / 300KB).
Expected: 1711 lines / 220K — UNDER threshold, no rotation needed now. (If a future run shows over, invoke `/rotate-chat`.)

- [ ] **Step 2: Confirm recaps are read-by-index, not bulk-loaded**

Verify the wake convention: next-wake reads the recap INDEX + the trimmed transcript pointer, not every recap. Document in `CLAUDE.md` reading-order:
```markdown
Recaps: read only YOUR latest recap + its trimmed transcript at wake — not the archive. Older recaps are searchable (grep / claude-mem), not auto-read.
```

- [ ] **Step 3: Commit**
```bash
git add CLAUDE.md
git commit -m "docs(context): codify recap-by-index + chat rotation discipline at wake"
```

---

### Task 8: Final verification + before/after proof

- [ ] **Step 1: Measure the new footprint**

Run:
```bash
cd "/c/Users/Suprabha/Desktop/Tankoban 2"
echo "kernel:"; wc -l CLAUDE.md
echo "state:"; wc -l agents/STATUS.md agents/ACTIVE_TODOS.md
echo "audits/_archive now:"; du -sh agents/audits agents/_archive
python scripts/memory_lint.py | head -1
```
Expected: CLAUDE.md <100 lines; audits/_archive down to tens of MB; memory freshness ~0 missing.

- [ ] **Step 2: Smoke a cold wake mentally** — read CLAUDE.md top to bottom as if newly woken; confirm it routes you (kernel → routes.yml → STATUS.md → domain files) without forcing the whole history. Fix any dangling pointer.

- [ ] **Step 3: Append the after-numbers to the baseline doc + commit**
```bash
git add docs/superpowers/data/2026-06-02-context-inventory.md
git commit -m "docs(context): record post-refactor footprint (before/after)"
git push origin master
```

- [ ] **Step 4: Tell the brothers (Office)** — broadcast that CLAUDE.md is now a kernel + where state/TODOs/routes moved, so no brother is surprised at next wake.

---

## Self-Review

**Spec coverage:** kernel (T3,T4) · live-state split (T3) · router (T5) · typed facts + freshness (T6) · archival/rotation (T7) · disk hygiene (T2) · before/after proof (T1,T8) · brother-broadcast (T8). ✓ All four-source recommendations covered.

**Placeholder scan:** every task has exact files, exact commands, real code (linter, backfill, router). No TBDs. ✓

**Consistency:** `agents/STATUS.md`, `agents/ACTIVE_TODOS.md`, `agents/routes.yml`, `scripts/memory_lint.py` named identically across all tasks. ✓

**Fence/safety:** off-git backup before prune (T1); `git status` gate before shared-file edits (T3); no history rewrite; additive-before-delete; one commit per task. ✓

**Honest scope note:** `.git` history bloat from the raw dumps is NOT fixed (would need a shared-history rewrite — too risky on master). Working-tree weight + future bloat ARE fixed. Compiled-context / task-packs (Codex's higher-build idea) are deliberately OUT of this plan — revisit only after this lands and proves the boot-light win.
