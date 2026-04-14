#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QThread>
#include <memory>

struct SherpaOnnxOfflineTts;

class KokoroTtsEngine : public QObject {
    Q_OBJECT
public:
    explicit KokoroTtsEngine(const QString& modelDir, QObject* parent = nullptr);
    ~KokoroTtsEngine();

    bool isReady() const;
    int  sampleRate() const;    // 24000
    int  numSpeakers() const;

    // Voice metadata
    struct VoiceInfo {
        QString id;       // e.g. "af_alloy"
        QString name;     // e.g. "Alloy"
        QString lang;     // e.g. "en-US"
        QString gender;   // "Female" or "Male"
        int     sid;      // speaker ID for sherpa-onnx
    };
    QList<VoiceInfo> englishVoices() const;
    int speakerIdForVoice(const QString& voiceId) const;

    // Blocking synthesis — returns raw PCM float32 mono at sampleRate()
    QByteArray synthesize(const QString& text, int speakerId = 0, float speed = 1.0f);

    // Async synthesis on worker thread — emits audioChunk / synthesisComplete
    void synthesizeAsync(const QString& text, int speakerId = 0, float speed = 1.0f);
    void cancelAsync();

signals:
    void audioChunk(const QByteArray& pcmFloat32, int sampleRate);
    void synthesisComplete();
    void error(const QString& message);

private:
    void initVoiceTable();

    const SherpaOnnxOfflineTts* m_tts = nullptr;
    QList<VoiceInfo> m_voices;
    QThread* m_workerThread = nullptr;
    bool m_cancelRequested = false;
    QString m_modelDir;
};
