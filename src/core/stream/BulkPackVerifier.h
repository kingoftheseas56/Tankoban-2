#pragma once

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/stream/StreamBulkPlan.h"

class QTimer;
class TorrentClient;

namespace tankostream::stream {

// ---------------------------------------------------------------------------
// detail: pure-logic helpers shared by BulkPackVerifier and StreamPackParser.
// Living in the header so both callers link cleanly without pulling
// BulkPackVerifier.cpp (which drags TorrentClient / libtorrent) into pure
// test targets.
// ---------------------------------------------------------------------------
namespace detail {

inline constexpr qint64 kMinEpisodeVideoBytes        = 100LL * 1024LL * 1024LL;
inline constexpr qint64 kLargeUnclassifiedVideoBytes = 500LL * 1024LL * 1024LL;

inline bool isWhitelistedVideoExtension(const QString& suffix)
{
    const QString ext = suffix.trimmed().toLower();
    return ext == QStringLiteral("mkv")  ||
           ext == QStringLiteral("mp4")  ||
           ext == QStringLiteral("webm") ||
           ext == QStringLiteral("m4v");
}

}  // namespace detail

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
                                          QString* unclassifiedVideoFile = nullptr)
    {
        if (episodeNum) *episodeNum = 0;
        if (fileIndex)  *fileIndex  = -1;
        if (unclassifiedVideoFile) unclassifiedVideoFile->clear();

        const QString  path     = file.value(QStringLiteral("name")).toString();
        const qint64   fileSize = file.value(QStringLiteral("size")).toVariant().toLongLong();
        const int      index    = file.value(QStringLiteral("index")).toInt(-1);
        const QFileInfo info(path);

        if (!detail::isWhitelistedVideoExtension(info.suffix()))
            return false;

        if (fileSize < detail::kMinEpisodeVideoBytes)
            return false;

        static const QRegularExpression kEpisodePattern(
            QStringLiteral("[._\\s]?[Ss](\\d{1,2})[._\\s]?[Ee](\\d{1,3})"),
            QRegularExpression::CaseInsensitiveOption);

        const QRegularExpressionMatch match = kEpisodePattern.match(info.completeBaseName());
        if (!match.hasMatch()) {
            if (fileSize > detail::kLargeUnclassifiedVideoBytes && unclassifiedVideoFile)
                *unclassifiedVideoFile = path;
            return false;
        }

        const int parsedSeason  = match.captured(1).toInt();
        const int parsedEpisode = match.captured(2).toInt();
        if (parsedSeason != seasonNumber)
            return false;

        if (episodeNum) *episodeNum = parsedEpisode;
        if (fileIndex)  *fileIndex  = index;
        return index >= 0 && parsedEpisode > 0;
    }

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
