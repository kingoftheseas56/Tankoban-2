#pragma once

#include <QWidget>
#include <QPropertyAnimation>
#include <QString>

// PLAYER_UX_FIX Phase 2.3 — centered overlay that surfaces player-lifecycle
// state the user would otherwise experience as a blank canvas: the "opening"
// window between sendOpen and first-frame, plus the "buffering" state during
// HTTP stall on stream URLs. Two modes (Loading / Buffering) render the same
// pill-shaped background with different text; transitions fade in/out via
// QPropertyAnimation on the opacity property. Mouse events pass through —
// user controls underneath remain responsive.
//
// Visual style matches VolumeHud / CenterFlash: dark translucent pill,
// hairline white border, off-white text. No color, no emoji per
// feedback_no_color_no_emoji.
class LoadingOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    // Wake 2026-04-24 — simple-loading-bar redesign per Hemanth directive
    // ("UI wise it's suffocating to see buffering, finding probe, resolving
    // meta data. just a simple loading bar would do"). `phase` drives an
    // indeterminate sweep inside the bar via QPropertyAnimation on a 1.5s
    // infinite loop. Replaces the 6-stage text-pill that rendered
    // verbose state transitions.
    Q_PROPERTY(qreal phase READ phase WRITE setPhase)

public:
    explicit LoadingOverlay(QWidget* parent = nullptr);

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — classified open-pipeline
    // stages. Replaces the binary Loading/Buffering model so the user
    // sees which sub-step of the open is active during the 10-70s
    // window that used to be a silent "Loading — <filename>" pill.
    // Driven by SidecarProcess classified events (Phase 1.2):
    //   Opening            → state_changed{opening}, sendOpen ack'd
    //   Probing            → probe_start (probe_file in flight)
    //   OpeningDecoder     → decoder_open_start (avcodec_open2 in flight)
    //   DecodingFirstFrame → first_decoder_receive (first frame decoded,
    //                        presentation imminent; Rule-14 pick — NOT
    //                        first_packet_read, which only signals
    //                        demuxer/IO motion and can stall on decoder
    //                        back-pressure)
    //   Buffering          → buffering (PLAYER_UX_FIX Phase 2.2 HTTP
    //                        stall on stream URLs, post-first-frame
    //                        recovery path)
    //   TakingLonger       → Phase 2.2 30s watchdog fires from VideoPlayer
    //                        when firstFrame hasn't arrived; non-terminal
    //                        (user can still close; further progress
    //                        events still resolve the stage cleanly)
    enum class Stage {
        Opening,
        Probing,
        OpeningDecoder,
        DecodingFirstFrame,
        Buffering,
        TakingLonger,
    };

    // Set the current stage + optionally update filename. Empty filename
    // keeps the current value (useful for mid-pipeline transitions where
    // the filename was already set by the initial Opening stage). Calling
    // with a new filename at any time refreshes the text. Idempotent-ish:
    // same-stage + same-filename calls update text in place without
    // re-fade.
    void setStage(Stage stage, const QString& filename = {});

    // Backward-compat shortcuts preserved for existing call sites that
    // predate Phase 2.1. Both forward to setStage internally. New code
    // should prefer setStage directly.
    // Connect to VideoPlayer::playerOpeningStarted(QString). Fades in
    // with "Opening source — <basename>" text. Shortcut for
    // setStage(Stage::Opening, filename).
    void showLoading(const QString& filename);

    // Connect to SidecarProcess::bufferingStarted(). Shortcut for
    // setStage(Stage::Buffering). Preserves existing Phase 2.2 wiring
    // semantics; can mutate in place without re-fade if visible.
    void showBuffering();

    // PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — structured cache-pause
    // progress update. Called from SidecarProcess::cacheStateChanged at
    // 2 Hz during an active HTTP stall. Upgrades the Buffering stage text
    // from the opaque "Buffering…" to "Buffering — N% (resumes in ~Xs)"
    // where N = clamp(bytesAhead / 1 MiB * 100, 0, 99) and Xs = etaResume.
    // Sentinel handling:
    //   etaResumeSec < 0  → text reads "(time unknown)"
    //   cacheDurationSec < 0 → duration clause omitted
    // In-place text update only — does NOT fade; safe to call at high
    // rate. No-op when current stage != Buffering (e.g. cleared by the
    // bufferingEnded → dismiss race at stall boundary).
    void setCacheProgress(qint64 bytesAhead,
                          qint64 inputRateBps,
                          double etaResumeSec,
                          double cacheDurationSec);

    // Reset cached progress fields. Called on bufferingEnded / dismiss so
    // a subsequent bufferingStarted starts from the plain "Buffering…"
    // text rather than stale % + ETA from the prior stall.
    void clearCacheProgress();

    // STREAM_STALL_UX_FIX Batch 2 — stream-engine stall diagnostic. Parallel
    // to setCacheProgress; surfaces the blocked piece index + swarm-peer
    // availability when the libtorrent-layer watchdog detects a mid-
    // playback stall (separate from the sidecar HTTP stall source that
    // feeds setCacheProgress). Pushed from VideoPlayer::setStreamStallInfo
    // at ~1 Hz while StreamEngineStats.stalled is true. Text pattern:
    //   peerHaveCount  > 0 → "Buffering — waiting for piece N (K peers have it)"
    //   peerHaveCount == 0 → "Buffering — waiting for piece N (no peers have it yet)"
    //   peerHaveCount  < 0 → "Buffering — waiting for piece N" (sentinel-unknown)
    // When both cache-progress AND stall-diagnostic are valid, the stall-
    // diagnostic wording wins — swarm state is more actionable than the
    // sidecar's internal ring-buffer percentage.
    void setStallDiagnostic(int piece, int peerHaveCount);

    // Reset cached stall fields. Called on dismiss so a subsequent stall
    // starts clean.
    void clearStallDiagnostic();

    // Connect to any of: SidecarProcess::firstFrame, bufferingEnded,
    // VideoPlayer::playerIdle. Fades out + hides on completion.
    // Idempotent — safe to call when already dismissed.
    void dismiss();

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o);

    qreal phase() const { return m_phase; }
    void setPhase(qreal p);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void fadeIn();
    void fadeOut();
    void repositionCentered();
    // Wake 2026-04-24 — textForStage() body retained as #if 0 for reference
    // but the declaration is kept so the linker finds a symbol. New
    // paintEvent ignores it entirely; every Stage produces the indeterminate
    // bar. See LoadingOverlay.cpp for the rationale block.
    QString textForStage() const;

    Stage                m_stage    = Stage::Opening;  // Unused until first show*/setStage.
    bool                 m_visible  = false;           // Drives paint guard + fadeIn-vs-mutate-in-place.
    QString              m_filename;
    qreal                m_opacity  = 0.0;
    qreal                m_phase    = 0.0;             // 0..1 indeterminate sweep phase
    QPropertyAnimation*  m_fadeAnim = nullptr;
    QPropertyAnimation*  m_phaseAnim = nullptr;

    // PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — cache-pause progress cache.
    // Populated only while a cache_state event is fresh; textForStage uses
    // these to enrich Stage::Buffering text. m_cacheValid is the gate —
    // any sentinel branch leaves it true but with fields flagged.
    bool                 m_cacheValid         = false;
    qint64               m_cacheBytesAhead    = 0;
    qint64               m_cacheInputRateBps  = 0;
    double               m_cacheEtaResumeSec  = -1.0;  // -1.0 = unknown
    double               m_cacheDurationSec   = -1.0;  // -1.0 = unknown

    // STREAM_STALL_UX_FIX Batch 2 — stream-engine stall diagnostic cache.
    // m_stallValid gates textForStage's enrichment branch; m_stallPeerHave
    // < 0 is a sentinel meaning peer count wasn't captured (StreamEngine
    // returns -1 for unknown hash / pre-metadata).
    bool                 m_stallValid         = false;
    int                  m_stallPiece         = -1;
    int                  m_stallPeerHaveCount = -1;

    // STREAM_STALL_RECOVERY_UX investigation 2026-04-22 — Direction C. Gates
    // paintEvent's [STALL_DEBUG] log to once-per-stall-cycle so long stalls
    // don't spam the log. Reset on setStage(Buffering) transition + dismiss.
    bool                 m_stallPaintLogged   = false;
};
