#include "VsyncTimingLogger.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <rhi/qrhi.h>

#ifdef _WIN32
#include <rhi/qrhi_platform.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#endif

VsyncTimingLogger::VsyncTimingLogger(int capacity)
{
    ring_.resize(capacity);
}

int VsyncTimingLogger::sampleCount() const
{
    return wrapped_ ? static_cast<int>(ring_.size()) : head_;
}

void VsyncTimingLogger::recordSample(QRhi* rhi)
{
    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    qint64 nowNs = timer.nsecsElapsed();

    if (startWallNs_ == 0) startWallNs_ = nowNs;

    Sample s{};
    s.wallNs = nowNs - startWallNs_;
    s.qtIntervalNs = (prevWallNs_ == 0) ? 0 : (nowNs - prevWallNs_);
    prevWallNs_ = nowNs;

#ifdef _WIN32
    // Try to get DXGI frame stats from the D3D11 device QRhi created.
    // Note: we have access to the device but not the swap chain. The swap
    // chain is owned by QRhiWidget privately. So we use the *device*-level
    // stats via IDXGISwapChain enumeration.
    //
    // For Phase 0 we accept that DXGI stats may not always be available
    // (Qt private internals). The Qt render interval is the primary signal.
    s.dxgiValid = false;
    if (rhi) {
        const QRhiD3D11NativeHandles* nh =
            static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
        if (nh && nh->dev) {
            ID3D11Device* dev = static_cast<ID3D11Device*>(nh->dev);
            IDXGIDevice* dxgiDev = nullptr;
            if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice),
                                              reinterpret_cast<void**>(&dxgiDev))) && dxgiDev) {
                // Walk the device's adapter to find an output, then use that
                // output's GetFrameStatistics. This gives us global vsync
                // timing for the primary monitor.
                IDXGIAdapter* adapter = nullptr;
                if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && adapter) {
                    IDXGIOutput* output = nullptr;
                    if (SUCCEEDED(adapter->EnumOutputs(0, &output)) && output) {
                        DXGI_FRAME_STATISTICS fs{};
                        if (SUCCEEDED(output->GetFrameStatistics(&fs))) {
                            s.presentCount   = fs.PresentCount;
                            s.presentRefresh = fs.PresentRefreshCount;
                            s.syncQpcTime    = fs.SyncQPCTime.QuadPart;
                            s.syncRefresh    = fs.SyncRefreshCount;
                            s.dxgiValid      = true;
                        }
                        output->Release();
                    }
                    adapter->Release();
                }
                dxgiDev->Release();
            }
        }
    }
#else
    Q_UNUSED(rhi);
    s.dxgiValid = false;
#endif

    ring_[head_] = s;
    head_++;
    if (head_ >= static_cast<int>(ring_.size())) {
        head_ = 0;
        wrapped_ = true;
    }
}

#ifdef _WIN32
void VsyncTimingLogger::recordSampleFromSwapChain(IDXGISwapChain1* swapChain,
                                                  double frameLatencyMs,
                                                  bool frameSkipped,
                                                  double latencyEmaMs,
                                                  double clockVelocity,
                                                  quint64 chosenFrameId,
                                                  bool fallbackUsed,
                                                  quint32 producerDropsSinceLast,
                                                  double consumerLateMs)
{
    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    qint64 nowNs = timer.nsecsElapsed();

    if (startWallNs_ == 0) startWallNs_ = nowNs;

    Sample s{};
    s.wallNs = nowNs - startWallNs_;
    s.qtIntervalNs = (prevWallNs_ == 0) ? 0 : (nowNs - prevWallNs_);
    prevWallNs_ = nowNs;
    s.frameLatencyMs = frameLatencyMs;
    s.frameSkipped = frameSkipped;
    s.latencyEmaMs = latencyEmaMs;
    s.clockVelocity = clockVelocity;
    s.chosenFrameId = chosenFrameId;
    s.fallbackUsed = fallbackUsed;
    s.producerDropsSinceLast = producerDropsSinceLast;
    s.consumerLateMs = consumerLateMs;

    s.dxgiValid = false;
    if (swapChain) {
        DXGI_FRAME_STATISTICS fs{};
        if (SUCCEEDED(swapChain->GetFrameStatistics(&fs))) {
            s.presentCount   = fs.PresentCount;
            s.presentRefresh = fs.PresentRefreshCount;
            s.syncQpcTime    = fs.SyncQPCTime.QuadPart;
            s.syncRefresh    = fs.SyncRefreshCount;
            s.dxgiValid      = true;
        }
        // GetFrameStatistics may legitimately return DXGI_ERROR_FRAME_STATISTICS_DISJOINT
        // on the first frames after Present; we accept that as dxgiValid=false.
    }

    ring_[head_] = s;
    head_++;
    if (head_ >= static_cast<int>(ring_.size())) {
        head_ = 0;
        wrapped_ = true;
    }
}
#endif

int VsyncTimingLogger::dumpToCsv(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return -1;

    QTextStream out(&f);
    out << "wall_ns,qt_interval_ns,dxgi_valid,present_count,present_refresh,"
           "sync_qpc_time,sync_refresh,frame_latency_ms,frame_skipped,"
           "latency_ema_ms,clock_velocity,chosen_frame_id,fallback_used,"
           "producer_drops_since_last,consumer_late_ms\n";

    int count = sampleCount();
    int start = wrapped_ ? head_ : 0;
    int written = 0;
    for (int i = 0; i < count; ++i) {
        int idx = (start + i) % static_cast<int>(ring_.size());
        const Sample& s = ring_[idx];
        out << s.wallNs << ','
            << s.qtIntervalNs << ','
            << (s.dxgiValid ? 1 : 0) << ','
            << s.presentCount << ','
            << s.presentRefresh << ','
            << s.syncQpcTime << ','
            << s.syncRefresh << ','
            << s.frameLatencyMs << ','
            << (s.frameSkipped ? 1 : 0) << ','
            << s.latencyEmaMs << ','
            << s.clockVelocity << ','
            << s.chosenFrameId << ','
            << (s.fallbackUsed ? 1 : 0) << ','
            << s.producerDropsSinceLast << ','
            << s.consumerLateMs << '\n';
        ++written;
    }

    f.close();

    // Reset ring after dump
    head_ = 0;
    wrapped_ = false;
    prevWallNs_ = 0;
    startWallNs_ = 0;

    return written;
}

QString VsyncTimingLogger::summary() const
{
    int count = sampleCount();
    if (count < 2) return QStringLiteral("no samples yet");

    int start = wrapped_ ? head_ : 0;
    qint64 sum = 0;
    qint64 minNs = INT64_MAX;
    qint64 maxNs = 0;
    int valid = 0;
    for (int i = 1; i < count; ++i) {  // skip first (interval == 0)
        int idx = (start + i) % static_cast<int>(ring_.size());
        qint64 iv = ring_[idx].qtIntervalNs;
        if (iv <= 0) continue;
        sum += iv;
        if (iv < minNs) minNs = iv;
        if (iv > maxNs) maxNs = iv;
        ++valid;
    }
    if (valid == 0) return QStringLiteral("no valid intervals");

    double meanMs = (sum / static_cast<double>(valid)) / 1e6;
    double minMs = minNs / 1e6;
    double maxMs = maxNs / 1e6;
    double approxFps = 1000.0 / meanMs;

    return QStringLiteral("vsync n=%1 mean=%2ms (~%3Hz) min=%4 max=%5")
        .arg(valid)
        .arg(meanMs, 0, 'f', 3)
        .arg(approxFps, 0, 'f', 2)
        .arg(minMs, 0, 'f', 3)
        .arg(maxMs, 0, 'f', 3);
}
