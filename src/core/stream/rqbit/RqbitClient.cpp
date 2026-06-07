#include "core/stream/rqbit/RqbitClient.h"
#include "core/net/NetSeam.h"
#include <QFileInfo>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QUrl>

namespace tankostream::rqbit {

RqbitStats RqbitClient::parseStats(const QJsonObject& j) {
    RqbitStats s;
    s.state           = j.value(QStringLiteral("state")).toString();
    s.totalBytes      = j.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
    s.downloadedBytes = j.value(QStringLiteral("progress_bytes")).toVariant().toLongLong();
    s.finished        = j.value(QStringLiteral("finished")).toBool();
    return s;
}

int RqbitClient::pickPrimaryVideoFile(const QJsonArray& files) {
    static const QStringList kVideoExt = {
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("webm"),
        QStringLiteral("ts"),  QStringLiteral("flv")
    };
    int best = -1; qint64 bestLen = -1;
    for (int i = 0; i < files.size(); ++i) {
        const QJsonObject f = files.at(i).toObject();
        const QString name = f.value(QStringLiteral("name")).toString();
        const QString ext  = QFileInfo(name).suffix().toLower();
        if (!kVideoExt.contains(ext)) continue;
        const qint64 len = f.value(QStringLiteral("length")).toVariant().toLongLong();
        if (len > bestLen) { bestLen = len; best = i; }
    }
    return best;
}

// ── Network surface ─────────────────────────────────────────────────────────

QNetworkAccessManager* RqbitClient::ensureNam() {
    if (!m_nam)
        m_nam = tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("rqbit-stream"));
    return m_nam;
}

void RqbitClient::setBaseUrl(const QString& base) { m_base = base; }

QString RqbitClient::streamUrl(const QString& torrentId, int fileIndex) const {
    return QStringLiteral("%1/torrents/%2/stream/%3").arg(m_base, torrentId).arg(fileIndex);
}

void RqbitClient::addTorrent(const QString& tag, const QString& magnet) {
    QNetworkRequest req(QUrl(m_base + QStringLiteral("/torrents")));
    // rqbit reads the raw request body and detects magnet:/http:/file (contract §3).
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
    req.setTransferTimeout(15000);
    QNetworkReply* reply = ensureNam()->post(req, magnet.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, tag]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tag, reply->errorString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        // id is a JSON number (contract §3); {id_or_infohash} endpoints take it as a string.
        const QJsonValue idVal = o.value(QStringLiteral("id"));
        if (!idVal.isDouble()) {
            emit requestFailed(tag, QStringLiteral("rqbit add: no numeric id in response"));
            return;
        }
        const QString id = QString::number(idVal.toInt());
        const QJsonArray files = o.value(QStringLiteral("details")).toObject()
                                  .value(QStringLiteral("files")).toArray();
        emit torrentAdded(tag, id, files);
    });
}

void RqbitClient::fetchStats(const QString& torrentId) {
    QNetworkRequest req(QUrl(QStringLiteral("%1/torrents/%2/stats/v1").arg(m_base, torrentId)));
    req.setTransferTimeout(10000);
    QNetworkReply* reply = ensureNam()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, torrentId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(torrentId, reply->errorString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        emit statsReady(torrentId, parseStats(o));
    });
}

void RqbitClient::deleteTorrent(const QString& torrentId) {
    QNetworkRequest req(QUrl(QStringLiteral("%1/torrents/%2/delete").arg(m_base, torrentId)));
    req.setTransferTimeout(10000);
    QNetworkReply* reply = ensureNam()->post(req, QByteArray());
    // Best-effort teardown; a failed delete is non-fatal (process exit cleans up).
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

} // namespace tankostream::rqbit
