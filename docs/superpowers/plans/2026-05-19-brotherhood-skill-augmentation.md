# Brotherhood Skill Augmentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the two-track brotherhood skill augmentation arc per `docs/superpowers/specs/2026-05-19-brotherhood-skill-augmentation-design.md` — Track A workflow skills (6 hook extensions + 10 universal skills + 2 specialist + 1 optional + sweeper race-fix) plus Track B dev-bridge expansion (~120-135 new tankoctl commands across 6 schema layers v1.3-v1.8) so the brotherhood's agents do their jobs faster and Hemanth notices less.

**Architecture:** Two parallel tracks under one umbrella arc. Track A is Agent 0 inline authoring of markdown skills in `.claude/commands/` + bash hook extensions in `.claude/scripts/` + sub-agent edits in `.claude/agents/`. Track B is Codex Trigger D commissions, one per agent domain, dispatched in staggered pairs (v1.3 + v1.5 first since they touch independent files; v1.4 + v1.6 second; v1.7 + v1.8 last since they're cross-cutting and benefit from real domain state to test against). Each phase has clear exit criteria; phases land sequentially A→B→C→E1 with D running in parallel; E2 closes when D completes.

**Tech Stack:** Bash 4+ (Git Bash on Windows; POSIX-compatible patterns), Markdown (skill files), C++20 + Qt 6.10 + MSVC 2022 (dispatcher refactor + bridge layer wiring via Codex), QLocalServer + named pipes (existing dev-control transport), QMetaObject::invokeMethod (synthetic UI v1.7 dispatch primitive), GoogleTest (existing tankoban_tests target for any unit-testable extracts).

---

## File Structure (overview)

**Track A — Created files:**
- `.claude/scripts/smoke-evidence-rename.sh` — Phase A.2 NEW
- `.claude/scripts/skill-provenance-detect.sh` — Phase A.5 NEW
- `.claude/commands/rtc.md` — Phase B.1 NEW
- `.claude/commands/mcp-lock.md` — Phase B.2 NEW
- `.claude/commands/smoke-package.md` — Phase B.3 NEW
- `.claude/commands/memory-trim.md` — Phase B.5 NEW
- `.claude/commands/memory-write.md` — Phase B.6 NEW
- `.claude/commands/fix-todo-new.md` — Phase B.7 NEW
- `.claude/commands/codex-trigger-d.md` — Phase B.8 NEW
- `.claude/commands/audit-skeleton.md` — Phase B.9 NEW
- `.claude/commands/handoff-brief.md` — Phase B.10 NEW
- `.claude/commands/summon-from-todo-phase.md` — Phase B.11 NEW
- `.claude/commands/tdd-scaffold.md` — Phase C.1 NEW
- `.claude/commands/smoke-report.md` — Phase C.2 NEW
- `.claude/commands/hemanth-rewrite.md` — Phase C.3 NEW

**Track A — Modified files:**
- `.claude/scripts/pre-rtc-checker.sh` — Phase A.1 (add scaffolding mode + skill-provenance hand-off)
- `.claude/scripts/session-brief.sh` — Phase A.3 + A.4 + A.6 (MEMORY.md size watch + dashboard drift + chat.md rotation watch)
- `.claude/scripts/memory-health.sh` — Phase A.3 (add MEMORY.md byte-count check)
- `.claude/settings.json` — Phase A.7 (no new triggers needed per Codex audit; verify hooks are wired)
- `.claude/agents/commit-sweeper.md` — Phase B.4 (sweeper race-fix)
- `agents/STATUS.md` — Phase E1 (per-agent shortlists)
- `CLAUDE.md` — Phase E1 + E2 (Tier list + bridge quick reference + Which MCP block)

**Track B — Created files (Codex commissions):**
- `src/devtools/UiInteractionDispatcher.h` + `.cpp` — Phase D.5 NEW (v1.7)
- `src/devtools/SystemIntrospection.h` + `.cpp` — Phase D.6 NEW (v1.8)
- `docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md` — Phase D.1 commission spec
- `docs/superpowers/specs/2026-05-19-bridge-v1.4-player-deeper-commission.md` — Phase D.2 commission spec
- `docs/superpowers/specs/2026-05-19-bridge-v1.5-sources-commission.md` — Phase D.3 commission spec
- `docs/superpowers/specs/2026-05-19-bridge-v1.6-library-commission.md` — Phase D.4 commission spec
- `docs/superpowers/specs/2026-05-19-bridge-v1.7-synthetic-ui-commission.md` — Phase D.5 commission spec
- `docs/superpowers/specs/2026-05-19-bridge-v1.8-system-state-commission.md` — Phase D.6 commission spec

**Track B — Modified files (Codex commissions touch):**
- `src/devtools/DevControlServer.h` — schema bump per layer (v1.2 → v1.3 → v1.4 → v1.5 → v1.6 → v1.7 → v1.8); Phase D.0 dispatcher delegation refactor lands first
- `src/ui/MainWindow.{h,cpp}` — dispatcher delegation refactor (Phase D.0) then per-layer command routing (Phase D.1-D.6)
- `tools/tankoctl.cpp` — CLI surface additions per layer
- Per-domain page files (BooksPage, BookSeriesView, `src/ui/readers/BookReader.{h,cpp}`, VideoPlayer, SidecarProcess, SubtitleOverlay, TankorentPage, TankoLibraryPage, TorrentClient, TorrentIndexer, library-section pages, Theme.*) — devSnapshot extensions + command handlers per layer
- `native_sidecar/src/main.cpp` — Phase D.2 IPC extensions for v1.4 sidecar deep-state commands
- `.claude/telemetry/skill-discipline.jsonl` — extended schema per Codex telemetry contract (sessionId + agent + source fields)
- `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\project_dev_control_bridge.md` — ship history extension per layer landing (Phase E2)

---

## Cross-track invariants (read before any phase fires)

1. **Tier defaults:** every new workflow skill starts Tier 2 conditional unless marked hook-fired. NO Tier 1 mandatory until 30-day re-measurement gives empirical evidence per `feedback_skill_discipline_remeasurement.md`.
2. **Schema versioning:** every Track B bridge commission is ADDITIVE within v1.x per `DevControlServer.h:24`. No removals, no renames. Each layer bumps the schema string and adds entries to `ping.commands`.
3. **ASCII protocol anchors:** every new chat.md line emitted by skills/hooks uses ASCII delimiters (` - `, `[`, `]`, `|`) per Rule 16. Parsers may accept legacy em-dash variants but emitters must produce ASCII.
4. **Attribution:** Codex commissioned work commits under `[Agent N (Codex), <work>]` where Agent N is the requesting agent (not Agent 7).
5. **Skill naming:** all references to superpowers plugin skills use `superpowers:<skill>` form per `feedback_always_prefix_superpowers.md`.
6. **Hook discipline:** hooks are passive (warn/scaffold, never block agent turns). Each hook script exits 0 on any error path. `set +e` at the top.
7. **No new PreToolUse triggers** per Codex audit finding #3 — existing Stop hook + SessionStart hook are sufficient. PreToolUse triggers add complexity for marginal benefit when Stop already runs every turn.
8. **Sweep dependency:** any plan task that creates new chat.md content should NOT post an RTC mid-execution; RTCs land at task-commit time per `feedback_commit_protocol.md` Rule 11.

---

## Phase A — Hook extensions (1 wake)

Six hooks total. All are extensions to existing scripts; no new trigger types in `.claude/settings.json`.

### Task A.1 — Extend pre-rtc-checker.sh with scaffolding mode

**Files:**
- Modify: `.claude/scripts/pre-rtc-checker.sh` (currently 149 lines, Stop-hook fires after every turn)

**Goal:** When a non-trivial RTC is missing the `Skills invoked:` field, emit not just a nag but a complete scaffolded replacement line the agent can copy. Add a stale-file warning when the `files:` list includes paths that are clean against HEAD or no longer exist.

- [ ] **Step 1: Read current pre-rtc-checker.sh end-to-end and identify the nag emission block**

Run: `cat .claude/scripts/pre-rtc-checker.sh | grep -n "system-reminder"`
Expected: line 140 (start of nag block) and line 144 (end).

- [ ] **Step 2: Add the scaffolding function after the parse loop**

Add this function between line 135 (after `done <<< "$ADDED_RTCS"`) and line 137 (before the nag emission block):

```bash
# -------- Step 2.5: scaffold corrected RTC + detect stale file references --------
# Per Codex audit finding #1: emit a complete corrected RTC line per nag-eligible
# RTC, so the agent can copy-paste rather than re-typing the field structure.
# Also flag files: paths that are clean against HEAD or missing entirely.

SCAFFOLD_LINES=""
STALE_LINES=""
while IFS= read -r LINE; do
    [ -z "$LINE" ] && continue
    TAG="$(echo "$LINE" | sed -nE 's/^READY TO COMMIT [—-] \[([^]]+)\]:.*/\1/p')"
    [ -z "$TAG" ] && continue

    FILES_RAW="$(echo "$LINE" | sed -nE 's/.*\| files:\s*(.+)$/\1/p')"
    [ -z "$FILES_RAW" ] && continue

    # Stale-file detection (Codex finding: warn on missing or clean files).
    OLD_IFS="$IFS"
    IFS=','
    for F in $FILES_RAW; do
        F_TRIMMED="$(echo "$F" | xargs)"
        F_PATH="$(echo "$F_TRIMMED" | sed -E 's/\s*\([A-Z]+\)\s*$//')"
        [ -z "$F_PATH" ] && continue
        if [ ! -e "$F_PATH" ]; then
            STALE_LINES="${STALE_LINES}  - [${TAG}]: missing file ${F_PATH}\n"
        elif [ -f "$F_PATH" ] && ! git diff --quiet HEAD -- "$F_PATH" 2>/dev/null; then
            : # File has diff — OK
        elif [ -f "$F_PATH" ]; then
            STALE_LINES="${STALE_LINES}  - [${TAG}]: file ${F_PATH} clean vs HEAD (already committed?)\n"
        fi
    done
    IFS="$OLD_IFS"

    # Skill-provenance scaffold for non-trivial + missing field.
    SKILLS_PRESENT=0
    echo "$LINE" | grep -qE '\| Skills invoked:\s*\[/' && SKILLS_PRESENT=1
    if [ "$SKILLS_PRESENT" -eq 0 ]; then
        # Detect candidate skills from this session's tool log (best-effort).
        DETECTED="$(bash "$REPO_ROOT/.claude/scripts/skill-provenance-detect.sh" --candidates-only 2>/dev/null || echo "")"
        [ -z "$DETECTED" ] && DETECTED="/build-verify, /superpowers:verification-before-completion"

        # Build the scaffolded replacement.
        MESSAGE_BODY="$(echo "$LINE" | sed -nE 's/^READY TO COMMIT [—-] \[[^]]+\]:\s+(.+?)\s+\| files:.*/\1/p')"
        SCAFFOLD_LINES="${SCAFFOLD_LINES}  Scaffolded for [${TAG}]:\n    READY TO COMMIT - [${TAG}]: ${MESSAGE_BODY} | Skills invoked: [${DETECTED}] | files: ${FILES_RAW}\n\n"
    fi
done <<< "$ADDED_RTCS"
```

- [ ] **Step 3: Extend the nag emission block to include scaffold + stale lines**

Replace lines 138-146 (the existing nag block) with:

```bash
if [ "$NAG_COUNT" -gt 0 ] || [ -n "$STALE_LINES" ]; then
    cat <<EOF
<system-reminder>
[pre-rtc-checker, contracts-v3 nag-only mode] Working-tree chat.md needs attention.
EOF

    if [ "$NAG_COUNT" -gt 0 ]; then
        cat <<EOF

$NAG_COUNT non-trivial RTC(s) missing 'Skills invoked: [/skill1, /skill2, ...]' field:
$(printf "%b" "$NAG_LINES")
EOF
    fi

    if [ -n "$SCAFFOLD_LINES" ]; then
        cat <<EOF

Suggested scaffolded replacements (detected from session telemetry; verify before pasting):
$(printf "%b" "$SCAFFOLD_LINES")
EOF
    fi

    if [ -n "$STALE_LINES" ]; then
        cat <<EOF

Stale file references in RTCs (file missing or clean against HEAD):
$(printf "%b" "$STALE_LINES")
EOF
    fi

    cat <<EOF
See agents/CONTRACTS.md § Skill Provenance in RTCs for the format. Nag-only first 30 days.
</system-reminder>
EOF
fi
```

- [ ] **Step 4: Smoke test the extended hook with a real chat.md tail**

Run:
```bash
cd /c/Users/Suprabha/Desktop/Tankoban\ 2
bash .claude/scripts/pre-rtc-checker.sh
```

Expected: if working-tree chat.md has any uncommitted RTCs missing the `Skills invoked:` field, see a system-reminder block with nag + scaffolded replacements. If clean, see no output (silent exit 0).

- [ ] **Step 5: Commit Task A.1**

```bash
git add .claude/scripts/pre-rtc-checker.sh
git commit -m "$(cat <<'EOF'
[Agent 0, SKILL_AUGMENTATION_ARC Phase A.1 — pre-rtc-checker scaffolding mode]: extend pre-rtc nag to emit complete scaffolded RTC replacements + stale-file warnings

Per Codex audit finding #1 in 2026-05-19-brotherhood-skill-augmentation-design.md: pre-rtc-checker.sh nag-only mode now emits a complete corrected RTC line per missing-field violation, plus warns on stale file: paths (missing OR clean against HEAD). Skill detection hand-off via .claude/scripts/skill-provenance-detect.sh --candidates-only (graceful fallback to /build-verify + /superpowers:verification-before-completion when telemetry unavailable). Stop-hook based; passive (never blocks); always exit 0. ASCII-only emissions per Rule 16.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task A.2 — Author smoke-evidence-rename.sh

**Files:**
- Create: `.claude/scripts/smoke-evidence-rename.sh`

**Goal:** Stop-hook script that scans working-tree `agents/audits/smoke_evidence/` for files written this turn, validates naming convention (`<UPPERCASE_FINDING>_<HHMMSS>.<ext>` or `<finding>_NN.png` sequence), and warns on violations.

Per Codex audit finding #2: PreToolUse cannot prove the file landed; use Stop-time validation instead. Post-write reconciliation: check evidence dir contents vs working-tree change set; flag misnamed files and missing-stub-md.

- [ ] **Step 1: Create the script with the standard hook scaffolding**

```bash
cat > .claude/scripts/smoke-evidence-rename.sh <<'SCRIPT_EOF'
#!/usr/bin/env bash
# Stop-hook helper for Tankoban 2 — smoke-evidence naming + bundle reconciliation.
#
# Purpose: when an agent adds files to agents/audits/smoke_evidence/ during a
# turn, validate naming convention + check for a matching evidence stub-md.
# Emits warnings via a single system-reminder block when violations are found.
#
# Mode: WARN-ONLY. Always exits 0. Never blocks agent turn.
#
# Called by: Stop hook (registered via .claude/settings.json).
#
# Convention (per project_smoke_evidence_naming):
#   - PNG screenshot: <UPPERCASE_FINDING>_<HHMMSS>.png  OR  <finding>_NN.png
#   - Each evidence dir SHOULD have an evidence_<NAME>_<HHMMSS>.md companion
#   - The /smoke-package command produces compliant names by default

set +e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

[ -d agents/audits/smoke_evidence ] || exit 0

# Files added since HEAD under smoke_evidence (untracked + staged additions).
ADDED_FILES="$(git status --porcelain agents/audits/smoke_evidence 2>/dev/null \
    | grep -E '^(\?\?|A |M )' \
    | awk '{print $2}')"

[ -z "$ADDED_FILES" ] && exit 0

WARN_LINES=""

while IFS= read -r FILE; do
    [ -z "$FILE" ] && continue
    BASENAME="$(basename "$FILE")"
    EXT="${BASENAME##*.}"

    case "$EXT" in
        png|jpg|jpeg|gif|webp)
            # Validate against convention.
            if ! echo "$BASENAME" | grep -qE '^([A-Z][A-Z0-9_]+_[0-9]{6}|[a-z0-9_-]+_[0-9]{2,3})\.(png|jpg|jpeg|gif|webp)$'; then
                WARN_LINES="${WARN_LINES}  - ${FILE} — does not match <UPPERCASE>_<HHMMSS>.png OR <slug>_NN.png\n"
            fi
            ;;
        log|md|jsonl)
            # Companion artifacts are OK in any naming.
            :
            ;;
    esac
done <<< "$ADDED_FILES"

if [ -n "$WARN_LINES" ]; then
    cat <<EOF
<system-reminder>
[smoke-evidence-rename] Smoke evidence files added this turn that do not match Tankoban's naming convention:
$(printf "%b" "$WARN_LINES")
Convention: <UPPERCASE_FINDING>_<HHMMSS>.png (one-off) or <slug>_NN.png (sequence). Use /smoke-package <finding-name> to scaffold compliant names automatically. Non-blocking warn; commit will still succeed.
</system-reminder>
EOF
fi

exit 0
SCRIPT_EOF
chmod +x .claude/scripts/smoke-evidence-rename.sh
```

- [ ] **Step 2: Smoke test with a fake violation**

Run:
```bash
mkdir -p agents/audits/smoke_evidence
touch agents/audits/smoke_evidence/badly_named_file.png
bash .claude/scripts/smoke-evidence-rename.sh
```

Expected: system-reminder warning naming the violation. Clean up: `rm agents/audits/smoke_evidence/badly_named_file.png`.

- [ ] **Step 3: Smoke test with a compliant filename**

Run:
```bash
touch agents/audits/smoke_evidence/TEST_FINDING_123456.png
bash .claude/scripts/smoke-evidence-rename.sh
rm agents/audits/smoke_evidence/TEST_FINDING_123456.png
```

Expected: silent exit 0 (no warning — name complies).

- [ ] **Step 4: Commit Task A.2**

```bash
git add .claude/scripts/smoke-evidence-rename.sh
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase A.2 — smoke-evidence-rename hook]: warn-only validator for smoke evidence naming convention. Stop-hook based; non-blocking. Per Codex audit finding #2 (PreToolUse cannot prove file landed; use Stop-time reconciliation instead).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task A.3 — Extend memory-health.sh with MEMORY.md size watch

**Files:**
- Modify: `.claude/scripts/memory-health.sh` (currently 71 lines)
- Modify: `.claude/scripts/session-brief.sh` (currently 119 lines)

**Goal:** memory-health.sh currently probes the claude-mem worker for observation/corpus health. Extend to ALSO check MEMORY.md file byte count. If > 24.4KB, emit a degraded line that session-brief.sh surfaces in the brief.

- [ ] **Step 1: Add MEMORY.md byte-count check to memory-health.sh**

Append before line 71 (the `exit 0` line):

```bash
# -------- MEMORY.md size check (Codex audit, Phase A.3) --------
# Per spec: if MEMORY.md > 24.4KB hard cap, signal degraded.
# 32KB threshold = red banner via session-brief.sh.

MEM_FILE="$HOME/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/MEMORY.md"
MEM_BYTES=0
if [ -f "$MEM_FILE" ]; then
    MEM_BYTES="$(stat -c%s "$MEM_FILE" 2>/dev/null || stat -f%z "$MEM_FILE" 2>/dev/null || echo 0)"
fi

# Size thresholds.
if [ "$MEM_BYTES" -gt 32768 ] 2>/dev/null; then
    echo "DEGRADED: MEMORY.md=${MEM_BYTES}b (>32KB RED — run /memory-trim NOW; observations:${OBS_COUNT:-?})"
    exit 1
elif [ "$MEM_BYTES" -gt 24986 ] 2>/dev/null; then  # 24.4KB = 24986 bytes
    echo "DEGRADED: MEMORY.md=${MEM_BYTES}b (>24.4KB cap — run /memory-trim; observations:${OBS_COUNT:-?})"
    exit 1
fi
```

Place this block AFTER the existing healthy-exit logic so it runs in the healthy-claude-mem path. Reorganize so the exit-1 here returns early if memory size is degraded, even when claude-mem is healthy.

Actually clearer: replace the final block (lines 56-71) with this rewritten ordering:

```bash
# -------- Decision logic (claude-mem health + MEMORY.md size) --------

CLAUDE_MEM_DEGRADED=0
CLAUDE_MEM_REASON=""
if [ "$OBS_COUNT" -eq 0 ] || [ "$CORPUS_EMPTY" -eq 1 ]; then
    CLAUDE_MEM_DEGRADED=1
    if [ "$OBS_COUNT" -eq 0 ]; then
        CLAUDE_MEM_REASON="${CLAUDE_MEM_REASON}observations:0 "
    fi
    if [ "$CORPUS_EMPTY" -eq 1 ]; then
        CLAUDE_MEM_REASON="${CLAUDE_MEM_REASON}corpora:empty "
    fi
fi

# MEMORY.md size check (Phase A.3 extension).
MEM_FILE="$HOME/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/MEMORY.md"
MEM_BYTES=0
if [ -f "$MEM_FILE" ]; then
    MEM_BYTES="$(stat -c%s "$MEM_FILE" 2>/dev/null || stat -f%z "$MEM_FILE" 2>/dev/null || echo 0)"
fi
MEM_DEGRADED=0
MEM_REASON=""
if [ "$MEM_BYTES" -gt 32768 ] 2>/dev/null; then
    MEM_DEGRADED=1
    MEM_REASON="MEMORY.md=${MEM_BYTES}b (>32KB RED — run /memory-trim NOW)"
elif [ "$MEM_BYTES" -gt 24986 ] 2>/dev/null; then
    MEM_DEGRADED=1
    MEM_REASON="MEMORY.md=${MEM_BYTES}b (>24.4KB cap — run /memory-trim)"
fi

# Emit single status line covering both axes.
if [ "$CLAUDE_MEM_DEGRADED" -eq 1 ] && [ "$MEM_DEGRADED" -eq 1 ]; then
    echo "DEGRADED: ${CLAUDE_MEM_REASON}+ ${MEM_REASON} (sessions:${SESS_COUNT})"
    exit 1
elif [ "$CLAUDE_MEM_DEGRADED" -eq 1 ]; then
    echo "DEGRADED: ${CLAUDE_MEM_REASON}(sessions:${SESS_COUNT} — pipeline stores prompts but not memory)"
    exit 1
elif [ "$MEM_DEGRADED" -eq 1 ]; then
    echo "DEGRADED: ${MEM_REASON} (claude-mem:healthy observations:${OBS_COUNT})"
    exit 1
fi

echo "HEALTHY: observations:${OBS_COUNT} sessions:${SESS_COUNT} MEMORY.md:${MEM_BYTES}b (corpora present)"
exit 0
```

- [ ] **Step 2: Update session-brief.sh banner to cite MEMORY.md size if degraded**

In `.claude/scripts/session-brief.sh`, lines 78-86, update the `MEMORY_BANNER` block to include /memory-trim recommendation when memory degraded:

Replace:
```bash
MEMORY_BANNER="
=== MEMORY DEGRADED ===
${MEMORY_STATUS}
Skill-discipline rules referencing /mem-search are auto-relaxed for this session.
"
```

With:
```bash
MEMORY_BANNER="
=== MEMORY DEGRADED ===
${MEMORY_STATUS}
$(echo "$MEMORY_STATUS" | grep -q 'MEMORY.md=' && echo 'Run /memory-trim to propose archive candidates before next sweep.' || echo 'Skill-discipline rules referencing /mem-search are auto-relaxed for this session.')
"
```

- [ ] **Step 3: Smoke test memory-health.sh**

Run:
```bash
bash .claude/scripts/memory-health.sh
```

Expected: since MEMORY.md is currently 27.4KB (per system reminder), see `DEGRADED: MEMORY.md=...b (>24.4KB cap — run /memory-trim) (claude-mem:healthy observations:N)` or similar. Exit code 1.

- [ ] **Step 4: Smoke test session-brief.sh**

Run:
```bash
bash .claude/scripts/session-brief.sh
```

Expected: digest shows MEMORY DEGRADED banner naming MEMORY.md size + `Run /memory-trim` recommendation.

- [ ] **Step 5: Commit Task A.3**

```bash
git add .claude/scripts/memory-health.sh .claude/scripts/session-brief.sh
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase A.3 — MEMORY.md size watch]: memory-health.sh now also checks MEMORY.md file size; >24.4KB triggers degraded + /memory-trim recommendation in session-brief banner. >32KB escalates to RED banner. Per spec Section 3 hook #3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task A.4 — Extend session-brief.sh with dashboard drift detection

**Files:**
- Modify: `.claude/scripts/session-brief.sh`

**Goal:** session-brief.sh already detects stale STATUS sections (>7d). Extend to also check CLAUDE.md "Last successful smoke" line + dashboard "As of:" line against recent commits — flag if mismatched.

- [ ] **Step 1: Add dashboard-drift detection block after the stale-STATUS check**

After line 56 (`[ -z "$STALE_AGENTS" ] && STALE_AGENTS=" none"`), add:

```bash
# Dashboard drift detection (Phase A.4 extension).
# Check CLAUDE.md "As of:" line + "Last successful smoke" line for staleness
# vs git log of last src/ commit.
DASH_DRIFT=""
if [ -f CLAUDE.md ]; then
    AS_OF_DATE="$(grep -oE '\*\*As of:\*\* [0-9]{4}-[0-9]{2}-[0-9]{2}' CLAUDE.md | head -1 | grep -oE '[0-9]{4}-[0-9]{2}-[0-9]{2}')"
    if [ -n "$AS_OF_DATE" ]; then
        AS_OF_EPOCH="$(date -d "$AS_OF_DATE" +%s 2>/dev/null || echo 0)"
        LAST_SRC_COMMIT_DATE="$(git log -1 --format=%cs -- src/ 2>/dev/null)"
        LAST_SRC_EPOCH="$(date -d "$LAST_SRC_COMMIT_DATE" +%s 2>/dev/null || echo 0)"
        if [ "$AS_OF_EPOCH" -gt 0 ] && [ "$LAST_SRC_EPOCH" -gt "$AS_OF_EPOCH" ]; then
            DAYS_DRIFT=$(( (LAST_SRC_EPOCH - AS_OF_EPOCH) / 86400 ))
            DASH_DRIFT=" CLAUDE.md As-of:${AS_OF_DATE} but last src/ commit:${LAST_SRC_COMMIT_DATE} (${DAYS_DRIFT}d drift)"
        fi
    fi
fi
```

- [ ] **Step 2: Surface the drift line in the digest cat block**

After the existing `- STATUS sections >7d stale:${STALE_AGENTS}` line, add:

```bash
- Dashboard drift:${DASH_DRIFT:- none}
```

- [ ] **Step 3: Smoke test dashboard drift**

Run:
```bash
bash .claude/scripts/session-brief.sh
```

Expected: digest now includes a `Dashboard drift:` line. If CLAUDE.md "As of:" is 2026-05-05 and last src/ commit was today, see N-day drift count. Otherwise see ` none`.

- [ ] **Step 4: Commit Task A.4**

```bash
git add .claude/scripts/session-brief.sh
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase A.4 — dashboard drift detection]: session-brief.sh now compares CLAUDE.md 'As of:' date against last src/ commit date; flags drift in the SessionStart digest. Per spec Section 3 hook #4.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task A.5 — Author skill-provenance-detect.sh

**Files:**
- Create: `.claude/scripts/skill-provenance-detect.sh`

**Goal:** Helper script that scans the current Claude Code session's tool-use log (via Claude Code's own telemetry conventions) and emits a comma-separated list of `superpowers:<skill>` and `/skill` invocations detected this session. Used by pre-rtc-checker.sh (Task A.1) as `--candidates-only`. Stop-hook on its own to populate `.claude/telemetry/skill-invocations.jsonl`.

Per Codex telemetry contract (audit finding):
- Each row: `{ts, sessionId, agent, skill, source, cwd}`
- Codex source = `codex-text` when skill is detected from text rather than tool call
- Detector ignores telemetry older than current session start

**Note:** Since Claude Code doesn't expose a per-skill telemetry stream directly, we approximate by scanning the agent's recent chat for Skill tool invocations + skill mentions in agent text. The script's value is in NORMALIZING the detected list, not in being a perfect record.

- [ ] **Step 1: Create the script with parse logic**

```bash
cat > .claude/scripts/skill-provenance-detect.sh <<'SCRIPT_EOF'
#!/usr/bin/env bash
# Skill-provenance detector for Tankoban 2 (Phase A.5).
#
# Purpose: emit a comma-separated list of /skill and superpowers:<skill>
# names detected as having fired in this session. Used by pre-rtc-checker.sh
# scaffolding mode (Task A.1) to suggest field values for missing Skills invoked.
#
# Mode: best-effort. If no telemetry available, emits a conservative default
# (/build-verify, /superpowers:verification-before-completion) for non-trivial
# RTCs. Never fails loud.
#
# Modes:
#   bash skill-provenance-detect.sh                 → emit JSONL append for current session
#   bash skill-provenance-detect.sh --candidates-only → emit comma-separated skill list (for pre-rtc-checker)
#   bash skill-provenance-detect.sh --schema-check  → emit telemetry schema for documentation
#
# Telemetry contract (per Codex audit):
#   { "ts": "<ISO ts>", "sessionId": "<id>", "agent": "<agent-tag>", "skill": "<name>", "source": "<claude-code|codex-text>", "cwd": "<pwd>" }
# Codex rows use source=codex-text. Detector ignores rows outside current session.

set +e

MODE="${1:-jsonl}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT" 2>/dev/null || exit 0

TELEMETRY_DIR=".claude/telemetry"
TELEMETRY_FILE="${TELEMETRY_DIR}/skill-invocations.jsonl"
SESSION_FILE="${TELEMETRY_DIR}/.current-session-id"
mkdir -p "$TELEMETRY_DIR" 2>/dev/null

# Get session id (Claude Code passes via env or we generate from session start time).
SESSION_ID="${CLAUDE_SESSION_ID:-$(cat "$SESSION_FILE" 2>/dev/null)}"
if [ -z "$SESSION_ID" ]; then
    SESSION_ID="session-$(date -u +%Y%m%dT%H%M%SZ)"
    echo "$SESSION_ID" > "$SESSION_FILE" 2>/dev/null
fi

case "$MODE" in
    --schema-check)
        cat <<EOF
{ "ts": "<ISO8601 UTC>", "sessionId": "<string>", "agent": "<string>", "skill": "<string>", "source": "claude-code|codex-text", "cwd": "<string>" }
EOF
        exit 0
        ;;
    --candidates-only)
        # Scan working-tree chat.md tail for this session's Skill tool invocations.
        # Heuristic: any line matching "Skill tool invocation: superpowers:<name>"
        # OR "/superpowers:<name>" mentions in agent text within the last 200 lines.

        if [ -f agents/chat.md ]; then
            CANDIDATES="$(tail -300 agents/chat.md \
                | grep -oE '(superpowers:|/superpowers:|/(brief|simplify|build-verify|commit-sweep|repo-health|rotate-chat|session-recap|security-review|claude-mem:[a-z-]+))[a-z-]*' \
                | sort -u \
                | head -10 \
                | tr '\n' ',' \
                | sed 's/,$//')"

            # Normalize: ensure all start with / for slash-skills, superpowers: prefix preserved.
            CANDIDATES="$(echo "$CANDIDATES" | sed -E 's/(^|,)([a-z])/\1\/\2/g; s/superpowers:\//\/superpowers:/g')"

            if [ -z "$CANDIDATES" ]; then
                # Conservative fallback.
                CANDIDATES="/build-verify, /superpowers:verification-before-completion"
            fi
            echo "$CANDIDATES"
        else
            echo "/build-verify, /superpowers:verification-before-completion"
        fi
        exit 0
        ;;
    jsonl|*)
        # Default: scan for new skill mentions vs telemetry tail, append rows.
        # This is best-effort logging; the --candidates-only output is the real value.

        if [ ! -f agents/chat.md ]; then
            exit 0
        fi

        TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        CWD="$(pwd | tr '\\' '/')"

        # Detect skills from session tail. Add a row per unique skill.
        SKILLS="$(tail -100 agents/chat.md \
            | grep -oE '/(brief|simplify|build-verify|commit-sweep|repo-health|rotate-chat|session-recap|security-review|superpowers:[a-z-]+|claude-mem:[a-z-]+)' \
            | sort -u)"

        while IFS= read -r SKILL; do
            [ -z "$SKILL" ] && continue
            # Dedup against last 50 rows of telemetry for this session.
            if [ -f "$TELEMETRY_FILE" ] && tail -50 "$TELEMETRY_FILE" 2>/dev/null | grep -qF "\"sessionId\":\"${SESSION_ID}\"" | grep -qF "\"skill\":\"${SKILL}\""; then
                continue
            fi
            printf '{"ts":"%s","sessionId":"%s","agent":"unknown","skill":"%s","source":"claude-code","cwd":"%s"}\n' \
                "$TS" "$SESSION_ID" "$SKILL" "$CWD" \
                >> "$TELEMETRY_FILE" 2>/dev/null
        done <<< "$SKILLS"

        exit 0
        ;;
esac

exit 0
SCRIPT_EOF
chmod +x .claude/scripts/skill-provenance-detect.sh
```

- [ ] **Step 2: Smoke test candidates-only mode**

Run:
```bash
bash .claude/scripts/skill-provenance-detect.sh --candidates-only
```

Expected: a comma-separated list of /skill names detected from chat.md tail (e.g. `/superpowers:brainstorming, /superpowers:writing-plans, /build-verify`).

- [ ] **Step 3: Smoke test schema-check mode**

Run:
```bash
bash .claude/scripts/skill-provenance-detect.sh --schema-check
```

Expected: prints the JSONL row schema documentation.

- [ ] **Step 4: Smoke test jsonl mode**

Run:
```bash
bash .claude/scripts/skill-provenance-detect.sh
cat .claude/telemetry/skill-invocations.jsonl | tail -5
```

Expected: telemetry file has 1+ row per unique skill detected this session, each with `ts`, `sessionId`, `agent`, `skill`, `source`, `cwd` fields.

- [ ] **Step 5: Commit Task A.5**

```bash
git add .claude/scripts/skill-provenance-detect.sh
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase A.5 — skill-provenance detect helper]: best-effort skill detector for pre-rtc scaffolding + telemetry append. Modes: --candidates-only (used by pre-rtc-checker), --schema-check (docs), default jsonl append. Per Codex telemetry contract (ts/sessionId/agent/skill/source/cwd).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task A.6 — Chat.md rotation watch (already partial in session-brief.sh)

**Files:**
- Modify: `.claude/scripts/session-brief.sh`

**Goal:** Session-brief.sh already detects chat.md > 3000 lines (line 36-38). Extend to also check byte size > 300KB.

- [ ] **Step 1: Add byte-size threshold to chat.md size detection block**

Update lines 32-39 from:

```bash
CHAT_LINES=0
CHAT_WARN=""
if [ -f agents/chat.md ]; then
    CHAT_LINES="$(wc -l < agents/chat.md 2>/dev/null || echo 0)"
    if [ "$CHAT_LINES" -gt 3000 ] 2>/dev/null; then
        CHAT_WARN=" [ROTATION DUE — run /rotate-chat]"
    fi
fi
```

To:

```bash
CHAT_LINES=0
CHAT_BYTES=0
CHAT_WARN=""
if [ -f agents/chat.md ]; then
    CHAT_LINES="$(wc -l < agents/chat.md 2>/dev/null || echo 0)"
    CHAT_BYTES="$(stat -c%s agents/chat.md 2>/dev/null || stat -f%z agents/chat.md 2>/dev/null || echo 0)"
    if [ "$CHAT_LINES" -gt 3000 ] 2>/dev/null; then
        CHAT_WARN=" [ROTATION DUE: ${CHAT_LINES} lines — run /rotate-chat]"
    elif [ "$CHAT_BYTES" -gt 307200 ] 2>/dev/null; then  # 300KB
        CHAT_WARN=" [ROTATION DUE: ${CHAT_BYTES}b — run /rotate-chat]"
    fi
fi
```

- [ ] **Step 2: Smoke test rotation watch**

Run:
```bash
bash .claude/scripts/session-brief.sh
```

Expected: digest's `chat.md` line shows current line count + rotation warning if applicable.

- [ ] **Step 3: Commit Task A.6**

```bash
git add .claude/scripts/session-brief.sh
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase A.6 — chat.md rotation watch]: session-brief now triggers ROTATION DUE warning on either >3000 lines OR >300KB. Per spec Section 3 hook #6.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task A.7 — Wire smoke-evidence-rename into Stop hook

**Files:**
- Modify: `.claude/settings.json`

**Goal:** smoke-evidence-rename.sh (Phase A.2) needs to be wired into the Stop hook so it fires every turn alongside pre-rtc-checker.

- [ ] **Step 1: Add the hook entry under "Stop"**

Edit `.claude/settings.json`. Replace the current `"Stop"` block:

```json
"Stop": [
  {
    "hooks": [
      {
        "type": "command",
        "command": "bash .claude/scripts/pre-rtc-checker.sh",
        "timeout": 5
      }
    ]
  }
]
```

With:

```json
"Stop": [
  {
    "hooks": [
      {
        "type": "command",
        "command": "bash .claude/scripts/pre-rtc-checker.sh",
        "timeout": 5
      },
      {
        "type": "command",
        "command": "bash .claude/scripts/smoke-evidence-rename.sh",
        "timeout": 3
      },
      {
        "type": "command",
        "command": "bash .claude/scripts/skill-provenance-detect.sh",
        "timeout": 3
      }
    ]
  }
]
```

- [ ] **Step 2: Verify settings.json is valid JSON**

Run:
```bash
python3 -c "import json; json.load(open('.claude/settings.json'))" && echo "VALID JSON"
```

Expected: `VALID JSON`

- [ ] **Step 3: Restart Claude Code session and verify Stop hooks fire**

Manual verification: end the current Claude Code turn, watch for any system-reminder blocks from the new hooks. (This is a one-time post-merge smoke; the Stop hooks fire on every subsequent turn.)

- [ ] **Step 4: Commit Task A.7**

```bash
git add .claude/settings.json
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase A.7 — wire smoke-evidence + skill-provenance into Stop hook]: settings.json now fires three Stop hooks (pre-rtc-checker, smoke-evidence-rename, skill-provenance-detect) on every turn. All three are non-blocking; combined timeout 11s.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task A.8 — Phase A smoke matrix

**Files:** none (verification only)

**Goal:** End-to-end verification that Phase A hooks all fire correctly without breaking each other or the brief.

- [ ] **Step 1: Run session-brief.sh and inspect output**

Run:
```bash
bash .claude/scripts/session-brief.sh
```

Expected: digest output includes (a) Uncommitted RTC count, (b) CONGRESS state, (c) chat.md size + rotation warning if applicable, (d) STATUS staleness, (e) MEMORY DEGRADED banner naming MEMORY.md size, (f) Dashboard drift line, (g) chat.md rotation warning if applicable, (h) standard skill-trigger map.

- [ ] **Step 2: Run pre-rtc-checker.sh**

Run:
```bash
bash .claude/scripts/pre-rtc-checker.sh
```

Expected: if working-tree chat.md has RTCs missing `Skills invoked:`, see scaffolded replacement + stale-file warnings. Otherwise silent exit 0.

- [ ] **Step 3: Run smoke-evidence-rename.sh**

Run:
```bash
bash .claude/scripts/smoke-evidence-rename.sh
```

Expected: scans untracked + staged files under `agents/audits/smoke_evidence/`; warns on naming violations. Silent if all compliant.

- [ ] **Step 4: Run skill-provenance-detect.sh in all 3 modes**

Run:
```bash
bash .claude/scripts/skill-provenance-detect.sh --candidates-only
bash .claude/scripts/skill-provenance-detect.sh --schema-check
bash .claude/scripts/skill-provenance-detect.sh
```

Expected: comma-separated candidates list, schema doc, and silent jsonl append respectively.

- [ ] **Step 5: Verify telemetry JSONL is well-formed**

Run:
```bash
tail -5 .claude/telemetry/skill-invocations.jsonl | python3 -c "import sys, json; [json.loads(line) for line in sys.stdin]" && echo "JSONL OK"
```

Expected: `JSONL OK`. Every row parses as valid JSON.

- [ ] **Step 6: No commit (verification only)**

Phase A complete. Move to Phase B.

---

## Phase B — Workflow skills (2-3 wakes)

10 universal skills + sweeper race-fix. Each skill is markdown in `.claude/commands/` matching the existing brotherhood pattern.

### Task B.1 — Author `/rtc` skill

**Files:**
- Create: `.claude/commands/rtc.md`

**Goal:** Scaffold a fully-formed contracts-v3 RTC line; validate against current git status + sweeper regex BEFORE the agent posts.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/rtc.md <<'EOF'
You are scaffolding a contracts-v3 RTC line for the agent to paste into agents/chat.md.

**Arguments:**
- `<tag>` — required, e.g. `Agent 4, Bug X round-2 fix`
- `<message>` — required, one-line commit subject describing the work

**Procedure:**

1. **Detect dirty files since HEAD.** Run:
   ```
   git diff --name-only HEAD
   git status --porcelain
   ```
   Filter to files that have actual diffs OR are staged additions. Skip ignored paths.

2. **Detect skills invoked this session.** Run:
   ```
   bash .claude/scripts/skill-provenance-detect.sh --candidates-only
   ```
   Capture the comma-separated list. If empty or only whitespace, fall back to `/build-verify, /superpowers:verification-before-completion`.

3. **Classify trivial vs non-trivial.** Non-trivial = ≥1 file under `src/` or `native_sidecar/src/`, OR ≥30 LOC changed cumulative against HEAD. Trivial RTCs may omit the `Skills invoked:` field; non-trivial must include it.

4. **Build the scaffolded line.** Use ASCII delimiters per Rule 16:
   ```
   READY TO COMMIT - [<tag>]: <message> | Skills invoked: [<skill-list>] | files: <file1>, <file2>, ...
   ```

5. **Validate against sweeper regex.** The line must match:
   ```
   ^READY TO COMMIT [—-] \[([^\]]+)\]:\s+(.+?)(?:\s+\|\s+Skills invoked:\s+\[([^\]]*)\])?\s+\|\s+files:\s+(.+?)\s*$
   ```
   If it fails to match, fix it (most common cause: unescaped pipe in message body, or empty files list).

6. **Emit the line to stdout.** Print the scaffolded RTC line. Do NOT append to chat.md automatically — the agent decides when to post.

**Quality gates:**
- File list reflects actual working-tree changes (no fabrications)
- Skills field only present for non-trivial RTCs
- ASCII delimiters only
- No trailing whitespace
- Single line (no embedded newlines in the RTC itself)

**Examples:**

For `/rtc "Agent 4, Bug 5 fix" "stop button color regression resolved"`:
```
READY TO COMMIT - [Agent 4, Bug 5 fix]: stop button color regression resolved | Skills invoked: [/build-verify, /superpowers:systematic-debugging] | files: src/ui/pages/StreamPage.cpp, agents/chat.md
```

For trivial RTC like `/rtc "Agent 0, docs typo" "fix typo in CLAUDE.md dashboard"`:
```
READY TO COMMIT - [Agent 0, docs typo]: fix typo in CLAUDE.md dashboard | files: CLAUDE.md
```
EOF
```

- [ ] **Step 2: Smoke test — invoke the skill via Claude Code's Skill tool**

In a fresh Claude Code session, run:
```
/rtc "Agent 0, RTC skill smoke" "verify /rtc scaffolds a clean line"
```

Expected: a properly-formed RTC line is emitted to stdout. The line passes the sweeper regex validation (verify by pasting into a chat.md-shaped buffer and running the sweeper parser).

- [ ] **Step 3: Validate the emitted line against the actual sweeper regex**

Run:
```bash
echo "READY TO COMMIT - [Agent 0, RTC skill smoke]: verify /rtc scaffolds a clean line | Skills invoked: [/build-verify] | files: .claude/commands/rtc.md" \
| grep -E '^READY TO COMMIT [—-] \[([^]]+)\]:\s+(.+?)(\s+\|\s+Skills invoked:\s+\[([^]]*)\])?\s+\|\s+files:\s+(.+?)\s*$'
```

Expected: line echoed back (regex match). If no match, the skill emission needs revision.

- [ ] **Step 4: Commit Task B.1**

```bash
git add .claude/commands/rtc.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.1 — /rtc skill]: scaffolds contracts-v3 RTC line with auto-detected files + skills + non-trivial classification. ASCII delimiters per Rule 16. Sweeper-regex validated. Per spec Section 1 Bucket B + B.1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.2 — Author `/mcp-lock` skill

**Files:**
- Create: `.claude/commands/mcp-lock.md`

**Goal:** Rule 19 LANE LOCK as a tool. Agents claim/release the desktop via single command, refuses claim if held.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/mcp-lock.md <<'EOF'
You are managing the Rule 19 MCP LANE LOCK for Tankoban 2.

**Arguments:**
- `<action>` — required, one of: `claim` / `release` / `peek`
- `<reason>` — required for `claim`, e.g. `Agent 4, TANKORENT Phase 11 smoke`

**State location:** the lock is tracked as plain text lines in `agents/chat.md`. There is no separate lock file. This avoids state drift and lets every agent see the lock by reading the chat tail.

**Procedure:**

1. **Read recent chat.md tail** (last 200 lines):
   ```
   tail -200 agents/chat.md
   ```

2. **Find the most recent MCP LOCK / MCP LOCK RELEASED pair.** Use ASCII protocol anchors per Rule 16:
   ```
   ^MCP LOCK - \[(?<holder>[^\]]+)\]:
   ^MCP LOCK RELEASED - \[(?<holder>[^\]]+)\]:
   ```
   If the most recent LOCK has no matching RELEASE after it, the lock is HELD.

3. **For `peek`:** report the current lock state. Format:
   - HELD: `MCP LOCK currently HELD by <holder> since <timestamp>` (with reason if available)
   - FREE: `MCP LOCK currently FREE — no active claim`

4. **For `claim`:**
   - If HELD: refuse with `MCP LOCK CLAIM REFUSED — already held by <holder>`. Exit without modifying chat.md.
   - If FREE: emit the claim line to stdout with current timestamp:
     ```
     MCP LOCK - [<reason>]: <current ISO timestamp>
     ```
   - Do NOT auto-append to chat.md — the agent decides when to post (typically in their own RTC block).

5. **For `release`:**
   - If FREE (no active claim): warn `MCP LOCK already FREE — emitting RELEASED line for audit anyway`. Continue.
   - If HELD: emit the release line to stdout:
     ```
     MCP LOCK RELEASED - [<holder>]: <current ISO timestamp>
     ```
   - The `holder` value should match the most recent active LOCK line exactly.

**Examples:**

For `/mcp-lock claim "Agent 4, TANKORENT Phase 11 smoke"`:
- If free, emit:
  ```
  MCP LOCK - [Agent 4, TANKORENT Phase 11 smoke]: 2026-05-19T19:30:00Z
  ```
- If held by Agent 1, emit:
  ```
  MCP LOCK CLAIM REFUSED — already held by [Agent 1, COMICS_SOURCES_SIDEBAR smoke] since 2026-05-19T19:25:00Z
  ```

For `/mcp-lock release`:
```
MCP LOCK RELEASED - [Agent 4, TANKORENT Phase 11 smoke]: 2026-05-19T19:45:00Z
```

For `/mcp-lock peek`:
```
MCP LOCK currently HELD by [Agent 4, TANKORENT Phase 11 smoke] since 2026-05-19T19:30:00Z
```

**Quality gates:**
- Emitted lines use ASCII ` - ` per Rule 16 (NOT em-dash)
- Holder tag preserved exactly across claim → release
- Timestamps in ISO 8601 UTC
- Refuse-claim path never modifies chat.md state suggestions
EOF
```

- [ ] **Step 2: Smoke test — verify peek mode against current chat.md**

Run:
```
/mcp-lock peek
```

Expected: report of current lock state. The chat.md tail currently has Agent 4 MCP LOCK from 2026-05-19 ~12:25am IST (per chat.md tail). Expected output: `MCP LOCK currently HELD by [Agent 4, TANKORENT_CINEMETA_PACK_MAPPING Task 11 ...]`.

- [ ] **Step 3: Smoke test — verify claim refusal**

Run:
```
/mcp-lock claim "Agent 0, smoke test"
```

Expected: refusal message naming the current holder (Agent 4 per peek above). No chat.md modification suggested.

- [ ] **Step 4: Commit Task B.2**

```bash
git add .claude/commands/mcp-lock.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.2 — /mcp-lock skill]: Rule 19 LANE LOCK as a tool. Claim/release/peek modes; refuses claim if held; ASCII protocol anchors per Rule 16. State tracked in chat.md (no separate lock file). Per spec Section 1 Bucket A + Bucket B.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.3 — Author `/smoke-package` skill

**Files:**
- Create: `.claude/commands/smoke-package.md`

**Goal:** Auto-create evidence directory, name PNGs per convention, capture sidecar + telemetry logs, write stub evidence-md.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/smoke-package.md <<'EOF'
You are scaffolding a smoke evidence bundle for Tankoban 2.

**Arguments:**
- `<finding-name>` — required, UPPERCASE_SNAKE_CASE identifier (e.g. `THEATRE_PACK_DISPATCH_RACE`)

**Procedure:**

1. **Compute timestamp:** `HHMMSS` in local time, e.g. `194523`.

2. **Create the evidence directory:**
   ```
   agents/audits/smoke_evidence/<FINDING>_<HHMMSS>/
   ```
   Use `mkdir -p`. If directory already exists, append a numeric suffix `_2`, `_3`, etc.

3. **Capture context logs (if Tankoban is running):**
   - `out/sidecar_debug_live.log` (last 500 lines) → `<dir>/sidecar.log`
   - `out/stream_telemetry.log` (last 200 lines) → `<dir>/telemetry.log`
   - `out/events.jsonl` (last 200 lines, if present) → `<dir>/events.jsonl`
   - Current Tankoban window screenshot via pywinauto-mcp → `<dir>/initial_state.png` (use ScreenCapture or automation_visual)
   Skip any source that doesn't exist; warn but don't fail.

4. **Write the stub evidence-md:**
   ```
   <dir>/evidence_<FINDING>_<HHMMSS>.md
   ```
   With this skeleton:
   ```markdown
   # Evidence: <FINDING>

   **Captured:** <ISO timestamp>
   **Agent:** <auto-detect from session>
   **Smoke scope:** <one-line description of what was being smoked>

   ## Smoke description

   <fill in: what was the agent trying to verify or reproduce?>

   ## Pre-smoke state

   - Tankoban running: <yes/no>
   - Branch: $(git rev-parse --abbrev-ref HEAD)
   - HEAD: $(git rev-parse --short HEAD)
   - Working tree: $(git status --short | wc -l) dirty files

   ## Observations

   <fill in: what happened during the smoke? include MCP screenshot pointers + log timestamps>

   ## Verdict

   - [ ] PASS / FAIL / INCONCLUSIVE
   - Notes:

   ## Evidence files in this directory

   - sidecar.log (if captured)
   - telemetry.log (if captured)
   - events.jsonl (if captured)
   - initial_state.png (if captured)
   - <numbered PNGs from smoke iteration>
   ```

5. **Print the directory path to stdout** so the agent can reference it in subsequent screenshots:
   ```
   Smoke evidence bundle ready: agents/audits/smoke_evidence/<FINDING>_<HHMMSS>/
   Append screenshots as <FINDING>_<HHMMSS>_<NN>.png to this directory.
   ```

**Quality gates:**
- Directory name uses UPPERCASE_FINDING per convention
- All captured logs exist OR have a per-source warning explaining absence
- Stub evidence-md has populated pre-smoke state from real git/system queries
- No silent failures: every skipped step prints a one-line reason

**Example:**

For `/smoke-package THEATRE_PACK_DISPATCH_RACE`:
```
Smoke evidence bundle ready: agents/audits/smoke_evidence/THEATRE_PACK_DISPATCH_RACE_194523/
Captured: sidecar.log (483 lines), telemetry.log (180 lines), initial_state.png
Append screenshots as THEATRE_PACK_DISPATCH_RACE_194523_NN.png to this directory.
```
EOF
```

- [ ] **Step 2: Smoke test the skill**

Run:
```
/smoke-package SKILL_TEST_BUNDLE
```

Expected: a new directory `agents/audits/smoke_evidence/SKILL_TEST_BUNDLE_<HHMMSS>/` exists with at least the stub evidence-md. Logs captured if Tankoban running, warnings if not.

- [ ] **Step 3: Verify the smoke-evidence-rename hook accepts the names**

Run:
```bash
bash .claude/scripts/smoke-evidence-rename.sh
```

Expected: silent (no warnings) since `/smoke-package` produces compliant names.

- [ ] **Step 4: Clean up test bundle (manual)**

Run:
```bash
rm -rf agents/audits/smoke_evidence/SKILL_TEST_BUNDLE_*
```

- [ ] **Step 5: Commit Task B.3**

```bash
git add .claude/commands/smoke-package.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.3 — /smoke-package skill]: scaffold smoke evidence bundle with auto-naming + log capture + stub evidence-md. Compatible with smoke-evidence-rename hook (Phase A.2). Per spec Section 1 Bucket A.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.4 — Sweeper race-fix in commit-sweeper.md

**Files:**
- Modify: `.claude/agents/commit-sweeper.md` (currently 115 lines)

**Goal:** Re-snapshot chat.md immediately before staging the marker commit; diff against parse-time snapshot to detect RTCs added during the sweep (the BulkPackVerifier race-orphan class).

- [ ] **Step 1: Add race-fix step before Step 4 (sweep marker commit)**

Edit `.claude/agents/commit-sweeper.md`. Insert this new Step 3.5 between Step 3 and Step 4:

```markdown
### Step 3.5: Race-condition detection (snapshot chat.md before marker)

Before staging the marker commit, re-read agents/chat.md and diff against the parse-time blob:

```
git diff "$SWEEP_BLOB" -- agents/chat.md > /tmp/chat-final-diff-$$.txt
```

Extract any `READY TO COMMIT` lines added to chat.md SINCE the initial parse (these would be lines that arrived mid-sweep from a concurrent agent post). Compare against the list of lines processed in Step 3.

If any `READY TO COMMIT` line exists in the final diff but was NOT processed in Step 3:
1. Log a `race_orphan_count` increment per missed line
2. Add the missed lines to the final report under "Race-orphan RTCs detected (not committed; manual lift required)"
3. Do NOT auto-retry — the marker still lands, but Agent 0 reviews the orphan list and lifts them manually under correct attribution post-sweep
4. The orphan RTC's `files:` paths are NOT staged or committed by this sweep

Race orphans are normal under concurrent multi-agent workloads. The warn-and-halt pattern lets Agent 0 catch them within the same wake; the alternative (auto-retry with a second parse pass) was deferred to a future iteration per spec §Technical decisions #2.
```

- [ ] **Step 2: Extend the final report to include race-orphan count**

In Step 5 (Final report), append this line to the structured summary template:

```
- Race-orphan RTCs (added mid-sweep, not committed): <count> [<tag list if >0>]
```

Update the prose at the bottom of Step 5 to reference race orphans:

> The `Race-orphan RTCs` line is the post-Codex-audit fix landed in SKILL_AUGMENTATION_ARC Phase B.4 (2026-05-19). When this number is >0, Agent 0 reads the listed tags, checks the working-tree state of their `files:` paths, and commits them manually under correct attribution (typically `[Agent N, ...]` where N is the originating agent named in the RTC's tag, not Agent 0).

- [ ] **Step 3: Smoke test the updated sweeper (dry-run mode against current state)**

Run the sweeper sub-agent with `--dry-run`. Verify the final report template includes the new race-orphan line.

- [ ] **Step 4: Commit Task B.4**

```bash
git add .claude/agents/commit-sweeper.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.4 — sweeper race-fix]: commit-sweeper.md now re-snapshots chat.md before marker commit and flags race-orphan RTCs that arrived mid-sweep. Warn-and-report pattern (not auto-retry) per spec Technical decision #2. Prevents BulkPackVerifier-class orphans from going invisible to the next sweep.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.5 — Author `/memory-trim` skill

**Files:**
- Create: `.claude/commands/memory-trim.md`

**Goal:** Propose MEMORY.md archive candidates by topic age + last-cited frequency. Propose-with-confirmation (per Hemanth §5 Q1 default).

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/memory-trim.md <<'EOF'
You are proposing MEMORY.md archive candidates for Tankoban 2.

**Arguments:** none (always interactive)

**Memory dir location:** `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/`

**Procedure:**

1. **Read MEMORY.md** (the index file in the memory dir).

2. **For each pointer entry in MEMORY.md, gather metrics:**
   - Pointer entry shape: `- [<slug>.md](<slug>.md) — <one-line hook>`
   - Read the target file's frontmatter to get `name` slug + `metadata.type`
   - Compute LAST-CITED frequency:
     ```
     grep -c <slug> agents/chat.md
     grep -rc <slug> agents/chat_archive/
     ```
     Sum the counts. Newer chat.md counts more (weight 2x) vs archived (weight 1x).
   - Compute AGE:
     ```
     git log -1 --format=%cs <memory-dir>/<slug>.md
     ```
     Days since last edit.

3. **Score each entry:**
   - Score = (age in days × 1) - (weighted citation count × 2)
   - Higher score = better archive candidate

4. **Sort by score descending** and propose the top 10 archive candidates. Present as a numbered list with:
   - Slug
   - Last-edit date
   - Citation count (chat + archive split)
   - Computed score
   - 1-line memory description from MEMORY.md

5. **Wait for user confirmation.** Format:
   ```
   Top archive candidates (highest score first):

   1. [12] feedback_foo.md — last edit 2026-04-15 (35d ago) — cited 1x chat / 0x archive — "Foo behavior pattern"
   2. [10] project_bar.md — last edit 2026-04-20 (30d ago) — cited 0x chat / 2x archive — "Bar shipped"
   ...

   Which to archive? Reply with comma-separated numbers (e.g. "1,3,5") or "none" to abort.
   ```

6. **On confirmation:**
   - `git mv` selected memory files to `memory/_archive/<original-name>.md`
   - Update MEMORY.md index: remove the moved entries
   - Append entry to `memory/_archive/INDEX.md`:
     ```
     - [<slug>.md](<slug>.md) — archived <YYYY-MM-DD>, last-active <last-cited-date>, score <score>
     ```
   - Print summary of moves

7. **Do NOT auto-archive without confirmation.** Per spec §5 Q1 default: propose-with-confirmation.

**Quality gates:**
- Never moves a file the user didn't confirm
- INDEX.md entry preserves the original 1-line description from MEMORY.md
- MEMORY.md index entries are removed cleanly (no leftover blank lines or section breaks broken)
- Recent (≤14 days since edit) entries are NEVER proposed regardless of citation count

**Examples:**

For a 28-entry MEMORY.md where 3 entries are aged + uncited:
```
Top archive candidates:
1. [22] feedback_old_xy_workaround.md — last edit 2026-04-12 (37d ago) — cited 0x chat / 0x archive
2. [18] project_closed_arc.md — last edit 2026-04-18 (31d ago) — cited 1x chat / 0x archive
3. [15] reference_unused_tool.md — last edit 2026-04-22 (27d ago) — cited 0x chat / 1x archive

Which to archive? Reply with comma-separated numbers (e.g. "1,3") or "none" to abort.
```

User responds `1,3` → script moves the two files, updates MEMORY.md + INDEX.md, prints confirmation.
EOF
```

- [ ] **Step 2: Smoke test the skill**

Run (in a test/dry-run mode):
```
/memory-trim
```

Expected: skill reads MEMORY.md, computes scores, proposes top 10 archive candidates. Does NOT actually move anything until user confirms.

- [ ] **Step 3: Verify a confirmation moves files correctly**

Pick one safe candidate (e.g. a closed-arc memory referenced 0x in chat). Confirm. Verify:
- File moved to `_archive/`
- MEMORY.md entry removed
- `_archive/INDEX.md` has new entry
- Git status shows `R` (rename) for the moved file

- [ ] **Step 4: Commit Task B.5**

```bash
git add .claude/commands/memory-trim.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.5 — /memory-trim skill]: propose-with-confirmation MEMORY.md archive helper. Scores by age + inverse citation count; user confirms which to move. Updates MEMORY.md + _archive/INDEX.md. Per Hemanth §5 Q1 default (propose-with-confirmation, not auto-archive).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.6 — Author `/memory-write` skill

**Files:**
- Create: `.claude/commands/memory-write.md`

**Goal:** Scaffold a new memory file with correct frontmatter + auto-add MEMORY.md index entry.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/memory-write.md <<'EOF'
You are scaffolding a new memory file for Tankoban 2.

**Arguments:**
- `<type>` — required, one of: `user` / `feedback` / `project` / `reference`
- `<name>` — required, kebab-case slug (e.g. `agent4-stream-ipc-pattern`)

**Memory dir location:** `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/`

**Procedure:**

1. **Validate type:** must be one of the 4 types per CLAUDE.md auto-memory section. If invalid, abort with the valid list.

2. **Construct filename:**
   - Pattern: `<type>_<name>.md`
   - Example: `feedback_<name>.md`, `project_<name>.md`, `reference_<name>.md`, `user_<name>.md`
   - Absolute path: `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/<filename>`

3. **Check for existing file:** if a file with this name already exists, abort with `File already exists — use Edit to update, not /memory-write to create.`

4. **Scaffold the file content with correct frontmatter:**

   For type=`user`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: user
   ---

   <Memory body — what does Claude need to know about the user? See CLAUDE.md auto-memory section for user-memory guidance.>
   ```

   For type=`feedback`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: feedback
   ---

   <Rule or principle — lead with the rule itself.>

   **Why:** <reason — often a past incident, strong preference, or empirical pattern>

   **How to apply:** <when/where this guidance kicks in; which agents/domains affected>

   Related: [[related-memory-slug]] (link liberally; broken links are OK markers).
   ```

   For type=`project`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: project
   ---

   <Fact or decision — lead with what changed/happened.>

   **Why:** <motivation — often a deadline, stakeholder ask, or empirical finding>

   **How to apply:** <how this should shape future agent decisions>

   Related: [[related-memory-slug]]
   ```

   For type=`reference`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: reference
   ---

   <External resource pointer — what's at the URL/path, how to use it.>

   Use when: <conditions that should trigger this reference>
   ```

5. **Write the file** to the memory dir.

6. **Add MEMORY.md index entry.** Read MEMORY.md, find the right section (Workflow / Player domain / Stream domain / Sources / MCP / etc), and insert:
   ```
   - [<filename>.md](<filename>.md) — <ONE-LINE summary>
   ```
   Section placement heuristic:
   - `feedback_*` → typically `Workflow / governance` section (or domain-specific if obvious)
   - `project_*` → domain-specific section based on the body content
   - `reference_*` → typically near top with other reference entries
   - `user_*` → near top with other user entries

7. **Print confirmation** with the file path + which MEMORY.md section the index entry landed in.

**Quality gates:**
- Frontmatter is valid YAML (no tabs, name/description/metadata fields present)
- Slug matches `<type>_<name>.md` exactly
- MEMORY.md index entry stays under ~150 chars total
- File body has the **Why:** + **How to apply:** lines for feedback/project types
- Related: line included (even if empty) to remind future-self to link

**Examples:**

For `/memory-write feedback "stream-ipc-batch-pattern"`:
- Creates `feedback_stream-ipc-batch-pattern.md` with the feedback template
- Adds MEMORY.md entry under `## Stream domain` (or `## Workflow / governance` if cross-cutting)
- Prints `Created: ~/.claude/projects/.../memory/feedback_stream-ipc-batch-pattern.md`
EOF
```

- [ ] **Step 2: Smoke test scaffold for a feedback-type memory**

Run:
```
/memory-write feedback "skill-write-test"
```

Expected:
- New file `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_skill-write-test.md` exists with feedback frontmatter
- MEMORY.md has a new entry for the file under an appropriate section
- Slug appears in the file's frontmatter `name:` field

- [ ] **Step 3: Smoke test validation — invalid type**

Run:
```
/memory-write invalid_type "wont-work"
```

Expected: abort with `Invalid type 'invalid_type'. Valid: user, feedback, project, reference.`

- [ ] **Step 4: Smoke test validation — existing file**

Run `/memory-write feedback "skill-write-test"` again. Expected: abort with `File already exists — use Edit to update, not /memory-write to create.`

- [ ] **Step 5: Clean up test file**

```bash
rm ~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_skill-write-test.md
# Remove the index entry from MEMORY.md (manual edit)
```

- [ ] **Step 6: Commit Task B.6**

```bash
git add .claude/commands/memory-write.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.6 — /memory-write skill]: scaffold new memory file with type-specific frontmatter + auto-add MEMORY.md index entry. Type validation, existing-file check, slug convention enforcement. Per spec Section 1 Bucket C.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.7 — Author `/fix-todo-new` skill

**Files:**
- Create: `.claude/commands/fix-todo-new.md`

**Goal:** Scaffold the ratified 14-section fix-TODO template per `feedback_fix_todo_authoring_shape.md`.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/fix-todo-new.md <<'EOF'
You are scaffolding a new fix-TODO for Tankoban 2.

**Arguments:**
- `<name>` — required, UPPERCASE_SNAKE_CASE identifier (e.g. `STREAM_PAUSE_RACE_FIX`)

**Output location:** `<NAME>_FIX_TODO.md` at repo root (NOT in `docs/superpowers/` — fix-TODOs live at root per established pattern; see existing TANKOLIBRARY_FIX_TODO.md, COMIC_READER_FIX_TODO.md, etc.)

**14-section template** (per `feedback_fix_todo_authoring_shape.md`):

```markdown
# <NAME> FIX TODO

**Author:** <auto-detect from session>
**Date:** <YYYY-MM-DD>
**Owner:** <leave TBD until §6>

---

## §1 — Strategic intent

<Why does this TODO exist? What's the load-bearing problem it solves? Include Hemanth-language verbatim quote if available.>

---

## §2 — Phase breakdown

| Phase | Goal | Owner | Wakes |
|-------|------|-------|-------|
| P1 | <one-line> | <agent> | <est> |
| P2 | <one-line> | <agent> | <est> |
| ... | | | |

---

## §3 — Deliverables per phase

### P1 deliverables
- <file or behavior change>
- <smoke matrix entry>

### P2 deliverables
- ...

---

## §4 — Acceptance criteria

Per phase: what's the gate that says "this phase is closed"?

- P1: <smoke verdict / build verification / Hemanth approval>
- P2: ...

---

## §5 — Hemanth ratification questions

(Only Hemanth-product-strategic decisions per Rule 14. Agent-0 technical decisions go to §10 Anti-patterns / §13 Standing contracts.)

1. **<Question 1 — product/strategic>** — Recommended answer + reasoning.
2. **<Question 2>** — Recommended + reasoning.
...

---

## §6 — Ownership

- Primary owner: <agent>
- Cross-agent contributors: <list>
- Codex Trigger D scope: <which sub-phases, if any>

---

## §7 — Dependencies

- Blocked by: <other TODO / arc>
- Blocks: <downstream work>
- Memory references: <list of relevant memory slugs>

---

## §8 — Risks

1. <Risk + mitigation>
2. <Risk + mitigation>
...

---

## §9 — Wake budget

Estimate per phase + total. Don't promise specific dates.

---

## §10 — Anti-patterns to avoid

1. <DO NOT pattern>
2. <DO NOT pattern>
...

---

## §11 — Evidence pointers

- Audit files: <list>
- Memory files: <list>
- Prior TODOs: <list>

---

## §12 — Close criteria

What does it look like when this TODO closes? Final-phase deliverables + Hemanth verdict.

---

## §13 — Standing contracts

Contracts that survive the TODO's close (architectural patterns, naming conventions, etc).

---

## §14 — Archive trigger

When this TODO closes, move to `agents/_archive/todos/<NAME>_FIX_TODO.md` and update CLAUDE.md "Active Fix TODOs" table to mark it CLOSED.
```

**Procedure:**

1. Validate name is UPPERCASE_SNAKE_CASE.
2. Construct filename: `<NAME>_FIX_TODO.md`.
3. Check for existing file: abort with `<NAME>_FIX_TODO.md already exists` if present.
4. Write the file with the 14-section template (all sections present but bodies marked `<...>` for the author to fill in).
5. Print confirmation: `Scaffolded: <NAME>_FIX_TODO.md (14 sections, ready for filling).`

**Quality gates:**
- File is created at repo root, not in subdirectories
- All 14 sections present in order
- Date field reflects current date
- All `<...>` placeholders are clearly marked
- Validates name against pattern `^[A-Z][A-Z0-9_]+$`

EOF
```

- [ ] **Step 2: Smoke test the scaffold**

Run:
```
/fix-todo-new SKILL_TEST_TODO
```

Expected:
- New file `SKILL_TEST_TODO_FIX_TODO.md` exists at repo root
- File has 14 sections, all with `<...>` placeholders

- [ ] **Step 3: Verify validation**

Run:
```
/fix-todo-new "lowercase_name"
```

Expected: abort with name-validation error.

- [ ] **Step 4: Clean up test file**

```bash
rm SKILL_TEST_TODO_FIX_TODO.md
```

- [ ] **Step 5: Commit Task B.7**

```bash
git add .claude/commands/fix-todo-new.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.7 — /fix-todo-new skill]: scaffold ratified 14-section fix-TODO template (per feedback_fix_todo_authoring_shape.md). Name validation, existing-file check, repo-root placement. Per spec Section 1 Bucket D.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.8 — Author `/codex-trigger-d` skill

**Files:**
- Create: `.claude/commands/codex-trigger-d.md`

**Goal:** Package a Codex Trigger D handoff per the established pattern (one-shot spec → commission with verification block).

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/codex-trigger-d.md <<'EOF'
You are packaging a Codex Trigger D handoff for Tankoban 2.

**Arguments:**
- `<spec-file>` — required, path to a spec markdown file (relative to repo root)
- `<attribution-agent>` — required, the agent commissioning Codex (e.g. `Agent 4`)
- `<scope-summary>` — required, one-line summary for the RTC tag

**Procedure:**

1. **Read the spec file.** Validate it exists; abort if missing.

2. **Construct the Codex prompt block** following the canonical Trigger D pattern (see `project_codex_substrate_live.md` memory + the v1.1/v1.2 ships at chat.md:~4575 for examples):

```
From <attribution-agent> — Trigger D (scoped src/ implementation).

Target spec: <spec-file>

Context: <auto-extract first paragraph from the spec's Strategic Intent or top-of-file>

YOUR TASK:
1. Read the full spec.
2. Implement the changes per the spec body in the listed files.
3. Match the spec's §Files list exactly — no edits outside the listed paths.
4. Build_check after each major change; commit only when build_check.bat = BUILD OK.

CONSTRAINTS — DO NOT:
- Expand scope beyond the spec
- Touch memory files or CLAUDE.md (unless spec explicitly lists them)
- Use --no-verify or skip build verification
- Introduce non-ASCII characters in protocol-anchored lines (RTC, MCP LOCK, ASCII Rule 16)

CONSTRAINTS — DO:
- Preserve all existing code outside your diff
- Keep additive changes (schema versioning rule: additive within v1.x = non-breaking)
- Match the existing code style (C++ MSVC2022 + Qt6, indent + brace patterns from neighboring files)
- ASCII-only emissions in any chat.md or protocol-parsed output

MEMORY POINTERS (already loaded for Codex sessions):
- project_dev_control_bridge.md (if bridge work)
- feedback_fix_todo_authoring_shape.md (if fix-TODO work)
- project_codex_substrate_live.md (Trigger A/B/C/D pattern)
- <any spec-specific memory pointers>

OUTPUT EXPECTATIONS:
1. Files modified per spec §Files list
2. build_check.bat = BUILD OK before commit
3. scoped git diff --check clean (no whitespace warnings)
4. ASCII-clean check on any modified Markdown/text files
5. RTC posted to agents/chat.md with attribution "[<attribution-agent> (Codex), <scope-summary>]"

VERIFICATION CHECKLIST FOR AGENT POST-CODEX:
- Build green (build_check.bat = BUILD OK)
- Spec §Files list matches actual diff scope (no scope creep)
- No memory or CLAUDE.md touched (unless spec listed)
- ASCII protocol anchors in any emitted chat.md lines
- RTC posted with correct attribution

When done, post an RTC in agents/chat.md with attribution "[<attribution-agent> (Codex), <scope-summary>]" plus your standard Codex Trigger D verification block.
```

3. **Print the prompt block to stdout** so the user can copy-paste into a Codex tab (or fire via mcp__codex__codex tool).

**Quality gates:**
- Spec file path is validated against working tree
- Attribution agent and scope summary are non-empty
- Memory pointers are extracted from the spec's frontmatter or `Related:` lines (best-effort)
- Output is self-contained (Codex doesn't need anything beyond the prompt to start work)

**Examples:**

For `/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md "Agent 2" "v1.3 books-side bridge layer"`:
prints a complete Codex prompt block referencing the v1.3 spec, with Agent 2 attribution and `v1.3 books-side bridge layer` as the RTC scope.

EOF
```

- [ ] **Step 2: Smoke test the skill against a real spec**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-brotherhood-skill-augmentation-design.md "Agent 0" "skill augmentation arc smoke test"
```

Expected: a Codex prompt block is emitted to stdout that includes attribution, spec reference, constraints, memory pointers, and verification block.

- [ ] **Step 3: Commit Task B.8**

```bash
git add .claude/commands/codex-trigger-d.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.8 — /codex-trigger-d skill]: package Codex Trigger D handoff per project_codex_substrate_live.md pattern. Spec validation, memory pointer extraction, attribution + verification block scaffolding. Per spec Section 1 Bucket D.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.9 — Author `/audit-skeleton` skill

**Files:**
- Create: `.claude/commands/audit-skeleton.md`

**Goal:** Agent 7-style audit shape skeleton (numbered findings + severity + repro + §5 ratification). NO markdown tables per Hemanth preference.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/audit-skeleton.md <<'EOF'
You are scaffolding an Agent 7-style audit document.

**Arguments:**
- `<topic>` — required, kebab-case identifier (e.g. `stream-pause-race-investigation`)

**Output location:** `agents/audits/<topic>_<YYYY-MM-DD>.md`

**Procedure:**

1. Construct filename with today's date suffix.
2. Check for existing file (numeric suffix if collides: `_2`, `_3`, etc).
3. Scaffold with this template:

```markdown
# Audit: <topic>

**Author:** <auto-detect agent>
**Date:** <YYYY-MM-DD>
**Commissioned by:** <agent or "spontaneous">
**Scope:** <one-line description>

---

## Executive summary

<2-3 sentences: what was audited, what was found, what's recommended. Lead with the most important finding.>

---

## Findings

(Numbered, ranked by severity. Use **observation** vs **hypothesis** separation — observations are verifiable facts; hypotheses are guesses about cause.)

### Finding 1: <title> [SEVERITY: CRITICAL / HIGH / MEDIUM / LOW]

**Observation:** <verifiable fact — what was seen, where, with timestamps/file:line refs>

**Hypothesis:** <best guess about cause — explicitly flagged as guess if not proven>

**Repro:** <minimal steps to reproduce>

**Recommendation:** <concrete action>

---

### Finding 2: ... [SEVERITY: ...]

(Same shape.)

---

## §5 — Ratification questions

(For audit commissioner — typically Hemanth + originating agent.)

1. **<Question 1>** — Recommended answer + reasoning.
...

---

## Anti-patterns identified

1. <What NOT to do, based on findings>
...

---

## Related work

- Prior audits: <list>
- Memory references: <list>
- Active TODOs: <list>

---

## Audit signature

Generated <YYYY-MM-DD>. <N findings, <K critical / J high / M medium / L low>.
```

4. Print confirmation: `Scaffolded: agents/audits/<topic>_<date>.md (audit template ready for filling).`

**Quality gates:**
- NO markdown tables in the scaffold (per `feedback_no_tables_simple_lists.md`)
- Observation vs hypothesis separation enforced via labeled sections
- Severity labels are uppercase enum (CRITICAL/HIGH/MEDIUM/LOW)
- §5 section reserved for Hemanth-product-strategic decisions only
- Repro steps required per finding (not optional)

**Examples:**

For `/audit-skeleton stream-pause-race-investigation`:
- Creates `agents/audits/stream-pause-race-investigation_2026-05-19.md`
- File has the 5-section template with placeholders
EOF
```

- [ ] **Step 2: Smoke test the scaffold**

Run:
```
/audit-skeleton skill-test-audit
```

Expected: new file `agents/audits/skill-test-audit_2026-05-19.md` exists with the 5-section template. NO tables in the file.

- [ ] **Step 3: Clean up test file**

```bash
rm agents/audits/skill-test-audit_*.md
```

- [ ] **Step 4: Commit Task B.9**

```bash
git add .claude/commands/audit-skeleton.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.9 — /audit-skeleton skill]: Agent 7-style audit shape scaffolding. Numbered findings + severity + observation/hypothesis split + repro + §5 ratification. NO tables per feedback_no_tables_simple_lists.md. Per spec Section 1 Bucket D + Codex audit finding (audit-skeleton implementation correction).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.10 — Author `/handoff-brief` skill

**Files:**
- Create: `.claude/commands/handoff-brief.md`

**Goal:** Mid-wake context handoff between agents (when work passes Agent A → Agent B during a single wake). Complement to `/session-recap` which is end-of-wake.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/handoff-brief.md <<'EOF'
You are generating a mid-wake handoff brief for Tankoban 2.

**Arguments:**
- `<target-agent>` — required, identifier (e.g. `Agent 4`, `Agent 7`)
- `<scope>` — required, one-line description of what's being handed off

**Procedure:**

1. **Capture current session state:**
   - Active TODOs being worked: scan working-tree for `*_FIX_TODO.md` files modified this session OR uncommitted RTCs naming TODOs
   - Files currently dirty: `git status --short` filtered to relevant scope
   - Pending RTCs: scan `agents/chat.md` tail since last sweep marker
   - Last 3 chat.md posts: tail -100 + extract last 3 `## Agent N - ...` blocks
   - Current MCP lock state: invoke `/mcp-lock peek` logic OR scan chat.md tail
   - Active arc(s): scan recent chat.md for arc identifiers (e.g. `THEATRE_DOWNLOAD_OVERHAUL`, `MANGAUPDATES_FALLBACK`)
   - Recent commits in this session: `git log --since="$(date -d 'today 00:00')" --format='%h %s'`

2. **Construct the handoff brief block:**

```markdown
# Handoff brief → <target-agent>: <scope>

**From:** <originating agent (auto-detect)>
**Wake date:** <today>
**Reason for handoff:** <one-line context>

## Files dirty in working tree
<git status --short output, scoped>

## Active TODOs being worked
- <TODO name + current phase cursor>

## Uncommitted RTCs
<list of recent RTCs in chat.md tail>

## MCP lock state
<HELD by X / FREE>

## Active arcs
<list>

## Recent commits this wake (last 10)
<git log output>

## What I need <target-agent> to do
<fill in: specific ask>

## Relevant memory pointers
- <list>

## Specific files to read for context
- <list>
```

3. **Print the brief to stdout** as a pastable block. Target agent's next wake prompt should paste this brief alongside their own instructions.

**Quality gates:**
- Brief is self-contained (target agent doesn't need to ask for additional context)
- File lists are real (from git status, not fabricated)
- Memory pointers reference actual files in the memory dir
- "What I need <target-agent> to do" section is concrete (verb + object + verification gate)
- Single chat.md paste, not multiple back-and-forth

**Difference from /session-recap:**
- `/session-recap` = end-of-wake recap written by an agent to brief the NEXT instance of themselves (off-tree at `~/.claude/recaps/<agent-slug>/`)
- `/handoff-brief` = mid-wake handoff to a DIFFERENT agent within the same wake (inline output, not saved to disk)

**Examples:**

For `/handoff-brief "Agent 4" "TANKORENT_CINEMETA Task 12 smoke verification"`:
emits a brief covering current dirty files (Agent 0 sweep state), pending RTCs from Agent 4's prior tasks, MCP lock state (currently held by Agent 4), arc identifier `TANKORENT_CINEMETA_PACK_MAPPING`, and the specific Task 12 ask.
EOF
```

- [ ] **Step 2: Smoke test the brief**

Run:
```
/handoff-brief "Agent 4" "skill brief smoke test"
```

Expected: a self-contained brief block emitted to stdout with all 8 sections populated from real git/system state.

- [ ] **Step 3: Commit Task B.10**

```bash
git add .claude/commands/handoff-brief.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.10 — /handoff-brief skill]: mid-wake context handoff between agents. Complements /session-recap (end-of-wake). Captures dirty files, pending RTCs, MCP lock, active arcs, recent commits. Per spec Section 1 Bucket D.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task B.11 — Author `/summon-from-todo-phase` skill

**Files:**
- Create: `.claude/commands/summon-from-todo-phase.md`

**Goal:** Given a TODO + phase, draft the summon prompt for the responsible agent.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/summon-from-todo-phase.md <<'EOF'
You are drafting a summon prompt for a specific TODO phase.

**Arguments:**
- `<todo>` — required, TODO filename (e.g. `TANKOLIBRARY_FIX_TODO.md`) or short name (`TANKOLIBRARY`)
- `<phase>` — required, phase identifier (e.g. `P3.1`, `M2`, `Task 4`)

**Procedure:**

1. **Resolve TODO file:** if `<todo>` is a short name, look for `<NAME>_FIX_TODO.md` at repo root. If still not found, abort with `TODO file not found: <todo>`.

2. **Parse the TODO:** read the file, find the §2 Phase breakdown section, locate the row matching `<phase>`.

3. **Extract from the matched phase:**
   - Goal (one-line)
   - Owner (agent identifier)
   - Wakes estimate
   - Deliverables list from §3
   - Acceptance criteria from §4
   - Dependencies from §7

4. **Identify relevant memory pointers:** scan the TODO for §11 Evidence pointers + memory references in §1/§3/§13.

5. **Construct the summon prompt:**

```
# Summon: <Agent N> for <TODO> <phase>

**Phase goal:** <from §2>

**Deliverables for this phase:**
- <from §3>

**Acceptance criteria:**
- <from §4>

**Dependencies (these must be in place first):**
- <from §7>

**Memory pointers (read first):**
- <relevant slugs>

**Files you'll likely touch:**
- <inferred from deliverables — scan §3 for file paths>

**Verification gate:**
- <from §4 — what proves this phase is closed>

**Wake budget estimate:** <from §2 wakes>

**Reading order:**
1. <TODO> §1 + §<phase>
2. Memory pointers above
3. Any prior phase's RTCs (look in chat.md for `[<Agent>, <TODO> <prior-phase>]:` lines)

When you're done with this phase, post an RTC in chat.md per the contracts-v3 format. Cross-reference the TODO + phase in your message body.
```

6. **Print the summon to stdout** as a pastable block.

**Quality gates:**
- TODO file is real (validate existence)
- Phase identifier matches an actual row in §2
- Memory pointers are real file slugs
- File paths inferred from deliverables are validated against working tree
- "Reading order" section is concrete (numbered + linked)

**Examples:**

For `/summon-from-todo-phase TANKOLIBRARY M1`:
extracts the M1 row from TANKOLIBRARY_FIX_TODO.md (Track A Main scaffold + AA search-only), identifies Agent 4B as owner, lists src/core/book/ scaffolding files from §3, references `feedback_libtorrent_windows_backslash_separator.md` if relevant, etc.
EOF
```

- [ ] **Step 2: Smoke test against an existing TODO**

Run:
```
/summon-from-todo-phase TANKOLIBRARY M1
```

Expected: a summon prompt block referencing TANKOLIBRARY_FIX_TODO.md §2 row M1, with deliverables + acceptance + memory pointers populated from the TODO body.

- [ ] **Step 3: Verify error handling**

Run:
```
/summon-from-todo-phase NONEXISTENT_TODO P1
```

Expected: abort with `TODO file not found: NONEXISTENT_TODO`.

- [ ] **Step 4: Commit Task B.11**

```bash
git add .claude/commands/summon-from-todo-phase.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase B.11 — /summon-from-todo-phase skill]: draft summon prompt for a specific TODO phase. Extracts goal/deliverables/acceptance/dependencies/memory pointers from the TODO body. Per spec Section 1 Bucket D.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Phase C — Specialist + optional skills (1 wake)

### Task C.1 — Author `/tdd-scaffold` skill (Agent 4 / 4B shortlist)

**Files:**
- Create: `.claude/commands/tdd-scaffold.md`

**Goal:** Scaffold pure-logic primitive test file shape (GoogleTest + frozen-fixture pattern + MSVC link-dep-free target wiring).

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/tdd-scaffold.md <<'EOF'
You are scaffolding a pure-logic GoogleTest file for Tankoban 2.

**Arguments:**
- `<class-name>` — required, target class to test (e.g. `StreamPackParser`)
- `<domain>` — optional, subsystem dir under tests/ (default: `core/stream`)

**Output location:** `tests/<domain>/test_<class_name_snake>.cpp`

**Procedure:**

1. **Compute snake_case from class name:** `StreamPackParser` → `stream_pack_parser`.

2. **Verify the source class exists.** Grep `src/` for `class <ClassName>` to find the header. If not found, abort with `Class not found: <ClassName>`.

3. **Construct the test file path** and check for collision.

4. **Scaffold the test file:**

```cpp
// Test file for <ClassName> — pure-logic primitives, frozen fixtures.
// Per Codex #4 Stage 3a pattern. Linked into tankoban_tests target via CMakeLists.txt.

#include "gtest/gtest.h"
#include "<path/to/ClassName.h>"

namespace {

// ---- Frozen fixtures ----
// Per project pattern: load fixture data once, deterministic across runs.
// Example pattern from test_stream_pack_parser.cpp.

constexpr const char* kFixtureBerserkVolumeList = R"(
<!-- paste a stable fixture body here -->
)";

// ---- Test cases ----

TEST(<ClassName>Test, ConstructorIsDefaulted) {
    <ClassName> instance;
    SUCCEED();
}

TEST(<ClassName>Test, MinimalInputReturnsExpectedOutput) {
    // Arrange
    <ClassName> instance;
    // Act
    auto result = instance.<methodName>(<minimalInput>);
    // Assert
    EXPECT_EQ(result.<field>, <expected>);
}

// Add more cases below. Pattern:
//   TEST(<ClassName>Test, BehaviorDescription) {
//       Arrange + Act + Assert
//   }

}  // namespace
```

5. **Update CMakeLists.txt** — add the test file to the `tankoban_tests` target. Find the existing test sources block (search for `test_stream_pack_parser.cpp` or similar) and append your new file in alphabetical order.

6. **Print build + test commands** for the agent to verify:

```
Test file scaffolded at tests/<domain>/test_<class_name_snake>.cpp.

To build + run:
  cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
  cmake --build out --target tankoban_tests
  cd out && ctest --output-on-failure -R <ClassName>Test
```

**Quality gates:**
- Source class header is verified to exist
- Snake_case conversion is correct
- CMakeLists.txt entry preserves alphabetical order in the test sources block
- Initial test scaffolds 1 trivial-passing + 1 minimal-input test (red-green-refactor warmup)
- No tabs in C++ file (4-space indent matches project convention)
EOF
```

- [ ] **Step 2: Smoke test (against an existing class)**

Run:
```
/tdd-scaffold StreamLibrary core/stream
```

Expected: file `tests/core/stream/test_stream_library.cpp` would be created (if it doesn't already exist). CMakeLists.txt would have a new test source entry.

(If StreamLibrary already has tests, pick another class for the smoke.)

- [ ] **Step 3: Verify error handling — nonexistent class**

Run:
```
/tdd-scaffold NonexistentClass
```

Expected: abort with `Class not found: NonexistentClass`.

- [ ] **Step 4: Commit Task C.1**

```bash
git add .claude/commands/tdd-scaffold.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase C.1 — /tdd-scaffold skill]: scaffold pure-logic GoogleTest file + CMakeLists wiring per Codex #4 Stage 3a pattern. Per-agent shortlist: Agent 4 / 4B. Per spec Section 1 Specialist skills.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task C.2 — Author `/smoke-report` skill

**Files:**
- Create: `.claude/commands/smoke-report.md`

**Goal:** Standardize smoke output format (matrix + evidence + deferred + findings).

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/smoke-report.md <<'EOF'
You are formatting a smoke verification report for Tankoban 2.

**Procedure:** This skill takes free-form smoke notes + evidence pointers and reformats them into the canonical brotherhood smoke-report shape.

**Output structure:**

```
## Smoke matrix

(S1, S2, ... = test scenarios. Each row: name, what was verified, verdict GREEN/RED/INCONCLUSIVE/SKIPPED, evidence pointer.)

- **S1: <name>** — <what was verified> — **<VERDICT>** — evidence: <png/log/md pointer>
- **S2: <name>** — <what was verified> — **<VERDICT>** — evidence: <pointer>
- ...

## Discovered findings (NOT regressions; pre-existing or out-of-scope)

(Things found incidentally that weren't the smoke target.)

- **F1: <finding>** — <impact + recommended owner/next-step>
- **F2: ...** — ...

## Deferred / not smoked

(Smokes that couldn't run in this session — wall-clock, dependency, or scope.)

- **D1: <name>** — reason: <wall-clock / dep-blocked / out-of-scope> — defer-to: <next-wake/never>

## Verdict

(One-line summary of whether the ship/work is GREEN.)

- <Overall verdict>
- Cross-reference: <TODO + phase being verified>
- Hemanth sign-off: <pending / done>
```

**Quality gates:**
- Each smoke row has explicit verdict (no implicit OKs)
- Evidence pointer is a real file path or `<none>`
- Discovered findings are flagged distinctly from smoke matrix (so they don't read as regressions of the work being verified)
- Deferred items list a defer-to target

**Examples:**

For an Agent 4 Theatre source picker smoke verifying Bug A + B + C:
```
## Smoke matrix
- **S1: Theatre source picker dropdown** — verify dropdown opens on click — **GREEN** — evidence: triggerd5_03_source_picker_open.png
- **S2: Per-show reset** — verify selecting a different show resets picker — **GREEN** — evidence: triggerd5_05_per_show_reset.png
- **S3: Nyaa source visible** — verify Nyaa now appears in indexer list — **GREEN** — evidence: triggerd5_01_nyaa_visible.png

## Discovered findings
- **F1: Tankoban crash on rapid back navigation** — pre-existing race, NOT introduced by this ship. v1.x carry-forward.
- **F2: Library badge missing on movie tile** — out-of-scope for source picker work; queue for Agent 5.

## Deferred / not smoked
- **D1: Completion transition** — wall-clock (would take hours of download). Defer-to: future wake.

## Verdict
- Theatre source picker ship: GREEN. Hemanth verbal "looks good" + all 3 smokes green.
- Cross-reference: TANKORENT_CINEMETA Trigger D #5
- Hemanth sign-off: done
```
EOF
```

- [ ] **Step 2: Smoke test the skill**

Run:
```
/smoke-report
```

(Pass it a stub set of smoke notes via the conversation context.)

Expected: a structured report block following the 4-section template.

- [ ] **Step 3: Commit Task C.2**

```bash
git add .claude/commands/smoke-report.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase C.2 — /smoke-report skill]: standardize smoke output format (matrix + findings + deferred + verdict). Per spec Section 1 Specialist skills + chat.md historical pattern.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

### Task C.3 — Author `/hemanth-rewrite` skill

**Files:**
- Create: `.claude/commands/hemanth-rewrite.md`

**Goal:** Opt-in Hemanth-language translator on a paragraph. Never mandated.

- [ ] **Step 1: Author the skill markdown**

```bash
cat > .claude/commands/hemanth-rewrite.md <<'EOF'
You are rewriting a paragraph in Hemanth-language for Tankoban 2.

**Argument:** the paragraph to rewrite (passed inline or via stdin).

**Rules** (per `feedback_simple_language.md` + `feedback_no_tables_simple_lists.md`):

1. **Lead with the answer.** First sentence states the conclusion or recommendation, not the context.
2. **Short sentences.** Aim for 15-20 words max. Break long sentences.
3. **Translate jargon.** Replace coder terms with concrete actions/observations:
   - "Schema versioning" → "the rules for how we add new commands without breaking old ones"
   - "Race condition" → "two pieces of code stepping on each other"
   - "Dispatcher delegation" → "splitting the routing logic"
4. **No markdown tables.** Convert to numbered lists with simple descriptions.
5. **No more than 5 numbered list items.** If more, break into sections.
6. **Avoid colons before tool calls or commands.** Just say what runs.
7. **One thought per paragraph.** Don't chain ideas.
8. **Concrete > abstract.** "fixes the bug where the checkbox shows as `[`" beats "addresses widget rendering quirks".

**Procedure:**

1. Read the input paragraph.
2. Identify the buried lede (the actual answer/recommendation).
3. Rewrite leading with the lede, applying rules 2-8.
4. Print the rewritten version.
5. If the input was already Hemanth-friendly, say so and leave unchanged.

**Quality gates:**
- Rewritten version is shorter than the input (or matches if input was already tight)
- No coder jargon survives unless followed by plain-language explanation in parens
- Tables removed; lists capped at 5 items per section
- Lede is in the first sentence

**Examples:**

**Input:**
> The dispatcher delegation refactor will modularize the per-domain command routing inside MainWindow::handleDevCommand() such that subsequent v1.3+ bridge layer additions can be appended additively without expanding the if/else chain's lexical complexity beyond its current ~270 LOC footprint.

**Output:**
> Before adding more `tankoctl` commands, we split the big routing block in MainWindow into smaller pieces — one per domain. After the split, new commands plug into their domain block instead of growing the central chain. The big chain stays the same length forever; new commands land in the small per-domain pieces.

EOF
```

- [ ] **Step 2: Smoke test the rewrite**

Pass a deliberately-jargon-heavy paragraph (e.g. anything from the spec's implementation detail section). Verify the rewrite simplifies it.

- [ ] **Step 3: Commit Task C.3**

```bash
git add .claude/commands/hemanth-rewrite.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase C.3 — /hemanth-rewrite skill]: opt-in Hemanth-language translator. Lead-with-answer, short sentences, translated jargon, no tables. Never mandated; agents discover organically. Per spec Section 1 Optional skill.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Phase D — Dev-bridge expansion (2-3 wakes, runs in parallel with Phase B + C)

Codex Trigger D commissions per bridge layer. Agent 0 authors the commission spec doc + pre-allocates dispatcher namespace; Codex implements; Agent 0 verifies + commits.

### Sub-phase D.0 — Dispatcher delegation refactor (BEFORE D.1-D.6)

**Files:**
- Modify: `src/devtools/DevControlServer.h` (no behavior change; add doc comment about delegation)
- Modify: `src/ui/MainWindow.h` (add `DispatchDelegate` forward declarations)
- Modify: `src/ui/MainWindow.cpp` (refactor `handleDevCommand` to delegate per-domain dispatch)
- Modify: per-domain page files: `src/ui/pages/StreamPage.{h,cpp}`, `src/ui/pages/ComicsPage.{h,cpp}` (add `dispatchDevCommand(cmd, payload, reply)` method)

**Goal:** Refactor the ~270-line `MainWindow::handleDevCommand()` if/else chain into a delegation pattern. Existing v1.0-v1.2 commands keep their inline implementations (preserve diff scope); v1.3+ will land in their respective page classes via the delegate.

- [ ] **Step 1: Author the dispatcher delegation commission spec**

Write to `docs/superpowers/specs/2026-05-19-bridge-dispatcher-delegation-refactor-commission.md`:

```markdown
# Dispatcher Delegation Refactor — Codex Trigger D Commission

**Commissioned by:** Agent 0
**Date:** 2026-05-19
**Pre-requisite for:** SKILL_AUGMENTATION_ARC Phase D.1-D.6

## Goal

Refactor `MainWindow::handleDevCommand()` so per-domain bridge commands route through `<DomainPage>::dispatchDevCommand(cmd, payload, reply)` instead of inline if/else in MainWindow.cpp.

## Constraints

- Additive only — existing v1.0/v1.1/v1.2 commands stay where they are
- New v1.3+ commands MUST route through the delegate
- Schema string unchanged (still v1.2 after this refactor)
- No new commands added by this refactor
- Build_check.bat = BUILD OK after the change

## Files

- `src/devtools/DevControlServer.h` — doc comment update only (no API change)
- `src/ui/MainWindow.h` — add `forwardToDispatch(QObject* page, const QString& cmd, const QJsonObject& payload, QJsonObject& reply) -> bool` private method
- `src/ui/MainWindow.cpp` — refactor `handleDevCommand` to call `forwardToDispatch` for v1.3+ command prefixes (`books_`, `player_` deeper, `sources_`, `library_`, `ui_`, `system_`/`app_`/`settings_`/`jsonstore_`/`cache_`/`log_`/`network_`/`theme_`/`perf_`/`dev_`)
- `src/ui/pages/StreamPage.{h,cpp}` — add `dispatchDevCommand` stub returning `false` (unknown command) for now
- `src/ui/pages/ComicsPage.{h,cpp}` — same stub
- `src/ui/pages/BooksPage.{h,cpp}` — same stub
- `src/ui/pages/VideosPage.{h,cpp}` — same stub
- `src/ui/pages/TankorentPage.{h,cpp}` — same stub
- `src/ui/pages/TankoLibraryPage.{h,cpp}` — same stub

## Verification

- Existing v1.0-v1.2 commands still respond correctly to `tankoctl ping`, `tankoctl get-state`, `tankoctl comics-get-state`, etc.
- New unknown command via tankoctl returns `UNKNOWN_COMMAND` error (already the existing behavior)
- Schema string in ping reply unchanged: `tankoban.dev.v1.2`
- build_check.bat = BUILD OK

## RTC attribution

`[Agent 0 (Codex), bridge dispatcher delegation refactor (D.0 of SKILL_AUGMENTATION_ARC)]`
```

- [ ] **Step 2: Commission Codex via `/codex-trigger-d`**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-dispatcher-delegation-refactor-commission.md "Agent 0" "bridge dispatcher delegation refactor"
```

Copy the emitted prompt block. Paste into a Codex tab (or fire via `mcp__codex__codex` tool). Wait for Codex completion.

- [ ] **Step 3: Verify Codex's diff**

Run:
```bash
git status --short
git diff --stat
build_check.bat
```

Expected: ~5-7 files modified, build_check.bat = BUILD OK, schema string unchanged, no v1.x commands lost.

- [ ] **Step 4: Smoke test existing v1.0-v1.2 commands**

Run:
```bash
build_and_run.bat  # in dev-control mode
# wait for Tankoban to launch, then:
out\tankoctl.exe ping
out\tankoctl.exe get-state
out\tankoctl.exe comics-get-state
out\tankoctl.exe get-torrents
out\tankoctl.exe close-player
```

Expected: each command returns a reply (not UNKNOWN_COMMAND). Schema in ping is `tankoban.dev.v1.2`.

- [ ] **Step 5: Commit D.0 (sweeper handles via Codex RTC)**

The Codex commit + Agent 0 sweep handle the commit. Verify via `git log`.

### Sub-phase D.1 — v1.3 books-side commission (Agent 2 attribution)

**Files:**
- Create: `docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md` (commission spec)
- Modify (via Codex): `src/devtools/DevControlServer.h` (schema bump to v1.3), `src/ui/MainWindow.{h,cpp}`, `src/ui/pages/BooksPage.{h,cpp}`, `src/ui/pages/BookSeriesView.{h,cpp}`, `src/ui/readers/BookReader.{h,cpp}`, `src/ui/readers/BookBridge.{h,cpp}` (if reader JS state exposed there), `src/core/tts/EdgeTtsClient.*`, `src/core/tts/EdgeTtsWorker.*`, `tools/tankoctl.cpp`

**Goal:** v1.3 books-side bridge per spec §Detailed catalog Track B v1.3 + Codex-added 2026-05-19 additions (~21 commands total).

- [ ] **Step 1: Author the commission spec**

Write to `docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md`. Use the spec template from `project_codex_substrate_live.md`. Include:
- Spec body referencing `docs/superpowers/specs/2026-05-19-brotherhood-skill-augmentation-design.md` §Detailed catalog v1.3
- Full command list (15 baseline + 6 additional from Codex-added block)
- Schema bump: v1.2 → v1.3
- Attribution: Agent 2 (Codex)
- Verification: tankoctl smokes for each command

- [ ] **Step 2: Commission Codex via `/codex-trigger-d`**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md "Agent 2" "v1.3 books-side bridge layer"
```

- [ ] **Step 3: Verify Codex's diff scope matches the spec**

Spot-check: `git diff --name-only` should match the spec's §Files list. No scope expansion.

- [ ] **Step 4: Smoke test each new v1.3 command via tankoctl**

Run:
```bash
build_and_run.bat
out\tankoctl.exe books-get-state
out\tankoctl.exe books-get-library
out\tankoctl.exe books-refresh-library
out\tankoctl.exe books-search-library "test"
out\tankoctl.exe books-get-series-state
out\tankoctl.exe books-set-sort title
# ... iterate through all 21 commands
out\tankoctl.exe ping  # verify schema string is now v1.3
```

Expected: every command returns a structured reply (no UNKNOWN_COMMAND). Schema in ping is `tankoban.dev.v1.3`.

- [ ] **Step 5: Verify ping.commands enumerates new v1.3 commands**

Run:
```bash
out\tankoctl.exe ping | grep -o 'books-[a-z-]*' | sort -u
```

Expected: all 21 books-side commands listed.

- [ ] **Step 6: Codex's RTC + Agent 0 sweep handle the commit**

### Sub-phase D.3 — v1.5 sources-side commission (Agent 4B attribution)

(Note: D.1 + D.3 fire concurrently per spec Technical decision #1 since they touch independent files. D.3 spec authored in parallel.)

**Files:**
- Create: `docs/superpowers/specs/2026-05-19-bridge-v1.5-sources-commission.md`
- Modify (via Codex): `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, `src/ui/pages/TankorentPage.{h,cpp}`, `src/ui/pages/TankoLibraryPage.{h,cpp}`, `src/core/TorrentIndexer.{h,cpp}`, `src/core/torrent/TorrentClient.{h,cpp}`, `src/core/book/BookDownloader.{h,cpp}`, `tools/tankoctl.cpp`

**Goal:** v1.5 sources-side bridge per spec §Detailed catalog v1.5 + Codex-added additions (~17 commands).

- [ ] **Step 1: Author the v1.5 commission spec**

(Same shape as D.1 spec; reference §Detailed catalog v1.5 + Codex-added block.)

- [ ] **Step 2: Commission Codex via /codex-trigger-d**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.5-sources-commission.md "Agent 4B" "v1.5 sources-side bridge layer"
```

- [ ] **Step 3: Verify diff scope matches spec**

- [ ] **Step 4: Smoke test each v1.5 command**

Run:
```bash
out\tankoctl.exe sources-search-tankorent "test query"
out\tankoctl.exe sources-search-tankolibrary "test"
out\tankoctl.exe sources-get-indexer-health
out\tankoctl.exe sources-add-magnet "magnet:?xt=urn:btih:..."
out\tankoctl.exe sources-pause-torrent "<infoHash>"
# ... iterate all 17 commands
```

- [ ] **Step 5: Verify ping.commands enumerates v1.5 + verify schema string**

Schema should now be `tankoban.dev.v1.5` (since D.3 lands after D.1 schema-wise, but they can land concurrently — Codex coordinates the schema string).

### Sub-phase D.2 — v1.4 player-side deeper commission (Agent 3 attribution)

**Files:**
- Create: `docs/superpowers/specs/2026-05-19-bridge-v1.4-player-deeper-commission.md`
- Modify (via Codex): `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, `src/ui/player/VideoPlayer.{h,cpp}`, `src/ui/player/SidecarProcess.{h,cpp}`, `src/ui/player/SubtitleOverlay.{h,cpp}`, `native_sidecar/src/main.cpp` (IPC extensions), `tools/tankoctl.cpp`

**Goal:** v1.4 player-side deeper per spec §Detailed catalog v1.4 + Codex-added additions (~28 commands).

- [ ] **Step 1: Author the v1.4 commission spec**

- [ ] **Step 2: Commission Codex via /codex-trigger-d**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.4-player-deeper-commission.md "Agent 3" "v1.4 player-side deeper bridge layer"
```

- [ ] **Step 3: Verify diff scope + sidecar IPC schema**

- [ ] **Step 4: Smoke test each v1.4 command**

Run:
```bash
out\tankoctl.exe play-file "<path>"  # use existing v1.0 to load a file
out\tankoctl.exe player-pause
out\tankoctl.exe player-resume
out\tankoctl.exe player-get-audio-tracks
out\tankoctl.exe player-select-audio-track 1
out\tankoctl.exe player-seek 60
out\tankoctl.exe player-frame-step forward
out\tankoctl.exe player-set-volume 50
out\tankoctl.exe player-screenshot "out/test_screenshot.png"
out\tankoctl.exe sidecar-get-process-state
out\tankoctl.exe sidecar-get-current-stream-info
# ... iterate all 28 commands
```

- [ ] **Step 5: Verify ping.commands + schema string**

### Sub-phase D.4 — v1.6 library-side commission (Agent 5 attribution)

**Files:**
- Create: `docs/superpowers/specs/2026-05-19-bridge-v1.6-library-commission.md`
- Modify (via Codex): `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, per-mode landing pages, `src/ui/Theme.*`, `tools/tankoctl.cpp`

**Goal:** v1.6 library-side per spec §Detailed catalog v1.6 + Codex-added (~15 commands).

- [ ] **Step 1: Author the v1.6 commission spec**

- [ ] **Step 2: Commission Codex via /codex-trigger-d**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.6-library-commission.md "Agent 5" "v1.6 library-side bridge layer"
```

- [ ] **Step 3: Verify diff scope**

- [ ] **Step 4: Smoke test each v1.6 command**

Run:
```bash
out\tankoctl.exe library-get-continue-reading comics
out\tankoctl.exe library-get-recently-added books
out\tankoctl.exe library-trigger-scan videos
out\tankoctl.exe library-apply-theme noir
out\tankoctl.exe library-set-density compact
out\tankoctl.exe library-get-active-mode-pill
# ... iterate all 15 commands
```

### Sub-phase D.5 — v1.7 synthetic UI commission (Agent 0 attribution)

**Files:**
- Create: `docs/superpowers/specs/2026-05-19-bridge-v1.7-synthetic-ui-commission.md`
- Create (via Codex): `src/devtools/UiInteractionDispatcher.{h,cpp}` (NEW class)
- Modify (via Codex): `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, `tools/tankoctl.cpp`

**Goal:** v1.7 synthetic UI interaction (~13 commands). Cross-cutting; lands AFTER at least 2 domain layers (D.1 + D.3) so it can be tested against real domain state.

- [ ] **Step 1: Author the v1.7 commission spec**

Include the additional v1.7 commands from Codex-added block (ui-list-widgets, ui-wait-for, ui-set-checkbox, ui-set-combo, ui-select-table-row, ui-dry-run).

- [ ] **Step 2: Commission Codex via /codex-trigger-d**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.7-synthetic-ui-commission.md "Agent 0" "v1.7 synthetic UI interaction layer"
```

- [ ] **Step 3: Verify new dispatcher class exists**

Run:
```bash
ls src/devtools/UiInteractionDispatcher.h src/devtools/UiInteractionDispatcher.cpp
```

- [ ] **Step 4: Smoke test against table-heavy domain (Comics sources panel)**

Per Codex audit Cross-track dependencies item #6: test v1.7 against a table/list-heavy domain.

Run:
```bash
out\tankoctl.exe open-page comics
out\tankoctl.exe comics-open-series "Death Note"
# Then synthetic interaction:
out\tankoctl.exe ui-list-widgets "ComicsSourcesTable"
out\tankoctl.exe ui-click "ComicsSourcesTable_Row0"
out\tankoctl.exe ui-select-table-row "ComicsSourcesTable" 0
out\tankoctl.exe ui-query-focus
out\tankoctl.exe ui-keypress "ComicsSourcesTable" Qt.Key_Down
out\tankoctl.exe ui-dry-run ui-click "ComicsSourcesTable_Row1"
```

Expected: synthetic events fire correctly, focus moves between rows, no crashes.

### Sub-phase D.6 — v1.8 system state commission (Agent 0 attribution)

**Files:**
- Create: `docs/superpowers/specs/2026-05-19-bridge-v1.8-system-state-commission.md`
- Create (via Codex): `src/devtools/SystemIntrospection.{h,cpp}` (NEW class)
- Modify (via Codex): `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, per-domain devSnapshot extensions for new state fields, `tools/tankoctl.cpp`

**Goal:** v1.8 system state + introspection (~25 commands). Honor Codex instrumentation prerequisites (return `unsupported` until counters land; don't fake values).

- [ ] **Step 1: Author the v1.8 commission spec**

Include the Instrumentation prerequisites section from Codex-added block (network observers, perf counter sourcing, cache hit-rate omission until counters land, write-flag gating).

- [ ] **Step 2: Commission Codex via /codex-trigger-d**

Run:
```
/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.8-system-state-commission.md "Agent 0" "v1.8 system state + introspection layer"
```

- [ ] **Step 3: Verify dev-write-flag is properly gated**

Run:
```bash
# Without TANKOBAN_DEV_UI_WRITE=1:
out\tankoctl.exe settings-set "general/theme" "noir"
```
Expected: `WRITE_DISABLED` error.

```bash
TANKOBAN_DEV_UI_WRITE=1 build_and_run.bat
out\tankoctl.exe settings-set "general/theme" "noir"
```
Expected: success.

- [ ] **Step 4: Smoke test read-only commands**

Run:
```bash
out\tankoctl.exe app-get-active-modals
out\tankoctl.exe app-get-window-list
out\tankoctl.exe app-get-shortcut-table
out\tankoctl.exe settings-get "general/theme"
out\tankoctl.exe cache-get-stats
out\tankoctl.exe scanner-get-status
out\tankoctl.exe log-tail sidecar 50
out\tankoctl.exe log-mark "smoke-correlation"
out\tankoctl.exe theme-get-palette
out\tankoctl.exe perf-get-frame-times
out\tankoctl.exe events-tail 20
# ... iterate the 25 commands
```

- [ ] **Step 5: Smoke test write commands behind flag**

(Per write-flag gating verified in step 3.)

---

## Phase E — Surface integration

### Sub-phase E1 — Skill surface integration (after Phase B + C land)

**Files:**
- Modify: `agents/STATUS.md` (per-agent shortlists)
- Modify: `CLAUDE.md` ("Required Skills & Protocols" section)

- [ ] **Step 1: Update STATUS.md per-agent shortlists**

For each agent (0, 1, 2, 3, 4, 4B, 5, 7, 8), add applicable new skills to their shortlist. Tier 2 conditional for most.

- [ ] **Step 2: Update CLAUDE.md "Required Skills & Protocols" Tier 2 list**

Add the 10 universal skills + 2 specialist + 1 optional skill from Phase B + C. Schema-bump section to reflect post-arc state.

- [ ] **Step 3: Commit E1**

```bash
git add agents/STATUS.md CLAUDE.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase E1 — skill surface integration]: STATUS.md per-agent shortlists + CLAUDE.md Tier 2 list updated with new workflow skills (Phase B + C). Hook-fired skills (Phase A) noted in CLAUDE.md hook section."
```

### Sub-phase E2 — Bridge surface integration (after Phase D lands)

**Files:**
- Modify: `CLAUDE.md` (Build Quick Reference tankoctl bullet + Which MCP block)
- Modify: `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/project_dev_control_bridge.md` (ship history extension)

- [ ] **Step 1: Update CLAUDE.md tankoctl bullets**

Add v1.3-v1.8 command lists to both the Build Quick Reference and Which MCP block. Bump schema string references to `tankoban.dev.v1.8`.

- [ ] **Step 2: Update project_dev_control_bridge.md ship history**

Append v1.3 (Agent 2, 2026-05-N), v1.4 (Agent 3), v1.5 (Agent 4B), v1.6 (Agent 5), v1.7 (Agent 0), v1.8 (Agent 0) ship dates with their respective RTC pointers.

- [ ] **Step 3: Commit E2**

```bash
git add CLAUDE.md
git commit -m "[Agent 0, SKILL_AUGMENTATION_ARC Phase E2 — bridge surface integration]: CLAUDE.md tankoctl bullets updated for v1.8 schema. project_dev_control_bridge.md ship history extended through v1.8."
```

- [ ] **Step 4: Write closure memory**

Use `/memory-write project "brotherhood-skill-augmentation-arc"` to capture the arc closure with key learnings, ship dates, and pointers to all 6 bridge specs.

---

## Self-Review

(Run after the plan is complete; fix inline.)

**1. Spec coverage check:**
- Track A — 6 hooks: ✓ (Tasks A.1-A.6)
- Track A — 10 universal skills: ✓ (Tasks B.1, B.2, B.3, B.5, B.6, B.7, B.8, B.9, B.10, B.11)
- Track A — sweeper race-fix: ✓ (Task B.4)
- Track A — 2 specialist + 1 optional: ✓ (Tasks C.1, C.2, C.3)
- Track B — dispatcher delegation refactor: ✓ (D.0)
- Track B — 6 bridge layers: ✓ (D.1-D.6)
- Phase E1 + E2: ✓
- Hemanth §5 ratification deferred items (memory-trim UX, hemanth-rewrite scope, arc identifier): the plan honors Hemanth's defaults per §5 recommendations; ratification can happen at any task gate without blocking execution.

**2. Placeholder scan:**
- No "TBD" in code blocks (verified — all scaffolds have actual content).
- No "implement later" in steps (verified).
- One placeholder allowed: `<...>` in scaffold templates IS the placeholder by design (it's what the agent fills in when using the skill).

**3. Type consistency:**
- skill-provenance-detect.sh `--candidates-only` returns comma-separated list; pre-rtc-checker.sh reads this and embeds in scaffold. Consistent.
- memory-trim scoring uses age × 1 - citations × 2; consistent across the skill body.
- Dispatcher delegation uses `dispatchDevCommand(cmd, payload, reply)` signature consistently across all per-domain pages.
- Schema versioning: v1.2 → v1.3 → v1.4 → ... → v1.8 sequential. No skipping.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-19-brotherhood-skill-augmentation.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Best for this scope (50+ tasks across 5 phases; isolation per task keeps context tight).

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints. Higher risk of context bloat given task count.

Which approach?
