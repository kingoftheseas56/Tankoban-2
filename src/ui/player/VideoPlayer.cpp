#include "ui/player/VideoPlayer.h"
#include "ui/player/KeyBindings.h"
#include "ui/player/SidecarProcess.h"
#include "ui/player/ShmFrameReader.h"
#include "ui/player/FrameCanvas.h"
#include "ui/player/VolumeHud.h"
#include "ui/player/CenterFlash.h"
#include "ui/player/LoadingOverlay.h"
#include "ui/player/KeybindingEditor.h"
#include "ui/player/StatsBadge.h"
#include "ui/player/PlaylistDrawer.h"
#include "ui/player/ToastHud.h"
#include "ui/player/EqualizerPopover.h"
#include "ui/player/FilterPopover.h"
#include "ui/player/TrackPopover.h"
#include "ui/player/SubtitleOverlay.h"
#include "ui/player/SubtitleMenu.h"
#include "ui/player/OpenUrlDialog.h"
#include "ui/player/PlayerUtils.h"
#include "ui/player/SeekSlider.h"
#include "ui/player/VideoContextMenu.h"
#include "core/CoreBridge.h"

#include <cmath>   // std::abs — Batch 4.1 audio-speed ticker deadband

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>   // Phase 6.1 — style()->polish() for dynamic [active="true"] property
#include <QCryptographicHash>
#include <QMenu>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QSvgRenderer>
#include <QPainter>
#include <QSettings>
#include <QPixmap>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

static void debugLog(const QString& msg) {
    QFile f("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
    f.open(QIODevice::Append | QIODevice::Text);
    QTextStream s(&f);
    s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
}

// Stable QSettings key for an audio device. Sanitized so the key is QSettings-safe
// (alphanumerics + underscore). Includes a short host-API tag so the same physical
// device routed through different APIs (MME vs WASAPI) gets separate offsets.
static QString makeDeviceKey(const QString& deviceName, const QString& hostApi) {
    QString sanitized;
    for (QChar c : deviceName) {
        if (c.isLetterOrNumber()) sanitized.append(c);
        else if (c.isSpace() || c == '_' || c == '-') sanitized.append('_');
    }
    return QString("audio_offsets/%1__%2").arg(sanitized, hostApi.left(8));
}

// Heuristic: does this device name look like a Bluetooth audio output?
// Note: devices that report only their MAC address (no friendly name) won't
// match — those fall through to 0ms and rely on manual tuning. The marker
// list is the common consumer audio brand names.
static bool looksLikeBluetooth(const QString& deviceName) {
    static const QStringList markers = {
        "bluetooth", "airpod", "airpods", "beats", "bose",
        "sony wh", "sony wf", "jabra", "jbl", "galaxy buds",
        "powerbeats", "wh-1000", "wf-1000", "surface headphones",
        "surface earbuds", "soundlink", "freebuds", "pixel buds",
        "wf-c", "wh-c"
    };
    QString lower = deviceName.toLower();
    for (const QString& m : markers)
        if (lower.contains(m)) return true;
    return false;
}

// ── Inline SVG icons ────────────────────────────────────────────────────────

static const QByteArray SVG_PLAY =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='#cccccc'>"
    "<polygon points='6,4 20,12 6,20'/>"
    "</svg>";

static const QByteArray SVG_PAUSE =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='#cccccc'>"
    "<rect x='5' y='4' width='4' height='16' rx='1'/>"
    "<rect x='15' y='4' width='4' height='16' rx='1'/>"
    "</svg>";

static const QByteArray SVG_SEEK_BACK =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='#cccccc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M11 17l-5-5 5-5'/><path d='M18 17l-5-5 5-5'/>"
    "</svg>";

static const QByteArray SVG_SEEK_FWD =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='#cccccc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M13 17l5-5-5-5'/><path d='M6 17l5-5-5-5'/>"
    "</svg>";

static const QByteArray SVG_BACK =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='#cccccc' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<line x1='19' y1='12' x2='5' y2='12'/>"
    "<polyline points='12,19 5,12 12,5'/>"
    "</svg>";

static const QByteArray SVG_PREV_EP =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='#cccccc'>"
    "<rect x='5' y='5' width='2' height='14' rx='1'/>"
    "<polygon points='9,12 19,6 19,18'/>"
    "</svg>";

static const QByteArray SVG_NEXT_EP =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='#cccccc'>"
    "<rect x='17' y='5' width='2' height='14' rx='1'/>"
    "<polygon points='15,12 5,6 5,18'/>"
    "</svg>";


static const char* SPEED_LABELS[] = { "0.5x","0.75x","1.0x","1.25x","1.5x","1.75x","2.0x" };
static const double SPEED_PRESETS[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
static const int    SPEED_COUNT    = 7;

// ── Constructor ─────────────────────────────────────────────────────────────

VideoPlayer::VideoPlayer(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: #000000;");
    // VIDEO_PLAYER_FIX Batch 4.3 — opt into Qt's drag-drop machinery so
    // dragEnterEvent / dropEvent fire on the player surface.
    setAcceptDrops(true);

    m_playIcon     = iconFromSvg(SVG_PLAY);
    m_pauseIcon    = iconFromSvg(SVG_PAUSE);
    m_backIcon     = iconFromSvg(SVG_BACK);
    m_prevEpIcon   = iconFromSvg(SVG_PREV_EP);
    m_nextEpIcon   = iconFromSvg(SVG_NEXT_EP);
    m_seekBackIcon = iconFromSvg(SVG_SEEK_BACK);
    m_seekFwdIcon  = iconFromSvg(SVG_SEEK_FWD);

    m_keys    = new KeyBindings();
    m_sidecar = new SidecarProcess(this);
    m_reader  = new ShmFrameReader();

    buildUI();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(3000);
    connect(&m_hideTimer, &QTimer::timeout, this, &VideoPlayer::hideControls);

    m_cursorTimer.setSingleShot(true);
    m_cursorTimer.setInterval(2000);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this]() {
        if (!m_paused && !m_controlBar->isVisible())
            setCursor(Qt::BlankCursor);
    });

    m_seekThrottle.setSingleShot(true);
    m_seekThrottle.setInterval(250);
    connect(&m_seekThrottle, &QTimer::timeout, this, [this]() {
        if (m_durationSec > 0 && m_seeking)
            m_sidecar->sendSeek(m_pendingSeekVal / 10000.0 * m_durationSec);
    });

    // VIDEO_PLAYER_FIX Batch 3.1 — restore persisted always-on-top state.
    // Just read the bool here; actual WindowStaysOnTopHint application
    // defers to the first showEvent because window() isn't guaranteed to
    // have a native handle yet, and setWindowFlag + show() during parent
    // construction races the shell's own initial show sequence.
    m_alwaysOnTop = QSettings("Tankoban", "Tankoban")
        .value("player/alwaysOnTop", false).toBool();

    // VIDEO_PLAYER_FIX Batch 7.1 — restore stats badge toggle. Applied
    // lazily on the first firstFrame event (no source metadata before
    // that, so showing the badge earlier would render empty).
    m_showStats = QSettings("Tankoban", "Tankoban")
        .value("player/showStats", false).toBool();

    // Sidecar events
    connect(m_sidecar, &SidecarProcess::ready,        this, &VideoPlayer::onSidecarReady);
    connect(m_sidecar, &SidecarProcess::firstFrame,   this, &VideoPlayer::onFirstFrame);
    connect(m_sidecar, &SidecarProcess::timeUpdate,   this, &VideoPlayer::onTimeUpdate);
    connect(m_sidecar, &SidecarProcess::stateChanged,  this, &VideoPlayer::onStateChanged);
    connect(m_sidecar, &SidecarProcess::tracksChanged,  this, &VideoPlayer::onTracksChanged);
    connect(m_sidecar, &SidecarProcess::endOfFile,    this, &VideoPlayer::onEndOfFile);
    connect(m_sidecar, &SidecarProcess::errorOccurred, this, &VideoPlayer::onError);
    connect(m_sidecar, &SidecarProcess::processCrashed, this, &VideoPlayer::onSidecarCrashed);

    // Batch 6.3 — non-fatal decode errors get a throttled toast. Corrupted
    // files can produce many per second; one toast every 3 s is enough to
    // communicate "something's wrong, we're skipping past it" without
    // spamming the UI. Throttle state lives in onDecodeError's static local.
    connect(m_sidecar, &SidecarProcess::decodeError, this,
            [this](const QString& code, const QString& message, bool recoverable) {
        debugLog(QString("[VideoPlayer] decode_error code=%1 msg=%2 recoverable=%3")
                 .arg(code, message).arg(recoverable ? "yes" : "no"));
        if (!recoverable) return;  // fatal path comes through errorOccurred
        static qint64 lastToastMs = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastToastMs < 3000) return;
        lastToastMs = now;
        m_toastHud->showToast("Skipping corrupt frame…");
    });

    // Batch 6.1 — single-shot restart timer drives backoff between respawns.
    m_sidecarRestartTimer.setSingleShot(true);
    connect(&m_sidecarRestartTimer, &QTimer::timeout, this, &VideoPlayer::restartSidecar);

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.2 — 30s first-frame watchdog.
    // Single-shot; armed on openFile entry, cancelled on firstFrame or
    // teardownUi. Fires setStage(TakingLonger) when first-frame doesn't
    // arrive within 30s (duration chosen to match the sidecar's existing
    // STREAM_TIMEOUT "no data for 30 seconds" at video_decoder.cpp:1087
    // so UX aligns with the sidecar's own give-up threshold). Explicit
    // stop() at all three sites (openFile re-arm, firstFrame dismiss,
    // teardownUi teardown) — Qt single-thread event loop serializes
    // those handlers so no generation-check race exists.
    m_firstFrameWatchdog.setSingleShot(true);
    connect(&m_firstFrameWatchdog, &QTimer::timeout, this, [this]() {
        if (m_loadingOverlay) {
            m_loadingOverlay->setStage(LoadingOverlay::Stage::TakingLonger);
        }
    });

    // SubtitleOverlay QLabel is unused under the current native sidecar
    // renderer (libass frame-blend at native_sidecar/src/subtitle_renderer.cpp
    // render_blend composites subs into the video BGRA frame, not via a
    // separate subtitle_text event). Dead subtitleText→setText connect
    // removed 2026-04-15 post-Agent-7 audit. SubtitleOverlay class kept
    // in place for a potential future text-fallback path.
    connect(m_sidecar, &SidecarProcess::subVisibilityChanged, this, [this](bool visible) {
        // VIDEO_PLAYER_FIX Batch 1.2 — keep m_subsVisible coherent with
        // sidecar renderer state. SubtitleMenu's Off path goes through
        // SidecarProcess::sendSetSubtitleTrack(-1) → sendSetSubVisibility(false)
        // which emits this signal; pre-fix, VideoPlayer's m_subsVisible
        // stayed stale (still true) after a SubtitleMenu Off, so pressing T
        // (toggleSubtitles) did the wrong flip. Now we mirror sidecar state
        // on every signal, regardless of which code path drove the change.
        m_subsVisible = visible;
        if (!visible)
            m_subOverlay->setText("");
    });
}

VideoPlayer::~VideoPlayer()
{
    m_canvas->stopPolling();
    m_reader->detach();
    delete m_reader;
    delete m_keys;
}

// ── Public ──────────────────────────────────────────────────────────────────

void VideoPlayer::openFile(const QString& filePath,
                            const QStringList& playlist, int playlistIndex,
                            double startPositionSec)
{
    debugLog("[VideoPlayer] openFile: " + filePath);

    // VIDEO_PLAYER_FIX Batch 4.2 — record user intent in the recents list
    // before any side effects. Crash-recovery restart (SidecarProcess
    // respawn after processCrashed) runs through `restartSidecar()` which
    // does NOT call openFile, so recents aren't duplicated on recovery.
    pushRecentFile(filePath);

    // PLAYER_LIFECYCLE_FIX Phase 2 — UI-only teardown. The sidecar
    // process-teardown happens below via either sendStopWithCallback
    // (file-switch fence, same-process) or start() (cold start); we
    // do NOT call the full stopPlayback() here because that path
    // issues sendStop+sendShutdown back-to-back with no fence — the
    // very race Agent 7 audit P0-2 flagged.
    teardownUi();

    m_currentFile = filePath;
    m_currentVideoId = videoIdForFile(filePath);
    m_currentShowId = showIdForFile(filePath);
    // Title for the bottom HUD label — use completeBaseName (strips the
    // file extension). For URL sources (Stream mode), QFileInfo still
    // pulls a reasonable basename from the last path segment.
    m_fullTitle = QFileInfo(filePath).completeBaseName();
    updateTitleElision();
    m_playlist = playlist;
    m_playlistIdx = playlistIndex;

    // If no playlist provided, build one from the same directory
    if (m_playlist.isEmpty()) {
        QDir dir(QFileInfo(filePath).absolutePath());
        QStringList exts = {"*.mp4","*.mkv","*.avi","*.webm","*.mov","*.wmv","*.flv","*.m4v","*.ts"};
        auto files = dir.entryInfoList(exts, QDir::Files, QDir::Name);
        for (const auto& fi : files)
            m_playlist.append(fi.absoluteFilePath());
        m_playlistIdx = m_playlist.indexOf(filePath);
        if (m_playlistIdx < 0) m_playlistIdx = 0;
    }
    m_pendingFile = filePath;
    m_paused = false;

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.2 — arm 30s first-frame watchdog.
    // Single-shot; teardownUi above already stop()ped any prior-session
    // armed timer, but start() is idempotent w.r.t. reset so safe either
    // way. firstFrame connection (setupUi) stops it on normal open.
    m_firstFrameWatchdog.start(30 * 1000);

    // Batch 6.1 — fresh user intent clears crash-retry state.
    m_sidecarRetryCount = 0;
    m_lastKnownPosSec   = 0.0;
    m_sidecarRestartTimer.stop();
    // PLAYER_LIFECYCLE_FIX Phase 3 Batch 3.2 — arm the pending-open
    // token. Consumed by onSidecarReady on the cold-start branch and
    // on the timeout-fallback branch (resetAndRestart → ready event).
    // Warm/fence branch sends `open` directly from its stop_ack callback
    // and doesn't touch the token; leaving it armed across a warm switch
    // is harmless — any intervening crash-recovery would re-consume it.
    m_openPending = true;
    // PLAYER_UX_FIX Phase 6.1 — a file is now in flight; re-enable chips
    // (teardownUi's intentional-stop block disabled them on the prior
    // close). Chip :disabled pseudo-state clears on the next repaint.
    setChipsEnabled(true);
    // Fresh file — re-apply preferences on the next tracks_changed.
    m_tracksRestored = false;
    // Fresh file — re-arm the external-sub auto-load one-shot.
    m_autoSubAttempted = false;

    // Aspect restore priority chain:
    //   1. In-session carry (playlist advance within one session)
    //   2. Per-file record ("videos" domain, aspectOverride field).
    //      Uses contains() not emptiness — "original" is a valid
    //      explicit user choice, not the absence of one.
    //   3. Per-show record ("shows" domain, aspectOverride field).
    //   4. "original" default (native source aspect).
    // Gated on LibraryVideos; Stream mode falls straight through to
    // "original" without touching persistence layers.
    QString aspectToken = QStringLiteral("original");
    if (!m_carryAspect.isEmpty()) {
        aspectToken = m_carryAspect;
        m_carryAspect.clear();
    } else if (m_bridge && m_persistenceMode == PersistenceMode::LibraryVideos) {
        QJsonObject prog = m_bridge->progress("videos", m_currentVideoId);
        if (prog.contains("aspectOverride")) {
            aspectToken = prog.value("aspectOverride").toString(QStringLiteral("original"));
        } else {
            QJsonObject showPrefs = loadShowPrefs();
            if (showPrefs.contains("aspectOverride")) {
                aspectToken = showPrefs.value("aspectOverride").toString(QStringLiteral("original"));
            }
        }
    }
    m_currentAspect = aspectToken;
    if (m_canvas) m_canvas->setForcedAspectRatio(aspectStringToDouble(aspectToken));

    // Crop restore priority chain — mirrors aspect chain. "none" is the
    // default; using prog.contains() lets an explicit "none" override
    // a per-show crop when the user wants to disable crop per-file.
    QString cropToken = QStringLiteral("none");
    if (!m_carryCrop.isEmpty()) {
        cropToken = m_carryCrop;
        m_carryCrop.clear();
    } else if (m_bridge && m_persistenceMode == PersistenceMode::LibraryVideos) {
        QJsonObject prog = m_bridge->progress("videos", m_currentVideoId);
        if (prog.contains("cropOverride")) {
            cropToken = prog.value("cropOverride").toString(QStringLiteral("none"));
        } else {
            QJsonObject showPrefs = loadShowPrefs();
            if (showPrefs.contains("cropOverride")) {
                cropToken = showPrefs.value("cropOverride").toString(QStringLiteral("none"));
            }
        }
    }
    m_currentCrop = cropToken;
    if (m_canvas) m_canvas->setCropAspect(cropStringToDouble(cropToken));

    // Repopulate playlist drawer (reflects new current episode)
    if (m_playlistDrawer)
        m_playlistDrawer->populate(m_playlist, m_playlistIdx);
    updateEpisodeButtons();

    // Check for saved progress — resume from last position.
    //
    // Two entry paths:
    //   1. Caller-supplied startPositionSec > 0 — Stream-mode feeds the resume
    //      offset from the "stream" progress domain (Phase 1 Batch 1.3 of
    //      STREAM_UX_PARITY). Honored regardless of PersistenceMode so the
    //      None-mode stream-playback flow can still resume.
    //   2. Otherwise the existing PersistenceMode::LibraryVideos gate runs:
    //      Videos-mode reads from "videos" domain; None mode leaves the
    //      pending seek at 0.0.
    m_pendingStartSec = 0.0;
    if (startPositionSec > 0.0) {
        m_pendingStartSec = startPositionSec;
    } else if (m_bridge && m_persistenceMode == PersistenceMode::LibraryVideos) {
        QJsonObject prog = m_bridge->progress("videos", m_currentVideoId);
        double savedPos = prog.value("positionSec").toDouble(0);
        double savedDur = prog.value("durationSec").toDouble(0);
        // Resume if we're not near the end (within 95%)
        if (savedPos > 2.0 && savedDur > 0 && savedPos < savedDur * 0.95)
            m_pendingStartSec = savedPos;
    }
    updatePlayPauseIcon();

    if (m_sidecar->isRunning()) {
        // PLAYER_LIFECYCLE_FIX Phase 2 Shape 2 — same-process stop/open
        // fence. Pre-fix, the running-sidecar branch was `sendOpen(...)`
        // with `stopPlayback()` above that had already fired `sendStop +
        // sendShutdown` back-to-back. The `open` raced against the
        // shutting-down sidecar process (Agent 7 audit P0-2). Now the
        // UI-only `teardownUi()` runs above (no process teardown), and
        // here the fence issues `sendStop` + waits for the sidecar's
        // `stop_ack` event (emitted after its `teardown_decode()` fully
        // completes) before firing `sendOpen`. The sidecar process
        // stays alive across the file switch (no respawn cost), Phase 1's
        // sessionId filter drops any old-session tail events arriving
        // mid-transition, and if `stop_ack` never arrives within 2s
        // (pre-Phase-2 sidecar binary / sidecar hang), the onTimeout
        // path forces a full sidecar reset + relies on `onSidecarReady`
        // to fire `sendOpen(m_pendingFile, m_pendingStartSec)` when the
        // fresh process is up.
        //
        // STREAM_PLAYBACK_FIX Phase 2 Batch 2.4 side-carry preserved —
        // m_pendingStartSec rides through both the warm (callback) and
        // cold (onSidecarReady) paths.
        debugLog("[VideoPlayer] sidecar running, fencing stop before open");
        const QString file = filePath;
        const double start = m_pendingStartSec;
        m_sidecar->sendStopWithCallback(
            [this, file, start]() {
                debugLog("[VideoPlayer] stop_ack received, sending open: " + file);
                sendCanvasSizeToSidecar();
                m_sidecar->sendOpen(file, start);
            },
            [this]() {
                debugLog("[VideoPlayer] stop_ack timeout — resetting sidecar; onSidecarReady will reopen m_pendingFile");
                m_sidecar->resetAndRestart();
                // onSidecarReady will fire sendOpen(m_pendingFile,
                // m_pendingStartSec) once the fresh sidecar is up.
            }
        );
    } else {
        debugLog("[VideoPlayer] starting sidecar...");
        m_sidecar->start();
    }

    showControls();
}

void VideoPlayer::dismissOtherPopovers(QWidget* keep)
{
    // PLAYER_UX_FIX Phase 6.4 — centralized popover dismiss logic.
    // Called from each chip's click handler before toggling (so only one
    // popover is ever visible) and from keyPressEvent ESC with
    // keep=nullptr to dismiss whichever is open. Chip :checked state
    // synced to false here so the chip's visual open-state clears when
    // its popover is force-hidden.
    if (m_filterPopover && m_filterPopover != keep && m_filterPopover->isOpen()) {
        m_filterPopover->hide();
        if (m_filtersChip) m_filtersChip->setChecked(false);
    }
    if (m_eqPopover && m_eqPopover != keep && m_eqPopover->isOpen()) {
        m_eqPopover->hide();
        if (m_eqChip) m_eqChip->setChecked(false);
    }
    if (m_trackPopover && m_trackPopover != keep && m_trackPopover->isOpen()) {
        m_trackPopover->hide();
        if (m_trackChip) m_trackChip->setChecked(false);
    }
    if (m_playlistDrawer && m_playlistDrawer != keep && m_playlistDrawer->isOpen()) {
        m_playlistDrawer->hide();
        if (m_playlistChip) m_playlistChip->setChecked(false);
    }
}

void VideoPlayer::setChipsEnabled(bool enable)
{
    // Phase 6.1 — toggles the :disabled pseudo-state on all four chips.
    // Playlist chip also gets disabled when no file is open, matching the
    // "nothing to interact with" invariant (playlist is empty anyway).
    if (m_filtersChip) m_filtersChip->setEnabled(enable);
    if (m_eqChip)      m_eqChip->setEnabled(enable);
    if (m_trackChip)   m_trackChip->setEnabled(enable);
    if (m_playlistChip) m_playlistChip->setEnabled(enable);
}

void VideoPlayer::teardownUi()
{
    // PLAYER_LIFECYCLE_FIX Phase 2 — UI-only teardown split out of
    // stopPlayback. Both the user-close path (stopPlayback) and the
    // file-switch path (openFile at the top) run this unconditionally.
    // The process-teardown portion — sendStop + sendShutdown, or the
    // new sendStopWithCallback fence — is handled by each caller per
    // its own lifecycle needs.

    // Batch 6.1 — cancel any pending crash-recovery respawn. User-driven
    // stop / new-file-open supersedes in-flight auto-restart.
    m_sidecarRestartTimer.stop();

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.2 — cancel any armed first-
    // frame watchdog. Covers both user-close (stopPlayback → teardownUi)
    // and file-switch (openFile → teardownUi pre-new-open). openFile
    // re-arms the timer post-teardownUi so file-switch cases transition
    // cleanly; this stop() call prevents a close-mid-open from leaving
    // the timer running to fire over a dismissed overlay.
    m_firstFrameWatchdog.stop();

    m_canvas->stopPolling();
    m_canvas->detachShm();
    m_canvas->detachD3D11Texture();  // release imported shared texture
    m_reader->detach();

    // Reset cached track lists so the next file's tracks_changed event
    // populates a fresh authoritative list. Without this, merge-on-update
    // (see onTracksChanged) would carry stale entries across file changes.
    m_audioTracks = {};
    m_subTracks   = {};

    // Batch 5.3 — clear Tankostream external subs so the next stream/file
    // doesn't inherit a stale addon track list in the SubtitleMenu.
    if (m_subMenu) m_subMenu->setExternalTracks({}, {});

    // PLAYER_UX_FIX Phase 3 Batch 3.1 — reset user-visible HUD surfaces
    // to a clean "loading" state on video switch / user close. Without
    // this, time labels / seekbar / chip text would show the previous
    // file's data until the new session's first time_update +
    // tracks_changed arrived (which after Phase 1.1 is pre-first-frame
    // but still ~1s+ on slow opens). Phase 2.3's LoadingOverlay visually
    // occupies this cleaned state; the pill + reset-to-clean HUD compose
    // as the unified "opening" visual.
    //
    // Scope note: EQ + Filter state is process-wide (persists across
    // files), not per-file — resetting the chip TEXT to generic labels
    // here briefly mis-represents active state until the next filter-
    // state emit re-populates. Following TODO spec literally; Hemanth
    // flag if this is a regression (trivial revert: drop the two lines).
    m_durationSec = 0.0;
    if (m_timeLabel)   m_timeLabel->setText(QStringLiteral("\u2014:\u2014"));
    if (m_durLabel)    m_durLabel->setText(QStringLiteral("\u2014:\u2014"));
    if (m_titleLabel) m_titleLabel->setText(QString());
    m_fullTitle.clear();
    if (m_seekBar) {
        m_seekBar->blockSignals(true);
        m_seekBar->setValue(0);
        m_seekBar->setDurationSec(0.0);
        // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 — clear buffered-range
        // overlay on teardown so stale ranges from the previous session
        // don't paint over the next open (stream → library switch or a
        // fresh stream open). Empty-list + zero-fileSize hits the paint
        // guard in setBufferedRanges and the overlay short-circuits.
        m_seekBar->setBufferedRanges({}, 0);
        m_seekBar->blockSignals(false);
    }
    if (m_trackChip)   m_trackChip->setText(QStringLiteral("Tracks"));
    if (m_eqChip)      m_eqChip->setText(QStringLiteral("EQ"));
    if (m_filtersChip) m_filtersChip->setText(QStringLiteral("Filters"));
    if (m_statsBadge)  m_statsBadge->hide();
    // Dismiss any open chip popovers so their next open shows fresh
    // content. TrackPopover reads from m_audioTracks/m_subTracks which
    // were cleared above; EqualizerPopover / FilterPopover keep their
    // process-wide state, just hide the window.
    if (m_trackPopover  && m_trackPopover->isOpen())  m_trackPopover->hide();
    if (m_eqPopover     && m_eqPopover->isOpen())     m_eqPopover->hide();
    if (m_filterPopover && m_filterPopover->isOpen()) m_filterPopover->hide();
}

void VideoPlayer::stopPlayback(bool isIntentional)
{
    // User-close path (isIntentional=true, default) — full teardown:
    // UI + sidecar process + identity state. Crash-recovery-style stops
    // (isIntentional=false) would preserve m_currentFile/m_pendingFile/
    // m_lastKnownPosSec for the respawn to resume from; the current
    // restartSidecar flow doesn't call stopPlayback, so this branch is
    // future-proofing rather than a current call site.
    teardownUi();

    if (m_sidecar->isRunning()) {
        m_sidecar->sendStop();
        // Give sidecar a moment to stop audio, then shut it down.
        // Note: this stop+shutdown sequence is NOT race-safe for file-
        // switch (Agent 7 audit P0-2). For file-switch, openFile uses
        // the new sendStopWithCallback fence instead, which waits for
        // stop_ack before the follow-on sendOpen. User-close tolerates
        // the race because the process is going away — any in-flight
        // events from the torn-down session are moot.
        m_sidecar->sendShutdown();
    }

    // PLAYER_LIFECYCLE_FIX Phase 3 Batch 3.1 + 3.2 — intentional stop
    // clears identity state AND the one-shot pending-open token so a
    // late onSidecarReady event in the user-close race window cannot
    // re-open the just-closed file. Crash-recovery paths don't call
    // this (they drive restartSidecar directly), so preserving those
    // fields with isIntentional=false is reserved for future callers.
    if (isIntentional) {
        m_currentFile.clear();
        m_pendingFile.clear();
        m_pendingStartSec = 0.0;
        m_playlist.clear();
        m_playlistIdx = 0;
        m_lastKnownPosSec = 0.0;
        m_openPending = false;
        // PLAYER_UX_FIX Phase 6.1 — disable chips on intentional close
        // so the :disabled pseudo-state kicks in. Re-enabled by openFile
        // on the next play start. Crash-recovery path leaves chips
        // enabled (isIntentional=false) because playback resumes on its
        // own — no user-action-required "nothing open" state.
        setChipsEnabled(false);
    }
}

void VideoPlayer::setExternalSubtitleTracks(
    const QList<tankostream::addon::SubtitleTrack>& tracks,
    const QHash<QString, QString>& originByTrackKey)
{
    if (!m_subMenu) return;
    m_subMenu->setExternalTracks(tracks, originByTrackKey);
}

void VideoPlayer::setPersistenceMode(PersistenceMode mode)
{
    // Small accessor with no side effects beyond the mode field.
    // Callers (today: StreamPage for Tankostream Phase 5) set None
    // before openFile and reset to LibraryVideos on close. Effect is
    // picked up at the NEXT bridge read/write inside this class —
    // flipping mode during active playback affects subsequent ticks,
    // not retroactively.
    m_persistenceMode = mode;
}

// PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 — stream-mode toggle. Sibling
// of setPersistenceMode; caller (StreamPage) pairs setStreamMode(true) with
// its existing setPersistenceMode(None) before openFile for stream playback,
// and setStreamMode(false) + setPersistenceMode(LibraryVideos) on close /
// file-switch. When false, the next bufferedRangesChanged signal that
// arrives is short-circuited in onBufferedRangesChanged. When true, ranges
// forward to the SeekSlider overlay. No effect on currently-painted state;
// teardownUi handles the clear separately on close so stale ranges don't
// bleed into the next file.
void VideoPlayer::setStreamMode(bool on)
{
    m_streamMode = on;
}

// PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 — buffered-range snapshot
// slot. Wired by StreamPage from StreamPlayerController::bufferedRangesChanged
// around stream-session lifecycle. Defensive stream-mode guard prevents a
// stale connection from painting over a library-file session. infoHash
// unused directly but preserved for log correlation across multi-stream
// scenarios. Forwards to SeekSlider::setBufferedRanges (Batch 1.4).
void VideoPlayer::onBufferedRangesChanged(const QString& /*infoHash*/,
                                          const QList<QPair<qint64, qint64>>& ranges,
                                          qint64 fileSize)
{
    if (!m_streamMode) return;
    if (!m_seekBar)   return;
    m_seekBar->setBufferedRanges(ranges, fileSize);
}

// ── Sidecar event handlers ──────────────────────────────────────────────────

void VideoPlayer::sendCanvasSizeToSidecar()
{
    if (!m_sidecar || !m_canvas || !m_sidecar->isRunning()) return;
    const QSize px = m_canvas->canvasPixelSize();
    if (px.width() <= 0 || px.height() <= 0) return;
    debugLog(QString("[VideoPlayer] set_canvas_size %1x%2")
                 .arg(px.width()).arg(px.height()));
    m_sidecar->sendSetCanvasSize(px.width(), px.height());
}

void VideoPlayer::tryAutoLoadSiblingSubtitle()
{
    // Guard 1: user pref. Default on, settable via QSettings elsewhere.
    if (!QSettings("Tankoban", "Tankoban")
            .value("video_sub_auto_load", true).toBool())
        return;

    // Guard 2: file has embedded subs — respect those over external.
    // External-sub loading replaces the active track; auto-loading when
    // embedded subs exist would silently swap the user's default pick.
    if (!m_subTracks.isEmpty()) return;

    // Guard 3: real filesystem path (not a stream URL). QFileInfo on a
    // URL produces garbage; the sidecar's load_external_sub expects a
    // local path it can fopen.
    if (m_currentFile.isEmpty()) return;
    if (m_currentFile.startsWith("http://", Qt::CaseInsensitive) ||
        m_currentFile.startsWith("https://", Qt::CaseInsensitive) ||
        m_currentFile.startsWith("magnet:", Qt::CaseInsensitive))
        return;

    QFileInfo videoInfo(m_currentFile);
    if (!videoInfo.exists()) return;
    const QDir parent = videoInfo.absoluteDir();
    const QString base = videoInfo.completeBaseName();
    if (base.isEmpty()) return;

    // Preference order matches what most players use. ASS first because
    // it carries style info the user author wanted; SRT is most common;
    // SSA is the older ASS predecessor; WEBVTT is streaming-native;
    // SUB is the VobSub-style fallback.
    static const char* kExts[] = { "ass", "srt", "ssa", "vtt", "sub" };
    for (const char* ext : kExts) {
        // Try exact-basename match first: video.mkv -> video.srt
        const QString candidate = parent.filePath(base + "." + ext);
        if (QFileInfo::exists(candidate)) {
            if (m_sidecar) m_sidecar->sendLoadExternalSub(candidate);
            debugLog(QString("[VideoPlayer] auto-loaded sibling sub: %1")
                         .arg(QFileInfo(candidate).fileName()));
            if (m_toastHud)
                m_toastHud->showToast("Loaded subtitle: " +
                                      QFileInfo(candidate).fileName());
            return;  // single best match — don't stack-load multiple
        }
    }
}

void VideoPlayer::updateTitleElision()
{
    if (!m_titleLabel) return;
    if (m_fullTitle.isEmpty()) {
        m_titleLabel->setText(QString());
        return;
    }
    // Account for the CSS padding (12px left + 12px right) before asking
    // QFontMetrics where to cut the string.
    const int pad = 24;
    const int avail = qMax(0, m_titleLabel->width() - pad);
    if (avail <= 0) {
        m_titleLabel->setText(QString());
        return;
    }
    const QFontMetrics fm(m_titleLabel->font());
    m_titleLabel->setText(fm.elidedText(m_fullTitle, Qt::ElideRight, avail));
}

void VideoPlayer::onSidecarReady()
{
    // VIDEO_PLAYER_FIX Batch 5.1 — push persisted loop-file state to the
    // freshly-ready sidecar before sending open, so the first file honors
    // the toggle. Pre-5.1 sidecar binaries treat this as NOT_IMPLEMENTED;
    // harmless (SidecarProcess swallows).
    if (m_playlistDrawer && m_playlistDrawer->loopFile())
        m_sidecar->sendSetLoopFile(true);

    // PLAYER_LIFECYCLE_FIX Phase 3 Batch 3.2 — gate the re-open on the
    // one-shot pending-open token, not just on a non-empty m_pendingFile.
    // Without this, a spurious `ready` event arriving after a user-close
    // would re-open the file the user just closed (audit P1-5). Post-
    // 3.1 stopPlayback(true) clears m_pendingFile too, so the empty-
    // check remains a secondary defense but the token is the primary
    // gate.
    //
    // Per-device audio offset is applied in the mediaInfo handler
    // (which fires after open() reports the active audio device).
    if (m_openPending && !m_pendingFile.isEmpty()) {
        m_openPending = false;
        sendCanvasSizeToSidecar();
        m_sidecar->sendOpen(m_pendingFile, m_pendingStartSec);
    } else {
        debugLog(QString("[VideoPlayer] onSidecarReady: skip open (openPending=%1 pendingFile=%2)")
                     .arg(m_openPending ? "true" : "false")
                     .arg(m_pendingFile.isEmpty() ? "empty" : "set"));
    }
}

void VideoPlayer::onFirstFrame(const QJsonObject& payload)
{
    debugLog("[VideoPlayer] onFirstFrame: " + QJsonDocument(payload).toJson(QJsonDocument::Compact));
    // Batch 6.1 — first frame after a crash-recovery restart confirms the
    // respawn succeeded; clear the retry counter so future crashes start
    // a fresh 3-attempt budget.
    m_sidecarRetryCount = 0;
    QString shmName  = payload["shmName"].toString();
    int slotCount    = payload["slotCount"].toInt(4);
    int w            = payload["width"].toInt();
    int h            = payload["height"].toInt();
    int slotBytes    = payload["slotBytes"].toInt(w * h * 4);

    // VIDEO_PLAYER_FIX Batch 7.1 — stash source metadata for the stats
    // badge. fps arrives from the sidecar probe (Batch 7.1 sidecar
    // change); pre-7.1 sidecar binaries don't emit it so we default to
    // 0.0 and the badge renders "— fps".
    m_statsCodec  = payload.value("codec").toString();
    m_statsWidth  = w;
    m_statsHeight = h;
    m_statsFps    = payload.value("fps").toDouble(0.0);
    if (m_showStats && m_statsBadge) {
        m_statsBadge->show();
        m_statsBadge->raise();
        const quint64 drops = m_canvas ? m_canvas->framesSkipped()
                                       : static_cast<quint64>(-1);
        m_statsBadge->setStats(m_statsCodec, m_statsWidth, m_statsHeight,
                               m_statsFps, drops);
        if (!m_statsTicker.isActive()) m_statsTicker.start();
    }

    if (shmName.isEmpty())
        return;

    // Attach to the sidecar's shared memory
    if (!m_reader->attach(shmName, slotCount, slotBytes)) {
        m_timeLabel->setText("SHM attach failed");
        return;
    }

    m_canvas->attachShm(m_reader);
    m_canvas->startPolling();
}

void VideoPlayer::onTimeUpdate(double positionSec, double durationSec)
{
    if (m_seeking) return;

    // Batch 6.1 — stash last clean position for crash-recovery resume.
    m_lastKnownPosSec = positionSec;

    m_durationSec = durationSec;
    m_seekBar->setDurationSec(durationSec);
    qint64 posMs = static_cast<qint64>(positionSec * 1000);
    qint64 durMs = static_cast<qint64>(durationSec * 1000);

    m_seekBar->blockSignals(true);
    m_seekBar->setValue(durationSec > 0 ? static_cast<int>(positionSec / durationSec * 10000) : 0);
    m_seekBar->blockSignals(false);

    m_timeLabel->setText(formatTime(posMs));
    m_durLabel->setText(formatTime(durMs));

    // Save progress every update (~1/sec from sidecar)
    saveProgress(positionSec, durationSec);
}

void VideoPlayer::onStateChanged(const QString& state)
{
    if (state == "paused") {
        m_paused = true;
        updatePlayPauseIcon();
    } else if (state == "playing") {
        m_paused = false;
        updatePlayPauseIcon();
    } else if (state == "opening") {
        // PLAYER_UX_FIX Phase 1.2 — sidecar ack'd the open command, probe
        // + decoder setup in flight. Metadata (tracks_changed + media_info)
        // arrives post-probe courtesy of Phase 1.1; first_frame follows.
        // No UI binding yet — Phase 2.3 will connect playerOpeningStarted
        // to the Loading HUD widget.
        debugLog("[VideoPlayer] state=opening file=" + m_pendingFile);
        emit playerOpeningStarted(m_pendingFile);
    } else if (state == "idle") {
        // PLAYER_UX_FIX Phase 1.2 — sidecar torn down decode (eof, stop,
        // or probe/open failure). Phase 2.3's Loading HUD dismisses on
        // this edge.
        debugLog("[VideoPlayer] state=idle");
        emit playerIdle();
    }
}

void VideoPlayer::onEndOfFile()
{
    // VIDEO_PLAYER_FIX Batch 5.1 — queue-mode precedence at EOF:
    //   loopFile > repeatOne > (atEnd + repeatAll) > shuffle > auto-advance.
    // loopFile normally short-circuits sidecar-side — we never see the
    // `eof` event when the sidecar is doing the seek-to-0 itself — but
    // pre-5.1 sidecar binaries don't support set_loop_file and will still
    // emit eof, so handle it client-side for compatibility. Single seek.
    if (m_playlistDrawer && m_playlistDrawer->loopFile()) {
        m_sidecar->sendSeek(0.0);
        return;
    }
    if (m_playlistDrawer && m_playlistDrawer->repeatOne()) {
        m_sidecar->sendSeek(0.0);
        return;
    }

    const bool havePlaylist = !m_playlist.isEmpty();
    const bool atEnd = havePlaylist && (m_playlistIdx >= m_playlist.size() - 1);

    if (havePlaylist && atEnd && m_playlistDrawer && m_playlistDrawer->repeatAll()
        && m_playlist.size() > 1) {
        // Wrap to the top of the queue.
        m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
        m_carrySubLang   = langForTrackId(m_subTracks,   m_activeSubId);
        m_carryAspect    = m_currentAspect;
        m_carryCrop      = m_currentCrop;
        openFile(m_playlist.at(0), m_playlist, 0);
        return;
    }

    if (havePlaylist && m_playlistDrawer && m_playlistDrawer->shuffle()
        && m_playlist.size() > 1) {
        // Pick a random other index — bounded retry avoids the 1/N chance
        // of repeatedly picking the current one on a tiny queue.
        int next = m_playlistIdx;
        for (int i = 0; i < 4 && next == m_playlistIdx; ++i)
            next = QRandomGenerator::global()->bounded(m_playlist.size());
        if (next == m_playlistIdx)
            next = (m_playlistIdx + 1) % m_playlist.size();
        m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
        m_carrySubLang   = langForTrackId(m_subTracks,   m_activeSubId);
        m_carryAspect    = m_currentAspect;
        m_carryCrop      = m_currentCrop;
        openFile(m_playlist.at(next), m_playlist, next);
        return;
    }

    // Default: existing auto-advance behavior.
    if (havePlaylist && m_playlistIdx < m_playlist.size() - 1
        && m_playlistDrawer->isAutoAdvance()) {
        nextEpisode();
        return;
    }

    m_paused = true;
    updatePlayPauseIcon();
    showControls();
}

void VideoPlayer::onError(const QString& message)
{
    m_toastHud->showToast(message);
}

// ── Batch 6.1 — sidecar crash auto-restart ─────────────────────────────────
//
// Sidecar is an external process (ffmpeg_sidecar.exe) — a crash, OS kill,
// or TDR-style GPU reset can take it down mid-playback. We respawn it
// up to 3 times with 250/500/1000 ms backoff and reopen the current file
// at the last known PTS. After 3 failures we give up and surface the
// error so the user can intervene (restart Tankoban, check logs).

void VideoPlayer::onSidecarCrashed(int exitCode, QProcess::ExitStatus status)
{
    debugLog(QString("[VideoPlayer] sidecar crashed: exit=%1 status=%2 retry=%3 file=%4")
             .arg(exitCode).arg(status == QProcess::NormalExit ? "normal" : "crash")
             .arg(m_sidecarRetryCount).arg(m_currentFile));

    // Canvas and SHM reader are pointing at a dead producer — detach so
    // the next first_frame event can re-attach to the fresh sidecar's SHM.
    m_canvas->stopPolling();
    m_canvas->detachShm();
    m_canvas->detachD3D11Texture();
    m_reader->detach();

    // Nothing was playing (crash during idle) — no recovery possible or needed.
    if (m_currentFile.isEmpty()) {
        return;
    }

    if (m_sidecarRetryCount >= 3) {
        m_toastHud->showToast("Player stopped — reconnection failed");
        m_sidecarRetryCount = 0;
        return;
    }

    // Exponential backoff: 250 ms, 500 ms, 1000 ms.
    static constexpr int kBackoffMs[3] = { 250, 500, 1000 };
    const int delay = kBackoffMs[m_sidecarRetryCount];
    m_toastHud->showToast("Reconnecting player…");
    m_sidecarRestartTimer.start(delay);
}

void VideoPlayer::restartSidecar()
{
    if (m_currentFile.isEmpty()) return;

    ++m_sidecarRetryCount;
    m_pendingFile     = m_currentFile;
    m_pendingStartSec = m_lastKnownPosSec;
    // Crash respawn re-opens from scratch; let the next tracks_changed
    // re-apply preferences (user's mid-playback track is lost otherwise).
    m_tracksRestored  = false;
    // PLAYER_LIFECYCLE_FIX Phase 3 Batch 3.2 — arm the pending-open
    // token so onSidecarReady dispatches the resume-open when the
    // respawned sidecar emits `ready`. Without this, post-3.2
    // onSidecarReady's gate would block crash recovery.
    m_openPending = true;
    debugLog(QString("[VideoPlayer] restarting sidecar attempt %1 at pos %2s")
             .arg(m_sidecarRetryCount).arg(m_pendingStartSec, 0, 'f', 2));
    m_sidecar->start();
}

// ── UI ──────────────────────────────────────────────────────────────────────

void VideoPlayer::buildUI()
{
    m_canvas = new FrameCanvas(this);

    // Batch 1.2 — hand FrameCanvas a pointer to the master SyncClock so it
    // can call reportFrameLatency() after each Present. Phase 4 reads the
    // accumulated velocity to drive sidecar audio-speed adjustment.
    m_canvas->setSyncClock(&m_syncClock);
    m_syncClock.start();

    // Batch 4.1 (Player Polish Phase 4) — wire the drift-correction loop.
    // The SyncClock.reportFrameLatency path (shipped in Phase 1) feeds an
    // EMA that derives a clock velocity in [0.995, 1.000]. Previously
    // nobody consumed that velocity — today it becomes a live control
    // signal: every 500ms, read the current velocity and if it has moved
    // past the deadband (0.0005 = 0.05%) since last send, push it to the
    // sidecar via sendSetAudioSpeed. Sidecar's SwrContext applies
    // swr_set_compensation to pad/drop samples, closing the Kodi-DVDClock-
    // style A/V feedback loop end-to-end.
    m_audioSpeedTicker.setInterval(500);
    m_audioSpeedTicker.setTimerType(Qt::CoarseTimer);
    connect(&m_audioSpeedTicker, &QTimer::timeout, this, [this]() {
        if (!m_sidecar || !m_sidecar->isRunning()) return;
        const double speed = m_syncClock.getClockVelocity();
        // Deadband to avoid spamming the sidecar with sub-audible changes.
        // 0.0005 = 0.05% ≈ one imperceptible adjustment per tick threshold.
        if (std::abs(speed - m_lastSentAudioSpeed) < 0.0005) return;
        m_sidecar->sendSetAudioSpeed(speed);
        m_lastSentAudioSpeed = speed;
    });
    m_audioSpeedTicker.start();

    // FrameCanvas is a native D3D11 HWND (WA_PaintOnScreen) — mouse events
    // don't bubble to VideoPlayer. Forward via signal carrying y position.
    // Cursor unhide on any move; HUD reveal only when cursor enters the
    // bottom-edge zone.
    connect(m_canvas, &FrameCanvas::mouseActivityAt, this, [this](int y) {
        // Always: unhide cursor + arm cursor auto-hide.
        setCursor(Qt::ArrowCursor);
        m_cursorTimer.start();
        // HUD reveal only when cursor is within the bottom-edge zone. Zone
        // is the control-bar height plus a small margin so the bar appears
        // before the cursor reaches it.
        constexpr int kBottomRevealZonePx = 120;
        if (y >= m_canvas->height() - kBottomRevealZonePx) {
            showControls();
        }
    });
    connect(m_canvas, &FrameCanvas::canvasPixelSizeSettled, this,
        [this](int, int) {
            sendCanvasSizeToSidecar();
        });

    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName("VideoControlBar");
    m_controlBar->setStyleSheet(
        "QWidget#VideoControlBar {"
        "  background: rgba(10, 10, 10, 0.92);"
        "  border-top: 1px solid rgba(255, 255, 255, 0.08);"
        "}"
    );

    auto* rootLayout = new QVBoxLayout(m_controlBar);
    rootLayout->setContentsMargins(12, 6, 12, 6);
    rootLayout->setSpacing(4);

    // Icon button style (transparent bg, no border)
    auto iconBtnStyle =
        "QPushButton { background: transparent; border: none; padding: 0; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); border-radius: 6px; }"
        "QPushButton:pressed { background: rgba(255,255,255,0.04); }";

    // Chip button style (3-stop gradient).
    // PLAYER_UX_FIX Phase 6.1 adds three additional visual states beyond
    // the prior normal+hover pair: :checked (popover open), dynamic
    // property [active="true"] (the chip's feature is on — e.g. EQ preset
    // applied, filters configured), and :disabled (no file open). All
    // monochrome per feedback_no_color_no_emoji — the "active" indicator
    // is an off-white left-border strip that composes with the :checked
    // pressed-gradient.
    auto chipStyle =
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 rgba(68,68,68,0.95), stop:0.5 rgba(44,44,44,0.98),"
        "    stop:1 rgba(24,24,24,0.98));"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px;"
        "  color: rgba(245,245,245,0.98);"
        "  font-size: 11px; font-weight: 600;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "  stop:0 rgba(80,80,80,0.95), stop:0.5 rgba(56,56,56,0.98),"
        "  stop:1 rgba(36,36,36,0.98)); }"
        // Open state (popover showing): darker pressed-look gradient +
        // brighter border. Driven by setChecked(true) whenever the chip's
        // companion popover goes visible.
        "QPushButton:checked {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 rgba(30,30,30,0.98), stop:0.5 rgba(20,20,20,0.98),"
        "    stop:1 rgba(12,12,12,0.98));"
        "  border: 1px solid rgba(255,255,255,0.38);"
        "}"
        // Active state (EQ preset applied, filters configured, subtitle
        // track chosen). Dynamic property [active="true"] is toggled at
        // each chip's state-update call site; style()->polish() must be
        // called after setProperty to re-apply the CSS.
        "QPushButton[active=\"true\"] {"
        "  border-left: 3px solid rgba(245,245,245,0.75);"
        "  padding-left: 8px;"  // compensate padding so text doesn't shift
        "}"
        // Disabled state (no file open). Applied via setEnabled(false)
        // from openFile enable / teardownUi intentional-stop disable.
        "QPushButton:disabled {"
        "  background: rgba(30,30,30,0.60);"
        "  color: rgba(245,245,245,0.35);"
        "  border: 1px solid rgba(255,255,255,0.08);"
        "}";

    // ── Row 1: Seek row ──────────────────────────────────────────────
    auto* seekRow = new QHBoxLayout();
    seekRow->setSpacing(6);

    m_timeLabel = new QLabel("0:00", m_controlBar);
    m_timeLabel->setStyleSheet(
        "color: rgba(255,255,255,0.70); font-size: 11px; font-family: monospace;"
    );
    m_timeLabel->setFixedWidth(48);
    m_timeLabel->setAlignment(Qt::AlignCenter);

    m_seekBackBtn = new QPushButton(m_controlBar);
    m_seekBackBtn->setIcon(m_seekBackIcon);
    m_seekBackBtn->setIconSize(QSize(16, 16));
    m_seekBackBtn->setFixedSize(28, 28);
    m_seekBackBtn->setCursor(Qt::PointingHandCursor);
    m_seekBackBtn->setFocusPolicy(Qt::NoFocus);
    m_seekBackBtn->setStyleSheet(iconBtnStyle);
    connect(m_seekBackBtn, &QPushButton::clicked, this, [this]() {
        double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
        m_sidecar->sendSeek(qMax(0.0, curSec - 10.0));
        m_centerFlash->flash(SVG_SEEK_BACK);
        showControls();
    });

    m_seekBar = new SeekSlider(Qt::Horizontal, m_controlBar);
    m_seekBar->setDurationSec(0.0);
    connect(m_seekBar, &QSlider::sliderPressed, this, [this]() {
        m_seeking = true;
        m_seekDragOrigin = m_seekBar->value();
    });
    connect(m_seekBar, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        if (m_durationSec > 0)
            m_sidecar->sendSeek(m_seekBar->value() / 10000.0 * m_durationSec);
        if (m_seekDragOrigin >= 0) {
            m_centerFlash->flash(m_seekBar->value() > m_seekDragOrigin ? SVG_SEEK_FWD : SVG_SEEK_BACK);
            m_seekDragOrigin = -1;
        }
        m_timeBubble->hide();
    });
    connect(m_seekBar, &QSlider::sliderMoved, this, [this](int val) {
        m_pendingSeekVal = val;
        if (!m_seekThrottle.isActive())
            m_seekThrottle.start();
        if (m_durationSec > 0) {
            // Update time label immediately for responsive feel during drag
            m_timeLabel->setText(formatTime(static_cast<qint64>(val / 10000.0 * m_durationSec * 1000)));
            double sec = val / 10000.0 * m_durationSec;
            m_timeBubble->setText(formatTime(static_cast<qint64>(sec * 1000)));
            QRect sliderGeo = m_seekBar->geometry();
            QPoint barPos = m_controlBar->pos();
            int handleX = sliderGeo.x() + barPos.x()
                + static_cast<int>((double)val / 10000.0 * sliderGeo.width());
            int bw = m_timeBubble->sizeHint().width();
            int bx = qBound(0, handleX - bw / 2, width() - bw);
            int by = barPos.y() - m_timeBubble->sizeHint().height() - 4;
            m_timeBubble->move(bx, qMax(0, by));
            m_timeBubble->adjustSize();
            m_timeBubble->show();
            m_timeBubble->raise();
        }
    });
    connect(m_seekBar, &SeekSlider::hoverPositionChanged, this, [this](double fraction) {
        if (m_durationSec > 0) {
            double sec = fraction * m_durationSec;
            QString label = formatTime(static_cast<qint64>(sec * 1000));
            QRect sliderGeo = m_seekBar->geometry();
            QPoint barPos = m_controlBar->pos();
            // VIDEO_PLAYER_FIX Batch 2.1 — if cursor is within 8 px of a
            // chapter tick, prefix the tooltip with the chapter title
            // ("Chapter Title · 12:34"). Tolerance scales to slider pixel
            // width so short videos with tight ticks don't over-select.
            if (!m_chapters.isEmpty() && sliderGeo.width() > 0) {
                const double pixelTolFrac = 8.0 / sliderGeo.width();
                for (const auto& cv : m_chapters) {
                    const QJsonObject ch = cv.toObject();
                    const double startSec = ch.value("start").toDouble();
                    const double startFrac = startSec / m_durationSec;
                    if (std::fabs(fraction - startFrac) <= pixelTolFrac) {
                        QString title = ch.value("title").toString();
                        if (title.isEmpty())
                            title = QStringLiteral("Chapter");
                        label = title + QStringLiteral(" · ") + label;
                        break;
                    }
                }
            }
            m_timeBubble->setText(label);
            int handleX = sliderGeo.x() + barPos.x()
                + static_cast<int>(fraction * sliderGeo.width());
            int bw = m_timeBubble->sizeHint().width();
            int bx = qBound(0, handleX - bw / 2, width() - bw);
            int by = barPos.y() - m_timeBubble->sizeHint().height() - 4;
            m_timeBubble->move(bx, qMax(0, by));
            m_timeBubble->adjustSize();
            m_timeBubble->show();
            m_timeBubble->raise();
        }
    });
    connect(m_seekBar, &SeekSlider::hoverLeft, this, [this]() {
        m_timeBubble->hide();
    });

    m_seekFwdBtn = new QPushButton(m_controlBar);
    m_seekFwdBtn->setIcon(m_seekFwdIcon);
    m_seekFwdBtn->setIconSize(QSize(16, 16));
    m_seekFwdBtn->setFixedSize(28, 28);
    m_seekFwdBtn->setCursor(Qt::PointingHandCursor);
    m_seekFwdBtn->setFocusPolicy(Qt::NoFocus);
    m_seekFwdBtn->setStyleSheet(iconBtnStyle);
    connect(m_seekFwdBtn, &QPushButton::clicked, this, [this]() {
        double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
        m_sidecar->sendSeek(curSec + 10.0);
        m_centerFlash->flash(SVG_SEEK_FWD);
        showControls();
    });

    m_durLabel = new QLabel("0:00", m_controlBar);
    m_durLabel->setStyleSheet(
        "color: rgba(255,255,255,0.70); font-size: 11px; font-family: monospace;"
    );
    m_durLabel->setFixedWidth(48);
    m_durLabel->setAlignment(Qt::AlignCenter);

    seekRow->addWidget(m_timeLabel);
    seekRow->addWidget(m_seekBackBtn);
    seekRow->addWidget(m_seekBar, 1);
    seekRow->addWidget(m_seekFwdBtn);
    seekRow->addWidget(m_durLabel);

    rootLayout->addLayout(seekRow);

    // ── Row 2: Controls row ──────────────────────────────────────────
    auto* ctrlRow = new QHBoxLayout();
    ctrlRow->setSpacing(0);

    m_backBtn = new QPushButton(m_controlBar);
    m_backBtn->setIcon(m_backIcon);
    m_backBtn->setIconSize(QSize(18, 18));
    m_backBtn->setFixedSize(30, 30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFocusPolicy(Qt::NoFocus);
    m_backBtn->setStyleSheet(iconBtnStyle);
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested();
    });

    m_prevEpisodeBtn = new QPushButton(m_controlBar);
    m_prevEpisodeBtn->setIcon(m_prevEpIcon);
    m_prevEpisodeBtn->setIconSize(QSize(16, 16));
    m_prevEpisodeBtn->setFixedSize(32, 32);
    m_prevEpisodeBtn->setCursor(Qt::PointingHandCursor);
    m_prevEpisodeBtn->setFocusPolicy(Qt::NoFocus);
    m_prevEpisodeBtn->setStyleSheet(iconBtnStyle);
    connect(m_prevEpisodeBtn, &QPushButton::clicked, this, &VideoPlayer::prevEpisode);

    m_playPauseBtn = new QPushButton(m_controlBar);
    m_playPauseBtn->setIcon(m_pauseIcon);
    m_playPauseBtn->setIconSize(QSize(20, 20));
    m_playPauseBtn->setFixedSize(40, 36);
    m_playPauseBtn->setCursor(Qt::PointingHandCursor);
    m_playPauseBtn->setFocusPolicy(Qt::NoFocus);
    m_playPauseBtn->setStyleSheet(iconBtnStyle);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &VideoPlayer::togglePause);

    m_nextEpisodeBtn = new QPushButton(m_controlBar);
    m_nextEpisodeBtn->setIcon(m_nextEpIcon);
    m_nextEpisodeBtn->setIconSize(QSize(16, 16));
    m_nextEpisodeBtn->setFixedSize(32, 32);
    m_nextEpisodeBtn->setCursor(Qt::PointingHandCursor);
    m_nextEpisodeBtn->setFocusPolicy(Qt::NoFocus);
    m_nextEpisodeBtn->setStyleSheet(iconBtnStyle);
    connect(m_nextEpisodeBtn, &QPushButton::clicked, this, &VideoPlayer::nextEpisode);

    m_speedChip = new QPushButton("1.0x", m_controlBar);
    m_speedChip->setCursor(Qt::PointingHandCursor);
    m_speedChip->setFocusPolicy(Qt::NoFocus);
    m_speedChip->setStyleSheet(chipStyle);
    connect(m_speedChip, &QPushButton::clicked, this, [this]() {
        auto* menu = new QMenu(this);
        menu->setStyleSheet(
            "QMenu { background: rgba(16,16,16,240); border: 1px solid rgba(255,255,255,0.12);"
            "  border-radius: 6px; padding: 4px 0; }"
            "QMenu::item { color: rgba(245,245,245,0.98); padding: 6px 16px; font-size: 12px; }"
            "QMenu::item:selected { background: rgba(255,255,255,0.08); }"
            "QMenu::indicator { width: 14px; height: 14px; margin-left: 6px; }"
        );
        for (int i = 0; i < SPEED_COUNT; ++i) {
            QAction* action = menu->addAction(SPEED_LABELS[i]);
            action->setCheckable(true);
            action->setChecked(i == m_speedIdx);
            double rate = SPEED_PRESETS[i];
            int idx = i;
            connect(action, &QAction::triggered, this, [this, rate, idx]() {
                m_speedIdx = idx;
                m_sidecar->sendSetRate(rate);
                m_speedChip->setText(SPEED_LABELS[m_speedIdx]);
                m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
            });
        }
        menu->addSeparator();
        QAction* resetAct = menu->addAction("Reset to 1.0x");
        connect(resetAct, &QAction::triggered, this, [this]() {
            m_speedIdx = 2; // 1.0x
            m_sidecar->sendSetRate(1.0);
            m_speedChip->setText(SPEED_LABELS[m_speedIdx]);
            m_toastHud->showToast("Speed: 1.0x");
        });
        menu->exec(m_speedChip->mapToGlobal(m_speedChip->rect().topLeft()));
        menu->deleteLater();
    });

    m_filtersChip = new QPushButton("Filters", m_controlBar);
    m_filtersChip->setCursor(Qt::PointingHandCursor);
    m_filtersChip->setFocusPolicy(Qt::NoFocus);
    m_filtersChip->setStyleSheet(chipStyle);
    m_filtersChip->setCheckable(true);  // Phase 6.1 — :checked for open state
    connect(m_filtersChip, &QPushButton::clicked, this, [this]() {
        // Phase 6.4 — cross-chip exclusion: close any other open popover
        // before toggling our own, so only one popover is ever visible.
        dismissOtherPopovers(m_filterPopover);
        m_filterPopover->toggle(m_filtersChip);
        m_filtersChip->setChecked(m_filterPopover->isOpen());
    });

    m_eqChip = new QPushButton("EQ", m_controlBar);
    m_eqChip->setCursor(Qt::PointingHandCursor);
    m_eqChip->setFocusPolicy(Qt::NoFocus);
    m_eqChip->setStyleSheet(chipStyle);
    m_eqChip->setCheckable(true);
    connect(m_eqChip, &QPushButton::clicked, this, [this]() {
        dismissOtherPopovers(m_eqPopover);
        m_eqPopover->toggle(m_eqChip);
        m_eqChip->setChecked(m_eqPopover->isOpen());
    });

    m_trackChip = new QPushButton("Tracks", m_controlBar);
    m_trackChip->setCursor(Qt::PointingHandCursor);
    m_trackChip->setFocusPolicy(Qt::NoFocus);
    m_trackChip->setStyleSheet(chipStyle);
    m_trackChip->setCheckable(true);
    connect(m_trackChip, &QPushButton::clicked, this, [this]() {
        dismissOtherPopovers(m_trackPopover);
        m_trackPopover->setStyle(m_trackPopover->subFontSize(),
                                 m_trackPopover->subMargin(),
                                 m_trackPopover->subOutline());
        m_trackPopover->toggle(m_trackChip);
        m_trackChip->setChecked(m_trackPopover->isOpen());
    });

    m_playlistChip = new QPushButton("List", m_controlBar);
    m_playlistChip->setCursor(Qt::PointingHandCursor);
    m_playlistChip->setFocusPolicy(Qt::NoFocus);
    m_playlistChip->setStyleSheet(chipStyle);
    m_playlistChip->setCheckable(true);
    connect(m_playlistChip, &QPushButton::clicked, this, [this]() {
        // Playlist drawer is its own widget class (not a chip popover);
        // route through togglePlaylistDrawer which handles its specific
        // lifecycle. Checked-state sync happens post-toggle.
        if (m_playlistDrawer && m_playlistDrawer->isOpen()) {
            dismissOtherPopovers(nullptr);  // harmless — drawer closes below
        } else {
            dismissOtherPopovers(nullptr);  // close any chip popovers
        }
        togglePlaylistDrawer();
        m_playlistChip->setChecked(m_playlistDrawer && m_playlistDrawer->isOpen());
    });

    // Video title label — sits between the play controls and the chip
    // row in the empty space to the right of play/pause. Dim white,
    // small font, left-aligned with a margin so it doesn't crowd the
    // next-episode button. Elision (ellipsis on overflow) is re-applied
    // on resize via updateTitleElision(). Mouse events pass through so
    // clicking the label doesn't interfere with context-menu / drag.
    m_titleLabel = new QLabel(m_controlBar);
    m_titleLabel->setObjectName("VideoTitle");
    m_titleLabel->setStyleSheet(
        "QLabel#VideoTitle {"
        "  color: rgba(255,255,255,0.55);"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "  padding-left: 12px;"
        "  padding-right: 12px;"
        "}"
    );
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_titleLabel->setMinimumWidth(0);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    ctrlRow->addWidget(m_backBtn);
    ctrlRow->addSpacing(8);
    ctrlRow->addWidget(m_prevEpisodeBtn);
    ctrlRow->addWidget(m_playPauseBtn);
    ctrlRow->addWidget(m_nextEpisodeBtn);
    ctrlRow->addSpacing(8);
    ctrlRow->addWidget(m_titleLabel, 1);  // stretch factor 1 — eats leftover space
    ctrlRow->addWidget(m_speedChip);
    ctrlRow->addSpacing(4);
    ctrlRow->addWidget(m_filtersChip);
    ctrlRow->addSpacing(4);
    ctrlRow->addWidget(m_eqChip);
    ctrlRow->addSpacing(4);
    ctrlRow->addWidget(m_trackChip);
    ctrlRow->addSpacing(4);
    ctrlRow->addWidget(m_playlistChip);

    rootLayout->addLayout(ctrlRow);

    updateEpisodeButtons();

    // Volume HUD (transient overlay — appears on volume change, auto-fades)
    m_volumeHud = new VolumeHud(this);

    // Center flash (play/pause/seek feedback)
    m_centerFlash = new CenterFlash(this);

    // PLAYER_UX_FIX Phase 2.3 — Loading / Buffering overlay. Centered
    // over the canvas, bound to Phase 1.2 + 2.2 signals; dismisses on
    // first_frame or explicit playerIdle / bufferingEnded. Mouse-
    // transparent so controls below stay usable.
    //
    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — upgraded to classified
    // stage transitions via LoadingOverlay::setStage. Sub-stage wiring
    // connects SidecarProcess Phase 1.2 events (probe_start / probe_done
    // / decoder_open_start / decoder_open_done / first_packet_read /
    // first_decoder_receive) into stage transitions. Rule-14 picks:
    //   - probeStarted → Probing (probe_done doesn't transition; we
    //     stay in Probing until decoder_open_start, which happens
    //     right after probe succeeds anyway)
    //   - decoderOpenStarted → OpeningDecoder (decoder_open_done
    //     doesn't transition; we stay in OpeningDecoder until
    //     first_decoder_receive, which is the honest "making progress"
    //     signal)
    //   - firstPacketRead is connected for future diagnostics but does
    //     NOT drive a stage transition — packet-read success before
    //     receive-frame success can stall indefinitely on decoder back-
    //     pressure (libavcodec internal buffering); the DecodingFirstFrame
    //     stage waits for the more honest first_decoder_receive.
    //   - firstDecoderReceive → DecodingFirstFrame
    // Each lambda also re-emits the matching VideoPlayer-level signal
    // so Batch 1.3 (Agent 4's StreamPlayerController consumer, future)
    // has a stable pass-through contract.
    m_loadingOverlay = new LoadingOverlay(this);
    connect(this, &VideoPlayer::playerOpeningStarted,
            m_loadingOverlay, &LoadingOverlay::showLoading);
    connect(this, &VideoPlayer::playerIdle,
            m_loadingOverlay, &LoadingOverlay::dismiss);
    connect(m_sidecar, &SidecarProcess::bufferingStarted,
            m_loadingOverlay, &LoadingOverlay::showBuffering);
    connect(m_sidecar, &SidecarProcess::bufferingEnded,
            m_loadingOverlay, &LoadingOverlay::dismiss);
    // firstFrame is the primary dismiss trigger — by the time the first
    // video frame renders, the Loading window is semantically closed.
    // Qt permits a slot with fewer args than the signal, so firstFrame's
    // QJsonObject payload is discarded by dismiss().
    connect(m_sidecar, &SidecarProcess::firstFrame,
            m_loadingOverlay, &LoadingOverlay::dismiss);
    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.2 — cancel the 30s watchdog on
    // normal first-frame arrival. Without this, a fast open (sub-30s)
    // would leave the timer running until timeout, which would then try
    // to flip a dismissed overlay back to TakingLonger — harmless but
    // confusing in logs. Explicit stop() is clean.
    connect(m_sidecar, &SidecarProcess::firstFrame, this, [this]() {
        m_firstFrameWatchdog.stop();
    });

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — sub-stage wiring + Batch 1.3
    // re-emit pass-through. One lambda per sidecar signal; each drives a
    // stage transition (or not, per Rule-14 picks above) + re-emits on
    // VideoPlayer. Lambdas capture `this` — safe because SidecarProcess
    // is owned by VideoPlayer (parent), so connection lifetime is bounded.
    connect(m_sidecar, &SidecarProcess::probeStarted, this, [this]() {
        m_loadingOverlay->setStage(LoadingOverlay::Stage::Probing);
        emit probeStarted();
    });
    connect(m_sidecar, &SidecarProcess::probeDone, this, [this]() {
        // No stage transition — stay in Probing. Next transition fires
        // on decoder_open_start (typically right after probe success).
        emit probeDone();
    });
    connect(m_sidecar, &SidecarProcess::decoderOpenStarted, this, [this]() {
        m_loadingOverlay->setStage(LoadingOverlay::Stage::OpeningDecoder);
        emit decoderOpenStarted();
    });
    connect(m_sidecar, &SidecarProcess::decoderOpenDone, this, [this]() {
        // No stage transition — stay in OpeningDecoder. Next transition
        // fires on first_decoder_receive (honest forward-progress signal).
        emit decoderOpenDone();
    });
    connect(m_sidecar, &SidecarProcess::firstPacketRead, this, [this]() {
        // No stage transition — Rule-14 pick: packet-read success before
        // receive-frame success can stall on decoder back-pressure;
        // DecodingFirstFrame waits for first_decoder_receive instead.
        // Re-emit preserved for Batch 1.3 diagnostic correlation.
        emit firstPacketRead();
    });
    connect(m_sidecar, &SidecarProcess::firstDecoderReceive, this, [this]() {
        m_loadingOverlay->setStage(LoadingOverlay::Stage::DecodingFirstFrame);
        emit firstDecoderReceive();
    });

    // Track popover (audio/subtitle track picker — opened by Tracks chip)
    m_trackPopover = new TrackPopover(this);
    // Restore saved subtitle style from global preferences
    {
        QSettings s("Tankoban", "Tankoban");
        int fontSize = s.value("video_sub_font_size", 24).toInt();
        int margin = s.value("video_sub_margin", 40).toInt();
        bool outline = s.value("video_sub_outline", true).toBool();
        m_trackPopover->setStyle(fontSize, margin, outline);
    }
    connect(m_trackPopover, &TrackPopover::audioTrackSelected, this, [this](int id) {
        QString idStr = QString::number(id);
        m_sidecar->sendSetTracks(idStr, "");
        // Save preferred audio language globally
        QString lang = langForTrackId(m_audioTracks, idStr);
        if (!lang.isEmpty())
            QSettings("Tankoban", "Tankoban").setValue("video_preferred_audio_lang", lang);
        // User-explicit pick — update the active id locally so per-show
        // prefs save the fresh language (langForTrackId reads m_activeAudioId),
        // then write. Sidecar's trackChanged echo will redundantly set the
        // same value later; harmless.
        m_activeAudioId = idStr;
        saveShowPrefs();
        m_toastHud->showToast("Audio: track " + idStr);
    });
    connect(m_trackPopover, &TrackPopover::subtitleTrackSelected, this, [this](int id) {
        if (id == 0) {
            // VIDEO_PLAYER_FIX Batch 1.2 — unified Off path. Previous code
            // sent sendSetTracks("", "off") which crashes the sidecar via
            // std::stoi("off") at main.cpp:850. Route through setSubtitleOff
            // for visibility-only semantics (no set_tracks payload).
            setSubtitleOff();
        } else {
            QString idStr = QString::number(id);
            // Re-enable visibility before the track change — Phase 1 Batch
            // 1.2's Off path leaves visibility=false; without this, the
            // new track lands correctly but renderer stays hidden.
            if (!m_subsVisible) {
                m_subsVisible = true;
                m_sidecar->sendSetSubVisibility(true);
            }
            m_sidecar->sendSetTracks("", idStr);
            // Save preferred subtitle language globally
            QString lang = langForTrackId(m_subTracks, idStr);
            if (!lang.isEmpty())
                QSettings("Tankoban", "Tankoban").setValue("video_preferred_sub_lang", lang);
            m_activeSubId = idStr;
            saveShowPrefs();
            m_toastHud->showToast("Subtitle: track " + idStr);
        }
    });
    connect(m_trackPopover, &TrackPopover::subDelayAdjusted, this, [this](int deltaMs) {
        if (deltaMs == 0)
            m_subDelayMs = 0;
        else
            m_subDelayMs += deltaMs;
        m_sidecar->sendSetSubDelay(m_subDelayMs);
        m_trackPopover->setDelay(m_subDelayMs);
        m_toastHud->showToast("Sub delay: " + QString::number(m_subDelayMs) + "ms");
    });
    connect(m_trackPopover, &TrackPopover::subStyleChanged, this,
        [this](int fontSize, int margin, bool outline,
               const QString& fontColor, int bgOpacity) {
            m_sidecar->sendSetSubStyle(fontSize, margin, outline);
            m_subOverlay->setStyle(fontSize, margin, outline);
            m_subOverlay->setColors(fontColor, bgOpacity);
            // Save subtitle style globally
            QSettings s("Tankoban", "Tankoban");
            s.setValue("video_sub_font_size", fontSize);
            s.setValue("video_sub_margin", margin);
            s.setValue("video_sub_outline", outline);
            s.setValue("video_sub_font_color", fontColor);
            s.setValue("video_sub_bg_opacity", bgOpacity);
        });
    // Batch 5.3 — Tankostream subtitle menu (addon-fetched external subs
    // + load-from-file). Anchored above m_trackChip like TrackPopover.
    m_subMenu = new SubtitleMenu(this);
    m_subMenu->setSidecar(m_sidecar);

    connect(m_trackPopover, &TrackPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) {
            m_hideTimer.stop();
            showControls();
        } else {
            m_hideTimer.start(3000);
        }
    });

    // Subtitle overlay (sibling of FrameCanvas, NOT child of it — critical for QRhiWidget z-order)
    m_subOverlay = new SubtitleOverlay(this);

    // Playlist drawer (L key — right-side episode list)
    m_playlistDrawer = new PlaylistDrawer(this);
    connect(m_playlistDrawer, &PlaylistDrawer::episodeSelected, this, [this](int idx) {
        if (idx >= 0 && idx < m_playlist.size()) {
            m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
            m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
            m_carryAspect = m_currentAspect;
            m_carryCrop = m_currentCrop;
            openFile(m_playlist[idx], m_playlist, idx);
        }
    });
    // VIDEO_PLAYER_FIX Batch 5.1 — relay loop-file toggle to the sidecar.
    // Sidecar short-circuits EOF to seek-to-0 when enabled. Pre-5.1
    // sidecar binaries don't know `set_loop_file` and return
    // NOT_IMPLEMENTED (swallowed to debug log by SidecarProcess). The
    // persisted state applies on the NEXT openFile via onSidecarReady's
    // implicit initial send below.
    connect(m_playlistDrawer, &PlaylistDrawer::loopFileChanged, this,
            [this](bool on) { m_sidecar->sendSetLoopFile(on); });
    // VIDEO_PLAYER_FIX Batch 5.2 — save/load handoff. Drawer is UI-only;
    // VideoPlayer owns m_playlist + the file dialogs + the format parse.
    connect(m_playlistDrawer, &PlaylistDrawer::saveRequested, this, &VideoPlayer::saveQueue);
    connect(m_playlistDrawer, &PlaylistDrawer::loadRequested, this, &VideoPlayer::loadQueue);

    // Toast HUD (transient messages — speed, mute, track changes, errors)
    m_toastHud = new ToastHud(this);

    // VIDEO_PLAYER_FIX Batch 7.1 — stats badge (top-right overlay).
    m_statsBadge = new StatsBadge(this);
    m_statsBadge->hide();
    m_statsTicker.setInterval(1000);  // 1 Hz — cheap + matches audit spec
    connect(&m_statsTicker, &QTimer::timeout, this, [this]() {
        if (!m_showStats || !m_statsBadge) return;
        const quint64 drops = m_canvas ? m_canvas->framesSkipped()
                                       : static_cast<quint64>(-1);
        m_statsBadge->setStats(m_statsCodec, m_statsWidth, m_statsHeight,
                               m_statsFps, drops);
    });

    // Equalizer popover (10-band audio EQ — opened by EQ chip)
    m_eqPopover = new EqualizerPopover(this);
    connect(m_eqPopover, &EqualizerPopover::eqChanged, this, [this](const QString& eqFilter) {
        // Combine normalize + EQ into audio filter chain
        QStringList audioParts;
        if (m_filterPopover->normalize())
            audioParts << "loudnorm=I=-16";
        if (!eqFilter.isEmpty())
            audioParts << eqFilter;
        m_sidecar->sendRawFilters(m_filterPopover->buildVideoFilter(),
                                  audioParts.join(","));
        // Phase 6.1 — sync chip text + dynamic active-property for the
        // stylesheet's [active="true"] selector (left-border indicator).
        const bool eqActive = m_eqPopover->isActive();
        m_eqChip->setText(eqActive ? "EQ (on)" : "EQ");
        m_eqChip->setProperty("active", eqActive);
        m_eqChip->style()->unpolish(m_eqChip);
        m_eqChip->style()->polish(m_eqChip);
    });
    connect(m_eqPopover, &EqualizerPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) { m_hideTimer.stop(); showControls(); }
        else if (!m_paused) m_hideTimer.start();
    });
    // Batch 4.3 — Dynamic Range Compression toggle forwarded to sidecar.
    // Pre-Phase-4 sidecar binaries ignore the unknown command cleanly;
    // main-app ships safely without coordinated sidecar rebuild.
    connect(m_eqPopover, &EqualizerPopover::drcToggled, this, [this](bool enabled) {
        if (m_sidecar) m_sidecar->sendSetDrcEnabled(enabled);
    });

    // Filter popover (video/audio filters — opened by Filters chip)
    m_filterPopover = new FilterPopover(this);
    connect(m_filterPopover, &FilterPopover::filtersChanged, this,
        [this](bool deinterlace, int brightness, int contrast, int saturation, bool normalize) {
            // GPU-side: brightness/contrast/saturation applied in fragment shader (instant)
            m_canvas->setColorParams(
                brightness / 100.0f,    // -1.0 to 1.0
                contrast / 100.0f,      //  0.0 to 2.0
                saturation / 100.0f,    //  0.0 to 2.0
                1.0f                    // gamma neutral
            );
            // Sidecar-side: deinterlace mode, interpolation, audio normalize
            bool interpolate = m_filterPopover->interpolate();
            QString diFilter = m_filterPopover->deinterlaceFilter();
            m_sidecar->sendSetFilters(deinterlace, 0, 100, 100, normalize, interpolate, diFilter);
            int count = m_filterPopover->activeFilterCount();
            m_filtersChip->setText(count > 0 ? QString("Filters (%1)").arg(count) : "Filters");
            // Phase 6.1 — sync active-property for the left-border
            // indicator via the [active="true"] stylesheet selector.
            m_filtersChip->setProperty("active", count > 0);
            m_filtersChip->style()->unpolish(m_filtersChip);
            m_filtersChip->style()->polish(m_filtersChip);
        });
    connect(m_filterPopover, &FilterPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) {
            m_hideTimer.stop();
            showControls();
        } else if (!m_paused) {
            m_hideTimer.start();
        }
    });
    // HDR tone mapping: wire FilterPopover → shader (Batch 3.4).
    // Pre-3.4 path drove sidecar's ffmpeg tonemap filter via
    // sendSetToneMapping; Phase 3 moves the tonemap into our HLSL shader
    // so the same operator applies end-to-end (SDR, HDR10, HLG all share
    // one linear-light pipeline). sendSetToneMapping is kept as a no-op
    // stub for sidecar-protocol stability until Phase 3 exits; removing
    // it mid-phase would break any still-in-flight sidecar that expects
    // the command. The peakDetect toggle is no longer meaningful on the
    // shader side (we don't dynamically re-measure source peak per
    // frame); will be removed from FilterPopover in Batch 3.5 or at
    // phase exit.
    connect(m_filterPopover, &FilterPopover::toneMappingChanged, this,
        [this](const QString& algorithm, bool /*peakDetect*/) {
            // PLAYER_UX_FIX Phase 5.1 (Path A). FilterPopover's dropdown
            // is now {"hable", "reinhard"} — the two algorithms with
            // actual shader implementations. Previously six entries
            // landed here, four of which silently fell to mode 0 (Off).
            // Dead `aces` branch dropped alongside the popover shrink —
            // FilterPopover never emitted it. The Off fallback remains
            // as a defensive default for any out-of-list string (e.g.
            // legacy saved settings from a pre-5.1 build).
            int mode = 0; // Off / defensive fallback
            const QString a = algorithm.toLower();
            if      (a == QStringLiteral("hable"))    mode = 3;
            else if (a == QStringLiteral("reinhard")) mode = 1;
            if (m_canvas) m_canvas->setTonemapMode(mode);
        });
    // HDR detection + chapters from media_info event
    connect(m_sidecar, &SidecarProcess::mediaInfo, this, [this](const QJsonObject& info) {
        m_isHdr = info.value("hdr").toBool(false);
        m_filterPopover->setHdrMode(m_isHdr);

        // Batch 3.1 (Player Polish Phase 3) — forward raw AVCOL_PRI_* +
        // AVCOL_TRC_* values to FrameCanvas so its shader can pick the
        // right gamut matrix / (eventually) transfer function. Sidecar's
        // media_info payload carries these straight from the demuxer probe
        // (demuxer.cpp:66-68). SDR sources normally report BT.709 / sRGB
        // which select the identity / no-transform path in the shader.
        const int colorPri = info.value("color_primaries").toInt(0);
        const int colorTrc = info.value("color_trc").toInt(0);
        if (m_canvas) {
            m_canvas->setHdrColorInfo(colorPri, colorTrc);
        }

        m_chapters = info.value("chapters").toArray();
        if (!m_chapters.isEmpty())
            debugLog("[VideoPlayer] chapters: " + QString::number(m_chapters.size()));

        // VIDEO_PLAYER_FIX Batch 2.1 — push chapter start times (ms) to the
        // seek slider so it can render tick marks. Empty array clears any
        // previously-rendered ticks (fresh file with no chapter metadata).
        QList<qint64> chapterMs;
        chapterMs.reserve(m_chapters.size());
        for (const auto& c : m_chapters) {
            const double startSec = c.toObject().value("start").toDouble();
            chapterMs.append(static_cast<qint64>(startSec * 1000.0));
        }
        m_seekBar->setChapterMarkers(chapterMs);

        // Per-device audio offset auto-recall.
        // Sidecar reports the current audio output device (PortAudio name + host API).
        // We compute a stable QSettings key per device and look up the user's
        // calibrated offset. New Bluetooth devices get a 300ms default + a one-time
        // toast so the user knows to fine-tune. The default is generous because
        // AirPods on Windows (Bluetooth via Microsoft stack) typically has
        // 280-350ms of hidden latency that PortAudio can't see.
        constexpr int BT_DEFAULT_MS = 300;
        // Migration: previous default was 200ms. If the stored value matches the
        // old default exactly AND the device looks like Bluetooth AND the user
        // hasn't manually set a "manual" flag, treat it as auto and bump.
        constexpr int OLD_BT_DEFAULT_MS = 200;

        QString device  = info.value("audio_device").toString();
        QString hostApi = info.value("audio_host_api").toString();
        if (!device.isEmpty()) {
            m_audioDeviceKey = makeDeviceKey(device, hostApi);
            QString manualKey = m_audioDeviceKey + "/manual";
            QSettings s("Tankoban", "Tankoban");
            QVariant stored = s.value(m_audioDeviceKey);
            bool wasManual = s.value(manualKey, false).toBool();
            if (stored.isValid()) {
                m_audioDelayMs = stored.toInt();
                // One-time migration of the old auto-default to the new one.
                if (!wasManual && m_audioDelayMs == OLD_BT_DEFAULT_MS && looksLikeBluetooth(device)) {
                    m_audioDelayMs = BT_DEFAULT_MS;
                    s.setValue(m_audioDeviceKey, BT_DEFAULT_MS);
                    if (m_toastHud) {
                        m_toastHud->showToast(
                            QString("Bluetooth offset bumped to %1ms (improved default).\n"
                                    "Fine-tune with Ctrl+= / Ctrl+-.").arg(BT_DEFAULT_MS));
                    }
                    debugLog(QString("[VideoPlayer] migrated '%1' from 200ms → %2ms (auto-default bump)")
                                .arg(device).arg(BT_DEFAULT_MS));
                } else {
                    debugLog(QString("[VideoPlayer] audio device '%1' → recalled offset %2ms (%3)")
                                .arg(device).arg(m_audioDelayMs)
                                .arg(wasManual ? "manual" : "auto"));
                }
            } else if (looksLikeBluetooth(device)) {
                m_audioDelayMs = BT_DEFAULT_MS;
                s.setValue(m_audioDeviceKey, BT_DEFAULT_MS);
                // Not manual — leave manualKey unset
                if (m_toastHud) {
                    m_toastHud->showToast(
                        QString("Bluetooth audio detected — using %1ms offset.\n"
                                "Use Ctrl+= / Ctrl+- to fine-tune.").arg(BT_DEFAULT_MS));
                }
                debugLog(QString("[VideoPlayer] Bluetooth device '%1' → default %2ms")
                            .arg(device).arg(BT_DEFAULT_MS));
            } else {
                m_audioDelayMs = 0;
                debugLog(QString("[VideoPlayer] wired/unknown device '%1' → no offset").arg(device));
            }
            m_sidecar->sendSetAudioDelay(m_audioDelayMs);
        }
    });
    // D3D11 Holy Grail (Windows): when sidecar publishes its shared D3D11
    // texture handle, hand it to FrameCanvas to import for zero-copy display.
    // Eliminates GPU→CPU→GPU round trip per frame on HW-accelerated content.
    connect(m_sidecar, &SidecarProcess::d3d11Texture, this,
        [this](quintptr handle, int w, int h) {
            debugLog(QString("[VideoPlayer] d3d11_texture handle=0x%1 %2x%3")
                        .arg(handle, 0, 16).arg(w).arg(h));
            m_canvas->attachD3D11Texture(handle, w, h);
        });
    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — subtitle overlay SHM.
    // Sidecar writes libass/PGS BGRA into the named SHM each frame;
    // FrameCanvas opens it and uploads per-frame into its own locally-
    // owned D3D11 overlay texture, drawn as an alpha-blended quad after
    // the video quad. No cross-process GPU resource sharing.
    connect(m_sidecar, &SidecarProcess::overlayShm, this,
        [this](const QString& name, int w, int h) {
            debugLog(QString("[VideoPlayer] overlay_shm name=%1 %2x%3")
                        .arg(name).arg(w).arg(h));
            m_canvas->attachOverlayShm(name, w, h);
        });
    // FrameCanvas tells us when zero-copy import succeeded/failed so we can
    // tell the sidecar to short-circuit its CPU pipeline (saves ~15ms/frame).
    connect(m_canvas, &FrameCanvas::zeroCopyActivated, this, [this](bool active) {
        debugLog(QString("[VideoPlayer] zero-copy %1").arg(active ? "ACTIVE" : "INACTIVE"));
        m_sidecar->sendSetZeroCopyActive(active);
    });

    // Batch 6.2 — FrameCanvas announces D3D device-lost recovery; surface
    // a brief ToastHud so the user sees why the display stuttered.
    connect(m_canvas, &FrameCanvas::deviceReconnecting, this, [this]() {
        debugLog("[VideoPlayer] D3D device-lost — FrameCanvas recovering");
        m_toastHud->showToast("Reconnecting display…");
    });

    // Frame stepping feedback — update time display
    connect(m_sidecar, &SidecarProcess::frameStepped, this, [this](double posSec) {
        m_paused = true;
        updatePlayPauseIcon();
        qint64 posMs = static_cast<qint64>(posSec * 1000);
        m_timeLabel->setText(formatTime(posMs));
        if (m_durationSec > 0) {
            m_seekBar->blockSignals(true);
            m_seekBar->setValue(static_cast<int>(posSec / m_durationSec * 10000));
            m_seekBar->blockSignals(false);
        }
    });

    // Time bubble (seek preview — shown above slider during drag)
    m_timeBubble = new QLabel(this);
    m_timeBubble->setStyleSheet(
        "background: rgba(12,12,12,209); color: rgba(245,245,245,0.98);"
        "font-size: 11px; padding: 2px 6px; border-radius: 3px;"
        "border: 1px solid rgba(255,255,255,0.12);"
    );
    m_timeBubble->hide();
}

// ── Controls ────────────────────────────────────────────────────────────────

void VideoPlayer::togglePause()
{
    if (m_paused) {
        m_sidecar->sendResume();
        m_centerFlash->flash(SVG_PLAY);
    } else {
        m_sidecar->sendPause();
        m_centerFlash->flash(SVG_PAUSE);
    }
    showControls();
}

void VideoPlayer::toggleFullscreen()
{
    m_fullscreen = !m_fullscreen;
    emit fullscreenRequested(m_fullscreen);
    showControls();
}

void VideoPlayer::toggleMute()
{
    m_muted = !m_muted;
    m_sidecar->sendSetMute(m_muted);
    m_volumeHud->showVolume(m_volume, m_muted);
    m_toastHud->showToast(m_muted ? "Muted" : "Unmuted");
}

void VideoPlayer::speedUp()
{
    m_speedIdx = qMin(m_speedIdx + 1, SPEED_COUNT - 1);
    m_sidecar->sendSetRate(SPEED_PRESETS[m_speedIdx]);
    m_speedChip->setText(SPEED_LABELS[m_speedIdx]);
    m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
}

void VideoPlayer::speedDown()
{
    m_speedIdx = qMax(m_speedIdx - 1, 0);
    m_sidecar->sendSetRate(SPEED_PRESETS[m_speedIdx]);
    m_speedChip->setText(SPEED_LABELS[m_speedIdx]);
    m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
}

void VideoPlayer::speedReset()
{
    m_speedIdx = 2; // 1.0x
    m_sidecar->sendSetRate(1.0);
    m_speedChip->setText(SPEED_LABELS[m_speedIdx]);
    m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
}

// Merge incoming track list into existing cache. Upsert by 'id': add new
// tracks, update fields on existing ones, never remove. Defends against
// the sidecar emitting a shortened tracks_changed payload after a
// set_tracks command — without this, subsequent right-clicks would find an
// empty Subtitles submenu (bake-in bug 2026-04-14, confirmed pre-existing
// via Tankoban 2 Legacy reproduction). Reset semantics live in
// stopPlayback so file changes still get fresh lists.
static void mergeTrackList(QJsonArray& cache, const QJsonArray& incoming)
{
    for (const auto& v : incoming) {
        const QJsonObject t = v.toObject();
        const QString id = t["id"].toString();
        if (id.isEmpty()) continue;

        bool replaced = false;
        for (int i = 0; i < cache.size(); ++i) {
            if (cache[i].toObject()["id"].toString() == id) {
                cache[i] = t;     // update existing fields
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            cache.append(t);      // add new
        }
    }
}

void VideoPlayer::onTracksChanged(const QJsonArray& audio, const QJsonArray& subtitle,
                                   const QString& activeAudioId, const QString& activeSubId)
{
    mergeTrackList(m_audioTracks, audio);
    mergeTrackList(m_subTracks,   subtitle);
    m_activeAudioId = activeAudioId;
    m_activeSubId   = activeSubId;

    // Update track chip label with counts
    m_trackChip->setText("Tracks");

    // Build TrackPopover payload from the (post-merge) cached lists, NOT
    // the incoming event arrays — those may be shorter than what we've
    // already discovered. See mergeTrackList comment above.
    QJsonArray merged;
    for (const auto& v : m_audioTracks) {
        QJsonObject t = v.toObject();
        t["type"] = "audio";
        merged.append(t);
    }
    for (const auto& v : m_subTracks) {
        QJsonObject t = v.toObject();
        t["type"] = "subtitle";
        merged.append(t);
    }
    int audioId = activeAudioId.toInt();
    int subId   = activeSubId.toInt();
    m_trackPopover->populate(merged, audioId, subId, m_subsVisible);

    // Restore saved track preferences ONCE per file — only on the first
    // tracks_changed after openFile. Re-running on subsequent events
    // would override manual picks: user selects sub 3, sidecar echoes
    // tracks_changed with active_sub_id=3, preference match resolves
    // preferred-lang to a different id, set_tracks fires and yanks the
    // user's choice back. Latched via m_tracksRestored (reset on openFile).
    if (!m_tracksRestored) {
        m_tracksRestored = true;
        restoreTrackPreferences();
    }

    // External-sub auto-load. Runs once per file after tracks_changed
    // so m_subTracks reflects the authoritative embedded-track list. If
    // the file has zero embedded subs AND a sibling file with a matching
    // basename exists, load it. Gated on m_autoSubAttempted (one-shot)
    // + QSettings toggle. Stream-mode files (HTTP URLs) are skipped
    // inside the helper.
    if (!m_autoSubAttempted) {
        m_autoSubAttempted = true;
        tryAutoLoadSiblingSubtitle();
    }
}

void VideoPlayer::cycleAudioTrack()
{
    if (m_audioTracks.isEmpty()) {
        m_toastHud->showToast("No audio tracks");
        return;
    }

    // Find current index, advance to next
    int idx = -1;
    for (int i = 0; i < m_audioTracks.size(); ++i) {
        if (m_audioTracks[i].toObject()["id"].toString() == m_activeAudioId) {
            idx = i;
            break;
        }
    }
    idx = (idx + 1) % m_audioTracks.size();
    QJsonObject track = m_audioTracks[idx].toObject();
    QString newId = track["id"].toString();
    m_sidecar->sendSetTracks(newId, "");
    m_activeAudioId = newId;
    saveShowPrefs();
    QString lang = track["lang"].toString();
    if (lang.isEmpty()) lang = track["title"].toString();
    if (lang.isEmpty()) lang = QString::number(idx + 1);
    m_toastHud->showToast("Audio: " + lang);
}

void VideoPlayer::cycleSubtitleTrack()
{
    if (m_subTracks.isEmpty()) {
        m_toastHud->showToast("No subtitle tracks");
        return;
    }

    int idx = -1;
    for (int i = 0; i < m_subTracks.size(); ++i) {
        if (m_subTracks[i].toObject()["id"].toString() == m_activeSubId) {
            idx = i;
            break;
        }
    }
    idx = (idx + 1) % m_subTracks.size();
    QJsonObject track = m_subTracks[idx].toObject();
    QString newId = track["id"].toString();
    m_sidecar->sendSetTracks("", newId);
    if (!m_subsVisible) {
        m_subsVisible = true;
        m_sidecar->sendSetSubVisibility(true);
    }
    m_activeSubId = newId;
    saveShowPrefs();
    QString lang = track["lang"].toString();
    if (lang.isEmpty()) lang = track["title"].toString();
    if (lang.isEmpty()) lang = QString::number(idx + 1);
    m_toastHud->showToast("Subtitle: " + lang);
}

void VideoPlayer::toggleSubtitles()
{
    m_subsVisible = !m_subsVisible;
    m_sidecar->sendSetSubVisibility(m_subsVisible);
    saveShowPrefs();
    m_toastHud->showToast(m_subsVisible ? "Subtitles on" : "Subtitles off");
}

void VideoPlayer::setSubtitleOff()
{
    // Canonical Off path. Idempotent on sidecar (sendSetSubVisibility is a
    // cheap bool-set on handle_set_sub_visibility). Track selection stays
    // at whatever was last picked — picking a numeric track via a later
    // action will re-enable visibility and land set_tracks on the right id.
    m_subsVisible = false;
    m_sidecar->sendSetSubVisibility(false);
    saveShowPrefs();
    m_toastHud->showToast("Subtitles off");
}

void VideoPlayer::takeSnapshot()
{
    // VIDEO_PLAYER_FIX Batch 3.2 — covers both SHM and D3D11 zero-copy
    // paths (staging-texture readback added to captureCurrentFrame).
    QImage img = m_canvas->captureCurrentFrame();
    if (img.isNull()) {
        m_toastHud->showToast("Snapshot failed — no frame available");
        return;
    }

    const QString picturesDir = QStandardPaths::writableLocation(
        QStandardPaths::PicturesLocation);
    const QString snapDir = picturesDir + QStringLiteral("/Tankoban Snapshots");
    QDir().mkpath(snapDir);

    // {baseName}_{HH-MM-SS}_{ptsSec}.png — baseName comes from the current
    // file (or a generic "snapshot" fallback for stream URLs without a
    // filename). PTS integer seconds keeps filenames short + sortable.
    QString baseName;
    if (!m_currentFile.isEmpty()) {
        baseName = QFileInfo(m_currentFile).completeBaseName();
    }
    if (baseName.isEmpty())
        baseName = QStringLiteral("snapshot");

    const QString ts = QDateTime::currentDateTime().toString("HH-mm-ss");
    const qint64  ptsSec = static_cast<qint64>(m_lastKnownPosSec);
    const QString path = QString("%1/%2_%3_%4s.png")
                             .arg(snapDir, baseName, ts, QString::number(ptsSec));

    if (img.save(path, "PNG")) {
        m_toastHud->showToast("Snapshot saved: " + QFileInfo(path).fileName());
        debugLog("[VideoPlayer] snapshot saved: " + path);
    } else {
        m_toastHud->showToast("Snapshot save failed");
    }
}

void VideoPlayer::showOpenUrlDialog()
{
    OpenUrlDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString url = dlg.url();
    if (url.isEmpty()) return;
    debugLog("[VideoPlayer] openUrl: " + url);
    openFile(url);
}

void VideoPlayer::pushRecentFile(const QString& filePath)
{
    if (filePath.isEmpty()) return;
    QSettings s("Tankoban", "Tankoban");
    QStringList recent = s.value("player/recentFiles").toStringList();
    recent.removeAll(filePath);
    recent.prepend(filePath);
    while (recent.size() > 20) recent.removeLast();
    s.setValue("player/recentFiles", recent);
}

void VideoPlayer::appendToQueue(const QString& filePath)
{
    if (filePath.isEmpty()) return;
    if (m_playlist.isEmpty()) {
        // No existing playlist — seed it with the current file so the new
        // file has something to append after. m_currentFile may be empty
        // (pre-playback); guard separately.
        if (!m_currentFile.isEmpty())
            m_playlist.append(m_currentFile);
    }
    m_playlist.append(filePath);
    if (m_playlistDrawer)
        m_playlistDrawer->populate(m_playlist, m_playlistIdx);
    updateEpisodeButtons();
}

void VideoPlayer::saveQueue()
{
    if (m_playlist.isEmpty() && m_currentFile.isEmpty()) {
        m_toastHud->showToast("Queue is empty — nothing to save");
        return;
    }

    const QString suggest = QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                            + "/playlist.m3u";
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Queue"),
        suggest, tr("M3U Playlists (*.m3u);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_toastHud->showToast("Save failed: " + f.errorString());
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    // Standard .m3u / .m3u8 format. #EXTM3U header + #EXTINF per entry
    // with -1 duration sentinel (we don't probe every file just to save)
    // + the file's display name. Fully compatible with VLC, mpv, QMPlay2.
    out << "#EXTM3U\n";
    const QStringList entries = m_playlist.isEmpty() ? QStringList{m_currentFile} : m_playlist;
    for (const QString& p : entries) {
        QString title = QFileInfo(p).completeBaseName();
        if (title.isEmpty()) title = p;
        out << "#EXTINF:-1," << title << "\n" << p << "\n";
    }
    f.close();
    m_toastHud->showToast("Saved " + QString::number(entries.size()) + " to "
                          + QFileInfo(path).fileName());
}

void VideoPlayer::toggleStatsBadge()
{
    m_showStats = !m_showStats;
    QSettings("Tankoban", "Tankoban").setValue("player/showStats", m_showStats);
    if (!m_statsBadge) return;
    if (m_showStats) {
        // Populate immediately if we already have source metadata; on an
        // empty-m_statsCodec case (toggle pre-firstFrame) the badge stays
        // hidden until onFirstFrame arrives and calls setStats.
        const quint64 drops = m_canvas ? m_canvas->framesSkipped()
                                       : static_cast<quint64>(-1);
        m_statsBadge->setStats(m_statsCodec, m_statsWidth, m_statsHeight,
                               m_statsFps, drops);
        if (!m_statsCodec.isEmpty()) {
            m_statsBadge->show();
            m_statsBadge->raise();
        }
        if (!m_statsTicker.isActive()) m_statsTicker.start();
    } else {
        m_statsBadge->hide();
        m_statsTicker.stop();
    }
    m_toastHud->showToast(m_showStats ? "Stats: on" : "Stats: off");
}

void VideoPlayer::openKeybindingEditor()
{
    // Stack-allocated modal: the editor mutates m_keys in-place as the user
    // accepts bindings (setBinding persists to QSettings per edit), so we
    // don't need to wait for the dialog to close to apply changes.
    KeybindingEditor dlg(m_keys, this);
    dlg.exec();
}

void VideoPlayer::loadQueue()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Queue"),
        start, tr("M3U Playlists (*.m3u *.m3u8);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_toastHud->showToast("Load failed: " + f.errorString());
        return;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QStringList parsed;
    const QDir baseDir = QFileInfo(path).absoluteDir();
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        // Treat as path or URL. Relative paths are resolved against the
        // .m3u file's directory — standard player behavior.
        if (player_utils::looksLikeUrl(line)) {
            parsed.append(line);
        } else {
            QFileInfo fi(line);
            if (fi.isAbsolute()) parsed.append(fi.absoluteFilePath());
            else                 parsed.append(baseDir.absoluteFilePath(line));
        }
    }
    f.close();

    if (parsed.isEmpty()) {
        m_toastHud->showToast("No playable entries in " + QFileInfo(path).fileName());
        return;
    }

    // Prompt Replace vs Append when a queue is already loaded.
    bool append = false;
    if (!m_playlist.isEmpty() || !m_currentFile.isEmpty()) {
        QMessageBox box(this);
        box.setWindowTitle(tr("Load Queue"));
        box.setText(tr("Replace the current queue or append to it?"));
        QPushButton* replaceBtn = box.addButton(tr("Replace"), QMessageBox::AcceptRole);
        QPushButton* appendBtn  = box.addButton(tr("Append"),  QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == appendBtn)   append = true;
        else if (box.clickedButton() != replaceBtn) return;  // cancelled
    }

    if (append) {
        for (const QString& p : parsed) appendToQueue(p);
        m_toastHud->showToast(QString("Appended %1 to queue").arg(parsed.size()));
    } else {
        openFile(parsed.first(), parsed, 0);
        m_toastHud->showToast(QString("Loaded %1-item queue").arg(parsed.size()));
    }
}

void VideoPlayer::togglePictureInPicture()
{
    QWidget* top = window();
    if (!top) return;

    if (m_inPip) {
        // Exit PiP — restore geometry + flags + HUD.
        top->setWindowFlags(m_prePipFlags);
        top->setGeometry(m_prePipGeometry);
        if (m_prePipFullscreen) {
            // User was fullscreen before PiP; re-enter fullscreen via the
            // normal path so the existing fullscreen bookkeeping runs.
            top->show();
            if (!m_fullscreen) toggleFullscreen();
        } else {
            top->show();
        }
        m_inPip = false;
        showControls();
        m_toastHud->showToast("Exited Picture-in-Picture");
        return;
    }

    // Enter PiP. Exit fullscreen first — PiP's always-on-top + framed
    // geometry don't compose with Qt's fullscreen state.
    m_prePipFullscreen = m_fullscreen;
    if (m_fullscreen) toggleFullscreen();

    m_prePipGeometry = top->geometry();
    m_prePipFlags    = top->windowFlags();

    // FramelessWindowHint strips the title bar + borders; WindowStaysOnTopHint
    // pins above other apps. Keep existing flags (hint OR) so platform
    // window attributes stay consistent.
    top->setWindowFlags(top->windowFlags()
                        | Qt::FramelessWindowHint
                        | Qt::WindowStaysOnTopHint);

    // 320x180 matches the TODO spec + is 16:9 so most content fits without
    // distracting letterbox. Bottom-right of the screen the window is
    // currently on (multi-monitor aware). 24 px margin from screen edges.
    const int pipW = 320;
    const int pipH = 180;
    QScreen* screen = top->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    QRect avail = screen->availableGeometry();
    const int x = avail.right()  - pipW - 24;
    const int y = avail.bottom() - pipH - 24;
    top->setGeometry(x, y, pipW, pipH);

    top->show();  // Required to apply the new window flags at runtime.
    hideControls();
    m_inPip = true;
    m_toastHud->showToast("Picture-in-Picture — Ctrl+P or Esc to exit");
}

void VideoPlayer::toggleAlwaysOnTop()
{
    // Target is the top-level window (MainWindow). VideoPlayer itself is
    // a child widget; setting the flag on `this` has no effect on the
    // shell window the user actually sees.
    QWidget* top = window();
    if (!top) return;

    m_alwaysOnTop = !m_alwaysOnTop;
    // Qt requires setWindowFlag + show() for runtime flag changes — the
    // underlying platform window needs to be recreated. Remember focus
    // state so the re-show doesn't steal or lose it in surprising ways.
    const bool wasVisible = top->isVisible();
    top->setWindowFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
    if (wasVisible) top->show();

    QSettings("Tankoban", "Tankoban").setValue("player/alwaysOnTop", m_alwaysOnTop);
    m_toastHud->showToast(m_alwaysOnTop ? "Always on top: on" : "Always on top: off");
}

void VideoPlayer::prevEpisode()
{
    if (m_playlist.isEmpty() || m_playlistIdx <= 0) return;
    // Carry forward current track language preferences
    m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
    m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
    m_carryAspect = m_currentAspect;
    m_carryCrop = m_currentCrop;
    openFile(m_playlist[m_playlistIdx - 1], m_playlist, m_playlistIdx - 1);
}

void VideoPlayer::nextEpisode()
{
    if (m_playlist.isEmpty() || m_playlistIdx >= m_playlist.size() - 1) return;
    // Carry forward current track language preferences
    m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
    m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
    m_carryAspect = m_currentAspect;
    m_carryCrop = m_currentCrop;
    openFile(m_playlist[m_playlistIdx + 1], m_playlist, m_playlistIdx + 1);
}

void VideoPlayer::togglePlaylistDrawer()
{
    if (m_playlist.isEmpty()) return;
    // Pass the chip as anchor so a second click on it closes the drawer
    // instead of triggering dismiss-then-reopen (PlaylistDrawer::eventFilter
    // swallows presses on the tracked anchor).
    m_playlistDrawer->toggle(m_playlistChip);
    if (m_playlistDrawer->isOpen()) {
        showControls();
        m_hideTimer.stop();
    }
}

void VideoPlayer::adjustVolume(int delta)
{
    // Batch 4.2 — range extended from [0, 100] to [0, 200]. 100 stays
    // the unity / pre-4.2 default; 101–200 is the amp zone (linear gain
    // up to 2×, capped at +6 dB). Sidecar applies tanh soft-clip when
    // gain > 1.0 so dialogue-heavy quiet sources stay audible without
    // the harsh clipping you'd get from straight multiplication.
    m_volume = qBound(0, m_volume + delta, 200);
    if (m_muted && delta > 0) {
        m_muted = false;
        m_sidecar->sendSetMute(false);
    }
    m_sidecar->sendSetVolume(m_volume / 100.0);  // 150 → 1.5, etc.
    m_volumeHud->showVolume(m_volume, m_muted);
}

void VideoPlayer::updatePlayPauseIcon()
{
    m_playPauseBtn->setIcon(m_paused ? m_playIcon : m_pauseIcon);
}


void VideoPlayer::updateEpisodeButtons()
{
    bool multi = m_playlist.size() > 1;
    m_prevEpisodeBtn->setVisible(multi);
    m_nextEpisodeBtn->setVisible(multi);
    if (multi) {
        m_prevEpisodeBtn->setEnabled(m_playlistIdx > 0);
        m_nextEpisodeBtn->setEnabled(m_playlistIdx < m_playlist.size() - 1);
    }
}

void VideoPlayer::showControls()
{
    m_controlBar->show();
    m_subOverlay->setControlsVisible(true);
    // The control bar was possibly hidden when the title label received
    // its intended width; re-elide now that layout is guaranteed to have
    // assigned the label its geometry.
    updateTitleElision();
    // Lift subtitle overlay above the HUD so the control bar doesn't
    // occlude subs. Physical pixels — multiply by dpr so the lift is
    // consistent on HiDPI displays where the swap chain is in physical
    // pixels but sizeHint() returns logical.
    if (m_canvas) {
        const qreal dpr = devicePixelRatioF();
        const int liftPx = qRound(m_controlBar->sizeHint().height() * dpr);
        m_canvas->setSubtitleLift(liftPx);
    }
    setCursor(Qt::ArrowCursor);
    m_cursorTimer.start();
    // Don't restart auto-hide timer while playlist drawer is open
    if (!m_playlistDrawer || !m_playlistDrawer->isOpen())
        m_hideTimer.start();
}

void VideoPlayer::hideControls()
{
    if (m_paused) return;
    if (m_seeking) return;
    m_controlBar->hide();
    m_subOverlay->setControlsVisible(false);
    // HUD gone — drop subtitles back to their natural position.
    if (m_canvas) m_canvas->setSubtitleLift(0);
}

void VideoPlayer::saveProgress(double positionSec, double durationSec)
{
    if (!m_bridge || m_currentVideoId.isEmpty() || m_currentFile.isEmpty())
        return;

    QJsonObject data;
    data["positionSec"] = positionSec;
    data["durationSec"] = durationSec;
    data["path"]        = m_currentFile;
    // Track & subtitle state persistence
    data["audioLang"]    = langForTrackId(m_audioTracks, m_activeAudioId);
    data["subtitleLang"] = langForTrackId(m_subTracks, m_activeSubId);
    // Track ids alongside language so restore can distinguish same-lang
    // tracks (e.g., English-forced stream 2 vs English-full stream 3).
    // Restore tries id-first with lang validation, falls back to lang
    // when the id is missing or its lang has changed.
    data["audioTrackId"]    = m_activeAudioId;
    data["subtitleTrackId"] = m_activeSubId;
    data["subsVisible"]  = m_subsVisible;
    data["subDelayMs"]   = m_subDelayMs;
    // Aspect override token — stored even when "original" so the restore
    // path can distinguish "user explicitly picked original" from "never
    // set" via QJsonObject::contains().
    data["aspectOverride"] = m_currentAspect;
    data["cropOverride"]   = m_currentCrop;
    // Gated on PersistenceMode::LibraryVideos. In None mode, StreamPage's
    // progressUpdated listener writes into the "stream" domain instead —
    // so we MUST still emit the signal below, just skip the "videos"
    // write that would pollute the Videos-mode continue-watching store.
    if (m_persistenceMode == PersistenceMode::LibraryVideos) {
        m_bridge->saveProgress("videos", m_currentVideoId, data);
    }
    emit progressUpdated(m_currentFile, positionSec, durationSec);
}

QString VideoPlayer::langForTrackId(const QJsonArray& tracks, const QString& id)
{
    for (const auto& v : tracks) {
        QJsonObject t = v.toObject();
        if (t["id"].toString() == id)
            return t["lang"].toString();
    }
    return {};
}

QString VideoPlayer::findTrackByLang(const QJsonArray& tracks, const QString& lang)
{
    if (lang.isEmpty()) return {};
    for (const auto& v : tracks) {
        QJsonObject t = v.toObject();
        if (t["lang"].toString() == lang)
            return t["id"].toString();
    }
    return {};
}

void VideoPlayer::restoreTrackPreferences()
{
    // Priority: carry-forward > per-file > per-show > global > sidecar default
    // Each layer contributes (id, lang) pairs. First non-empty wins per layer.
    // Final resolution: try id (validated against lang), fall back to lang.
    QString targetAudioLang, targetSubLang;
    QString targetAudioId,   targetSubId;
    bool perFileVisibilityApplied = false;

    if (!m_carryAudioLang.isEmpty()) {
        targetAudioLang = m_carryAudioLang;
        targetSubLang = m_carrySubLang;
        m_carryAudioLang.clear();
        m_carrySubLang.clear();
    } else if (m_bridge && m_persistenceMode == PersistenceMode::LibraryVideos) {
        // Gated — Stream-mode playback doesn't persist or restore track
        // preferences via the "videos" domain. Falls through to the
        // sidecar-default track selection path after this branch when
        // targetAudioLang / targetSubLang remain empty.
        QJsonObject prog = m_bridge->progress("videos", m_currentVideoId);
        targetAudioLang = prog.value("audioLang").toString();
        targetSubLang = prog.value("subtitleLang").toString();
        targetAudioId = prog.value("audioTrackId").toString();
        targetSubId = prog.value("subtitleTrackId").toString();

        // Restore per-file subtitle visibility and delay
        if (prog.contains("subsVisible")) {
            bool vis = prog.value("subsVisible").toBool(true);
            if (vis != m_subsVisible) {
                m_subsVisible = vis;
                m_sidecar->sendSetSubVisibility(vis);
            }
            perFileVisibilityApplied = true;
        }
        if (prog.contains("subDelayMs")) {
            int delay = prog.value("subDelayMs").toInt(0);
            if (delay != 0) {
                m_subDelayMs = delay;
                m_sidecar->sendSetSubDelay(delay);
                m_trackPopover->setDelay(delay);
            }
        }
    }

    // Per-show layer — folder-scoped prefs inherit across episodes of the
    // same show. Only fills in fields the per-file record didn't already
    // set, so a user's explicit per-episode choice still wins.
    if (m_bridge && m_persistenceMode == PersistenceMode::LibraryVideos) {
        QJsonObject showPrefs = loadShowPrefs();
        if (targetAudioLang.isEmpty())
            targetAudioLang = showPrefs.value("audioLang").toString();
        if (targetSubLang.isEmpty())
            targetSubLang = showPrefs.value("subtitleLang").toString();
        if (targetAudioId.isEmpty())
            targetAudioId = showPrefs.value("audioTrackId").toString();
        if (targetSubId.isEmpty())
            targetSubId = showPrefs.value("subtitleTrackId").toString();
        if (!perFileVisibilityApplied && showPrefs.contains("subsVisible")) {
            bool vis = showPrefs.value("subsVisible").toBool(true);
            if (vis != m_subsVisible) {
                m_subsVisible = vis;
                m_sidecar->sendSetSubVisibility(vis);
            }
        }
    }

    // Fall back to global preferred languages
    QSettings settings("Tankoban", "Tankoban");
    if (targetAudioLang.isEmpty())
        targetAudioLang = settings.value("video_preferred_audio_lang").toString();
    if (targetSubLang.isEmpty())
        targetSubLang = settings.value("video_preferred_sub_lang").toString();

    // Resolution lambda: prefer id match (validated by lang agreement)
    // over bare lang match. Required so a same-lang-but-different-track
    // saved pick (English-forced stream 2 vs English-full stream 3)
    // restores to the exact track rather than the first "eng" track.
    auto resolveTrack = [](const QJsonArray& tracks, const QString& id,
                           const QString& lang) -> QString {
        if (!id.isEmpty()) {
            for (const auto& v : tracks) {
                QJsonObject t = v.toObject();
                if (t["id"].toString() == id) {
                    const QString trackLang = t["lang"].toString();
                    // Accept id if stored lang agrees (or either side is
                    // untagged — robust to partial metadata).
                    if (lang.isEmpty() || trackLang.isEmpty() || trackLang == lang)
                        return id;
                    break;  // id exists but lang drifted — fall back to lang
                }
            }
        }
        return findTrackByLang(tracks, lang);
    };

    QString audioId = resolveTrack(m_audioTracks, targetAudioId, targetAudioLang);
    QString subId   = resolveTrack(m_subTracks,   targetSubId,   targetSubLang);

    if ((!audioId.isEmpty() && audioId != m_activeAudioId) ||
        (!subId.isEmpty() && subId != m_activeSubId)) {
        m_sidecar->sendSetTracks(
            audioId.isEmpty() ? "" : audioId,
            subId.isEmpty() ? "" : subId);
    }

    // Player Polish Batch 5.1 fix (2026-04-15): unconditionally sync
    // sidecar visibility with our m_subsVisible state on every file
    // open. Pre-fix path only sent sendSetSubVisibility when a per-file
    // preference existed (line ~1324 above) — fresh files (no prior
    // viewing history) never got the command, leaving the sidecar's
    // renderer state divergent from m_subsVisible. Symptom: subtitles
    // inconsistently appear across videos (the user reports "subtitles
    // appear for some videos, not others"). Fix: always send the
    // current intent, on every file open. Idempotent — sidecar's
    // handle_set_sub_visibility is cheap when state hasn't changed.
    if (m_sidecar) {
        m_sidecar->sendSetSubVisibility(m_subsVisible);
    }
}

QString VideoPlayer::videoIdForFile(const QString& filePath)
{
    QFileInfo fi(filePath);
    if (!fi.exists()) return {};

    // SHA1(absoluteFilePath + "::" + fileSize + "::" + lastModifiedMs)
    // Matches GroundWorks _video_id_for_file format
    QString key = fi.absoluteFilePath()
                + "::" + QString::number(fi.size())
                + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());

    QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1);
    return hash.toHex();
}

QString VideoPlayer::showIdForFile(const QString& filePath)
{
    if (filePath.isEmpty()) return {};
    QString parent = QFileInfo(filePath).absolutePath();
    parent = QDir::cleanPath(QDir::fromNativeSeparators(parent));
#ifdef Q_OS_WIN
    parent = parent.toLower();
#endif
    return parent;
}

double VideoPlayer::aspectStringToDouble(const QString& token)
{
    if (token == QLatin1String("4:3"))    return 4.0 / 3.0;
    if (token == QLatin1String("16:9"))   return 16.0 / 9.0;
    if (token == QLatin1String("2.35:1")) return 2.35;
    if (token == QLatin1String("1.85:1")) return 1.85;
    return 0.0;  // "original" or unknown -> let native aspect apply
}

double VideoPlayer::cropStringToDouble(const QString& token)
{
    if (token == QLatin1String("4:3"))    return 4.0 / 3.0;
    if (token == QLatin1String("16:9"))   return 16.0 / 9.0;
    if (token == QLatin1String("1.85:1")) return 1.85;
    if (token == QLatin1String("2.35:1")) return 2.35;
    if (token == QLatin1String("2.39:1")) return 2.39;
    return 0.0;  // "none" or unknown -> no crop
}

QJsonObject VideoPlayer::loadShowPrefs() const
{
    if (!m_bridge || m_currentShowId.isEmpty()) return {};
    if (m_persistenceMode != PersistenceMode::LibraryVideos) return {};
    return m_bridge->progress("shows", m_currentShowId);
}

void VideoPlayer::saveShowPrefs()
{
    if (!m_bridge || m_currentShowId.isEmpty()) return;
    if (m_persistenceMode != PersistenceMode::LibraryVideos) return;

    // Read-modify-write: fetching the existing record first guarantees
    // that a single-field mutation (e.g., user just changed aspect) can't
    // wipe unrelated fields (audioLang/subtitleLang/subsVisible) that
    // the user set in a prior action. CoreBridge::saveProgress stamps
    // updatedAt automatically.
    QJsonObject data = m_bridge->progress("shows", m_currentShowId);
    data["aspectOverride"] = m_currentAspect;
    data["cropOverride"]   = m_currentCrop;
    const QString audioLang = langForTrackId(m_audioTracks, m_activeAudioId);
    if (!audioLang.isEmpty()) data["audioLang"] = audioLang;
    const QString subLang = langForTrackId(m_subTracks, m_activeSubId);
    if (!subLang.isEmpty()) data["subtitleLang"] = subLang;
    // Track ids alongside language so restore can pick the exact track
    // the user chose (e.g., the full English sub vs. a forced/signs-only
    // English track — both tagged "eng"). ID-first match with lang
    // validation on restore, with lang-only as fallback.
    if (!m_activeAudioId.isEmpty()) data["audioTrackId"]    = m_activeAudioId;
    if (!m_activeSubId.isEmpty())   data["subtitleTrackId"] = m_activeSubId;
    data["subsVisible"] = m_subsVisible;
    m_bridge->saveProgress("shows", m_currentShowId, data);
}

QString VideoPlayer::formatTime(qint64 ms)
{
    int totalSecs = static_cast<int>(ms / 1000);
    int h = totalSecs / 3600;
    int m = (totalSecs % 3600) / 60;
    int s = totalSecs % 60;
    if (h > 0)
        return QString::asprintf("%d:%02d:%02d", h, m, s);
    return QString::asprintf("%d:%02d", m, s);
}

QIcon VideoPlayer::iconFromSvg(const QByteArray& svg, int size)
{
    QSvgRenderer renderer(svg);
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    renderer.render(&p);
    return QIcon(pix);
}

// ── Layout ──────────────────────────────────────────────────────────────────

void VideoPlayer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_canvas->setGeometry(0, 0, width(), height());

    int barH = m_controlBar->sizeHint().height();
    m_controlBar->setGeometry(0, height() - barH, width(), barH);
    // Re-elide the title once the control bar's layout has applied the
    // new width (title label gets its leftover-space share on layout pass).
    updateTitleElision();

    // Playlist drawer: right side, 12px from edge, 10px from top, above control bar
    int dw = 320;
    int dh = height() - 22 - barH;
    m_playlistDrawer->setGeometry(width() - dw - 12, 10, dw, dh);

    // VolumeHUD position: centered horizontally, above control bar with 18px gap
    {
        int vBarH = m_controlBar->isVisible() ? m_controlBar->sizeHint().height() : 0;
        int vx = (width() - m_volumeHud->width()) / 2;
        int vy = height() - vBarH - m_volumeHud->height() - 18;
        m_volumeHud->move(vx, vy);
    }

    // Toast position: top-right corner
    if (m_toastHud) {
        m_toastHud->setGeometry(width() - 280 - 12, 12, 280, m_toastHud->sizeHint().height());
        m_toastHud->raise();
    }

    // VIDEO_PLAYER_FIX Batch 7.1 — stats badge: top-right, below toast
    // so it doesn't collide when both are visible. Auto-sized via
    // adjustSize() inside setStats; we only set position here.
    if (m_statsBadge && m_statsBadge->isVisible()) {
        const QSize sh = m_statsBadge->sizeHint();
        m_statsBadge->setGeometry(width() - sh.width() - 12, 52, sh.width(), sh.height());
        m_statsBadge->raise();
    }

    // Z-order: FrameCanvas → controlBar → subOverlay (above HUD) → transient overlays → drawer
    m_controlBar->raise();
    m_subOverlay->reposition();
    m_subOverlay->raise();
    m_volumeHud->raise();
    m_centerFlash->raise();
    if (m_playlistDrawer->isOpen())      m_playlistDrawer->raise();
}

// ── Input ───────────────────────────────────────────────────────────────────

void VideoPlayer::keyPressEvent(QKeyEvent* event)
{
    // Diagnostic: log every key press so we can see what arrives + what action it maps to.
    {
        QString actionName = m_keys ? m_keys->actionForKey(event->key(), event->modifiers()) : QString();
        debugLog(QString("[VideoPlayer] keyPress key=0x%1 mods=0x%2 action='%3'")
                    .arg(event->key(), 0, 16)
                    .arg(static_cast<int>(event->modifiers()), 0, 16)
                    .arg(actionName));
    }

    // PLAYER_UX_FIX Phase 6.4 — ESC dismisses any open chip popover
    // before falling through to the back-to-library / PiP-exit bindings.
    // Only intercept ESC when something is actually open, so the key
    // retains its normal binding behavior when no popover is showing.
    if (event->key() == Qt::Key_Escape) {
        const bool anyOpen =
            (m_filterPopover  && m_filterPopover->isOpen()) ||
            (m_eqPopover      && m_eqPopover->isOpen()) ||
            (m_trackPopover   && m_trackPopover->isOpen()) ||
            (m_playlistDrawer && m_playlistDrawer->isOpen());
        if (anyOpen) {
            dismissOtherPopovers(nullptr);
            event->accept();
            return;
        }
    }

    // VIDEO_PLAYER_FIX Batch 3.3 — in PiP, Escape exits PiP (preempts the
    // normal back_to_library binding). Gives the user a consistent
    // "tiny window feels like an overlay" exit without learning Ctrl+P.
    if (m_inPip && event->key() == Qt::Key_Escape) {
        togglePictureInPicture();
        event->accept();
        return;
    }

    // Enter/Return always toggles fullscreen (not rebindable)
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        toggleFullscreen();
        return;
    }

    // Look up action from configurable keybindings
    QString action = m_keys->actionForKey(event->key(), event->modifiers());
    if (action.isEmpty()) {
        // Legacy: C and X for speed (not in keybindings to avoid conflict with . and ,)
        if (event->key() == Qt::Key_C) { speedUp(); return; }
        if (event->key() == Qt::Key_X) { speedDown(); return; }
        if (event->key() == Qt::Key_Z && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            speedReset(); return;
        }
        QWidget::keyPressEvent(event);
        return;
    }

    auto seekBy = [this](double delta) {
        double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
        // Clear FrameCanvas's sustained-lag accumulator BEFORE the seek —
        // the wall-clock Present-interval gap across the seek would
        // otherwise arm the 3-in-a-row skip-next-Present guard and
        // produce a visible post-seek pause.
        if (m_canvas) m_canvas->resetLagAccounting();
        m_sidecar->sendSeek(qMax(0.0, curSec + delta));
        m_centerFlash->flash(delta > 0 ? SVG_SEEK_FWD : SVG_SEEK_BACK);
        // No showControls() — the center flash arrow is sufficient feedback.
        // Revealing the bottom HUD on every arrow-key seek is noisy and
        // fights the auto-hide UX when the user is scrubbing through content.
    };

    auto adjustSubDelay = [this](int delta) {
        m_subDelayMs += delta;
        m_sidecar->sendSetSubDelay(m_subDelayMs);
        m_trackPopover->setDelay(m_subDelayMs);
        m_toastHud->showToast("Sub delay: " + QString::number(m_subDelayMs) + "ms");
    };

    // Dispatch action
    if      (action == "toggle_pause")       togglePause();
    else if (action == "seek_back_10s")      seekBy(-10.0);
    else if (action == "seek_fwd_10s")       seekBy(10.0);
    else if (action == "seek_back_60s")      seekBy(-60.0);
    else if (action == "seek_fwd_60s")       seekBy(60.0);
    else if (action == "frame_step_fwd") {
        if (!m_paused) togglePause();
        m_sidecar->sendFrameStep(false);
        m_toastHud->showToast("Step forward");
    }
    else if (action == "frame_step_back") {
        if (!m_paused) togglePause();
        double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
        m_sidecar->sendFrameStep(true, curSec);
        m_toastHud->showToast("Step backward");
    }
    else if (action == "speed_up")           speedUp();
    else if (action == "speed_down")         speedDown();
    else if (action == "speed_reset")        speedReset();
    else if (action == "volume_up")          adjustVolume(5);
    else if (action == "volume_down")        adjustVolume(-5);
    else if (action == "toggle_mute")        toggleMute();
    else if (action == "toggle_fullscreen" || action == "toggle_fullscreen2")
                                             toggleFullscreen();
    else if (action == "toggle_deinterlace") {
        m_filterPopover->setDeinterlace(!m_filterPopover->deinterlace());
        m_toastHud->showToast(m_filterPopover->deinterlace() ? "Deinterlace on" : "Deinterlace off");
    }
    else if (action == "cycle_audio")        cycleAudioTrack();
    else if (action == "toggle_normalize") {
        m_filterPopover->setNormalize(!m_filterPopover->normalize());
        m_toastHud->showToast(m_filterPopover->normalize() ? "Normalization on" : "Normalization off");
    }
    else if (action == "cycle_subtitle")     cycleSubtitleTrack();
    else if (action == "toggle_subtitles")   toggleSubtitles();
    else if (action == "toggle_always_on_top") toggleAlwaysOnTop();
    else if (action == "take_snapshot")        takeSnapshot();
    else if (action == "toggle_pip")           togglePictureInPicture();
    else if (action == "open_url")             showOpenUrlDialog();
    else if (action == "toggle_stats")         toggleStatsBadge();
    else if (action == "open_subtitle_menu") m_subMenu->toggle(m_trackChip);
    else if (action == "sub_delay_minus")    adjustSubDelay(-100);
    else if (action == "sub_delay_plus")     adjustSubDelay(100);
    else if (action == "sub_delay_reset") {
        m_subDelayMs = 0;
        m_sidecar->sendSetSubDelay(0);
        m_trackPopover->setDelay(0);
        m_toastHud->showToast("Sub delay reset");
    }
    else if (action == "audio_delay_minus") {
        m_audioDelayMs -= 50;
        m_sidecar->sendSetAudioDelay(m_audioDelayMs);
        if (!m_audioDeviceKey.isEmpty()) {
            QSettings s("Tankoban", "Tankoban");
            s.setValue(m_audioDeviceKey, m_audioDelayMs);
            s.setValue(m_audioDeviceKey + "/manual", true);  // user touched it
        }
        m_toastHud->showToast(QString("Audio delay: %1ms").arg(m_audioDelayMs));
    }
    else if (action == "audio_delay_plus") {
        m_audioDelayMs += 50;
        m_sidecar->sendSetAudioDelay(m_audioDelayMs);
        if (!m_audioDeviceKey.isEmpty()) {
            QSettings s("Tankoban", "Tankoban");
            s.setValue(m_audioDeviceKey, m_audioDelayMs);
            s.setValue(m_audioDeviceKey + "/manual", true);
        }
        m_toastHud->showToast(QString("Audio delay: %1ms").arg(m_audioDelayMs));
    }
    else if (action == "audio_delay_reset") {
        m_audioDelayMs = 0;
        m_sidecar->sendSetAudioDelay(0);
        if (!m_audioDeviceKey.isEmpty()) {
            QSettings s("Tankoban", "Tankoban");
            s.setValue(m_audioDeviceKey, 0);
            s.setValue(m_audioDeviceKey + "/manual", true);
        }
        m_toastHud->showToast("Audio delay reset");
    }
    else if (action == "chapter_next") {
        if (!m_chapters.isEmpty()) {
            double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
            for (const auto& ch : m_chapters) {
                double start = ch.toObject().value("start").toDouble();
                if (start > curSec + 1.0) {
                    m_sidecar->sendSeek(start);
                    m_toastHud->showToast(ch.toObject().value("title").toString());
                    break;
                }
            }
        }
    }
    else if (action == "chapter_prev") {
        if (!m_chapters.isEmpty()) {
            double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
            for (int i = m_chapters.size() - 1; i >= 0; --i) {
                double start = m_chapters[i].toObject().value("start").toDouble();
                if (start < curSec - 2.0) {
                    m_sidecar->sendSeek(start);
                    m_toastHud->showToast(m_chapters[i].toObject().value("title").toString());
                    break;
                }
            }
        }
    }
    else if (action == "next_episode")       nextEpisode();
    else if (action == "prev_episode")       prevEpisode();
    else if (action == "stream_next_episode") emit streamNextEpisodeRequested();
    else if (action == "toggle_playlist")    togglePlaylistDrawer();
    else if (action == "show_shortcuts") {
        openKeybindingEditor();
    }
    else if (action == "back_to_library") {
        if (m_fullscreen) toggleFullscreen();
        else emit closeRequested();
    }
    else if (action == "back_fullscreen") {
        if (m_fullscreen) toggleFullscreen();
        emit closeRequested();
    }
    else if (action == "vsync_log_toggle") {
        // Phase 0 feasibility instrumentation. F12 starts logging, auto-dumps
        // after 60 seconds. File path matches _player_debug.txt convention.
        // Then: python tools/analyze_vsync.py _vsync_timing.csv
        const QString dumpPath = QStringLiteral(
            "C:/Users/Suprabha/Desktop/Tankoban 2/_vsync_timing.csv");
        debugLog(QString("[VideoPlayer] vsync_log_toggle pressed (path: %1)").arg(dumpPath));

        if (m_canvas->vsyncLoggingEnabled()) {
            // Already running — early dump
            m_canvas->setVsyncLogging(false, dumpPath);
            int n = m_canvas->vsyncSampleCount();
            m_toastHud->showToast(QString("Vsync log → _vsync_timing.csv (n=%1)").arg(n));
            return;
        }

        m_canvas->setVsyncLogging(true, dumpPath);
        m_toastHud->showToast("Vsync timing log: recording 60s...");

        // Auto-dump after 60s — fire-and-forget single-shot timer.
        QTimer::singleShot(60000, this, [this, dumpPath]() {
            if (!m_canvas->vsyncLoggingEnabled()) return;  // already stopped manually
            m_canvas->setVsyncLogging(false, dumpPath);
            int n = m_canvas->vsyncSampleCount();
            debugLog(QString("[VideoPlayer] vsync auto-dump n=%1").arg(n));
            m_toastHud->showToast(QString("Vsync log → _vsync_timing.csv (n=%1)").arg(n));
        });
    }
}

void VideoPlayer::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    setFocus(Qt::OtherFocusReason);

    // VIDEO_PLAYER_FIX Batch 3.1 — apply persisted always-on-top once the
    // top-level window exists + is visible. Guarded so we only apply on
    // the first show (avoids repeated setWindowFlag+show cycles on
    // in-app transitions like fullscreen toggle).
    static bool applied = false;
    if (!applied && m_alwaysOnTop) {
        applied = true;
        if (QWidget* top = window()) {
            top->setWindowFlag(Qt::WindowStaysOnTopHint, true);
            top->show();
        }
    } else if (!applied) {
        applied = true;  // no-op flip still consumed
    }
}

void VideoPlayer::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);

    // VIDEO_PLAYER_FIX Batch 3.3 — mini-PiP window drag. Frameless window
    // has no OS-provided title bar drag, so we record the press origin
    // and move the top-level on mouseMoveEvent. Drag starts on any left-
    // button press while m_inPip.
    if (m_inPip && event->button() == Qt::LeftButton) {
        m_pipDragOrigin = event->globalPosition().toPoint()
                          - window()->frameGeometry().topLeft();
        event->accept();
        return;
    }

    // Close any open popover/drawer when clicking outside of them.
    // PLAYER_UX_FIX Phase 6.4 — unified dismiss: adds EQ to the list
    // (previously only dismissed via EqualizerPopover's internal event
    // filter), and syncs the corresponding chip's :checked state to
    // false so the visual open-state clears too.
    bool closedSomething = false;
    if (m_filterPopover && m_filterPopover->isOpen() &&
        !m_filterPopover->geometry().contains(event->pos()) &&
        !m_filtersChip->geometry().contains(event->pos())) {
        m_filterPopover->hide();
        if (m_filtersChip) m_filtersChip->setChecked(false);
        closedSomething = true;
    }
    if (m_eqPopover && m_eqPopover->isOpen() &&
        !m_eqPopover->geometry().contains(event->pos()) &&
        !m_eqChip->geometry().contains(event->pos())) {
        m_eqPopover->hide();
        if (m_eqChip) m_eqChip->setChecked(false);
        closedSomething = true;
    }
    if (m_trackPopover && m_trackPopover->isOpen() &&
        !m_trackPopover->geometry().contains(event->pos()) &&
        !m_trackChip->geometry().contains(event->pos())) {
        m_trackPopover->hide();
        if (m_trackChip) m_trackChip->setChecked(false);
        closedSomething = true;
    }
    if (m_playlistDrawer && m_playlistDrawer->isOpen() &&
        !m_playlistDrawer->geometry().contains(event->pos()) &&
        !m_playlistChip->geometry().contains(event->pos())) {
        m_playlistDrawer->hide();
        if (m_playlistChip) m_playlistChip->setChecked(false);
        closedSomething = true;
    }

    if (!closedSomething)
        QWidget::mousePressEvent(event);
}

void VideoPlayer::mouseMoveEvent(QMouseEvent* event)
{
    // VIDEO_PLAYER_FIX Batch 3.3 — drive the PiP drag. Only on buttons-
    // held; bare mouse moves fall through to the normal HUD-reveal path.
    if (m_inPip && (event->buttons() & Qt::LeftButton)
        && m_pipDragOrigin != QPoint(-1, -1)) {
        window()->move(event->globalPosition().toPoint() - m_pipDragOrigin);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
    // Unhide cursor + arm cursor auto-hide for any mouse motion. HUD
    // reveal is gated to the bottom-edge zone (same zone the canvas-path
    // mouseActivityAt lambda uses) so a wiggle in the middle of the
    // frame doesn't flash the control bar.
    setCursor(Qt::ArrowCursor);
    m_cursorTimer.start();
    constexpr int kBottomRevealZonePx = 120;
    if (event->position().y() >= height() - kBottomRevealZonePx)
        showControls();
}

void VideoPlayer::mouseDoubleClickEvent(QMouseEvent* event)
{
    QWidget::mouseDoubleClickEvent(event);
    // VIDEO_PLAYER_FIX Batch 3.3 — fullscreen + PiP don't compose; suppress
    // the double-click-to-fullscreen gesture while in PiP.
    if (m_inPip) return;
    toggleFullscreen();
}

void VideoPlayer::wheelEvent(QWheelEvent* event)
{
    int delta = event->angleDelta().y() > 0 ? 5 : -5;
    adjustVolume(delta);
    event->accept();
}

// ── VIDEO_PLAYER_FIX Batch 4.3 — drag-drop open + enqueue ──────────────────

void VideoPlayer::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime) return;
    // Accept local + remote URL drops (Explorer / Finder / browsers all
    // emit text/uri-list). Also accept plain text when it parses as a
    // URL (browser address-bar drag).
    if (mime->hasUrls()
        || (mime->hasText() && player_utils::looksLikeUrl(mime->text()))) {
        event->acceptProposedAction();
    }
}

void VideoPlayer::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime) return;

    // Classify: video files, subtitle files, URL text.
    QStringList videos;
    QStringList subs;
    QStringList urls;  // remote (http/rtsp/rtmp); local file:// is treated
                       // as a path and routed through the video/sub branches.

    if (mime->hasUrls()) {
        for (const QUrl& u : mime->urls()) {
            if (u.isLocalFile()) {
                const QString p = u.toLocalFile();
                if (player_utils::isSubtitleFile(p)) subs.append(p);
                else                                  videos.append(p);
            } else if (player_utils::looksLikeUrl(u.toString())) {
                urls.append(u.toString());
            }
        }
    } else if (mime->hasText()) {
        const QString t = mime->text().trimmed();
        if (player_utils::looksLikeUrl(t))
            urls.append(t);
    }

    const bool active = !m_currentFile.isEmpty();

    // Subtitles take the fast path — if playback is active, load each one;
    // otherwise toast and drop (subs need a video to attach to).
    if (!subs.isEmpty()) {
        if (!active) {
            m_toastHud->showToast("Start a video first to load subtitles");
        } else {
            for (const QString& p : subs)
                m_sidecar->sendSetSubtitleUrl(QUrl::fromLocalFile(p), 0, 0);
            if (subs.size() == 1)
                m_toastHud->showToast("Loaded subtitle: " + QFileInfo(subs.first()).fileName());
            else
                m_toastHud->showToast(QString("Loaded %1 subtitles").arg(subs.size()));
        }
    }

    // Remote URLs: treat as Open-URL intent. Opens the first URL; any
    // additional ones queue (mirrors the multi-video branch below).
    if (!urls.isEmpty()) {
        if (!active) {
            openFile(urls.first());
            for (int i = 1; i < urls.size(); ++i) appendToQueue(urls.at(i));
            if (urls.size() > 1)
                m_toastHud->showToast(QString("Queued %1 URLs").arg(urls.size() - 1));
        } else {
            for (const QString& u : urls) appendToQueue(u);
            m_toastHud->showToast(urls.size() == 1
                ? "Added to queue: " + urls.first()
                : QString("Queued %1 URLs").arg(urls.size()));
        }
        event->acceptProposedAction();
        return;
    }

    // Video files.
    if (videos.isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    if (!active) {
        // Open the first; any extras build the initial playlist.
        openFile(videos.first(), videos, 0);
        if (videos.size() > 1)
            m_toastHud->showToast(QString("Queued %1 files").arg(videos.size() - 1));
    } else {
        for (const QString& p : videos) appendToQueue(p);
        m_toastHud->showToast(videos.size() == 1
            ? "Added to queue: " + QFileInfo(videos.first()).fileName()
            : QString("Queued %1 files").arg(videos.size()));
    }

    event->acceptProposedAction();
}

void VideoPlayer::contextMenuEvent(QContextMenuEvent* e)
{
    VideoContextData data;
    data.paused = m_paused;
    data.muted = m_muted;
    data.currentSpeed = SPEED_PRESETS[m_speedIdx];
    data.audioTracks = m_audioTracks;
    data.subtitleTracks = m_subTracks;
    data.currentAudioId = m_activeAudioId.toInt();
    data.currentSubId = m_activeSubId.toInt();
    data.subsVisible = m_subsVisible;
    data.deinterlace = m_filterPopover->deinterlace();
    data.normalize = m_filterPopover->normalize();
    data.alwaysOnTop   = m_alwaysOnTop;
    data.inPip         = m_inPip;
    data.showStats     = m_showStats;
    data.currentAspect = m_currentAspect;
    data.currentCrop   = m_currentCrop;
    // VIDEO_PLAYER_FIX Batch 4.2 — fresh QSettings read each menu open.
    // Cheap (small list), avoids cache invalidation complexity.
    data.recentFiles = QSettings("Tankoban", "Tankoban")
                           .value("player/recentFiles").toStringList();

    auto* menu = VideoContextMenu::build(data, this,
        [this](VideoContextMenu::ActionType t, QVariant v) {
        switch (t) {
        case VideoContextMenu::TogglePlayPause: togglePause(); break;
        case VideoContextMenu::ToggleMute:      toggleMute();  break;
        case VideoContextMenu::SetSpeed: {
            double s = v.toDouble();
            m_sidecar->sendSetRate(s);
            // Find nearest preset index
            for (int i = 0; i < SPEED_COUNT; ++i) {
                if (qAbs(SPEED_PRESETS[i] - s) < 0.01) {
                    m_speedIdx = i;
                    break;
                }
            }
            m_speedChip->setText(SPEED_LABELS[m_speedIdx]);
            m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
            break;
        }
        case VideoContextMenu::SetAspectRatio: {
            // The Aspect Ratio submenu was a silent no-op pre-Phase-7
            // (case was missing from this switch entirely). Now wired to
            // FrameCanvas::setForcedAspectRatio. "original" → 0 (use natural
            // frame aspect from m_frameW/m_frameH). Mapping lives in
            // aspectStringToDouble so the openFile restore path stays in
            // sync with the menu's write path.
            const QString val = v.toString();
            m_canvas->setForcedAspectRatio(aspectStringToDouble(val));
            m_currentAspect = val;
            // Write to the per-show record so sibling episodes in the same
            // folder inherit the user's aspect choice on next open.
            saveShowPrefs();
            m_toastHud->showToast(QString("Aspect: %1").arg(val));
            break;
        }
        case VideoContextMenu::SetCrop: {
            // Crop-to-aspect: zoom the video viewport uniformly so baked
            // letterbox / pillarbox strips get clipped by the render
            // target. Orthogonal to Aspect Ratio — user might run
            // Aspect=Original + Crop=2.35:1 on a 1920x1080 container with
            // baked 2.35 content. Mapping mirrors aspectStringToDouble's
            // convention; "none" → 0.0 = no crop.
            const QString val = v.toString();
            m_canvas->setCropAspect(cropStringToDouble(val));
            m_currentCrop = val;
            saveShowPrefs();
            m_toastHud->showToast(QString("Crop: %1").arg(val));
            break;
        }
        case VideoContextMenu::ToggleFullscreen: toggleFullscreen(); break;
        case VideoContextMenu::ToggleAlwaysOnTop: toggleAlwaysOnTop(); break;
        case VideoContextMenu::TakeSnapshot:     takeSnapshot();      break;
        case VideoContextMenu::TogglePip:        togglePictureInPicture(); break;
        case VideoContextMenu::OpenUrl:          showOpenUrlDialog();     break;
        case VideoContextMenu::OpenRecent: {
            const QString path = v.toString();
            if (path.isEmpty()) break;
            // URLs always openable (no cheap existence probe); local
            // files: check on disk, prune + toast if missing.
            const bool isRemote = player_utils::looksLikeUrl(path)
                && !path.startsWith("file://", Qt::CaseInsensitive);
            if (isRemote || QFileInfo(path).exists()) {
                openFile(path);
            } else {
                QSettings s("Tankoban", "Tankoban");
                QStringList recent = s.value("player/recentFiles").toStringList();
                recent.removeAll(path);
                s.setValue("player/recentFiles", recent);
                m_toastHud->showToast("File no longer exists — removed from recent list");
            }
            break;
        }
        case VideoContextMenu::OpenKeybindings: openKeybindingEditor(); break;
        case VideoContextMenu::ToggleStats:     toggleStatsBadge();      break;
        case VideoContextMenu::ClearRecent: {
            QSettings("Tankoban", "Tankoban").remove("player/recentFiles");
            m_toastHud->showToast("Recent list cleared");
            break;
        }
        case VideoContextMenu::SetAudioTrack:
            m_sidecar->sendSetTracks(QString::number(v.toInt()), "");
            m_toastHud->showToast("Audio: track " + QString::number(v.toInt()));
            break;
        case VideoContextMenu::SetSubtitleTrack:
            // Player Polish Batch 5.2 fix (2026-04-15): mirror the
            // visibility-on-track-switch logic from cycleSubtitleTrack
            // (around VideoPlayer.cpp:1162). Pre-fix path only sent
            // sendSetTracks with the new sub id but never adjusted
            // m_subsVisible / sendSetSubVisibility, so if the user had
            // previously toggled subtitles off the new track would land
            // hidden — appearing to the user as "context menu doesn't
            // change subtitles." Now: picking a real track auto-enables
            // visibility; picking "off" auto-disables it. Both paths
            // keep m_subsVisible coherent with sidecar state.
            if (v.toInt() == -1) {
                // VIDEO_PLAYER_FIX Batch 1.2 — unified Off path via
                // setSubtitleOff() helper. Sentinel is -1 (not 0) so a
                // subtitle stream with AVStream index 0 is a real track,
                // not Off. Never sends sendSetTracks("", "off") — that
                // payload crashes the sidecar via std::stoi("off") at
                // main.cpp:850 (no try/catch in command-dispatch loop).
                setSubtitleOff();
            } else {
                if (!m_subsVisible) {
                    m_subsVisible = true;
                    m_sidecar->sendSetSubVisibility(true);
                }
                m_sidecar->sendSetTracks("", QString::number(v.toInt()));
                m_toastHud->showToast("Subtitle: track " + QString::number(v.toInt()));
            }
            break;
        case VideoContextMenu::LoadExternalSub: {
            QString p = QFileDialog::getOpenFileName(this, "Load Subtitle", "",
                "Subtitles (*.srt *.ass *.ssa *.sub *.vtt)");
            if (!p.isEmpty())
                m_sidecar->sendLoadExternalSub(p);
            break;
        }
        case VideoContextMenu::OpenSubtitleMenu:
            m_subMenu->toggle(m_trackChip);
            break;
        case VideoContextMenu::ToggleDeinterlace:
            m_filterPopover->setDeinterlace(!m_filterPopover->deinterlace());
            m_sidecar->sendSetFilters(m_filterPopover->deinterlace(), 0, 100, 100,
                                      m_filterPopover->normalize(), m_filterPopover->interpolate(),
                                      m_filterPopover->deinterlaceFilter());
            break;
        case VideoContextMenu::ToggleNormalize:
            m_filterPopover->setNormalize(!m_filterPopover->normalize());
            m_sidecar->sendSetFilters(m_filterPopover->deinterlace(), 0, 100, 100,
                                      m_filterPopover->normalize(), m_filterPopover->interpolate(),
                                      m_filterPopover->deinterlaceFilter());
            break;
        case VideoContextMenu::OpenTracks:   m_trackPopover->toggle(m_trackChip);   break;
        case VideoContextMenu::OpenPlaylist: m_playlistDrawer->toggle();             break;
        case VideoContextMenu::BackToLibrary: emit closeRequested();                 break;
        default: break;
        }
    });
    menu->exec(e->globalPos());
    menu->deleteLater();
}

#ifdef Q_OS_WIN
#include <windows.h>
bool VideoPlayer::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_APPCOMMAND) {
            int cmd = GET_APPCOMMAND_LPARAM(msg->lParam);
            switch (cmd) {
            case APPCOMMAND_MEDIA_PLAY_PAUSE: togglePause(); *result = 1; return true;
            case APPCOMMAND_MEDIA_PLAY:       if (m_paused) togglePause(); *result = 1; return true;
            case APPCOMMAND_MEDIA_PAUSE:      if (!m_paused) togglePause(); *result = 1; return true;
            case APPCOMMAND_MEDIA_STOP:       stopPlayback(); emit closeRequested(); *result = 1; return true;
            case APPCOMMAND_MEDIA_NEXTTRACK:  nextEpisode(); *result = 1; return true;
            case APPCOMMAND_MEDIA_PREVIOUSTRACK: prevEpisode(); *result = 1; return true;
            case APPCOMMAND_VOLUME_UP:        adjustVolume(5); *result = 1; return true;
            case APPCOMMAND_VOLUME_DOWN:      adjustVolume(-5); *result = 1; return true;
            case APPCOMMAND_VOLUME_MUTE:      toggleMute(); *result = 1; return true;
            default: break;
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#endif
