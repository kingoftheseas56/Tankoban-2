#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

class QLocalServer;
class QLocalSocket;
class QTimer;
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
// v1.9 (Phase D.6, 2026-05-19) adds the cross-cutting system state +
// introspection layer (27 commands across app-/settings-/jsonstore-/
// cache-/scanner-/log-/events-/theme-/font-/perf-/dev- prefixes) backed
// by SystemIntrospection. Headline unlock is `log-mark <label>` — emits
// a correlation marker into all four active log streams simultaneously
// (out/sidecar_debug_live.log + out/stream_telemetry.log + out/events.jsonl
// + out/ipc_latency.log), so multi-source log analysis post-smoke can
// pivot on the label instead of timestamp guesswork. Read-only commands
// gate on `--dev-control` only; write-capable commands (settings-set /
// settings-reset / jsonstore-set / cache-clear / log-set-level /
// theme-reload / dev-inject-error / dev-toggle-feature) additionally
// require `TANKOBAN_DEV_WRITE=1` (env var, SEPARATE from D.5's
// TANKOBAN_DEV_UI_SIM) or return DEV_WRITE_DISABLED. Twelve commands
// from the spec catalogue (network-list-requests, network-get-active,
// network-throttle-set, network-block-host, perf-get-frame-times,
// perf-get-cpu-usage, perf-get-gpu-usage, scanner-pause, scanner-resume,
// scanner-trigger, cache-get-stats, app-trace-signals, jsonstore-dump)
// are deferred to a follow-on commission — their underlying instrumen-
// tation (shared QNetworkAccessManager observer, paint-time counters,
// VideosScanner pause API, PosterCache stats accessor, signal tracer)
// does not yet exist, and the spec's "ship `unsupported` placeholders"
// option was vetoed by Agent 0 in favour of a tighter v1.9 surface.
//
// Gated dev-only — caller (MainWindow::enableDevControl) is itself gated
// behind the `--dev-control` argv flag or `TANKOBAN_DEV_CONTROL=1` env var.
// v1.10 (2026-05-21) adds an in-memory lease registry for machine-readable
// agent lane coordination. `lease_*` commands cover acquire/release/heartbeat/
// get/list, use UUID bearer tokens for release + heartbeat, and expire by TTL.
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

    QJsonObject handleLeaseCommand(const QString& cmd, int seq, const QJsonObject& payload);

private slots:
    void onNewConnection();
    void cleanupExpiredLeases();

private:
    struct Lease {
        QString lane;
        QString holder;
        QString purpose;
        QString token;
        qint64 expiryMs = 0;
    };

    void handleConnection(QLocalSocket* conn);
    QByteArray buildErrorReply(int seq, const char* code, const QString& message) const;
    QByteArray serialize(const QJsonObject& obj) const;
    void expireLeaseIfStale(const QString& lane, qint64 nowMs);
    QJsonObject buildLeaseReply(int seq, QJsonObject extras) const;
    QJsonObject buildLeaseError(int seq, const QString& reason) const;

    QPointer<MainWindow> m_window;
    QLocalServer*        m_server = nullptr;
    QTimer*              m_leaseCleanupTimer = nullptr;
    QHash<QString, Lease> m_leases;
    QHash<QString, QString> m_expiredPriorHolders;
};
