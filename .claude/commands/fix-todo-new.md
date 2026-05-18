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

1. **P1** — <one-line goal> — Owner: <agent> — Wakes: <est>
2. **P2** — <one-line goal> — Owner: <agent> — Wakes: <est>
3. ...

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

1. Validate name is UPPERCASE_SNAKE_CASE matching `^[A-Z][A-Z0-9_]+$`. Abort with format error if invalid.
2. Construct filename: `<NAME>_FIX_TODO.md`.
3. Check for existing file: abort with `<NAME>_FIX_TODO.md already exists` if present at repo root.
4. Write the file with the 14-section template (all sections present but bodies marked `<...>` for the author to fill in).
5. Print confirmation: `Scaffolded: <NAME>_FIX_TODO.md (14 sections, ready for filling).`

**Quality gates:**
- File is created at repo root, not in subdirectories
- All 14 sections present in order
- Date field reflects current date
- All `<...>` placeholders are clearly marked
- No markdown tables in §2 phase breakdown (per `feedback_no_tables_simple_lists.md`); use numbered list with inline owner + wakes
