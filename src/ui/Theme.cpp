#include "Theme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QSettings>
#include <QString>
#include <QStringLiteral>

namespace Theme {
namespace {

// ── Active palette cache (returned by current()) ─────────────────────────────
// Initialized to noir-default values; applyTheme() rewrites this on every call.
// Defined inside an anonymous namespace so external code goes through current().

ThemePalette s_current = {};
bool         s_initialized = false;

// ── Dark baseline palette ────────────────────────────────────────────────────
// Byte-for-byte equal to today's hardcoded values in noirStylesheet() +
// applyDarkPalette(). For Mode::Dark + Preset::Noir the resolved palette MUST
// equal this; that's the "P1 ships zero visual change" guarantee.

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
    return p;
}

// Look up a preset entry by id. Returns Noir as fallback (defensive).
const ThemePresetEntry& presetEntry(Preset id)
{
    for (const auto& entry : kPresets) {
        if (entry.id == id) return entry;
    }
    return kPresets[0]; // Noir
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
// Compose the active ThemePalette from a (mode, preset) pair.
//   - Dark + Noir → byte-equal to current noirStylesheet literals
//   - Dark + non-Noir → bg0/bg1/accent/accentSoft/accentLine swap to preset
//   - Light + any → returns Dark baseline for now (P3 fills Light overrides)

ThemePalette resolvePalette(Mode mode, Preset preset)
{
    ThemePalette p = darkBaselineNoir();
    const ThemePresetEntry& entry = presetEntry(preset);

    // Override bg0/bg1/accent from the picked preset.
    p.bg0    = QString::fromLatin1(entry.bg0);
    p.bg1    = QString::fromLatin1(entry.bg1);
    p.accent = QString::fromLatin1(entry.accent);

    // Derive accentSoft + accentLine from the preset's accent rgb triplet.
    const QString rgb = QString::fromLatin1(entry.accentRgb);  // "199,167,107"
    p.accentSoft = QStringLiteral("rgba(%1,0.22)").arg(rgb);
    p.accentLine = QStringLiteral("rgba(%1,0.40)").arg(rgb);

    if (mode == Mode::Light) {
        // P3 deliverable — fills the 55-effect light override layer here.
        // For P1 we return Dark baseline; toggling to Light is a no-op until P3.
    }

    return p;
}

// ── buildStylesheet ─────────────────────────────────────────────────────────
// QSS template with __PLACEHOLDER__ tokens. Pure substitution loop produces
// the final stylesheet. For Dark + Noir defaults, output is byte-equal to the
// pre-Phase-1 hardcoded noirStylesheet() string in main.cpp.
//
// Note: surface alpha levels (0.05 / 0.06 / 0.10 / 0.12 / 0.16 / 18 / 30 / .78
// etc.) are NOT placeholdered — those are border/transparency conventions, not
// theme-bound colors. They stay literal across all presets.

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
    color: rgba(255,255,255,0.78);
    background: rgba(255,255,255,0.06);
    border: 1px solid __BORDER__;
    border-radius: 14px;
    padding: 4px 14px;
    min-height: 22px;
    font-weight: 800;
    font-size: 11px;
}

QPushButton#TopNavButton:hover {
    background: rgba(255,255,255,0.10);
    border-color: __BORDER_HI__;
    color: rgba(255,255,255,0.92);
}

QPushButton#TopNavButton:checked {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 __ACCENT_SOFT__, stop:1 rgba(255,255,255,0.06));
    border-color: __ACCENT_LINE__;
    color: __TEXT__;
    font-weight: 800;
}

/* ── Icon buttons ── */

QPushButton#IconButton {
    text-align: center;
    color: rgba(255,255,255,0.78);
    background: rgba(255,255,255,0.06);
    border: 1px solid __BORDER__;
    border-radius: 12px;
    padding: 0px;
    min-width: 28px;  max-width: 28px;
    min-height: 24px; max-height: 24px;
    font-weight: 700;
    font-size: 11px;
}

QPushButton#IconButton:hover {
    background: rgba(255,255,255,0.10);
    border-color: __BORDER_HI__;
}

QPushButton#IconButton:disabled {
    color: __MUTED__;
    border-color: rgba(255,255,255,0.06);
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

/* ── Section titles ── */

QLabel#SectionTitle {
    color: __TEXT__;
    font-size: 16px;
    font-weight: 800;
    letter-spacing: 0.2px;
}

/* ── Sidebar action buttons ── */

QPushButton#SidebarAction {
    text-align: left;
    color: rgba(255,255,255,0.86);
    background: rgba(255,255,255,0.06);
    border: 1px solid __BORDER__;
    border-radius: 10px;
    padding: 6px 10px;
    font-size: 11px;
    font-weight: 600;
}

QPushButton#SidebarAction:hover {
    background: rgba(255,255,255,0.10);
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
    background: rgba(255,255,255,0.05);
    border: 1px solid rgba(255,255,255,18);
    border-radius: 10px;
    selection-background-color: __ACCENT_SOFT__;
    selection-color: __TEXT__;
    font-size: 11px;
    outline: 0;
}

QHeaderView::section {
    background: rgba(255,255,255,0.06);
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
    background: rgba(255,255,255,60);
    border-radius: 4px;
    min-height: 32px;
    margin: 2px 3px;
}
QScrollBar::handle:horizontal {
    background: rgba(255,255,255,60);
    border-radius: 4px;
    min-width: 32px;
    margin: 3px 2px;
}

QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
    background: rgba(255,255,255,110);
}

QScrollBar::handle:vertical:pressed, QScrollBar::handle:horizontal:pressed {
    background: rgba(255,255,255,150);
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
    border: 1px solid rgba(255,255,255,30);
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
    background: rgba(255,255,255,18);
    margin: 4px 8px;
}

/* ── Input fields ── */

QLineEdit {
    background: rgba(255,255,255,0.05);
    color: __TEXT__;
    border: 1px solid rgba(255,255,255,18);
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 11px;
    selection-background-color: __ACCENT_SOFT__;
    selection-color: __TEXT__;
}

QLineEdit:focus {
    border-color: rgba(199,167,107,0.45);
}

QComboBox {
    background: rgba(255,255,255,0.05);
    color: __TEXT__;
    border: 1px solid rgba(255,255,255,18);
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 11px;
}

QComboBox::drop-down {
    border: none;
    width: 18px;
}

QComboBox QAbstractItemView {
    background: __MENU_BG__;
    border: 1px solid rgba(255,255,255,30);
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
    border: 1px solid rgba(255,255,255,0.12);
    border-radius: 14px;
}

QLabel#RootFoldersTitle {
    font-size: 14px;
    font-weight: 800;
    color: __TEXT__;
}

QPushButton#RootFoldersCloseBtn {
    background: rgba(255,255,255,0.06);
    border: 1px solid __BORDER__;
    border-radius: 13px;
    color: rgba(255,255,255,0.78);
    font-size: 14px;
    font-weight: bold;
}

QPushButton#RootFoldersCloseBtn:hover {
    background: rgba(255,255,255,0.12);
}

QPushButton#RootFoldersAddBtn {
    background: rgba(255,255,255,0.06);
    border: 1px solid __BORDER__;
    border-radius: 10px;
    color: rgba(255,255,255,0.78);
    font-size: 11px;
    font-weight: 600;
}

QPushButton#RootFoldersAddBtn:hover {
    background: rgba(255,255,255,0.10);
    border-color: __BORDER_HI__;
}

/* ── Tooltips ── */

QToolTip {
    background: __CARD_BG__;
    color: __TEXT__;
    border: 1px solid rgba(255,255,255,0.12);
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

    QString out = kTemplate;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        out.replace(it.key(), it.value());
    }
    return out;
}

// ── applyTheme ──────────────────────────────────────────────────────────────

void applyTheme(QApplication& app, Mode mode, Preset preset)
{
    const ThemePalette palette = resolvePalette(mode, preset);
    s_current = palette;
    s_initialized = true;

    app.setPalette(buildQPalette(palette));
    app.setStyleSheet(buildStylesheet(palette));
}

void applyThemeFromSettings(QApplication& app)
{
    applyTheme(app, loadMode(), loadPreset());
}

const ThemePalette& current()
{
    if (!s_initialized) {
        s_current = resolvePalette(Mode::Dark, Preset::Noir);
        s_initialized = true;
    }
    return s_current;
}

// ── QSettings persistence ───────────────────────────────────────────────────
// Named QSettings("Tankoban", "Tankoban") matches the codebase convention
// (VideoPlayer.cpp:212, BooksPage.cpp:257, ComicsPage.cpp:282 all use this
// org/app pair). Slash-separated namespace per player/, stream/, tankorent/
// precedent. Two SEPARATE keys (mode + preset) — fixes Tankoban-Max's
// localStorage.appTheme shared-key boot-reset bug.

Mode loadMode()
{
    QSettings settings(QStringLiteral("Tankoban"), QStringLiteral("Tankoban"));
    return modeFromSlug(settings.value(QStringLiteral("theme/mode"),
                                       QStringLiteral("dark")).toString(),
                        Mode::Dark);
}

Preset loadPreset()
{
    QSettings settings(QStringLiteral("Tankoban"), QStringLiteral("Tankoban"));
    return presetFromSlug(settings.value(QStringLiteral("theme/preset"),
                                         QStringLiteral("noir")).toString(),
                          Preset::Noir);
}

void saveMode(Mode m)
{
    QSettings settings(QStringLiteral("Tankoban"), QStringLiteral("Tankoban"));
    settings.setValue(QStringLiteral("theme/mode"), slugFor(m));
}

void savePreset(Preset p)
{
    QSettings settings(QStringLiteral("Tankoban"), QStringLiteral("Tankoban"));
    settings.setValue(QStringLiteral("theme/preset"), slugFor(p));
}

// ── Slug ↔ enum conversion ──────────────────────────────────────────────────

QString slugFor(Preset p)
{
    return QString::fromLatin1(presetEntry(p).slug);
}

QString slugFor(Mode m)
{
    return (m == Mode::Light) ? QStringLiteral("light") : QStringLiteral("dark");
}

Preset presetFromSlug(const QString& slug, Preset fallback)
{
    for (const auto& entry : kPresets) {
        if (slug.compare(QString::fromLatin1(entry.slug), Qt::CaseInsensitive) == 0) {
            return entry.id;
        }
    }
    return fallback;
}

Mode modeFromSlug(const QString& slug, Mode fallback)
{
    if (slug.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) return Mode::Light;
    if (slug.compare(QStringLiteral("dark"),  Qt::CaseInsensitive) == 0) return Mode::Dark;
    return fallback;
}

} // namespace Theme
