#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>

struct ShowInfo {
    QString showName;
    QString showPath;
    int episodeCount = 0;
    qint64 totalSizeBytes = 0;
};
Q_DECLARE_METATYPE(ShowInfo)

class VideosScanner : public QObject {
    Q_OBJECT
public:
    explicit VideosScanner(QObject* parent = nullptr);

public slots:
    void scan(const QStringList& rootFolders);

signals:
    void showFound(const ShowInfo& show);
    void scanFinished(const QList<ShowInfo>& allShows);

private:
    static const QStringList VIDEO_EXTS;
};
