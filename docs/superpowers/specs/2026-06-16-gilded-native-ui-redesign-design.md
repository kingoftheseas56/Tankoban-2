# Gilded — Native C++/Qt UI Redesign (Design Spec)

**Date:** 2026-06-16
**Author:** Agent 0 (with Hemanth)
**Status:** Approved direction; ready for implementation planning
**Supersedes:** the QWebEngine web-UI pivot (retired); see memory `project_native_cpp_qt_direction_locked_2026-06-15`.

---

## 1. Context & Decision Lineage

The content UI is built **natively in C++/Qt6, inside Tankoban 2**, as **our own design** — a pastiche/homage of the apps that influenced us (Harbor, Stremio, Kodi, Netflix), never a 1:1 clone. This was decided 2026-06-15:

- The **QWebEngine pivot** (rendering Tankoban Electron's React UI in our window) worked and was validated, but Hemanth retired it as a *destination* — reasons: (1) ownership ("it's ours, not ripping someone else's work"); (2) the C++ ceiling — future features require native C++ that a web bridge would cap. It stays only as flag-gated dormant code (`TANKOBAN_WEB_UI=1`).
- **Flutter** (the 2026-06-05 migration) is **dead**.
- The **libmpv embedding constraint** that historically forced native C++ is now resolved (it embeds cleanly in Electron too), so native Qt is a **deliberate choice by vision**, not a forced one.
- **Tankoban Electron** remains the **always-ahead, fast-dev reference** — we study it and Harbor for taste/patterns, then synthesize our own.

The native engine (libmpv/ffmpeg player, libtorrent, ffmpeg sidecar, manga/comics scrapers, downloads, dev-control bridge) is the crown jewel and is **kept untouched** — this redesign is a **UI-layer** effort only.

### Decisions locked in this brainstorm (2026-06-15/16)

| Question | Decision |
|---|---|
| Flagship mode | **Theatre first**; Comics/Manga/Books reuse the kit afterward |
| Visual identity | **Build on the shipped foundation** — gold `#e8b923`, Fraunces + Inter, OKLCH hue-260 dark ladder, the NavRail |
| Boldness | **Familiar shell + signature moments** — known streaming grammar (rail · hero · rows) + a few unmistakably-Tankoban flourishes |
| Direction | **C · Gilded** — gold treated as a *material*, not just an accent |
| Scope of this arc | **Browse surfaces only** — Home, catalogue "See all", series/title detail. Reader/player chrome are a separate later arc |

---

## 2. Goals & Non-Goals

**Goals**
- A cohesive **Gilded** native design language, expressed through the existing QSS token system (so all 5 theme modes inherit it for free).
- A **reusable component kit** so every screen is *assembled*, not hand-built (this is what prevents the per-component grind).
- **Full-quality posters everywhere** — a hard, testable requirement (Section 4).
- Theatre browse surfaces (Home → See-all → Detail) rebuilt to the Gilded language; then the kit rolls to Comics/Manga/Books.

**Non-Goals (explicitly out of scope)**
- Comic reader & video player chrome (separate arc; they own hard-won native logic).
- The QWebEngine web UI (retired) and Flutter (dead).
- Watch-Together, parental controls, TMDB/Trakt/Simkl service bindings, anime-award overlays (Harbor leave-behinds — not our domain).
- Re-litigating the visual identity (gold/Fraunces/Inter/NavRail are the locked foundation).

---

## 3. The Gilded Design Language

Built **entirely on the existing locked foundation** (`src/ui/Theme.{h,cpp}`):

**Tokens (already in `darkBaselineNoir()`, Theme.cpp:84–130):**
- Surfaces (OKLCH hue-260 ladder): canvas `#121317` · surface `#1a1d24` · elevated `#232833` · raised `#2d333f`
- Ink: `#f3f1ea` / dim `#cfd4dc` / muted `#aab1bd`
- Accent gold `#e8b923`; accentSoft `rgba(232,185,35,.22)`; accentLine `rgba(232,185,35,.40)`; onAccent `#14110a`
- Borders `rgba(255,255,255,.10)` / hover `.16`; error `#e50914` (firewalled, never brand)
- Radius ladder (Theme.h:63–68): `kRadXs 6 · kRadSm 10 · kRadMd 14 · kRadLg 20 · kRadXl 28 · kRadPill 999`
- Motion (NavRail.cpp:44–52): `kEaseOut` (≈ cubic-bezier 0.16,1,0.3,1) and `kEasePull` (≈ cubic-bezier 0.32,0.72,0.24,1, "the pull")
- Fonts: **Fraunces** (display/serif) + **Inter** (body), `registerFonts()` Theme.cpp:1191–1210

**Signature moments (what makes it "Gilded" / ours):**
1. **Gilded featured frame** — the hero/featured surface wears a thin gold frame (1px `accentLine` border + soft inset gold glow + outer warm drop-glow).
2. **Warm gold vignette** — a subtle warm radial wash from the top edge of each home (`GlassBackground` accent or a page-level gradient).
3. **Gold hairlines under row titles** — each catalogue-row header sits over a left-weighted gold→transparent hairline.
4. **Gold as the "active/progress" material** — progress bars, hover rings (2px inset gold on card hover), and the active-state fills all use the jewel gold; everything else stays restrained so gold reads as precious.
5. **Motion = `kEasePull`** on card lift (`translateY(-8px)` + shadow bloom), hero cross-fade, and rail collapse — taut, physical, consistent.

All of the above is delivered through the `kTemplate` QSS `__TOKEN__` substitution system (Theme.cpp:324–1096) plus a small set of new object-name styled rules — **no per-mode work** (the 4 other theme modes retint automatically).

---

## 4. Full-Quality Poster Pipeline (HARD REQUIREMENT)

Posters/thumbnails rendering blurry is a recurring, load-bearing failure ("last time every poster was blurred"). This redesign treats sharpness as **non-negotiable** — a blurry cover in a gilded frame is worse than in a plain one.

### Root causes (verified, file:line)

1. **HiDPI 1× rendering (the universal cause).** `TileCard.cpp:139–148` scales pixmaps to *logical* size and stores `m_basePixmap` **without `setDevicePixelRatio(dpr)`**. On a 2× display Qt upscales every tile 2× on paint → *all* posters blur regardless of source quality.
2. **Low-res source URLs.** Manga tiles use AniList `medium` (~256px) instead of `large`/`extraLarge` (`MangaPage.cpp:3457`, `AniListParser.cpp:55–60`). Catalog tiles are *actively degraded* to metahub `small` (`CatalogBrowseScreen.cpp:128–137`, used :724).
3. **Upscaling small → big card** (consequence of 1 + 2).
4. **Hero backdrop half-res.** `FeaturedHero.cpp:541–542` scales the backdrop to logical `size()` inside a correctly DPR-sized canvas → half physical resolution on HiDPI. Also falls back to a portrait poster when no wide backdrop exists.
5. **No TMDB size-token rewriting** — addon URLs pass through verbatim (no `w780`/`original`).
6. **Small images locked in cache.** `PosterCache.cpp:98` has a `scaledSize` param that is **never passed** by callers; once a small image is cached it is served forever.

### The fix (becomes plan tasks)

**(a) URL selection per provider:**
| Provider | Use |
|---|---|
| AniList | `coverImage.extraLarge` (pass `coverFullUrl`, not `coverThumbUrl`) |
| Cinemeta/metahub | remove the `small` rewrite; prefer `large` (min `medium`) |
| TMDB (from addons) | rewrite `/t/p/wNNN/` → `/w780/` (cards) or `/original/` (hero) |
| iTunes covers | rewrite trailing `100x100bb` → `600x600bb` |
| Hero backdrop | prefer the wide `background`/`backdrop` (TMDB `w1280`/`original`), never the portrait poster |
| WeebCentral | ~71% source downscale is a known limitation; document, no in-app fix |

**(b) Qt scaling (DPR-aware, smooth, never upscale):**
```cpp
const qreal dpr = devicePixelRatioF();
const QSize physical = QSize(innerW, innerH) * dpr;
QPixmap scaled = src.scaled(physical, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
// center-crop to exact physical, then:
scaled.setDevicePixelRatio(dpr);
```
If `src.width() < physical.width()`, fix the URL layer (fetch bigger) rather than upscale. Apply the same `size()*dpr` fix to `FeaturedHero::composeSlide()`.

**(c) Caching:** cache at **display resolution** keyed by `"<id>_<physW>x<physH>"`; decode directly to `physical` via `QImageReader::setScaledSize()` (pass the long-unused `PosterCache` `scaledSize`). Never cache `small`/`medium`.

### Acceptance criterion (testable)
> Every displayed poster's source width ≥ `card_logical_width × devicePixelRatioF()`; all scaling uses `Qt::SmoothTransformation`; the stored pixmap has `setDevicePixelRatio(dpr)` applied; no source is upscaled. Check: `pix.width() >= card->width() * card->devicePixelRatioF()` holds for every pixmap entering `TileCard::setThumbPixmap()`.

---

## 5. The Reusable Component Kit

### Reuse as-is / lightly restyle (already built)
| Component | File | Role in kit |
|---|---|---|
| **NavRail** | `src/ui/widgets/NavRail.*` | Left nav shell (240/72, gold-active, flyout). Done. |
| **CenterSearchBar** | `src/ui/widgets/CenterSearchBar.*` | Window-centered frosted search pill. Done. |
| **FeaturedHero** | `src/ui/pages/stream/FeaturedHero.*` | Home billboard carousel (scrim + melt + `kEasePull` cross-fade). Add: **gilded frame**, **DPR backdrop fix** (§4). |
| **TileCard** | `src/ui/pages/TileCard.*` | Atomic portrait poster card. Add: **DPR/full-quality fix** (§4); align radius to `kRadMd`; gilded hover ring. |
| **TileStrip** | `src/ui/pages/TileStrip.*` | Grid + continue-scroll engine (density 0/1/2). Reused for grids/"See all". |
| **FadingStackedWidget** | `src/ui/widgets/FadingStackedWidget.*` | Per-page view transitions. |
| **GlassBackground** | `src/ui/GlassBackground.*` | Atmospheric canvas; host the warm gold vignette. |
| **Toast / SidebarDrawer** | `src/ui/widgets/*` | Small surfaces. |

### Build new (the ~5 gaps)
1. **CatalogueRow** — a horizontal catalogue shelf: a `SectionHeader` (title + "See all →") over a horizontal-scroll track of cards with edge-arrow affordances and the gold hairline. Distinct from TileStrip's grid; this is the Harbor "row" pattern that does not yet exist (`StreamHomeBoard` currently reuses the grid).
2. **Card variants:**
   - **ContinueCard** — poster/landscape with a 3px gold **progress bar** + play-overlay on hover + optional episode/chapter badge + dismiss.
   - **LandscapeCard** — 16:9 variant for episodes/continue/collections.
   - **RankCard** — "Top 10" with the large hollow Fraunces rank numeral behind the poster (signature typographic moment).
3. **Chip/Badge widget** — reusable pill (provenance/source/quality/score), replacing today's painted-on-pixmap chips.
4. **SectionHeader** — label (+ optional kicker) with an inline action slot ("See all →" / sort), with the gold hairline rule.
5. **SeriesDetailHero** — a per-series/title backdrop hero variant (TitlePlate + MetaPills + ActionBar + Synopsis + Episode/Volume list), gilded — used by the Theatre detail page and later by Comics/Manga/Books series pages.

All new components live in `src/ui/widgets/` (or `src/ui/pages/` for page-coupled ones), are signal-based and self-contained (one purpose, clear interface), and style through the QSS token system.

---

## 6. Screen Scope (Theatre first)

1. **Theatre Home** (`StreamPage` / `StreamHomeBoard`): gilded **FeaturedHero** → **Continue Watching** (`CatalogueRow` of `ContinueCard`) → catalogue rows ("Trending", "Popular", "Top 10" via `RankCard`, …) using `CatalogueRow` + `TileCard`. Replaces the current grid-style home rows.
2. **"See all" catalogue view** (`CatalogBrowseScreen`): a full grid (`TileStrip`) of cards for a single catalogue, with the poster fix applied.
3. **Series/Title detail** (`StreamDetailView`): **SeriesDetailHero** + meta pills + action bar (gold primary "Play", translucent secondary) + synopsis (4-line clamp + expand) + season/episode list.
4. **Then** Comics/Manga/Books (`MangaPage`, `WesternComicsPage`, `BooksPage`) adopt the same kit: they already host FeaturedHero + TileStrip; swap in `CatalogueRow`, the card variants, `SeriesDetailHero`, and the gilded styling + poster fix.

---

## 7. Architecture & Approach

- **Native C++/Qt6**, in Tankoban 2; engine untouched.
- **Styling via the `kTemplate` `__TOKEN__` QSS system** (Theme.cpp) — new components add object-name rules using existing tokens; gilded treatment = a small set of new rules + the signature gradients. All 5 theme modes inherit automatically.
- New widgets are **composable primitives** assembled into pages; no monolithic page rewrites.
- **Suggested phasing** (detailed in the implementation plan):
  - **P1 — Full-quality poster pipeline** (§4): the DPR fix + URL selection + cache. Foundational — *everything* looks better immediately and it de-risks the "blurry" failure first.
  - **P2 — Gilded token/QSS pass** + build the new kit components (CatalogueRow, card variants, Chip, SectionHeader, SeriesDetailHero).
  - **P3 — Assemble the Theatre Home** + the "See all" view + the Detail page from the kit.
  - **P4 — Roll the kit to Comics / Manga / Books.**

---

## 8. Definition of Done / Acceptance

- Theatre Home renders the gilded hero + Continue + catalogue rows; the Detail page is gilded; the "See all" grid works.
- **Poster acceptance criterion (§4) passes** on a HiDPI display — no blurry/upscaled posters anywhere.
- The new kit components are reusable and used by at least the Theatre surfaces.
- All 5 theme modes still render correctly (token system intact).
- No regression to NavRail, CenterSearchBar, the engine, or existing navigation.
- Builds clean (`build_check.bat` / `out_*` exe mtime advances); smoked live by Hemanth on the Theatre Home.

---

## 9. Risks & Open Questions

- **Backdrop data for the hero** (last night's grind): catalogs often lack a wide backdrop; the hero must source a real wide image (TMDB `original`/`w1280`) or gracefully fall back (blurred cover), never a stretched portrait. Covered by §4 but flagged as the historical pain point.
- **HiDPI assumption**: the fix assumes `devicePixelRatioF()` is meaningful on Hemanth's display; verify the actual DPR during P1 smoke.
- **TileCard radius inconsistency** (hardcoded 12px vs `kRadMd` 14) — align during the gilded pass.
- **Comics/Manga/Books detail parity**: `SeriesDetailHero` must generalize across video episodes and manga/comic volumes/chapters — design its interface for both from the start.

---

## 10. Key File Pointers

- Tokens/QSS: `src/ui/Theme.{h,cpp}` (ladder :84–130, radius :63–68, kTemplate :324–1096, registerFonts :1191–1210), `src/ui/TankobanFont.h`
- Existing kit: `src/ui/widgets/NavRail.*`, `CenterSearchBar.*`, `FadingStackedWidget.*`; `src/ui/pages/TileCard.*`, `TileStrip.*`, `src/ui/pages/stream/FeaturedHero.*`; `src/ui/GlassBackground.*`
- Poster fix sites: `TileCard.cpp:139–148`, `MangaPage.cpp:3457`, `AniListParser.cpp:55–60`, `CatalogBrowseScreen.cpp:128–137/724`, `FeaturedHero.cpp:518–574`, `PosterCache.cpp:98`
- Pages to restyle: `src/ui/pages/StreamPage.*` (+ `StreamHomeBoard`, `StreamDetailView`, `CatalogBrowseScreen`), `MangaPage.*`, `WesternComicsPage.*`, `BooksPage.*`
- Reference (study, don't trace): Harbor `%TEMP%/harbor-ref`; Tankoban Electron `…/Tankoban Electron/src/renderer/src`
- Memories: `project_native_cpp_qt_direction_locked_2026-06-15`, `reference_harbor_ultimate_design`, `project_tankoban_architecture_lineage`
