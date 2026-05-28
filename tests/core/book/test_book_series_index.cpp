#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "core/book/BookSeriesIndex.h"

namespace {
SeriesIndexEntry mk(const QString& name, const QString& author) {
    SeriesIndexEntry e;
    e.seriesId   = name.toLower().replace(QLatin1Char(' '), QLatin1Char('-')) + QStringLiteral("~1");
    e.seriesName = name;
    e.author     = author;
    return e;
}
}  // namespace

TEST(BookSeriesIndex, RanksExactBeforePrefixBeforeContains) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    BookSeriesIndex idx(dir.path());
    // "Stormlight Shorts" starts with the query (prefix, 200);
    // "The Stormlight Archive" only contains it (contains, 100).
    idx.setEntries({ mk("The Stormlight Archive", "Brandon Sanderson"),
                     mk("Stormlight Shorts", "Someone Else"),
                     mk("Unrelated Title", "Other Author") }, 1);

    const auto r = idx.query(QStringLiteral("stormlight"), 10);
    ASSERT_GE(r.size(), 2);

    int idxPrefix = -1, idxContains = -1;
    for (int i = 0; i < r.size(); ++i) {
        if (r[i].seriesName == "Stormlight Shorts")      idxPrefix = i;
        if (r[i].seriesName == "The Stormlight Archive") idxContains = i;
    }
    ASSERT_GE(idxPrefix, 0);
    ASSERT_GE(idxContains, 0);
    EXPECT_LT(idxPrefix, idxContains);  // prefix outranks contains
}

TEST(BookSeriesIndex, ExactNameRanksFirst) {
    QTemporaryDir dir;
    BookSeriesIndex idx(dir.path());
    idx.setEntries({ mk("Dune Chronicles", "Frank Herbert"),
                     mk("Dune", "Frank Herbert") }, 1);
    const auto r = idx.query(QStringLiteral("dune"), 10);
    ASSERT_GE(r.size(), 2);
    EXPECT_EQ(r.first().seriesName.toStdString(), "Dune");  // exact match floats to #1
}

TEST(BookSeriesIndex, RoundTripsThroughJson) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    {
        BookSeriesIndex a(dir.path());
        a.setEntries({ mk("Dune Chronicles", "Frank Herbert") }, 42);
        a.save();
    }
    BookSeriesIndex b(dir.path());
    b.load(QString());  // empty bundled path → loads the data-dir copy
    ASSERT_EQ(b.size(), 1);
    EXPECT_EQ(b.builtAt(), 42);
    EXPECT_EQ(b.query(QStringLiteral("dune"), 5).size(), 1);
}

TEST(BookSeriesIndex, EmptyQueryReturnsNothing) {
    QTemporaryDir dir;
    BookSeriesIndex idx(dir.path());
    idx.setEntries({ mk("Dune Chronicles", "Frank Herbert") }, 1);
    EXPECT_TRUE(idx.query(QStringLiteral("   "), 10).isEmpty());
}
