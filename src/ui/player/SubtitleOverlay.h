#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>

class SubtitleOverlay : public QWidget {
    Q_OBJECT

public:
    explicit SubtitleOverlay(QWidget* parent = nullptr);

    void setText(const QString& text);
    void setStyle(int fontSize, int marginPercent, bool outline);
    void setColors(const QString& fontColor, int bgOpacity);
    void reposition();
    void setControlsVisible(bool visible);

private:
    void applyStyleSheet();

    QLabel* m_label;
    QTimer  m_endTimer;
    int     m_fontSize       = 24;
    int     m_marginPercent  = 40;
    bool    m_outline        = true;
    bool    m_controlsVisible = false;
    QString m_fontColor      = "#ffffff";
    int     m_bgOpacity      = 140;
};
