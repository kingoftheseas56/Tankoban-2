#pragma once

#include <QGroupBox>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QString>
#include <QWidget>

#include "core/stream/MetaAggregator.h"

class CoreBridge;
class StreamLibrary;
class TileStrip;

class StreamContinueStrip : public QWidget
{
    Q_OBJECT

public:
    // Phase 2 Batch 2.2: MetaAggregator is required to resolve next-unwatched
    // episodes for series whose most-recent progress entry is finished. The
    // parameter is optional for ctor-compat with legacy tests; when null,
    // next-up auto-advance is disabled (series with all-finished most-recent
    // just drop from the strip, matching pre-2.2 behavior).
    explicit StreamContinueStrip(CoreBridge* bridge, StreamLibrary* library,
                                 tankostream::stream::MetaAggregator* meta,
                                 QWidget* parent = nullptr);

    void refresh();

signals:
    void playRequested(const QString& imdbId, int season, int episode);

private:
    void buildUI();
    void onSeriesMetaReady(const QString& imdbId,
                           const QMap<int, QList<tankostream::stream::StreamEpisode>>& seasons);
    void onSeriesMetaError(const QString& imdbId, const QString& message);
    void processNextFetch();

    // Phase 2 Batch 2.2: state held between refresh() and the async
    // seriesMetaReady callbacks that resolve next-up episodes.
    struct PendingNextUp {
        QString imdbId;
        qint64  updatedAt   = 0;     // finished-episode's updatedAt — sort key
        int     prevSeason  = 0;     // for the "Continue after S1E5" hint
        int     prevEpisode = 0;
    };

    void renderInProgressCard(const QString& imdbId, int season, int episode,
                              double percent, const QString& posterPath);
    void renderNextUpCard(const QString& imdbId, int season, int episode,
                          const QString& posterPath);

    CoreBridge*                          m_bridge;
    StreamLibrary*                       m_library;
    tankostream::stream::MetaAggregator* m_meta = nullptr;

    QGroupBox* m_group = nullptr;
    TileStrip* m_strip = nullptr;

    QList<PendingNextUp> m_pendingNextUps;   // sorted by updatedAt DESC
    int                  m_nextPendingIdx = 0;
    QString              m_inFlightImdb;

    static constexpr int MAX_ITEMS = 20;
    static constexpr double MIN_POSITION_SEC = 10.0;
};
