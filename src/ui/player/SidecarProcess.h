#pragma once

#include "ui/player/IPlayerBackend.h"

#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVector>
#include <atomic>
#include <functional>

class QNetworkAccessManager;
class QTemporaryFile;

// MPV_BACKEND_INTEGRATION P1 2026-04-26 — SubtitleTrackInfo struct +
// Q_DECLARE_METATYPE moved to IPlayerBackend.h so the interface header is
// self-contained. SidecarProcess gets the struct via the include above.

// Manages the ffmpeg_sidecar.exe subprocess. Sends JSON commands on stdin,
// receives JSON events from stdout. Implements IPlayerBackend (P1 2026-04-26)
// so VideoPlayer can route through the abstract interface; MpvBackend (P2)
// implements the same interface against an mpv subprocess.
class SidecarProcess : public IPlayerBackend {
    Q_OBJECT

public:
    explicit SidecarProcess(QObject* parent = nullptr);
    ~SidecarProcess() override;

    void start() override;
    bool isRunning() const override;

    // Commands — returns seq number
    int sendOpen(const QString& filePath, double startSeconds = 0.0) override;
    int sendPause() override;
    int sendResume() override;
    // STREAM_AV_SUB_SYNC_AFTER_STALL 2026-04-21 — mpv-style cache-pause
    // semantics. Distinct from sendPause/sendResume (which are user-
    // initiated) so the sidecar can tell "user paused" from "network
    // starvation paused" and handle them differently: stall_pause
    // freezes the master clock + halts PortAudio writes without
    // flipping the UI pause state; stall_resume re-anchors the clock
    // to the current video PTS via seek_anchor + restarts audio. Per
    // Agent 7 audit av_sub_sync_after_stall_2026-04-21.md Option A
    // (mpv `paused-for-cache`) layered on Option C (telemetry → IPC
    // promotion).
    int sendStallPause() override;
    int sendStallResume() override;
    int sendSeek(double positionSec) override;
    // PLAYER_STREMIO_PARITY Phase 3 — per-call seek precision override.
    // mode = "fast" or "exact"; empty string defers to sticky default set
    // via sendSetSeekMode (sidecar default is fast).
    int sendSeek(double positionSec, const QString& modeOverride) override;
    // Sticky seek-mode preference. mode = "fast" or "exact". Pre-Phase-3
    // sidecar binaries return NOT_IMPLEMENTED — SidecarProcess swallows it.
    int sendSetSeekMode(const QString& mode) override;
    int sendFrameStep(bool backward = false, double currentPosSec = 0.0) override;
    int sendStop() override;
    int sendSetVolume(double volume) override;
    int sendSetMute(bool muted) override;
    int sendSetRate(double rate) override;
    int sendSetTracks(const QString& audioId, const QString& subId) override;
    int sendSetSubVisibility(bool visible) override;
    int sendSetSubDelay(double delayMs) override;
    int sendSetAudioDelay(int delayMs) override;
    int sendSetSubStyle(int fontSize, int marginV, bool outline) override;
    // VIDEO_SUB_POSITION 2026-04-24 — user-facing 0..100 percent baseline
    // (100 = bottom, mpv parity). Persisted globally under QSettings
    // "videoPlayer/subtitlePosition" by VideoPlayer; pushed via this on
    // every slider change + on onSidecarReady for first-file restore.
    int sendSetSubtitlePosition(int percent) override;
    int sendLoadExternalSub(const QString& path) override;
    int sendSetFilters(bool deinterlace, int brightness, int contrast, int saturation, bool normalize, bool interpolate = false, const QString& deinterlaceFilter = {}) override;
    int sendRawFilters(const QString& videoFilter, const QString& audioFilter) override;
    int sendSetToneMapping(const QString& algorithm, bool peakDetect) override;
    int sendSetZeroCopyActive(bool active) override;
    int sendSetCanvasSize(int width, int height) override;
    int sendResize(int width, int height) override;
    int sendShutdown() override;

    // CLOSE_AUDIO_CONTINUES_FIX 2026-04-26 — wait briefly for the sidecar
    // process to exit gracefully after sendShutdown, then force-kill if it
    // hasn't. Shipped because the close-button path (VideoPlayer::stopPlayback)
    // previously fired sendStop+sendShutdown fire-and-forget — if the sidecar
    // dispatcher was busy or PortAudio had buffered audio mid-write, the
    // process stayed alive and audio kept playing until app exit (when the
    // ~SidecarProcess destructor finally hit its existing wait+kill backstop
    // at SidecarProcess.cpp:94-99). This makes the same backstop reachable
    // from any close path with a tighter timeout. Synchronous (blocks the
    // calling thread). Default 500ms covers the typical clean-exit window
    // (~50-100ms) with comfortable headroom; longer values are appropriate
    // for the destructor-on-app-exit case.
    void ensureTerminated(int timeoutMs = 500) override;

    // PLAYER_LIFECYCLE_FIX Phase 2 — same-process stop/open fence.
    // Sends `stop` and stores onComplete to fire when the sidecar emits
    // its matching `stop_ack` (emitted after teardown_decode completes,
    // so the callback runs only once the sidecar is idle and ready for
    // the next sendOpen). Returns the stop's seq. A subsequent call
    // replaces the pending callback (last-click-wins semantics) — the
    // prior stop_ack will still correlate by seq but its callback is
    // gone; safe because the sidecar is just going idle, not starting a
    // new session. If stop_ack doesn't arrive within timeoutMs, onTimeout
    // fires instead and the pending callback is cleared. Intended to be
    // called from VideoPlayer::openFile when the sidecar is already
    // running; cold-start path (sidecar not running) still uses start()
    // + onSidecarReady. The user-close path (stopPlayback → sendStop +
    // sendShutdown) is unchanged.
    int sendStopWithCallback(std::function<void()> onComplete,
                             std::function<void()> onTimeout = nullptr,
                             int timeoutMs = 2000) override;

    // Edge-case fallback used by VideoPlayer when sendStopWithCallback
    // times out (sidecar hang or pre-Phase-2 binary without stop_ack).
    // Kills any running process synchronously (blocks up to 2s on
    // waitForFinished), then starts a fresh sidecar. After this returns,
    // the caller relies on onSidecarReady to dispatch the pending open.
    // Synchronous / GUI-blocking by design — only fires on the edge-case
    // failure path; a brief GUI stall is better than a stuck file-switch.
    void resetAndRestart() override;

    // VIDEO_PLAYER_FIX Batch 5.1 — loop the currently-open file. On enable,
    // sidecar's video decoder treats AVERROR_EOF as a seek-to-0 instead of
    // emitting the `eof` event, avoiding probe+open overhead on each loop.
    // Unknown to pre-5.1 sidecar binaries — they return NOT_IMPLEMENTED
    // cleanly (SidecarProcess swallows that to debug log post-patch).
    int sendSetLoopFile(bool enabled) override;

    // Batch 4.1 (Player Polish Phase 4) — dynamic audio-speed adjustment
    // for A/V drift correction. Sidecar applies via `swr_set_compensation`
    // on its SwrContext; ±5% range matches Kodi's ActiveAE m_maxspeedadjust.
    // Values outside [0.95, 1.05] clamp inside the method. Driven by
    // VideoPlayer's ticker which polls SyncClock::getClockVelocity() and
    // forwards on change beyond deadband. Unknown to pre-Phase-4 sidecar
    // binaries — they ignore the command cleanly (warning log, no break).
    int sendSetAudioSpeed(double speed) override;

    // Batch 4.3 (Player Polish Phase 4) — Dynamic Range Compression
    // toggle. Sidecar runs a feed-forward compressor (threshold -12 dB,
    // ratio 3:1, attack 10 ms, release 100 ms) post-volume in the audio
    // thread. Off by default. Use case: "loud movie at low volume keeps
    // dialogue audible" — compresses explosions/music peaks so dialogue
    // RMS rises relative to full-scale. Toggled by EqualizerPopover's
    // DRC checkbox.
    int sendSetDrcEnabled(bool enabled) override;

    // Batch 5.2 — subtitle protocol extensions for Tankostream Phase 5.
    // Thin Qt-side wrappers composing over existing sidecar commands
    // (set_tracks, set_sub_delay, set_sub_style, load_external_sub). The
    // sidecar's subtitle_renderer already supports libass + PGS end-to-
    // end — no sidecar-side protocol change is required. Agent 4's
    // Batch 5.3 subtitle menu dispatches through these wrappers.

    // Synchronous cached-state getters (updated on every tracks_changed).
    QList<SubtitleTrackInfo> listSubtitleTracks() const override;
    int activeSubtitleIndex() const override;

    // Index-based track selection. index == -1 disables subtitles.
    int sendSetSubtitleTrack(int index) override;

    // Download a subtitle from `url` (async) then load via load_external_sub.
    // `offsetPx` + `delayMs` are applied after load. Emits
    // subtitleUrlLoaded(url, localPath, ok) on completion.
    int sendSetSubtitleUrl(const QUrl& url, int offsetPx = 0, int delayMs = 0) override;

    // Style composers — each one recomposes the full set_sub_style payload
    // from cached state (offset + size + outline) because sidecar expects
    // font + margin + outline atomically.
    int sendSetSubtitlePixelOffset(int pixelOffsetY) override;
    int sendSetSubtitleSize(double scale) override;

    // int-ms alias matching Batch 5.2 naming (forwards to sendSetSubDelay).
    int sendSetSubtitleDelayMs(int ms) override;

    // MPV_BACKEND_INTEGRATION P1 2026-04-26 — signals: block removed.
    // All 33 signals (ready/firstFrame/timeUpdate/stateChanged/tracksChanged/
    // endOfFile/errorOccurred/subtitleText/subVisibilityChanged/subDelayChanged/
    // filtersChanged/mediaInfo/frameStepped/d3d11Texture/overlayShm/
    // bufferingStarted/bufferingEnded/cacheStateChanged/probeStarted/probeDone/
    // decoderOpenStarted/decoderOpenDone/firstPacketRead/firstDecoderReceive/
    // nearEndEstimate/processClosed/processCrashed/decodeError/
    // subtitleTracksListed/subtitleTrackApplied/subtitleUrlLoaded/
    // subtitleOffsetChanged/subtitleSizeChanged) are declared in
    // IPlayerBackend.h's signals: block. SidecarProcess inherits them
    // automatically and emits via the standard `emit signalName()` syntax.
    // moc generates per-class implementations for both base + derived;
    // connect() through IPlayerBackend* resolves to the right emitter via
    // virtual signal dispatch (standard Qt pattern).

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    int sendCommand(const QString& name, const QJsonObject& payload = {});
    void processLine(const QByteArray& line);

    // Batch 5.2 helpers.
    void updateSubtitleCache(const QJsonArray& subtitle, const QString& activeSubId);
    int  pushSubStyle();

    // VIDEO_IPC_INSTRUMENTATION 2026-04-24 — IPC round-trip latency tracker.
    // sendCommand stamps a send-time per seq; the `ack` event handler in
    // processLine matches seqAck back to the pending entry and records the
    // delta in per-command-name histograms. dumpIpcLatency writes p50/p99/
    // max/count per command to out/ipc_latency.log (append) at shutdown.
    // Non-behavioral — measurement only. See chat.md 2026-04-24 evening-late
    // for the strategic context (companion to scripts/compare-mpv-tanko.ps1:
    // harness tells us IF we regressed; this tells us WHERE the time goes).
    struct PendingSend {
        QString name;
        qint64  sendMs = 0;
    };
    void recordIpcAck(int seqAck);
    void dumpIpcLatency();
    QHash<int, PendingSend>             m_ipcPending;
    QHash<QString, QVector<qint64>>     m_ipcLatencies;
    static constexpr int                kIpcPendingMaxSize = 1024;

    QProcess* m_process = nullptr;
    QString   m_sessionId;
    std::atomic<int> m_seq{0};
    QByteArray m_readBuffer;

    // REPO_HYGIENE Phase 4 P4.6 (2026-04-26) — session-strict mode. Set true
    // when sidecar emits its `version` event with `session_strict: true` at
    // startup. Once true, processLine drops session-scoped events with
    // empty sessionId (rather than the pre-handshake legacy tolerance).
    bool m_sessionStrict = false;

    // Batch 6.1 — true after sendShutdown(), false after start(). Used by
    // onProcessFinished to decide whether a finished event is a normal
    // shutdown or a crash that should trigger auto-restart.
    bool m_intentionalShutdown = false;

    // PLAYER_LIFECYCLE_FIX Phase 2 — same-process stop/open fence state.
    // m_pendingStopSeq == -1 means no stop awaiting ack. Any other value
    // is the seq of the in-flight stop; stop_ack with matching seqAck
    // fires m_pendingStopCallback. If QTimer::singleShot(timeoutMs) fires
    // first (no stop_ack arrived), m_pendingStopTimeoutCallback fires
    // and the pending-stop state clears. Exactly one of the two callbacks
    // fires per pending stop; both are moved out before invocation so
    // re-entry through a nested callback is safe.
    int m_pendingStopSeq = -1;
    std::function<void()> m_pendingStopCallback;
    std::function<void()> m_pendingStopTimeoutCallback;

    // Batch 5.2 state — cached subtitle tracks (mirrors last tracks_changed
    // subtitle array) + composed style (sidecar's set_sub_style is atomic
    // over font+margin+outline, so we hold the unchanged fields between
    // calls) + URL download infrastructure.
    QList<SubtitleTrackInfo> m_subTracks;
    int    m_activeSubIndex     = -1;
    int    m_subPixelOffsetY    = 0;      // added to kSubBaseMargin
    double m_subSizeScale       = 1.0;    // multiplied into kSubBaseFontSize
    bool   m_subOutline         = true;
    QNetworkAccessManager* m_nam = nullptr;  // lazy-init on first URL load
    QList<QTemporaryFile*> m_subTempFiles;   // keep downloaded subs alive
    static constexpr int    kSubBaseFontSize = 24;
    static constexpr int    kSubBaseMargin   = 40;

    int m_canvasWidth = 0;
    int m_canvasHeight = 0;
};
