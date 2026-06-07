#pragma once

// THEATRE_RQBIT_REVIVAL Phase 1 (2026-06-07) — subprocess lifecycle wrapper
// around the headless rqbit torrent-streaming server. Modeled on the deleted
// StreamServerProcess (64213b5^), simplified: rqbit lets US choose the HTTP
// port via --http-api-listen-addr, so readiness is detected by polling
// GET /torrents (200 = up) rather than parsing a port out of stdout.
//
// Binary discovery (mirrors the ffmpeg sidecar): prefer next-to-exe
// (applicationDirPath()/rqbit.exe — the POST_BUILD-deployed copy), fall back
// to the repo resources/rqbit/rqbit.exe for a raw run-from-repo dev launch.

#include <QObject>
#include <QProcess>
#include <QString>

class QNetworkAccessManager;

namespace tankostream::rqbit {

class RqbitProcess : public QObject {
    Q_OBJECT
public:
    explicit RqbitProcess(QObject* parent = nullptr);
    ~RqbitProcess() override;

    // Spawns rqbit headless bound to 127.0.0.1:<free-port> with `downloadDir`
    // as the output/staging folder. Async: callers connect ready()/failed().
    void start(const QString& downloadDir);
    // Graceful shutdown: terminate(), then kill() after a short grace. Flips an
    // intentional-shutdown flag so a normal exit isn't reported as a crash.
    void stop();

    int  port() const { return m_port; }
    bool isReady() const { return m_ready; }

signals:
    void ready(int port);                  // after the health-check first sees 200
    void failed(const QString& message);   // launch error, crash, or readiness timeout

private slots:
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    static QString binaryPath();
    static int     pickFreePort();
    void           pollHealth();           // self-rescheduling GET /torrents until 200

    QProcess*              m_proc = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    int  m_port = 0;
    bool m_ready = false;
    bool m_intentionalShutdown = false;
    int  m_healthAttempts = 0;

    static constexpr int kHealthIntervalMs  = 250;
    static constexpr int kHealthMaxAttempts = 40;   // ~10s before declaring failure
};

} // namespace tankostream::rqbit
