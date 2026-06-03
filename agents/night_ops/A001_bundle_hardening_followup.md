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

### C2 — QPointer guard read from the worker thread (severity: low)
The off-thread `cleanupOrphanPosters()` task checks `QPointer<StreamLibraryLayout> guard` from the
worker thread. QPointer isn't safe to read concurrently with GUI-thread deletion. In practice it's
only an early-bail hint and the object it actually dereferences (`StreamLibrary*`, captured raw) is
documented to outlive the widget, so worst case is one wasted `has()` call. Consider removing the
cross-thread guard read (rely on the by-value `library` capture) or marshaling the bail decision.

### C3 — overlapping cleanup sweeps on rapid refresh (severity: low)
Repeated `refresh()` can launch multiple concurrent `QtConcurrent::run` poster sweeps; two threads can
race to `QFile::remove` the same orphan (one wins, the other gets a benign failure). No crash / no data
loss. Add a simple in-flight guard (`std::atomic<bool>`/`QFuture` skip-if-running) to coalesce.

---

## Also noted by review (not bugs — judgment calls left as-is)
- A001 landed with the `[auto-dl]` candidate diagnostic logging kept IN, intentionally, to confirm the
  download fix in a live smoke. Trim once One Piece download is verified working end-to-end.
- OBS-1 timer-census walks topLevelWidgets + qApp + allWidgets; misses raw timers on non-widget,
  non-qApp-parented QObject trees. Fine for the storm-class timers it targets; enhance if needed.
