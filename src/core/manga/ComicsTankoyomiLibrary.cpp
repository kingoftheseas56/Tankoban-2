#include "ComicsTankoyomiLibrary.h"
#include "core/JsonStore.h"
#include "ui/pages/comics/SidecarMeta.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

ComicsTankoyomiLibrary::ComicsTankoyomiLibrary(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    load();
}

void ComicsTankoyomiLibrary::load()
{
    // JsonStore::read returns QJsonObject directly (not QJsonDocument);
    // empty object means file missing or malformed — treat as empty.
    const QJsonObject root = m_store->read(FILENAME);
    if (root.isEmpty()) return;
    const QJsonArray records = root.value("records").toArray();
    for (const auto& v : records) {
        auto rec = ComicsLibraryRecord::fromJson(v.toObject());
        m_byKey.insert(rec.key(), rec);
        if (!rec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.insert(rec.canonicalSeriesPath, rec.key());
    }
}

// Audit P2-3 (2026-05-15): snapshot under lock, write off-lock. Mirrors
// MangaDownloadIndex::save() pattern at MangaDownloadIndex.cpp:97-122. The
// disk I/O no longer happens while m_mutex is held; mutator callers
// (add/remove/relocate) tightened their QMutexLocker scopes to mutation-only
// and call save() after release. m_store->write() is the only I/O path here
// and is now strictly off-lock.
void ComicsTankoyomiLibrary::save()
{
    if (!m_store) return;

    QJsonArray arr;
    {
        QMutexLocker lk(&m_mutex);
        for (auto it = m_byKey.constBegin(); it != m_byKey.constEnd(); ++it)
            arr.append(it.value().toJson());
    }
    QJsonObject root;
    root["schemaVersion"] = kSchemaVersion;
    root["records"]       = arr;
    m_store->write(FILENAME, root);
}

void ComicsTankoyomiLibrary::add(const ComicsLibraryRecord& rec)
{
    {
        QMutexLocker lk(&m_mutex);
        const auto oldRec = m_byKey.value(rec.key());
        if (!oldRec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.remove(oldRec.canonicalSeriesPath);
        m_byKey.insert(rec.key(), rec);
        if (!rec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.insert(rec.canonicalSeriesPath, rec.key());
    }
    save();
    emit libraryChanged();
}

void ComicsTankoyomiLibrary::remove(const QString& sourceId, const QString& seriesId)
{
    {
        QMutexLocker lk(&m_mutex);
        const auto key = ComicsLibraryRecord::makeKey(sourceId, seriesId);
        const auto oldRec = m_byKey.take(key);
        if (!oldRec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.remove(oldRec.canonicalSeriesPath);
    }
    save();
    emit libraryChanged();
}

bool ComicsTankoyomiLibrary::contains(const QString& sourceId, const QString& seriesId) const
{
    QMutexLocker lk(&m_mutex);
    return m_byKey.contains(ComicsLibraryRecord::makeKey(sourceId, seriesId));
}

bool ComicsTankoyomiLibrary::containsCanonicalPath(const QString& canonicalPath) const
{
    QMutexLocker lk(&m_mutex);
    return m_canonicalToKey.contains(canonicalPath);
}

ComicsLibraryRecord ComicsTankoyomiLibrary::get(const QString& sourceId, const QString& seriesId) const
{
    QMutexLocker lk(&m_mutex);
    return m_byKey.value(ComicsLibraryRecord::makeKey(sourceId, seriesId));
}

std::optional<ComicsLibraryRecord> ComicsTankoyomiLibrary::getByCanonicalPath(const QString& canonicalPath) const
{
    QMutexLocker lk(&m_mutex);
    const auto it = m_canonicalToKey.constFind(canonicalPath);
    if (it == m_canonicalToKey.constEnd()) return std::nullopt;
    const auto recIt = m_byKey.constFind(*it);
    if (recIt == m_byKey.constEnd()) return std::nullopt;
    return *recIt;
}

QList<ComicsLibraryRecord> ComicsTankoyomiLibrary::all() const
{
    QMutexLocker lk(&m_mutex);
    return m_byKey.values();
}

QStringList ComicsTankoyomiLibrary::claimedCanonicalPaths() const
{
    QMutexLocker lk(&m_mutex);
    return m_canonicalToKey.keys();
}

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 40.
// Depth contract: Tankoyomi-origin series ALWAYS live exactly one level
// under a Comics root (canonicalSeriesPath = rootFolder + "/" + seriesFolderName
// per ComicsTankoyomiDetailView::onAddRemoveClicked add path). This iterator
// is therefore intentionally non-recursive (QDir::Dirs without
// QDirIterator::Subdirectories). Cross-root scans for renamed-folder recovery
// match that contract; moves of a series into a nested subdirectory are out
// of scope (user can re-Add to library to reconcile).
// Returns the FIRST sidecar match across roots in m_bridge->rootFolders("comics")
// order; duplicate sidecars under multiple roots are silently masked.
std::optional<ComicsLibraryRecord> ComicsTankoyomiLibrary::findByIdentityAcrossRoots(
    const QString& sourceId, const QString& seriesId,
    const QStringList& comicsRoots) const
{
    // No locker — pure-filesystem read walk, doesn't touch m_byKey/m_canonicalToKey.
    for (const auto& root : comicsRoots) {
        if (root.isEmpty()) continue;
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            const auto folder = it.next();
            const auto meta = sidecar::read(folder);
            if (meta && meta->sourceId == sourceId && meta->seriesId == seriesId) {
                ComicsLibraryRecord r;
                r.sourceId = sourceId;
                r.seriesId = seriesId;
                r.title    = meta->title;
                r.origin   = "tankoyomi";
                r.rootFolder = root;
                r.canonicalSeriesPath = folder;
                r.seriesFolderName    = QFileInfo(folder).fileName();
                return r;
            }
        }
    }
    return std::nullopt;
}

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 40.
// Audit P2-3 (2026-05-15): mutation under lock, save() + emit off-lock to
// match the snapshot-under-lock pattern in save() above. The early-return
// for the no-record-found case stays inside the lock so the no-op path
// neither writes nor emits.
void ComicsTankoyomiLibrary::relocate(const QString& sourceId, const QString& seriesId,
                                       const QString& newCanonicalPath,
                                       const QString& newRootFolder,
                                       const QString& newSeriesFolderName)
{
    {
        QMutexLocker lk(&m_mutex);
        const auto key = ComicsLibraryRecord::makeKey(sourceId, seriesId);
        if (!m_byKey.contains(key)) return;  // no-op path: don't save, don't emit
        auto rec = m_byKey.value(key);
        if (!rec.canonicalSeriesPath.isEmpty())
            m_canonicalToKey.remove(rec.canonicalSeriesPath);
        rec.canonicalSeriesPath = newCanonicalPath;
        rec.rootFolder          = newRootFolder;
        rec.seriesFolderName    = newSeriesFolderName;
        m_byKey.insert(key, rec);
        if (!newCanonicalPath.isEmpty())
            m_canonicalToKey.insert(newCanonicalPath, key);
    }
    save();
    emit libraryChanged();
}

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 6 Task 40.
std::optional<ComicsLibraryRecord> ComicsTankoyomiLibrary::findAndRelocateByIdentity(
    const QString& sourceId, const QString& seriesId,
    const QStringList& comicsRoots)
{
    if (!contains(sourceId, seriesId)) return std::nullopt;
    const auto found = findByIdentityAcrossRoots(sourceId, seriesId, comicsRoots);
    if (!found) return std::nullopt;
    relocate(sourceId, seriesId,
             found->canonicalSeriesPath,
             found->rootFolder,
             found->seriesFolderName);
    // Code-quality review Important: relocate() silently no-ops if the
    // record was concurrently removed between our contains() check and
    // the relocate() lock. Re-check before get() — otherwise a
    // default-constructed record would clobber a previously-valid record
    // at the caller. Today's single-threaded UI-thread caller mitigates
    // the race, but the explicit guard documents the invariant.
    if (!contains(sourceId, seriesId)) return std::nullopt;
    // get(...) returns by value with the updated canonical fields.
    return get(sourceId, seriesId);
}
