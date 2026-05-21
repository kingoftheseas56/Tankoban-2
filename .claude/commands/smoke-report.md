---
description: Format a smoke verification report. Use when reporting smoke outcomes against acceptance criteria.
---

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

- **D1: <name>** — reason: <wall-clock / dep-blocked / out-of-scope> — defer-to: <next-wake / future-arc / never>

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
- NO markdown tables — bulleted list format only per `feedback_no_tables_simple_lists.md`

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

**Anti-patterns:**
- Don't bury a failing smoke in prose — flag explicitly with **RED** in the verdict cell
- Don't conflate discovered findings with smoke failures (they're orthogonal axes)
- Don't omit the cross-reference; future grep for the TODO + phase needs it
