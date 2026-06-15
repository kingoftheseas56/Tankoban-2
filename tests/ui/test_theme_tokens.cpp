// Harbor redesign Phase 1 Task 1 — design-token ladder unit test.
//
// Pure-logic verification of the Harbor dark elevation ladder + gold accent +
// radius scale established in src/ui/Theme.{h,cpp}. Dep-free of libtorrent
// (links only src/ui/Theme.cpp + Qt Core/Gui), so it runs in the standard
// tankoban_tests dep-free cluster.
//
// NOTE: the namespace is `Theme::` (not `tankoban::ui::Theme::` as the plan
// snippet wrote) — Theme.h declares `namespace Theme { ... }`. The assertions
// are identical to the plan; only the namespace path is corrected to match the
// real code.

#include <gtest/gtest.h>

#include "ui/Theme.h"

TEST(HarborTokens, DarkLadderAscendsAndAccentIsGold) {
    const Theme::ThemePalette p = Theme::resolvePalette(Theme::Mode::Dark);
    EXPECT_EQ(p.accent.toLower(), QStringLiteral("#e8b923"));   // gold, not #c7a76b
    EXPECT_EQ(p.onAccent.toLower(), QStringLiteral("#14110a"));
    EXPECT_EQ(p.error.toLower(), QStringLiteral("#e50914"));
    // surface ladder strictly distinct from canvas + each rung distinct
    EXPECT_NE(p.surface, p.bg0);
    EXPECT_NE(p.elevated, p.surface);
    EXPECT_NE(p.raised, p.elevated);
}

TEST(HarborTokens, RadiusLadderExists) {
    EXPECT_EQ(Theme::kRadXs, 6);
    EXPECT_EQ(Theme::kRadMd, 14);
    EXPECT_EQ(Theme::kRadPill, 999);
}
