# Theatre Source Picker + Nyaa Registration Fixes — Design

**Authored:** 2026-05-17 IST
**Owner:** Agent 4 (Stream/Theatre)
**Status:** Brainstorm complete, ready for plan

## Goal

Hemanth's request: "I do not see any results from Nyaa from our tankorent sources... I would love to have only Nyaa as source for an anime."

After exploring the design space, Hemanth picked the simpler interpretation: **let the user pick the source manually, mirroring the Tankorent tab's existing source-combo pattern.** No anime auto-detection, no genre parsing, no special-casing. If you want Nyaa-only for an anime, you pick Nyaa from the dropdown. If you want YTS-only for a movie, you pick YTS. Etc.

This collapses the work to two pieces:

1. **Fix Nyaa registration gaps** so Nyaa actually fires when selected (and is included in "All Sources" by default).
2. **Add a Source dropdown to the Theatre Layers-3 panel** (TheatreDownloadPanel), persisting the last choice via QSettings.

## Background

Three sites currently fan out to indexers:

- `src/ui/pages/TankorentPage.cpp:1228-1234` — standalone Tankorent tab. Lists all 7 indexers including Nyaa, but the `kMediaTypeIndexers["videos"]` allowlist at line 1188 excludes Nyaa for Videos searches under "All Sources" pick.
- `src/core/stream/StreamAggregator.cpp:761-772` — Theatre Layers-3 pack panel fan-out. Hardcodes 5 indexers; Nyaa was never added.
- `src/ui/pages/stream/TorrentPackPicker.cpp:160-164` — legacy picker. Scheduled for deletion in THEATRE_DOWNLOAD_OVERHAUL Phase G. Out of scope.

The user-facing UI flow from Layers-3 today:

```
TheatreDownloadPanel (UI)
  └─ UnifiedPackSearchEngine::search(imdb, show, season)
       └─ subscribes to StreamAggregator::packsAvailable
            └─ StreamAggregator::searchPacks(...)
                 └─ for each query: dispatch(piratebay, 1337x, yts, eztv, exttorrents)
```

The source filter needs to thread from the UI down to the dispatch loop.

## Scope

### In scope

**Piece A — Bug fixes (independently valuable, ships first as a small commit):**

- **A.1** Add Nyaa to `StreamAggregator::searchPacks` dispatch loop (line 761-772). One new `dispatch(...)` call per query, mirroring the existing five.
- **A.2** Add `"nyaa"` to `kMediaTypeIndexers["videos"]` in `TankorentPage.cpp:1188`. One-word change.

**Piece B — Theatre source picker (the actual feature):**

- **B.1** Add `QComboBox m_sourceCombo` to `TheatreDownloadPanel`. Same option list as TankorentPage's source combo: All Sources / Nyaa / PirateBay / 1337x / YTS / EZTV / ExtraTorrents. (TorrentsCsv excluded from Theatre — historically books-only; preserve TankorentPage parity.)
- **B.2** Plumb the selected source through `UnifiedPackSearchEngine::search()` → `StreamAggregator::searchPacks(...)`. New optional parameter `const QString& sourceFilter = QStringLiteral("all")`.
- **B.3** Gate the dispatch loop in StreamAggregator: if `sourceFilter != "all"`, only dispatch the matching indexer.
- **B.4** Persist last choice via `QSettings` key `theatre/pack_panel/source`, defaulting to `"all"`. Load on panel construction, save on combo change.
- **B.5** Placement: above the existing 4 filter chips ("All / Complete Series / Multi-Season / Season Pack"), as its own row with a "Source" label. Same height/padding language as the chip row.

### Out of scope

- Anime auto-detection (deferred — Hemanth picked manual over auto)
- Override toggle on filter chips (not needed — source dropdown IS the control)
- Sources sidebar changes (two-lane arch preserved — Torrentio addons own Sources)
- TorrentPackPicker fix (gets deleted in Phase G)
- TankorentPage UI changes beyond the one-word `kMediaTypeIndexers` addition
- IndexerStatusPanel sentinel changes (already includes Nyaa)

## Data flow (post-change)

```
TheatreDownloadPanel
  ├─ m_sourceCombo (NEW) → selected source ID ("all" | "nyaa" | "piratebay" | ...)
  │   └─ QSettings persistence (NEW)
  └─ UnifiedPackSearchEngine::search(imdb, show, season, sourceFilter)  ← NEW PARAM
       └─ StreamAggregator::searchPacks(imdb, show, season, sourceFilter)  ← NEW PARAM
            └─ for each query, for each indexer:
                 if sourceFilter == "all" OR sourceFilter == indexer_id:
                     dispatch(indexer_id, new <Indexer>(...), query)
```

The `sourceFilter` parameter defaults to `"all"` at every layer — backward-compatible with any future caller that doesn't supply it.

## Files touched

- `src/core/stream/StreamAggregator.h` — add optional sourceFilter param to searchPacks signature
- `src/core/stream/StreamAggregator.cpp` — add Nyaa to dispatch loop + gate by sourceFilter
- `src/core/stream/UnifiedPackSearchEngine.h` — add optional sourceFilter param to search() signature, store as `m_pendingSource`
- `src/core/stream/UnifiedPackSearchEngine.cpp` — thread sourceFilter into the aggregator call
- `src/ui/pages/stream/TheatreDownloadPanel.h` — declare `QComboBox* m_sourceCombo`, `QString m_sourceFilter`, slot `onSourceComboChanged(int)`, helper `loadPersistedSource()` / `savePersistedSource()`
- `src/ui/pages/stream/TheatreDownloadPanel.cpp` — build + style the combo, load persistence in ctor, wire signal, pass current selection to engine on search
- `src/ui/pages/TankorentPage.cpp` — one-word allowlist edit (line 1188)

Estimated diff: ~80-120 LOC additive, zero deletions (the chip-simplification-arc removed `m_sourceFilter` member was for FILTERING RESULTS post-fan-out; this is a different member that GATES the fan-out — semantically distinct, no contradiction).

## Testing

- **Build verification:** `build_check.bat` BUILD OK after each piece.
- **Smoke A (bug fixes):** TankorentPage with Videos + Nyaa picked → Nyaa results appear. Theatre Layers-3 with "All Sources" (current default; no UI for source pick yet) → Nyaa results appear alongside the other 5.
- **Smoke B (source picker):** Open Demon Slayer in Theatre → click Layers-3 → see Source combo at top, defaulting to "All Sources" → pick "Nyaa" → only Nyaa packs appear in tile list. Close panel, re-open on a different anime → combo remembers "Nyaa" pick.
- **Persistence smoke:** Pick Nyaa, kill Tankoban, relaunch, re-open Layers-3 → combo still shows "Nyaa".

## Risks + edge cases

- **Risk 1 — source filter clobbers a follow-up anime feature.** If we later want auto-detect-anime → Nyaa, the manual combo could fight it. Mitigation: auto-detect can simply set the combo programmatically (it's a regular QComboBox), and persistence-on-change handles the rest.
- **Risk 2 — Nyaa returns zero for a non-anime title.** User picks "Nyaa" out of curiosity, gets zero results, doesn't know why. Mitigation: the panel's existing empty-state message already covers this ("no packs found"); maybe append `(filtered to: <source>)` to the empty-state text. Low-priority polish, not blocking.
- **Risk 3 — chip-simplification arc just removed source filtering.** Different semantic (filter results post-search vs limit dispatch pre-search) but worth a one-line comment in the code to explain why this isn't a regression.

## Open questions (none blocking — Agent 4 picks)

All technical decisions resolved. Hemanth's strategic call: locked.

## Done criteria

- Piece A shipped, RTC posted, BUILD OK, smoke evidence: Nyaa results visible in TankorentPage (Videos + Nyaa pick) and in Theatre Layers-3 (All Sources default).
- Piece B shipped, RTC posted, BUILD OK, smoke evidence: source combo renders, gates dispatch correctly, persists across launches.
- Spec file committed alongside or before Piece A.
