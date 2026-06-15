# Pivot Plan — QWebEngine + Tankoban Electron Frontend (over our native engine)

> Decided by Hemanth 2026-06-15 (see memory [[project_qwebengine_content_ui_pivot]]). Render the
> content UI with QWebEngine running **Tankoban Electron's** React app; keep the native ENGINE;
> bridge via QWebChannel. Native player + readers STAY (deferred swap). Harbor = design north-star.
> Revert anchor: tag **`pre-qwebengine-pivot` → `7954527`**. Study: workflow `wf_9476cefc-576`.
> Electron app: `C:\Users\Suprabha\Desktop\Tankoban Electron`.

## Architecture (the hybrid)
```
Native Qt MainWindow (frameless window chrome, drag, min/max/close)  ← stays native
  └─ QWebEngineView (central widget) ── loads qrc:///index.html (Electron renderer build)
        ▲  window.api shim (injected QWebEngineScript) → QWebChannel
        ▼
  TankobanWebBridge (QObject, Q_INVOKABLE slots + signals)  ← the bridge
        │  maps each window.api.* call to:
        ├─ HTTP proxies  (tmdb / cinemeta / anilist / anilistBrowse / mangadex / itunes)
        ├─ native engine (MetaAggregator, CatalogAggregator, TorrentClient, StreamLibrary, scrapers)
        └─ native player (intercept "play" → VideoPlayer)   ← holyGrail deferred
```
- The renderer assumes `window.api.*` (Electron preload). QWebEngine has no preload → inject a JS shim
  (`QWebEngineScript`, document-creation) that defines `window.api` as Promise-returning wrappers over a
  QWebChannel object (`window.qt.webChannelTransport`). One shim, generated from the contract.
- QWebEngine + QWebChannel are ALREADY dependencies (the ebook reader uses QWebEngine). No new dep.

## The bridge contract (from the study — 48 routes + 4 signals)
- **window** (4): setFullscreen / toggleFullscreen / isFullscreen / onFullscreenChange → `QMainWindow` directly.
- **metadata proxies** (5): `tmdb(path,params)` [needs C++ TMDB-token holder], `cinemeta(path)` [keyless], `anilist.art`, `anilistBrowse.section/genre`, `mangadex.volumes`, `itunes.cover` → C++ `QNetworkAccessManager` fetches (or map to our AniList/Cinemeta layers).
- **addons** (6): list/add/remove/getStreams/setHeaders/clearHeaders → `CatalogAggregator` + addon registry + `StreamAggregator.searchPacks`/streams. `setHeaders` is the one GAP (Electron webRequest; Qt has none — defer / move header injection into the stream layer or a `QWebEngineUrlSchemeHandler`).
- **manga** (6) + **manga:download** (7+signal): popular/latest/search/series/chapters/pages + downloads → map to our NATIVE scrapers (`WeebCentralScraper`, `MangaDex`, `ReadAllComics`) + `MangaDownloader`. (Electron uses cheerio in JS; we already have these natively → reuse, don't port.)
- **holyGrail** (~14, mpv embed): DEFERRED. Intercept the renderer's "play" → launch native `VideoPlayer`. Long-term, Hemanth's Electron mpv player replaces native (later stage).

## Phasing (each phase = build + load + Hemanth smoke)

### Phase 0 — Scaffold + "it loads" (proof of embedding)
- `cd "Tankoban Electron" && npm run build` → produces `out/renderer/` (self-contained, relative paths).
- Bundle `out/renderer/` into our app (qrc resource, or a `resources/webui/` dir).
- New `src/ui/web/WebShell.{h,cpp}`: a `QWebEngineView` + `QWebChannel` + `TankobanWebBridge` (stub) + the injected `window.api` shim (`QWebEngineScript`). Host it in MainWindow behind a feature flag (`TANKOBAN_WEB_UI=1`) so the native UI stays the default until the web UI is real.
- DoD: launching with the flag shows the Electron React app rendered inside our native window; nav/routing works; console shows the shim wired (even if data calls are stubbed).

### Phase 1 — Metadata/discovery proxies → the HOME renders with real data
- Implement the HTTP-proxy slots in `TankobanWebBridge`: `cinemeta`, `tmdb` (C++ token), `anilist.art`, `anilistBrowse.section/genre`, `mangadex.volumes`, `itunes.cover`.
- DoD: the Theatre + Manga **home/browse renders real catalogue rows + hero** (these are metadata-driven). This is the big visual win — the Harbor home, for real, off our bridge.

### Phase 2 — Actions: detail, search, play, library
- `addons.getStreams` → `StreamAggregator`/`CatalogAggregator`; search → existing search; **play** → intercept → native `VideoPlayer` (keep native player); library get/add → `StreamLibrary`; progress → `CoreBridge`.
- DoD: open a title → detail → play in the native player; search works; add-to-library works.

### Phase 3 — Manga content + downloads
- Map `manga.*` + `manga:download:*` to native `WeebCentral`/`MangaDex` scrapers + `MangaDownloader`; comics likewise. Open a chapter → native ComicReader (kept).
- DoD: Manga browse/detail/read + downloads via the web UI over native scrapers/readers.

### Phase 4 — Polish + decisions
- Header injection (`setHeaders`) via `QWebEngineUrlSchemeHandler` or stream-layer headers; fullscreen signal relay; Comics/Books (Electron had them "coming soon" — decide native-bridge vs build-in-Electron-first); automate the `npm run build` → qrc step in our build.

## Native engine mapping (bridge ↔ C++) — already exists
`MetaAggregator` (searchCatalog/fetchMetaItem/fetchSeriesMeta), `CatalogAggregator` (load/loadNextPage),
`StreamAggregator` (load/searchPacks → streams), `TorrentClient` (add/progress/control), `StreamLibrary`
(add/remove/has/getAll), `VideoPlayer` (play), `CoreBridge` (progress/prefs), manga/book scrapers +
`MangaDownloader`. Wrap as `Q_INVOKABLE` + signals (Qt::QueuedConnection for bg-thread signals); payloads
as `QJsonObject` (QWebChannel auto-serializes); async calls correlate via generation tokens.

## Gaps / decisions (flag to Hemanth before the phase that hits them)
1. **TMDB token** — Electron holds it in main; we need it C++-side (env/secret). Small.
2. **`setHeaders` (addon proxy headers)** — no Qt webRequest equivalent; defer to Phase 4 (scheme handler or stream-layer).
3. **Manga scrapers** — reuse OUR native scrapers (don't port Electron's cheerio); the renderer's manga:* maps to native.
4. **Build integration** — Phase 0 does `npm run build` manually + commits/bundles `out/renderer/`; Phase 4 automates.
5. **Native shell vs web nav** — the Electron app renders its OWN sidebar+search; the native NavRail (7954527) becomes the *window frame* only, or is hidden under the web UI. Decide in Phase 0.

## Safety
Feature-flagged (`TANKOBAN_WEB_UI`) so native stays default until the web UI is real. Revert anchor tagged
`pre-qwebengine-pivot`. Each phase build-verified in the agent5 lane + Hemanth-smoked.

## Self-review
- De-risked: embedding is a solved pattern (qrc + QWebEngineView); the bridge is mostly HTTP proxies +
  existing native methods; the player (hardest) is deferred. Highest-risk unknowns are the `window.api`
  shim wiring (Phase 0) and header injection (Phase 4) — both contained.
