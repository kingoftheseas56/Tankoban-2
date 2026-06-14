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
    // SIX_MODE_RESTRUCTURE Arc 2 (2026-06-07), Task 5 — the video split gives
    // anime/tv/movies their own continue-watching JSON files. Each routes onto
    // the shared UnifiedProgressStore under its own domain-prefixed key
    // namespace (see isUnifiedVideoDomain below). Legacy "stream" stays for
    // back-compat until the downstream call sites flip (Tasks 9-13).
    {"anime",  "anime_progress.json"},
    {"tv",     "tv_progress.json"},
    {"movies", "movies_progress.json"},
    {"shows",  "show_prefs.json"},
};

namespace {

struct StreamProgressIdentity {
    QString imdbId;
    int season = 0;
    int episode = 0;
    bool valid = false;
};

// SIX_MODE_RESTRUCTURE Arc 2 (2026-06-07), Task 5 — the video modes that route
// their continue-watching onto the shared UnifiedProgressStore. Each owns a
// distinct progress JSON file (PROGRESS_FILES) AND a distinct key prefix.
bool isUnifiedVideoDomain(const QString& domain)
{
    return domain == QLatin1String("stream") || domain == QLatin1String("anime")
        || domain == QLatin1String("tv") || domain == QLatin1String("movies");
}

// Parse a continue-watching key under the given domain prefix. Delegates to the
// dep-free UnifiedProgressStore::parseDomainKey so the build (in the store) and
// parse halves share one implementation and can never drift. The domain string
// doubles as the key prefix (e.g. "anime:tt..:s..:e..").
StreamProgressIdentity parseStreamProgressKey(const QString& itemId,
                                              const QString& domainPrefix)
{
    QString imdb;
    int season = 0;
    int episode = 0;
    if (UnifiedProgressStore::parseDomainKey(itemId, domainPrefix, imdb, season, episode))
        return {imdb, season, episode, true};
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
    if (isUnifiedVideoDomain(domain) && m_unifiedProgress) {
        const QJsonObject unified = m_unifiedProgress->allEpisodePayloadsForStreamDomain(domain);
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
    if (m_unifiedProgress && isUnifiedVideoDomain(domain)) {
        const StreamProgressIdentity id = parseStreamProgressKey(itemId, domain);
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

    if (m_unifiedProgress && isUnifiedVideoDomain(domain)) {
        const StreamProgressIdentity id = parseStreamProgressKey(itemId, domain);
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

    if (m_unifiedProgress && isUnifiedVideoDomain(domain)) {
        const StreamProgressIdentity id = parseStreamProgressKey(itemId, domain);
        if (id.valid)
            m_unifiedProgress->clearEpisode(id.imdbId, id.season, id.episode);
    } else if (m_unifiedProgress && domain == QLatin1String("videos")) {
        m_unifiedProgress->clearLegacyVideoId(itemId);
    }
}
