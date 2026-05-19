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
// Schema version is returned by MainWindow's `ping` command. Additive
// changes within v1.x are non-breaking; removals/renames bump to v2.
// MainWindow owns command dispatch; v1.3+ domain surfaces may delegate
// prefixed commands to page-local dispatchDevCommand methods. v1.5
// (Phase D.3, 2026-05-19) adds the sources-side bridge layer covering
// TankorentPage + TankoLibraryPage (search dispatch, indexer health,
// torrent lifecycle controls, TankoLibrary detail/download surface).
// v1.6 (Phase D.4, 2026-05-19) adds the library-side bridge layer
// covering cross-mode landing-page state (continue-reading, recently-
// added, search/scan/sort/density/selection/active-layer) plus theme
// apply + active-mode-pill query.
// v1.7 (Phase D.2, 2026-05-19) adds the player-side deeper surface
// covering player/sidecar/subs/osd prefixes: track + delay controls,
// chapters, screenshot, decoder stats, HUD state, sidecar process +
// IPC-latency introspection, sidecar graceful restart, subtitle
// overlay + positioning state, and OSD overlay state. Two queue-depth
// commands (sidecar-get-decoder-queue + sidecar-get-render-queue) are
// stubbed with NYI replies until a push-event-based sidecar stats
// surface lands in a follow-on commission (kept additive within v1.x).
// v1.8 (Phase D.5, 2026-05-19) adds the synthetic UI interaction layer
// (`ui_*` prefix, 14 commands) backed by UiInteractionDispatcher. Bypasses
// UIA/pixel-click flakiness by looking up the target QObject by objectName
// and dispatching via QApplication::postEvent + QMetaObject::invokeMethod.
// Read-only commands (ui_query_widget / ui_query_focus / ui_active_layer /
// ui_list_widgets / ui_dry_run) gate on `--dev-control` only; write-capable
// commands (ui_click, ui_keypress, ui_text_input, ui_simulate_scroll,
// ui_simulate_mouse, ui_wait_for, ui_set_checkbox, ui_set_combo,
// ui_select_table_row) additionally require `TANKOBAN_DEV_UI_SIM=1` or
// return UI_SIM_DISABLED. Synthetic UI proves a widget RECEIVED an event,
// NOT that the screen looks right — visual verification still needs
// screenshots or human eyes (main-spec anti-pattern #10).
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
