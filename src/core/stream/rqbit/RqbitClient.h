#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

class QNetworkAccessManager;

namespace tankostream::rqbit {

// Parsed view of rqbit's GET /torrents/{id}/stats/v1 response.
// Field names captured from the real rqbit 8.1.1 API in
// docs/superpowers/specs/rqbit-api-contract.md §4.
struct RqbitStats {
    QString state;                  // "live" / "initializing" / "paused" / "error"
    qint64  totalBytes = 0;         // total_bytes
    qint64  downloadedBytes = 0;    // progress_bytes
    bool    finished = false;       // finished
    double  progressFraction() const {
        return totalBytes > 0 ? double(downloadedBytes) / double(totalBytes) : 0.0;
    }
};

// Thin REST adapter to a headless rqbit subprocess (HTTP on 127.0.0.1:<port>).
// The two static helpers are pure (no network) and unit-tested; the QNAM-backed
// methods (Task 3) drive the actual /torrents endpoints.
class RqbitClient : public QObject {
    Q_OBJECT
public:
    explicit RqbitClient(QObject* parent = nullptr) : QObject(parent) {}

    // --- Pure parsers (unit-tested; no network). ---
    static RqbitStats parseStats(const QJsonObject& statsJson);
    // Index of the largest file with a video extension in a rqbit details.files[]
    // array (each element has "name" + "length"), or -1 if none is a video.
    static int pickPrimaryVideoFile(const QJsonArray& files);

    // --- Network surface (talks to the headless rqbit HTTP API). ---
    void setBaseUrl(const QString& base);   // e.g. "http://127.0.0.1:3030"

    // POST /torrents (magnet as raw body). Emits torrentAdded(tag, id, files)
    // or requestFailed(tag, msg). `tag` is an opaque caller correlation token.
    void addTorrent(const QString& requestTag, const QString& magnet);

    // Pure URL builder once base + id are known: GET-able stream endpoint that
    // honours open-ended Range requests (206) for seeking — see contract §5.
    QString streamUrl(const QString& torrentId, int fileIndex) const;

    // GET /torrents/{id}/stats/v1 -> emits statsReady(id, RqbitStats).
    void fetchStats(const QString& torrentId);

    // POST /torrents/{id}/delete — forget torrent + remove files (best-effort).
    void deleteTorrent(const QString& torrentId);

signals:
    void torrentAdded(const QString& requestTag, const QString& torrentId, const QJsonArray& files);
    void statsReady(const QString& torrentId, const tankostream::rqbit::RqbitStats& stats);
    void requestFailed(const QString& requestTag, const QString& message);

private:
    QNetworkAccessManager* ensureNam();

    QString m_base = QStringLiteral("http://127.0.0.1:3030");
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankostream::rqbit
