# WeebCentral Scraper Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to walk this in-session task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Brotherhood conventions OVERRIDE the skill's default TDD/`git commit` shape — see Phase 0 conventions.

**Goal:** Patch the two P0 WeebCentral scraper bugs found during the 2026-05-15 smoke walkthrough so the COMICS_TANKOYOMI_STREAM_MERGER arc's detail-view + chapter-download flow works end-to-end against weebcentral.com.

**Architecture:** Three focused edits, each in one file. (1) `WeebCentralScraper::fetchDetail` prepends `BASE` to relative `preview.url` before issuing the HTTP request. (2) `WeebCentralScraper::parseChaptersHtml` strips embedded `<svg>` + CSS + "Last Read" badge + ISO date residue from chapter names. (3) `MangaDownloader` chapter-write path stops adding a duplicate series-name subfolder under canonicalSeriesPath. Re-smoke from Task 6 onwards of the smoke plan to verify the cascade-downstream P1 findings (synopsis/genres/date col) self-resolve.

**Tech Stack:** C++20 / Qt6 (QNetworkRequest, QUrl, QRegularExpression), `out/tankoctl.exe` (dev bridge), `pywinauto-mcp` (UIA + visual), Bash for filesystem checks. Build via `build_check.bat` and `build_and_run.bat`.

---

## Phase 0 — Conventions + Pre-flight

**Brotherhood conventions (OVERRIDE skill defaults):**
- Work on master directly. **No worktrees, no branches.** (feedback_no_worktrees.md)
- **Do NOT run `git commit` / `git add` / `git rm`.** Agent 0 sweeps RTC lines from chat.md.
- `build_check.bat` per phase. Invoke via `cmd.exe //C ".\build_check.bat"`. Tail = `BUILD OK` or `BUILD FAILED exit=<n>`.
- chat.md gets ONE RTC line at the END of each phase that ships src/ touches. Phase 4 (re-smoke) gets a NOTE line + the final ARC-CLOSE-ish RTC.
- Don't touch `.claude/telemetry/skill-discipline.jsonl` — system hook.
- Smoke is Agent 1's lane via MCP (Hemanth is hands-off per CLAUDE.md "HEMANTH'S ROLE"). Use `pywinauto-mcp` + `tankoctl` — no asking Hemanth to drive.

### Task 0: Pre-flight — verify working-tree state

**Files:** none (verification only)

- [ ] **Step 1: Confirm we're on master with a clean-ish working tree**

Run:
```bash
git status --short | head -30
git log --oneline -5
```

Expected: master branch. If smoke-session RTCs from 2026-05-15 haven't been swept by Agent 0 yet, that's fine — we'll work on top of them. If Agent 0 already swept them, the working tree should be clean except for telemetry.

- [ ] **Step 2: Confirm no stale Tankoban process is holding files**

Run:
```bash
taskkill //F //IM Tankoban.exe 2>&1
taskkill //F //IM ffmpeg_sidecar.exe 2>&1
```

Ignore "not found" errors. This avoids stale-EXE locks during build_check.

- [ ] **Step 3: Re-read the smoke findings md**

Path: `agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md`.

The exact symptoms + evidence pointers for both P0s live in §"Findings → Phase 1 → S-P0-1" and §"Findings → Phase 1 → S-P0-2".

---

## Phase 1 — Fix P0-S1: WeebCentralScraper::fetchDetail URL absolutising

**Files:**
- Modify: `src/core/manga/WeebCentralScraper.cpp:301` (the `makeRequest(QUrl(preview.url), ...)` line inside `fetchDetail`)

**Context** (verified by reading the file at plan-author time):
- `BASE = "https://weebcentral.com"` is defined at line 11.
- `parseSearchHtml` (lines 56-195) captures `r.url = hm.captured(1)` from the `hrefRe` regex (lines 67-68). The regex pattern `(?:https?://[^/"]+)?(/series/...)` makes the protocol+host portion OPTIONAL and NON-CAPTURING, so capture group 1 is ALWAYS the relative `/series/...` path regardless of whether the live HTML emitted absolute or relative URLs.
- `fetchDetail` passes `QUrl(preview.url)` directly to QNetworkRequest → URL has no scheme → "Protocol \"\" is unknown" error fires.

`ReadComicsScraper::fetchDetail` is the reference pattern — it composes its URL by prepending its own BASE constant. WeebCentral should do the same.

- [ ] **Step 1: Edit `WeebCentralScraper.cpp` fetchDetail URL construction**

Open `src/core/manga/WeebCentralScraper.cpp`. Find line ~299-301:

```cpp
void WeebCentralScraper::fetchDetail(const MangaResult& preview)
{
    QNetworkRequest req = makeRequest(QUrl(preview.url), /*isHtmx=*/false);
```

Replace with:

```cpp
void WeebCentralScraper::fetchDetail(const MangaResult& preview)
{
    // preview.url is always the relative "/series/{ULID}/{slug}" path captured
    // by parseSearchHtml's hrefRe (capture group 1 strips any absolute prefix).
    // Resolve against BASE so QNetworkRequest sees a fully-qualified https URL.
    // Smoke 2026-05-15 P0-S1: pre-fix this line passed a schemeless QUrl,
    // tripping "Protocol \"\" is unknown" on every fetchDetail dispatch.
    const QUrl full = QUrl(BASE).resolved(QUrl(preview.url));
    QNetworkRequest req = makeRequest(full, /*isHtmx=*/false);
```

Everything else in `fetchDetail` (lines 302-368) stays unchanged.

- [ ] **Step 2: Build_check gate**

Run: `cmd.exe //C ".\build_check.bat"`
Expected: `BUILD OK`.

If `BUILD FAILED`, read the 30-line cl.exe tail. Most likely cause would be a typo in the new lines — fix and rerun.

- [ ] **Step 3: Append RTC line to `agents/chat.md`**

Append:
```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER post-smoke P0-S1 fix — WeebCentralScraper::fetchDetail URL absolutising 2026-05-15 ~HH:MMam/pm. preview.url is the relative "/series/{ULID}/{slug}" path captured by parseSearchHtml's hrefRe (capture group 1 strips any absolute prefix); fetchDetail's pre-fix `makeRequest(QUrl(preview.url), false)` passed a schemeless QUrl to QNetworkRequest, surfacing as "weebcentral fetchDetail: Protocol "" is unknown" on every detail-view open. Post-fix wraps via `QUrl(BASE).resolved(QUrl(preview.url))` so the QNetworkRequest sees a fully-qualified https URL. Mirrors ReadComicsScraper::fetchDetail's URL composition (which already prepends its own BASE). Cascade-downstream effects expected to self-resolve once verified during re-smoke: P1 synopsis blank, P1 genres blank, P1 heroCoverUrl empty in library record (the renderer + cache-writer paths are correct; they were just receiving empty payloads because fetchDetail's HTTP request never landed). BUILD OK first try.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/core/manga/WeebCentralScraper.cpp, agents/chat.md
```

Fill in actual local time for `~HH:MMam/pm` (GMT+5:30).

---

## Phase 2 — Fix P0-S2 part A: WeebCentralScraper chapter-name parser

**Files:**
- Modify: `src/core/manga/WeebCentralScraper.cpp:214-253` (the `parseChaptersHtml` function)

**Context** (verified by reading the file at plan-author time):
- Lines 221-223 define `chapterRe` matching `<a href="/chapters/{id}">CONTENT</a>` with `DotMatchesEverythingOption`, capturing the FULL inner content as group 2.
- Lines 233-235 trim, strip-tags via `<[^>]*>` regex, trim again. **The strip-tags regex only removes tag delimiters — it leaves all TEXT NODES between tags intact.** That's why an `<a>` containing nested `<span>Prologue 1</span><span>Last Read</span><svg><style>.st0 { fill: #d3d629; }</style></svg><time>2024-09-07T17:04:15.717343Z</time>` ends up as the concatenation `Prologue 1\n            \n... Last Read \n... .st0 { fill: #d3d629; } \n... 2024-09-07T17:04:15.717343Z` after the strip.
- The garbage propagates into the chapter filename via MangaDownloader, which leads to the corrupted `canonicalPath` observed in `manga_downloads_index.json` during smoke.

**Fix strategy**: BEFORE the strip-tags pass, remove the contents of nested `<svg>...</svg>` AND `<style>...</style>` blocks entirely (those are where the CSS leaks from). Also remove "Last Read" badge text and ISO-8601 timestamp strings, since those aren't part of the chapter name. After that the remaining strip-tags pass cleans the rest, and trim collapses whitespace.

Optional bonus: capture the ISO date into `ch.dateUpload` (a field that exists on `ChapterInfo` per the Stream-pattern struct shape — verify by reading `MangaScraper.h` if needed; if absent, defer that bonus).

- [ ] **Step 1: Edit `parseChaptersHtml` chapter name + date extraction**

Open `src/core/manga/WeebCentralScraper.cpp`. Find the body of `parseChaptersHtml` (around line 214-253). Replace the middle of the function — specifically the block starting `ch.name = m.captured(2).trimmed();` through `ch.source = source;` (lines 233-236) — with a more aggressive cleanup pass:

```cpp
        // Smoke 2026-05-15 P0-S2: parseChaptersHtml's pre-fix name extraction
        // ran `m.captured(2).trimmed()` → strip-tags `<[^>]*>` → trimmed(), which
        // removed tag delimiters but left ALL text nodes between tags intact.
        // The chapter <a> on weebcentral contains nested <span>name</span>,
        // <span>Last Read</span> badges, an <svg> icon with embedded <style>
        // CSS (".st0 { fill: #d3d629; }"), and a <time> ISO timestamp. All of
        // those concatenated into ch.name, which then leaked into the chapter
        // filename via MangaDownloader and produced the 0-byte unreadable
        // `Prologue 1\n...{ fill_ #d3d629; }...2024-09-07T17_04_15Z.cbz`.
        // Post-fix: strip <svg>...</svg> + <style>...</style> blocks entirely
        // (kill the CSS source), capture + remove the ISO timestamp into
        // ch.dateUpload, remove the literal "Last Read" badge text, THEN run
        // the tag-delimiter strip + whitespace collapse.
        QString rawInner = m.captured(2);

        // Kill nested svg + style blocks (DotMatchesEverythingOption is already
        // set on chapterRe; the local regex needs the same flag).
        static QRegularExpression svgBlockRe(
            R"RX(<svg\b[^>]*>[\s\S]*?</svg>)RX");
        static QRegularExpression styleBlockRe(
            R"RX(<style\b[^>]*>[\s\S]*?</style>)RX");
        rawInner.remove(svgBlockRe);
        rawInner.remove(styleBlockRe);

        // Capture + remove ISO-8601 timestamp (with optional fractional seconds).
        // Bind to ch.dateUpload if the field exists on ChapterInfo; otherwise
        // just drop the matched text. Use captured(0) (the full match) to remove.
        static QRegularExpression isoDateRe(
            R"RX(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z?)RX");
        const auto isoMatch = isoDateRe.match(rawInner);
        if (isoMatch.hasMatch()) {
            ch.dateUpload = isoMatch.captured(0);
            rawInner.remove(isoMatch.captured(0));
        }

        // Drop "Last Read" badge text (case-insensitive guard against future
        // localisation variants is unnecessary — WeebCentral emits this string
        // verbatim in English).
        rawInner.remove(QLatin1String("Last Read"));

        // Strip remaining tag delimiters + collapse runs of whitespace to a
        // single space. The collapse handles all the `\n            ` spacing
        // left behind by HTML indentation that the old code left intact.
        static QRegularExpression chapterTagStripRe(R"re(<[^>]*>)re");
        rawInner.remove(chapterTagStripRe);

        static QRegularExpression wsRunRe(R"re(\s+)re");
        rawInner.replace(wsRunRe, QLatin1String(" "));

        ch.name   = rawInner.trimmed();
        ch.source = source;
```

Notes:
- The local `static QRegularExpression` definitions are safe — they cache per-translation-unit just like the existing `chapterRe` at line 221.
- `ch.dateUpload` is set conditionally. If `ChapterInfo` doesn't have a `dateUpload` field, the build will fail with an "unknown member" error; in that case, replace `ch.dateUpload = isoMatch.captured(0);` with a `// ch.dateUpload = ...;` comment-out (the timestamp removal from the name still happens). Verify by reading `src/core/manga/MangaScraper.h` for the `ChapterInfo` struct definition before editing.

- [ ] **Step 2: Verify `ChapterInfo::dateUpload` field exists (or skip the assignment)**

Run:
```bash
grep -n "dateUpload\|struct ChapterInfo" src/core/manga/MangaScraper.h
```

If `dateUpload` appears as a member of `ChapterInfo`, keep the Step-1 code as-is.
If `dateUpload` doesn't exist, edit Step-1's code to comment out the `ch.dateUpload = ...;` line (leave the timestamp `remove()` call intact — we still want it out of the name).

- [ ] **Step 3: Build_check gate**

Run: `cmd.exe //C ".\build_check.bat"`
Expected: `BUILD OK`.

If `BUILD FAILED` due to `ch.dateUpload`, see Step 2 fallback.
If `BUILD FAILED` for any other reason, read the 30-line tail + fix.

- [ ] **Step 4: Append RTC line to `agents/chat.md`**

Append:
```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER post-smoke P0-S2 part A — WeebCentralScraper::parseChaptersHtml name cleanup 2026-05-15 ~HH:MMam/pm. Pre-fix the strip-tags regex `<[^>]*>` only removed tag delimiters, leaving ALL text nodes between tags concatenated into ch.name — including nested <svg> icon's embedded <style> CSS (".st0 { fill: #d3d629; }"), "Last Read" badge text, and ISO-8601 <time>. The concatenation leaked into MangaDownloader's filename construction and produced the 0-byte unreadable `Prologue 1\n...{ fill_ #d3d629; }...2024-09-07T17_04_15Z.cbz` observed in smoke. Post-fix order-of-ops: (1) remove <svg>...</svg> blocks entirely; (2) remove <style>...</style> blocks entirely; (3) capture ISO-8601 timestamp into ch.dateUpload (or drop if ChapterInfo doesn't have that field) + remove from input; (4) remove "Last Read" literal; (5) strip remaining tag delimiters; (6) collapse all-whitespace runs to single space; (7) trim. The remaining MangaDownloader half of P0-S2 (extra series-name subfolder under canonicalSeriesPath) ships in Phase 3 of this plan. BUILD OK.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/core/manga/WeebCentralScraper.cpp, agents/chat.md
```

---

## Phase 3 — Fix P0-S2 part B: MangaDownloader chapter path construction

**Files:**
- Modify: `src/core/manga/MangaDownloader.cpp` (the chapter-write path construction — exact lines TBD by Phase 3 Step 1)

**Context**:
Smoke evidence at `agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md` §"S-P0-2" shows the observed `canonicalPath` in `manga_downloads_index.json` was:

```
C:/Users/Suprabha/Desktop/Media/Comics/Berserk/Berserk/Prologue 1[garbage].cbz
```

The library record's `canonicalSeriesPath` is `C:/Users/Suprabha/Desktop/Media/Comics/Berserk` (one Berserk). The chapter file is being written at `<canonicalSeriesPath>/<seriesTitle-AGAIN>/<chapter>.cbz` — there's an extra series-name subfolder layer.

Per `ComicsTankoyomiDetailView::onAddRemoveClicked`'s Add path, `canonicalSeriesPath` already encodes the per-series folder (`rootFolder + "/" + seriesFolderName`). So `MangaDownloader` should write directly to `canonicalSeriesPath`, not `canonicalSeriesPath + "/" + seriesTitle`.

This is the cleaner of the two possible fixes. The alternative (changing `canonicalSeriesPath` to NOT include the series folder) would ripple through 5+ call sites (Library record, sidecar location, evictBySeries lookups, scanner skip set, etc.) — much higher blast radius. Don't go there.

- [ ] **Step 1: Locate the chapter-write path construction**

Run:
```bash
grep -n "canonicalSeriesPath\|chapterFilePath\|finalPath\|seriesTitle.*\\.cbz\|seriesPath.*\\.cbz" src/core/manga/MangaDownloader.cpp | head -30
```

Look for the line where the chapter's on-disk path is composed. Likely shape: `QString chapterPath = seriesPath + "/" + seriesTitle + "/" + sanitisedChapterName + ".cbz";` or similar. The redundant `+ "/" + seriesTitle +` is the bug.

If `MangaDownloader` instead receives the series-folder-name as a separate parameter and reconstructs the path internally, the fix point is wherever that internal concat happens.

If the grep doesn't pinpoint, also check:
```bash
grep -n "ensureDir\|QDir.*mkpath\|writeChapter\|saveChapter" src/core/manga/MangaDownloader.cpp | head -20
```

- [ ] **Step 2: Inspect the construction site + understand the flow**

Read the function containing the path construction (likely 20-40 lines around the matched line). Trace:
- Where does `seriesPath` come from? (Should be `record.canonicalSeriesPath` from the library record.)
- Where does the extra `seriesTitle` get appended?
- Is the extra subfolder created intentionally for some isolation reason (e.g., dedup against same-titled series from different sources)? If so, the fix needs to preserve that — only collapse when the parent IS the series folder.

Most likely the code has an unconditional `seriesPath / seriesTitle / chapter.cbz` shape and the fix is to drop the `seriesTitle` segment.

- [ ] **Step 3: Edit the path construction to write directly to canonicalSeriesPath**

Construct the chapter path WITHOUT the extra series-name subfolder. The chapter should land at:

```
<record.canonicalSeriesPath>/<sanitisedChapterName>.cbz
```

NOT:
```
<record.canonicalSeriesPath>/<seriesTitle>/<sanitisedChapterName>.cbz
```

The exact edit depends on Step 1's findings. Typical shape — find code like:

```cpp
const QString chapterDir = QDir(seriesPath).filePath(sanitiseFilename(seriesTitle));
QDir().mkpath(chapterDir);
const QString chapterPath = QDir(chapterDir).filePath(sanitiseFilename(chapterName) + ".cbz");
```

Replace with:

```cpp
// Smoke 2026-05-15 P0-S2 part B: canonicalSeriesPath ALREADY encodes the
// per-series folder (rootFolder + "/" + seriesFolderName per
// ComicsTankoyomiDetailView::onAddRemoveClicked Add path). The pre-fix
// extra `seriesTitle` subfolder produced `<canonicalSeriesPath>/<title>/...`
// (the observed `.../Berserk/Berserk/...` double-Berserk). Write directly
// to canonicalSeriesPath.
QDir().mkpath(seriesPath);
const QString chapterPath = QDir(seriesPath).filePath(sanitiseFilename(chapterName) + ".cbz");
```

If the actual code structure differs from this template, adapt the fix to match — the invariant is "no extra series-name subfolder between canonicalSeriesPath and chapter.cbz".

- [ ] **Step 4: Build_check gate**

Run: `cmd.exe //C ".\build_check.bat"`
Expected: `BUILD OK`.

- [ ] **Step 5: Append RTC line to `agents/chat.md`**

Append:
```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER post-smoke P0-S2 part B — MangaDownloader chapter path construction (drop duplicate series-name subfolder) 2026-05-15 ~HH:MMam/pm. Smoke evidence showed `manga_downloads_index.json` canonicalPath as `C:/Users/Suprabha/Desktop/Media/Comics/Berserk/Berserk/Prologue 1[garbage].cbz` — but `canonicalSeriesPath` in `comics_library.json` was `C:/Users/Suprabha/Desktop/Media/Comics/Berserk` (one Berserk). The pre-fix MangaDownloader path construction added an extra `<seriesTitle>` segment between canonicalSeriesPath and the chapter file, producing the double-Berserk. Post-fix writes directly to canonicalSeriesPath since it already encodes the per-series folder (rootFolder + "/" + seriesFolderName per ComicsTankoyomiDetailView::onAddRemoveClicked Add path). BUILD OK. P0-S2's full surface (parser cleanup in Phase 2 + path fix here in Phase 3) now closed; re-smoke in Phase 4 verifies end-to-end.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify] | files: src/core/manga/MangaDownloader.cpp, agents/chat.md
```

---

## Phase 4 — Re-smoke from Task 6 (validate cascade-downstream P1s self-resolve)

**Files:** none (verification only)

**Goal:** Re-launch Tankoban via `build_and_run.bat`, walk the smoke matrix from Task 6 onwards, confirm the cascade-downstream P1 findings (synopsis/genres blank, date col empty, double-Berserk path, corrupted chapter filename) all clear. Then knock out the deferred tasks (12 source-failure toast, 17 search race, 20-22 sidecar/renamed/empty-root edges) that were blocked.

Walk-through script and expected outcomes per task live at `docs/superpowers/plans/2026-05-15-comics-tankoyomi-merger-smoke.md`. The two key validation tasks are:

- **Task 6** (search "berserk" → click first manga result → verify detail view): post-fix the detail hero should populate synopsis + genres + heroCoverUrl (or fall back to thumbnailUrl). Chapter table date column (col 3) should populate per chapter. Status line on the search-takeover view should say "Done: 2 manga / 1 comics" — NOT "Done with errors. Last: weebcentral fetchDetail: Protocol \"\" is unknown."
- **Task 8** (download a chapter): post-fix the `manga_downloads_index.json` entry's canonicalPath should be `<rootFolder>/Berserk/<chapterName>.cbz` (one Berserk, no garbage in filename). File should exist on disk with non-zero size.

- [ ] **Step 1: Claim MCP LOCK**

Append to `agents/chat.md`:
```
MCP LOCK — Agent 1, COMICS_TANKOYOMI_STREAM_MERGER post-fix re-smoke, 2026-05-15 ~HH:MMam/pm. Driving via tankoctl + pywinauto-mcp + windows-mcp (if available). Validating P0-S1 + P0-S2 cascade-downstream resolution per Phase 4 of `docs/superpowers/plans/2026-05-15-weebcentral-scraper-fixes.md`. Will release at close.
```

- [ ] **Step 2: Launch Tankoban**

Run in background: `cmd.exe //C ".\build_and_run.bat"` (will incrementally rebuild + launch). Wait for `out/tankoctl.exe ping` to succeed.

Verify:
```bash
out/tankoctl.exe ping
out/tankoctl.exe get-state
```

Both should succeed; `activePageId` should be `comics`.

- [ ] **Step 3: Re-run smoke Task 6 — search + detail view**

Via pywinauto-mcp:
1. Click the search bar (AutomationId `LibrarySearch`) at the top of the Comics page.
2. Type `berserk`.
3. Press Enter.
4. Wait 4 seconds for scraper fan-out.
5. UIA-list `ComicsTankoyomiSearchWidget` — confirm two-section split (Manga / Comics).
6. Click the first Manga-section TileCard (the WeebCentral Berserk result).
7. Wait 8-15 seconds for fetchDetail HTTP round-trip + parse.
8. Screenshot the detail view.

Expected: detail view shows cover + title "Berserk" + meta + **NON-BLANK synopsis** + **NON-BLANK genres**. Chapter table populates with rows; **col 3 (Date) shows readable dates**, chapter names in col 2 are clean (just "Prologue 1", "Chapter 1", etc — no CSS/SVG/date residue).

If synopsis/genres are STILL blank: re-check Phase 1 fix landed (`grep "QUrl(BASE).resolved" src/core/manga/WeebCentralScraper.cpp`); also check Tankoban logs (`out/tankoctl.exe logs 40`) for any new error messages — the WeebCentral detail-page HTML structure may have selectors that differ from the pinned regex (per the file's `TODO(smoke-verify)` comment at line 328-329).

If only some chapters render clean and others still have residue: Phase 2's strip ordering may be missing a case — capture the offending raw HTML via curl and refine the regex.

- [ ] **Step 4: Re-run smoke Task 7 — Add to library**

Via pywinauto-mcp:
1. Click `AddRemoveLibraryBtn` on the detail view.
2. Wait 2 seconds.
3. Inspect `comics_library.json`:
```bash
cat "/c/Users/Suprabha/AppData/Local/Tankoban/data/comics_library.json" | python -m json.tool
```

Expected: one record present, `detailCache.synopsis` is NON-EMPTY, `detailCache.genres` is NON-EMPTY array, `detailCache.heroCoverUrl` non-empty (or falls back to `thumbnailUrl`).

If `detailCache.synopsis` is still empty AFTER fetchDetail succeeded (no protocol error in logs), the regex selectors in `fetchDetail` (lines 330-361 of WeebCentralScraper.cpp) don't match the actual WeebCentral detail-page HTML. That's a follow-up — file a P1 in the findings md and tune the regex.

- [ ] **Step 5: Re-run smoke Task 8 — download a chapter**

Via pywinauto-mcp:
1. Click the download arrow on the first chapter row in the detail view (col 1, row 0).
2. Wait 60 seconds for download to complete.
3. Inspect `manga_downloads_index.json`:
```bash
cat "/c/Users/Suprabha/AppData/Local/Tankoban/data/manga_downloads_index.json" | python -m json.tool
```

Expected:
- Entry exists with sourceId/seriesId/chapterId.
- `canonicalPath` is `C:/Users/Suprabha/Desktop/Media/Comics/Berserk/<cleanChapterName>.cbz` — **one Berserk, clean filename**.
- `fileSizeBytes` > 0.

Verify on disk:
```bash
ls -la "/c/Users/Suprabha/Desktop/Media/Comics/Berserk/" | head -10
```

Expected: the chapter .cbz file is directly inside the Berserk folder, NOT in a nested `Berserk/Berserk/` path.

If filename still has garbage: Phase 2 strip ordering missed a case.
If path still has double-Berserk: Phase 3 fix didn't land or has wrong call site.
If file is 0 bytes: scraper's `fetchPages` may also have a relative-URL issue (unlikely — it uses `BASE + ...` internally per line 258). Capture log evidence.

- [ ] **Step 6: Knock out remaining deferred smoke tasks**

Walk the remaining tasks from the smoke plan: 12 (source-failure toast — kill network, search, verify per-source toast), 17 (search stale-result race — rapid double-query), 20 (sidecar rewrite-if-missing — delete sidecar, re-open detail view, verify regenerated), 21 (renamed-folder recovery), 22 (empty-root guard).

Record any new findings in the smoke findings md as a separate "Re-smoke 2026-05-15 second pass" section.

- [ ] **Step 7: Cleanup + release lock**

```bash
taskkill //F //IM Tankoban.exe 2>&1
taskkill //F //IM ffmpeg_sidecar.exe 2>&1
```

Append to `agents/chat.md`:
```
MCP LOCK RELEASED — Agent 1, COMICS_TANKOYOMI_STREAM_MERGER post-fix re-smoke complete, 2026-05-15 ~HH:MMam/pm. P0-S1 + P0-S2 verified clean end-to-end (synopsis/genres populate, chapter filename clean, no double-path). [N] deferred smoke tasks re-run with [M] new findings appended to agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md §"Re-smoke 2026-05-15".
```

- [ ] **Step 8: Final ARC-CLOSE RTC**

Append to `agents/chat.md`:
```
READY TO COMMIT - [Agent 1, COMICS_TANKOYOMI_STREAM_MERGER ARC CLOSE post-smoke 2026-05-15 ~HH:MMam/pm. The 9-phase arc + Trigger D audit (15 findings closed) + 2026-05-15 smoke (2 P0s found in WeebCentralScraper) + this plan's 3 fixes (Phase 1 URL absolutising + Phase 2 chapter-name parser cleanup + Phase 3 MangaDownloader path construction) close the merger end-to-end. Re-smoke verified: search takeover → detail hero with synopsis+genres+cover → Add to library with persisted record + sidecar → click tile from library opens new detail view (audit P0-1 centralized opener) → click chapter download writes clean filename to canonicalSeriesPath + registers in MangaDownloadIndex (audit P0-2) → Remove (keep files) drops record + sidecar + evicts index. Remaining P1/P2 (tile subtitle convention + lowercase source-name in meta) are cosmetic polish for a future sweep — no v1 blockers remain. BUILD OK throughout. Smoke artefacts at agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify, /superpowers:requesting-code-review] | files: agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md, agents/chat.md
```

---

## Out of Scope (defer to a follow-up sweep)

- **P1-3 (library tile subtitle convention)** — "0 issues" vs source-name vs "Tankoyomi" vs empty for Tankoyomi-origin tiles. Design call (Hemanth ratification needed).
- **P2-1 (lowercase "weebcentral" in detail meta line)** — replace `preview.source` with `m_registry->find(preview.source)->sourceName()` in `ComicsTankoyomiDetailView::renderDetailHero` / `renderPreviewHero`. One-line cosmetic.
- **WeebCentral detail-page selector tuning** — if Phase 4 Step 4 finds synopsis/genres regex doesn't match the live HTML, that's a separate scraper-side investigation (curl the page, eyeball the structure, refine the regex). Not a P0 — defer.
- **`fetchPages` URL absolutising** — `WeebCentralScraper::fetchPages` (line 258) already uses `BASE + "/chapters/" + chapterId + "/images"` so it builds an absolute URL by construction. No fix needed unless smoke shows otherwise.

## Self-Review

**1. Spec coverage:**
- P0-S1 (fetchDetail URL): Phase 1. ✅
- P0-S2 part A (chapter name parser): Phase 2. ✅
- P0-S2 part B (path construction): Phase 3. ✅
- Verification + cascade-downstream P1 resolution: Phase 4. ✅

**2. Placeholder scan:** No "TODO"/"TBD"/"add error handling" anywhere. Phase 3 Step 2 names "exact lines TBD" but that's an intentional read-first instruction (the executor must grep MangaDownloader.cpp because I haven't pinned the line number — the plan author hasn't read that file's current state and is being honest about it rather than fabricating a line number).

**3. Type consistency:**
- `MangaResult.url` is the relative-path field captured by `parseSearchHtml` → used by `fetchDetail` (Phase 1).
- `ChapterInfo.name` + `ChapterInfo.dateUpload` + `ChapterInfo.source` — Phase 2 sets all three; verify dateUpload exists in Step 2.
- `ComicsLibraryRecord.canonicalSeriesPath` — Phase 3 writes the chapter file directly to this path, matching the contract that `addPath = rootFolder + "/" + seriesFolderName`.
- `MangaSeriesDetail.{synopsis, genres, heroCoverUrl, year, status, sourceUrl, preview}` — used by Phase 1's expected outcome. Already exists in the struct.

**4. Scope sanity:** 3 fixes, 4 phases, ~30-50 LOC of source edits + a re-smoke pass. One full smoke session for verification. Wall-clock estimate 1-2 hours for the fixes + 30-45 min for the re-smoke.
