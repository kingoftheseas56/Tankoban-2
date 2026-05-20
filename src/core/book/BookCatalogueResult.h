#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>

// Catalogue-side search result for Books mode (Open Library + Google Books).
// Parallel to BookResult under src/core/book/ but represents the metadata
// catalogue layer, not the source layer. A BookCatalogueResult flows from
// the catalogue aggregator into the search-takeover view; the user clicks
// it to land on a detail page; the detail page's [Search for downloads]
// fans out to the source layer (which returns BookResult rows in the picker).
//
// Series-shape (isSeries=true) -> opens BooksTankoLibrarySeriesDetailView
//   with the book list table populated by sibling catalogue results.
// Movie-shape (isSeries=false) -> opens BooksTankoLibraryDetailView
//   with a single [Search for downloads] action.
struct BookCatalogueResult {
    // Identity
    QString catalogueId;        // "openlib:OL27448W" | "googlebooks:abc123"
    QString isbn;               // when known (multi-ISBN joined with ',')
    QString workId;             // Open Library work key (OL...W), groups editions

    // Display
    QString title;
    QString author;             // multi-author joined with " & "
    QString publisher;
    QString year;
    QString language;
    QString description;        // synopsis (may be HTML in Google Books; plain in OL)
    QStringList genres;         // Open Library subjects / Google Books categories
    QString coverUrl;           // absolute URL; remote, lazy-fetched

    // Series shape
    bool    isSeries = false;
    QString seriesId;           // catalogueId of the SERIES record (== self.catalogueId
                                //   for a series tile, or the parent series for a book)
    QString seriesName;
    int     seriesPosition = 0; // 1-indexed; 0 if standalone or unknown
    int     seriesTotal = 0;    // when known; 0 if unknown

    // Physical (when known from catalogue side)
    QString pages;              // string — sometimes "pp." suffix in source data
};
Q_DECLARE_METATYPE(BookCatalogueResult)
Q_DECLARE_METATYPE(QList<BookCatalogueResult>)
