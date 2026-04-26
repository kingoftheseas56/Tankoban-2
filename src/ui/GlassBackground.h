#pragma once

#include <QWidget>
#include <QTimer>
#include <QColor>
#include <vector>

struct GlassBlob {
    float cxRatio;
    float cyRatio;
    int   radius;
    QColor color;
};

class GlassBackground : public QWidget {
    Q_OBJECT
public:
    explicit GlassBackground(QWidget* parent = nullptr);

    void setBlobs(const std::vector<GlassBlob>& blobs);
    void setBaseColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void tick();

private:
    double m_phase = 0.0;
    QColor m_baseColor{5, 5, 5};   // override hook; only used if m_baseColorOverride
    bool   m_baseColorOverride = false;  // set by setBaseColor; otherwise canvas
                                         // tracks Theme::current().bg0
    QTimer m_timer;
    std::vector<GlassBlob> m_blobs;
};
