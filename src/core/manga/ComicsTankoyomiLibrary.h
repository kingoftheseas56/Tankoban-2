#pragma once

#include "ComicsLibraryRecord.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <optional>
#include <QStringList>

class JsonStore;

// Tankoyomi-origin Comics library store. Authoritative source-of-truth
// for "is this series Tankoyomi-origin?" — folder-imported series do
// NOT have entries here.
//
// JSON file: <appDataDir>/comics_library.json
// Schema version 1.
//
// Thread safety: mutating methods take m_mutex. Read accessors take
// m_mutex via QMutexLocker. libraryChanged is emitted off-lock.
class ComicsTankoyomiLibrary : public QObject
{
    Q_OBJECT
public:
    explicit ComicsTankoyomiLibrary(JsonStore* store, QObject* parent = nullptr);

    // Insert or replace by key (sourceId:seriesId). Idempotent.
    void add(const ComicsLibraryRecord& rec);
    void remove(const QString& sourceId, const QString& seriesId);

    bool contains(const QString& sourceId, const QString& seriesId) const;
    bool containsCanonicalPath(const QString& canonicalPath) const;
    ComicsLibraryRecord get(const QString& sourceId, const QString& seriesId) const;
    // O(1) lookup by canonical path, reusing the m_canonicalToKey map that
    // already powers containsCanonicalPath. Returns nullopt if no record
    // matches. Lets ComicsPage::onTileClicked skip the O(n) linear scan it
    // would otherwise need to find the record matching a clicked tile's
    // canonical path (code-quality review I3).
    std::optional<ComicsLibraryRecord> getByCanonicalPath(const QString& canonicalPath) const;
    QList<ComicsLibraryRecord> all() const;

    // Used by LibraryScanner integration (Task 11): paths that the
    // scanner MUST NOT emit as folder-origin SeriesInfo because
    // they're already claimed by Tankoyomi-origin records.
    QStringList claimedCanonicalPaths() const;

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 40 —
    // renamed-folder recovery. Walks every Comics root and looks for
    // any subfolder whose .tankoyomi-meta.json sidecar matches the given
    // (sourceId, seriesId) identity. Returns a reconstructed
    // ComicsLibraryRecord shell (sufficient to drive relocate(...)) on
    // hit; nullopt on miss. Read-only; does not mutate library state.
    std::optional<ComicsLibraryRecord> findByIdentityAcrossRoots(
        const QString& sourceId, const QString& seriesId,
        const QStringList& comicsRoots) const;

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 40 —
    // re-points an existing record's canonical path / root / folder
    // name after the user renamed or moved the folder on disk. No-op if
    // the (sourceId, seriesId) key isn't already in the library.
    // Re-saves under m_mutex and emits libraryChanged off-lock (matches
    // add()/remove() precedent).
    void relocate(const QString& sourceId, const QString& seriesId,
                  const QString& newCanonicalPath,
                  const QString& newRootFolder,
                  const QString& newSeriesFolderName);

    // Convenience: find-and-relocate in one call. Returns the updated
    // record (post-relocate) when an on-disk match is found and
    // relocate succeeded, nullopt otherwise (no record in library OR
    // sidecar identity not found under any root).
    std::optional<ComicsLibraryRecord> findAndRelocateByIdentity(
        const QString& sourceId, const QString& seriesId,
        const QStringList& comicsRoots);

signals:
    void libraryChanged();

private:
    void load();
    void save();

    JsonStore* m_store;
    mutable QMutex m_mutex;
    QHash<QString, ComicsLibraryRecord> m_byKey;             // sourceId:seriesId -> record
    QHash<QString, QString>             m_canonicalToKey;    // canonical path -> sourceId:seriesId

    static constexpr const char* FILENAME = "comics_library.json";
    static constexpr int kSchemaVersion = 1;
};
