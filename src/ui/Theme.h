#pragma once

#include <QColor>
#include <QString>
#include <array>

class QApplication;
class QIcon;

// Tankoban-Max → Tankoban 2 theme system. Single axis:
//   Mode = 5 theme palettes (Dark / Nord / Solarized / Gruvbox / Catppuccin).
//   Each Mode owns its own bg + accent. Cycled via the sun picker button
//   in the topbar.
//
// Light-mode history: removed 2026-04-25 P3.1 ("doesn't fit here"); re-added
// later same day under P5 as 3-texture comparison (Dawn / Cream / Beige);
// removed AGAIN 2026-04-26 ~10:18 per Hemanth "remove the white colours for
// now, we will include them with everything planned out, everything visible
// and stuff" — Light returns later as a planned, full-coverage feature; not
// a partial chrome-only port. Cookbook + plan reference at
// memory/project_theme_p5_light_in_progress.md.
//
// Source audit:  agents/audits/qt_theme_feasibility_2026-04-25.md (§ 5.2 PATH B)
// Source TODO:   THEME_SYSTEM_FIX_TODO.md
//
// Spec correction 2026-04-25: the original PATH B audit + Phase 1 ship
// included a second axis ("Preset" — 7 palette swatches). On Phase-2 smoke
// Hemanth pulled back from the Preset axis ("I've never asked for colour
// palettes" / "42 looks complicate things"). Pre-Phase-3 removal applied;
// system reduces to single Mode axis with each Mode owning its accent.

namespace Theme {

// ── Existing scaffolding (from 2026-04-24 Phase 1 foundation ship) ─────────────
// Kept verbatim for back-compat. TileCard.cpp consumes Theme::kAccent literally
// for its hover/select gold border. These tokens are also the "Noir defaults"
// that the new ThemePalette resolves to for Mode::Dark.

enum class AppSection { Comics, Books, Videos, Stream, Default };

inline constexpr auto kBg         = "#050505";
inline constexpr auto kPanel      = "rgba(255,255,255,0.06)";
inline constexpr auto kPanelHover = "rgba(255,255,255,0.10)";
inline constexpr auto kText       = "#eeeeee";
inline constexpr auto kMuted      = "rgba(238,238,238,0.58)";
inline constexpr auto kBorder     = "rgba(255,255,255,0.10)";
inline constexpr auto kBorderHi   = "rgba(255,255,255,0.16)";
// Harbor redesign (2026-06-15 Phase 1 Task 1): the single jewel accent flips
// from the old desaturated tan #c7a76b to brand gold #e8b923. TileCard.cpp
// reads kAccent literally for its hover/select border (load-bearing).
inline constexpr auto kAccent     = "#e8b923";
inline constexpr auto kAccentSoft = "rgba(232,185,35,0.22)";
inline constexpr auto kAccentLine = "rgba(232,185,35,0.40)";

inline QColor accentForSection(AppSection /*section*/)
{
    return QColor(0xe8, 0xb9, 0x23);
}

// ── Harbor radius ladder (2026-06-15 Phase 1 Task 1) ───────────────────────────
// Spec §3 radius ladder: 6 / 10 / 14 / 20 / 28 px + 999px pills. These are the
// canonical corner radii for Harbor widgets (cards, pills, hero, nav buttons).
inline constexpr int kRadXs   = 6;
inline constexpr int kRadSm   = 10;
inline constexpr int kRadMd   = 14;
inline constexpr int kRadLg   = 20;
inline constexpr int kRadXl   = 28;
inline constexpr int kRadPill = 999;

// ── Harbor ease curves (notes for Task 3 animation callers) ────────────────────
// Eases are C++ constants for QPropertyAnimation::setEasingCurve callers (QSS
// cannot consume them). No animation code lives in Task 1 — these are the
// authoring notes for the NavRail collapse + hover animators in Task 3:
//   kEaseOut  ~ cubic-bezier(0.16, 1, 0.3, 1)     -> QEasingCurve::OutQuint (closest builtin)
//   kEasePull ~ cubic-bezier(0.32, 0.72, 0.24, 1) -> custom QEasingCurve via addCubicBezierSegment

inline constexpr int kLibTopbarH  = 56;
inline constexpr int kLibSideW    = 252;
inline constexpr int kLibGap      = 12;
inline constexpr int kLibPad      = 12;
inline constexpr int kLibRadius   = 14;
inline constexpr int kLibRadiusSm = 10;
inline constexpr int kLibRadiusLg = 18;
inline constexpr int kTileRadius       = 12;
inline constexpr int kTileCornerRadius = 8;

// ── Theme system types ─────────────────────────────────────────────────────────

enum class Mode { Dark, Nord, Solarized, Gruvbox, Catppuccin };

// Resolved per-active-mode palette. ~17 slots — every color that varies across
// modes in noirStylesheet's templated form. buildStylesheet consumes this and
// produces the final QSS string via __PLACEHOLDER__ substitution.
struct ThemePalette {
    QString bg0;          // app background / canvas (Harbor oklch .18 #121317)
    QString bg1;          // raised panels (Harbor oklch .32 #2d333f)
    QString text;         // primary text (Harbor #f3f1ea)
    QString textDim;      // toast / secondary (Harbor #cfd4dc)
    QString muted;        // muted text (Harbor #aab1bd)
    QString border;       // hairline border (rgba(255,255,255,0.10))
    QString borderHover;  // hover border (rgba(255,255,255,0.16))
    QString accent;       // primary accent (Harbor gold #e8b923)
    QString accentSoft;   // selection fill (rgba(232,185,35,0.22))
    QString accentLine;   // focus border (rgba(232,185,35,0.40))
    // ── Harbor elevation ladder (2026-06-15 Phase 1 Task 1) ───────────────────
    // OKLCH hue-260 low-chroma ladder baked to sRGB; bg0 is the canvas, then
    // surface < elevated < raised ascend in lightness. onAccent = ink that sits
    // on the gold accent; error = firewalled semantic red (never per-mode).
    QString surface;      // surface oklch .22 (#1a1d24) — panels above canvas
    QString elevated;     // elevated oklch .27 (#232833) — cards / popovers / pills
    QString raised;       // raised oklch .32 (#2d333f) — hover rows / active nav fill
    QString onAccent;     // text/ink on the gold accent (#14110a)
    QString error;        // semantic error red (#e50914) — firewalled, never brand
    QString topbarBg;     // topbar surface (rgba(8,8,8,0.52))
    QString sidebarBg;    // sidebar surface (rgba(8,8,8,0.46))
    QString menuBg;       // menu / popover surface (rgba(8,8,8,0.88))
    QString toastBg;      // toast surface (rgba(8,8,8,0.82))
    QString cardBg;       // overlay card (rgba(8,8,8,0.92))
    QString overlayDim;   // modal dim (rgba(0,0,0,180))
    QString inkRgb;       // RGB triplet for derived alpha overlays — e.g.
                          // rgba(__INK_RGB__,0.06) substitutes to
                          // rgba(255,255,255,0.06) for Dark and
                          // rgba(20,20,24,0.06) for Light. Used everywhere
                          // the QSS template paints "ink on surface" overlays
                          // (button bg, hover, scrollbar handle, table border).
};

// Mode registry. Each Mode owns its own bg0/bg1 + accent + accentRgb + inkRgb.
// inkRgb is the triplet used for derived overlays: light on dark for the
// Dark/Nord/Solarized/Gruvbox/Catppuccin variants, dark on light for the
// Light variant. Sourced from Tankoban-Max theme-light.css per-mode
// `body[data-app-theme="<mode>"]` blocks (`--ink-rgb` variable).
struct ThemeModeEntry {
    Mode        id;
    const char* slug;       // QSettings serialization
    const char* label;      // user-facing
    const char* bg0;        // app background
    const char* bg1;        // raised panels
    const char* accent;     // primary accent
    const char* accentRgb;  // "R,G,B" — used for rgba(R,G,B,a) derivation
    const char* inkRgb;     // "R,G,B" ink-color triplet for surface overlays
};

inline constexpr std::array<ThemeModeEntry, 5> kModes = {{
    // Harbor (2026-06-15 Phase 1 Task 1): Dark resolves to the OKLCH ladder
    // canvas/raised + brand gold accent. resolvePalette() overwrites
    // bg0/bg1/accent/inkRgb from this row and derives accentSoft/accentLine from
    // accentRgb, so the gold flip MUST live here too (not only in
    // darkBaselineNoir()). bg0 = canvas oklch .18; bg1 = raised oklch .32.
    {Mode::Dark,       "dark",       "Dark",       "#121317", "#2d333f", "#e8b923", "232,185,35",  "255,255,255"},
    {Mode::Nord,       "nord",       "Nord",       "#2e3440", "#3b4252", "#88c0d0", "136,192,208", "216,222,233"},
    {Mode::Solarized,  "solarized",  "Solarized",  "#002b36", "#073642", "#b58900", "181,137,0",   "192,207,207"},
    {Mode::Gruvbox,    "gruvbox",    "Gruvbox",    "#282828", "#3c3836", "#fe8019", "254,128,25",  "235,219,178"},
    {Mode::Catppuccin, "catppuccin", "Catppuccin", "#1e1e2e", "#313244", "#cba6f7", "203,166,247", "205,214,244"},
}};

// ── API ───────────────────────────────────────────────────────────────────────

// Compute the active palette for a mode. Pure function.
ThemePalette resolvePalette(Mode mode);

// Build the full QSS string from a palette via __PLACEHOLDER__ substitution.
QString buildStylesheet(const ThemePalette& palette);

// Apply mode to a QApplication: builds QPalette + QSS, calls setPalette() +
// setStyleSheet(), updates the current() cache.
void applyTheme(QApplication& app, Mode mode);

// Read mode from QSettings (defaults Dark if unset) and apply.
void applyThemeFromSettings(QApplication& app);

// Active palette accessor — useful for code that wants to introspect the
// current accent color etc. without re-running resolvePalette.
const ThemePalette& current();

// Per-mode accent-blob colors for GlassBackground's radial gradient overlay.
// Three QColors per mode (with built-in alphas) corresponding to the three
// drift-blob positions GlassBackground paints at (top-left / top-right /
// bottom-center). Sourced from Tankoban-Max theme-light.css `.bgFx` blocks
// per mode. Dark/Noir returns the neutral baseline (matching the original
// hardcoded GlassBackground colors); each named mode returns its own warm /
// cool / saturated palette so the canvas atmosphere tracks the mode.
struct ModeBlobs {
    QColor a;  // top-left blob
    QColor b;  // top-right blob
    QColor c;  // bottom-center blob
};
ModeBlobs accentBlobsForMode(Mode mode);

// Active mode + active mode's accent blobs — populated by applyTheme()
// at theme-switch time and read by per-paint surfaces (GlassBackground)
// without paying the QSettings + QColor allocation cost on every paint.
// Mirrors the current() pattern for cached palette state.
Mode      currentMode();
ModeBlobs currentBlobs();

// Render an SVG asset to a tinted QIcon. The SVGs at resources/icons/
// hardcode stroke="#c6c6c6"; this helper string-replaces it with the
// caller's tint color before rendering, so an icon contrasts against the
// active theme's surface (light ink on dark topbar, dark ink on light
// topbar). Originally a private helper in ThemePicker; promoted here so
// any caller can re-tint icons against Theme::current().text without
// duplicating the QSvgRenderer + QPainter dance.
QIcon tintedSvgIcon(const QString& path, const QColor& tint, int size = 32);

// QSettings persistence.
//   theme/mode = "dark" | "light" | "nord" | "solarized" | "gruvbox" | "catppuccin"
Mode loadMode();
void saveMode(Mode);

// Slug ↔ enum conversion (QSettings string round-trip).
QString slugFor(Mode);
Mode    modeFromSlug(const QString& slug, Mode fallback = Mode::Dark);

} // namespace Theme
