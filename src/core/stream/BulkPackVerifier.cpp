#include "core/stream/BulkPackVerifier.h"

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"

#include <QFileInfo>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVariant>

#include <algorithm>

namespace tankostream::stream {

namespace {

constexpr qint64 kMinEpisodeVideoBytes = 100ll * 1024ll * 1024ll;
constexpr qint64 kLargeUnclassifiedVideoBytes = 500ll * 1024ll * 1024ll;

bool isWhitelistedVideoExtension(const QString& suffix)
{
    const QString ext = suffix.trimmed().toLower();
    return ext == QStringLiteral("mkv") ||
           ext == QStringLiteral("mp4") ||
           ext == QStringLiteral("webm") ||
           ext == QStringLiteral("m4v");
}

QString packDiagnosticLabel(const BulkSelectionPlan& plan)
{
    if (!plan.preflight.packLabel.trimmed().isEmpty())
        return plan.preflight.packLabel;

    for (const BulkSelectionItem& item : plan.items) {
        if (item.reason != BulkSelectionReason::PackCovered) continue;
        if (!item.choice.packLabel.trimmed().isEmpty()) return item.choice.packLabel;
        if (!item.choice.displayTitle.trimmed().isEmpty()) return item.choice.displayTitle;
    }
    return QStringLiteral("selected season pack");
}

QString episodeListString(const QList<int>& episodeNums)
{
    QStringList parts;
    for (int episodeNum : episodeNums)
        parts.push_back(QStringLiteral("E%1").arg(episodeNum, 2, 10, QLatin1Char('0')));
    return parts.join(QStringLiteral(", "));
}

}  // namespace

BulkPackVerifier::BulkPackVerifier(TorrentClient* client, QObject* parent)
    : QObject(parent)
    , m_client(client)
{
    qRegisterMetaType<tankostream::stream::BulkPackVerificationResult>(
        "tankostream::stream::BulkPackVerificationResult");
}

BulkPackVerifier::~BulkPackVerifier()
{
    cancelInternal(false);
}

void BulkPackVerifier::begin(const BulkSelectionPlan& plan, int seasonNumber)
{
    reset();
    m_plan = plan;
    m_seasonNumber = seasonNumber;
    m_packLabel = packDiagnosticLabel(plan);
    m_running = true;
    m_cancelled = false;

    if (plan.mode == BulkSelectionMode::PerEpisode) {
        QString failureReason;
        BulkPackVerificationResult result = verifyFiles(plan, seasonNumber, {}, &failureReason);
        m_running = false;
        emit verificationComplete(result);
        return;
    }

    if (!m_client || !m_client->engine()) {
        fail(QStringLiteral("Torrent metadata verification cannot start: torrent client unavailable"));
        return;
    }

    QString packMagnet;
    for (const BulkSelectionItem& item : plan.items) {
        if (item.reason != BulkSelectionReason::PackCovered) continue;
        packMagnet = item.choice.magnetUri;
        if (!packMagnet.trimmed().isEmpty()) break;
    }
    if (packMagnet.trimmed().isEmpty()) {
        fail(QStringLiteral("Torrent metadata verification cannot start: pack magnet is empty for %1")
                 .arg(m_packLabel));
        return;
    }

    m_metadataConn = connect(m_client->engine(), &TorrentEngine::metadataReady,
                             this, &BulkPackVerifier::onMetadataReady);
    m_errorConn = connect(m_client->engine(), &TorrentEngine::torrentError,
                          this, &BulkPackVerifier::onTorrentError);

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kMetadataTimeoutMs);
    m_timeoutConn = connect(m_timeout, &QTimer::timeout,
                            this, &BulkPackVerifier::onMetadataTimeout);
    m_timeout->start();

    m_infoHash = makeTorrentKey(m_client->resolveMetadata(packMagnet));
    if (m_infoHash.isEmpty()) {
        fail(QStringLiteral("Torrent metadata verification cannot start: engine rejected pack magnet for %1")
                 .arg(m_packLabel));
    }
}

void BulkPackVerifier::cancel()
{
    cancelInternal(true);
}

bool BulkPackVerifier::matchEpisodeFileForSeason(const QJsonObject& file,
                                                 int seasonNumber,
                                                 int* episodeNum,
                                                 int* fileIndex,
                                                 QString* unclassifiedVideoFile)
{
    if (episodeNum) *episodeNum = 0;
    if (fileIndex) *fileIndex = -1;
    if (unclassifiedVideoFile) unclassifiedVideoFile->clear();

    const QString path = file.value(QStringLiteral("name")).toString();
    const qint64 fileSize = file.value(QStringLiteral("size")).toVariant().toLongLong();
    const int index = file.value(QStringLiteral("index")).toInt(-1);
    const QFileInfo info(path);

    if (!isWhitelistedVideoExtension(info.suffix()))
        return false;

    if (fileSize < kMinEpisodeVideoBytes)
        return false;

    static const QRegularExpression kEpisodePattern(
        QStringLiteral("[._\\s]?[Ss](\\d{1,2})[._\\s]?[Ee](\\d{1,3})"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = kEpisodePattern.match(info.completeBaseName());
    if (!match.hasMatch()) {
        if (fileSize > kLargeUnclassifiedVideoBytes && unclassifiedVideoFile)
            *unclassifiedVideoFile = path;
        return false;
    }

    const int parsedSeason = match.captured(1).toInt();
    const int parsedEpisode = match.captured(2).toInt();
    if (parsedSeason != seasonNumber)
        return false;

    if (episodeNum) *episodeNum = parsedEpisode;
    if (fileIndex) *fileIndex = index;
    return index >= 0 && parsedEpisode > 0;
}

BulkPackVerificationResult BulkPackVerifier::verifyFiles(
    const BulkSelectionPlan& plan,
    int seasonNumber,
    const QJsonArray& files,
    QString* failureReason)
{
    if (failureReason) failureReason->clear();

    BulkPackVerificationResult result;
    result.updatedPlan = plan;

    if (plan.mode == BulkSelectionMode::PerEpisode)
        return result;

    result.filePriorities = QVector<int>(files.size(), 0);

    QHash<int, int> fileIndexByEpisode;
    QSet<int> duplicateEpisodes;
    QSet<int> invalidIndexEpisodes;

    for (int i = 0; i < files.size(); ++i) {
        QJsonObject file = files.at(i).toObject();
        if (!file.contains(QStringLiteral("index")))
            file.insert(QStringLiteral("index"), i);

        int episodeNum = 0;
        int fileIndex = -1;
        QString unclassified;
        if (!matchEpisodeFileForSeason(
                file, seasonNumber, &episodeNum, &fileIndex, &unclassified)) {
            if (!unclassified.isEmpty())
                result.unclassifiedVideoFiles.push_back(unclassified);
            continue;
        }

        if (fileIndex < 0 || fileIndex >= result.filePriorities.size()) {
            invalidIndexEpisodes.insert(episodeNum);
            continue;
        }

        if (fileIndexByEpisode.contains(episodeNum) &&
            fileIndexByEpisode.value(episodeNum) != fileIndex) {
            duplicateEpisodes.insert(episodeNum);
            continue;
        }

        fileIndexByEpisode.insert(episodeNum, fileIndex);
        result.filePriorities[fileIndex] = 4;
    }

    if (!duplicateEpisodes.isEmpty()) {
        QList<int> dupes = duplicateEpisodes.values();
        std::sort(dupes.begin(), dupes.end());
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Pack metadata verification failed for %1: duplicate matched files for %2")
                .arg(packDiagnosticLabel(plan), episodeListString(dupes));
        }
        return result;
    }

    if (!invalidIndexEpisodes.isEmpty()) {
        QList<int> invalid = invalidIndexEpisodes.values();
        std::sort(invalid.begin(), invalid.end());
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Pack metadata verification failed for %1: invalid metadata file indexes for %2")
                .arg(packDiagnosticLabel(plan), episodeListString(invalid));
        }
        return result;
    }

    QList<int> missingEpisodes;
    for (BulkSelectionItem& item : result.updatedPlan.items) {
        if (item.reason != BulkSelectionReason::PackCovered)
            continue;

        const auto indexIt = fileIndexByEpisode.constFind(item.episodeNum);
        if (indexIt == fileIndexByEpisode.cend()) {
            missingEpisodes.push_back(item.episodeNum);
            continue;
        }

        item.choice.fileIndex = indexIt.value();
        result.fileIndexByEpisode.insert(item.episodeNum, indexIt.value());
    }

    if (!missingEpisodes.isEmpty()) {
        std::sort(missingEpisodes.begin(), missingEpisodes.end());
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Pack metadata verification failed for %1: missing %2")
                .arg(packDiagnosticLabel(plan), episodeListString(missingEpisodes));
        }
        return result;
    }

    return result;
}

void BulkPackVerifier::onMetadataReady(const QString& infoHash, const QString& name,
                                       qint64 totalSize, const QJsonArray& files)
{
    Q_UNUSED(name)
    Q_UNUSED(totalSize)

    if (!m_running || m_cancelled)
        return;
    if (m_infoHash.isEmpty())
        return;
    if (makeTorrentKey(infoHash) != m_infoHash)
        return;

    QString failureReason;
    BulkPackVerificationResult result = verifyFiles(
        m_plan, m_seasonNumber, files, &failureReason);

    if (!failureReason.isEmpty()) {
        fail(failureReason);
        return;
    }

    m_running = false;
    disconnectHandlers();
    emit verificationComplete(result);
}

void BulkPackVerifier::onTorrentError(const QString& infoHash, const QString& message)
{
    if (!m_running || m_cancelled)
        return;
    if (m_infoHash.isEmpty())
        return;
    if (makeTorrentKey(infoHash) != m_infoHash)
        return;

    fail(QStringLiteral("Pack metadata verification failed for %1: %2")
             .arg(m_packLabel, message));
}

void BulkPackVerifier::onMetadataTimeout()
{
    if (!m_running || m_cancelled)
        return;

    fail(QStringLiteral("Pack metadata verification timed out after %1 ms for %2")
             .arg(kMetadataTimeoutMs)
             .arg(m_packLabel));
}

void BulkPackVerifier::reset()
{
    cancelInternal(false);
    m_plan = {};
    m_infoHash.clear();
    m_packLabel.clear();
    m_seasonNumber = 0;
    m_cancelled = false;
}

void BulkPackVerifier::cancelInternal(bool emitSignal)
{
    const bool wasRunning = m_running && !m_cancelled;
    m_cancelled = true;
    m_running = false;
    disconnectHandlers();

    if (emitSignal && wasRunning)
        emit cancelled();
}

void BulkPackVerifier::fail(const QString& reason)
{
    if (!m_running)
        return;

    m_running = false;
    disconnectHandlers();
    emit verificationFailed(reason);
}

void BulkPackVerifier::disconnectHandlers()
{
    QObject::disconnect(m_metadataConn);
    QObject::disconnect(m_errorConn);
    QObject::disconnect(m_timeoutConn);
    m_metadataConn = {};
    m_errorConn = {};
    m_timeoutConn = {};

    if (m_timeout) {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }
}

}  // namespace tankostream::stream
