// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 1.1 (STREAM_LIFECYCLE)
// Date: 2026-04-16
// References consulted:
//   - STREAM_LIFECYCLE_FIX_TODO.md:70
//   - agents/audits/tankostream_session_lifecycle_2026-04-15.md:13
//   - src/ui/pages/StreamPage.h:260
//   - src/ui/pages/StreamPage.h:295
//   - src/ui/pages/StreamPage.cpp:1475
//   - src/ui/pages/StreamPage.cpp:1885
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================

// Scope:
//   Introduce PlaybackSession + generation helpers only. Do not migrate
//   _currentEpKey, m_pendingPlay, m_nextPrefetch, m_nearEndCrossed,
//   m_nextShortcutPending, m_lastDeadlineUpdateMs, or m_seekRetryState in
//   this batch. Later batches can migrate call sites behind this API.
//
// Concrete from current src:
//   - PendingPlay and NextEpisodePrefetch are nested structs in StreamPage.
//   - resetNextEpisodePrefetch() already centralizes overlay timer and
//     prefetch aggregator disconnects.
//   - m_seekRetryState is still QObject* identity in current src.
//
// Guess:
//   - A small SeekRetryState struct becomes useful when Batch 1.3 migrates
//     the existing QObject* timer identity to generation-based checks.

#include <memory>
#include <optional>

namespace agent7_stream_lifecycle_batch_1_1 {

// ---------------- StreamPage.h additions ----------------

class StreamPage : public QWidget
{
    Q_OBJECT

private:
    struct PendingPlay {
        QString imdbId;
        QString mediaType;
        int     season  = 0;
        int     episode = 0;
        QString epKey;
        bool    valid   = false;
    };

    struct NextEpisodePrefetch {
        QString imdbId;
        int     season  = 0;
        int     episode = 0;
        QString epKey;
        std::optional<tankostream::stream::StreamPickerChoice> matchedChoice;
        bool    streamsLoaded = false;
    };

    struct SeekRetryState {
        quint64 generation = 0;
        int attempts = 0;
    };

    struct PlaybackSession {
        quint64 generation = 0; // 0 means no active playback session.
        QString epKey;
        PendingPlay pending;
        std::optional<NextEpisodePrefetch> nextPrefetch;
        bool nearEndCrossed = false;
        bool nextShortcutPending = false;
        qint64 lastDeadlineUpdateMs = 0;
        std::shared_ptr<SeekRetryState> seekRetry;

        bool isValid() const
        {
            return generation != 0 && !epKey.isEmpty();
        }
    };

    quint64 beginSession(const QString& epKey, const PendingPlay& pending,
                         const QString& reason = {});
    void resetSession(const QString& reason);
    quint64 currentGeneration() const;
    bool isCurrentGeneration(quint64 generation) const;

    PlaybackSession m_session;
    quint64 m_nextGeneration = 1;

    // Existing members stay during Batch 1.1:
    // PendingPlay m_pendingPlay;
    // std::optional<NextEpisodePrefetch> m_nextPrefetch;
    // bool m_nearEndCrossed = false;
    // bool m_nextShortcutPending = false;
    // qint64 m_lastDeadlineUpdateMs = 0;
    // QObject* m_seekRetryState = nullptr;
};

// ---------------- StreamPage.cpp additions ----------------

quint64 StreamPage::currentGeneration() const
{
    return m_session.generation;
}

bool StreamPage::isCurrentGeneration(quint64 generation) const
{
    return generation != 0 && generation == m_session.generation;
}

void StreamPage::resetSession(const QString& reason)
{
    if (m_nextEpisodeCountdownTimer)
        m_nextEpisodeCountdownTimer->stop();

    if (m_metaAggregator) {
        disconnect(m_metaAggregator,
                   &tankostream::stream::MetaAggregator::seriesMetaReady,
                   this,
                   nullptr);
    }

    if (m_streamAggregator) {
        disconnect(m_streamAggregator,
                   &tankostream::stream::StreamAggregator::streamsReady,
                   this,
                   nullptr);
    }

    m_session = PlaybackSession{};

    qInfo().noquote() << QStringLiteral("[stream-session] reset: reason=%1")
                             .arg(reason.isEmpty() ? QStringLiteral("unspecified")
                                                   : reason);
}

quint64 StreamPage::beginSession(const QString& epKey,
                                 const PendingPlay& pending,
                                 const QString& reason)
{
    resetSession(reason.isEmpty()
                     ? QStringLiteral("beginSession")
                     : QStringLiteral("beginSession:%1").arg(reason));

    if (m_nextGeneration == 0)
        m_nextGeneration = 1;

    m_session.generation = m_nextGeneration++;
    m_session.epKey = epKey;
    m_session.pending = pending;

    qInfo().noquote() << QStringLiteral("[stream-session] begin: gen=%1 epKey=%2")
                             .arg(m_session.generation)
                             .arg(epKey);

    return m_session.generation;
}

} // namespace agent7_stream_lifecycle_batch_1_1
