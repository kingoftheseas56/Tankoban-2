#include "ui/player/StatsBadge.h"

#include <QHBoxLayout>
#include <QLabel>

StatsBadge::StatsBadge(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        // Consistent with ToastHud / CenterFlash: 12 px radius, translucent
        // dark, subtle 1 px border, gray-on-white typography. No accent.
        "StatsBadge {"
        "  background: rgba(16, 16, 16, 230);"
        "  border: 1px solid rgba(255, 255, 255, 24);"
        "  border-radius: 10px;"
        "}"
    );

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(0);

    m_label = new QLabel(this);
    m_label->setStyleSheet(
        "color: rgba(245, 245, 245, 235);"
        "font-size: 11px;"
        "font-family: Consolas, 'Cascadia Code', monospace;"
        "background: transparent;"
        "border: none;"
    );
    lay->addWidget(m_label);
    adjustSize();
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

    m_label->setText(QStringLiteral("%1 · %2×%3 · %4 fps · %5 drops")
                     .arg(codec)
                     .arg(width).arg(height)
                     .arg(fpsStr)
                     .arg(dropStr));
    adjustSize();
}

void StatsBadge::clear()
{
    if (m_label) m_label->clear();
    adjustSize();
}
