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
