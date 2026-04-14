#pragma once

#include <QFrame>
#include <QLabel>
#include <QPixmap>

class TileCard : public QFrame {
    Q_OBJECT
public:
    explicit TileCard(const QString& thumbPath,
                      const QString& title,
                      const QString& subtitle,
                      QWidget* parent = nullptr);

    void setCardSize(int width, int imageHeight);
    void setBadges(double progressFraction, const QString& pageBadge = {},
                   const QString& countBadge = {}, const QString& status = {});
    void setIsNew(bool isNew);
    void setIsFolder(bool isFolder);
    void setThumbPath(const QString& path);
    void setSelected(bool selected);
    void setFocused(bool focused);

    bool isSelected() const { return m_selected; }
    bool isFocused() const  { return m_focused; }
    int cardWidth() const   { return m_cardWidth; }
    int imageHeight() const { return m_imageHeight; }

    static constexpr int DEFAULT_WIDTH  = 200;
    static constexpr int DEFAULT_IMAGE_HEIGHT = 308;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void applyBadges();
    void updateBorder();
    static QPixmap roundPixmap(const QPixmap& src, int radius);

    QFrame* m_imageWrap = nullptr;
    QLabel* m_imageLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;

    QString m_thumbPath;
    QString m_title;
    QPixmap m_basePixmap;

    int m_cardWidth = DEFAULT_WIDTH;
    int m_imageHeight = DEFAULT_IMAGE_HEIGHT;

    double  m_progressFraction = 0.0;
    QString m_pageBadge;
    QString m_countBadge;
    QString m_status;
    bool    m_isNew    = false;
    bool    m_isFolder = false;
    bool    m_selected = false;
    bool    m_focused  = false;
    bool    m_hovered  = false;
    bool    m_flashing = false;
};
