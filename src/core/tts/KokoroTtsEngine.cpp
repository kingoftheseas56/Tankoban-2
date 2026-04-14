#include "KokoroTtsEngine.h"

#ifdef HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#endif

#include <QDir>
#include <QDebug>
#include <QTimer>
#include <cstring>

// ─── Voice table ────────────────────────────────────────────────────
// Kokoro multi-lang v1.1 English voices — sid values are indices into voices.bin
// Ordering matches the sherpa-onnx kokoro speaker list

struct VoiceEntry {
    const char* id;
    const char* name;
    const char* lang;
    const char* gender;
    int sid;
};

static const VoiceEntry kEnglishVoices[] = {
    // American Female
    {"af_alloy",   "Alloy",    "en-US", "Female", 0},
    {"af_aoede",   "Aoede",    "en-US", "Female", 1},
    {"af_bella",   "Bella",    "en-US", "Female", 2},
    {"af_heart",   "Heart",    "en-US", "Female", 3},
    {"af_jessica", "Jessica",  "en-US", "Female", 4},
    {"af_kore",    "Kore",     "en-US", "Female", 5},
    {"af_nicole",  "Nicole",   "en-US", "Female", 6},
    {"af_nova",    "Nova",     "en-US", "Female", 7},
    {"af_river",   "River",    "en-US", "Female", 8},
    {"af_sarah",   "Sarah",    "en-US", "Female", 9},
    {"af_sky",     "Sky",      "en-US", "Female", 10},
    // American Male
    {"am_adam",    "Adam",     "en-US", "Male",   11},
    {"am_echo",    "Echo",     "en-US", "Male",   12},
    {"am_eric",    "Eric",     "en-US", "Male",   13},
    {"am_fenrir",  "Fenrir",   "en-US", "Male",   14},
    {"am_liam",    "Liam",     "en-US", "Male",   15},
    {"am_michael", "Michael",  "en-US", "Male",   16},
    {"am_onyx",    "Onyx",     "en-US", "Male",   17},
    {"am_puck",    "Puck",     "en-US", "Male",   18},
    {"am_santa",   "Santa",    "en-US", "Male",   19},
    // British Female
    {"bf_alice",    "Alice",    "en-GB", "Female", 20},
    {"bf_emma",     "Emma",     "en-GB", "Female", 21},
    {"bf_isabella", "Isabella", "en-GB", "Female", 22},
    {"bf_lily",     "Lily",     "en-GB", "Female", 23},
    // British Male
    {"bm_daniel",  "Daniel",   "en-GB", "Male",   24},
    {"bm_fable",   "Fable",    "en-GB", "Male",   25},
    {"bm_george",  "George",   "en-GB", "Male",   26},
    {"bm_lewis",   "Lewis",    "en-GB", "Male",   27},
};
static constexpr int kEnglishVoiceCount = sizeof(kEnglishVoices) / sizeof(kEnglishVoices[0]);

// ─── Constructor ────────────────────────────────────────────────────

KokoroTtsEngine::KokoroTtsEngine(const QString& modelDir, QObject* parent)
    : QObject(parent)
    , m_modelDir(modelDir)
{
    initVoiceTable();

#ifndef HAS_SHERPA_ONNX
    qWarning() << "KokoroTtsEngine: built without sherpa-onnx, TTS disabled";
    return;
#else
    QDir dir(modelDir);

    // Try model.int8.onnx first (quantized), fall back to model.onnx
    QString modelFile = dir.exists("model.int8.onnx") ? "model.int8.onnx" : "model.onnx";
    if (!dir.exists(modelFile) || !dir.exists("voices.bin") || !dir.exists("tokens.txt")) {
        qWarning() << "KokoroTtsEngine: model files not found in" << modelDir;
        return;
    }

    // Build config using C API — all strings must outlive the config
    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    std::string modelPath   = dir.filePath(modelFile).toStdString();
    std::string voicesPath  = dir.filePath("voices.bin").toStdString();
    std::string tokensPath  = dir.filePath("tokens.txt").toStdString();
    std::string dataDir     = dir.filePath("espeak-ng-data").toStdString();
    std::string lexiconUs   = dir.filePath("lexicon-us-en.txt").toStdString();
    std::string lexiconGb   = dir.filePath("lexicon-gb-en.txt").toStdString();
    std::string dictDir     = dir.filePath("dict").toStdString();

    config.model.kokoro.model    = modelPath.c_str();
    config.model.kokoro.voices   = voicesPath.c_str();
    config.model.kokoro.tokens   = tokensPath.c_str();
    config.model.kokoro.data_dir = dataDir.c_str();
    config.model.kokoro.length_scale = 1.0f;

    // Build comma-separated lexicon list
    std::string lexicons;
    if (QFile::exists(dir.filePath("lexicon-us-en.txt")))
        lexicons = lexiconUs;
    if (QFile::exists(dir.filePath("lexicon-gb-en.txt"))) {
        if (!lexicons.empty()) lexicons += ",";
        lexicons += lexiconGb;
    }
    if (!lexicons.empty())
        config.model.kokoro.lexicon = lexicons.c_str();

    if (dir.exists("dict"))
        config.model.kokoro.dict_dir = dictDir.c_str();

    config.model.num_threads  = 2;
    config.model.debug        = 0;
    config.model.provider     = "cpu";
    config.max_num_sentences   = 2;
    config.silence_scale       = 1.0f;

    m_tts = SherpaOnnxCreateOfflineTts(&config);
    if (!m_tts) {
        qWarning() << "KokoroTtsEngine: failed to create TTS engine";
        return;
    }

    qDebug() << "KokoroTtsEngine: ready."
             << "Sample rate:" << sampleRate()
             << "Speakers:" << numSpeakers();
#endif // HAS_SHERPA_ONNX
}

KokoroTtsEngine::~KokoroTtsEngine()
{
    cancelAsync();
#ifdef HAS_SHERPA_ONNX
    if (m_tts)
        SherpaOnnxDestroyOfflineTts(m_tts);
#endif
}

// ─── Queries ────────────────────────────────────────────────────────

bool KokoroTtsEngine::isReady() const
{
    return m_tts != nullptr;
}

int KokoroTtsEngine::sampleRate() const
{
#ifdef HAS_SHERPA_ONNX
    return m_tts ? SherpaOnnxOfflineTtsSampleRate(m_tts) : 24000;
#else
    return 24000;
#endif
}

int KokoroTtsEngine::numSpeakers() const
{
#ifdef HAS_SHERPA_ONNX
    return m_tts ? SherpaOnnxOfflineTtsNumSpeakers(m_tts) : 0;
#else
    return 0;
#endif
}

// ─── Voice table ────────────────────────────────────────────────────

void KokoroTtsEngine::initVoiceTable()
{
    m_voices.clear();
    m_voices.reserve(kEnglishVoiceCount);
    for (int i = 0; i < kEnglishVoiceCount; ++i) {
        const auto& v = kEnglishVoices[i];
        m_voices.append({
            QString::fromUtf8(v.id),
            QString::fromUtf8(v.name),
            QString::fromUtf8(v.lang),
            QString::fromUtf8(v.gender),
            v.sid
        });
    }
}

QList<KokoroTtsEngine::VoiceInfo> KokoroTtsEngine::englishVoices() const
{
    return m_voices;
}

int KokoroTtsEngine::speakerIdForVoice(const QString& voiceId) const
{
    for (const auto& v : m_voices) {
        if (v.id == voiceId)
            return v.sid;
    }
    return 0; // default to first voice
}

// ─── Blocking synthesis ─────────────────────────────────────────────

QByteArray KokoroTtsEngine::synthesize(const QString& text, int speakerId, float speed)
{
#ifdef HAS_SHERPA_ONNX
    if (!m_tts) {
        emit error("TTS engine not initialized");
        return {};
    }

    std::string utf8 = text.toStdString();
    const SherpaOnnxGeneratedAudio* audio =
        SherpaOnnxOfflineTtsGenerate(m_tts, utf8.c_str(), speakerId, speed);

    if (!audio || audio->n <= 0) {
        emit error("Synthesis produced no audio");
        if (audio)
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return {};
    }

    QByteArray pcm(reinterpret_cast<const char*>(audio->samples),
                   static_cast<int>(audio->n * sizeof(float)));

    int sr = audio->sample_rate;
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    qDebug() << "KokoroTtsEngine: synthesized"
             << (pcm.size() / sizeof(float)) << "samples at" << sr << "Hz"
             << "=" << (pcm.size() / sizeof(float) / (float)sr) << "seconds";

    return pcm;
#else
    Q_UNUSED(text); Q_UNUSED(speakerId); Q_UNUSED(speed);
    emit error("TTS not available (built without sherpa-onnx)");
    return {};
#endif
}

// ─── Async synthesis ────────────────────────────────────────────────

#ifdef HAS_SHERPA_ONNX
// Progress callback — called by sherpa-onnx as audio chunks are generated
static int32_t ttsProgressCallback(const float* samples, int32_t n, float /*progress*/, void* arg)
{
    auto* engine = static_cast<KokoroTtsEngine*>(arg);
    if (!engine)
        return 0;

    if (n > 0) {
        QByteArray chunk(reinterpret_cast<const char*>(samples),
                         static_cast<int>(n * sizeof(float)));
        QMetaObject::invokeMethod(engine, [engine, chunk]() {
            emit engine->audioChunk(chunk, 24000);
        }, Qt::QueuedConnection);
    }

    return 1;
}
#endif

void KokoroTtsEngine::synthesizeAsync(const QString& text, int speakerId, float speed)
{
#ifdef HAS_SHERPA_ONNX
    if (!m_tts) {
        emit error("TTS engine not initialized");
        return;
    }

    cancelAsync();
    m_cancelRequested = false;

    m_workerThread = QThread::create([this, text, speakerId, speed]() {
        std::string utf8 = text.toStdString();

        const SherpaOnnxGeneratedAudio* audio =
            SherpaOnnxOfflineTtsGenerateWithProgressCallbackWithArg(
                m_tts, utf8.c_str(), speakerId, speed,
                ttsProgressCallback, this);

        if (audio)
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

        QMetaObject::invokeMethod(this, [this]() {
            emit synthesisComplete();
        }, Qt::QueuedConnection);
    });

    m_workerThread->start();
#else
    Q_UNUSED(text); Q_UNUSED(speakerId); Q_UNUSED(speed);
    emit error("TTS not available (built without sherpa-onnx)");
#endif
}

void KokoroTtsEngine::cancelAsync()
{
    m_cancelRequested = true;
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}
