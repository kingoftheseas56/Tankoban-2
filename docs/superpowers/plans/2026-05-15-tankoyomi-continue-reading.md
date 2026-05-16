# Tankoyomi Continue Reading Integration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to walk this in-session task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Brotherhood conventions OVERRIDE the skill's default TDD/`git commit` shape — see Phase 0 conventions.

**Goal:** Make a Tankoyomi-downloaded chapter appear in the Comics "CONTINUE READING" strip the moment the user starts reading it, with subtitle `"<ChapterName> • Page X/Y"`, series-hero cover, and the existing `[tankoyomi]` provenance chip — closing the post-merger smoke gap surfaced 2026-05-15.

**Architecture (Approach A, per the spec):** Just-in-time registration into `ComicsPage::m_progressKeyMap` via the existing `ComicsTankoyomiDetailView::openComicRequested` slot. When the user clicks a chapter row, ComicsPage inserts the cbz path into the lookup map before forwarding the open request. Then `ComicReader::saveProgress` writes a JSON entry under the same SHA1 key, and `refreshContinueStrip` resolves the key cleanly. Forward-compatible with a Tankoyomi-exclusive Comics mode (factored helpers + inline exit-ramp comments).

**Tech Stack:** Qt6 / C++20, `QCryptographicHash`, `QFileInfo`, `QDir`, brotherhood smoke tools (`build_check.bat`, `build_and_run.bat`, `tankoctl.exe`, `pywinauto-mcp`).

**Spec reference:** `docs/superpowers/specs/2026-05-15-tankoyomi-continue-reading-design.md` (7 locked product decisions in §"Locked product decisions").

---

## Phase 0 — Conventions + Pre-flight

**Brotherhood conventions (OVERRIDE skill defaults):**
- Work on `master` directly. **No worktrees, no feature branches.** (`feedback_no_worktrees.md`)
- **Do NOT run `git commit` / `git add` / `git rm` / `git checkout` / `git stash`.** Agent 0 sweeps RTC lines from `agents/chat.md` in batched commits.
- **No TDD / automated tests for this feature** — it's pure UI glue. Smoke-first per CLAUDE.md "Required Skills & Protocols" Tier 2.
- `build_check.bat` runs after every phase that touches `src/`. Invoke via `cmd.exe //C ".\build_check.bat"`. Tail must be `BUILD OK`. If `BUILD FAILED exit=<n>`, read the last 30 cl.exe lines + fix.
- One RTC line appended to `agents/chat.md` at the END of every phase that ships `src/` touches. Phase 5 (smoke) appends a NOTE not an RTC; Phase 6 appends the final ARC-CLOSE-shaped RTC.
- RTC format (contracts-v3): `READY TO COMMIT - [Agent 1, <ARC_NAME> <phase-summary> 2026-05-15 ~HH:MMam/pm. <body>...] | Skills invoked: [/skill1, /skill2, ...] | files: <path1>, <path2>, ...`. Non-trivial RTCs (≥1 src/ file or ≥30 LOC) MUST include the `Skills invoked` field.
- Do NOT touch `.claude/telemetry/skill-discipline.jsonl` (system hook).
- Smoke is Agent 1's lane via MCP (Hemanth is hands-off per CLAUDE.md "HEMANTH'S ROLE" block). Drive via `tankoctl` + `pywinauto-mcp` — no asking Hemanth to drive UI or paste output.

### Task 0: Pre-flight — verify working-tree state

**Files:** none (verification only)

- [ ] **Step 1: Confirm we're on master with a sane working tree**

Run:
```bash
git status --short | head -20
git log --oneline -5
```

Expected: master branch, recent commits visible. Unswept RTCs from prior wakes (e.g. the post-fix scraper RTCs from 2026-05-15 morning) are fine to work on top of — do NOT sweep them.

- [ ] **Step 2: Confirm no stale Tankoban process is holding files**

Run:
```bash
taskkill //F //IM Tankoban.exe 2>&1
taskkill //F //IM ffmpeg_sidecar.exe 2>&1
```

Ignore "not found" errors. Avoids stale-EXE locks during the first build_check.

- [ ] **Step 3: Re-read the spec**

Path: `docs/superpowers/specs/2026-05-15-tankoyomi-continue-reading-design.md`.

The 7 product decisions in §"Locked product decisions" are non-negotiable. The §"Components" section names every function this plan creates.

---

## Phase 1 — Promote `itemIdForPath` to a shared free helper

**Goal:** Single source of truth for the "cbz path → 20-char hex SHA1 prefix" contract that `ComicReader::saveProgress`, `ComicsPage::ensureTankoyomiChapterInMap` (next phase), and `ComicsPage::refreshContinueStrip` all rely on. Eliminates the silent-drift risk if any caller's inline hash recipe diverges.

**Files:**
- Create: `src/ui/readers/comic_progress_key.h`
- Modify: `src/ui/readers/ComicReader.cpp` (single-line body of `itemIdForPath` at `:1671-1674`)
- Modify: `CMakeLists.txt` (add the new header to the source list — find existing `src/ui/readers/ComicReader.h` line and add the new header next to it)

### Task 1: Create the shared key helper

- [ ] **Step 1: Create the header file with the helper function**

Path: `src/ui/readers/comic_progress_key.h`.

Content:
```cpp
#pragma once

#include <QCryptographicHash>
#include <QString>

// Shared "cbz absolute path → progress-key" contract for Comics-mode reading
// progress. Used by:
//   - ComicReader::itemIdForPath (delegates here) — writes saveProgress with this key
//   - ComicsPage::ensureTankoyomiChapterInMap — registers a Tankoyomi cbz under
//     the same key just-in-time before reading begins
//   - ComicsPage::refreshContinueStrip — reads the key back via m_bridge->allProgress
//
// Contract: SHA1(utf8(path)).hex().left(20). Same algorithm callers used
// historically — promoted to a free helper to prevent silent drift.
//
// Forward-compat note (TANKOYOMI_CONTINUE_READING design 2026-05-15): if Comics
// mode pivots to Tankoyomi-exclusive in the future and m_progressKeyMap goes
// away in favour of a direct MangaDownloadIndex ↔ JsonStoreBridge join, this
// helper STAYS — it's the persistence-side contract, not a map-side artefact.
inline QString comicProgressKeyForPath(const QString& cbzPath)
{
    return QString(QCryptographicHash::hash(
        cbzPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}
```

- [ ] **Step 2: Refactor `ComicReader::itemIdForPath` to delegate**

Open `src/ui/readers/ComicReader.cpp`. Find lines 1671-1674:

```cpp
QString ComicReader::itemIdForPath(const QString& path) const
{
    return QString(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}
```

Replace with:

```cpp
QString ComicReader::itemIdForPath(const QString& path) const
{
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — delegated to shared helper so
    // ComicsPage::ensureTankoyomiChapterInMap (just-in-time map population
    // before reader open) writes under an identical key.
    return comicProgressKeyForPath(path);
}
```

- [ ] **Step 3: Add the include to ComicReader.cpp**

At the top of `src/ui/readers/ComicReader.cpp`, locate the existing include block (likely near the top, alongside `#include "ComicReader.h"`). Add:

```cpp
#include "comic_progress_key.h"
```

Keep the existing `#include <QCryptographicHash>` — other callsites in the file still use it directly for non-progress-key hashing.

- [ ] **Step 4: Register the new header in CMakeLists**

Open `CMakeLists.txt`. Search for `src/ui/readers/ComicReader.h` (or `src/ui/readers/ComicReader.cpp`). Find the surrounding `set(...)` or `target_sources(...)` block listing reader-side sources. Add `src/ui/readers/comic_progress_key.h` to that block (preserving alphabetical order if the existing list maintains it).

Example, if the surrounding block looks like:

```cmake
src/ui/readers/BookReader.cpp
src/ui/readers/BookReader.h
src/ui/readers/ComicReader.cpp
src/ui/readers/ComicReader.h
```

After:

```cmake
src/ui/readers/BookReader.cpp
src/ui/readers/BookReader.h
src/ui/readers/ComicReader.cpp
src/ui/readers/ComicReader.h
src/ui/readers/comic_progress_key.h
```

(Header-only `.h` files for Qt projects only need CMakeLists registration if AUTOMOC scans them or if they're part of a public-headers install set. For brotherhood pattern consistency with `MangaResult.h`, `MangaPosterCache.h` etc., add it.)

- [ ] **Step 5: Build_check gate**

Run: `cmd.exe //C ".\build_check.bat"`
Expected: `BUILD OK`.

If `BUILD FAILED`:
- "undefined reference to `comicProgressKeyForPath`" → missing or misnamed `#include "comic_progress_key.h"` in ComicReader.cpp.
- "comic_progress_key.h: No such file or directory" → the file wasn't created at the right path, or CMake didn't reconfigure. Try `cmake --build out --target Tankoban` once to force re-configure.
- Anything else → read the last 30 lines of `out/_build_check.log` and fix.

### Task 2: Phase 1 RTC

- [ ] **Step 1: Append the Phase 1 RTC line to `agents/chat.md`**

Append (fill in actual local time):

```
READY TO COMMIT - [Agent 1, TANKOYOMI_CONTINUE_READING Phase 1 — promote itemIdForPath SHA1 contract to shared free helper 2026-05-15 ~HH:MMam/pm. New src/ui/readers/comic_progress_key.h with inline `comicProgressKeyForPath(QString cbzPath) → 20-char hex SHA1 prefix` free function. ComicReader::itemIdForPath at :1671-1674 now delegates (single-line body). Eliminates silent-drift risk between ComicReader's progress-save call site and the new ComicsPage::ensureTankoyomiChapterInMap (Phase 2) which writes to the same map under the same key. CMakeLists.txt updated to include the new header. BUILD OK first try. Inline forward-compat note in the header: helper stays even if Comics-mode pivots to Tankoyomi-exclusive (it's the persistence-side contract, not a map-side artefact).] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/readers/comic_progress_key.h, src/ui/readers/ComicReader.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 2 — Add `ComicsPage::ensureTankoyomiChapterInMap` helper

**Goal:** A private method that takes a cbz path and, if it's inside a Tankoyomi-claimed series folder and not already in `m_progressKeyMap`, inserts an entry mapping the SHA1 key to `{cbzPath, canonicalSeriesPath, record.coverPath}`. Pure data-layer helper — no UI side effects.

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (add a private method declaration)
- Modify: `src/ui/pages/ComicsPage.cpp` (add the helper implementation; add `#include "comic_progress_key.h"` if not already present)

### Task 3: Declare the helper in ComicsPage.h

- [ ] **Step 1: Add the declaration**

Open `src/ui/pages/ComicsPage.h`. Find the existing `m_progressKeyMap` member declaration (line 139 — `QMap<QString, FileRef> m_progressKeyMap;`). The private method declarations should sit in the same `private:` section. Find a suitable spot in that section (near `seriesInfoFromRecord` at `:116` if present, or near other related private methods).

Add:

```cpp
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — just-in-time population of
    // m_progressKeyMap when a Tankoyomi-origin chapter is about to be
    // read (called from the openComicRequested slot before forwarding
    // to MainWindow). No-op if cbzPath is already in the map or if
    // cbzPath isn't inside any Tankoyomi-claimed series folder.
    void ensureTankoyomiChapterInMap(const QString& cbzPath);
```

(Place it as a `private:` method — match the surrounding access specifier. If unsure, grep for `seriesInfoFromRecord` and add right after that line.)

### Task 4: Implement the helper in ComicsPage.cpp

- [ ] **Step 1: Add `#include "../readers/comic_progress_key.h"` to ComicsPage.cpp**

Open `src/ui/pages/ComicsPage.cpp`. Find the existing include block at the top. Verify the path needed — `src/ui/pages/ComicsPage.cpp` to `src/ui/readers/comic_progress_key.h` is `../readers/comic_progress_key.h`. Add:

```cpp
#include "../readers/comic_progress_key.h"
```

If the file already includes `ComicReader.h` somewhere (it might, for the open-comic emission machinery), and that header transitively includes `comic_progress_key.h`, the include is still good to keep explicit per "include what you use".

- [ ] **Step 2: Implement `ensureTankoyomiChapterInMap`**

In `src/ui/pages/ComicsPage.cpp`, find `seriesInfoFromRecord` (the helper at `:819-832`). Add the new implementation IMMEDIATELY AFTER it (i.e. between `seriesInfoFromRecord`'s closing `}` at `:832` and `onTileClicked` at `:834`):

```cpp
void ComicsPage::ensureTankoyomiChapterInMap(const QString& cbzPath)
{
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — bridge between today's
    // two-origin Continue-Reading model (folder-imported scanner walk +
    // Tankoyomi library records) and a future Tankoyomi-exclusive model.
    //
    // m_progressKeyMap normally gets populated only on a full library
    // rescan (via addSeriesTile walking each series' cbz files). A
    // chapter downloaded mid-session sits in canonicalSeriesPath on
    // disk but its progress key is unknown to refreshContinueStrip
    // until the next scan. This helper registers the path right before
    // the user reads it (called from the openComicRequested slot), so
    // ComicReader's saveProgress writes under a key the strip can
    // resolve at its very next refresh.
    //
    // Forward-compat: if Comics mode pivots to Tankoyomi-exclusive and
    // refreshContinueStrip is refactored to join MangaDownloadIndex ↔
    // JsonStoreBridge directly (no intermediate map), this helper +
    // m_progressKeyMap both become deletable.

    if (cbzPath.isEmpty() || !m_tyLibrary) return;

    const QString progressKey = comicProgressKeyForPath(cbzPath);
    if (m_progressKeyMap.contains(progressKey)) return;

    const QString parentDir = QFileInfo(cbzPath).absolutePath();
    const auto rec = m_tyLibrary->getByCanonicalPath(parentDir);
    if (!rec) return;  // not a Tankoyomi-claimed cbz — folder-imported
                       // path handled at scan-time by addSeriesTile.

    m_progressKeyMap[progressKey] = {cbzPath, rec->canonicalSeriesPath, rec->coverPath};
}
```

- [ ] **Step 3: Build_check gate**

Run: `cmd.exe //C ".\build_check.bat"`
Expected: `BUILD OK`.

Likely failure modes:
- "no member named 'getByCanonicalPath'" → verify against `src/core/manga/ComicsTankoyomiLibrary.h`. The method should exist per the smoke evidence's `m_tyLibrary->getByCanonicalPath` usage at `ComicsPage.cpp:1075`. If different, adapt the call shape.
- "no type named 'FileRef'" in `m_progressKeyMap[progressKey] = {...}` → check `ComicsPage.h` for the FileRef struct definition (it's the `m_progressKeyMap`'s value type). The brace-init order MUST match the struct member order — re-check `ComicsPage.h:139` and adjust if needed.

### Task 5: Phase 2 RTC

- [ ] **Step 1: Append the Phase 2 RTC line to `agents/chat.md`**

Append:

```
READY TO COMMIT - [Agent 1, TANKOYOMI_CONTINUE_READING Phase 2 — ComicsPage::ensureTankoyomiChapterInMap helper 2026-05-15 ~HH:MMam/pm. New private method on ComicsPage that takes a cbz path, computes its SHA1 progress key via comicProgressKeyForPath (Phase 1 helper), and if the key isn't already in m_progressKeyMap AND the cbz's parent dir matches a Tankoyomi-claimed canonicalSeriesPath via m_tyLibrary->getByCanonicalPath, inserts {cbzPath, canonicalSeriesPath, record.coverPath} into the map. Folder-imported cbzs (parent not Tankoyomi-claimed) fall through silently — handled by addSeriesTile at scan time. Inline comment block documents the just-in-time-bridge intent + the forward-compat exit ramp for a future Tankoyomi-exclusive Comics mode. No wiring yet — Phase 3 connects this helper to the openComicRequested slot. BUILD OK first try.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 3 — Wire `ensureTankoyomiChapterInMap` into the `openComicRequested` slot

**Goal:** Call the Phase 2 helper from ComicsPage's existing slot that listens to `ComicsTankoyomiDetailView::openComicRequested`. The map entry must be in place BEFORE `openComic` is emitted upward to MainWindow → ComicReader, so by the time ComicReader's `saveProgress` runs, the map already knows about this cbz.

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` (extend the slot wiring at `:125`)

### Task 6: Replace the direct signal-to-signal forward with a slot

The current wiring at `ComicsPage.cpp:125-126` is a direct signal-to-signal connect:

```cpp
connect(m_tyDetailView, &ComicsTankoyomiDetailView::openComicRequested,
        this, &ComicsPage::openComic);
```

It forwards `openComicRequested(cbzPath, list, title)` straight to `ComicsPage::openComic` (which is itself a signal that MainWindow listens to). We need to insert one method call between them.

- [ ] **Step 1: Replace the direct forward with a lambda that calls the helper first**

In `src/ui/pages/ComicsPage.cpp`, find lines 125-126:

```cpp
connect(m_tyDetailView, &ComicsTankoyomiDetailView::openComicRequested,
        this, &ComicsPage::openComic);
```

Replace with:

```cpp
connect(m_tyDetailView, &ComicsTankoyomiDetailView::openComicRequested,
        this,
        [this](const QString& cbzPath, const QStringList& cbzList, const QString& seriesName) {
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — register the Tankoyomi
    // cbz in m_progressKeyMap before forwarding to MainWindow. Must
    // happen synchronously here (same-thread direct connection) so
    // that by the time ComicReader::saveProgress writes the first
    // progress entry, refreshContinueStrip can resolve the SHA1 key.
    ensureTankoyomiChapterInMap(cbzPath);
    emit openComic(cbzPath, cbzList, seriesName);
});
```

The lambda captures `this`, calls the Phase 2 helper, then emits the existing `openComic` signal with the same parameters — preserving the MainWindow listener's contract exactly.

- [ ] **Step 2: Verify `openComic` is actually a signal (not a slot)**

The replacement is `emit openComic(...)`. This is only valid if `openComic` is declared as a signal in `ComicsPage.h`. The pre-fix code used `this, &ComicsPage::openComic` as the connect's slot, which Qt accepts for both signals and slots (signals are slots too). Run:

```bash
grep -n "openComic" src/ui/pages/ComicsPage.h
```

Confirm `openComic(QString, QStringList, QString)` is listed under a `signals:` block. If it's declared under `public:` (i.e. a regular method, not a signal), the lambda needs to call `openComic(cbzPath, cbzList, seriesName)` WITHOUT the `emit` keyword.

Expected pre-existing declaration (signal form):
```cpp
signals:
    void openComic(const QString& filePath,
                   const QStringList& cbzList,
                   const QString& seriesName);
```

If you find this shape, keep `emit openComic(...)`. If it's a non-signal method, drop `emit`.

- [ ] **Step 3: Build_check gate**

Run: `cmd.exe //C ".\build_check.bat"`
Expected: `BUILD OK`.

Likely failure modes:
- "passing 'const ComicsPage' as 'this' argument of 'void emit'" → the lambda is missing `this` capture or the `emit` keyword on a non-signal. Re-read Step 2.
- "no matching function for call to 'ComicsPage::openComic'" → parameter types in the lambda's emit call don't match the signal's declared parameter types. Copy the signal's parameter types from `ComicsPage.h` exactly.

### Task 7: Phase 3 RTC

- [ ] **Step 1: Append the Phase 3 RTC line to `agents/chat.md`**

Append:

```
READY TO COMMIT - [Agent 1, TANKOYOMI_CONTINUE_READING Phase 3 — wire ensureTankoyomiChapterInMap into openComicRequested slot 2026-05-15 ~HH:MMam/pm. Replaced the direct signal-to-signal forward at ComicsPage.cpp:125-126 (`connect(detailView, &openComicRequested, this, &ComicsPage::openComic)`) with a lambda that calls ensureTankoyomiChapterInMap(cbzPath) BEFORE forwarding via emit openComic(cbzPath, cbzList, seriesName). Synchronous direct connection means the map entry is in place before ComicReader's first saveProgress writes. Folder-imported chapters bypass the helper's body (parent dir not Tankoyomi-claimed → no-op) so this introduces zero behaviour change for non-Tankoyomi reads. BUILD OK first try.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 4 — Tankoyomi subtitle branch in `refreshContinueStrip`

**Goal:** When the Continue strip renders a tile for a Tankoyomi-origin entry, use title = series name (e.g. "Berserk") and subtitle = `"<ChapterName> • Page X/Y"`. Folder-imported tiles keep today's `Page X/Y` subtitle. The provenance chip wiring at `:1075-1076` is already in place — only title/subtitle formatting changes.

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (add private static helper declaration)
- Modify: `src/ui/pages/ComicsPage.cpp` (add helper impl + branch in `refreshContinueStrip`)

### Task 8: Declare the labels helper

- [ ] **Step 1: Add the declaration**

Open `src/ui/pages/ComicsPage.h`. Find the `ensureTankoyomiChapterInMap` declaration added in Phase 2 Task 3. Add immediately after it:

```cpp
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — produces the Continue tile's
    // title + subtitle for a Tankoyomi-origin entry. Title is the series
    // name (record.title), subtitle is "<ChapterName> • Page X/Y" derived
    // from the cbz filename + the saveProgress JSON's page/pageCount.
    // Static because it has no ComicsPage state dependencies — keeps it
    // unit-testable in principle and easy to relocate if the Tankoyomi-
    // exclusive pivot happens later.
    struct ContinueLabels {
        QString title;
        QString subtitle;
    };
    static ContinueLabels continueLabelsForRecord(const ComicsLibraryRecord& rec,
                                                  const QString& cbzPath,
                                                  int page,
                                                  int pageCount);
```

Verify that `ComicsLibraryRecord` is already included in `ComicsPage.h`'s include block. If not, add `#include "../../core/manga/ComicsLibraryRecord.h"` near the existing manga-side includes. (Check what's already included by searching `ComicsPage.h` for `ComicsLibraryRecord` or `ComicsTankoyomiLibrary` — one of them likely already pulls the header in.)

### Task 9: Implement the labels helper

- [ ] **Step 1: Add the implementation**

In `src/ui/pages/ComicsPage.cpp`, find the `ensureTankoyomiChapterInMap` implementation added in Phase 2 Task 4. Add immediately after it (before `onTileClicked`):

```cpp
ComicsPage::ContinueLabels ComicsPage::continueLabelsForRecord(
    const ComicsLibraryRecord& rec, const QString& cbzPath, int page, int pageCount)
{
    // TANKOYOMI_CONTINUE_READING 2026-05-15 — Title = series name (rec.title).
    // Subtitle = "<ChapterName> • Page X/Y". Chapter name comes from the
    // cbz filename (e.g. "Prologue 1.cbz" → "Prologue 1"), which is the
    // sanitised chapter name MangaDownloader writes to disk at write-time.
    // For chapter names containing characters in `[<>:"/\\|?*]` (sanitised
    // to `_`), this gives a near-display-quality result; the rare case
    // where the original name is meaningfully nicer (e.g. "Ep. 5: Crisis"
    // vs on-disk "Ep. 5_ Crisis") is acceptable display loss for v1.
    const QString chapterName = QFileInfo(cbzPath).completeBaseName();
    const QString pageLabel = pageCount > 0
        ? QStringLiteral("Page %1/%2").arg(page + 1).arg(pageCount)
        : QStringLiteral("Page %1").arg(page + 1);
    return {
        rec.title,
        chapterName.isEmpty() ? pageLabel
                              : QStringLiteral("%1 • %2").arg(chapterName, pageLabel)
    };
}
```

- [ ] **Step 2: Branch on Tankoyomi-origin inside `refreshContinueStrip`**

In `src/ui/pages/ComicsPage.cpp`, find `refreshContinueStrip` at `:999`. Specifically lines 1035-1040 (the title + subtitle + items.append block):

```cpp
QString title = ScannerUtils::cleanMediaFolderTitle(QFileInfo(ref->filePath).completeBaseName());
QString subtitle = pageCount > 0
    ? QString("Page %1/%2").arg(page + 1).arg(pageCount)
    : QString("Page %1").arg(page + 1);

items.append({updatedAt, ref->filePath, ref->seriesPath, title, subtitle, ref->coverPath});
```

Replace with:

```cpp
// TANKOYOMI_CONTINUE_READING 2026-05-15 — Tankoyomi-origin entries get
// `<ChapterName> • Page X/Y` subtitle with title = series name. Folder-
// imported keeps the historical `Page X/Y` shape with title = file basename.
// The chip badge for Tankoyomi is rendered later (line ~1075) via the same
// m_tyLibrary->getByCanonicalPath lookup; this branch only handles labels.
QString title;
QString subtitle;
if (const auto rec = m_tyLibrary
                         ? m_tyLibrary->getByCanonicalPath(ref->seriesPath)
                         : std::nullopt) {
    const auto labels = continueLabelsForRecord(*rec, ref->filePath, page, pageCount);
    title = labels.title;
    subtitle = labels.subtitle;
} else {
    title = ScannerUtils::cleanMediaFolderTitle(QFileInfo(ref->filePath).completeBaseName());
    subtitle = pageCount > 0
        ? QString("Page %1/%2").arg(page + 1).arg(pageCount)
        : QString("Page %1").arg(page + 1);
}

items.append({updatedAt, ref->filePath, ref->seriesPath, title, subtitle, ref->coverPath});
```

Verify the `getByCanonicalPath` return type. It's likely `std::optional<ComicsLibraryRecord>` based on the existing usage at `:1075-1076` (`if (m_tyLibrary && m_tyLibrary->getByCanonicalPath(item.seriesPath))`). Adjust the `if (const auto rec = ...)` pattern to match — for example, if it returns a raw pointer, use `if (const auto* rec = m_tyLibrary->getByCanonicalPath(...))`. Confirm by reading `src/core/manga/ComicsTankoyomiLibrary.h` once before this step.

- [ ] **Step 3: Build_check gate**

Run: `cmd.exe //C ".\build_check.bat"`
Expected: `BUILD OK`.

Likely failure modes:
- "no member named 'title' in 'ComicsLibraryRecord'" → check `src/core/manga/ComicsLibraryRecord.h` for the actual field name. It's almost certainly `title` per the existing smoke evidence's `"title": "Berserk"` in the JSON dump.
- `getByCanonicalPath` return-type mismatch → adjust the `if (const auto rec = ...)` pattern per the actual signature.

### Task 10: Phase 4 RTC

- [ ] **Step 1: Append the Phase 4 RTC line to `agents/chat.md`**

Append:

```
READY TO COMMIT - [Agent 1, TANKOYOMI_CONTINUE_READING Phase 4 — Tankoyomi-origin subtitle/title branch in refreshContinueStrip 2026-05-15 ~HH:MMam/pm. New ComicsPage::continueLabelsForRecord static helper produces {title=rec.title, subtitle="<ChapterName> • Page X/Y"} for Tankoyomi-origin entries — chapter name derived from the cbz filename (sanitised display, acceptable per spec §Components). refreshContinueStrip's existing title/subtitle construction at :1035-1040 wrapped in an if/else branching on m_tyLibrary->getByCanonicalPath(ref->seriesPath): Tankoyomi-origin → use continueLabelsForRecord; folder-imported → keep historical Page X/Y shape with cleanMediaFolderTitle on the file basename. Provenance chip wire at :1075-1076 unchanged — still fires for Tankoyomi-origin tiles via the same getByCanonicalPath check. Folder-imported tiles behave EXACTLY as before. BUILD OK first try.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 5 — In-app smoke verification (MCP-driven, Agent 1's lane)

**Goal:** Validate the 6 smoke matrix rows in the spec §"Testing" against a live Tankoban build. Agent 1 drives via `tankoctl` + `pywinauto-mcp`; Hemanth visual-judges only the final tile rendering.

**Files:** none (verification only)

### Task 11: Claim MCP LOCK + launch Tankoban

- [ ] **Step 1: Claim MCP LOCK in `agents/chat.md`**

Append:

```
MCP LOCK — Agent 1, TANKOYOMI_CONTINUE_READING Phase 5 smoke, 2026-05-15 ~HH:MMam/pm. Driving via tankoctl + pywinauto-mcp. Walking the 6-row smoke matrix in docs/superpowers/specs/2026-05-15-tankoyomi-continue-reading-design.md §"Testing". Will release at close.
```

- [ ] **Step 2: Approve pywinauto-mcp automation for 30 minutes**

Call `mcp__pywinauto-mcp__approve_automation` with `duration_minutes: 30`.

- [ ] **Step 3: Kill any stale Tankoban + launch**

Run:
```bash
taskkill //F //IM Tankoban.exe 2>&1
taskkill //F //IM ffmpeg_sidecar.exe 2>&1
```

Then launch via background:
```bash
cmd.exe //C ".\build_and_run.bat"
```
(Use `run_in_background: true` on the Bash tool call.)

- [ ] **Step 4: Wait for tankoctl ping**

Run:
```bash
until out/tankoctl.exe ping 2>/dev/null; do sleep 4; done
```

Expected: ping reply with `"schema":"tankoban.dev.v1"`.

### Task 12: Smoke row 1 — fresh download → read first page → return to library → tile present

Pre-condition: the existing Berserk record from prior 2026-05-15 smoke session is in `comics_library.json` with at least one downloaded chapter at `Berserk (WeebCentral)/Prologue 1.cbz`.

- [ ] **Step 1: Get Tankoban window handle**

Call `mcp__pywinauto-mcp__automation_windows` with `{operation: "find", title: "Tankoban", partial: false}`. Capture the returned handle.

- [ ] **Step 2: Confirm Berserk record + downloaded chapter exist**

Run:
```bash
cat "/c/Users/Suprabha/AppData/Local/Tankoban/data/comics_library.json" | python -m json.tool | head -8
ls "/c/Users/Suprabha/Desktop/Media/Comics/Berserk (WeebCentral)/" | head -5
```

Expected: at least one record with `"sourceId": "weebcentral"`, and `Prologue 1.cbz` (~45 MB) present on disk.

If not present, do an Add + chapter download first via the search → click Berserk → Add to library → click chapter download flow from the prior smoke session.

- [ ] **Step 3: Open Berserk via library search → detail view**

Click the search bar at screen coords (951, 130):
```
mcp__pywinauto-mcp__automation_mouse: {operation: "click", x: 951, y: 130}
```

Type "berserk":
```
mcp__pywinauto-mcp__automation_keyboard: {operation: "type", text: "berserk"}
```

Press Enter:
```
mcp__pywinauto-mcp__automation_keyboard: {operation: "press", key: "enter"}
```

Wait 6 seconds for search results, then click the first manga result tile at screen coords (155, 271):
```
mcp__pywinauto-mcp__automation_mouse: {operation: "click", x: 155, y: 271}
```

Wait 8 seconds for fetchDetail + render.

- [ ] **Step 4: Click chapter download indicator on Prologue 1 row**

Wait — chapter is already downloaded, so the click on the indicator at (250, 682) should fire `openDownloadedChapter` and emit `openComicRequested`, which the new lambda handles by calling `ensureTankoyomiChapterInMap` and then forwarding to MainWindow.

```
mcp__pywinauto-mcp__automation_mouse: {operation: "click", x: 250, y: 682}
```

Wait 5 seconds for the reader to load.

- [ ] **Step 5: Advance one page in the reader, then exit**

Press Right arrow once:
```
mcp__pywinauto-mcp__automation_keyboard: {operation: "press", key: "right"}
```

Wait 2 seconds, then press Escape to exit the reader:
```
mcp__pywinauto-mcp__automation_keyboard: {operation: "press", key: "escape"}
```

Wait 2 seconds.

- [ ] **Step 6: Screenshot the library and verify the Continue tile appears**

```
mcp__pywinauto-mcp__automation_visual: {operation: "screenshot", output_path: "agents/audits/smoke_evidence/0090_cr_task1_tile_present.png"}
```

Read the screenshot. Verify:
- Continue Reading strip is visible at top of Comics page
- A tile with the Berserk cover appears in the strip
- Tile title reads "Berserk"
- Tile subtitle reads "Prologue 1 • Page 2/N" (where N is the chapter's page count)
- A `[Tankoyomi]` chip is visible on the tile

If the tile doesn't appear: dump `m_bridge->allProgress("comics")` via the JSON store (`cat "/c/Users/Suprabha/AppData/Local/Tankoban/data/comic_progress.json" | python -m json.tool` — adjust path if different) and confirm the entry was written. If the entry is present in JSON but no tile renders, `m_progressKeyMap` didn't pick it up — re-check Phase 2's helper logic.

### Task 13: Smoke row 2 — click Continue tile → resume to saved page

- [ ] **Step 1: Inspect UIA tree to find the new Continue tile's screen rect**

Call `mcp__pywinauto-mcp__automation_elements` with `{window_handle: <handle from Task 12>, operation: "list", auto_id: "QApplication.MainWindow.QWidget.Content.QStackedWidget.comics.FadingStackedWidget.ComicsGridScroll.qt_scrollarea_viewport.ComicsGridPage", max_depth: 8}`.

Look for a child under `ComicsGridPage` named something like `ContinueStrip` or matching pattern from the existing TileStrip with `setMode("continue")` (per `ComicsPage.cpp:298`). Inside, find the first tile child — capture its screen rect (left, top, right, bottom). Click target = center.

- [ ] **Step 2: Click the Continue tile**

```
mcp__pywinauto-mcp__automation_mouse: {operation: "click", x: <center.x>, y: <center.y>}
```

Wait 5 seconds.

- [ ] **Step 3: Verify reader opened at the saved page (not page 1)**

Take a screenshot:
```
mcp__pywinauto-mcp__automation_visual: {operation: "screenshot", output_path: "agents/audits/smoke_evidence/0091_cr_task2_resume.png"}
```

Check for the page indicator at the bottom of the reader (typical format `2 / 45` or similar) and verify it reads page 2 (NOT page 1).

If page 1 instead: the resume mechanism isn't picking up the saved progress. Compare ComicReader's open-time progress lookup (`ComicReader.cpp:1032`'s `m_bridge->progress("comics", itemIdForPath(cbzPath))`) against what was written. Should be the same key per the Phase 1 helper.

Exit reader (Escape) after verification.

### Task 14: Smoke row 3 — finish chapter → tile evicts

- [ ] **Step 1: Open the Continue tile (click from current library state) and skip to the last page**

Click the Continue tile again to reopen. In the reader, press End or hold Right arrow until you reach the last page. ComicReader sets `finished=true` when the user navigates past or onto the last page (per the existing `saveProgress` write at `ComicReader.cpp:1727` + `:4000`).

Easiest: press the End key once (if ComicReader supports it) or PageDown many times. Or send `right` 50 times (chapters are typically 30-50 pages).

```
mcp__pywinauto-mcp__automation_keyboard: {operation: "press", key: "right", presses: 60, pause: 0.1}
```

Wait 3 seconds for the last-page progress to write.

- [ ] **Step 2: Exit reader and return to library**

Press Escape. Wait 2 seconds.

- [ ] **Step 3: Verify Continue tile no longer appears**

Screenshot:
```
mcp__pywinauto-mcp__automation_visual: {operation: "screenshot", output_path: "agents/audits/smoke_evidence/0092_cr_task3_evict.png"}
```

Verify the Continue Reading strip either disappears entirely (if no other CR entries exist) or no longer contains the Berserk tile.

If the tile still shows: read the JSON progress store; `finished` should be `true` on the Berserk Prologue 1 entry. If `finished` is missing or false, `ComicReader` didn't write the finished flag — that's a ComicReader bug, not a TANKOYOMI_CONTINUE_READING bug. Document and proceed.

### Task 15: Smoke row 4 — two partial chapters of same series → only latest shows

Pre-condition: at least 2 downloaded chapters (e.g. Prologue 1 + Prologue 2). If only Prologue 1 is downloaded, download Prologue 2 first via the detail view's chapter row indicator.

- [ ] **Step 1: Read 2 pages of Prologue 1 (re-open if needed)**

- [ ] **Step 2: Read 2 pages of Prologue 2**

- [ ] **Step 3: Verify only Prologue 2 appears in Continue strip**

Screenshot + read tile subtitle. Should read "Prologue 2 • Page 2/N", not "Prologue 1 • Page 2/N". (Dedup at ComicsPage.cpp:1049-1054 keeps most-recently-updated per seriesPath.)

If both tiles show: the per-series dedup isn't keying on Tankoyomi-origin tiles correctly. Re-check that both entries have the same `ref->seriesPath` value (both point to `Berserk (WeebCentral)`).

### Task 16: Smoke row 5 — app restart with in-progress Tankoyomi chapter

- [ ] **Step 1: Note the current Continue tile state (should show Prologue 2 from Task 15)**

- [ ] **Step 2: Kill Tankoban and relaunch**

```bash
taskkill //F //IM Tankoban.exe 2>&1
cmd.exe //C ".\build_and_run.bat"
```
(background launch)

```bash
until out/tankoctl.exe ping 2>/dev/null; do sleep 4; done
```

Wait an additional 4 seconds for the scan-on-launch to complete.

- [ ] **Step 3: Verify Continue tile reappears**

Screenshot + verify the Prologue 2 Berserk tile is in the Continue strip post-restart with the same subtitle.

This validates that the scan-time addSeriesTile path (which walks `Berserk (WeebCentral)/` for cbz files) correctly populates the map for previously-read chapters, even though Phase 2's helper didn't fire this session.

### Task 17: Smoke row 6 — mixed library (folder-imported + Tankoyomi)

Pre-condition: one folder-imported series with in-progress reading (e.g. Kingdom v06) AND one Tankoyomi series (Berserk Prologue 2) — both should already be visible from prior tasks.

- [ ] **Step 1: Read 2 pages of Kingdom v06 (or any other in-progress folder-imported series visible in the strip)**

Click the Kingdom Continue tile, advance 2 pages, exit.

- [ ] **Step 2: Verify BOTH tiles appear in the strip**

Screenshot. Both Kingdom AND Berserk tiles should be visible in the Continue Reading strip.

- [ ] **Step 3: Verify each tile uses its origin's subtitle convention**

- Kingdom tile: title = file basename ("Kingdom v06" or similar), subtitle = "Page X/Y", NO `[Tankoyomi]` chip.
- Berserk tile: title = "Berserk", subtitle = "Prologue 2 • Page X/Y", `[Tankoyomi]` chip visible.

This is the key parity check — confirms the Tankoyomi-origin branch in Phase 4's `refreshContinueStrip` change doesn't disturb folder-imported behaviour.

### Task 18: Cleanup + release MCP LOCK

- [ ] **Step 1: Kill Tankoban + sidecar per Rule 17**

```bash
taskkill //F //IM Tankoban.exe 2>&1
taskkill //F //IM ffmpeg_sidecar.exe 2>&1
```

- [ ] **Step 2: Append MCP LOCK RELEASED to `agents/chat.md`**

Append:

```
MCP LOCK RELEASED — Agent 1, TANKOYOMI_CONTINUE_READING Phase 5 smoke complete 2026-05-15 ~HH:MMam/pm. All 6 smoke matrix rows from docs/superpowers/specs/2026-05-15-tankoyomi-continue-reading-design.md §"Testing" walked. Evidence at agents/audits/smoke_evidence/009{0,1,2,3,4,5}_cr_*.png. Folder-imported + Tankoyomi-origin mixed library tile rendering verified — origin-specific subtitle conventions hold, provenance chip wire honoured.
```

(If any smoke row failed, append the failure description here instead and STOP the plan execution.)

---

## Phase 6 — ARC-CLOSE RTC

**Goal:** Single bundled RTC capturing the entire 4-phase arc + smoke verification, ready for Agent 0's `/commit-sweep`.

**Files:** none (chat.md only)

### Task 19: Append the ARC-CLOSE RTC

- [ ] **Step 1: Append to `agents/chat.md`**

Append:

```
READY TO COMMIT - [Agent 1, TANKOYOMI_CONTINUE_READING ARC COMPLETE 2026-05-15 ~HH:MMam/pm. Closes the post-merger gap surfaced 2026-05-15 morning: a Tankoyomi-downloaded chapter now appears in the Comics CONTINUE READING strip the moment the user starts reading it. 4-phase arc: P1 promoted itemIdForPath SHA1 contract to shared free helper (src/ui/readers/comic_progress_key.h); P2 added ComicsPage::ensureTankoyomiChapterInMap private helper that registers a Tankoyomi cbz in m_progressKeyMap just-in-time on read-start; P3 wired the helper into the openComicRequested slot via a lambda intercepting the signal-to-signal forward; P4 added ComicsPage::continueLabelsForRecord static helper producing "<ChapterName> • Page X/Y" subtitle + series-name title for Tankoyomi-origin Continue tiles (folder-imported tiles unchanged). Phase 5 6-row smoke matrix all GREEN: fresh-read-tile-appears, click-resumes-to-saved-page, finish-evicts-tile, dedup-per-series, restart-restores-tile, mixed-library-parity. Spec at docs/superpowers/specs/2026-05-15-tankoyomi-continue-reading-design.md. Plan at docs/superpowers/plans/2026-05-15-tankoyomi-continue-reading.md. Forward-compat: factored helpers + inline comments document the exit ramp if Comics-mode pivots to Tankoyomi-exclusive (Hemanth's gated-future-decision). No persistence schema changes. Total footprint: ~50 LOC across 3 files + 1 new header. BUILD OK across all 4 build_check cycles.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify, /superpowers:requesting-code-review, /simplify] | files: src/ui/readers/comic_progress_key.h, src/ui/readers/ComicReader.cpp, src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, CMakeLists.txt, docs/superpowers/specs/2026-05-15-tankoyomi-continue-reading-design.md, docs/superpowers/plans/2026-05-15-tankoyomi-continue-reading.md, agents/audits/smoke_evidence/009{0,1,2,3,4,5}_cr_*.png, agents/chat.md
```

---

## Out of scope (deferred per spec §"Out of scope")

- Read indicators on chapter rows in the detail view
- "X chapters read" badge on library tiles
- Per-chapter cover thumbnails (uses series hero per spec)
- Auto-advance to next chapter on read-completion (matches folder-imported behaviour, which today does not auto-advance)
- Auto-download next chapter
- File-existence check in `refreshContinueStrip`'s resolver loop (pre-existing gap affecting folder-imported too)
- Tankoyomi-exclusive Comics-mode pivot — gated on Hemanth's nyaa.si vs WeebCentral scan-quality test
- BooksPage analogue — if the same gap exists in Books, address separately

## Self-Review

**1. Spec coverage:**
- §"Locked product decisions" 1-7: all reflected in code/comment behaviour (scope minimal in §"Out of scope"; trigger via openComicRequested in P2/P3; subtitle in P4; finish via existing filter; click via existing tile-handler; cover via record.coverPath; dedup unchanged). ✅
- §"Architecture" Approach A: P2+P3 implement just-in-time registration. ✅
- §"Components" 1-4: continueLabelsForRecord (P4 Task 8/9), ensureTankoyomiChapterInMap (P2 Task 3/4), slot extension (P3 Task 6), cover-path source via helper (P2 Task 4 Step 2). ✅
- §"Data flow" steps 1-6: each step has a corresponding smoke row in Phase 5. ✅
- §"Error handling + edge cases": same-cbz-twice (P2 helper's `contains` guard), non-Tankoyomi cbz (P2 helper's `if (!rec) return` fallthrough), empty coverPath (no special case — TileCard default handles), file deleted race (called out as out-of-scope), restart race (validated by P5 Task 16). ✅
- §"Forward-compatibility": inline comments in Phase 1 header (helper stays) + Phase 2 helper (bridge intent + exit ramp) + Phase 4 helper (static, easy relocate). ✅
- §"Testing" 1-6: each row mapped to a Phase 5 task. ✅
- §"Estimated implementation footprint": 30-50 LOC + a header touch — actual plan's edits stay within that envelope. ✅

**2. Placeholder scan:** No "TBD", "TODO", "fill in later". The "fill in actual local time for `~HH:MMam/pm`" instruction in each RTC step is an executor-side action, not a planning gap.

**3. Type consistency:**
- `comicProgressKeyForPath(const QString&) → QString` declared in P1 Task 1, used in P2 Task 4. ✅
- `ensureTankoyomiChapterInMap(const QString&)` declared P2 Task 3, defined P2 Task 4, called P3 Task 6. ✅
- `continueLabelsForRecord(const ComicsLibraryRecord&, const QString&, int, int) → ContinueLabels` declared P4 Task 8 (with nested `ContinueLabels` struct), defined P4 Task 9, called P4 Task 9 Step 2. ✅
- `FileRef` struct (existing in ComicsPage.h:139) — assumed `{filePath, seriesPath, coverPath}` shape per the existing brace-init at `:611`. Plan flags the verify-and-adjust step. ✅
- `m_tyLibrary->getByCanonicalPath(QString) → std::optional<ComicsLibraryRecord>` — assumed shape, plan flags the verify-and-adjust step (P4 Task 9 Step 2). ✅

**4. Scope sanity:** 4 src-touching phases + 1 smoke phase + 1 RTC phase. 7 unique src/ files modified (counting the new header). Single implementation plan, well-bounded.
