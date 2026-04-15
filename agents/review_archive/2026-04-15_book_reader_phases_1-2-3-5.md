# Archive: Book Reader — Unified Review of Phases 1 + 2 + 3 + 5

**Agent:** 2 (Book Reader)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source (dual):**
- **Primary (plan):** `BOOK_READER_FIX_TODO.md` — phase/batch specs + success criteria + file-touch lists
- **Secondary (audit):** `agents/audits/book_reader_2026-04-15.md` — Agent 7's root-cause audit that produced the TODO

**Outcome:** REVIEW PASSED 2026-04-15 first-read pass. 0 P0, 0 P1, 8 P2 (all non-blocking polish observations). Zero questions back to Agent 2. BOOK_READER_FIX_TODO is CLOSED on Agent 2's side pending Hemanth smoke + optional Phase 4 reopen-on-evidence.

**Shape:** eight batches unified per Agent 2's combined-submission request (Phases 1/2/3/5 share the same TODO + audit objective source per [TODO:256](BOOK_READER_FIX_TODO.md#L256) review-gate framing). Phase 4 (file-loading random access) explicitly deferred per Hemanth direction + TODO:212's own "can be dropped if not causing visible pain" hedge.

---

## Scope summary

- **Phase 1** (1.1 + 1.2 + 1.3): closes the three P0 adapter mismatches the audit confirmed.
  - 1.1 Progress contract alignment (SHA1[:20] key + flat CONTRACTS.md fields + nested locator/bookMeta preserved)
  - 1.2 Settings API reshape (zero-arg get + one-arg save + legacy `{"":{}}` migration swallow)
  - 1.3 Foliate stabilized gating (loading overlay + 220ms JS debounce + 700ms JS fallback + 5s C++ watchdog)
- **Phase 2** (2.1 + 2.2): renderer-lifecycle P1s.
  - 2.1 Style consolidation (5 layers → 3 non-overlapping, 120ms setTimeout → RAF×2)
  - 2.2 Host resize → Foliate relayout (200ms C++ debounce + opacity-flash __ebookRelayout + 220ms stabilized-quiet restore + 800ms backstop)
- **Phase 3** (3.1): input-consistency gap.
  - 3.1 Centralized keyboard handling (handleKeyEvent single dispatch + iframe direct-call vs synthetic + scrolled-mode fall-through)
- **Phase 5** (5.1 + 5.2): polish P2s.
  - 5.1 Toolbar pin/unpin on Shift+T (on top of pre-existing auto-hide infra Agent 2 discovered post-audit)
  - 5.2 Flow-mode comment drift resolved (scrolled flow stays, scrollContent helper had already been removed)
- **Phase 4**: deferred per TODO-sanctioned open question.

Out of scope: TTS (TODO:21), audiobook features (TODO:22), functional verification (Hemanth's smoke is his job).

---

## Parity (Present) — 20+ correct features

### Phase 1.1 — Progress contract alignment
- `BookBridge::progressKey(absPath)` at [BookBridge.cpp:50-57](src/ui/readers/BookBridge.cpp#L50-L57) normalizes `\`→`/` then SHA1[:20]. Matches BooksPage.cpp:623-625 + BookSeriesView.cpp:740 exactly per header comment.
- JS standalone boot SHA1 resolution at [reader_standalone_boot.js:54-61](resources/book_reader/domains/books/reader/reader_standalone_boot.js#L54-L61); bare-path fallback removed.
- Belt-and-suspenders re-resolution in reader_core.js::open() at :657-661.
- Flat CONTRACTS.md fields + nested locator/bookMeta preserved.
- Migration: orphans-stay option chosen per TODO:73 — `FIX-PROG-ID` read-fallback handles first-reopen of pre-1.1 books.

### Phase 1.2 — Settings API reshape
- Zero-arg get + one-arg save declarations match TODO:90-94 exactly.
- Flat-root persistence with Tankoban-Max-shaped `{"settings": <flat>}` wrap on read (matches reader_state.js:359-360 unwrap); raw flat write on save. Symmetric roundtrip.
- Legacy `{"":{}}` migration at BookBridge.cpp:97-100 returns empty on first read; first post-1.2 save overwrites cleanly.

### Phase 1.3 — Stabilized gating
- `markReaderReady()` + `readerReady()` signal at BookBridge.cpp:468-471.
- Opaque black loading overlay + 5s C++ watchdog at BookReader.cpp:211-239. Solid `#000000` + centered `rgba(255,255,255,0.55)` label — `feedback_no_color_no_emoji` compliant.
- Overlay-on-open at :321 + readerReady→hideLoadingOverlay wire at :66.
- JS stabilized subscription BEFORE view.init() — catches fast stabilizations on small books that would race init's return.
- 220ms debounce for two-stabilized window (init-layout + RAF-deferred reflow).
- 700ms JS fallback for PDF/older-foliate-js renderers that never emit stabilized.
- readyFired idempotency + single-shot-semantics-with-debounce.

### Phase 2.1 — Style consolidation
- applySettings rewrite collapses 5 overlapping layers to 3 non-overlapping: (A) setStyles body typography/layout; (B) --USER__ CSS vars; (E) paginator element attributes. Mutually exclusive domains.
- PATCH7/9 direct body writes deleted (redundant with A's !important rules).
- 120ms setTimeout replaced with `requestAnimationFrame×2` (~32ms) — semantically correct fix for QWebEngine CSS-layout-commit-async race.
- Paginator attribute writes guard on `getAttribute` pre-check to skip redundant writes.

### Phase 2.2 — Host resize relayout
- C++ resize debounce 200ms gated on `m_readerReady` so pre-ready resizes don't fire.
- JS __ebookRelayout with flash-to-opacity-0 → renderer.render() → stabilized-quiet 220ms restore.
- 800ms JS backstop for post-render paths that don't emit stabilized.
- Hook cleared in destroy() so stale C++ fires post-teardown hit nothing.

### Phase 3.1 — Centralized keyboard
- `handleKeyEvent(e) → bool` single decision tree at reader_keyboard.js:154-398.
- Parent-document listener calls handleKeyEvent directly at :408.
- Iframe keydown calls handleKeyEvent directly + preventDefault/stopPropagation on handled at engine_foliate.js:784-790 (fixes pre-3.1 synthetic-KeyboardEvent-dispatch limitation where iframe native defaults couldn't be cancelled).
- `isScrolledMode(state)` helper at :147-152; ArrowUp/Down fall-through in scrolled flow only; PageUp/Down/ArrowLeft/Right/Space page-turn regardless.
- Space→nav:next added alongside existing Shift+Space→prev symmetry.

### Phase 5.1 — Toolbar pin
- `toggleHudPin` state flip + toast at reader_core.js:411-421.
- setHudVisible + scheduleHudAutoHide short-circuit when pinned.
- Shift+T keybinding integrates with Batch 3.1's central dispatch.
- Scope-shrinkage justified: auto-hide was already shipped via pre-existing scheduleHudAutoHide + .br-hud-hidden CSS; audit's "toolbar always visible" pattern-matched static HTML.

### Phase 5.2 — Flow-mode comment drift
- Resolved as: scrolled FLOW mode stays (foliate-js + flowBtn + 3.1 keyboard awareness); scrollContent() HELPER had already been removed. Comments aligned in engine_foliate.js + reader_nav.js::syncProgressOnActivity.

---

## Gaps

**P0:** none.
**P1:** none.
**P2** (all non-blocking):

1. Legacy settings migration at BookBridge.cpp:97-100 catches `disk.size() == 1` shape only; hypothetical mixed `{"": x, "fontSize": y}` wouldn't trip. Theoretical — the broken bridge only ever wrote the canonical `{"":{}}` shape.
2. 220ms stabilized debounce is over-margin against 2×RAF (~32ms). User waits up to 220ms for overlay drop on fast machines. Could tune toward 80-120ms if reveal-latency ever flagged.
3. Resize debounce 200ms + relayout settle 220ms = up to 420ms "opacity:0" during resize. Fine for drag-resize; corner-case fullscreen toggle may feel like ~400ms blank.
4. MainWindow.cpp:505 still calls `m_bookReader->show()` synchronously — TODO:135 naive reading would flag, but TODO:137-138's open-question-for-Agent-2 clause supersedes. Overlay-inside-widget is the TODO-hinted preferred design.
5. `readerReady→hideLoadingOverlay` is unconditional at :66; covered by showLoadingOverlay being called on every openBook at :321. Sound on careful inspection.
6. `FIX-PROG-ID` read-fallback in reader_core.js is now load-bearing for pre-1.1 orphan progress records (migration choice = orphans stay).
7. Phase 5.1 scope shrinkage appropriate; audit pattern-matched static HTML.
8. Phase 4 defer documented but no empirical "we measured it" data point. TODO:212's hedge framing is explicit.

---

## Agent 6 verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (n/a — none raised)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 2, Book Reader Phases 1 + 2 + 3 + 5]** 2026-04-15 first-read pass.

BOOK_READER_FIX_TODO is CLOSED on Agent 2's side pending Hemanth smoke. Phase 4 reopens only on evidence of visible pain.

No design questions back to Agent 2 — every scope deviation from literal TODO text is either explicitly sanctioned by the TODO itself (1.3 open question at :137-138, Phase 4 hedge at :212) or grounded in a correct post-ship discovery (5.1 auto-hide-already-shipped).
