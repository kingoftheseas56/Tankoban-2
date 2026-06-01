# Book Reader Fix TODO

**Owner:** Agent 2 (Book Reader). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc as the objective source.

**Created:** 2026-04-15 by Agent 0 after Agent 7's book reader audit (`agents/audits/book_reader_2026-04-15.md`) + Agent 2's validation pass confirming all three P0 hypotheses on-disk.

## Context

Hemanth's book reader has been glitchy for weeks. Agent 7's audit diagnosed the dominant root cause: the reader's JavaScript is largely copied from Tankoban-Max's Electron/IPC frontend, but the Qt/C++ QWebChannel backend was rewritten with different API shapes and different persistence contracts. Every settings save, progress write, and library lookup crosses a broken adapter boundary. "Glitchy" is mostly integration drift, not rendering.

Agent 2's validation pass confirmed all three P0s with on-disk proof:

- **Progress mismatch CONFIRMED.** `books_progress.json` contains two disjoint records for the same book — a stale SHA1-keyed contract-shape record the library UI reads (showing 3.6%, chapter 4), and a live abs-path-keyed Tankoban-Max-shape record the reader actually writes (`locator.fraction=0.437`, ~44%). Library reads the wrong record; Continue reading is wrong; Reset-progress wipes the wrong one.
- **Settings mismatch CONFIRMED.** `books_settings.json` contains literally `{"":{}}`. JS calls `booksSettings.save(settingsObject)` one-arg (Tankoban-Max shape); bridge expects `save(bookId, data)` two-arg (Tankoban 2 shape); QWebChannel stringifies the settings object as empty `""` for the id, and passes undefined as data. Every settings save drops on the floor.
- **Stabilized gating CONFIRMED.** Zero `stabilized` references in the reader JS. Reader shown synchronously at `MainWindow.cpp:504`. Delayed second `setStyles()` at 120ms guaranteed to reflow. Intermediate pre-style visual state + second-reflow shimmer are architecturally guaranteed.

Framing: three adapter mismatches + one renderer-lifecycle miss. Phase 1 closes all three. Phases 2-5 are polish on top.

## Decisions locked in

- **TTS is out of scope for this TODO.** Hemanth deferred the decision (`window.speechSynthesis` refactor vs strip UI vs implement Edge bridge). The existing `booksTtsEdge` stubs (returning `ok:false`) stay as-is. The UI is visible but non-functional — known-broken, deliberately deferred. Do NOT try to fix TTS as part of any phase below.
- **Audiobook features stay in scope** — they use a different bridge (`audiobooks.*`) which is not implicated in any P0/P1.
- **Adapter alignment direction = C++ bridge conforms to Tankoban-Max JS shapes** (mostly). The JS was copied from a working reference app; forcing the JS to reshape would reinvent proven UX patterns. Cheaper + safer to reshape the C++ bridge where it deviates. Exception: progress contract follows `agents/CONTRACTS.md` (since BooksPage/BookSeriesView and other domain agents depend on that shape).

## Objective

A user opens the reader, changes a setting, reads a few pages, closes, reopens. Settings restore. Progress restores. The library continue strip shows the right percent. No early-render flashes or 120ms reflow shimmer at open.

---

## Phase 1 — Adapter alignment (all three P0s)

**Why:** Every other fix is wasted while saves land in the wrong key with the wrong shape.

### [ ] Batch 1.1 — Progress contract alignment

**Symptom:** reader writes one record, library reads another; they don't match.

**Fix:** make the reader write under the **SHA1 key** with a payload that has the **CONTRACTS.md-mandated flat fields at the top level** while preserving the current rich locator shape as a nested field (the reader uses it on resume).

**Required payload shape** (the union of contract + reader needs):

```json
{
  "chapter": <int, 0-based section index>,
  "chapterCount": <int, total sections>,
  "scrollFraction": <float 0..1, Foliate fraction>,
  "percent": <float 0..100, scrollFraction * 100>,
  "finished": <bool, true when scrollFraction >= 0.97 or fraction==1 at last section>,
  "path": "<absolute file path, forward-slash normalized>",
  "bookmarks": [<user bookmarks array>],
  "locator": { <existing foliate locator — cfi, href, fraction, location, chapterReadState, etc.> },
  "bookMeta": { <existing bookMeta — format, mediaType, series, seriesId, title> },
  "updatedAt": <ms since epoch — set by CoreBridge, don't overwrite from JS>
}
```

**Required key shape:** `SHA1(absolutePath).hexdigest()[:20]` — exactly matches `BooksPage.cpp:623-625` and `BookSeriesView.cpp:740`. The reader needs to compute this key before saving.

Two implementation choices for the SHA1 computation:
- **(a)** Add `Q_INVOKABLE QString BookBridge::progressKey(const QString& absPath)` that returns the SHA1[:20]. JS calls it once on book open, caches the result, uses it for every save.
- **(b)** Compute SHA1 in JS directly (Web Crypto `crypto.subtle.digest('SHA-1', ...)`). Async, but one-shot per book open.

**Recommended: (a)** — matches how the library computes it, one canonical implementation, no divergence risk. Expose the same helper `BooksPage.cpp` already uses.

**Files touched:**
- `src/ui/readers/BookBridge.h/.cpp` — add `progressKey(absPath)` method. Use existing QCryptographicHash::Sha1.
- `src/ui/readers/BookReader.cpp` — JS shim: add `booksProgress.keyFor: function(p) { return b.progressKey(p); }`.
- `resources/book_reader/domains/books/reader/reader_state.js` — on book open, resolve `state.book.id = await Tanko.api.booksProgress.keyFor(abs_path)` instead of leaving it as abs_path. In the save payload builder (~line 484-503), derive the flat fields (`chapter`, `chapterCount`, `percent`, `scrollFraction`, `finished`, `path`, `bookmarks`) from the existing locator, then spread the existing locator+bookMeta as nested fields alongside. Keep `updatedAt` out of the JS payload — CoreBridge adds it.
- `resources/book_reader/domains/books/reader/reader_standalone_boot.js:50` — remove the fallback that defaults `bookInput.id` to file path. id must be the SHA1 key, computed via the new bridge method.

**Migration concern:** users with existing abs-path-keyed records in `books_progress.json` will see those records become stranded. Options:
- Do nothing (orphan records stay; next save creates fresh SHA1 record; harmless but files grow).
- One-shot migration: on first open after this batch ships, scan the file, find any record whose key is a path starting with `C:/` or `/`, compute its SHA1[:20], move the record under the new key (preserving the newer `updatedAt`), delete the old key. Agent 2's call.

**Success criteria:**
- Open a book, turn pages, close. `books_progress.json` has exactly one record for that book, keyed by SHA1[:20] of the absolute path.
- Record has all seven top-level contract fields (`chapter`, `chapterCount`, `scrollFraction`, `percent`, `finished`, `path`, `bookmarks`) plus nested `locator` + `bookMeta` + `updatedAt`.
- `BooksPage` continue strip shows the correct percent (not the stale one from a leftover abs-path record).
- Reopen book — reader resumes at the correct `locator.cfi` / `fraction`.

### [ ] Batch 1.2 — Settings API reshape

**Symptom:** `books_settings.json` is `{"":{}}`. Settings evaporate on every close.

**Fix:** change the C++ bridge to match the global-settings shape the JS expects (zero-arg get, one-arg save). This is what Tankoban-Max does and what `reader_state.js:357, 461, 463` already assumes.

**New bridge signatures:**

```cpp
// BookBridge.h
Q_INVOKABLE QJsonObject booksSettingsGet();               // no args
Q_INVOKABLE void booksSettingsSave(const QJsonObject& data); // one arg
```

**New persistence shape** (`books_settings.json`):

```json
{
  "fontSize": 18,
  "lineHeight": 1.5,
  "theme": "dark",
  ...
}
```

Flat settings object at root. No per-book keying. (If per-book settings ever becomes a user-requested feature, add it as a layered override later; global is the default.)

**Files touched:**
- `src/ui/readers/BookBridge.h` — replace the two method declarations.
- `src/ui/readers/BookBridge.cpp` — replace the two method bodies. Read/write to `books_settings.json` directly via `CoreBridge::store()`. Drop the `booksSettings/` map-of-bookId-to-settings pattern.
- `src/ui/readers/BookReader.cpp` — update the JS shim from `get: function(id) { return b.booksSettingsGet(id); }` to `get: function() { return b.booksSettingsGet(); }`, same for save.
- No JS changes needed — `reader_state.js:357, 461, 463` already call the zero/one-arg shape.

**Migration:** the existing `{"":{}}` record is obviously garbage. First save after this batch ships overwrites it. No migration needed.

**Success criteria:**
- Change a setting in the reader, close, reopen. Setting persists.
- `books_settings.json` shows the actual settings flat at the root, no `""` key.
- Settings apply immediately on reopen (theme renders correctly from the first paint after stabilized — see Batch 1.3).

### [ ] Batch 1.3 — Foliate `stabilized` gating

**Symptom:** reader visible during HTML-load + Foliate-boot + first-paint + 120ms-delayed-second-setStyles reflow. Flashes, page-count snap, reflow shimmer.

**Fix:** don't show the reader widget until Foliate's renderer emits `stabilized`.

**JS side:**
- In `engine_foliate.js` after `view.open(file)` + `view.init(...)` (around line 957), subscribe to `view.renderer.addEventListener('stabilized', ...)` (event fires when pagination settles). On first `stabilized`, emit a readiness signal to the bridge: `await Tanko.api.reader.ready()` (or via a new `BookBridge::markReaderReady()`).
- Collapse or remove the delayed 120ms second `setStyles()` call at `engine_foliate.js:612`. If it's there to catch dynamic font loads, do it differently — hook into `document.fonts.ready` and call `setStyles()` once after fonts resolve, BEFORE emitting stabilized. If it's scaffolding from an older integration, delete it.

**Qt side:**
- Add `Q_INVOKABLE void BookBridge::markReaderReady()` emitting a `readerReady()` signal.
- `BookReader` widget starts hidden (or with an opaque loading overlay). On `readerReady()`, show the webview (or fade out the overlay). Reasonable timeout fallback: if `readerReady()` doesn't fire within 5s, show anyway with a warning log (so a broken JS handler doesn't keep the reader invisible forever).
- `MainWindow::openBookReader()` stops calling `show()` synchronously — defers to the readiness signal.

**Open design question for Agent 2:** is the "not yet ready" state shown as an opaque loading overlay inside the reader widget, or does the widget itself stay hidden while the library page sits behind it? Pick the one that avoids a visible "pop" when the reader appears. Loading overlay with fade-in is usually smoother.

**Files touched:**
- `src/ui/readers/BookBridge.h/.cpp` — `markReaderReady()` method + `readerReady()` signal.
- `src/ui/readers/BookReader.h/.cpp` — ready-signal connection, deferred show, loading overlay or hidden-until-ready pattern.
- `src/ui/MainWindow.cpp:504-505` — remove the synchronous `show()`; defer to readerReady.
- `resources/book_reader/domains/books/reader/engine_foliate.js` — subscribe to `stabilized`, call `markReaderReady`, remove or restructure the 120ms delayed `setStyles()`.

**Success criteria:**
- Open a mid-size EPUB. No flash of unstyled content. No page-count snap visible in the first 500ms. No text-reflow shimmer.
- If JS crashes or stabilized never fires, reader becomes visible within 5s with a warning in the debug log — never invisible forever.
- Resize/fullscreen (Phase 2 Batch 2.2) will hook the same stabilized event for re-layout gating.

### Phase 1 exit criteria

- All three P0s closed, validated by re-running Agent 2's validation methodology (trace open → page turn → close → inspect JSON files).
- `books_progress.json` has exactly one SHA1-keyed record per book with the contract shape.
- `books_settings.json` has a flat settings object, no empty-string keys.
- Reader opens without visible flashes.
- `READY FOR REVIEW` posted. Agent 6 reviews against this phase's objectives in the audit + this TODO.

---

## Phase 2 — Renderer lifecycle polish

**Why:** Phase 1 closes the P0s. Phase 2 addresses the related P1s — overlapping style systems, resize-to-Foliate propagation. Less critical but user-visible.

### [ ] Batch 2.1 — Consolidate overlapping style systems

**Symptom** (from audit lines 50, 65): `setStyles(buildEpubStyles())` + ReadiumCSS vars + direct body `font/line-height/weight/max-width/padding` writes + renderer attributes + delayed second `setStyles()` — five overlapping layers that can conflict during settings changes.

**Fix:** pick ONE canonical style-application path. Readest's `FoliateViewer.tsx:522-556` + `657-670` applies all settings through `renderer.setStyles()` + CSS custom properties on the renderer element — that's the one path to match. Delete the direct-body writes and the renderer-attribute writes. All font/layout/theme changes go through `setStyles()` with a complete regenerated stylesheet (let Foliate handle font-size, line-height, margins, max-width, colors as CSS properties).

**Files:** `resources/book_reader/domains/books/reader/engine_foliate.js` — rewrite `applySettings()` and style-application paths.

**Success:** changing a setting mid-book causes exactly one reflow, not 2-3. No body-style leakage.

### [ ] Batch 2.2 — Host resize → Foliate relayout

**Symptom** (audit line 51): `BookReader::resizeEvent` only sets webview geometry; Foliate doesn't know to recompute columns/margins.

**Fix:** on `resizeEvent` and fullscreen toggle, post a message to JS (via `BookBridge::requestRelayout()` signal or a direct `runJavaScript("view.renderer.size()")` or equivalent foliate-js API). The JS handler gates the visible re-layout the same way Batch 1.3's `stabilized` gates the initial one (flash protection).

**Files:**
- `src/ui/readers/BookReader.h/.cpp` — resize/fullscreen hooks call JS.
- `resources/book_reader/domains/books/reader/engine_foliate.js` — expose a relayout helper that triggers Foliate's internal resize and waits for the next `stabilized` before confirming.

**Success:** resize window during reading, no stale columns. Fullscreen in/out is smooth.

---

## Phase 3 — Input consistency

### [ ] Batch 3.1 — Centralize keyboard handling

**Symptom** (audit line 52): iframe `keydown` synthesizes a parent `keydown`; parent `reader_keyboard.js` owns global shortcuts. Focus-dependent dispatch path; page flips, selection popups, and shortcuts feel inconsistent.

**Fix:** consolidate on Readest's pattern (`usePagination.ts:49-63, 128-146, 206-224`). One decision point that owns: arrow-key navigation, Space/PgUp/PgDn, Ctrl+F (search), Escape, bookmark shortcut, TOC shortcut, fullscreen, zoom. Iframe listeners forward events with minimal transformation; parent is the dispatcher. Handle RTL, scrolled mode, page mode, panning explicitly (Readest's pattern as reference).

**Files:** `resources/book_reader/domains/books/reader/reader_keyboard.js` rewrite; `engine_foliate.js` input forwarding cleanup.

**Success:** page flips work identically whether focus is in the parent chrome, inside the iframe, or on a selected text range.

---

## Phase 4 — File loading path

### [ ] Batch 4.1 — zip.js random access instead of whole-file bridge read

**Symptom** (audit line 53): `BookBridge::filesRead` reads the entire EPUB into memory before JS decodes anything; JS then falls back to `file://` fetch if ArrayBuffer conversion fails. Large books stall; behavior varies based on QWebChannel value conversion.

**Fix:** foliate-js's README (line 128) explicitly recommends zip.js against `File` objects for random access. Adopt that pattern — expose a `File`-like interface to foliate-js rather than pushing the whole blob through the bridge.

**Files:** `src/ui/readers/BookBridge.h/.cpp` + `resources/book_reader/domains/books/reader/engine_foliate.js`.

**Open design question for Agent 2:** QWebEngine doesn't give us direct `File` constructors in JS that reference disk files without going through `fetch(file://...)`. One path is to register a `QWebEngineUrlSchemeHandler` for a custom scheme (e.g. `tankoban-epub://<sha1>/...`) that serves file bytes with HTTP Range support, so foliate-js + zip.js can random-access without whole-file reads. Implementation scope is non-trivial; defer if simpler fix paths exist. If the whole-file read isn't actually causing visible pain, this batch can be dropped.

**Success:** opening a 200MB EPUB starts rendering within ~1s, not after a full-file load.

---

## Phase 5 — Polish (P2s, advisory priority)

### [ ] Batch 5.1 — Toolbar auto-hide / immersive reading mode

**Symptom** (audit line 56): toolbar always visible with persistent search/font/theme/flow/fullscreen buttons. Feels like an app shell, not an immersive reading surface. Calibre and Apple Books both conceal chrome by default.

**Fix:** add an auto-hide behavior — toolbar visible on mouse move / tap / hover near the top, fades after 3s of inactivity. Keyboard shortcut (e.g. T) pins/unpins. Preserve all existing controls; just hide them by default.

**Files:** `resources/book_reader/ebook_reader.html` + `resources/book_reader/domains/books/reader/reader_core.js`.

### [ ] Batch 5.2 — Flow-mode consistency

**Symptom** (audit line 57): comments say "scroll mode removed" while code still special-cases scrolled mode in navigation. Ambiguity.

**Fix:** decide — scrolled mode stays (per foliate-js support) or goes (per comment). Pick one, align code + comments.

---

## Deferred (known, not in this TODO)

- **TTS** — `booksTtsEdge` stubs return `ok:false` from a prior Kokoro cleanup. UI visible but non-functional. Hemanth has not decided direction. Separate TODO when he does.
- **Audiobook feature parity** — not audited, not in P0/P1 set, not touched.
- **Search-within-book polish** — works today, no P0/P1 flagged. Revisit if Hemanth asks.

---

## Verification per phase

- **Phase 1:** re-run Agent 2's validation methodology. Trace open → page turn → close. Inspect `books_progress.json` + `books_settings.json`. Both files show clean contract-shape records. Continue reading strip in BooksPage shows correct percent. Reader opens without flash.
- **Phase 2:** change a setting mid-book — one reflow, not three. Resize window — columns recompute cleanly.
- **Phase 3:** navigate with keyboard from every focus state. No duplicate or missed keys.
- **Phase 4:** open a large EPUB, measure time-to-first-paint.
- **Phase 5:** subjective — does it feel like a reading app now.

**Rule 6 per batch:** build clean + smoke-test in the running app before declaring done. Paste exit code + one-line smoke result in chat.md before `READY TO COMMIT`.

**Rule 11 per batch:** `READY TO COMMIT — [Agent 2, Batch X.Y]: <message> | files: ...` in chat.md. Agent 0 batches commits at phase boundary.

**Review gate per phase:** `READY FOR REVIEW — [Agent 2, Phase X]: Book Reader Phase X | Objective: Phase X per BOOK_READER_FIX_TODO.md | Audit: agents/audits/book_reader_2026-04-15.md. Files: ...`. Agent 6 reviews against the audit + this TODO as co-objective. No phase advances past commit until `REVIEW PASSED`.

---

## Critical files modified (summary)

**Modified:**
- `src/ui/readers/BookBridge.h/.cpp` — add `progressKey`, reshape `booksSettingsGet/Save` to zero/one-arg, add `markReaderReady`, add `requestRelayout`
- `src/ui/readers/BookReader.h/.cpp` — deferred show, ready-signal wiring, resize→JS hook, JS shim updates
- `src/ui/MainWindow.cpp` — defer reader show to readerReady signal
- `resources/book_reader/domains/books/reader/reader_state.js` — use SHA1 key, payload shape update
- `resources/book_reader/domains/books/reader/reader_standalone_boot.js` — drop abs-path fallback id
- `resources/book_reader/domains/books/reader/engine_foliate.js` — subscribe stabilized, drop delayed setStyles, consolidate style path, expose relayout helper
- `resources/book_reader/domains/books/reader/reader_keyboard.js` — centralized dispatch (Phase 3)
- `resources/book_reader/ebook_reader.html` — auto-hide toolbar (Phase 5)

**No files created unless zip.js path is taken in Phase 4.**
