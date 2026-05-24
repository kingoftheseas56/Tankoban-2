# COMICS_WC_VOLUME_WIRING — Design Spec

**Status:** Brainstorm-locked 2026-05-24 (Agent 1 with Codex Trigger C review).
**Owner:** Agent 1 (Comics + Tankoyomi domain).
**Executor:** Agent 7 (Codex Trigger D, per Hemanth's "Codex on full-time fuel" directive for this arc).

## Goal

Wire WeebCentral chapter scraping as a second per-volume source in
ComicsSeriesView's Sources panel, so any MangaFire-cataloged series gets a
viable "Download" path for every volume — even when no Nyaa-trusted-uploader
torrent exists.

WeebCentral chapters ARE the original volume scans split into chapter-sized
chunks. A 1-to-1 integer range filter against MangaFire's per-volume
`chapterStart..chapterEnd` produces the right chapter set for that volume.
Hand the chapter IDs to the existing `WeebCentralVolumePacker`, which already
fetches images + zips into a `.cbz` + validates + registers with
`MangaDownloadIndex`.

## Anchor decisions (ratified)

| # | Decision | Lock source |
|---|----------|-------------|
| 1 | Sources panel = Stremio-style row list (StreamSourceList parity); user clicks a row to start that source. | Hemanth mockup pick, Batch 1 |
| 2 | Source row name is literally "WeebCentral" — no "pack" / "compile" / "scraper" suffix. | Hemanth, Batch 1 |
| 3 | Source row visual mirrors StreamSourceCard: badge + two-line text + tier pill. WeebCentral row stays sparse (no quality variance). | Stream-mode parity directive |
| 4 | Chapter matching is 1-to-1 integer. MangaFire vol 1 = "ch 1-5" maps to WeebCentral's ch 1, 2, 3, 4, 5. | Hemanth, Batch 1 |
| 5 | Incomplete chapter coverage of a volume = skip the WeebCentral source entirely for that volume. No partial cbz. | Hemanth, Batch 1 |
| 6 | WeebCentral seriesId resolution is lazy on first vol-click; result cached back into the MangaCatalog JSON for subsequent clicks. | Hemanth Rule 14, Batch 1 |
| 7 | "Loading sources…" panel header during the ~1s WC search. | Hemanth, Batch 2 |
| 8 | Wrong-WC-match recovery is YAGNI for v1. Top match wins; no override UI. | Hemanth, Batch 2 |
| 9 | Already-downloaded vol = Stream-mode parity: direct "Open" + an option to see alternative sources. | Hemanth, Batch 2 |
| 10 | Output cbz lands in the same canonical series dir as torrent-packed vols. | Hemanth Rule 14, Batch 1 |
| 11 | Resolver owns a PRIVATE `WeebCentralScraper` instance (not the shared one used by Tankoyomi search/detail/packing). Shared QNAM is fine. | Codex Trigger C guardrail |
| 12 | Each resolve carries a generation key `seriesId + volumeNumber + requestSerial`; late results that arrive after navigation get dropped at the receive guard. | Codex Trigger C guardrail |
| 13 | WC `VolumePackRequest.seriesId` = MangaCatalog.seriesId (the MangaFire slug). NOT `anilist_<id>`. Otherwise the packed cbz completes but doesn't light up the MangaFire vol tile via MangaDownloadIndex. | Codex Trigger C guardrail |
| 14 | Schema additive: `weebCentral: { seriesId, chaptersFetchedAt, volumeChapterIds }` — namespaced sub-object, no schema-version bump. | Codex Trigger C guardrail |
| 15 | JSON-on-write uses an atomic patch (read existing JSON, mutate the `weebCentral` key, write back). Do NOT re-serialize from the in-memory `MangaCatalog` — risks dropping MangaFire-only fields not yet mirrored to the C++ struct. | Codex Trigger C guardrail |

## Architecture

### The bridge class

**New:** `tankoban::manga::mangafire::MangaWeebCentralResolver` at
`src/core/manga/mangafire/MangaWeebCentralResolver.{h,cpp}`.

**Inputs:** `MangaCatalog` + `volumeNumber` + `requestSerial`.

**Outputs:** one of:
- `Verdict::Viable { chapterIds: QStringList, requestKey: ResolveKey }`
- `Verdict::Skip { reason: enum {NoSeriesMatch, NoChapterOverlap, IncompleteCoverage, NetworkError}, requestKey: ResolveKey }`

**Public surface (sketch):**
```cpp
class MangaWeebCentralResolver : public QObject {
    Q_OBJECT
public:
    explicit MangaWeebCentralResolver(QNetworkAccessManager* nam,
                                      QObject* parent = nullptr);
    ~MangaWeebCentralResolver() override;

    struct ResolveKey {
        QString seriesId;       // MangaFire slug
        int     volumeNumber;
        quint64 requestSerial;  // monotonic per process
    };

    void resolve(const MangaCatalog& catalog, int volumeNumber, ResolveKey key);

signals:
    void viable(ResolveKey key, QStringList chapterIds);
    void skip(ResolveKey key, QString reasonCode);

private:
    // Private WeebCentralScraper instance — does NOT share the registry's
    // scraper, which is used by Tankoyomi search/detail/packing and whose
    // searchFinished / chaptersReady signals carry no request id.
    WeebCentralScraper*           m_scraper = nullptr;
    QNetworkAccessManager*        m_nam     = nullptr;

    // Per-series in-flight cap (one resolve per series at a time; later
    // calls for the same series queue behind the first).
    QHash<QString, /*pending*/>   m_inflight;

    // Per-series session cache of fetched chapter list (avoids re-fetching
    // for every vol click within the same series).
    QHash<QString, QStringList>   m_chapterCache;
};
```

### Resolve pipeline

```
resolve(catalog, volumeNumber, key) →
  1. If catalog.weebCentral.seriesId is empty:
       run m_scraper->searchByTitle(catalog.seriesTitle)
       on result → top hit = WC seriesId. Persist via atomic JSON patch
       (anchor decision 15). Continue.
     Else: use the cached weebCentralSeriesId.

  2. If m_chapterCache[catalog.seriesId] is missing:
       run m_scraper->fetchDetail(wc seriesId)
       on result → chapter list. Cache in m_chapterCache.
     Else: use cached list.

  3. Look up vol N in catalog.volumes[]: get chapterRangeStart, chapterRangeEnd.

  4. Filter chapter list to integer ids in [start, end]. Verify every
     integer in [start, end] has a matching WC chapter id; if any are missing,
     emit skip(key, "IncompleteCoverage") and return.

  5. Emit viable(key, chapterIds).
```

### Identity guardrail

The resolver returns chapter IDs. The caller builds the `VolumePackRequest`:

```cpp
VolumePackRequest req;
req.seriesId        = catalog.seriesId;          // MangaFire slug
req.volumeNumber    = N;
req.destinationPath = /* canonical series dir, same as torrent path */;
req.chapterIds      = resolverChapterIds;
m_weebCentralVolumePacker->requestVolume(req);
```

`req.seriesId` is the MangaFire slug — explicitly NOT `anilist_<id>`. This is
how `MangaDownloadIndex.registerVolume` keys the entry, and how the vol tile
in ComicsSeriesView (which is rendered from MangaCatalog) finds its
"downloaded" state on subsequent opens.

`destinationPath` is the same canonical series directory torrent-packed vols
use. `MangaDownloadIndex` does not branch on source; vol 1 from WeebCentral
sits next to vol 2 from Nyaa in the same folder.

### Generation key guard

The user can click vol 1 → vol 2 → vol 3 in rapid succession before any
resolver round-trip completes. Without a stale-event guard, late vol 1 results
would append a "WeebCentral" row to the now-displayed vol 3.

Pattern:
```cpp
quint64 m_nextRequestSerial = 0;  // ComicsPage member

void onVolumeSelected(int volumeNumber) {
    const auto key = MangaWeebCentralResolver::ResolveKey{
        m_catalog.seriesId, volumeNumber, ++m_nextRequestSerial
    };
    m_currentResolveKey = key;  // stash latest
    m_wcResolver->resolve(m_catalog, volumeNumber, key);
}

void onResolverViable(ResolveKey key, QStringList chapterIds) {
    if (key.seriesId != m_currentResolveKey.seriesId
     || key.volumeNumber != m_currentResolveKey.volumeNumber
     || key.requestSerial != m_currentResolveKey.requestSerial) {
        return;  // stale; user has moved on
    }
    m_sourcesPanel->appendWeebCentralRow(/*...*/);
}
```

## Schema additive

### JSON shape

Existing MangaFire JSON at `data/mangafire_catalog/<slug>.json` gains one
top-level key:

```json
{
  "seriesId": "yu-yu-hakusho",
  "seriesTitle": "Yu Yu Hakusho",
  "mangafireUrl": "https://mangafire.to/manga/yu-yu-hakushoo.abc1",
  "...": "(all existing MangaFire fields untouched)",
  "weebCentral": {
    "seriesId": "yu-yu-hakusho-3a7b9c",
    "chaptersFetchedAt": "2026-05-24T08:30:00Z",
    "volumeChapterIds": {
      "1": ["c1", "c2", "c3", "c4", "c5"],
      "2": ["c6", "c7", "c8", "c9", "c10", "c11", "c12", "c13", "c14"]
    }
  }
}
```

`weebCentral.seriesId` is the cache hint. `chaptersFetchedAt` is an ISO 8601
UTC timestamp for future invalidation policy (out of scope for v1).
`volumeChapterIds` is optional on disk — the in-memory session cache is the
primary store; the on-disk cache is the speed-up for repeat sessions and may
be left empty in v1 if resolver round-trips are fast enough.

### Schema version

**No bump.** `kMangaCatalogSchemaVersion` stays at 1. The additive shape is
backward compatible — older JSONs without a `weebCentral` block parse cleanly
and just take the resolver round-trip on first vol click.

### C++ struct extension

`MangaCatalogTypes.h` gains:

```cpp
struct WeebCentralCacheBlock {
    QString    seriesId;
    QDateTime  chaptersFetchedAt;
    QHash<int, QStringList> volumeChapterIds;   // empty if not cached
    bool isEmpty() const { return seriesId.isEmpty(); }
};

struct MangaCatalog {
    /* ... existing fields ... */
    WeebCentralCacheBlock weebCentral;
};
```

### Deserializer extension

`LocalMangaCatalogLoader::loadFromFile` gains a small block to populate
`cat.weebCentral` if the JSON has the key; absent key = empty
`WeebCentralCacheBlock`. Default-empty case is the common path for v1.

### Atomic JSON patch on write

The resolver writes the cache back via a new helper (not by re-serializing
`MangaCatalog`):

```cpp
namespace tankoban::manga::mangafire {
    // Reads <slug>.json from disk, mutates the "weebCentral" key, writes
    // back atomically. Does NOT touch any other field.
    bool patchWeebCentralBlock(const QString& seriesId,
                                const WeebCentralCacheBlock& block);
}
```

This is Codex's guardrail (anchor decision 15): re-serializing from
`MangaCatalog` would drop MangaFire-only fields that haven't been mirrored to
the C++ struct yet (e.g. `seriesTitleAlt`, `genres`, `mangazine`,
`malScoreRaw` — these were added in COMICS_MANGAFIRE_PIVOT Phase B.2 but the
`MangaCatalog` struct only carries a subset).

## UI shape

### Sources panel parity with Stream

The Comics Sources panel (`ComicsSourcesPanel`) mirrors Stream's
`StreamSourceList` shape:
- Header: "Sources for Volume N" (or "Loading sources…" while resolver works)
- Row content per source: badge + two-line text (name + release tag) +
  tier/source pill on right
- Click anywhere on a row → start that source

### Source row content

| Source | Badge | Line 1 | Line 2 | Pill |
|--------|-------|--------|--------|------|
| Nyaa torrent | "NY" | "Nyaa torrent" | release.name + uploader | Tier 1 / Tier 2 / Tier 3 |
| WeebCentral | "WC" | "WeebCentral" | (empty — no per-row variance) | "Scraped" or none |

The WeebCentral row stays minimal per Hemanth's "just WeebCentral, nothing
else" directive. The name is the row identity; no "(N chapters)" suffix, no
quality badge, no size estimate.

### Panel state machine

```
.------------------.
| Empty state      |  →  vol clicked  →  | Loading sources… |
| (no vol selected)|                     '------------------'
'------------------'                              ↓
                                          resolver finishes
                                                  ↓
                          ,-----------------------+------------------,
                          ↓                                          ↓
                  | Populated state |                       | Empty - no sources |
                  | Nyaa row(s)     |                       | (no Nyaa torrent + |
                  | WeebCentral row |                       | WC viable returned |
                  | (if viable)     |                       | Skip)              |
                  '-----------------'                       '--------------------'
                          ↓
                  user clicks WC row
                          ↓
                  pack starts, vol tile
                  flips to "Downloading 12%"
                  via volumeProgress signal
                          ↓
                  completes, vol tile
                  flips to "Open" + cover
                  via volumeCompleted signal
```

### `populateSourcesForRow(-1)` refactor (prereq per Codex)

`ComicsSeriesView::populateSourcesForRow(int legacyRowIndex)` currently uses
the old QTableWidget row index. The MangaFire tile-click path passes `-1`
(line 1301 in ComicsSeriesView.cpp), which is a placeholder. Spec prereq:
refactor to `populateSourcesForVolume(int volumeNumber)` so the resolver can
be invoked with the actual vol number from the catalog.

This is a small one-call-site change but it lives in the spec because it
unblocks the resolver wire-up.

### Source row label edit

`ComicsSourcesPanel` already has WeebCentral row support (Codex spotted this
during the audit). The current label says "WeebCentral pack" — change to
literal "WeebCentral" per anchor decision 2. One-line edit.

### Already-downloaded vol UX

When `MangaDownloadIndex` confirms a vol cbz exists:
- Vol tile state flips from "Download" button to "Open" button
- A small secondary "Show alternative sources" link sits beside it
- Clicking "Show alternative sources" populates the Sources panel with the
  full row list (Nyaa + WC if viable), allowing re-download from a different
  source if the user wants an upgrade

Per Stream-mode parity (anchor decision 9). Mirrors
`StreamSourceList::triggerDirectDownloadAt` behavior on already-downloaded
episodes.

## Failure paths & state guards

| Trigger | Behavior |
|---------|----------|
| WC search returns no matching series | `skip(NoSeriesMatch)`; no WC row appears; Nyaa rows render alone (or empty state if no Nyaa either) |
| WC fetchDetail HTTP failure | `skip(NetworkError)`; no WC row appears |
| WC chapter list has no chapters overlapping vol range | `skip(NoChapterOverlap)`; no WC row appears |
| WC has partial coverage of vol range (e.g. chapter 4 missing from 1-5) | `skip(IncompleteCoverage)`; no WC row appears |
| User navigates away mid-resolve | Late `viable`/`skip` signal arrives with stale `requestSerial`; receive guard drops it silently |
| WC pack succeeds → MangaDownloadIndex registers under MangaFire seriesId | Vol tile flips to "Open" via existing path |
| WC pack fails mid-flight | Existing `WeebCentralVolumePacker::volumeFailed` signal handled by the same wiring that handles torrent failures |
| Existing zero-page guard (MangaDownloader Fix A) | Catches scraper-returning-nothing failure inside the pack pipeline |

## File shape

| File | Op | Scope |
|------|----|-------|
| `src/core/manga/mangafire/MangaWeebCentralResolver.h` | NEW | ~80 LOC: class decl + nested ResolveKey / Verdict types |
| `src/core/manga/mangafire/MangaWeebCentralResolver.cpp` | NEW | ~200 LOC: pipeline + generation guard + in-flight cap + private scraper construction |
| `src/core/manga/MangaCatalogTypes.h` | EDIT | additive `WeebCentralCacheBlock` sub-struct + `MangaCatalog.weebCentral` field |
| `src/core/manga/LocalMangaCatalogLoader.cpp` | EDIT | additive `weebCentral` block deserialize (~30 LOC) |
| `src/core/manga/mangafire/MangaFireCatalogClient.{h,cpp}` | EDIT | new `patchWeebCentralBlock(seriesId, block)` static helper (~40 LOC) |
| `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` | EDIT | label "WeebCentral pack" → "WeebCentral"; populate WC row from resolver Viable verdict |
| `src/ui/pages/comics/ComicsSeriesView.cpp` | EDIT | `populateSourcesForRow(-1)` → `populateSourcesForVolume(int volumeNumber)` refactor + resolver consult |
| `src/ui/pages/ComicsPage.{h,cpp}` | EDIT | own `m_wcResolver`, instantiate alongside `m_mangafireClient`, connect viable/skip signals to ComicsSeriesView slots |
| `CMakeLists.txt` | EDIT | register `MangaWeebCentralResolver.cpp` in main app target |
| `tests/manga/MangaWeebCentralResolverTest.cpp` | NEW | pure-logic TDD for `filterChaptersToRange` + generation-key stale-guard simulation |

Net scope: 1 new class, 1 new test file, ~350 LOC across edits.

## Testing & verification

### Pure-logic TDD targets

`MangaWeebCentralResolver::filterChaptersToRange(chapterList, rangeStart,
rangeEnd)` is a pure function — given a chapter list and a range, return
the filtered list OR an "incomplete coverage" verdict.

Test cases:
- Full coverage: chapters [1,2,3,4,5], range [1,5] → Viable([1,2,3,4,5])
- Partial coverage: chapters [1,2,3,5], range [1,5] → Skip(IncompleteCoverage)
- No overlap: chapters [10,11,12], range [1,5] → Skip(NoChapterOverlap)
- Subset: chapters [1,2,3,4,5,6,7], range [3,5] → Viable([3,4,5])
- Empty list: chapters [], range [1,5] → Skip(NoChapterOverlap)
- Out-of-order: chapters [5,3,1,4,2], range [1,5] → Viable([1,2,3,4,5])

Generation-key guard test:
- Fire resolve(seriesA, vol 1, serial=1)
- Fire resolve(seriesA, vol 2, serial=2) — same series, different vol
- Stub-deliver serial=1's result; assert receive-guard drops it

### Manual smoke (Hemanth-driven)

1. Open Yu Yu Hakusho from library
2. Click Volume 1
3. Sources panel header → "Loading sources…" briefly
4. Sources panel populates: Nyaa row (if any trusted-uploader torrent indexed)
   + WeebCentral row (assuming WC has full coverage of ch 1-5)
5. Click WeebCentral row
6. Vol 1 tile state → "Downloading X%" with progress
7. Pack completes; vol 1 tile flips to "Open" with cover
8. Click "Open" → vol 1 cbz opens in the comic reader
9. Re-open Yu Yu Hakusho later: vol 1 tile shows "Open" instantly (state is
   persisted via MangaDownloadIndex)

### Cross-source coexistence smoke

1. Pick a series with both Nyaa-trusted-uploader torrents AND WeebCentral
   coverage
2. Download vol 1 via Nyaa torrent; vol 2 via WeebCentral
3. Both .cbz files land in the same canonical series dir
4. Both vols register correctly via MangaDownloadIndex; both vol tiles flip
   to "Open"
5. Re-opening the series: both vols show "Open" with covers

### Generation-key smoke

1. Open a fresh series with WC viable for vol 1 + vol 2
2. Click vol 1 quickly, then click vol 2 before vol 1's resolve finishes
3. Vol 2's WC row should appear; vol 1's late result should NOT append a
   second WC row to vol 2

## Phasing

**Single-phase ship.** Scope is bounded (~350 LOC across edits), single
brainstorm-locked design, no UI shape ambiguity remaining. Codex Trigger D
executes the full implementation in one pass; smoke verifies; commit.

If unexpected scope expansion surfaces during execution (e.g. resolver
needs more state than anticipated), split at the smoke gate: Phase 1 ships
the resolver + schema + JSON patch + tests; Phase 2 ships the UI wire-up.

## Risk + rollback

**Schema risk:** the additive `weebCentral` JSON block is backward
compatible. Older JSONs without the key parse cleanly. Rollback = revert
the deserializer extension; the on-disk JSON's extra key gets ignored.

**Identity-key risk:** the wrong-WC-match case (resolver picks the wrong
series from search) currently produces a wrong cbz under the right
seriesId. v1 ships with no override UI (YAGNI per anchor decision 8). If
this turns into a recurring user complaint, a future patch adds a "Wrong
series? Re-match." link in the Sources panel.

**Generation-key foot-gun:** if the receive guard's comparison logic is
buggy, late resolves could still leak into the wrong vol's Sources panel.
Mitigation: unit tests on the guard (testing target above) cover the
common stale-event scenarios.

**Shared-scraper foot-gun:** if a future refactor accidentally swaps
`m_scraper` from the private instance to the shared `MangaSourceRegistry`
one, signal cross-talk regresses. Mitigation: code comment in the resolver
constructor explicitly flags the private-instance requirement.

## Out of scope (deferred)

- WC seriesId override UI (Hemanth YAGNI; anchor decision 8)
- WC chapter cache TTL / invalidation policy (`chaptersFetchedAt` field
  reserved for future use)
- Multi-source result aggregation (e.g. "vol 1 partial from WC + filler from
  another scraper") — anchor decision 5 explicitly skips partial-source packs
- 3rd-party Mihon-style source registry refactor — Approach 3 from the
  brainstorm; correct shape when 4+ source types exist, not at 2
- Quality / size badge on the WeebCentral row — Hemanth "just WeebCentral,
  nothing else"
- Tankoban-cloud aggregator pivot — separate longer-term arc; out of scope
  for this wiring
- Pre-fetching WC chapter lists at MangaFire series-open time — eager step
  violates the lazy-fetch lock (anchor decision 6)

## Done definition

- `MangaWeebCentralResolver` class shipped at `src/core/manga/mangafire/`
- `MangaCatalog::weebCentral` field + `LocalMangaCatalogLoader` extension
- `MangaFireCatalogClient::patchWeebCentralBlock` atomic JSON-patch helper
- `ComicsSourcesPanel` label fix + WC row populate via resolver verdict
- `ComicsSeriesView::populateSourcesForVolume(int)` refactor in place
- `ComicsPage` owns and wires the resolver alongside the MangaFire client
- `tankoban_tests` GREEN on the resolver pure-logic + generation-guard cases
- `build_check.bat` GREEN
- Manual smoke per the matrix above passes end-to-end on Yu Yu Hakusho (or
  any catalog series with WC coverage)
- Hemanth signs off on the smoke
