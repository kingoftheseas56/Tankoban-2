#include "ui/player/VideoPlayer.h"
#include "ui/player/SyncClock.h"
#include "ui/player/FfmpegDecoder.h"
#include "ui/player/AudioDecoder.h"
#include "ui/player/FrameCanvas.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>

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

    m_clock   = new SyncClock();
    m_decoder = new FfmpegDecoder(m_clock, this);
    m_audio   = new AudioDecoder(m_clock, this);

    buildUI();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(3000);
    connect(&m_hideTimer, &QTimer::timeout, this, &VideoPlayer::hideControls);

    connect(m_decoder, &FfmpegDecoder::frameReady,
            m_canvas,  &FrameCanvas::setFrame);
    connect(m_decoder, &FfmpegDecoder::positionChanged,
            this, &VideoPlayer::onPositionChanged);
    connect(m_decoder, &FfmpegDecoder::playbackFinished,
            this, &VideoPlayer::onPlaybackFinished);
    connect(m_decoder, &FfmpegDecoder::errorOccurred,
            this, [this](const QString& msg) { m_timeLabel->setText(msg); });
}

VideoPlayer::~VideoPlayer()
{
    m_audio->stop();
    m_decoder->stop();
    delete m_clock;
}

// ── Public ──────────────────────────────────────────────────────────────────

void VideoPlayer::openFile(const QString& filePath)
{
    m_audio->stop();
    m_decoder->stop();

    if (!m_decoder->openFile(filePath))
        return;

    // Audio may fail (silent video) — that's OK
    m_audio->openFile(filePath);

    m_duration = m_decoder->durationMs();
    m_seekBar->setRange(0, static_cast<int>(m_duration / 1000));
    m_seekBar->setValue(0);
    m_timeLabel->setText(formatTime(0) + " / " + formatTime(m_duration));
    m_paused = false;
    updatePlayPauseIcon();

    // Start audio first — it drives the clock
    m_audio->play();
    m_decoder->play();
    showControls();
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
        m_audio->stop();
        m_decoder->stop();
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
        onSeek(m_seekBar->value());
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
    m_paused = !m_paused;
    m_clock->setPaused(m_paused);
    m_decoder->togglePause();
    m_audio->pause();
    if (!m_paused) m_audio->play();
    updatePlayPauseIcon();
    showControls();
}

void VideoPlayer::updatePlayPauseIcon()
{
    m_playPauseBtn->setIcon(m_paused ? m_playIcon : m_pauseIcon);
}

void VideoPlayer::onPositionChanged(qint64 ptsMs)
{
    if (m_seeking) return;
    int secs = static_cast<int>(ptsMs / 1000);
    m_seekBar->blockSignals(true);
    m_seekBar->setValue(secs);
    m_seekBar->blockSignals(false);
    m_timeLabel->setText(formatTime(ptsMs) + " / " + formatTime(m_duration));
}

void VideoPlayer::onSeek(int sliderValue)
{
    qint64 ms = static_cast<qint64>(sliderValue) * 1000;
    m_audio->seek(ms);
    m_decoder->seek(ms);
}

void VideoPlayer::onPlaybackFinished()
{
    m_paused = true;
    m_audio->stop();
    updatePlayPauseIcon();
    showControls();
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
        m_audio->stop();
        m_decoder->stop();
        emit closeRequested();
        break;
    case Qt::Key_Space:
        togglePause();
        break;
    case Qt::Key_Left: {
        qint64 ms = qMax(0LL, m_seekBar->value() * 1000LL - 10000);
        m_audio->seek(ms);
        m_decoder->seek(ms);
        showControls();
        break;
    }
    case Qt::Key_Right: {
        qint64 ms = m_seekBar->value() * 1000LL + 10000;
        m_audio->seek(ms);
        m_decoder->seek(ms);
        showControls();
        break;
    }
    default:
        QWidget::keyPressEvent(event);
    }
}

void VideoPlayer::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    showControls();
}
