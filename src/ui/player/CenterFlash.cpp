#include "ui/player/CenterFlash.h"

#include <QPainter>

CenterFlash::CenterFlash(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(80, 80);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();

    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(300);

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);

    connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
        m_fadeAnim->stop();
        m_fadeAnim->setDuration(350);
        m_fadeAnim->setStartValue(m_opacity);
        m_fadeAnim->setEndValue(0.0);
        disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
        connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
            if (m_opacity <= 0.01) hide();
        });
        m_fadeAnim->start();
    });
}

void CenterFlash::flash(const QByteArray& svg)
{
    m_svg = svg;

    // Center in parent
    if (parentWidget()) {
        int px = (parentWidget()->width()  - width())  / 2;
        int py = (parentWidget()->height() - height()) / 2;
        move(px, py);
    }

    m_fadeAnim->stop();
    setOpacity(0.0);
    show();
    raise();

    // 120ms fade-in, then hold timer starts
    m_fadeAnim->setDuration(120);
    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        m_holdTimer.start();
    });
    m_fadeAnim->start();
}

void CenterFlash::setOpacity(qreal o)
{
    m_opacity = o;
    update();
}

void CenterFlash::paintEvent(QPaintEvent*)
{
    if (m_opacity <= 0.01 || m_svg.isEmpty()) return;

    QPainter p(this);
    p.setOpacity(m_opacity);
    p.setRenderHint(QPainter::Antialiasing);

    // VIDEO_HUD_MINIMALIST polish 2026-04-25 (hemanth: "I want that black
    // blob removed and just for the icons to be in toast"): the prior
    // black-at-140-alpha ellipse backdrop is gone; bare SVG only. The
    // 80x80 widget + 20px padding around the 40x40 iconRect is preserved
    // so the icon's centered-in-canvas position doesn't shift across
    // builds (the padding was originally for the circle's curve; now
    // it's just whitespace around a bare icon, which is fine).
    QSvgRenderer renderer(m_svg);
    QRectF iconRect(20, 20, 40, 40);
    renderer.render(&p, iconRect);
}
