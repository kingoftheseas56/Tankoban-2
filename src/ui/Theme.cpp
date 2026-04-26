#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QIODevice>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QString>
#include <QStringLiteral>
#include <QSvgRenderer>

namespace Theme {
namespace {

// ── Active palette + mode + blobs cache (returned by current/currentMode/
//     currentBlobs accessors) ───────────────────────────────────────────────
// Initialized to noir-default values; applyTheme() rewrites all three on
// every call. Defined inside an anonymous namespace so external code goes
// through the accessors. The s_currentBlobs cache eliminates per-paint
// QColor allocation in GlassBackground::paintEvent (was the smoking gun
// for "scroll unsmooth after theme switch" 2026-04-26 ~01:05).

ThemePalette s_current      = {};
bool         s_initialized  = false;
Mode         s_currentMode  = Mode::Dark;
ModeBlobs    s_currentBlobs = {};

// ── Compiled-QSS cache by Mode ──────────────────────────────────────────────
// buildStylesheet() runs 17 full-text scans over an 11.2 KB template per call.
// Caching the compiled output per Mode means cycling back to a previously-
// visited Mode skips the entire substitution pass — only the unavoidable
// Qt polish cascade on setStyleSheet remains. 8 Modes × ~12 KB ≈ 96 KB
// resident memory in the cache (negligible). Cleared via s_qssCache.clear()
// if a future runtime override path needs to invalidate; not exposed yet.

QHash<Mode, QString> s_qssCache;

// ── Dark baseline palette ────────────────────────────────────────────────────
// Byte-for-byte equal to today's hardcoded values in noirStylesheet() +
// applyDarkPalette(). Mode::Dark resolves to this; non-Dark modes start from
// this baseline and override bg0/bg1/accent/accentSoft/accentLine.

ThemePalette darkBaselineNoir()
{
    ThemePalette p;
    p.bg0         = QStringLiteral("#050505");
    p.bg1         = QStringLiteral("#0a0a0a");
    p.text        = QStringLiteral("#eeeeee");
    p.textDim     = QStringLiteral("#e0e0e0");
    p.muted       = QStringLiteral("rgba(238,238,238,0.58)");
    p.border      = QStringLiteral("rgba(255,255,255,0.10)");
    p.borderHover = QStringLiteral("rgba(255,255,255,0.16)");
    p.accent      = QStringLiteral("#c7a76b");
    p.accentSoft  = QStringLiteral("rgba(199,167,107,0.22)");
    p.accentLine  = QStringLiteral("rgba(199,167,107,0.40)");
    p.topbarBg    = QStringLiteral("rgba(8,8,8,0.52)");
    p.sidebarBg   = QStringLiteral("rgba(8,8,8,0.46)");
    p.menuBg      = QStringLiteral("rgba(8,8,8,0.88)");
    p.toastBg     = QStringLiteral("rgba(8,8,8,0.82)");
    p.cardBg      = QStringLiteral("rgba(8,8,8,0.92)");
    p.overlayDim  = QStringLiteral("rgba(0,0,0,180)");
    p.inkRgb      = QStringLiteral("255,255,255");
    return p;
}

// ── Per-mode override layers (P3.2) ─────────────────────────────────────────
// Port the Tankoban-Max `body[data-app-theme="<mode>"]` palette overrides
// from theme-light.css into ThemePalette tokens. Without these overlays each
// named-color mode renders Dark/Noir's chrome literally on top of a slightly
// different bg0 — which is what made Hemanth's smoke flag "Gruvbox looks
// like Dark with a different shade". The overlay re-tints the chrome (text,
// borders, surface alphas) to match the mode's ink-rgb so each named mode
// has its own visual identity matching TM.
//
// Cookbook for porting any TM mode block:
//   --vx-ink                    → text (#hexform full opacity)
//   rgba(ink, 0.92)             → textDim
//   rgba(ink, 0.58)             → muted
//   --lib-border (rgba ink .10) → border
//   --vx-border2 (rgba ink .18) → borderHover
//   rgba(bg-rgb, .65)           → topbarBg
//   rgba(bg-rgb, .55)           → sidebarBg
//   rgba(bg-rgb, .88)           → menuBg
//   rgba(bg-rgb, .82)           → toastBg
//   rgba(bg-rgb, .92)           → cardBg

void gruvboxOverlay(ThemePalette& p)
{
    // TM Gruvbox: warm coffee-brown canvas, cream-yellow ink (#ebdbb2),
    // orange primary accent (#fe8019). Ink-rgb 235,219,178; bg-rgb 40,40,40
    // (raised one bg-stop above #282828 so glass surfaces brighten the canvas
    // rather than darken it — TM convention).
    p.text        = QStringLiteral("#ebdbb2");
    p.textDim     = QStringLiteral("rgba(235,219,178,0.92)");
    p.muted       = QStringLiteral("rgba(235,219,178,0.58)");
    p.border      = QStringLiteral("rgba(235,219,178,0.10)");
    p.borderHover = QStringLiteral("rgba(235,219,178,0.18)");
    p.topbarBg    = QStringLiteral("rgba(40,40,40,0.65)");
    p.sidebarBg   = QStringLiteral("rgba(40,40,40,0.55)");
    p.menuBg      = QStringLiteral("rgba(40,40,40,0.88)");
    p.toastBg     = QStringLiteral("rgba(40,40,40,0.82)");
    p.cardBg      = QStringLiteral("rgba(40,40,40,0.92)");
    // overlayDim (modal dim) stays at Dark baseline rgba(0,0,0,140) — modal
    // overlays are mode-agnostic; consistent dim across all modes.
}

void nordOverlay(ThemePalette& p)
{
    // TM Nord: arctic blue-grey canvas, snow-storm ink (#d8dee9), frost
    // cyan accent (#88c0d0). Ink-rgb 216,222,233; bg-rgb 46,52,64 (raised
    // one bg-stop above #2e3440).
    p.text        = QStringLiteral("#d8dee9");
    p.textDim     = QStringLiteral("rgba(216,222,233,0.92)");
    p.muted       = QStringLiteral("rgba(216,222,233,0.58)");
    p.border      = QStringLiteral("rgba(216,222,233,0.10)");
    p.borderHover = QStringLiteral("rgba(216,222,233,0.18)");
    p.topbarBg    = QStringLiteral("rgba(46,52,64,0.65)");
    p.sidebarBg   = QStringLiteral("rgba(46,52,64,0.55)");
    p.menuBg      = QStringLiteral("rgba(46,52,64,0.88)");
    p.toastBg     = QStringLiteral("rgba(46,52,64,0.82)");
    p.cardBg      = QStringLiteral("rgba(46,52,64,0.92)");
}

void solarizedOverlay(ThemePalette& p)
{
    // TM Solarized: deep teal canvas, base1 ink (#c0cfcf), yellow primary
    // accent (#b58900). Ink-rgb 192,207,207; bg-rgb 0,43,54. Border color
    // diverges from ink — Solarized's spec uses base01 (rgb 131,148,150)
    // for borders / lib-borders specifically; ink is base1 (rgb 192,207,207)
    // for text/surface fills.
    p.text        = QStringLiteral("#c0cfcf");
    p.textDim     = QStringLiteral("rgba(192,207,207,0.94)");
    p.muted       = QStringLiteral("rgba(192,207,207,0.58)");
    p.border      = QStringLiteral("rgba(131,148,150,0.10)");
    p.borderHover = QStringLiteral("rgba(131,148,150,0.18)");
    p.topbarBg    = QStringLiteral("rgba(0,43,54,0.65)");
    p.sidebarBg   = QStringLiteral("rgba(0,43,54,0.55)");
    p.menuBg      = QStringLiteral("rgba(0,43,54,0.88)");
    p.toastBg     = QStringLiteral("rgba(0,43,54,0.82)");
    p.cardBg      = QStringLiteral("rgba(0,43,54,0.92)");
}

void catppuccinOverlay(ThemePalette& p)
{
    // TM Catppuccin (Mocha flavor): deep navy canvas, text ink (#cdd6f4),
    // mauve primary accent (#cba6f7). Ink-rgb 205,214,244; bg-rgb 30,30,46.
    p.text        = QStringLiteral("#cdd6f4");
    p.textDim     = QStringLiteral("rgba(205,214,244,0.92)");
    p.muted       = QStringLiteral("rgba(205,214,244,0.58)");
    p.border      = QStringLiteral("rgba(205,214,244,0.10)");
    p.borderHover = QStringLiteral("rgba(205,214,244,0.18)");
    p.topbarBg    = QStringLiteral("rgba(30,30,46,0.65)");
    p.sidebarBg   = QStringLiteral("rgba(30,30,46,0.55)");
    p.menuBg      = QStringLiteral("rgba(30,30,46,0.88)");
    p.toastBg     = QStringLiteral("rgba(30,30,46,0.82)");
    p.cardBg      = QStringLiteral("rgba(30,30,46,0.92)");
}

// Light-mode overlays were here (LightDawn / LightCream / LightBeige) — removed
// 2026-04-26 ~10:18 per Hemanth "remove the white colours for now, we will
// include them with everything planned out." Cookbook preserved at
// memory/project_theme_p5_light_in_progress.md for the future re-add.

// Look up a mode entry by id. Returns Dark as fallback (defensive).
const ThemeModeEntry& modeEntry(Mode id)
{
    for (const auto& entry : kModes) {
        if (entry.id == id) return entry;
    }
    return kModes[0]; // Dark
}

// Build a QPalette from a ThemePalette. Mirrors today's applyDarkPalette()
// QPalette mapping; just sources the colors from the palette struct now.
QPalette buildQPalette(const ThemePalette& palette)
{
    QPalette pal;
    const QColor bg0  = QColor(palette.bg0);
    const QColor bg1  = QColor(palette.bg1);
    const QColor text = QColor(palette.text);

    pal.setColor(QPalette::Window,          bg0);
    pal.setColor(QPalette::WindowText,      text);
    pal.setColor(QPalette::Base,            bg0);
    pal.setColor(QPalette::AlternateBase,   bg1);
    pal.setColor(QPalette::ToolTipBase,     bg1);
    pal.setColor(QPalette::ToolTipText,     text);
    pal.setColor(QPalette::Text,            text);
    pal.setColor(QPalette::Button,          bg1);
    pal.setColor(QPalette::ButtonText,      text);
    pal.setColor(QPalette::BrightText,      QColor(0xff, 0x44, 0x44));   // accessibility fallback, not theme-bound
    pal.setColor(QPalette::Link,            QColor(palette.accent));

    // Highlight = accent at alpha 0x38 (22%) — matches today's noir behavior.
    QColor hi = QColor(palette.accent);
    hi.setAlpha(0x38);
    pal.setColor(QPalette::Highlight, hi);
    pal.setColor(QPalette::HighlightedText, text);

    pal.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x55, 0x55, 0x55));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x55, 0x55, 0x55));
    return pal;
}

} // anonymous namespace

// ── resolvePalette ──────────────────────────────────────────────────────────
// Compose the active ThemePalette from a Mode.
//   - Dark → byte-equal to current noirStylesheet literals
//   - non-Dark → bg0/bg1/accent/accentSoft/accentLine swap to mode's values;
//     the remaining 13 tokens (text/border/topbarBg/menuBg/etc.) stay at the
//     Dark baseline. Phase 3 fills per-mode override layers.

ThemePalette resolvePalette(Mode mode)
{
    ThemePalette p = darkBaselineNoir();
    const ThemeModeEntry& entry = modeEntry(mode);

    p.bg0    = QString::fromLatin1(entry.bg0);
    p.bg1    = QString::fromLatin1(entry.bg1);
    p.accent = QString::fromLatin1(entry.accent);
    p.inkRgb = QString::fromLatin1(entry.inkRgb);

    const QString rgb = QString::fromLatin1(entry.accentRgb);
    p.accentSoft = QStringLiteral("rgba(%1,0.22)").arg(rgb);
    p.accentLine = QStringLiteral("rgba(%1,0.40)").arg(rgb);

    // Per-mode chrome overlay (P3.2). Without this, the named modes render
    // Dark/Noir chrome on top of a different bg0 — which Hemanth's smoke flagged
    // as "looks like Dark with a different shade" rather than the warm-coffee
    // / arctic / lavender atmospheres TM ships. Each overlay ports the
    // text/muted/border/glass-surface tokens from TM's `body[data-app-theme="..."]`
    // block. Dark passes through (Noir baseline = no overlay).
    switch (mode) {
        case Mode::Gruvbox:    gruvboxOverlay(p);    break;
        case Mode::Nord:       nordOverlay(p);       break;
        case Mode::Solarized:  solarizedOverlay(p);  break;
        case Mode::Catppuccin: catppuccinOverlay(p); break;
        case Mode::Dark:       /* Noir baseline, no overlay */ break;
    }

    return p;
}

// ── buildStylesheet ─────────────────────────────────────────────────────────
// QSS template with __PLACEHOLDER__ tokens. Pure substitution loop produces
// the final stylesheet. For Dark defaults, output is byte-equal to the
// pre-Phase-1 hardcoded noirStylesheet() string in main.cpp.
//
// Note: surface alpha levels (0.05 / 0.06 / 0.10 / 0.12 / 0.16 / 18 / 30 / .78
// etc.) are NOT placeholdered — those are transparency conventions, not
// theme-bound colors. The TRIPLET preceding the alpha is __INK_RGB__ (e.g.
// rgba(__INK_RGB__,0.06) → rgba(255,255,255,0.06) for Dark, rgba(20,20,24,0.06)
// for Light) so the same overlay alpha works for any mode's bg/ink contrast.

QString buildStylesheet(const ThemePalette& palette)
{
    static const QString kTemplate = QStringLiteral(R"qss(
/* ── Base: glass transparency ── */

* {
    font-family: "Segoe UI Variable", "Segoe UI", "Inter", sans-serif;
}

QWidget {
    background: transparent;
    color: __TEXT__;
}

/* ── Top-level window canvas ── */
/* QMainWindow paints its central area with palette-Window. Setting it
   explicitly here guarantees the bg follows the active theme even when
   QPalette::Window isn't picked up (e.g. some Win32 NC-area paint paths). */
QMainWindow {
    background: __BG__;
}

QScrollArea {
    background: transparent;
    border: none;
}

/* ── Brand ── */

QLabel#Brand {
    color: __TEXT__;
    font-size: 14px;
    font-weight: 800;
    letter-spacing: 0.2px;
}

/* ── Topbar: frosted glass strip ── */

QFrame#TopBar {
    min-height: 56px;
    max-height: 56px;
    background: __TOPBAR_BG__;
    border-bottom: 1px solid __BORDER__;
}

QFrame#TopNav {
    background: transparent;
}

/* ── Nav buttons: pill-shaped, glass surface ── */

QPushButton#TopNavButton {
    text-align: left;
    color: rgba(__INK_RGB__,0.78);
    background: rgba(__INK_RGB__,0.06);
    border: 1px solid __BORDER__;
    border-radius: 14px;
    padding: 4px 14px;
    min-height: 22px;
    font-weight: 800;
    font-size: 11px;
}

QPushButton#TopNavButton:hover {
    background: rgba(__INK_RGB__,0.10);
    border-color: __BORDER_HI__;
    color: rgba(__INK_RGB__,0.92);
}

QPushButton#TopNavButton:checked {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 __ACCENT_SOFT__, stop:1 rgba(__INK_RGB__,0.06));
    border-color: __ACCENT_LINE__;
    color: __TEXT__;
    font-weight: 800;
}

/* ── Icon buttons ── */

QPushButton#IconButton {
    text-align: center;
    color: rgba(__INK_RGB__,0.78);
    background: rgba(__INK_RGB__,0.06);
    border: 1px solid __BORDER__;
    border-radius: 12px;
    padding: 0px;
    min-width: 28px;  max-width: 28px;
    min-height: 24px; max-height: 24px;
    font-weight: 700;
    font-size: 11px;
}

QPushButton#IconButton:hover {
    background: rgba(__INK_RGB__,0.10);
    border-color: __BORDER_HI__;
}

QPushButton#IconButton:disabled {
    color: __MUTED__;
    border-color: rgba(__INK_RGB__,0.06);
}

/* ── Content area ── */

QFrame#Content {
    background: transparent;
}

/* ── Labels ── */

QLabel {
    color: __TEXT__;
}

/* ── Sidebar: translucent glass ── */

QFrame#LibrarySidebar {
    min-width: 252px;
    max-width: 252px;
    background: __SIDEBAR_BG__;
    border-right: 1px solid __BORDER__;
    border-radius: 0px;
}

/* ── Sidebar action buttons ── */

QPushButton#SidebarAction {
    text-align: left;
    color: rgba(__INK_RGB__,0.86);
    background: rgba(__INK_RGB__,0.06);
    border: 1px solid __BORDER__;
    border-radius: 10px;
    padding: 6px 10px;
    font-size: 11px;
    font-weight: 600;
}

QPushButton#SidebarAction:hover {
    background: rgba(__INK_RGB__,0.10);
    border-color: __BORDER_HI__;
}

/* ── Tile cards ── */

QFrame#TileCard {
    background: transparent;
    border: none;
}

QFrame#TileImageWrap {
    border: 1px solid __BORDER__;
    background: __BG1__;
    border-radius: 12px;
}

QLabel#TileTitle {
    font-size: 12px;
    font-weight: 700;
    color: __TEXT__;
}

QLabel#TileSubtitle {
    font-size: 11px;
    color: __MUTED__;
}

/* ── Tables / Trees / Lists ── */

QTableWidget, QListWidget, QTreeWidget {
    background: rgba(__INK_RGB__,0.05);
    border: 1px solid rgba(__INK_RGB__,18);
    border-radius: 10px;
    selection-background-color: __ACCENT_SOFT__;
    selection-color: __TEXT__;
    font-size: 11px;
    outline: 0;
}

QHeaderView::section {
    background: rgba(__INK_RGB__,0.06);
    color: __TEXT__;
    border: none;
    border-bottom: 1px solid __BORDER__;
    padding: 5px 8px;
    font-size: 11px;
    font-weight: 800;
}

/* ── Scrollbars: thin-bubble, brighten on hover ── */
/* Tankoban-Max overhaul.css thin-bubble convention: track has breathing room
   around the handle via handle-margin (not scrollbar-padding — Qt QSS ignores
   padding on QScrollBar). Thumb sits as a pill "floating" inside the track,
   with hover/pressed brightening steps for interaction feedback. */

QScrollBar:vertical {
    background: transparent;
    border: none;
    width: 12px;
    margin: 0;
}
QScrollBar:horizontal {
    background: transparent;
    border: none;
    height: 12px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background: rgba(__INK_RGB__,60);
    border-radius: 4px;
    min-height: 32px;
    margin: 2px 3px;
}
QScrollBar::handle:horizontal {
    background: rgba(__INK_RGB__,60);
    border-radius: 4px;
    min-width: 32px;
    margin: 3px 2px;
}

QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
    background: rgba(__INK_RGB__,110);
}

QScrollBar::handle:vertical:pressed, QScrollBar::handle:horizontal:pressed {
    background: rgba(__INK_RGB__,150);
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
    background: none;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
    background: none;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}

/* ── Context menus ── */

QMenu {
    background: __MENU_BG__;
    border: 1px solid rgba(__INK_RGB__,30);
    border-radius: 10px;
    padding: 4px 0px;
}

QMenu::item {
    color: __TEXT__;
    padding: 6px 16px;
    font-size: 11px;
    background: transparent;
}

QMenu::item:selected {
    background: __ACCENT_SOFT__;
}

QMenu::item:disabled {
    color: __MUTED__;
}

QMenu::separator {
    height: 1px;
    background: rgba(__INK_RGB__,18);
    margin: 4px 8px;
}

/* ── Input fields ── */

QLineEdit {
    background: rgba(__INK_RGB__,0.05);
    color: __TEXT__;
    border: 1px solid rgba(__INK_RGB__,18);
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 11px;
    selection-background-color: __ACCENT_SOFT__;
    selection-color: __TEXT__;
}

QLineEdit:focus {
    border-color: __ACCENT_LINE__;
}

QComboBox {
    background: rgba(__INK_RGB__,0.14);
    color: __TEXT__;
    border: 1px solid __BORDER_HI__;
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 11px;
}

QComboBox:hover {
    background: rgba(__INK_RGB__,0.20);
    border-color: __ACCENT_LINE__;
}

QComboBox::drop-down {
    border: none;
    width: 18px;
}

/* ── Library page section headers ── */
/* CONTINUE READING / SERIES / SHOWS / etc. on Comics + Books + Videos pages.
   Was previously inline-styled with rgba(255,255,255,0.55) in the page .cpp
   files; promoted to a theme-bound objectName rule for Light/Dark parity. */

QLabel#LibraryHeading {
    color: __MUTED__;
    font-size: 12px;
    font-weight: bold;
    letter-spacing: 1px;
}

/* ── Density slider min/max labels ── */
/* The "A" labels flanking the tile-size slider on Comics/Books/Videos pages. */

QLabel#DensityLabel {
    color: __MUTED__;
}

QLabel#DensityLabelSmall {
    color: __MUTED__;
    font-size: 10px;
}

QLabel#DensityLabelLarge {
    color: __MUTED__;
    font-size: 16px;
}

/* ── Tile-size slider ── */
/* Sits between the two "A" density labels; sets the cover grid item size.
   Default Qt QSlider styling renders nearly invisible on light bg. */

QSlider::groove:horizontal {
    background: rgba(__INK_RGB__,0.20);
    height: 4px;
    border-radius: 2px;
}

QSlider::sub-page:horizontal {
    background: __ACCENT__;
    height: 4px;
    border-radius: 2px;
}

QSlider::add-page:horizontal {
    background: rgba(__INK_RGB__,0.20);
    height: 4px;
    border-radius: 2px;
}

QSlider::handle:horizontal {
    background: __ACCENT__;
    width: 12px;
    height: 12px;
    margin: -4px 0;
    border-radius: 6px;
}

QSlider::handle:horizontal:hover {
    background: __ACCENT__;
    width: 14px;
    height: 14px;
    margin: -5px 0;
    border-radius: 7px;
}

/* ── Tile-grid view toggle (grid/list mode button) ── */
/* Right of the density slider on Comics page. */

QPushButton#ViewToggle {
    background: rgba(__INK_RGB__,0.12);
    border: 1px solid __BORDER_HI__;
    border-radius: 4px;
    color: __MUTED__;
    font-size: 14px;
}

QPushButton#ViewToggle:hover {
    background: rgba(__INK_RGB__,0.20);
    color: __TEXT__;
    border-color: __ACCENT_LINE__;
}

/* ── Smaller-font sister of LibraryHeading ── */
/* Used by stream search-result section headers (SERIES / MOVIES / etc.)
   where the surrounding tiles are smaller than library-mode tiles. */

QLabel#LibraryHeadingSmall {
    color: __MUTED__;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 1.5px;
    padding: 4px 0 2px 0;
}

/* ── Stream-mode misc theme-bound text ── */

QLabel#StreamStatusText {
    color: __MUTED__;
    font-size: 12px;
}

/* ── Sources-page launcher tiles ── */
/* Tankorent / Tankoyomi / Tankolibrary tiles on the Sources launcher.
   Was inline-styled with rgba(20,20,24,0.7) bg + #888 subtitle hardcodes
   that broke on light modes; promoted to theme-bound rules so the tiles
   inherit the active mode's card surface + text/muted tokens. */

QPushButton#AppTile {
    background: __CARD_BG__;
    border: 1px solid __BORDER__;
    border-radius: 12px;
}

QPushButton#AppTile:hover {
    border-color: __ACCENT__;
}

QLabel#AppTileTitle {
    color: __TEXT__;
    font-size: 18px;
    font-weight: bold;
    background: transparent;
    border: none;
}

QLabel#AppTileSubtitle {
    color: __MUTED__;
    font-size: 12px;
    background: transparent;
    border: none;
}

QComboBox QAbstractItemView {
    background: __MENU_BG__;
    border: 1px solid rgba(__INK_RGB__,30);
    border-radius: 10px;
    selection-background-color: __ACCENT_SOFT__;
    selection-color: __TEXT__;
    outline: 0;
}

/* ── Toast ── */

QLabel#ShellToast {
    background: __TOAST_BG__;
    color: __TEXT_DIM__;
    border-radius: 6px;
    padding: 6px 18px;
    font-size: 13px;
}

/* ── Root folders overlay ── */

QWidget#root_folders_overlay {
    background: __OVERLAY_DIM__;
}

QFrame#RootFoldersCard {
    background: __CARD_BG__;
    border: 1px solid rgba(__INK_RGB__,0.12);
    border-radius: 14px;
}

QLabel#RootFoldersTitle {
    font-size: 14px;
    font-weight: 800;
    color: __TEXT__;
}

QPushButton#RootFoldersCloseBtn {
    background: rgba(__INK_RGB__,0.06);
    border: 1px solid __BORDER__;
    border-radius: 13px;
    color: rgba(__INK_RGB__,0.78);
    font-size: 14px;
    font-weight: bold;
}

QPushButton#RootFoldersCloseBtn:hover {
    background: rgba(__INK_RGB__,0.12);
}

QPushButton#RootFoldersAddBtn {
    background: rgba(__INK_RGB__,0.06);
    border: 1px solid __BORDER__;
    border-radius: 10px;
    color: rgba(__INK_RGB__,0.78);
    font-size: 11px;
    font-weight: 600;
}

QPushButton#RootFoldersAddBtn:hover {
    background: rgba(__INK_RGB__,0.10);
    border-color: __BORDER_HI__;
}

/* ── Tooltips ── */

QToolTip {
    background: __CARD_BG__;
    color: __TEXT__;
    border: 1px solid rgba(__INK_RGB__,0.12);
    border-radius: 6px;
    padding: 4px 8px;
    font-size: 11px;
}
)qss");

    // Substitution table — order doesn't matter, all replacements are
    // independent. Compute once per applyTheme call (negligible cost: ~10KB
    // string × 16 tokens at app boot, well under 1ms).
    QHash<QString, QString> tokens;
    tokens.insert(QStringLiteral("__BG__"),          palette.bg0);
    tokens.insert(QStringLiteral("__BG1__"),         palette.bg1);
    tokens.insert(QStringLiteral("__TEXT__"),        palette.text);
    tokens.insert(QStringLiteral("__TEXT_DIM__"),    palette.textDim);
    tokens.insert(QStringLiteral("__MUTED__"),       palette.muted);
    tokens.insert(QStringLiteral("__BORDER__"),      palette.border);
    tokens.insert(QStringLiteral("__BORDER_HI__"),   palette.borderHover);
    tokens.insert(QStringLiteral("__ACCENT__"),      palette.accent);
    tokens.insert(QStringLiteral("__ACCENT_SOFT__"), palette.accentSoft);
    tokens.insert(QStringLiteral("__ACCENT_LINE__"), palette.accentLine);
    tokens.insert(QStringLiteral("__TOPBAR_BG__"),   palette.topbarBg);
    tokens.insert(QStringLiteral("__SIDEBAR_BG__"),  palette.sidebarBg);
    tokens.insert(QStringLiteral("__MENU_BG__"),     palette.menuBg);
    tokens.insert(QStringLiteral("__TOAST_BG__"),    palette.toastBg);
    tokens.insert(QStringLiteral("__CARD_BG__"),     palette.cardBg);
    tokens.insert(QStringLiteral("__OVERLAY_DIM__"), palette.overlayDim);
    tokens.insert(QStringLiteral("__INK_RGB__"),     palette.inkRgb);

    QString out = kTemplate;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        out.replace(it.key(), it.value());
    }
    return out;
}

// ── applyTheme ──────────────────────────────────────────────────────────────
// Populates the s_current / s_currentMode / s_currentBlobs caches once per
// switch so per-paint surfaces don't pay QSettings + alloc cost.
// Caches the compiled QSS by Mode so cycling back to a previously-visited
// Mode skips the buildStylesheet substitution pass.
// Idempotent fast-skip: if the requested Mode is already active and its QSS
// is in cache, return without re-running setPalette/setStyleSheet (which
// would otherwise trigger a redundant Qt polish cascade across the tree).

void applyTheme(QApplication& app, Mode mode)
{
    if (s_initialized && mode == s_currentMode && s_qssCache.contains(mode)) {
        return;  // No-op switch — same mode + QSS already applied.
    }

    const ThemePalette palette = resolvePalette(mode);
    s_current      = palette;
    s_currentMode  = mode;
    s_currentBlobs = accentBlobsForMode(mode);
    s_initialized  = true;

    app.setPalette(buildQPalette(palette));

    auto it = s_qssCache.find(mode);
    if (it == s_qssCache.end()) {
        it = s_qssCache.insert(mode, buildStylesheet(palette));
    }
    app.setStyleSheet(it.value());
}

void applyThemeFromSettings(QApplication& app)
{
    applyTheme(app, loadMode());
}

const ThemePalette& current()
{
    if (!s_initialized) {
        s_current = resolvePalette(Mode::Dark);
        s_currentMode = Mode::Dark;
        s_currentBlobs = accentBlobsForMode(Mode::Dark);
        s_initialized = true;
    }
    return s_current;
}

Mode currentMode()
{
    if (!s_initialized) {
        // Lazy-init so a per-paint caller before applyTheme() ran (e.g.
        // GlassBackground constructor firing before main.cpp's
        // applyThemeFromSettings) gets the Dark default instead of stale
        // default-constructed state.
        s_current = resolvePalette(Mode::Dark);
        s_currentMode = Mode::Dark;
        s_currentBlobs = accentBlobsForMode(Mode::Dark);
        s_initialized = true;
    }
    return s_currentMode;
}

ModeBlobs currentBlobs()
{
    if (!s_initialized) {
        s_current = resolvePalette(Mode::Dark);
        s_currentMode = Mode::Dark;
        s_currentBlobs = accentBlobsForMode(Mode::Dark);
        s_initialized = true;
    }
    return s_currentBlobs;
}

// ── QSettings persistence ───────────────────────────────────────────────────
// Named QSettings("Tankoban", "Tankoban") matches the codebase convention
// (VideoPlayer.cpp:212, BooksPage.cpp:257, ComicsPage.cpp:282 all use this
// org/app pair). Slash-separated namespace per player/, stream/, tankorent/
// precedent.
//
// theme/preset key (from pre-spec-correction Phase 1/2) is stale — stops being
// read or written. Legacy users with a saved preset get it ignored on next
// launch and land on their Mode's default accent.

Mode loadMode()
{
    QSettings settings(QStringLiteral("Tankoban"), QStringLiteral("Tankoban"));
    return modeFromSlug(settings.value(QStringLiteral("theme/mode"),
                                       QStringLiteral("dark")).toString(),
                        Mode::Dark);
}

void saveMode(Mode m)
{
    QSettings settings(QStringLiteral("Tankoban"), QStringLiteral("Tankoban"));
    settings.setValue(QStringLiteral("theme/mode"), slugFor(m));
}

// ── Slug ↔ enum conversion ──────────────────────────────────────────────────

QString slugFor(Mode m)
{
    for (const auto& entry : kModes) {
        if (entry.id == m) return QString::fromLatin1(entry.slug);
    }
    return QStringLiteral("dark");
}

Mode modeFromSlug(const QString& slug, Mode fallback)
{
    for (const auto& entry : kModes) {
        if (slug.compare(QString::fromLatin1(entry.slug), Qt::CaseInsensitive) == 0) {
            return entry.id;
        }
    }
    return fallback;
}

// ── Per-mode accent-blob colors for GlassBackground ─────────────────────────
// Sourced from Tankoban-Max theme-light.css `body[data-app-theme="<mode>"] .bgFx`
// blocks. Each block sets 3 radial-gradient blobs of mode-specific tint that
// give the canvas its atmospheric warmth (Gruvbox = yellow/orange/red,
// Nord = blue-grey, etc.). GlassBackground reads these per paint and
// overlays them as 3 drift-blobs at fixed positions.
//
// Alpha values: TM uses 14%/12%/8% on a 0-1 scale; Qt's QColor 0-255 alpha
// scale needs them × 255 = 36/30/20 (rounded). Dark/Noir keeps the original
// hardcoded neutral grays so the existing visual is byte-equal.

ModeBlobs accentBlobsForMode(Mode mode)
{
    // Alphas converted from TM .bgFx 0-1 scale to Qt 0-255: ×255 rounded.
    // Each named mode uses its own primary/secondary/hot color triplet.
    switch (mode) {
        case Mode::Gruvbox:
            return {
                QColor(250, 189, 47, 36),   // yellow (top-left)    .14
                QColor(254, 128, 25, 30),   // orange (top-right)   .12
                QColor(251, 73,  52, 20),   // red    (bottom)      .08
            };
        case Mode::Nord:
            return {
                QColor(129, 161, 193, 46),  // frost-blue           .18
                QColor(136, 192, 208, 36),  // frost-cyan           .14
                QColor(180, 142, 173, 26),  // aurora-purple        .10
            };
        case Mode::Solarized:
            return {
                QColor(38,  139, 210, 41),  // blue (vx-accent2)    .16
                QColor(181, 137, 0,   30),  // yellow (vx-accent)   .12
                QColor(203, 75,  22,  20),  // orange (vx-hot)      .08
            };
        case Mode::Catppuccin:
            return {
                QColor(137, 180, 250, 41),  // sky (vx-accent2)     .16
                QColor(203, 166, 247, 30),  // mauve (vx-accent)    .12
                QColor(243, 139, 168, 20),  // pink (vx-hot)        .08
            };
        case Mode::Dark:
        default:
            // Dark/Noir keeps the original neutral grays so the existing
            // visual is byte-equal to pre-P3.2.
            return {
                QColor(140, 140, 140, 20),
                QColor(148, 163, 184, 16),
                QColor(120, 120, 120, 14),
            };
    }
}

// ── tintedSvgIcon ───────────────────────────────────────────────────────────
// Originally a private helper in src/ui/widgets/ThemePicker.cpp — promoted
// here so any caller can re-tint icons against Theme::current().text without
// duplicating the QSvgRenderer + QPainter dance. Used by ThemePicker today;
// future callers in pages/widgets that load hardcoded-stroke SVGs (audio,
// settings, gear, etc.) can adopt this rather than embedding their own
// tinting helper.

QIcon tintedSvgIcon(const QString& path, const QColor& tint, int size)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QIcon();
    QString svg = QString::fromUtf8(f.readAll());
    f.close();
    svg.replace(QStringLiteral("#c6c6c6"), tint.name(QColor::HexRgb), Qt::CaseInsensitive);

    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter);
    return QIcon(pix);
}

} // namespace Theme
