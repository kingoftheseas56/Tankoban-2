#include "StreamContinueStrip.h"

#include "core/CoreBridge.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/StreamProgress.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QFile>
#include <QHash>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>

StreamContinueStrip::StreamContinueStrip(CoreBridge* bridge, StreamLibrary* library,
                                         tankostream::stream::MetaAggregator* meta,
                                         QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_library(library)
    , m_meta(meta)
{
    buildUI();

    // Phase 2 Batch 2.2: the next-up pipeline. refresh() queues async
    // fetches; the aggregator's seriesMetaReady emits back into us with the
    // episode list, which we feed through StreamProgress::nextUnwatchedEpisode
    // to pick the card target.
    if (m_meta) {
        connect(m_meta, &tankostream::stream::MetaAggregator::seriesMetaReady,
                this, &StreamContinueStrip::onSeriesMetaReady);
        connect(m_meta, &tankostream::stream::MetaAggregator::seriesMetaError,
                this, &StreamContinueStrip::onSeriesMetaError);
    }
}

void StreamContinueStrip::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_group = new QGroupBox(this);
    m_group->setFlat(true);
    m_group->setStyleSheet("QGroupBox { border: none; margin: 0; padding: 0; }");

    auto* groupLayout = new QVBoxLayout(m_group);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(4);

    auto* label = new QLabel("CONTINUE WATCHING", m_group);
    label->setStyleSheet(
        "color: rgba(255,255,255,0.55); font-size: 12px; font-weight: bold; letter-spacing: 1px;");
    groupLayout->addWidget(label);

    m_strip = new TileStrip(m_group);
    m_strip->setMode("continue");
    groupLayout->addWidget(m_strip);

    m_group->hide();
    root->addWidget(m_group);
}

void StreamContinueStrip::refresh()
{
    m_strip->clear();
    m_pendingNextUps.clear();
    m_nextPendingIdx = 0;
    m_inFlightImdb.clear();

    // Phase 2 Batch 2.2: next-unwatched-episode cache is refresh-scoped until
    // a CoreBridge::progressUpdated signal exists. Clearing at the top of
    // refresh ensures a user who just finished an episode and re-opens the
    // Stream mode sees the strip re-computed against fresh allProgress.
    StreamProgress::clearNextUnwatchedCache();

    QJsonObject allProgress = m_bridge->allProgress("stream");
    if (allProgress.isEmpty()) {
        m_group->hide();
        return;
    }

    // Collect most-recent state per show. Unlike pre-2.2, we DON'T exclude
    // finished episodes — those become the entry point for next-up lookup
    // via m_meta->fetchSeriesMeta + StreamProgress::nextUnwatchedEpisode.
    struct MostRecent {
        QString epKey;
        int     season       = 0;
        int     episode      = 0;
        double  positionSec  = 0;
        double  durationSec  = 0;
        double  percent      = 0;
        bool    finished     = false;
        qint64  updatedAt    = 0;
    };
    QHash<QString, MostRecent> mostRecent;

    for (auto it = allProgress.begin(); it != allProgress.end(); ++it) {
        const QString key = it.key();
        if (!key.startsWith("stream:"))
            continue;

        const QJsonObject state = it->toObject();
        const double pos = state.value("positionSec").toDouble(0);
        if (pos < MIN_POSITION_SEC)
            continue;

        const qint64 updated = state.value("updatedAt").toInteger(0);

        const QStringList parts = key.split(':');
        QString imdbId;
        int season = 0, episode = 0;
        if (parts.size() >= 2)
            imdbId = parts[1];
        if (parts.size() >= 4) {
            season  = parts[2].mid(1).toInt();   // "s1" → 1
            episode = parts[3].mid(1).toInt();   // "e3" → 3
        }

        if (imdbId.isEmpty() || !m_library->has(imdbId))
            continue;

        const auto existing = mostRecent.find(imdbId);
        if (existing == mostRecent.end() || updated > existing->updatedAt) {
            MostRecent entry;
            entry.epKey       = key;
            entry.season      = season;
            entry.episode     = episode;
            entry.positionSec = pos;
            entry.durationSec = state.value("durationSec").toDouble(0);
            entry.percent     = StreamProgress::percent(state);
            entry.finished    = StreamProgress::isFinished(state);
            entry.updatedAt   = updated;
            mostRecent[imdbId] = entry;
        }
    }

    if (mostRecent.isEmpty()) {
        m_group->hide();
        return;
    }

    // Split into in-progress vs finished. In-progress renders synchronously;
    // finished goes through the async fetch queue to resolve next-unwatched.
    struct InProgress {
        QString imdbId;
        int     season;
        int     episode;
        double  percent;
        qint64  updatedAt;
    };
    QList<InProgress>    inProgress;
    QList<PendingNextUp> needsFetch;

    for (auto it = mostRecent.constBegin(); it != mostRecent.constEnd(); ++it) {
        const auto& e = it.value();
        if (e.finished) {
            PendingNextUp p;
            p.imdbId      = it.key();
            p.updatedAt   = e.updatedAt;
            p.prevSeason  = e.season;
            p.prevEpisode = e.episode;
            needsFetch.append(p);
        } else {
            inProgress.append({it.key(), e.season, e.episode, e.percent, e.updatedAt});
        }
    }

    std::sort(inProgress.begin(), inProgress.end(),
        [](const InProgress& a, const InProgress& b) {
            return a.updatedAt > b.updatedAt;
        });
    std::sort(needsFetch.begin(), needsFetch.end(),
        [](const PendingNextUp& a, const PendingNextUp& b) {
            return a.updatedAt > b.updatedAt;
        });

    if (inProgress.isEmpty() && needsFetch.isEmpty()) {
        m_group->hide();
        return;
    }
    m_group->show();

    // Poster dir shared by in-progress + next-up render paths.
    const QString posterDir = QStandardPaths::writableLocation(
                                  QStandardPaths::GenericDataLocation)
                              + "/Tankoban/data/stream_posters";

    // Render in-progress cards immediately.
    int rendered = 0;
    for (const InProgress& item : inProgress) {
        if (rendered >= MAX_ITEMS) break;
        QString posterPath = posterDir + "/" + item.imdbId + ".jpg";
        if (!QFile::exists(posterPath)) posterPath.clear();
        renderInProgressCard(item.imdbId, item.season, item.episode,
                             item.percent, posterPath);
        ++rendered;
    }

    // Queue next-up fetches for the remaining slots.
    if (m_meta) {
        const int slotsRemaining = MAX_ITEMS - rendered;
        if (slotsRemaining > 0 && !needsFetch.isEmpty()) {
            m_pendingNextUps = needsFetch.mid(0, slotsRemaining);
            processNextFetch();
        }
    }
    // If m_meta is null, finished-most-recent series are effectively filtered
    // out — matches pre-2.2 behavior for that degenerate ctor path.
}

void StreamContinueStrip::processNextFetch()
{
    if (!m_inFlightImdb.isEmpty()) return;
    if (m_nextPendingIdx >= m_pendingNextUps.size()) return;

    m_inFlightImdb = m_pendingNextUps[m_nextPendingIdx].imdbId;
    // Serialized: MetaAggregator::fetchSeriesMeta has a single-slot pending
    // imdb state (m_seriesPendingImdb) and concurrent calls clobber it,
    // dropping seriesMetaReady emissions for all but one. Until that bug is
    // fixed at the aggregator level, we fan out one at a time. Cache-hit
    // path emits seriesMetaReady synchronously, so warm-cache refreshes
    // chain through the queue within a single call stack.
    m_meta->fetchSeriesMeta(m_inFlightImdb);
}

void StreamContinueStrip::onSeriesMetaReady(
    const QString& imdbId,
    const QMap<int, QList<tankostream::stream::StreamEpisode>>& seasons)
{
    if (imdbId != m_inFlightImdb) return;   // not our pending fetch

    const int idx = m_nextPendingIdx;
    ++m_nextPendingIdx;
    m_inFlightImdb.clear();

    // Flatten seasons → (season, episode) tuples in ascending order.
    QList<QPair<int, int>> episodesInOrder;
    for (auto it = seasons.constBegin(); it != seasons.constEnd(); ++it) {
        for (const auto& ep : it.value()) {
            episodesInOrder.append({it.key(), ep.episode});
        }
    }
    std::sort(episodesInOrder.begin(), episodesInOrder.end());

    // Re-read allProgress; the user may have finished another episode
    // between refresh() start and this async callback landing.
    const QJsonObject allProgress = m_bridge->allProgress("stream");
    const QPair<int, int> next = StreamProgress::nextUnwatchedEpisode(
        imdbId, episodesInOrder, allProgress);

    // next.first > 0 → real (season, episode); {0, 0} → all watched, drop
    // this series from the strip.
    if (next.first > 0 && next.second > 0
        && idx < m_pendingNextUps.size()) {
        const QString posterDir = QStandardPaths::writableLocation(
                                      QStandardPaths::GenericDataLocation)
                                  + "/Tankoban/data/stream_posters";
        QString posterPath = posterDir + "/" + imdbId + ".jpg";
        if (!QFile::exists(posterPath)) posterPath.clear();
        renderNextUpCard(imdbId, next.first, next.second, posterPath);
    }

    processNextFetch();
}

void StreamContinueStrip::onSeriesMetaError(const QString& imdbId,
                                             const QString& /*message*/)
{
    if (imdbId != m_inFlightImdb) return;
    ++m_nextPendingIdx;
    m_inFlightImdb.clear();
    // Silent skip — if meta fetch fails, that series simply doesn't get a
    // next-up card this refresh. Cache stays empty for it, retry on next
    // refresh.
    processNextFetch();
}

void StreamContinueStrip::renderInProgressCard(const QString& imdbId,
                                                int season, int episode,
                                                double percent,
                                                const QString& posterPath)
{
    const StreamLibraryEntry entry = m_library->get(imdbId);

    QString subtitle;
    if (season > 0 && episode > 0)
        subtitle = QString("S%1E%2").arg(season, 2, 10, QChar('0'))
                                     .arg(episode, 2, 10, QChar('0'));
    else
        subtitle = "Movie";

    auto* card = new TileCard(posterPath, entry.name, subtitle);
    card->setProperty("imdbId", imdbId);
    card->setProperty("season", season);
    card->setProperty("episode", episode);

    const int pctInt = static_cast<int>(percent);
    // pageBadge dropped — the episode code "SxxExx" was duplicated on the
    // thumbnail and in the subtitle label beneath. Keep the subtitle.
    card->setBadges(percent / 100.0, QString(),
                    QString::number(pctInt) + "%", "reading");

    connect(card, &TileCard::clicked, this, [this, card]() {
        const QString imdb = card->property("imdbId").toString();
        const int s = card->property("season").toInt();
        const int e = card->property("episode").toInt();
        if (!imdb.isEmpty())
            emit playRequested(imdb, s, e);
    });

    m_strip->addTile(card);
}

void StreamContinueStrip::renderNextUpCard(const QString& imdbId,
                                            int season, int episode,
                                            const QString& posterPath)
{
    const StreamLibraryEntry entry = m_library->get(imdbId);

    const QString sxxexx = QString("S%1E%2")
                              .arg(season, 2, 10, QChar('0'))
                              .arg(episode, 2, 10, QChar('0'));
    // "Next · S01E02" conveys the TODO's "Continue with Episode N" hint in
    // a single subtitle line without needing a new TileCard badge slot.
    const QString subtitle = "Next \u00B7 " + sxxexx;

    auto* card = new TileCard(posterPath, entry.name, subtitle);
    card->setProperty("imdbId", imdbId);
    card->setProperty("season", season);
    card->setProperty("episode", episode);
    // 0% progress + no "reading" status differentiates this visually from
    // the in-progress card shape (no progress bar fill, no "N%" pill).
    // pageBadge dropped — "Next · SxxExx" already in the subtitle label.
    card->setBadges(0.0, QString(), QString(), QString());

    connect(card, &TileCard::clicked, this, [this, card]() {
        const QString imdb = card->property("imdbId").toString();
        const int s = card->property("season").toInt();
        const int e = card->property("episode").toInt();
        if (!imdb.isEmpty())
            emit playRequested(imdb, s, e);
    });

    m_strip->addTile(card);
}
