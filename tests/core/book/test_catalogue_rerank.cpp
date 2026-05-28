#include <gtest/gtest.h>

#include "core/book/BookCatalogueAggregator.h"

namespace {
BookCatalogueResult bk(const QString& title, const QString& author) {
    BookCatalogueResult b;
    b.title  = title;
    b.author = author;
    return b;
}
}  // namespace

// FictionDB's flat search isn't relevance-ranked (Puzo's "The Godfather" lands
// at #13). The aggregator re-ranks so the obvious title floats to the top.
TEST(CatalogueRerank, ExactTitleFloatsToTop) {
    QList<BookCatalogueResult> in {
        bk("The Godfather's Revenge", "Mark Winegardner"),
        bk("The Godfather Returns", "Mark Winegardner"),
        bk("The Fairy Godfather", "Someone"),
        bk("The Godfather", "Mario Puzo"),
    };
    const auto out = BookCatalogueAggregator::rerankBooks(QStringLiteral("the godfather"), in);
    ASSERT_FALSE(out.isEmpty());
    EXPECT_EQ(out.first().title.toStdString(), "The Godfather");  // exact title → #1
}

TEST(CatalogueRerank, PrefixBeatsContains) {
    QList<BookCatalogueResult> in {
        bk("A Fairy Godfather Tale", "X"),   // contains "godfather"
        bk("Godfather of Harlem", "Y"),      // prefix "godfather"
    };
    const auto out = BookCatalogueAggregator::rerankBooks(QStringLiteral("godfather"), in);
    ASSERT_GE(out.size(), 2);
    EXPECT_EQ(out.first().title.toStdString(), "Godfather of Harlem");  // prefix outranks contains
}

TEST(CatalogueRerank, AuthorMatchBonus) {
    QList<BookCatalogueResult> in {
        bk("Some Mystery", "Jane Doe"),
        bk("Some Mystery", "Mario Puzo"),    // author matches the query
    };
    const auto out = BookCatalogueAggregator::rerankBooks(QStringLiteral("puzo"), in);
    ASSERT_GE(out.size(), 2);
    EXPECT_EQ(out.first().author.toStdString(), "Mario Puzo");  // author-contains bonus floats it up
}
