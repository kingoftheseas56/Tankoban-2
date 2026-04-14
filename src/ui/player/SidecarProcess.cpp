#include "ui/player/SidecarProcess.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

static void debugLog(const QString& msg) {
    QFile f("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
    f.open(QIODevice::Append | QIODevice::Text);
    QTextStream s(&f);
    s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
}

// Path to the sidecar executable — shipped alongside the app
static QString sidecarPath()
{
    // Check next to the app exe first
    QString appDir = QCoreApplication::applicationDirPath();
    QString local = appDir + "/ffmpeg_sidecar.exe";
    if (QFile::exists(local))
        return local;

    // Fallback to GroundWorks build
    QString gw = "C:/Users/Suprabha/Desktop/TankobanQTGroundWork/resources/ffmpeg_sidecar/ffmpeg_sidecar.exe";
    if (QFile::exists(gw))
        return gw;

    return {};
}

SidecarProcess::SidecarProcess(QObject* parent)
    : QObject(parent)
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &SidecarProcess::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &SidecarProcess::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SidecarProcess::onProcessFinished);
}

SidecarProcess::~SidecarProcess()
{
    if (m_process->state() != QProcess::NotRunning) {
        sendShutdown();
        m_process->waitForFinished(3000);
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
    }
}

void SidecarProcess::start()
{
    QString path = sidecarPath();
    if (path.isEmpty()) {
        emit errorOccurred("ffmpeg_sidecar.exe not found");
        return;
    }

    debugLog("[Sidecar] starting: " + path);
    m_process->start(path, QStringList());
    if (!m_process->waitForStarted(5000)) {
        debugLog("[Sidecar] FAILED to start");
        emit errorOccurred("Failed to start ffmpeg_sidecar.exe");
    } else {
        debugLog("[Sidecar] started OK, pid=" + QString::number(m_process->processId()));
    }
}

bool SidecarProcess::isRunning() const
{
    return m_process->state() == QProcess::Running;
}

// ── Commands ────────────────────────────────────────────────────────────────

int SidecarProcess::sendCommand(const QString& name, const QJsonObject& payload)
{
    int seq = ++m_seq;
    QJsonObject cmd;
    cmd["type"] = "cmd";
    cmd["name"] = name;
    cmd["sessionId"] = m_sessionId;
    cmd["seq"] = seq;
    cmd["payload"] = payload;

    QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
    debugLog("[Sidecar] SEND: " + QString::fromUtf8(line.trimmed()));
    m_process->write(line);
    return seq;
}

int SidecarProcess::sendOpen(const QString& filePath, double startSeconds)
{
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject payload;
    // Don't mangle HTTP URLs with native separators
    payload["path"] = filePath.startsWith("http", Qt::CaseInsensitive)
        ? filePath : QDir::toNativeSeparators(filePath);
    payload["startSeconds"] = startSeconds;
    return sendCommand("open", payload);
}

int SidecarProcess::sendPause()   { return sendCommand("pause"); }
int SidecarProcess::sendResume()  { return sendCommand("resume"); }
int SidecarProcess::sendStop()    { return sendCommand("stop"); }
int SidecarProcess::sendShutdown(){ return sendCommand("shutdown"); }

int SidecarProcess::sendSeek(double positionSec)
{
    QJsonObject p;
    p["positionSec"] = positionSec;
    return sendCommand("seek", p);
}

int SidecarProcess::sendFrameStep(bool backward, double currentPosSec)
{
    QJsonObject p;
    p["backward"] = backward;
    if (backward)
        p["positionSec"] = currentPosSec;
    return sendCommand("frame_step", p);
}

int SidecarProcess::sendSetVolume(double volume)
{
    QJsonObject p;
    p["volume"] = volume;
    return sendCommand("set_volume", p);
}

int SidecarProcess::sendSetMute(bool muted)
{
    QJsonObject p;
    p["muted"] = muted;
    return sendCommand("set_mute", p);
}

int SidecarProcess::sendSetRate(double rate)
{
    QJsonObject p;
    p["rate"] = rate;
    return sendCommand("set_rate", p);
}

int SidecarProcess::sendSetTracks(const QString& audioId, const QString& subId)
{
    QJsonObject p;
    if (!audioId.isEmpty()) p["audio_id"] = audioId;
    if (!subId.isEmpty())   p["sub_id"]   = subId;
    return sendCommand("set_tracks", p);
}

int SidecarProcess::sendSetSubVisibility(bool visible)
{
    QJsonObject p;
    p["visible"] = visible;
    return sendCommand("set_sub_visibility", p);
}

int SidecarProcess::sendSetSubDelay(double delayMs)
{
    QJsonObject p;
    p["delay_ms"] = (int)delayMs;
    return sendCommand("set_sub_delay", p);
}

int SidecarProcess::sendSetAudioDelay(int delayMs)
{
    QJsonObject p;
    p["delay_ms"] = delayMs;
    return sendCommand("set_audio_delay", p);
}

int SidecarProcess::sendSetSubStyle(int fontSize, int marginV, bool outline)
{
    QJsonObject p;
    p["font_size"] = fontSize;
    p["margin_v"] = marginV;
    p["outline"] = outline;
    return sendCommand("set_sub_style", p);
}

int SidecarProcess::sendLoadExternalSub(const QString& path)
{
    QJsonObject p;
    p["path"] = path;
    return sendCommand("load_external_sub", p);
}

int SidecarProcess::sendSetFilters(bool deinterlace, int brightness, int contrast, int saturation, bool normalize, bool interpolate, const QString& deinterlaceFilter)
{
    QStringList videoParts;
    if (interpolate)
        videoParts << "minterpolate=fps=60:mi_mode=dup";
    // Use explicit deinterlace filter string if provided, else fall back to bool
    if (!deinterlaceFilter.isEmpty())
        videoParts << deinterlaceFilter;
    else if (deinterlace)
        videoParts << "yadif=mode=0";
    double b = brightness / 100.0;
    double c = contrast / 100.0;
    double s = saturation / 100.0;
    if (brightness != 0 || contrast != 100 || saturation != 100)
        videoParts << QString("eq=brightness=%1:contrast=%2:saturation=%3")
                          .arg(b, 0, 'f', 2).arg(c, 0, 'f', 2).arg(s, 0, 'f', 2);

    QJsonObject p;
    p["video"] = videoParts.join(",");
    p["audio"] = normalize ? QStringLiteral("loudnorm=I=-16") : QStringLiteral("");
    return sendCommand("set_filters", p);
}

int SidecarProcess::sendRawFilters(const QString& videoFilter, const QString& audioFilter)
{
    QJsonObject p;
    p["video"] = videoFilter;
    p["audio"] = audioFilter;
    return sendCommand("set_filters", p);
}

int SidecarProcess::sendSetToneMapping(const QString& algorithm, bool peakDetect)
{
    QJsonObject p;
    p["algorithm"] = algorithm;
    p["peak_detect"] = peakDetect;
    return sendCommand("set_tone_mapping", p);
}

int SidecarProcess::sendResize(int width, int height)
{
    QJsonObject p;
    p["width"] = width;
    p["height"] = height;
    return sendCommand("resize", p);
}

// ── Event parsing ───────────────────────────────────────────────────────────

void SidecarProcess::onReadyRead()
{
    m_readBuffer += m_process->readAllStandardOutput();

    while (true) {
        int nl = m_readBuffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = m_readBuffer.left(nl).trimmed();
        m_readBuffer = m_readBuffer.mid(nl + 1);
        if (!line.isEmpty())
            processLine(line);
    }
}

void SidecarProcess::processLine(const QByteArray& line)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError)
        return;

    QJsonObject obj = doc.object();
    if (obj["type"].toString() != "evt")
        return;

    QString name = obj["name"].toString();
    QJsonObject payload = obj["payload"].toObject();
    debugLog("[Sidecar] RECV: " + name);

    if (name == "ready") {
        emit ready();
    } else if (name == "first_frame") {
        emit firstFrame(payload);
    } else if (name == "time_update") {
        emit timeUpdate(payload["positionSec"].toDouble(),
                        payload["durationSec"].toDouble());
    } else if (name == "state_changed") {
        emit stateChanged(payload["state"].toString());
    } else if (name == "tracks_changed") {
        emit tracksChanged(
            payload["audio"].toArray(),
            payload["subtitle"].toArray(),
            payload["active_audio_id"].toString(),
            payload["active_sub_id"].toString());
    } else if (name == "eof") {
        emit endOfFile();
    } else if (name == "error") {
        emit errorOccurred(payload["message"].toString());
    } else if (name == "subtitle_text") {
        emit subtitleText(payload["text"].toString());
    } else if (name == "sub_visibility_changed") {
        emit subVisibilityChanged(payload["visible"].toBool());
    } else if (name == "sub_delay_changed") {
        emit subDelayChanged(payload["delay_ms"].toDouble());
    } else if (name == "filters_changed") {
        emit filtersChanged(payload);
    } else if (name == "frame_stepped") {
        emit frameStepped(payload["positionSec"].toDouble());
    } else if (name == "media_info") {
        emit mediaInfo(payload);
    } else if (name == "closed") {
        emit processClosed();
    }
}

void SidecarProcess::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    emit errorOccurred("Sidecar process error: " + m_process->errorString());
}

void SidecarProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);
}
