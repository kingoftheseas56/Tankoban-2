#pragma once

#include <QHash>
#include <QSet>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <optional>

#include "CatalogueRecord.h"

// BOOKS_STREMIO_PIVOT 2026-05-20 — persistent catalogue-records library for
// Books mode after the burn-the-ships migration (spec §3.8).
//
// Replaces the folder-scan-driven model: a book is in the library iff a
// CatalogueRecord wraps it. Files on disk without a matching record are
// ignored (we never delete user data, but we don't surface them either).
//
// Owns three in-memory lookup maps derived from a single sibling JSON file
// (<dataDir>/books_catalogue_library.json), patterned after StreamDownloadIndex
// (src/core/stream/StreamDownloadIndex.h:21):
//   - m_byId       : catalogueId -> CatalogueRecord (primary)
//   - m_bySeries   : seriesId -> set<catalogueId> (series aggregation for grid)
//   - m_byFilePath : filePath -> catalogueId (reverse lookup for validate)
//
// Threadsafe — BooksScanner reads from a worker thread via mutex-guarded
// const APIs. Mutating methods (upsertRecord / evict / validateAll /
// updateReadProgress) execute synchronously on the calling thread, acquire
// m_mutex around map mutations, then call save() and emit recordsChanged()
// OFF the lock.
class BooksCatalogueLibraryStore : public QObject
{
    Q_OBJECT

public:
    // dataDir is the folder under which books_catalogue_library.json lives.
    // Production callsite passes CoreBridge::dataDir(); tests pass a QTemporaryDir.
    explicit BooksCatalogueLibraryStore(const QString& dataDir, QObject* parent = nullptr);

    // ── Mutate ────────────────────────────────────────────────────────────
    // Upsert (insert or replace by catalogueId). Updates all three derived maps
    // and persists. Emits recordsChanged() after save returns.
    void upsertRecord(const CatalogueRecord& r);

    // Drop a record by catalogueId. File on disk is NOT deleted. If the
    // catalogueId was the last entry for its seriesId, the series goes off
    // the seriesId map. Emits recordsChanged().
    void evictByCatalogueId(const QString& catalogueId);

    // Drop all records whose filePath does not exist on disk anymore.
    // Mirror of StreamDownloadIndex::validateAll. Called on BooksPage::showEvent.
    void validateAll();

    // Update per-record read state. Persists. Emits recordReadStateChanged.
    void updateReadProgress(const QString& catalogueId,
                            double readProgress,
                            qint64 lastReadAt,
                            const QString& lastReadCfi);

    // ── Read (const, mutex-guarded) ───────────────────────────────────────
    bool hasRecord(const QString& catalogueId) const;
    std::optional<CatalogueRecord> recordFor(const QString& catalogueId) const;
    std::optional<QString> catalogueIdForFile(const QString& filePath) const;
    QList<CatalogueRecord> all() const;

    // Series-aware reads
    QList<QString> catalogueIdsForSeries(const QString& seriesId) const;
    QSet<QString> allSeriesIds() const;

    // ── Persistence (explicit for testability) ────────────────────────────
    void load();
    void save();

    static constexpr const char* FILENAME = "books_catalogue_library.json";
    static constexpr int kSchemaVersion = 1;

signals:
    void recordsChanged();
    // Granular: fires when a single record's read progress or lastReadAt updates.
    void recordReadStateChanged(const QString& catalogueId);

private:
    void rebuildDerivedMapsLocked();

    QString m_dataDir;
    mutable QMutex m_mutex;
    QHash<QString, CatalogueRecord> m_byId;
    QHash<QString, QSet<QString>>   m_bySeries;       // seriesId -> {catalogueId}
    QHash<QString, QString>         m_byFilePath;     // filePath -> catalogueId
};
