#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QIcon>

class FfmpegDecoder;
class FrameCanvas;

class VideoPlayer : public QWidget {
    Q_OBJECT

public:
    explicit VideoPlayer(QWidget* parent = nullptr);
    ~VideoPlayer() override;

    void openFile(const QString& filePath);

signals:
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUI();
    void togglePause();
    void updatePlayPauseIcon();
    void onPositionChanged(qint64 ptsMs);
    void onSeek(int sliderValue);
    void onPlaybackFinished();
    void showControls();
    void hideControls();

    static QString formatTime(qint64 ms);
    static QIcon iconFromSvg(const QByteArray& svg, int size = 20);

    // Components
    FfmpegDecoder* m_decoder = nullptr;
    FrameCanvas*   m_canvas  = nullptr;

    // Controls
    QWidget*     m_controlBar  = nullptr;
    QPushButton* m_backBtn     = nullptr;
    QPushButton* m_playPauseBtn = nullptr;
    QSlider*     m_seekBar     = nullptr;
    QLabel*      m_timeLabel   = nullptr;

    // Icons
    QIcon m_playIcon;
    QIcon m_pauseIcon;
    QIcon m_backIcon;

    // State
    bool   m_paused   = false;
    bool   m_seeking  = false;
    qint64 m_duration = 0;

    // Auto-hide
    QTimer m_hideTimer;
};
