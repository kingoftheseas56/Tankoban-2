#include "devtools/DevControlServer.h"

#include "core/DebugLogBuffer.h"
#include "ui/MainWindow.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDateTime>
#include <QTimer>
#include <QUuid>

DevControlServer::DevControlServer(MainWindow* window, QObject* parent)
    : QObject(parent), m_window(window)
{
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection,
            this, &DevControlServer::onNewConnection);

    m_leaseCleanupTimer = new QTimer(this);
    m_leaseCleanupTimer->setInterval(5000);
    connect(m_leaseCleanupTimer, &QTimer::timeout,
            this, &DevControlServer::cleanupExpiredLeases);
    m_leaseCleanupTimer->start();
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

QJsonObject DevControlServer::buildLeaseReply(int seq, QJsonObject extras) const
{
    extras["type"] = "reply";
    extras["seq"] = seq;
    return extras;
}

QJsonObject DevControlServer::buildLeaseError(int seq, const QString& reason) const
{
    return buildLeaseReply(seq, {
        {"status", QStringLiteral("ERROR")},
        {"reason", reason}
    });
}

void DevControlServer::expireLeaseIfStale(const QString& lane, qint64 nowMs)
{
    auto it = m_leases.find(lane);
    if (it == m_leases.end())
        return;
    if (it->expiryMs > nowMs)
        return;

    m_expiredPriorHolders.insert(lane, it->holder);
    m_leases.erase(it);
}

void DevControlServer::cleanupExpiredLeases()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QList<QString> lanes = m_leases.keys();
    for (const QString& lane : lanes)
        expireLeaseIfStale(lane, nowMs);
}

QJsonObject DevControlServer::handleLeaseCommand(const QString& cmd, int seq, const QJsonObject& payload)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    if (cmd == QLatin1String("lease_list")) {
        cleanupExpiredLeases();
        QJsonArray leases;
        for (auto it = m_leases.cbegin(); it != m_leases.cend(); ++it) {
            const Lease& lease = it.value();
            QJsonObject row;
            row["lane"] = lease.lane;
            row["holder"] = lease.holder;
            row["purpose"] = lease.purpose;
            row["expiry_ms"] = static_cast<double>(lease.expiryMs);
            leases.append(row);
        }
        return buildLeaseReply(seq, {{"leases", leases}});
    }

    const QString lane = payload.value(QStringLiteral("lane")).toString().trimmed();
    if (lane.isEmpty())
        return buildLeaseError(seq, QStringLiteral("lane_required"));

    expireLeaseIfStale(lane, nowMs);

    if (cmd == QLatin1String("lease_acquire")) {
        const QString holder = payload.value(QStringLiteral("holder")).toString().trimmed();
        const QString purpose = payload.value(QStringLiteral("purpose")).toString().trimmed();
        const int ttlSec = payload.value(QStringLiteral("ttl_sec")).toInt(0);
        if (holder.isEmpty())
            return buildLeaseError(seq, QStringLiteral("holder_required"));
        if (purpose.isEmpty())
            return buildLeaseError(seq, QStringLiteral("purpose_required"));
        if (ttlSec <= 0)
            return buildLeaseError(seq, QStringLiteral("ttl_sec_required"));

        const auto active = m_leases.constFind(lane);
        if (active != m_leases.constEnd()) {
            return buildLeaseReply(seq, {
                {"status", QStringLiteral("BUSY")},
                {"holder", active->holder},
                {"expiry_ms", static_cast<double>(active->expiryMs)}
            });
        }

        const bool staleReclaimed = m_expiredPriorHolders.contains(lane);
        m_expiredPriorHolders.remove(lane);

        Lease lease;
        lease.lane = lane;
        lease.holder = holder;
        lease.purpose = purpose;
        lease.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        lease.expiryMs = nowMs + static_cast<qint64>(ttlSec) * 1000;
        m_leases.insert(lane, lease);

        return buildLeaseReply(seq, {
            {"status", staleReclaimed ? QStringLiteral("STALE_RECLAIMED")
                                      : QStringLiteral("ACQUIRED")},
            {"token", lease.token},
            {"expiry_ms", static_cast<double>(lease.expiryMs)}
        });
    }

    if (cmd == QLatin1String("lease_get")) {
        const auto active = m_leases.constFind(lane);
        if (active != m_leases.constEnd()) {
            return buildLeaseReply(seq, {
                {"holder", active->holder},
                {"purpose", active->purpose},
                {"expiry_ms", static_cast<double>(active->expiryMs)},
                {"token_prefix", active->token.left(8)}
            });
        }

        const auto expired = m_expiredPriorHolders.constFind(lane);
        if (expired != m_expiredPriorHolders.constEnd()) {
            return buildLeaseReply(seq, {
                {"status", QStringLiteral("EXPIRED")},
                {"prior_holder", *expired}
            });
        }

        return buildLeaseReply(seq, {{"status", QStringLiteral("FREE")}});
    }

    if (cmd == QLatin1String("lease_release")) {
        const QString token = payload.value(QStringLiteral("token")).toString();
        if (token.isEmpty())
            return buildLeaseError(seq, QStringLiteral("token_required"));
        auto active = m_leases.find(lane);
        if (active == m_leases.end())
            return buildLeaseError(seq, QStringLiteral("no_active_lease"));
        if (active->token != token)
            return buildLeaseError(seq, QStringLiteral("token_mismatch"));

        m_leases.erase(active);
        m_expiredPriorHolders.remove(lane);
        return buildLeaseReply(seq, {{"status", QStringLiteral("OK")}});
    }

    if (cmd == QLatin1String("lease_heartbeat")) {
        const QString token = payload.value(QStringLiteral("token")).toString();
        if (token.isEmpty())
            return buildLeaseError(seq, QStringLiteral("token_required"));
        auto active = m_leases.find(lane);
        if (active == m_leases.end())
            return buildLeaseError(seq, QStringLiteral("no_active_lease"));
        if (active->token != token)
            return buildLeaseError(seq, QStringLiteral("token_mismatch"));

        const int requestedTtlSec = payload.value(QStringLiteral("ttl_sec")).toInt(0);
        const qint64 ttlMs = requestedTtlSec > 0
            ? static_cast<qint64>(requestedTtlSec) * 1000
            : qMax<qint64>(1000, active->expiryMs - nowMs);
        active->expiryMs = nowMs + ttlMs;
        return buildLeaseReply(seq, {
            {"status", QStringLiteral("OK")},
            {"expiry_ms", static_cast<double>(active->expiryMs)}
        });
    }

    return buildLeaseError(seq, QStringLiteral("unknown_lease_command"));
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
