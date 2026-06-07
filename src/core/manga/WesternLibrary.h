#pragma once
#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QJsonObject>
#include <QList>
#include <optional>

class JsonStore;

namespace tankoban::manga {

// One "my western series" record. Slug-keyed (readallcomics/RCO slug).
struct WesternLibraryRecord {
    QString seriesId;   // e.g. "invincible"
    QString title;      // "Invincible"
    QString coverUrl;   // remote cover URL (may be empty)
    qint64  addedAt = 0; // epoch ms; set by caller (keeps the store deterministic)

    QJsonObject toJson() const;
    static WesternLibraryRecord fromJson(const QJsonObject& o);
};

// Per-user store of added western series. Authoritative answer to "is this
// western series in my library?" — distinct from the shipped read-only
// catalogue (data/western_catalogue/). JsonStore file: western_library.json.
// nullptr store => in-memory only (unit tests), mirrors MangaDownloadIndex.
class WesternLibrary : public QObject {
    Q_OBJECT
public:
    explicit WesternLibrary(JsonStore* store, QObject* parent = nullptr);

    void addOrUpdate(const WesternLibraryRecord& rec); // idempotent by seriesId
    void remove(const QString& seriesId);
    bool contains(const QString& seriesId) const;
    std::optional<WesternLibraryRecord> get(const QString& seriesId) const;
    QList<WesternLibraryRecord> all() const;

signals:
    void libraryChanged();

private:
    void load();
    void save();

    JsonStore* m_store;
    mutable QMutex m_mutex;
    QHash<QString, WesternLibraryRecord> m_byId; // seriesId -> record

    static constexpr const char* FILENAME = "western_library.json";
    static constexpr int kSchemaVersion = 1;
};

} // namespace tankoban::manga
