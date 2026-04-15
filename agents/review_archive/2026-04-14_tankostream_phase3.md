# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources)
Subsystem: Tankostream Phase 3 — Catalog Browsing (Batches 3.1 CatalogAggregator, 3.2 StreamHomeBoard, 3.3 CatalogBrowseScreen)
Reference spec: `STREAM_PARITY_TODO.md` Phase 3 + `stremio-core/src/models/catalog_with_filters.rs` (Rust canonical) + `stremio-core/src/types/addon/manifest.rs:102` for `catalogs` shape.
Objective: Stream mode opens to a home board (continue + catalog rows), click a row header → filtered browse screen. Browse without searching works.
Files reviewed:
- `src/core/stream/CatalogAggregator.h/.cpp` (3.1 — parallel per-addon catalog fan-out)
- `src/ui/pages/stream/StreamHomeBoard.h/.cpp` (3.2 — home board with continue strip + N featured catalogs)
- `src/ui/pages/stream/CatalogBrowseScreen.h/.cpp` (3.3 — 5th stack layer with selectors + filter chips + tile grid + Load More)
Cross-references:
- `stremio-core/src/models/catalog_with_filters.rs:494-506` — SKIP_EXTRA_PROP pagination semantic
- `stremio-core/src/types/addon/manifest.rs:102-103` — catalogs vs addon_catalogs

Date: 2026-04-14

### Scope

Phase 3 catalog browsing layer. Relies on Phase 1 (PASSED) `AddonRegistry::list()` + `AddonTransport::fetchResource` APIs. Phase 2 (PASSED) is unrelated here (manager UI). Out of scope: Phase 4 stream aggregation, Phase 5 subtitles. Static read only; Hemanth's runtime smoke-pass is the functional oracle. Fresh-install empty-state caveat (TODO noted: seeded Cinemeta/Torrentio catalogs may carry `catalogs=[]`) is an addon-side concern, not an aggregator bug.

### Parity (Present)

**Batch 3.1 — CatalogAggregator**

- **Parallel per-addon fan-out via separate `AddonTransport` workers.** Spec `STREAM_PARITY_TODO.md:142`. Code: `CatalogAggregator.cpp:192-208` — `new AddonTransport(this)` per active cursor, each connected to its own resourceReady/resourceFailed handler. Workers `deleteLater()` on reply. Parent=aggregator ensures no leak on destruction. ✓
- **`AddonCursor` tracks per-addon `{baseUrl, catalog, skip, hasMore, inFlight}`.** Code: `CatalogAggregator.h:43-50`. One cursor per enabled addon that advertises the requested catalog. ✓
- **Dedup by meta id.** Code: `CatalogAggregator.cpp:233-236` — `m_seenMetaIds.contains(item.id)` filter on merge. ✓
- **Respects `ExtraProp.options_limit`.** Spec `STREAM_PARITY_TODO.md:141`. Code: `applyOptionsLimit` at `CatalogAggregator.cpp:25-69` truncates comma-separated values per prop's `optionsLimit`. Also auto-inserts a default value for `isRequired` props that the caller didn't supply. Matches Stremio's `SelectableExtra` semantic (though see P2 below on UI-side single-select not exercising the multi-value path). ✓
- **Pagination via `skip` extra param.** Reference: `stremio-core/src/models/catalog_with_filters.rs:506` extends extras with `("skip", n)`. Code: `CatalogAggregator.cpp:187-190` appends `("skip", QString::number(cursor.skip))` when `cursor.skip > 0`. Cursor increments by `addedFromAddon` after parsing (`:241`) — not by hardcoded page size, which correctly handles deduped-drops. ✓
- **`hasMore` inference when field absent.** Code: `CatalogAggregator.cpp:243-247` — if response contains `hasMore`, trust it; otherwise fall back to `addedFromAddon > 0`. Defensive for older addons that don't emit the field. ✓
- **Per-addon error does not block the page.** Code: `onAddonFailed` at `:252-260` emits `catalogError(addonId, msg)`, clears `inFlight`, then `completeIfReady()`. Other cursors continue. The UI can surface partial results with per-addon error annotations. ✓
- **`loadNextPage` only dispatches cursors with `hasMore && !inFlight`.** Code: `:110-132`. If no cursors meet both criteria, emits `catalogPage({}, false)` as a terminal signal. ✓
- **`resetInternalState` on `load()`.** Code: `:98-108, :281-288` clears query, seen ids, cursors, page buffer, pending counter. Fresh query state on every new `load()`. ✓
- **Meta preview parsing handles missing `id` / `type` gracefully.** `parseMetaPreview` at `CatalogAggregator.cpp:71-88`: falls back to `imdb_id` if `id` missing, falls back to cursor's `catalog.type` if `type` missing. Drops items whose id or name is empty at `:230-232`. ✓

**Batch 3.2 — StreamHomeBoard**

- **Continue-watching strip + N horizontal catalog rows.** Spec `STREAM_PARITY_TODO.md:150`. Code: `StreamHomeBoard.cpp:267-275` — `m_continueStrip` + `m_rowsHost`/`m_rowsLayout` vertical layout. Continue strip re-refreshes on every `refresh()` call at `:247`. ✓
- **Enumerate all enabled addons' `manifest.catalogs[]`, 4-6 default, user-persisted selection.** Spec `:151`. Code: `enumerateCatalogs` at `:290-321` walks enabled addons, dedups by `specKey(addonId|type|catalogId)`. `chooseFeaturedCatalogs` at `:323-362` reads `QSettings("stream_home_catalogs")`, reconciles against live catalogs (drops saved entries that no longer exist), falls back to first `qMin(6, qMax(4, all.size()))` on empty saved list, then persists the default. ✓
- **Each row = `HomeCatalogRow` subclassing QFrame with embedded Q_OBJECT.** Uses `TileStrip`/`TileCard` shared infrastructure — consistent with Agent 5's library pattern. Title label + "Browse" button in header; TileStrip in body. `StreamHomeBoard.moc` included at bottom of .cpp for the inline Q_OBJECT class. Unusual but valid Qt pattern. ✓
- **Double-click a tile → `metaActivated(metaId, metaType)`.** Code: `HomeCatalogRow::buildUi` at `:121-127` wires `tileDoubleClicked` → emit. StreamHomeBoard re-forwards via `:239-240`. ✓
- **"Browse" button → `browseCatalogRequested(addonId, type, catalogId, title)`.** Code: `:111-113, :235-238`. Drives the Phase 3.3 pivot. ✓
- **Poster cache reuses `{AppData}/Tankoban/data/stream_posters/`.** Spec `:153`. Code: `posterCacheDir()` at `:35-41`. ✓
- **Async poster fetch with `QPointer<TileCard>` guard.** Code: `ensurePoster` at `:135-172` — safe against card destruction mid-fetch. ✓

**Batch 3.3 — CatalogBrowseScreen**

- **Fifth layer in the StreamPage stack.** Spec `STREAM_PARITY_TODO.md:160`. Code: wired in StreamPage.cpp alongside the other stack members (verified by class existence + Agent 4's READY FOR REVIEW line listing it). ✓
- **Catalog picker: addon + catalog combos.** Spec `:162`. Code: `CatalogBrowseScreen.cpp:109-115` — `m_addonCombo` + `m_catalogCombo` in the top bar. Addon-combo change triggers `rebuildCatalogCombo → rebuildFilterBar → reload` (`:166-173`). Catalog-combo change triggers `rebuildFilterBar → reload` (`:174-180`). ✓
- **Filter chips populated from `ExtraProp.options`.** Spec `:162`. Code: `rebuildFilterBar` at `:239-285` iterates `catalog.extra`, skips `skip` prop (engine-managed), skips props with empty options, creates a QComboBox per remaining prop with "Any" as the first entry + up to `options_limit` user options (`:270-273`). Each combo carries the extra-name as a property; selection change triggers `reload()`. ✓
- **Tile grid + Load More.** Spec `:162`. Code: `m_strip = new TileStrip` inside a `QScrollArea` at `:130-142`. `m_loadMoreButton` at `:143-152` calls `m_aggregator->loadNextPage()` on click, disables during fetch, reappears if next-page signal indicates more. Spec explicitly allows either "Load More button or infinite scroll" — Load More is simpler and shipped. ✓
- **Filter change → aggregator reload with new request.** Code: `reload()` at `:287-307` builds a fresh `CatalogQuery`, clears strip + status, calls `m_aggregator->load(q)`. Aggregator's `resetInternalState` ensures no carry-over from previous query. ✓
- **Paginate → loadNextPage.** Code: `:147-151`. Aggregator appends new pages to `m_pageBuffer`, emits `catalogPage`. Screen's slot at `:48-55` calls `appendTiles` (not `clear + fill`), so pagination is additive. ✓
- **Back → StreamHomeBoard.** Code: `:102-106` — backButton connects to `backRequested` signal; StreamPage wires the signal to stack-index-0. ✓
- **Status label provides liveness feedback.** Code: `:126-128, :52-61`. "Idle" → "Loading..." → "Loaded" / "No results" / "Catalog error ({addon}): {msg}". ✓
- **Open-with-pre-selected-catalog flow.** Code: `open(addonId, type, catalogId)` at `:64-78` — rebuilds selectors (catches newly-installed addons), suppresses reload signals during programmatic selection, triggers one explicit reload at the end. Suppress flag prevents duplicate reloads from chained combo changes. ✓
- **Per-tile async poster fetch with `QPointer<TileCard>` guard.** Code: `ensurePoster` at `:333-372` — same defensive pattern as StreamHomeBoard. ✓

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **Stale-worker race on back-to-back `load()` calls.** `CatalogAggregator::load()` at `CatalogAggregator.cpp:98-108` calls `resetInternalState()` before re-planning. If a previous query's worker is still in flight, its late reply lands in `onAddonReady` at `:215-250`, finds `it == m_activeByAddon.end()` (since the cursor map was cleared), and falls through to `completeIfReady()` at `:262-278` which decrements `m_pendingResponses`. That counter is now owned by the *new* query — the stale decrement causes premature emission of the first page with possibly-empty results, then the real new-query response races in. In practice triggered only if the user slams filter changes faster than the 10s per-addon timeout; Cinemeta's catalog is usually <500ms so the race is rare. Fix is a generation counter passed into each worker lambda — if the generation doesn't match the current value, the callback early-returns without touching aggregator state. Flag for future hardening; not urgent.
- **No in-flight cancellation on `load()` reset.** Related to the above — the outstanding `QNetworkReply` objects from the previous query are not aborted. They waste bandwidth and trigger the stale-worker callbacks. A simple `m_nam->abort()` pattern isn't available since each worker owns its own transport. Either maintain a QList of in-flight replies to abort, or adopt a generation counter (above) so the late callbacks are harmless. Second option is cheaper.
- **New `AddonTransport` instance per request.** `CatalogAggregator.cpp:192` allocates a fresh transport per addon per page. Each owns its own `QNetworkAccessManager` (via AddonTransport's ctor). Wastes connection-pool warmth across subsequent pages from the same addon. Optional optimization: cache per-addon transports in the aggregator. Not a correctness issue.
- **`ExtraProp.options_limit` interpretation divergence.** Stremio semantic: `options_limit` = max number of values the user can select *simultaneously* for multi-select props (e.g., pick up to 3 genres). Tankoban UI: `CatalogBrowseScreen.cpp:269-273` caps the number of options *displayed in the combo* to `options_limit`, and the combo is single-select. For `options_limit=1` (Cinemeta's default for every prop) the effect is identical; for `options_limit>1` (hypothetical 3rd-party addon with multi-select genres) Tankoban would hide options rather than allow multi-select. `CatalogAggregator::applyOptionsLimit` at `:56-66` *does* implement the value-truncation semantic correctly for a comma-separated selection — it's just that the current UI never produces comma-separated values. Working Aggregator + UI-single-select is a consistent but shallower surface than the spec. Flag for when a multi-select filter surfaces in a 3rd-party addon.
- **`QHash` iteration for addon combo ordering is non-deterministic.** `CatalogBrowseScreen.cpp:194-214` builds `addonLabelById` as QHash, then iterates it at `:212` to populate `m_addonCombo`. QHash is unordered, so the combo item order can change across sessions or even Qt versions. Fix: iterate `m_registry->list()` directly (registry returns a deterministic QList), or use QMap. Minor UX churn; flag for consistency.
- **Process note — no chat.md heads-up to Agent 5 before editing browse layer.** Spec `STREAM_PARITY_TODO.md:154` explicitly asks for an announcement before Batch 3.2 because library-side UX is Agent 5's domain and the home board touches the browse layer. I can't find that heads-up in chat.md. No code harm — Agent 5 hasn't flagged a conflict — but the spec asked for it. Flag for process hygiene; informative only.
- **`specKey` uses `|` as separator.** `StreamHomeBoard.cpp:251-253` builds the dedup key as `addonId|type|catalogId`. If any component contains `|`, keys collide. Addon ids are conventionally reverse-domain (`com.linvo.cinemeta`) so no `|`; Stremio catalog ids are usually slugs. Realistically safe; a more defensive separator would be `\x1F` (unit separator) or a QPair<QPair<QString,QString>, QString>. Note only.

### Questions for Agent 4

1. **Stale-worker race.** Is a generation counter guard worth folding into Batch 3.1 now, or defer to a "when a filter storm bug is reported" fix? The defensive cost is ~10 lines; the impact today is probably invisible.
2. **`options_limit` interpretation.** Is the UI single-select + aggregator multi-value-truncation split a deliberate choice (simpler UI), or an implementation gap that surfaces only when a 3rd-party addon ships multi-value filters? If the latter, a future "multi-select filter combo" UI is the fix.
3. **Agent 5 heads-up.** Did you coordinate with Agent 5 on StreamHomeBoard placement outside chat.md (e.g., DM'd Hemanth, or Agent 5 declined), or was the spec's announcement requirement overlooked?
4. **Catalog ordering** — rebuildSelectors via QHash produces unstable combo order. Intentional (and users don't notice), or a cleanup item?

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 4, Tankostream Phase 3], 2026-04-14.** Seven P2 observations (two defensive hardening items, two UI semantic gaps, one process note, two cosmetic). Four informational Qs. Agent 4 clear for Rule 11 commit of Phase 3 alongside Phases 1+2. Phase 4 review pulls next.

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
