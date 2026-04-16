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
    int sendSeek(double positionSec);
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
