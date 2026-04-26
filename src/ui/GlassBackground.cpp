#include "GlassBackground.h"

#include <QPainter>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QApplication>
#include <cmath>

#include "Theme.h"

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
    m_baseColorOverride = true;
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

    // Base vertical gradient — sources from Theme::current().bg0 so the
    // canvas tracks the active theme Mode (Dark/Light/Nord/etc.) instead
    // of the constructor-time literal RGB(5,5,5). m_baseColor remains the
    // override hook for callers that want a custom canvas tint via
    // setBaseColor(...) — those wins over the theme.
    QColor canvasBase = m_baseColorOverride ? m_baseColor : QColor(Theme::current().bg0);
    if (!canvasBase.isValid()) canvasBase = m_baseColor;
    QLinearGradient baseGrad(0, 0, 0, h);
    baseGrad.setColorAt(0.0, canvasBase);
    QColor darker = canvasBase;
    // Light vs dark gradient direction: dark base → slightly brighter at bottom;
    // light base → slightly darker at bottom. Pick the direction by luminance.
    const int lum = (canvasBase.red() * 299 + canvasBase.green() * 587 + canvasBase.blue() * 114) / 1000;
    const int delta = (lum < 128) ? +6 : -6;
    darker.setRed(std::clamp(darker.red() + delta, 0, 255));
    darker.setGreen(std::clamp(darker.green() + delta, 0, 255));
    darker.setBlue(std::clamp(darker.blue() + delta + (lum < 128 ? 2 : -2), 0, 255));
    baseGrad.setColorAt(1.0, darker);
    p.fillRect(0, 0, w, h, baseGrad);

    // Radial gradient blobs with orbital drift. Blob POSITION + SIZE come from
    // m_blobs (ctor defaults); blob COLOR comes from Theme::currentBlobs() —
    // the cached blob triplet for the active mode, populated once per theme
    // switch by applyTheme(). Reading from the cache here (instead of calling
    // accentBlobsForMode(loadMode()) per paint) eliminates the QSettings
    // lookup + 3 QColor allocations that were causing scroll judder on every
    // 50ms timer tick — the smoking gun for "scroll unsmooth after theme
    // switch" (perf plan 2026-04-26).
    Theme::ModeBlobs accentBlobs = Theme::currentBlobs();
    const QColor accentColors[3] = { accentBlobs.a, accentBlobs.b, accentBlobs.c };

    for (int i = 0; i < static_cast<int>(m_blobs.size()); ++i) {
        const auto& blob = m_blobs[i];
        double driftX = std::sin(m_phase + i * 2.1) * 60.0;
        double driftY = std::cos(m_phase * 0.7 + i * 1.4) * 45.0;
        double cx = blob.cxRatio * w + driftX;
        double cy = blob.cyRatio * h + driftY;

        // Mode-tinted color overrides the ctor blob.color for blob index 0/1/2;
        // additional blobs (rare) fall back to ctor color.
        const QColor blobColor = (i < 3) ? accentColors[i] : blob.color;

        QRadialGradient grad(QPointF(cx, cy), blob.radius);
        grad.setColorAt(0.0, blobColor);
        QColor mid = blobColor;
        mid.setAlpha(blobColor.alpha() / 3);
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
