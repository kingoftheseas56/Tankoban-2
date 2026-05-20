#include "BooksCatalogueLibraryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>

BooksCatalogueLibraryStore::BooksCatalogueLibraryStore(const QString& dataDir,
                                                       QObject* parent)
    : QObject(parent), m_dataDir(dataDir)
{
    // Lazy load so tests can construct before calling load().
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
            const auto& old = existing.value();
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
    QList<QString> toEvict;
    {
        QMutexLocker lk(&m_mutex);
        for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
            const auto& rec = it.value();
            if (rec.filePath.isEmpty()) continue;
            const QString abs = QDir(m_dataDir).absoluteFilePath(rec.filePath);
            if (!QFileInfo::exists(abs) &&
                !QFileInfo::exists(rec.filePath)) {
                toEvict.append(it.key());
            }
        }
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

    QDir().mkpath(m_dataDir);
    const QString path = QDir(m_dataDir).absoluteFilePath(QString::fromLatin1(FILENAME));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
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
