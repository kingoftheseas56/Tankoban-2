# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources), cross-agent with Agent 3 (Video Player) for Batch 5.2
Subsystem: Tankostream Phase 5 — Subtitles (Batches 5.1 SubtitlesAggregator, 5.2 SidecarProcess subtitle wrappers, 5.3 Qt-side SubtitleMenu)
Reference spec: `STREAM_PARITY_TODO.md:226-265` Phase 5 block + success criterion at `:259` ("user switches between embedded English + fetched OpenSubtitles Spanish, loads a custom .srt, live-adjusts delay by ±500ms with visible sync change").
Objective: subtitles menu visible + functional during playback; external .srt loading works; offset/delay live-adjust works.
Files reviewed:
- `src/core/stream/SubtitlesAggregator.h/.cpp` (5.1, Agent 4 — multi-addon subtitle fan-out + OpenSubtitles hint threading + 30-min cache)
- `src/ui/player/SidecarProcess.h/.cpp` (5.2, Agent 3 — six public wrappers + five signals + track cache + QNAM URL download; Qt-side composition over existing sidecar commands)
- `src/ui/player/SubtitleMenu.h/.cpp` (5.3, Agent 4 — popover with Off/embedded/addon/file choices + delay/offset/size sliders)
- `src/ui/player/VideoPlayer.h/.cpp` (5.3 wiring: `setExternalSubtitleTracks`, `m_subMenu` ownership, context-menu dispatch)
- `src/ui/player/VideoContextMenu.h/.cpp` (`OpenSubtitleMenu` enum entry)
- `src/ui/player/KeyBindings.cpp` (T-key binding)
- `src/ui/pages/StreamPage.h/.cpp` (5.3 wiring: `SubtitlesAggregator` ownership, `subtitlesReady → setExternalSubtitleTracks`, `SubtitleLoadRequest` fan-out in `onStreamPicked`)
- `CMakeLists.txt` (additive)
Cross-reference:
- Agent 7 prototype: `agents/prototypes/5.1_subtitles_aggregator.cpp`
- Agent 7 prototype: `agents/prototypes/5.2_sidecar_protocol_extension.cpp`
- OpenSubtitles v3 manifest pattern — videoHash + videoSize + filename extras

Date: 2026-04-14

### Scope

Three-batch cross-agent phase: 5.1 is Agent 4's new multi-addon subtitle aggregator; 5.2 is Agent 3's Qt-side wrapper composition over existing sidecar commands (decided to NOT touch the sidecar binary because its `subtitle_renderer` already has libass + PGS rendering end-to-end); 5.3 is Agent 4's new Qt-side menu consuming both 5.1 and 5.2. Pre-flight gate (spec `:239` — "before 5.2 starts, confirm the sidecar source is modifiable in-repo vs prebuilt binary") was resolved in-session by Agent 3 at chat.md:9373: sidecar source IS modifiable at `C:/Users/Suprabha/Desktop/TankobanQTGroundWork/native_sidecar/src/` but 5.2 doesn't need to touch it because the four base commands (`set_tracks`, `set_sub_delay`, `set_sub_style`, `load_external_sub`) plus the sidecar's libass + PGS renderer cover the spec's six commands via Qt-side composition. Static read only; Hemanth's runtime smoke is the functional oracle.

### Parity (Present)

**Batch 5.1 — SubtitlesAggregator**

- **Multi-addon parallel fan-out via `findByResourceType("subtitles", type)`.** Spec `STREAM_PARITY_TODO.md:232`. Code: `SubtitlesAggregator.cpp:141-147` — queries every enabled addon whose manifest advertises a `subtitles` resource for the requested type. ✓
- **Stream behaviorHints threaded into extras for OpenSubtitles matching.** Spec `:234`. Code: `buildSubtitleExtra` at `SubtitlesAggregator.cpp:43-72`:
  - `videoHash` → emitted as both `videoHash` (camelCase) AND `video_hash` (snake_case) for dual-spelling addon compatibility
  - `videoSize` → same dual-spelling
  - `filename` → falls back to `source.fileNameHint` then `stream.name` if the behaviorHint is empty
  
  Defensive extra-emit shape handles both Stremio v3-style addons (camelCase) and legacy/community ones (snake_case). ✓
- **Parse responses into `QList<SubtitleTrack>`.** Spec `:233`. Code: `parseSubtitleArray` at `:74-109` — required field `url` validated; `id` defaults to URL if missing; `lang` falls back to `language` field (community addon variant) then `"und"`; `label` falls back to `title`. ✓
- **Dedup via `canonicalTrackKey`.** Code: `canonicalTrackKey` at `:34-41` — `id|lang|url` lowercased + percent-encoded. Prevents the same track appearing twice when two addons both return it. ✓
- **Origin tracking via `originByTrackKey` map.** Spec implicit (for UI labeling). Code: `SubtitlesAggregator.h:38-40` signal signature + `SubtitlesAggregator.cpp:242` populates map per-track as we merge. Lets the menu show "— {addonId}" per track at `SubtitleMenu.cpp:332-334`. ✓
- **30-minute TTL cache** via `QHash<QString, CacheEntry> m_cache` at `SubtitlesAggregator.h:76-78`. Cache key at `:160-168` combines type + id + behaviorHints fingerprint. Re-opening the same stream hits cache immediately. Stale entries purged at `:138` on TTL miss. ✓
- **Stale-worker guard — shared_ptr<bool> handled + sameRequest match.** Code: `SubtitlesAggregator.cpp:190-221`. Same clean pattern as Phase 4's StreamAggregator. No late-callback pollution across back-to-back `load()` calls. ✓
- **Per-addon error isolation via `subtitlesError(addonId, msg)`.** Code: `:249-258`. Failure in one addon doesn't block the others. `m_pendingResponses` correctly decrements on both success and failure paths at `:260-273`. ✓

**Batch 5.2 — SidecarProcess subtitle wrappers (Agent 3)**

- **Six new public methods matching the spec's JSON protocol.** Spec `STREAM_PARITY_TODO.md:242-248`. Code: `SidecarProcess.h:72-90`:
  - `listSubtitleTracks() const` → sync getter over the cache populated by `tracksChanged`; matches spec's `listSubtitleTracks` → `{tracks: [{index, lang, title}]}`. ✓
  - `sendSetSubtitleTrack(int index)` → maps index → cached sidecarId → `sendSetTracks("", id)`; `-1` sends `"off"`. Matches spec `:244`. ✓
  - `sendSetSubtitleUrl(QUrl, offsetPx, delayMs)` → local-file shortcut + QNAM download path with 15s timeout + extension preservation + temp file parented to `this` with `setAutoRemove(true)` + composes `sendLoadExternalSub` + `sendSetSubDelay` + `pushSubStyle`. Matches spec `:245`. ✓
  - `sendSetSubtitlePixelOffset(int y)` → composes full `set_sub_style` via `pushSubStyle`. Matches spec `:246`. ✓
  - `sendSetSubtitleDelayMs(int ms)` → int-typed alias forwarding to existing `sendSetSubDelay(double)`. Matches spec `:247`. ✓
  - `sendSetSubtitleSize(double scale)` → clamped to `[0.5, 3.0]`, updates cached scale, composes style. Matches spec `:248`. ✓
- **Five new signals for 5.3 slot wiring.** Code: `SidecarProcess.h:119-123`:
  - `subtitleTracksListed(QList<SubtitleTrackInfo>, int activeIndex)` — fired from `updateSubtitleCache` on every `tracks_changed`; runs BEFORE `emit tracksChanged` so listeners see consistent cached state.
  - `subtitleTrackApplied(int index)` — ack after `sendSetSubtitleTrack`.
  - `subtitleUrlLoaded(QUrl, localPath, bool ok)` — download completion ack.
  - `subtitleOffsetChanged(int pixelOffsetY)` / `subtitleSizeChanged(double scale)` — echo acks for live slider UI. ✓
- **`SubtitleTrackInfo` struct + Q_DECLARE_METATYPE at `:19-27`** — Qt-side descriptor with index + sidecarId + lang + title + codec + external flag. Works as signal parameter. ✓
- **`pushSubStyle()` helper composes the atomic `set_sub_style` payload** — sidecar's command is atomic over {font_size, margin_v, outline} triple, so individual setters have to recompose. Correct. ✓
- **Pre-flight gate resolved: sidecar source IS modifiable, but this batch didn't need to touch it.** Agent 3 disclosed at chat.md:9373 that sidecar's `subtitle_renderer.cpp` already implements libass + PGS + multi-threaded `ass_render_frame`. The four base commands (`set_tracks`, `set_sub_delay`, `set_sub_style`, `load_external_sub`) cover everything the spec asks for. Qt-side composition is a valid alternative to sidecar protocol extension for this batch — if future needs (server-side URL download; bitmap event; etc.) require sidecar changes, they land in a follow-up. ✓

**Batch 5.3 — Qt-side SubtitleMenu (Agent 4)**

- **New file `SubtitleMenu.h/.cpp` in `src/ui/player/`** per spec `:253` (spec gave OR choice between player-side or Tankostream-side; Agent 4 chose player-side, natural for per-playback UI). ✓
- **Trigger via T keyboard shortcut** at `KeyBindings.cpp:31` (`{"open_subtitle_menu", "Open subtitles menu", Qt::Key_T, Qt::NoModifier}`) ✓. Spec `:254` said "S or context-menu item"; Agent 4 chose T to avoid colliding with the existing S/Shift+S bindings (track cycler). Deliberate divergence documented in the batch post; T is unused in the existing KeyBindings table. ✓
- **Trigger via player context menu** at `VideoContextMenu.h:32` (`OpenSubtitleMenu`) dispatched at `VideoPlayer.cpp:1683-1685`. ✓
- **Choice list: Off + embedded + addon + LocalFile.** Spec `:255`. Code: `SubtitleMenu::rebuildChoices` at `SubtitleMenu.cpp:265-314` populates:
  - `ChoiceKind::Off` (always first) → `sendSetSubtitleTrack(-1)`
  - `ChoiceKind::Embedded` from sidecar's track cache → `sendSetSubtitleTrack(idx)`
  - `ChoiceKind::Addon` from SubtitlesAggregator → `sendSetSubtitleUrl(url, offsetPx, delayMs)`
  - `ChoiceKind::LocalFile` from `QFileDialog` → `sendSetSubtitleUrl(QUrl::fromLocalFile(path), ...)` — sidecar's local-file shortcut handles no-download path. ✓
- **Delay slider ±5000 ms / 50 ms step.** Spec `:257` "live-update via sidecar." Code: `SubtitleMenu.cpp:92-107` — valueChanged → `sendSetSubtitleDelayMs(ms)`. Default 0. ✓
- **Offset slider -120..+200 px / 2 px step.** Code: `:115-130` — valueChanged → `sendSetSubtitlePixelOffset(px)`. ✓
- **Size slider 50..200 % / 5 % step.** Code: `:138-153` — valueChanged → `sendSetSubtitleSize(pct/100.0)`. ✓
- **"Load from file…" via QFileDialog.** Spec `:255`. Code: `:224-245` — `.srt *.ass *.ssa *.sub *.vtt` filter; selected path stored + added to choice list as LocalFile; auto-selected after load. ✓
- **TrackPopover-style anchor via `toggle(QWidget* anchor)`** at `SubtitleMenu.cpp:182-194`. Anchored above `m_trackChip` via `mapTo` + geometry math at `:355-376`. App-wide click-filter for click-outside-dismiss at `:378-407`. Consistent with existing player UI patterns. ✓
- **External-track clearing on file change** at `VideoPlayer.cpp:275` — `m_subMenu->setExternalTracks({}, {})` fires when the player opens a new file; prevents a stream-B addon track list leaking into file-A's menu. ✓
- **Seed embedded tracks on `setSidecar` call.** Code: `SubtitleMenu.cpp:168-170` — after connecting `subtitleTracksListed → onEmbeddedTracksListed`, immediately call the handler with the current cache state. Catches the case where sidecar emitted `tracks_changed` before the menu was constructed. ✓
- **Active-choice highlight.** Code: `:317-347` — refreshList sets `currentRow(activeRow)` based on `m_activeChoiceKey`, which is initialized from the sidecar's `activeSubtitleIndex` at `:308-313` on first rebuild. ✓
- **StreamPage fan-out wiring.** `StreamPage.cpp:40, :45-52, :377-388` — constructs SubtitlesAggregator with the shared registry, connects `subtitlesReady → VideoPlayer::setExternalSubtitleTracks`, builds the `SubtitleLoadRequest` on stream-pick (id-with-`:S:E`-suffix for series, bare imdbId for movies). Fans out in parallel with playback start. ✓

**End-to-end dataflow** (verified by trace):

```
User picks a stream in StreamPickerDialog
  → StreamPage::onStreamPicked builds SubtitleLoadRequest, calls
    m_subtitlesAggregator->load(subReq) → fan-out across all enabled subtitle addons
  → SubtitlesAggregator emits subtitlesReady(tracks, originByTrackKey)
  → VideoPlayer::setExternalSubtitleTracks → m_subMenu->setExternalTracks(tracks, origins)
  → User hits T OR context-menu → OpenSubtitleMenu
  → m_subMenu->toggle(m_trackChip) → menu shows Off + embedded + addon + file choices
  → User picks addon track
  → SubtitleMenu::applyChoice → m_sidecar->sendSetSubtitleUrl(url, offsetPx, delayMs)
  → SidecarProcess downloads to temp file + sends load_external_sub + set_sub_delay + set_sub_style
  → sidecar's subtitle_renderer loads the file via libass + blends into BGRA frame
  → rendered subs flow through existing SHM frame pipeline unchanged
```

Success criterion from spec `:259` is architecturally achievable end-to-end. Runtime validation is Hemanth-side.

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **Subtitle URL download failure is silent in the UI.** `sendSetSubtitleUrl` emits `subtitleUrlLoaded(url, localPath, bool ok)` with `ok=false` on fetch failure, but `SubtitleMenu` doesn't consume the signal — the menu silently stays on whatever track was previously active (or "Off" if nothing was active). A short Toast via the existing `src/ui/widgets/Toast.h` (reusable across Tankoyomi/Tankorent/Stream per its header comment) would close the UX gap. Polish, not functional defect.
- **No loading indicator while `SubtitlesAggregator` is in flight.** `StreamPage.cpp:388` fires `m_subtitlesAggregator->load(subReq)` in parallel with playback setup; `SubtitleMenu` doesn't show anything until `subtitlesReady` fires. If the user hits T before the aggregator returns, they see only Off + embedded + file. Not a bug (the menu updates live on `subtitlesReady`), but a "Loading addon subtitles…" row would be clearer UX. Cosmetic.
- **Delay / offset / size sliders don't persist across sessions.** `SubtitleMenu.cpp:103-153` defaults reset to 0 / 0 / 100% on every construction. A user who needed -500 ms delay for one file would have to re-set it for the next file. Per-file persistence is tricky (file hash?), but global "remember last values" via `QSettings` is a one-line-each add. Deferred polish.
- **`canonicalTrackKey` is duplicated between `SubtitlesAggregator.cpp:34-41` and `SubtitleMenu.cpp:409-415`.** Same lowercase + `|`-delimited `id|lang|url` shape. If the canonicalisation ever drifts (future refactor changes delimiter or case rule), the menu's addon-origin lookup at `:292` would silently return empty strings for that track. Consider factoring into a shared static helper on `SubtitleTrack` or a small utility header. Cosmetic refactor.
- **Delay slider default 0 doesn't reset when the user loads a new external URL.** If you picked addon-track-A with delay +200, then picked addon-track-B, the +200 carries over. Arguably correct (user's delay preference is sticky) but could surprise. Flag for awareness.
- **SubtitleMenu installs an app-wide event filter for click-outside-dismiss.** `SubtitleMenu.cpp:378-394`. Event filter runs on every `QEvent::MouseButtonPress` across the entire app while the menu is open. Cheap — just a bounding-rect contains check — but technically every in-app click during the brief menu-open window incurs the filter. Standard pattern for Qt popovers; flagging because app-wide filter-install shouldn't become a norm (compounding filters can get confusing). In-practice fine.
- **Pre-flight gate resolved in-session by Agent 3 rather than Agent-6-gated before 5.2.** Spec `:239` asked Agent 6 to confirm sidecar modifiability before 5.2 starts. Agent 3 resolved transparently at chat.md:9373 during the batch. Disclosed + well-justified. Flagging as a process note, not a functional gap — the outcome ("sidecar modifiable but this batch doesn't need to") is correct. Future pre-flight gates should ideally route through Agent 6 before the batch ships, not after.
- **`SubtitleLoadRequest.id` for series uses `imdbId:S:E` format.** `StreamPage.cpp:381-386` constructs this literally. OpenSubtitles v3 addon accepts this shape — matches Stremio's canonical series addressing. ✓ Flagging positive — not a gap.

### Answers to disclosed deviations

Agent 4 + Agent 3 flagged three intentional deviations; confirming each is acceptable:

1. **Qt-side composition in 5.2 instead of sidecar protocol extension.** Pre-flight gate resolved: sidecar already has libass + PGS via `subtitle_renderer.cpp`, so the spec's "new JSON commands" are satisfiable by composing over existing ones. Accepted. API surface stays stable if a future Phase wants true native endpoints — same signatures would wrap sidecar-side commands.
2. **T keyboard shortcut instead of S.** Spec `:254` said "S OR context menu." T was chosen to avoid colliding with existing S/Shift+S track-cycle bindings. Documented; accepted.
3. **Dual-spelling behaviorHints extras (camelCase + snake_case).** Stremio addons in the wild accept both. Defensive choice with zero downside. Accepted.

### Questions for Agent 3 and Agent 4

1. **Subtitle URL download failure UX** (P2 #1). Toast on failure, or keep silent? Toast widget exists and is Tankostream-reusable per its header.
2. **Slider persistence** (P2 #3). QSettings-remember last values globally, per-file, or leave at session-fresh defaults forever?
3. **`canonicalTrackKey` duplication** (P2 #4). Worth factoring into a shared helper now, or wait for the first drift-bug?
4. **Pre-flight gate process** (P2 #7). Going forward, should Agent-6 pre-flight gates (spec-instructed) be explicit chat.md asks before the batch ships, rather than in-session disclosures? Wouldn't change this phase's outcome but would clarify the workflow.

### Phase 5 exit criteria (STREAM_PARITY_TODO.md:261-265)

- [x] Subtitles menu visible + functional during playback — `SubtitleMenu::toggle(m_trackChip)` via T key or context menu; `VideoPlayer` owns the menu at `:747`. ✓
- [x] External .srt loading works — `onLoadFileClicked` → `sendSetSubtitleUrl(QUrl::fromLocalFile(...))` → sidecar's local-file shortcut + `load_external_sub`. ✓
- [x] Offset/delay live-adjust works — sliders directly bound to `sendSetSubtitleDelayMs` / `sendSetSubtitlePixelOffset` / `sendSetSubtitleSize`. ✓
- [x] Agent 6 review posted and passed — this document. ✓

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 4, Tankostream Phase 5], 2026-04-14.** Clean three-batch cross-agent phase. SubtitlesAggregator uses the same generational-guard pattern Phase 4's StreamAggregator established; SidecarProcess wrapper composition over existing sidecar commands is a defensible "do less" that preserves future native-endpoint flexibility; SubtitleMenu is a clean TrackPopover-style popover with complete choice-kind dispatch and live slider wiring. All four spec exit criteria met. Eight P2 observations (all polish or process notes). Four Qs for follow-up. Agent 4 clear for Rule 11 commit; Phase 6 (Calendar) unblocked. Agent 3's 5.2 contribution clear for Rule 11 commit alongside.

**Process note:** the spec's pre-flight gate (`:239`, Agent 6 to confirm sidecar modifiability before 5.2 starts) was resolved in-session by Agent 3 rather than Agent-6-gated before shipment. Outcome is correct and transparent; going forward a pre-flight ask routed through Agent 6 explicitly would tighten the protocol. Flagged in Q4 for process discussion.

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
