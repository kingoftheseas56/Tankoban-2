#pragma once

#include <QString>
#include <QtGlobal>
#include <vector>

class QRhi;

#ifdef _WIN32
struct IDXGISwapChain1;
#endif

// Phase 0 feasibility instrumentation for display-resample.
// Records per-render timing samples to a ring buffer; on demand, dumps to CSV
// for offline analysis (tools/analyze_vsync.py).
//
// Two timing sources are captured per sample:
//   1. Qt-observed render() interval (wall clock between consecutive calls)
//   2. DXGI frame statistics from the underlying D3D11 device (when available)
//
// The DXGI source is the authoritative one — we cross-check Qt's timing
// against it to decide whether display-resample can build on Qt's signal
// directly or whether we need a deeper refactor.
class VsyncTimingLogger {
public:
    explicit VsyncTimingLogger(int capacity = 8192);

    // Called once per render() — captures wall clock + DXGI stats.
    // Cheap; safe to call from the render thread.
    void recordSample(QRhi* rhi);

    // FrameCanvas overload. Uses swap-chain-level GetFrameStatistics
    // directly, which (unlike the QRhi/output-level path above) returns valid
    // stats every Present because the widget owns its own swap chain. The
    // QRhi overload above is dead code now that FrameCanvas is native D3D11
    // (kept for the moment in case any other future consumer needs it).
    //
    // frameLatencyMs (Batch 1.1): present-to-present interval overage vs the
    // expected frame interval (0.0 if on-schedule). Logged as a CSV column
    // for offline drift analysis.
    //
    // frameSkipped (Batch 1.2): true when this tick skipped Present() due to
    // sustained lag (OBS video_sleep pattern). When true, swapChain may be
    // nullptr — DXGI stats are stale so the logger won't call
    // GetFrameStatistics on a null chain.
    //
    // latencyEmaMs / clockVelocity (Batch 1.3): snapshot of SyncClock's
    // smoothed latency and derived velocity after the current tick's
    // reportFrameLatency call. Exposes the Phase 1 feedback loop's state
    // for offline CSV analysis (plan verification criterion: "velocity
    // drifts under forced load, returns to 1.0 under normal").
    //
    // chosenFrameId / fallbackUsed (Batch 2.1): this tick's SHM frame
    // selection outcome. chosenFrameId is the frame ID the render
    // consumed (0 if no SHM frame was consumed — zero-copy D3D11 path or
    // no new frame this tick). fallbackUsed is true when readBestForClock
    // returned invalid and consumeShmFrame fell back to readLatest
    // (startup + post-seek race windows). Agent 6 verifies "no dupes /
    // skips" by chosen_frame_id monotonicity during steady-state.
    //
    // producerDropsSinceLast / consumerLateMs (Batch 2.2): SHM-boundary
    // overflow-drop telemetry. producerDropsSinceLast is the count of
    // frameIds the sidecar wrote but we never saw, inferred from the
    // consumed-id gap. consumerLateMs is (sidecarClockUs - framePtsUs) in
    // ms — positive = displayed frame is stale; negative = ahead of clock
    // within readBestForClock tolerance. Agent 6 verifies: 60fps on 60Hz
    // healthy system → both near zero; GPU stall → drops burst; startup/
    // seek → brief negative consumer late values.
#ifdef _WIN32
    void recordSampleFromSwapChain(IDXGISwapChain1* swapChain,
                                   double frameLatencyMs = 0.0,
                                   bool frameSkipped = false,
                                   double latencyEmaMs = 0.0,
                                   double clockVelocity = 1.0,
                                   quint64 chosenFrameId = 0,
                                   bool fallbackUsed = false,
                                   quint32 producerDropsSinceLast = 0,
                                   double consumerLateMs = 0.0);
#endif

    // Dump all currently-buffered samples to a CSV file. Resets the ring.
    // Returns sample count written, or -1 on file error.
    int dumpToCsv(const QString& path);

    // One-line summary stat for toast / log.
    QString summary() const;

    // How many samples currently in the buffer.
    int sampleCount() const;

private:
    struct Sample {
        qint64 wallNs;            // QElapsedTimer at render() start
        qint64 qtIntervalNs;      // wallNs - prev wallNs (0 for first sample)
        // DXGI frame stats (zero-filled if unavailable)
        quint32 presentCount;
        quint32 presentRefresh;
        qint64  syncQpcTime;
        quint32 syncRefresh;
        bool    dxgiValid;
        double  frameLatencyMs;   // Batch 1.1 — present overage vs expected
        bool    frameSkipped;     // Batch 1.2 — Present() skipped this tick
        double  latencyEmaMs;     // Batch 1.3 — smoothed lag from SyncClock
        double  clockVelocity;    // Batch 1.3 — derived clock rate [0.995, 1.000]
        quint64 chosenFrameId;    // Batch 2.1 — frame ID consumed this tick
        bool    fallbackUsed;     // Batch 2.1 — readLatest fallback path taken
        quint32 producerDropsSinceLast;  // Batch 2.2 — frameIds sidecar produced that we never saw
        double  consumerLateMs;   // Batch 2.2 — (clock - ptsUs) ms for consumed frame
    };

    std::vector<Sample> ring_;
    int    head_ = 0;
    bool   wrapped_ = false;
    qint64 prevWallNs_ = 0;
    qint64 startWallNs_ = 0;
};
