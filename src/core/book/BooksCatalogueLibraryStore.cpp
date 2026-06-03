#include "BooksCatalogueLibraryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrent>

BooksCatalogueLibraryStore::BooksCatalogueLibraryStore(const QString& dataDir,
                                                       QObject* parent)
    : QObject(parent), m_dataDir(dataDir)
{
    // Lazy load so tests can construct before calling load().
}

BooksCatalogueLibraryStore::~BooksCatalogueLibraryStore()
{
    // The async save writer captures `this`; block until it finishes so it
    // never writes through a destroyed store. Any bytes queued after the writer
    // started are flushed by its internal drain loop before it returns. Copy the
    // future out under the lock, then wait WITHOUT holding m_saveMutex — the
    // writer needs that lock to make progress (waiting under it would deadlock).
    QFuture<void> fut;
    {
        QMutexLocker wlk(&m_saveMutex);
        fut = m_saveFuture;
    }
    // Always wait (not just when isRunning()): a task that is queued in the
    // thread pool but not yet scheduled is not "running" yet, and skipping the
    // wait there would let it run through a destroyed store. waitForFinished()
    // is a no-op on a default-constructed or already-finished future.
    fut.waitForFinished();
}

// ── Mutate ──────────────────────────────────────────────────────────────────

void BooksCatalogueLibraryStore::upsertRecord(const CatalogueRecord& r)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        // Replace existing record's derived-map entries before re-inserting.
        auto existing = m_byId.find(r.catalogueId);
        if (existing != m_byId.end()) {
            // Value copy (not reference) — m_byId.insert below could
            // invalidate the iterator; we want to be safe against any
            // future refactor that reaches for `old` post-insert.
            const auto old = existing.value();
            if (!old.seriesId.isEmpty()) {
                auto sit = m_bySeries.find(old.seriesId);
                if (sit != m_bySeries.end()) {
                    sit.value().remove(old.catalogueId);
                    if (sit.value().isEmpty()) m_bySeries.erase(sit);
                }
            }
            if (!old.filePath.isEmpty()) m_byFilePath.remove(old.filePath);
        }
        m_byId.insert(r.catalogueId, r);
        if (!r.seriesId.isEmpty()) m_bySeries[r.seriesId].insert(r.catalogueId);
        if (!r.filePath.isEmpty()) m_byFilePath.insert(r.filePath, r.catalogueId);
        changed = true;
    }
    if (changed) {
        save();
        emit recordsChanged();
    }
}

void BooksCatalogueLibraryStore::evictByCatalogueId(const QString& catalogueId)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        auto it = m_byId.find(catalogueId);
        if (it == m_byId.end()) return;
        const auto rec = it.value();
        m_byId.erase(it);
        if (!rec.seriesId.isEmpty()) {
            auto sit = m_bySeries.find(rec.seriesId);
            if (sit != m_bySeries.end()) {
                sit.value().remove(catalogueId);
                if (sit.value().isEmpty()) m_bySeries.erase(sit);
            }
        }
        if (!rec.filePath.isEmpty()) m_byFilePath.remove(rec.filePath);
        changed = true;
    }
    if (changed) {
        save();
        emit recordsChanged();
    }
}

void BooksCatalogueLibraryStore::validateAll()
{
    // Snapshot (catalogueId, relative filePath) under the lock, then stat the
    // disk OFF the lock. validateAll() now runs on a background thread
    // (BooksPage migrates its call sites to QtConcurrent::run); holding m_mutex
    // across the per-record QFileInfo::exists() disk stats would block every
    // GUI-thread read (all()/recordFor()/…) on file I/O — the exact hitch the
    // off-thread move is meant to remove. Mirrors StreamDownloadIndex's
    // snapshot-under-lock / stat-off-lock validateAll contract.
    struct Pending { QString id; QString rel; };
    QList<Pending> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot.reserve(m_byId.size());
        for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
            const auto& rec = it.value();
            if (rec.filePath.isEmpty()) continue;
            snapshot.append({it.key(), rec.filePath});
        }
    }

    QList<QString> toEvict;
    for (const auto& p : snapshot) {
        // CatalogueRecord.filePath is the canonical relative path under Books
        // root; the absolute resolution against m_dataDir is the only
        // existence-check signal.
        const QString abs = QDir(m_dataDir).absoluteFilePath(p.rel);
        if (!QFileInfo::exists(abs)) toEvict.append(p.id);
    }
    for (const auto& id : toEvict) evictByCatalogueId(id);
}

void BooksCatalogueLibraryStore::updateReadProgress(const QString& catalogueId,
                                                    double readProgress,
                                                    qint64 lastReadAt,
                                                    const QString& lastReadCfi)
{
    {
        QMutexLocker lk(&m_mutex);
        auto it = m_byId.find(catalogueId);
        if (it == m_byId.end()) return;
        it.value().readProgress = readProgress;
        it.value().lastReadAt = lastReadAt;
        it.value().lastReadCfi = lastReadCfi;
    }
    save();
    emit recordReadStateChanged(catalogueId);
}

// ── Read ────────────────────────────────────────────────────────────────────

bool BooksCatalogueLibraryStore::hasRecord(const QString& catalogueId) const
{
    QMutexLocker lk(&m_mutex);
    return m_byId.contains(catalogueId);
}

std::optional<CatalogueRecord>
BooksCatalogueLibraryStore::recordFor(const QString& catalogueId) const
{
    QMutexLocker lk(&m_mutex);
    auto it = m_byId.constFind(catalogueId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

std::optional<QString>
BooksCatalogueLibraryStore::catalogueIdForFile(const QString& filePath) const
{
    QMutexLocker lk(&m_mutex);
    auto it = m_byFilePath.constFind(filePath);
    if (it == m_byFilePath.constEnd()) return std::nullopt;
    return it.value();
}

QList<CatalogueRecord> BooksCatalogueLibraryStore::all() const
{
    QMutexLocker lk(&m_mutex);
    return m_byId.values();
}

QList<QString>
BooksCatalogueLibraryStore::catalogueIdsForSeries(const QString& seriesId) const
{
    QMutexLocker lk(&m_mutex);
    auto it = m_bySeries.constFind(seriesId);
    if (it == m_bySeries.constEnd()) return {};
    return it.value().values();
}

QSet<QString> BooksCatalogueLibraryStore::allSeriesIds() const
{
    QMutexLocker lk(&m_mutex);
    return QSet<QString>(m_bySeries.keyBegin(), m_bySeries.keyEnd());
}

// ── Persistence ─────────────────────────────────────────────────────────────

void BooksCatalogueLibraryStore::load()
{
    const QString path = QDir(m_dataDir).absoluteFilePath(QString::fromLatin1(FILENAME));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonArray records = root.value(QStringLiteral("records")).toArray();

    QMutexLocker lk(&m_mutex);
    m_byId.clear();
    m_bySeries.clear();
    m_byFilePath.clear();
    for (const auto& v : records) {
        if (!v.isObject()) continue;
        auto rec = CatalogueRecord::fromJson(v.toObject());
        if (rec.catalogueId.isEmpty()) continue;
        m_byId.insert(rec.catalogueId, rec);
        if (!rec.seriesId.isEmpty()) m_bySeries[rec.seriesId].insert(rec.catalogueId);
        if (!rec.filePath.isEmpty()) m_byFilePath.insert(rec.filePath, rec.catalogueId);
    }
}

void BooksCatalogueLibraryStore::save()
{
    // 1) Serialize the CURRENT state to bytes synchronously under m_mutex. This
    //    is in-memory only (fast); the slow part — the disk write — is handed
    //    off below so the calling (usually GUI) thread never blocks on file I/O.
    QJsonArray records;
    {
        QMutexLocker lk(&m_mutex);
        for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
            records.append(it.value().toJson());
        }
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = kSchemaVersion;
    root[QStringLiteral("records")] = records;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // 2) Publish the newest snapshot as the pending write. If a background
    //    writer is already running it will pick these bytes up on its next loop
    //    (coalescing bursts of saves into one write of the latest state). Only
    //    the first caller launches the writer. m_pendingSaveBytes is never empty
    //    for real data, so emptiness is a safe "nothing queued" sentinel.
    //
    //    save() can run concurrently (GUI thread + the async validateAll worker
    //    that evicts → save), so the launch decision, the QtConcurrent dispatch,
    //    and the m_saveFuture store all happen INSIDE one m_saveMutex section —
    //    that keeps m_saveFuture in lock-step with m_saveInFlight (the destructor
    //    relies on it). QtConcurrent::run only enqueues here; the worker body
    //    runs on a pool thread and blocks on m_saveMutex until we release, so
    //    dispatching under the lock cannot deadlock.
    QMutexLocker wlk(&m_saveMutex);
    m_pendingSaveBytes = bytes;
    if (m_saveInFlight) return;
    m_saveInFlight = true;

    const QString dataDir = m_dataDir;
    m_saveFuture = QtConcurrent::run([this, dataDir]() {
        QDir().mkpath(dataDir);
        const QString path =
            QDir(dataDir).absoluteFilePath(QString::fromLatin1(FILENAME));
        // Drain loop: keep writing while newer bytes keep arriving, then clear
        // the in-flight flag under the lock so a subsequent save() relaunches.
        for (;;) {
            QByteArray out;
            {
                QMutexLocker dlk(&m_saveMutex);
                if (m_pendingSaveBytes.isEmpty()) {
                    m_saveInFlight = false;
                    return;
                }
                out = m_pendingSaveBytes;
                m_pendingSaveBytes.clear();
            }
            QSaveFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(out);
                f.commit();
            }
        }
    });
}

void BooksCatalogueLibraryStore::rebuildDerivedMapsLocked()
{
    m_bySeries.clear();
    m_byFilePath.clear();
    for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
        const auto& r = it.value();
        if (!r.seriesId.isEmpty()) m_bySeries[r.seriesId].insert(r.catalogueId);
        if (!r.filePath.isEmpty()) m_byFilePath.insert(r.filePath, r.catalogueId);
    }
}
