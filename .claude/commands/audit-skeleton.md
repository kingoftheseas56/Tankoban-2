You are scaffolding an Agent 7-style audit document for Tankoban 2.

**Arguments:**
- `<topic>` — required, kebab-case identifier (e.g. `stream-pause-race-investigation`)

**Output location:** `agents/audits/<topic>_<YYYY-MM-DD>.md`

**Procedure:**

1. Construct filename with today's date suffix in YYYY-MM-DD format.
2. Check for existing file. If collision, append numeric suffix (`_2`, `_3`, etc).
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

(Numbered, ranked by severity. Use **observation** vs **hypothesis** separation — observations are verifiable facts; hypotheses are guesses about cause. Do NOT use markdown tables; numbered findings with labeled sub-fields per `feedback_no_tables_simple_lists.md`.)

### Finding 1: <title> [SEVERITY: CRITICAL / HIGH / MEDIUM / LOW]

**Observation:** <verifiable fact — what was seen, where, with timestamps/file:line refs>

**Hypothesis:** <best guess about cause — explicitly flagged as guess if not proven>

**Repro:** <minimal steps to reproduce>

**Recommendation:** <concrete action>

---

### Finding 2: ... [SEVERITY: ...]

(Same shape: observation / hypothesis / repro / recommendation.)

---

## §5 — Ratification questions

(For audit commissioner — typically Hemanth + originating agent. Only product/strategic decisions per Rule 14. Agent-0 technical decisions go in the body.)

1. **<Question 1>** — Recommended answer + reasoning.
...

---

## Anti-patterns identified

1. <What NOT to do, based on findings>
...

---

## Related work

- Prior audits: <list with paths>
- Memory references: <list of slugs>
- Active TODOs: <list>

---

## Audit signature

Generated <YYYY-MM-DD>. <N findings, <K critical / J high / M medium / L low>.
```

4. Print confirmation: `Scaffolded: agents/audits/<topic>_<date>.md (audit template ready for filling).`

**Quality gates:**
- NO markdown tables in the scaffold (per `feedback_no_tables_simple_lists.md`)
- Observation vs hypothesis separation enforced via labeled sub-fields
- Severity labels are uppercase enum (CRITICAL/HIGH/MEDIUM/LOW)
- §5 section reserved for Hemanth-product-strategic decisions only
- Repro steps required per finding (not optional)
- Audit signature line summarizes counts by severity tier

**Examples:**

For `/audit-skeleton stream-pause-race-investigation`:
- Creates `agents/audits/stream-pause-race-investigation_2026-05-19.md`
- File has the 7-section template with placeholders for Executive summary, Findings, §5, Anti-patterns, Related work, Audit signature
