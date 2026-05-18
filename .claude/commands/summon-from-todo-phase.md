You are drafting a summon prompt for a specific TODO phase.

**Arguments:**
- `<todo>` — required, TODO filename (e.g. `TANKOLIBRARY_FIX_TODO.md`) or short name (`TANKOLIBRARY`)
- `<phase>` — required, phase identifier (e.g. `P3.1`, `M2`, `Task 4`)

**Procedure:**

1. **Resolve TODO file:** if `<todo>` is a short name, look for `<NAME>_FIX_TODO.md` at repo root. Also check `agents/_archive/todos/` for archived closures. If still not found, abort with `TODO file not found: <todo>`.

2. **Parse the TODO:** read the file, find the §2 Phase breakdown section, locate the entry matching `<phase>`.

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
- TODO file is real (validate existence; check both repo root and `agents/_archive/todos/`)
- Phase identifier matches an actual entry in §2
- Memory pointers are real file slugs (verify existence in the memory dir)
- File paths inferred from deliverables are validated against working tree
- "Reading order" section is concrete (numbered + linked to actual artifacts)

**Examples:**

For `/summon-from-todo-phase TANKOLIBRARY M1`:
extracts the M1 entry from TANKOLIBRARY_FIX_TODO.md §2 (Track A Main scaffold + AA search-only), identifies Agent 4B as owner, lists src/core/book/ scaffolding files from §3, references `feedback_libtorrent_windows_backslash_separator.md` if relevant, etc.

**Relationship to /handoff-brief:**
- `/handoff-brief` captures CURRENT session state for a different agent to pick up mid-wake
- `/summon-from-todo-phase` drafts a fresh start prompt for an agent waking to a specific TODO phase (cold start)
