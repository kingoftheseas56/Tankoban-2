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
