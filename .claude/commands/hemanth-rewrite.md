---
description: Rewrite a paragraph in Hemanth-language. Use when a draft is jargon-heavy, missing user-end framing, or lacks context anchor.
---

You are rewriting a paragraph in Hemanth-language for Tankoban 2.

**Argument:** the paragraph to rewrite (passed inline or via stdin).

**Status:** OPT-IN ONLY. Never mandated. Agents discover this skill organically when they realize their prose is too technical for Hemanth-facing communication.

**Rules** (per `feedback_simple_language.md` + `feedback_no_tables_simple_lists.md`):

1. **Lead with the answer in user-end terms.** First sentence states the conclusion or recommendation in terms of what Hemanth sees / feels / clicks / waits on as the user of the app — not in terms of code internals. Example: "you'll see one card per series instead of two" beats "the projection collapses duplicate tiles." For purely internal work with no user-end manifestation (build infra, refactors, agent governance), lead with a plain-language analogy instead. If the original paragraph buries the lede, surface it.
2. **Short sentences.** Aim for 15-20 words max. Break long sentences. One thought per sentence.
3. **Translate jargon.** Replace coder terms with concrete actions / observations:
   - "Schema versioning" → "the rules for how we add new commands without breaking old ones"
   - "Race condition" → "two pieces of code stepping on each other"
   - "Dispatcher delegation" → "splitting the routing logic into smaller pieces"
   - "QObject lookup by objectName" → "find the widget by name"
4. **No markdown tables.** Convert to numbered lists with simple descriptions.
5. **No more than 5 numbered list items per section.** If more, break into sections with H2 headers.
6. **Avoid colons before tool calls or commands.** Just say what runs without ceremony.
7. **One thought per paragraph.** Don't chain ideas with "and" or "while".
8. **Concrete > abstract.** "fixes the bug where the checkbox shows as `[`" beats "addresses widget rendering quirks".

**Procedure:**

1. Read the input paragraph.
2. Identify the buried lede (the actual answer or recommendation).
3. Rewrite leading with the lede, applying rules 2-8.
4. Print the rewritten version.
5. If the input was already Hemanth-friendly, say so and leave unchanged with a one-line note: "Already lands clean — no rewrite needed."

**Quality gates:**
- Rewritten version is shorter than the input (or matches if input was already tight)
- No coder jargon survives unless followed by plain-language explanation in parens
- Tables removed; lists capped at 5 items per section
- Lede is in the first sentence

**Anti-patterns:**
- Don't dumb-down. Hemanth understands the brotherhood deeply — just doesn't speak coder. Treat him as a smart non-coder, not a beginner.
- Don't pad with "great question" / "good point" filler.
- Don't sacrifice precision for brevity. If a technical term is the right one, use it + explain in parens.

**Examples:**

**Input:**
> The dispatcher delegation refactor will modularize the per-domain command routing inside MainWindow::handleDevCommand() such that subsequent v1.3+ bridge layer additions can be appended additively without expanding the if/else chain's lexical complexity beyond its current ~270 LOC footprint.

**Output:**
> Before adding more `tankoctl` commands, we split the big routing block in MainWindow into smaller pieces — one per domain. After the split, new commands plug into their domain block instead of growing the central chain. The big chain stays the same length forever; new commands land in the small per-domain pieces.

**Input (no rewrite needed):**
> Phase A is done. All 6 hooks are live. Boys will notice less janky behavior on the next few wakes.

**Output:**
> Already lands clean — no rewrite needed.

**Relationship to other skills:**
- Optional partner to `/handoff-brief` and `/session-recap` (mid-wake and end-of-wake artifacts that Hemanth may read).
- NOT a replacement for `feedback_simple_language.md` discipline — agents should internalize the rules, not lean on this skill for every Hemanth-facing message.
