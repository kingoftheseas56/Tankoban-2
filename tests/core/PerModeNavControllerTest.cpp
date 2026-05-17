// tests/core/PerModeNavControllerTest.cpp
#include "ui/PerModeNavController.h"
#include "ui/LayerEntry.h"

#include <gtest/gtest.h>

using namespace tankoban::ui;

TEST(PerModeNavController, EmptyControllerCannotGoBack) {
    PerModeNavController c;
    EXPECT_FALSE(c.canGoBack("comics"));
    EXPECT_FALSE(c.canGoBack("stream"));
    EXPECT_EQ(c.peekBack("comics").pageId, QString());
}

TEST(PerModeNavController, SettingActiveModeIsIndependentOfStacks) {
    PerModeNavController c;
    c.setActiveMode("comics");
    EXPECT_EQ(c.activeMode(), QString("comics"));
    EXPECT_FALSE(c.canGoBack("comics"));
}

TEST(PerModeNavController, PushOneLayerNoBackYet) {
    PerModeNavController c;
    c.setActiveMode("comics");
    LayerEntry e{"comics", "library", "Library", {}};
    c.pushLayer("comics", e);
    EXPECT_FALSE(c.canGoBack("comics"));  // need 2 entries (current + behind) for back to be possible
}

TEST(PerModeNavController, PushTwoLayersCanGoBack) {
    PerModeNavController c;
    c.setActiveMode("comics");
    LayerEntry root{"comics", "library", "Library", {}};
    LayerEntry deeper{"comics", "seriesView", "Death Note", {}};
    c.pushLayer("comics", root);
    c.pushLayer("comics", deeper);
    EXPECT_TRUE(c.canGoBack("comics"));
    EXPECT_EQ(c.peekBack("comics").label, QString("Library"));
}

TEST(PerModeNavController, PerModeIsolation) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.pushLayer("stream",  {"stream", "browse",  "Home", {}});
    EXPECT_TRUE(c.canGoBack("comics"));    // 2 deep
    EXPECT_FALSE(c.canGoBack("stream"));   // only 1 entry
    EXPECT_FALSE(c.canGoBack("books"));    // never pushed
}

TEST(PerModeNavController, PopRemovesTop) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library",   "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView","DN",      {}});
    c.popLayer("comics");
    EXPECT_FALSE(c.canGoBack("comics"));   // only library left
}
