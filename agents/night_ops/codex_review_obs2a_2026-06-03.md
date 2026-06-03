OpenAI Codex v0.131.0
--------
workdir: C:\Users\Suprabha\Desktop\Tankoban 2
model: gpt-5.5
provider: openai
approval: never
sandbox: read-only
reasoning effort: high
reasoning summaries: none
session id: 019e8c7e-27b6-78d3-b3a0-72a9f83b6b54
--------
user
Cross-model review for Tankoban 2 (requested by Agent 0). You are a DIFFERENT model than the author. Review these TWO COMPLETE NEW FILES (HangWatchdog.h + HangWatchdog.cpp) plus the main.cpp wiring shown below. This is OBS-2a: an off-GUI-thread hang detector. Read-only review — do NOT edit.

DEFINITION OF DONE — verify EACH:
E1. Background thread monitors a GUI heartbeat (atomic bumped ~16ms by a QTimer on the GUI thread).
E2. If GUI silent >750ms it writes out/HANG_DETECTED.json (timestamp + stall duration); auto-clears on recovery.
E3. Detection-only: no effect on normal runtime; gated behind --dev-control only.
E4. Watchdog thread shuts down cleanly on app exit (joined/stopped — no crash-on-exit, no detached thread touching freed state).
E5. Atomic heartbeat correctly synchronized (memory ordering; no torn reads).

Also: any real bug (race, UAF, deadlock, exit-order hazard between the std::thread and the GUI-thread QTimer teardown).

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason. Default REQUEST-CHANGES if any item NOT-MET or unsure.

======== src/devtools/HangWatchdog.h ========
#pragma once

#include <atomic>
#include <thread>

#include <QString>

class QObject;
class QTimer;

// OBS-2a (Track D observability) — heartbeat-detector watchdog.
//
// Writes out/HANG_DETECTED.json when the Qt GUI thread stops bumping a heartbeat
// for longer than thresholdMs. DETECTION ONLY: it suspends nothing, walks no
// stacks, and allocates nothing on the GUI thread during a stall — this is the
// auto-safe half. The in-process stack-walk that needs SuspendThread (and its
// loader-lock deadlock hazard) is the separate, deliberately-staged OBS-2b.
//
// Why a file artifact and not a tankoctl bridge command: the dev-control bridge
// dispatches synchronously ON the GUI thread, so it is DEAD during a freeze (a
// wedged GUI thread can't answer the pipe). An agent's persistent Monitor watches
// out/HANG_DETECTED.json as a wake channel that survives a frozen GUI thread.
//
// Lifetime contract: construct AFTER the QObject that owns the heartbeat timer
// (e.g. MainWindow) so this object destructs FIRST and stop()+joins the worker
// while that QObject (and the parented QTimer) is still alive. See src/main.cpp.
class HangWatchdog
{
public:
    explicit HangWatchdog(QObject* guiContext);   // guiContext owns the heartbeat QTimer
    ~HangWatchdog();                              // calls stop()

    HangWatchdog(const HangWatchdog&) = delete;
    HangWatchdog& operator=(const HangWatchdog&) = delete;

    // Starts the GUI-side heartbeat QTimer + the off-GUI poll thread. Must be
    // called on the GUI thread (the timer lives on that thread's event loop).
    void start(int thresholdMs = 750, int pollMs = 250, int beatMs = 16);

    // Stops + joins the poll thread, then tears the timer down. Idempotent.
    void stop();

private:
    void pollLoop();   // runs ON m_thread; touches NO QObject with thread affinity

    std::atomic<qint64> m_lastBeatMs{0};
    std::atomic<bool>   m_run{false};
    std::atomic<bool>   m_hangActive{false};
    std::thread         m_thread;
    int                 m_thresholdMs = 750;
    int                 m_pollMs      = 250;
    QString             m_hangPath;            // resolved in start(), never in pollLoop()
    QObject*            m_guiContext = nullptr;
    QTimer*             m_beatTimer  = nullptr;  // parented to m_guiContext (GUI thread)
};

======== src/devtools/HangWatchdog.cpp ========
#include "devtools/HangWatchdog.h"

#include <chrono>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

HangWatchdog::HangWatchdog(QObject* guiContext)
    : m_guiContext(guiContext)
{
}

HangWatchdog::~HangWatchdog()
{
    stop();
}

void HangWatchdog::start(int thresholdMs, int pollMs, int beatMs)
{
    if (m_run.load(std::memory_order_relaxed))
        return;  // already running

    m_thresholdMs = thresholdMs;
    m_pollMs      = pollMs;
    m_lastBeatMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
    m_hangActive.store(false, std::memory_order_relaxed);

    // Resolve the artifact path up-front (NOT in pollLoop) so the worker never
    // derives a path inside a stall window. Lands beside the exe in out/ (mirrors
    // JsonlEventLog's applicationDirPath() resolver — agents/Monitor look there).
    m_hangPath = QDir(QCoreApplication::applicationDirPath())
                     .absoluteFilePath(QStringLiteral("HANG_DETECTED.json"));
    // A stale artifact from a previous run must not lie to a fresh Monitor.
    QFile::remove(m_hangPath);

    // GUI-side heartbeat: a 16ms QTimer (NOT 0ms — a 0ms QTimer is an idle timer
    // that can be starved under a busy/rendering loop, producing false-positive
    // hangs). The threshold (750ms) is far larger than the beat interval, so
    // normal frame work never trips it.
    m_beatTimer = new QTimer(m_guiContext);
    m_beatTimer->setInterval(beatMs);
    QObject::connect(m_beatTimer, &QTimer::timeout, m_guiContext, [this]() {
        m_lastBeatMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
    });
    m_beatTimer->start();

    m_run.store(true, std::memory_order_relaxed);
    m_thread = std::thread([this]() { pollLoop(); });   // OFF the GUI thread — survives a freeze
}

void HangWatchdog::stop()
{
    if (m_run.exchange(false, std::memory_order_relaxed)) {
        if (m_thread.joinable())
            m_thread.join();
    }
    // Tear down the timer on the GUI thread. By the lifetime contract this object
    // destructs before m_guiContext, so the parented timer is still valid here and
    // we run on its thread — a direct delete is correct (deleteLater would never
    // fire: app.exec() has already returned, so there is no event loop left).
    if (m_beatTimer) {
        m_beatTimer->stop();
        delete m_beatTimer;
        m_beatTimer = nullptr;
    }
}

void HangWatchdog::pollLoop()
{
    // Worker thread. Only reentrant value/IO Qt classes (QFile / QJsonDocument /
    // QDateTime / QDir) are touched here — NEVER m_beatTimer / m_guiContext, which
    // have GUI-thread affinity.
    while (m_run.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_pollMs));
        if (!m_run.load(std::memory_order_relaxed))
            break;

        const qint64 now  = QDateTime::currentMSecsSinceEpoch();
        const qint64 last = m_lastBeatMs.load(std::memory_order_relaxed);
        const qint64 age  = now - last;

        if (age > m_thresholdMs && !m_hangActive.load(std::memory_order_relaxed)) {
            // Rising edge (healthy -> stalled): write the artifact exactly once.
            m_hangActive.store(true, std::memory_order_relaxed);
            const QJsonObject o{
                {QStringLiteral("schema"),           QStringLiteral("tankoban.hang.v1")},
                {QStringLiteral("detectedAt"),       QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {QStringLiteral("stalledMs"),        double(age)},
                {QStringLiteral("thresholdMs"),      double(m_thresholdMs)},
                {QStringLiteral("pid"),              double(QCoreApplication::applicationPid())},
                {QStringLiteral("lastResponsiveAt"), QDateTime::fromMSecsSinceEpoch(last, Qt::UTC).toString(Qt::ISODateWithMs)},
            };
            QFile f(m_hangPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
                f.close();
            }
        } else if (age <= m_thresholdMs && m_hangActive.load(std::memory_order_relaxed)) {
            // Recovery edge (stalled -> healthy): remove so a stale file never lies.
            m_hangActive.store(false, std::memory_order_relaxed);
            QFile::remove(m_hangPath);
        }
    }
}

======== main.cpp wiring (declared after window so it destructs first) ========
diff --git a/src/main.cpp b/src/main.cpp
index b3bfd82..cf9683d 100644
--- a/src/main.cpp
+++ b/src/main.cpp
@@ -10,11 +10,14 @@
 #include <QPixmapCache>
 #include <QStringList>
 #include <QTimer>
+#include <optional>
+
 #include "core/CoreBridge.h"
 #include "core/DebugLogBuffer.h"
 #include "core/JsonlEventLog.h"
 #include "core/manga/ComicsPrePivotMigrator.h"
 #include "ui/MainWindow.h"
+#include "devtools/HangWatchdog.h"
 #include "ui/Theme.h"
 
 #ifdef Q_OS_WIN
@@ -297,6 +300,12 @@ int main(int argc, char *argv[])
     MainWindow window(&bridge);
     dbg("5-mainwindow-created");
 
+    // OBS-2a (Track D observability) — heartbeat watchdog. Declared at function
+    // scope AFTER `window` so it destructs FIRST: stop()+join the off-GUI worker
+    // while `window` and the parented heartbeat QTimer are still alive. Started
+    // only under --dev-control (below), so production builds carry no watchdog.
+    std::optional<HangWatchdog> hangWatchdog;
+
     // Single-instance: claim the local socket so subsequent launches signal us.
     QLocalServer* instanceServer = createInstanceServer(&window);
     Q_UNUSED(instanceServer);  // window-parented, dies with window
@@ -309,6 +318,8 @@ int main(int argc, char *argv[])
     const bool devControlEnv  = qEnvironmentVariableIntValue("TANKOBAN_DEV_CONTROL") == 1;
     if (devControlFlag || devControlEnv) {
         window.enableDevControl();
+        hangWatchdog.emplace(&window);   // OBS-2a — off-GUI heartbeat -> out/HANG_DETECTED.json on a >750ms GUI stall
+        hangWatchdog->start();
         dbg("6a-devcontrol-enabled");
     }
 

codex
I’ll treat this as a read-only code review against the supplied snippets and the repository governance. I’m checking the five explicit exit criteria first, then the lifecycle/race hazards around `QTimer`, `std::thread`, and app shutdown.
codex
No blocking findings.

E1 met. E2 met assuming `applicationDirPath()` is the repo’s `out` executable directory as stated in the comments. E3 met for dev-control mode, including the existing env-gated dev-control path. E4 met under the shown `main.cpp` lifetime ordering: `HangWatchdog` is destroyed before `MainWindow`, stops the worker, joins it, then deletes the GUI-thread timer. E5 met: relaxed atomics are sufficient here because the heartbeat is a single atomic timestamp with no dependent shared state, and atomic `qint64` prevents torn reads.

APPROVE
tokens used
22,439
No blocking findings.

E1 met. E2 met assuming `applicationDirPath()` is the repo’s `out` executable directory as stated in the comments. E3 met for dev-control mode, including the existing env-gated dev-control path. E4 met under the shown `main.cpp` lifetime ordering: `HangWatchdog` is destroyed before `MainWindow`, stops the worker, joins it, then deletes the GUI-thread timer. E5 met: relaxed atomics are sufficient here because the heartbeat is a single atomic timestamp with no dependent shared state, and atomic `qint64` prevents torn reads.

APPROVE
