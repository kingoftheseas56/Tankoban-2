#include "ChapterDownloadIndicator.h"

#include <QMouseEvent>
#include <QPainter>

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
    update();
}

void ChapterDownloadIndicator::setProgress(int pct)
{
    pct = qBound(0, pct, 100);
    if (m_progress == pct) return;
    m_progress = pct;
    if (m_state == State::Downloading) update();
}

void ChapterDownloadIndicator::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) emit clicked();
    QWidget::mousePressEvent(event);
}

void ChapterDownloadIndicator::paintEvent(QPaintEvent*)
{
    // B.1 PLACEHOLDER -- 5 distinct fills so the state machine is visible.
    // B.2 will replace each branch with the proper Mihon-style visual
    // (palette()-resolved colors, no hex literals).
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect().adjusted(2, 2, -2, -2);

    QColor fill;
    switch (m_state) {
        case State::NotDownloaded: fill = QColor("#888888"); break;
        case State::Queued:        fill = QColor("#bbbbbb"); break;
        case State::Downloading:   fill = QColor("#dddddd"); break;
        case State::Downloaded:    fill = QColor("#ffffff"); break;
        case State::Errored:       fill = QColor("#666666"); break;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(r);
}
