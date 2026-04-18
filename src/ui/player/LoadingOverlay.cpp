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

// STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — primary API. Sets the current
// stage + optionally updates filename. Mutates in place (no re-fade) when
// already visible to avoid flashing on every sub-stage transition during
// a fast open. Fades in from hidden.
void LoadingOverlay::setStage(Stage stage, const QString& filename)
{
    m_stage = stage;
    if (!filename.isEmpty()) {
        // Display just the basename — full paths are noise in the
        // centered-over-canvas position. Streams (http://...) get their
        // tail after the last slash which tends to be a query-string'd
        // token; acceptable for rare-path visibility.
        m_filename = QFileInfo(filename).fileName();
    }
    if (m_visible && m_opacity >= 0.98) {
        // Already on-screen — swap text instantly, no re-fade. Typical
        // during Opening → Probing → OpeningDecoder → DecodingFirstFrame
        // transitions on a fast open where all 4 stages fire within a
        // few hundred ms. Re-fading on each would flicker.
        update();
    } else {
        m_visible = true;
        fadeIn();
    }
}

void LoadingOverlay::showLoading(const QString& filename)
{
    setStage(Stage::Opening, filename);
}

void LoadingOverlay::showBuffering()
{
    setStage(Stage::Buffering);
}

void LoadingOverlay::dismiss()
{
    if (!m_visible) return;
    m_visible = false;
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

// STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — per-stage text mapping. Proposal
// A (bracketed-progress, precise, matches sidecar vocabulary). Flip to
// Proposal B (user-literal smoother wording — "Connecting…" / "Loading…"
// / "Almost ready…" / "Almost ready…" / "Buffering…" / "Still working —
// close to retry") at Hemanth's smoke via Rule 14 product decision if
// preferred. Filename appended via em-dash on Opening stage only; sub-
// stages replace the filename line with the stage text (filename retained
// in m_filename for TakingLonger → recovery paint continuity).
QString LoadingOverlay::textForStage() const
{
    switch (m_stage) {
    case Stage::Opening:
        return m_filename.isEmpty()
            ? QStringLiteral("Opening source\u2026")
            : QStringLiteral("Opening source \u2014 ") + m_filename;
    case Stage::Probing:
        return QStringLiteral("Probing source\u2026");
    case Stage::OpeningDecoder:
        return QStringLiteral("Opening decoder\u2026");
    case Stage::DecodingFirstFrame:
        return QStringLiteral("Decoding first frame\u2026");
    case Stage::Buffering:
        return QStringLiteral("Buffering\u2026");
    case Stage::TakingLonger:
        return QStringLiteral("Taking longer than expected \u2014 close to retry");
    }
    return {};
}

void LoadingOverlay::paintEvent(QPaintEvent*)
{
    if (m_opacity <= 0.01 || !m_visible) return;

    QPainter p(this);
    p.setOpacity(m_opacity);
    p.setRenderHint(QPainter::Antialiasing);

    // Rounded pill background — matches VolumeHud/CenterFlash aesthetic.
    QPainterPath bg;
    bg.addRoundedRect(QRectF(0, 0, width(), height()), 8, 8);
    p.fillPath(bg, QColor(10, 10, 10, 218));
    p.setPen(QPen(QColor(255, 255, 255, 46), 1));
    p.drawPath(bg);

    const QString text = textForStage();

    p.setPen(QColor(245, 245, 245, 250));
    QFont f = font();
    f.setPixelSize(15);
    p.setFont(f);

    // Elide to pill interior (12px padding each side). Middle-ellipsis
    // because series filenames commonly share prefixes ("The Sopranos
    // S03E07 …") where the middle is the identifying part.
    QFontMetrics fm(f);
    const QString elided = fm.elidedText(text, Qt::ElideMiddle, width() - 24);
    p.drawText(rect(), Qt::AlignCenter, elided);
}
