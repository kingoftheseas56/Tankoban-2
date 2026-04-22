#include "AudiobookMetaCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QProcess>
#include <QSaveFile>
#include <QDateTime>

namespace {

constexpr int kProbeTimeoutMs = 15000;
constexpr int kSchemaVersion  = 1;

// Parses ffprobe JSON output of the form:
//   {"format": {"duration": "1234.567890"}}
// Returns ms (rounded), or -1 if any step fails.
qint64 parseDurationJson(const QByteArray& stdoutBytes)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(stdoutBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return -1;

    const QJsonObject root = doc.object();
    const QJsonObject fmt  = root.value("format").toObject();
    const QString durStr   = fmt.value("duration").toString();
    if (durStr.isEmpty())
        return -1;

    bool ok = false;
    const double seconds = durStr.toDouble(&ok);
    if (!ok || seconds <= 0.0)
        return -1;

    return static_cast<qint64>(seconds * 1000.0 + 0.5);
}

// Cache key = path relative to folderPath, with forward slashes. For a leaf
// folder this is just the filename; for a wrapper folder it's `<subdir>/<file>`.
QString cacheKeyFor(const QString& folderPath, const QString& audioFilePath)
{
    const QString relative = QDir(folderPath).relativeFilePath(audioFilePath);
    QString normalized = relative;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalized;
}

}  // namespace

QMutex AudiobookMetaCache::s_mutex;
QString AudiobookMetaCache::s_ffprobeOverride;

QString AudiobookMetaCache::ffprobePath()
{
    {
        QMutexLocker lock(&s_mutex);
        if (!s_ffprobeOverride.isEmpty())
            return s_ffprobeOverride;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty())
        return {};

    // Installed bundle: <app>/resources/ffmpeg_sidecar/ffprobe.exe
    QString bundled = appDir + QStringLiteral("/resources/ffmpeg_sidecar/ffprobe.exe");
    if (QFileInfo::exists(bundled))
        return bundled;

    // Dev fallback candidates — repo layout is a couple of levels up from out/.
    const QStringList candidates{
        appDir + QStringLiteral("/../resources/ffmpeg_sidecar/ffprobe.exe"),
        appDir + QStringLiteral("/../../resources/ffmpeg_sidecar/ffprobe.exe"),
        appDir + QStringLiteral("/../../../resources/ffmpeg_sidecar/ffprobe.exe"),
    };
    for (const QString& cand : candidates) {
        if (QFileInfo::exists(cand))
            return QFileInfo(cand).canonicalFilePath();
    }

    return {};
}

QString AudiobookMetaCache::setFfprobePathOverrideForTest(const QString& override)
{
    QMutexLocker lock(&s_mutex);
    const QString prev = s_ffprobeOverride;
    s_ffprobeOverride = override;
    return prev;
}

qint64 AudiobookMetaCache::probeDurationMsRaw(const QString& audioFilePath)
{
    const QString probe = ffprobePath();
    if (probe.isEmpty() || !QFileInfo::exists(audioFilePath))
        return -1;

    QProcess proc;
    proc.setProgram(probe);
    proc.setArguments({
        QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
        QStringLiteral("-v"),            QStringLiteral("quiet"),
        QStringLiteral("-of"),           QStringLiteral("json"),
        QStringLiteral("-hide_banner"),
        audioFilePath,
    });
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QIODevice::ReadOnly);
    if (!proc.waitForStarted(5000))
        return -1;
    if (!proc.waitForFinished(kProbeTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(500);
        return -1;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return -1;

    return parseDurationJson(proc.readAllStandardOutput());
}

QString AudiobookMetaCache::cacheFilePath(const QString& folderPath)
{
    return QDir(folderPath).absoluteFilePath(QStringLiteral(".audiobook_meta.json"));
}

AudiobookMetaCache::FolderCache
AudiobookMetaCache::loadCache(const QString& folderPath)
{
    FolderCache cache;
    QFile f(cacheFilePath(folderPath));
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return cache;

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return cache;

    const QJsonObject root = doc.object();
    if (root.value("schemaVersion").toInt() != kSchemaVersion)
        return cache;

    const QJsonObject chapters = root.value("chapters").toObject();
    for (auto it = chapters.begin(); it != chapters.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const qint64 durationMs = static_cast<qint64>(entry.value("durationMs").toDouble());
        const qint64 mtimeMs    = static_cast<qint64>(entry.value("mtimeMs").toDouble());
        if (durationMs > 0 && mtimeMs > 0) {
            cache.durationsMs[it.key()] = durationMs;
            cache.mtimesMs[it.key()]    = mtimeMs;
        }
    }
    return cache;
}

void AudiobookMetaCache::saveCache(const QString& folderPath, const FolderCache& cache)
{
    QJsonObject chapters;
    for (auto it = cache.durationsMs.constBegin();
         it != cache.durationsMs.constEnd(); ++it) {
        QJsonObject entry;
        entry["durationMs"] = static_cast<double>(it.value());
        entry["mtimeMs"]    = static_cast<double>(cache.mtimesMs.value(it.key(), 0));
        chapters[it.key()]  = entry;
    }

    QJsonObject root;
    root["schemaVersion"] = kSchemaVersion;
    root["chapters"]      = chapters;
    root["updatedAt"]     = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    QSaveFile f(cacheFilePath(folderPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void AudiobookMetaCache::invalidateFolder(const QString& folderPath)
{
    QMutexLocker lock(&s_mutex);
    QFile::remove(cacheFilePath(folderPath));
}

qint64 AudiobookMetaCache::durationMsFor(const QString& folderPath,
                                         const QString& audioFilePath)
{
    const QFileInfo fi(audioFilePath);
    if (!fi.exists() || !fi.isFile())
        return -1;

    const QString key        = cacheKeyFor(folderPath, audioFilePath);
    const qint64 fileMtimeMs = fi.lastModified().toMSecsSinceEpoch();

    QMutexLocker lock(&s_mutex);

    FolderCache cache = loadCache(folderPath);
    auto itDur = cache.durationsMs.constFind(key);
    const qint64 cachedMtime = cache.mtimesMs.value(key, 0);
    if (itDur != cache.durationsMs.constEnd() && cachedMtime >= fileMtimeMs) {
        return itDur.value();
    }

    // Miss or stale → re-probe this single file, persist updated cache.
    // Release the lock during the subprocess to avoid serializing N scanner
    // threads behind one 15s-bounded ffprobe call. Safe: cache-file rewrite
    // is atomic (QSaveFile), last-writer-wins on this one key, probes are
    // side-effect-free + deterministic for identical inputs.
    lock.unlock();
    const qint64 durationMs = probeDurationMsRaw(audioFilePath);
    lock.relock();

    if (durationMs <= 0)
        return -1;

    cache = loadCache(folderPath);
    cache.durationsMs[key] = durationMs;
    cache.mtimesMs[key]    = fileMtimeMs;
    saveCache(folderPath, cache);

    return durationMs;
}
