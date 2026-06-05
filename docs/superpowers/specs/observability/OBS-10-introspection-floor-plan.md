# OBS-10 — The Introspection Floor Plan (`introspect-tree` / `introspect-object` / `introspect-actions` + `IDevInspectable`)

**Author:** Agent 5 (Library UX + Theme; Track D observability lead)
**Date:** 2026-06-05
**Status:** SPEC — ready for Codex Trigger-D implementation
**Priority:** #1 (Hemanth, 2026-06-05) — nothing jumps the line.
**Covers backlog items:** OBS-10 (generic introspection family) + OBS-19 (residue: formalize `devSnapshot()` → `IDevInspectable`). The acceptance test needs both, so they ship together.
**Schema:** bumps `tankoban.dev.v1.13` → `tankoban.dev.v1.14` (additive, non-breaking).
**Parent design:** `docs/superpowers/specs/2026-06-02-agent-observability-layer-design.md` § Pillar 1.

---

## 1. Strategic Intent

Today an agent cannot drive Tankoban the way Playwright drives a webpage or pywinauto drives PotPlayer. A standard Windows app self-describes through UI Automation; a webpage hands over its whole DOM. **Tankoban hands over nothing** — it is custom-painted Qt with a 140-key hand-wired `tankoctl` keyring and no floor plan. A live trial on 2026-06-05 proved the cost: opening "Grand Blue" from the comics continue-reading strip took **~7 discovery round-trips** because the continue-reading tiles are a blank wall — invisible to pywinauto, absent from `dump-ui`, and `comics-get-state` never surfaced the saved volume/page. PotPlayer and the web flew through the same trial in a few decisive calls because they self-describe.

This spec gives Tankoban a generic, read-only **"describe yourself" surface** — the in-process equivalent of the DOM — so any agent reads the floor plan instead of brute-forcing 140 keys. Three generic verbs cover the structure and live state of *every standard widget* with zero per-widget code, via Qt's meta-object system. A small `IDevInspectable` interface formalizes the already-pervasive `devSnapshot()` convention so the verbs also reach the **custom-painted residue** — the hand-drawn tiles (`VolumeTile`, `TileCard`), the reader (`ComicReader`), `SeekSlider`, `EpisodeTile` — whose state lives in plain C++ members that meta-object reflection cannot see.

**North-star acceptance test (the literal task that failed today):** a fresh agent does *"Comics → open Grand Blue from continue-reading → report volume, page, reading mode"* in **one or two introspection calls** by reading the floor plan, not 7+ rounds of discovery. See §7.

---

## 2. Ground Truth (verified 2026-06-05 against the live tree)

The implementation MUST follow these existing patterns exactly. All anchors verified this session.

### 2.1 The verb lifecycle (mirror `diag_timer_census` / OBS-1 end-to-end)
- **Client (`tools/tankoctl.cpp`):** CLI is kebab-case (`introspect-tree`), converted to snake_case (`introspect_tree`) on the wire. Request JSON: `{"cmd":"<snake>","seq":1,"payload":{...}}` over `QLocalSocket` named pipe `TankobanDevControl` (`tools/tankoctl.cpp:220`, `sendCommand()` at `466-506`). The **no-payload allowlist** is at `tools/tankoctl.cpp:1562-1652` — a verb that sends *no* args is added there; a verb *with* args is parsed into `payload` in its own `else if` branch BEFORE the allowlist (pattern: `dispatch-episode` at `600-615`).
- **Server (`src/devtools/DevControlServer.cpp:247-306`):** parses the line, hands off at line 299 to `m_window->handleDevCommand(cmd, seq, payload)` — **synchronous on the GUI thread** (a slow handler blocks the UI). NOTE (corrected per Codex spec review 2026-06-05): the 500 ms figure is only the server's per-socket read/write wait, NOT a command-execution cap; the client (`tankoctl`) waits up to 60 s for a reply. The caps in §5 exist to keep the GUI-block short and the payload bounded, not to fit a 500 ms execution budget. Reply contract: `{"type":"reply","seq":<int>,...}` or `{"type":"error","seq":<int>,"code":"<UPPER_SNAKE>","message":"..."}` (`DevControlServer.h:22-25`).
- **Dispatch (`src/ui/MainWindow.cpp:2649-2701`):** the `isSysCmd` prefix block routes `app_`/`settings_`/.../`diag_` to `m_systemIntrospection->dispatch(...)`. Line ~2676 is where `diag_` was added; `introspect_` is added in the same list. Write-capable commands are gated on `TANKOBAN_DEV_WRITE=1` via `SystemIntrospection::isWriteCapable(cmd)` BEFORE forwarding — **our verbs are read-only, so they are NOT added to `isWriteCapable`.**
- **Handler (`src/devtools/SystemIntrospection.{h,cpp}`):** `dispatch()` (`.cpp:259-276`) routes by prefix to `handle<Prefix>(cmd, payload, reply)` → returns `bool` (true = recognised). Success merges via `mergeReply(r, {...})` (`.cpp:120-127`); error via `setError(r, code, msg)` (`.cpp:129-133`). `diag_` → `handleDiag` is the reference handler (`.cpp:1094-1136`).

### 2.2 Reusable helpers already in `SystemIntrospection.cpp`
- `geometryObject(QWidget*)` — emits `{x,y,width,height}` (used by `widgetSummary`).
- `widgetSummary(QWidget*)` (`.cpp:149-160`) — `{objectName, className, title, visible, enabled, geometry}`.
- Resolve-by-name loop (`.cpp:913-918`): walks `QApplication::topLevelWidgets()` + `findChild<QWidget*>(name)`.
- `QJsonValue::fromVariant(v)` (`.cpp:365`) — the property-value serializer.
- `app_get_shortcut_table` handler (`.cpp:319-344`) — `QShortcut` enumeration to reuse for `introspect-actions`.

### 2.3 The existing tree walk (DO NOT reuse verbatim — it lacks property + interface reflection)
`src/devtools/UiInteractionDispatcher.cpp`: `snapshotObject(QObject*)` (`69-81`) emits `{objectName,className,isWidget,visible,enabled,geometry,text}`; `listVisitor()` (`160-175`) recurses `children()`; `findByName()` (`38-43`). **Confirmed: NO `Q_PROPERTY` enumeration anywhere in the codebase** — that reflection is exactly the multiplier this spec adds.

### 2.4 The `devSnapshot()` convention (16 existing implementations)
~16 classes have a `devSnapshot()` **member method**, none declared via a shared interface. Most are `QJsonObject devSnapshot() const`; **exception (per Codex review): `VideosPage::devSnapshot(int limit = 50) const`** takes an optional arg — so a future `IDevInspectable` retrofit of the 16 must reconcile that signature (not this commission's problem; residue widgets all use the no-arg form). Page-level: `MainWindow` (`MainWindow.cpp:1727`), `ComicsPage` (`ComicsPage.cpp:4602`), `StreamPage`, `VideosPage` (arg variant), `BooksPage`, `TankorentPage`, `TankoLibraryPage`. View/component: `ComicsSeriesView`, `StreamDetailView`, `ShowView`, `ComicsSourcesPanel`, `VideoPlayer` (`VideoPlayer.cpp:4089`), `SidecarProcess`, `SubtitleOverlay`, `BookReader`, `BookDownloader`. **This spec does NOT retrofit these 16** (they stay reachable via `dump_ui`); it formalizes the interface and adopts it on the *residue* widgets below. Retrofitting the 16 to `IDevInspectable` is a documented fast-follow (§8).

### 2.5 The residue (verified — meta-object reflection CANNOT reach these)
| Widget | Header | Base | Namespace | objectName? | State members (no `Q_PROPERTY`) |
|---|---|---|---|---|---|
| `VolumeTile` | `src/ui/pages/comics/VolumeTile.h:54` | `QFrame` | `tankoban::ui::comics` | yes `"VolumeTile"` (`.cpp:167`) — non-unique | `m_data` (`VolumeTileData`: sourceId, seriesId, volumeNumber, title, chapterRange, pages, publishDate, isRawScan, upgradeAvailable), `m_state` (`VolumeTileState`: state enum, progressPct, statusText, cbzPath, provenance), `m_readProgressFraction`, `m_selected` |
| `TileCard` | `src/ui/pages/TileCard.h:9` | `QFrame` | (global) | yes `"TileCard"` (`.cpp:26`) — **non-unique, all cards share it** | `m_title`, `m_subtitle`, `m_thumbPath`, `m_progressFraction`, `m_pageBadge` ("Page N/M"), `m_countBadge`, `m_status`, `m_provenance`, `m_isNew`, `m_isFolder`, `m_selected` + **dynamic props** set by callers (`filePath`, `seriesTitle`, `seriesId`, `imdbId`, `catalogueId`, …) |
| `TileStrip` | `src/ui/pages/TileStrip.h:11` | `QWidget` | (global) | no | `m_tiles` (`QList<TileCard*>`), `m_filteredOut`, `m_selected`, `m_mode`, `m_density`; has `Q_PROPERTY(int scrollOffsetX)`; cards via `tiles()` (`.h:35`), `tileAt(QPoint)→TileCard*` (`.cpp:183-192`) |
| `ComicReader` | `src/ui/readers/ComicReader.h:316` | `QWidget` | (global) | no | `m_cbzPath` (481), `m_seriesName` (490), `m_seriesCbzList` (489), `m_currentPage` (484, 0-based), `m_pageNames` (482, count), `m_readerMode` (507, enum `DoublePage`/`ScrollStrip`), `m_fitMode` (508, `FitPage`/`FitWidth`/`FitHeight`), `m_rtl` (528), `m_isVolumeX` (497), `m_isStitchedCompilation` (504) |
| `SeekSlider` | `src/ui/player/SeekSlider.h:6` | `QSlider` | (global) | no | `m_durationSec`, `m_chapterMarkersMs`, `m_bufferedRanges` (`QList<QPair<qint64,qint64>>`), `m_bufferedTotalBytes` |
| `EpisodeTile` | `src/ui/pages/stream/EpisodeTile.h:41` | `QFrame` | `tankoban::stream::theatre` | yes `"EpisodeTile"` (`.cpp:15`) | `m_data` (`EpisodeTileData`: season, episode, title, sizeBytes, alreadyHave), `m_episodeState` (state, progressPct, provenance), `m_hasIndexEntry`, `m_imdbId` |

**Domain note (Rule 14):** `VolumeTile`/`TileCard`/`TileStrip`/`ComicReader` are Agent 1's turf; `SeekSlider` Agent 3's; `EpisodeTile` Agent 4's. The residue changes are **purely additive, read-only, zero-behavior-change** (a new `const` method + an interface base). Agent 0 coordinates; owners are notified via the RTC; the review gate covers it. No existing code path changes.

---

## 3. Files (§Files — match exactly, no edits outside this list)

**NEW**
- `src/devtools/IDevInspectable.h` — the interface.

**MODIFY — Track D core (Agent 5 domain)**
- `src/devtools/SystemIntrospection.h` — `handleIntrospect` decl + includes.
- `src/devtools/SystemIntrospection.cpp` — `dispatch()` route, `handleIntrospect` impl + helpers, `commandList()` additions.
- `src/ui/MainWindow.cpp` — add `introspect_` to the `isSysCmd` prefix list (~2676); bump the `ping` schema string to `tankoban.dev.v1.14` (~1944).
- `tools/tankoctl.cpp` — three verbs (arg parsing + allowlist + usage text).
- `cmake/TankobanSources.cmake` — add `src/devtools/IDevInspectable.h` to HEADERS (new `.h` only; no new `.cpp`). NOTE (per Codex review): editing the CMake include list may still trigger a reconfigure on next build — let `build_check.bat` reconfigure naturally and confirm the header is picked up (a header-only addition compiles via the TUs that `#include` it; ensure at least `SystemIntrospection.cpp` and each residue `.cpp` include it).

**MODIFY — residue widgets (cross-domain, additive: `+ public IDevInspectable`, `devSnapshot() const override`)**
- `src/ui/pages/comics/VolumeTile.{h,cpp}`
- `src/ui/pages/TileCard.{h,cpp}`
- `src/ui/pages/TileStrip.{h,cpp}`
- `src/ui/readers/ComicReader.{h,cpp}`
- `src/ui/player/SeekSlider.{h,cpp}`
- `src/ui/pages/stream/EpisodeTile.{h,cpp}`

---

## 4. The interface — `src/devtools/IDevInspectable.h`

```cpp
// src/devtools/IDevInspectable.h
//
// OBS-19 / Pillar 1 residue strategy (2026-06-05, Agent 5).
// Formalizes the pervasive `QJsonObject devSnapshot() const` convention into a
// minimal polymorphic interface so the generic `introspect-object` verb can
// reach the live state of custom-painted widgets whose data lives in plain C++
// members that Qt meta-object reflection cannot see (VolumeTile, TileCard,
// ComicReader, SeekSlider, EpisodeTile, ...).
//
// A widget adopts it by multiple-inheriting alongside its QWidget base:
//     class VolumeTile : public QFrame, public tankoban::devtools::IDevInspectable
// and overriding devSnapshot(). introspect-object recovers it via
//     dynamic_cast<const tankoban::devtools::IDevInspectable*>(qobjectPtr)
// (RTTI is on by default under MSVC /GR). The interface is intentionally NOT a
// QObject and declares NO signals/slots, so it composes cleanly with moc.

#pragma once

#include <QJsonObject>

namespace tankoban::devtools {

class IDevInspectable {
public:
    virtual ~IDevInspectable() = default;
    // Cheap, side-effect-free snapshot of the object's live state. MUST NOT
    // allocate unboundedly, trigger network/disk, or mutate the object. Runs
    // on the GUI thread inside the 500 ms bridge window.
    virtual QJsonObject devSnapshot() const = 0;
};

} // namespace tankoban::devtools
```

---

## 5. The three verbs (`handleIntrospect` in `SystemIntrospection.cpp`)

Add the prefix route in `dispatch()` (alongside the existing prefixes, before the trailing `return false;`):
```cpp
if (cmd.startsWith(QLatin1String("introspect_"))) return handleIntrospect(cmd, payload, reply);
```
Declare `bool handleIntrospect(const QString& cmd, const QJsonObject& p, QJsonObject& r);` in the `private:` block of `SystemIntrospection.h` (next to `handleDiag`), and `#include "devtools/IDevInspectable.h"` in the `.cpp`.

All three are **read-only**, run on the GUI thread, and emit hard caps + a `truncated` flag so introspection never becomes the perf event it observes.

### 5.1 `introspect_tree` — the structural floor plan
**Payload:** `{ "root"?: string, "depth"?: int, "maxNodes"?: int }`
- `root` — objectName/className to start from; default = the `MainWindow` (resolve via `SystemIntrospection`'s `m_window` member — NOT `m_mainWindow`; corrected per Codex review). If `root` given but not found → `error WIDGET_NOT_FOUND`.
- `depth` — max recursion depth; default `-1` (unbounded, subject to `maxNodes`). `0` = the root node only.
- `maxNodes` — hard cap on total emitted nodes; default `2000`. On hitting it, stop and set top-level `"truncated": true`.

**Per-node shape:**
```json
{
  "objectName": "TileCard",
  "className": "TileCard",
  "isWidget": true,
  "visible": true,
  "enabled": true,
  "geometry": { "x": 0, "y": 0, "width": 200, "height": 360 },
  "text": "",            // best-effort label: QAbstractButton::text / QLabel::text /
                          // QLineEdit::text / QComboBox::currentText / window title; "" otherwise
  "inspectable": true,   // dynamic_cast<const IDevInspectable*> succeeded
  "childCount": 4,
  "children": [ ... ]    // recurse over children(); QObjects (not just QWidgets) included
}
```
**Top-level reply:** `{ "root": "<objectName>", "nodeCount": <int>, "truncated": <bool>, "tree": <node> }`.
Reuse `geometryObject()`; add a file-local `bestEffortText(QObject*)` mirroring `UiInteractionDispatcher::widgetText`. Recurse over `obj->children()` so non-widget QObjects appear too (the agent sees the full object graph, not just the visible widget subtree).

### 5.2 `introspect_object` — the coverage multiplier (selects by objectName OR className)
**Payload:** `{ "selector": string (required), "root"?: string, "maxObjects"?: int }`
- **`selector` matches by `objectName()` OR `className()`** — this is mandatory and load-bearing (corrected per Codex spec review 2026-06-05): the acceptance-test widgets `ComicReader`, `SeekSlider`, and `TileStrip` set **no objectName at all**, so an objectName-only resolver would make them unreachable and the §7 test would fail. Resolution order per object: if `obj->objectName() == selector` OR `obj->metaObject()->className() == selector`, it matches. (className match also elegantly handles the inverse problem — cards share `objectName("TileCard")`, so both name and class collapse to the same useful "give me all the cards" query.)
- Resolve **all** matching objects, scoped to the `root` subtree if given (resolve `root` by objectName/className → use the FIRST match as the search root; else whole app: iterate `QApplication::topLevelWidgets()` + `findChildren<QObject*>()` on each; dedupe by pointer).
- Cap matches at `maxObjects` (payload override; default `200`); set top-level `"truncated": true` if exceeded.
- Empty/missing `selector` → `error BAD_REQUEST`. Zero matches → reply with `"objects": []` (NOT an error — absence is information).
- Back-compat alias: accept `objectName` as a synonym for `selector` if `selector` is absent (so older callers/notes keep working).

**Per-object shape:**
```json
{
  "objectName": "TileCard",
  "className": "TileCard",
  "isWidget": true,
  "visible": true, "enabled": true,
  "geometry": { ... },
  "windowTitle": "",
  "properties": {            // every Q_PROPERTY: metaObject()->property(i) for i in
    "scrollOffsetX": 0       // [0, propertyCount()); value via QJsonValue::fromVariant(p.read(obj))
  },
  "dynamicProperties": {     // dynamicPropertyNames() -> property(name)
    "filePath": "C:/.../Grand Blue/Volume 03.cbz",
    "seriesTitle": "Grand Blue Dreaming"
  },
  "inspectable": true,
  "devSnapshot": { ... }     // present iff dynamic_cast<const IDevInspectable*> succeeded
}
```
**Top-level reply:** `{ "selector": "<query>", "matchCount": <int>, "truncated": <bool>, "objects": [ ... ] }` (each object's own `objectName` and `className` are inside the per-object shape).
Property enumeration walks `obj->metaObject()->property(i)` from `0` to `propertyCount()` (includes inherited Qt properties — that is fine and useful; do NOT filter to `propertyOffset()`). Guard `p.isReadable()` before `p.read(obj)`. Serialize with `QJsonValue::fromVariant`; if a variant is not JSON-representable, emit its `typeName()` string as the value.

### 5.3 `introspect_actions` — the available-actions list
**Payload:** none (add to the no-payload allowlist).
**Reply:**
```json
{
  "actions": [
    { "text": "Play", "objectName": "", "shortcut": "Space", "enabled": true,
      "visible": true, "checkable": false, "checked": false,
      "ownerObjectName": "VideoPlayer", "ownerClassName": "VideoPlayer" }
  ],
  "shortcuts": [ /* reuse app_get_shortcut_table shape: key, keys[], enabled, owner* */ ]
}
```
Enumerate `QAction` via `findChildren<QAction*>()` across `QApplication::topLevelWidgets()` (dedupe by pointer); `shortcut` via `a->shortcut().toString(QKeySequence::PortableText)`. Reuse the `QShortcut` enumeration from `app_get_shortcut_table` (`SystemIntrospection.cpp:319-344`) for the `shortcuts` array.

### 5.3a tankoctl CLI surface (client arg parsing — corrected per Codex review)
Define the kebab-case CLI explicitly in `tools/tankoctl.cpp` (these take args, so they parse into `payload` in their own `else if` branches BEFORE the no-payload allowlist — pattern: `dispatch-episode` at `600-615`; only `introspect-actions` goes in the allowlist):
- `introspect-tree [root] [depth] [maxNodes]` — all positional + optional. `root` (string, default omitted → server uses MainWindow), `depth` (int), `maxNodes` (int). Parse present args into `payload{root,depth,maxNodes}`; integers via `toInt(&ok)` with a `BAD_REQUEST`-style usage error on non-integer depth/maxNodes.
- `introspect-object <selector> [root] [maxObjects]` — `selector` required (else print usage, return 64); `root` (string) + `maxObjects` (int) optional. Parse into `payload{selector,root,maxObjects}`.
- `introspect-actions` — no args; add to the no-payload allowlist at `tools/tankoctl.cpp:1562-1652`.
Add a usage/help line for each (mirror neighboring verbs). Wire names convert kebab→snake (`introspect-tree`→`introspect_tree`) exactly as the existing v1.9 system commands do.

### 5.4 Schema + catalogue
- Add the three commands to `SystemIntrospection::commandList()`.
- Bump the `ping` schema string in `MainWindow.cpp` (~1944) from `tankoban.dev.v1.13` to `tankoban.dev.v1.14`. (Recap note: the ping string lagged at v1.11 before OBS-1 set it to v1.13 — confirm it currently reads v1.13 and advance to v1.14.)

---

## 6. Residue `devSnapshot()` bodies (additive, read-only)

Each residue widget: add `#include "devtools/IDevInspectable.h"`, change the class to also inherit `public tankoban::devtools::IDevInspectable`, declare `QJsonObject devSnapshot() const override;`, and implement it. **No other change to these classes.** moc handles the extra non-QObject base. Keep the emitted keys snake_or_camel consistent with neighboring `devSnapshot()` impls in the same domain.

- **`VolumeTile`** → `{ sourceId, seriesId, volumeNumber, title, chapterRange, pages, isRawScan, upgradeAvailable, state (enum→string: NotStarted/Queued/Downloading/Complete/Failed), progressPct, statusText, cbzPath, provenance, readProgressFraction, selected }`.
- **`TileCard`** → `{ title (m_title), subtitle (m_subtitle), thumbPath, progressFraction, pageBadge, countBadge, status, provenance, isNew, isFolder, selected, downloadingChip }`. **Do NOT** re-enumerate dynamic properties here — `introspect_object` already emits those; keep `devSnapshot()` to the private members reflection can't reach.
- **`TileStrip`** → `{ mode, density, tileCount (m_tiles.size()), selectedCount, filteredOutCount, scrollOffsetX }`. (Cards themselves are reached individually via `introspect_object TileCard`; the strip snapshot is the index.)
- **`ComicReader`** → `{ currentFile (m_cbzPath), seriesName, currentPage (m_currentPage, 0-based), pageCount (m_pageNames.size()), readerMode (DoublePage/ScrollStrip), fitMode (FitPage/FitWidth/FitHeight), rtl, isVolumeX, isStitchedCompilation, seriesVolumeCount (m_seriesCbzList.size()) }`. This is the object that answers "volume, page, reading mode" in the acceptance test.
- **`SeekSlider`** → `{ value, minimum, maximum, durationSec, chapterMarkerCount, bufferedRangeCount, bufferedTotalBytes, bufferedFraction (computed) }`.
- **`EpisodeTile`** → `{ season, episode, title, sizeBytes, alreadyHave, state (enum→string), progressPct, provenance, hasIndexEntry, imdbId }`. **State enum (corrected per Codex review):** `EpisodeTileState` maps to the `StreamDownloadIndex::Entry` states (`Complete`/`Pending`/`Downloading`/`Failed` — NOT the comics-style `Queued`); read the actual enum in `EpisodeTile.h` and stringify those exact names.

---

## 7. Acceptance Test (the literal task that failed today)

With the app running under `build_and_run.bat` (`--dev-control` auto-set), a fresh agent reproduces *"open Grand Blue from continue-reading → report volume, page, reading mode"* in **≤ 2 introspection calls** plus the existing open action:

1. **Locate (1 call):** `out\tankoctl.exe introspect-object TileCard` → returns the array of all cards; each carries `devSnapshot` (`title`, `subtitle`, `pageBadge`) + `dynamicProperties` (`filePath`, `seriesId`, `seriesTitle`, …). The agent finds the card whose `title`≈"Grand Blue" and reads its identity (`filePath` / `seriesId`). *(This single call replaces the 7-round crawl — it is the core deliverable.)*
2. **Open (existing action — open-seam note):** open it via whatever existing comics open verb fits the identity read in step 1. **Honesty (per Codex spec review):** the current `comics-open-chapter` takes `seriesId volume chapter` (not a bare `filePath`), and `ui-click` targets an objectName that is non-unique for `TileCard` — so a *single* clean "open this exact CR card" verb may not exist today. That open-by-card seam, if missing, is a **one-line Agent-1 fast-follow (out of scope for this introspection commission)** — it does NOT block the introspection deliverable. The acceptance bar for OBS-10 is the discovery+report introspection calls, not the open mechanism.
3. **Report (1 call):** `out\tankoctl.exe introspect-object ComicReader` → `devSnapshot` returns `{ currentFile, seriesName, currentPage, pageCount, readerMode, ... }` → agent reports **volume** (from `currentFile`/`seriesName`), **page** (`currentPage`/`pageCount`), **reading mode** (`readerMode`). (Note: `ComicReader` has no objectName — this call works ONLY because §5.2's selector matches by className. That is the load-bearing design fix.)

**Pass bar (OBS-10 deliverable):** the two `introspect-object` calls above (locate + report), or one `introspect-tree comics` + one `introspect-object`, replace 7+ rounds of guessing. If discovery is as fast as Playwright reading a DOM, OBS-10 is done. The open-by-card action gap (step 2) is tracked separately and is NOT a gate on this commission. Smoke evidence (the JSON replies + timing) goes in the RTC.

Also smoke the generic surface directly:
- `introspect-tree` (no args) → returns a bounded tree rooted at MainWindow, `truncated` correct at the cap.
- `introspect-object <a standard widget, e.g. LibrarySortCombo>` → `properties` includes live Qt properties (e.g. `currentText`), proving zero-per-widget reflection.
- `introspect-actions` → non-empty `actions`/`shortcuts`.

---

## 8. Constraints, hazards, fast-follows

- **Read-only:** none of the three verbs is write-capable; do NOT add them to `isWriteCapable`; they run under `--dev-control` like other `diag_`/`app_` reads.
- **GUI-thread (no 500 ms execution cap):** the bridge dispatches synchronously on the GUI thread, so a slow handler blocks the UI; the client reply timeout is ~60 s, and the server's 500 ms is only a per-socket read/write wait (corrected per Codex review). The `maxNodes` (2000) / `maxObjects` (200) / `depth` caps keep the GUI-block short and the payload bounded. `devSnapshot()` impls MUST be cheap and side-effect-free (the existing 16 are).
- **No model dump:** `introspect_model` from the parent design is deliberately **out of scope** here (lazy-model `fetchMore`/thread-safety hazards). Fast-follow.
- **RTTI:** `dynamic_cast` to the interface requires `/GR` (MSVC default — confirm not disabled in `CMakeLists.txt`).
- **Non-unique objectNames are expected:** `introspect_object` returns an array by design; do not "fix" cards to unique names in this commission (that is Agent 1's call and a separate change).
- **Fast-follows (NOT this commission):** (a) retrofit the 16 existing `devSnapshot()` classes to inherit `IDevInspectable` so `introspect-object` reaches pages/views too; (b) `introspect_model`; (c) `input-routing` introspection (focus chain + event filters + grabs) per design § Pillar 1 / OBS-22; (d) stable unique objectNames on key comics containers (Agent 1).

---

## 9. Definition of Done (for the review gate)
1. `build_check.bat` = BUILD OK (and the new header compiles into at least one TU).
2. All three verbs respond over `tankoctl` against a live app; replies match the shapes in §5.
3. The §7 acceptance test passes in ≤ 2 introspection calls; JSON evidence captured.
4. `introspect-object` returns Q_PROPERTY values, dynamic properties, AND `devSnapshot` for residue widgets.
5. Diff scope == §3 Files list exactly (no scope creep).
6. Schema string reads `tankoban.dev.v1.14`; `ping` lists the three new commands.
7. Residue changes are additive only — no existing behavior altered (reviewer confirms by reading each widget diff).
8. ASCII-clean on any emitted protocol/markdown lines.
