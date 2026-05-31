#include "core/net/NetSeam.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>
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
// record timing (QElapsedTimer — monotonic, not system-clock deltas), status,
// and bytes (accumulated via downloadProgress — not bytesAvailable() at
// finished(), which is unreliable when callers consume data in earlier slots).
// Does NOT wrap the reply — callers interact with the same QNetworkReply type
// they always have, preserving every existing connect()/abort()/deleteLater()
// pattern (Risk E — abort emits finished with OperationCanceledError, logged
// distinctly).
//
// Block hook: checks NetSeam::isHostBlocked(host) before calling the base class.
// On match, returns a SyntheticErrorReply with ConnectionRefusedError — the
// caller's existing error-handling path fires normally. No real I/O.
//
// Throttle hook: checks NetSeam::throttleRuleForHost(host) for a latency-ms
// rule. If non-zero, delays the base-class createRequest() call via QTimer.
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
        const qint64 epochStartMs = QDateTime::currentMSecsSinceEpoch();

        // ── Block check ──
        const QString host = url.host().toLower();
        if (NetSeam::instance()->isHostBlocked(host)) {
            const QString errMsg = QStringLiteral("Blocked by NetSeam: %1")
                .arg(host);

            // Record the blocked request in the observer ring buffer
            // (zero-duration, no real I/O, so we hand-craft the record).
            RequestRecord blockedRec;
            blockedRec.url         = url.toString();
            blockedRec.method      = QString::fromLatin1(opName);
            blockedRec.sourceTag   = sourceTag;
            blockedRec.startTimeMs = epochStartMs;
            blockedRec.durationMs  = 0;
            blockedRec.statusCode  = -1;
            blockedRec.errorString = errMsg;
            NetSeam::instance()->recordRequest(blockedRec);

            return new SyntheticErrorReply(
                QNetworkReply::ConnectionRefusedError, errMsg, nullptr);
            // Note: no parent → caller owns the reply via deleteLater().
        }

        // ── Throttle check ──
        const ThrottleRule throttle = NetSeam::instance()
            ->throttleRuleForHost(host);
        const int delayMs = throttle.latencyMs;

        // ── Monotonic timer for accurate duration (starts NOW) ──
        auto elapsed = QSharedPointer<QElapsedTimer>::create();
        elapsed->start();

        // ── Byte accumulator shared between downloadProgress + finished ──
        auto bytesAccum = QSharedPointer<qint64>::create(0);

        // Throttle latency injection is NOT applied here yet (follow-on).
        // Adding GENUINE latency while returning a QNetworkReply* synchronously
        // needs a deferred-dispatch proxy reply. The tempting "dispatch now and
        // subtract delayMs from the recorded duration" shortcut is wrong twice
        // over — it adds zero real latency AND corrupts the timing metric (makes
        // requests look faster than reality) — so we do NEITHER. Throttle rules
        // are stored + listable (forward-compatible for the real impl); for now
        // the request dispatches immediately and durationMs stays honest.
        Q_UNUSED(delayMs);
        QNetworkReply* reply = QNetworkAccessManager::createRequest(
            op, originalReq, outgoingData);
        attachObserver(reply, url, opName, sourceTag,
                       epochStartMs, elapsed, bytesAccum);
        return reply;
    }

private:
    void attachObserver(QNetworkReply* reply,
                        const QUrl& url,
                        const char* opName,
                        const QString& sourceTag,
                        qint64 epochStartMs,
                        QSharedPointer<QElapsedTimer> elapsed,
                        QSharedPointer<qint64> bytesAccum)
    {
        // ── downloadProgress → accumulate bytes ──
        connect(reply, &QNetworkReply::downloadProgress, this,
                [bytesAccum](qint64 received, qint64 /*total*/) {
            *bytesAccum = received;
        });

        // ── finished → record timing + status ──
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, url, opName, sourceTag,
                 epochStartMs, elapsed, bytesAccum]() {
            RequestRecord rec;
            rec.url          = url.toString();
            rec.method       = QString::fromLatin1(opName);
            rec.sourceTag    = sourceTag;
            rec.startTimeMs  = epochStartMs;

            // Monotonic duration from QElapsedTimer (fixes the ~21000ms
            // system-clock artifact flagged in the canary lead-in).
            rec.durationMs = elapsed->elapsed();

            // Bytes from the downloadProgress accumulator
            // (reliable; not bytesAvailable() which is best-effort at finish).
            rec.bytesReceived = *bytesAccum;

            const auto err = reply->error();
            if (err == QNetworkReply::NoError) {
                rec.statusCode = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
            } else {
                // Distinguish user-cancelled from network failure (Risk E).
                rec.statusCode =
                    (err == QNetworkReply::OperationCanceledError) ? 0 : -1;
                rec.errorString = reply->errorString();
            }

            NetSeam::instance()->recordRequest(rec);
        });
    }

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
    // Read via qgetenv (codebase convention; no QProcessEnvironment overhead).
    const QByteArray raw = qgetenv("TANKOBAN_NET_SEAM");
    if (raw != QByteArrayLiteral("1")) {
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

// ── Block rules ───────────────────────────────────────────────────────────────

void NetSeam::setBlockRule(const BlockRule& rule)
{
    QMutexLocker lock(&m_mutex);
    m_blockRules.insert(rule.host.toLower(), rule);
}

void NetSeam::clearBlockRule(const QString& host)
{
    QMutexLocker lock(&m_mutex);
    m_blockRules.remove(host.toLower());
}

bool NetSeam::isHostBlocked(const QString& host) const
{
    QMutexLocker lock(&m_mutex);
    const QString key = host.toLower();
    if (!m_blockRules.contains(key))
        return false;
    return m_blockRules.value(key).enabled;
}

QList<BlockRule> NetSeam::listBlockRules() const
{
    QMutexLocker lock(&m_mutex);
    return m_blockRules.values();
}

// ── Throttle rules ────────────────────────────────────────────────────────────

void NetSeam::setThrottleRule(const ThrottleRule& rule)
{
    QMutexLocker lock(&m_mutex);
    m_throttleRules.insert(rule.host.toLower(), rule);
}

void NetSeam::clearThrottleRule(const QString& host)
{
    QMutexLocker lock(&m_mutex);
    m_throttleRules.remove(host.toLower());
}

ThrottleRule NetSeam::throttleRuleForHost(const QString& host) const
{
    QMutexLocker lock(&m_mutex);
    // Exact-host match first, then global ("") fallback.
    const QString key = host.toLower();
    if (m_throttleRules.contains(key))
        return m_throttleRules.value(key);
    if (m_throttleRules.contains(QString()))
        return m_throttleRules.value(QString());
    return {};
}

QList<ThrottleRule> NetSeam::listThrottleRules() const
{
    QMutexLocker lock(&m_mutex);
    return m_throttleRules.values();
}

} // namespace tankoban::net

// SyntheticErrorReply + NetInstrumentedManager have Q_OBJECT — need the moc output.
#include "NetSeam.moc"
