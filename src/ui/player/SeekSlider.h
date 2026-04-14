#pragma once
#include <QSlider>

class SeekSlider : public QSlider {
    Q_OBJECT
public:
    explicit SeekSlider(Qt::Orientation o, QWidget* parent = nullptr);
    void setDurationSec(double dur);

    static constexpr int RANGE = 10000;

signals:
    void hoverPositionChanged(double fraction);
    void hoverLeft();

protected:
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRect grooveRect() const;
    double fractionForX(int x) const;

    double m_durationSec = 0.0;
};
