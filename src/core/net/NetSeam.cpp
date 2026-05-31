#include "core/net/NetSeam.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>

namespace tankoban::net {

// ── SyntheticErrorReply ───────────────────────────────────────────────────────
//
// QNetworkReply subclass that emits finished() with a pre-set error on the next
// event-loop tick. Used by NetInstrumentedManager::createRequest() when a
// block rule fires — returns a synthetic error instead of making a real HTTP
// call. The QTimer::singleShot(0) timing guarantees the caller's connect() to
// finished() fires first (same event-loop iteration), avoiding the classic Qt
// "signal emitted before slot connected" race (Risk B, Congress 9).
//
// This reply carries no data, no headers, no body. It exists solely to deliver
// the error signal with the same lifetime contract as a real QNetworkReply
// (caller owns it, calls deleteLater() when finished).

class SyntheticErrorReply : public QNetworkReply
{
    Q_OBJECT
public:
    SyntheticErrorReply(QNetworkReply::NetworkError code,
                        const QString& errorString,
                        QObject* parent = nullptr)
        : QNetworkReply(parent)
    {
        setError(code, errorString);
        // Emit finished() on the next event-loop tick so the caller's
        // connect(reply, &QNetworkReply::finished, ...) fires first.
        QTimer::singleShot(0, this, [this]() {
            emit finished();
        });
    }

    void abort() override {}  // no-op — synthetic, nothing to cancel
    qint64 bytesAvailable() const override { return 0; }

protected:
    qint64 readData(char*, qint64) override { return 0; }
    qint64 writeData(const char*, qint64) override { return 0; }
};

// ── NetInstrumentedManager ─────────────────────────────────────────────────────
//
// Internal subclass of QNetworkAccessManager. Overrides the protected-virtual
// createRequest() to inject observer hooks + block/throttle checks via the
// NetSeam registry singleton. createRequest() handles ALL 6 QNAM operations
// (Get/Post/Put/Delete/Head/CustomOperation) — AniList + MangaUpdates use Post
// and a Get-only override would silently miss those (Risk A, Congress 9).
//
// Observer hook: connects to the real QNetworkReply's finished() signal to
// record timing, status, and bytes. Does NOT wrap the reply — callers interact
// with the same QNetworkReply type they always have, preserving every existing
// connect()/abort()/deleteLater() pattern (Risk E — abort emits finished with
// OperationCanceledError, which we log distinctly).
//
// Source-tag: read from QNetworkRequest::Attribute::User (kSourceTagAttr).
// Set by callers via req.setAttribute(NetSeam::kSourceTagAttr, "tag") before
// calling get()/post(), or (preferred) preset by the factory so all requests
// from a given manager carry the same tag automatically.
//
// Configuration forwarding (Risk D): NetInstrumentedManager overrides ONLY
// createRequest(). All QNAM config methods (setTransferTimeout, setProxy,
// setCookieJar, setStrictTransportSecurityEnabled, etc.) are non-virtual on
// QNetworkAccessManager and pass through to the base class unmodified.

class NetInstrumentedManager : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit NetInstrumentedManager(const QString& sourceTag,
                                    QObject* parent = nullptr)
        : QNetworkAccessManager(parent)
        , m_sourceTag(sourceTag)
    {}

protected:
    QNetworkReply* createRequest(Operation op,
                                 const QNetworkRequest& originalReq,
                                 QIODevice* outgoingData) override
    {
        // ── Read source-tag ──
        // Prefer the attribute on the request (per-request override), fall
        // back to the manager's default tag.
        QString sourceTag = m_sourceTag;
        const QVariant attr = originalReq.attribute(NetSeam::kSourceTagAttr);
        if (attr.isValid() && !attr.toString().isEmpty())
            sourceTag = attr.toString();

        // ── Operation name for logging ──
        const char* opName = "UNKNOWN";
        switch (op) {
        case HeadOperation:    opName = "HEAD";    break;
        case GetOperation:     opName = "GET";     break;
        case PutOperation:     opName = "PUT";     break;
        case PostOperation:    opName = "POST";    break;
        case DeleteOperation:  opName = "DELETE";  break;
        case CustomOperation:  opName = "CUSTOM";  break;
        }

        const QUrl url = originalReq.url();
        const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

        // ── Block check (future: per-host block rules in registry) ──
        // Stub — full block-rules table comes with net-block-host tankoctl
        // command in a follow-on commission. The hook point is here.
        // When implemented, check NetSeam::instance()->blockTable().contains(host)
        // and return a SyntheticErrorReply on match.

        // ── Throttle check (future: per-domain throttle rules in registry) ──
        // Stub — full throttle rules come with net-throttle-set tankoctl
        // command in a follow-on commission. The hook point is here.
        // When implemented, check NetSeam::instance()->throttleTable() and
        // QTimer-delay or queuing as appropriate.

        // ── Create the real reply (all 6 operations forwarded) ──
        QNetworkReply* reply = QNetworkAccessManager::createRequest(
            op, originalReq, outgoingData);

        // ── Observer hooks on the REAL reply (no wrapping) ──
        // Connect to finished() for timing + status recording.
        // Use a QueuedConnection? No — the reply lives on this thread
        // (all current callers are main-thread), so DirectConnection is fine.
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, url, opName, sourceTag, startMs]() {
            RequestRecord rec;
            rec.url          = url.toString();
            rec.method       = QString::fromLatin1(opName);
            rec.sourceTag    = sourceTag;
            rec.startTimeMs  = startMs;
            rec.durationMs   = QDateTime::currentMSecsSinceEpoch() - startMs;

            const auto err = reply->error();
            if (err == QNetworkReply::NoError) {
                rec.statusCode = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                rec.bytesReceived = reply->bytesAvailable();
            } else {
                // Distinguish user-cancelled from network failure (Risk E).
                rec.statusCode =
                    (err == QNetworkReply::OperationCanceledError) ? 0 : -1;
                rec.errorString = reply->errorString();
            }

            NetSeam::instance()->recordRequest(rec);
        });

        // Also log download progress for throughput tracking (lightweight —
        // only fires when bytes arrive, not on a timer).
        connect(reply, &QNetworkReply::downloadProgress, this,
                [this](qint64 received, qint64 total) {
            Q_UNUSED(received);
            Q_UNUSED(total);
            // Future: per-request throughput aggregation in the registry.
            // NOTE: finished()'s bytesAvailable() is best-effort — a caller that
            // reads the body in an earlier-connected slot leaves 0 here. Wire
            // downloadProgress accumulation when throttle/block lands (follow-on).
        });

        return reply;
    }

private:
    QString m_sourceTag;
};

// ── NetSeam singleton ─────────────────────────────────────────────────────────

NetSeam* NetSeam::instance()
{
    static NetSeam s_instance;
    return &s_instance;
}

NetSeam::NetSeam(QObject* parent)
    : QObject(parent)
{
}

QNetworkAccessManager* NetSeam::createManager(QObject* parent,
                                               const QString& sourceTag)
{
    // ── Flag gate: TANKOBAN_NET_SEAM ──
    // When OFF (unset or "0"), return a vanilla QNetworkAccessManager.
    // Zero overhead, zero signal connections, identical to pre-Congress-9.
    const QString flagVal = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("TANKOBAN_NET_SEAM"), QStringLiteral("0"))
        .trimmed();
    if (flagVal != QStringLiteral("1")) {
        return new QNetworkAccessManager(parent);
    }

    return new NetInstrumentedManager(sourceTag, parent);
}

// ── Observer ring buffer ──────────────────────────────────────────────────────

void NetSeam::recordRequest(const RequestRecord& rec)
{
    QMutexLocker lock(&m_mutex);
    m_records.append(rec);
    if (m_records.size() > kMaxRecords)
        m_records.removeFirst();
}

QList<RequestRecord> NetSeam::requestList() const
{
    QMutexLocker lock(&m_mutex);
    return m_records;
}

QJsonArray NetSeam::requestListJson() const
{
    QMutexLocker lock(&m_mutex);
    QJsonArray arr;
    for (const auto& r : m_records) {
        QJsonObject obj;
        obj["url"]           = r.url;
        obj["method"]        = r.method;
        obj["sourceTag"]     = r.sourceTag;
        obj["startTimeMs"]   = r.startTimeMs;
        obj["durationMs"]    = r.durationMs;
        obj["statusCode"]    = r.statusCode;
        if (!r.errorString.isEmpty())
            obj["errorString"] = r.errorString;
        obj["bytesReceived"] = r.bytesReceived;
        arr.append(obj);
    }
    return arr;
}

} // namespace tankoban::net

// SyntheticErrorReply has Q_OBJECT — need the moc output.
// Since it's defined in this .cpp (not a header), include the generated moc.
#include "NetSeam.moc"
