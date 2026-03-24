#include "ui/player/VideoPlayer.h"
#include "ui/player/SidecarProcess.h"
#include "ui/player/ShmFrameReader.h"
#include "ui/player/FrameCanvas.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QJsonObject>
#include <QSvgRenderer>
#include <QPainter>
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

// ── Inline SVG icons ────────────────────────────────────────────────────────

static const QByteArray SVG_PLAY =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='white'>"
    "<polygon points='6,4 20,12 6,20'/>"
    "</svg>";

static const QByteArray SVG_PAUSE =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='white'>"
    "<rect x='5' y='4' width='4' height='16' rx='1'/>"
    "<rect x='15' y='4' width='4' height='16' rx='1'/>"
    "</svg>";

static const QByteArray SVG_BACK =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='white' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<line x1='19' y1='12' x2='5' y2='12'/>"
    "<polyline points='12,19 5,12 12,5'/>"
    "</svg>";

// ── Constructor ─────────────────────────────────────────────────────────────

VideoPlayer::VideoPlayer(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: #000000;");

    m_playIcon  = iconFromSvg(SVG_PLAY);
    m_pauseIcon = iconFromSvg(SVG_PAUSE);
    m_backIcon  = iconFromSvg(SVG_BACK);

    m_sidecar = new SidecarProcess(this);
    m_reader  = new ShmFrameReader();

    buildUI();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(3000);
    connect(&m_hideTimer, &QTimer::timeout, this, &VideoPlayer::hideControls);

    // Sidecar events
    connect(m_sidecar, &SidecarProcess::ready,        this, &VideoPlayer::onSidecarReady);
    connect(m_sidecar, &SidecarProcess::firstFrame,   this, &VideoPlayer::onFirstFrame);
    connect(m_sidecar, &SidecarProcess::timeUpdate,   this, &VideoPlayer::onTimeUpdate);
    connect(m_sidecar, &SidecarProcess::stateChanged,  this, &VideoPlayer::onStateChanged);
    connect(m_sidecar, &SidecarProcess::endOfFile,    this, &VideoPlayer::onEndOfFile);
    connect(m_sidecar, &SidecarProcess::errorOccurred, this, &VideoPlayer::onError);
}

VideoPlayer::~VideoPlayer()
{
    m_canvas->stopPolling();
    m_reader->detach();
    delete m_reader;
}

// ── Public ──────────────────────────────────────────────────────────────────

void VideoPlayer::openFile(const QString& filePath)
{
    debugLog("[VideoPlayer] openFile: " + filePath);

    // Stop any current playback
    stopPlayback();

    m_pendingFile = filePath;
    m_pendingStartSec = 0.0;
    m_paused = false;
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

    if (m_sidecar->isRunning())
        m_sidecar->sendStop();
}

// ── Sidecar event handlers ──────────────────────────────────────────────────

void VideoPlayer::onSidecarReady()
{
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

    m_durationMs = static_cast<qint64>(durationSec * 1000);
    qint64 posMs = static_cast<qint64>(positionSec * 1000);

    m_seekBar->blockSignals(true);
    m_seekBar->setRange(0, static_cast<int>(durationSec));
    m_seekBar->setValue(static_cast<int>(positionSec));
    m_seekBar->blockSignals(false);

    m_timeLabel->setText(formatTime(posMs) + " / " + formatTime(m_durationMs));
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
    m_paused = true;
    updatePlayPauseIcon();
    showControls();
}

void VideoPlayer::onError(const QString& message)
{
    m_timeLabel->setText(message);
}

// ── UI ──────────────────────────────────────────────────────────────────────

void VideoPlayer::buildUI()
{
    m_canvas = new FrameCanvas(this);

    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName("VideoControlBar");
    m_controlBar->setFixedHeight(52);
    m_controlBar->setStyleSheet(
        "QWidget#VideoControlBar {"
        "  background: rgba(8, 8, 8, 0.85);"
        "  border-top: 1px solid rgba(255, 255, 255, 0.10);"
        "}"
    );

    auto* layout = new QHBoxLayout(m_controlBar);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    auto btnStyle =
        "QPushButton { background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.10); border-radius: 8px;"
        "  padding: 0; }"
        "QPushButton:hover { background: rgba(255,255,255,0.14); }";

    m_backBtn = new QPushButton(m_controlBar);
    m_backBtn->setIcon(m_backIcon);
    m_backBtn->setIconSize(QSize(18, 18));
    m_backBtn->setFixedSize(42, 30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(btnStyle);
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        stopPlayback();
        emit closeRequested();
    });

    m_playPauseBtn = new QPushButton(m_controlBar);
    m_playPauseBtn->setIcon(m_pauseIcon);
    m_playPauseBtn->setIconSize(QSize(18, 18));
    m_playPauseBtn->setFixedSize(36, 30);
    m_playPauseBtn->setCursor(Qt::PointingHandCursor);
    m_playPauseBtn->setStyleSheet(btnStyle);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &VideoPlayer::togglePause);

    m_seekBar = new QSlider(Qt::Horizontal, m_controlBar);
    m_seekBar->setRange(0, 1000);
    m_seekBar->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  background: rgba(255,255,255,0.15); height: 4px; border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #ffffff; width: 12px; height: 12px;"
        "  margin: -4px 0; border-radius: 6px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: rgba(255,255,255,0.55); border-radius: 2px;"
        "}"
    );
    connect(m_seekBar, &QSlider::sliderPressed, this, [this]() { m_seeking = true; });
    connect(m_seekBar, &QSlider::sliderReleased, this, [this]() {
        m_seeking = false;
        m_sidecar->sendSeek(static_cast<double>(m_seekBar->value()));
    });

    m_timeLabel = new QLabel("0:00 / 0:00", m_controlBar);
    m_timeLabel->setStyleSheet(
        "color: rgba(255,255,255,0.70); font-size: 11px; font-family: monospace;"
    );
    m_timeLabel->setFixedWidth(110);

    layout->addWidget(m_backBtn);
    layout->addWidget(m_playPauseBtn);
    layout->addWidget(m_seekBar, 1);
    layout->addWidget(m_timeLabel);
}

// ── Controls ────────────────────────────────────────────────────────────────

void VideoPlayer::togglePause()
{
    if (m_paused)
        m_sidecar->sendResume();
    else
        m_sidecar->sendPause();
    // State will update via onStateChanged callback
    showControls();
}

void VideoPlayer::updatePlayPauseIcon()
{
    m_playPauseBtn->setIcon(m_paused ? m_playIcon : m_pauseIcon);
}

void VideoPlayer::showControls()
{
    m_controlBar->show();
    setCursor(Qt::ArrowCursor);
    m_hideTimer.start();
}

void VideoPlayer::hideControls()
{
    if (m_paused) return;
    m_controlBar->hide();
    setCursor(Qt::BlankCursor);
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
    m_controlBar->setGeometry(0, height() - m_controlBar->height(), width(), m_controlBar->height());
    m_controlBar->raise();
}

// ── Input ───────────────────────────────────────────────────────────────────

void VideoPlayer::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        stopPlayback();
        emit closeRequested();
        break;
    case Qt::Key_Space:
        togglePause();
        break;
    case Qt::Key_Left:
        m_sidecar->sendSeek(qMax(0.0, m_seekBar->value() - 10.0));
        showControls();
        break;
    case Qt::Key_Right:
        m_sidecar->sendSeek(m_seekBar->value() + 10.0);
        showControls();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void VideoPlayer::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    showControls();
}
