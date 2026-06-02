---
description: Package a cross-model REVIEW handoff (Codex / Agent 9) that checks a diff against a written Definition of Done, not just code-against-code. Use before merging non-trivial work.
---

You are packaging a cross-model **review** handoff for Tankoban 2 — the Review Gate (`agents/GOVERNANCE.md` § Review Gate). The point is that the reviewer verifies the diff against a **written Definition of Done (intent)**, because a reviewer with no spec checks "code against code, not code against intent" (deep-research 2026-06-02).

**Arguments:**
- `<diff-ref>` — required. What to review: a commit range (`abc123..def456`), a single SHA, or an explicit file list / working-tree diff.
- `<dod-source>` — required. The Definition of Done: either a path to a plan/spec/fix-TODO whose Acceptance Criteria section IS the DoD, OR an inline one-line `Done-when:` statement. **Abort if empty** — a review without a DoD is exactly the gap this skill closes.
- `<attribution-agent>` — required. The agent requesting the review (e.g. `Agent 4`).

**Hard rule — producer ≠ reviewer:** the reviewer must be a DIFFERENT model/agent than the one that produced the diff (a model silently endorses ~1-in-3 of its own semantic-drift bugs). Default reviewer = Codex (`codex exec`); use Agent 9 (DeepSeek) if Codex quota is low and the producer was not DeepSeek.

**Procedure:**

1. **Resolve the DoD.** If `<dod-source>` is a file, extract its "Acceptance Criteria" / "Definition of Done" section verbatim; abort with `No acceptance criteria found in <dod-source>` if absent. If inline, use the text. The DoD must be a concrete, checkable list — not "make it good."

2. **Gather the diff** for `<diff-ref>` (e.g. `git diff <range>` or the named files) so the reviewer sees exactly what changed.

3. **Construct the review prompt block:**

```
Cross-model review for Tankoban 2 (requested by <attribution-agent>). You are a DIFFERENT model than the author — your job is to check this diff against the written Definition of Done, not just read the code.

DEFINITION OF DONE (verify the diff against EACH item):
<the resolved DoD criteria, one per line>

DIFF UNDER REVIEW:
<the diff for diff-ref>

YOUR REVIEW — do all four:
1. For EACH Definition-of-Done item: state MET / NOT-MET / PARTIAL with one line of evidence from the diff.
2. Flag anything the diff DOES that the DoD never asked for (scope creep / unrequested behavior change).
3. Correctness + security pass: real bugs, regressions, leaked secrets, unsafe input/network handling.
4. Anything the DoD SHOULD have specified but didn't (gap in intent itself).

END with exactly one line: APPROVE or REQUEST-CHANGES, plus a one-sentence reason. Be terse; default to REQUEST-CHANGES if any DoD item is NOT-MET or you are unsure.
```

4. **Print the block to stdout** for paste into a reviewer tab, or fire via `codex exec "<block>" </dev/null` (stdin closed — `codex exec` blocks on stdin otherwise; multiline rides stdin, never argv, on Windows).

5. **After the review returns:** the requesting agent addresses every NOT-MET / REQUEST-CHANGES item before merge. Record the outcome — a non-trivial RTC carries `Done-when: <the DoD>` (CONTRACTS § Done-when) so the intent is on record with the commit.

**Quality gates:**
- DoD is non-empty and concrete (abort otherwise — this is the whole point).
- Reviewer is a different model than the producer.
- The emitted block is self-contained (reviewer needs no follow-up to start).

**Example:**
`/codex-review HEAD~3..HEAD docs/superpowers/plans/2026-06-02-memory-context-architecture.md "Agent 0"` →
emits a review block listing that plan's acceptance criteria as the DoD and the last-3-commits diff, asking Codex to verify each criterion MET/NOT-MET and return APPROVE/REQUEST-CHANGES.
