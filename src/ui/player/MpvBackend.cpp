// TANKOBAN_HDR_PROBE: define to activate one-shot HDR capability probes (Task 6.1).
// Probe output captured 2026-05-02; define removed. Re-add #define to re-probe.
// #define TANKOBAN_HDR_PROBE 1

// MpvBackend.cpp — IPlayerBackend implementation against in-process libmpv.
// mpv owns decode/audio/transport. MAKE_MPV_BEAT_FFMPEG Task 3 adds the
// backend-owned SW render context used by MpvLibplaceboRenderer.
//
// File compiled only when CMake finds libmpv on disk (HAS_LIBMPV=1) — see
// CMakeLists.txt. When libmpv is absent, this TU isn't added to the
// Tankoban target and VideoPlayer's TANKOBAN_FORCE_MPV gate is preprocessed
// away.

#include "MpvBackend.h"

#include "core/DebugLogBuffer.h"
#include "ui/player/MpvLibplaceboRenderer.h"

#include <mpv/client.h>

extern "C" {
#include <libplacebo/colorspace.h>
}

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cstring>

namespace {

void mpvLog(const QString& msg) {
    DebugLogBuffer::instance().info("MpvBackend", msg);
}

// MAKE_MPV_SOLO Task 3 — mpv emits color primaries / TRC as strings (e.g.
// "bt.709", "pq"); the rest of the player consumes the AVCOL_PRI_* /
// AVCOL_TRC_* int constants the ffmpeg sidecar supplies. Map the strings
// libmpv actually emits today; UNSPECIFIED on anything outside the table.
int mpvPrimariesToAv(const char* s) {
    if (!s || !*s) return 2;  // AVCOL_PRI_UNSPECIFIED
    if (qstrcmp(s, "bt.709")     == 0) return 1;   // AVCOL_PRI_BT709
    if (qstrcmp(s, "bt.470bg")   == 0) return 5;   // AVCOL_PRI_BT470BG
    if (qstrcmp(s, "smpte170m")  == 0) return 6;   // AVCOL_PRI_SMPTE170M
    if (qstrcmp(s, "smpte240m")  == 0) return 7;   // AVCOL_PRI_SMPTE240M
    if (qstrcmp(s, "bt.2020")    == 0) return 9;   // AVCOL_PRI_BT2020
    if (qstrcmp(s, "dci-p3")     == 0) return 11;  // AVCOL_PRI_SMPTE431
    if (qstrcmp(s, "display-p3") == 0) return 12;  // AVCOL_PRI_SMPTE432
    return 2;
}
int mpvGammaToAv(const char* s) {
    if (!s || !*s) return 2;  // AVCOL_TRC_UNSPECIFIED
    if (qstrcmp(s, "bt.1886")    == 0) return 1;   // AVCOL_TRC_BT709-equivalent
    if (qstrcmp(s, "srgb")       == 0) return 13;  // AVCOL_TRC_IEC61966_2_1
    if (qstrcmp(s, "linear")     == 0) return 8;   // AVCOL_TRC_LINEAR
    if (qstrcmp(s, "pq")         == 0) return 16;  // AVCOL_TRC_SMPTE2084 (HDR PQ)
    if (qstrcmp(s, "hlg")        == 0) return 18;  // AVCOL_TRC_ARIB_STD_B67 (HDR HLG)
    if (qstrcmp(s, "gamma1.8")   == 0) return 4;   // AVCOL_TRC_GAMMA22-ish
    if (qstrcmp(s, "gamma2.2")   == 0) return 4;
    return 2;
}

// MAKE_MPV_SOLO Task 5 — categorize mpv playback failures into plain-English
// toast messages. Mirrors the ffmpeg side's pattern (SidecarProcess emits short,
// non-jargon error strings via errorOccurred → VideoPlayer::onError → toast HUD).
//
// Reached from MPV_EVENT_END_FILE when reason == MPV_END_FILE_REASON_ERROR. The
// error field in mpv_event_end_file is one of the negative mpv_error codes; the
// most common load-failure paths are LOADING_FAILED (file missing / corrupt /
// network drop), NOTHING_TO_PLAY (no streams), UNSUPPORTED (codec/format),
// INVALID_PARAMETER (bad path/URL). Anything else falls through to a labeled
// pass-through of mpv_error_string so future failure modes still surface
// readable text without burying the cause.
QString formatMpvPlaybackError(int errorCode, const QString& path) {
    const QString name = QFileInfo(path).fileName();
    const bool isUrl =
        path.startsWith(QStringLiteral("http://"),  Qt::CaseInsensitive) ||
        path.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
        path.startsWith(QStringLiteral("rtmp://"),  Qt::CaseInsensitive) ||
        path.startsWith(QStringLiteral("rtsp://"),  Qt::CaseInsensitive);

    switch (errorCode) {
    case MPV_ERROR_LOADING_FAILED:
        return isUrl
            ? QStringLiteral("Couldn't connect to stream. Check your network or the URL.")
            : QStringLiteral("Couldn't open '%1'. File may be missing or unreadable.").arg(name);
    case MPV_ERROR_NOTHING_TO_PLAY:
        return QStringLiteral("'%1' has no playable video or audio streams.").arg(name);
    case MPV_ERROR_UNSUPPORTED:
        return QStringLiteral("'%1' uses a codec Tankoban can't play.").arg(name);
    case MPV_ERROR_INVALID_PARAMETER:
        return QStringLiteral("Invalid file path or URL.");
    default:
        return QStringLiteral("Playback failed: %1")
                .arg(QString::fromUtf8(mpv_error_string(errorCode)));
    }
}

// Convenience: set a string option before mpv_initialize().
int setOpt(mpv_handle* h, const char* name, const char* value) {
    return mpv_set_option_string(h, name, value);
}

// Convenience: set a bool flag property after init.
int setFlag(mpv_handle* h, const char* name, bool value) {
    int v = value ? 1 : 0;
    return mpv_set_property(h, name, MPV_FORMAT_FLAG, &v);
}

// Convenience: set a double property after init.
int setDouble(mpv_handle* h, const char* name, double value) {
    return mpv_set_property(h, name, MPV_FORMAT_DOUBLE, &value);
}

// Convenience: send an async command with reply_userdata = seq.
int cmdAsync(mpv_handle* h, uint64_t seq, std::initializer_list<const char*> args) {
    QList<const char*> a(args);
    a.append(nullptr);  // mpv_command_async expects NULL terminator
    return mpv_command_async(h, seq, a.data());
}

} // namespace

// ─── Lifecycle ──────────────────────────────────────────────────────────────

MpvBackend::MpvBackend(QObject* parent)
    : IPlayerBackend(parent)
{
    qRegisterMetaType<SubtitleTrackInfo>("SubtitleTrackInfo");
    qRegisterMetaType<QList<SubtitleTrackInfo>>("QList<SubtitleTrackInfo>");

    m_pendingStopTimer = new QTimer(this);
    m_pendingStopTimer->setSingleShot(true);
    connect(m_pendingStopTimer, &QTimer::timeout, this, [this]() {
        auto cb = std::move(m_pendingStopTimeout);
        m_pendingStopComplete = nullptr;
        m_pendingStopTimeout = nullptr;
        if (cb) cb();
    });

    // MAKE_MPV_SOLO Task 10 (2026-05-01) — telemetry sample timer. Fires
    // every 5s while mpv is running; each tick reads counters via
    // mpv_get_property and appends a TelemetrySample to m_telemetrySamples.
    // Started in startTelemetry() after mpv_initialize succeeds; stopped
    // in teardownMpv before mpv_terminate_destroy. Cadence matches the
    // existing [MPV-RENDER] log line in MpvVideoWidget so cross-correlation
    // between widget-side frame counts and backend-side mpv counters is
    // straightforward.
    m_telemetryTimer = new QTimer(this);
    m_telemetryTimer->setInterval(5000);
    m_telemetryTimer->setSingleShot(false);
    connect(m_telemetryTimer, &QTimer::timeout,
            this, &MpvBackend::sampleTelemetry);

    // Wakeup callback fires on internal libmpv threads; queued connection
    // marshals back to GUI thread before draining.
    connect(this, &MpvBackend::wakeupRequested,
            this, &MpvBackend::onWakeup,
            Qt::QueuedConnection);
}

MpvBackend::~MpvBackend()
{
    teardownMpv();
}

void MpvBackend::start()
{
    if (m_mpv) {
        mpvLog("[start] already running, no-op");
        return;
    }
    initializeMpv();
}

bool MpvBackend::isRunning() const
{
    return m_running && m_mpv != nullptr;
}

void MpvBackend::ensureTerminated(int /*timeoutMs*/)
{
    // mpv_terminate_destroy is synchronous: aborts the playback core, frees
    // all resources, and returns once teardown completes. No timeout needed
    // (no subprocess to outlive us).
    teardownMpv();
}

void MpvBackend::resetAndRestart()
{
    teardownMpv();
    initializeMpv();
}

// ─── Setup / teardown ───────────────────────────────────────────────────────

void MpvBackend::initializeMpv()
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        emit errorOccurred(QStringLiteral("mpv_create failed"));
        return;
    }

    // Hermetic config — don't read user's local mpv config.
    setOpt(m_mpv, "config", "no");

    // MAKE_MPV_BEAT_FFMPEG Task 3: route video through mpv's render API.
    // The backend creates the SW render context before ready()/loadfile, so
    // mpv does not permanently fall back to the old audio-only vo=null path.
    setOpt(m_mpv, "vo", "libmpv");

    // Audio + identity.
    setOpt(m_mpv, "audio-client-name", "Tankoban");

    // MAKE_MPV_SOLO Task 10.5 (2026-05-01) — default `hwdec=no` (CPU
    // decode), TANKOBAN_MPV_HWDEC env var as escape hatch.
    //
    // Why `no` is the default: the Task 10 baseline run with `auto`
    // selected `d3d11va-copy` on Intel UHD 620 + 1080p HEVC 10-bit and
    // produced 404 frame-drop-count over 260s with bursty 70-drop windows
    // (see `out/mpv_telemetry.log` first session block). Two Task 10.5
    // smokes with `hwdec=no` produced 37 and 31 drops over 255-240s —
    // ~92% reduction. Plain `d3d11va` (zero-copy) is not available on
    // Intel UHD 620 + Windows 11 — mpv falls back to CPU decode anyway
    // — so the GPU↔CPU memcpy in `-copy` is the only thing the auto-
    // selection adds, and that's what's causing the drops. CPU decode
    // for 1080p HEVC 10-bit is well within budget on any modern x86.
    //
    // Why env var override stays: future users on hardware with working
    // d3d11va zero-copy (recent NVIDIA / AMD discrete) would benefit
    // from GPU decode. They can set TANKOBAN_MPV_HWDEC=d3d11va or
    // =auto-safe via the environment without a rebuild.
    QByteArray hwdecOverride = qgetenv("TANKOBAN_MPV_HWDEC");
    const char* hwdecValue = hwdecOverride.isEmpty() ? "no"
                                                     : hwdecOverride.constData();
    setOpt(m_mpv, "hwdec", hwdecValue);
    // MAKE_MPV_SOLO Task 12.A (2026-05-02) — remember whether the user
    // deliberately picked hwdec via env var. If they did, the file-load
    // HDR-detect auto-pick path MUST NOT override their choice. The
    // m_currentHwdec cache mirrors what we just set so the auto-pick
    // dedupe doesn't re-set unnecessarily.
    m_hwdecOverriddenByEnv = !hwdecOverride.isEmpty();
    m_currentHwdec = QString::fromUtf8(hwdecValue);
    mpvLog(QStringLiteral("[init] hwdec=%1%2")
               .arg(QString::fromUtf8(hwdecValue))
               .arg(hwdecOverride.isEmpty() ? " (default)"
                                            : " (TANKOBAN_MPV_HWDEC override)"));

    // MAKE_MPV_SOLO Task 10.7 (2026-05-02) — picture-quality parity with
    // the ffmpeg sidecar's libplacebo path. Hemanth-flagged "picture
    // quality seems better in ffmpeg" 2026-05-01 ~23:30 (post-Task-10.5
    // smoke). Root cause: mpv defaults to cheap bilinear-class scalers
    // while the ffmpeg sidecar's libplacebo path uses ewa_lanczossharp
    // upscaler + hermite downscaler (see native_sidecar/src/
    // gpu_renderer.cpp:110-111). On a 1080p source rendered into a sub-
    // 1080p video viewport (after HUD chrome), the chroma reconstruction
    // + downscale filter is doing real work — and mpv's defaults are
    // visibly softer there.
    //
    // Tier 1 picks (initial attempt 2026-05-02 ~00:05) tried libplacebo
    // verbatim — `ewa_lanczossharp` upscaler + `hermite` downscaler +
    // `ewa_lanczossharp` cscale — and tipped Intel UHD 620 OpenGL into
    // 1585 drops over 120s (13.2 drops/sec, 130× worse than Task 10.5's
    // hwdec=no floor). Lesson: libplacebo runs on Vulkan compute shaders
    // and has a much larger shader budget than mpv's OpenGL path on iGPU.
    // The polar `ewa_*` family is 2D shader work, prohibitive on Intel GL.
    //
    // Tier 0 picks (this commit, post-regression) swap to separable
    // scalers (1D horizontal + 1D vertical passes — much cheaper than
    // polar 2D, still markedly sharper than mpv's bilinear-class default):
    //   scale=spline36   (separable, sharp, ~6-tap kernel; wide-used as
    //                    high-quality preset baseline, e.g., mpv's own
    //                    profile=gpu-hq sets this)
    //   dscale=mitchell  (separable; classic high-quality downscaler that
    //                    avoids both sharp ringing and excessive softness)
    //   cscale=spline36  (separable chroma reconstruction; matches the
    //                    upscaler family for visual consistency)
    //
    // If Hemanth still finds the picture soft after this, Tier 0.5 adds
    // deband=yes (cheap perceptual uplift; addresses gradient artifacts
    // in dark scenes without GPU cost).
    // MAKE_MPV_SOLO Task 12.B diagnostic (2026-05-02 ~09:48am) —
    // temporarily flipped scalers to bilinear to test whether Task 10.7
    // Tier 0 separable-scaler GPU cost is the bottleneck on Hemanth's
    // heavy-SDR-HEVC stutter (Sopranos S06E04). If this flip restores
    // smooth playback on Sopranos AND Community SDR floor stays clean,
    // Task 12.B real fix becomes "scaler-budget-aware pick: bilinear
    // for heavy content / spline36 for light." If it doesn't help,
    // revert this and move to the next suspect (telemetry timer / hwdec).
    // setOpt(m_mpv, "scale",  "spline36");
    // setOpt(m_mpv, "dscale", "mitchell");
    // setOpt(m_mpv, "cscale", "spline36");
    setOpt(m_mpv, "scale",  "bilinear");
    setOpt(m_mpv, "dscale", "bilinear");
    setOpt(m_mpv, "cscale", "bilinear");
    mpvLog(QStringLiteral("[init] scalers: bilinear (Task 12.B diag — "
                          "testing whether Tier 0 spline36 cost is the "
                          "Sopranos stutter bottleneck)"));

    // Don't render mpv's own OSD; Tankoban draws HUD itself.
    setOpt(m_mpv, "osd-level", "0");

    // Keep the file open after EOF so seek-back works without reload (mpv
    // parity with the ffmpeg sidecar's loop-file=keep semantics on EOF).
    setOpt(m_mpv, "keep-open", "yes");

    // Subtitle behaviour. force-margins keeps libass-rendered subs inside
    // the safe area when subs script defines aggressive margins.
    setOpt(m_mpv, "sub-ass-force-margins", "yes");
    setOpt(m_mpv, "sub-visibility", "yes");

    // MPV_FFMPEG_PARITY Phase 2.D (2026-04-30) — Q5 visual-spec constants
    // mirroring ffmpeg DEFAULT_ASS_HEADER baseline (white text, black
    // outline, subtle drop shadow, BorderStyle=outline-and-shadow). Set
    // once at backend init; per-session mutable params (size/margin/
    // outline) flow through sendSetSubStyle below. sub-ass-override=no
    // honors Q4 ratification (Force-authored toggle off by default —
    // anime ASS authored styles for signs/karaoke/dialogue stay intact;
    // these constants apply mainly to text/SRT tracks where mpv's libass
    // injects its own default style).
    setOpt(m_mpv, "sub-color", "#FFFFFFFF");
    setOpt(m_mpv, "sub-border-color", "#FF000000");
    setOpt(m_mpv, "sub-shadow-color", "#80000000");
    setOpt(m_mpv, "sub-shadow-offset", "1.0");
    setOpt(m_mpv, "sub-border-style", "outline-and-shadow");
    setOpt(m_mpv, "sub-ass-override", "no");

    // Send libmpv log messages at >= warn into our debug log.
    mpv_request_log_messages(m_mpv, "warn");

    int rc = mpv_initialize(m_mpv);
    if (rc < 0) {
        emit errorOccurred(QString::fromLatin1("mpv_initialize failed: %1")
                              .arg(QString::fromUtf8(mpv_error_string(rc))));
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

#ifdef TANKOBAN_HDR_PROBE
    {
        // Step 1.3 — probe libmpv for HDR-related option acceptance.
        // This is observation-only; options are reset immediately after.
        auto probeOpt = [this](const char* opt, const char* val) {
            int prc = mpv_set_option_string(m_mpv, opt, val);
            mpvLog(QStringLiteral("[hdr-probe] %1=%2: rc=%3 %4")
                .arg(opt).arg(val).arg(prc).arg(prc == 0 ? "ACCEPTED" : "REJECTED"));
        };
        probeOpt("tone-mapping", "clip");
        probeOpt("target-trc", "auto");
        probeOpt("target-prim", "auto");
        probeOpt("target-peak", "auto");

        // Reset probe-set options so the probe is observation-only.
        // Note: "auto" may not be valid for tone-mapping; if rejected, document and skip.
        int resetTm = mpv_set_option_string(m_mpv, "tone-mapping", "auto");
        if (resetTm != 0) {
            mpvLog(QStringLiteral("[hdr-probe] tone-mapping reset to 'auto' REJECTED (rc=%1) — leaving as probed 'clip'").arg(resetTm));
            // clip is actually a valid functional value; reset back to mpv's natural default
            mpv_set_option_string(m_mpv, "tone-mapping", "hable");
        }
        mpv_set_option_string(m_mpv, "target-trc", "auto");
        mpv_set_option_string(m_mpv, "target-prim", "auto");
        mpv_set_option_string(m_mpv, "target-peak", "auto");
    }
#endif

    // MAKE_MPV_BEAT_FFMPEG Task 6 step 4 (2026-05-02) — disable mpv's
    // internal tone-mapping so the HDR signal passes through to the
    // render-API output unmodified. libplacebo (in MpvLibplaceboRenderer)
    // does the tone-mapping downstream via pl_render_params.color_map_params
    // + peak_detect_params (added in Task 6 step 2). Without these four
    // options, mpv tone-maps to whatever its default target-* heuristics
    // pick (typically SDR sRGB), which crushes the HDR range Task 3's
    // RGBA16F texture format just lifted.
    //
    // tone-mapping=clip  → don't apply any tone curve; pass values straight
    //                      through (libplacebo will apply the proper curve
    //                      once Task 5 bridges color metadata into pl_frame).
    // target-trc=auto    → let mpv pick the transfer curve to match what the
    //                      render target advertises; combined with our
    //                      RGBA16F FBO, this means linear-or-PQ-encoded HDR
    //                      to a high-bit-depth target.
    // target-prim=auto   → same auto-pick for color primaries (BT.2020 for
    //                      HDR sources; BT.709 for SDR; mpv picks based on
    //                      source).
    // target-peak=auto   → let mpv pick target peak luminance based on the
    //                      target advertised peak; defers actual peak
    //                      handling to libplacebo.
    //
    // Task 1 probe (agents/audits/mpv_hdr_capability_probe_2026-05-02.md)
    // confirmed all four options accepted at init (rc=0) on this libmpv
    // build. These fire on every init (NOT gated by TANKOBAN_HDR_PROBE);
    // on SDR sources mpv's auto-pick resolves to SDR targets so clip just
    // clips at 1.0 — no change in behaviour vs the prior defaults.
    setOpt(m_mpv, "tone-mapping", "clip");
    setOpt(m_mpv, "target-trc", "auto");
    setOpt(m_mpv, "target-prim", "auto");
    setOpt(m_mpv, "target-peak", "auto");

    m_libplaceboRenderer = std::make_unique<MpvLibplaceboRenderer>();
    if (!m_libplaceboRenderer->attachMpv(m_mpv)) {
        mpvLog(QStringLiteral("[init] SW render context unavailable; Vulkan widget will clear black"));
        m_libplaceboRenderer.reset();
    }

    observeProperties();
    mpv_set_wakeup_callback(m_mpv, &MpvBackend::wakeupCallback, this);

    // Phase 5 redux — render context creation moves to MpvVideoWidget. The
    // widget owns its QOpenGLContext via QOpenGLWidget; it grabs the mpv
    // handle via mpvHandle() and creates the render context against its own
    // GL context. Pure GL path; no D3D11/GL interop bridge (which was
    // structurally blocked on Intel iGPU).
    m_running = true;
    mpvLog(QStringLiteral("[init] api=0x%1 ready").arg(mpv_client_api_version(), 0, 16));

    // MAKE_MPV_SOLO Task 10 (2026-05-01) — start telemetry capture once
    // mpv is alive. Identity probes (hwdec/vo/ao/codecs) deferred to the
    // first sampleTelemetry tick because they're file-state-dependent
    // (codecs only resolve after the first file opens). startTelemetry
    // here just zeroes the buffers + starts the timer.
    startTelemetry();

    emit ready();
}

void MpvBackend::teardownMpv()
{
    if (!m_mpv) return;

    if (m_pendingStopTimer && m_pendingStopTimer->isActive())
        m_pendingStopTimer->stop();
    m_pendingStopComplete = nullptr;
    m_pendingStopTimeout = nullptr;

    // MAKE_MPV_SOLO Task 10 (2026-05-01) — flush telemetry BEFORE
    // mpv_terminate_destroy so the final-counter reads in dumpTelemetry
    // can still query the live handle. Stop the sampling timer first so
    // no rogue tick fires mid-dump.
    if (m_telemetryTimer && m_telemetryTimer->isActive())
        m_telemetryTimer->stop();
    dumpTelemetry();

    // P5 redux — let any external render-context owner (MpvVideoWidget) free
    // its resources BEFORE we call mpv_terminate_destroy. Direct connections
    // ensure synchronous teardown ordering on the GUI thread.
    emit mpvHandleInvalidating();
    m_libplaceboRenderer.reset();

    // Detach wakeup before destroy — libmpv may fire the callback during
    // teardown otherwise, racing our destructor.
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);

    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
    m_running = false;

    m_subtitleTracks.clear();
    m_activeSubIndex = -1;
    m_lastPositionSec = 0.0;
    m_lastDurationSec = 0.0;
    m_isPaused = false;
    m_eofReached = false;
    m_firstFrameEmitted = false;
    m_currentFilePath.clear();
    // 1.C 2026-04-30 — reset cache-edge + throttle state on every
    // session teardown so the next file-open's rising-edge detection
    // works correctly. Without this, a stale m_pausedForCache=true from
    // a session that ended mid-stall would suppress the next session's
    // bufferingStarted emit on the first real stall.
    m_pausedForCache = false;
    m_lastCacheStateEmitMs = 0;
    // 1.E 2026-04-30 — reset stall-pause gate on teardown so the next
    // session's pause-property handler isn't permanently suppressed by
    // stale state from a session that ended mid-stall.
    m_inStallPause = false;
    // 1.E.1 hotfix — reset deferred-bufferingEnded flag on teardown so
    // a session that ended between paused-for-cache=false and
    // PLAYBACK_RESTART doesn't leak the pending emit into the next session.
    m_pendingBufferingEnd = false;
    // MAKE_MPV_SOLO Task 10 (2026-05-01) — reset telemetry state so the
    // next session (resetAndRestart path) starts a fresh sample buffer +
    // re-captures identity on its first sampleTelemetry tick.
    m_telemetrySamples.clear();
    m_telemetryStartMs = 0;
    m_telemetryHwdec.clear();
    m_telemetryVo.clear();
    m_telemetryAo.clear();
    m_telemetryVideoCodec.clear();
    m_telemetryAudioCodec.clear();
    m_telemetryFileFormat.clear();
    m_telemetryFile.clear();
    m_telemetryDurationSec = 0.0;
    m_telemetryDisplayFps  = 0.0;
    // MAKE_MPV_SOLO Task 12.A — reset hwdec state so the next session's
    // initializeMpv re-reads the env var fresh and the HDR auto-pick
    // dedupe doesn't carry stale state across resetAndRestart.
    m_hwdecOverriddenByEnv = false;
    m_currentHwdec.clear();

    emit processClosed();
}

// ─── Telemetry (MAKE_MPV_SOLO Task 10) ─────────────────────────────────────

void MpvBackend::startTelemetry()
{
    // Called from initializeMpv() right after mpv_initialize succeeds.
    // Identity probes (hwdec/vo/ao/codecs/file) deferred to the first
    // sampleTelemetry tick because most of those properties only resolve
    // after a file is actually loaded — at startTelemetry time the player
    // is alive but no file is open yet on cold start.
    m_telemetrySamples.clear();
    m_telemetryStartMs = QDateTime::currentMSecsSinceEpoch();
    if (m_telemetryTimer && !m_telemetryTimer->isActive())
        m_telemetryTimer->start();
}

namespace {

// Helper: read an mpv string property, copy into QString, mpv_free the
// returned char*. Returns empty QString on read failure (property absent
// or property error). All mpv_get_property_string callers in this file
// honor the same null-check + mpv_free pattern.
QString mpvReadString(mpv_handle* mpv, const char* name)
{
    if (!mpv) return {};
    if (char* p = mpv_get_property_string(mpv, name)) {
        QString s = QString::fromUtf8(p);
        mpv_free(p);
        return s;
    }
    return {};
}

// Helper: read a mpv int property via MPV_FORMAT_INT64. Returns -1 on
// read failure (most counters never go negative; -1 is a clear sentinel).
qint64 mpvReadInt64(mpv_handle* mpv, const char* name)
{
    if (!mpv) return -1;
    int64_t v = -1;
    if (mpv_get_property(mpv, name, MPV_FORMAT_INT64, &v) < 0) return -1;
    return v;
}

// Helper: read a mpv double property via MPV_FORMAT_DOUBLE. Returns 0.0
// on read failure — caller should interpret 0.0 as "not yet measured"
// rather than authoritative zero (estimated-vf-fps, playback-time, etc.
// all start at 0 and grow).
double mpvReadDouble(mpv_handle* mpv, const char* name)
{
    if (!mpv) return 0.0;
    double v = 0.0;
    if (mpv_get_property(mpv, name, MPV_FORMAT_DOUBLE, &v) < 0) return 0.0;
    return v;
}

// Helper: read a mpv bool property via MPV_FORMAT_FLAG.
bool mpvReadFlag(mpv_handle* mpv, const char* name)
{
    if (!mpv) return false;
    int v = 0;
    if (mpv_get_property(mpv, name, MPV_FORMAT_FLAG, &v) < 0) return false;
    return v != 0;
}

} // namespace

void MpvBackend::sampleTelemetry()
{
    if (!m_mpv) return;

    // Lazy identity capture on the first tick after a file becomes live.
    // We detect "file is now loaded" by checking whether any of the
    // identity properties returns a non-empty value; if so, latch them.
    // hwdec-current is the most reliable single signal here — it stays
    // empty pre-file-load and resolves to "no" / "d3d11va-copy" / etc.
    // post-decode-open. video-codec also resolves at the same moment.
    if (m_telemetryHwdec.isEmpty()) {
        const QString hwdec = mpvReadString(m_mpv, "hwdec-current");
        if (!hwdec.isEmpty()) {
            m_telemetryHwdec       = hwdec;
            m_telemetryVo          = mpvReadString(m_mpv, "current-vo");
            m_telemetryAo          = mpvReadString(m_mpv, "current-ao");
            m_telemetryVideoCodec  = mpvReadString(m_mpv, "video-codec");
            m_telemetryAudioCodec  = mpvReadString(m_mpv, "audio-codec");
            m_telemetryFileFormat  = mpvReadString(m_mpv, "file-format");
            m_telemetryFile        = mpvReadString(m_mpv, "filename");
            m_telemetryDurationSec = mpvReadDouble(m_mpv, "duration");
            m_telemetryDisplayFps  = mpvReadDouble(m_mpv, "display-fps");
        }
    }

    TelemetrySample s;
    s.elapsedMs        = QDateTime::currentMSecsSinceEpoch() - m_telemetryStartMs;
    s.frameDropCount   = static_cast<int>(mpvReadInt64(m_mpv, "frame-drop-count"));
    s.voDelayedCount   = static_cast<int>(mpvReadInt64(m_mpv, "vo-delayed-frame-count"));
    s.estimatedVfFps   = mpvReadDouble(m_mpv, "estimated-vf-fps");
    s.playbackTimeSec  = mpvReadDouble(m_mpv, "playback-time");
    s.pausedForCache   = mpvReadFlag(m_mpv, "paused-for-cache");
    m_telemetrySamples.append(s);

    // Bound memory: 5s cadence × 1024 cap = ~85 minutes per session block,
    // way past any normal play length. Drop oldest if we ever exceed (ring
    // semantic) so a forgotten background instance doesn't leak forever.
    constexpr int kCap = 1024;
    if (m_telemetrySamples.size() > kCap) {
        m_telemetrySamples.remove(0, m_telemetrySamples.size() - kCap);
    }
}

void MpvBackend::dumpTelemetry()
{
    if (m_telemetrySamples.isEmpty()) return;

    // Mirror SidecarProcess::dumpIpcLatency path-resolution: prefer out/
    // for consistency with stream_telemetry.log + ipc_latency.log; fall
    // back to repo root if out/ doesn't exist (fresh-checkout edge).
    QString path = QStringLiteral("out/mpv_telemetry.log");
    QFileInfo outDir(QStringLiteral("out"));
    if (!outDir.exists() || !outDir.isDir()) {
        path = QStringLiteral("mpv_telemetry.log");
    }

    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream stream(&f);

    // ── Session header ────────────────────────────────────────────────────
    stream << "## session_end=" << QDateTime::currentDateTime().toString(Qt::ISODate)
           << " samples=" << m_telemetrySamples.size()
           << " duration_sec=" << QString::number(m_telemetryDurationSec, 'f', 2)
           << " display_fps=" << QString::number(m_telemetryDisplayFps, 'f', 2)
           << " hwdec=" << (m_telemetryHwdec.isEmpty() ? "none" : m_telemetryHwdec)
           << " vo=" << (m_telemetryVo.isEmpty() ? "?" : m_telemetryVo)
           << " ao=" << (m_telemetryAo.isEmpty() ? "?" : m_telemetryAo)
           << " video_codec=" << (m_telemetryVideoCodec.isEmpty() ? "?" : m_telemetryVideoCodec)
           << " audio_codec=" << (m_telemetryAudioCodec.isEmpty() ? "?" : m_telemetryAudioCodec)
           << " file_format=" << (m_telemetryFileFormat.isEmpty() ? "?" : m_telemetryFileFormat)
           << " file=" << (m_telemetryFile.isEmpty() ? "?" : m_telemetryFile)
           << "\n";

    // ── Per-sample rows ──────────────────────────────────────────────────
    for (const auto& s : m_telemetrySamples) {
        stream << "sample t=" << (s.elapsedMs / 1000) << "s"
               << " drops=" << s.frameDropCount
               << " vo_delayed=" << s.voDelayedCount
               << " vf_fps=" << QString::number(s.estimatedVfFps, 'f', 2)
               << " playtime=" << QString::number(s.playbackTimeSec, 'f', 2) << "s"
               << " buffering=" << (s.pausedForCache ? "true" : "false")
               << "\n";
    }

    // ── Summary line ─────────────────────────────────────────────────────
    int totalDrops    = 0;
    int totalDelayed  = 0;
    int bufferingTicks = 0;
    double sumFps     = 0.0;
    int    fpsSampleCount = 0;
    int finalDrops   = m_telemetrySamples.last().frameDropCount;
    int finalDelayed = m_telemetrySamples.last().voDelayedCount;
    int firstDrops   = m_telemetrySamples.first().frameDropCount;
    int firstDelayed = m_telemetrySamples.first().voDelayedCount;
    if (firstDrops   < 0) firstDrops   = 0;
    if (firstDelayed < 0) firstDelayed = 0;
    if (finalDrops   < 0) finalDrops   = 0;
    if (finalDelayed < 0) finalDelayed = 0;
    totalDrops   = qMax(0, finalDrops   - firstDrops);
    totalDelayed = qMax(0, finalDelayed - firstDelayed);
    for (const auto& s : m_telemetrySamples) {
        if (s.pausedForCache) ++bufferingTicks;
        if (s.estimatedVfFps > 0.0) {
            sumFps += s.estimatedVfFps;
            ++fpsSampleCount;
        }
    }
    const double avgFps = (fpsSampleCount > 0) ? (sumFps / fpsSampleCount) : 0.0;
    stream << "## summary"
           << " avg_vf_fps=" << QString::number(avgFps, 'f', 2)
           << " total_drops=" << totalDrops
           << " total_vo_delayed=" << totalDelayed
           << " buffering_ticks=" << bufferingTicks
           << "/" << m_telemetrySamples.size()
           << "\n\n";

    f.close();
}

void MpvBackend::observeProperties()
{
    if (!m_mpv) return;
    // Reply userdata is unused — we dispatch on property name in handler.
    mpv_observe_property(m_mpv, 0, "time-pos",        MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration",        MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause",           MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached",     MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "track-list",      MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "aid",             MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "sid",             MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "sub-visibility",  MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "sub-delay",       MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "audio-delay",     MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "metadata",        MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "filename",        MPV_FORMAT_STRING);
    // 1.C 2026-04-30 — production cache-state observations.
    // paused-for-cache (FLAG): edge-driven; rising→bufferingStarted,
    //   falling→bufferingEnded. Silent in healthy steady-state per 1.B §3.
    // demuxer-cache-state (NODE): rich payload (cache-end, reader-pts,
    //   cache-duration, fw-bytes, raw-input-rate, ts-per-stream, ...);
    //   parsed via mpv_get_property_string("demuxer-cache-state") JSON
    //   in the dispatcher branch, synthesized into cacheStateChanged's
    //   4-field shape. Throttled at emit-time (m_lastCacheStateEmitMs)
    //   to ~2Hz so observation cadence (sub-second native) doesn't flood
    //   the GUI consumer. cache-buffering-state + cache-secs intentionally
    //   NOT observed — 1.B §4 evidence proved both are silent in healthy
    //   steady-state and redundant with demuxer-cache-state.cache-duration.
    mpv_observe_property(m_mpv, 0, "paused-for-cache",     MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "demuxer-cache-state",  MPV_FORMAT_NODE);
}

// ─── Wakeup → drain events on GUI thread ────────────────────────────────────

void MpvBackend::wakeupCallback(void* d)
{
    // Fires on a libmpv-internal thread. Marshal to GUI by emitting our
    // queued signal — the connection in the ctor delivers via
    // Qt::QueuedConnection.
    auto* self = static_cast<MpvBackend*>(d);
    if (!self) return;
    emit self->wakeupRequested();
}

void MpvBackend::onWakeup()
{
    if (!m_mpv) return;
    while (true) {
        mpv_event* ev = mpv_wait_event(m_mpv, 0.0);
        if (!ev || ev->event_id == MPV_EVENT_NONE) break;

        switch (ev->event_id) {
        case MPV_EVENT_SHUTDOWN:
            mpvLog("[event] SHUTDOWN");
            teardownMpv();
            return;  // m_mpv is null after teardown — stop draining

        case MPV_EVENT_PROPERTY_CHANGE:
            if (ev->data) handlePropertyChange(static_cast<mpv_event_property*>(ev->data));
            break;

        case MPV_EVENT_FILE_LOADED:
            mpvLog("[event] FILE_LOADED");
            // Phase 3: synthesize the open-pipeline progression that
            // VideoPlayer's HUD relies on. Real progression timing comes
            // in Phase 5 once we have visibility into the decode pipeline.
            emit probeStarted();
            emit probeDone(/*durationIsEstimate*/ false);
            emit decoderOpenStarted();
            emit decoderOpenDone();
            emit firstPacketRead();
            emit firstDecoderReceive();
            // MAKE_MPV_SOLO Task 3 — rich mediaInfo bridge. Mirrors the
            // ffmpeg sidecar's media_info shape (native_sidecar/main.cpp:538-548)
            // so VideoPlayer's mediaInfo handler at VideoPlayer.cpp:3712-3760
            // populates HDR badge / color shader / chapter list / per-device
            // audio-delay recall identically on both backends.
            {
                QJsonObject mi;
                mi.insert("duration_sec", m_lastDurationSec);
                mi.insert("file_path", m_currentFilePath);

                // Color primaries — string from mpv → AV enum int.
                if (char* p = mpv_get_property_string(m_mpv, "video-params/primaries")) {
                    mi.insert("color_primaries", mpvPrimariesToAv(p));
                    mpv_free(p);
                } else {
                    mi.insert("color_primaries", 2);  // UNSPECIFIED
                }

                // Color TRC — needed for both the shader fields AND the HDR
                // boolean. Capture the string for the HDR derivation below.
                QString trcStr;
                if (char* t = mpv_get_property_string(m_mpv, "video-params/gamma")) {
                    trcStr = QString::fromUtf8(t);
                    mi.insert("color_trc", mpvGammaToAv(t));
                    mpv_free(t);
                } else {
                    mi.insert("color_trc", 2);
                }

                // HDR — PQ or HLG TRC means HDR transfer function. Mirrors
                // demuxer.cpp:450-452 which sets hdr=true on SMPTE2084/HLG.
                const bool isHdr = (trcStr == QStringLiteral("pq")
                                    || trcStr == QStringLiteral("hlg"));
                mi.insert("hdr", isHdr);

                // MAKE_MPV_SOLO Task 12.A (2026-05-02) — HDR-conditional
                // hwdec auto-pick. Task 10.5 measured `hwdec=no` (CPU
                // decode) as the floor for SDR HEVC on Intel UHD 620
                // (0.10-0.24 drops/sec). Task 12 soak surfaced HDR HEVC
                // sustains 6-8 drops/sec on the same CPU path — HDR adds
                // 10-bit decode pressure + tone-mapping shader cost that
                // pushes the CPU decoder over budget. d3d11va-copy on
                // SDR was 1.55 drops/sec (worse than CPU) but on HDR is
                // expected to be dramatically better (the GPU↔CPU memcpy
                // cost is the same, but GPU decode handles 10-bit HEVC
                // far cheaper than CPU). So: HDR file → flip to
                // d3d11va-copy automatically. SDR stays on hwdec=no.
                //
                // Honor env-var override: if the user set TANKOBAN_MPV_HWDEC
                // explicitly at app launch, NEVER override here — they
                // know what they want. This is auto-pick only for the
                // default-path users.
                //
                // Dedupe via m_currentHwdec cache — if the previous file
                // was also HDR and we already flipped to d3d11va-copy,
                // skip the re-set (avoids unnecessary decoder reinit).
                if (isHdr
                    && !m_hwdecOverriddenByEnv
                    && m_currentHwdec != QStringLiteral("d3d11va-copy")) {
                    const char* hdrHwdec = "d3d11va-copy";
                    int rc = mpv_set_property_string(m_mpv, "hwdec", hdrHwdec);
                    if (rc >= 0) {
                        m_currentHwdec = QString::fromUtf8(hdrHwdec);
                        mpvLog(QStringLiteral("[hdr-detect] gamma=%1 → "
                                               "hwdec %2 → %3 (auto-pick)")
                                   .arg(trcStr,
                                        QStringLiteral("no"),
                                        m_currentHwdec));
                    } else {
                        mpvLog(QStringLiteral("[hdr-detect] gamma=%1 but "
                                               "hwdec swap failed: %2")
                                   .arg(trcStr,
                                        QString::fromUtf8(mpv_error_string(rc))));
                    }
                } else if (!isHdr
                           && !m_hwdecOverriddenByEnv
                           && m_currentHwdec == QStringLiteral("d3d11va-copy")) {
                    // Reverse path: prior file was HDR and flipped us to
                    // d3d11va-copy; this file is SDR. Flip back to `no`
                    // so we don't pay the d3d11va-copy memcpy cost on
                    // SDR (where it measured worse than CPU at 1.55 vs
                    // 0.24 drops/sec in Task 10.5).
                    const char* sdrHwdec = "no";
                    int rc = mpv_set_property_string(m_mpv, "hwdec", sdrHwdec);
                    if (rc >= 0) {
                        m_currentHwdec = QString::fromUtf8(sdrHwdec);
                        mpvLog(QStringLiteral("[hdr-detect] SDR file → "
                                               "hwdec d3d11va-copy → no "
                                               "(auto-pick)"));
                    }
                }

                // Chapter list — mpv emits {title, time}; ffmpeg side uses
                // {title, start}. Translate so VideoPlayer.cpp:3743's
                // c.value("start").toDouble() works on both.
                if (char* json = mpv_get_property_string(m_mpv, "chapter-list")) {
                    QJsonParseError perr;
                    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json), &perr);
                    if (perr.error == QJsonParseError::NoError && doc.isArray()) {
                        QJsonArray chapters;
                        for (const auto& v : doc.array()) {
                            const QJsonObject c = v.toObject();
                            QJsonObject out;
                            out.insert("title", c.value("title").toString());
                            out.insert("start", c.value("time").toDouble());
                            chapters.append(out);
                        }
                        mi.insert("chapters", chapters);
                    }
                    mpv_free(json);
                }

                // Audio device + host API. mpv's `audio-device` is the user's
                // device pick (often "auto"); `current-ao` is the actual audio
                // output module (e.g. "wasapi"). Together they form a stable
                // per-device key for VideoPlayer's audio-delay recall.
                if (char* d = mpv_get_property_string(m_mpv, "audio-device")) {
                    mi.insert("audio_device", QString::fromUtf8(d));
                    mpv_free(d);
                }
                if (char* a = mpv_get_property_string(m_mpv, "current-ao")) {
                    mi.insert("audio_host_api", QString::fromUtf8(a));
                    mpv_free(a);
                }

                emit mediaInfo(mi);

                // MAKE_MPV_BEAT_FFMPEG Task 6 step 5 — push HDR metadata to renderer
                // so libplacebo can tone-map correctly on the new RGBA16F interop path
                // (Task 6 step 3 + step 4 set up the texture format + mpv passthrough;
                // this call delivers the missing third leg — actual color metadata).
                pushSourceColorSpaceToRenderer();
            }
            break;

        case MPV_EVENT_END_FILE: {
            // MAKE_MPV_SOLO Task 5 — surface playback failures as user-facing
            // toast messages. Pre-fix this case treated every end-file as a
            // clean EOF, including error endings, so failed loads went silent.
            // mpv tags the reason in event data + carries the error code on
            // ERROR reason; map it to a plain-English string and route through
            // the same errorOccurred → VideoPlayer::onError → toast HUD path
            // the ffmpeg sidecar already uses.
            auto* eef = static_cast<mpv_event_end_file*>(ev->data);
            const int reason = eef ? eef->reason : MPV_END_FILE_REASON_EOF;
            mpvLog(QString("[event] END_FILE reason=%1").arg(reason));
            if (reason == MPV_END_FILE_REASON_ERROR && eef) {
                const QString msg = formatMpvPlaybackError(eef->error, m_currentFilePath);
                mpvLog(QString("[error] code=%1 (%2): %3")
                          .arg(eef->error)
                          .arg(QString::fromUtf8(mpv_error_string(eef->error)))
                          .arg(msg));
                emit errorOccurred(msg);
            }
            emit endOfFile();
            break;
        }

        case MPV_EVENT_PLAYBACK_RESTART:
            // Fires on every seek-completion + initial first-frame +
            // frame-step completion. First fires the firstFrame stub
            // (Phase 5 redux now uses real dimensions via Fix 1);
            // subsequent fires emit `frameStepped` so HUD time labels
            // snap to the new position immediately rather than waiting
            // for the next 1Hz time-update tick (Z/X frame-step UX).
            // 1.E.1 hotfix 2026-04-30: also consume the deferred
            // bufferingEnded set by paused-for-cache falling-edge —
            // PLAYBACK_RESTART is the honest "frames actually resumed"
            // signal, so we dismiss the LoadingOverlay here rather than
            // when mpv merely declared the buffer non-constraint. Order
            // matters: emit bufferingEnded BEFORE firstFrame stub so any
            // overlay-dismiss handlers see "buffering ended" first, then
            // "first frame rendered" — matches the cold-open semantics.
            if (m_pendingBufferingEnd) {
                m_pendingBufferingEnd = false;
                emit bufferingEnded();
            }
            if (!m_firstFrameEmitted) {
                emitFirstFrameStub();
                m_firstFrameEmitted = true;
            } else if (m_mpv) {
                double pos = 0.0;
                if (mpv_get_property(m_mpv, "time-pos",
                                      MPV_FORMAT_DOUBLE, &pos) == 0) {
                    emit frameStepped(pos);
                }
            }
            break;

        case MPV_EVENT_LOG_MESSAGE: {
            auto* msg = static_cast<mpv_event_log_message*>(ev->data);
            if (msg && msg->text) {
                QString text = QString::fromUtf8(msg->text).trimmed();
                // P6.1 Fix 3 — drop the recurring Intel iGPU false-positive
                // emitted during libmpv's render-context texture-format
                // probe. libmpv tries multiple GL texture configurations
                // and some return INVALID_ENUM on Intel UHD; the supported
                // configs still work + rendering proceeds normally. Known-
                // benign per upstream issue tracker; suppress here so the
                // ring buffer doesn't fill with the same line every paint.
                if (text.contains(QStringLiteral("after creating texture: OpenGL error INVALID_ENUM"),
                                  Qt::CaseInsensitive)) {
                    break;
                }
                if (!text.isEmpty()) {
                    mpvLog(QString("[mpv:%1/%2] %3")
                               .arg(QString::fromUtf8(msg->prefix ? msg->prefix : ""))
                               .arg(QString::fromUtf8(msg->level ? msg->level : ""))
                               .arg(text));
                }
            }
            break;
        }

        case MPV_EVENT_COMMAND_REPLY:
            // sendStopWithCallback completion handler — the MPV_EVENT_END_FILE
            // path covers it for the stop command. Future commands that need
            // ack semantics can match on ev->reply_userdata.
            if (m_pendingStopComplete && ev->reply_userdata != 0) {
                auto cb = std::move(m_pendingStopComplete);
                m_pendingStopComplete = nullptr;
                m_pendingStopTimeout = nullptr;
                if (m_pendingStopTimer) m_pendingStopTimer->stop();
                if (cb) cb();
            }
            break;

        default:
            break;
        }
    }
}

void MpvBackend::handlePropertyChange(mpv_event_property* prop)
{
    if (!prop || !prop->name) return;
    const QString name = QString::fromUtf8(prop->name);

    if (name == QLatin1String("time-pos")) {
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            m_lastPositionSec = *static_cast<double*>(prop->data);
            emit timeUpdate(m_lastPositionSec, m_lastDurationSec);
        }
    } else if (name == QLatin1String("duration")) {
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            m_lastDurationSec = *static_cast<double*>(prop->data);
        }
    } else if (name == QLatin1String("pause")) {
        if (prop->format == MPV_FORMAT_FLAG && prop->data) {
            const bool wasPaused = m_isPaused;
            m_isPaused = *static_cast<int*>(prop->data) != 0;
            if (m_isPaused != wasPaused) {
                // 1.E 2026-04-30 — suppress stateChanged emit during a
                // transparent network stall. mpv pauses its decoder when
                // paused-for-cache fires true; the resulting pause-property
                // event would otherwise flip the user-visible play/pause
                // icon to "paused" mid-stall. m_inStallPause is set/cleared
                // by the paused-for-cache dispatcher branch below; while
                // it's true, we update m_isPaused so internal callers
                // (e.g. togglePause) see the correct mpv state, but skip
                // the externally-visible stateChanged emit. ffmpeg sidecar
                // parity: native_sidecar/src/main.cpp:1162-1184
                // handle_stall_pause is documented as explicitly not
                // emitting state_changed for the same reason.
                if (!m_inStallPause) {
                    emit stateChanged(m_isPaused ? QStringLiteral("paused")
                                                  : QStringLiteral("playing"));
                }
            }
        }
    } else if (name == QLatin1String("eof-reached")) {
        if (prop->format == MPV_FORMAT_FLAG && prop->data) {
            m_eofReached = *static_cast<int*>(prop->data) != 0;
            if (m_eofReached) emit endOfFile();
        }
    } else if (name == QLatin1String("track-list")) {
        // mpv emits the track-list as a node-array of objects. We rely on
        // mpv_get_property_string(json) to keep parsing simple — the cost
        // (allocate/free a JSON string per change) is fine because
        // track-list changes are rare (per-file, not per-frame).
        if (m_mpv) {
            char* json = mpv_get_property_string(m_mpv, "track-list");
            if (json) {
                QJsonParseError err{};
                const auto doc = QJsonDocument::fromJson(QByteArray(json), &err);
                mpv_free(json);
                if (err.error == QJsonParseError::NoError && doc.isArray()) {
                    QJsonArray audio, sub;
                    QString activeAud, activeSub;
                    QList<SubtitleTrackInfo> subs;
                    int subIdx = 0;
                    int activeSubIdx = -1;
                    for (const auto v : doc.array()) {
                        const auto o = v.toObject();
                        const QString type = o.value(QStringLiteral("type")).toString();
                        const QString id = QString::number(o.value(QStringLiteral("id")).toInt());
                        // MAKE_MPV_SOLO Task 6 (2026-05-01) — re-emit `id` as
                        // a string in the per-track JSON object before
                        // appending to audio/sub. mpv emits id as int; the
                        // ffmpeg sidecar emits as string. VideoPlayer's
                        // mergeTrackList reads `id` as QString — the int
                        // case skips every track via `if (id.isEmpty())`,
                        // leaving the embedded sub track-list empty in the
                        // right-click "Subtitles" submenu and the bottom-HUD
                        // SubtitlePopover (Pattern A root cause from Task 1
                        // baseline). Fix: clone the object, overwrite `id`
                        // with the stringified value, then append.
                        QJsonObject o2 = o;
                        o2.insert(QStringLiteral("id"), id);
                        if (type == QLatin1String("audio")) {
                            audio.append(o2);
                            if (o.value(QStringLiteral("selected")).toBool()) activeAud = id;
                        } else if (type == QLatin1String("sub")) {
                            sub.append(o2);
                            if (o.value(QStringLiteral("selected")).toBool()) {
                                activeSub = id;
                                activeSubIdx = subIdx;
                            }
                            SubtitleTrackInfo info;
                            info.index    = subIdx;
                            info.sidecarId = id;
                            info.lang     = o.value(QStringLiteral("lang")).toString();
                            info.title    = o.value(QStringLiteral("title")).toString();
                            info.codec    = o.value(QStringLiteral("codec")).toString();
                            info.external = o.value(QStringLiteral("external")).toBool();
                            subs.append(info);
                            ++subIdx;
                        }
                    }
                    m_subtitleTracks = subs;
                    m_activeSubIndex = activeSubIdx;
                    emit tracksChanged(audio, sub, activeAud, activeSub);
                    emit subtitleTracksListed(subs, activeSubIdx);
                }
            }
        }
    } else if (name == QLatin1String("aid") || name == QLatin1String("sid")) {
        // Cached active-track ids are surfaced via the next track-list
        // change; don't emit standalone signals here to avoid spurious
        // wiring in VideoPlayer.
    } else if (name == QLatin1String("sub-visibility")) {
        if (prop->format == MPV_FORMAT_FLAG && prop->data) {
            emit subVisibilityChanged(*static_cast<int*>(prop->data) != 0);
        }
    } else if (name == QLatin1String("sub-delay")) {
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            // mpv reports sub-delay in seconds; IPlayerBackend convention is ms.
            emit subDelayChanged(*static_cast<double*>(prop->data) * 1000.0);
        }
    } else if (name == QLatin1String("filename")) {
        if (prop->format == MPV_FORMAT_STRING && prop->data) {
            char* s = *static_cast<char**>(prop->data);
            if (s) m_currentFilePath = QString::fromUtf8(s);
        }
    }
    // 1.C 2026-04-30 — paused-for-cache → bufferingStarted/bufferingEnded
    // edge translation. Edge-only; mpv re-fires the property event on
    // every value change but we only emit on actual boolean transitions.
    // Decoupled from m_isPaused (user-paused state) per IPlayerBackend.h:
    // bufferingStarted/Ended is the network-stall signal; pause property
    // is the user-pause signal (handled separately at line 360-368). 1.E
    // will gate stateChanged emit on a m_inStallPause flag set/cleared
    // here; for 1.C we only emit the buffering edges, preserving Pre-1.E
    // behavior for the play/pause icon (which will spuriously flip on
    // stall transitions until 1.E lands — Hemanth's eyes-on-screen smoke
    // for 1.C must NOT block on this; flag in 1.C RTC).
    else if (name == QLatin1String("paused-for-cache")) {
        if (prop->format == MPV_FORMAT_FLAG && prop->data) {
            const bool nowPaused = *static_cast<int*>(prop->data) != 0;
            if (nowPaused != m_pausedForCache) {
                m_pausedForCache = nowPaused;
                m_inStallPause = nowPaused;  // 1.E gates pause-property stateChanged emit
                if (nowPaused) {
                    emit bufferingStarted();
                } else {
                    // 1.E.1 hotfix 2026-04-30 — DON'T emit bufferingEnded
                    // immediately. paused-for-cache=false means "buffer is
                    // no longer the constraint" but frames may not have
                    // resumed rendering yet (decoder catch-up window).
                    // Defer to the next MPV_EVENT_PLAYBACK_RESTART which
                    // fires when mpv actually resumes producing frames.
                    // Without this defer, the LoadingOverlay dismisses
                    // while the user still sees a frozen frame — the bug
                    // Hemanth flagged at ~18:50pm.
                    m_pendingBufferingEnd = true;
                }
            }
        }
    }
    // 1.C 2026-04-30 — demuxer-cache-state → cacheStateChanged 4-field
    // synthesis. Throttled to ~2Hz (500ms minimum gap) to match ffmpeg
    // sidecar cadence at native_sidecar/src/main.cpp:752-780. Native mpv
    // event cadence is sub-second per 1.B §2 (180+ events / 5 min steady
    // state); unthrottled re-emit would flood LoadingOverlay UI-thread
    // consumer. Parse via mpv_get_property_string JSON same as the
    // track-list handler above (line 380-419) — mpv_node walking is
    // verbose for one-off probes; JSON path is clean.
    else if (name == QLatin1String("demuxer-cache-state")) {
        if (prop->format != MPV_FORMAT_NODE || !prop->data || !m_mpv) return;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastCacheStateEmitMs < 500) return;  // 2Hz throttle
        m_lastCacheStateEmitMs = nowMs;

        char* json = mpv_get_property_string(m_mpv, "demuxer-cache-state");
        if (!json) return;
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(QByteArray(json), &err);
        mpv_free(json);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

        const QJsonObject obj = doc.object();
        // Per IPlayerBackend.h:243-244 sentinels:
        //   etaResumeSec == -1.0 → unknown (mpv doesn't atomically
        //     provide ETA in this node; computing from cache-duration /
        //     raw-input-rate is brittle and consumer accepts unknown)
        //   cacheDurationSec == -1.0 → bitrate / cache-duration unavailable
        const qint64 bytesAhead   =
            static_cast<qint64>(obj.value(QStringLiteral("fw-bytes")).toDouble(0.0));
        const qint64 inputRateBps =
            static_cast<qint64>(obj.value(QStringLiteral("raw-input-rate")).toDouble(0.0));
        const double cacheDurSec  =
            obj.contains(QStringLiteral("cache-duration"))
            ? obj.value(QStringLiteral("cache-duration")).toDouble(-1.0)
            : -1.0;
        emit cacheStateChanged(bytesAhead, inputRateBps,
                               /*etaResumeSec=*/-1.0,
                               cacheDurSec);
    }
    // metadata / audio-delay / others — Phase 6 wires per-feature.
}

void MpvBackend::emitFirstFrameStub()
{
    // P6.1 — Query mpv for real native video dimensions. VideoPlayer's
    // aspect-drift safety net at VideoPlayer.cpp:1027 gates on `w > 0 &&
    // h > 0` and resets stale persisted aspectOverride if drift > 10%.
    // Using `width`/`height` (NOT `dwidth`/`dheight`) gives pre-aspect-
    // override source dims — matches the safety net's nativeAspect = w/h
    // formula. shmName/slotCount/slotBytes stay empty/0 — those are SHM-
    // overlay-specific (sidecar path); mpv path doesn't use them.
    int64_t w = 0;
    int64_t h = 0;
    if (m_mpv) {
        mpv_get_property(m_mpv, "width",  MPV_FORMAT_INT64, &w);
        mpv_get_property(m_mpv, "height", MPV_FORMAT_INT64, &h);
    }
    QJsonObject payload;
    payload.insert("shmName", QString());
    payload.insert("width",  static_cast<int>(w));
    payload.insert("height", static_cast<int>(h));
    payload.insert("slotCount", 0);
    payload.insert("slotBytes", 0);
    emit firstFrame(payload);
}

int MpvBackend::nextSeq()
{
    return ++m_seq;
}

// ─── Playback control commands ─────────────────────────────────────────────

int MpvBackend::sendOpen(const QString& filePath, double startSeconds)
{
    if (!m_mpv) return -1;
    m_currentFilePath = filePath;
    m_firstFrameEmitted = false;
    if (m_libplaceboRenderer) {
        m_libplaceboRenderer->resetFrameState();
    }
    m_eofReached = false;
    const QByteArray utf8 = filePath.toUtf8();
    // mpv 0.40+ loadfile signature: <url> [<flags> [<index> [<options>]]].
    // Pass index="-1" (default) when we need to attach per-file options like
    // start=, otherwise the parser interprets the option string as the
    // playlist index integer and bails with "must be an integer".
    if (startSeconds > 0.0) {
        const QByteArray startArg = QByteArrayLiteral("start=+") + QByteArray::number(startSeconds);
        const int seq = nextSeq();
        cmdAsync(m_mpv, seq, {"loadfile", utf8.constData(), "replace", "-1", startArg.constData()});
        return seq;
    }
    const int seq = nextSeq();
    cmdAsync(m_mpv, seq, {"loadfile", utf8.constData(), "replace"});
    return seq;
}

int MpvBackend::sendPause()
{
    if (!m_mpv) return -1;
    setFlag(m_mpv, "pause", true);
    return nextSeq();
}

int MpvBackend::sendResume()
{
    if (!m_mpv) return -1;
    setFlag(m_mpv, "pause", false);
    return nextSeq();
}

int MpvBackend::sendStallPause()
{
    // 1.D 2026-04-30 — engine-driven stall path (StreamServerEngine::
    // stallDetected → StreamPage → VideoPlayer::onStreamStallEdgeFromEngine
    // → here). Set m_inStallPause BEFORE calling sendPause so the resulting
    // pause-property event handled at handlePropertyChange:382-389 sees the
    // flag and skips the externally-visible stateChanged emit. Without this,
    // engine-driven stalls flip the user-visible play/pause icon to "paused"
    // even though it's a transparent network stall — same bug 1.E base
    // closed for the mpv-self-detected (paused-for-cache) path.
    // mpv's own paused-for-cache typically fires alongside in practice
    // (cache exhaustion when stream-server can't supply), but the engine
    // path may fire first OR alone depending on stall shape — guarding
    // both paths is the safe semantic.
    m_inStallPause = true;
    return sendPause();
}

int MpvBackend::sendStallResume()
{
    // 1.D 2026-04-30 — clear the gate before sendResume so the resulting
    // pause-property=false event correctly emits stateChanged("playing").
    m_inStallPause = false;
    return sendResume();
}

int MpvBackend::sendSeek(double positionSec)
{
    if (!m_mpv) return -1;
    const int seq = nextSeq();
    const QByteArray pos = QByteArray::number(positionSec, 'f', 3);
    cmdAsync(m_mpv, seq, {"seek", pos.constData(), "absolute"});
    return seq;
}

int MpvBackend::sendSeek(double positionSec, const QString& modeOverride)
{
    if (!m_mpv) return -1;
    const int seq = nextSeq();
    const QByteArray pos = QByteArray::number(positionSec, 'f', 3);
    const QByteArray mode = (modeOverride == QLatin1String("exact"))
                              ? QByteArrayLiteral("absolute+exact")
                              : QByteArrayLiteral("absolute+keyframes");
    cmdAsync(m_mpv, seq, {"seek", pos.constData(), mode.constData()});
    return seq;
}

int MpvBackend::sendSetSeekMode(const QString& mode)
{
    if (!m_mpv) return -1;
    // mpv's hr-seek=yes means "exact"; default "keyframes" is "fast".
    const char* val = (mode == QLatin1String("exact")) ? "yes" : "no";
    setOpt(m_mpv, "hr-seek", val);
    return nextSeq();
}

int MpvBackend::sendFrameStep(bool backward, double /*currentPosSec*/)
{
    if (!m_mpv) return -1;
    const int seq = nextSeq();
    cmdAsync(m_mpv, seq, {backward ? "frame-back-step" : "frame-step"});
    return seq;
}

int MpvBackend::sendStop()
{
    if (!m_mpv) return -1;
    const int seq = nextSeq();
    cmdAsync(m_mpv, seq, {"stop"});
    return seq;
}

int MpvBackend::sendShutdown()
{
    if (!m_mpv) return -1;
    // Quit asks libmpv to stop playback + emit MPV_EVENT_SHUTDOWN; we tear
    // down on receipt. For symmetry with SidecarProcess.sendShutdown we
    // also tear down synchronously after a short async fire.
    cmdAsync(m_mpv, 0, {"quit"});
    return nextSeq();
}

// ─── Audio commands ────────────────────────────────────────────────────────

int MpvBackend::sendSetVolume(double volume)
{
    if (!m_mpv) return -1;
    // Tankoban convention: 0..1.0 linear; mpv's volume is 0..100 percent.
    const double pct = qBound(0.0, volume, 1.0) * 100.0;
    setDouble(m_mpv, "volume", pct);
    return nextSeq();
}

int MpvBackend::sendSetMute(bool muted)
{
    if (!m_mpv) return -1;
    setFlag(m_mpv, "mute", muted);
    return nextSeq();
}

int MpvBackend::sendSetRate(double rate)
{
    if (!m_mpv) return -1;
    setDouble(m_mpv, "speed", rate);
    return nextSeq();
}

int MpvBackend::sendSetAudioDelay(int delayMs)
{
    if (!m_mpv) return -1;
    setDouble(m_mpv, "audio-delay", static_cast<double>(delayMs) / 1000.0);
    return nextSeq();
}

int MpvBackend::sendSetAudioSpeed(double /*speed*/)
{
    if (!m_mpv) return -1;
    // MAKE_MPV_SOLO Task 8 (2026-05-01) — no-op on mpv. Pre-fix routed to
    // sendSetRate which writes to the `speed` property — that changes
    // playback speed (video + audio together), not just audio sync.
    // Actively harmful. mpv handles A/V sync internally via its own audio
    // clock + resampler (audio-pts mode); no manual ±5% drift correction
    // needed (which is what the ffmpeg sidecar's set_audio_speed does for
    // its own clock). Accept the IPlayerBackend call but ignore it.
    return nextSeq();
}

int MpvBackend::sendSetDrcEnabled(bool enabled)
{
    if (!m_mpv) return -1;
    // MAKE_MPV_SOLO Task 8 (2026-05-01) — incremental af-chain edit using
    // mpv's `af-add` / `af-remove` commands with a `@drc:` label.
    // Pre-fix: setOpt("af", "acompressor"|"") OVERWROTE the entire audio
    // filter chain — any other filters (sendRawFilters, future EQ) would
    // get clobbered each time DRC was toggled. The label-prefixed form
    // (`@drc:`) lets us add/remove this specific filter by name without
    // touching neighbors. Idempotent: af-add on a label that already
    // exists is benign; af-remove on a label that doesn't exist is benign.
    const int seq = nextSeq();
    if (enabled) {
        cmdAsync(m_mpv, seq, {"af-add", "@drc:acompressor"});
    } else {
        cmdAsync(m_mpv, seq, {"af-remove", "@drc"});
    }
    return seq;
}

// ─── Track selection ───────────────────────────────────────────────────────

int MpvBackend::sendSetTracks(const QString& audioId, const QString& subId)
{
    if (!m_mpv) return -1;
    if (!audioId.isEmpty()) {
        const QByteArray a = audioId.toUtf8();
        setOpt(m_mpv, "aid", a.constData());
    }
    if (!subId.isEmpty()) {
        const QByteArray s = subId.toUtf8();
        setOpt(m_mpv, "sid", s.constData());
    }
    return nextSeq();
}

int MpvBackend::sendSetSubtitleTrack(int index)
{
    if (!m_mpv) return -1;
    if (index < 0) {
        setOpt(m_mpv, "sid", "no");
    } else if (index < m_subtitleTracks.size()) {
        const QByteArray id = m_subtitleTracks.at(index).sidecarId.toUtf8();
        setOpt(m_mpv, "sid", id.constData());
    }
    emit subtitleTrackApplied(index);
    return nextSeq();
}

// ─── Subtitle commands ─────────────────────────────────────────────────────

int MpvBackend::sendSetSubVisibility(bool visible)
{
    if (!m_mpv) return -1;
    setFlag(m_mpv, "sub-visibility", visible);
    return nextSeq();
}

int MpvBackend::sendSetSubDelay(double delayMs)
{
    if (!m_mpv) return -1;
    setDouble(m_mpv, "sub-delay", delayMs / 1000.0);
    return nextSeq();
}

int MpvBackend::sendSetSubStyle(int fontSize, int marginV, bool outline)
{
    if (!m_mpv) return -1;
    // MPV_FFMPEG_PARITY Phase 2.D (2026-04-30) — was a Phase-3 stub. Now
    // wires Tankoban's 3-param style payload (fontSize / marginV /
    // outline) to mpv's libass via the corresponding sub-* properties.
    // Q5 fixed visual constants (color / outline-color / shadow / border
    // style / ass-override) live in initializeMpv since they don't change
    // per-session. Per Q3 ratification the font-family pick lives in Task
    // 6 (2.G UI surface), so this signature stays 3-param. fontSize maps
    // to sub-font-size (mpv unit: scaled pixels at 720-tall reference,
    // default 55); marginV to sub-margin-y (same unit); outline toggles
    // sub-border-size between 2px (on) and 0 (off) mirroring the ffmpeg
    // DEFAULT_ASS_HEADER Outline=2 / Outline=0 (outline-off variant).
    if (fontSize > 0)
        setDouble(m_mpv, "sub-font-size", static_cast<double>(fontSize));
    if (marginV >= 0)
        setDouble(m_mpv, "sub-margin-y", static_cast<double>(marginV));
    setOpt(m_mpv, "sub-border-size", outline ? "2" : "0");
    return nextSeq();
}

int MpvBackend::sendSetSubtitlePosition(int percent)
{
    if (!m_mpv) return -1;
    // mpv sub-pos is 0..100 (100 = bottom). IPlayerBackend agrees.
    setDouble(m_mpv, "sub-pos", static_cast<double>(qBound(0, percent, 100)));
    return nextSeq();
}

int MpvBackend::sendSetSubtitlePositionMode(const QString& mode)
{
    if (!m_mpv) return -1;
    // MAKE_MPV_SOLO Task 6 (2026-05-01) — Force mode now wires to mpv's
    // `sub-ass-override` property. Maps:
    //   "standard" → sub-ass-override=no    (default; preserve authored
    //                ASS layout — signs/karaoke/multi-event scripts
    //                stay where the script wrote them)
    //   "force"    → sub-ass-override=force (override authored positions
    //                + margins; sub-pos / sub-margin-y now win against
    //                aggressive-MarginV scripts)
    // Replaces Phase 2.F's warning-only stub. Same Q4 ratification still
    // holds: default is Standard so anime ASS authored styles persist
    // unless user explicitly toggles Force in the SettingsPopover.
    setOpt(m_mpv, "sub-ass-override",
           mode == QStringLiteral("force") ? "force" : "no");
    return nextSeq();
}

int MpvBackend::sendLoadExternalSub(const QString& path)
{
    if (!m_mpv) return -1;
    const int seq = nextSeq();
    const QByteArray p = path.toUtf8();
    cmdAsync(m_mpv, seq, {"sub-add", p.constData(), "select"});
    return seq;
}

int MpvBackend::sendSetSubtitleUrl(const QUrl& url, int offsetPx, int delayMs)
{
    if (!m_mpv) return -1;
    const int seq = nextSeq();
    const QByteArray u = url.isLocalFile()
                            ? url.toLocalFile().toUtf8()
                            : url.toString().toUtf8();
    cmdAsync(m_mpv, seq, {"sub-add", u.constData(), "select"});
    // MAKE_MPV_SOLO Task 6 (2026-05-01) — apply offsetPx + delayMs after
    // the sub-add command. Pre-fix these args were dropped (the /*offsetPx*/
    // /*delayMs*/ comments named them as deliberately ignored). Now: offset
    // routes through sub-margin-y (same pixel mapping as
    // sendSetSubtitlePixelOffset above), and delay routes through sub-delay
    // (mpv's native sub-timing offset, in seconds; we receive ms).
    if (offsetPx != 0) {
        setDouble(m_mpv, "sub-margin-y", static_cast<double>(offsetPx));
    }
    if (delayMs != 0) {
        setDouble(m_mpv, "sub-delay", static_cast<double>(delayMs) / 1000.0);
    }
    emit subtitleUrlLoaded(url, QString::fromUtf8(u), true);
    return seq;
}

int MpvBackend::sendSetSubtitlePixelOffset(int pixelOffsetY)
{
    if (!m_mpv) return -1;
    // MAKE_MPV_SOLO Task 6 (2026-05-01) — pixel-offset now wires to mpv's
    // native `sub-margin-y` property (in pixels, top-aligned for top
    // tracks / bottom-aligned for bottom tracks). The ffmpeg side hacks
    // this via libass Y-translation; mpv owns it natively. Direct
    // mapping: positive pixelOffsetY moves subs further from edge.
    setDouble(m_mpv, "sub-margin-y", static_cast<double>(pixelOffsetY));
    emit subtitleOffsetChanged(pixelOffsetY);
    return nextSeq();
}

int MpvBackend::sendSetSubtitleSize(double scale)
{
    if (!m_mpv) return -1;
    // mpv sub-scale is a multiplier; default 1.0.
    setDouble(m_mpv, "sub-scale", scale);
    emit subtitleSizeChanged(scale);
    return nextSeq();
}

int MpvBackend::sendSetSubtitleDelayMs(int ms)
{
    return sendSetSubDelay(static_cast<double>(ms));
}

QList<SubtitleTrackInfo> MpvBackend::listSubtitleTracks() const
{
    return m_subtitleTracks;
}

int MpvBackend::activeSubtitleIndex() const
{
    return m_activeSubIndex;
}

// ─── Filters / rendering ───────────────────────────────────────────────────

int MpvBackend::sendSetFilters(bool /*deinterlace*/, int brightness,
                                int /*contrast*/, int /*saturation*/,
                                bool /*normalize*/, bool /*interpolate*/,
                                const QString& /*deinterlaceFilter*/)
{
    // MAKE_MPV_SOLO Task 9 (2026-05-01) — brightness-only fill. Hemanth
    // narrowed Task 9 scope to brightness; contrast/saturation stay
    // ignored on the mpv path (and unsurfaced in the UI). mpv's
    // `brightness` property is integer -100..+100, default 0, applied
    // through the GPU shader (no vf chain rebuild — instant + flicker-free,
    // satisfies the slider live-update reliability gate). Other params
    // are accepted but not propagated; the IPlayerBackend signature is
    // preserved so the sidecar path can keep using contrast=100,
    // saturation=100 as neutrals.
    if (!m_mpv) return -1;
    if (brightness < -100) brightness = -100;
    if (brightness > 100)  brightness = 100;
    setOpt(m_mpv, "brightness",
           QByteArray::number(brightness).constData());
    return nextSeq();
}

int MpvBackend::sendRawFilters(const QString& videoFilter, const QString& audioFilter)
{
    if (!m_mpv) return -1;
    if (!videoFilter.isEmpty()) setOpt(m_mpv, "vf", videoFilter.toUtf8().constData());
    if (!audioFilter.isEmpty()) setOpt(m_mpv, "af", audioFilter.toUtf8().constData());
    return nextSeq();
}

int MpvBackend::sendSetToneMapping(const QString& algorithm, bool peakDetect)
{
    if (!m_mpv) return -1;
    if (!algorithm.isEmpty()) setOpt(m_mpv, "tone-mapping", algorithm.toUtf8().constData());
    // MAKE_MPV_SOLO Task 4 Phase E (2026-05-01) — was setFlag on
    // hdr-peak-decay-rate, which is mpv's *numeric* decay-rate option
    // (default 100ms), not a boolean flag — pre-fix this line silently
    // no-op'd. The boolean for enabling per-scene peak detection is
    // hdr-compute-peak. Mirrors libplacebo's pl_peak_detect_params toggle
    // on the ffmpeg sidecar side (gpu_renderer.cpp:234).
    setFlag(m_mpv, "hdr-compute-peak", peakDetect);
    return nextSeq();
}

void MpvBackend::pushSourceColorSpaceToRenderer()
{
    if (!m_mpv || !m_libplaceboRenderer) return;

    pl_color_space csp{};

    // ── color primaries ──────────────────────────────────────────
    char* primStr = nullptr;
    mpv_get_property(m_mpv, "video-params/primaries", MPV_FORMAT_STRING, &primStr);
    if (primStr) {
        QString p = QString::fromUtf8(primStr);
        mpv_free(primStr);
        if      (p == QLatin1String("bt.2020"))    csp.primaries = PL_COLOR_PRIM_BT_2020;
        else if (p == QLatin1String("bt.709"))     csp.primaries = PL_COLOR_PRIM_BT_709;
        else if (p == QLatin1String("bt.601-525")) csp.primaries = PL_COLOR_PRIM_BT_601_525;
        else if (p == QLatin1String("bt.601-625")) csp.primaries = PL_COLOR_PRIM_BT_601_625;
        else if (p == QLatin1String("dci-p3"))     csp.primaries = PL_COLOR_PRIM_DCI_P3;
        else if (p == QLatin1String("display-p3")) csp.primaries = PL_COLOR_PRIM_DISPLAY_P3;
        else                                        csp.primaries = PL_COLOR_PRIM_UNKNOWN;
    }

    // ── transfer function (gamma curve) ─────────────────────────
    char* gammaStr = nullptr;
    mpv_get_property(m_mpv, "video-params/gamma", MPV_FORMAT_STRING, &gammaStr);
    if (gammaStr) {
        QString g = QString::fromUtf8(gammaStr);
        mpv_free(gammaStr);
        if      (g == QLatin1String("pq"))      csp.transfer = PL_COLOR_TRC_PQ;
        else if (g == QLatin1String("hlg"))     csp.transfer = PL_COLOR_TRC_HLG;
        else if (g == QLatin1String("bt.1886")) csp.transfer = PL_COLOR_TRC_BT_1886;
        else if (g == QLatin1String("srgb"))    csp.transfer = PL_COLOR_TRC_SRGB;
        else if (g == QLatin1String("linear"))  csp.transfer = PL_COLOR_TRC_LINEAR;
        else                                     csp.transfer = PL_COLOR_TRC_UNKNOWN;
    }

    // ── peak luminance (HDR metadata) ────────────────────────────
    // mpv reports "sig-peak" as a relative multiplier of nominal peak (1.0
    // = SDR reference white = ~203 nits). Scale to nits for libplacebo's
    // hdr.max_luma field, which expects absolute candela/m².
    double peak = 0.0;
    mpv_get_property(m_mpv, "video-params/sig-peak", MPV_FORMAT_DOUBLE, &peak);
    if (peak > 0.0) {
        csp.hdr.max_luma = static_cast<float>(peak * PL_COLOR_SDR_WHITE);
    }

    m_libplaceboRenderer->setSourceColorSpace(csp);

    mpvLog(QStringLiteral("[task6-hdr] pushed source color space: prim=%1 trc=%2 peak_nits=%3")
        .arg(static_cast<int>(csp.primaries))
        .arg(static_cast<int>(csp.transfer))
        .arg(static_cast<double>(csp.hdr.max_luma), 0, 'f', 1));
}

int MpvBackend::sendSetZeroCopyActive(bool /*active*/)
{
    // Zero-copy NT-handle path is sidecar-specific; mpv's render API path
    // (Phase 5) provides its own zero-copy via WGL_NV_DX_interop. Stub.
    return nextSeq();
}

int MpvBackend::sendSetCanvasSize(int /*width*/, int /*height*/)
{
    // Phase 5 wires this to mpv_render_context_render's FBO size update.
    return nextSeq();
}

int MpvBackend::sendResize(int /*width*/, int /*height*/)
{
    // Same as canvas-size for mpv. Phase 5.
    return nextSeq();
}

int MpvBackend::sendSetLoopFile(bool enabled)
{
    if (!m_mpv) return -1;
    setOpt(m_mpv, "loop-file", enabled ? "inf" : "no");
    return nextSeq();
}

// ─── Lifecycle fence ───────────────────────────────────────────────────────

int MpvBackend::sendStopWithCallback(std::function<void()> onComplete,
                                      std::function<void()> onTimeout,
                                      int timeoutMs)
{
    if (!m_mpv) return -1;
    m_pendingStopComplete = std::move(onComplete);
    m_pendingStopTimeout = std::move(onTimeout);
    if (m_pendingStopTimer) m_pendingStopTimer->start(timeoutMs);
    // Use a non-zero reply_userdata so MPV_EVENT_COMMAND_REPLY fires the
    // completion handler rather than being ignored.
    const uint64_t userdata = static_cast<uint64_t>(nextSeq());
    cmdAsync(m_mpv, userdata, {"stop"});
    return static_cast<int>(userdata);
}
