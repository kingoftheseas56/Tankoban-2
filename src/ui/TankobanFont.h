#pragma once

#include <QFont>

// Typography helpers paired with Theme.h. Tankoban-Max sizes + weights ported
// from styles.css + overhaul.css. Returns QFont you can apply via setFont().
//
// Font family is already set at QApplication scope in noirStylesheet() to
// "Segoe UI Variable, Segoe UI, Inter, sans-serif" — these helpers only carry
// size + weight. Existing setStyleSheet calls with inline font-size/font-weight
// migrate in Phase 2+.

namespace TankobanFont {

inline QFont body()
{
    QFont f;
    f.setPixelSize(11);
    return f;
}

inline QFont meta()
{
    QFont f;
    f.setPixelSize(12);
    return f;
}

inline QFont tileTitle()
{
    QFont f;
    f.setPixelSize(12);
    f.setWeight(QFont::Bold); // 700 — matches current QLabel#TileTitle inline QSS
    return f;
}

inline QFont tileMeta()
{
    QFont f;
    f.setPixelSize(11);
    return f;
}

inline QFont panelTitle()
{
    QFont f;
    f.setPixelSize(18);
    f.setWeight(QFont::ExtraBold); // 800
    return f;
}

inline QFont topbarTitle()
{
    QFont f;
    f.setPixelSize(14);
    f.setWeight(QFont::Black); // 900
    return f;
}

inline QFont sectionHeader()
{
    QFont f;
    f.setPixelSize(11);
    f.setWeight(QFont::ExtraBold); // 800 uppercase — call site adds letter-spacing
    return f;
}

} // namespace TankobanFont
