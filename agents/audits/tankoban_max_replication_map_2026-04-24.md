# Tankoban-Max → Tankoban 2 visual replication map

**Audit author:** Agent 5 (Library UX)
**Date:** 2026-04-24
**Reference source:** `C:\Users\Suprabha\Downloads\Tankoban-Max-master\Tankoban-Max-master\` (Electron + HTML/CSS/JS)
**Target:** Tankoban 2 (Qt/C++, 61 widget files, 450 `setStyleSheet` call sites — no external `.qss` file in use)
**Scope of this wake:** inventory + portability categorization + phased implementation proposal. **NO `src/` changes shipped this wake** — implementation lands in follow-on wakes after Hemanth ratifies.
**Inputs read this wake:** Tankoban-Max HTML shell (`src/index.html` 2319 lines), `ui-tokens.css` (19), `ui-bridge.css` (157), `overhaul.css` targeted sections (1668 total), `styles.css` targeted sections around inside-series + toast + context-menu + loading-overlay (1815 total), `comic-reader.css` head + grep, `video-player.css` head, `video-library-match.css` head, `books-reader.css` head, targeted grep across the other four stylesheets. Tankoban 2: `TileCard.cpp`, `BooksPage.cpp` head, `MainWindow.cpp` head, `setStyleSheet` distribution survey across all 61 files.

---

## 0. Load-bearing tension flag — requires Hemanth's explicit call

Two existing durable preferences collide in this port:

- **`feedback_no_color_no_emoji.md`** — "Strictly gray/black/white UI, no colored text, SVG icons only."
- **`feedback_reader_rendering.md`** — "Tankoban-Max is source of truth for reader UX. Match behavior 1:1."

Tankoban-Max is **not** black-and-white. It ships two theme layers that both use color:

- **Baseline (`ui-tokens.css`)** — per-mode accent: comics `#ff5555` red, books `#4a90d9` blue, videos `#67d48b` green, default `#60a5fa` light-blue. Accent surfaces as: focus-ring outline (`color-mix accent 70%`), mode-pill fill (`accent 20%`), row-selected bg (`accent-rgb, .18`), `volRow:hover` border (`accent-rgb, .55`), loading-bar fill (`accent-rgb, .55`), danger text (`rgb(248,113,113)` red).
- **Noir overlay (`overhaul.css`)** — muted gold `--vx-accent: #c7a76b` + smoke grey `#9ca3af` + amber `#d97706` for hot/capture states. Loads after baseline so it wins.

There is no greyscale mode in Tankoban-Max today. Even in Noir the hover glow at `seriesCard:hover` and scoreboard-style amber-pulse at capture states carry color.

**Three ways forward — Hemanth picks, Agent 5 does not:**

1. **Accept per-mode accent.** Port both layers. The visible deltas from old feedback: four accent hues on focus rings, mode pills, progress bars, danger items. Supersede `feedback_no_color_no_emoji.md` or carve an exception scoped to "Tankoban-Max port surfaces only." Visual cost of refusing this: lose the per-mode "where am I" cue Hemanth coded three times across the Electron app.
2. **Greyscale-only port.** Retain all shapes, spacing, typography, animation timings, hover states — but force every `--accent*` token to a white/grey ramp. All four modes look identical except for the section title. Honors `feedback_no_color_no_emoji.md` literally. Visual cost: Tankoban-Max's per-mode identity is gone; users navigating between comics/books/videos see an undifferentiated UI.
3. **Hybrid — structural port + greyscale chrome.** Pull over tile shapes, scrollbars, topbar/sidebar layout, spacing tokens, hover-lift animation, context menu + toast + loading overlay shapes. Refuse accent color everywhere. Section titles and icons do the per-mode differentiation instead. This is the closest to a coherent reading of both memories.

**Agent 5 recommendation — no pick.** Option 3 is the minimum-risk interpretation of the two memories together, but it strips something Hemanth built intentionally three times. Option 1 is what "make Tankoban 2 look like Tankoban-Max" means in plain language. Option 2 is the literal letter of the older memory. This is a product choice, not a technical call — per Rule 14 it is squarely a Hemanth question. All three are ~same implementation effort at the token layer; the bucket-per-surface work in §2 below is bucket-identical across the three. The accent line item is a single-file token table.

**Until Hemanth picks:** the phased implementation below assumes **Option 1 tokens** as the base, with a single `Theme::accentColorForMode(Mode)` helper that is trivial to neutralize to greyscale if Option 2 or 3 wins. No code is shipped yet — this only shapes how I'd scope the token phase if green-lit today.

---

## 1. Token + palette map — PORTABLE

Tankoban-Max's design language lives in two token tables. Both are QSS-portable (QSS has no variables, but `QString::arg()` substitution from a single `Theme` class gets us the same result, and the palette has already been partially inlined into Tankoban 2 via `rgba(255,255,255,...)` literals).

### 1.1 OLED baseline (`ui-tokens.css` + `overhaul.css` :7-65)

| Token | Value | Current Tankoban 2 equivalent |
|-------|-------|-------------------------------|
| `--vx-bg0` / body bg | `#050505` | implicit Qt palette `window` (dark) — confirmed via Grep: no explicit widget bg override |
| `--vx-bg1` panel bg | `#0a0a0a` | `rgba(255,255,255,0.04)` on TileImageWrap, `rgba(255,255,255,0.07)` on LibrarySearch |
| `--ink-rgb` text | `245,245,245` = `.92 opacity` = `#f5f5f5 ~ 92%` | `#eee` in LibrarySearch — close |
| `--vx-muted` muted text | `rgba(245,245,245,.60)` | `rgba(255,255,255,0.55)` in continueLabel, `rgba(238,238,238,0.58)` in bookStatus |
| `--vx-glass` / glass | `rgba(0,0,0,.42)` | — none yet in Qt |
| `--vx-border` / `--vx-border2` | `rgba(255,255,255,.10 / .16)` | `rgba(255,255,255,0.10)` on TileImageWrap (match), `rgba(255,255,255,0.12)` on LibrarySearch (match) |
| `--vx-accent` Noir | `#c7a76b` muted gold (rgb 199,167,107) | none |
| `--vx-accent2` Noir | `#9ca3af` smoke | none |
| `--vx-hot` | `#d97706` amber | none |
| `--vx-radius` / `-sm` / `-lg` | 12 / 10 / 18 px | 8 on TileCard (CORNER_RADIUS), 6 on LibrarySearch, 12 on TileImageWrap, 10 on SortCombo |
| `--vx-blur` | 10px | **structurally blocked** (see §3.B) |

**Verdict:** Tankoban 2 is already at ~70% token parity by accident. The deltas are two missing tokens (`--vx-hot`, `--vx-accent2`) and a sizing inconsistency (8 vs 10 vs 12 vs 14 vs 18px radii used ad-hoc instead of a 3-step scale).

### 1.2 Library sizing (`overhaul.css` :46-55, baseline `styles.css`)

| Token | Baseline | Noir | Current Tankoban 2 |
|-------|----------|------|--------------------|
| `--lib-topbar-h` | 38 | 46 | no dedicated topbar widget surveyed — MainWindow doesn't `setStyleSheet` |
| `--lib-side-w` | 210 | 252 | sidebar-width-per-page ad-hoc (TankoLibraryPage has own, StreamPage has own) |
| `--lib-gap` | 10 | 12 | hard-coded per page (BooksPage uses 24 between sections, 10 between rows) |
| `--lib-pad` | 8-12 | 12 | `(20, 0, 20, 20)` content margins in BooksPage — parallel |
| `--lib-radius` | 10 | 14 | 6-18 ad-hoc |

**Verdict:** introducing a single `LibraryTokens` namespace (or anonymous-const block in a shared header) is ~20 LOC, then replacing magic numbers across ~30 call sites is ~60 LOC of mechanical edits. **PORTABLE.**

### 1.3 Typography

| Token | Tankoban-Max | Current Tankoban 2 |
|-------|--------------|--------------------|
| Font stack | `-apple-system, 'Segoe UI', ...` | `QFontMetrics` on default (Qt picks Segoe UI on Win11 automatically — de facto match) |
| Body | 11px 400 | QLabel default — depends on system DPI, roughly matches at 100% scaling |
| Meta | 12px 400 `muted` | `12px 12px` in various setStyleSheet calls |
| Topbar title | 14px 900 | no topbar title |
| Tile title | `--tile-title-size` ~12px 800 | QLabel default (elided in TileCard:54) |
| Tile meta | `--tile-meta-size` ~11px 400 muted | — |
| Panel title | ~18px 800 | — (no `.panelTitle` equivalent) |

**Effort:** `~30 LOC` to wire a `TankobanFont::{body, meta, title, tileTitle, tileMeta, panelTitle}` helper; **PORTABLE**.

---

## 2. Surface inventory — mapped to Tankoban 2 files, bucketed

### Bucket legend
- **P** — PORTABLE: QSS 1:1, no workaround needed.
- **W** — PORTABLE-WITH-WORKAROUND: QSS can't directly, but `QPropertyAnimation` / `QGraphicsEffect` / `QPainter` subclass closes the gap acceptably.
- **B** — STRUCTURALLY BLOCKED: QSS + Qt Widgets can't replicate. Flag for a future QML track (if the project ever adopts one per `feedback_qt_vs_electron_aesthetic.md`). **Do not propose porting.** Do not even wire a fallback — the whole point of the memory is to stop relitigating this.

### 2.1 Shell chrome

| # | Tankoban-Max surface | Source file:line | Qt target | Bucket | LOC est. |
|---|---------------------|------------------|-----------|--------|----------|
| 2.1.1 | Topbar strip (38-46px dark, border-bottom, drag-region) | `overhaul.css:160-170`, `index.html:50-96` | `MainWindow.cpp` (new topbar widget on top of existing `QMainWindow`) | P | 40 |
| 2.1.2 | Mode-switch pills (Comics/Books/Videos/Sources — accent 20% bg when active) | `ui-bridge.css:113-124`, `overhaul.css:180-207` | `MainWindow.cpp` custom `QButtonGroup` styled via QSS `border-radius:999px` | P (tension-flag-gated on accent fill) | 30 |
| 2.1.3 | Window controls strip (min/max/close SVG buttons) | `index.html:83-94`, `overhaul.css:209-220` | `MainWindow.cpp` — already ships some; restyle needed | P | 15 |
| 2.1.4 | Global search box (260px min, 12px radius, accent focus glow) | `ui-bridge.css:69-85`, `overhaul.css:241-250` | `MainWindow.cpp` or new `GlobalSearchWidget` | P (base) + W (focus-glow fade) | 40 |
| 2.1.5 | Sidebar (210/252px, translucent `rgba(18,18,18,.40)` on OLED) | `overhaul.css:251-280` | `MainWindow.cpp` left `QStackedWidget` wrapper | P | 35 |
| 2.1.6 | Sidebar nav sections + nav items | `overhaul.css:515-528`, `index.html:108-140` | Per-page sidebar — each page emits its own, candidate for shared `NavSection` widget | P | 50 |
| 2.1.7 | Sidebar pin button | `index.html:105-107` | Custom icon button in shell | P | 10 |
| 2.1.8 | Off-canvas drawer backdrop (mobile-style reveal) | `overhaul.css:281-298` | Not Tankoban 2 — skip (desktop-only app, no drawer motion) | — | 0 |
| 2.1.9 | Animated background `bgFx` + film-grain `bgFx::after` + `@keyframes vxBgDrift` | `overhaul.css:86-127` | — | **B** | — |

### 2.2 Library home views (comics / books / videos)

| # | Surface | Source | Qt target | Bucket | LOC |
|---|---------|--------|-----------|--------|-----|
| 2.2.1 | `libraryShell` 2-col grid (sidebar + content) | `overhaul.css` layout + `index.html:103-209` | Per page | P | 20 |
| 2.2.2 | `continuePanel` shelf | `styles.css` panel rules + `overhaul.css:546-551` | `ComicsPage` / `BooksPage` / `VideosPage` — `TileStrip` already ships the shelf; surface shape already parallel | P | 15 |
| 2.2.3 | Continue-tile 160x240 base card | `styles.css` tile block | `TileCard.cpp` — already at 65:100 aspect, ports cleanly | P | 10 |
| 2.2.4 | Continue-tile `::before` + `::after` stacked plates (rotated -4° / 3°) | `styles.css` inside-tile block | — | **B** (pseudo-elements + transform-rotate on widget siblings) | — |
| 2.2.5 | Continue-tile percent-bottom-right with `text-shadow` | `styles.css` | — | **B** (QLabel text-shadow would need `QGraphicsDropShadowEffect` on the label only, which flattens the label; acceptable for corner badges → **W**, 15 LOC) | W, 15 |
| 2.2.6 | `seriesGrid` auto-fill `minmax(132px, 1fr)` | `overhaul.css:622-625` | `TileStrip` or `QGridLayout` over `TileCard` with resize-events recomputing column count | P (Qt-idiomatic via `QGridLayout` + resizeEvent) | 25 |
| 2.2.7 | `seriesCard` hover `transform:scale(1.01)` 90ms | `overhaul.css:633-636`, `video-library-match.css:41-48` | `TileCard.cpp` with `QPropertyAnimation` on `geometry` or a `QTransform` in `paintEvent` | W | 20 |
| 2.2.8 | `seriesCard` border hover (1px light at 12.5% → 18.8% per video-library-match.css) | `video-library-match.css:34-36,61` | QSS `:hover` pseudo-state on `#TileCard` | P | 5 |
| 2.2.9 | `seriesCard` hover bg-tint `--lib-hover` | `video-library-match.css:45` | QSS `#TileCard:hover { background: ... }` | P | 5 |
| 2.2.10 | No-thumbnail film-strip SVG placeholder | `video-library-match.css:79-89` | `TileCard.cpp` noThumb branch — paint a cached `QPixmap` built from that SVG | P | 20 |
| 2.2.11 | Panel title row with `panelTitle` + breadcrumb + tools cluster | `styles.css` `.panelTitle` + `index.html:161-164` | Already present in pages (various `QLabel` + `QHBoxLayout`) | P | 0 (shape existing) |
| 2.2.12 | Scan-pill "Refreshing..." indicator | `styles.css:1665-1677`, `overhaul.css` | `LibraryScanPill` widget (new) + existing `Toast.cpp` could be extended | P | 30 |

### 2.3 Inside-series views (volumes / chapters / episodes)

| # | Surface | Source | Qt target | Bucket | LOC |
|---|---------|--------|-----------|--------|-----|
| 2.3.1 | `volSplit` vertical 2-pane (preview on top, table on bottom) with `flex: 2 1 0` / `1 1 0` | `styles.css:1108-1109` | `SeriesView.cpp` / `BookSeriesView.cpp` / `ShowView.cpp` — `QSplitter` with fixed ratios or `QVBoxLayout` stretch factors 2:1 | P | 15 per page |
| 2.3.2 | `volPreviewPane` (`rgba(0,0,0,.72)` bg, centered cover, top-left info badge) | `styles.css:1110-1133` | Above pages | P | 20 per page |
| 2.3.3 | `volPreviewImg` height-led sizing (640px max, 92% height cap) + box-shadow | `styles.css:1138-1143` | QSS on `QLabel` with `QPixmap` scaled + drop-shadow graphics effect | P (sizing) + W (shadow) | 15 per page |
| 2.3.4 | `volTableWrap` surface (bordered, rounded, subtle bg) | `styles.css:1148-1157` | QSS on container | P | 5 per page |
| 2.3.5 | `volTableHead` grid columns `42px minmax(160px, 2.1fr) ...` | `styles.css:1158-1170` | `QTableView` / `QAbstractItemView` with custom `QHeaderView::resizeMode` mix | P | 30 |
| 2.3.6 | `volRow:hover` 5% white + `.selected` row | `overhaul.css:661-671` | QSS `QTableView::item:hover` + `:selected` | P | 5 |
| 2.3.7 | Videos Episodes extra `Resolution` column | `video-player.css:44-45` | `ShowView.cpp` model columns | P | 10 |
| 2.3.8 | Progress-bar inline cell (track + fill + label) | `video-player.css:58-85` | Custom `QStyledItemDelegate` for progress column | W (delegate) | 40 |

### 2.4 Reader surfaces (already partially ported per `feedback_reader_rendering.md`)

| # | Surface | Source | Qt target | Bucket | LOC |
|---|---------|--------|-----------|--------|-----|
| 2.4.1 | Comic-reader `playerBar` top HUD + YouTube-style gradient fade | `comic-reader.css:10-48` | `ComicReader.cpp` (75 setStyleSheet sites — highest) — this work has been iterated on already | P (gradient via `QLinearGradient` paint) | 25 |
| 2.4.2 | Comic-reader `.ytIcon` 36px round | `comic-reader.css:52-73` | `ComicReader.cpp` | P | 10 |
| 2.4.3 | Comic-reader stage 16px rounded black frame | `comic-reader.css:111-117` | `ComicReader.cpp` | P | 5 |
| 2.4.4 | Book-reader toolbar + sidebar + reading area layout (books-reader.css, 3374 lines) | `books-reader.css:506-992` | `BookReader.cpp` (7 setStyleSheet sites) + `BookSeriesView.cpp` (14) — already drawn from this reference | Defer to Agent 2 | — |
| 2.4.5 | Book-reader mode-tab strip | `books-reader.css:23-56` | `BookReader.cpp` | P | 15 |
| 2.4.6 | Video-player HUD + folder-continue overlay | `video-player.css:10-45` | `VideoPlayer.cpp` (18) — owned by Agent 3, Congress 8 / comparative-player audit track | Defer to Agent 3 | — |

**Cross-reference:** Sections 2.4.4 / 2.4.6 are **out of Agent 5's library-UX scope** — these belong to Agent 2 (Book Reader) and Agent 3 (Video Player) respectively per the Hemanth 2026-04-14 ownership boundary. I flag them here because they are Tankoban-Max surfaces, but implementation is not mine. Recommendation: Agent 2 / Agent 3 copy the bucket categorizations from §2.4 into their own followup TODOs if they adopt this map.

### 2.5 Transient surfaces (toast, context menu, overlays)

| # | Surface | Source | Qt target | Bucket | LOC |
|---|---------|--------|-----------|--------|-----|
| 2.5.1 | `.toast` pill 999px radius, fixed bottom-center, 140ms transition | `styles.css:1506-1520` | `Toast.cpp` (1 setStyleSheet site) — already exists, restyle + add `QPropertyAnimation` on `pos` | P + W | 30 |
| 2.5.2 | `.toast` backdrop-filter blur(8px) | `styles.css:1518` | — | **B** (keep solid `rgba(bg,.90)` as substitute — loses depth but no user-reported complaint) | — |
| 2.5.3 | `.contextMenu` 190-280px, 12px radius, 8% white hover | `styles.css:1525-1567` | `ContextMenuHelper.cpp` (2) — QMenu + QSS on `QMenu::item` | P | 20 |
| 2.5.4 | `.contextMenu` backdrop-filter blur(10px) | `styles.css:1534` | — | **B** (same — solid fallback) | — |
| 2.5.5 | `.contextMenu .danger` red text | `styles.css:1559-1561` | QMenu entry with custom QAction and QSS class | P (tension-flag-gated: danger text is red regardless of mode; this is universally accepted even in greyscale-strict readings) | 5 |
| 2.5.6 | `.loadingOverlay` full-screen 45% black + card center | `styles.css:1611-1632` | `LoadingOverlay.cpp` (already exists under `player/`) — extend or pattern into a library version | P | 25 |
| 2.5.7 | `.spinner` rotating ring + `@keyframes spin` | `styles.css:1633-1642` | `QMovie` with a GIF, or a `QPropertyAnimation` on a custom painter | W | 30 |
| 2.5.8 | `.loadingBar` progress bar | `styles.css:1645-1657` | QSS on `QProgressBar` | P | 5 |
| 2.5.9 | `.keysOverlay` (K-key tips overlay) | `styles.css:1335+`, `index.html:211-250` | Not in Tankoban 2 today — candidate new widget `KeysOverlay.cpp` | P | 60 (build) |

### 2.6 Input + form elements

| # | Surface | Source | Qt target | Bucket | LOC |
|---|---------|--------|-----------|--------|-----|
| 2.6.1 | `.btn` (6px 8px padding, 11-12px 800 weight, accent-shadow hover) | `overhaul.css:221-229` | Shared QSS applied globally via `QApplication::setStyleSheet` | P + W (shadow fade) | 20 |
| 2.6.2 | `.btn-ghost` transparent variant | `overhaul.css:231-236` | Same | P | 5 |
| 2.6.3 | `.iconBtn` 28x28 rounded 6% white | `overhaul.css:209-219` | Same | P | 10 |
| 2.6.4 | `.search` input base | `ui-bridge.css:69-80`, `overhaul.css:241-250` | QSS on `QLineEdit` | P | 10 |
| 2.6.5 | `.search` focus glow `3px rgba(--accent-rgb,.14) box-shadow` | `overhaul.css:241-250` | `QGraphicsDropShadowEffect` toggled on focus | W | 15 |
| 2.6.6 | `.select` / `sl-select` combobox | `ui-bridge.css:69-80` | QSS on `QComboBox` + `QComboBox::drop-down` | P | 20 |
| 2.6.7 | Pill badges (999px radius, accent tint bg + border) | `overhaul.css` scanPill etc. | QSS on `QLabel`-as-badge | P | 10 |
| 2.6.8 | Pill badges backdrop-filter blur on cover overlays | same | — | **B** (solid fallback, same as 2.5.2/2.5.4) | — |

### 2.7 Scrollbars

| # | Surface | Source | Qt target | Bucket | LOC |
|---|---------|--------|-----------|--------|-----|
| 2.7.1 | 8px thin-bubble vertical scrollbar, thumb 18% → 32% hover → 44% active, 999px radius, 2px transparent-content-box border | `overhaul.css` (confirmed in Agent 8 first-pass, not re-read this wake) | Global QSS via `QApplication::setStyleSheet` targeting `QScrollBar`/`QScrollBar::handle`/`QScrollBar::add-line`/`QScrollBar::sub-line` | P | 40 |
| 2.7.2 | Horizontal variant | same | Same QSS block | P | 10 |

### 2.8 Animated / decorative — **ALL Bucket B**

Flagged-not-proposed. Listed here only so the inventory is complete and future-QML-track has a starting point:

- `.bgFx` + `.bgFx::after` animated radial-gradient + film-grain — multi-layer non-widget element with `@keyframes` + `mix-blend-mode: overlay`. QSS cannot express `mix-blend-mode` on a widget composite.
- `backdrop-filter: blur(8-40px) saturate(140-160%)` — repeated ~15 times across surfaces (toast, context menu, sidebar panels, loading card, badges). Qt Widgets has no per-widget-region backdrop blur. `QGraphicsBlurEffect` blurs the *widget itself*, not *what's behind it*. Alternatives (captured-pixmap-blur tricks) are expensive and not maintainable at 60fps.
- `::before` / `::after` pseudo-elements used for: film grain, stacked continue-tile plates, loading spinner styling decorations, theme-swatch ring selection, `.stageWrap::before` soft inner-glow. QSS `::before/::after` exists for narrow cases (`QComboBox::drop-down`) but not as general DOM extension.
- `mix-blend-mode: overlay` on grain.
- `transform: translate3d` + `hue-rotate` filter keyframe — Qt Widgets has no filter pipeline.
- `text-shadow` on body text — `QGraphicsDropShadowEffect` applied to a `QLabel` works for isolated labels but flattens vector AA; not acceptable for body paragraphs. OK for corner badges (one-word labels) — that's the "W" carve-out in §2.2.5.
- `transition:` on `all` or multi-property — Qt animates one property at a time via `QPropertyAnimation`; acceptable proxy but code-heavy enough it's only worth doing on hot surfaces (tile hover, focus glow).

---

## 3. Phased implementation proposal

**Gate:** Hemanth picks option 1/2/3 on the color-memory tension (§0). Phases 1-3 are token-shape and structural — the answer to the tension changes one file (the accent-color producer) but not the phase ordering. Phase 4+ touches accent-facing surfaces and needs the pick resolved.

**Sequencing principle:** foundation → chrome → grid/tiles → inside-series → transient/polish → decorative (skipped). Mirror order Tankoban-Max itself built in (styles.css → overhaul.css → reader-specific → theme-light → overrides).

### Phase 1 — Foundation (tokens + typography + globals)

- 1.1 Add `src/ui/Theme.h` with `namespace Theme { QColor bg, panel, panelHover, text, muted, border, accentForMode(Mode); int libRadius, libRadiusSm, libGap, libPad, libTopbarH, libSideW; }`.
- 1.2 Add `src/ui/TankobanFont.h` with `body()/meta()/title()/tileTitle()/tileMeta()/panelTitle()` helpers.
- 1.3 Add global QSS via `QApplication::setStyleSheet` for scrollbars (§2.7) and button base classes (.btn/.btn-ghost/.iconBtn — §2.6.1-2.6.3).
- **Deliverable:** ~150 LOC new, zero widget changes. Sanity-smoke: existing pages still render.
- **Gates subsequent phases:** yes. Without Theme tokens Phases 2-5 are ad-hoc.

### Phase 2 — Shell chrome

- 2.1 Topbar widget at top of MainWindow (§2.1.1–2.1.4).
- 2.2 Mode-switch pills (§2.1.2) — **accent-facing, tension-flag-gated**.
- 2.3 Global search widget (§2.1.4) — base portable, focus-glow deferred to Phase 5.
- 2.4 Sidebar shell + pin button (§2.1.5, 2.1.7).
- **Deliverable:** ~120 LOC + modified MainWindow.cpp. Per-page sidebar becomes a shared `NavSection` subcomponent.
- **Risk:** MainWindow is heavily edited; need to preserve window-controls + keyboard shortcuts from existing code. Smoke matrix: F11 fullscreen, Ctrl+R refresh, menu button, tab switching across 4 modes.

### Phase 3 — Tile + grid primitives

- 3.1 `TileCard.cpp` adopt Theme tokens (§1.1, §1.2) + `:hover` QSS + `QPropertyAnimation` on `geometry` for scale-on-hover (§2.2.7–2.2.9). **~30 LOC edit** on a 500-line file.
- 3.2 `TileCard` noThumb film-strip placeholder (§2.2.10).
- 3.3 Per-page `seriesGrid` equivalent: confirm `QGridLayout` with `resizeEvent` dynamic column count is already the pattern (VideosPage/BooksPage) — if not, port (§2.2.6).
- **Deliverable:** tile-level fit-and-finish. Visible win even before inside-series port.
- **Risk:** the hover-scale animation needs 60fps budget verified on 200+ tiles (seriesGrid can be large libraries). If perf-bad, degrade to CSS border-color-only hover.

### Phase 4 — Inside-series views

- 4.1 `SeriesView.cpp` + `BookSeriesView.cpp` + `ShowView.cpp` — 3 parallel ports (§2.3.1–2.3.6).
- 4.2 Videos resolution column (§2.3.7).
- 4.3 Progress delegate (§2.3.8) — shared across all three via `LibraryProgressDelegate`.
- **Deliverable:** ~150-200 LOC across 3 files + 1 new delegate.
- **Risk:** each `QTableView` is wired to a different model; need to verify col-index stability.

### Phase 5 — Transient surfaces + hover-glow polish

- 5.1 `Toast.cpp` restyle + `QPropertyAnimation` slide-in (§2.5.1).
- 5.2 `ContextMenuHelper.cpp` QMenu restyle + danger QAction class (§2.5.3, 2.5.5).
- 5.3 `LoadingOverlay.cpp` library-mode variant (§2.5.6–2.5.8).
- 5.4 Search focus-glow fade (§2.6.5).
- 5.5 Tile-hover scale (Phase 3 deferred if perf-flagged).
- 5.6 Scan pill widget (§2.2.12).
- **Deliverable:** ~150 LOC across ~5 files.

### Phase 6 — NEW library surfaces (K-overlay, etc.)

- 6.1 `KeysOverlay.cpp` — keyboard tips overlay, 'K' to toggle (§2.5.9). New feature, not parity-critical.
- 6.2 Anything else surfaced during Phases 1-5 smoke that Hemanth wants.
- **Deliverable:** discretionary.

**Phases explicitly EXCLUDED from this proposal (per `feedback_qt_vs_electron_aesthetic.md`):**

- All §2.8 decorative bucket (bgFx, backdrop-filter blur, ::before/::after pseudo-elements, mix-blend-mode, multi-property transitions, hue-rotate filters). **Do not propose for a QML track from Agent 5 — that is a Hemanth-scope directional pivot, not a TODO.** Memory `feedback_qt_vs_electron_aesthetic.md` is quite clear: "QSS structurally can't replicate Tankoban-Max Noir CSS. Stop proposing ports; only QML closes the gap." This audit honors that by flagging + skipping, not by sneaking them under a "maybe" flag.

---

## 4. Scope of "Tankoban-Max surface" vs "Tankoban 2 surface"

Critical asymmetry to flag before authoring any fix-TODOs:

**Tankoban 2 has surfaces Tankoban-Max does not:**

- **Tankoyomi** (manga online sources + reader) — `TankoyomiPage.cpp` (7 setStyleSheet sites), `MangaResultsGrid.cpp`, `AddMangaDialog.cpp`
- **Tankorent** (torrent client with tabs — general/files/peers/trackers) — `TankorentPage.cpp` + 4 `tankorent/*Tab.cpp`
- **TankoLibrary** (book shadow-library search) — `TankoLibraryPage.cpp` (31 sites — second-highest in repo), `BookResultsGrid.cpp`, `TransfersView.cpp`
- **StreamPage** Stremio-style detail view, continue strip, addon manager, catalog browse, source cards — 12 files under `src/ui/pages/stream/*`

**Tankoban-Max has surfaces Tankoban 2 does not:**

- Embedded web browser with tab strip + omnibox (the `webLibraryView` in index.html:1530+) — Tankoban 2 has Tankorent + TankoLibrary as successors that do NOT share the browser UI shape
- Audiobook player overlay with chapter list (books-reader.css:154+) — Tankoban 2 has `AudiobookDetailView.cpp` as a start (7 sites); Agent 2's `AUDIOBOOK_PAIRED_READING_FIX` Phase 2 is actively extending this

**Implication for the map:** porting is **one-way subset** — Tankoban-Max's looks go onto Tankoban 2's widget tree. The Tankoban-2-only surfaces (Tankorent tabs, TankoLibrary, StreamPage variants) should **inherit the Phase 1 tokens and Phase 2 chrome** without porting anything surface-specific from Tankoban-Max because there is no Tankoban-Max analogue to port from. Agent 4B + Agent 4 surface styling stays in-domain; the tokens give them consistency without constraining.

---

## 5. Cross-references to existing reader ports

Per `feedback_reader_rendering.md` ("Tankoban-Max is source of truth for reader UX. Match behavior 1:1"), Tankoban 2's BookReader and ComicReader were already drawn from the same CSS files this audit surveys:

- **`ComicReader.cpp`** — 75 `setStyleSheet` call sites, highest in the repo. Already references `comic-reader.css` patterns (ytIcon, playerBar gradient, stageWrap). **No new work from this audit flows here** — Agent 1 owns comic-reader visual fidelity and has its own fix-TODO track (`COMIC_READER_FIX_TODO.md` Phase 6 closed, polish mode).
- **`BookReader.cpp`** (7 sites) + **`BookSeriesView.cpp`** (14 sites) — drawn from `books-reader.css` (3374 lines, ~7% of my remaining unread). Agent 2 owns; `BOOK_READER_FIX_TODO.md` tracks. **No new work flows here either.**
- **`VideoPlayer.cpp`** (18 sites) + **`FrameCanvas.cpp`** — Agent 3 owns; Congress 8 ratified, FC-2 aspect-persistence + Phase-4 reference-reading track active. **Out of scope.**

**What remains for Agent 5 after this audit ratifies:** the library-side surfaces in §2.1, §2.2, §2.3, §2.5, §2.6, §2.7. Roughly 500-700 LOC across 15 files at final estimate, ~6 phases.

---

## 6. Source-reference discipline (Congress 8 continuity)

Congress 8 ratified the principle: read the actual source at fix time, don't work from audit summaries (`feedback_reference_during_implementation.md`). When implementing Phase 2-5 of this map, implementors MUST:

- Open `C:\Users\Suprabha\Downloads\Tankoban-Max-master\Tankoban-Max-master\src\styles\{file}.css` and read the surrounding context — especially for selectors that depend on CSS cascade order (ui-tokens → ui-bridge → styles → overhaul → theme-light → reader-specific → video-library-match).
- Cross-check the HTML at `src/index.html` to see which classes actually compose onto which DOM nodes — some selectors (e.g. `#videoShowsGrid .seriesCard`) only fire in specific contexts.
- Verify by reference (not guess) when `!important` chains appear — `video-library-match.css` uses 30+ `!important`s to override `overhaul.css` — the winning value is often several layers deep.

---

## 7. What this audit does NOT do

Explicit exclusions, to make the next handoff clean:

- **No `src/` writes.** All code references are for the follow-on wakes' implementors, not for this one.
- **No "which option to pick" for the §0 color-memory tension.** That's a Hemanth call.
- **No prototype or proof-of-concept** of any phase. If Hemanth wants a Phase-1-tokens proof before ratifying Phase 2+, he can summon me for that scope specifically.
- **No decorative-layer ports.** All §2.8 and the backdrop-filter items in §2.5/2.6 are flagged-not-proposed. A QML track is a separate Hemanth-level decision, not something this audit smuggles in via a Phase 7.
- **No ports of Tankoban-Max-only surfaces** (embedded browser, audiobook-player overlay in its pre-Phase-2 Agent 2 shape, mpv-holy-grail experiments in `archive/`, etc.).
- **No commitment on tokens surviving Option 2/3 gating.** Token values in §1 assume Option 1; they flex to greyscale in Option 2/3 with a single-function change.
- **No cross-agent port.** If Agent 1/2/3/4/4B/7 want visual parity, they own their own domain ports using the Phase-1 tokens as shared foundation. This audit does NOT author their TODOs.

---

## 8. Next actions (conditional on Hemanth response)

1. **Hemanth picks §0 option** → update tokens accordingly, commit the pick as a new memory (`feedback_tankoban_max_accent_decision.md`) pointing to this audit.
2. **Hemanth ratifies phase order** → Agent 0 authors `TANKOBAN_MAX_REPLICATION_FIX_TODO.md` with the 6 phases above as the skeleton.
3. **Agent 5 executes Phase 1** on next summon (Theme + TankobanFont + global scrollbar QSS — ~150 LOC, zero-risk).
4. **Per-phase Hemanth smoke** at each phase boundary per Rule 11 + `feedback_one_fix_per_rebuild.md`.

---

**END AUDIT** — no further work this wake.
