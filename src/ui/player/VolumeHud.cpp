#include "ui/player/VolumeHud.h"

#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

// Speaker SVG icons — film noir style, white strokes on transparent
static const QByteArray SVG_SPEAKER_HIGH =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='white' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>"
    "<polygon points='11,5 6,9 2,9 2,15 6,15 11,19' fill='rgba(255,255,255,0.15)'/>"
    "<path d='M19.07 4.93a10 10 0 0 1 0 14.14'/>"
    "<path d='M15.54 8.46a5 5 0 0 1 0 7.07'/>"
    "</svg>";

static const QByteArray SVG_SPEAKER_MID =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='white' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>"
    "<polygon points='11,5 6,9 2,9 2,15 6,15 11,19' fill='rgba(255,255,255,0.15)'/>"
    "<path d='M15.54 8.46a5 5 0 0 1 0 7.07'/>"
    "</svg>";

static const QByteArray SVG_SPEAKER_LOW =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='white' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>"
    "<polygon points='11,5 6,9 2,9 2,15 6,15 11,19' fill='rgba(255,255,255,0.15)'/>"
    "</svg>";

static const QByteArray SVG_SPEAKER_MUTE =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
    "stroke='white' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>"
    "<polygon points='11,5 6,9 2,9 2,15 6,15 11,19' fill='rgba(255,255,255,0.08)'/>"
    "<line x1='23' y1='9' x2='17' y2='15'/>"
    "<line x1='17' y1='9' x2='23' y2='15'/>"
    "</svg>";

VolumeHud::VolumeHud(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(160, 32);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(1200);

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);

    connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
        m_fadeAnim->stop();
        m_fadeAnim->setDuration(200);
        m_fadeAnim->setStartValue(m_opacity);
        m_fadeAnim->setEndValue(0.0);
        disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
        connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
            if (m_opacity <= 0.01) hide();
        });
        m_fadeAnim->start();
    });
}

void VolumeHud::showVolume(int percent, bool muted)
{
    // Batch 4.2 — accept up to 200%. The percentage text rendered on
    // the right-hand side already shows the raw value (e.g. "150%"),
    // so the amp zone is immediately discoverable in the existing UX
    // with zero new layout work. Fill bar clamps visually below to
    // prevent overflow past the bar rectangle.
    m_percent = qBound(0, percent, 200);
    m_muted   = muted;

    // Position center-bottom of parent, above control bar
    if (parentWidget()) {
        QWidget* bar = parentWidget()->findChild<QWidget*>("VideoControlBar");
        int barH = (bar && bar->isVisible()) ? bar->sizeHint().height() : 0;
        int px = (parentWidget()->width()  - width())  / 2;
        int py = parentWidget()->height() - barH - height() - 18;
        move(px, py);
    }

    // Skip fade-in if already fully visible — just restart hold timer
    if (isVisible() && m_opacity >= 0.98) {
        m_hideTimer.start(1200);
        update();
        return;
    }

    // 120ms fade-in
    m_fadeAnim->stop();
    setOpacity(0.0);
    show();
    raise();

    m_fadeAnim->setDuration(120);
    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        m_hideTimer.start();
    });
    m_fadeAnim->start();
    update();
}

void VolumeHud::setOpacity(qreal o)
{
    m_opacity = o;
    update();
}

void VolumeHud::paintEvent(QPaintEvent*)
{
    if (m_opacity <= 0.01) return;

    QPainter p(this);
    p.setOpacity(m_opacity);
    p.setRenderHint(QPainter::Antialiasing);

    // Background pill
    QPainterPath bg;
    bg.addRoundedRect(QRectF(0, 0, width(), height()), 6, 6);
    p.fillPath(bg, QColor(10, 10, 10, 218));
    p.setPen(QPen(QColor(255, 255, 255, 46), 1));
    p.drawPath(bg);

    // Speaker icon (left side)
    const QByteArray& svg = m_muted ? SVG_SPEAKER_MUTE
                          : m_percent > 66 ? SVG_SPEAKER_HIGH
                          : m_percent > 33 ? SVG_SPEAKER_MID
                          : SVG_SPEAKER_LOW;

    QSvgRenderer renderer(svg);
    QRectF iconRect(10, 6, 24, 24);
    renderer.render(&p, iconRect);

    // Volume bar (center)
    int barX = 40;
    int barY = 14;
    int barW = 86;
    int barH = 8;
    // Batch 4.2 — clamp fill width to the bar rectangle. Values > 100%
    // (amp zone) visually cap at full bar; the numeric "150%" / "200%"
    // text to the right communicates the amp level.
    int fillW = m_muted ? 0 : qMin(barW, barW * m_percent / 100);

    // Bar background
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 20, 20, 230));
    p.drawRoundedRect(barX, barY, barW, barH, 3, 3);

    // Bar fill — warm white
    if (fillW > 0) {
        p.setBrush(QColor(214, 194, 164, 240));
        p.drawRoundedRect(barX, barY, fillW, barH, 3, 3);
    }

    // Percentage text (right side)
    p.setPen(QColor(245, 245, 245, 250));
    QFont f = font();
    f.setPixelSize(11);
    f.setFamily("monospace");
    p.setFont(f);

    QString text = m_muted ? "MUTE" : QString::number(m_percent) + "%";
    p.drawText(QRect(130, 0, 30, height()), Qt::AlignCenter, text);
}
