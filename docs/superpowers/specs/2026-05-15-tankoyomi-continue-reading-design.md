# Tankoyomi-Origin Continue Reading Integration — Design

**Date:** 2026-05-15
**Author:** Agent 1 (with Hemanth — product decisions ratified during brainstorm)
**Status:** DRAFT — pending Hemanth review of the written spec before `/superpowers:writing-plans` handoff

## Goal

Make a Tankoyomi-downloaded chapter appear in the Comics-page "CONTINUE READING" strip the moment a user starts reading it, with the same visual rules as folder-imported volumes and zero divergence in click-to-resume behaviour. Closes the gap surfaced post-merger smoke 2026-05-15: today, reading a Tankoyomi-downloaded chapter saves progress to the shared JSON store correctly but the strip can't resolve the entry's SHA1 key because `ComicsPage::m_progressKeyMap` only learns about a Tankoyomi cbz on the *next* library rescan after it was downloaded.

## Locked product decisions (brainstorm 2026-05-15)

| # | Decision | Pick |
|---|----------|------|
| 1 | Scope | Continue Reading wire only — no chapter-row read indicators, no tile read-count badges. Strict YAGNI on adjacent polish. |
| 2 | Trigger | A Tankoyomi chapter enters CR when the user *starts reading* (the first `saveProgress` for that cbz path). NOT on chapter-download-complete. |
| 3 | Subtitle | `"<ChapterName> • Page X/Y"` (e.g. `"Prologue 1 • Page 5/45"`). Distinct from folder-imported `"Page X/Y"`. |
| 4 | Finish | Match folder-imported behaviour 1:1 — `ComicReader` sets `finished=true` on last page, the strip's existing `prog.value("finished").toBool()` filter at `ComicsPage.cpp:1021` auto-evicts. No new code needed. |
| 5 | Tile click | Open `ComicReader` directly to the saved page (parity with folder-imported tiles + Stream-mode resume semantics). Uses the existing `openComic` emission shape. |
| 6 | Cover | Series hero cover (`ComicsLibraryRecord::coverPath`) — same image the library tile uses. NOT a per-chapter first-page extract. |
| 7 | Dedup | Most-recently-updated chapter per series — exact parity with the existing per-series dedup at `ComicsPage.cpp:1049-1054`. |

## Architecture

The existing Continue Reading wire is a three-stage pipeline:

1. **Source-of-truth phase** — `m_progressKeyMap: QMap<sha1, {filePath, seriesPath, coverPath}>` gets populated by `ComicsPage::addSeriesTile` walking each series folder's `*.cbz`/`*.cbr`/`*.rar` entries (`ComicsPage.cpp:600-612`). For folder-imported series the walk comes from `LibraryScanner::seriesFound`; for Tankoyomi series it comes from `for (const auto& r : m_tyLibrary->all()) addSeriesTile(seriesInfoFromRecord(r));` at `ComicsPage.cpp:782`. Both origins share the same map.
2. **Progress persistence phase** — `ComicReader::saveProgress` writes `{page, pageCount, finished, updatedAt, ...}` to `m_bridge->saveProgress("comics", SHA1(cbzPath), data)` (`ComicReader.cpp:1727`). Origin-agnostic.
3. **Render phase** — `ComicsPage::refreshContinueStrip` reads `m_bridge->allProgress("comics")`, walks each entry, looks up `m_progressKeyMap[key]` to resolve the cbz file (`ComicsPage.cpp:1028`). If the lookup misses, the entry is skipped. Already chips Tankoyomi-origin tiles via `m_tyLibrary->getByCanonicalPath(item.seriesPath)` at line 1075-1076.

**The gap:** `m_progressKeyMap` is rebuilt only on a full rescan. A chapter downloaded mid-session doesn't appear in the map until the next scan completes. So `saveProgress` writes a JSON entry, but `refreshContinueStrip` can't resolve the SHA1 key → no tile.

**Approach A — just-in-time map registration:** extend `ComicsPage`'s existing slot on `ComicsTankoyomiDetailView::openComicRequested` (already connected at `ComicsPage.cpp:125`) to insert the cbz into `m_progressKeyMap` if not already present. Because `openComicRequested` fires exactly at "user is about to read this chapter", the map is up to date by the time `ComicReader::saveProgress` runs, and the next strip refresh resolves the key.

## Components

### 1. `ComicsPage::ensureTankoyomiChapterInMap(cbzPath)` *(new private helper)*

Signature: `void ensureTankoyomiChapterInMap(const QString& cbzPath);`

Behaviour:
- Compute `progressKey = SHA1(cbzPath.toUtf8()).hex().left(20)`. This must match `ComicReader::itemIdForPath` at `ComicReader.cpp:1671-1674` exactly — the contract is "any cbz path → 20-char hex SHA1 prefix". Plan-side decision: either promote `itemIdForPath` to a free helper in a shared header (e.g. `src/ui/readers/comic_progress_key.h`) and call from both `ComicReader` and `ComicsPage`, or duplicate the 2-line hash inline in `ensureTankoyomiChapterInMap` with a `// must match ComicReader::itemIdForPath` comment. Plan author picks based on broader-impact preferences.
- If `m_progressKeyMap.contains(progressKey)`, return (no-op — covers the case where a scan-time walk already registered the entry, or where the user opens the same chapter twice in one session).
- Else: look up the owning record via `m_tyLibrary->getByCanonicalPath(QFileInfo(cbzPath).absolutePath())`. If no record (path isn't inside any Tankoyomi-claimed series folder), fall through silently — this is a folder-imported cbz and `addSeriesTile` already handled it at scan time.
- Otherwise: insert `m_progressKeyMap[progressKey] = {cbzPath, record->canonicalSeriesPath, record->coverPath}`.

### 2. `ComicsPage` slot on `openComicRequested` *(extend existing connect at line 125)*

Before forwarding the `(cbzPath, cbzList, seriesName)` triple to MainWindow's reader-opening path, call `ensureTankoyomiChapterInMap(cbzPath)`. The map entry is now in place; `ComicReader::saveProgress` will write under a key the strip can resolve.

### 3. `ComicsPage::refreshContinueStrip` — subtitle/title branch for Tankoyomi-origin

Today, lines 1035-1038:

```cpp
QString title = ScannerUtils::cleanMediaFolderTitle(QFileInfo(ref->filePath).completeBaseName());
QString subtitle = pageCount > 0
    ? QString("Page %1/%2").arg(page + 1).arg(pageCount)
    : QString("Page %1").arg(page + 1);
```

Replace with a Tankoyomi-origin branch using a new helper `continueLabelsForRecord(record, cbzPath, page, pageCount)` that returns `{title, subtitle}`:
- Title = `record.title` (series name, e.g. "Berserk")
- Subtitle = `QFileInfo(cbzPath).completeBaseName() + " • " + (pageCount > 0 ? "Page X/Y" : "Page X")` — the cbz filename is the sanitised chapter name (per `MangaDownloader` writing `<sanitisedChapterName>.cbz`).

`refreshContinueStrip` checks `m_tyLibrary->getByCanonicalPath(item.seriesPath)` (the existing chip-routing check at line 1075-1076) — on hit, use `continueLabelsForRecord`; on miss, use today's folder-imported formatting.

### 4. Cover-path source — already correct via the helper in §1

Step 1's helper inserts `coverPath = record->coverPath` (the cached series hero from `MangaPosterCache`, persisted to `manga_posters/<source>_<seriesId>.jpg`). `refreshContinueStrip` uses `item.coverPath` directly when building the `TileCard`. No further change.

## Data flow

```
1. User clicks chapter-download indicator on Tankoyomi detail view
   → MangaDownloader writes Prologue 1.cbz to canonicalSeriesPath
   → MangaDownloadIndex registers {sourceId, seriesId, chapterId} → canonicalPath
   (Continue strip unchanged at this point — chapter not yet read.)

2. User clicks chapter row again to read
   → ComicsTankoyomiDetailView::openDownloadedChapter looks up the cbz path
   → emits openComicRequested(cbzPath, [list of downloaded cbzs], seriesTitle)

3. ComicsPage slot receives openComicRequested
   → ensureTankoyomiChapterInMap(cbzPath) — populates m_progressKeyMap with
     {cbzPath, canonicalSeriesPath, record.coverPath}
   → forwards to MainWindow → opens ComicReader

4. ComicReader runs — user navigates pages
   → saveCurrentProgress fires → m_bridge->saveProgress("comics", SHA1(cbzPath), {page, pageCount, finished, ...})

5. User returns to library (Comics page becomes visible OR refresh triggered)
   → refreshContinueStrip walks m_bridge->allProgress("comics")
   → for the just-saved key: m_progressKeyMap lookup succeeds (registered in step 3)
   → m_tyLibrary->getByCanonicalPath hits → builds tile via Tankoyomi-branch
     {title="Berserk", subtitle="Prologue 1 • Page 5/45", cover=record.coverPath,
      provenance="tankoyomi"}
   → tile rendered with the Tankoyomi chip (existing line 1075-1076 path)

6. User clicks the Continue tile
   → existing tile-click handler (lines 1077-1092) emits openComic(filePath, seriesCbzList, seriesName)
   → MainWindow opens ComicReader at the saved page → instant resume
```

## Error handling + edge cases

- **Same cbz opened twice in one session** — `ensureTankoyomiChapterInMap` short-circuits if key already in map. No duplicate state.
- **cbz inside a non-Tankoyomi folder** (i.e. user manually placed a cbz somewhere that doesn't match any record's canonicalSeriesPath) — helper silently no-ops, falls through to folder-imported handling at the next scan. No crash, no half-registered entry.
- **`record->coverPath` empty** (cover fetch never completed) — strip's `TileCard` falls back to its default placeholder, matching existing folder-imported behaviour when a thumbnail is missing.
- **File deleted between read and strip refresh** (user manually `rm`'d the cbz, or `Remove from library — delete files` ran) — `refreshContinueStrip` doesn't currently check `QFileInfo::exists` on the resolved filePath. This is a pre-existing limitation that affects folder-imported tiles too; out of scope here. If it bites, address in a follow-up by adding an existence check to the resolver loop (~3 LOC).
- **Race: app restart with progress JSON entry but no scan yet** — first scan completes within ~1-2s of launch and `refreshContinueStrip` is called from `onScanFinished` (line 752). The synthetic addSeriesTile path at line 782 populates the map for Tankoyomi records during that scan, so the SHA1 key resolves naturally after first scan completes. No special-case needed.

## Forward-compatibility — Tankoyomi-exclusive Comics-mode future

Hemanth flagged a possible future strategic pivot: Comics mode becomes Tankoyomi-only (Mihon-style), dropping folder-imported entirely. Decision is gated on WeebCentral vs nyaa.si scan quality testing (data-quality decision, orthogonal to this design).

This design is forward-compatible:

- **No persistence schema changes.** The JSON progress store, library record store, download index, and sidecar formats are all untouched. Same for `m_progressKeyMap`'s in-memory shape — it gains entries via a new path but the entry shape is identical.
- **The Tankoyomi-formatting branch in `refreshContinueStrip`** uses a dedicated helper (`continueLabelsForRecord`). In a Tankoyomi-exclusive future, the folder-imported else branch goes away cleanly; the helper becomes the only path.
- **The just-in-time map population** in `ensureTankoyomiChapterInMap` is a bridge between today's two-origin model and the future single-origin model. Inline comment on the function will say so, so the next agent reading the code knows this layer is removable.
- **Replacement path documented in the comment:** if/when the pivot happens, `refreshContinueStrip` can join `m_bridge->allProgress("comics")` directly with `MangaDownloadIndex::entries()` + `ComicsLibraryRecord::canonicalSeriesPath` (no intermediate map), and `ensureTankoyomiChapterInMap` + `m_progressKeyMap` both become deletable.

## Out of scope (defer to future polish)

- Read indicators on chapter rows in the detail view (Hemanth Q1 → minimal)
- "X chapters read" badge on library tiles (Hemanth Q1 → minimal)
- Per-chapter cover thumbnails (Hemanth Q6 → series hero only)
- Auto-advance to next chapter on read-completion (Hemanth Q4 → match folder-imported behaviour, which today does not auto-advance)
- Auto-download next chapter (anti-feature for v1)
- File-existence check in `refreshContinueStrip`'s resolver loop (pre-existing gap that affects folder-imported too; address holistically if it bites)
- Forward-compat code removal (i.e. actually deleting folder-imported support) — gated on Hemanth's nyaa.si vs WeebCentral scan-quality test

## Testing — smoke matrix

Manual MCP-driven smoke after implementation (Agent 1 drives via `tankoctl` + pywinauto-mcp per Rule 19, Hemanth visual-judges only the final tile rendering):

1. **Fresh download → read first page → return to library → tile present.** Download Prologue 1 of Berserk, open chapter, advance one page, exit reader, navigate to Comics root. Continue tile appears with title "Berserk", subtitle "Prologue 1 • Page 2/N", series cover, Tankoyomi chip.
2. **Click Continue tile → resume to saved page.** Click the tile from step 1. Reader opens at page 2 (not page 1).
3. **Finish chapter → tile evicts.** Read to last page (let `finished` flag set). Return to library. Continue tile is gone (existing finished-filter at line 1021 handles it).
4. **Two partial chapters of same series → only latest shows.** Read 2 pages of Prologue 1, then 2 pages of Prologue 2. Return to library. Only one tile shows (most recent), subtitle reflects the latest chapter.
5. **App restart with in-progress Tankoyomi chapter.** Read 2 pages of Prologue 1, kill Tankoban, relaunch. After first scan completes (~1-2s), Continue tile reappears for Prologue 1.
6. **Mixed library — folder-imported + Tankoyomi.** Have Kingdom v06 in-progress AND Berserk Prologue 1 in-progress. Both tiles show in strip, both render correctly with their respective subtitle conventions ("Page X/Y" for Kingdom, "Prologue 1 • Page X/Y" for Berserk), Tankoyomi chip only on Berserk.

No new automated tests — the feature lives entirely in UI glue code; existing manual smoke matrix covers it.

## Estimated implementation footprint

- `src/ui/pages/ComicsPage.h` — 2 new private method declarations (`ensureTankoyomiChapterInMap`, `continueLabelsForRecord`)
- `src/ui/pages/ComicsPage.cpp` — 3 edits: (a) `ensureTankoyomiChapterInMap` implementation (~12-18 LOC including the forward-compat comment), (b) slot-extension to call it before forwarding `openComicRequested` (~2-3 LOC), (c) `refreshContinueStrip` branch for Tankoyomi-origin title/subtitle (~10-15 LOC, splitting into `continueLabelsForRecord` helper)

Total: roughly 30-50 LOC plus a header touch. Single build_check cycle.
