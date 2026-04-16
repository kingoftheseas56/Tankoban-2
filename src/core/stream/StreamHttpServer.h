#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QTcpServer>
#include <QThreadPool>

#include <atomic>

class TorrentEngine;
class StreamEngine;

class StreamHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit StreamHttpServer(TorrentEngine* engine, QObject* parent = nullptr);
    ~StreamHttpServer() override;

    bool start();
    void stop();
    int port() const;

    void registerFile(const QString& infoHash, int fileIndex,
                      const QString& filePath, qint64 fileSize);
    void unregisterFile(const QString& infoHash, int fileIndex);
    void clear();

    struct FileEntry {
        QString infoHash;
        int fileIndex = 0;
        QString path;
        qint64 size = 0;
    };

    FileEntry lookupFile(const QString& infoHash, int fileIndex) const;
    TorrentEngine* engine() const { return m_engine; }

    // STREAM_LIFECYCLE_FIX Phase 5 Batch 5.2 — optional StreamEngine pointer
    // for handleConnection to query per-stream cancellation tokens.
    // StreamEngine sets this on itself after its own construction (it owns
    // the server). Stays nullptr if the server is used outside StreamEngine
    // context — handleConnection tolerates nullptr (token lookup returns
    // empty, worker falls through to pre-5.2 behavior).
    void setStreamEngine(StreamEngine* eng) { m_streamEngine = eng; }
    StreamEngine* streamEngine() const { return m_streamEngine; }

    // STREAM_PLAYBACK_FIX Batch 1.3 — graceful shutdown support. The worker
    // threads (QtConcurrent::run → global QThreadPool) check these atomics
    // each serve-loop iteration so `stop()` can request in-flight
    // connections to drain instead of being abandoned on shutdown.
    bool isShuttingDown() const { return m_shuttingDown.load(); }
    void connectionStarted() { ++m_activeConnections; }
    void connectionEnded()   { --m_activeConnections; }

private slots:
    void onNewConnection();

private:
    static QString registryKey(const QString& infoHash, int fileIndex);

    TorrentEngine* m_engine;
    StreamEngine* m_streamEngine = nullptr;  // Batch 5.2 — optional; see setStreamEngine.
    QTcpServer* m_server = nullptr;
    mutable QMutex m_mutex;
    QHash<QString, FileEntry> m_registry;

    // Batch 1.3 — see isShuttingDown / connectionStarted / connectionEnded.
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<int>  m_activeConnections{0};
};
