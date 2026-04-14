#include "ui/player/ShortcutsOverlay.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

struct ShortcutEntry { const char* key; const char* desc; };

static const ShortcutEntry SHORTCUTS[] = {
    { "Space",       "Play / Pause" },
    { "F / F11 / Enter", "Toggle fullscreen" },
    { "Esc",         "Exit fullscreen / Close" },
    { "\xe2\x86\x90 / \xe2\x86\x92",  "Seek \xc2\xb1" "10 seconds" },
    { "\xe2\x86\x91 / \xe2\x86\x93",  "Volume \xc2\xb1" "5%" },
    { "M",           "Mute / Unmute" },
    { "Scroll",      "Volume up / down" },
    { "C / ]",       "Speed up" },
    { "X / [",       "Speed down" },
    { "Z / \\",      "Reset speed (1.0x)" },
    { "A",           "Cycle audio track" },
    { "S",           "Cycle subtitle track" },
    { "Shift+S",     "Toggle subtitles" },
    { "N",           "Next episode" },
    { "P",           "Previous episode" },
    { "L",           "Episode list" },
    { "Double-click","Toggle fullscreen" },
    { "< / >",       "Subtitle delay \xc2\xb1" "100ms" },
    { "Ctrl+Shift+Z","Reset subtitle delay" },
    { "D",           "Toggle deinterlace" },
    { "Shift+A",     "Audio normalization" },
    { "Backspace",   "Back to library" },
    { "?",           "This help" },
};
static const int SHORTCUT_COUNT = sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0]);

ShortcutsOverlay::ShortcutsOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    hide();

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnim->setDuration(150);
}

void ShortcutsOverlay::toggle()
{
    if (m_showing) {
        m_showing = false;
        m_fadeAnim->stop();
        m_fadeAnim->setDuration(150);
        m_fadeAnim->setStartValue(m_opacity);
        m_fadeAnim->setEndValue(0.0);
        connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
            if (m_opacity <= 0.01) hide();
        });
        m_fadeAnim->start();
    } else {
        m_showing = true;
        if (parentWidget())
            setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
        show();
        raise();
        m_fadeAnim->stop();
        m_fadeAnim->setDuration(150);
        m_fadeAnim->setStartValue(m_opacity);
        m_fadeAnim->setEndValue(1.0);
        m_fadeAnim->start();
    }
}

void ShortcutsOverlay::setOpacity(qreal o)
{
    m_opacity = o;
    update();
}

void ShortcutsOverlay::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    if (m_showing) toggle();
}

void ShortcutsOverlay::paintEvent(QPaintEvent*)
{
    if (m_opacity <= 0.01) return;

    QPainter p(this);
    p.setOpacity(m_opacity);
    p.setRenderHint(QPainter::Antialiasing);

    // Scrim
    p.fillRect(rect(), QColor(0, 0, 0, 190));

    // Card dimensions
    int cardW = 420;
    int rowH  = 28;
    int cardH = 48 + SHORTCUT_COUNT * rowH + 30;
    int cardX = (width()  - cardW) / 2;
    int cardY = (height() - cardH) / 2;

    // Card background
    QPainterPath card;
    card.addRoundedRect(QRectF(cardX, cardY, cardW, cardH), 12, 12);
    p.fillPath(card, QColor(18, 18, 18, 245));
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawPath(card);

    // Title
    QFont titleFont = font();
    titleFont.setPixelSize(14);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(QColor(214, 194, 164, 240));
    p.drawText(QRect(cardX, cardY + 16, cardW, 24), Qt::AlignCenter, "Keyboard Shortcuts");

    // Rows
    QFont keyFont = font();
    keyFont.setPixelSize(12);
    keyFont.setFamily("monospace");
    keyFont.setBold(true);

    QFont descFont = font();
    descFont.setPixelSize(12);

    int y = cardY + 48;
    for (int i = 0; i < SHORTCUT_COUNT; ++i) {
        // Key column (left, warm accent)
        p.setFont(keyFont);
        p.setPen(QColor(214, 194, 164, 240));
        p.drawText(QRect(cardX + 24, y, 120, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(SHORTCUTS[i].key));

        // Description column (right, white)
        p.setFont(descFont);
        p.setPen(QColor(245, 245, 245, 200));
        p.drawText(QRect(cardX + 154, y, cardW - 178, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(SHORTCUTS[i].desc));

        y += rowH;
    }

    // Hint label at bottom of card
    QFont hintFont = font();
    hintFont.setPixelSize(11);
    p.setFont(hintFont);
    p.setPen(QColor(180, 180, 180, 160));
    p.drawText(QRect(cardX, y + 4, cardW, 20), Qt::AlignCenter, "Press ? or click to close");
}
