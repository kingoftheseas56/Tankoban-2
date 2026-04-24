#include "core/stream/stremio/StreamServerProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QtGlobal>

StreamServerProcess::StreamServerProcess(QObject* parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &StreamServerProcess::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &StreamServerProcess::onReadyReadStandardError);
    connect(m_process, &QProcess::errorOccurred,
            this, &StreamServerProcess::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &StreamServerProcess::onProcessFinished);

    m_readinessTimer = new QTimer(this);
    m_readinessTimer->setSingleShot(true);
    m_readinessTimer->setInterval(kReadinessTimeoutMs);
    connect(m_readinessTimer, &QTimer::timeout,
            this, &StreamServerProcess::onReadinessTimeout);
}

StreamServerProcess::~StreamServerProcess()
{
    shutdown();
}

// Binary discovery mirrors SidecarProcess.cpp:37-68 — prefer next-to-exe in
// production, fall back to resources/ dir relative to exe, then to CWD's
// resources/ for raw "run from repo root" dev invocation.
QString StreamServerProcess::discoverBinaryPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/stream_server/stremio-runtime.exe"),
        appDir + QStringLiteral("/../resources/stream_server/stremio-runtime.exe"),
        QDir::currentPath() + QStringLiteral("/resources/stream_server/stremio-runtime.exe"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            return QDir::cleanPath(c);
        }
    }
    return {};
}

bool StreamServerProcess::start(const QString& cacheDir)
{
    if (isRunning()) {
        qWarning("StreamServerProcess::start: already running");
        return true;
    }

    const QString binary = discoverBinaryPath();
    if (binary.isEmpty()) {
        const QString msg = QStringLiteral(
            "StreamServerProcess: stremio-runtime.exe not found in "
            "<appDir>/stream_server/, <appDir>/../resources/stream_server/, or "
            "resources/stream_server/ — did the post-build copy run?");
        qWarning().noquote() << msg;
        emit errorOccurred(msg);
        return false;
    }

    const QString binaryDir = QFileInfo(binary).absolutePath();

    // Ensure cacheDir exists so server.js doesn't bomb trying to mkdir.
    if (!cacheDir.isEmpty()) {
        QDir().mkpath(cacheDir);
    }

    // Process environment: inherit system, then override our two knobs.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NO_HTTPS_SERVER"), QStringLiteral("1"));
    if (!cacheDir.isEmpty()) {
        env.insert(QStringLiteral("APP_PATH"), QDir::toNativeSeparators(cacheDir));
    }
    m_process->setProcessEnvironment(env);

    // Working dir = binary dir so server.js's internal relative paths
    // (ffmpeg discovery, node_modules lookups if any) resolve correctly.
    m_process->setWorkingDirectory(binaryDir);

    // Reset per-launch state.
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_httpPort.store(0, std::memory_order_release);
    m_readyEmitted = false;
    m_intentionalShutdown = false;

    qInfo().noquote() << "StreamServerProcess: launching"
                       << binary << "from" << binaryDir
                       << "cache=" << cacheDir;

    m_process->start(binary, QStringList{QStringLiteral("server.js")});
    if (!m_process->waitForStarted(5000)) {
        const QString msg = QStringLiteral("StreamServerProcess: waitForStarted timeout — ")
                          + m_process->errorString();
        qWarning().noquote() << msg;
        emit errorOccurred(msg);
        return false;
    }

    m_readinessTimer->start();
    return true;
}

bool StreamServerProcess::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void StreamServerProcess::shutdown()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) return;

    m_intentionalShutdown = true;
    if (m_readinessTimer) m_readinessTimer->stop();

    m_process->terminate();
    if (!m_process->waitForFinished(2000)) {
        qWarning("StreamServerProcess::shutdown: terminate timeout, killing");
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

// Line-buffered stdout consumer. Stream-server emits the readiness signal
// as a single line `EngineFS server started at http://127.0.0.1:<PORT>`
// (server.js:46853). We parse that line once, emit ready(), then keep the
// handler registered so later lines don't leak to stdout loss. All stdout
// is also logged at qDebug level for post-hoc diagnosis.
void StreamServerProcess::onReadyReadStandardOutput()
{
    m_stdoutBuffer += m_process->readAllStandardOutput();
    int lineBreak;
    while ((lineBreak = m_stdoutBuffer.indexOf('\n')) != -1) {
        const QByteArray line = m_stdoutBuffer.left(lineBreak).trimmed();
        m_stdoutBuffer.remove(0, lineBreak + 1);
        if (line.isEmpty()) continue;
        qDebug().noquote() << "[stream-server stdout]" << line;

        if (!m_readyEmitted) {
            // Regex avoids allocating QStringList for every line. Matches
            // "EngineFS server started at http://127.0.0.1:11470" or similar.
            static const QRegularExpression re(
                QStringLiteral(R"(EngineFS server started at http://127\.0\.0\.1:(\d+))"));
            const auto match = re.match(QString::fromUtf8(line));
            if (match.hasMatch()) {
                const int port = match.captured(1).toInt();
                m_httpPort.store(port, std::memory_order_release);
                m_readyEmitted = true;
                if (m_readinessTimer) m_readinessTimer->stop();
                qInfo() << "StreamServerProcess: ready on port" << port;
                emit ready(port);
            }
        }
    }
}

void StreamServerProcess::onReadyReadStandardError()
{
    m_stderrBuffer += m_process->readAllStandardError();
    int lineBreak;
    while ((lineBreak = m_stderrBuffer.indexOf('\n')) != -1) {
        const QByteArray line = m_stderrBuffer.left(lineBreak).trimmed();
        m_stderrBuffer.remove(0, lineBreak + 1);
        if (!line.isEmpty()) {
            qDebug().noquote() << "[stream-server stderr]" << line;
        }
    }
}

void StreamServerProcess::onProcessError(QProcess::ProcessError error)
{
    QString msg;
    switch (error) {
    case QProcess::FailedToStart: msg = QStringLiteral("FailedToStart"); break;
    case QProcess::Crashed:       msg = QStringLiteral("Crashed"); break;
    case QProcess::Timedout:      msg = QStringLiteral("Timedout"); break;
    case QProcess::WriteError:    msg = QStringLiteral("WriteError"); break;
    case QProcess::ReadError:     msg = QStringLiteral("ReadError"); break;
    default:                      msg = QStringLiteral("UnknownError"); break;
    }
    msg = QStringLiteral("StreamServerProcess error: ") + msg
        + QStringLiteral(" — ") + m_process->errorString();
    qWarning().noquote() << msg;
    emit errorOccurred(msg);
}

void StreamServerProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    if (m_readinessTimer) m_readinessTimer->stop();
    if (!m_intentionalShutdown) {
        qWarning() << "StreamServerProcess: unexpected exit, code=" << exitCode;
        emit crashed(exitCode);
    } else {
        qInfo() << "StreamServerProcess: graceful exit, code=" << exitCode;
    }
}

void StreamServerProcess::onReadinessTimeout()
{
    if (m_readyEmitted) return;
    const QString msg = QStringLiteral(
        "StreamServerProcess: readiness timeout — stream-server did not emit "
        "its 'EngineFS server started' line within 15s. Likely cause: all of "
        "ports 11470-11474 occupied (Stremio Desktop running concurrently?).");
    qWarning().noquote() << msg;
    emit errorOccurred(msg);
    shutdown();
}
