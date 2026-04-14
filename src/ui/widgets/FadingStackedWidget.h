#pragma once

#include <QStackedWidget>

class QPropertyAnimation;

class FadingStackedWidget : public QStackedWidget {
    Q_OBJECT
public:
    explicit FadingStackedWidget(QWidget* parent = nullptr);

    void setCurrentIndexAnimated(int index);
    void setFadeDuration(int ms) { m_duration = ms; }

private:
    int m_duration = 200;
};
