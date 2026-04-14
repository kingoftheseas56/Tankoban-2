#include "CoreBridge.h"
#include "JsonStore.h"

#include <QStandardPaths>
#include <QDir>
#include <QJsonArray>
#include <QDateTime>

// ── Domain → JSON filename mapping ──
static const QMap<QString, QString> ROOTS_FILES = {
    {"comics",     "library_state.json"},
    {"books",      "books_state.json"},
    {"videos",     "video_state.json"},
    {"audiobooks", "audiobook_state.json"},
};

static const QMap<QString, QString> ROOTS_KEYS = {
    {"comics",     "rootFolders"},
    {"books",      "bookRootFolders"},
    {"videos",     "videoFolders"},
    {"audiobooks", "audiobookRootFolders"},
};

static const QMap<QString, QString> PROGRESS_FILES = {
    {"comics", "progress.json"},
    {"books",  "books_progress.json"},
    {"videos", "video_progress.json"},
    {"stream", "stream_progress.json"},
};

// ── Resolve data directory ──
QString CoreBridge::resolveDataDir()
{
    // Check environment variable first
    QString envDir = qEnvironmentVariable("TANKOBAN_DATA_DIR");
    if (!envDir.isEmpty())
        return QDir(envDir).absolutePath();

    // Default: %LOCALAPPDATA%/Tankoban/data
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).absoluteFilePath("Tankoban/data");
}

// ── Constructor / Destructor ──
CoreBridge::CoreBridge(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_store(std::make_unique<JsonStore>(dataDir))
{
}

CoreBridge::~CoreBridge() = default;

QString CoreBridge::dataDir() const
{
    return m_store->dataDir();
}

JsonStore& CoreBridge::store()
{
    return *m_store;
}

// ── Root folders ──
QStringList CoreBridge::rootFolders(const QString& domain) const
{
    QString file = ROOTS_FILES.value(domain);
    QString key  = ROOTS_KEYS.value(domain);
    if (file.isEmpty() || key.isEmpty())
        return {};

    auto state = m_store->read(file);
    QJsonArray arr = state.value(key).toArray();
    QStringList result;
    for (const auto& v : arr)
        result.append(v.toString());
    return result;
}

void CoreBridge::addRootFolder(const QString& domain, const QString& path)
{
    QString file = ROOTS_FILES.value(domain);
    QString key  = ROOTS_KEYS.value(domain);
    if (file.isEmpty() || key.isEmpty() || path.isEmpty())
        return;

    auto state = m_store->read(file);
    QJsonArray arr = state.value(key).toArray();

    // Deduplicate
    QString normalized = QDir(path).absolutePath();
    for (const auto& v : arr) {
        if (QDir(v.toString()).absolutePath() == normalized)
            return;
    }

    arr.append(normalized);
    state[key] = arr;
    m_store->write(file, state);
    emit rootFoldersChanged(domain);
}

void CoreBridge::removeRootFolder(const QString& domain, const QString& path)
{
    QString file = ROOTS_FILES.value(domain);
    QString key  = ROOTS_KEYS.value(domain);
    if (file.isEmpty() || key.isEmpty())
        return;

    auto state = m_store->read(file);
    QJsonArray arr = state.value(key).toArray();
    QJsonArray filtered;
    QString normalized = QDir(path).absolutePath();
    for (const auto& v : arr) {
        if (QDir(v.toString()).absolutePath() != normalized)
            filtered.append(v);
    }

    state[key] = filtered;
    m_store->write(file, state);
    emit rootFoldersChanged(domain);
}

// ── Shell prefs ──
QJsonObject CoreBridge::prefs() const
{
    return m_store->read("shell_prefs.json");
}

void CoreBridge::savePrefs(const QJsonObject& patch)
{
    auto current = prefs();
    for (auto it = patch.begin(); it != patch.end(); ++it)
        current[it.key()] = it.value();
    m_store->write("shell_prefs.json", current);
}

// ── Progress ──
QJsonObject CoreBridge::allProgress(const QString& domain) const
{
    QString file = PROGRESS_FILES.value(domain);
    if (file.isEmpty())
        return {};
    return m_store->read(file);
}

QJsonObject CoreBridge::progress(const QString& domain, const QString& itemId) const
{
    auto all = allProgress(domain);
    return all.value(itemId).toObject();
}

void CoreBridge::saveProgress(const QString& domain, const QString& itemId, const QJsonObject& data)
{
    QString file = PROGRESS_FILES.value(domain);
    if (file.isEmpty() || itemId.isEmpty())
        return;

    auto all = allProgress(domain);
    QJsonObject entry = data;
    entry["updatedAt"] = QDateTime::currentMSecsSinceEpoch();
    all[itemId] = entry;
    m_store->write(file, all);
}

void CoreBridge::clearProgress(const QString& domain, const QString& itemId)
{
    QString file = PROGRESS_FILES.value(domain);
    if (file.isEmpty() || itemId.isEmpty())
        return;

    auto all = allProgress(domain);
    all.remove(itemId);
    m_store->write(file, all);
}
