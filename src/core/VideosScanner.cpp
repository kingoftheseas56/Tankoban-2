#include "VideosScanner.h"
#include "ScannerUtils.h"
#include "core/stream/StreamDownloadIndex.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QCollator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <algorithm>

const QStringList VideosScanner::VIDEO_EXTS = {
    "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov", "*.wmv", "*.flv",
    "*.m4v", "*.ts", "*.mpg", "*.mpeg", "*.ogv"
};

VideosScanner::VideosScanner(QObject* parent)
    : QObject(parent)
{
}

QString VideosScanner::cacheKey(const QString& path, qint64 size, qint64 mtimeMs)
{
    return path + "::" + QString::number(size) + "::" + QString::number(mtimeMs);
}

void VideosScanner::loadDurationCache()
{
    if (!m_durationCache.isEmpty() || m_cacheDir.isEmpty())
        return;
    QFile f(m_cacheDir + "/video_durations.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        m_durationCache.insert(it.key(), it.value().toDouble());
}

void VideosScanner::saveDurationCache()
{
    if (m_cacheDir.isEmpty())
        return;
    QDir().mkpath(m_cacheDir);
    QJsonObject obj;
    for (auto it = m_durationCache.begin(); it != m_durationCache.end(); ++it)
        obj[it.key()] = it.value();
    QFile f(m_cacheDir + "/video_durations.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void VideosScanner::scan(const QStringList& rootFolders)
{
    loadDurationCache();

    // Group video files by first-level subdirectory under each root
    QMap<QString, QStringList> showMap =
        ScannerUtils::groupByFirstLevelSubdir(rootFolders, VIDEO_EXTS);

    QCollator collator;
    collator.setNumericMode(true);

    QList<ShowInfo> allShows;
    m_pendingProbes.clear();

    for (auto it = showMap.begin(); it != showMap.end(); ++it) {
        QString showPath = it.key();
        QStringList& files = it.value();

        // STREAM_DOWNLOADED_LIBRARY Phase 5 (2026-05-10) — drop files that
        // the stream-side download index marks as Stream-owned. They live on
        // disk at the canonical path but Stream mode is the blessed UI for
        // them (spec §3 P1 + §6.4 + §8). Single QHash::contains per file
        // (~1µs); no measurable scanner regression. If every file in this
        // show is Stream-owned, skip the show entirely to avoid emitting an
        // empty ShowInfo.
        if (m_downloadIndex) {
            QStringList filtered;
            filtered.reserve(files.size());
            for (const QString& path : files) {
                const QString key = StreamDownloadIndex::computeCanonicalKey(path);
                if (!m_downloadIndex->isStreamOwned(key))
                    filtered.append(path);
            }
            files = filtered;
            if (files.isEmpty())
                continue;
        }

        std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });

        bool isLoose = showPath.endsWith("::LOOSE");
        if (isLoose) {
            for (const auto& f : files) {
                QFileInfo fi(f);
                const qint64 sz = fi.size();
                const qint64 mt = fi.lastModified().toMSecsSinceEpoch();

                ShowInfo info;
                info.showName = ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName());
                if (info.showName.isEmpty())
                    info.showName = fi.fileName();
                info.showPath = fi.absoluteFilePath();
                info.isLoose = true;
                info.episodeCount = 1;
                info.totalSizeBytes = sz;
                info.newestMtimeMs = mt;

                ShowInfo::FileEntry fe;
                fe.path = fi.absoluteFilePath();
                fe.sizeBytes = sz;
                fe.mtimeMs = mt;

                QString key = cacheKey(fe.path, sz, mt);
                auto cit = m_durationCache.find(key);
                if (cit != m_durationCache.end()) {
                    fe.durationSec = cit.value();
                } else {
                    fe.durationSec = 0.0;
                    m_pendingProbes.append(fe.path);
                }

                info.files.append(fe);
                allShows.append(info);
                emit showFound(info);
            }
            continue;
        }

        ShowInfo info;
        info.showName = ScannerUtils::cleanMediaFolderTitle(QDir(showPath).dirName());
        info.showPath = showPath;
        info.isLoose = false;
        info.episodeCount = files.size();

        qint64 totalSize = 0;
        qint64 newest = 0;
        for (const auto& f : files) {
            QFileInfo fi(f);
            qint64 sz = fi.size();
            qint64 mt = fi.lastModified().toMSecsSinceEpoch();
            totalSize += sz;
            if (mt > newest) newest = mt;

            ShowInfo::FileEntry fe;
            fe.path = f;
            fe.sizeBytes = sz;
            fe.mtimeMs = mt;

            // Lookup cached duration; if missing, leave at 0 and queue for background probe
            QString key = cacheKey(f, sz, mt);
            auto cit = m_durationCache.find(key);
            if (cit != m_durationCache.end()) {
                fe.durationSec = cit.value();
            } else {
                fe.durationSec = 0.0;
                m_pendingProbes.append(f);
            }

            info.files.append(fe);
        }
        info.totalSizeBytes = totalSize;
        info.newestMtimeMs = newest;

        allShows.append(info);
        emit showFound(info);
    }

    std::sort(allShows.begin(), allShows.end(), [&collator](const ShowInfo& a, const ShowInfo& b) {
        return collator.compare(a.showName, b.showName) < 0;
    });

    emit scanFinished(allShows);

    // Kick off background probing for files that weren't cached
    if (!m_pendingProbes.isEmpty())
        QMetaObject::invokeMethod(this, "backgroundProbeDurations", Qt::QueuedConnection);
}

void VideosScanner::backgroundProbeDurations()
{
    QMap<QString, double> updates;
    int batchCount = 0;

    while (!m_pendingProbes.isEmpty()) {
        QString f = m_pendingProbes.takeFirst();
        QFileInfo fi(f);
        if (!fi.exists()) continue;

        double dur = probeDuration(f);
        if (dur > 0.0) {
            QString key = cacheKey(f, fi.size(), fi.lastModified().toMSecsSinceEpoch());
            m_durationCache.insert(key, dur);
            updates.insert(f, dur);
        }

        // Flush a batch every 20 files so the UI updates progressively
        if (++batchCount >= 20) {
            if (!updates.isEmpty()) {
                emit durationsUpdated(updates);
                updates.clear();
            }
            saveDurationCache();
            batchCount = 0;
        }
    }

    if (!updates.isEmpty())
        emit durationsUpdated(updates);
    saveDurationCache();
}

double VideosScanner::probeDuration(const QString& filePath)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("ffprobe", {
        "-v", "quiet",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        filePath
    });

    if (!proc.waitForFinished(5000))
        return 0.0;

    QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    bool ok = false;
    double dur = output.toDouble(&ok);
    return ok ? dur : 0.0;
}
