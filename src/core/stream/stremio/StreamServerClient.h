#pragma once

// STREAM_SERVER_PIVOT Phase 1 (2026-04-24) — thin async REST client over
// Stremio stream-server's HTTP API. All methods are callback-based (no
// blocking waits), safe to invoke from the main thread. Mirrors the lazy-
// QNetworkAccessManager field pattern from SidecarProcess.h:337.
//
// API surface (the 4 endpoints Tankoban uses — there are more in server.js
// but this subset is sufficient for Phase 1):
//   POST /:hash/create         — start a torrent; empty body → server.js:18122
//                                 auto-constructs magnet from hash
//   GET  /:hash/stats.json     — torrent-level stats + file list + swarm
//   GET  /:hash/remove         — graceful stop + cleanup for a single torrent
//   (stream URL construction)  — http://127.0.0.1:<port>/<hash>/<fileIdx>
//                                 returned to sidecar for HTTP ffmpeg read
//
// 5s per-request timeout via QNetworkRequest::setTransferTimeout. Error
// classification (HTTP 5xx / timeout / connection-refused) → callback
// `ok=false` with a short error string. Callers map this to streamError
// emissions in StreamServerEngine.

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <functional>

class QNetworkAccessManager;

class StreamServerClient : public QObject {
    Q_OBJECT

public:
    explicit StreamServerClient(QObject* parent = nullptr);
    ~StreamServerClient() override;

    // Update the port stream-server is listening on. Must be called before
    // any request method. Passing 0 disables; subsequent calls will error
    // via the callback. Thread-safe (atomic); typically called once from
    // the main thread after StreamServerProcess::ready(port).
    void setPort(int port);
    int  port() const;

    // POST /:hash/create with empty body. stream-server auto-constructs a
    // magnet URI from infoHash when body lacks a `torrent` field
    // (server.js:18122). If callers have a full magnet URI they pass it
    // as `magnetUri`; we extract the infoHash client-side and POST to
    // /:hash/create (stream-server then enriches trackers from its bundled
    // list). Callback fires once per call; on success, the returned JSON
    // object is the /create response (contains .infoHash, .name, .files[]).
    void createTorrent(const QString& magnetUri,
                       std::function<void(bool ok,
                                          const QString& infoHash,
                                          const QJsonObject& response,
                                          const QString& err)> cb);

    // GET /:hash/stats.json. Returns the full JSON dict on success. Cheap;
    // safe to call at StreamPlayerController's 300ms poll cadence.
    void getStats(const QString& infoHash,
                  std::function<void(bool ok,
                                     const QJsonObject& stats,
                                     const QString& err)> cb);

    // GET /:hash/:idx/stats.json. Same shape as getStats but adds three
    // file-level fields (server.js:18332-18336): `streamLen` (bytes of
    // selected file), `streamName` (filename), `streamProgress` (fractional
    // 0.0–1.0 = availablePieces/filePieces for the file's piece range). Use
    // this for seek-slider gray-paint and per-file download tracking — the
    // top-level `downloaded` field on /stats.json is swarm-wide not
    // file-specific, and the `files[idx].downloaded` field doesn't exist
    // (reading it returns 0, which is why Phase 1 had a latent always-zero
    // read in refreshStats). STREAM_SERVER_PIVOT Phase 2B.
    void getFileStats(const QString& infoHash,
                      int fileIndex,
                      std::function<void(bool ok,
                                         const QJsonObject& stats,
                                         const QString& err)> cb);

    // GET /:hash/remove. Fire-and-forget is fine — the callback is for
    // diagnostics, not correctness. Returns 200 `{}` on success.
    void removeTorrent(const QString& infoHash,
                       std::function<void(bool ok,
                                          const QString& err)> cb);

    // Synthetic — no network call. Builds the URL the sidecar will open.
    QString buildStreamUrl(const QString& infoHash, int fileIndex) const;

    // Exposed for diagnostic dumps.
    bool isReady() const;

private:
    QNetworkAccessManager* ensureNam();

    // Convert a magnet URI to its 40-char hex infoHash. Returns empty string
    // on malformed input. Handles "magnet:?xt=urn:btih:<hash>&…" and also
    // accepts already-plain-hash input (so callers needn't branch).
    static QString magnetToInfoHash(const QString& magnet);

    QNetworkAccessManager* m_nam = nullptr;
    std::atomic<int>       m_port{0};

    static constexpr int kRequestTimeoutMs = 5000;
};
