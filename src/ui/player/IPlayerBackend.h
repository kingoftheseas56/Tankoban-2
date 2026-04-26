#pragma once

// MPV_BACKEND_INTEGRATION P1 2026-04-26 — pure-virtual interface extracted
// from SidecarProcess so a second backend (MpvBackend, Phase 2) can slot
// in without forking VideoPlayer. SidecarProcess (renamed in Phase 6 if
// survivor) inherits this; MpvBackend will inherit it in Phase 2; both get
// a Q_OBJECT macro and moc generates per-class signal-emit code, while
// connect() through the interface pointer routes via virtual signal
// dispatch (standard Qt pattern).
//
// Zero behavior change in Phase 1 — every method declaration is mirrored
// from SidecarProcess.h verbatim (including default arg values + comment
// blocks describing the contract). The concrete SidecarProcess body is
// untouched; it just gains `: public IPlayerBackend` + `override` keywords.

#include <QObject>
#include <QProcess>           // for QProcess::ExitStatus in processCrashed signal
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QUrl>
#include <cstdint>            // for qint64 in cacheStateChanged
#include <functional>         // for std::function in sendStopWithCallback

// Batch 5.2 (Tankostream Phase 5) — subtitle track descriptor exposed to
// the Qt UI. `index` is the ordinal used by the API surface and menus;
// `sidecarId` is the string ID the backend uses in its set_tracks payload.
// Populated by caching tracks_changed events from the backend. Moved here
// from SidecarProcess.h in Phase 1 so the interface header is self-contained.
struct SubtitleTrackInfo {
    int     index    = -1;
    QString sidecarId;
    QString lang;
    QString title;
    QString codec;
    bool    external = false;
};
Q_DECLARE_METATYPE(SubtitleTrackInfo)

// Pure-virtual interface for any video playback backend (ffmpeg sidecar,
// mpv subprocess, future backends). Implementations: SidecarProcess (current
// ffmpeg path), MpvBackend (Phase 2). VideoPlayer holds an IPlayerBackend*
// and routes all backend interaction through this surface.
class IPlayerBackend : public QObject {
    Q_OBJECT

public:
    explicit IPlayerBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~IPlayerBackend() override = default;

    // ── Lifecycle ───────────────────────────────────────────────────────────

    virtual void start() = 0;
    virtual bool isRunning() const = 0;

    // CLOSE_AUDIO_CONTINUES_FIX 2026-04-26 — wait for graceful exit, force-kill
    // if exceeded. Synchronous. Default 500ms covers the typical clean-exit
    // window (~50-100ms) with comfortable headroom; longer values are
    // appropriate for the destructor-on-app-exit case.
    virtual void ensureTerminated(int timeoutMs = 500) = 0;

    // Edge-case fallback used by VideoPlayer when sendStopWithCallback times
    // out (backend hang). Kills any running process synchronously, then starts
    // a fresh backend. After this returns, the caller relies on `ready` event
    // to dispatch the pending open. Synchronous / GUI-blocking by design.
    virtual void resetAndRestart() = 0;

    // ── Playback control commands (return seq number) ──────────────────────

    virtual int sendOpen(const QString& filePath, double startSeconds = 0.0) = 0;
    virtual int sendPause() = 0;
    virtual int sendResume() = 0;

    // STREAM_AV_SUB_SYNC_AFTER_STALL 2026-04-21 — mpv-style cache-pause
    // semantics. Distinct from sendPause/sendResume (user-initiated) so the
    // backend can tell "user paused" from "network starvation paused" and
    // handle them differently.
    virtual int sendStallPause() = 0;
    virtual int sendStallResume() = 0;

    virtual int sendSeek(double positionSec) = 0;

    // PLAYER_STREMIO_PARITY Phase 3 — per-call seek precision override.
    // mode = "fast" or "exact"; empty string defers to sticky default.
    virtual int sendSeek(double positionSec, const QString& modeOverride) = 0;
    virtual int sendSetSeekMode(const QString& mode) = 0;

    virtual int sendFrameStep(bool backward = false, double currentPosSec = 0.0) = 0;
    virtual int sendStop() = 0;
    virtual int sendShutdown() = 0;

    // ── Audio commands ─────────────────────────────────────────────────────

    virtual int sendSetVolume(double volume) = 0;
    virtual int sendSetMute(bool muted) = 0;
    virtual int sendSetRate(double rate) = 0;
    virtual int sendSetAudioDelay(int delayMs) = 0;

    // Batch 4.1 (Player Polish Phase 4) — dynamic audio-speed adjustment for
    // A/V drift correction. ±5% range. Driven by VideoPlayer ticker on
    // SyncClock velocity change beyond deadband.
    virtual int sendSetAudioSpeed(double speed) = 0;

    // Batch 4.3 (Player Polish Phase 4) — Dynamic Range Compression toggle.
    virtual int sendSetDrcEnabled(bool enabled) = 0;

    // ── Track selection ────────────────────────────────────────────────────

    virtual int sendSetTracks(const QString& audioId, const QString& subId) = 0;

    // Batch 5.2 — index-based selection. index == -1 disables subtitles.
    virtual int sendSetSubtitleTrack(int index) = 0;

    // ── Subtitle commands ──────────────────────────────────────────────────

    virtual int sendSetSubVisibility(bool visible) = 0;
    virtual int sendSetSubDelay(double delayMs) = 0;
    virtual int sendSetSubStyle(int fontSize, int marginV, bool outline) = 0;

    // VIDEO_SUB_POSITION 2026-04-24 — user-facing 0..100 percent baseline
    // (100 = bottom, mpv parity). Persisted globally under QSettings.
    virtual int sendSetSubtitlePosition(int percent) = 0;
    virtual int sendLoadExternalSub(const QString& path) = 0;

    // Batch 5.2 — async URL download then load via load_external_sub.
    virtual int sendSetSubtitleUrl(const QUrl& url, int offsetPx = 0, int delayMs = 0) = 0;

    // Style composers — recompose full set_sub_style payload from cached state.
    virtual int sendSetSubtitlePixelOffset(int pixelOffsetY) = 0;
    virtual int sendSetSubtitleSize(double scale) = 0;

    // int-ms alias forwards to sendSetSubDelay.
    virtual int sendSetSubtitleDelayMs(int ms) = 0;

    // Synchronous cached-state getters (updated on every tracks_changed).
    virtual QList<SubtitleTrackInfo> listSubtitleTracks() const = 0;
    virtual int activeSubtitleIndex() const = 0;

    // ── Video filters / rendering ──────────────────────────────────────────

    virtual int sendSetFilters(bool deinterlace, int brightness, int contrast,
                               int saturation, bool normalize,
                               bool interpolate = false,
                               const QString& deinterlaceFilter = {}) = 0;
    virtual int sendRawFilters(const QString& videoFilter, const QString& audioFilter) = 0;
    virtual int sendSetToneMapping(const QString& algorithm, bool peakDetect) = 0;
    virtual int sendSetZeroCopyActive(bool active) = 0;
    virtual int sendSetCanvasSize(int width, int height) = 0;
    virtual int sendResize(int width, int height) = 0;

    // VIDEO_PLAYER_FIX Batch 5.1 — loop the currently-open file. On enable,
    // backend treats EOF as seek-to-0 instead of emitting `eof` event.
    virtual int sendSetLoopFile(bool enabled) = 0;

    // ── Lifecycle fence (callback-bearing) ─────────────────────────────────

    // PLAYER_LIFECYCLE_FIX Phase 2 — same-process stop/open fence. Sends
    // `stop` and stores onComplete to fire when the backend emits its
    // matching `stop_ack` (after teardown completes). If stop_ack doesn't
    // arrive within timeoutMs, onTimeout fires instead. Intended for
    // VideoPlayer::openFile when the backend is already running.
    virtual int sendStopWithCallback(std::function<void()> onComplete,
                                     std::function<void()> onTimeout = nullptr,
                                     int timeoutMs = 2000) = 0;

signals:
    // ── Lifecycle / state ──────────────────────────────────────────────────

    void ready();
    void stateChanged(const QString& state);
    void endOfFile();
    void processClosed();

    // Batch 6.1 — fires when QProcess::finished arrives without a prior
    // sendShutdown(). VideoPlayer drives respawn + reopen at last PTS with
    // max 3 retries + exponential backoff.
    void processCrashed(int exitCode, QProcess::ExitStatus status);

    // Batch 6.3 — non-fatal avcodec error caught and skipped past.
    void decodeError(const QString& code, const QString& message, bool recoverable);

    void errorOccurred(const QString& message);

    // ── Timing / positioning ───────────────────────────────────────────────

    void timeUpdate(double positionSec, double durationSec);
    void frameStepped(double positionSec);

    // ── Media / decoding pipeline ──────────────────────────────────────────

    void firstFrame(const QJsonObject& payload);   // shmName, width, height, slotCount, slotBytes
    void mediaInfo(const QJsonObject& info);

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.2 — classified open-pipeline
    // progress events. Drive LoadingOverlay setStage transitions.
    void probeStarted();
    // STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — extended with
    // durationIsEstimate flag for HUD `~` prefix.
    void probeDone(bool durationIsEstimate);
    void decoderOpenStarted();
    void decoderOpenDone();
    void firstPacketRead();
    void firstDecoderReceive();

    // ── Track management ───────────────────────────────────────────────────

    void tracksChanged(const QJsonArray& audio, const QJsonArray& subtitle,
                       const QString& activeAudioId, const QString& activeSubId);

    // ── Subtitle-specific events ───────────────────────────────────────────

    void subtitleText(const QString& text);
    void subVisibilityChanged(bool visible);
    void subDelayChanged(double delayMs);
    void subtitleTracksListed(const QList<SubtitleTrackInfo>& tracks, int activeIndex);
    void subtitleTrackApplied(int index);
    void subtitleUrlLoaded(const QUrl& url, const QString& localPath, bool ok);
    void subtitleOffsetChanged(int pixelOffsetY);
    void subtitleSizeChanged(double scale);

    // ── Filters / effects ──────────────────────────────────────────────────

    void filtersChanged(const QJsonObject& state);

    // ── Texture / rendering bridge ─────────────────────────────────────────

    // D3D11 shared texture NT handle for zero-copy display.
    void d3d11Texture(quintptr ntHandle, int width, int height);

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay SHM ready.
    void overlayShm(const QString& shmName, int width, int height);

    // ── Buffering / streaming ──────────────────────────────────────────────

    // PLAYER_UX_FIX Phase 2.2 — backend-observed HTTP-stall state on stream
    // URL playback. Distinct from one-shot state_changed{playing}; can fire
    // repeatedly. LoadingOverlay consumes both to toggle "Buffering…".
    void bufferingStarted();
    void bufferingEnded();

    // PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — structured cache-pause
    // progress (sentinels: etaResumeSec == -1.0 → unknown,
    // cacheDurationSec == -1.0 → bitrate unavailable).
    void cacheStateChanged(qint64 bytesAhead,
                           qint64 inputRateBps,
                           double etaResumeSec,
                           double cacheDurationSec);

    // STREAM_AUTO_NEXT_ESTIMATE_FIX 2026-04-21 — ground-truth near-end
    // signal on bitrate-estimate sources.
    void nearEndEstimate();
};
