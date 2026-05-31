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
// Flag-gate: the TANKOBAN_NET_SEAM env var. When unset or "0", createManager()
// returns a vanilla QNetworkAccessManager — zero instrumentation, zero overhead,
// identical behavior to the pre-Congress-9 world. When set to "1", returns an
// instrumented NetInstrumentedManager subclass.
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
    qint64  startTimeMs = 0;   // QDateTime::currentMSecsSinceEpoch()
    qint64  durationMs  = 0;
    int     statusCode  = 0;    // HTTP status, or -1 for network error
    QString errorString;        // empty on success
    qint64  bytesReceived = 0;
};

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

    // Observer ring buffer access (QMutex-guarded).
    QList<RequestRecord> requestList() const;
    QJsonArray           requestListJson() const;

    // Internal — called by NetInstrumentedManager on each finished request.
    void recordRequest(const RequestRecord& rec);

    // Max ring-buffer entries (oldest evicted when exceeded).
    static constexpr int kMaxRecords = 500;

    // Reserved QNetworkRequest::Attribute slot for source-tag.
    // NetInstrumentedManager reads this in createRequest().
    static constexpr QNetworkRequest::Attribute kSourceTagAttr =
        QNetworkRequest::Attribute::User;

private:
    explicit NetSeam(QObject* parent = nullptr);
    ~NetSeam() override = default;
    NetSeam(const NetSeam&) = delete;
    NetSeam& operator=(const NetSeam&) = delete;

    mutable QMutex m_mutex;
    QList<RequestRecord> m_records;          // ring buffer
};

} // namespace tankoban::net
