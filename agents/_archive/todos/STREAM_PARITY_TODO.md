# Tankostream → Stremio Basic Parity TODO

**Owner:** Agent 4 (Stream & Sources). Phase 5 Batch 5.2 is Agent 3's (sidecar protocol extension). Agent 6 reviews each phase against this doc as the objective source. Agent 0 coordinates.

**Created:** 2026-04-14 by Agent 0 after Hemanth briefed a Stremio-parity overhaul of Tankostream.

---

## Decisions (locked by Hemanth)

- **Full Stremio Addon Protocol.** Cinemeta and Torrentio become data (default installed addons), not hardcoded clients. 3rd-party addons installable by URL.
- **Scope:** 5 core phases + Calendar. Addon foundation, addon manager UI, catalog browsing, multi-source stream aggregation, subtitles menu, calendar.
- **Subtitles:** keep ffmpeg sidecar rendering. Add Qt-side menu + sidecar protocol extension. No HTML overlay renderer.
- **Reference spec:** `C:\Users\Suprabha\Downloads\Stremio Reference\stremio-core-development\stremio-core-development\` (Rust) + `...\stremio-video-master\stremio-video-master\` (JS).

## Non-Goals (explicitly out of scope)

- Notifications, detail-view polish (cast/trailers/related), audio track menu, playback speed, debrid integration, cloud sync, multi-device, pre-indexed local search, Chromecast, HTML-overlay subtitle renderer.
- Any work outside `src/core/stream/`, `src/ui/pages/stream*`, `src/ui/pages/StreamPage.*` — except the minimal sidecar extension in Phase 5 Batch 5.2 (Agent 3 owns).

## Objective (a user can, inside Stream mode)

1. See a home board with catalogs from installed addons (not just saved library).
2. Browse any catalog filtered by genre/year/sort.
3. Install a 3rd-party Stremio addon by URL, enable/disable, uninstall.
4. Open a title and see streams aggregated from every installed stream-capable addon.
5. Pick a source, play it, switch subtitle tracks from an in-player menu, offset/delay subs.
6. See a calendar of upcoming episodes for series in the library.

## Per-batch protocol

- Follow Build Rule 6: build clean + smoke-test feature before declaring done.
- Post `READY TO COMMIT — [Agent 4, Batch X.Y]: <msg> | files: ...` in chat.md on green.
- Agent 0 batches commits at phase boundaries (not per batch), unless a batch is risky enough to isolate.
- At end of each phase: post `READY FOR REVIEW — [Agent 4, Phase X]: Tankostream Phase X | Objective: <phase name> per STREAM_PARITY_TODO.md. Files: ...`. No phase advances past commit until Agent 6 posts `REVIEW PASSED`.

---

## Phase 1 — Addon Protocol Foundation

**Migration invariant:** the existing end-to-end flow (search → add → play) must keep working at the end of EVERY batch. `CinemetaClient` and `TorrentioClient` public signatures stay identical through Phase 1 — only their internals swap to use AddonTransport. They are deleted in Batch 4.4.

### [ ] Batch 1.1 — Addon data types
Create `src/core/stream/addon/` with 7 headers:
- [ ] `Manifest.h` — `AddonManifest` (id, version, name, types[], resources[], catalogs[], idPrefixes[], behaviorHints, optional description/logo/background). Mirror `stremio-core/src/types/addon/manifest.rs`.
- [ ] `Descriptor.h` — `AddonDescriptor` = manifest + transportUrl + flags (official, enabled, protected).
- [ ] `ResourcePath.h` — `ResourceRequest` (resource, type, id, extra: QList<QPair<QString,QString>>). Mirror `types/addon/resource_path.rs`.
- [ ] `StreamSource.h` — tagged union: Url, Magnet, YouTube, Http. Mirror `types/resource/stream.rs` (other variants deferred).
- [ ] `MetaItem.h` — `MetaItemPreview`, `MetaItem` (with `videos[]`), `Video`, `SeriesInfo`, `PosterShape`.
- [ ] `StreamInfo.h` — `Stream` = source + name + description + thumbnail + behaviorHints + subtitles[].
- [ ] `SubtitleInfo.h` — `SubtitleTrack` (id, lang, url, label).
- [ ] Wire all 7 into `CMakeLists.txt`.

**Success:** compiles clean, zero runtime effect. Agent 6 confirms type shapes match reference.

### [ ] Batch 1.2 — AddonTransport (HTTP client)
- [ ] New: `src/core/stream/addon/AddonTransport.h/.cpp`.
- [ ] `class AddonTransport : public QObject` with `fetchManifest(QUrl base)` → `manifestReady(AddonDescriptor)` / `manifestFailed(QString)`.
- [ ] `fetchResource(QUrl base, ResourceRequest req)` → `resourceReady(ResourceRequest, QJsonObject)` / `resourceFailed(ResourceRequest, QString)`.
- [ ] URL encoding per `stremio-core/src/addon_transport/http_transport/http_transport.rs` + `types/addon/resource_path.rs`. Pattern: `{base}/{resource}/{type}/{id}[/{extra}].json`. Extras = comma-joined `key=value`, percent-encoded.
- [ ] Use existing `QNetworkAccessManager`. 10s timeout per request.

**Success:** fetches `https://v3-cinemeta.strem.io/manifest.json` and returns a matching manifest.

### [ ] Batch 1.3 — AddonRegistry (persistence + default set)
- [ ] New: `src/core/stream/addon/AddonRegistry.h/.cpp`.
- [ ] Persists to `{AppData}/Tankoban/stream_addons.json`. Schema in Cross-Cutting section below.
- [ ] API: `installByUrl(QUrl)`, `uninstall(id)`, `setEnabled(id, bool)`, `list()`, `findByResourceType(QString resource, QString type)`.
- [ ] First-run seed: Cinemeta (`https://v3-cinemeta.strem.io`) + Torrentio (`https://torrentio.strem.fun`) as enabled + `official=true` + `protected=true`.
- [ ] Emit `addonsChanged()` on every mutation.
- [ ] `installByUrl` calls `AddonTransport::fetchManifest`, validates, persists.

**Success:** new run creates `stream_addons.json` with the two defaults. Install-by-URL with e.g. OpenSubtitles manifest adds a third.

### [ ] Batch 1.4 — Rewire CinemetaClient on AddonTransport
- [ ] Modify `src/core/stream/CinemetaClient.h/.cpp`. **Public signatures unchanged.**
- [ ] `searchCatalog` builds `ResourceRequest{catalog, movie/series, top, extra=[search=query]}`, looks up Cinemeta in `AddonRegistry`, calls `AddonTransport::fetchResource`. Parses `metas[]` into existing `CinemetaEntry`.
- [ ] `fetchSeriesMeta` builds `ResourceRequest{meta, series, imdbId}`. Parses `meta.videos[]` into existing `StreamEpisode` map.
- [ ] Delete hardcoded `QUrl("https://v3-cinemeta.strem.io")` from CinemetaClient.
- [ ] `CinemetaEntry` stays (retired in Batch 4.4).

**Success:** existing search + detail flow works identically; smoke-test end-to-end.

### [ ] Batch 1.5 — Rewire TorrentioClient on AddonTransport
- [ ] Modify `src/core/stream/TorrentioClient.h/.cpp`. Same pattern as 1.4.
- [ ] Emoji/quality/tracker parsing stays — Torrentio-response-specific.
- [ ] Delete hardcoded `QUrl("https://torrentio.strem.fun")`.

**Success:** existing torrent picker flow works identically; smoke-test end-to-end.

### Phase 1 exit criteria
- [ ] `stream_addons.json` exists, holds Cinemeta + Torrentio.
- [ ] Both clients route through AddonTransport + AddonRegistry.
- [ ] End-to-end flow works as before; no user-visible regression.
- [ ] Transport ready for 3rd-party addons.
- [ ] Agent 6 review posted and passed.

---

## Phase 2 — Addon Manager UI

### [ ] Batch 2.1 — Addon list screen shell
- [ ] New: `src/ui/pages/stream/AddonManagerScreen.h/.cpp`.
- [ ] Fourth layer in `StreamPage` QStackedWidget (index 3: browse / detail / player / addons).
- [ ] QListView of installed addons: 32x32 logo (async `manifest.logo`), name, version, enabled toggle, "official" badge, description.
- [ ] Entry: gear icon (28x28) in browse header of StreamPage.
- [ ] Back → browse.

**Success:** gear button → screen lists Cinemeta + Torrentio; back returns correctly.

### [ ] Batch 2.2 — Install-by-URL dialog
- [ ] New: `src/ui/dialogs/AddAddonDialog.h/.cpp`.
- [ ] Modal 500x300. QLineEdit for manifest URL, Install button, status label.
- [ ] Install → `AddonRegistry::installByUrl`. Success → close + refresh list. Failure → inline error.
- [ ] Trigger: "+ Add addon" button in AddonManagerScreen header.

**Success:** real 3rd-party URL (e.g. `https://opensubtitles-v3.strem.io/manifest.json`) installs and appears in list.

### [ ] Batch 2.3 — Addon detail panel
- [ ] Right-side detail pane inside `AddonManagerScreen`, visible on row select.
- [ ] Logo, name, version, transport URL, description.
- [ ] Capabilities as read-only chips: types[], resources[], catalogs[].
- [ ] Actions: Enable/Disable toggle, Uninstall (DANGER styled — use `ContextMenuHelper::addDangerAction` pattern or equivalent styling).
- [ ] `protected=true` addons (Cinemeta, Torrentio) hide Uninstall. Enable/Disable still works.
- [ ] Disabling excludes from all aggregator queries (Phases 3 + 4).

**Success:** select Torrentio → capabilities chips shown; uninstall 3rd-party removes from list + JSON; disable survives restart.

### Phase 2 exit criteria
- [ ] Install / enable / disable / uninstall all work via UI.
- [ ] `stream_addons.json` is never hand-edited in normal use.
- [ ] Agent 6 review posted and passed.

---

## Phase 3 — Catalog Browsing

### [ ] Batch 3.1 — CatalogAggregator
- [ ] New: `src/core/stream/CatalogAggregator.h/.cpp`.
- [ ] Given a content type + optional filters (genre, year, sort), plans requests to every enabled addon whose manifest advertises a matching `catalog` resource entry.
- [ ] Respects `ExtraProp.options_limit` per catalog entry.
- [ ] Parallel requests via `AddonTransport`, merge responses (concat metas, dedupe by id).
- [ ] Pagination: per-addon cursor, respect `hasMore`.
- [ ] Emit `catalogPage(QList<MetaItemPreview>, bool hasMore)`, `catalogError(QString addonId, QString msg)`.

**Success:** `load({type=movie, catalog=top, extra=[]})` returns Cinemeta top movies. 2nd catalog addon (if installed) merges in.

### [ ] Batch 3.2 — Home board with featured catalogs
- [ ] New: `src/ui/pages/stream/StreamHomeBoard.h/.cpp`.
- [ ] Replaces current bare browse layer: continue-watching strip (existing `StreamContinueStrip`) + N horizontal catalog rows.
- [ ] Enumerate all enabled addons' `manifest.catalogs[]`, show first 4-6 by default. User selection persisted to QSettings key `stream_home_catalogs`.
- [ ] Each row = one TileStrip-style horizontal scroller. Tile = `MetaItemPreview`. Double-click → `StreamDetailView`.
- [ ] Poster cache reuses `{AppData}/Tankoban/data/stream_posters/`.
- [ ] Announce in chat.md BEFORE editing browse layer (Agent 5 may want to weigh in on library placement — library-side UX is his domain per governance scope note).

**Success:** app opens Stream mode → continue-watching + 4-6 catalog rows populated. Installing 2nd catalog addon (e.g. Kitsu) adds its rows.

### [ ] Batch 3.3 — Catalog browse screen with filters
- [ ] New: `src/ui/pages/stream/CatalogBrowseScreen.h/.cpp`.
- [ ] Fifth layer in `StreamPage` stack.
- [ ] Entry: click catalog row header "Top Movies →" in StreamHomeBoard, OR Browse nav entry.
- [ ] Layout: catalog picker (addon + catalog combo), filter chips (genre, year, sort — populated from `ExtraProp.options`), tile grid, pagination (Load More button or infinite scroll).
- [ ] Filter change → `CatalogAggregator::load` with new request. Paginate → `loadNextPage`.
- [ ] Back → `StreamHomeBoard`.

**Success:** "Top Movies → genre=Action → year=2024 → sort=imdb_rating" shows correct results; pagination fetches next page.

### Phase 3 exit criteria
- [ ] Home board replaces empty browse layer.
- [ ] Browse without searching works.
- [ ] Filters respect `options_limit`.
- [ ] Agent 6 review posted and passed.

---

## Phase 4 — Multi-Source Stream Aggregation

### [ ] Batch 4.1 — StreamAggregator
- [ ] New: `src/core/stream/StreamAggregator.h/.cpp`.
- [ ] Given `ResourceRequest{stream, type, id}` (id includes `:season:episode` for series), parallel fetch from every enabled stream-capable addon.
- [ ] Parse each response into `QList<Stream>` using types from Batch 1.1.
- [ ] Port Torrentio emoji/quality parsing from `TorrentioClient` into `StreamAggregator` as post-process when `source.kind == Magnet && behaviorHints.bingeGroup != ""` (Torrentio signature).
- [ ] Emit `streamsReady(QList<Stream>, QHash<QString,QString> addonsById)` with per-stream origin addon metadata.

**Success:** `load({stream, movie, tt0111161})` returns Torrentio's current streams with quality parsed. 2nd stream addon (if installed) merges in.

### [ ] Batch 4.2 — StreamPickerDialog replaces TorrentPickerDialog
- [ ] Rename `src/ui/pages/stream/TorrentPickerDialog.h/.cpp` → `StreamPickerDialog.h/.cpp`.
- [ ] 6 columns: Source (addon logo + name), Stream title, Quality, Size (when known), Seeders (Magnet only), Origin (addon id).
- [ ] Sort primary: magnet seeders desc when present, else quality ladder (2160p > 1080p > 720p > ...).
- [ ] Handle non-magnet sources: Url/Http row shows "direct" in source column, no seeders.
- [ ] Extend `StreamChoices::saveChoice` to remember source kind + addon id (not just magnet URI).
- [ ] Update call sites in `StreamPage.cpp`.

**Success:** picker opens with rows from all stream addons. Pick a direct URL → plays. Pick a magnet → still buffers.

### [ ] Batch 4.3 — Engine handles non-Magnet sources
- [ ] Modify `src/core/stream/StreamEngine.h/.cpp`.
- [ ] `startStream` accepts a `Stream` (not bare magnet).
- [ ] `source.kind == Magnet` → existing path (TorrentEngine + HTTP server + buffer gate).
- [ ] `source.kind == Url / Http` → skip torrent, return remote URL directly to player. VideoPlayer + sidecar already handle HTTP GET.
- [ ] `source.kind == YouTube` → defer; show "YouTube playback not yet supported" error.
- [ ] Extend `StreamFileResult` with `playbackMode` enum (LocalHttp / DirectUrl).
- [ ] Update `StreamPlayerController` to respect `playbackMode` (skip buffer polling for DirectUrl).

**Success:** direct-URL stream plays without buffer delay; magnet stream still buffers; both reach the player correctly.

### [ ] Batch 4.4 — Retire CinemetaClient and TorrentioClient
- [ ] `StreamPage::onPlayRequested` calls `StreamAggregator`, not `TorrentioClient`.
- [ ] `StreamDetailView` + `StreamSearchWidget` call AddonTransport-wrapped equivalents directly (or new lightweight `MetaAggregator` if cleaner). Call sites use `MetaItemPreview` and `Stream` directly.
- [ ] Delete `src/core/stream/CinemetaClient.h/.cpp`.
- [ ] Delete `src/core/stream/TorrentioClient.h/.cpp`.
- [ ] Retire `CinemetaEntry` and `TorrentioStream` structs.
- [ ] Remove from `CMakeLists.txt`.

**Success:** `grep -i "cinemeta\|torrentio"` in `src/` turns up only default-seeded URLs in `AddonRegistry.cpp`. End-to-end flow still works.

### Phase 4 exit criteria
- [ ] Single picker shows streams from any installed stream addon.
- [ ] Direct URLs bypass torrent buffering.
- [ ] Cinemeta/Torrentio are data, not code.
- [ ] Agent 6 review posted and passed.

---

## Phase 5 — Subtitles (ffmpeg + Qt-side menu)

**Cross-agent:** Batch 5.2 is Agent 3's domain.

### [ ] Batch 5.1 — Subtitles resource fetching
- [ ] Extend `StreamAggregator` OR add `src/core/stream/SubtitlesAggregator.h/.cpp` as parallel class.
- [ ] Query every enabled addon with `subtitles` resource for the currently-playing meta id.
- [ ] Parse responses into `QList<SubtitleTrack>`.
- [ ] Thread through `videoHash`, `videoSize`, `filename` from Stream `behaviorHints` for OpenSubtitles matching.

**Success:** with OpenSubtitles addon installed, playing a known movie returns subtitle tracks.

### [ ] Batch 5.2 — Sidecar protocol extension (AGENT 3)
**Pre-flight (Agent 6):** before 5.2 starts, confirm the sidecar source is modifiable in-repo vs prebuilt binary. If prebuilt, scope shrinks to "what the existing sidecar already exposes" — Hemanth's call.

- [ ] Agent 3: modify `src/ui/player/SidecarProcess.h/.cpp` + sidecar exe command handler.
- [ ] New commands (JSON protocol):
  - [ ] `listSubtitleTracks` → `{tracks: [{index, lang, title}]}` for embedded tracks.
  - [ ] `setSubtitleTrack {index}` → select embedded track (or -1 for off).
  - [ ] `setSubtitleUrl {url, offset_ms, delay_ms}` → load external .srt/.vtt (download to temp, apply via ffmpeg filter).
  - [ ] `setSubtitleOffset {pixel_offset_y}`.
  - [ ] `setSubtitleDelay {ms}`.
  - [ ] `setSubtitleSize {scale}`.

**Success:** Qt-side can list/select embedded tracks and load external .srt mid-playback.

### [ ] Batch 5.3 — Qt-side subtitle menu
- [ ] New: `src/ui/player/SubtitleMenu.h/.cpp` (coordinate with Agent 3 on placement) OR Tankostream-side wrapper in `src/ui/pages/stream/`.
- [ ] Trigger: `S` keyboard shortcut OR player context menu item.
- [ ] Shows: embedded tracks (from 5.2 `listSubtitleTracks`), external tracks (from 5.1 aggregator), "Off", "Load from file..." (QFileDialog).
- [ ] Selecting a track dispatches the right sidecar command.
- [ ] Bottom of popover: offset/delay/size sliders, live-update via sidecar.

**Success:** during playback with embedded English + fetched OpenSubtitles Spanish, user switches between them, loads a custom .srt, live-adjusts delay by ±500ms with visible sync change.

### Phase 5 exit criteria
- [ ] Subtitles menu visible + functional during playback.
- [ ] External .srt loading works.
- [ ] Offset/delay live-adjust works.
- [ ] Agent 6 review posted and passed.

---

## Phase 6 — Calendar

### [ ] Batch 6.1 — Calendar backend
- [ ] New: `src/core/stream/CalendarEngine.h/.cpp`.
- [ ] Per library series: query `meta` resource from any meta-capable enabled addon. Use `MetaItem.videos[]` (includes release dates).
- [ ] Filter `videos[]` where `now < released < now + 60 days`. Group by day/week.
- [ ] Cache to `{AppData}/stream_calendar_cache.json` with 12h TTL.
- [ ] Emit `calendarReady(QList<CalendarItem>)` where `CalendarItem = { MetaItemPreview, Video }`.

**Success:** library with a currently-airing series returns upcoming entries; stale cache expires per TTL.

### [ ] Batch 6.2 — Calendar screen
- [ ] New: `src/ui/pages/stream/CalendarScreen.h/.cpp`.
- [ ] Sixth layer in `StreamPage` stack.
- [ ] Entry: calendar-icon button in stream browse header.
- [ ] Grouped list by day (This Week / Next Week / Later): thumbnail, series name, SxxExx, episode title, date.
- [ ] Double-click → navigate to that series' `StreamDetailView` with episode preselected.

**Success:** calendar populated from library, grouped correctly, double-click lands on right detail page.

### Phase 6 exit criteria
- [ ] Calendar functional end-to-end.
- [ ] Agent 6 review against `stremio-core/src/models/calendar.rs` posted and passed.

---

## Cross-Cutting Concerns

### stream_addons.json schema
```json
{
  "version": 1,
  "addons": [
    {
      "transportUrl": "https://v3-cinemeta.strem.io/manifest.json",
      "manifest": { /* cached manifest */ },
      "flags": { "official": true, "enabled": true, "protected": true }
    }
  ]
}
```

### Behavior hints Tankostream honors
- `not_web_ready` — defer; assume false.
- `binge_group` — Torrentio signature; used by quality parser (Batch 4.1). Keep.
- `proxy_headers` — forward to sidecar via a `setHeaders` command. Defer or fold into 4.3 if trivial.
- `country_whitelist` — surface as non-blocking note in picker; don't enforce client-side.
- `filename`, `videoHash`, `videoSize` — thread through to Phase 5 subtitle matching.

### Agent roles per phase
| Phase | Agent | Cross-agent |
|-------|-------|-------------|
| 1 | Agent 4 | None |
| 2 | Agent 4 | Uses `ContextMenuHelper` pattern (Agent 5 code; additive use only, no modification) |
| 3 | Agent 4 | Home board layout may touch Agent 5's turf — announce before 3.2 |
| 4 | Agent 4 | None |
| 5 | Agent 4 + Agent 3 | Batch 5.2 is Agent 3's. Agent 4 blocks on 5.2. |
| 6 | Agent 4 | None |

### Risks & mitigations
1. **Migration breakage** — Phase 1 keeps public APIs unchanged. No regression until 4.4, which ships after the picker upgrade.
2. **Sidecar source availability** — Agent 6 pre-flights before 5.2. If prebuilt, scope shrinks.
3. **Bad 3rd-party manifests** — Batch 1.3 validates against type defs and rejects with clear error.
4. **Aggregator fan-out latency** — 10s per-addon timeout, emit partial-results signals so UI can update incrementally, cache (url, request) tuples with short TTL.
5. **StreamPage conflict with Agent 5 library-side UX** — Batch 3.2 heads-up in chat.md before editing.
6. **Scope creep** — non-goals enumerated above. Agent 6 flags drift.

---

## Reference paths (read-only during implementation)

- `C:\Users\Suprabha\Downloads\Stremio Reference\stremio-core-development\stremio-core-development\src\addon_transport\` — transport + URL encoding
- `...\src\types\addon\` — manifest + descriptor
- `...\src\types\resource\` — meta, stream, subtitle
- `...\src\models\catalog_with_filters.rs`, `catalogs_with_extra.rs`, `meta_details.rs`, `installed_addons_with_filters.rs`, `addon_details.rs`, `calendar.rs`, `continue_watching_preview.rs`
- `C:\Users\Suprabha\Downloads\Stremio Reference\stremio-video-master\stremio-video-master\src\withHTMLSubtitles\` — subtitle parsing semantics (reference only; we keep ffmpeg)

---

## Verification per phase

- **Phase 1:** `stream_addons.json` contains Cinemeta + Torrentio. Existing search → add → play flow unchanged.
- **Phase 2:** Install 3rd-party URL → appears. Disable → excluded from queries. Uninstall → removed from JSON. Restart → state persisted.
- **Phase 3:** Stream mode opens to home board (continue + catalog rows). Click row header → browse screen. Filter + paginate work. Tile → detail.
- **Phase 4:** Torrentio magnet plays (buffers). URL stream plays (no buffer). Picker shows origin addon per row.
- **Phase 5:** Embedded track switch works. External .srt loads. ±500ms delay visibly syncs.
- **Phase 6:** Library with airing series → calendar lists upcoming episodes grouped by week. Double-click → correct detail page.

**Rule 6:** every batch ends with `build_and_run.bat` → exit 0 → smoke-tested in running app. Paste build exit code + one-line smoke result in chat.md before `READY TO COMMIT`.
