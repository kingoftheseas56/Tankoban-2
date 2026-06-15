# Harbor Redesign — Theatre Flagship (Design Spec)

- **Date:** 2026-06-15
- **Status:** Approved in brainstorm; pending spec review → plan.
- **Owners:** Agent 0 (orchestration) · Agent 4 (Theatre/stream domain) · Agent 5 (UI/theme/widgets).
- **Reference:** `../Tankoban Electron` (React/Electron app) + Harbor (`tankoban-harbor-design.md` memory, `harbor-ref/harbor-core` source, installed `harbor.exe`).
- **Supersedes (UI direction):** the six-mode video split into separate Anime/TV/Movies top-level modes (Arc 2 UI endgame). See §2.

---

## 1. Vision

Transform the **native C++/Qt** Tankoban 2 UI/UX to match the design language of the Electron reference, which fuses **Stremio + Netflix + Harbor**. This is a **re-skin + motion layer + re-layout over the existing native engine — not a rewrite.** The engine stays C++/Qt; only presentation changes. The **only** web tech permitted is `QWebEngineView` for **readers** (the ebook reader already uses it).

**Core finding (from the 4-agent understand-phase study):** the gap to a Netflix/Stremio/Harbor bar is almost entirely **presentation and motion, not capability**. Tankoban 2 already has the structure (modes, a Stremio-blueprint detail view, continue strip, card grids, glass background, a 5-variant theme system, a per-mode back stack) and **heavier engines than the reference has natively** (frameless ffmpeg-sidecar player with HW decode / zero-copy / libass / WASAPI, libtorrent, the stream-server adapter, the full scraper fleet, a Foliate QWebEngine ebook reader, a Mihon-grade comic reader). Notably the reference's "Holy Grail" native-mpv path relies on the D3D11/GL-interop that is **broken on the target Intel UHD 620**; our player uses native GL, which works — **we are ahead on the hardest part.**

## 2. Locked decisions (brainstorm, 2026-06-15)

1. **Nav model: left rail + window-centered search.** A collapsible left rail (modes + the active mode's pages + collections) replaces the top mode-pills; the search lives dead-center in the top bar always.
2. **Sequencing: mode-by-mode, complete; Theatre/Video is the flagship.** The shared foundation (shell, tokens, widgets, motion) is built **as Theatre's substrate** and inherited by Manga/Comics/Books afterward — so the flagship produces the shared system as a byproduct.
3. **Ambition: Core Harbor look first.** Full premium *feel* (design system + cinematic hero + rows + cards + reskinned detail/player HUD); deep extras deferred (§9).
4. **Window chrome: frameless, custom title-bar.** The rail + center-search bar become the window's top edge; custom min/max/close; the bar is the OS drag region.
5. **One unified video mode ("Theatre").** The split of video into separate Anime/TV/Movies top-level modes is **cancelled.** The already-shipped `StreamMode` classification (anime/tv/movies — Arc 2 Tasks 1–5, commits `befc7bf`/`ab43e56`/`ba51d6a`/`b177c4a`) is **repurposed as pages/filters within the one Theatre mode** (Movies / Shows / Trending / Anime sections) — not wasted, just relocated from "split modes" to "organize content." **Final mode set: Theatre · Manga · Comics · Books** (matches the reference's rail exactly).
6. **Keep the native engine; reskin only.** No player-engine rewrite; we overlay a cinematic HUD on the existing ffmpeg pipeline.

## 3. Design language (tokens → `src/ui/Theme.{h,cpp}`)

Dark-first, single-accent, editorial. Defined in **OKLCH** for perceptual evenness, then **baked to sRGB hex** for Qt QSS (Qt has no native OKLCH).

- **Elevation ladder (hue 260, cool-neutral):** `--bg` oklch(.18) canvas → `--surface` .22 → `--elevated` .27 (cards/popovers/pills) → `--raised` .32 (hover rows). Ink: `--text` .97 / `--muted` .72 / `--subtle` .50. Borders: `--edge` (.36 α55) / `--edge-soft` (α25 hairlines).
- **One jewel accent:** brand gold `#e8b923` (`--accent`), on-accent text `#14110a`. Used **only** on active nav, focus ring, card-hover inset ring, progress/seek fill, CTAs, key chips. **Semantic red `#e50914` is firewalled to errors/flags only — never brand.**
- **Typography (bundle fonts):** display serif **Fraunces** (hero/section/detail titles, logo wordmark; weight 600, tracking −0.01/−0.02em, line-height ~0.98) + body sans **Inter** (Segoe UI fallback). Hero title ~48–60px; section H2 ~18–20px; body 14–16px; labels 11–13px uppercase tracked.
- **Radius ladder:** 6 / 10 / 14 / 20 / 28 px + 999px pills.
- **Motion:** two signature eases — `ease-out` cubic-bezier(.16,1,.3,1) (UI) and `ease-smooth` cubic-bezier(.32,.72,.24,1) (the "pull": card lift, carousels, rail width). Card lift ~0.3s; button scale ~0.2s; hero crossfade ~0.9s + ~7s Ken-Burns; rail width ~320ms. Honor a reduced-motion setting.
- **Signature micro-interactions** (the identity): (a) **card hover-lift** — translateY(−8px) + deep shadow + 2px gold inset ring + bottom scrim, on the "pull" ease; (b) the **art-to-UI gradient melt** — stacked directional gradients dissolving key art into the canvas (the #1 premium tell); (c) cinematic rotating hero; (d) frosted window-centered search; (e) hover-and-capable-only row arrows.
- **Consistency mandate:** migrate the stream pages (and Tankorent/sidebar) **off hardcoded inline QSS onto these tokens** so one re-skin propagates everywhere. Today the most-Stremio page is the least theme-compliant.

## 4. App shell (shared foundation — built during the flagship)

- **Frameless `QMainWindow` + custom title-bar `QWidget`.** Title bar is the drag region (`startSystemMove`); interactive children opt out. Custom min/max/close at top-right.
- **Left rail (`QWidget`, collapsible 62↔210px, `QPropertyAnimation` on `maximumWidth`, state persisted):** logo (serif gold, doubles as go-home) → **MODES** group (Theatre/Manga/Comics/Books) → gradient-hairline divider → the **active mode's PAGES** (Theatre: Home/Movies/Shows/Trending) → spacer → **collections** (Library/Downloads/Settings) → collapse toggle. Active state = gold icon when collapsed, subtle elevated pill + gold icon + label when expanded. Switching mode is a hard reset to that mode's home (reuse existing mode-pill semantics).
- **Top bar:** hierarchy-back circle on the left (visibility-reserved at a mode root, walks the route hierarchy — reuse `PerModeNavController`), the **frosted window-centered search pill** (`/` hotkey hint + recent-search dropdown), window controls on the right.
- **Reuse:** `PerModeNavController` already implements hierarchy-back + mode hard-reset + root-seed; `GlassBackground` provides the animated atmosphere layer.

## 5. Reusable widget vocabulary (shared, built here, used by every mode)

Each unit is independently testable via the dev bridge (`introspect-tree`/`devSnapshot`).

- **`FeaturedHero`** — rotating carousel (≤5 slides, ~7s auto-advance, pause on hover). Crossfading backdrops (`QStackedWidget`/`QGraphicsOpacityEffect`) + slow Ken-Burns scale (`QPropertyAnimation`) + painted scrim & art-to-canvas melt (`QPainter` `QLinearGradient`). Content block: uppercase meta, serif title, clamped synopsis, gold "Play" pill + ghost "More info", widening gold dot pager. *Interface:* `setSlides(list<HeroSlide>)`, `playRequested(id)`, `detailsRequested(id)`. *Deps:* Theme tokens, metadata source.
- **`Row`** — horizontal poster shelf: head (serif title + "See all"), hidden-scrollbar scroller, **hover-and-capable-only** edge gradient arrows (track canLeft/canRight), lazy cell mount. *Interface:* `setTitle`, `setItems`, `seeAllRequested`. Built on `QScrollArea` + overlay `QToolButton` arrows + scrollbar `QPropertyAnimation`.
- **`Card`** — portrait poster (~160×240, radius 14) with the signature hover-lift (`QGraphicsDropShadowEffect` + event-filter + painted inset ring + bottom scrim). Optional overlays: progress underline, remove-x, center play. *Interface:* `setPoster/setTitle/setProgress`, `clicked`, `playRequested`, `removeRequested`.
- **`ContinueCard`** — 16:9 landscape variant: backdrop + (logo or title) + `SxxExx` chip + "X min left" + gold progress bar + hover circular Play + dismiss-x. The home spine. (Download/play = the implicit library action; no explicit "follow.")
- **`PillButton` / states** — gold primary (glow + inset highlight + press-scale) and ghost; one unified gold spinner-ring loading widget; "never a blank screen" dashed empty-state widget; deterministic hashed-gradient poster fallback.

## 6. Theatre flagship screens

- **Home** (the approved mockup): `FeaturedHero` → **Continue Watching** row → **Top 10** (big serif rank numerals) → **Trending / genre** rails. Movies/Shows/Trending/Anime are rail **pages** within the one Theatre mode, populated via the existing catalog + the shipped `StreamMode` classification as the filter.
- **Detail** — **reskin the existing `StreamDetailView`** (already the Stremio blueprint other modes mirror): backdrop band + art-to-UI melt + serif title/logo plate + dot-joined meta (year, runtime, rating, genres) + gold Play / ghost actions + season selector + episode rows (number, 16:9 still, name+overview, per-row state/progress).
- **Player HUD** — **keep the native ffmpeg engine**; overlay a cinematic HUD: auto-hiding top + bottom gradient scrims; bottom = seek-bar row then a 3-zone control grid (left: play/pause + prev/next + volume + time; center: optional; right: subs/audio/speed/PiP/fullscreen); rich seek bar (buffered-ahead fill drawn separately, gold played-fill, hover-scrub time tooltip). Tight-proximity auto-hide. *Reuse:* `SeekSlider`, existing track/subtitle/audio controls; the HUD is a re-layout + re-skin of existing controls, not new playback code.
- **Search** — the persistent window-centered search resolves into a results grid (reuse `TileStrip`/grid); cross-mode-aware; recent-search dropdown.

## 7. Architecture & component mapping

| Unit | Purpose | Qt construct | Depends on |
|---|---|---|---|
| `HarborTheme` (extend `Theme.cpp`) | token source (palette, fonts, radius, eases) | QSS template + resolved tokens | — |
| `AppShell` (frameless window + title bar) | window chrome + drag | frameless `QMainWindow` + title `QWidget` | Theme |
| `NavRail` | mode/page navigation | `QWidget` + width `QPropertyAnimation` | Theme, `PerModeNavController` |
| `CenterSearchBar` | global search entry | `QLineEdit` in a frosted pill + dropdown | Theme, search service |
| `FeaturedHero`/`Row`/`Card`/`ContinueCard`/`PillButton` | content vocabulary | §5 | Theme, metadata |
| `TheatreHome` | flagship home page | composes the widgets | catalog + `StreamMode` |
| `StreamDetailView` (reskin) | detail | existing widget, tokenized | Theme |
| `PlayerHud` (reskin/overlay) | cinematic controls | overlay `QWidget` over the video surface | existing player + `SeekSlider` |

**Isolation principle:** the design-token layer, the shell, and each widget have one purpose and a narrow interface; they are built once during the flagship and reused unchanged by later modes. A widget should be understandable and dev-bridge-inspectable without reading its internals.

## 8. Reuse / build / defer

- **Reuse (do not rebuild):** ffmpeg `native_sidecar` + `VideoPlayer` engine; `libtorrent` `TorrentClient` + `StreamDownloadIndex` + stream-server adapter; the scraper/metadata fleet; `StreamDetailView` blueprint; `PerModeNavController`; `Theme` system; `GlassBackground`; `TileCard`/`TileStrip`; `SeekSlider`; the `tankoctl` dev bridge + `IDevInspectable`/`devSnapshot` for verification.
- **Build (new, shared):** `HarborTheme` token layer; frameless `AppShell` + `NavRail` + `CenterSearchBar`; `FeaturedHero`/`Row`/`Card`/`ContinueCard`/`PillButton`; the motion/easing helpers; `TheatreHome`; `PlayerHud` re-skin.
- **Defer (post-flagship / "full Harbor"):** Theme Studio, trickplay scrubber thumbnails, addon source-ranking badges, drag-reorder home customizer, skip-intro/outro segment markers, multiple seek-bar styles.

## 9. Data flow

No new data backends. Metadata/catalog comes from the existing stream-server + Cinemeta/Kitsu + addon registry; Continue Watching from the existing progress store; the `StreamMode` classifier sorts content into the Theatre pages. The redesign consumes existing accessors off `MainWindow`; it is a presentation layer over a built data layer.

## 10. Verification & testing

- The engine is unchanged → verification is **mostly visual + structural**, done by the agent via the **dev bridge** (`introspect-tree` for widget visibility/geometry, `devSnapshot`, `log-mark`) and confirmed by Hemanth's eyes on the running app. Agent 0 **builds and launches** the app for every smoke (direct-exe fast launch; Hemanth only looks + reports — per `feedback_agent_launches_app`).
- Pure-logic units (e.g., hero rotation timing, row scroll math, hashed-gradient fallback) get GoogleTest coverage where dep-free.
- Each phase ends green (build + the touched screen verified live) before the next.

## 11. Phasing (preview — detailed in the plan)

1. **Foundation:** `HarborTheme` tokens + fonts + frameless `AppShell` + `NavRail` + `CenterSearchBar`. (Theatre reachable in the new shell.)
2. **Widget vocabulary:** `FeaturedHero`, `Row`, `Card`, `ContinueCard`, motion helpers, loading/empty states.
3. **Theatre Home:** compose the widgets into the flagship home.
4. **Theatre Detail:** reskin `StreamDetailView`.
5. **Theatre Player HUD:** cinematic overlay on the native engine.
6. **Consolidate:** migrate stream pages off inline QSS onto tokens; tidy the one-video-mode (fold Movies/Shows/Trending/Anime as pages).

## 12. Risks & notes

- **OKLCH→sRGB:** bake the ladder to hex once; keep the OKLCH definitions as comments for future tuning.
- **Frameless gotchas:** drag region, snap/aero-snap, resize borders, multi-monitor DPI — budget time; reuse Qt's `startSystemMove`/`startSystemResize`.
- **No D3D11/GL-interop adoption** — we keep our working native-GL player; the reference's Holy-Grail path (broken on UHD 620) is **not** adopted.
- **Inline-QSS migration** is load-bearing for "one re-skin propagates" — treat as part of the foundation, not optional polish.
- **One-video-mode reframe** reverses the Arc 2 UI endgame; update the `project_three_modes` / video-split memories when the reframe lands.

## 13. Scope boundary

This spec + its plan cover the **Theatre flagship only**, plus the shared foundation it produces. Manga, Comics, and Books each get their own follow-on spec → plan, reusing the foundation (and adding the manga/comic reader — where the QWebEngine reader question is revisited). This keeps the work to one coherent implementation plan.
