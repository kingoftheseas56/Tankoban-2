#include "SubtitleOverlay.h"

#include <QVBoxLayout>

SubtitleOverlay::SubtitleOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    m_label->setWordWrap(true);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    applyStyleSheet();

    m_endTimer.setSingleShot(true);
    connect(&m_endTimer, &QTimer::timeout, this, [this]() {
        m_label->hide();
        hide();
    });

    hide();
}

void SubtitleOverlay::setText(const QString& text)
{
    if (text.isEmpty()) {
        m_endTimer.stop();
        m_label->hide();
        hide();
        return;
    }
    m_label->setText(text);
    m_label->show();
    show();
    reposition();
}

void SubtitleOverlay::setStyle(int fontSize, int marginPercent, bool outline)
{
    m_fontSize      = fontSize;
    m_marginPercent = marginPercent;
    m_outline       = outline;
    applyStyleSheet();
    if (isVisible())
        reposition();
}

void SubtitleOverlay::reposition()
{
    QWidget* p = parentWidget();
    if (!p) return;

    int w = p->width();
    int h = p->height();
    if (w <= 0 || h <= 0) return;

    int marginX = qMax(40, w / 8);
    int labelW  = w - marginX * 2;

    m_label->setFixedWidth(labelW);
    m_label->adjustSize();
    int labelH = m_label->sizeHint().height();

    int bottomOffset = 20;
    if (m_controlsVisible)
        bottomOffset = qMax(80, 90) + 10;   // ~90px two-row HUD + 10px gap

    int y = h - labelH - bottomOffset;

    setGeometry(marginX, y, labelW, labelH);
    m_label->setGeometry(0, 0, labelW, labelH);
    raise();
}

void SubtitleOverlay::setControlsVisible(bool visible)
{
    m_controlsVisible = visible;
    if (isVisible())
        reposition();
}

void SubtitleOverlay::setColors(const QString& fontColor, int bgOpacity)
{
    m_fontColor = fontColor;
    m_bgOpacity = qBound(0, bgOpacity, 255);
    applyStyleSheet();
    if (isVisible())
        reposition();
}

void SubtitleOverlay::applyStyleSheet()
{
    m_label->setStyleSheet(
        QStringLiteral(
            "QLabel {"
            "  color: %1;"
            "  font-size: %2px;"
            "  font-weight: bold;"
            "  font-family: Arial, sans-serif;"
            "  background: rgba(0, 0, 0, %3);"
            "  border-radius: 4px;"
            "  padding: 6px 14px;"
            "}"
        ).arg(m_fontColor).arg(m_fontSize).arg(m_bgOpacity)
    );
}
