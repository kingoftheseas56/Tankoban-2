#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>

class VolumeHud : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit VolumeHud(QWidget* parent = nullptr);

    void showVolume(int percent, bool muted);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int   m_percent = 100;
    bool  m_muted   = false;
    qreal m_opacity = 0.0;

    QTimer m_hideTimer;
    QPropertyAnimation* m_fadeAnim = nullptr;
};
