#include "ChapterDownloadIndicator.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

ChapterDownloadIndicator::ChapterDownloadIndicator(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFixedSize(28, 28);
}

void ChapterDownloadIndicator::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    if (s != State::Downloading) m_progress = 0;
    emit stateChanged(s);
    update();
}

void ChapterDownloadIndicator::setProgress(int pct)
{
    pct = qBound(0, pct, 100);
    if (m_progress == pct) return;
    if (!m_progressAnim) {
        m_progressAnim = new QPropertyAnimation(this, "progress", this);
        m_progressAnim->setDuration(300);
        m_progressAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_progressAnim->stop();
    m_progressAnim->setStartValue(m_progress);
    m_progressAnim->setEndValue(pct);
    m_progressAnim->start();
}

void ChapterDownloadIndicator::setProgressImmediate(int pct)
{
    pct = qBound(0, pct, 100);
    if (m_progress == pct) return;
    m_progress = pct;
    emit progressChanged(pct);
    if (m_state == State::Downloading) update();
}

void ChapterDownloadIndicator::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) emit clicked();
    QWidget::mousePressEvent(event);
}

void ChapterDownloadIndicator::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF outer = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);

    const QColor strokeFg  = palette().color(QPalette::Text);
    const QColor strokeDim = palette().color(QPalette::Mid);
    const QColor bg        = palette().color(QPalette::Window);
    const QColor errorFg   = palette().color(QPalette::BrightText);

    switch (m_state) {
        case State::NotDownloaded:
            paintArrow(p, outer, strokeDim);
            break;
        case State::Queued:
            paintSpinnerWithArrow(p, outer, strokeFg);
            break;
        case State::Downloading:
            paintProgressArc(p, outer, strokeFg, bg, m_progress);
            break;
        case State::Downloaded:
            paintCheck(p, outer, strokeFg, bg);
            break;
        case State::Errored:
            paintError(p, outer, errorFg);
            break;
    }
}

void ChapterDownloadIndicator::paintArrow(QPainter& p, const QRectF& r, const QColor& c) const
{
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal s  = r.width() * 0.50;

    QPainterPath path;
    path.moveTo(cx, cy - s/2);
    path.lineTo(cx, cy + s/2);
    path.moveTo(cx - s/3, cy + s/4);
    path.lineTo(cx, cy + s/2);
    path.lineTo(cx + s/3, cy + s/4);

    QPen pen(c, 1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void ChapterDownloadIndicator::paintSpinnerWithArrow(QPainter& p, const QRectF& r, const QColor& c) const
{
    // Static dashed spinner ring (B.3 may add animation) + centered arrow
    QPen pen(c, 1.6);
    pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r);
    paintArrow(p, r.adjusted(r.width() * 0.20, r.width() * 0.20,
                              -r.width() * 0.20, -r.width() * 0.20), c);
}

void ChapterDownloadIndicator::paintProgressArc(QPainter& p, const QRectF& r,
                                                  const QColor& fg, const QColor& bg, int pct) const
{
    // Determinate progress arc -- stroke width = half the widget size for the
    // ring-fill look from Mihon's ChapterDownloadIndicator.kt:158-163.
    QPen pen(fg, r.width() * 0.50);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const int startAngle = 90 * 16;            // 12 o'clock
    const int spanAngle  = -qRound(pct / 100.0 * 360 * 16);  // clockwise
    p.drawArc(r.adjusted(pen.widthF()/2, pen.widthF()/2,
                          -pen.widthF()/2, -pen.widthF()/2),
              startAngle, spanAngle);
    // The 50% recolor threshold assumes pen width ≈ r.width()/2 (the half-widget
    // stroke set above). At that geometry, the arc fill begins to occlude the
    // centered arrow at 50% sweep; flipping to bg keeps the arrow legible. If
    // the pen width changes, this threshold needs to track it.
    const QColor arrowColor = (pct < 50) ? fg : bg;
    paintArrow(p, r.adjusted(r.width() * 0.25, r.width() * 0.25,
                              -r.width() * 0.25, -r.width() * 0.25),
               arrowColor);
}

void ChapterDownloadIndicator::paintCheck(QPainter& p, const QRectF& r,
                                            const QColor& fg, const QColor& bg) const
{
    // Filled circle with a check mark inside (the Downloaded state)
    p.setPen(Qt::NoPen);
    p.setBrush(fg);
    p.drawEllipse(r);

    QPen pen(bg, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal s  = r.width() * 0.30;
    QPainterPath check;
    check.moveTo(cx - s, cy);
    check.lineTo(cx - s/3, cy + s * 0.7);
    check.lineTo(cx + s, cy - s/2);
    p.drawPath(check);
}

void ChapterDownloadIndicator::paintError(QPainter& p, const QRectF& r, const QColor& c) const
{
    // Outlined circle + exclamation mark
    QPen pen(c, 1.8);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r);
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal s  = r.width() * 0.30;
    p.drawLine(QPointF(cx, cy - s), QPointF(cx, cy + s/3));
    p.drawPoint(QPointF(cx, cy + s/2));
}
