#include "core/stream/rqbit/RqbitProcess.h"
#include "core/net/NetSeam.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

namespace tankostream::rqbit {

RqbitProcess::RqbitProcess(QObject* parent)
    : QObject(parent)
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::errorOccurred,
            this, &RqbitProcess::onProcessError);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RqbitProcess::onProcessFinished);
}

RqbitProcess::~RqbitProcess()
{
    stop();
}

// Mirrors the ffmpeg sidecar discovery: next-to-exe first (the POST_BUILD copy),
// then the repo resources/ path for a run-from-repo dev launch.
QString RqbitProcess::binaryPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/rqbit.exe"),
        appDir + QStringLiteral("/../resources/rqbit/rqbit.exe"),
        QDir::currentPath() + QStringLiteral("/resources/rqbit/rqbit.exe"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c))
            return QDir::cleanPath(c);
    }
    return {};
}

int RqbitProcess::pickFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = probe.serverPort();
    probe.close();
    return port;
}

void RqbitProcess::start(const QString& downloadDir)
{
    if (m_proc->state() != QProcess::NotRunning)
        return;  // already started

    const QString bin = binaryPath();
    if (bin.isEmpty()) {
        emit failed(QStringLiteral("rqbit.exe not found beside the app or in resources/rqbit/"));
        return;
    }

    QDir().mkpath(downloadDir);  // rqbit creates it too, but be explicit

    m_port = pickFreePort();
    if (m_port == 0) {
        emit failed(QStringLiteral("could not allocate a local port for rqbit"));
        return;
    }

    m_ready = false;
    m_intentionalShutdown = false;
    m_healthAttempts = 0;

    const QStringList args = {
        QStringLiteral("--http-api-listen-addr"),
        QStringLiteral("127.0.0.1:%1").arg(m_port),
        QStringLiteral("-v"), QStringLiteral("info"),
        QStringLiteral("server"), QStringLiteral("start"),
        QDir::toNativeSeparators(downloadDir),
    };
    qInfo("[rqbit] launching %s on 127.0.0.1:%d (dir=%s)",
          qUtf8Printable(bin), m_port, qUtf8Printable(downloadDir));
    m_proc->start(bin, args);
    // Begin polling immediately; connection-refused until the server binds,
    // then the first 200 flips us ready.
    QTimer::singleShot(kHealthIntervalMs, this, &RqbitProcess::pollHealth);
}

void RqbitProcess::pollHealth()
{
    if (m_ready || m_intentionalShutdown)
        return;
    if (m_proc->state() == QProcess::NotRunning)
        return;  // onProcessError/onProcessFinished will have surfaced the failure
    if (++m_healthAttempts > kHealthMaxAttempts) {
        emit failed(QStringLiteral("rqbit health-check timed out (no HTTP API after ~%1s)")
                        .arg((kHealthIntervalMs * kHealthMaxAttempts) / 1000));
        stop();
        return;
    }
    if (!m_nam)
        m_nam = tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("rqbit-health"));
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/torrents").arg(m_port)));
    req.setTransferTimeout(2000);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_ready || m_intentionalShutdown)
            return;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && http == 200) {
            m_ready = true;
            qInfo("[rqbit] ready on 127.0.0.1:%d", m_port);
            emit ready(m_port);
        } else {
            QTimer::singleShot(kHealthIntervalMs, this, &RqbitProcess::pollHealth);
        }
    });
}

void RqbitProcess::stop()
{
    m_intentionalShutdown = true;
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        // rqbit is a console app; QProcess::terminate posts WM_CLOSE which it
        // ignores on Windows, so fall through to kill() after a short grace.
        if (!m_proc->waitForFinished(1500)) {
            m_proc->kill();
            m_proc->waitForFinished(500);
        }
    }
    m_ready = false;
}

void RqbitProcess::onProcessError(QProcess::ProcessError error)
{
    if (m_intentionalShutdown)
        return;
    emit failed(QStringLiteral("rqbit process error (%1): %2")
                    .arg(QString::number(static_cast<int>(error)),
                         m_proc ? m_proc->errorString() : QString()));
}

void RqbitProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_intentionalShutdown)
        return;  // expected exit during stop()
    emit failed(QStringLiteral("rqbit exited unexpectedly (code %1, %2)")
                    .arg(exitCode)
                    .arg(status == QProcess::CrashExit ? QStringLiteral("crash")
                                                       : QStringLiteral("normal")));
}

} // namespace tankostream::rqbit
