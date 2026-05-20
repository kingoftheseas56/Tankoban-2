#pragma once

#include <QList>
#include <QString>

#include "BookCatalogueResult.h"

// Series-shape detection for catalogue results.
//
// Given a list of BookCatalogueResult (typically the merged output of
// OpenLibraryClient + GoogleBooksClient parsers), group them into series
// when a clear series-shape signal exists. Otherwise leave them as
// standalones — the safer fallback per spec §3.2 (a wrong-singleton is
// recoverable; a wrong-grouping is jarring).
//
// Signals used, in priority order:
//   1. Open Library / GoogleBooks `seriesName` field (when populated)
//      AND at least one sibling under the same (author, seriesName).
//   2. Title pattern: extract base + position from
//        "<base> #N", "<base>, Book N", "<base>: ...", "<base> N",
//        "<base> (N)", "<base> I/II/III/IV/V/VI/VII/VIII/IX/X"
//      AND at least one sibling under the same (author, base) extracted.
//
// All grouping is author-bound — different authors with similar title
// patterns do NOT merge into one series.
//
// Single books that match a pattern alone (no siblings) stay standalone.
class SeriesDetector
{
public:
    struct SeriesGroup {
        QString seriesName;
        QString author;
        QList<BookCatalogueResult> books;   // sorted by seriesPosition asc
    };

    struct DetectionResult {
        QList<SeriesGroup> seriesGroups;
        QList<BookCatalogueResult> standalones;
    };

    // Pure function: takes a flat list, returns the partition.
    // Mutates each grouped book's isSeries / seriesName / seriesPosition fields.
    static DetectionResult detect(const QList<BookCatalogueResult>& flatResults);

    // ── Exposed for unit-testing the title parsing primitive ─────────────
    struct TitleParse {
        bool matched = false;
        QString base;       // "The Stormlight Archive"
        int     position = 0; // 1-indexed; 0 if no positional signal
    };
    static TitleParse parseSeriesTitlePattern(const QString& title);

    // Roman numeral 1-10 → int; returns 0 on no match.
    static int romanToInt(const QString& roman);
};
