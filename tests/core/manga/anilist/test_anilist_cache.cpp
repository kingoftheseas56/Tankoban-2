// tests/core/manga/anilist/test_anilist_cache.cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include "core/manga/anilist/AniListCache.h"
#include "core/manga/anilist/AniListTypes.h"

using namespace tankoban::manga::anilist;

TEST(AniListCache, RoundTripsStaffTagsAndCountryOfOrigin)
{
    int argc = 0;
    static QCoreApplication app(argc, nullptr);  // QSettings needs a QCoreApplication

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    MediaDetail detail;
    detail.preview.anilistId    = 30021;
    detail.preview.title        = QStringLiteral("Death Note");
    detail.preview.countryOfOrigin = QStringLiteral("JP");
    detail.preview.staff.append({ QStringLiteral("Tsugumi Ohba"),  QStringLiteral("Story") });
    detail.preview.staff.append({ QStringLiteral("Takeshi Obata"), QStringLiteral("Art") });
    detail.preview.tags.append({ QStringLiteral("Detective"),     95, false });
    detail.preview.tags.append({ QStringLiteral("Psychological"), 93, false });
    detail.preview.tags.append({ QStringLiteral("Anti-Hero"),     82, true  });
    detail.totalChapters = 108;
    detail.totalVolumes  = 12;

    {
        AniListCache cache(tmp.path());
        cache.put(detail);
    }  // cache flush on destruction

    AniListCache cache2(tmp.path());
    const auto loaded = cache2.get(30021);
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->preview.countryOfOrigin, QStringLiteral("JP"));
    ASSERT_EQ(loaded->preview.staff.size(), 2);
    EXPECT_EQ(loaded->preview.staff.at(0).name, QStringLiteral("Tsugumi Ohba"));
    EXPECT_EQ(loaded->preview.staff.at(1).role, QStringLiteral("Art"));
    ASSERT_EQ(loaded->preview.tags.size(), 3);
    EXPECT_EQ(loaded->preview.tags.at(0).rank, 95);
    EXPECT_TRUE(loaded->preview.tags.at(2).isSpoiler);
}
