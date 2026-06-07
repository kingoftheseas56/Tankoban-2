#include "WesternLibrary.h"
#include "core/JsonStore.h"
#include <QJsonArray>

namespace tankoban::manga {

QJsonObject WesternLibraryRecord::toJson() const {
    QJsonObject o;
    o["seriesId"] = seriesId;
    o["title"]    = title;
    o["coverUrl"] = coverUrl;
    o["addedAt"]  = addedAt;
    return o;
}

WesternLibraryRecord WesternLibraryRecord::fromJson(const QJsonObject& o) {
    WesternLibraryRecord r;
    r.seriesId = o.value("seriesId").toString();
    r.title    = o.value("title").toString();
    r.coverUrl = o.value("coverUrl").toString();
    r.addedAt  = o.value("addedAt").toVariant().toLongLong();
    return r;
}

WesternLibrary::WesternLibrary(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store) { load(); }

void WesternLibrary::load() {
    if (!m_store) return;
    const QJsonObject root = m_store->read(FILENAME);
    if (root.isEmpty()) return;
    const QJsonArray records = root.value("records").toArray();
    for (const auto& v : records) {
        const auto r = WesternLibraryRecord::fromJson(v.toObject());
        if (!r.seriesId.isEmpty()) m_byId.insert(r.seriesId, r);
    }
}

void WesternLibrary::save() {
    if (!m_store) return;
    QJsonArray arr;
    {
        QMutexLocker lk(&m_mutex);
        for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it)
            arr.append(it.value().toJson());
    }
    QJsonObject root;
    root["schemaVersion"] = kSchemaVersion;
    root["records"]       = arr;
    m_store->write(FILENAME, root);
}

void WesternLibrary::addOrUpdate(const WesternLibraryRecord& rec) {
    if (rec.seriesId.isEmpty()) return;
    {
        QMutexLocker lk(&m_mutex);
        m_byId.insert(rec.seriesId, rec); // insert replaces by key
    }
    save();
    emit libraryChanged();
}

void WesternLibrary::remove(const QString& seriesId) {
    {
        QMutexLocker lk(&m_mutex);
        m_byId.remove(seriesId);
    }
    save();
    emit libraryChanged();
}

bool WesternLibrary::contains(const QString& seriesId) const {
    QMutexLocker lk(&m_mutex);
    return m_byId.contains(seriesId);
}

std::optional<WesternLibraryRecord> WesternLibrary::get(const QString& seriesId) const {
    QMutexLocker lk(&m_mutex);
    const auto it = m_byId.constFind(seriesId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

QList<WesternLibraryRecord> WesternLibrary::all() const {
    QMutexLocker lk(&m_mutex);
    return m_byId.values();
}

} // namespace tankoban::manga
