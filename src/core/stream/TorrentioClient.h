#pragma once

#include <QObject>
#include <QList>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

struct TorrentioStream {
    QString title;          // raw multi-line title from Torrentio
    QString magnetUri;      // built from infoHash + trackers
    QString infoHash;       // 40-char hex lowercase
    qint64  sizeBytes = 0;  // parsed from emoji marker
    int     seeders = 0;    // parsed from emoji marker
    QString quality;        // "1080p / BluRay / HEVC / DDP 5.1"
    QString trackerSource;  // release name (first line of title)
    QString tracker;        // tracker name from title
    QString languages;      // flag emojis line
    QString fileNameHint;   // from behaviorHints or parsed
    int     fileIndex = -1; // pre-selected video file index, -1 if unknown
};

class TorrentioClient : public QObject
{
    Q_OBJECT

public:
    explicit TorrentioClient(QObject* parent = nullptr);

    void fetchStreams(const QString& imdbId, const QString& mediaType,
                     int season = 1, int episode = 1);

signals:
    void streamsReady(const QList<TorrentioStream>& streams);
    void streamsError(const QString& message);

private:
    void onReply(QNetworkReply* reply);

    static TorrentioStream parseStream(const QJsonObject& streamObj);
    static QString parseQuality(const QString& rawTitle);
    static QString buildMagnet(const QString& infoHash, const QString& title,
                               const QStringList& trackers);
    static qint64 parseSize(const QString& sizeStr);

    QNetworkAccessManager* m_nam;
};
