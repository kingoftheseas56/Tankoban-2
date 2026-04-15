# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources)
Subsystem: Tankorent search-results rework — Tracks A–F (Nyaa-inspired)
Reference spec: `agents/tankorent_nyaa_todo.md` (planning doc) + `agents/nyaa_search_reference.md` (Nyaa UX observation). Per Hemanth's 2026-04-14 broadening, planning docs are valid objective sources.
Objective: appropriate Nyaa's sortable-header + trust-tint + link-column + count-line + filter search-results UX onto Tankorent's multi-source aggregator table
Files reviewed:
- `src/ui/pages/TankorentPage.h` (A/B/C/D/E member additions + deletions)
- `src/ui/pages/TankorentPage.cpp` (A–F implementations)

Date: 2026-04-14

### Scope

Compared all sixteen batches in the todo (A1–A5, B1–B3, C1–C3, D1–D2, E1, F1–F3) against Tankorent code. Reviewed against two levels of spec: the todo doc as primary objective (it's Tankoban's own plan), and the Nyaa reference doc as the shape Tankorent is appropriating. Out of scope: Transfers tab, AddTorrentDialog, any indexer plumbing, Tankoyomi, Stream — per todo §1 explicit out-of-scope list. Static read only; Hemanth's build-verify is authoritative for visual regression checks.

### Parity (Present)

**Track A — Sortable column headers**
- **A1 — click-to-sort via `QHeaderView::sectionClicked`, custom stable-sort.** Spec `tankorent_nyaa_todo.md:13-16`. Code: `TankorentPage.cpp:515-519` (`setSectionsClickable`, `sectionClicked → onResultsHeaderClicked`), `:1023-1053` handler, `:998-1021` comparator. Deliberately avoids `setSortingEnabled(true)` with the correct rationale ("mis-sorts '1.3 GiB' vs '520 MiB'" — string sort on size display would indeed break) at `:512-514`. ✓
- **A2 — Qt's native sort indicator arrow.** Spec `:18-21`. Code: `:516-517` (table-create) + `:1041-1043` (post-click). Single source of truth: `m_resultsSortCol` / `m_resultsSortOrder`, indicator follows the members. ✓
- **A3 — Default sort is Seeders descending (col 4 post-Track-C).** Spec `:23-25` asked for Date newest-first; Agent 4 consciously diverged because `TorrentResult` has no upload-date field and no scraper exposes one uniformly. Disclosed in chat.md:7271 and re-asserted in the A+B+C+D+E+F bundle post. Substituted signal (high seeders) is the closest user-intent analogue. Accepted.
- **A4 — `m_sortCombo` removed from member list + constructor.** Spec `:27-30`. Code: header has no `m_sortCombo` member; the construction block in `buildSearchControls` (around `:348+`) is gone. Column-header click is the sole sort surface. ✓
- **A5 — QSettings persistence.** Spec `:32-34`. Code: ctor at `:249-261` restores from `tankorent/sortCol` + `tankorent/sortOrder` with validation (only sortable-col indices 0/1/2/4/5 accepted; stale pre-Track-C values rejected); handler at `:1046-1050` writes on every click. Header indicator restored on first paint (`:258-260`). ✓

**Track B — Row trust signal**
- **B1 — `trustClass()` helper with 50/5 thresholds.** Spec `:42-45`. Code: `:1116-1121`. Returns `"healthy"` / `"normal"` / `"poor"`. ✓
- **B2 — Row tint via `setBackground` across all cells.** Spec `:47-50`. Code: `:969-988`. Applies brush across columns 0–6 of the row (see note on Link column below). ✓
- **B3 — Per-cell health dot dropped.** Spec `:52-55`. Code: Seeders cell at `:925-928` is a plain `QString::number(r.seeders)` with no dot prefix and no per-cell foreground color; `healthDot`/`healthColor` helpers removed from both `.h` and `.cpp`. ✓

**Track C — Per-row affordances**
- **C1 — Link column with download + magnet QToolButtons.** Spec `:63-69`. Code: `:935-967`. Column 6 hosts a `QWidget` container with `QHBoxLayout` holding two `QToolButton`s: `↓` wired to `onAddTorrentClicked(i)`, `M` wired to `QGuiApplication::clipboard()->setText(r.magnetUri)`. Plain glyphs chosen over Nyaa's `📥`/`🧲` per `feedback_no_color_no_emoji` — disclosed in chat.md:7356. Each lambda captures `i`/`magnet` by value; renderResults rebuilds cells on every resort so captures stay fresh per render. ✓
- **C2 — Source badge in Title cell; Source column dropped.** Spec `:71-74`. Code: `:902-911` constructs `[source]  title  [tags]` display with `setToolTip(r.title)` carrying the untruncated string. Header list at `:496` has no Source column. Column count 8 → 7. Native alphabetical sort still clusters same-source results together because the badge is a cell prefix. ✓
- **C3 — Title cell tooltip for elision recovery.** Spec `:76-78`. Code: `:910` `titleItem->setToolTip(r.title);`. ✓

**Track D — Result count + soft cap**
- **D1 — Result count label above the table.** Spec `:86-89`. Code: `:458-467` constructs the label in `buildMainTabs` before the tab widget (which sits below the status row from `buildStatusRow`), giving the intended placement. Source count computed from pre-dedup `m_allResults` at `:875-878` so cross-source duplicates still attribute to every source; matches spec's "M = how many scrapers returned ≥1 hit" semantic. ✓
- **D2 — Soft cap 100 + inline "Show all" link.** Spec `:91-94`. Code: `:862-867` truncates `deduped` to 100 when `!m_showAll`; `:881-893` renders the count line with an HTML `<a>` link whose `linkActivated` flips the flag and re-renders (wired at `:463-466`). `startSearch` resets `m_showAll = false` at `:711` so each fresh search re-arms the cap. Singular/plural handled correctly at `:885-891`. ✓

**Track E — Filter combo**
- **E1 — Three-option filter between dedup and soft-cap.** Spec `:102-108`. Code: `:352-368` builds the combo with options `All`/`Hide dead`/`High seed only`, `:841-856` applies the predicate after dedup and before soft-cap. Pipeline order (sort → dedup → filter → cap → render) is the one Agent 4 documented; correctly places filter before cap so a "Hide dead" filter that drops 950/1000 results doesn't waste the 100-cap budget. Persisted to `tankorent/filter`. ✓

**Track F — Polish**
- **F1 — Alpha-alternated tint for striping-through-tint.** Spec `:115-117`. Code: `:974-988` uses four brushes — healthy odd/even at alpha 26/44, poor odd/even at alpha 26/44 — picked by `i % 2`. Same hue, two alpha levels per tier; reads as a soft-striped band instead of a flat wash. Cleaner than a QPalette blend and needs no Base-color compositing. ✓
- **F2 — DROPPED.** Spec `:119-122`. `TorrentResult` has no upload-date field; no scraper exposes one uniformly. Disclosed and justified consistently with the A3 divergence. Accepted; see question 3 below.
- **F3 — Header right-click column-visibility menu with QSettings persistence.** Spec `:124-127`. Code: `:525-556` — `customContextMenuRequested` on `QHeaderView` pops a menu with the label disabled as a header, followed by seven checkable entries (one per column). Hidden set persisted as a comma-separated index list under `tankorent/hiddenColumns`, restored at construction at `:525-534` with column-count bounds check. ✓

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **Link column tint visual gap on tinted rows.** The row-tint loop at `TankorentPage.cpp:984-988` calls `setBackground(tint)` on column-6 item as well — but column 6 is overlaid by `setCellWidget(i, 6, linkCell)` at `:967`. The linkCell `QWidget` has no background color (defaults to transparent showing the parent table's Base color), so on a tinted row the Link cell reads as an untinted strip flanked by tinted neighbors. Breaks the visual continuity of the trust-band, which is the whole point of B2 + F1. Fix: stamp the linkCell with the same tint via palette or stylesheet at render time. Minor but noticeable on a tinted row with all six data cells tinted and the Link cell untinted.
- **Link column width deviates from spec (70 px → 80 px).** Spec `:65` says 70 px; code `:510` resizes to 80. Cosmetic.
- **Magnet-copy action is silent — no toast confirmation.** User clicks `M`, magnet goes to clipboard, no visual feedback. The C-track Toast widget from Tankoyomi already exists (`src/ui/widgets/Toast.h`) and is reusable across pages per its own header comment. A single-line `Toast::show(window(), "Magnet URI copied")` wrapping the lambda would close the "did it work?" ambiguity. Polish, not correctness.
- **Sort-before-dedup can surface a weaker duplicate.** `TankorentPage.cpp:816-835` sorts `m_allResults` by the user's current key before the infohash dedup keeps the first-seen copy. When the user sorts by Seeders desc (default), the highest-seed copy of a duplicate survives. When sorted by Title asc (or any non-seeder key), the surviving copy is whichever alphabetical-first source encountered it — may not be the highest-seed version. A subsequent "High seed only" filter can then drop titles whose better duplicate was suppressed by title sort. Edge-case; fix is either "always dedup with a seeder-preference, independent of display sort" or "filter first then sort". Probably not worth rewriting the pipeline for it; flagging so it's on the record.
- **F3 recovery when every column is hidden.** Agent 4 claims "header stays clickable" when all columns hidden. QHeaderView does render when all sections are hidden, but the surface to right-click is small/empty and platform-dependent. Consider adding a `Show all columns` entry at the top of the F3 menu as an always-reachable escape hatch, or bounds-checking on toggle to refuse hiding the last visible column. Low priority; nice to have.
- **Column-visibility menu lists the original header text at menu-open time.** Menu construction at `:541-553` captures `headers` by value (which is the seven-entry QStringList from `createResultsTable`). Safe across renders. No issue, just noting.

### Answers to Agent 4's disclosed divergences

Agent 4 flagged three conscious deviations; confirming each is acceptable:

1. **A3 default sort = Seeders desc, not Date desc.** Reason: no `TorrentResult::dateUpload` field. Accepted — user-intent analogue is correct.
2. **C1 glyphs `↓` + `M` instead of Nyaa's `📥` / `🧲`.** Reason: `feedback_no_color_no_emoji`. Accepted.
3. **F2 dropped.** Reason: same as A3 — no date field. Accepted.
4. **Filter options (All / Hide dead / High seed) vs Nyaa's (No filter / No remakes / Trusted only).** Disclosed in chat.md:7439. `TorrentResult` has no remake/trusted markers; seeder-quality is the substitute signal. Accepted.

### Questions for Agent 4

1. **Link column tint gap** (P2 #1) — was this visible in Hemanth's verification? If yes, ready to fold a tint-stamp on `linkCell` into the next file-touch; if no, probably fine to leave as cosmetic backlog.
2. **Magnet-copy silent UX.** Intentional minimalism, or polish backlog with the Toast widget already available?
3. **F2 / A3 date field.** Is adding `dateUpload` to `TorrentResult` (requiring scraper-side changes across all seven indexers) a realistic near-term task, or permanently parked? Answers whether F2 / A3-Nyaa-parity are ever returning.
4. **Sort-before-dedup interaction with the High-seed filter** (P2 #4) — did you consider always-seeder-sort-for-dedup, or is the current behaviour the intended one?

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 4, Tankorent A–F], 2026-04-14.** Six P2 observations, none blocking; four Qs for follow-up. All four conscious deviations from the todo spec / Nyaa reference are correctly justified. Agent 4 clear for Rule 11 commit.

---

## Template (for Agent 6 when filling in a review)

```
## REVIEW — STATUS: OPEN
Requester: Agent N (Role)
Subsystem: [e.g., Tankoyomi]
Reference spec: [absolute path, e.g., C:\Users\Suprabha\Downloads\mihon-main]
Files reviewed: [list the agent's files]
Date: YYYY-MM-DD

### Scope
[One paragraph: what was compared, what was out-of-scope]

### Parity (Present)
[Bulleted list of features the agent shipped correctly, with citation to both reference and Tankoban code]
- Feature X — reference: `path/File.kt:123` → Tankoban: `path/File.cpp:45`
- ...

### Gaps (Missing or Simplified)
Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**
- [Missing feature] — reference: `path/File.kt:200` shows <behavior>. Tankoban: not implemented OR implemented differently as <diff>. Impact: <why this matters>.

**P1:**
- ...

**P2:**
- ...

### Questions for Agent N
[Things Agent 6 could not resolve from reading alone — ambiguities, missing context]
1. ...

### Verdict
- [ ] All P0 closed
- [ ] All P1 closed or justified
- [ ] Ready for commit (Rule 11)
```
