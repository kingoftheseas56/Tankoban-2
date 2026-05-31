#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// ── NetSeam — brotherhood-wide observable network layer (Congress 9 ratified) ──
//
// Singleton that owns the instrumentation registry (observer + throttle + block
// tables) and vends instrumented QNetworkAccessManager instances via the factory
// createManager(). Each domain gets its OWN wrapped QNAM wired to the shared
// registry — "one observable layer, many managers" (Agent 4, Congress 9).
//
// Thread-safety: the registry tables are QMutex-guarded. createManager() returns
// a QNAM with the caller's thread-affinity (Qt's default — object lives on the
// creating thread). All 20 existing creation sites are main-thread, so no current
// risk; the mutex is cheap future-proofing for any worker-thread caller.
//
// Flag-gate: the TANKOBAN_NET_SEAM env var (read via qgetenv). When unset or "0",
// createManager() returns a vanilla QNetworkAccessManager — zero instrumentation,
// zero overhead, identical to the pre-Congress-9 world. When set to "1", returns
// an instrumented NetInstrumentedManager subclass.
//
// QNetworkRequest attribute reservation:
//   Attribute::User + 0  — kSourceTagAttr (QString source-tag for attribution).
//     Set by NetInstrumentedManager::createRequest() before forwarding to the
//     base class so the observer records the source-tag. Callers may also set
//     this attribute on individual requests for per-request overrides. This
//     reservation is load-bearing — any code that also uses Attribute::User
//     MUST coordinate with NetSeam to avoid collision.
//
// Factory API: NetSeam::createManager(QObject* parent, const QString& sourceTag)
//   parent    — mandatory QObject parent (enforces explicit ownership; no leak).
//   sourceTag — human-readable label for request attribution (e.g. "comics-cover-grid",
//               "book-anna-search"). Carried on QNetworkRequest::Attribute::User.

namespace tankoban::net {

// Lightweight record of one observed HTTP request. Written to the observer ring
// buffer from the signal-connected finished() callback.
struct RequestRecord {
    QString url;
    QString method;       // "GET", "POST", etc.
    QString sourceTag;
    qint64  startTimeMs = 0;   // epoch ms (for display/ordering)
    qint64  durationMs  = 0;   // monotonic elapsed (QElapsedTimer)
    int     statusCode  = 0;    // HTTP status, or -1 for network error
    QString errorString;        // empty on success
    qint64  bytesReceived = 0;  // accumulated via downloadProgress
};

// ── Block rule ─────────────────────────────────────────────────────────────────

struct BlockRule {
    QString host;      // e.g. "api.example.com"
    bool    enabled = true;
};

// ── Throttle rule ──────────────────────────────────────────────────────────────

struct ThrottleRule {
    QString host;          // matched host, or "" for global
    int     latencyMs = 0; // added delay before request dispatch
};

} // namespace tankoban::net

// Register with Qt meta-type system so throttle lookups can copy.
Q_DECLARE_METATYPE(tankoban::net::ThrottleRule)

namespace tankoban::net {

// ── NetSeam singleton ──────────────────────────────────────────────────────────

class NetSeam : public QObject
{
    Q_OBJECT
public:
    static NetSeam* instance();

    // Factory: returns a QNetworkAccessManager* owned by `parent`.
    // sourceTag labels requests for attribution (e.g. "comics-cover-grid"); an
    // empty tag is harmless — requests are recorded with no source label.
    QNetworkAccessManager* createManager(QObject* parent, const QString& sourceTag);

    // ── Observer ring buffer (QMutex-guarded) ──
    QList<RequestRecord> requestList() const;
    QJsonArray           requestListJson() const;

    // Internal — called by NetInstrumentedManager on each finished request.
    void recordRequest(const RequestRecord& rec);

    // Max ring-buffer entries (oldest evicted when exceeded).
    static constexpr int kMaxRecords = 500;

    // Reserved QNetworkRequest::Attribute slot for source-tag.
    // NetInstrumentedManager reads this in createRequest().
    // See top-of-file QNetworkRequest attribute reservation doc.
    static constexpr QNetworkRequest::Attribute kSourceTagAttr =
        QNetworkRequest::Attribute::User;

    // ── Block rules (QMutex-guarded, gated behind TANKOBAN_DEV_WRITE) ──
    void setBlockRule(const BlockRule& rule);
    void clearBlockRule(const QString& host);
    bool isHostBlocked(const QString& host) const;
    QList<BlockRule> listBlockRules() const;

    // ── Throttle rules (QMutex-guarded, gated behind TANKOBAN_DEV_WRITE) ──
    void setThrottleRule(const ThrottleRule& rule);
    void clearThrottleRule(const QString& host);
    ThrottleRule throttleRuleForHost(const QString& host) const; // host → global fallback
    QList<ThrottleRule> listThrottleRules() const;

private:
    explicit NetSeam(QObject* parent = nullptr);
    ~NetSeam() override = default;
    NetSeam(const NetSeam&) = delete;
    NetSeam& operator=(const NetSeam&) = delete;

    mutable QMutex m_mutex;
    QList<RequestRecord> m_records;          // observer ring buffer
    QHash<QString, BlockRule> m_blockRules;  // host → rule
    QHash<QString, ThrottleRule> m_throttleRules; // host → rule; "" = global
};

} // namespace tankoban::net
