#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class MainWindow;

// REPO_HYGIENE Phase D.6 (2026-05-19) — cross-cutting system state +
// introspection layer behind the v1.9 bridge prefix surface.
//
// Twenty-seven commands across eleven prefixes — none of them tied to a
// single domain page, so they live here rather than fanning out into
// per-page `dispatchDevCommand` slots. The prefixes are: app-, settings-,
// jsonstore-, cache-, scanner-, log-, events-, theme-, font-, perf-, dev-.
//
// Headline unlock is `log-mark <label>` — writes a correlation marker
// (`=== MARK: <label> @ <ISO ts> ===`) into all four active log streams
// simultaneously (out/sidecar_debug_live.log + out/stream_telemetry.log +
// out/events.jsonl + out/ipc_latency.log). Multi-source log analysis can
// then pivot on the label instead of timestamp guesswork.
//
// Caller (MainWindow::handleDevCommand) is responsible for the
// TANKOBAN_DEV_WRITE=1 env gate on write-capable commands (see
// `isWriteCapable` for the catalogue). Read-only commands gate only on
// `--dev-control`. The gate name is DELIBERATELY DISTINCT from D.5's
// TANKOBAN_DEV_UI_SIM — synthetic UI clicks and state writes / cache
// busts are independent risk surfaces and should not share a flag.
//
// Twelve spec-catalogue commands are intentionally absent from this
// surface — see DevControlServer.h's v1.9 block for the deferral list
// and the follow-on instrumentation commission outline.
class SystemIntrospection : public QObject
{
    Q_OBJECT
public:
    explicit SystemIntrospection(MainWindow* mainWindow, QObject* parent = nullptr);

    // Returns true if `cmd` is a SystemIntrospection-owned command and the
    // reply has been populated. The caller (MainWindow) is expected to have
    // pre-populated reply with framing keys ("type":"reply","seq":<int>);
    // on success we merge result fields; on error we overwrite "type" to
    // "error" and add "code" + "message" (MainWindow keeps "seq").
    bool dispatch(const QString& cmd,
                  const QJsonObject& payload,
                  QJsonObject& reply);

    // Catalogue of write-capable commands. MainWindow uses this to gate on
    // TANKOBAN_DEV_WRITE=1 before forwarding. Read-only commands return
    // false. The gate check happens BEFORE dispatch so write-capable
    // commands never partially execute and then bail.
    static bool isWriteCapable(const QString& cmd);

    // Full v1.9 command list — emitted by MainWindow's `ping` handler.
    static QStringList commandList();

private:
    // Per-prefix handlers. Each returns true iff it recognised the command.
    bool handleApp(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleSettings(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleJsonstore(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleCache(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleScanner(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleLog(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleEvents(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleTheme(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleFont(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handlePerf(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    bool handleDev(const QString& cmd, const QJsonObject& p, QJsonObject& r);
    // diag-* (v1.13 observability cluster) — read-only introspection of live
    // runtime state. timer-census enumerates every GUI-thread timer (QTimer
    // objects + raw startTimer/QBasicTimer ids). No state members.
    bool handleDiag(const QString& cmd, const QJsonObject& p, QJsonObject& r);

    QPointer<MainWindow> m_window;

    // Perf counters — named timing regions opened by perf-mark-start /
    // closed by perf-mark-end / read by perf-dump-counters. In-memory
    // only; cleared on app restart. No I/O — these stay cheap enough to
    // sprinkle anywhere.
    struct PerfRegion {
        QElapsedTimer running;
        bool          isOpen   = false;
        qint64        totalNs  = 0;
        int           count    = 0;
    };
    QHash<QString, PerfRegion> m_perfRegions;

    // dev-toggle-feature flag map + dev-inject-error fault map. In-memory
    // only — these mutate live app behaviour for resilience smokes (e.g.
    // "feature.streamCalendar = false" makes the calendar surface read
    // empty; "error.networkOffline = on" simulates an outage). The actual
    // consumers query these maps; v1.9 ships the map + write API but the
    // consumer-side reads are added piecemeal as agents request specific
    // injection points (so we don't litter random `if (injected)` checks
    // across the codebase preemptively).
    QHash<QString, bool>    m_featureFlags;
    QHash<QString, QString> m_errorInjects;

    // Per-component log level overrides — in-memory only. Consumed by
    // DebugLogBuffer / JsonlEventLog via a lookup helper exposed elsewhere
    // (initial v1.9 ship records the setting; actual filter-side wiring
    // is deferred to the same follow-on commission as the perf counter
    // infra).
    QHash<QString, QString> m_logLevels;
};
