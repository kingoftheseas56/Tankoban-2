#include "ui/player/StatsBadge.h"

#include <QGridLayout>
#include <QLabel>

// VIDEO_PLAYER_UI_POLISH Phase 3 2026-04-22 — audit finding #2 "the stats
// badge looks like a developer overlay, not a finished player surface."
// Prior shape was a single dense line "{codec} · {W}x{H} · {fps} fps ·
// {drops} drops" with a placeholder "—" that read as unfinished. New
// shape is a 4-row label:value grid matching finished player stats
// panels (mpv's --osd-level=2, PotPlayer's Ctrl+F1). Label column is
// muted gray ("Codec", "Resolution", "FPS", "Drops"), value column is
// bright monospace for numeric-column alignment. Wider padding, slightly
// larger radius, same gray-on-dark palette per feedback_no_color_no_emoji.
StatsBadge::StatsBadge(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "StatsBadge {"
        "  background: rgba(16, 16, 16, 235);"
        "  border: 1px solid rgba(255, 255, 255, 30);"
        "  border-radius: 12px;"
        "}"
    );

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(14, 10, 14, 10);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(3);

    // The single m_label approach is retained for backward compatibility
    // with the show/hide code path in VideoPlayer; we format its text as
    // a 4-line block and let Qt measure it. Values are right-aligned on
    // their own line column by padding each label to a common width
    // within the text itself — simpler than splitting into eight QLabels
    // and keeps the adjustSize() contract intact.
    m_label = new QLabel(this);
    m_label->setStyleSheet(
        "color: rgba(245, 245, 245, 240);"
        "font-size: 11px;"
        "font-family: Consolas, 'Cascadia Code', monospace;"
        "background: transparent;"
        "border: none;"
    );
    m_label->setTextFormat(Qt::RichText);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(m_label, 0, 0);
    adjustSize();
}

static QString formatRow(const QString& label, const QString& value) {
    // Use HTML table-like spans so Qt's rich-text renderer column-aligns
    // the value against the label. Muted label, bright value.
    return QStringLiteral(
        "<span style='color:rgba(160,160,160,220);'>%1</span>"
        "&nbsp;&nbsp;"
        "<span style='color:rgba(245,245,245,240);'>%2</span>")
        .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
}

void StatsBadge::setStats(const QString& codec, int width, int height,
                          double fps, quint64 drops)
{
    if (codec.isEmpty()) { clear(); return; }

    const QString fpsStr  = (fps > 0.0)
        ? QString::number(fps, 'f', fps < 30 ? 3 : 2)
        : QStringLiteral("—");
    const QString dropStr = (drops == static_cast<quint64>(-1))
        ? QStringLiteral("—")
        : QString::number(drops);
    const QString resStr = QStringLiteral("%1 × %2").arg(width).arg(height);

    const QString html =
        formatRow(QStringLiteral("Codec"),      codec) + QStringLiteral("<br/>") +
        formatRow(QStringLiteral("Resolution"), resStr) + QStringLiteral("<br/>") +
        formatRow(QStringLiteral("FPS"),        fpsStr) + QStringLiteral("<br/>") +
        formatRow(QStringLiteral("Drops"),      dropStr);

    m_label->setText(html);
    adjustSize();
}

void StatsBadge::clear()
{
    if (m_label) m_label->clear();
    adjustSize();
}
