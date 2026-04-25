#pragma once

#include <QColor>
#include <QString>
#include <array>

class QApplication;

// Tankoban-Max → Tankoban 2 theme system. Two orthogonal axes:
//   - Mode (axis A):  Dark | Light       — toggled via sun/moon button
//   - Preset (axis B): 7 vibrant swatches — picked via paint-palette popover
//
// Phase 1 (this scaffolding): infrastructure only, defaults Dark+Noir,
// behavior unchanged from pre-Phase-1 build.
//
// Source audit:  agents/audits/qt_theme_feasibility_2026-04-25.md (§ 5.2 PATH B)
// Source TODO:   THEME_SYSTEM_FIX_TODO.md
// Reference:     C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\theme.py:97-244

namespace Theme {

// ── Existing scaffolding (from 2026-04-24 Phase 1 foundation ship) ─────────────
// Kept verbatim for back-compat. TileCard.cpp consumes Theme::kAccent literally
// for its hover/select gold border. These tokens are also the "Noir defaults"
// that the new ThemePalette resolves to for (Mode::Dark, Preset::Noir).

enum class AppSection { Comics, Books, Videos, Stream, Default };

inline constexpr auto kBg         = "#050505";
inline constexpr auto kPanel      = "rgba(255,255,255,0.06)";
inline constexpr auto kPanelHover = "rgba(255,255,255,0.10)";
inline constexpr auto kText       = "#eeeeee";
inline constexpr auto kMuted      = "rgba(238,238,238,0.58)";
inline constexpr auto kBorder     = "rgba(255,255,255,0.10)";
inline constexpr auto kBorderHi   = "rgba(255,255,255,0.16)";
inline constexpr auto kAccent     = "#c7a76b";
inline constexpr auto kAccentSoft = "rgba(199,167,107,0.22)";
inline constexpr auto kAccentLine = "rgba(199,167,107,0.40)";

inline QColor accentForSection(AppSection /*section*/)
{
    return QColor(0xc7, 0xa7, 0x6b);
}

inline constexpr int kLibTopbarH  = 56;
inline constexpr int kLibSideW    = 252;
inline constexpr int kLibGap      = 12;
inline constexpr int kLibPad      = 12;
inline constexpr int kLibRadius   = 14;
inline constexpr int kLibRadiusSm = 10;
inline constexpr int kLibRadiusLg = 18;
inline constexpr int kTileRadius       = 12;
inline constexpr int kTileCornerRadius = 8;

// ── Theme system types (Phase 1 of THEME_SYSTEM_FIX_TODO) ─────────────────────

enum class Mode { Dark, Light };

enum class Preset { Noir, Midnight, Ember, Forest, Lavender, Arctic, Warm };

// Resolved per-active-theme palette. ~16 slots — every color that varies across
// modes + presets in noirStylesheet's templated form. buildStylesheet consumes
// this and produces the final QSS string via __PLACEHOLDER__ substitution.
struct ThemePalette {
    QString bg0;          // app background (#050505 noir)
    QString bg1;          // raised panels (#0a0a0a noir)
    QString text;         // primary text (#eeeeee)
    QString textDim;      // toast / secondary (#e0e0e0)
    QString muted;        // muted text (rgba(238,238,238,0.58))
    QString border;       // hairline border (rgba(255,255,255,0.10))
    QString borderHover;  // hover border (rgba(255,255,255,0.16))
    QString accent;       // primary accent (#c7a76b noir gold)
    QString accentSoft;   // selection fill (rgba(199,167,107,0.22))
    QString accentLine;   // focus border (rgba(199,167,107,0.40))
    QString topbarBg;     // topbar surface (rgba(8,8,8,0.52))
    QString sidebarBg;    // sidebar surface (rgba(8,8,8,0.46))
    QString menuBg;       // menu / popover surface (rgba(8,8,8,0.88))
    QString toastBg;      // toast surface (rgba(8,8,8,0.82))
    QString cardBg;       // overlay card (rgba(8,8,8,0.92))
    QString overlayDim;   // modal dim (rgba(0,0,0,180))
};

// Static registry of presets (axis B). Mirror Tankoban-Max's THEME_PRESETS at
// shell_bindings.js:905-913. accentRgb is the unwrapped triplet used for
// rgba(R,G,B,a) interpolation in the stylesheet.
struct ThemePresetEntry {
    Preset      id;
    const char* slug;       // "noir" — QSettings serialization
    const char* label;      // "Noir" — user-facing
    const char* bg0;
    const char* bg1;
    const char* accent;
    const char* accentRgb;  // "199,167,107"
    const char* swatch;     // picker-circle bg (decorative; P2 picker UI uses)
};

inline constexpr std::array<ThemePresetEntry, 7> kPresets = {{
    {Preset::Noir,     "noir",     "Noir",     "#050505", "#0a0a0a", "#c7a76b", "199,167,107", "#1a1c24"},
    {Preset::Midnight, "midnight", "Midnight", "#080d14", "#0d1117", "#58a6ff", "88,166,255",  "#162030"},
    {Preset::Ember,    "ember",    "Ember",    "#100808", "#1a0c0c", "#ff6b4a", "255,107,74",  "#2a1410"},
    {Preset::Forest,   "forest",   "Forest",   "#060e08", "#0c1a10", "#4ade80", "74,222,128",  "#0f2418"},
    {Preset::Lavender, "lavender", "Lavender", "#0c080e", "#140c1a", "#c084fc", "192,132,252", "#1e1228"},
    {Preset::Arctic,   "arctic",   "Arctic",   "#060c14", "#0c1420", "#7dd3fc", "125,211,252", "#0e1c30"},
    {Preset::Warm,     "warm",     "Warm",     "#0e0c04", "#1a1408", "#fbbf24", "251,191,36",  "#2a2010"},
}};

// ── API ───────────────────────────────────────────────────────────────────────

// Compute the active palette for a (mode, preset) pair. Pure function.
ThemePalette resolvePalette(Mode mode, Preset preset);

// Build the full QSS string from a palette via __PLACEHOLDER__ substitution.
QString buildStylesheet(const ThemePalette& palette);

// Apply (mode, preset) to a QApplication: builds QPalette + QSS, calls
// setPalette() + setStyleSheet(), updates the current() cache.
void applyTheme(QApplication& app, Mode mode, Preset preset);

// Read mode + preset from QSettings (defaults Dark + Noir if unset) and apply.
void applyThemeFromSettings(QApplication& app);

// Active palette accessor — useful for code that wants to introspect the
// current accent color etc. without re-running resolvePalette.
const ThemePalette& current();

// QSettings persistence (split keys per RESOLVED ANSWER #6 — fixes Tankoban-Max's
// shared-key boot-reset bug where both axes wrote to localStorage.appTheme).
//   theme/mode    = "dark" | "light"
//   theme/preset  = "noir" | "midnight" | ... | "warm"
Mode   loadMode();
Preset loadPreset();
void   saveMode(Mode);
void   savePreset(Preset);

// Slug ↔ enum conversion (QSettings string round-trip).
QString slugFor(Preset);
QString slugFor(Mode);
Preset  presetFromSlug(const QString& slug, Preset fallback = Preset::Noir);
Mode    modeFromSlug(const QString& slug, Mode fallback = Mode::Dark);

} // namespace Theme
