#include "devtools/DevControlServer.h"

#include "core/DebugLogBuffer.h"
#include "ui/MainWindow.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

DevControlServer::DevControlServer(MainWindow* window, QObject* parent)
    : QObject(parent), m_window(window)
{
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection,
            this, &DevControlServer::onNewConnection);
}

DevControlServer::~DevControlServer()
{
    stop();
}

bool DevControlServer::start()
{
    if (m_server->isListening())
        return true;

    if (!m_server->listen(QString::fromLatin1(kSocketName))) {
        // Stale pipe — remove and retry once. Mirrors the single-instance
        // pattern at src/main.cpp:51-72.
        QLocalServer::removeServer(QString::fromLatin1(kSocketName));
        if (!m_server->listen(QString::fromLatin1(kSocketName))) {
            DebugLogBuffer::instance().error(
                "devcontrol",
                QStringLiteral("Failed to listen on %1: %2")
                    .arg(QString::fromLatin1(kSocketName))
                    .arg(m_server->errorString()));
            return false;
        }
    }
    return true;
}

void DevControlServer::stop()
{
    if (m_server && m_server->isListening())
        m_server->close();
}

bool DevControlServer::isListening() const
{
    return m_server && m_server->isListening();
}

void DevControlServer::onNewConnection()
{
    while (auto* conn = m_server->nextPendingConnection())
        handleConnection(conn);
}

QByteArray DevControlServer::serialize(const QJsonObject& obj) const
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray DevControlServer::buildErrorReply(int seq, const char* code, const QString& message) const
{
    QJsonObject e;
    e["type"]    = "error";
    e["seq"]     = seq;
    e["code"]    = QString::fromLatin1(code);
    e["message"] = message;
    return serialize(e);
}

void DevControlServer::handleConnection(QLocalSocket* conn)
{
    // Cap UI-thread block at 500ms for the read.
    if (!conn->waitForReadyRead(500)) {
        conn->write(buildErrorReply(0, "BAD_JSON", QStringLiteral("no data within 500ms")));
        conn->waitForBytesWritten(500);
        conn->disconnectFromServer();
        conn->deleteLater();
        return;
    }

    const QByteArray bytes = conn->readAll();
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes.trimmed(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        conn->write(buildErrorReply(0, "BAD_JSON",
            QStringLiteral("parse error: %1").arg(parseErr.errorString())));
        conn->waitForBytesWritten(500);
        conn->disconnectFromServer();
        conn->deleteLater();
        return;
    }

    const QJsonObject req = doc.object();
    const QString cmd     = req.value("cmd").toString();
    const int seq         = req.value("seq").toInt(0);
    const QJsonObject payload = req.value("payload").toObject();

    if (cmd.isEmpty()) {
        conn->write(buildErrorReply(seq, "MISSING_CMD",
            QStringLiteral("request must include non-empty 'cmd' field")));
        conn->waitForBytesWritten(500);
        conn->disconnectFromServer();
        conn->deleteLater();
        return;
    }

    // Trace every accepted command into the ring buffer so `logs` can surface
    // dev-bridge activity to investigating agents.
    DebugLogBuffer::instance().info(
        "devcontrol",
        QStringLiteral("cmd=%1 seq=%2").arg(cmd).arg(seq));

    QJsonObject reply;
    if (!m_window) {
        QJsonObject e;
        e["type"]    = "error";
        e["seq"]     = seq;
        e["code"]    = "INTERNAL";
        e["message"] = "MainWindow gone";
        reply = e;
    } else {
        reply = m_window->handleDevCommand(cmd, seq, payload);
    }

    conn->write(serialize(reply));
    conn->waitForBytesWritten(500);
    conn->disconnectFromServer();
    conn->deleteLater();
}
