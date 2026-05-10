#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/stream/StreamBulkPlan.h"

class QTimer;
class TorrentClient;

namespace tankostream::stream {

struct BulkPackVerificationResult {
    BulkSelectionPlan updatedPlan;
    QVector<int> filePriorities;
    QHash<int, int> fileIndexByEpisode;
    QStringList unclassifiedVideoFiles;
};

class BulkPackVerifier : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMetadataTimeoutMs = 60 * 1000;

    explicit BulkPackVerifier(TorrentClient* client, QObject* parent = nullptr);
    ~BulkPackVerifier() override;

    void begin(const BulkSelectionPlan& plan, int seasonNumber);
    void cancel();

    static bool matchEpisodeFileForSeason(const QJsonObject& file,
                                          int seasonNumber,
                                          int* episodeNum,
                                          int* fileIndex,
                                          QString* unclassifiedVideoFile = nullptr);

    static BulkPackVerificationResult verifyFiles(
        const BulkSelectionPlan& plan,
        int seasonNumber,
        const QJsonArray& files,
        QString* failureReason = nullptr);

signals:
    void verificationComplete(const tankostream::stream::BulkPackVerificationResult& result);
    void verificationFailed(const QString& reason);
    void cancelled();

private slots:
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onTorrentError(const QString& infoHash, const QString& message);
    void onMetadataTimeout();

private:
    void reset();
    void cancelInternal(bool emitSignal);
    void fail(const QString& reason);
    void disconnectHandlers();

    TorrentClient* m_client = nullptr;
    QTimer* m_timeout = nullptr;
    QMetaObject::Connection m_metadataConn;
    QMetaObject::Connection m_errorConn;
    QMetaObject::Connection m_timeoutConn;
    BulkSelectionPlan m_plan;
    QString m_infoHash;
    QString m_packLabel;
    int m_seasonNumber = 0;
    bool m_running = false;
    bool m_cancelled = false;
};

}  // namespace tankostream::stream

Q_DECLARE_METATYPE(tankostream::stream::BulkPackVerificationResult)
