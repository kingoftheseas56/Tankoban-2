#include "TileCard.h"

#include <QVBoxLayout>
#include <QPixmap>
#include <QMouseEvent>
#include <QPainter>
#include <QFontMetrics>

TileCard::TileCard(const QString& thumbPath,
                   const QString& title,
                   const QString& subtitle,
                   QWidget* parent)
    : QFrame(parent)
{
    setObjectName("TileCard");
    setFixedWidth(CARD_WIDTH);
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // Cover image wrap
    m_imageWrap = new QFrame(this);
    m_imageWrap->setObjectName("TileImageWrap");
    m_imageWrap->setFixedSize(CARD_WIDTH, IMAGE_HEIGHT);

    auto* imageLabel = new QLabel(m_imageWrap);
    imageLabel->setFixedSize(CARD_WIDTH - 2, IMAGE_HEIGHT - 2); // inside border
    imageLabel->move(1, 1);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet("border-radius: 11px;");

    if (!thumbPath.isEmpty()) {
        QPixmap pix(thumbPath);
        if (!pix.isNull()) {
            imageLabel->setPixmap(pix.scaled(CARD_WIDTH - 2, IMAGE_HEIGHT - 2,
                                             Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
        }
    }

    if (imageLabel->pixmap().isNull()) {
        // Placeholder: dark rect with first letter
        QPixmap placeholder(CARD_WIDTH - 2, IMAGE_HEIGHT - 2);
        placeholder.fill(QColor(20, 20, 20));
        QPainter p(&placeholder);
        p.setPen(QColor(80, 80, 80));
        QFont font;
        font.setPixelSize(48);
        font.setBold(true);
        p.setFont(font);
        QString initial = title.isEmpty() ? "?" : title.left(1).toUpper();
        p.drawText(placeholder.rect(), Qt::AlignCenter, initial);
        p.end();
        imageLabel->setPixmap(placeholder);
    }

    layout->addWidget(m_imageWrap);

    // Title
    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setObjectName("TileTitle");
    m_titleLabel->setFixedWidth(CARD_WIDTH);
    m_titleLabel->setWordWrap(false);
    QFontMetrics fm(m_titleLabel->font());
    m_titleLabel->setText(fm.elidedText(title, Qt::ElideRight, CARD_WIDTH - 4));
    layout->addWidget(m_titleLabel);

    // Subtitle
    m_subtitleLabel = new QLabel(subtitle, this);
    m_subtitleLabel->setObjectName("TileSubtitle");
    m_subtitleLabel->setFixedWidth(CARD_WIDTH);
    layout->addWidget(m_subtitleLabel);

    layout->addStretch();
}

void TileCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QFrame::mousePressEvent(event);
}

void TileCard::enterEvent(QEnterEvent* event)
{
    m_imageWrap->setStyleSheet(
        "#TileImageWrap { border: 1px solid rgba(199,167,107,0.40); "
        "background: rgba(255,255,255,0.06); border-radius: 12px; }"
    );
    QFrame::enterEvent(event);
}

void TileCard::leaveEvent(QEvent* event)
{
    m_imageWrap->setStyleSheet("");
    QFrame::leaveEvent(event);
}
