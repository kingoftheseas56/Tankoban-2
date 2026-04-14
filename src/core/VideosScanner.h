#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>

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
};
