#pragma once

#include <QWidget>
#include <QTimer>
#include <QPropertyAnimation>
#include <QSvgRenderer>

class CenterFlash : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit CenterFlash(QWidget* parent = nullptr);

    void flash(const QByteArray& svg);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QByteArray m_svg;
    qreal      m_opacity = 0.0;
    QTimer     m_holdTimer;
    QPropertyAnimation* m_fadeAnim = nullptr;
};
