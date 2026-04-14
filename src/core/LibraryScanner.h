#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>

struct SeriesInfo {
    struct FileEntry {
        QString path;
        int pageCount = 0;
        qint64 mtimeMs = 0;
    };

    QString seriesName;
    QString seriesPath;
    QString coverThumbPath;
    int fileCount = 0;
    qint64 newestMtimeMs = 0;
    QList<FileEntry> files;
};
Q_DECLARE_METATYPE(SeriesInfo)

class LibraryScanner : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(const QString& thumbsDir, QObject* parent = nullptr);

public slots:
    void scan(const QStringList& rootFolders);

signals:
    void seriesFound(const SeriesInfo& series);
    void scanFinished(const QList<SeriesInfo>& allSeries);

private:
    QByteArray extractCoverFromCbz(const QString& cbzPath);
    int countPagesInCbz(const QString& cbzPath);
    QString saveThumbnail(const QByteArray& imageData, const QString& seriesPath);

    QString m_thumbsDir;
};
