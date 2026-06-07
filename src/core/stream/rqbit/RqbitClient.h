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

    // Network surface lands in Task 3.
};

} // namespace tankostream::rqbit
