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
    // MAKE_MPV_BEAT_FFMPEG Task 8 — backdrop alpha refactored to a method
    // call (setBackdropOpaque) so VideoPlayer can flip it backend-aware.
    // Default state is the pre-Task-8 ffmpeg-friendly alpha=217 styling.
    setBackdropOpaque(false);

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

    // MAKE_MPV_BEAT_FFMPEG Task 8 (2026-05-03) — moved from top-right →
    // top-left → center-bottom-above-HUD. Iteration trail:
    //   (1) Top-right (original): meshed with chrome cluster Min/Max/Close
    //       added in PER_VIEW_CHROME_FIX P2 (2026-05-02).
    //   (2) Top-left: meshed with the Tankoban app-text on MainWindow's top
    //       bar (separate regression: top bar shouldn't be visible in video
    //       mode but is, per Hemanth 2026-05-03; tracked as Task 8 carry-
    //       forward).
    //   (3) Center-bottom-above-HUD (current): mirrors the original
    //       VolumeHud placement, known-clear region — sits above the opaque
    //       VideoControlBar (Codex Task 2 made it #0a0a0a on mpv) and below
    //       the video content. Auto-hides cleanly with the rest of the HUD
    //       lifecycle since both share the same parent.
    if (p) {
        QWidget* bar = p->findChild<QWidget*>("VideoControlBar");
        const int barH = (bar && bar->isVisible()) ? bar->sizeHint().height() : 0;
        const int x = (p->width() - width()) / 2;
        const int y = p->height() - barH - height() - 18;
        move(qMax(0, x), qMax(0, y));
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

void ToastHud::setBackdropOpaque(bool opaque)
{
    m_backdropOpaque = opaque;
    // MAKE_MPV_BEAT_FFMPEG Task 8 — alpha 217 for ffmpeg/FrameCanvas path
    // (composites cleanly), 255 (fully opaque) for mpv path (Vulkan child
    // HWND below means alpha<255 lets library page bleed through).
    const char* alpha = opaque ? "255" : "217";
    m_label->setStyleSheet(QStringLiteral(
        "background: rgba(10,10,10,%1);"
        "color: rgba(245,245,245,250);"
        "border: 1px solid rgba(255,255,255,31);"
        "border-radius: 6px;"
        "padding: 8px 14px;"
        "font-size: 12px;"
        "font-weight: 600;"
    ).arg(QString::fromUtf8(alpha)));
}
