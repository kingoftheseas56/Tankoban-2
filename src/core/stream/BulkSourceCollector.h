#pragma once

#include <QHash>
#include <QList>
#include <QObject>

#include "core/stream/StreamBulkPlan.h"
#include "ui/pages/stream/StreamSourceChoice.h"

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

struct BulkSourceCollectionResult {
    int episodeNum = 0;
    QList<StreamPickerChoice> choices;
    int addonsQueried = 0;
    int addonsErrored = 0;
    int addonsEmpty = 0;
    bool timedOut = false;
};

struct BulkSourceCollectionPayload {
    QHash<int, BulkSourceCollectionResult> byEpisode;
    qint64 elapsedMs = 0;
    bool cancelled = false;
};

class BulkSourceCollector : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultConcurrencyCap = 4;
    static constexpr int kPerEpisodeTimeoutMs = 30 * 1000;

    explicit BulkSourceCollector(tankostream::addon::AddonRegistry* registry,
                                 QObject* parent = nullptr,
                                 int concurrencyCap = kDefaultConcurrencyCap);
    ~BulkSourceCollector() override;

    void begin(const BulkPlanInput& input);
    void cancel();

signals:
    void progressTick(int episodesResolved, int episodesTotal);
    void collectionComplete(const tankostream::stream::BulkSourceCollectionPayload& payload);
    void cancelled();

private:
    struct InFlightEpisode;

    void reset();
    void cancelInternal(bool emitSignal);
    void startNextEpisodes();
    void startEpisode(const BulkPlanEpisodeInput& episode);
    void finishEpisode(int episodeNum,
                       const QList<tankostream::addon::Stream>& streams,
                       const QHash<QString, QString>& addonsById,
                       bool timedOut);
    void maybeComplete();
    QString streamRequestIdForEpisode(const BulkPlanEpisodeInput& episode) const;

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    int m_concurrencyCap = kDefaultConcurrencyCap;

    BulkPlanInput m_input;
    QList<BulkPlanEpisodeInput> m_pendingEpisodes;
    QHash<int, InFlightEpisode*> m_inFlightByEpisode;
    QHash<int, BulkSourceCollectionResult> m_resultsByEpisode;
    qint64 m_startedAtMs = 0;
    int m_episodesTotal = 0;
    bool m_running = false;
    bool m_cancelled = false;
};

}  // namespace tankostream::stream

Q_DECLARE_METATYPE(tankostream::stream::BulkSourceCollectionResult)
Q_DECLARE_METATYPE(tankostream::stream::BulkSourceCollectionPayload)
