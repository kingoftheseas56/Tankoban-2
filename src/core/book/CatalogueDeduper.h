#pragma once

#include <QList>

#include "BookCatalogueResult.h"

// Cross-source dedup for catalogue results. Merges two source lists
// (OpenLibrary + GoogleBooks) into a single ordered list with duplicates
// removed and missing fields cross-filled.
//
// Dedup signals, in priority order:
//   1. Any shared ISBN between the two records (most reliable).
//   2. Fuzzy title + author equality (normalized: lowercased, punctuation-stripped).
//
// Winner policy: OpenLibrary wins when both have the same book (primary source).
// Missing fields on the winner get filled from the loser (description, coverUrl, etc.).
class CatalogueDeduper
{
public:
    static QList<BookCatalogueResult> merge(
        const QList<BookCatalogueResult>& openlib,
        const QList<BookCatalogueResult>& googlebooks);

    // Exposed for unit-testing: normalize a string for fuzzy compare.
    static QString normalize(const QString& s);
};
