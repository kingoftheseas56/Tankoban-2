#include "ui/player/SidecarProcess.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <algorithm>

static void debugLog(const QString& msg) {
    QFile f("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
    f.open(QIODevice::Append | QIODevice::Text);
    QTextStream s(&f);
    s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
}

// Path to the sidecar executable. Post-migration (2026-04-15) the sidecar
// source lives at `{repoRoot}/native_sidecar/`; build.ps1 installs the
// produced exe at `{repoRoot}/resources/ffmpeg_sidecar/ffmpeg_sidecar.exe`.
// Production path is "next to Tankoban.exe" (build_and_run.bat copies the
// installed exe into the build dir alongside the app). Dev fallbacks walk
// up from the app exe to find either the in-repo install location or the
// raw sidecar build output, so a one-off sidecar rebuild via native_sidecar/
// build.ps1 is immediately picked up without re-running build_and_run.bat.
static QString sidecarPath()
{
    QString appDir = QCoreApplication::applicationDirPath();

    // Primary: next to the app exe (production layout).
    QString local = appDir + "/ffmpeg_sidecar.exe";
    if (QFile::exists(local))
        return local;

    // Dev fallback 1: in-repo install dir. When running from `out/` inside
    // the repo, `appDir/../resources/ffmpeg_sidecar/` is the build.ps1
    // install target.
    QString installed = appDir + "/../resources/ffmpeg_sidecar/ffmpeg_sidecar.exe";
    if (QFile::exists(installed))
        return installed;

    // Dev fallback 2: raw sidecar build output. Useful during active
    // sidecar development when the user hasn't run `build.ps1`'s install
    // step yet — the exe lives directly in `native_sidecar/build/`.
    QString raw = appDir + "/../native_sidecar/build/ffmpeg_sidecar.exe";
    if (QFile::exists(raw))
        return raw;

    // Transitional fallback: pre-migration groundwork location. Remains
    // for a session or two while dev machines get their first in-repo
    // sidecar build; can be removed once everyone has rebuilt locally.
    QString gw = "C:/Users/Suprabha/Desktop/TankobanQTGroundWork/resources/ffmpeg_sidecar/ffmpeg_sidecar.exe";
    if (QFile::exists(gw))
        return gw;

    return {};
}

SidecarProcess::SidecarProcess(QObject* parent)
    : QObject(parent)
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    // Batch 5.2 — register SubtitleTrackInfo for queued signal emissions
    // (e.g., subtitleTracksListed across threads). Idempotent per Qt.
    qRegisterMetaType<SubtitleTrackInfo>("SubtitleTrackInfo");
    qRegisterMetaType<QList<SubtitleTrackInfo>>("QList<SubtitleTrackInfo>");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &SidecarProcess::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &SidecarProcess::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SidecarProcess::onProcessFinished);
}

SidecarProcess::~SidecarProcess()
{
    // VIDEO_IPC_INSTRUMENTATION 2026-04-24 — flush tracker before teardown.
    // Safe even if m_ipcLatencies is empty (dumpIpcLatency early-returns).
    dumpIpcLatency();

    if (m_process->state() != QProcess::NotRunning) {
        sendShutdown();
        ensureTerminated(3000);
    }
}

void SidecarProcess::ensureTerminated(int timeoutMs)
{
    // CLOSE_AUDIO_CONTINUES_FIX 2026-04-26. Wait for graceful exit; force-kill
    // if exceeded. Idempotent (early-returns when already NotRunning) so
    // double-calls from rapid close+open sequences are harmless.
    if (m_process->state() == QProcess::NotRunning) return;
    m_process->waitForFinished(timeoutMs);
    if (m_process->state() != QProcess::NotRunning) {
        debugLog(QString("[Sidecar] graceful shutdown timed out after %1ms — force-killing pid=%2")
                 .arg(timeoutMs).arg(m_process->processId()));
        m_process->kill();
        // Brief reap window so QProcess marks NotRunning before any
        // immediately-following m_process->start() call inspects state.
        m_process->waitForFinished(200);
    }
}

void SidecarProcess::start()
{
    QString path = sidecarPath();
    if (path.isEmpty()) {
        emit errorOccurred("ffmpeg_sidecar.exe not found");
        return;
    }

    debugLog("[Sidecar] starting: " + path);
    m_intentionalShutdown = false;
    m_process->start(path, QStringList());
    if (!m_process->waitForStarted(5000)) {
        debugLog("[Sidecar] FAILED to start");
        emit errorOccurred("Failed to start ffmpeg_sidecar.exe");
    } else {
        debugLog("[Sidecar] started OK, pid=" + QString::number(m_process->processId()));
    }
}

bool SidecarProcess::isRunning() const
{
    return m_process->state() == QProcess::Running;
}

// ── Commands ────────────────────────────────────────────────────────────────

int SidecarProcess::sendCommand(const QString& name, const QJsonObject& payload)
{
    int seq = ++m_seq;
    QJsonObject cmd;
    cmd["type"] = "cmd";
    cmd["name"] = name;
    cmd["sessionId"] = m_sessionId;
    cmd["seq"] = seq;
    cmd["payload"] = payload;

    QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
    debugLog("[Sidecar] SEND: " + QString::fromUtf8(line.trimmed()));
    m_process->write(line);

    // VIDEO_IPC_INSTRUMENTATION 2026-04-24 — stamp send-time per seq for the
    // round-trip tracker. Matched by recordIpcAck when the sidecar's `ack`
    // event arrives in processLine. Cap the pending-map at
    // kIpcPendingMaxSize so un-acked commands (pre-rebuild sidecar, or a
    // handler that skipped write_ack) don't grow the map without bound —
    // drop oldest arbitrary entries if exceeded. Typical session holds
    // single-digit pending entries.
    if (m_ipcPending.size() >= kIpcPendingMaxSize) {
        m_ipcPending.erase(m_ipcPending.begin());
    }
    m_ipcPending.insert(seq, PendingSend{ name, QDateTime::currentMSecsSinceEpoch() });
    return seq;
}

int SidecarProcess::sendOpen(const QString& filePath, double startSeconds)
{
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject payload;
    // Don't mangle HTTP URLs with native separators
    payload["path"] = filePath.startsWith("http", Qt::CaseInsensitive)
        ? filePath : QDir::toNativeSeparators(filePath);
    payload["startSeconds"] = startSeconds;
    if (m_canvasWidth > 0 && m_canvasHeight > 0) {
        payload["canvasWidth"] = m_canvasWidth;
        payload["canvasHeight"] = m_canvasHeight;
    }
    return sendCommand("open", payload);
}

int SidecarProcess::sendPause()   { return sendCommand("pause"); }
int SidecarProcess::sendResume()  { return sendCommand("resume"); }
// STREAM_AV_SUB_SYNC_AFTER_STALL 2026-04-21 — mpv paused-for-cache shape.
// Sidecar distinguishes from user pause/resume: stall_pause freezes
// AVSyncClock + halts PortAudio writes without flipping UI state;
// stall_resume seek_anchor's the clock to current video PTS + restarts
// audio output. Coordinated by StreamEngine::stallDetected/stallRecovered
// signals routed through StreamPage → SidecarProcess. Pre-fix sidecars
// don't know these commands → they return NOT_IMPLEMENTED which gets
// silently swallowed (same forward-compat pattern as sendSetSeekMode).
int SidecarProcess::sendStallPause()  { return sendCommand("stall_pause"); }
int SidecarProcess::sendStallResume() { return sendCommand("stall_resume"); }
int SidecarProcess::sendStop()    { return sendCommand("stop"); }
int SidecarProcess::sendShutdown(){
    m_intentionalShutdown = true;
    return sendCommand("shutdown");
}

int SidecarProcess::sendStopWithCallback(std::function<void()> onComplete,
                                          std::function<void()> onTimeout,
                                          int timeoutMs)
{
    // Last-click-wins: overwriting a still-pending callback is fine, the
    // prior stop_ack's seq mismatches the new m_pendingStopSeq and gets
    // silently dropped.
    m_pendingStopCallback = std::move(onComplete);
    m_pendingStopTimeoutCallback = std::move(onTimeout);
    const int seq = sendCommand("stop");
    m_pendingStopSeq = seq;

    QPointer<SidecarProcess> guard(this);
    QTimer::singleShot(timeoutMs, this, [guard, seq]() {
        if (!guard) return;
        if (guard->m_pendingStopSeq != seq) return;  // already acked or replaced
        debugLog(QString("[Sidecar] stop_ack timeout seq=%1 (falling back)").arg(seq));
        auto cb = std::move(guard->m_pendingStopTimeoutCallback);
        guard->m_pendingStopSeq = -1;
        guard->m_pendingStopCallback = {};
        guard->m_pendingStopTimeoutCallback = {};
        if (cb) cb();
    });
    return seq;
}

void SidecarProcess::resetAndRestart()
{
    if (m_process->state() != QProcess::NotRunning) {
        debugLog("[Sidecar] resetAndRestart: killing hung process");
        m_intentionalShutdown = true;
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    // Clear any stale pending-stop state — the process that would have
    // sent stop_ack is gone.
    m_pendingStopSeq = -1;
    m_pendingStopCallback = {};
    m_pendingStopTimeoutCallback = {};
    start();
}

int SidecarProcess::sendSetLoopFile(bool enabled)
{
    QJsonObject p;
    p["enabled"] = enabled;
    return sendCommand("set_loop_file", p);
}

int SidecarProcess::sendSeek(double positionSec)
{
    QJsonObject p;
    p["positionSec"] = positionSec;
    return sendCommand("seek", p);
}

int SidecarProcess::sendSeek(double positionSec, const QString& modeOverride)
{
    QJsonObject p;
    p["positionSec"] = positionSec;
    if (!modeOverride.isEmpty()) {
        p["mode"] = modeOverride;
    }
    return sendCommand("seek", p);
}

int SidecarProcess::sendSetSeekMode(const QString& mode)
{
    QJsonObject p;
    p["mode"] = mode;
    return sendCommand("set_seek_mode", p);
}

int SidecarProcess::sendFrameStep(bool backward, double currentPosSec)
{
    QJsonObject p;
    p["backward"] = backward;
    if (backward)
        p["positionSec"] = currentPosSec;
    return sendCommand("frame_step", p);
}

int SidecarProcess::sendSetVolume(double volume)
{
    QJsonObject p;
    p["volume"] = volume;
    return sendCommand("set_volume", p);
}

int SidecarProcess::sendSetMute(bool muted)
{
    QJsonObject p;
    p["muted"] = muted;
    return sendCommand("set_mute", p);
}

int SidecarProcess::sendSetRate(double rate)
{
    QJsonObject p;
    p["rate"] = rate;
    return sendCommand("set_rate", p);
}

int SidecarProcess::sendSetAudioSpeed(double speed)
{
    // Batch 4.1 — ±5% clamp. Outside this range the swr_set_compensation
    // delta becomes large enough that pitch artifacts creep in;
    // Kodi's ActiveAE caps at the same value.
    if (speed < 0.95) speed = 0.95;
    if (speed > 1.05) speed = 1.05;
    QJsonObject p;
    p["speed"] = speed;
    return sendCommand("set_audio_speed", p);
}

int SidecarProcess::sendSetDrcEnabled(bool enabled)
{
    // Batch 4.3 — trivial wrapper. Sidecar applies the compressor in its
    // audio-decode thread post-volume. Pre-Phase-4 sidecars ignore the
    // command (NOT_IMPLEMENTED error, no break) — safe to ship ahead of
    // sidecar rebuild.
    QJsonObject p;
    p["enabled"] = enabled;
    return sendCommand("set_drc_enabled", p);
}

int SidecarProcess::sendSetTracks(const QString& audioId, const QString& subId)
{
    QJsonObject p;
    if (!audioId.isEmpty()) p["audio_id"] = audioId;
    if (!subId.isEmpty())   p["sub_id"]   = subId;
    return sendCommand("set_tracks", p);
}

int SidecarProcess::sendSetSubVisibility(bool visible)
{
    QJsonObject p;
    p["visible"] = visible;
    return sendCommand("set_sub_visibility", p);
}

int SidecarProcess::sendSetSubDelay(double delayMs)
{
    QJsonObject p;
    p["delay_ms"] = (int)delayMs;
    return sendCommand("set_sub_delay", p);
}

int SidecarProcess::sendSetSubtitlePosition(int percent)
{
    QJsonObject p;
    p["percent"] = percent;
    return sendCommand("set_sub_position", p);
}

int SidecarProcess::sendSetAudioDelay(int delayMs)
{
    QJsonObject p;
    p["delay_ms"] = delayMs;
    return sendCommand("set_audio_delay", p);
}

int SidecarProcess::sendSetSubStyle(int fontSize, int marginV, bool outline)
{
    QJsonObject p;
    p["font_size"] = fontSize;
    p["margin_v"] = marginV;
    p["outline"] = outline;
    return sendCommand("set_sub_style", p);
}

int SidecarProcess::sendLoadExternalSub(const QString& path)
{
    QJsonObject p;
    p["path"] = path;
    return sendCommand("load_external_sub", p);
}

int SidecarProcess::sendSetFilters(bool deinterlace, int brightness, int contrast, int saturation, bool normalize, bool interpolate, const QString& deinterlaceFilter)
{
    QStringList videoParts;
    if (interpolate)
        videoParts << "minterpolate=fps=60:mi_mode=dup";
    // Use explicit deinterlace filter string if provided, else fall back to bool
    if (!deinterlaceFilter.isEmpty())
        videoParts << deinterlaceFilter;
    else if (deinterlace)
        videoParts << "yadif=mode=0";
    double b = brightness / 100.0;
    double c = contrast / 100.0;
    double s = saturation / 100.0;
    if (brightness != 0 || contrast != 100 || saturation != 100)
        videoParts << QString("eq=brightness=%1:contrast=%2:saturation=%3")
                          .arg(b, 0, 'f', 2).arg(c, 0, 'f', 2).arg(s, 0, 'f', 2);

    QJsonObject p;
    p["video"] = videoParts.join(",");
    p["audio"] = normalize ? QStringLiteral("loudnorm=I=-16") : QStringLiteral("");
    return sendCommand("set_filters", p);
}

int SidecarProcess::sendRawFilters(const QString& videoFilter, const QString& audioFilter)
{
    QJsonObject p;
    p["video"] = videoFilter;
    p["audio"] = audioFilter;
    return sendCommand("set_filters", p);
}

int SidecarProcess::sendSetToneMapping(const QString& algorithm, bool peakDetect)
{
    QJsonObject p;
    p["algorithm"] = algorithm;
    p["peak_detect"] = peakDetect;
    return sendCommand("set_tone_mapping", p);
}

int SidecarProcess::sendSetZeroCopyActive(bool active)
{
    QJsonObject p;
    p["active"] = active;
    return sendCommand("set_zero_copy_active", p);
}

int SidecarProcess::sendSetCanvasSize(int width, int height)
{
    m_canvasWidth = width;
    m_canvasHeight = height;
    QJsonObject p;
    p["width"] = width;
    p["height"] = height;
    return sendCommand("set_canvas_size", p);
}

int SidecarProcess::sendResize(int width, int height)
{
    QJsonObject p;
    p["width"] = width;
    p["height"] = height;
    return sendCommand("resize", p);
}

// ── Event parsing ───────────────────────────────────────────────────────────

void SidecarProcess::onReadyRead()
{
    m_readBuffer += m_process->readAllStandardOutput();

    while (true) {
        int nl = m_readBuffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = m_readBuffer.left(nl).trimmed();
        m_readBuffer = m_readBuffer.mid(nl + 1);
        if (!line.isEmpty())
            processLine(line);
    }
}

void SidecarProcess::processLine(const QByteArray& line)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError)
        return;

    QJsonObject obj = doc.object();
    if (obj["type"].toString() != "evt")
        return;

    QString name = obj["name"].toString();

    // PLAYER_LIFECYCLE_FIX Phase 1 Batch 1.1 — sessionId filter.
    // sendOpen() regenerates m_sessionId per open; sendCommand() stamps it
    // on every outgoing command. The native sidecar mirrors that sid on
    // events (per audit agents/audits/tankostream_session_lifecycle_2026-04-15.md:112).
    // Drop session-scoped events whose sessionId doesn't match the current
    // open — otherwise stale time_update / first_frame / tracks_changed
    // from a prior session rewrites new-session state on the Qt side.
    // Process-global events (no session context) pass through. Missing
    // sessionId is tolerated so legacy sidecar binaries keep working until
    // the next rebuild.
    static const QSet<QString> kProcessGlobalEvents = {
        QStringLiteral("ready"),
        QStringLiteral("closed"),
        QStringLiteral("shutdown_ack"),
        QStringLiteral("version"),
        QStringLiteral("process_error"),
    };
    if (!kProcessGlobalEvents.contains(name)) {
        const QString eventSid = obj["sessionId"].toString();
        if (!eventSid.isEmpty() && eventSid != m_sessionId) {
            debugLog(QString("[Sidecar] drop stale event: %1 eventSid=%2 currentSid=%3")
                         .arg(name, eventSid, m_sessionId));
            return;
        }
    }

    QJsonObject payload = obj["payload"].toObject();
    debugLog("[Sidecar] RECV: " + name);

    // VIDEO_IPC_INSTRUMENTATION 2026-04-24 — generic `ack` event from the
    // sidecar's write_ack(). Fires for ~every handle_*() — see protocol.cpp
    // write_ack → write_event("ack", sid, seqAck). Records round-trip
    // latency per command and returns; no payload processing needed.
    if (name == QStringLiteral("ack")) {
        recordIpcAck(obj.value("seqAck").toInt(-1));
        return;
    }

    if (name == "ready") {
        emit ready();
    } else if (name == "first_frame") {
        emit firstFrame(payload);
    } else if (name == "time_update") {
        emit timeUpdate(payload["positionSec"].toDouble(),
                        payload["durationSec"].toDouble());
    } else if (name == "state_changed") {
        emit stateChanged(payload["state"].toString());
    } else if (name == "tracks_changed") {
        // Batch 5.2 — update the subtitle cache BEFORE emitting so any
        // listener using the cache on tracksChanged signal (or triggered
        // from it) sees consistent state. Also emits subtitleTracksListed
        // for menu consumers.
        const QJsonArray subtitleArr = payload["subtitle"].toArray();
        const QString activeSubId = payload["active_sub_id"].toString();
        updateSubtitleCache(subtitleArr, activeSubId);
        emit tracksChanged(
            payload["audio"].toArray(),
            subtitleArr,
            payload["active_audio_id"].toString(),
            activeSubId);
    } else if (name == "eof") {
        emit endOfFile();
    } else if (name == "error") {
        // NOT_IMPLEMENTED is the sidecar's graceful-degradation signal for
        // commands a pre-Phase-4 / pre-feature binary doesn't recognize
        // (e.g., set_audio_speed, set_drc_enabled from an older sidecar).
        // Swallow to debug log; do NOT surface as a user-facing toast —
        // otherwise the audio-speed ticker at 500 ms cadence spams the
        // HUD until the user runs build_qrhi.bat. Will disappear naturally
        // on the next sidecar rebuild.
        const QString code = payload["code"].toString();
        const QString msg  = payload["message"].toString();
        if (code == QStringLiteral("NOT_IMPLEMENTED")) {
            debugLog("[Sidecar] NOT_IMPLEMENTED (stale sidecar, rebuild required): " + msg);
        } else {
            emit errorOccurred(msg);
        }
    } else if (name == "decode_error") {
        // Batch 6.3 — sidecar recovered from a non-fatal avcodec error.
        // Payload: {code, message, recoverable}. Forward to VideoPlayer
        // for toast UI; playback is already continuing sidecar-side.
        emit decodeError(payload["code"].toString(),
                         payload["message"].toString(),
                         payload["recoverable"].toBool(true));
    } else if (name == "subtitle_text") {
        emit subtitleText(payload["text"].toString());
    } else if (name == "sub_visibility_changed") {
        emit subVisibilityChanged(payload["visible"].toBool());
    } else if (name == "sub_delay_changed") {
        emit subDelayChanged(payload["delay_ms"].toDouble());
    } else if (name == "filters_changed") {
        emit filtersChanged(payload);
    } else if (name == "frame_stepped") {
        emit frameStepped(payload["positionSec"].toDouble());
    } else if (name == "d3d11_texture") {
        // Sidecar published its shared D3D11 texture for zero-copy display.
        // Payload from native_sidecar/main.cpp:423-433:
        //   {ntHandle: u64, width: u32, height: u32, format: "bgra8"}
        // ntHandle is a HANDLE in the producer process; it's valid cross-process
        // because the texture was created with D3D11_RESOURCE_MISC_SHARED_NTHANDLE.
        quintptr handle = static_cast<quintptr>(payload["ntHandle"].toVariant().toULongLong());
        int w = payload["width"].toInt();
        int h = payload["height"].toInt();
        if (handle != 0 && w > 0 && h > 0)
            emit d3d11Texture(handle, w, h);
    } else if (name == "overlay_shm") {
        // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — subtitle overlay SHM.
        // Payload: {name: str, width: u32, height: u32}. FrameCanvas opens
        // the named SHM and uploads BGRA into its own locally-owned D3D11
        // overlay texture each time the overlay frame counter advances.
        QString shmName = payload["name"].toString();
        int w = payload["width"].toInt();
        int h = payload["height"].toInt();
        if (!shmName.isEmpty() && w > 0 && h > 0)
            emit overlayShm(shmName, w, h);
    } else if (name == "media_info") {
        emit mediaInfo(payload);
    } else if (name == "closed") {
        emit processClosed();
    } else if (name == "stop_ack") {
        // PLAYER_LIFECYCLE_FIX Phase 2 — sidecar confirmed stop teardown
        // is complete. Fire the pending callback (if seq matches) so the
        // waiting openFile fence can emit its sendOpen now. Mismatched
        // seq = the stop_ack belongs to a superseded sendStopWithCallback
        // (rapid file-switch); ignore silently. Missing seqAck = legacy
        // sidecar that didn't carry the field; accept on latest-pending
        // semantics so pre-rebuild binaries don't hang forever when the
        // sidecar is updated for stop_ack but the seq field is somehow
        // zero'd (defensive).
        const int ackSeq = obj.contains("seqAck") ? obj["seqAck"].toInt(-1) : -1;
        if (m_pendingStopSeq >= 0 &&
            (ackSeq < 0 || ackSeq == m_pendingStopSeq)) {
            debugLog(QString("[Sidecar] stop_ack seq=%1 firing callback")
                         .arg(m_pendingStopSeq));
            auto cb = std::move(m_pendingStopCallback);
            m_pendingStopSeq = -1;
            m_pendingStopCallback = {};
            m_pendingStopTimeoutCallback = {};
            if (cb) cb();
        } else {
            debugLog(QString("[Sidecar] stop_ack seq mismatch or no pending: ackSeq=%1 pending=%2")
                         .arg(ackSeq).arg(m_pendingStopSeq));
        }
    } else if (name == "near_end_estimate") {
        // STREAM_AUTO_NEXT_ESTIMATE_FIX 2026-04-21 — sidecar fires once
        // per session when the consumer read position crosses 90 s of
        // bytes before HTTP EOF (native_sidecar/src/video_decoder.cpp).
        // Main-app's session-ID filter earlier in processLine already
        // ensures this event belongs to the current session. StreamPage
        // treats this as equivalent to nearEndCrossed for AUTO_NEXT
        // prefetch scheduling.
        emit nearEndEstimate();
    } else if (name == "buffering") {
        // PLAYER_UX_FIX Phase 2.2 — sidecar signalled an HTTP-stall
        // (av_read_frame hit EAGAIN/ETIMEDOUT/EIO on a stream URL; see
        // native_sidecar/src/video_decoder.cpp:984 + main.cpp "buffering"
        // case). Phase 1's sessionId filter already passed this through
        // session-matched. Consumers (VideoPlayer / Phase 2.3 LoadingOverlay)
        // listen on bufferingStarted to show a "Buffering…" indicator;
        // the matching `playing` event below dismisses it.
        emit bufferingStarted();
    } else if (name == "playing") {
        // PLAYER_UX_FIX Phase 2.2 — companion to "buffering": fires when
        // a stalled read clears and decode resumes (video_decoder.cpp:1006
        // + main.cpp "playing" case). Distinct from state_changed{playing}
        // (one-shot at first frame); this can fire repeatedly if a stream
        // stalls and recovers multiple times during a session.
        emit bufferingEnded();
    } else if (name == "cache_state") {
        // PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — structured cache-pause
        // progress from sidecar (video_decoder.cpp HTTP stall emits at 2 Hz;
        // main.cpp "cache_state" dispatch builds the JSON payload). Session
        // filter already applied upstream (line 449 region). JSON numbers
        // are doubles under the hood; int64 fields fit within double
        // precision for realistic ring sizes (64 MiB ring = 67M bytes, well
        // under 2^53). Sentinel values (-1.0 etaResume or cacheDur) pass
        // through unchanged for the overlay to render honestly.
        emit cacheStateChanged(
            static_cast<qint64>(payload["cache_bytes_ahead"].toDouble()),
            static_cast<qint64>(payload["raw_input_rate_bps"].toDouble()),
            payload["eta_resume_sec"].toDouble(),
            payload["cache_duration_sec"].toDouble());
    }
    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.2 — classified open-pipeline
    // event parsing. Sidecar Phase 1.1 emits these 6 session-scoped events
    // as open_worker progresses (main.cpp probe_start/probe_done + main.cpp
    // decoder_open_start + video_decoder.cpp decoder_open_done/
    // first_packet_read/first_decoder_receive). Session-id filter above
    // already dropped stale-session variants; we just dispatch to the
    // typed signal. Payload fields (t_ms_from_open, analyze_duration_ms,
    // stream_count, pts_ms, packet_size, stream_index) are discarded at
    // the signal boundary — the generic `[Sidecar] RECV: <name>` log line
    // at :437 preserves them for agent-side diagnostic reads, and
    // consumers (LoadingOverlay::setStage transitions) don't need the
    // per-event scalars. If Batch 1.3 StreamPlayerController consumer
    // (Agent 4's surface) needs the data later, extend each signal
    // signature additively at that point.
    else if (name == "probe_start") {
        emit probeStarted();
    } else if (name == "probe_done") {
        // STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — parse the
        // duration_is_estimate flag added sidecar-side when the bitrate
        // × fileSize fallback rescued an otherwise-zero duration. Pre-
        // fix sidecars don't include this key; .toBool() on a missing
        // QJsonValue returns false, preserving non-estimate behavior.
        const bool durationIsEstimate =
            payload.value("duration_is_estimate").toBool();
        emit probeDone(durationIsEstimate);
    } else if (name == "decoder_open_start") {
        emit decoderOpenStarted();
    } else if (name == "decoder_open_done") {
        emit decoderOpenDone();
    } else if (name == "first_packet_read") {
        emit firstPacketRead();
    } else if (name == "first_decoder_receive") {
        emit firstDecoderReceive();
    }
}

void SidecarProcess::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    emit errorOccurred("Sidecar process error: " + m_process->errorString());
}

void SidecarProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool wasIntentional = m_intentionalShutdown;
    m_intentionalShutdown = false;
    debugLog(QString("[Sidecar] process finished: exit=%1 status=%2 intentional=%3")
             .arg(exitCode).arg(status == QProcess::NormalExit ? "normal" : "crash")
             .arg(wasIntentional ? "yes" : "no"));
    if (!wasIntentional)
        emit processCrashed(exitCode, status);
}

// ============================================================================
// Batch 5.2 (Tankostream Phase 5) — subtitle protocol extensions
// ============================================================================
//
// These are Qt-side wrappers that compose over the existing sidecar commands
// (set_tracks, set_sub_delay, set_sub_style, load_external_sub). The sidecar's
// subtitle_renderer.cpp already supports libass + PGS end-to-end, so no
// sidecar-side protocol changes are required for 5.2 basic parity. If
// Tankostream Phase 5 follow-on work wants true native per-command sidecar
// endpoints (e.g., a single setSubtitleUrl that downloads sidecar-side), the
// bridge APIs below stay stable and we just swap the internals — external
// callers (Agent 4's Batch 5.3 subtitle menu) don't notice.

namespace {
bool isSubtitleExtension(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QStringLiteral("srt")
        || ext == QStringLiteral("vtt")
        || ext == QStringLiteral("ass")
        || ext == QStringLiteral("ssa")
        || ext == QStringLiteral("sub");
}

QString bestTrackTitle(const QJsonObject& t) {
    const QString title = t.value(QStringLiteral("title")).toString().trimmed();
    if (!title.isEmpty()) return title;
    const QString lang = t.value(QStringLiteral("lang")).toString().trimmed();
    if (!lang.isEmpty()) return lang.toUpper();
    return QStringLiteral("Subtitle");
}
} // namespace

void SidecarProcess::updateSubtitleCache(const QJsonArray& subtitle,
                                         const QString& activeSubId)
{
    m_subTracks.clear();
    m_activeSubIndex = -1;

    int nextIndex = 0;
    for (const QJsonValue& v : subtitle) {
        const QJsonObject t = v.toObject();
        const QString id = t.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) continue;

        SubtitleTrackInfo row;
        row.index    = nextIndex++;
        row.sidecarId = id;
        row.lang     = t.value(QStringLiteral("lang")).toString().trimmed();
        row.title    = bestTrackTitle(t);
        row.codec    = t.value(QStringLiteral("codec")).toString().trimmed();
        // Sidecar naming convention: external tracks carry an "ext:" prefix
        // in the id (per prototype reference; validate in smoke test).
        row.external = id.startsWith(QStringLiteral("ext:"), Qt::CaseInsensitive);

        if (id == activeSubId) m_activeSubIndex = row.index;
        m_subTracks.push_back(row);
    }

    emit subtitleTracksListed(m_subTracks, m_activeSubIndex);
}

QList<SubtitleTrackInfo> SidecarProcess::listSubtitleTracks() const
{
    return m_subTracks;
}

int SidecarProcess::activeSubtitleIndex() const
{
    return m_activeSubIndex;
}

int SidecarProcess::sendSetSubtitleTrack(int index)
{
    if (index < 0) {
        // VIDEO_PLAYER_FIX Batch 1.2 — Off is visibility-only. Previous code
        // sent sendSetTracks("", "off") as the "sidecar convention for
        // disabled subtitles," but the sidecar's handle_set_tracks at
        // main.cpp:850 actually calls std::stoi(new_sub_id) without a
        // try/catch, so "off" throws std::invalid_argument → std::terminate
        // → sidecar dies. Honest Off semantics: flip renderer visibility,
        // leave track selection unchanged. Picking a numeric track later
        // restores visibility and lands set_tracks on a valid id.
        const int seq = sendSetSubVisibility(false);
        m_activeSubIndex = -1;
        emit subtitleTrackApplied(-1);
        return seq;
    }

    auto it = std::find_if(m_subTracks.cbegin(), m_subTracks.cend(),
        [index](const SubtitleTrackInfo& t) { return t.index == index; });
    if (it == m_subTracks.cend()) {
        emit errorOccurred(QStringLiteral("Subtitle track index %1 not found in cache")
                               .arg(index));
        return 0;
    }
    // Ensure the renderer is visible BEFORE selecting the track. Phase 1
    // Batch 1.2's Off path sets visibility=false; without this re-enable
    // step, picking a real track via SubtitleMenu / TrackPopover after
    // Off would land set_tracks correctly but the renderer would stay
    // hidden — subs selected but not drawn. Idempotent on sidecar side.
    sendSetSubVisibility(true);
    const int seq = sendSetTracks(QString(), it->sidecarId);
    m_activeSubIndex = index;
    emit subtitleTrackApplied(index);
    return seq;
}

int SidecarProcess::sendSetSubtitleUrl(const QUrl& url, int offsetPx, int delayMs)
{
    // Local file shortcut — no download needed.
    if (url.isLocalFile()) {
        const QString path = url.toLocalFile();
        const int seq = sendLoadExternalSub(path);
        sendSetSubDelay(delayMs);
        m_subPixelOffsetY = offsetPx;
        pushSubStyle();
        emit subtitleUrlLoaded(url, path, true);
        return seq;
    }

    if (!url.isValid() || url.scheme().isEmpty()) {
        emit errorOccurred(QStringLiteral("Invalid subtitle URL"));
        emit subtitleUrlLoaded(url, QString(), false);
        return 0;
    }
    if (!isSubtitleExtension(url.path())) {
        emit errorOccurred(QStringLiteral("Unsupported subtitle extension in URL: %1")
                               .arg(url.toString()));
        emit subtitleUrlLoaded(url, QString(), false);
        return 0;
    }

    // Cache the style/delay context so the async callback can apply it.
    const int pendingOffset = offsetPx;
    const int pendingDelay = delayMs;

    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    QNetworkRequest req(url);
    req.setTransferTimeout(15000);
    QPointer<SidecarProcess> guard(this);
    QNetworkReply* reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this,
        [this, guard, reply, url, pendingOffset, pendingDelay]() {
            reply->deleteLater();
            if (!guard) return;

            if (reply->error() != QNetworkReply::NoError) {
                emit errorOccurred(QStringLiteral("Subtitle download failed: %1")
                                       .arg(reply->errorString()));
                emit subtitleUrlLoaded(url, QString(), false);
                return;
            }

            const QByteArray data = reply->readAll();
            if (data.isEmpty()) {
                emit errorOccurred(QStringLiteral("Subtitle download empty payload"));
                emit subtitleUrlLoaded(url, QString(), false);
                return;
            }

            // Preserve extension for sidecar codec detection.
            const QString ext = QFileInfo(url.path()).suffix().toLower();
            auto* tmp = new QTemporaryFile(
                QDir::tempPath() + QStringLiteral("/tankoban_subtitle_XXXXXX.")
                    + (ext.isEmpty() ? QStringLiteral("srt") : ext),
                this);
            tmp->setAutoRemove(true);
            if (!tmp->open()) {
                emit errorOccurred(QStringLiteral("Failed to stage subtitle temp file"));
                emit subtitleUrlLoaded(url, QString(), false);
                delete tmp;
                return;
            }
            tmp->write(data);
            tmp->flush();
            const QString path = tmp->fileName();
            tmp->close();
            m_subTempFiles.append(tmp);

            sendLoadExternalSub(path);
            sendSetSubDelay(pendingDelay);
            m_subPixelOffsetY = pendingOffset;
            pushSubStyle();
            emit subtitleUrlLoaded(url, path, true);
        });

    return 0;  // async; no single seq to return
}

int SidecarProcess::sendSetSubtitlePixelOffset(int pixelOffsetY)
{
    m_subPixelOffsetY = pixelOffsetY;
    const int seq = pushSubStyle();
    emit subtitleOffsetChanged(pixelOffsetY);
    return seq;
}

int SidecarProcess::sendSetSubtitleSize(double scale)
{
    // Clamp to a sane UX range; prevents sub from being invisible or huge.
    if (scale < 0.5) scale = 0.5;
    if (scale > 3.0) scale = 3.0;
    m_subSizeScale = scale;
    const int seq = pushSubStyle();
    emit subtitleSizeChanged(scale);
    return seq;
}

int SidecarProcess::sendSetSubtitleDelayMs(int ms)
{
    return sendSetSubDelay(static_cast<double>(ms));
}

int SidecarProcess::pushSubStyle()
{
    // Sidecar expects font + margin + outline atomically. Compose from
    // cached state so each individual setter (offset/size) only touches
    // its own field.
    const int fontSize = qBound(14,
        static_cast<int>(kSubBaseFontSize * m_subSizeScale),
        72);
    const int margin = qBound(0, kSubBaseMargin + m_subPixelOffsetY, 200);
    return sendSetSubStyle(fontSize, margin, m_subOutline);
}

// ── VIDEO_IPC_INSTRUMENTATION 2026-04-24 ────────────────────────────────────
// Round-trip latency tracker. sendCommand stamps a send-time per seq;
// processLine's `ack` handler calls recordIpcAck with the seqAck value.
// dumpIpcLatency writes per-command p50/p99/max/count to out/ipc_latency.log
// on destructor. Measurement-only — no behavior change.

void SidecarProcess::recordIpcAck(int seqAck)
{
    if (seqAck < 0) return;
    auto it = m_ipcPending.find(seqAck);
    if (it == m_ipcPending.end()) return;  // stale or already consumed
    const qint64 rt = QDateTime::currentMSecsSinceEpoch() - it->sendMs;
    // Guard against clock weirdness — treat negative as zero rather than
    // polluting the histogram.
    m_ipcLatencies[it->name].append(rt < 0 ? 0 : rt);
    m_ipcPending.erase(it);
}

void SidecarProcess::dumpIpcLatency()
{
    if (m_ipcLatencies.isEmpty()) return;

    // Prefer `out/` for consistency with stream_telemetry.log + runtime-health
    // conventions. Fall back to repo root if `out/` doesn't exist (e.g.,
    // sidecar launched from a fresh checkout pre-build).
    QString path = QStringLiteral("out/ipc_latency.log");
    QFileInfo outDir(QStringLiteral("out"));
    if (!outDir.exists() || !outDir.isDir()) {
        path = QStringLiteral("ipc_latency.log");
    }

    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream stream(&f);

    // Session header: wall-clock timestamp + total commands measured
    int totalSamples = 0;
    for (auto it = m_ipcLatencies.constBegin(); it != m_ipcLatencies.constEnd(); ++it) {
        totalSamples += it.value().size();
    }
    stream << "## session_end=" << QDateTime::currentDateTime().toString(Qt::ISODate)
           << " total_commands=" << totalSamples
           << " distinct_cmd_types=" << m_ipcLatencies.size()
           << " pending_unmatched=" << m_ipcPending.size()
           << "\n";

    // Per-command-name rows, sorted alphabetically for stable diffing across
    // sessions. Within each row: count + p50 + p99 + max in ms.
    QList<QString> names = m_ipcLatencies.keys();
    std::sort(names.begin(), names.end());
    for (const QString& cmdName : names) {
        QVector<qint64> samples = m_ipcLatencies.value(cmdName);
        if (samples.isEmpty()) continue;
        std::sort(samples.begin(), samples.end());
        const int n = samples.size();
        const qint64 p50 = samples[n / 2];
        // p99 index: floor(n * 0.99), clamped to last
        const int p99Idx = qMin(n - 1, static_cast<int>(n * 0.99));
        const qint64 p99 = samples[p99Idx];
        const qint64 maxV = samples.last();
        stream << "cmd=" << cmdName
               << " count=" << n
               << " p50=" << p50 << "ms"
               << " p99=" << p99 << "ms"
               << " max=" << maxV << "ms\n";
    }
    stream << "\n";
    f.close();

    // Clear post-dump so a subsequent re-run of the same SidecarProcess
    // instance (e.g., under testing harness) starts fresh.
    m_ipcLatencies.clear();
    m_ipcPending.clear();
}
