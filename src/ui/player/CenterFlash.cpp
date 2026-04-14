#include "ui/player/CenterFlash.h"

#include <QPainter>
#include <QPainterPath>

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

    // Circle background
    QPainterPath bg;
    bg.addEllipse(QRectF(0, 0, width(), height()));
    p.fillPath(bg, QColor(0, 0, 0, 140));

    // SVG icon centered with padding
    QSvgRenderer renderer(m_svg);
    QRectF iconRect(20, 20, 40, 40);
    renderer.render(&p, iconRect);
}
