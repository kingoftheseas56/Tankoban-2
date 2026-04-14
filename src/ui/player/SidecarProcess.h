#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
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
    int sendFrameStep(bool backward = false, double currentPosSec = 0.0);
    int sendStop();
    int sendSetVolume(double volume);
    int sendSetMute(bool muted);
    int sendSetRate(double rate);
    int sendSetTracks(const QString& audioId, const QString& subId);
    int sendSetSubVisibility(bool visible);
    int sendSetSubDelay(double delayMs);
    int sendSetAudioDelay(int delayMs);
    int sendSetSubStyle(int fontSize, int marginV, bool outline);
    int sendLoadExternalSub(const QString& path);
    int sendSetFilters(bool deinterlace, int brightness, int contrast, int saturation, bool normalize, bool interpolate = false, const QString& deinterlaceFilter = {});
    int sendRawFilters(const QString& videoFilter, const QString& audioFilter);
    int sendSetToneMapping(const QString& algorithm, bool peakDetect);
    int sendResize(int width, int height);
    int sendShutdown();

signals:
    void ready();
    void firstFrame(const QJsonObject& payload);   // shmName, width, height, slotCount, slotBytes
    void timeUpdate(double positionSec, double durationSec);
    void stateChanged(const QString& state);
    void tracksChanged(const QJsonArray& audio, const QJsonArray& subtitle,
                       const QString& activeAudioId, const QString& activeSubId);
    void endOfFile();
    void errorOccurred(const QString& message);
    void subtitleText(const QString& text);
    void subVisibilityChanged(bool visible);
    void subDelayChanged(double delayMs);
    void filtersChanged(const QJsonObject& state);
    void mediaInfo(const QJsonObject& info);
    void frameStepped(double positionSec);
    void processClosed();

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
