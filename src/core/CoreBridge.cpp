#include "CoreBridge.h"
#include "JsonStore.h"
#include "core/stream/UnifiedProgressStore.h"

#include <QStandardPaths>
#include <QDir>
#include <QJsonArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFileInfo>

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
    {"shows",  "show_prefs.json"},
};

namespace {

struct StreamProgressIdentity {
    QString imdbId;
    int season = 0;
    int episode = 0;
    bool valid = false;
};

StreamProgressIdentity parseStreamProgressKey(const QString& itemId)
{
    const QStringList parts = itemId.split(QLatin1Char(':'));
    if (parts.size() == 2 && parts[0] == QLatin1String("stream")
        && !parts[1].isEmpty()) {
        return {parts[1], 0, 0, true};
    }
    if (parts.size() >= 4 && parts[0] == QLatin1String("stream")
        && parts[2].startsWith(QLatin1Char('s'))
        && parts[3].startsWith(QLatin1Char('e'))) {
        const int season = parts[2].mid(1).toInt();
        const int episode = parts[3].mid(1).toInt();
        if (!parts[1].isEmpty() && season > 0 && episode > 0)
            return {parts[1], season, episode, true};
    }
    return {};
}

QString videoIdForFilePath(const QString& path)
{
    QFileInfo fi(path);
    if (!fi.exists())
        return {};
    const QString raw = fi.absoluteFilePath() + QStringLiteral("::")
                      + QString::number(fi.size()) + QStringLiteral("::")
                      + QString::number(fi.lastModified().toMSecsSinceEpoch());
    return QString(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex());
}

}  // namespace

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
    , m_unifiedProgress(std::make_unique<UnifiedProgressStore>(m_store.get(), this))
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

void CoreBridge::notifyRootFoldersChanged(const QString& domain)
{
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

    QJsonObject all = m_store->read(file);
    if (domain == QLatin1String("stream") && m_unifiedProgress) {
        const QJsonObject unified = m_unifiedProgress->allEpisodePayloadsForStreamDomain();
        for (auto it = unified.constBegin(); it != unified.constEnd(); ++it)
            all[it.key()] = it.value();
    } else if (domain == QLatin1String("videos") && m_unifiedProgress) {
        const QJsonObject unified = m_unifiedProgress->allPathPayloadsForVideosDomain();
        for (auto it = unified.constBegin(); it != unified.constEnd(); ++it)
            all[it.key()] = it.value();
    }
    return all;
}

QJsonObject CoreBridge::progress(const QString& domain, const QString& itemId) const
{
    if (m_unifiedProgress && domain == QLatin1String("stream")) {
        const StreamProgressIdentity id = parseStreamProgressKey(itemId);
        if (id.valid) {
            const QJsonObject unified =
                m_unifiedProgress->episodePayload(id.imdbId, id.season, id.episode);
            if (!unified.isEmpty())
                return unified;
        }
    } else if (m_unifiedProgress && domain == QLatin1String("videos")) {
        const QJsonObject unified = m_unifiedProgress->payloadForLegacyVideoId(itemId);
        if (!unified.isEmpty())
            return unified;
    }

    const auto all = allProgress(domain);
    return all.value(itemId).toObject();
}

void CoreBridge::saveProgress(const QString& domain, const QString& itemId, const QJsonObject& data)
{
    QString file = PROGRESS_FILES.value(domain);
    if (file.isEmpty() || itemId.isEmpty())
        return;

    QJsonObject entry = data;
    entry["updatedAt"] = QDateTime::currentMSecsSinceEpoch();

    if (m_unifiedProgress && domain == QLatin1String("stream")) {
        const StreamProgressIdentity id = parseStreamProgressKey(itemId);
        if (id.valid) {
            entry[QStringLiteral("imdbId")] = id.imdbId;
            entry[QStringLiteral("season")] = id.season;
            entry[QStringLiteral("episode")] = id.episode;
            const QString path = entry.value(QStringLiteral("path")).toString();
            const QString legacyVideoId = videoIdForFilePath(path);
            if (!legacyVideoId.isEmpty())
                entry[QStringLiteral("legacyVideoId")] = legacyVideoId;
            m_unifiedProgress->setEpisodePayload(id.imdbId, id.season, id.episode, entry);
            return;
        }
    } else if (m_unifiedProgress && domain == QLatin1String("videos")) {
        const QString path = entry.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            entry[QStringLiteral("legacyVideoId")] = itemId;
            m_unifiedProgress->setPathPayload(path, entry, itemId);
            return;
        }
    }

    auto all = m_store->read(file);
    all[itemId] = entry;
    m_store->write(file, all);
}

void CoreBridge::clearProgress(const QString& domain, const QString& itemId)
{
    QString file = PROGRESS_FILES.value(domain);
    if (file.isEmpty() || itemId.isEmpty())
        return;

    auto all = m_store->read(file);
    all.remove(itemId);
    m_store->write(file, all);

    if (m_unifiedProgress && domain == QLatin1String("stream")) {
        const StreamProgressIdentity id = parseStreamProgressKey(itemId);
        if (id.valid)
            m_unifiedProgress->clearEpisode(id.imdbId, id.season, id.episode);
    } else if (m_unifiedProgress && domain == QLatin1String("videos")) {
        m_unifiedProgress->clearLegacyVideoId(itemId);
    }
}
