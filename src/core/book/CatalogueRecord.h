#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaType>

// The v1 library entity for Books mode after BOOKS_STREMIO_PIVOT.
// Replaces the folder-scan-driven model (where on-disk files defined the
// library) with a catalogue-record-driven model: a book is in the library
// iff a CatalogueRecord wraps it. See spec §3.8 (burn the ships).
//
// A series of N books = N CatalogueRecords sharing the same seriesId.
// The library grid renders the series tile from the most-progressed or
// most-recently-touched record in the group.
struct CatalogueRecord {
    // Identity
    QString catalogueId;        // "openlib:OL27448W" | "googlebooks:abc123" | for-series-books: "openlib:OL27448W:3" (work + position)
    QString isbn;               // when known (multi-ISBN joined with ',')
    QString md5;                // BookResult.md5 of the downloaded file; cross-source dedup

    // Display metadata (from catalogue layer)
    QString title;
    QString author;
    QString publisher;
    QString year;
    QString language;
    QString description;        // synopsis
    QStringList genres;         // Open Library subjects
    QString coverUrl;           // remote (lazy-fetched into cache)
    QString cachedCoverPath;    // local cached cover for offline render

    // Series (empty seriesId means movie-shape standalone)
    QString seriesId;
    QString seriesName;
    int     seriesPosition = 0; // 1-indexed; 0 if standalone or unknown
    int     seriesTotal = 0;

    // File (from source layer + downloader)
    QString filePath;           // canonical relative path under Books root
    QString format;             // "epub" | "pdf" | "mobi" | "azw3" | "djvu"
    QString fileSize;           // human-readable display

    // State (from app runtime)
    qint64  addedAt = 0;        // epoch seconds; 0 == not added
    double  readProgress = 0.0; // 0.0..1.0
    qint64  lastReadAt = 0;     // epoch seconds; 0 == never opened
    QString lastReadCfi;        // EPUB CFI position for resume

    QJsonObject toJson() const;
    static CatalogueRecord fromJson(const QJsonObject& obj);
};
Q_DECLARE_METATYPE(CatalogueRecord)
