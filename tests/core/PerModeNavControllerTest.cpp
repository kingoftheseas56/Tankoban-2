// tests/core/PerModeNavControllerTest.cpp
#include "ui/PerModeNavController.h"
#include "ui/LayerEntry.h"

#include <QSignalSpy>
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

TEST(PerModeNavController, GoBackEmitsRestoreForBehindEntry) {
    PerModeNavController c;
    c.setActiveMode("comics");
    LayerEntry root{"comics", "library", "Library", {}};
    LayerEntry deeper{"comics", "seriesView", "Death Note", {}};
    c.pushLayer("comics", root);
    c.pushLayer("comics", deeper);

    QSignalSpy spy(&c, &PerModeNavController::layerRestoreRequested);
    c.goBack("comics");
    ASSERT_EQ(spy.count(), 1);
    const auto args = spy.takeFirst();
    const LayerEntry restored = args.at(0).value<LayerEntry>();
    EXPECT_EQ(restored.kind,  QString("library"));
    EXPECT_EQ(restored.label, QString("Library"));
}

TEST(PerModeNavController, GoBackNoOpWhenStackTooShort) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    QSignalSpy spy(&c, &PerModeNavController::layerRestoreRequested);
    c.goBack("comics");
    EXPECT_EQ(spy.count(), 0);  // can't go back from single-entry stack
}

TEST(PerModeNavController, GoBackRemovesTopAndUpdatesAvailability) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.goBack("comics");
    EXPECT_FALSE(c.canGoBack("comics"));  // only library remains, can't go back further
}

TEST(PerModeNavController, ResetModeClearsThatModeOnly) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.pushLayer("stream", {"stream", "browse",  "Home", {}});
    c.pushLayer("stream", {"stream", "detail",  "Daredevil", {}});

    c.resetMode("comics");
    EXPECT_FALSE(c.canGoBack("comics"));     // wiped
    EXPECT_TRUE(c.canGoBack("stream"));      // untouched
}

TEST(PerModeNavController, SetActiveModeEmitsAvailabilityForNewMode) {
    PerModeNavController c;
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});

    QSignalSpy spy(&c, &PerModeNavController::backAvailableChanged);
    c.setActiveMode("comics");  // comics has 2 entries -> back is available
    ASSERT_GE(spy.count(), 1);
    EXPECT_EQ(spy.takeLast().at(0).toBool(), true);
}

TEST(PerModeNavController, SetActiveModeToFreshModeDisablesBack) {
    PerModeNavController c;
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.setActiveMode("comics");

    QSignalSpy spy(&c, &PerModeNavController::backAvailableChanged);
    c.setActiveMode("books");  // books has no entries -> back disabled
    ASSERT_GE(spy.count(), 1);
    EXPECT_EQ(spy.takeLast().at(0).toBool(), false);
}
