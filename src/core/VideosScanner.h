#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>

class StreamDownloadIndex;

struct ShowInfo {
    struct FileEntry {
        QString path;
        double durationSec = 0.0;
        qint64 sizeBytes = 0;
        qint64 mtimeMs = 0;
    };

    QString showName;
    QString showPath;
    int episodeCount = 0;
    qint64 totalSizeBytes = 0;
    qint64 newestMtimeMs = 0;
    bool isLoose = false;
    QList<FileEntry> files;
};
Q_DECLARE_METATYPE(ShowInfo)

class VideosScanner : public QObject {
    Q_OBJECT
public:
    explicit VideosScanner(QObject* parent = nullptr);

    void setCacheDir(const QString& dir) { m_cacheDir = dir; }

    // STREAM_DOWNLOADED_LIBRARY Phase 5 (2026-05-10) — non-owning pointer.
    // When set, scan() filters out files that the index marks as Stream-owned
    // before grouping them into shows; spec §3 P1 + §6.4 + §8.
    void setStreamDownloadIndex(StreamDownloadIndex* idx) { m_downloadIndex = idx; }

public slots:
    void scan(const QStringList& rootFolders);
    void backgroundProbeDurations();

signals:
    void showFound(const ShowInfo& show);
    void scanFinished(const QList<ShowInfo>& allShows);
    void durationsUpdated(const QMap<QString, double>& durations);

private:
    static double probeDuration(const QString& filePath);
    static QString cacheKey(const QString& path, qint64 size, qint64 mtimeMs);
    void loadDurationCache();
    void saveDurationCache();
    static const QStringList VIDEO_EXTS;

    QString m_cacheDir;
    QMap<QString, double> m_durationCache;  // cacheKey -> seconds
    QStringList m_pendingProbes;            // file paths still needing probe
    StreamDownloadIndex* m_downloadIndex = nullptr;  // non-owning; Phase 5
};
