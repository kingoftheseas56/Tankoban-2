#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

class ToastHud : public QWidget {
    Q_OBJECT

public:
    explicit ToastHud(QWidget* parent = nullptr);

    void showToast(const QString& message);

private slots:
    void startFadeOut();

private:
    QLabel*                m_label      = nullptr;
    QGraphicsOpacityEffect* m_effect    = nullptr;
    QPropertyAnimation*    m_fadeAnim   = nullptr;
    QTimer                 m_holdTimer;
};
