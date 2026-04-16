#include "ui/player/LoadingOverlay.h"

#include <QPainter>
#include <QPainterPath>
#include <QFileInfo>
#include <QFontMetrics>

LoadingOverlay::LoadingOverlay(QWidget* parent)
    : QWidget(parent)
{
    // Width tuned to fit a comfortably-long filename at 13px with
    // ellipsis-middle for anything longer. Height matches VolumeHud's
    // pill aesthetic scaled up for a single line of larger text.
    setFixedSize(400, 48);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);
}

void LoadingOverlay::showLoading(const QString& filename)
{
    m_mode = Mode::Loading;
    // Display just the basename — full paths are noise in the
    // centered-over-canvas position. Streams (http://...) get their
    // tail after the last slash which tends to be a query-string'd
    // token; acceptable for rare-path visibility.
    m_filename = filename.isEmpty()
        ? QString()
        : QFileInfo(filename).fileName();
    fadeIn();
}

void LoadingOverlay::showBuffering()
{
    m_mode = Mode::Buffering;
    if (isVisible() && m_opacity >= 0.98) {
        // Already on-screen (e.g. Loading → Buffering transition within
        // the same open window). Update in place without a re-fade; the
        // text swap is instant but visually smooth at 15px.
        update();
    } else {
        fadeIn();
    }
}

void LoadingOverlay::dismiss()
{
    if (m_mode == Mode::Hidden) return;
    m_mode = Mode::Hidden;
    fadeOut();
}

void LoadingOverlay::fadeIn()
{
    repositionCentered();

    m_fadeAnim->stop();
    if (!isVisible()) {
        setOpacity(0.0);
    }
    show();
    raise();

    m_fadeAnim->setDuration(200);
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(1.0);
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    m_fadeAnim->start();
    update();
}

void LoadingOverlay::fadeOut()
{
    m_fadeAnim->stop();
    m_fadeAnim->setDuration(200);
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(0.0);
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacity <= 0.01) hide();
    });
    m_fadeAnim->start();
}

void LoadingOverlay::repositionCentered()
{
    if (!parentWidget()) return;
    const int px = (parentWidget()->width()  - width())  / 2;
    const int py = (parentWidget()->height() - height()) / 2;
    move(px, py);
}

void LoadingOverlay::setOpacity(qreal o)
{
    m_opacity = o;
    update();
}

void LoadingOverlay::paintEvent(QPaintEvent*)
{
    if (m_opacity <= 0.01 || m_mode == Mode::Hidden) return;

    QPainter p(this);
    p.setOpacity(m_opacity);
    p.setRenderHint(QPainter::Antialiasing);

    // Rounded pill background — matches VolumeHud/CenterFlash aesthetic.
    QPainterPath bg;
    bg.addRoundedRect(QRectF(0, 0, width(), height()), 8, 8);
    p.fillPath(bg, QColor(10, 10, 10, 218));
    p.setPen(QPen(QColor(255, 255, 255, 46), 1));
    p.drawPath(bg);

    // Compose the status string. Em-dash matches Hemanth's naming in the
    // TODO scope text; filename elision uses middle-ellipsis because
    // series filenames commonly share prefixes ("The Sopranos S03E07
    // …") where the middle is the identifying part.
    QString text;
    if (m_mode == Mode::Loading) {
        text = m_filename.isEmpty()
            ? QStringLiteral("Loading")
            : QStringLiteral("Loading \u2014 ") + m_filename;
    } else {
        text = QStringLiteral("Buffering\u2026");
    }

    p.setPen(QColor(245, 245, 245, 250));
    QFont f = font();
    f.setPixelSize(15);
    p.setFont(f);

    // Elide to pill interior (12px padding each side).
    QFontMetrics fm(f);
    const QString elided = fm.elidedText(text, Qt::ElideMiddle, width() - 24);
    p.drawText(rect(), Qt::AlignCenter, elided);
}
