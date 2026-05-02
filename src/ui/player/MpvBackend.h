#pragma once

// MpvBackend — IPlayerBackend implementation against an in-process libmpv
// (linked via shinchiro/mpv-winbuild-cmake's libmpv-2.dll). Constructed by
// VideoPlayer when the user picks the mpv backend (Phase 7 BackendFactory)
// or when TANKOBAN_FORCE_MPV=1 is set in the environment.
//
// Phase 3 (this file): vo=null. Audio plays through mpv's audio backend;
// time-pos / pause / track property observers feed IPlayerBackend signals
// for the existing HUD; video has nowhere to go (FrameCanvas integration
// is Phase 4-5).
//
// Phase 4-5: vo=libmpv with render API → OpenGL FBO → D3D11 shared texture
// → FrameCanvas. The MpvBackend itself stays the same — only the render
// path is added; same Qt signals, same behavior surface.
//
// Threading model: libmpv's wakeup callback fires from any internal thread.
// We marshal back to the GUI thread by emitting a queued Qt signal that
// dispatches to onWakeup(); onWakeup drains mpv_wait_event(0) on the GUI
// thread (where the IPlayerBackend signals must run).

#include "IPlayerBackend.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

// Forward declarations of opaque libmpv types so this header stays
// includable even when libmpv isn't on disk (CMake graceful-disable).
struct mpv_handle;
struct mpv_event_property;
class MpvLibplaceboRenderer;

class QTimer;

class MpvBackend : public IPlayerBackend {
    Q_OBJECT

public:
    explicit MpvBackend(QObject* parent = nullptr);
    ~MpvBackend() override;

    // Expose the underlying mpv handle so MpvVideoWidget (Phase 5 redux)
    // can attach a libmpv render context against its own QOpenGLContext.
    // Concrete-class API only — not part of IPlayerBackend.
    mpv_handle* mpvHandle() const { return m_mpv; }
    MpvLibplaceboRenderer* libplaceboRenderer() const { return m_libplaceboRenderer.get(); }

    // ── Lifecycle ───────────────────────────────────────────────────────────

    void start() override;
    bool isRunning() const override;
    void ensureTerminated(int timeoutMs = 500) override;
    void resetAndRestart() override;

    // ── Playback control ───────────────────────────────────────────────────

    int sendOpen(const QString& filePath, double startSeconds = 0.0) override;
    int sendPause() override;
    int sendResume() override;
    int sendStallPause() override;
    int sendStallResume() override;
    int sendSeek(double positionSec) override;
    int sendSeek(double positionSec, const QString& modeOverride) override;
    int sendSetSeekMode(const QString& mode) override;
    int sendFrameStep(bool backward = false, double currentPosSec = 0.0) override;
    int sendStop() override;
    int sendShutdown() override;

    // ── Audio ──────────────────────────────────────────────────────────────

    int sendSetVolume(double volume) override;
    int sendSetMute(bool muted) override;
    int sendSetRate(double rate) override;
    int sendSetAudioDelay(int delayMs) override;
    int sendSetAudioSpeed(double speed) override;
    int sendSetDrcEnabled(bool enabled) override;

    // ── Tracks ─────────────────────────────────────────────────────────────

    int sendSetTracks(const QString& audioId, const QString& subId) override;
    int sendSetSubtitleTrack(int index) override;

    // ── Subtitles ──────────────────────────────────────────────────────────

    int sendSetSubVisibility(bool visible) override;
    int sendSetSubDelay(double delayMs) override;
    int sendSetSubStyle(int fontSize, int marginV, bool outline) override;
    int sendSetSubtitlePosition(int percent) override;
    int sendSetSubtitlePositionMode(const QString& mode) override;
    int sendLoadExternalSub(const QString& path) override;
    int sendSetSubtitleUrl(const QUrl& url, int offsetPx = 0, int delayMs = 0) override;
    int sendSetSubtitlePixelOffset(int pixelOffsetY) override;
    int sendSetSubtitleSize(double scale) override;
    int sendSetSubtitleDelayMs(int ms) override;

    QList<SubtitleTrackInfo> listSubtitleTracks() const override;
    int activeSubtitleIndex() const override;

    // ── Filters / rendering ────────────────────────────────────────────────

    int sendSetFilters(bool deinterlace, int brightness, int contrast,
                       int saturation, bool normalize,
                       bool interpolate = false,
                       const QString& deinterlaceFilter = {}) override;
    int sendRawFilters(const QString& videoFilter, const QString& audioFilter) override;
    int sendSetToneMapping(const QString& algorithm, bool peakDetect) override;
    int sendSetZeroCopyActive(bool active) override;
    int sendSetCanvasSize(int width, int height) override;
    int sendResize(int width, int height) override;
    int sendSetLoopFile(bool enabled) override;

    // ── Lifecycle fence ────────────────────────────────────────────────────

    int sendStopWithCallback(std::function<void()> onComplete,
                             std::function<void()> onTimeout = nullptr,
                             int timeoutMs = 2000) override;

private slots:
    // Drains mpv_wait_event() on the GUI thread. Connected to a queued
    // signal posted by the static wakeupCallback (which fires on libmpv's
    // worker threads).
    void onWakeup();

signals:
    // Internal signal — fired by the static wakeup callback, queued onto
    // the GUI thread, drained by onWakeup. Not part of IPlayerBackend.
    void wakeupRequested();

    // P5 redux — emitted at the very start of teardownMpv() so external
    // owners of mpv-handle-derived state (MpvVideoWidget's render context)
    // can free their resources BEFORE mpv_terminate_destroy runs. Direct
    // (synchronous) connections only.
    void mpvHandleInvalidating();

private:
    void initializeMpv();
    void teardownMpv();
    void observeProperties();
    void handlePropertyChange(mpv_event_property* prop);
    int  nextSeq();
    void emitFirstFrameStub();

    // MAKE_MPV_BEAT_FFMPEG Task 6 step 5 — extract HDR color metadata from
    // mpv (video-params/primaries + gamma + sig-peak) after probe completes
    // + push to MpvLibplaceboRenderer so libplacebo gets correct
    // pl_frame.color on each render tick.
    void pushSourceColorSpaceToRenderer();

    // MAKE_MPV_SOLO Task 10 (2026-05-01) — telemetry plumbing.
    // startTelemetry: captures one-time identity (hwdec/vo/ao/codecs/file)
    //   right after mpv_initialize succeeds and starts the 5s sample timer.
    // sampleTelemetry: timer-tick handler. Reads frame-drop-count,
    //   vo-delayed-frame-count, estimated-vf-fps, playback-time,
    //   paused-for-cache; appends a TelemetrySample to m_telemetrySamples.
    // dumpTelemetry: writes the session block (header + per-sample rows +
    //   summary) to out/mpv_telemetry.log on teardown. Append-only file
    //   (one block per session) mirroring SidecarProcess::dumpIpcLatency
    //   contract at SidecarProcess.cpp.
    void startTelemetry();
    void sampleTelemetry();
    void dumpTelemetry();

    // libmpv's wakeup callback — fires on a libmpv-internal thread. Posts
    // a queued signal to ourselves so onWakeup() runs on the GUI thread.
    static void wakeupCallback(void* d);

    mpv_handle* m_mpv = nullptr;
    std::unique_ptr<MpvLibplaceboRenderer> m_libplaceboRenderer;
    std::atomic<int> m_seq{0};
    bool m_running = false;

    // Cached state populated by property observers.
    QList<SubtitleTrackInfo> m_subtitleTracks;
    int    m_activeSubIndex      = -1;
    double m_lastPositionSec     = 0.0;
    double m_lastDurationSec     = 0.0;
    bool   m_durationIsEstimate  = false;
    bool   m_isPaused            = false;
    bool   m_eofReached          = false;
    QString m_currentFilePath;
    bool   m_firstFrameEmitted   = false;

    // 1.C 2026-04-30 — track paused-for-cache edge transitions so we emit
    // bufferingStarted exactly once on the rising edge (false→true) and
    // bufferingEnded on the falling edge (true→false). mpv re-fires the
    // property event on every value change including same-value re-emits;
    // gating on this flag prevents spurious double-emit. Initial value
    // false matches mpv's startup state (no cache pause until demuxer
    // signals starvation).
    bool   m_pausedForCache      = false;

    // 1.C 2026-04-30 — throttle wall-clock timestamp for cacheStateChanged
    // emit. mpv fires demuxer-cache-state property events at sub-second
    // cadence (~0.5-2 Hz native per 1.B evidence §4 = 180+ events / 5min);
    // unthrottled re-emit would flood LoadingOverlay::setCacheProgress on
    // the GUI thread. Cap emit cadence at 2 Hz (500 ms minimum gap),
    // matching ffmpeg sidecar's cache_state cadence at
    // native_sidecar/src/main.cpp:752-780. Sentinel 0 means "first emit
    // allowed immediately" — reset on teardownMpv so a fresh session
    // starts un-throttled.
    qint64 m_lastCacheStateEmitMs = 0;

    // 1.E 2026-04-30 — true between paused-for-cache rising-edge (stall
    // detected) and falling-edge (cache refilled). Gates the existing
    // `pause` property handler at handlePropertyChange:360-368 from
    // emitting stateChanged("paused") during a transparent network stall.
    // Without this gate, mpv's pause property fires true alongside
    // paused-for-cache=true (mpv internally pauses its decoder while
    // waiting for cache), the existing stateChanged emit flips the user-
    // visible play/pause icon to "paused" mid-stall, and Hemanth sees a
    // false "user paused playback" UI state during what should be a
    // transparent network buffering pause. ffmpeg sidecar's handle_stall_pause
    // (native_sidecar/src/main.cpp:1162-1184) is explicitly documented
    // as NOT emitting state_changed for the same reason — 1.E ports that
    // semantic to the mpv backend.
    bool   m_inStallPause        = false;

    // MAKE_MPV_SOLO Task 12.A (2026-05-02) — HDR-conditional hwdec auto-pick.
    // Remembers whether the user set TANKOBAN_MPV_HWDEC at init so the
    // file-load HDR-detect path doesn't override a deliberate user pick.
    // Set true in initializeMpv() iff qgetenv returns non-empty; cleared
    // back to false on teardownMpv() so a subsequent resetAndRestart
    // re-reads the env var fresh.
    bool   m_hwdecOverriddenByEnv = false;
    // Tracks the currently-active hwdec value so the file-load auto-pick
    // can dedupe (don't re-set if already on d3d11va-copy from a previous
    // HDR file). String comparison via mpv_get_property_string would also
    // work; caching the local string saves the round-trip per file open.
    QString m_currentHwdec;

    // 1.E.1 hotfix 2026-04-30 — defer bufferingEnded emit from
    // paused-for-cache falling-edge to the next MPV_EVENT_PLAYBACK_RESTART
    // event. Hemanth-reported (~18:50pm): "the buffering overlay isn't
    // accurate. the video doesn't start playing yet the buffering overlay
    // disappears." Root cause: paused-for-cache flips false when buffer
    // is no longer the constraint, NOT when frames actually start
    // rendering again — there's a decoder-catch-up window where buffer
    // is fine but no new frame has hit the screen yet. PLAYBACK_RESTART
    // is mpv's "rendering actually resumed" signal (fires on initial
    // first-frame + every seek-completion + post-stall resume); it's
    // the honest dismiss trigger. This flag is set on paused-for-cache
    // falling-edge and consumed (cleared + emit) on the next PLAYBACK_RESTART.
    bool   m_pendingBufferingEnd = false;

    // sendStopWithCallback state.
    std::function<void()> m_pendingStopComplete;
    std::function<void()> m_pendingStopTimeout;
    QTimer*               m_pendingStopTimer = nullptr;

    // MAKE_MPV_SOLO Task 10 (2026-05-01) — periodic telemetry sampling.
    // Hemanth-flagged half-rate stutter (10-15 fps on 24 fps source) was
    // diagnosed via [MPV-RENDER] log lines but had no persistent record;
    // every smoke session lost the data on Tankoban exit. This struct
    // captures the same shape every 5s into a vector that flushes to
    // out/mpv_telemetry.log on teardown — append-only, one session block
    // per run, mirroring SidecarProcess::dumpIpcLatency's pattern.
    struct TelemetrySample {
        qint64 elapsedMs;          // ms since startTelemetry
        int    frameDropCount;     // mpv frame-drop-count (decoder side)
        int    voDelayedCount;     // mpv vo-delayed-frame-count (display side)
        double estimatedVfFps;     // mpv estimated-vf-fps (post-filter measured)
        double playbackTimeSec;    // mpv playback-time (current pts)
        bool   pausedForCache;     // mpv paused-for-cache (buffering)
    };
    QTimer*  m_telemetryTimer    = nullptr;
    qint64   m_telemetryStartMs  = 0;
    QVector<TelemetrySample> m_telemetrySamples;
    // One-time identity captured at startTelemetry. Empty strings indicate
    // the property wasn't queryable (hwdec on no-hardware-decode files,
    // ao on vo=null start window before audio device opens, etc.).
    QString  m_telemetryHwdec;
    QString  m_telemetryVo;
    QString  m_telemetryAo;
    QString  m_telemetryVideoCodec;
    QString  m_telemetryAudioCodec;
    QString  m_telemetryFileFormat;
    QString  m_telemetryFile;
    double   m_telemetryDurationSec = 0.0;
    double   m_telemetryDisplayFps  = 0.0;
};
