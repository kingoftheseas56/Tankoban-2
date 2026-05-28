#pragma once

#include <QList>
#include <QString>

#include "BookCatalogueResult.h"

// Wire-type for the catalogue series/standalone split surfaced to the
// storefront (aggregator → BookCatalogueSearchWidget).
//
// Heuristic series detection retired 2026-05-28 (BOOKS_FICTIONDB_CATALOGUE):
// series membership now comes from FictionDB's explicit series index, not
// title-pattern/Roman-numeral guessing. The SeriesGroup struct is preserved as
// the established aggregator→storefront wire type; the old detect()/title-parse
// heuristics + SeriesDetector.cpp were removed.
class SeriesDetector
{
public:
    struct SeriesGroup {
        QString seriesName;
        QString author;
        QList<BookCatalogueResult> books;   // sorted by seriesPosition asc
    };
};
