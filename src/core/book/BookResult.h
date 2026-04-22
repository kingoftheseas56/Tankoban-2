#pragma once

#include <QString>
#include <QList>
#include <QMetaType>

// Result row returned by BookScraper implementations. Parallel to MangaResult
// under src/core/manga/ but carries book-specific metadata (publisher, year,
// pages, language, ISBN, format, size, md5) that manga doesn't need.
//
// Fields left empty (QString{} / 0) when the source doesn't surface them in
// search results — many are detail-page-only. AnnaArchiveScraper surfaces the
// common case (title/author/format/size/year/language/md5) directly from
// search; LibGenScraper's JSON API returns most fields from the list endpoint.
struct BookResult {
    // Identity
    QString source;       // "annas-archive" | "libgen"
    QString sourceId;     // source-native identifier (AA md5, LibGen id)
    QString md5;          // content hash — primary cross-source dedup key

    // Display
    QString title;
    QString author;       // single string; multi-author joined with " & "
    QString publisher;
    QString year;         // string (not int) — AA returns year+month+edition combos
    QString description;  // detail-page only in most sources

    // Categorization
    QString language;     // "English", "Spanish", ISO code varies per source
    QString format;       // "epub" | "pdf" | "mobi" | "azw3" | "djvu" | ...
    QString isbn;         // may be comma-separated list if multiple

    // Physical
    QString pages;        // string — sometimes includes "pp." suffix from source
    QString fileSize;     // human-readable as-returned ("3.2 MB") — display-only

    // Presentation
    QString coverUrl;     // absolute URL; may be empty

    // Access / download state (filled during M2 / detail-fetch flow)
    QString detailUrl;    // URL to the source's detail page
    QString downloadUrl;  // resolved direct URL — only set by resolveDownload()
};
Q_DECLARE_METATYPE(BookResult)
Q_DECLARE_METATYPE(QList<BookResult>)

// Maps a scraper source key to its user-facing display name.
// Mirror of mangaSourceDisplayName() pattern.
inline QString bookSourceDisplayName(const QString& key)
{
    if (key == QLatin1String("annas-archive")) return QStringLiteral("Anna's Archive");
    if (key == QLatin1String("libgen"))        return QStringLiteral("LibGen");
    return key;
}
