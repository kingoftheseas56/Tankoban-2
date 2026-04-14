#pragma once

#include <QWidget>
#include <QPropertyAnimation>

class ShortcutsOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit ShortcutsOverlay(QWidget* parent = nullptr);

    void toggle();
    bool isShowing() const { return m_showing; }

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    bool  m_showing = false;
    qreal m_opacity = 0.0;
    QPropertyAnimation* m_fadeAnim = nullptr;
};
