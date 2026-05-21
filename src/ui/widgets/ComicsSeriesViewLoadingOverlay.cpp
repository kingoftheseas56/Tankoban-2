#include "ComicsSeriesViewLoadingOverlay.h"

#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace tankoban::ui::widgets {

ComicsSeriesViewLoadingOverlay::ComicsSeriesViewLoadingOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("ComicsSeriesViewLoadingOverlay"));

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("ComicsSeriesViewLoadingOverlay_Label"));
    m_label->setText(QStringLiteral("Loading"));
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(QStringLiteral(
        "QLabel#ComicsSeriesViewLoadingOverlay_Label { color: #d0d0d0; font-size: 14px; }"));

    auto* layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(m_label, 0, Qt::AlignCenter);
    layout->addStretch();
}

ComicsSeriesViewLoadingOverlay::~ComicsSeriesViewLoadingOverlay() = default;

void ComicsSeriesViewLoadingOverlay::setMessage(const QString& text)
{
    if (m_label) m_label->setText(text);
}

void ComicsSeriesViewLoadingOverlay::paintEvent(QPaintEvent* /*ev*/)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
}

void ComicsSeriesViewLoadingOverlay::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
}

} // namespace tankoban::ui::widgets
