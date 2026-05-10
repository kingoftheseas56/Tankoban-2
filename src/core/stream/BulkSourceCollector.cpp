#include "core/stream/BulkSourceCollector.h"

#include "core/stream/StreamAggregator.h"
#include "core/stream/addon/AddonRegistry.h"
#include "core/stream/addon/StreamInfo.h"

#include <QDateTime>
#include <QMetaType>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace tankostream::stream {

struct BulkSourceCollector::InFlightEpisode {
    int episodeNum = 0;
    int addonsQueried = 0;
    StreamAggregator* aggregator = nullptr;
    QTimer* timeout = nullptr;
    QSet<QString> erroredAddonIds;
    QMetaObject::Connection readyConn;
    QMetaObject::Connection errorConn;
    QMetaObject::Connection timeoutConn;
};

BulkSourceCollector::BulkSourceCollector(tankostream::addon::AddonRegistry* registry,
                                         QObject* parent,
                                         int concurrencyCap)
    : QObject(parent)
    , m_registry(registry)
    , m_concurrencyCap(std::max(1, concurrencyCap))
{
    qRegisterMetaType<tankostream::stream::BulkSourceCollectionResult>(
        "tankostream::stream::BulkSourceCollectionResult");
    qRegisterMetaType<tankostream::stream::BulkSourceCollectionPayload>(
        "tankostream::stream::BulkSourceCollectionPayload");
}

BulkSourceCollector::~BulkSourceCollector()
{
    cancelInternal(false);
}

void BulkSourceCollector::begin(const BulkPlanInput& input)
{
    reset();
    m_input = input;
    m_pendingEpisodes = input.episodes;
    m_episodesTotal = m_pendingEpisodes.size();
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_running = true;
    m_cancelled = false;

    if (m_episodesTotal == 0) {
        maybeComplete();
        return;
    }

    startNextEpisodes();
}

void BulkSourceCollector::cancel()
{
    cancelInternal(true);
}

void BulkSourceCollector::reset()
{
    cancelInternal(false);
    m_input = {};
    m_pendingEpisodes.clear();
    m_resultsByEpisode.clear();
    m_startedAtMs = 0;
    m_episodesTotal = 0;
    m_running = false;
    m_cancelled = false;
}

void BulkSourceCollector::cancelInternal(bool emitSignal)
{
    const bool wasRunning = m_running && !m_cancelled;
    m_cancelled = true;
    m_running = false;
    m_pendingEpisodes.clear();

    const auto inFlight = m_inFlightByEpisode;
    m_inFlightByEpisode.clear();
    for (InFlightEpisode* episode : inFlight) {
        if (!episode) continue;
        if (episode->timeout) {
            QObject::disconnect(episode->timeoutConn);
            episode->timeout->stop();
            episode->timeout->deleteLater();
        }
        if (episode->aggregator) {
            QObject::disconnect(episode->readyConn);
            QObject::disconnect(episode->errorConn);
            episode->aggregator->deleteLater();
        }
        delete episode;
    }

    if (emitSignal && wasRunning)
        emit cancelled();
}

void BulkSourceCollector::startNextEpisodes()
{
    if (!m_running || m_cancelled)
        return;

    while (!m_pendingEpisodes.isEmpty()
           && m_inFlightByEpisode.size() < m_concurrencyCap) {
        const BulkPlanEpisodeInput episode = m_pendingEpisodes.takeFirst();
        startEpisode(episode);
    }
}

void BulkSourceCollector::startEpisode(const BulkPlanEpisodeInput& episode)
{
    auto* inFlight = new InFlightEpisode;
    inFlight->episodeNum = episode.episode;

    StreamLoadRequest request;
    request.type = QStringLiteral("series");
    request.id = streamRequestIdForEpisode(episode);

    if (m_registry) {
        inFlight->addonsQueried =
            m_registry->findByResourceType(QStringLiteral("stream"), request.type).size();
    }

    auto* aggregator = new StreamAggregator(m_registry, this);
    auto* timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(kPerEpisodeTimeoutMs);

    inFlight->aggregator = aggregator;
    inFlight->timeout = timeout;
    m_inFlightByEpisode.insert(inFlight->episodeNum, inFlight);

    inFlight->readyConn = connect(
        aggregator, &StreamAggregator::streamsReady, this,
        [this, episodeNum = inFlight->episodeNum](
            const QList<tankostream::addon::Stream>& streams,
            const QHash<QString, QString>& addonsById) {
            finishEpisode(episodeNum, streams, addonsById, false);
        });

    inFlight->errorConn = connect(
        aggregator, &StreamAggregator::streamError, this,
        [this, episodeNum = inFlight->episodeNum](
            const QString& addonId, const QString& /*message*/) {
            if (auto* episode = m_inFlightByEpisode.value(episodeNum, nullptr))
                episode->erroredAddonIds.insert(addonId);
        });

    inFlight->timeoutConn = connect(
        timeout, &QTimer::timeout, this,
        [this, episodeNum = inFlight->episodeNum]() {
            finishEpisode(episodeNum, {}, {}, true);
        });

    timeout->start();
    aggregator->load(request);
}

void BulkSourceCollector::finishEpisode(
    int episodeNum,
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>& addonsById,
    bool timedOut)
{
    if (!m_running || m_cancelled)
        return;

    InFlightEpisode* episode = m_inFlightByEpisode.take(episodeNum);
    if (!episode)
        return;

    QObject::disconnect(episode->readyConn);
    QObject::disconnect(episode->errorConn);
    QObject::disconnect(episode->timeoutConn);
    if (episode->timeout) {
        episode->timeout->stop();
        episode->timeout->deleteLater();
    }
    if (episode->aggregator)
        episode->aggregator->deleteLater();

    BulkSourceCollectionResult result;
    result.episodeNum = episodeNum;
    result.timedOut = timedOut;
    result.addonsErrored = episode->erroredAddonIds.size();
    result.addonsQueried = !addonsById.isEmpty()
        ? addonsById.size()
        : episode->addonsQueried;

    if (!timedOut) {
        result.choices = buildPickerChoices(streams, addonsById);

        QSet<QString> addonsWithChoices;
        for (const StreamPickerChoice& choice : result.choices) {
            if (!choice.addonId.isEmpty())
                addonsWithChoices.insert(choice.addonId);
        }
        result.addonsEmpty = std::max(
            0,
            result.addonsQueried - result.addonsErrored
                - static_cast<int>(addonsWithChoices.size()));
    } else {
        result.addonsEmpty = 0;
    }

    m_resultsByEpisode.insert(episodeNum, result);
    delete episode;

    emit progressTick(m_resultsByEpisode.size(), m_episodesTotal);

    startNextEpisodes();
    maybeComplete();
}

void BulkSourceCollector::maybeComplete()
{
    if (!m_running || m_cancelled)
        return;
    if (m_resultsByEpisode.size() < m_episodesTotal)
        return;
    if (!m_inFlightByEpisode.isEmpty() || !m_pendingEpisodes.isEmpty())
        return;

    BulkSourceCollectionPayload payload;
    payload.byEpisode = m_resultsByEpisode;
    payload.elapsedMs = m_startedAtMs > 0
        ? QDateTime::currentMSecsSinceEpoch() - m_startedAtMs
        : 0;
    payload.cancelled = false;

    m_running = false;
    emit collectionComplete(payload);
}

QString BulkSourceCollector::streamRequestIdForEpisode(
    const BulkPlanEpisodeInput& episode) const
{
    const int season = episode.season > 0 ? episode.season : m_input.seasonNumber;
    return QStringLiteral("%1:%2:%3")
        .arg(m_input.seriesId)
        .arg(season)
        .arg(episode.episode);
}

}  // namespace tankostream::stream
