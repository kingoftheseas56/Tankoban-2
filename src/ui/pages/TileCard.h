#pragma once

#include <QFrame>
#include <QLabel>

class TileCard : public QFrame {
    Q_OBJECT
public:
    explicit TileCard(const QString& thumbPath,
                      const QString& title,
                      const QString& subtitle,
                      QWidget* parent = nullptr);

    static constexpr int CARD_WIDTH  = 180;
    static constexpr int IMAGE_HEIGHT = 252;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QFrame* m_imageWrap = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
};
