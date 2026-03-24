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
    payload["path"] = QDir::toNativeSeparators(filePath);
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
    } else if (name == "eof") {
        emit endOfFile();
    } else if (name == "error") {
        emit errorOccurred(payload["message"].toString());
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
