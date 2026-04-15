# Archive: Tankostream Phase 6 — Calendar (Batches 6.1 + 6.2 + 6.3 fix-batch)

**Agent:** 4 (Stream & Sources)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source (dual):**
- **Primary (plan):** `STREAM_PARITY_TODO.md:271-287` — Phase 6.1 (backend) + 6.2 (screen) batch lists + exit criteria
- **Secondary (parity):** `stremio-core/src/models/calendar.rs` per exit criterion :291

**Outcome:** REVIEW PASSED 2026-04-15.
**Shape:** Initial 0 P0, 2 P1, 8 P2. Both P1s fixed by Batch 6.3 fix-batch. Q5 bonus self-caught by Agent 4 (error/ready double-emission clobber) folded into the same fix-batch. All four Agent 6 re-verification criteria satisfied.

---

## Initial review summary (earlier this session)

Reviewed 6.1 CalendarEngine + 6.2 CalendarScreen against both the explicit spec plan (rolling 60d + This Week/Next Week/Later) and the named parity reference (stremio-core month-grid calendar.rs). Plan won on paradigm; calendar.rs used to audit behavioral details. 11 parity-correct features documented.

### P1 raised

1. **Re-entrancy hazard** — `resetTransientState()` zeroed state while in-flight `AddonTransport` workers remained parented; second `loadUpcoming` call could have mis-aligned `completeOne`'s counter + interleaved stale data. Reproducible via CalendarScreen's un-debounced Refresh button.

2. **Unbounded fan-out** — Tankoban dispatched N_series × N_addons per-series `meta` requests; stremio-core caps at `CALENDAR_ITEMS_COUNT = 100` via one batched `catalog + ExtraType::Ids` request per addon, library sorted by `mtime DESC`. 300 series + 2 addons = 600 HTTP roundtrips vs stremio's 2.

### Q5 bonus (self-caught by Agent 4 during response)

On empty-items-with-errors, engine emitted `calendarError + calendarReady({}) + calendarGroupedReady({})`. CalendarScreen's `renderGroups → setEmpty()` clobbered the error message with "No upcoming episodes…". User saw empty-state, not the actual error. Fix: exclusive-or emission in `completeOne`.

---

## Agent 4 response (Batch 6.3 fix-batch shipped 2026-04-15)

**P1 #1 — generation counter (option b).** Accepted cleanly; option (a) early-return would have swallowed legit Refresh intent; option (c) aborting QNetworkReply was a race hazard not worth opening.

**P1 #2 — hybrid cap + manifest probe.** Mandatory `addedAt DESC` cap at 100 closes impoliteness at scale; conditional batched `calendarVideosIds` / `lastVideosIds` catalog path wins where declared; per-series fallback covers catalog-less addons.

**Q5 bonus.** Exclusive-or emission in `completeOne` with one-line contract note.

**Re-review nits from Agent 6's ack post:** generation check at `completeOne` + `tryServeFreshCache` emission sites (not just at the two callbacks). Agent 4 delivered both.

Files touched: `src/core/stream/CalendarEngine.h` + `src/core/stream/CalendarEngine.cpp`.

---

## Agent 6 re-verdict

All four re-verification criteria code-verified:

### 1. Generation counter captured at dispatch + checked at all mutation/emission sites ✓

- `quint64 m_generation = 0;` at [CalendarEngine.h:143](src/core/stream/CalendarEngine.h#L143).
- Incremented FIRST at [CalendarEngine.cpp:237](src/core/stream/CalendarEngine.cpp#L237) via `const quint64 gen = ++m_generation`, BEFORE `resetTransientState` — stale callbacks observe the bump and short-circuit before we zero state they'd otherwise inspect.
- `resetTransientState` at :551-564 explicitly does NOT reset `m_generation`; comment at :553-555 documents why (would defeat the fix).
- `dispatchMetaFetch` captures `gen` by value in both lambdas (:361, :375) with outer-gate short-circuits at :370 + :382.
- `dispatchBatchedCatalogFetch` identical pattern (:411, :423 with gates :418 + :430).
- All four callback methods gate at top: `onMetaReady` :474, `onMetaFailed` :482, `onBatchedCatalogReady` :494, `onBatchedCatalogFailed` :509.
- `completeOne` gates at :522.
- `tryServeFreshCache` gates at :572 — explicitly cites "per Agent 6's Phase 6 REVIEW nit" in the comment. This was the nit in my ack post and it landed correctly.

Defense-in-depth at every emission site holds the invariant "no emission without current generation."

### 2. `addedAt DESC` cap + cache signature includes cap + batched-addon-id set ✓

- `kMaxSeries = 100` at [CalendarEngine.h:164](src/core/stream/CalendarEngine.h#L164) — parity with `CALENDAR_ITEMS_COUNT`.
- Sort at [CalendarEngine.cpp:274-277](src/core/stream/CalendarEngine.cpp#L274-L277) by `addedAt` descending. Entries pre-dating `addedAt` tracking (addedAt == 0) fall to the bottom naturally.
- Cap at :278-279 via `series.mid(0, kMaxSeries)`.
- `buildCacheSignature` at :649-677 takes `int cap` + `QList<QString> batchedAddonIds` parameters; signature now salts on both.
- **`kSchemaVersion` bumped 1 → 2** at [CalendarEngine.h:160](src/core/stream/CalendarEngine.h#L160) with comment "bumped for cap+batched salt." Agent 4 did this on his own — I hadn't explicitly called for it, but it's the right call: old-schema caches won't match the new-signature logic and would otherwise serve stale data.
- Fallback previews built AFTER cap at :281-284 — memory-efficient, no fallbacks for series we won't fan out.
- P2 #8 (`addedAt` plumbed but unused as sort key) implicitly closed by this fix consuming it.

### 3. Error XOR success emission ✓

[CalendarEngine.cpp:542-548](src/core/stream/CalendarEngine.cpp#L542-L548):

```cpp
if (m_items.isEmpty() && !m_firstError.isEmpty()) {
    emit calendarError(m_firstError);
    return;
}
emit calendarReady(m_items);
emit calendarGroupedReady(buildDayGroups(m_items, m_nowUtc.date()));
```

Clean exclusive-or. Empty-no-errors case still fires `calendarReady({}) + calendarGroupedReady({})` so CalendarScreen's "No upcoming episodes" path renders. Comment at :538-541 calls out the semantic. Q5 closed.

### 4. Manifest-probe reads descriptor ABI correctly (no extra network call) ✓

`findBatchedSeriesCatalog` at [CalendarEngine.cpp:679-704](src/core/stream/CalendarEngine.cpp#L679-L704) walks `addon.manifest.catalogs` (already cached from install time), filters by `type == "series"`, checks `extra[].name` against `{"calendarVideosIds", "lastVideosIds"}`. No `fetchResource` call. Writes matched extra prop name into the out-param for use at the batched-dispatch site.

Dispatch logic at :321-333: one batched catalog request per batched-supporting addon (via `dispatchBatchedCatalogFetch` — resource "catalog", extra param `<extraName>=<csv-of-ids>`); per-series `meta` fan-out for the rest. Mixed flows work: Cinemeta (batched) + Torrentio (per-series) in the same load both fire correctly.

### Additional clean-code observations

- `ingestMetaObj` helper at :439-466 factored out between `onMetaReady` (single-meta shape) and `onBatchedCatalogReady` (multi-meta `metas` array) — eliminates the ingest-path duplication. Uses `seriesHintId` as fallback when the meta body lacks an `id`.
- Sorted `batchedAddonIds` at :308 + re-sorted defensively in `buildCacheSignature` :665-666 — resilient to caller ordering drift.

### Minor observation (not a fix-ask)

`saveCache(m_items)` at :536 fires unconditionally, including the empty-items-with-errors case that took the early return at :543. So the empty-errored state gets persisted to disk, and the next run within 12h TTL would serve a fresh-cache-hit emitting `calendarReady({}) + calendarGroupedReady({})` without re-raising the error. Arguably correct (transient errors resolve via TTL); arguably a gap (user would see "No upcoming episodes" instead of the original addon error until TTL expires). Not blocking; noting for a future polish pass if Hemanth observes it.

### P2 dispositions at archive

- P2 #1 (paradigm divergence), P2 #4 (no `selected` state persistence) — spec-driven design, accepted as context.
- P2 #2 (library-filter `!removed && !temp`), P2 #5 (error-channel divergence), P2 #6 (`resetTransientState` clears fallback before cache-hit path) — flagged in original review as forward-port debt; no fix-ask.
- P2 #3 (`ContentItem` full MetaItem vs preview), P2 #7 (cache URL round-trip fragility) — accepted as shipped.
- P2 #8 (`addedAt` unused) — CLOSED by P1 #2 fix consuming it.

### Verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed (P1 #1 generation counter; P1 #2 cap + hybrid batched/per-series)
- [x] Q5 bonus closed (exclusive-or emission)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 4, Tankostream Phase 6]** 2026-04-15 (initial 6.1 + 6.2 + 6.3 fix-batch).

Phase 6 closes. `READY TO COMMIT` line at chat.md:10919 stands. Schema bump 1→2 will re-fetch every user's calendar cache on first 6.3 launch — expected, one-time.
