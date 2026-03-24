#include "GlassBackground.h"

#include <QPainter>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QApplication>
#include <cmath>

GlassBackground::GlassBackground(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);

    // Subtle neutral blobs for noir aesthetic
    m_blobs = {
        {0.12f, 0.08f, 1200, QColor(140, 140, 140, 20)},
        {0.88f, 0.18f, 1000, QColor(148, 163, 184, 16)},
        {0.50f, 0.90f, 1100, QColor(120, 120, 120, 14)},
    };

    m_timer.setInterval(50); // ~20fps
    connect(&m_timer, &QTimer::timeout, this, &GlassBackground::tick);
}

void GlassBackground::setBlobs(const std::vector<GlassBlob>& blobs) {
    m_blobs = blobs;
    update();
}

void GlassBackground::setBaseColor(const QColor& color) {
    m_baseColor = color;
    update();
}

void GlassBackground::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_timer.isActive())
        m_timer.start();
}

void GlassBackground::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    m_timer.stop();
}

void GlassBackground::tick() {
    m_phase += 0.016;
    update();
}

void GlassBackground::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    // Base vertical gradient
    QLinearGradient baseGrad(0, 0, 0, h);
    baseGrad.setColorAt(0.0, m_baseColor);
    QColor darker = m_baseColor;
    darker.setRed(std::min(255, darker.red() + 6));
    darker.setGreen(std::min(255, darker.green() + 6));
    darker.setBlue(std::min(255, darker.blue() + 8));
    baseGrad.setColorAt(1.0, darker);
    p.fillRect(0, 0, w, h, baseGrad);

    // Radial gradient blobs with orbital drift
    for (int i = 0; i < static_cast<int>(m_blobs.size()); ++i) {
        const auto& blob = m_blobs[i];
        double driftX = std::sin(m_phase + i * 2.1) * 60.0;
        double driftY = std::cos(m_phase * 0.7 + i * 1.4) * 45.0;
        double cx = blob.cxRatio * w + driftX;
        double cy = blob.cyRatio * h + driftY;

        QRadialGradient grad(QPointF(cx, cy), blob.radius);
        grad.setColorAt(0.0, blob.color);
        QColor mid = blob.color;
        mid.setAlpha(blob.color.alpha() / 3);
        grad.setColorAt(0.5, mid);
        grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setBrush(grad);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(cx - blob.radius, cy - blob.radius,
                          blob.radius * 2.0, blob.radius * 2.0));
    }

    // Corner vignette
    QRadialGradient vig(QPointF(w * 0.5, h * 0.5), std::max(w, h) * 0.7);
    vig.setColorAt(0.0, QColor(0, 0, 0, 0));
    vig.setColorAt(0.7, QColor(0, 0, 0, 0));
    vig.setColorAt(1.0, QColor(0, 0, 0, 55));
    p.setBrush(vig);
    p.drawRect(0, 0, w, h);
}
