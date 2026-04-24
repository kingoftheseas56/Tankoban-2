#pragma once

#include <QColor>
#include <QString>

// Tankoban-Max → Tankoban 2 visual replication map Phase 1 foundation.
// Named constants extracted from Tankoban-Max's Noir overlay + library baseline.
// Migration target: existing inline QSS in src/main.cpp noirStylesheet() +
// the 450 scattered setStyleSheet() calls across 61 widget files.
//
// Reference audit: agents/audits/tankoban_max_replication_map_2026-04-24.md
// Reference source: C:\Users\Suprabha\Downloads\Tankoban-Max-master\...\src\styles\overhaul.css

namespace Theme {

// Per-mode accent gating (audit §0). Options:
//   1 — accept Tankoban-Max's per-mode accent (comics red / books blue / videos green).
//   2 — greyscale-only port (neutral grey for every mode).
//   3 — hybrid (greyscale chrome, section titles differentiate).
// Default ONE-LINE CHANGE POINT: swap accentForMode() body to enable.
// Today: returns the Noir gold that applyDarkPalette + noirStylesheet already use,
// matching the current live visual. Safe no-op until Hemanth picks.
enum class Mode { Comics, Books, Videos, Stream, Default };

// ── Palette (OLED base + Noir overlay) ─────────────────────────
inline constexpr auto kBg         = "#050505"; // app background
inline constexpr auto kPanel      = "rgba(255,255,255,0.06)"; // surface tint
inline constexpr auto kPanelHover = "rgba(255,255,255,0.10)"; // hover tint
inline constexpr auto kText       = "#eeeeee";
inline constexpr auto kMuted      = "rgba(238,238,238,0.58)";
inline constexpr auto kBorder     = "rgba(255,255,255,0.10)";
inline constexpr auto kBorderHi   = "rgba(255,255,255,0.16)";

// Noir gold accent — currently the app-wide accent (palette.Highlight in main.cpp
// + rgba(199,167,107,...) scattered across noirStylesheet). Single point of truth.
inline constexpr auto kAccent     = "#c7a76b";
inline constexpr auto kAccentSoft = "rgba(199,167,107,0.22)"; // selection fill
inline constexpr auto kAccentLine = "rgba(199,167,107,0.40)"; // focus border

inline QColor accentForMode(Mode /*mode*/)
{
    // Hemanth §0 pick pending. Today: neutral Noir gold for every mode.
    // If Option 1 lands: branch on mode → return per-mode hue.
    // If Option 2 lands: change to a white/grey (e.g. rgba(255,255,255,0.22)).
    // If Option 3 lands: keep neutral here, section titles carry the cue.
    return QColor(0xc7, 0xa7, 0x6b);
}

// ── Library sizing (matches Noir overlay.css :46-55) ───────────
inline constexpr int kLibTopbarH  = 56;  // current live value — Noir spec is 46, kept at 56 to avoid Phase 2 chrome rework this phase
inline constexpr int kLibSideW    = 252; // matches existing LibrarySidebar
inline constexpr int kLibGap      = 12;
inline constexpr int kLibPad      = 12;
inline constexpr int kLibRadius   = 14;
inline constexpr int kLibRadiusSm = 10;
inline constexpr int kLibRadiusLg = 18;

// ── Tile primitives (matches TileCard.cpp CORNER_RADIUS family) ─
inline constexpr int kTileRadius       = 12; // TileImageWrap matches
inline constexpr int kTileCornerRadius = 8;  // TileCard::CORNER_RADIUS

} // namespace Theme
