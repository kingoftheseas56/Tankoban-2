# THEATRE_DOWNLOAD_OVERHAUL — Chip Simplification + Tankorent-Only Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Simplify the TheatreDownloadPanel to 4 pack-type chips (All / Complete Series / Multi-Season / Season Pack) and cut Stremio fan-out from its search engine, making the panel a dedicated Tankorent-indexer pack viewer.

**Architecture:** Two-lane separation — Torrentio (Stremio) feeds the existing Sources sidebar + powers the primary Download button's per-episode auto-dispatch; Tankorent indexers (the custom 5-source scraper: PirateBay/1337x/YTS/EZTV/ExtTorrents) become the EXCLUSIVE data source for the Layers-3 pack panel. Drops source-axis chip row (now redundant), drops "Single Episode" pack-type chip (covered by primary Download button), strips Stremio fan-out from UnifiedPackSearchEngine in-place (engine name kept; class becomes Tankorent-only internally).

**Tech Stack:** Qt6 C++ (signals/slots, QWidget child-removal, QPushButton chip filter). Existing UnifiedPackSearchEngine retains its public interface (search/packResults/searchComplete) — internal Stremio path stripped.

**Scope:** ~80-120 LOC across 3 files. Behavior change: panel never displays Stremio addon results anymore (live in Sources sidebar exclusively). UnifiedPackSearchEngine becomes effectively single-source — the "Unified" name is slightly misleading after this pass but acceptable (no other consumers; class is internal).

---

## File Structure

**Files modified:**
- `src/ui/pages/stream/TheatreDownloadPanel.cpp` — drop source chip row construction, drop Single Episode chip from typeOptions, drop m_sourceFilter usage at 4 sites
- `src/ui/pages/stream/TheatreDownloadPanel.h` — drop m_sourceFilter member declaration
- `src/core/stream/UnifiedPackSearchEngine.cpp` — strip Stremio fan-out internals (StreamAggregator::load subscription + Stream→TorrentResult mapping). Tankorent path retained.

**Files NOT modified** (deliberately):
- `src/core/stream/UnifiedPackSearchEngine.h` — public interface stays stable (search/packResults/searchComplete). Internal refactor only.
- StreamPickerChoice / TorrentResult / EnrichedPack structs — unchanged.
- The new split-button + Layers-3 UI from prior refinement — unchanged.

---

## Task 1: Investigate consumers + map strip points

**Files:** none modified — investigation only.

- [ ] **Step 1: Verify TheatreDownloadPanel is the sole UnifiedPackSearchEngine consumer**

Run:
```
findstr /S /N /M "UnifiedPackSearchEngine" src
```
Expected: matches in `src/core/stream/UnifiedPackSearchEngine.{h,cpp}`, `src/ui/pages/stream/TheatreDownloadPanel.{h,cpp}`, `src/ui/pages/StreamPage.{cpp}`. If matches appear in unexpected files, surface for review before proceeding.

- [ ] **Step 2: Identify Stremio fan-out entry points in UnifiedPackSearchEngine.cpp**

Run:
```
findstr /N "StreamAggregator::load\|streamsReady\|Stream\\u002A\>\|StreamPickerChoice\|PackSource::Stremio" src\core\stream\UnifiedPackSearchEngine.cpp
```
Expected: identifies the StreamAggregator::load subscription, streamsReady connect, and Stream→TorrentResult mapping helper. These are the strip targets.

- [ ] **Step 3: Identify source-filter references in TheatreDownloadPanel.cpp**

Run:
```
findstr /N "m_sourceFilter\|kDimSource\|All sources\|PackSource::Stremio\|PackSource::Tankorent" src\ui\pages\stream\TheatreDownloadPanel.cpp
```
Expected: 4 hit sites — chip-row construction (lines 222-234), filter dispatch (596-597), rerenderPackList filter application (629-631), status-line condition (674). Confirm line numbers match before Task 5 edits.

- [ ] **Step 4: Confirm baseline build GREEN**

Run: `build_check.bat`
Expected: `BUILD OK` + exit 0.

If Tankoban.exe is running: `taskkill /F /IM Tankoban.exe` first (Rule 1).

- [ ] **Step 5: NO commit at this task**

Investigation-only; no code changes.

---

## Task 2: Strip Stremio fan-out from UnifiedPackSearchEngine

**Files:**
- Modify: `src/core/stream/UnifiedPackSearchEngine.cpp`

After this task, UnifiedPackSearchEngine still emits `packResults` + `searchComplete` (interface unchanged) but internally only listens to `StreamAggregator::packsAvailable` (Tankorent indexer path). The Stream→TorrentResult mapping helper + Stremio subscription are removed.

- [ ] **Step 1: Read the Stremio path in UnifiedPackSearchEngine.cpp**

Open the file. Locate the `search()` method body, then any `connect(...streamsReady...)` lines, the Stream→TorrentResult mapping function (likely named `streamToTorrentResult` or inline), and the `normalizeAndEmit` / dual-source merging path.

The Tankorent-side wiring (around `connect(..., &StreamAggregator::packsAvailable, this, ...)`) is the KEEP path. The Stremio-side wiring (`connect(..., &StreamAggregator::streamsReady, ...)` + `StreamAggregator::load(...)` call) is the STRIP path.

- [ ] **Step 2: Remove the StreamAggregator::load() call in search()**

Find the line in `search()` body that calls `m_streamAggregator->load(...)` (or similar Stremio kick-off). Remove that line + any associated state setup that was specific to the Stremio side.

Keep the `m_streamAggregator->searchPacks(imdbId, showName, season)` call — that's the Tankorent path.

- [ ] **Step 3: Remove streamsReady connect/disconnect in setStreamAggregator() or constructor**

Find the `connect(m_streamAggregator, &StreamAggregator::streamsReady, ...)` line. Remove it. Also remove any corresponding `disconnect(..., streamsReady, ...)` in teardown.

Keep the `connect(m_streamAggregator, &StreamAggregator::packsAvailable, this, &UnifiedPackSearchEngine::onPacksAvailable)` (or similar) — that's the Tankorent path.

- [ ] **Step 4: Remove the Stream→TorrentResult mapping helper**

Find the helper function that converts a Stream (Stremio output) into a TorrentResult (Tankorent format) — likely involves description-regex seeder extraction. Per memory: `description-regex seeder extraction (digits-first form + emoji codepoint fallback, both ASCII-clean via \x{1F464} escape)`.

Remove the helper function entirely. Also remove its call site (likely in `onStreamsReady` or similar — that whole slot can be removed).

- [ ] **Step 5: Add header comment explaining single-source state**

At the top of UnifiedPackSearchEngine.cpp (just after the existing file-header comment), add:

```cpp
// THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 - Stremio fan-out
// stripped. The "Unified" name is now slightly misleading - this engine is
// effectively single-source (Tankorent indexers only). Rationale: the
// Stremio addon results already live in the Sources sidebar (Torrentio
// path); duplicating them inside the pack panel was redundant + confusing.
// The Layers-3 pack panel now serves exclusively as the Tankorent custom-
// scraper viewer (broader result set than Torrentio per Hemanth's curator
// flow). Class name retained to minimize blast radius; rename to
// TankorentPackSearchEngine deferred as a separate polish pass.
```

- [ ] **Step 6: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK` + exit 0.

If linker errors about missing symbols (e.g., streamToTorrentResult): you removed a function but a caller still references it. Re-grep for the function name and remove the dangling call site.

- [ ] **Step 7: NO commit, NO RTC**

Bundled at Task 6.

---

## Task 3: Drop "Single Episode" from type chip list

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp` (around lines 204-211)

- [ ] **Step 1: Locate the typeOptions QStringList**

Run:
```
findstr /N "QStringLiteral(\"Single Episode\")" src\ui\pages\stream\TheatreDownloadPanel.cpp
```
Expected: one match at line 210 (per Task 1 investigation).

- [ ] **Step 2: Remove the Single Episode entry**

The current code reads:
```cpp
        const QStringList typeOptions = {
            QStringLiteral("All"),
            QStringLiteral("Complete Series"),
            QStringLiteral("Multi-Season"),
            QStringLiteral("Season Pack"),
            QStringLiteral("Single Episode"),
        };
```

Remove the `QStringLiteral("Single Episode"),` line. Result:

```cpp
        // THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 - "Single
        // Episode" chip dropped. The primary Download button (post-E1 fast-
        // path restore) already handles per-episode highest-seeded dispatch
        // via onDownloadSeasonClicked / theatreTopSeededDownloadRequested.
        // Surfacing a Single-Episode chip in the pack panel duplicates that
        // entry point + adds visual noise. Per-episode flow lives on the
        // primary button; pack flow lives here.
        const QStringList typeOptions = {
            QStringLiteral("All"),
            QStringLiteral("Complete Series"),
            QStringLiteral("Multi-Season"),
            QStringLiteral("Season Pack"),
        };
```

- [ ] **Step 3: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK` + exit 0.

- [ ] **Step 4: NO commit, NO RTC**

Bundled at Task 6.

---

## Task 4: Drop source chip row construction block

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp` (around lines 220-237)

- [ ] **Step 1: Locate the source chip row block**

Run:
```
findstr /N "Source filter group\|All sources" src\ui\pages\stream\TheatreDownloadPanel.cpp
```
Expected: matches around lines 222-228 (per Task 1 investigation).

- [ ] **Step 2: Remove the source chip block + the spacing separator above it**

Current code (lines 219-236):
```cpp
        chipLayout->addSpacing(10);  // visual separator between dimension groups

        // Source filter group: All sources / Stremio / Indexers.
        const QStringList sourceOptions = {
            QStringLiteral("All sources"),
            QStringLiteral("Stremio"),
            QStringLiteral("Indexers"),
        };
        for (const QString& opt : sourceOptions) {
            auto* chip = makeFilterChip(opt, opt == m_sourceFilter, m_filterChipRow);
            chip->setProperty(kPropDimension, kDimSource);
            chip->setProperty(kPropValue, opt);
            connect(chip, &QPushButton::clicked, this, &TheatreDownloadPanel::onFilterChipClicked);
            chipLayout->addWidget(chip);
        }

        chipLayout->addStretch();
```

Replace with just the trailing stretch:
```cpp
        // THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 - source
        // chip row removed. Stremio results now live exclusively in the
        // Sources sidebar; the pack panel is dedicated to Tankorent's custom
        // indexer scraper (PirateBay/1337x/YTS/EZTV/ExtTorrents fan-out).
        // With Stremio gone, the source-axis filter has only one option
        // (Indexers = everything in the panel) - a single-option filter
        // adds no value. Source chip row dropped; type chip row stretches
        // to fill the chip-row width.
        chipLayout->addStretch();
```

The `chipLayout->addSpacing(10);` line is also removed since there's no second group to separate from anymore.

- [ ] **Step 3: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK` + exit 0.

The build should still pass at this point even though `m_sourceFilter` is unused (Task 5 cleans it up). Unused-member warnings are not errors.

- [ ] **Step 4: NO commit, NO RTC**

Bundled at Task 6.

---

## Task 5: Drop m_sourceFilter member + clean up all references

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.h`
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp`

After Task 4 dropped the chip row, `m_sourceFilter` is still referenced at 4 places in the .cpp (filter dispatch, rerenderPackList filter application, status-line condition). All those references become dead code — clean them up.

- [ ] **Step 1: Find m_sourceFilter declaration in the header**

Run:
```
findstr /N "m_sourceFilter" src\ui\pages\stream\TheatreDownloadPanel.h
```
Expected: one match — the member declaration `QString m_sourceFilter = QStringLiteral("All sources");` or similar.

- [ ] **Step 2: Remove the m_sourceFilter declaration + the kDimSource constant**

In TheatreDownloadPanel.h, remove the `m_sourceFilter` line. The default-initialized member is dead after Task 4.

In TheatreDownloadPanel.cpp, around line 39, remove the `kDimSource` constant:
```cpp
constexpr const char* kDimSource     = "source";
```

Also remove `kPropDimension` / `kPropValue` only if they're now unused (likely still used for the kDimType chips — keep them if so).

- [ ] **Step 3: Remove m_sourceFilter dispatch branch in onFilterChipClicked**

Around line 595-597 in TheatreDownloadPanel.cpp:
```cpp
    if (dim == QLatin1String(kDimType))
        m_typeFilter = val;
    else if (dim == QLatin1String(kDimSource))
        m_sourceFilter = val;
```

Becomes:
```cpp
    if (dim == QLatin1String(kDimType))
        m_typeFilter = val;
```

- [ ] **Step 4: Remove m_sourceFilter filter application in rerenderPackList**

Around lines 627-633 in TheatreDownloadPanel.cpp:
```cpp
        // Source filter: "All sources" matches everything; "Stremio" only
        // matches PackSource::Stremio; "Indexers" only matches PackSource::Tankorent.
        if (m_sourceFilter == QLatin1String("Stremio") && p.source != PackSource::Stremio)
            continue;
        if (m_sourceFilter == QLatin1String("Indexers") && p.source != PackSource::Tankorent)
            continue;
```

Remove all 5 lines including the comment. Source filtering is no longer needed (all packs are Tankorent-origin after Task 2's engine strip).

- [ ] **Step 5: Simplify the status-line condition**

Around line 672-680 in TheatreDownloadPanel.cpp:
```cpp
    if (m_statusLine && !m_packs.isEmpty()) {
        const bool hasFilter = (m_typeFilter != QLatin1String("All"))
                            || (m_sourceFilter != QLatin1String("All sources"));
        if (hasFilter) {
            m_statusLine->setText(tr("%1 of %2 packs match")
                                       .arg(m_filteredPacks.size())
                                       .arg(m_packs.size()));
        } else {
            m_statusLine->setText(tr("%1 packs found").arg(m_filteredPacks.size()));
        }
    }
```

Simplify to just check the type-filter:
```cpp
    if (m_statusLine && !m_packs.isEmpty()) {
        const bool hasFilter = (m_typeFilter != QLatin1String("All"));
        if (hasFilter) {
            m_statusLine->setText(tr("%1 of %2 packs match")
                                       .arg(m_filteredPacks.size())
                                       .arg(m_packs.size()));
        } else {
            m_statusLine->setText(tr("%1 packs found").arg(m_filteredPacks.size()));
        }
    }
```

- [ ] **Step 6: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK` + exit 0.

If unused-member warnings appear for `kPropDimension`/`kPropValue`: they're still used by the kDimType chip-state read on click. Leave them.

If linker error about `m_sourceFilter` undefined: search for any remaining reference and remove.

- [ ] **Step 7: NO commit, NO RTC**

Bundled at Task 6.

---

## Task 6: Final build verify + ASCII check + bundled RTC

**Files:**
- Modify: `agents/chat.md` (append RTC line)

- [ ] **Step 1: Final build verify**

Run: `build_check.bat`
Expected: `BUILD OK` + exit 0.

If Tankoban.exe is running: `taskkill /F /IM Tankoban.exe` first (Rule 1).

- [ ] **Step 2: ASCII discipline check**

Run:
```
powershell -NoProfile -Command "$files = @('src/ui/pages/stream/TheatreDownloadPanel.h', 'src/ui/pages/stream/TheatreDownloadPanel.cpp', 'src/core/stream/UnifiedPackSearchEngine.cpp'); $total = 0; foreach ($f in $files) { $hits = Select-String -Path $f -Pattern 'THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17' -SimpleMatch; foreach ($h in $hits) { $bytes = [System.Text.Encoding]::UTF8.GetBytes($h.Line); $nonascii = ($bytes | Where-Object { $_ -gt 127 } | Measure-Object).Count; if ($nonascii -gt 0) { Write-Output \"$f L$($h.LineNumber): $nonascii non-ASCII bytes\" } } }; Write-Output \"check done\""
```

Expected: no non-ASCII findings on marker lines.

- [ ] **Step 3: Append RTC line to agents/chat.md**

Append this single-line RTC (exact text):

```
READY TO COMMIT - [Agent 4, THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 IST - drop source-axis chip row + Single Episode pack-type chip + Stremio fan-out from UnifiedPackSearchEngine. Two-lane architecture clarified per Hemanth call 2026-05-17: Torrentio (Stremio addons via StreamAggregator::load) stays as the per-episode + alternate-streams data source feeding the Sources sidebar + powering the primary Download button's top-seeded auto-pick; Tankorent's custom 5-indexer scraper (PirateBay/1337x/YTS/EZTV/ExtTorrents via StreamAggregator::searchPacks) becomes the EXCLUSIVE data source for the Layers-3 pack panel. UI: typeOptions now has 4 entries (All / Complete Series / Multi-Season / Season Pack) - "Single Episode" dropped (covered by primary Download button per the split-button refinement that shipped this morning). Source chip row + 10px separator + sourceOptions block all removed (after dropping Stremio, the "Indexers" chip would be the only option - a single-option filter adds no value). m_sourceFilter member + kDimSource constant + 4 reference sites (filter dispatch / rerenderPackList filter application / status-line condition) all cleaned up. Engine: UnifiedPackSearchEngine.cpp strips Stremio fan-out internally - StreamAggregator::load() subscription + Stream-to-TorrentResult description-regex mapping helper + streamsReady connect all removed; Tankorent searchPacks path retained verbatim. Public interface (search / packResults / searchComplete) unchanged - rename to TankorentPackSearchEngine deferred as polish pass. Pack panel now serves exclusively as the Tankorent custom-scraper viewer per [[tankorent-as-foundation-vision]] - broader result set than Torrentio (5 indexers natively vs Torrentio's curated selection). Compile-only verify GREEN. ASCII clean.] | Skills invoked: [/superpowers:writing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/stream/TheatreDownloadPanel.h, src/ui/pages/stream/TheatreDownloadPanel.cpp, src/core/stream/UnifiedPackSearchEngine.cpp, docs/superpowers/plans/2026-05-17-theatre-download-overhaul-chip-simplification.md
```

- [ ] **Step 4: NO git commit**

Agent 0 sweeps RTC lines via `/commit-sweep`. Do not commit manually.

---

## Self-Review

**1. Spec coverage:**

- Drop source chip row entirely → Task 4
- Drop Single Episode chip → Task 3
- Keep Indexers feature (functionality) → Task 2 (Tankorent path retained in engine + panel calls preserved)
- Drop Stremio results from panel → Task 2 (Stremio fan-out stripped from engine)
- 4 final chips (All / Complete Series / Multi-Season / Season Pack) → Tasks 3 + 4 combined produce this final state
- Cleanup of m_sourceFilter references → Task 5
- Build verify after every task → present in each task
- ASCII discipline → Task 6 Step 2
- RTC line → Task 6 Step 3

All design points covered.

**2. Placeholder scan:**

No "TBD", "implement later", "similar to Task N", or "add appropriate handling" instances. Task 2's Stremio-strip steps require the implementer to read the file to identify exact line numbers (the .cpp internals aren't fully shown in this plan because the file is ~150-200 LOC of dual-source orchestration logic and the plan would balloon if it transcribed all of it). Compensation: Task 1 Step 2 has a precise grep that the implementer uses to find the exact line numbers, and Task 2 specifies WHAT to remove semantically (streamsReady connect, Stream→TorrentResult helper, load() call) — those are findable by name.

**3. Type consistency:**

- `m_typeFilter` (QString) — preserved across Tasks 3/4/5
- `m_sourceFilter` (QString) — removed in Task 5
- `kDimType` constant — preserved
- `kDimSource` constant — removed in Task 5
- `kPropDimension` / `kPropValue` — preserved (still used by kDimType chips)
- UnifiedPackSearchEngine class name — preserved across Task 2 (rename deferred)
- `packResults` / `searchComplete` signals — preserved

Names + signatures consistent.

---

## Risk Notes

**Stripped streamToTorrentResult helper might have callers outside UnifiedPackSearchEngine.cpp** (Task 2). The mapping helper was internal to the engine per the Phase B2 memory, but if some other file picked it up via `#include "core/stream/UnifiedPackSearchEngine.h"` and the helper is publicly exposed, Task 2's removal will surface as a linker error. Task 1 Step 1 grep catches this. If found: fall back to making the helper file-static (anonymous namespace) instead of removing — keeps it for the linker but unused.

**Status-line "%1 of %2 packs match" wording** (Task 5 Step 5) might want updating now that source filter is gone — "match" implies a multi-axis filter. Acceptable as-is; can polish later if Hemanth's smoke flags it.

**UnifiedPackSearchEngine.h** is not touched in this plan. If the header declares any helper methods that become unused after Task 2 (e.g., the Stream→TorrentResult mapping helper if it was declared there), the build will succeed with unused-method warnings but won't fail. Polish pass can clean those later. The intent is to keep the .h public interface stable across this refinement.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-17-theatre-download-overhaul-chip-simplification.md`. Two execution options:

**1. Subagent-Driven (recommended)** — Fresh subagent per task with two-stage review. Matches the pattern from this morning's UI refinement that caught two real bugs via reviews.

**2. Inline Execution** — Execute tasks in this session via executing-plans skill with checkpoints.

Recommendation: **Subagent-Driven** per the established pattern.
