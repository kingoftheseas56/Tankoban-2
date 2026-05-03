#include "ui/player/VideoPlayer.h"
#include "ui/player/KeyBindings.h"
#include "ui/player/BackendFactory.h"
#include "ui/player/SidecarProcess.h"
#ifdef HAS_LIBMPV
#include "ui/player/MpvBackend.h"
// MAKE_MPV_BEAT_FFMPEG Task 2 (2026-05-02) — MpvVulkanWidget (QOpenGLWidget,
// OpenGL render path) replaced by MpvVulkanWidget (native HWND, Vulkan +
// libplacebo). Old header kept ifdef-around-able for emergency revert
// during Task 2 smoke; deleted entirely in Task 9 cleanup.
#include "ui/player/MpvVulkanWidget.h"
#endif
#include "ui/player/ShmFrameReader.h"
#include "ui/player/FrameCanvas.h"
#include "ui/player/VolumeHud.h"
#include "ui/player/CenterFlash.h"
#include "ui/player/LoadingOverlay.h"
#include "ui/player/KeybindingEditor.h"
#include "ui/player/StatsBadge.h"
#include "ui/player/PlaylistDrawer.h"
#include "ui/player/ToastHud.h"
#include "ui/player/SubtitleOverlay.h"
#include "ui/player/SubtitlePopover.h"
#include "ui/player/AudioPopover.h"
#include "ui/player/SettingsPopover.h"
#include "ui/player/BrightnessPopover.h"
#include "ui/player/AudioDeviceWatcher.h"
#include "ui/player/OpenUrlDialog.h"
#include "ui/player/PlayerUtils.h"
#include "ui/player/SeekSlider.h"
#include "ui/player/VideoContextMenu.h"
#include "core/CoreBridge.h"
#include "core/DebugLogBuffer.h"

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
    // REPO_HYGIENE P1.2 (2026-04-26): routed through DebugLogBuffer instead
    // of writing to hardcoded C:/Users/Suprabha/.../_player_debug.txt.
    DebugLogBuffer::instance().info("video-player", msg);
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

// STREAM_STALL_RECOVERY_UX investigation 2026-04-22 — Direction C instrumentation.
// Transition-only logging of setStreamStalled + first-per-piece setStreamStallInfo
// so we can cross-check against LoadingOverlay's [STALL_DEBUG] trail. Writes
// directly to _player_debug.txt (qDebug doesn't land there on Windows GUI
// binaries — matches FrameCanvas.cpp:876-890 pattern).
namespace {
void logStallPlayerDbg(const QString& line)
{
    // REPO_HYGIENE P1.2 (2026-04-26): routed through DebugLogBuffer.
    DebugLogBuffer::instance().info("video-player-stall", line);
}
}  // namespace

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
    // MPV_RENDER_API_INTEGRATION P7 2026-04-30 — backend selection via
    // persisted preference + dev-only TANKOBAN_FORCE_MPV env var (now
    // wrapped inside BackendFactory).
    // Apply-on-next-launch semantics: VideoPlayer is constructed once per
    // Tankoban session, so right-click preference changes via VideosPage
    // take effect on next launch (per AskUserQuestion 2026-04-30
    // ratification — keeps current playback unchanged, no mid-session churn).
    m_currentBackendType = BackendFactory::chooseFor();
    m_backend = BackendFactory::create(m_currentBackendType, this);
    m_reader  = new ShmFrameReader();

    buildUI();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(3000);
    connect(&m_hideTimer, &QTimer::timeout, this, &VideoPlayer::hideControls);

    m_seekThrottle.setSingleShot(true);
    m_seekThrottle.setInterval(250);
    connect(&m_seekThrottle, &QTimer::timeout, this, [this]() {
        if (m_durationSec > 0 && m_seeking)
            m_backend->sendSeek(m_pendingSeekVal / 10000.0 * m_durationSec);
        // Phase 2 Batch 2.3 pre-fire is NOT invoked from the mid-drag
        // throttle path on purpose: while the user is still dragging,
        // the target hasn't been committed and the drag may pass over
        // unbuffered regions en route to a buffered final target. Pre-
        // firing from here would leave a stale overlay if the release
        // target turns out to be buffered. sliderReleased owns the
        // commit-point pre-fire instead.
    });

    // VIDEO_PLAYER_FIX Batch 3.1 — restore persisted always-on-top state.
    // Just read the bool here; actual WindowStaysOnTopHint application
    // defers to the first showEvent because window() isn't guaranteed to
    // have a native handle yet, and setWindowFlag + show() during parent
    // construction races the shell's own initial show sequence.
    m_alwaysOnTop = QSettings("Tankoban", "Tankoban")
        .value("player/alwaysOnTop", false).toBool();

    // MAKE_MPV_SOLO Task 8.B (2026-05-02) — Windows audio device-change
    // watcher. Closes the deferred per-device-delay-recall deliverable
    // from Task 8 (#3 of 3 named items). Listens for IMMNotificationClient
    // OnDefaultDeviceChanged events and routes through onAudioDeviceChanged
    // to apply the saved per-device delay mid-playback. No Hemanth-level
    // semantics on non-Windows builds (Impl is a stub there) — Windows-
    // only feature by design.
    m_audioDeviceWatcher = new AudioDeviceWatcher(this);
    connect(m_audioDeviceWatcher, &AudioDeviceWatcher::defaultDeviceChanged,
            this, &VideoPlayer::onAudioDeviceChanged);

    // VIDEO_PLAYER_FIX Batch 7.1 — restore stats badge toggle. Applied
    // lazily on the first firstFrame event (no source metadata before
    // that, so showing the badge earlier would render empty).
    m_showStats = QSettings("Tankoban", "Tankoban")
        .value("player/showStats", false).toBool();

    // 2026-04-30 — non-backend timers stay in the ctor; backend signals
    // (ready, firstFrame, timeUpdate, etc.) all live in wireBackendSignals
    // so a mid-session backend swap (switchBackendTo) can rewire them.
    m_sidecarRestartTimer.setSingleShot(true);
    connect(&m_sidecarRestartTimer, &QTimer::timeout, this, &VideoPlayer::restartSidecar);

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.2 — 30s first-frame watchdog.
    // Single-shot; armed on openFile entry, cancelled on firstFrame or
    // teardownUi. Fires setStage(TakingLonger) when first-frame doesn't
    // arrive within 30s.
    m_firstFrameWatchdog.setSingleShot(true);
    connect(&m_firstFrameWatchdog, &QTimer::timeout, this, [this]() {
        if (m_loadingOverlay) {
            m_loadingOverlay->setStage(LoadingOverlay::Stage::TakingLonger);
        }
    });

    wireBackendSignals();
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
                            double startPositionSec,
                            const QString& displayTitle,
                            std::optional<BackendFactory::Type> explicitBackend)
{
    debugLog("[VideoPlayer] openFile: " + filePath);

    // 2026-04-30 Phase 1.A — per-open backend reselection.
    // MAKE_MPV_SOLO Task 2 (2026-05-01) — §Q4 stream-mode lock removed; streams
    // now honor the saved preference and explicit override the same way library
    // files do. chooseFor honors the precedence:
    //   1. TANKOBAN_FORCE_MPV=1 dev override
    //   2. explicitBackend (right-click "Play with X" override)
    //   3. saved QSettings preference
    // Same-backend is a no-op fast path inside switchBackendTo.
    const auto wantBackend = BackendFactory::chooseFor(explicitBackend);
    if (wantBackend != m_currentBackendType) {
        switchBackendTo(wantBackend);
    }

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
    // Title for the bottom HUD label. For URL sources (Stream mode)
    // QFileInfo on e.g. "http://127.0.0.1:PORT/stream/{hash}/{idx}"
    // yields just the trailing path segment (the file index — "0", "3",
    // etc.), which is useless as a title. Callers pass displayTitle to
    // override with a human-readable name (StreamPage routes
    // StreamPlayerController::currentFileName() through here).
    // VIDEO_PLAYER_UI_POLISH Phase 2 2026-04-22: run the chosen title
    // through player_utils::episodeLabel so raw release filenames (e.g.
    // "[DB]Saiki Kusuo no Psi-nan_-_04_(Dual Audio_10bit_BD1080p_x265)")
    // render as "Saiki Kusuo no Psi-nan · Episode 4" in the HUD. Closes
    // audit finding #3 ("raw release filenames leak into playback UI").
    const QString rawForLabel = !displayTitle.isEmpty() ? displayTitle : filePath;
    m_fullTitle = player_utils::episodeLabel(rawForLabel);
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

    if (m_backend->isRunning()) {
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
        m_backend->sendStopWithCallback(
            [this, file, start]() {
                debugLog("[VideoPlayer] stop_ack received, sending open: " + file);
                sendCanvasSizeToSidecar();
                m_backend->sendOpen(file, start);
            },
            [this]() {
                debugLog("[VideoPlayer] stop_ack timeout — resetting sidecar; onSidecarReady will reopen m_pendingFile");
                m_backend->resetAndRestart();
                // onSidecarReady will fire sendOpen(m_pendingFile,
                // m_pendingStartSec) once the fresh sidecar is up.
            }
        );
    } else {
        debugLog("[VideoPlayer] starting sidecar...");
        m_backend->start();
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
    // VIDEO_HUD_MINIMALIST 2026-04-25 — three icon-only chips replace
    // the prior {Filters, EQ, Tracks} cluster.
    if (m_subtitlePopover && m_subtitlePopover != keep && m_subtitlePopover->isOpen()) {
        m_subtitlePopover->hide();
        if (m_subtitleChip) m_subtitleChip->setChecked(false);
    }
    if (m_audioPopover && m_audioPopover != keep && m_audioPopover->isOpen()) {
        m_audioPopover->hide();
        if (m_audioChip) m_audioChip->setChecked(false);
    }
    if (m_settingsPopover && m_settingsPopover != keep && m_settingsPopover->isOpen()) {
        m_settingsPopover->hide();
        if (m_settingsChip) m_settingsChip->setChecked(false);
    }
    // MAKE_MPV_SOLO Task 9 (2026-05-01) — brightness chip parity with the
    // other minimalist HUD chips so cross-chip exclusion + ESC dismiss
    // reach it too.
    if (m_brightnessPopover && m_brightnessPopover != keep && m_brightnessPopover->isOpen()) {
        m_brightnessPopover->hide();
        if (m_brightnessChip) m_brightnessChip->setChecked(false);
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
    if (m_subtitleChip)   m_subtitleChip->setEnabled(enable);
    if (m_audioChip)      m_audioChip->setEnabled(enable);
    if (m_brightnessChip) m_brightnessChip->setEnabled(enable);  // Task 9
    if (m_settingsChip)   m_settingsChip->setEnabled(enable);
    if (m_playlistChip)   m_playlistChip->setEnabled(enable);
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
    // REPO_HYGIENE Phase 3 (2026-04-26) — reset firstFrame flag for the
    // next open. Set true in the firstFrame slot (lambda below).
    m_firstFrameSeen = false;

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
    // doesn't inherit a stale addon track list. VIDEO_HUD_MINIMALIST
    // 2026-04-25 routes through the merged SubtitlePopover.
    if (m_subtitlePopover) m_subtitlePopover->setExternalTracks({}, {});

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
    // STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — clear the
    // estimate flag so the next session's HUD starts clean. probeDone
    // from the next openFile will repopulate based on its own probe.
    m_durationIsEstimate = false;
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
    if (m_statsBadge)  m_statsBadge->hide();
    // VIDEO_HUD_MINIMALIST 2026-04-25 — dismiss any open chip popovers
    // so their next open shows fresh content. New three-chip cluster.
    if (m_subtitlePopover   && m_subtitlePopover->isOpen())   m_subtitlePopover->hide();
    if (m_audioPopover      && m_audioPopover->isOpen())      m_audioPopover->hide();
    if (m_settingsPopover   && m_settingsPopover->isOpen())   m_settingsPopover->hide();
    if (m_brightnessPopover && m_brightnessPopover->isOpen()) m_brightnessPopover->hide();  // Task 9
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

    if (m_backend->isRunning()) {
        m_backend->sendStop();
        // Give sidecar a moment to stop audio, then shut it down.
        // Note: this stop+shutdown sequence is NOT race-safe for file-
        // switch (Agent 7 audit P0-2). For file-switch, openFile uses
        // the new sendStopWithCallback fence instead, which waits for
        // stop_ack before the follow-on sendOpen. User-close tolerates
        // the race because the process is going away — any in-flight
        // events from the torn-down session are moot.
        m_backend->sendShutdown();
        // CLOSE_AUDIO_CONTINUES_FIX 2026-04-26 — backstop the fire-and-forget
        // sendShutdown above so the user-close path actually guarantees the
        // sidecar (and its audio) is dead before stopPlayback returns. Pre-fix
        // bug: when the dispatcher was busy or PortAudio had buffered audio
        // mid-write, the process stayed alive and audio kept playing until
        // app exit (when ~SidecarProcess hit its existing wait+kill backstop).
        // 500ms covers the typical ~50-100ms graceful-exit window with
        // headroom; force-kill on timeout. Synchronous block on the GUI
        // thread is acceptable here — close-button latency budget tolerates
        // half a second.
        m_backend->ensureTerminated(500);
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
    if (!m_subtitlePopover) return;
    m_subtitlePopover->setExternalTracks(tracks, originByTrackKey);
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
    // STREAM_STALL_UX_FIX Batch 1 — session bookend also clears the stall
    // flag so a stale "true" from the prior stream doesn't gate the first
    // few HUD ticks of the next one before statsSnapshot pushes reality.
    m_streamStalled = false;
    // Batch 2 — release any overlay ownership held at session boundary.
    // Not calling dismiss here: sidecar playerIdle / firstFrame dismiss
    // chains already own overlay lifecycle during teardown, and
    // setStreamMode(true) fires BEFORE the next openFile's showLoading,
    // so lingering state from a prior session would otherwise mask the
    // fresh cold-open cascade. clearStallDiagnostic keeps the cached
    // piece/peer fields from bleeding into the next stall's text.
    m_streamStallOverlayOwner = false;
    if (m_loadingOverlay) m_loadingOverlay->clearStallDiagnostic();
}

// STREAM_STALL_UX_FIX Batch 1 — cache the stall flag so onTimeUpdate can
// suppress positionSec writes while the stream engine has a piece-wait
// watchdog firing. Pushed from StreamPage's progressUpdated lambda at
// ~1 Hz; worst-case HUD-freeze latency from real-stall-onset is one
// stall-watchdog tick (2s) + one progressUpdated tick (1s) + one sidecar
// timeUpdate (1s) ~ 4s. Acceptable vs the current 13-32s of mis-ticking
// observed in the 2026-04-21 Invincible smoke. Flip back to false also
// re-opens the HUD naturally on recovery.
//
// Batch 2 extension — on false→true transition, show the LoadingOverlay
// in Buffering stage so the user sees a legible "buffering" state instead
// of a silently frozen frame. On true→false transition, clear the stall
// diagnostic + dismiss the overlay IFF we own it (the sidecar's HTTP
// stall tracking on m_sidecarBuffering prevents premature dismiss when
// it independently wants the overlay visible — that path dismisses via
// bufferingEnded). showBuffering is idempotent so collisions with other
// sources (cold-open cascade, Agent 3 Batch 2.3 seek pre-fire, sidecar
// bufferingStarted) are safe — all mutate Stage::Buffering in place.
void VideoPlayer::setStreamStalled(bool stalled)
{
    if (stalled == m_streamStalled) return;  // transition-only dedup
    const bool wasStalled = m_streamStalled;
    m_streamStalled = stalled;

    logStallPlayerDbg(QString("setStreamStalled transition stalled=%1 wasStalled=%2 have_overlay=%3 overlay_owner=%4 sidecar_buffering=%5")
                          .arg(stalled).arg(wasStalled)
                          .arg(m_loadingOverlay != nullptr)
                          .arg(m_streamStallOverlayOwner)
                          .arg(m_sidecarBuffering));

    if (!m_loadingOverlay) return;

    if (!wasStalled && stalled) {
        m_streamStallOverlayOwner = true;
        m_loadingOverlay->showBuffering();
    } else if (wasStalled && !stalled) {
        m_loadingOverlay->clearStallDiagnostic();
        if (m_streamStallOverlayOwner) {
            m_streamStallOverlayOwner = false;
            // Only dismiss when the sidecar isn't also in an HTTP-stall
            // state — if it is, its bufferingEnded signal owns the dismiss.
            if (!m_sidecarBuffering) {
                m_loadingOverlay->dismiss();
            }
        }
    }
}

// STREAM_STALL_UX_FIX Batch 2 — enrichment pushed from StreamPage on the
// same progressUpdated tick as setStreamStalled, only while stalled is
// true. Forwards piece index + peer-have count to LoadingOverlay which
// repaints in place when showing Stage::Buffering. Safe to call before
// showBuffering has landed — LoadingOverlay caches the fields silently
// and uses them the next time paint runs.
void VideoPlayer::setStreamStallInfo(int piece, int peerHaveCount)
{
    if (!m_loadingOverlay) return;
    if (piece != m_lastLoggedStallPiece) {
        logStallPlayerDbg(QString("setStreamStallInfo piece_change piece=%1 peer_have=%2 was_piece=%3 stalled=%4")
                              .arg(piece).arg(peerHaveCount)
                              .arg(m_lastLoggedStallPiece)
                              .arg(m_streamStalled));
        m_lastLoggedStallPiece = piece;
    }
    m_loadingOverlay->setStallDiagnostic(piece, peerHaveCount);
}

// STREAM_AV_SUB_SYNC_AFTER_STALL 2026-04-21 — edge-driven sidecar IPC
// forwarder per Agent 7 audit av_sub_sync_after_stall_2026-04-21.md
// Option A + C. Called from StreamPage's connect on StreamEngine's
// stallDetected/stallRecovered Qt signals. Forwards to sidecar via
// sendStallPause/sendStallResume which freezes AVSyncClock + halts
// PortAudio writes on pause; re-anchors clock to current video PTS
// on resume (mpv paused-for-cache semantics). Only fires in stream
// mode; library playback has no stall concept. Fires ~2s after real
// stall watchdog event (vs ~4s polling latency of Batch 1's setStream
// Stalled) because this is the edge-driven path, not polling.
void VideoPlayer::onStreamStallEdgeFromEngine(bool detected)
{
    if (!m_streamMode) return;
    if (!m_backend)   return;
    if (detected) {
        m_backend->sendStallPause();
    } else {
        m_backend->sendStallResume();
    }
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
    if (!m_backend || !m_canvas || !m_backend->isRunning()) return;
    const QSize px = m_canvas->canvasPixelSize();
    if (px.width() <= 0 || px.height() <= 0) return;
    debugLog(QString("[VideoPlayer] set_canvas_size %1x%2")
                 .arg(px.width()).arg(px.height()));
    m_backend->sendSetCanvasSize(px.width(), px.height());
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
            if (m_backend) m_backend->sendLoadExternalSub(candidate);
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
        m_backend->sendSetLoopFile(true);

    // PLAYER_STREMIO_PARITY Phase 3 Batch 3.2 — push persisted seek-mode
    // pref so the first user seek of the session honors the saved choice.
    // Sidecar default is fast; only push on non-default to keep the wire
    // quiet for the common case. Pre-Phase-3 sidecar binaries return
    // NOT_IMPLEMENTED; SidecarProcess swallows that cleanly.
    {
        QSettings s("Tankoban", "Tankoban");
        const QString seekMode = s.value("Player/seekMode", "fast").toString();
        if (seekMode == "exact")
            m_backend->sendSetSeekMode(seekMode);
        // VIDEO_SUB_POSITION 2026-04-24 — push the persisted user-facing
        // baseline percent so the first file's first frame honors the
        // saved choice. Sidecar default is 100 (bottom); only push on
        // non-default to keep the wire quiet for the common case.
        const int subPos = s.value("videoPlayer/subtitlePosition", 100).toInt();
        m_subPositionPct = qBound(0, subPos, 100);
        if (m_subPositionPct != 100)
            m_backend->sendSetSubtitlePosition(m_subPositionPct);
        // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — push the persisted
        // position policy mode. Sidecar + mpv backend both default to
        // Standard; only push on Force to keep wire quiet for the common
        // case. Validates against {"standard", "force"} so a corrupt
        // QSettings value can't drive an unknown payload.
        const QString subPosMode = s.value("videoPlayer/subtitlePositionMode",
                                           "standard").toString();
        m_subPositionMode = (subPosMode == QStringLiteral("force"))
                                ? QStringLiteral("force")
                                : QStringLiteral("standard");
        if (m_subPositionMode == QStringLiteral("force"))
            m_backend->sendSetSubtitlePositionMode(m_subPositionMode);
        // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — restore subtitle size
        // scale. Sidecar + mpv backend both default to 1.0; only push on
        // non-default to keep wire quiet for the common case. Clamps
        // mirror adjustSubtitleSize so a corrupt QSettings value can't
        // drive an out-of-range payload.
        const double subSize = s.value("videoPlayer/subtitleSize", 1.0).toDouble();
        m_subtitleSize = qBound(0.5, subSize, 2.0);
        if (!qFuzzyCompare(m_subtitleSize, 1.0))
            m_backend->sendSetSubtitleSize(m_subtitleSize);
        // MAKE_MPV_SOLO Task 9 (2026-05-01) — restore brightness. Default 0
        // = neutral on both backends; only push on non-zero to keep wire
        // quiet for the common case. Clamp mirrors setBrightness so a
        // corrupt QSettings value can't drive an out-of-range payload.
        const int brightness = s.value("videoPlayer/brightness", 0).toInt();
        m_brightness = qBound(-100, brightness, 100);
        if (m_brightness != 0) {
            m_backend->sendSetFilters(false, m_brightness,
                                      /*contrast*/ 100, /*saturation*/ 100,
                                      false, false, QString());
        }
    }

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
        m_backend->sendOpen(m_pendingFile, m_pendingStartSec);
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

    // D-2 aspect-override drift reset (FC-2 option b from
    // agents/audits/vlc_aspect_crop_reference_2026-04-20.md §10.2).
    // Closes Hemanth-reported "Chainsaw Man stretches vertically on play"
    // when a stale persisted aspectOverride (from per-file, per-show, or
    // carry) differs sharply from the content's actual native aspect.
    //
    // Congress 8 reference cite (VLC vlc-master/src/):
    //   player/medialib.c:244-249  — save path: var_GetNonEmptyString(vout,
    //     "aspect-ratio") + CompareAssignState() → only persists when user
    //     changed aspect DURING playback (not on every tick).
    //   player/medialib.c:105-108  — restore path: var_SetString(vout,
    //     "aspect-ratio", input->ml.states.aspect_ratio) — per-media MRL
    //     lookup, scoped to vout lifetime.
    //   video_output/vout_intf.c:275-277 — aspect-ratio is a per-vout
    //     VLC_VAR_STRING | VLC_VAR_ISCOMMAND variable, reinitialized each
    //     new vout (each file open).
    //   libvlc-module.c:1739 — default is NULL (native aspect passthrough).
    //
    // VLC's policy: user-intent-gated persistence via medialib. Only saves
    // when user explicitly picked an aspect this session; unchanged files
    // stay at NULL (native). This prevents stale overrides from ever
    // accumulating.
    //
    // Tankoban's policy pre-FC-2: unconditional aspectOverride persistence
    // on every saveProgress tick + per-show carry. Stale "16:9" from any
    // prior interaction would re-apply to unrelated content.
    //
    // FC-2 option (b) shipped here = safety-net reset-on-drift overlay on
    // top of unconditional persistence, NOT a wholesale copy of VLC's
    // user-intent-gated save policy (which would be FC-2 option a —
    // deferred; requires touching saveProgress/saveShowPrefs save paths,
    // ~30 LOC scope, candidate for future Congress 8 discipline work).
    // > 10% ratio drift catches the 16:9-vs-2.40 + 4:3-vs-16:9 classes
    // that produce visible distortion while leaving near-match user
    // intents (2.35 vs 2.39, 16:9 vs 1.85) alone. Reset writes to BOTH
    // per-file and per-show records so subsequent opens don't re-apply
    // the stale override.
    if (w > 0 && h > 0) {
        const double persistedAspect = aspectStringToDouble(m_currentAspect);
        if (persistedAspect > 0.0) {
            const double nativeAspect = static_cast<double>(w) / static_cast<double>(h);
            const double ratioDrift = qAbs(persistedAspect - nativeAspect) / nativeAspect;
            if (ratioDrift > 0.10) {
                debugLog(QString("[VideoPlayer] D-2 aspect reset: persisted=%1 (%2) native=%3 drift=%4, reset to original")
                    .arg(m_currentAspect)
                    .arg(persistedAspect, 0, 'f', 4)
                    .arg(nativeAspect, 0, 'f', 4)
                    .arg(ratioDrift, 0, 'f', 4));
                m_currentAspect = QStringLiteral("original");
                if (m_canvas) m_canvas->setForcedAspectRatio(0.0);
                saveShowPrefs();
                debugLog(QString("[VideoPlayer] D-2 reset diagnostic: bridge=%1 mode=%2 videoId='%3'")
                    .arg(m_bridge ? "ok" : "null")
                    .arg(m_persistenceMode == PersistenceMode::LibraryVideos ? "LibraryVideos" : "None")
                    .arg(m_currentVideoId));
                if (m_bridge && m_persistenceMode == PersistenceMode::LibraryVideos
                    && !m_currentVideoId.isEmpty()) {
                    QJsonObject prog = m_bridge->progress("videos", m_currentVideoId);
                    const bool wasPresent = prog.contains("aspectOverride");
                    prog["aspectOverride"] = m_currentAspect;
                    m_bridge->saveProgress("videos", m_currentVideoId, prog);
                    debugLog(QString("[VideoPlayer] D-2 reset saveProgress: wasPresent=%1 wrote aspectOverride=%2")
                        .arg(wasPresent).arg(m_currentAspect));
                }
            }
        }
    }

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

    // Task 7 (2026-05-01) — Pattern C accumulator clear: when the real
    // position catches up to within ±1s of the pending seek target,
    // the seek has effectively completed. Subsequent arrow-key presses
    // re-base from the seekbar value (the standard path). Without this
    // clear the accumulator would persist forever and prevent seekBar-
    // based scrubs from re-syncing.
    if (m_pendingSeekTargetSec >= 0.0
        && qAbs(positionSec - m_pendingSeekTargetSec) < 1.0) {
        m_pendingSeekTargetSec = -1.0;
    }

    // STREAM_STALL_UX_FIX Batch 1 — during a stream-engine stall, the sidecar
    // keeps emitting timeUpdate from the audio PTS clock while the video
    // decoder is dry (FrameCanvas painting the last frame). The positionSec
    // reported while stalled is meaningless-ahead of what the user actually
    // sees, so pinning the seek slider + time label prevents the "screen
    // frozen but clock ticking" lie. m_lastKnownPosSec also stays pinned so
    // crash-recovery resumes from the last on-screen-accurate second rather
    // than an extrapolated future position. Duration label + m_durationSec
    // + setDurationSec still run (duration is invariant mid-playback).
    // saveProgress still fires so the ~1 Hz progressUpdated tick continues
    // to drive StreamPage's stats-pull + deadline retarget pipeline; the
    // saved position is the last-good one via the pinned m_lastKnownPosSec.
    const bool gateHud = m_streamMode && m_streamStalled;

    if (!gateHud) {
        m_lastKnownPosSec = positionSec;
    }

    m_durationSec = durationSec;
    m_seekBar->setDurationSec(durationSec);
    qint64 posMs = static_cast<qint64>(positionSec * 1000);
    qint64 durMs = static_cast<qint64>(durationSec * 1000);

    if (!gateHud) {
        m_seekBar->blockSignals(true);
        m_seekBar->setValue(durationSec > 0 ? static_cast<int>(positionSec / durationSec * 10000) : 0);
        m_seekBar->blockSignals(false);

        m_timeLabel->setText(formatTime(posMs));
    }
    // STREAM_DURATION_FIX — when the sidecar reports durationSec=0 it means
    // the probe's duration estimate was FROM_BITRATE (unreliable) and was
    // discarded in demuxer.cpp. Show "—:—" to signal unknown honestly, so
    // the HUD never displays a wildly-wrong number (1h content mis-rendered
    // as 2h was the repro that motivated this guard).
    // STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — when durationSec
    // is a bitrate x fileSize estimate (sidecar's last-resort fallback
    // rescuing pack torrents where video + audio AVStream::duration are
    // both unset), prefix the formatted time with a tilde to honestly
    // signal approximation (~10-50% VBR error). User sees "~42:00" and
    // knows the exact end is plus/minus a few minutes, preserving the
    // anti-lie UX contract while enabling interactive seek.
    // m_durationIsEstimate is cached from SidecarProcess::probeDone's
    // new payload flag; defaults false, stays false on Branch 1 (video
    // stream duration) + Branch 3 (FROM_PTS) + stream-max fallback where
    // duration is ground-truth.
    if (durationSec > 0.0) {
        const QString durText = formatTime(durMs);
        m_durLabel->setText(m_durationIsEstimate
            ? QStringLiteral("~") + durText
            : durText);
    } else {
        m_durLabel->setText(QStringLiteral("\u2014:\u2014"));
    }

    // Save progress every update (~1/sec from sidecar). During a stall we
    // save (and emit progressUpdated with) the pinned last-good position so
    // StreamPage's watch-state write + updatePlaybackWindow see reality, not
    // the extrapolated audio-clock position that's ahead of the screen.
    const double effectivePosSec = gateHud ? m_lastKnownPosSec : positionSec;
    saveProgress(effectivePosSec, durationSec);
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
        m_backend->sendSeek(0.0);
        return;
    }
    if (m_playlistDrawer && m_playlistDrawer->repeatOne()) {
        m_backend->sendSeek(0.0);
        return;
    }

    const bool havePlaylist = !m_playlist.isEmpty();
    const bool atEnd = havePlaylist && (m_playlistIdx >= m_playlist.size() - 1);

    if (havePlaylist && atEnd && m_playlistDrawer && m_playlistDrawer->repeatAll()
        && m_playlist.size() > 1) {
        // Wrap to the top of the queue.
        m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
        m_carrySubLang   = langForTrackId(m_subTracks,   m_activeSubId);
        m_carryAudioId   = m_activeAudioId;     // Task 6.B
        m_carrySubId     = m_activeSubId;       // Task 6.B
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
        m_carryAudioId   = m_activeAudioId;     // Task 6.B
        m_carrySubId     = m_activeSubId;       // Task 6.B
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
    // MAKE_MPV_SOLO Task 5 — failed open means no firstFrame ever fires,
    // so the 30 s firstFrameWatchdog (armed in openFile) would otherwise
    // re-show the LoadingOverlay in TakingLonger stage and trap the user
    // in an "endless buffering" view. Stop the watchdog AND dismiss any
    // currently-visible overlay so the toast is the only thing on screen.
    // Both calls are idempotent — safe to fire on mid-playback errors too.
    m_firstFrameWatchdog.stop();
    if (m_loadingOverlay) m_loadingOverlay->dismiss();
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
    m_backend->start();
}

// ── UI ──────────────────────────────────────────────────────────────────────

void VideoPlayer::buildUI()
{
    m_canvas = new FrameCanvas(this);

#ifdef HAS_LIBMPV
    // Create the native mpv/Vulkan surface before HUD widgets, matching
    // FrameCanvas construction order. Showing a native HWND after alien HUD
    // widgets exist can make Windows put the render surface above them.
    m_mpvWidget = new MpvVulkanWidget(this);
    m_mpvWidget->setGeometry(0, 0, width(), height());
    m_mpvWidget->hide();
    connect(m_mpvWidget, &MpvVulkanWidget::firstFrameRendered,
            this, [this]() {
                // Task 3: this now means an actual mpv frame reached the
                // Vulkan swapchain, not just the black pre-frame clear.
                m_firstFrameSeen = true;
            });
    // Agent 3 2026-05-02 — mouseActivityAt connect disabled while the
    // MpvVulkanWidget mouseMoveEvent + nativeEvent paths are off (see
    // MpvVulkanWidget.cpp). The signal cannot fire so the connect is dead;
    // kept commented for the Task 2 follow-up that re-enables a safer
    // mouse-bridge.
    // connect(m_mpvWidget, &MpvVulkanWidget::mouseActivityAt, this, [this](int /*y*/) {
    //     showControls();
    // });
#endif

    // 2026-04-30 hotfix — MpvVulkanWidget setup/teardown lives in
    // syncMpvIntegrationToBackend() so a mid-session swap into mpv (via
    // switchBackendTo + right-click "Play with mpv") wires the widget
    // correctly. The original P5-redux block here only created m_mpvWidget
    // when the CTOR-time backend was already mpv; users with default-ffmpeg
    // saved pref hit a null m_mpvWidget on swap → audio-only playback +
    // blank video canvas. The helper handles both initial-mpv and
    // swap-to-mpv paths; this call covers the initial-mpv case.
    syncMpvIntegrationToBackend();

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
        if (!m_backend || !m_backend->isRunning()) return;
        const double speed = m_syncClock.getClockVelocity();
        // Deadband to avoid spamming the sidecar with sub-audible changes.
        // 0.0005 = 0.05% ≈ one imperceptible adjustment per tick threshold.
        if (std::abs(speed - m_lastSentAudioSpeed) < 0.0005) return;
        m_backend->sendSetAudioSpeed(speed);
        m_lastSentAudioSpeed = speed;
    });
    m_audioSpeedTicker.start();

    // FrameCanvas is a native D3D11 HWND (WA_PaintOnScreen) — mouse events
    // don't bubble to VideoPlayer. Forward via signal carrying y position.
    // VIDEO_PLAYER_UI_POLISH Phase 1 2026-04-22 (audit finding #1
    // "HUD reveal feels unreliable"): prior code gated the HUD reveal on
    // `y >= height - 120` i.e. only the bottom ~12 % of the frame would
    // surface the control bar. That made the player feel hesitant — the
    // auditor had to press L to get controls up because moving the
    // pointer into the lower player area (bottom third, not bottom 120 px)
    // wasn't enough. VLC / mpv / PotPlayer all reveal on *any* mouse
    // motion over the player area and rely on the auto-hide timer to
    // keep the HUD out of the way — that's the convention we should
    // match. Y parameter is now unused at the consumer side (kept in
    // the signal for future needs like cursor-locality-aware effects).
    connect(m_canvas, &FrameCanvas::mouseActivityAt, this, [this](int /*y*/) {
        // VIDEO_CURSOR_AUTOHIDE 2026-04-24 (hemanth): cursor lifecycle is
        // bound to HUD lifecycle via showControls/hideControls on m_canvas.
        // Prior setCursor/Qt::ArrowCursor + m_cursorTimer plumbing targeted
        // VideoPlayer's Qt logical cursor scope which doesn't reach
        // FrameCanvas's WA_NativeWindow HWND — blank-cursor never landed.
        showControls();
    });
    connect(m_canvas, &FrameCanvas::canvasPixelSizeSettled, this,
        [this](int, int) {
            sendCanvasSizeToSidecar();
        });

    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName("VideoControlBar");

    // PER_VIEW_CHROME_FIX 2026-05-02 P2 — top-right floating chrome cluster.
    // Glass-look (per spec §4.1: floating-over-canvas treatment) — semi-
    // opaque dark plate so the cluster reads against any video content.
    // Visibility synced with m_controlBar via showControls / hideControls;
    // hidden in fullscreen. Click signals route through MainWindow chrome
    // slots (showMinimized / onChromeMaximizeToggle / close).
    m_chromeOverlay = new QFrame(this);
    m_chromeOverlay->setObjectName("VideoChromeOverlay");
    m_chromeOverlay->setAttribute(Qt::WA_StyledBackground, true);
    m_chromeOverlay->setStyleSheet(
        "QFrame#VideoChromeOverlay {"
        "  background: #141418;"
        "  border: 1px solid rgba(255, 255, 255, 0.10);"
        "  border-radius: 6px;"
        "}"
        "QPushButton#VideoChromeBtn,"
        "QPushButton#VideoChromeCloseBtn {"
        "  background: transparent; border: none;"
        "  border-radius: 4px; padding: 4px;"
        "}"
        "QPushButton#VideoChromeBtn:hover {"
        "  background: rgba(255, 255, 255, 0.16);"
        "}"
        "QPushButton#VideoChromeCloseBtn:hover {"
        "  background: rgba(232, 17, 35, 0.85);"
        "}"
    );
    {
        auto* chromeLay = new QHBoxLayout(m_chromeOverlay);
        chromeLay->setContentsMargins(4, 4, 4, 4);
        chromeLay->setSpacing(2);
        auto makeChromeBtn = [this](const QString& iconPath, const QString& tip,
                                    const QString& objName) {
            auto* b = new QPushButton(m_chromeOverlay);
            b->setObjectName(objName);
            b->setIcon(QIcon(iconPath));
            b->setIconSize(QSize(16, 16));
            b->setFixedSize(32, 28);
            b->setFocusPolicy(Qt::NoFocus);
            b->setCursor(Qt::ArrowCursor);
            b->setToolTip(tip);
            return b;
        };
        m_chromeMinBtn   = makeChromeBtn(":/icons/chrome_min.svg",   "Minimize",       "VideoChromeBtn");
        m_chromeMaxBtn   = makeChromeBtn(":/icons/chrome_max.svg",   "Maximize",       "VideoChromeBtn");
        m_chromeCloseBtn = makeChromeBtn(":/icons/chrome_close.svg", "Close Tankoban", "VideoChromeCloseBtn");
        chromeLay->addWidget(m_chromeMinBtn);
        chromeLay->addWidget(m_chromeMaxBtn);
        chromeLay->addWidget(m_chromeCloseBtn);
        connect(m_chromeMinBtn,   &QPushButton::clicked, this, &VideoPlayer::chromeMinimizeRequested);
        connect(m_chromeMaxBtn,   &QPushButton::clicked, this, &VideoPlayer::chromeMaximizeToggleRequested);
        connect(m_chromeCloseBtn, &QPushButton::clicked, this, &VideoPlayer::chromeCloseRequested);
    }
    m_chromeOverlay->hide();

    // SUBTITLE_VIDEO_BOTTOM_CUTOFF_FIX 2026-04-22 (hemanth-reported
    // "video cut off at bottom in fullscreen"): the prior 0.92 alpha +
    // Qt's WA_PaintOnScreen + separate-HWND FrameCanvas render stack
    // made the HUD panel render fully opaque to DWM composition —
    // video behind it was invisible, so the bottom ~120 px of video
    // appeared clipped whenever HUD was visible. Pixel analysis of
    // tb_D2_fs_paused_hudvisible.png showed RGB=(9,9,9) uniformly
    // across the HUD bg over a tan/beige video region, confirming zero
    // video show-through. Dropping alpha 0.92 -> 0.50 matches reference-
    // player convention (mpv OSC ~0.60, VLC OSD ~0.55, PotPlayer HUD
    // ~0.60). If the DWM/HWND path still ignores alpha, hideControls
    // auto-hide-on-pause (companion edit at ~line 2782) still lets the
    // full 1920x1080 video surface the moment the cursor stills.
    m_controlBar->setStyleSheet(
        "QWidget#VideoControlBar {"
        "  background: rgba(10, 10, 10, 0.50);"
        "  border-top: 1px solid rgba(255, 255, 255, 0.08);"
        "}"
    );
    applySurfaceOverlayStyle();

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
        m_backend->sendSeek(qMax(0.0, curSec - 10.0));
        m_centerFlash->flash(SVG_SEEK_BACK);
        showControls();
    });

    m_seekBar = new SeekSlider(Qt::Horizontal, m_controlBar);
    m_seekBar->setDurationSec(0.0);
    // Task 7 (2026-05-01) — Esc/keyboard fix: pre-fix the seekbar slider
    // accepted keyboard focus on click (Qt::QSlider default = StrongFocus),
    // so any user scrub stole focus from VideoPlayer; subsequent keypresses
    // (Esc, space, arrows) routed to the slider's default arrow-step
    // handler instead of VideoPlayer::keyPressEvent. Setting NoFocus keeps
    // the mouse-drag scrub functional while preventing focus theft. All
    // other interactive children (transport buttons + chips) already use
    // Qt::NoFocus per the existing setupUi pattern.
    m_seekBar->setFocusPolicy(Qt::NoFocus);
    connect(m_seekBar, &QSlider::sliderPressed, this, [this]() {
        m_seeking = true;
        m_seekDragOrigin = m_seekBar->value();
    });
    connect(m_seekBar, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        if (m_durationSec > 0) {
            const double targetSec = m_seekBar->value() / 10000.0 * m_durationSec;
            // PLAYER_STREMIO_PARITY Phase 2 Batch 2.3 — anticipatory
            // cache-pause UI on seek commit. See throttle-handler comment
            // above for rationale. Idempotent: if the drag already pre-
            // fired showBuffering via the throttle path, setStage(Buffering)
            // is a no-op same-stage call (returns without re-fade).
            if (m_loadingOverlay && !m_seekBar->isTimeBuffered(targetSec)) {
                m_loadingOverlay->showBuffering();
            }
            m_backend->sendSeek(targetSec);
        }
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
        m_backend->sendSeek(curSec + 10.0);
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
    // MAKE_MPV_SOLO Task 9 follow-up (2026-05-01) — icon mirrors STATE not
    // next-action per Hemanth verbatim "Pause symbol must show when the
    // video is paused. Play symbol must show when the video is playing."
    // Initial m_paused=false → currently playing → show play icon (▶).
    m_playPauseBtn->setIcon(m_playIcon);
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

    // VIDEO_HUD_MINIMALIST 2026-04-25 — three icon-only chips (Subtitles
    // / Audio / Settings) + List. Speed/Filters/EQ/Tracks chips removed
    // per Hemanth's "make my player more minimalistic" directive. Z/X/C
    // keys still adjust speed via toast feedback.
    m_subtitleChip = new QPushButton(m_controlBar);
    m_subtitleChip->setIcon(QIcon(":/icons/subtitles.svg"));
    m_subtitleChip->setIconSize(QSize(16, 16));
    m_subtitleChip->setToolTip("Subtitles");
    m_subtitleChip->setCursor(Qt::PointingHandCursor);
    m_subtitleChip->setFocusPolicy(Qt::NoFocus);
    m_subtitleChip->setStyleSheet(chipStyle);
    m_subtitleChip->setCheckable(true);
    connect(m_subtitleChip, &QPushButton::clicked, this, [this]() {
        dismissOtherPopovers(m_subtitlePopover);
        m_subtitlePopover->toggle(m_subtitleChip);
        m_subtitleChip->setChecked(m_subtitlePopover->isOpen());
    });

    m_audioChip = new QPushButton(m_controlBar);
    m_audioChip->setIcon(QIcon(":/icons/audio.svg"));
    m_audioChip->setIconSize(QSize(16, 16));
    m_audioChip->setToolTip("Audio");
    m_audioChip->setCursor(Qt::PointingHandCursor);
    m_audioChip->setFocusPolicy(Qt::NoFocus);
    m_audioChip->setStyleSheet(chipStyle);
    m_audioChip->setCheckable(true);
    connect(m_audioChip, &QPushButton::clicked, this, [this]() {
        dismissOtherPopovers(m_audioPopover);
        m_audioPopover->populate(m_audioTracks, m_activeAudioId.toInt());
        m_audioPopover->toggle(m_audioChip);
        m_audioChip->setChecked(m_audioPopover->isOpen());
    });

    // MAKE_MPV_SOLO Task 9 (2026-05-01) — brightness chip. Uses
    // brightness.svg (half-filled circle = universal contrast/brightness
    // glyph). Sun-shaped icons are NOT reused here — settings.svg is
    // already drawn as a sun (circle + radial lines), so a sun icon for
    // brightness would visually duplicate the settings chip in the HUD.
    // Chip sits between Audio and Settings in the bottom-HUD utility cluster.
    m_brightnessChip = new QPushButton(m_controlBar);
    m_brightnessChip->setIcon(QIcon(":/icons/brightness.svg"));
    m_brightnessChip->setIconSize(QSize(16, 16));
    m_brightnessChip->setToolTip("Brightness");
    m_brightnessChip->setCursor(Qt::PointingHandCursor);
    m_brightnessChip->setFocusPolicy(Qt::NoFocus);
    m_brightnessChip->setStyleSheet(chipStyle);
    m_brightnessChip->setCheckable(true);
    connect(m_brightnessChip, &QPushButton::clicked, this, [this]() {
        dismissOtherPopovers(m_brightnessPopover);
        if (m_brightnessPopover) m_brightnessPopover->setBrightness(m_brightness);
        if (m_brightnessPopover) m_brightnessPopover->toggle(m_brightnessChip);
        if (m_brightnessPopover) m_brightnessChip->setChecked(m_brightnessPopover->isOpen());
    });

    m_settingsChip = new QPushButton(m_controlBar);
    m_settingsChip->setIcon(QIcon(":/icons/settings.svg"));
    m_settingsChip->setIconSize(QSize(16, 16));
    m_settingsChip->setToolTip("Settings");
    m_settingsChip->setCursor(Qt::PointingHandCursor);
    m_settingsChip->setFocusPolicy(Qt::NoFocus);
    m_settingsChip->setStyleSheet(chipStyle);
    m_settingsChip->setCheckable(true);
    connect(m_settingsChip, &QPushButton::clicked, this, [this]() {
        dismissOtherPopovers(m_settingsPopover);
        m_settingsPopover->setAudioDelay(m_audioDelayMs);
        m_settingsPopover->setSubtitleDelay(m_subDelayMs);
        m_settingsPopover->setSubtitlePosition(m_subPositionPct);
        // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — sync Force-position
        // checkbox with persisted state on every popover open.
        m_settingsPopover->setSubtitlePositionMode(m_subPositionMode);
        // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — sync size value
        // label with persisted scale on every popover open.
        m_settingsPopover->setSubtitleSize(m_subtitleSize);
        m_settingsPopover->toggle(m_settingsChip);
        m_settingsChip->setChecked(m_settingsPopover->isOpen());
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
    // VIDEO_PLAYER_UI_POLISH Phase 3 2026-04-22 — audit finding #5 "the
    // title line is low-contrast and visually secondary even though it
    // carries the current item identity." Prior style was
    // rgba(255,255,255,0.55) at 11 px / 500 — reads as hint text next
    // to the chips. Raise to 0.95 alpha + 12 px + 600 so the title is
    // clearly primary text at the same weight class as the chip labels,
    // matching the bottom-bar hierarchy convention. No color palette
    // change (still off-white); respects feedback_no_color_no_emoji.
    m_titleLabel->setStyleSheet(
        "QLabel#VideoTitle {"
        "  color: rgba(245,245,245,0.95);"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding-left: 12px;"
        "  padding-right: 12px;"
        "}"
    );
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_titleLabel->setMinimumWidth(0);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // VIDEO_PLAYER_UI_POLISH Phase 3 2026-04-22 — audit finding #5 "bottom
    // control bar is visually crowded." Prior layout packed every chip
    // with uniform 4 px spacing, no visual grouping. Rebalance:
    //   [back] — [prev|play|next]  —  [title, stretch]  —  [speed]
    //     [filters|eq]  [tracks|list]
    // Wider cross-group gaps (16 px) separate transport / title / chip
    // clusters; narrow 3 px intra-group gaps keep related chips visually
    // cohesive. No chip count change, no re-order — purely spacing.
    ctrlRow->addWidget(m_backBtn);
    ctrlRow->addSpacing(16);                      // back → transport
    ctrlRow->addWidget(m_prevEpisodeBtn);
    ctrlRow->addWidget(m_playPauseBtn);
    ctrlRow->addWidget(m_nextEpisodeBtn);
    ctrlRow->addSpacing(16);                      // transport → title
    ctrlRow->addWidget(m_titleLabel, 1);          // stretch — eats leftover space
    ctrlRow->addSpacing(12);                      // title → utility cluster
    ctrlRow->addWidget(m_subtitleChip);
    ctrlRow->addSpacing(6);                       // tight intra-cluster
    ctrlRow->addWidget(m_audioChip);
    ctrlRow->addSpacing(6);
    ctrlRow->addWidget(m_brightnessChip);          // MAKE_MPV_SOLO Task 9
    ctrlRow->addSpacing(6);
    ctrlRow->addWidget(m_settingsChip);
    ctrlRow->addSpacing(12);                      // utility → list
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
    // 2026-04-30 — m_backend signal connects (buffering / cache / firstFrame /
    // probe / decoder-open / firstPacketRead / firstDecoderReceive) live in
    // wireBackendSignals() at end of file so a backend swap can rewire them.
    // The connects to `this`/m_loadingOverlay above (playerOpeningStarted /
    // playerIdle) stay inline — they don't reference m_backend.

    // VIDEO_HUD_MINIMALIST 2026-04-25 — three new popovers replace
    // {TrackPopover + SubtitleMenu + EqualizerPopover + FilterPopover}.
    // SubtitlePopover is the merged surface (embedded + addon external +
    // load-from-file). Style controls (font size / margin / position /
    // outline / color / BG opacity) dropped per Hemanth's "nothing else"
    // / "nothing fancy". Persisted style values continue to apply via
    // QSettings reads at startup; they just have no UI surface to change.
    m_subtitlePopover = new SubtitlePopover(this);
    m_subtitlePopover->setSidecar(m_backend);
    connect(m_subtitlePopover, &SubtitlePopover::embeddedSubtitleSelected,
        this, [this](int id) {
            if (id == 0) {
                // VIDEO_PLAYER_FIX Batch 1.2 — unified Off path. Routes
                // through setSubtitleOff for visibility-only semantics
                // (no set_tracks payload that would crash the sidecar).
                setSubtitleOff();
            } else {
                QString idStr = QString::number(id);
                if (!m_subsVisible) {
                    m_subsVisible = true;
                    m_backend->sendSetSubVisibility(true);
                }
                m_backend->sendSetTracks("", idStr);
                QString lang = langForTrackId(m_subTracks, idStr);
                if (!lang.isEmpty())
                    QSettings("Tankoban", "Tankoban").setValue("video_preferred_sub_lang", lang);
                m_activeSubId = idStr;
                saveShowPrefs();
                m_toastHud->showToast("Subtitle: track " + idStr);
            }
        });
    connect(m_subtitlePopover, &SubtitlePopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) { m_hideTimer.stop(); showControls(); }
        else m_hideTimer.start(3000);
    });
    // VIDEO_HUD_MINIMALIST polish 2026-04-25 (hemanth: "ensure clicking
    // icons do not darken them indefinitely") — chip :checked state now
    // mirrors popover visibility 1:1. The dismissed signal fires from
    // popover.dismiss() (single chokepoint for item-click + click-
    // outside paths); dismissOtherPopovers already manually unchecks
    // chips on its hide() path so all 3 dismiss routes are covered.
    connect(m_subtitlePopover, &SubtitlePopover::dismissed, this, [this]() {
        if (m_subtitleChip) m_subtitleChip->setChecked(false);
    });

    m_audioPopover = new AudioPopover(this);
    connect(m_audioPopover, &AudioPopover::audioTrackSelected, this, [this](int id) {
        QString idStr = QString::number(id);
        m_backend->sendSetTracks(idStr, "");
        QString lang = langForTrackId(m_audioTracks, idStr);
        if (!lang.isEmpty())
            QSettings("Tankoban", "Tankoban").setValue("video_preferred_audio_lang", lang);
        m_activeAudioId = idStr;
        saveShowPrefs();
        m_toastHud->showToast("Audio: track " + idStr);
    });
    connect(m_audioPopover, &AudioPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) { m_hideTimer.stop(); showControls(); }
        else m_hideTimer.start(3000);
    });
    connect(m_audioPopover, &AudioPopover::dismissed, this, [this]() {
        if (m_audioChip) m_audioChip->setChecked(false);
    });

    m_settingsPopover = new SettingsPopover(this);
    connect(m_settingsPopover, &SettingsPopover::audioDelayAdjusted,
        this, [this](int delta) { adjustAudioDelay(delta); });
    connect(m_settingsPopover, &SettingsPopover::subtitleDelayAdjusted,
        this, [this](int delta) { adjustSubDelay(delta); });
    connect(m_settingsPopover, &SettingsPopover::subtitlePositionAdjusted,
        this, [this](int delta) { adjustSubPosition(delta); });
    // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — Force-position toggle.
    connect(m_settingsPopover, &SettingsPopover::subtitlePositionModeChanged,
        this, [this](const QString& mode) { setSubPositionMode(mode); });
    // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — subtitle size +/-.
    connect(m_settingsPopover, &SettingsPopover::subtitleSizeAdjusted,
        this, [this](double delta) { adjustSubtitleSize(delta); });
    connect(m_settingsPopover, &SettingsPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) { m_hideTimer.stop(); showControls(); }
        else m_hideTimer.start(3000);
    });
    connect(m_settingsPopover, &SettingsPopover::dismissed, this, [this]() {
        if (m_settingsChip) m_settingsChip->setChecked(false);
    });

    // MAKE_MPV_SOLO Task 9 (2026-05-01) — brightness popover construction +
    // connects. Live-update on every slider tick (Hemanth gate: drag → see
    // change immediately). Mirrors the SettingsPopover wiring shape:
    // valueChanged → setBrightness (push + persist + sync); hoverChanged
    // gates HUD auto-hide; dismissed unchecks the chip in lockstep.
    m_brightnessPopover = new BrightnessPopover(this);
    connect(m_brightnessPopover, &BrightnessPopover::brightnessChanged,
        this, [this](int v) { setBrightness(v); });
    // MAKE_MPV_SOLO Task 9 follow-up (2026-05-01) — popover Reset button
    // mirrors the keyboard 'r' reset path: setBrightness(0) + toast.
    connect(m_brightnessPopover, &BrightnessPopover::resetClicked, this, [this]() {
        setBrightness(0);
        if (m_toastHud) m_toastHud->showToast(QStringLiteral("Brightness: 0"));
    });
    connect(m_brightnessPopover, &BrightnessPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) { m_hideTimer.stop(); showControls(); }
        else m_hideTimer.start(3000);
    });
    connect(m_brightnessPopover, &BrightnessPopover::dismissed, this, [this]() {
        if (m_brightnessChip) m_brightnessChip->setChecked(false);
    });

    // Subtitle overlay (sibling of FrameCanvas, NOT child of it — critical for QRhiWidget z-order)
    m_subOverlay = new SubtitleOverlay(this);

    // Playlist drawer (L key — right-side episode list)
    m_playlistDrawer = new PlaylistDrawer(this);
    connect(m_playlistDrawer, &PlaylistDrawer::episodeSelected, this, [this](int idx) {
        if (idx >= 0 && idx < m_playlist.size()) {
            m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
            m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
            m_carryAudioId = m_activeAudioId;     // Task 6.B
            m_carrySubId = m_activeSubId;         // Task 6.B
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
            [this](bool on) { m_backend->sendSetLoopFile(on); });
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

    // VIDEO_HUD_MINIMALIST 2026-04-25 — Equalizer + Filters popovers
    // deleted entirely. Sidecar audio/video filter pipeline defaults to
    // "no filters" so removing the UI = original audio/video qualities
    // pass straight through, satisfying Hemanth's "I wanna see it that
    // way" intent without sidecar changes. The shader-side HDR tone-
    // mapping is also no longer driven from a UI toggle; it remains
    // off by default. Pre-existing QSettings persisting filter / EQ
    // state become orphaned reads (no UI to write them); harmless.

    // 2026-04-30 — m_backend signal connects (mediaInfo / d3d11Texture /
    // overlayShm) extracted to wireBackendSignals() at end of file. The
    // m_canvas connects below (zeroCopyActivated / deviceReconnecting) stay
    // inline — they're driven by the local FrameCanvas member, not by
    // m_backend, so a backend swap doesn't affect them.

    // FrameCanvas tells us when zero-copy import succeeded/failed so we can
    // tell the sidecar to short-circuit its CPU pipeline (saves ~15ms/frame).
    connect(m_canvas, &FrameCanvas::zeroCopyActivated, this, [this](bool active) {
        debugLog(QString("[VideoPlayer] zero-copy %1").arg(active ? "ACTIVE" : "INACTIVE"));
        m_backend->sendSetZeroCopyActive(active);
    });

    // Batch 6.2 — FrameCanvas announces D3D device-lost recovery; surface
    // a brief ToastHud so the user sees why the display stuttered.
    connect(m_canvas, &FrameCanvas::deviceReconnecting, this, [this]() {
        debugLog("[VideoPlayer] D3D device-lost — FrameCanvas recovering");
        m_toastHud->showToast("Reconnecting display…");
    });

    // 2026-04-30 — m_backend frameStepped connect extracted to
    // wireBackendSignals() at end of file.

    // Time bubble (seek preview — shown above slider during drag)
    m_timeBubble = new QLabel(this);
    m_timeBubble->setStyleSheet(
        "background: rgba(12,12,12,209); color: rgba(245,245,245,0.98);"
        "font-size: 11px; padding: 2px 6px; border-radius: 3px;"
        "border: 1px solid rgba(255,255,255,0.12);"
    );
    m_timeBubble->hide();

    // MAKE_MPV_BEAT_FFMPEG Task 8 (2026-05-03) — re-apply backend-aware
    // overlay style now that all transient HUD widgets exist (m_volumeHud
    // at line 1924, m_toastHud at line 2103, etc.). The earlier call at
    // line 1510 styled m_controlBar correctly but its fan-out null-checks
    // skipped the not-yet-created HUDs. Idempotent on m_controlBar; sets
    // setBackdropOpaque(mpvNativeSurface) on m_volumeHud + m_toastHud.
    applySurfaceOverlayStyle();
}

// ── Controls ────────────────────────────────────────────────────────────────

void VideoPlayer::togglePause()
{
    // MAKE_MPV_BEAT_FFMPEG Task 8 (2026-05-03) — defensive m_paused resync
    // before deciding which path. Hemanth-reported pause/un-pause asymmetric
    // bug (2026-05-03 Task 7 smoke): keyPress dispatched action='toggle_pause'
    // 7 times but state stuck paused. Hypothesis: m_inStallPause flag
    // (MpvBackend.cpp:1143) gets stuck-true under timing races with
    // paused-for-cache events on local files, suppressing all subsequent
    // stateChanged emits — VideoPlayer's m_paused never updates past the
    // first pause, all subsequent togglePause calls go down the same branch
    // (one-way no-op). Bug did not reproduce on second launch but root
    // cause unconfirmed.
    //
    // Fix shape: query mpv's actual pause property directly via the
    // MpvBackend cast and resync m_paused BEFORE deciding sendPause vs
    // sendResume. Even if stateChanged emit got suppressed by m_inStallPause,
    // this re-check forces correct path selection. Only applies on mpv
    // backend; ffmpeg/SidecarProcess path uses a different state-machine
    // and doesn't have this bug shape.
#ifdef HAS_LIBMPV
    if (auto* mpvb = qobject_cast<MpvBackend*>(m_backend)) {
        const bool actuallyPaused = mpvb->isPausedSnapshot();
        if (actuallyPaused != m_paused) {
            m_paused = actuallyPaused;
            updatePlayPauseIcon();  // mirror onStateChanged side-effect
        }
    }
#endif

    if (m_paused) {
        m_backend->sendResume();
        m_centerFlash->flash(SVG_PLAY);
    } else {
        m_backend->sendPause();
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
    m_backend->sendSetMute(m_muted);
    // MAKE_MPV_BEAT_FFMPEG Task 8 (2026-05-03) — VolumeHud abandoned in
    // favor of ToastHud-style text popup per Hemanth directive ("just the
    // text, like, how the pop up for speed is"). VolumeHud's alpha-aware
    // pill paint timing on the mpv backend produced library bleed-through
    // during the fade window even after multiple paint-restructure
    // attempts; the simpler text toast pattern (already proven on Speed)
    // sidesteps the issue entirely. m_volumeHud stays as dead member;
    // full delete is a cleanup follow-up.
    m_toastHud->showToast(m_muted
        ? QStringLiteral("Volume: %1%% (muted)").arg(m_volume)
        : QStringLiteral("Volume: %1%%").arg(m_volume));
}

void VideoPlayer::speedUp()
{
    m_speedIdx = qMin(m_speedIdx + 1, SPEED_COUNT - 1);
    m_backend->sendSetRate(SPEED_PRESETS[m_speedIdx]);
    m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
}

void VideoPlayer::speedDown()
{
    m_speedIdx = qMax(m_speedIdx - 1, 0);
    m_backend->sendSetRate(SPEED_PRESETS[m_speedIdx]);
    m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
}

void VideoPlayer::speedReset()
{
    m_speedIdx = 2; // 1.0x
    m_backend->sendSetRate(1.0);
    m_toastHud->showToast(QString("Speed: %1").arg(SPEED_LABELS[m_speedIdx]));
}

// VIDEO_HUD_MINIMALIST 2026-04-25 — single dispatch path for audio
// and subtitle delay. Both keyboard handlers and SettingsPopover
// signals route through these.
void VideoPlayer::adjustAudioDelay(int delta)
{
    // delta == 0 is treated as a reset sentinel (mirrors the
    // audio_delay_reset action body).
    if (delta == 0) m_audioDelayMs = 0;
    else            m_audioDelayMs += delta;
    if (m_backend) m_backend->sendSetAudioDelay(m_audioDelayMs);
    if (!m_audioDeviceKey.isEmpty()) {
        QSettings s("Tankoban", "Tankoban");
        s.setValue(m_audioDeviceKey, m_audioDelayMs);
        s.setValue(m_audioDeviceKey + "/manual", true);
    }
    if (m_settingsPopover) m_settingsPopover->setAudioDelay(m_audioDelayMs);
    if (m_toastHud) {
        if (delta == 0)
            m_toastHud->showToast("Audio delay reset");
        else
            m_toastHud->showToast(QString("Audio delay: %1ms").arg(m_audioDelayMs));
    }
}

void VideoPlayer::onAudioDeviceChanged(const QString& friendlyName)
{
    // MAKE_MPV_SOLO Task 8.B (2026-05-02) — mid-playback audio device
    // switch handler. Mirrors the file-open recall logic at
    // VideoPlayer.cpp:3970-4007 but keyed on the watcher-supplied
    // friendly name + cached host API (the watcher knows the new device
    // friendly name from Windows IMMDevice property; the host API is
    // whatever the active backend was running through, captured when
    // the file last opened via mediaInfo).
    if (friendlyName.isEmpty()) return;
    if (!m_backend) return;
    if (m_audioHostApi.isEmpty()) {
        // No file ever opened in this session — nothing to recall against.
        // The next file-open mediaInfo handler will populate hostApi and
        // recall the saved delay; no further work needed here.
        return;
    }

    constexpr int BT_DEFAULT_MS = 300;

    QString newKey = makeDeviceKey(friendlyName, m_audioHostApi);
    if (newKey == m_audioDeviceKey) return;  // same device, nothing to do
    m_audioDeviceKey = newKey;

    QSettings s("Tankoban", "Tankoban");
    QVariant stored = s.value(m_audioDeviceKey);

    if (stored.isValid()) {
        m_audioDelayMs = stored.toInt();
        m_backend->sendSetAudioDelay(m_audioDelayMs);
        if (m_settingsPopover) m_settingsPopover->setAudioDelay(m_audioDelayMs);
        if (m_toastHud) {
            m_toastHud->showToast(
                QString("%1 → %2ms").arg(friendlyName).arg(m_audioDelayMs));
        }
        debugLog(QString("[VideoPlayer] audio device switched to '%1' → recalled offset %2ms")
                    .arg(friendlyName).arg(m_audioDelayMs));
    } else if (looksLikeBluetooth(friendlyName)) {
        m_audioDelayMs = BT_DEFAULT_MS;
        s.setValue(m_audioDeviceKey, BT_DEFAULT_MS);
        m_backend->sendSetAudioDelay(m_audioDelayMs);
        if (m_settingsPopover) m_settingsPopover->setAudioDelay(m_audioDelayMs);
        if (m_toastHud) {
            m_toastHud->showToast(
                QString("Bluetooth: %1 → %2ms")
                    .arg(friendlyName).arg(BT_DEFAULT_MS));
        }
        debugLog(QString("[VideoPlayer] Bluetooth device '%1' → default %2ms")
                    .arg(friendlyName).arg(BT_DEFAULT_MS));
    } else {
        m_audioDelayMs = 0;
        m_backend->sendSetAudioDelay(0);
        if (m_settingsPopover) m_settingsPopover->setAudioDelay(0);
        debugLog(QString("[VideoPlayer] wired/unknown device '%1' → no offset")
                    .arg(friendlyName));
    }
}

void VideoPlayer::adjustSubDelay(int delta)
{
    if (delta == 0) m_subDelayMs = 0;
    else            m_subDelayMs += delta;
    if (m_backend) m_backend->sendSetSubDelay(m_subDelayMs);
    if (m_settingsPopover) m_settingsPopover->setSubtitleDelay(m_subDelayMs);
    if (m_toastHud) {
        if (delta == 0)
            m_toastHud->showToast("Sub delay reset");
        else
            m_toastHud->showToast("Sub delay: " + QString::number(m_subDelayMs) + "ms");
    }
}

void VideoPlayer::adjustSubPosition(int delta)
{
    m_subPositionPct = qBound(0, m_subPositionPct + delta, 100);
    if (m_backend) m_backend->sendSetSubtitlePosition(m_subPositionPct);
    QSettings("Tankoban", "Tankoban")
        .setValue("videoPlayer/subtitlePosition", m_subPositionPct);
    if (m_settingsPopover) m_settingsPopover->setSubtitlePosition(m_subPositionPct);
    if (m_toastHud)
        m_toastHud->showToast(QString("Sub position: %1%").arg(m_subPositionPct));
}

void VideoPlayer::adjustSubtitleSize(double delta)
{
    // MPV_FFMPEG_PARITY Phase 2.G (2026-04-30) — clamp 0.5..2.0; push +
    // persist + toast + sync popover. SidecarProcess::sendSetSubtitleSize
    // already clamps internally to 0.5..3.0, but we narrow to 0.5..2.0
    // for UI sanity (3x is rarely useful and wastes slider real estate).
    double next = m_subtitleSize + delta;
    if (next < 0.5) next = 0.5;
    if (next > 2.0) next = 2.0;
    // Snap to nearest 0.1 step to avoid floating-point drift after many
    // ±0.1 button presses (0.7 + 0.1 - 0.1 = 0.6999... without snap).
    next = std::round(next * 10.0) / 10.0;
    if (qFuzzyCompare(next, m_subtitleSize)) return;
    m_subtitleSize = next;
    if (m_backend) m_backend->sendSetSubtitleSize(m_subtitleSize);
    QSettings("Tankoban", "Tankoban")
        .setValue("videoPlayer/subtitleSize", m_subtitleSize);
    if (m_settingsPopover) m_settingsPopover->setSubtitleSize(m_subtitleSize);
    if (m_toastHud)
        m_toastHud->showToast(
            QStringLiteral("Sub size: %1x").arg(m_subtitleSize, 0, 'f', 1));
}

void VideoPlayer::setBrightness(int value)
{
    // MAKE_MPV_SOLO Task 9 (2026-05-01) — single dispatch path for the
    // brightness slider. Clamp -100..+100; dedupe (no-op if unchanged);
    // push to backend; persist; sync popover label; toast on settle.
    //
    // Backend payload shape: reuse the existing 7-arg sendSetFilters
    // contract with neutral contrast/saturation (100/100) so the ffmpeg
    // sidecar's `eq=` filter graph is built brightness-only — no behavior
    // change to contrast/saturation paths. The MpvBackend stub fills with
    // the `brightness` mpv property only; other params remain ignored.
    //
    // Live-update during slider drag is intentional (Hemanth reliability
    // gate: "Drag the bar → picture brightness changes immediately").
    // mpv property write is cheap; ffmpeg sidecar set_filters rebuild on
    // each drag tick may flicker on heavy streams — known follow-up if
    // observed in practice. Smoke close-gate is the mpv path.
    if (value < -100) value = -100;
    if (value > 100)  value = 100;
    if (value == m_brightness) return;
    m_brightness = value;
    if (m_backend) {
        m_backend->sendSetFilters(false, m_brightness,
                                  /*contrast*/ 100, /*saturation*/ 100,
                                  /*normalize*/ false,
                                  /*interpolate*/ false,
                                  /*deinterlaceFilter*/ QString());
    }
    QSettings("Tankoban", "Tankoban")
        .setValue("videoPlayer/brightness", m_brightness);
    if (m_brightnessPopover) m_brightnessPopover->setBrightness(m_brightness);
    // No toast on every slider tick — would spam the HUD. Slider value
    // label inside the popover already reads the current value live.
}

void VideoPlayer::adjustBrightness(int delta)
{
    // MAKE_MPV_SOLO Task 9 follow-up (2026-05-01) — keyboard delta path.
    // setBrightness clamps + dedupes + pushes; this just adds the toast.
    const int prev = m_brightness;
    setBrightness(m_brightness + delta);
    if (m_brightness == prev) return;  // clamped at limit; suppress toast
    if (m_toastHud) {
        const QString sign = (m_brightness > 0) ? "+" : "";
        m_toastHud->showToast(QStringLiteral("Brightness: %1%2").arg(sign).arg(m_brightness));
    }
}

void VideoPlayer::setSubPositionMode(const QString& mode)
{
    // MPV_FFMPEG_PARITY Phase 2.F (2026-04-30) — sanitize unknown values
    // to "standard" so a stale popover state can't poison the dispatcher.
    const QString sanitized = (mode == QStringLiteral("force"))
                                  ? QStringLiteral("force")
                                  : QStringLiteral("standard");
    if (sanitized == m_subPositionMode) return;
    m_subPositionMode = sanitized;
    if (m_backend) m_backend->sendSetSubtitlePositionMode(m_subPositionMode);
    QSettings("Tankoban", "Tankoban")
        .setValue("videoPlayer/subtitlePositionMode", m_subPositionMode);
    if (m_toastHud)
        m_toastHud->showToast(QStringLiteral("Sub position: %1")
                                  .arg(sanitized == QStringLiteral("force")
                                           ? QStringLiteral("Force")
                                           : QStringLiteral("Standard")));
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

    // VIDEO_HUD_MINIMALIST 2026-04-25 — popovers split by media type;
    // pass the type-specific cached arrays directly. Cache is post-merge,
    // so it survives the sidecar emitting shortened tracks_changed
    // payloads after set_tracks.
    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 — was passing a typed
    // `merged` array which exercised a now-deleted defensive type filter;
    // the chip-click entry point bypassed that path entirely (passing
    // m_audioTracks without type stamps), causing an empty audio popover
    // on chip-click. Symmetric for subtitles.
    int audioId = activeAudioId.toInt();
    int subId   = activeSubId.toInt();
    if (m_audioPopover)    m_audioPopover->populate(m_audioTracks, audioId);
    if (m_subtitlePopover) m_subtitlePopover->setEmbeddedTracksFromJson(m_subTracks, subId, m_subsVisible);

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
    m_backend->sendSetTracks(newId, "");
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
    m_backend->sendSetTracks("", newId);
    if (!m_subsVisible) {
        m_subsVisible = true;
        m_backend->sendSetSubVisibility(true);
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
    m_backend->sendSetSubVisibility(m_subsVisible);
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
    m_backend->sendSetSubVisibility(false);
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
    // Carry forward current track language preferences + Task 6.B IDs
    m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
    m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
    m_carryAudioId = m_activeAudioId;     // Task 6.B
    m_carrySubId = m_activeSubId;         // Task 6.B
    m_carryAspect = m_currentAspect;
    m_carryCrop = m_currentCrop;
    openFile(m_playlist[m_playlistIdx - 1], m_playlist, m_playlistIdx - 1);
}

void VideoPlayer::nextEpisode()
{
    if (m_playlist.isEmpty() || m_playlistIdx >= m_playlist.size() - 1) return;
    // Carry forward current track language preferences + Task 6.B IDs
    m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
    m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
    m_carryAudioId = m_activeAudioId;     // Task 6.B
    m_carrySubId = m_activeSubId;         // Task 6.B
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
        m_backend->sendSetMute(false);
    }
    m_backend->sendSetVolume(m_volume / 100.0);  // 150 → 1.5, etc.
    // MAKE_MPV_BEAT_FFMPEG Task 8 (2026-05-03) — see toggleMute above for
    // rationale: VolumeHud abandoned, ToastHud-style text popup replaces it.
    m_toastHud->showToast(m_muted
        ? QStringLiteral("Volume: %1%% (muted)").arg(m_volume)
        : QStringLiteral("Volume: %1%%").arg(m_volume));
}

void VideoPlayer::updatePlayPauseIcon()
{
    // MAKE_MPV_SOLO Task 9 follow-up (2026-05-01) — icon mirrors current
    // STATE per Hemanth verbatim "Pause symbol must show when the video
    // is paused. Play symbol must show when the video is playing."
    // (Industry "next-action" convention was inverted on his screen, hence
    // this flip.) Pre-flip: paused=true → playIcon (next-action = play).
    // Post-flip: paused=true → pauseIcon (state = paused).
    m_playPauseBtn->setIcon(m_paused ? m_pauseIcon : m_playIcon);
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

int VideoPlayer::subtitleBaselineLiftPx() const
{
    // STREAM_SUBTITLE_POSITION_FIX 2026-04-20 (hemanth-reported "subs
    // sit too high vs VLC/mpv/PotPlayer" during PLAYER_COMPARATIVE_AUDIT
    // Phase 2 smoke): baseline dropped 6% → 2% of canvas height.
    //
    // Prior 6% (= 65 px on 1080, 58 px on 974 windowed) was chosen as
    // the Netflix/YouTube streaming title-safe zone, which is the wrong
    // reference — that's a broadcast/encoding safe-zone for producers,
    // not a subtitle-rendering default for consumption players. Applied
    // as a floor on top of libass MarginV, it pushed subs ~150 px above
    // where VLC / mpv / PotPlayer render them on the same content.
    //
    // Reference-player defaults (measured Phase 2 pilot 2026-04-20):
    //   mpv:       sub-margin-y=22 → 22 px from bottom
    //   VLC 3.0.23: ~30 px default (Preferences → Subtitles)
    //   PotPlayer:  ~20-30 px default
    //
    // 2% of canvas = 22 px on 1080, 20 px on 974 — matches mpv, slightly
    // under VLC. Still a non-zero floor so ASS files with MarginV=0 don't
    // render flush at the frame edge (prevents the file-supplied-style
    // issue the original rationale cited). HUD-visible lift path
    // (qMax(hudLiftPx, baseline)) is unchanged — 120 px hud-height still
    // wins when controls are up, keeping subs from being occluded.
    if (!m_canvas) return 0;
    const qreal dpr = devicePixelRatioF();
    const int canvasPxH = qRound(m_canvas->height() * dpr);
    // SUBTITLE_SINKING_FIX 2026-04-22 + WINDOWED_TOO_HIGH_FIX same-wake
    // (hemanth two-phase symptom report): fullscreen needed 6 % to stop
    // subs clipping past the frame bottom on the same-shape Main02 cue
    // empirically measured at y=1079 (1 px margin) before the bump.
    // In windowed / maximized mode the overlay vp is already scaled down
    // by canvas_h / 1080 (e.g. 974/1080 = 0.90 in maximized), which
    // *compresses* the SHM y-axis upward — so the same 6 % baseline that
    // was necessary to rescue fullscreen renders windowed subs too high
    // against a smaller video area. Keep 6 % for fullscreen where the
    // vp is 1:1 with the SHM, fall back to the prior 2 % in windowed
    // where the natural compression already supplies ~50 px of headroom.
    // `m_fullscreen` is the same boolean that drives the aspect log's
    // widget-dim transitions, so this responds to double-click / F-key
    // toggles correctly. HUD-visible path (qMax(hudLiftPx, baseline))
    // unchanged — HUD height still wins when controls are up.
    const double ratio = m_fullscreen ? 0.06 : 0.02;
    return qMax(0, qRound(canvasPxH * ratio));
}

void VideoPlayer::showControls()
{
    m_controlBar->show();
    m_subOverlay->setControlsVisible(true);
    // PER_VIEW_CHROME_FIX 2026-05-02 P2 — chrome cluster rides the HUD
    // show/hide lifecycle. Hidden in fullscreen (matches Windows convention
    // of no chrome over fullscreen content).
    if (m_chromeOverlay && !window()->isFullScreen()) {
        m_chromeOverlay->show();
        m_chromeOverlay->raise();
    }
    // The control bar was possibly hidden when the title label received
    // its intended width; re-elide now that layout is guaranteed to have
    // assigned the label its geometry.
    updateTitleElision();
    // Lift subtitle overlay above the HUD so the control bar doesn't
    // occlude subs. Physical pixels — multiply by dpr so the lift is
    // consistent on HiDPI displays where the swap chain is in physical
    // pixels but sizeHint() returns logical. Floored at the 6% baseline
    // so a tiny HUD on a 4K canvas still keeps subs in the safe zone.
    if (m_canvas) {
        const qreal dpr = devicePixelRatioF();
        const int hudLiftPx = qRound(m_controlBar->sizeHint().height() * dpr);
        m_canvas->setSubtitleLift(qMax(hudLiftPx, subtitleBaselineLiftPx()));
        // VIDEO_CURSOR_AUTOHIDE 2026-04-24 (hemanth): unblank cursor on the
        // canvas HWND when HUD reveals. setCursor on VideoPlayer alone does
        // not reach the native child — must target m_canvas directly.
        m_canvas->unsetCursor();
    }
    // Task 7 (2026-05-01) — mpv path mirror. m_mpvWidget is the active
    // video widget on the mpv backend; cursor blank/unblank must target
    // it directly for the same reason the canvas path does (parent's
    // cursor doesn't propagate to a sized child widget).
    if (m_mpvWidget) m_mpvWidget->unsetCursor();
    // Don't restart auto-hide timer while playlist drawer is open
    if (!m_playlistDrawer || !m_playlistDrawer->isOpen())
        m_hideTimer.start();
}

bool VideoPlayer::isAnyPopoverOpen() const
{
    return (m_subtitlePopover   && m_subtitlePopover->isOpen())
        || (m_audioPopover      && m_audioPopover->isOpen())
        || (m_settingsPopover   && m_settingsPopover->isOpen())
        || (m_brightnessPopover && m_brightnessPopover->isOpen());  // Task 9
}

void VideoPlayer::hideControls()
{
    // VIDEO_HUD_AUTOHIDE_ON_PAUSE 2026-04-24 (hemanth: "100% zoom is just
    // cropping the bottom part of the screen"): the control bar at 180px
    // tall with 0.50 alpha obscures the source scoreboard row whenever
    // the user pauses to read it — reads as "video cut off at bottom"
    // even though the D3D viewport is 1:1 (videoRect=={0,0,1920,1080}).
    // Every reference player (mpv OSC, VLC OSD, PotPlayer) auto-hides
    // the HUD after ~3s of cursor idle REGARDLESS of pause state — only
    // cursor movement reshows it. Prior `if (m_paused) return` pinned
    // the HUD visible during every pause, shipped in the April 22 cutoff
    // fix as a hedge; it was the wrong hedge. The m_seeking guard stays
    // because active scrubbing needs the progress bar visible.
    if (m_seeking) return;
    // VIDEO_HUD_MINIMALIST 1.x bug-fix 2026-04-25 (hemanth: "subtitle and
    // audio overlays are separate from the bottom hud, meaning the bottom
    // hud can disappear while the overlays are still active"): popover
    // open == user is mid-task; HUD must stay visible (the popover is
    // anchored to a chip in the HUD — popover floating over a fading HUD
    // is broken). On popover dismiss, AudioPopover/SubtitlePopover/
    // SettingsPopover::dismiss emits hoverChanged(false) which restarts
    // the auto-hide timer with a fresh 3s window.
    if (isAnyPopoverOpen()) return;
    // PER_VIEW_CHROME_FIX 2026-05-02 P2 — keep HUD + chrome alive while the
    // cursor is parked on the chrome cluster (matches comic reader pattern).
    if (m_chromeOverlay && m_chromeOverlay->isVisible() && m_chromeOverlay->underMouse()) {
        m_hideTimer.start();
        return;
    }
    m_controlBar->hide();
    m_subOverlay->setControlsVisible(false);
    if (m_chromeOverlay) m_chromeOverlay->hide();
    // HUD gone — drop to the 6% safe-zone baseline (was 0 = flush at
    // frame bottom for any ASS file with low MarginV — broken for
    // file-supplied styles even though our injected SRT header was fine).
    if (m_canvas) {
        m_canvas->setSubtitleLift(subtitleBaselineLiftPx());
        // VIDEO_CURSOR_AUTOHIDE 2026-04-24 (hemanth): blank the cursor on
        // the canvas HWND when HUD hides. Reference players (mpv / VLC /
        // PotPlayer) all hide cursor + HUD as a single idle gesture
        // regardless of pause state — matches the paused-guard removal
        // applied to hideControls itself this same day.
        m_canvas->setCursor(Qt::BlankCursor);
    }
    // Task 7 (2026-05-01) — mpv path mirror. Same cursor-blank as canvas.
    if (m_mpvWidget) m_mpvWidget->setCursor(Qt::BlankCursor);
}

void VideoPlayer::saveProgress(double positionSec, double durationSec)
{
    // Stream mode (PersistenceMode::None) never populates m_currentVideoId
    // because videoIdForFile() requires QFileInfo::exists() which fails on
    // HTTP URLs. Pre-fix: the videoId guard here early-returned before
    // either the "videos" write OR the progressUpdated emit, so StreamPage's
    // progress listener never heard a tick → Continue Watching for streams
    // was permanently empty. Now the guard only blocks the "videos"-domain
    // write (moved inside the LibraryVideos branch below); the signal emit
    // always fires when there's a real current file and a bridge. Stream
    // writes happen in StreamPage::onReadyToPlay's progressUpdated lambda
    // via m_bridge->saveProgress("stream", epKey, ...) — that path does
    // not depend on m_currentVideoId.
    if (!m_bridge || m_currentFile.isEmpty())
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
    // videoId presence is re-guarded here since the function-top guard
    // was relaxed to let stream mode through (URLs never resolve a
    // real videoId).
    if (m_persistenceMode == PersistenceMode::LibraryVideos
        && !m_currentVideoId.isEmpty()) {
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
        // Task 6.B (2026-05-01) — also consume the carried track IDs.
        // resolveTrack (below) tries id first and validates by lang
        // agreement, so adding the id makes anime cross-playlist track
        // pick respect "user picked sub track 2 on ep 1 → ep 2 also gets
        // sub track 2 if its lang matches" instead of falling back to
        // the lang-only resolve which lands on the file's default
        // (typically Signs/Songs on anime release-group conventions).
        targetAudioId = m_carryAudioId;
        targetSubId = m_carrySubId;
        m_carryAudioLang.clear();
        m_carrySubLang.clear();
        m_carryAudioId.clear();
        m_carrySubId.clear();
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
                m_backend->sendSetSubVisibility(vis);
            }
            perFileVisibilityApplied = true;
        }
        if (prog.contains("subDelayMs")) {
            int delay = prog.value("subDelayMs").toInt(0);
            if (delay != 0) {
                m_subDelayMs = delay;
                m_backend->sendSetSubDelay(delay);
                if (m_settingsPopover) m_settingsPopover->setSubtitleDelay(delay);
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
                m_backend->sendSetSubVisibility(vis);
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
        m_backend->sendSetTracks(
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
    if (m_backend) {
        m_backend->sendSetSubVisibility(m_subsVisible);
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
    if (token == QLatin1String("2.39:1")) return 2.39;
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
    if (m_mpvWidget) m_mpvWidget->setGeometry(0, 0, width(), height());

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

    // PER_VIEW_CHROME_FIX 2026-05-02 P2 — chrome cluster top-right corner.
    // Hidden in fullscreen; otherwise placed 12px from edges. Sized to its
    // sizeHint so the QHBoxLayout settles before move().
    if (m_chromeOverlay) {
        if (window()->isFullScreen()) {
            m_chromeOverlay->hide();
        } else {
            m_chromeOverlay->resize(m_chromeOverlay->sizeHint());
            m_chromeOverlay->move(width() - m_chromeOverlay->width() - 12, 12);
        }
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
    if (m_chromeOverlay && m_chromeOverlay->isVisible()) m_chromeOverlay->raise();
    if (m_playlistDrawer->isOpen())      m_playlistDrawer->raise();
}

// PER_VIEW_CHROME_FIX 2026-05-02 P2 — Max ↔ Restore icon swap, called by
// MainWindow on WindowStateChange so the video player chrome reflects the
// live state of the underlying window.
void VideoPlayer::updateChromeMaxIcon(bool isMaximized)
{
    if (!m_chromeMaxBtn) return;
    m_chromeMaxBtn->setIcon(QIcon(isMaximized
                                  ? ":/icons/chrome_restore.svg"
                                  : ":/icons/chrome_max.svg"));
    m_chromeMaxBtn->setToolTip(isMaximized ? "Restore" : "Maximize");
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
            (m_subtitlePopover && m_subtitlePopover->isOpen()) ||
            (m_audioPopover    && m_audioPopover->isOpen()) ||
            (m_settingsPopover   && m_settingsPopover->isOpen()) ||
            (m_brightnessPopover && m_brightnessPopover->isOpen()) ||  // Task 9
            (m_playlistDrawer    && m_playlistDrawer->isOpen());
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
        // Task 7 (2026-05-01) — Pattern C fix: use the pending target as
        // the seek base if a previous seek hasn't been confirmed by a
        // time_update yet. Pre-fix, rapid arrow-key double-tap read
        // m_seekBar->value() (which echoes the BACKEND-confirmed position
        // and lags ~200ms behind the user's keypress) twice → both
        // presses computed the same target → second press visually
        // flashed but landed nowhere. Backend-independent UI bug;
        // applies to both ffmpeg sidecar AND mpv paths identically.
        const double basePos = (m_pendingSeekTargetSec >= 0.0)
            ? m_pendingSeekTargetSec
            : (m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0.0);
        const double target = qMax(0.0, basePos + delta);
        m_pendingSeekTargetSec = target;
        // Clear FrameCanvas's sustained-lag accumulator BEFORE the seek —
        // the wall-clock Present-interval gap across the seek would
        // otherwise arm the 3-in-a-row skip-next-Present guard and
        // produce a visible post-seek pause.
        if (m_canvas) m_canvas->resetLagAccounting();
        m_backend->sendSeek(target);
        m_centerFlash->flash(delta > 0 ? SVG_SEEK_FWD : SVG_SEEK_BACK);
        // No showControls() — the center flash arrow is sufficient feedback.
        // Revealing the bottom HUD on every arrow-key seek is noisy and
        // fights the auto-hide UX when the user is scrubbing through content.
    };

    // VIDEO_HUD_MINIMALIST 2026-04-25 — adjustSubDelay promoted to
    // VideoPlayer::adjustSubDelay member function. Local lambda dropped;
    // dispatch below calls the member directly.

    // Dispatch action
    if      (action == "toggle_pause")       togglePause();
    else if (action == "seek_back_10s")      seekBy(-10.0);
    else if (action == "seek_fwd_10s")       seekBy(10.0);
    else if (action == "seek_back_60s")      seekBy(-60.0);
    else if (action == "seek_fwd_60s")       seekBy(60.0);
    else if (action == "frame_step_fwd") {
        if (!m_paused) togglePause();
        m_backend->sendFrameStep(false);
        m_toastHud->showToast("Step forward");
    }
    else if (action == "frame_step_back") {
        if (!m_paused) togglePause();
        double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
        m_backend->sendFrameStep(true, curSec);
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
    // VIDEO_HUD_MINIMALIST 2026-04-25 — toggle_deinterlace + toggle_normalize
    // actions deleted with the FilterPopover. Their KeyBindings.cpp
    // entries (D + Shift+A) also removed. Original audio + video qualities
    // pass through with no UI to enable filters.
    else if (action == "cycle_audio")        cycleAudioTrack();
    else if (action == "cycle_subtitle")     cycleSubtitleTrack();
    else if (action == "toggle_subtitles")   toggleSubtitles();
    else if (action == "toggle_always_on_top") toggleAlwaysOnTop();
    else if (action == "take_snapshot")        takeSnapshot();
    else if (action == "toggle_pip")           togglePictureInPicture();
    else if (action == "open_url")             showOpenUrlDialog();
    else if (action == "toggle_stats")         toggleStatsBadge();
    // MAKE_MPV_SOLO Task 9 follow-up (2026-05-01) — brightness keyboard
    // adjustments. v/b for ±5 deltas; r for reset to 0. Each routes through
    // setBrightness (clamp + push + persist + popover sync) plus a one-shot
    // toast (slider drag path is intentionally toast-free to avoid HUD
    // spam; single-key-press is rare enough to toast).
    else if (action == "brightness_minus")     adjustBrightness(-5);
    else if (action == "brightness_plus")      adjustBrightness(+5);
    else if (action == "brightness_reset") {
        setBrightness(0);
        if (m_toastHud) m_toastHud->showToast(QStringLiteral("Brightness: 0"));
    }
    else if (action == "open_subtitle_menu") {
        // Reroute T-key to the merged SubtitlePopover anchored on the
        // new Subtitle chip.
        if (m_subtitlePopover && m_subtitleChip) {
            dismissOtherPopovers(m_subtitlePopover);
            m_subtitlePopover->toggle(m_subtitleChip);
            m_subtitleChip->setChecked(m_subtitlePopover->isOpen());
        }
    }
    else if (action == "sub_delay_minus")    adjustSubDelay(-100);
    else if (action == "sub_delay_plus")     adjustSubDelay(100);
    else if (action == "sub_delay_reset")    adjustSubDelay(0);
    else if (action == "audio_delay_minus")  adjustAudioDelay(-50);
    else if (action == "audio_delay_plus")   adjustAudioDelay(50);
    else if (action == "audio_delay_reset")  adjustAudioDelay(0);
    else if (action == "chapter_next") {
        if (!m_chapters.isEmpty()) {
            double curSec = m_durationSec > 0 ? m_seekBar->value() / 10000.0 * m_durationSec : 0;
            for (const auto& ch : m_chapters) {
                double start = ch.toObject().value("start").toDouble();
                if (start > curSec + 1.0) {
                    // PLAYER_STREMIO_PARITY Phase 3 — chapter boundaries are
                    // UX-critical (subtitle sync, scene markers); force Exact
                    // regardless of sticky user pref.
                    m_backend->sendSeek(start, QStringLiteral("exact"));
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
                    // PLAYER_STREMIO_PARITY Phase 3 — see chapter_next note.
                    m_backend->sendSeek(start, QStringLiteral("exact"));
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
        // after 60 seconds. REPO_HYGIENE P1.2 (2026-04-26): resolved via Qt
        // standard paths instead of hardcoded developer machine path.
        const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(appDataDir);  // ensure exists
        const QString dumpPath = QDir(appDataDir).absoluteFilePath("_vsync_timing.csv");
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
    // PLAYER_UX_FIX Phase 6.4 — unified dismiss. VIDEO_HUD_MINIMALIST
    // 2026-04-25 reshape covers the new three-chip cluster.
    bool closedSomething = false;
    if (m_subtitlePopover && m_subtitlePopover->isOpen() &&
        !m_subtitlePopover->geometry().contains(event->pos()) &&
        !m_subtitleChip->geometry().contains(event->pos())) {
        m_subtitlePopover->hide();
        if (m_subtitleChip) m_subtitleChip->setChecked(false);
        closedSomething = true;
    }
    if (m_audioPopover && m_audioPopover->isOpen() &&
        !m_audioPopover->geometry().contains(event->pos()) &&
        !m_audioChip->geometry().contains(event->pos())) {
        m_audioPopover->hide();
        if (m_audioChip) m_audioChip->setChecked(false);
        closedSomething = true;
    }
    if (m_settingsPopover && m_settingsPopover->isOpen() &&
        !m_settingsPopover->geometry().contains(event->pos()) &&
        !m_settingsChip->geometry().contains(event->pos())) {
        m_settingsPopover->hide();
        if (m_settingsChip) m_settingsChip->setChecked(false);
        closedSomething = true;
    }
    // MAKE_MPV_SOLO Task 9 (2026-05-01) — brightness chip click-outside.
    if (m_brightnessPopover && m_brightnessPopover->isOpen() &&
        !m_brightnessPopover->geometry().contains(event->pos()) &&
        m_brightnessChip &&
        !m_brightnessChip->geometry().contains(event->pos())) {
        m_brightnessPopover->hide();
        if (m_brightnessChip) m_brightnessChip->setChecked(false);
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
    // VIDEO_PLAYER_UI_POLISH Phase 1 2026-04-22: reveal HUD on any mouse
    // motion, matching VLC / mpv / PotPlayer convention. Prior code gated
    // reveal to the bottom 120 px which made the player feel hesitant
    // because moving into the lower player area (bottom third) wouldn't
    // surface the bar. Twin of the FrameCanvas mouseActivityAt lambda
    // above — both paths must behave the same since the native D3D11
    // canvas child doesn't bubble mouse events.
    // VIDEO_CURSOR_AUTOHIDE 2026-04-24: cursor unblank handled inside
    // showControls (m_canvas->unsetCursor) — removed dead setCursor on
    // VideoPlayer that never reached the canvas HWND.
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
                m_backend->sendSetSubtitleUrl(QUrl::fromLocalFile(p), 0, 0);
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
    // VIDEO_CONTEXT_MENU_MINIMALIST 2026-04-25 (Phase 2) — slim 8-item
    // menu. See VideoContextMenu.h for the full final shape. Keyboard
    // bindings for all dropped entries (Pause/Mute/Speed/Snapshot/
    // Stats/AOT/PiP/OpenURL) are preserved — only the menu surface
    // goes away.
    VideoContextData data;
    data.audioTracks    = m_audioTracks;
    data.subtitleTracks = m_subTracks;
    data.currentAudioId = m_activeAudioId.toInt();
    data.currentSubId   = m_activeSubId.toInt();
    data.subsVisible    = m_subsVisible;
    data.currentAspect  = m_currentAspect;
    data.currentCrop    = m_currentCrop;

    auto* menu = VideoContextMenu::build(data, this,
        [this](VideoContextMenu::ActionType t, QVariant v) {
        switch (t) {
        case VideoContextMenu::SetAspectRatio: {
            // Aspect Ratio override: rewires FrameCanvas::setForcedAspectRatio.
            // "original" → 0 (use natural frame aspect from m_frameW/m_frameH).
            // Mapping in aspectStringToDouble keeps openFile restore path in
            // sync with the menu's write path.
            const QString val = v.toString();
            m_canvas->setForcedAspectRatio(aspectStringToDouble(val));
            m_currentAspect = val;
            saveShowPrefs();
            m_toastHud->showToast(QString("Aspect: %1").arg(val));
            break;
        }
        case VideoContextMenu::SetCrop: {
            // Crop-to-aspect: zoom video viewport uniformly so baked
            // letterbox/pillarbox strips get clipped by the render target.
            // Orthogonal to Aspect Ratio. "none" → 0.0 = no crop.
            const QString val = v.toString();
            m_canvas->setCropAspect(cropStringToDouble(val));
            m_currentCrop = val;
            saveShowPrefs();
            m_toastHud->showToast(QString("Crop: %1").arg(val));
            break;
        }
        case VideoContextMenu::ToggleFullscreen: toggleFullscreen(); break;
        case VideoContextMenu::SetAudioTrack:
            m_backend->sendSetTracks(QString::number(v.toInt()), "");
            m_toastHud->showToast("Audio: track " + QString::number(v.toInt()));
            break;
        case VideoContextMenu::SetSubtitleTrack:
            // Player Polish Batch 5.2 fix (2026-04-15): visibility-on-
            // track-switch logic mirrors cycleSubtitleTrack. Picking a
            // real track auto-enables visibility; picking "off" (sentinel
            // -1, NOT 0 — id=0 is a real stream) auto-disables it.
            if (v.toInt() == -1) {
                setSubtitleOff();
            } else {
                if (!m_subsVisible) {
                    m_subsVisible = true;
                    m_backend->sendSetSubVisibility(true);
                }
                m_backend->sendSetTracks("", QString::number(v.toInt()));
                m_toastHud->showToast("Subtitle: track " + QString::number(v.toInt()));
            }
            break;
        case VideoContextMenu::LoadExternalSub: {
            QString p = QFileDialog::getOpenFileName(this, "Load Subtitle", "",
                "Subtitles (*.srt *.ass *.ssa *.sub *.vtt)");
            if (!p.isEmpty())
                m_backend->sendLoadExternalSub(p);
            break;
        }
        case VideoContextMenu::OpenPlaylist:    m_playlistDrawer->toggle();    break;
        case VideoContextMenu::OpenKeybindings: openKeybindingEditor();        break;
        case VideoContextMenu::BackToLibrary:   emit closeRequested();         break;
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

// ── REPO_HYGIENE Phase 3 — dev-control bridge snapshot ──────────────────────

QJsonObject VideoPlayer::devSnapshot() const
{
    auto persistenceModeStr = [](PersistenceMode m) -> const char* {
        switch (m) {
        case PersistenceMode::None:          return "None";
        case PersistenceMode::LibraryVideos: return "LibraryVideos";
        }
        return "Unknown";
    };

    QJsonObject snap;
    snap["currentFile"]         = m_currentFile;
    snap["pendingFile"]         = m_pendingFile;
    snap["openPending"]         = m_openPending;
    snap["paused"]              = m_paused;
    snap["streamMode"]          = m_streamMode;
    snap["persistenceMode"]     = QString::fromLatin1(persistenceModeStr(m_persistenceMode));
    snap["streamStalled"]       = m_streamStalled;
    snap["currentAspect"]       = m_currentAspect;
    snap["currentCrop"]         = m_currentCrop;
    snap["durationSec"]         = m_durationSec;
    snap["lastKnownPosSec"]     = m_lastKnownPosSec;
    snap["sidecarRetryCount"]   = m_sidecarRetryCount;
    snap["firstFrameWatchdogActive"] = m_firstFrameWatchdog.isActive();
    snap["firstFrameSeen"]      = m_firstFrameSeen;
    snap["visible"]             = isVisible();
    snap["fullScreen"]          = isFullScreen();
    return snap;
}

// ── Backend wiring (2026-04-30) ──────────────────────────────────────────────
//
// Single source of truth for every connect(m_backend, ...) call this class
// makes. Called from the constructor (after buildUI returns — all member
// widgets the lambdas reference exist by then) and from switchBackendTo
// after a mid-session backend swap. Adding a new m_backend signal handler
// belongs HERE; inline connects elsewhere will silently break swap.

void VideoPlayer::wireBackendSignals()
{
    // Core sidecar events (was ctor lines 231-238 pre-refactor).
    connect(m_backend,&IPlayerBackend::ready,        this, &VideoPlayer::onSidecarReady);
    connect(m_backend,&IPlayerBackend::firstFrame,   this, &VideoPlayer::onFirstFrame);
    connect(m_backend,&IPlayerBackend::timeUpdate,   this, &VideoPlayer::onTimeUpdate);
    connect(m_backend,&IPlayerBackend::stateChanged,  this, &VideoPlayer::onStateChanged);
    connect(m_backend,&IPlayerBackend::tracksChanged,  this, &VideoPlayer::onTracksChanged);
    connect(m_backend,&IPlayerBackend::endOfFile,    this, &VideoPlayer::onEndOfFile);
    connect(m_backend,&IPlayerBackend::errorOccurred, this, &VideoPlayer::onError);
    connect(m_backend,&IPlayerBackend::processCrashed, this, &VideoPlayer::onSidecarCrashed);

    // Batch 6.3 — non-fatal decode errors get a throttled toast. Corrupted
    // files can produce many per second; one toast every 3 s is enough to
    // communicate "something's wrong, we're skipping past it" without
    // spamming the UI. Throttle state lives in the lambda's static local —
    // shared across backend swaps (same VideoPlayer instance, same lambda
    // generated text in source); a fresh swap doesn't reset the throttle,
    // which is the desired UX (anti-spam stays anti-spam).
    connect(m_backend,&IPlayerBackend::decodeError, this,
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

    // VIDEO_PLAYER_FIX Batch 1.2 — keep m_subsVisible coherent with sidecar
    // renderer state across SubtitleMenu Off → toggleSubtitles flip path.
    connect(m_backend,&IPlayerBackend::subVisibilityChanged, this, [this](bool visible) {
        m_subsVisible = visible;
        if (!visible)
            m_subOverlay->setText("");
    });

    // Buffering / loading overlay stage transitions (was buildUI lines
    // 1818-1853 pre-refactor). LoadingOverlay member widget is built in
    // buildUI before this helper runs the first time.
    connect(m_backend,&IPlayerBackend::bufferingStarted,
            m_loadingOverlay, &LoadingOverlay::showBuffering);
    connect(m_backend,&IPlayerBackend::bufferingEnded,
            m_loadingOverlay, &LoadingOverlay::dismiss);
    // STREAM_STALL_UX_FIX Batch 2 — track sidecar HTTP-stall state.
    connect(m_backend,&IPlayerBackend::bufferingStarted,
            this, [this]() { m_sidecarBuffering = true; });
    connect(m_backend,&IPlayerBackend::bufferingEnded,
            this, [this]() { m_sidecarBuffering = false; });
    // PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — cache progress text upgrade.
    connect(m_backend,&IPlayerBackend::cacheStateChanged,
            m_loadingOverlay, &LoadingOverlay::setCacheProgress);
    // firstFrame is the primary dismiss trigger (overlay closed semantically).
    connect(m_backend,&IPlayerBackend::firstFrame,
            m_loadingOverlay, &LoadingOverlay::dismiss);
    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.2 — cancel watchdog on first frame.
    connect(m_backend,&IPlayerBackend::firstFrame, this, [this]() {
        m_firstFrameWatchdog.stop();
        m_firstFrameSeen = true;
    });

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — sub-stage wiring + Batch 1.3
    // re-emit pass-through. Lambdas capture this; SidecarProcess parented
    // to VideoPlayer so connection lifetime is bounded.
    connect(m_backend,&IPlayerBackend::probeStarted, this, [this]() {
        m_loadingOverlay->setStage(LoadingOverlay::Stage::Probing);
        emit probeStarted();
    });
    connect(m_backend,&IPlayerBackend::probeDone, this,
            [this](bool durationIsEstimate) {
        m_durationIsEstimate = durationIsEstimate;
        emit probeDone();
    });
    connect(m_backend,&IPlayerBackend::decoderOpenStarted, this, [this]() {
        m_loadingOverlay->setStage(LoadingOverlay::Stage::OpeningDecoder);
        emit decoderOpenStarted();
    });
    connect(m_backend,&IPlayerBackend::decoderOpenDone, this, [this]() {
        emit decoderOpenDone();
    });
    connect(m_backend,&IPlayerBackend::firstPacketRead, this, [this]() {
        emit firstPacketRead();
    });
    connect(m_backend,&IPlayerBackend::firstDecoderReceive, this, [this]() {
        m_loadingOverlay->setStage(LoadingOverlay::Stage::DecodingFirstFrame);
        emit firstDecoderReceive();
    });

    // HDR detection + chapters + per-device audio-offset auto-recall + duration
    // hoist (was buildUI lines 2030-2146 pre-refactor). Single mediaInfo
    // payload drives many subsystems — hence the long lambda body.
    connect(m_backend,&IPlayerBackend::mediaInfo, this, [this](const QJsonObject& info) {
        m_isHdr = info.value("hdr").toBool(false);

        // VIDEO_HUD_TIME_LABELS_FIX 2026-04-25 — duration hoist (probeDone
        // fires before mediaInfo; m_durationIsEstimate already cached).
        const double mediaDurSec = info.value("duration_sec").toDouble(0.0);
        if (mediaDurSec > 0.0) {
            m_durationSec = mediaDurSec;
            const QString durText = formatTime(static_cast<qint64>(mediaDurSec * 1000));
            if (m_durLabel) m_durLabel->setText(m_durationIsEstimate
                ? QStringLiteral("~") + durText
                : durText);
            if (m_timeLabel) m_timeLabel->setText(formatTime(0));
            if (m_seekBar) m_seekBar->setDurationSec(mediaDurSec);
        }

        // Batch 3.1 — forward color primaries / TRC to FrameCanvas shader.
        const int colorPri = info.value("color_primaries").toInt(0);
        const int colorTrc = info.value("color_trc").toInt(0);
        if (m_canvas) {
            m_canvas->setHdrColorInfo(colorPri, colorTrc);
        }

        m_chapters = info.value("chapters").toArray();
        if (!m_chapters.isEmpty())
            debugLog("[VideoPlayer] chapters: " + QString::number(m_chapters.size()));

        // VIDEO_PLAYER_FIX Batch 2.1 — chapter tick markers on seek slider.
        QList<qint64> chapterMs;
        chapterMs.reserve(m_chapters.size());
        for (const auto& c : m_chapters) {
            const double startSec = c.toObject().value("start").toDouble();
            chapterMs.append(static_cast<qint64>(startSec * 1000.0));
        }
        m_seekBar->setChapterMarkers(chapterMs);

        // Per-device audio offset auto-recall (Bluetooth defaults +
        // wired/unknown zeroing + one-time migration).
        constexpr int BT_DEFAULT_MS = 300;
        constexpr int OLD_BT_DEFAULT_MS = 200;

        QString device  = info.value("audio_device").toString();
        QString hostApi = info.value("audio_host_api").toString();
        if (!device.isEmpty()) {
            m_audioDeviceKey = makeDeviceKey(device, hostApi);
            // MAKE_MPV_SOLO Task 8.B (2026-05-02) — cache hostApi so the
            // mid-playback audio-device-change watcher can re-key on
            // device switch using the same host API tag the backend is
            // actually running on (sidecar=MME, mpv=wasapi typically).
            m_audioHostApi = hostApi;
            QString manualKey = m_audioDeviceKey + "/manual";
            QSettings s("Tankoban", "Tankoban");
            QVariant stored = s.value(m_audioDeviceKey);
            bool wasManual = s.value(manualKey, false).toBool();
            if (stored.isValid()) {
                m_audioDelayMs = stored.toInt();
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
            m_backend->sendSetAudioDelay(m_audioDelayMs);
        }
    });

    // D3D11 Holy Grail — sidecar shared D3D11 texture handle → FrameCanvas
    // zero-copy import path (eliminates GPU→CPU→GPU per frame).
    connect(m_backend,&IPlayerBackend::d3d11Texture, this,
        [this](quintptr handle, int w, int h) {
            debugLog(QString("[VideoPlayer] d3d11_texture handle=0x%1 %2x%3")
                        .arg(handle, 0, 16).arg(w).arg(h));
            m_canvas->attachD3D11Texture(handle, w, h);
        });
    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — subtitle overlay SHM.
    connect(m_backend,&IPlayerBackend::overlayShm, this,
        [this](const QString& name, int w, int h) {
            debugLog(QString("[VideoPlayer] overlay_shm name=%1 %2x%3")
                        .arg(name).arg(w).arg(h));
            m_canvas->attachOverlayShm(name, w, h);
        });

    // Frame stepping feedback — update time display.
    connect(m_backend,&IPlayerBackend::frameStepped, this, [this](double posSec) {
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
}

void VideoPlayer::switchBackendTo(BackendFactory::Type t)
{
    if (t == m_currentBackendType) return;  // no-op fast path

    debugLog(QString("[VideoPlayer] switchBackendTo: %1 → %2")
                .arg(BackendFactory::toString(m_currentBackendType))
                .arg(BackendFactory::toString(t)));

    if (m_backend) {
        // Stop the live backend cleanly (mirrors stopPlayback's user-close
        // teardown — sendStop + sendShutdown + ensureTerminated). The
        // synchronous 500ms wait is acceptable for a user-driven swap;
        // the right-click → "Play with X" gesture's latency budget tolerates it.
        if (m_backend->isRunning()) {
            m_backend->sendStop();
            m_backend->sendShutdown();
            m_backend->ensureTerminated(500);
        }
        // Disconnect every signal we wired (mirrors wireBackendSignals' set)
        // so any in-flight queued events from the dying backend can't deliver
        // to a half-deleted target. Belt-and-suspenders against deleteLater
        // racing with pending Qt::QueuedConnection deliveries.
        m_backend->disconnect(this);
        m_backend->deleteLater();
        m_backend = nullptr;
    }

    m_backend = BackendFactory::create(t, this);
    m_currentBackendType = t;

    // SubtitlePopover holds a non-signal IPlayerBackend* via setSidecar
    // (used by its embedded-track + load-from-file paths). Refresh it.
    if (m_subtitlePopover) m_subtitlePopover->setSidecar(m_backend);

    wireBackendSignals();

    // 2026-04-30 hotfix — toggle MpvVulkanWidget visibility + (re)wire its
    // mpv handle. Without this, swapping ffmpeg → mpv leaves m_mpvWidget
    // null + FrameCanvas visible: mpv plays audio but video has nowhere
    // to render (Hemanth-reported "mpv is blank" 2026-04-30 ~17:30).
    syncMpvIntegrationToBackend();
}

void VideoPlayer::syncMpvIntegrationToBackend()
{
#ifdef HAS_LIBMPV
    auto* mpvBackend = qobject_cast<MpvBackend*>(m_backend);
    if (!mpvBackend) {
        // Active backend is ffmpeg (or HAS_LIBMPV unset upstream).
        // Hide MpvVulkanWidget if it exists from a prior mpv session,
        // restore FrameCanvas as the active video surface.
        if (m_mpvWidget) {
            m_mpvWidget->setLibplaceboRenderer(nullptr);
            m_mpvWidget->setMpvHandle(nullptr);
            m_mpvWidget->hide();
        }
        if (m_canvas) m_canvas->show();
        applySurfaceOverlayStyle();
        return;
    }

    // Active backend is mpv. Lazy-create MpvVulkanWidget the first time
    // mpv is selected (works whether the initial ctor backend was mpv
    // or we just swapped from ffmpeg). The firstFrameRendered connect is
    // tied to the widget instance (not to a specific backend) so it's
    // wired exactly once at first creation.
    if (!m_mpvWidget) {
        m_mpvWidget = new MpvVulkanWidget(this);
        m_mpvWidget->setGeometry(0, 0, width(), height());
        connect(m_mpvWidget, &MpvVulkanWidget::firstFrameRendered,
                this, [this]() {
                    // Actual mpv frame reached the Vulkan swapchain.
                    m_firstFrameSeen = true;
                });
        // Agent 3 2026-05-02 — paired with the disabled buildUI connect
        // and the disabled mouseMoveEvent + nativeEvent overrides on
        // MpvVulkanWidget.
        // connect(m_mpvWidget, &MpvVulkanWidget::mouseActivityAt, this, [this](int /*y*/) {
        //     showControls();
        // });
    } else {
        // Re-show after a prior swap-away. Refresh geometry in case the
        // window was resized while we were on the FrameCanvas branch.
        m_mpvWidget->setGeometry(0, 0, width(), height());
    }

    // Backend-specific connects — must be re-made on every swap because
    // they capture the current mpvBackend pointer. The OLD backend's
    // connects are auto-disconnected by Qt when its QObject was deleted
    // in switchBackendTo above.
    //
    // ready() fires after MpvBackend::initializeMpv() — at that point the
    // mpv handle is valid + the widget can attach a render context. If
    // mpv is ALREADY initialized (mpvHandle non-null), attach immediately
    // rather than waiting for the next ready cycle.
    connect(mpvBackend, &IPlayerBackend::ready,
            this, [this, mpvBackend]() {
                if (m_mpvWidget) {
                    m_mpvWidget->setLibplaceboRenderer(mpvBackend->libplaceboRenderer());
                    m_mpvWidget->setMpvHandle(mpvBackend->mpvHandle());
                }
            });
    connect(mpvBackend, &MpvBackend::mpvHandleInvalidating,
            this, [this]() {
                if (m_mpvWidget) {
                    m_mpvWidget->setLibplaceboRenderer(nullptr);
                    m_mpvWidget->setMpvHandle(nullptr);
                }
            },
            Qt::DirectConnection);
    if (mpvBackend->mpvHandle()) {
        m_mpvWidget->setLibplaceboRenderer(mpvBackend->libplaceboRenderer());
        m_mpvWidget->setMpvHandle(mpvBackend->mpvHandle());
    }

    // Toggle render surface: hide FrameCanvas, show MpvVulkanWidget.
    if (m_canvas) m_canvas->hide();
    m_mpvWidget->show();
    applySurfaceOverlayStyle();

    // MAKE_MPV_BEAT_FFMPEG Task 2 (2026-05-02) — z-order fix. The Vulkan
    // widget is WA_NativeWindow, which means it owns a child HWND that
    // Windows renders above non-native QWidgets in its parent's backbuffer.
    // VideoPlayer's resizeEvent (line 3388-3394) explicitly raises the HUD
    // widgets to lift them above any sibling native HWND, but that chain
    // doesn't fire on widget show — only on resize. Without an explicit
    // raise here, m_mpvWidget's HWND covers the entire VideoPlayer
    // including controlBar/subOverlay/popovers/center-flash. The
    // ffmpeg-path equivalent (m_canvas) doesn't show this regression
    // because m_canvas is created BEFORE the HUD widgets in buildUI; HUD
    // widgets shown later naturally land above. m_mpvWidget is also
    // created in buildUI but show() happens in this function, possibly
    // post-HUD-creation, so explicit raise is the load-bearing call.
    if (m_controlBar)        m_controlBar->raise();
    if (m_subOverlay)        m_subOverlay->raise();
    if (m_volumeHud)         m_volumeHud->raise();
    if (m_centerFlash)       m_centerFlash->raise();
    if (m_toastHud)          m_toastHud->raise();
    if (m_statsBadge && m_statsBadge->isVisible()) m_statsBadge->raise();
    if (m_chromeOverlay && m_chromeOverlay->isVisible()) m_chromeOverlay->raise();
    if (m_playlistDrawer && m_playlistDrawer->isOpen()) m_playlistDrawer->raise();
#else
    // libmpv not compiled in — m_backend is always SidecarProcess.
    // FrameCanvas is the only video surface; nothing to toggle.
    if (m_canvas) m_canvas->show();
#endif
}

void VideoPlayer::applySurfaceOverlayStyle()
{
    if (!m_controlBar) return;

    bool mpvNativeSurface = false;
#ifdef HAS_LIBMPV
    mpvNativeSurface = qobject_cast<MpvBackend*>(m_backend) != nullptr;
#endif

    // Semi-transparent HUD works on the ffmpeg/FrameCanvas path, but the
    // Vulkan child HWND cannot be used as a Qt alpha-blend backing surface.
    // On mpv, keep the HUD panel fully opaque so empty HUD regions never show
    // the library/main window layer behind VideoPlayer.
    const QString background = mpvNativeSurface
        ? QStringLiteral("#0a0a0a")
        : QStringLiteral("rgba(10, 10, 10, 0.50)");

    m_controlBar->setStyleSheet(
        QStringLiteral(
            "QWidget#VideoControlBar {"
            "  background: %1;"
            "  border-top: 1px solid rgba(255, 255, 255, 0.08);"
            "}").arg(background));

    // MAKE_MPV_BEAT_FFMPEG Task 8 (2026-05-03) — fan-out the same backend-
    // aware-opaque pattern Codex established for VideoControlBar in Task 2
    // to ToastHud (transient text-toast widget). VolumeHud was abandoned
    // mid-Task-8 in favor of routing volume display through ToastHud per
    // Hemanth directive (the VolumeHud fade-window paint timing on the
    // mpv backend produced library bleed-through that multiple paint-
    // restructure attempts couldn't cleanly fix; ToastHud's QGraphicsOpacity
    // Effect-driven fade behaves differently and Hemanth confirmed it works
    // for the Speed toast). CenterFlash explicitly EXCLUDED — Hemanth had
    // Codex remove its black-blob backdrop in the 2026-04-25 minimalist
    // redesign; re-adding any backdrop (even mpv-conditional) would
    // conflict with that user-direction.
    if (m_toastHud) m_toastHud->setBackdropOpaque(mpvNativeSurface);
}
