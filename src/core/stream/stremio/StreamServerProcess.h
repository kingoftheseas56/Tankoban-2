#pragma once

// STREAM_SERVER_PIVOT Phase 1 (2026-04-24) — subprocess lifecycle wrapper
// around Stremio Desktop's bundled stream-server (Node.js-repackaged runtime
// `stremio-runtime.exe` + webpacked JS `server.js`). Mirrors the shape of
// `src/ui/player/SidecarProcess.{h,cpp}` — QProcess-owned, line-buffered
// stdout parse for readiness signal, intentionalShutdown flag distinguishes
// crash vs graceful exit.
//
// Binary discovery: prefer `applicationDirPath()/stream_server/stremio-runtime.exe`
// (production install), fall back to `applicationDirPath()/../resources/
// stream_server/stremio-runtime.exe` (dev build), then the repo path
// `resources/stream_server/stremio-runtime.exe` from CWD.
//
// Port: stream-server's HTTP listener is hardcoded at 11470 in server.js:46846
// with a fallback scan 11470→11474 (`port++ < 11474`). The actual bound port
// is emitted on stdout as `EngineFS server started at http://127.0.0.1:<PORT>`
// (server.js:46853). We parse that line to recover the bound port. If Stremio
// Desktop is running concurrently, ports 11470-11474 may all be taken — we
// surface a clean `errorOccurred` + fail the stream rather than hang.
//
// Env vars passed to subprocess:
//   NO_HTTPS_SERVER=1 — disables the HTTPS listener (server.js:46579); we
//                        only need HTTP, saves one less bind conflict
//   APP_PATH=<cacheDir> — overrides the default %APPDATA%\stremio\stremio-server
//                         cache location (server.js:35949), so we don't share
//                         cache with a concurrently-running Stremio Desktop

#include <QObject>
#include <QProcess>
#include <QString>
#include <QByteArray>
#include <atomic>

class StreamServerProcess : public QObject {
    Q_OBJECT

public:
    explicit StreamServerProcess(QObject* parent = nullptr);
    ~StreamServerProcess() override;

    // Launches the subprocess. `cacheDir` becomes APP_PATH — stream-server
    // stores .tankoboan_server_cache/<hash>/ subdirs there. Returns false
    // immediately on binary-not-found or QProcess::start failure. Callers
    // MUST connect to ready() / errorOccurred() to discover success; start()
    // return is only the initial dispatch outcome.
    bool start(const QString& cacheDir);

    // Graceful shutdown: QProcess::terminate (sends WM_QUIT on Windows), then
    // kill after 2s if still alive. Flips intentionalShutdown so crashed()
    // won't fire for this exit.
    void shutdown();

    bool isRunning() const;
    int  httpPort() const { return m_httpPort.load(std::memory_order_acquire); }

signals:
    // Fires once after we parse the "EngineFS server started at …" line from
    // stdout. `port` is the actual bound port (11470 on clean boot, may be
    // 11471-11474 if Stremio Desktop squatted earlier ports). Consumers
    // should cache this port; it never changes for the lifetime of the
    // subprocess.
    void ready(int port);

    // Fires when the subprocess exits unexpectedly (intentionalShutdown was
    // false). Consumers should route to UI error + offer a restart. Exit
    // code 0 post-crash is possible if Node threw and returned 0 — treat
    // any non-intentional exit as a crash.
    void crashed(int exitCode);

    // Fires on QProcess::errorOccurred (launch failure, pipe error, etc.),
    // or on readiness timeout (15s without the "EngineFS server started"
    // line). `message` is user-facing.
    void errorOccurred(const QString& message);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onReadinessTimeout();

private:
    QString discoverBinaryPath() const;

    QProcess*          m_process = nullptr;
    QByteArray         m_stdoutBuffer;
    QByteArray         m_stderrBuffer;
    std::atomic<int>   m_httpPort{0};
    bool               m_readyEmitted = false;
    bool               m_intentionalShutdown = false;
    QTimer*            m_readinessTimer = nullptr;

    static constexpr int kReadinessTimeoutMs = 15000;
};
