#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QIcon>

class SidecarProcess;
class ShmFrameReader;
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
    void stopPlayback();
    void showControls();
    void hideControls();

    // Sidecar event handlers
    void onSidecarReady();
    void onFirstFrame(const QJsonObject& payload);
    void onTimeUpdate(double positionSec, double durationSec);
    void onStateChanged(const QString& state);
    void onEndOfFile();
    void onError(const QString& message);

    static QString formatTime(qint64 ms);
    static QIcon iconFromSvg(const QByteArray& svg, int size = 20);

    // Components
    SidecarProcess* m_sidecar = nullptr;
    ShmFrameReader* m_reader  = nullptr;
    FrameCanvas*    m_canvas  = nullptr;

    // Pending open (file path stored until sidecar is ready)
    QString m_pendingFile;
    double  m_pendingStartSec = 0.0;

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
    qint64 m_durationMs = 0;

    // Auto-hide
    QTimer m_hideTimer;
};
