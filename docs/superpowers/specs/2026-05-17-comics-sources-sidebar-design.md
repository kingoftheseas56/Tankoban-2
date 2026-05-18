# Comics Sources Sidebar (Stremio-style) — Design Spec

- **Date:** 2026-05-17
- **Author:** Agent 1 (Comic Reader + Tankoyomi)
- **Status:** Brainstorm ratified by Hemanth same-session (3 rounds of batches-of-4 conceptual questions + 1 approach pick + design-overview confirmation). Implementation plan to follow via `/superpowers:writing-plans`.
- **Surface:** `src/ui/pages/comics/ComicsSourceCard.{h,cpp}` (NEW) + `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` (REFACTORED) + `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` (TOUCHED) + `CMakeLists.txt`.
- **Visual companion session:** `.superpowers/brainstorm/1393-1779034877/` (intro mockup at `content/01-intro.html`).

---

## 1. Purpose

Replace the current QListWidget-based `ComicsSourcesPanel` rendering — a single-line text row per source (the "ugly looking distracting WeebCentral synthesized text" Hemanth flagged at the end of last night's smoke session) — with a Stremio-style card layout mirroring Theatre's `StreamSourceCard` + `StreamSourceList` pattern. Adapt the card chips to manga-native data (archive type, uploader trust tier, page count where available) instead of streaming-specific data (HDR/DV/sub badges).

The Stremio-for-manga product vision (`feedback_stremio_for_manga_vibe`) implies the Sources sidebar should feel parallel to Theatre's right-pane Sources panel. The data substrate is already in place — `UnifiedSourceRow` POD, three provider kinds (Catalog / NyaaRuntime / WeebCentralPacker), stable-sort by tier asc + seeders desc — only the render layer changes.

## 2. Scope

### In-scope (v1)

- New file: `src/ui/pages/comics/ComicsSourceCard.{h,cpp}` — Stremio-style card widget. Takes a `UnifiedSourceRow` (existing POD at `ComicsSourcesPanel.h:48-58`).
- Refactor: `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` — swap `QListWidget` for `QScrollArea + QVBoxLayout<ComicsSourceCard>`. Add 4 states (placeholder / loading / populated / empty). Add auto-pick orchestration for tier-1 Catalog rows.
- Touch: `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` — add gold-accent left-edge stripe to the active volume row in `m_volumesTable`.
- `CMakeLists.txt` — register the new source files.
- Smoke matrix: Death Note (Catalog auto-pick), Kingdom (no Catalog → WC + Nyaa), zero-source series (empty state), hover/selected visual states.

### Out of scope (explicit)

1. **New source providers.** Hemanth picked "visual polish only — keep existing 3 source kinds" in Round 1 Q1. MangaUpdates-as-source, Tankoyomi-runtime-per-chapter, addon-class systems all deferred to a future arc.
2. **Refactor of Stream's StreamSourceCard.** No regression risk to Theatre. Approach B (shared SourceCardBase) explicitly rejected.
3. **Right-click context menu.** Hemanth picked "skip entirely" in Round 1 Q4. Left-click downloads. Period.
4. **Cover thumbnail in the source card.** Round 2 Q1. The volume cover is already visible in the table row below — showing it twice is redundant.
5. **Stream-style "saved choice + auto-launch toast" full mirror.** Round 1 Q3 picked auto-pick-top-tier-only — simpler than Stream's saved-choice persistence. No saved-choice writes, no toast cancellation, no per-volume choice memory.
6. **Theme.cpp palette work.** Reuse existing gold-accent tokens. Coordinate with Agent 5 via `agents/chat.md` if a new token is needed.
7. **Unit tests for the card widget.** Per CLAUDE.md (`/superpowers:test-driven-development` opt-in ONLY for `tankoban_tests` pure-logic primitives) — the visual widget is smoke-verified, not unit-tested. The existing AniListVolumeMapperTest 9/9 + MangaUpdatesStatusParserTest 7/7 + MangaUpdatesDisambiguatorTest 5/5 cover the data-layer correctness this card RENDERS.

## 3. Locked decisions

Twelve decisions across the 3 brainstorm rounds, plus the implementation approach pick. Recorded verbatim so the implementer can trace each design choice to its source.

### Round 1 — Scope + vision

1. **v1 scope:** visual polish of existing 3-source surface. No new providers in v1.
2. **Mirror vs adapt:** adapt — manga-native chips (archive type, uploader trust tier, page count where available). Stream's HDR/DV/sub badges don't translate.
3. **Auto-pick behavior:** auto-pick top-tier Catalog rows silently. No saved-choice mechanism. For Nyaa-only volumes (no Catalog hit), always show the picker.
4. **Right-click context menu:** skipped entirely. Left-click = download.

### Round 2 — Card content + states

5. **Cover thumbnail in card:** no. Just the source-initials badge. The volume cover is visible in the table row below.
6. **Source-initials badge text:** derive from source kind — `CT` (Catalog, gold-accent bg) / `NY` (NyaaRuntime, neutral) / `WC` (WeebCentralPacker, dim). Single load-bearing signal.
7. **Loading state:** skeleton cards (2 dim placeholder card silhouettes pulsing softly while Nyaa runtime is in flight).
8. **Empty state:** friendly message + suggested next step. `"No sources found for this volume"` heading + `"Try a different volume or check back as indexers refresh."` subtitle.

### Round 3 — UX flow + density

9. **Auto-pick UX flow:** visible pick + 300ms beat + download fires. User clicks Vol N row → panel populates with Catalog card highlighted in gold-accent for 300ms → `QTimer::singleShot` fires `downloadRequested` automatically. User SEES the pick happen; no surprise, no extra click.
10. **Hybrid loading state:** Catalog + WC render instantly (synchronous code paths). 2 skeleton cards render below for Nyaa. As Nyaa results land they replace skeletons one-by-one.
11. **Card density:** match Stream — ~80px per card. 5-6 cards fit before scroll in the 532×412 panel rect.
12. **Volume-row selection feedback:** 3px gold-accent left-edge stripe on the active row in `m_volumesTable`. Sources panel header echoes `"Volume N"` as text confirmation.

### Implementation approach

Approach A — mirror Stream's pattern. New `ComicsSourceCard.{h,cpp}` (parallel to `StreamSourceCard`) + refactor `ComicsSourcesPanel` from `QListWidget` to `QScrollArea + QVBoxLayout<ComicsSourceCard>` (parallel to `StreamSourceList`). `UnifiedSourceRow` POD stays unchanged.

Approaches B (extract shared `SourceCardBase`) and C (in-place `QStyledItemDelegate` paint) rejected. B has regression risk on Stream's just-shipped THEATRE_DOWNLOAD_OVERHAUL work; C makes hover/click state machines harder to express than widget composition.

## 4. Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│ ComicsSeriesView (existing)                                       │
│ ┌─────────────────────────────────────────────────────────────┐  │
│ │ Title pane: title + meta + synopsis + library button        │  │
│ └─────────────────────────────────────────────────────────────┘  │
│ ┌─────────────────────────────────────────────────────────────┐  │
│ │ m_volumesTable (QTableWidget)                                │  │
│ │ Active row gets 3px gold-accent left-stripe (new in scope)  │  │
│ └─────────────────────────────────────────────────────────────┘  │
│                                                                   │
│ ┌──────────────────────┐  ┌─────────────────────────────────┐    │
│ │ Vol cover, vol#,     │  │ ComicsSourcesPanel (refactored) │    │
│ │ chapter range,       │  │                                  │    │
│ │ progress, status     │  │ State machine:                   │    │
│ │ (table rows)         │  │  - setPlaceholder()              │    │
│ │                      │  │  - setLoading()                  │    │
│ │                      │  │  - setSources(rows, hasMore)     │    │
│ │                      │  │  - setEmpty()                    │    │
│ │                      │  │                                  │    │
│ │                      │  │ Internals (Approach A):          │    │
│ │                      │  │  QScrollArea > QVBoxLayout       │    │
│ │                      │  │   > ComicsSourceCard[*]          │    │
│ │                      │  │   > Skeleton cards (loading)     │    │
│ │                      │  │   > Status label (placeholder /  │    │
│ │                      │  │     empty / error)               │    │
│ └──────────────────────┘  └─────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
```

### File-by-file responsibilities

**`ComicsSourceCard.{h,cpp}` (NEW, ~200 LOC).**
- One Qt widget per source row. Constructed with a `UnifiedSourceRow` value.
- Renders the 4-element card shape (badge + 2-line text + tier pill + chip row).
- Emits `clicked(UnifiedSourceRow)` on left-click anywhere in the card.
- Public setter `setSelected(bool)` for the auto-pick 300ms beat highlight.
- `enterEvent` / `leaveEvent` paint hover state (1px brighter border).
- `mouseReleaseEvent` → emit `clicked` (NOT `mousePressEvent` — F3 lesson learned: mousePressEvent-only widgets reject synthetic mouse_event from MCP smokes; mouseReleaseEvent + accessible metadata is the proven-working pattern).
- No right-click. No `contextMenuEvent` override (Round 1 Q4).
- No QStyledItemDelegate. The widget composes via direct QHBoxLayout + QLabels (mirror of `StreamSourceCard.cpp`).

**`ComicsSourcesPanel.{h,cpp}` (REFACTORED, ~150 LOC delta).**
- Public API surface UNCHANGED: `clear()`, `populate(seriesTitle, anilistSeriesId, vol, chapterIds)`, signal `downloadRequested(...)`. `ComicsSeriesView`'s wiring (existing connect at construction time) needs zero adjustment.
- Internal swap: `QListWidget m_list` → `QScrollArea + QWidget + QVBoxLayout m_cardsLayout`. Mirror of `StreamSourceList.{h,cpp}` structure.
- Four state-transition methods (private; called from `populate` orchestration):
  - `setPlaceholder()` — clears cards + shows `"Select a volume to see sources"` centered status label. Default state on construction + on `clear()`.
  - `setLoading()` — clears cards + renders 2 dim skeleton card silhouettes. Used when Nyaa runtime is in flight AND Catalog + WC haven't rendered yet (rare; usually Catalog/WC populate before Nyaa even fires).
  - `setSources(QList<UnifiedSourceRow> rows, bool nyaaStillInFlight)` — renders one `ComicsSourceCard` per row, sorted via stable_sort. Appends 2 skeleton cards at the bottom IF `nyaaStillInFlight == true`. Triggers auto-pick orchestration if `rows[0].kind == Catalog && rows[0].tier == 1`.
  - `setEmpty()` — clears cards + shows `"No sources found for this volume"` heading + `"Try a different volume or check back as indexers refresh."` subtitle. Only entered when Nyaa has completed AND total source count == 0.
- Auto-pick state: private `bool m_autoPickArmed` set true when a top-tier Catalog row is detected; `QTimer::singleShot(300ms, this, [this]{ emit downloadRequested(...) })` fires the actual dispatch. Prevents double-fire if user clicks the card during the 300ms beat.

**`ComicsSeriesView.{h,cpp}` (TOUCHED, ~30 LOC).**
- Active volume row gets a 3px gold-accent left-edge stripe. Two implementation options (pick one in plan):
  - (a) `QStyledItemDelegate` override on the `m_volumesTable` `#` column, painting a left-edge accent rect when `option.state & QStyle::State_Selected`.
  - (b) QSS pseudo-state on `QTableWidget::item:selected` adding a `border-left: 3px solid #d4a574` style — cleaner CSS, may have row-height jitter issues on selection transitions.
- Sources panel header label (existing `m_sourcesPanel` header text) echoes the active volume: `"Volume 3"` etc. Existing label can be repurposed; new label not needed.
- ComicsSeriesView's connect to `ComicsSourcesPanel::downloadRequested` stays as-is.

**`CMakeLists.txt`.**
- Register `src/ui/pages/comics/ComicsSourceCard.cpp` next to `ComicsSourcesPanel.cpp` in the existing `tankoban_core` (or per the actual current target — confirm in plan) source list.

## 5. ComicsSourceCard widget — visual recipe

80px tall. Three-pane horizontal layout + chip row underneath. Mirror of `StreamSourceCard` proportions.

```
┌────────────────────────────────────────────────────────────────┐
│ [CT]  1r0n — Vol 1 (Tankobon)               [CATALOG]         │
│       Death Note v01 (2003) (Digital) (1r0n).cbz              │
│       ↓ 23 seeders   146 MB   cbz                              │
└────────────────────────────────────────────────────────────────┘
   ^   ^                                          ^
  36×36  Two-line text column (flex-grow)     Tier pill (right)
  badge  title + subtitle                     CATALOG / NYAA / FALLBACK
```

### Elements

**Badge (left, 36×36):**
- Border-radius 6px. Centered text 11px bold.
- Three tiers via background + border + foreground:
  - `Kind::Catalog` → bg `rgba(212,165,116,0.18)` + border `rgba(212,165,116,0.40)` + fg `#d4a574`. Text: `CT`.
  - `Kind::NyaaRuntime` → bg `rgba(255,255,255,0.06)` + border `rgba(255,255,255,0.12)` + fg `rgba(255,255,255,0.65)`. Text: `NY`.
  - `Kind::WeebCentralPacker` → bg `rgba(255,255,255,0.04)` + border `rgba(255,255,255,0.10)` + fg `rgba(255,255,255,0.45)`. Text: `WC`.

**Two-line text column (center, flex-grow):**
- **Title (line 1):** bold 13px `#e5e7eb`. Composed from `UnifiedSourceRow.title`. Stream's title-elision pattern: tooltip on the QLabel carries the full untruncated string (mirror `StreamSourceCard::reelideTitle`).
- **Subtitle (line 2):** regular 11px `rgba(255,255,255,0.55)`. Composed from `UnifiedSourceRow.uploaderHint` plus filename hint OR synthesis note depending on Kind.
  - `Kind::Catalog` → `"<uploaderHint> — <inferred filename from catalog entry>"` (e.g. `"1r0n — Death Note v01 (2003) (Digital).cbz"`).
  - `Kind::NyaaRuntime` → the nyaa result's display title (already populated in `UnifiedSourceRow.uploaderHint` by `NyaaRuntimeSource`).
  - `Kind::WeebCentralPacker` → `"<chapterCount> chapters → vol pack on demand"` (or `"chapters unavailable"` if chapterIds is empty — though that row wouldn't render in the first place).

**Tier pill (right):**
- 11px text, padded 2px 8px, border-radius 4px. Right-aligned in the card.
- Text:
  - `Kind::Catalog` → `CATALOG` (gold-accent colors matching badge).
  - `Kind::NyaaRuntime` → `NYAA` (neutral).
  - `Kind::WeebCentralPacker` → `FALLBACK` (dim).

**Chip row (underneath the 3-pane row, ~16px tall):**
- 11px text, `rgba(255,255,255,0.55)`, space-separated chips.
- Composition (chips silently omit when the source field is missing):
  - `↓ <seeders> seeders` — when `UnifiedSourceRow.seeders > 0`. WC rows skip (seeders == -1).
  - `<size>` — pretty-printed from `UnifiedSourceRow.sizeBytes` (e.g. `"146 MB"`, `"1.4 GB"`). Skip when `sizeBytes == 0`.
  - `cbz` / `cbr` — archive type from filename hint or `UnifiedSourceRow.uploaderHint` parse. Default `cbz` for Catalog + WC (the validated archive type per `PremiumArchiveValidator`).
  - `1r0n` / `Hox` / `VIZ Digital` / `KG Manga` etc. — uploader trust tier badge from `resources/manga_uploader_trust.json` when matched. For Nyaa rows where uploader is unknown, omit.

### States

**Default:** flat. Background `rgba(255,255,255,0.04)` for Nyaa/WC kind, `rgba(255,255,255,0.06)` for Catalog. Border 1px matching.

**Hover (`enterEvent`):** background +0.02 alpha, border +0.04 alpha. Subtle. No scale, no glow, no transform (Stream's pattern).

**Selected (`setSelected(true)` via auto-pick 300ms beat):** 1px gold-accent border (`rgba(212,165,116,0.50)`) + slightly brighter background. Same look as Stream's `setSelected` for saved-choice highlight.

**Skeleton (loading placeholder):** rendered as a separate widget class `ComicsSourceSkeletonCard` (or inline branch in `ComicsSourceCard` with a constructor variant). Dim gray silhouette of the card shape, no text. Optionally pulse animation via `QPropertyAnimation` on opacity (~1.5s loop). Two of these stack below real cards during hybrid loading.

## 6. ComicsSourcesPanel — state machine + data flow

### Public API (unchanged from current)

```cpp
namespace tankoban::manga::comics {

class ComicsSourcesPanel : public QWidget {
    Q_OBJECT
public:
    ComicsSourcesPanel(premium::PremiumCatalog* catalog,
                        NyaaRuntimeSource* nyaa,
                        QWidget* parent = nullptr);

    void clear();
    void populate(const QString& seriesTitle,
                  int anilistSeriesId,
                  const anilist::VolumeRow& vol,
                  const QStringList& chapterIds);

signals:
    void downloadRequested(const UnifiedSourceRow& row,
                           const QString& seriesTitle,
                           int anilistSeriesId,
                           int volumeNumber,
                           const QStringList& chapterIds);

private:
    // ... internals change (see §4) but the public surface stays.
};

} // namespace
```

`ComicsSeriesView`'s existing `connect(m_sourcesPanel, &ComicsSourcesPanel::downloadRequested, this, &ComicsSeriesView::onDownloadDispatch)` stays valid. Zero ComicsSeriesView/ComicsPage wiring delta.

### Data flow on `populate(...)`

1. **Set placeholder.** If `vol.volumeNumber == 0` (no vol selected) or `chapterIds.isEmpty() && no anilist series id` → `setPlaceholder()`. Return.
2. **Synchronous Catalog lookup.** `PremiumCatalog::entryForAnilistIdAndVolume(anilistSeriesId, vol.volumeNumber)`. If hit → append a `UnifiedSourceRow{ Kind::Catalog, tier=1, ... }` to `m_rows`. Map the catalog entry's `magnetUri` + `infoHash` + uploader hint into the row.
3. **Synchronous WC synthesis check.** If `chapterIds.size() > 0` → append `UnifiedSourceRow{ Kind::WeebCentralPacker, tier=99, ... }` to `m_rows`. Synthesis is on-demand — the row represents the capability to pack, not a pre-existing artifact.
4. **Sort.** `std::stable_sort(m_rows.begin(), m_rows.end(), byTierAscSeedersDesc)` — Catalog rises to top, WC sinks to bottom.
5. **Render.** `setSources(m_rows, /*nyaaStillInFlight=*/true)` — renders the rows we already have + 2 skeleton cards below.
6. **Async Nyaa runtime search.** `m_nyaa->search(seriesTitle, vol.volumeNumber, /*requestId=*/m_nextReqId++)`. The result handler `onNyaaResults(reqId, hits)` appends Nyaa rows + re-sorts + re-renders with `nyaaStillInFlight=false`. The failure handler `onNyaaFailed(reqId, reason)` re-renders with `nyaaStillInFlight=false` (no rows added; status logged via `qDebug`, not user-facing).
7. **Empty state check.** After `nyaaStillInFlight=false` re-render: if `m_rows.isEmpty()` → `setEmpty()`. Else stays populated.

### Auto-pick orchestration

After step 5 (initial synchronous render) AND every subsequent re-render: check if `m_autoPickArmed == false && m_rows[0].kind == Kind::Catalog && m_rows[0].tier == 1 && !m_rows[0].magnetUri.isEmpty()`. If yes:
- Set `m_autoPickArmed = true` (single-fire latch — prevents double-pick on subsequent re-renders).
- Call `m_cards[0]->setSelected(true)` — gold-accent highlight on the first card.
- `QTimer::singleShot(300, this, [this]() { emitTopRowDownload(); })`.
- If the user clicks any card during the 300ms beat: cancel the QTimer (track via `QTimer*` member; call `stop()` and reset `m_autoPickArmed = false`). The user's explicit click takes precedence.

`emitTopRowDownload()`:
- Re-verifies `m_rows[0]` still valid + still meets the auto-pick condition (defensive, handles race against late-arriving sort that displaced the catalog row).
- Emits `downloadRequested(m_rows[0], seriesTitle, anilistSeriesId, volumeNumber, chapterIds)`.

Auto-pick is reset to disarmed state on every `populate(...)` (new volume click = new auto-pick decision). Reset via `m_autoPickArmed = false` at the top of `populate`.

## 7. ComicsSeriesView — gold-accent active row stripe

Active volume row in `m_volumesTable` gets a 3px-wide gold-accent left-edge stripe. Two implementation options for the plan to pick:

**Option (a) — QStyledItemDelegate on the `#` column.**
- New file (or new class in ComicsSeriesView.cpp): `VolumesTableRowAccentDelegate : public QStyledItemDelegate`.
- Override `paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index)`.
- If `option.state & QStyle::State_Selected`: `p->fillRect(option.rect.left(), option.rect.top(), 3, option.rect.height(), QColor(212,165,116, 200))`.
- Call base `QStyledItemDelegate::paint(p, option, index)` for the rest.
- Set on the `#` column via `m_volumesTable->setItemDelegateForColumn(0, new VolumesTableRowAccentDelegate(this))`.
- Pros: precise control, survives QSS theme changes.
- Cons: more code, custom painting risk.

**Option (b) — QSS pseudo-state.**
- Add to ComicsSeriesView's root QSS sheet: `QTableWidget#ComicsSeriesVolumesTable::item:selected { border-left: 3px solid rgba(212,165,116,0.80); }`.
- Single CSS rule.
- Pros: trivial diff, theme-token-friendly.
- Cons: Qt's `border-left` on table items has known row-height jitter in some Qt6 versions (border eats height); test on real series view before locking.

Plan should try (b) first. If row-height jitter visible, fall back to (a).

Sources panel header label (already exists in the panel layout — see `ComicsSourcesPanel.cpp::buildUi`) updates to echo `"Volume <N>"` whenever `populate(...)` is called with a non-zero volumeNumber. Single setText line in `populate`.

## 8. Error handling + edge cases

**Nyaa runtime network failure / timeout.**
- `NyaaRuntimeSource` emits `failed(reqId, reason)`.
- ComicsSourcesPanel logs via `qDebug() << "[ComicsSources] nyaa failed:" << reason`.
- Re-renders with `nyaaStillInFlight=false`.
- If `m_rows.isEmpty()` after this → `setEmpty()`. Else stays populated with Catalog + WC.
- No user-facing toast. The user-relevant signal is "did sources appear or not"; the underlying failure mode is plumbing.

**WC synthesis with empty `chapterIds`.**
- The WC row is skipped at step 3 (never appended to `m_rows`). No WC card renders.
- Catalog and Nyaa rows unaffected.

**Catalog `magnetUri` empty / malformed.**
- Auto-pick skipped (`!m_rows[0].magnetUri.isEmpty()` guard). Panel stays populated for manual pick.
- User-clicked dispatch still fires (existing downstream code in `ComicsPage::onDownloadDispatchRequested` validates the magnet — that's the failure boundary).

**Rapid volume-row clicking (user clicks Vol 1, then Vol 2 within 300ms).**
- The QTimer on Vol 1's auto-pick is cancelled in `populate(...)`'s reset block (`m_autoPickTimer->stop()` if alive).
- Vol 2's `populate` runs fresh; its own auto-pick fires if Vol 2 has a Catalog row.
- No double-fire risk.

**Stale Nyaa result (Vol 1 Nyaa fetch returns AFTER user clicked Vol 2).**
- `onNyaaResults(reqId, ...)` checks `reqId == m_currentReqId`. If stale, discard.
- `m_currentReqId` advances on every `populate(...)`.
- Mirror of the existing `m_nextReqId` pattern already in ComicsSourcesPanel today.

**Panel widget destroyed mid-fetch (e.g., user navigates back from series view).**
- ComicsSourcesPanel's destructor cancels `m_autoPickTimer` (`if (m_autoPickTimer) m_autoPickTimer->stop()`).
- The `QNetworkReply` for Nyaa is owned by `NyaaRuntimeSource`, not the panel — it survives the panel's destruction. Its result handler should check `m_sender` validity via `QPointer<ComicsSourcesPanel>` in the lambda capture if cross-object lifetime is at risk. (Existing pattern in current ComicsSourcesPanel — verify in plan.)

**Catalog row present but its magnetUri's torrent has zero seeders.**
- Out of scope for v1. The Catalog row still auto-picks. Downstream `TorrentVolumeProvider` will surface a "no seeds yet, waiting..." status to the volume table row. No per-card seeder health UI in v1.

## 9. Testing strategy

### Unit tests

None. Per CLAUDE.md: TDD opt-in only for `tankoban_tests` pure-logic primitives. `ComicsSourceCard` is a visual widget; `ComicsSourcesPanel` is a stateful container with Qt signal/slot orchestration. Both smoke-verified, not unit-tested.

Existing data-layer test coverage REMAINS in force (no changes to these files):
- `AniListVolumeMapperTest` 9/9 PASS (mapper produces row sets consumed by the volume table; downstream of this design).
- `MangaUpdatesStatusParserTest` 7/7 PASS (resolver fills MediaDetail totals; downstream).
- `MangaUpdatesDisambiguatorTest` 5/5 PASS (resolver picks the right MangaUpdates entry; downstream).

### Smoke matrix (agent-drivable via pywinauto-mcp + tankoctl)

1. **Death Note + Catalog auto-pick.** Open Death Note (anilistId 30021, post-F1 fix the catalog row now lights up). Click Vol 1 row. Verify: gold-accent left-stripe on Vol 1 row; ComicsSourcesPanel populates with `CT 1r0n` card at top (gold-accent border); 300ms beat visible; `downloadRequested` fires; Vol 1 row Status transitions to "Downloading...". Evidence: 2 screenshots (pre-click, mid-300ms-beat).

2. **Kingdom + no Catalog → WC + Nyaa.** Open Kingdom (anilistId 46765, no catalog hit). Click Vol 1 row. Verify: panel populates synchronously with `WC FALLBACK` card + 2 skeleton cards below. ~2-5s later: skeletons replace with Nyaa rows (or disappear if 0 Nyaa hits). No auto-pick (no Catalog tier-1 row). User clicks WC card → `downloadRequested` fires. Evidence: 3 screenshots (initial sync render, post-Nyaa, post-click).

3. **Zero-source series (empty state).** Pick a series with no catalog + no chapterIds (= no WC) + 0 Nyaa hits. Click Vol 1. Verify: empty state heading + subtitle render correctly. No auto-pick. Evidence: 1 screenshot.

4. **Hybrid loading.** Open a series where Catalog hits AND Nyaa is in flight. Verify: Catalog card + skeleton cards visible simultaneously. As Nyaa lands: skeletons replace one-by-one. Evidence: 2 screenshots (mid-fetch + post-fetch).

5. **Rapid volume click.** Click Vol 1 → within 200ms click Vol 2. Verify: Vol 1's auto-pick cancelled (no double-download). Vol 2's panel renders + its own auto-pick fires if applicable. Evidence: tankoctl logs (look for "auto-pick cancelled" / "auto-pick armed" sequence).

6. **Hover state.** Mouse over a non-selected card. Verify: subtle border brightening. Mouse out: returns to default. Evidence: 2 screenshots (hover on + off).

### Visual-quality (Hemanth's lane)

- Does the gold-accent on the Catalog card feel right vs the badge color? (Could compete visually.)
- Does the 300ms beat feel like enough pause to be "visible pick"? (Too fast might feel like instant download; too slow might feel sluggish.)
- Does the empty-state copy land right or feel preachy?

## 10. Implementation phases (hint for `/superpowers:writing-plans`)

The plan-writing skill will decompose this into bite-sized tasks. Suggested phase shape so the engineer has a reference framework:

**Phase 1 — ComicsSourceCard widget (new file).**
- Task 1.1: Header + POD types if any. Constructor signature.
- Task 1.2: `buildUI()` — QHBoxLayout with 3 panes + chip QHBoxLayout underneath.
- Task 1.3: `applyStateStyle()` — default/hover/selected QSS branching.
- Task 1.4: `setSelected(bool)` + `clicked` signal emission via `mouseReleaseEvent`.
- Task 1.5: `enterEvent` / `leaveEvent` hover handling.
- Task 1.6: Register in CMakeLists. Build check GREEN.

**Phase 2 — Skeleton card variant.**
- Task 2.1: Either inline branch in ComicsSourceCard (constructor variant) or new ComicsSourceSkeletonCard class.
- Task 2.2: Pulse animation via QPropertyAnimation on opacity.

**Phase 3 — ComicsSourcesPanel refactor.**
- Task 3.1: Swap QListWidget for QScrollArea + QVBoxLayout. Members updated.
- Task 3.2: Implement 4 state methods (setPlaceholder/setLoading/setSources/setEmpty).
- Task 3.3: Wire setSources to construct ComicsSourceCard per row + append to layout.
- Task 3.4: Skeleton cards appended at bottom when nyaaStillInFlight.
- Task 3.5: Source-row click → emit downloadRequested (existing signal chain).

**Phase 4 — Auto-pick orchestration in ComicsSourcesPanel.**
- Task 4.1: m_autoPickArmed latch + QTimer member.
- Task 4.2: Detect top-tier Catalog row in setSources; arm timer; set first card selected.
- Task 4.3: Cancel timer on populate() reset (rapid-click safety).
- Task 4.4: emitTopRowDownload() re-verifies + emits.

**Phase 5 — ComicsSeriesView gold-accent volume row stripe.**
- Task 5.1: Try Option (b) QSS pseudo-state first. Build check + visual smoke.
- Task 5.2: If row-height jitter visible: fall back to Option (a) QStyledItemDelegate.

**Phase 6 — Smoke matrix execution.**
- Task 6.1: Death Note auto-pick smoke (with screenshots).
- Task 6.2: Kingdom WC + Nyaa smoke.
- Task 6.3: Zero-source empty state.
- Task 6.4: Hybrid loading + hover states.

**Phase 7 — RTC bundle + memory save.**
- Task 7.1: Single bundled RTC posted to `agents/chat.md` with the contracts-v3 `Skills invoked` field.
- Task 7.2: Memory save: `project_comics_sources_sidebar_shipped_2026-05-17.md` (or whatever date the work lands).

## 11. Known risks + open questions

### Risks

- **Row-height jitter** on Option (b) QSS `border-left` per Qt6 issue tracking. Fallback to Option (a) delegate. Plan should smoke (b) first.
- **F3 carry-forward (synthetic-input rejection).** Codex's narrow F3 fix is for `m_libraryButton`'s release-without-press case; not for the broader synthetic-click pattern Win32 mouse_event hits on QPushButton subtypes. ComicsSourceCard is a QFrame not a QPushButton, so it shouldn't inherit the same issue, but agent-driven smoke might still hit the broader Win11 anti-focus-stealing pattern from last night. Smoke matrix step 1's auto-pick path BYPASSES this entirely (auto-pick fires without user click) — that's the primary verification surface. Manual-click verification on the WC fallback rows (smoke step 2) may need the ALT-trick from last night's smoke session.
- **Cards exceeding panel height** when many Nyaa hits land (e.g., 15+ rows). QScrollArea handles overflow; verify scroll-bar styling matches the panel's dark theme.

### Open questions (defer to plan-writing OR implementation discretion)

- Exact uploader-trust chip text source: parse `UnifiedSourceRow.uploaderHint` OR cross-reference `resources/manga_uploader_trust.json` for the canonical label. Plan should pick.
- Skeleton card animation: pulse opacity OR shimmer-gradient effect. Plan should pick the visually-cheaper option (probably pulse).
- Gold-accent left-stripe color exact value: `rgba(212,165,116,0.80)` proposed but Agent 5's theme work shipped tokens earlier this week. Plan should check `Theme.h` for the canonical gold-accent token + reuse instead of hardcoding.

---

## 12. References

- **Brainstorm session:** This document was authored via `/superpowers:brainstorming` on 2026-05-17 in 3 rounds of batches-of-4 conceptual questions plus 1 implementation-approach pick. Visual companion session at `.superpowers/brainstorm/1393-1779034877/`.
- **Stream pattern reference:**
  - `src/ui/pages/stream/StreamSourceCard.{h,cpp}` — card widget structure.
  - `src/ui/pages/stream/StreamSourceList.{h,cpp}` — scrollable container + 4-state machine.
- **Existing Comics surface:**
  - `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` — to be refactored.
  - `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` — to be lightly touched.
- **Adjacent Sources UI work (out-of-scope reference):** `docs/superpowers/specs/2026-05-13-sources-ui-refinement-design.md` — covers the Tankorent / Tankoyomi / TankoLibrary standalone Sources pages (Agent 4B's domain). Industry-standard density + theme rules from that spec apply here too (36px controls, 13px body text, strict gray/black/white + gold-accent, SVG icons not Unicode glyphs).
- **Related shipped work this session arc:**
  - F1 (catalog anilistId Death Note 30005 → 30021) — Codex Trigger D 2026-05-17 — Catalog row now actually appears for Death Note, unblocking Smoke 1.
  - F2 (NAV_LAYER_BACK Phase 0+1) — Agent 5 2026-05-17 — back-from-series-view nav wired.
  - F3 (m_libraryButton synthetic-input fix) — Codex Trigger D 2026-05-17 — narrow release-without-press fallback; partial.
  - TANKOYOMI_MANGAUPDATES_FALLBACK Tasks 1-8 — Codex Trigger D 2026-05-17 + Agent 1 post-ship validate + Agent 1 MCP smoke — Kingdom/Berserk/One Piece now resolve to real Vol counts.
- **Brotherhood conventions in scope:**
  - ASCII only in source files (`feedback_no_color_no_emoji`).
  - No worktrees — work on master directly (`feedback_no_worktrees`).
  - `build_check.bat` after every src/ touch (CLAUDE.md Tier 1).
  - Rule 17 cleanup on every smoke launch (`scripts/stop-tankoban.ps1`).
  - Rule 19 MCP LANE LOCK during agent-driven smoke.
  - RTC format per contracts-v3 with `Skills invoked: [...]` field.
  - Agents flag READY TO COMMIT in `agents/chat.md`; Agent 0 batches commits (`feedback_commit_protocol`).
