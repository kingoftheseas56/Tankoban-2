#pragma once

#include <QObject>
#include <QString>
#include <QList>

#include "core/book/FictionDbClient.h"  // SeriesIndexEntry

// Local series catalogue for Books mode (BOOKS_FICTIONDB_CATALOGUE §4.2).
//
// Built from FictionDB's A–Z series directory (author-series~<a..z>.htm), this
// answers series search instantly + offline + series-first — sidestepping
// FictionDB's flat, noisy free-text search. JSON-backed sibling of
// BooksCatalogueLibraryStore.
//
// Load order (load()): a refreshed copy in the data dir if present + schema-
// valid, else the bundled resource shipped with the app (so there is no
// first-launch crawl wait). The builder (BookSeriesIndexBuilder) walks the
// directory and calls setEntries()+save() to produce/refresh the data-dir copy.
class BookSeriesIndex : public QObject
{
    Q_OBJECT

public:
    explicit BookSeriesIndex(const QString& dataDir, QObject* parent = nullptr);

    // Prefer <dataDir>/book_series_index.json; fall back to bundledResourcePath
    // (may be empty to skip). Both are plain JSON.
    void load(const QString& bundledResourcePath);

    // Case-insensitive ranked match on series name (+author). exact > prefix >
    // contains; author-contains adds a small bonus. Returns up to `limit`.
    QList<SeriesIndexEntry> query(const QString& text, int limit = 24) const;

    int size() const { return m_entries.size(); }
    qint64 builtAt() const { return m_builtAt; }

    // Used by the builder to persist a freshly-walked index.
    void setEntries(const QList<SeriesIndexEntry>& entries, qint64 builtAt);
    void save() const;  // writes <dataDir>/book_series_index.json

    static constexpr const char* FILENAME = "book_series_index.json";
    static constexpr int kSchemaVersion = 1;

    // Pure ranking primitive — exposed for tests. 0 == no match.
    static int matchScore(const QString& query, const SeriesIndexEntry& e);

private:
    bool loadFromFile(const QString& path);

    QString m_dataDir;
    QList<SeriesIndexEntry> m_entries;
    qint64 m_builtAt = 0;
};
