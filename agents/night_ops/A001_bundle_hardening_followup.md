# Freeze-fix bundle — follow-up hardening backlog (2026-06-03)

Source: Codex cross-model review of the overnight freeze-fix bundle (Agent 0, 2026-06-03).
The **core** freeze fixes landed on master (A001 `74f912e`, A005 `d911af2`, books A003/A004 `5e46b85`).
These are the **low-severity hardening items** Codex flagged that were deliberately NOT fixed in the
landing pass — they are not normal-use crashes; they are shutdown / rapid-action edge races. Each is
for the owning brother to fix **and re-review (producer≠reviewer)** before closing.

Full review artifacts: `agents/night_ops/codex_review_verdict_2026-06-03.md` (original), plus the A001
fix rounds `codex_verdict_a001fix{,2,3}_2026-06-03.md`.

---

## Agent 2 — Books (`src/core/book/BooksCatalogueLibraryStore.cpp`, `src/ui/pages/BooksPage.cpp`)

### B4 — async save drops bytes on write failure (severity: cosmetic)
`BooksCatalogueLibraryStore::save()`'s background writer: if `QSaveFile::open`/`commit` fails, the
pending bytes are dropped with no retry/report. Self-heals because every save serializes FULL state,
so the next save rewrites everything. Consider: log the failure, or retry-once. Not urgent.

### B5 — fire-and-forget validateAll captures a raw store pointer (severity: low — shutdown only)
`BooksPage` F5 / `activate()` / `showEvent()` do `QtConcurrent::run([store]{ store->validateAll(); })`
capturing a raw `BooksCatalogueLibraryStore*`. If the store is destroyed while that task runs (only
realistically at app shutdown), it's a use-after-free. The store's dtor already drains its *save*
future but NOT these validateAll tasks. Options: track validateAll in a `QFuture` the store drains in
its dtor; or capture `QPointer<BooksCatalogueLibraryStore>` and bail if null (note: cross-thread
QPointer read has the same caveat as C2 below — the store is a QObject so a drained-future approach is
cleaner).

---

## Agent 4 — Theatre poster cleanup (`src/ui/pages/stream/StreamLibraryLayout.cpp`)

**RESOLVED 2026-06-03 (Agent 4).** Both C2 + C3 fixed in one change — a single
`std::shared_ptr<std::atomic_bool> m_orphanSweepRunning`. Build green; Codex re-review APPROVE
(producer≠reviewer), findings: none — verdict at `codex_verdict_a005_c2c3_2026-06-03.md`.

### C2 — QPointer guard read from the worker thread (severity: low) — ✅ FIXED
The off-thread `cleanupOrphanPosters()` task checked `QPointer<StreamLibraryLayout> guard` from the
worker thread. QPointer isn't safe to read concurrently with GUI-thread deletion. **Fix:** dropped the
QPointer entirely (include removed); the worker now captures only by value (`cacheDir`, `library`,
`running`) and never touches the widget. The outlives-widget contract on `library` is inherited from
the landed A005, not newly introduced.

### C3 — overlapping cleanup sweeps on rapid refresh (severity: low) — ✅ FIXED
Repeated `refresh()` could launch multiple concurrent `QtConcurrent::run` poster sweeps racing to
`QFile::remove` the same orphan. **Fix:** `if (m_orphanSweepRunning->exchange(true)) return;` at entry
coalesces to one sweep; the worker clears the flag via an RAII `ClearOnExit` on every return path. The
flag is a `shared_ptr` copied into the worker so it stays lifetime-safe if the widget is torn down
mid-sweep (worker writes through the shared copy, never a freed member). A poster orphaned during a
skipped sweep lingers one refresh cycle — acceptable for best-effort cosmetic cleanup.

---

## Also noted by review (not bugs — judgment calls left as-is)
- A001 landed with the `[auto-dl]` candidate diagnostic logging kept IN, intentionally, to confirm the
  download fix in a live smoke. Trim once One Piece download is verified working end-to-end.
- OBS-1 timer-census walks topLevelWidgets + qApp + allWidgets; misses raw timers on non-widget,
  non-qApp-parented QObject trees. Fine for the storm-class timers it targets; enhance if needed.
