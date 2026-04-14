#include "ui/player/ToastHud.h"

#include <QFontMetrics>
#include <algorithm>

static constexpr int MAX_WIDTH    = 280;
static constexpr int HIDE_MS      = 2000;
static constexpr int FADE_IN_MS   = 120;
static constexpr int FADE_OUT_MS  = 200;

ToastHud::ToastHud(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setStyleSheet("background: transparent;");

    m_label = new QLabel(this);
    m_label->setWordWrap(false);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setStyleSheet(
        "background: rgba(10,10,10,217);"
        "color: rgba(245,245,245,250);"
        "border: 1px solid rgba(255,255,255,31);"
        "border-radius: 6px;"
        "padding: 8px 14px;"
        "font-size: 12px;"
        "font-weight: 600;"
    );

    m_effect = new QGraphicsOpacityEffect(this);
    m_effect->setOpacity(0.0);
    setGraphicsEffect(m_effect);

    m_fadeAnim = new QPropertyAnimation(m_effect, "opacity", this);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_effect->opacity() <= 0.01)
            hide();
    });

    m_holdTimer.setSingleShot(true);
    connect(&m_holdTimer, &QTimer::timeout, this, &ToastHud::startFadeOut);

    hide();
}

void ToastHud::showToast(const QString& message)
{
    QString text = message.trimmed();
    if (text.isEmpty()) {
        hide();
        return;
    }

    m_fadeAnim->stop();
    m_holdTimer.stop();

    QWidget* p = parentWidget();
    int parentWidth = p ? p->width() : MAX_WIDTH;
    int availableWidth = std::max(1, std::min(MAX_WIDTH, parentWidth > 0 ? parentWidth - 28 : MAX_WIDTH));

    QFontMetrics fm(m_label->font());
    int textLimit = std::max(1, availableWidth - 28);
    QString elided = fm.elidedText(text, Qt::ElideRight, textLimit);

    m_label->setMaximumWidth(availableWidth);
    m_label->setText(elided);
    m_label->adjustSize();
    adjustSize();
    m_label->move(0, 0);

    // Position top-right of parent, 12px inset
    if (p) {
        int x = p->width() - width() - 12;
        move(x, 12);
    }

    // Fade in
    m_effect->setOpacity(0.0);
    m_fadeAnim->setDuration(FADE_IN_MS);
    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);

    if (!isVisible())
        show();
    raise();
    m_fadeAnim->start();

    m_holdTimer.start(HIDE_MS);
}

void ToastHud::startFadeOut()
{
    m_fadeAnim->stop();
    m_fadeAnim->setDuration(FADE_OUT_MS);
    m_fadeAnim->setStartValue(m_effect->opacity());
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}
