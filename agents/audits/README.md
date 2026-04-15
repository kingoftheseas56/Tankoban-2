# Agent 7 Audits

Research-only comparative audits written by Agent 7 (Codex) when a domain agent or Hemanth posts a `REQUEST AUDIT` line in `agents/chat.md`. Nothing here is code, nothing here is authoritative on root cause, nothing here prescribes fixes. Domain agents read audits for perspective and decide what becomes real work.

## Rules

- **Not prescriptive.** Audits observe behavior, compare to references, and list gaps. They do NOT diagnose root causes authoritatively — that is the domain master's job. All hypothesized root causes must be explicitly labeled `Hypothesis — <Agent N> to validate`.
- **Observation vs hypothesis separation is mandatory.** Every claim must be either (a) an observation backed by a concrete source (our `src/` file:line, a reference app's source, a web-sourced specification, or an end-user-visible test result) OR (b) explicitly tagged as hypothesis. Mixing the two is a failed audit.
- **Not authoritative over domain masters.** An audit is advisory. Domain master may reject any gap with technical justification, same as with prototypes.
- **Web search is authorized** for audit runs (not for prototype runs). Citations to web sources must include the URL.
- **Read-only in src/.** Codex reads the subsystem's code to observe behavior; never edits.

## Naming

`<subsystem>_<YYYY-MM-DD>.md` — example: `book_reader_2026-04-14.md`, `video_player_2026-05-02.md`, `tankostream_buffering_2026-05-10.md`.

## Required report structure

```markdown
# Audit — <subsystem> — <YYYY-MM-DD>

By Agent 7 (Codex). For <domain master, e.g. "Agent 2 (Book Reader)">.
Reference comparison: <apps/sources compared — e.g. Readest (Foliate-js), Apple Books, calibre Viewer>.
Scope: <one paragraph — what was audited, what was out of scope>.

## Observed behavior (in our codebase)

<Tankoban-side behaviors, backed by file:line citations or reproducible user-visible actions. No speculation. If you couldn't determine a behavior from code inspection, say so explicitly: "could not determine from src/; recommend Agent N reproduce.">

## Reference behavior

<What reference apps do in the same scenarios. Cite sources: reference codebase paths, web-search URLs, documentation links. If a reference couldn't be verified, don't include it — omission beats invention.>

## Gaps (ranked P0 / P1 / P2)

**P0 (user-blocking or severely degrading):**
- <Gap>. Observed: <citation>. Reference: <citation>. Impact: <why this matters>.

**P1 (user-visible shortfall):** ...

**P2 (polish):** ...

## Hypothesized root causes (Agent <N> to validate)

Every item in this section must start with the phrase `Hypothesis —` and end with `Agent <N> to validate`. Nothing in this section is asserted as fact. If Agent 7 can't even hypothesize, leave the section empty with "None — pure UX-level audit, root causes left to domain master."

- **Hypothesis —** <plausible root cause given observed behavior and reference comparison>. **Agent <N> to validate.**

## Recommended follow-ups (advisory)

<Phrased as questions or areas of investigation for the domain master, not as prescriptions.>

- Consider whether <thing> should be reworked to match <reference> behavior.
- Investigate whether <hypothesis> holds — specific test would be ...
```

## Workflow

1. A domain agent or Hemanth posts in chat.md:
   ```
   REQUEST AUDIT — [subsystem name]: <scope / questions> | References: <reference apps or sites>. Web search: authorized.
   ```
2. Hemanth starts a Codex session. Codex reads AGENTS.md, does required reading, reads the target subsystem's src/ code, performs web-searches against cited references (and discovers adjacent references as needed).
3. Codex writes one audit file to `agents/audits/<subsystem>_<date>.md` following the template above.
4. Codex appends one announcement line to chat.md:
   ```
   Agent 7 audit written — agents/audits/<filename>. For <subsystem / domain master>. Reference only.
   ```
5. Domain agent reads when ready. Takes what's useful. No follow-up required from Agent 7.

## How audits relate to other agents

| | Agent 6 (Reviewer) | Agent 7 (Audit mode) |
|---|---|---|
| **When** | After work is shipped | Before a plan exists |
| **Against what** | A chosen objective | Reference apps + web sources |
| **Output** | Gap report vs objective | Observations + gaps + hypotheses |
| **Purpose** | Did we deliver what was asked? | What should we ask for next? |

Audits are pre-planning research. Agent 6 reviews are post-implementation compliance checks. An audit can become the objective Agent 6 later reviews against — they're sequential, not overlapping.
