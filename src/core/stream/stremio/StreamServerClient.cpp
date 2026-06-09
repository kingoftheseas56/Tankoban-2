#include "core/stream/stremio/StreamServerClient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

StreamServerClient::StreamServerClient(QObject* parent)
    : QObject(parent)
{
}

StreamServerClient::~StreamServerClient() = default;

void StreamServerClient::setPort(int port)
{
    m_port.store(port, std::memory_order_release);
}

int StreamServerClient::port() const
{
    return m_port.load(std::memory_order_acquire);
}

bool StreamServerClient::isReady() const
{
    return m_port.load(std::memory_order_acquire) > 0;
}

QNetworkAccessManager* StreamServerClient::ensureNam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }
    return m_nam;
}

// Accepts "magnet:?xt=urn:btih:<40hex>&…" OR just a 40-char hex string.
// Returns the lowercase 40-char infoHash on success, empty on malformed.
QString StreamServerClient::magnetToInfoHash(const QString& magnet)
{
    if (magnet.length() == 40) {
        // Raw hex? Validate it's all hex.
        static const QRegularExpression hexRe(QStringLiteral("^[0-9a-fA-F]{40}$"));
        if (hexRe.match(magnet).hasMatch()) {
            return magnet.toLower();
        }
    }

    // Parse magnet URL properly. QUrl handles "magnet:" scheme; QUrlQuery
    // extracts xt parameter; we strip "urn:btih:" prefix and case-fold.
    if (!magnet.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) {
        return {};
    }
    const QUrl u(magnet);
    const QUrlQuery q(u.query());
    // Magnet URIs can have multiple xt params; find the btih one.
    for (const auto& pair : q.queryItems()) {
        if (pair.first.compare(QStringLiteral("xt"), Qt::CaseInsensitive) != 0) continue;
        const QString v = pair.second;
        if (v.startsWith(QStringLiteral("urn:btih:"), Qt::CaseInsensitive)) {
            const QString hash = v.mid(9);  // strip "urn:btih:"
            if (hash.length() == 40) {
                static const QRegularExpression hexRe(QStringLiteral("^[0-9a-fA-F]{40}$"));
                if (hexRe.match(hash).hasMatch()) {
                    return hash.toLower();
                }
            }
            // Base32 (32-char) infohashes exist but Stremio stream-server
            // expects hex; bail if we can't produce hex.
        }
    }
    return {};
}

QString StreamServerClient::buildStreamUrl(const QString& infoHash, int fileIndex) const
{
    const int p = port();
    if (p <= 0 || infoHash.isEmpty() || fileIndex < 0) return {};
    return QStringLiteral("http://127.0.0.1:%1/%2/%3")
        .arg(p).arg(infoHash).arg(fileIndex);
}

namespace {

// Shared helper: wire reply → callback with 5s transfer timeout. Handles
// HTTP status + JSON parse + error classification.
void wireJsonReply(QNetworkReply* reply,
                   std::function<void(bool ok,
                                      const QJsonObject& body,
                                      const QString& err)> cb)
{
    reply->setParent(nullptr);  // we manage via deleteLater in the slot
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, cb]() {
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netErr = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError) {
            const QString errStr = QStringLiteral("net err %1 http=%2")
                .arg(netErr).arg(httpStatus);
            cb(false, {}, errStr);
            return;
        }
        if (httpStatus < 200 || httpStatus >= 300) {
            const QString errStr = QStringLiteral("http %1").arg(httpStatus);
            cb(false, {}, errStr);
            return;
        }
        QJsonParseError parseErr{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            cb(false, {}, QStringLiteral("bad json: ") + parseErr.errorString());
            return;
        }
        cb(true, doc.object(), {});
    });
}

}  // namespace

void StreamServerClient::createTorrent(
    const QString& magnetUri,
    std::function<void(bool, const QString&, const QJsonObject&, const QString&)> cb)
{
    if (!isReady()) {
        cb(false, {}, {}, QStringLiteral("client not ready (port=0)"));
        return;
    }

    const QString hash = magnetToInfoHash(magnetUri);
    if (hash.isEmpty()) {
        cb(false, {}, {},
            QStringLiteral("could not extract infoHash from magnet"));
        return;
    }

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/%2/create")
                       .arg(port()).arg(hash));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setTransferTimeout(kRequestTimeoutMs);

    // Empty JSON body → stream-server auto-constructs magnet from hash
    // (server.js:18122: `options.torrent = "magnet:?xt=urn:btih:" + infoHash`).
    // If callers want to pass a full magnet (e.g. with trackers), they can
    // provide it here — but the empty-body path suffices for Phase 0 repro.
    const QByteArray body = "{}";

    QNetworkReply* reply = ensureNam()->post(req, body);
    wireJsonReply(reply, [cb, hash](bool ok, const QJsonObject& body, const QString& err) {
        cb(ok, hash, body, err);
    });
}

void StreamServerClient::getStats(
    const QString& infoHash,
    std::function<void(bool, const QJsonObject&, const QString&)> cb)
{
    if (!isReady()) {
        cb(false, {}, QStringLiteral("client not ready (port=0)"));
        return;
    }
    if (infoHash.isEmpty()) {
        cb(false, {}, QStringLiteral("empty infoHash"));
        return;
    }
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/%2/stats.json")
                       .arg(port()).arg(infoHash));
    QNetworkRequest req(url);
    req.setTransferTimeout(kRequestTimeoutMs);
    QNetworkReply* reply = ensureNam()->get(req);
    wireJsonReply(reply, std::move(cb));
}

void StreamServerClient::getFileStats(
    const QString& infoHash,
    int fileIndex,
    std::function<void(bool, const QJsonObject&, const QString&)> cb)
{
    if (!isReady()) {
        cb(false, {}, QStringLiteral("client not ready (port=0)"));
        return;
    }
    if (infoHash.isEmpty()) {
        cb(false, {}, QStringLiteral("empty infoHash"));
        return;
    }
    if (fileIndex < 0) {
        cb(false, {}, QStringLiteral("negative fileIndex"));
        return;
    }
    // /:hash/:idx/stats.json — server.js:18331 route. Enriches the base
    // stats payload with streamLen + streamName + streamProgress (fraction
    // of the file's piece range that is present in the bitfield).
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/%2/%3/stats.json")
                       .arg(port()).arg(infoHash).arg(fileIndex));
    QNetworkRequest req(url);
    req.setTransferTimeout(kRequestTimeoutMs);
    QNetworkReply* reply = ensureNam()->get(req);
    wireJsonReply(reply, std::move(cb));
}

void StreamServerClient::removeTorrent(
    const QString& infoHash,
    std::function<void(bool, const QString&)> cb)
{
    if (!isReady()) {
        cb(false, QStringLiteral("client not ready (port=0)"));
        return;
    }
    if (infoHash.isEmpty()) {
        cb(false, QStringLiteral("empty infoHash"));
        return;
    }
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/%2/remove")
                       .arg(port()).arg(infoHash));
    QNetworkRequest req(url);
    req.setTransferTimeout(kRequestTimeoutMs);
    QNetworkReply* reply = ensureNam()->get(req);
    wireJsonReply(reply, [cb](bool ok, const QJsonObject& /*body*/, const QString& err) {
        cb(ok, err);
    });
}
