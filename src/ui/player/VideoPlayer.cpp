#include "ui/player/VideoPlayer.h"
#include "ui/player/KeyBindings.h"
#include "ui/player/SidecarProcess.h"
#include "ui/player/ShmFrameReader.h"
#include "ui/player/FrameCanvas.h"
#include "ui/player/VolumeHud.h"
#include "ui/player/CenterFlash.h"
#include "ui/player/ShortcutsOverlay.h"
#include "ui/player/PlaylistDrawer.h"
#include "ui/player/ToastHud.h"
#include "ui/player/EqualizerPopover.h"
#include "ui/player/FilterPopover.h"
#include "ui/player/TrackPopover.h"
#include "ui/player/SubtitleOverlay.h"
#include "ui/player/SeekSlider.h"
#include "ui/player/VideoContextMenu.h"
#include "core/CoreBridge.h"

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
#include <QFileInfo>
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

    // Sidecar events
    connect(m_sidecar, &SidecarProcess::ready,        this, &VideoPlayer::onSidecarReady);
    connect(m_sidecar, &SidecarProcess::firstFrame,   this, &VideoPlayer::onFirstFrame);
    connect(m_sidecar, &SidecarProcess::timeUpdate,   this, &VideoPlayer::onTimeUpdate);
    connect(m_sidecar, &SidecarProcess::stateChanged,  this, &VideoPlayer::onStateChanged);
    connect(m_sidecar, &SidecarProcess::tracksChanged,  this, &VideoPlayer::onTracksChanged);
    connect(m_sidecar, &SidecarProcess::endOfFile,    this, &VideoPlayer::onEndOfFile);
    connect(m_sidecar, &SidecarProcess::errorOccurred, this, &VideoPlayer::onError);

    // Subtitle overlay signals (Batch H)
    connect(m_sidecar, &SidecarProcess::subtitleText, this, [this](const QString& text) {
        m_subOverlay->setText(text);
    });
    connect(m_sidecar, &SidecarProcess::subVisibilityChanged, this, [this](bool visible) {
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
                            const QStringList& playlist, int playlistIndex)
{
    debugLog("[VideoPlayer] openFile: " + filePath);

    // Stop any current playback
    stopPlayback();

    m_currentFile = filePath;
    m_currentVideoId = videoIdForFile(filePath);
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
    // Repopulate playlist drawer (reflects new current episode)
    if (m_playlistDrawer)
        m_playlistDrawer->populate(m_playlist, m_playlistIdx);
    updateEpisodeButtons();

    // Check for saved progress — resume from last position
    m_pendingStartSec = 0.0;
    if (m_bridge) {
        QJsonObject prog = m_bridge->progress("videos", m_currentVideoId);
        double savedPos = prog.value("positionSec").toDouble(0);
        double savedDur = prog.value("durationSec").toDouble(0);
        // Resume if we're not near the end (within 95%)
        if (savedPos > 2.0 && savedDur > 0 && savedPos < savedDur * 0.95)
            m_pendingStartSec = savedPos;
    }
    updatePlayPauseIcon();

    if (m_sidecar->isRunning()) {
        debugLog("[VideoPlayer] sidecar already running, sending open directly");
        m_sidecar->sendOpen(filePath);
    } else {
        debugLog("[VideoPlayer] starting sidecar...");
        m_sidecar->start();
    }

    showControls();
}

void VideoPlayer::stopPlayback()
{
    m_canvas->stopPolling();
    m_canvas->detachShm();
    m_reader->detach();

    if (m_sidecar->isRunning()) {
        m_sidecar->sendStop();
        // Give sidecar a moment to stop audio, then shut it down
        m_sidecar->sendShutdown();
    }
}

// ── Sidecar event handlers ──────────────────────────────────────────────────

void VideoPlayer::onSidecarReady()
{
    // Per-device audio offset is now applied in the mediaInfo handler
    // (which fires after open() reports the active audio device).
    if (!m_pendingFile.isEmpty()) {
        m_sidecar->sendOpen(m_pendingFile, m_pendingStartSec);
    }
}

void VideoPlayer::onFirstFrame(const QJsonObject& payload)
{
    debugLog("[VideoPlayer] onFirstFrame: " + QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QString shmName  = payload["shmName"].toString();
    int slotCount    = payload["slotCount"].toInt(4);
    int w            = payload["width"].toInt();
    int h            = payload["height"].toInt();
    int slotBytes    = payload["slotBytes"].toInt(w * h * 4);

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
    }
}

void VideoPlayer::onEndOfFile()
{
    // Auto-advance to next episode if available and enabled
    if (!m_playlist.isEmpty() && m_playlistIdx < m_playlist.size() - 1
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

// ── UI ──────────────────────────────────────────────────────────────────────

void VideoPlayer::buildUI()
{
    m_canvas = new FrameCanvas(this);

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

    // Chip button style (3-stop gradient)
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
        "  stop:1 rgba(36,36,36,0.98)); }";

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
            m_timeBubble->setText(formatTime(static_cast<qint64>(sec * 1000)));
            QRect sliderGeo = m_seekBar->geometry();
            QPoint barPos = m_controlBar->pos();
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
    connect(m_filtersChip, &QPushButton::clicked, this, [this]() {
        m_filterPopover->toggle(m_filtersChip);
    });

    m_eqChip = new QPushButton("EQ", m_controlBar);
    m_eqChip->setCursor(Qt::PointingHandCursor);
    m_eqChip->setFocusPolicy(Qt::NoFocus);
    m_eqChip->setStyleSheet(chipStyle);
    connect(m_eqChip, &QPushButton::clicked, this, [this]() {
        m_eqPopover->toggle(m_eqChip);
    });

    m_trackChip = new QPushButton("Tracks", m_controlBar);
    m_trackChip->setCursor(Qt::PointingHandCursor);
    m_trackChip->setFocusPolicy(Qt::NoFocus);
    m_trackChip->setStyleSheet(chipStyle);
    connect(m_trackChip, &QPushButton::clicked, this, [this]() {
        m_trackPopover->setStyle(m_trackPopover->subFontSize(),
                                 m_trackPopover->subMargin(),
                                 m_trackPopover->subOutline());
        m_trackPopover->toggle(m_trackChip);
    });

    m_playlistChip = new QPushButton("List", m_controlBar);
    m_playlistChip->setCursor(Qt::PointingHandCursor);
    m_playlistChip->setFocusPolicy(Qt::NoFocus);
    m_playlistChip->setStyleSheet(chipStyle);
    connect(m_playlistChip, &QPushButton::clicked, this, &VideoPlayer::togglePlaylistDrawer);

    ctrlRow->addWidget(m_backBtn);
    ctrlRow->addSpacing(8);
    ctrlRow->addWidget(m_prevEpisodeBtn);
    ctrlRow->addWidget(m_playPauseBtn);
    ctrlRow->addWidget(m_nextEpisodeBtn);
    ctrlRow->addSpacing(8);
    ctrlRow->addStretch(1);
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

    // Shortcuts overlay (? key — full-screen modal card)
    m_shortcutsOverlay = new ShortcutsOverlay(this);

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
        m_toastHud->showToast("Audio: track " + idStr);
    });
    connect(m_trackPopover, &TrackPopover::subtitleTrackSelected, this, [this](int id) {
        if (id == 0) {
            m_sidecar->sendSetTracks("", "off");
            m_toastHud->showToast("Subtitles off");
        } else {
            QString idStr = QString::number(id);
            m_sidecar->sendSetTracks("", idStr);
            // Save preferred subtitle language globally
            QString lang = langForTrackId(m_subTracks, idStr);
            if (!lang.isEmpty())
                QSettings("Tankoban", "Tankoban").setValue("video_preferred_sub_lang", lang);
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
            openFile(m_playlist[idx], m_playlist, idx);
        }
    });

    // Toast HUD (transient messages — speed, mute, track changes, errors)
    m_toastHud = new ToastHud(this);

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
        m_eqChip->setText(m_eqPopover->isActive() ? "EQ (on)" : "EQ");
    });
    connect(m_eqPopover, &EqualizerPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) { m_hideTimer.stop(); showControls(); }
        else if (!m_paused) m_hideTimer.start();
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
        });
    connect(m_filterPopover, &FilterPopover::hoverChanged, this, [this](bool hovered) {
        if (hovered) {
            m_hideTimer.stop();
            showControls();
        } else if (!m_paused) {
            m_hideTimer.start();
        }
    });
    // HDR tone mapping: wire FilterPopover ↔ sidecar
    connect(m_filterPopover, &FilterPopover::toneMappingChanged, this,
        [this](const QString& algorithm, bool peakDetect) {
            m_sidecar->sendSetToneMapping(algorithm, peakDetect);
        });
    // HDR detection + chapters from media_info event
    connect(m_sidecar, &SidecarProcess::mediaInfo, this, [this](const QJsonObject& info) {
        m_isHdr = info.value("hdr").toBool(false);
        m_filterPopover->setHdrMode(m_isHdr);
        m_chapters = info.value("chapters").toArray();
        if (!m_chapters.isEmpty())
            debugLog("[VideoPlayer] chapters: " + QString::number(m_chapters.size()));

        // Per-device audio offset auto-recall.
        // Sidecar reports the current audio output device (PortAudio name + host API).
        // We compute a stable QSettings key per device and look up the user's
        // calibrated offset. New Bluetooth devices get a 200ms default + a one-time
        // toast so the user knows to fine-tune.
        QString device  = info.value("audio_device").toString();
        QString hostApi = info.value("audio_host_api").toString();
        if (!device.isEmpty()) {
            m_audioDeviceKey = makeDeviceKey(device, hostApi);
            QSettings s("Tankoban", "Tankoban");
            QVariant stored = s.value(m_audioDeviceKey);
            if (stored.isValid()) {
                m_audioDelayMs = stored.toInt();
                debugLog(QString("[VideoPlayer] audio device '%1' → recalled offset %2ms")
                            .arg(device).arg(m_audioDelayMs));
            } else if (looksLikeBluetooth(device)) {
                m_audioDelayMs = 200;
                s.setValue(m_audioDeviceKey, 200);
                if (m_toastHud) {
                    m_toastHud->showToast(
                        "Bluetooth audio detected — using 200ms offset.\n"
                        "Use Ctrl+= / Ctrl+- to fine-tune.");
                }
                debugLog(QString("[VideoPlayer] Bluetooth device '%1' → default 200ms").arg(device));
            } else {
                m_audioDelayMs = 0;
                debugLog(QString("[VideoPlayer] wired/unknown device '%1' → no offset").arg(device));
            }
            m_sidecar->sendSetAudioDelay(m_audioDelayMs);
        }
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

void VideoPlayer::onTracksChanged(const QJsonArray& audio, const QJsonArray& subtitle,
                                   const QString& activeAudioId, const QString& activeSubId)
{
    m_audioTracks   = audio;
    m_subTracks     = subtitle;
    m_activeAudioId = activeAudioId;
    m_activeSubId   = activeSubId;

    // Update track chip label with counts
    m_trackChip->setText("Tracks");

    // Merge into a single array for TrackPopover (expects "type" field on each track)
    QJsonArray merged;
    for (const auto& v : audio) {
        QJsonObject t = v.toObject();
        t["type"] = "audio";
        merged.append(t);
    }
    for (const auto& v : subtitle) {
        QJsonObject t = v.toObject();
        t["type"] = "subtitle";
        merged.append(t);
    }
    int audioId = activeAudioId.toInt();
    int subId   = activeSubId.toInt();
    m_trackPopover->populate(merged, audioId, subId, m_subsVisible);

    // Restore saved track preferences (carry-forward > per-file > global > default)
    restoreTrackPreferences();
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
    QString lang = track["lang"].toString();
    if (lang.isEmpty()) lang = track["title"].toString();
    if (lang.isEmpty()) lang = QString::number(idx + 1);
    m_toastHud->showToast("Subtitle: " + lang);
}

void VideoPlayer::toggleSubtitles()
{
    m_subsVisible = !m_subsVisible;
    m_sidecar->sendSetSubVisibility(m_subsVisible);
    m_toastHud->showToast(m_subsVisible ? "Subtitles on" : "Subtitles off");
}

void VideoPlayer::prevEpisode()
{
    if (m_playlist.isEmpty() || m_playlistIdx <= 0) return;
    // Carry forward current track language preferences
    m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
    m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
    openFile(m_playlist[m_playlistIdx - 1], m_playlist, m_playlistIdx - 1);
}

void VideoPlayer::nextEpisode()
{
    if (m_playlist.isEmpty() || m_playlistIdx >= m_playlist.size() - 1) return;
    // Carry forward current track language preferences
    m_carryAudioLang = langForTrackId(m_audioTracks, m_activeAudioId);
    m_carrySubLang = langForTrackId(m_subTracks, m_activeSubId);
    openFile(m_playlist[m_playlistIdx + 1], m_playlist, m_playlistIdx + 1);
}

void VideoPlayer::togglePlaylistDrawer()
{
    if (m_playlist.isEmpty()) return;
    m_playlistDrawer->toggle();
    if (m_playlistDrawer->isOpen()) {
        showControls();
        m_hideTimer.stop();
    }
}

void VideoPlayer::adjustVolume(int delta)
{
    m_volume = qBound(0, m_volume + delta, 100);
    if (m_muted && delta > 0) {
        m_muted = false;
        m_sidecar->sendSetMute(false);
    }
    m_sidecar->sendSetVolume(m_volume / 100.0);
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
    data["subsVisible"]  = m_subsVisible;
    data["subDelayMs"]   = m_subDelayMs;
    m_bridge->saveProgress("videos", m_currentVideoId, data);
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
    // Priority: carry-forward > per-file > global > sidecar default
    QString targetAudioLang, targetSubLang;

    if (!m_carryAudioLang.isEmpty()) {
        targetAudioLang = m_carryAudioLang;
        targetSubLang = m_carrySubLang;
        m_carryAudioLang.clear();
        m_carrySubLang.clear();
    } else if (m_bridge) {
        QJsonObject prog = m_bridge->progress("videos", m_currentVideoId);
        targetAudioLang = prog.value("audioLang").toString();
        targetSubLang = prog.value("subtitleLang").toString();

        // Restore per-file subtitle visibility and delay
        if (prog.contains("subsVisible")) {
            bool vis = prog.value("subsVisible").toBool(true);
            if (vis != m_subsVisible) {
                m_subsVisible = vis;
                m_sidecar->sendSetSubVisibility(vis);
            }
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

    // Fall back to global preferred languages
    QSettings settings("Tankoban", "Tankoban");
    if (targetAudioLang.isEmpty())
        targetAudioLang = settings.value("video_preferred_audio_lang").toString();
    if (targetSubLang.isEmpty())
        targetSubLang = settings.value("video_preferred_sub_lang").toString();

    // Match by language in available tracks
    QString audioId = findTrackByLang(m_audioTracks, targetAudioLang);
    QString subId = findTrackByLang(m_subTracks, targetSubLang);

    if ((!audioId.isEmpty() && audioId != m_activeAudioId) ||
        (!subId.isEmpty() && subId != m_activeSubId)) {
        m_sidecar->sendSetTracks(
            audioId.isEmpty() ? "" : audioId,
            subId.isEmpty() ? "" : subId);
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

    // Shortcuts overlay covers full viewport
    m_shortcutsOverlay->setGeometry(0, 0, width(), height());

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

    // Z-order: FrameCanvas → controlBar → subOverlay (above HUD) → transient overlays → drawer → shortcuts
    m_controlBar->raise();
    m_subOverlay->reposition();
    m_subOverlay->raise();
    m_volumeHud->raise();
    m_centerFlash->raise();
    if (m_playlistDrawer->isOpen())      m_playlistDrawer->raise();
    if (m_shortcutsOverlay->isShowing()) m_shortcutsOverlay->raise();
}

// ── Input ───────────────────────────────────────────────────────────────────

void VideoPlayer::keyPressEvent(QKeyEvent* event)
{
    // Shortcuts overlay intercepts all keys — only ? and Esc close it
    if (m_shortcutsOverlay->isShowing()) {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Question)
            m_shortcutsOverlay->toggle();
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
        m_sidecar->sendSeek(qMax(0.0, curSec + delta));
        m_centerFlash->flash(delta > 0 ? SVG_SEEK_FWD : SVG_SEEK_BACK);
        showControls();
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
        if (!m_audioDeviceKey.isEmpty())
            QSettings("Tankoban", "Tankoban").setValue(m_audioDeviceKey, m_audioDelayMs);
        m_toastHud->showToast(QString("Audio delay: %1ms").arg(m_audioDelayMs));
    }
    else if (action == "audio_delay_plus") {
        m_audioDelayMs += 50;
        m_sidecar->sendSetAudioDelay(m_audioDelayMs);
        if (!m_audioDeviceKey.isEmpty())
            QSettings("Tankoban", "Tankoban").setValue(m_audioDeviceKey, m_audioDelayMs);
        m_toastHud->showToast(QString("Audio delay: %1ms").arg(m_audioDelayMs));
    }
    else if (action == "audio_delay_reset") {
        m_audioDelayMs = 0;
        m_sidecar->sendSetAudioDelay(0);
        if (!m_audioDeviceKey.isEmpty())
            QSettings("Tankoban", "Tankoban").setValue(m_audioDeviceKey, 0);
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
    else if (action == "toggle_playlist")    togglePlaylistDrawer();
    else if (action == "show_shortcuts") {
        m_shortcutsOverlay->toggle();
        m_hideTimer.stop();
    }
    else if (action == "back_to_library") {
        if (m_fullscreen) toggleFullscreen();
        else emit closeRequested();
    }
    else if (action == "back_fullscreen") {
        if (m_fullscreen) toggleFullscreen();
        emit closeRequested();
    }
}

void VideoPlayer::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    setFocus(Qt::OtherFocusReason);
}

void VideoPlayer::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);

    // Close any open popover/drawer when clicking outside of them
    bool closedSomething = false;
    if (m_filterPopover && m_filterPopover->isOpen() &&
        !m_filterPopover->geometry().contains(event->pos()) &&
        !m_filtersChip->geometry().contains(event->pos())) {
        m_filterPopover->hide();
        closedSomething = true;
    }
    if (m_trackPopover && m_trackPopover->isOpen() &&
        !m_trackPopover->geometry().contains(event->pos()) &&
        !m_trackChip->geometry().contains(event->pos())) {
        m_trackPopover->hide();
        closedSomething = true;
    }
    if (m_playlistDrawer && m_playlistDrawer->isOpen() &&
        !m_playlistDrawer->geometry().contains(event->pos()) &&
        !m_playlistChip->geometry().contains(event->pos())) {
        m_playlistDrawer->hide();
        closedSomething = true;
    }

    if (!closedSomething)
        QWidget::mousePressEvent(event);
}

void VideoPlayer::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    showControls();
}

void VideoPlayer::mouseDoubleClickEvent(QMouseEvent* event)
{
    QWidget::mouseDoubleClickEvent(event);
    toggleFullscreen();
}

void VideoPlayer::wheelEvent(QWheelEvent* event)
{
    int delta = event->angleDelta().y() > 0 ? 5 : -5;
    adjustVolume(delta);
    event->accept();
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
        case VideoContextMenu::ToggleFullscreen: toggleFullscreen(); break;
        case VideoContextMenu::SetAudioTrack:
            m_sidecar->sendSetTracks(QString::number(v.toInt()), "");
            m_toastHud->showToast("Audio: track " + QString::number(v.toInt()));
            break;
        case VideoContextMenu::SetSubtitleTrack:
            if (v.toInt() == 0) {
                m_sidecar->sendSetTracks("", "off");
                m_toastHud->showToast("Subtitles off");
            } else {
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
