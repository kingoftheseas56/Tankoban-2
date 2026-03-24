#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <atomic>

// Manages the ffmpeg_sidecar.exe subprocess.
// Sends JSON commands on stdin, receives JSON events from stdout.
class SidecarProcess : public QObject {
    Q_OBJECT

public:
    explicit SidecarProcess(QObject* parent = nullptr);
    ~SidecarProcess() override;

    void start();
    bool isRunning() const;

    // Commands — returns seq number
    int sendOpen(const QString& filePath, double startSeconds = 0.0);
    int sendPause();
    int sendResume();
    int sendSeek(double positionSec);
    int sendStop();
    int sendSetVolume(double volume);
    int sendSetMute(bool muted);
    int sendShutdown();

signals:
    void ready();
    void firstFrame(const QJsonObject& payload);   // shmName, width, height, slotCount, slotBytes
    void timeUpdate(double positionSec, double durationSec);
    void stateChanged(const QString& state);
    void endOfFile();
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    int sendCommand(const QString& name, const QJsonObject& payload = {});
    void processLine(const QByteArray& line);

    QProcess* m_process = nullptr;
    QString   m_sessionId;
    std::atomic<int> m_seq{0};
    QByteArray m_readBuffer;
};
