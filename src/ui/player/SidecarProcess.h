#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QUrl>
#include <atomic>
#include <functional>

class QNetworkAccessManager;
class QTemporaryFile;

// Batch 5.2 (Tankostream Phase 5) — subtitle track descriptor exposed to
// the Qt UI. `index` is the ordinal used by the API surface and menus;
// `sidecarId` is the string ID the sidecar uses in its set_tracks payload.
// Populated by caching tracks_changed events from the sidecar.
struct SubtitleTrackInfo {
    int     index    = -1;
    QString sidecarId;
    QString lang;
    QString title;
    QString codec;
    bool    external = false;
};
Q_DECLARE_METATYPE(SubtitleTrackInfo)

// Manages the ffmpeg_sidecar.exe subprocess.
// Sends JSON commands on stdin, receives JSON events from stdout.
class SidecarProcess : public QObject {
    Q_OBJECT

public:
    explicit SidecarProcess(QObject* parent = nullptr);
    ~SidecarProcess() override;

    void start();
    bool isRunning() const;

    // Commands — returns seq number
    int sendOpen(const QString& filePath, double startSeconds = 0.0);
    int sendPause();
    int sendResume();
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
    int sendStallPause();
    int sendStallResume();
    int sendSeek(double positionSec);
    // PLAYER_STREMIO_PARITY Phase 3 — per-call seek precision override.
    // mode = "fast" or "exact"; empty string defers to sticky default set
    // via sendSetSeekMode (sidecar default is fast).
    int sendSeek(double positionSec, const QString& modeOverride);
    // Sticky seek-mode preference. mode = "fast" or "exact". Pre-Phase-3
    // sidecar binaries return NOT_IMPLEMENTED — SidecarProcess swallows it.
    int sendSetSeekMode(const QString& mode);
    int sendFrameStep(bool backward = false, double currentPosSec = 0.0);
    int sendStop();
    int sendSetVolume(double volume);
    int sendSetMute(bool muted);
    int sendSetRate(double rate);
    int sendSetTracks(const QString& audioId, const QString& subId);
    int sendSetSubVisibility(bool visible);
    int sendSetSubDelay(double delayMs);
    int sendSetAudioDelay(int delayMs);
    int sendSetSubStyle(int fontSize, int marginV, bool outline);
    int sendLoadExternalSub(const QString& path);
    int sendSetFilters(bool deinterlace, int brightness, int contrast, int saturation, bool normalize, bool interpolate = false, const QString& deinterlaceFilter = {});
    int sendRawFilters(const QString& videoFilter, const QString& audioFilter);
    int sendSetToneMapping(const QString& algorithm, bool peakDetect);
    int sendSetZeroCopyActive(bool active);
    int sendSetCanvasSize(int width, int height);
    int sendResize(int width, int height);
    int sendShutdown();

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
                             int timeoutMs = 2000);

    // Edge-case fallback used by VideoPlayer when sendStopWithCallback
    // times out (sidecar hang or pre-Phase-2 binary without stop_ack).
    // Kills any running process synchronously (blocks up to 2s on
    // waitForFinished), then starts a fresh sidecar. After this returns,
    // the caller relies on onSidecarReady to dispatch the pending open.
    // Synchronous / GUI-blocking by design — only fires on the edge-case
    // failure path; a brief GUI stall is better than a stuck file-switch.
    void resetAndRestart();

    // VIDEO_PLAYER_FIX Batch 5.1 — loop the currently-open file. On enable,
    // sidecar's video decoder treats AVERROR_EOF as a seek-to-0 instead of
    // emitting the `eof` event, avoiding probe+open overhead on each loop.
    // Unknown to pre-5.1 sidecar binaries — they return NOT_IMPLEMENTED
    // cleanly (SidecarProcess swallows that to debug log post-patch).
    int sendSetLoopFile(bool enabled);

    // Batch 4.1 (Player Polish Phase 4) — dynamic audio-speed adjustment
    // for A/V drift correction. Sidecar applies via `swr_set_compensation`
    // on its SwrContext; ±5% range matches Kodi's ActiveAE m_maxspeedadjust.
    // Values outside [0.95, 1.05] clamp inside the method. Driven by
    // VideoPlayer's ticker which polls SyncClock::getClockVelocity() and
    // forwards on change beyond deadband. Unknown to pre-Phase-4 sidecar
    // binaries — they ignore the command cleanly (warning log, no break).
    int sendSetAudioSpeed(double speed);

    // Batch 4.3 (Player Polish Phase 4) — Dynamic Range Compression
    // toggle. Sidecar runs a feed-forward compressor (threshold -12 dB,
    // ratio 3:1, attack 10 ms, release 100 ms) post-volume in the audio
    // thread. Off by default. Use case: "loud movie at low volume keeps
    // dialogue audible" — compresses explosions/music peaks so dialogue
    // RMS rises relative to full-scale. Toggled by EqualizerPopover's
    // DRC checkbox.
    int sendSetDrcEnabled(bool enabled);

    // Batch 5.2 — subtitle protocol extensions for Tankostream Phase 5.
    // Thin Qt-side wrappers composing over existing sidecar commands
    // (set_tracks, set_sub_delay, set_sub_style, load_external_sub). The
    // sidecar's subtitle_renderer already supports libass + PGS end-to-
    // end — no sidecar-side protocol change is required. Agent 4's
    // Batch 5.3 subtitle menu dispatches through these wrappers.

    // Synchronous cached-state getters (updated on every tracks_changed).
    QList<SubtitleTrackInfo> listSubtitleTracks() const;
    int activeSubtitleIndex() const;

    // Index-based track selection. index == -1 disables subtitles.
    int sendSetSubtitleTrack(int index);

    // Download a subtitle from `url` (async) then load via load_external_sub.
    // `offsetPx` + `delayMs` are applied after load. Emits
    // subtitleUrlLoaded(url, localPath, ok) on completion.
    int sendSetSubtitleUrl(const QUrl& url, int offsetPx = 0, int delayMs = 0);

    // Style composers — each one recomposes the full set_sub_style payload
    // from cached state (offset + size + outline) because sidecar expects
    // font + margin + outline atomically.
    int sendSetSubtitlePixelOffset(int pixelOffsetY);
    int sendSetSubtitleSize(double scale);

    // int-ms alias matching Batch 5.2 naming (forwards to sendSetSubDelay).
    int sendSetSubtitleDelayMs(int ms);

signals:
    void ready();
    void firstFrame(const QJsonObject& payload);   // shmName, width, height, slotCount, slotBytes
    void timeUpdate(double positionSec, double durationSec);
    void stateChanged(const QString& state);
    void tracksChanged(const QJsonArray& audio, const QJsonArray& subtitle,
                       const QString& activeAudioId, const QString& activeSubId);
    void endOfFile();
    void errorOccurred(const QString& message);
    void subtitleText(const QString& text);
    void subVisibilityChanged(bool visible);
    void subDelayChanged(double delayMs);
    void filtersChanged(const QJsonObject& state);
    void mediaInfo(const QJsonObject& info);
    void frameStepped(double positionSec);
    // Emitted when the sidecar publishes a D3D11 shared texture NT handle.
    // FrameCanvas opens this in its own D3D11 device for zero-copy display
    // (eliminating the GPU→CPU→GPU round trip we have via SHM).
    void d3d11Texture(quintptr ntHandle, int width, int height);

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay SHM ready.
    // Sidecar writes rendered subtitle BGRA into this named SHM each frame;
    // FrameCanvas opens it and uploads per-frame to a locally-owned D3D11
    // overlay texture. Decoupled from the video texture so there's no
    // cross-process GPU sync — main-app owns all the draw-side resources.
    void overlayShm(const QString& shmName, int width, int height);

    // PLAYER_UX_FIX Phase 2.2 — sidecar-observed HTTP-stall state on
    // stream URL playback. `bufferingStarted` fires when
    // av_read_frame hits EAGAIN/ETIMEDOUT/EIO; `bufferingEnded` fires
    // when a subsequent read succeeds (stall cleared). Distinct from
    // the one-shot state_changed{playing} emitted at first_frame —
    // these can fire repeatedly across a session. Phase 2.3's
    // LoadingOverlay consumes both to toggle a "Buffering…" indicator.
    void bufferingStarted();
    void bufferingEnded();

    // PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — structured cache-pause
    // progress from sidecar (video_decoder.cpp HTTP stall loop emits at
    // 2 Hz; main.cpp dispatches as `cache_state` JSON). Fires only during
    // an active stall (between bufferingStarted and bufferingEnded). The
    // empty-payload `bufferingStarted` / `bufferingEnded` events continue
    // to mark stall boundaries; this signal surfaces the progress detail
    // so the LoadingOverlay can render Stremio-style "Buffering — N% (~Xs)".
    //
    // Sentinels preserved from sidecar side:
    //   etaResumeSec == -1.0 → rate too low to estimate ("time unknown")
    //   cacheDurationSec == -1.0 → container bitrate unavailable
    // Consumers should render honest-unknown text for sentinel values
    // instead of computing with them.
    void cacheStateChanged(qint64 bytesAhead,
                           qint64 inputRateBps,
                           double etaResumeSec,
                           double cacheDurationSec);

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.2 — classified open-pipeline
    // progress events. Sidecar Phase 1.1 emits 6 session-scoped events
    // between `state_changed{opening}` and `first_frame` (previously a
    // silent 10-70s window on slow opens). Qt consumers drive the
    // LoadingOverlay's setStage (Phase 2.1) for classified user-facing
    // text + can cross-correlate with StreamEngineStats for diagnostic
    // cohesion (Batch 1.3, Agent 4's surface — future consumer).
    // All 6 signals are session-filtered at the parser layer via the
    // PLAYER_LIFECYCLE_FIX pattern; stale-session events dropped before
    // emit. Payload data is discarded at the signal boundary (minimal
    // signatures) because the current consumers (LoadingOverlay stage
    // transitions) don't need the per-event scalars; stderr log + sidecar
    // payload JSON retain the t_ms_from_open / analyze_duration_ms /
    // stream_count / pts_ms / etc. for agent-side diagnostics via the
    // generic `[Sidecar] RECV: <name>` log line at SidecarProcess.cpp:437.
    // Rule-14 design pick: first_decoder_receive — NOT first_packet_read
    // — drives the DecodingFirstFrame stage transition in Phase 2.1
    // (packet-read success before receive-frame success can stall on
    // decoder back-pressure; receive is the honest "making progress"
    // signal).
    void probeStarted();
    // STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — extended from
    // parameterless to carry `durationIsEstimate` flag from the sidecar
    // probe payload. VideoPlayer consumes this to decide whether to
    // prefix the HUD duration label with `~` (tilde) for estimate
    // transparency. Qt permits slots with fewer parameters than their
    // signal; any existing zero-arg listeners to probeDone continue to
    // compile + run unchanged. Default false for non-estimate paths.
    void probeDone(bool durationIsEstimate);
    void decoderOpenStarted();
    void decoderOpenDone();
    void firstPacketRead();
    void firstDecoderReceive();

    // STREAM_AUTO_NEXT_ESTIMATE_FIX 2026-04-21 — sidecar fires once per
    // session when the consumer read position crosses 90 s of bytes before
    // HTTP EOF. StreamPage wires this as a parallel nearEndCrossed trigger
    // alongside the existing pct/remaining duration check. Gives AUTO_NEXT
    // a ground-truth near-end signal on bitrate-estimate sources where the
    // duration-based check is structurally unreachable. Parameterless —
    // event presence IS the signal.
    void nearEndEstimate();

    void processClosed();

    // Batch 6.1 (Player Polish Phase 6) — fires when QProcess::finished
    // arrives without a prior sendShutdown(). Distinguishes crash exit
    // (or non-zero return without shutdown) from normal clean shutdown.
    // VideoPlayer listens and drives respawn + reopen at last PTS with
    // max 3 retries + exponential backoff.
    void processCrashed(int exitCode, QProcess::ExitStatus status);

    // Batch 6.3 (Player Polish Phase 6) — sidecar caught a non-fatal
    // avcodec error (corrupt packet, lost frame, etc.) and advanced past
    // it rather than aborting. `code` is the sidecar's category tag
    // ("DECODE_SKIP_PACKET", "DECODE_SKIP_FRAME"); `message` is the
    // human-readable `av_strerror` output; `recoverable` is always true
    // for decode_error (the fatal path still uses `errorOccurred`).
    // VideoPlayer shows a brief toast; playback keeps going.
    void decodeError(const QString& code, const QString& message, bool recoverable);

    // Batch 5.2 — subtitle protocol signals.
    // subtitleTracksListed: pre-parsed mirror of tracksChanged's subtitle
    // array, populated from the cache. Activates on every tracks_changed.
    // subtitleTrackApplied: index successfully selected via sendSetSubtitleTrack.
    // subtitleUrlLoaded: URL download path — ok indicates local file ready.
    // subtitleOffsetChanged / subtitleSizeChanged: ack echoes for live UI.
    void subtitleTracksListed(const QList<SubtitleTrackInfo>& tracks, int activeIndex);
    void subtitleTrackApplied(int index);
    void subtitleUrlLoaded(const QUrl& url, const QString& localPath, bool ok);
    void subtitleOffsetChanged(int pixelOffsetY);
    void subtitleSizeChanged(double scale);

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

    QProcess* m_process = nullptr;
    QString   m_sessionId;
    std::atomic<int> m_seq{0};
    QByteArray m_readBuffer;

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
