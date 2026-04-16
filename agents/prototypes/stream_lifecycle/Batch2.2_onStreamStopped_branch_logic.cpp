// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 2.2 (STREAM_LIFECYCLE)
// Date: 2026-04-16
// References consulted:
//   - STREAM_LIFECYCLE_FIX_TODO.md:172
//   - agents/audits/tankostream_session_lifecycle_2026-04-15.md:104
//   - src/ui/pages/StreamPage.cpp:103
//   - src/ui/pages/StreamPage.cpp:1633
//   - src/ui/pages/StreamPage.cpp:1913
//   - src/ui/pages/stream/StreamPlayerController.cpp:26
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================

// Scope:
//   Medium-confidence skeleton for StreamPage's consumer side of
//   streamStopped(StopReason). The replacement branch must not clear the
//   just-installed session identity and must not navigate to browse.
//
// Concrete from current src:
//   - StreamPage's constructor connects streamStopped to onStreamStopped().
//   - onStreamStopped() currently clears _currentEpKey, disconnects player
//     signals, resets persistence mode, hides buffer overlay, then showBrowse()
//     unless the next-episode overlay is visible.
//   - onReadyToPlay() already uses wildcard disconnects before reconnecting
//     per-session player signals.
//
// Guess / TODO for Agent 4:
//   - Whether Replacement should hide the old buffer overlay immediately or
//     leave the already-shown new-session overlay untouched depends on the
//     final ordering in onSourceActivated() after Batch 1 migrations.

namespace agent7_stream_lifecycle_batch_2_2 {

// ---------------- StreamPage.h deltas ----------------

class StreamPage : public QWidget
{
    Q_OBJECT

private slots:
    void onStreamStopped(StreamPlayerController::StopReason reason);
};

// ---------------- StreamPage.cpp deltas ----------------

void StreamPage::wireControllerSignals()
{
    connect(m_playerController,
            &StreamPlayerController::streamStopped,
            this,
            &StreamPage::onStreamStopped);
}

void StreamPage::onStreamStopped(StreamPlayerController::StopReason reason)
{
    using StopReason = StreamPlayerController::StopReason;

    auto* player = window() ? window()->findChild<VideoPlayer*>() : nullptr;

    if (reason == StopReason::Replacement) {
        // Old stream is being stopped only because startStream() is about
        // to replace it. Do not clear m_session / _currentEpKey here and
        // do not call showBrowse(); those belong to true end-of-session.
        if (player) {
            disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
            disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
            disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        }

        // TODO(Agent 4): choose one after checking final UX ordering:
        //   A. keep m_bufferOverlay visible because onSourceActivated()
        //      already painted "Buffering stream..." for the new source.
        //   B. hide old overlay here and let onBufferUpdate() show the new
        //      one on first controller tick.
        return;
    }

    // UserEnd and Failure keep the existing user-facing teardown shape.
    // Once Batch 1 migrations land, this should become resetSession(...),
    // not direct property/member clearing.
    setProperty("_currentEpKey", QString());

    if (player) {
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
        disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);
    }

    if (m_bufferOverlay)
        m_bufferOverlay->hide();

    if (m_nextEpisodeOverlay && m_nextEpisodeOverlay->isVisible())
        return;

    showBrowse();
}

} // namespace agent7_stream_lifecycle_batch_2_2
