#pragma once

#include <QObject>
#include <QPointer>

class QLocalServer;
class QLocalSocket;
class MainWindow;

// REPO_HYGIENE Phase 3 (2026-04-26) — dev-control bridge.
//
// QLocalServer-based local IPC channel for agent / tankoctl smokes. Listens on
// `TankobanDevControl` named pipe (distinct from `TankobanSingleInstance`).
// One command per connection: client sends a single JSON line, server replies
// with a single JSON line, connection closes. Single-thread on Qt UI thread —
// `waitForReadyRead` / `waitForBytesWritten` cap block time at 500 ms.
//
// Wire format: newline-delimited compact JSON.
//   request  = {"cmd": <name>, "seq": <int>, "payload": {...}}
//   reply    = {"type": "reply", "seq": <int>, ...}
//   error    = {"type": "error", "seq": <int>, "code": "<UPPER_SNAKE>", "message": "<human>"}
//
// Schema version returned in `ping` response: "tankoban.dev.v1".
// Additive changes within v1.x are non-breaking; removals/renames bump to v2.
//
// Gated dev-only — caller (MainWindow::enableDevControl) is itself gated
// behind the `--dev-control` argv flag or `TANKOBAN_DEV_CONTROL=1` env var.
class DevControlServer : public QObject
{
    Q_OBJECT
public:
    static constexpr const char* kSocketName = "TankobanDevControl";

    explicit DevControlServer(MainWindow* window, QObject* parent = nullptr);
    ~DevControlServer() override;

    bool start();
    void stop();
    bool isListening() const;

private slots:
    void onNewConnection();

private:
    void handleConnection(QLocalSocket* conn);
    QByteArray buildErrorReply(int seq, const char* code, const QString& message) const;
    QByteArray serialize(const QJsonObject& obj) const;

    QPointer<MainWindow> m_window;
    QLocalServer*        m_server = nullptr;
};
