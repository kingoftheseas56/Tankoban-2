#include "FrameCanvas.h"
#include "ShmFrameReader.h"
#include "SyncClock.h"

#include <QImage>
#include <QMouseEvent>
#include <cstring>

#ifdef _WIN32
#include <d3d11.h>
#include <d3d11_1.h>     // ID3D11Device1 — Phase 5 OpenSharedResource1
#include <dxgi1_2.h>
#include <dxgi1_4.h>     // IDXGISwapChain3::SetColorSpace1 — Batch 3.5
#include <dxgi1_6.h>     // IDXGIOutput6::GetDesc1 — Batch 3.5 HDR detection
#include <d3dcompiler.h>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#endif

FrameCanvas::FrameCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);

    // Phase 7 bake-in fix — emit mouseActivity() on every mouse move so
    // VideoPlayer can drive the bottom-HUD-reveal. Required because
    // WA_PaintOnScreen makes us a separate native HWND that doesn't bubble
    // mouse events up to the parent.
    setMouseTracking(true);

#ifdef _WIN32
    m_renderTimer.setTimerType(Qt::PreciseTimer);
    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout, this, &FrameCanvas::renderFrame);
#endif
}

FrameCanvas::~FrameCanvas()
{
#ifdef _WIN32
    m_renderTimer.stop();
    tearDownD3D();
#endif
}

void FrameCanvas::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

#ifdef _WIN32
    if (m_device) {
        if (!m_renderTimer.isActive()) {
            m_renderTimer.start();
        }
        return;
    }
    initializeD3D();
#endif
}

#ifdef _WIN32
bool FrameCanvas::initializeD3D()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // FL_11_1 requested first (vs the spec preamble's "don't be clever" 11_0)
    // because Phase 5's OpenSharedResource1 is the FL_11_1 entrypoint on
    // ID3D11Device1. FL_11_0 is the documented fallback — we still accept
    // it for older GPUs, just without the Holy Grail zero-copy path.
    // (Closes Phase 1 review P2 #2 — Agent 6 2026-04-14.)
    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D_FEATURE_LEVEL gotLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // no software rasterizer
        flags,
        requested,
        ARRAYSIZE(requested),
        D3D11_SDK_VERSION,
        &m_device,
        &gotLevel,
        &m_context);

    if (FAILED(hr)) {
        qWarning("FrameCanvas: D3D11CreateDevice failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: device created (feature level 0x%x)", static_cast<unsigned>(gotLevel));

    // Walk D3D11Device -> IDXGIDevice -> IDXGIAdapter -> IDXGIFactory2 to reach
    // CreateSwapChainForHwnd (the only swap-chain entrypoint that supports the
    // FLIP_DISCARD model we need for tear-free vsync presentation on Win10+).
    IDXGIDevice*  dxgiDevice  = nullptr;
    IDXGIAdapter* dxgiAdapter = nullptr;
    IDXGIFactory2* dxgiFactory = nullptr;

    hr = m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr)) {
        qWarning("FrameCanvas: QueryInterface(IDXGIDevice) failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    dxgiDevice->Release();
    if (FAILED(hr)) {
        qWarning("FrameCanvas: GetAdapter failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));
    if (FAILED(hr)) {
        dxgiAdapter->Release();
        qWarning("FrameCanvas: GetParent(IDXGIFactory2) failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    // Batch 3.5 — HDR capability detection. Find the adapter output whose
    // monitor matches the window's monitor (via MonitorFromWindow +
    // DXGI_OUTPUT_DESC.Monitor comparison), QueryInterface to IDXGIOutput6
    // (Win10 Creators Update+), call GetDesc1, inspect ColorSpace. When the
    // OS+display advertise HDR10 (DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
    // we create the swap chain with R16G16B16A16_FLOAT + SetColorSpace1 to
    // scRGB (G10_NONE_P709) so the shader can output linear light directly.
    //
    // Phase 3 REVIEW P1 fix (2026-04-15): original Batch 3.5 implementation
    // used dxgiAdapter->EnumOutputs(0) — unconditionally picked adapter
    // output 0, wrong on multi-monitor rigs where the player window can
    // live on output 1..N. Pattern now mirrors OBS libobs-d3d11/
    // d3d11-subsystem.cpp:71-78's MonitorFromWindow + iterate-outputs
    // match. Falls back to output 0 if no match (rare — window off all
    // known monitors during a drag, etc.), and to SDR if everything below
    // fails. All queries best-effort.
    HWND hwnd = reinterpret_cast<HWND>(winId());
    HMONITOR targetMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    bool hdrCapable = false;
    {
        IDXGIOutput* matchedOutput = nullptr;
        if (targetMonitor) {
            for (UINT i = 0; ; ++i) {
                IDXGIOutput* out = nullptr;
                HRESULT er = dxgiAdapter->EnumOutputs(i, &out);
                if (FAILED(er) || !out) break;
                DXGI_OUTPUT_DESC odesc = {};
                if (SUCCEEDED(out->GetDesc(&odesc)) && odesc.Monitor == targetMonitor) {
                    matchedOutput = out;  // take ownership, stop iterating
                    break;
                }
                out->Release();
            }
        }
        // Fallback — no window-monitor match (rare). Take adapter output 0
        // so at least SOMETHING gets probed; behaves identically to the
        // pre-P1-fix code on single-monitor systems.
        if (!matchedOutput) {
            dxgiAdapter->EnumOutputs(0, &matchedOutput);
        }
        if (matchedOutput) {
            IDXGIOutput6* output6 = nullptr;
            if (SUCCEEDED(matchedOutput->QueryInterface(__uuidof(IDXGIOutput6),
                                                        reinterpret_cast<void**>(&output6))) && output6) {
                DXGI_OUTPUT_DESC1 desc1 = {};
                if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                    hdrCapable = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                    qDebug("FrameCanvas: window monitor color space 0x%x, max %.0f nits — HDR %s",
                           static_cast<unsigned>(desc1.ColorSpace),
                           desc1.MaxLuminance,
                           hdrCapable ? "supported" : "not supported");
                }
                output6->Release();
            }
            matchedOutput->Release();
        }
    }
    dxgiAdapter->Release();

    // Swap chain dims must be in physical pixels — width()/height() are
    // logical, so multiply by devicePixelRatio. Without this the back buffer
    // is created at logical size and DXGI bilinearly upscales to fill the
    // native HWND, producing visible blur on HiDPI (>100% scale) displays.
    // (Agent 6 review P1, 2026-04-14.)
    const qreal dpr = devicePixelRatioF();
    const int w = qMax(1, qRound(width()  * dpr));
    const int h = qMax(1, qRound(height() * dpr));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width              = static_cast<UINT>(w);
    desc.Height             = static_cast<UINT>(h);
    // Batch 3.5 — 16-bit float back buffer on HDR displays (enough headroom
    // for scRGB values > 1.0 representing highlights above 80 nits); keeps
    // the existing 8-bit BGRA for SDR displays where precision isn't the
    // bottleneck.
    desc.Format             = hdrCapable ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                         : DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo             = FALSE;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount        = 2;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags              = 0;

    // hwnd already acquired above for the MonitorFromWindow HDR probe.
    hr = dxgiFactory->CreateSwapChainForHwnd(
        m_device,
        hwnd,
        &desc,
        nullptr,    // no fullscreen desc
        nullptr,    // no output restriction
        &m_swapChain);
    dxgiFactory->Release();

    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateSwapChainForHwnd failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: swap chain created %dx%d (%s)",
           w, h,
           hdrCapable ? "R16G16B16A16_FLOAT / HDR10 target" : "B8G8R8A8_UNORM / SDR");

    // Batch 3.5 — set the back-buffer color space when we've committed to
    // the 16-bit float HDR format. scRGB (G10_NONE_P709) expects linear
    // light with sRGB primaries where 1.0 = 80 nits; the shader's
    // hdrOutput path skips tonemap + gamma encoding so the buffer
    // receives values in that space directly. If SetColorSpace1 fails
    // we fall back silently to SDR behavior — the 16-bit buffer still
    // works as a linear sRGB swap chain, just without HDR-range output.
    if (hdrCapable) {
        IDXGISwapChain3* swapChain3 = nullptr;
        HRESULT cshr = m_swapChain->QueryInterface(__uuidof(IDXGISwapChain3),
                                                    reinterpret_cast<void**>(&swapChain3));
        if (SUCCEEDED(cshr) && swapChain3) {
            cshr = swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
            if (SUCCEEDED(cshr)) {
                m_colorParams.hdrOutput = 1;
                qDebug("FrameCanvas: HDR output active (scRGB / G10_NONE_P709)");
            } else {
                qWarning("FrameCanvas: SetColorSpace1 failed, hr=0x%08lx — staying SDR-output",
                         static_cast<unsigned long>(cshr));
            }
            swapChain3->Release();
        } else {
            qWarning("FrameCanvas: IDXGISwapChain3 unavailable — staying SDR-output");
        }
    }

    // Batch 1.2 — query display refresh once the HWND is in its final monitor
    // (post-swap-chain creation) and convert to the expected per-frame
    // interval that seeds lag computation. GetDeviceCaps(VREFRESH) returns
    // the current monitor mode's advertised refresh rate (60 typical,
    // 120/144/165/240 on gaming monitors, 1 for "default"/variable). We
    // only accept values in [30, 360]; anything outside keeps the 60Hz
    // fallback set at construction.
    if (HDC hdc = GetDC(hwnd)) {
        const int refreshHz = GetDeviceCaps(hdc, VREFRESH);
        ReleaseDC(hwnd, hdc);
        if (refreshHz >= 30 && refreshHz <= 360) {
            m_expectedFrameMs = 1000.0 / refreshHz;
            qDebug("FrameCanvas: expected frame interval %.3fms (%d Hz)",
                   m_expectedFrameMs, refreshHz);
        } else {
            qDebug("FrameCanvas: refresh query returned %d Hz, keeping 60Hz default (%.3fms)",
                   refreshHz, m_expectedFrameMs);
        }
    }

    if (!createShaders()) {
        // Shaders failing isn't fatal for Phase 1 magenta — render loop still
        // runs and the test harness keeps showing magenta (pre-shader path).
        // Phase 2.4+ will start binding shaders for textured rendering.
        qWarning("FrameCanvas: shader creation failed; continuing with clear-only render path");
    } else if (!createVertexBuffer()) {
        qWarning("FrameCanvas: vertex buffer/input layout creation failed; continuing with clear-only render path");
    } else if (!createStateObjects()) {
        qWarning("FrameCanvas: pipeline state object creation failed; continuing with clear-only render path");
    } else if (!createColorBuffer()) {
        qWarning("FrameCanvas: color buffer creation failed; continuing with textured-quad-without-color-processing fallback");
    }

    m_renderTimer.start();
    return true;
}
#endif

void FrameCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

#ifdef _WIN32
    if (!m_swapChain) {
        return;
    }

    // Skip resize on hidden / minimized windows — Win32 fires a 0x0 resize
    // when minimized, which our qMax(1, ...) clamps to 1x1; ResizeBuffers
    // would succeed but waste a buffer realloc that gets immediately undone
    // when the window is restored. (Closes Phase 1 review P2 #3 —
    // Agent 6 2026-04-14.)
    if (!isVisible() || isMinimized()) {
        return;
    }

    // FLIP_DISCARD requires releasing all back-buffer references before
    // ResizeBuffers, otherwise the call fails with DXGI_ERROR_INVALID_CALL.
    releaseBackBufferView();

    // Match swap chain creation: physical-pixel dims, not logical.
    const qreal dpr = devicePixelRatioF();
    HRESULT hr = m_swapChain->ResizeBuffers(
        0,                          // keep existing buffer count
        static_cast<UINT>(qMax(1, qRound(width()  * dpr))),
        static_cast<UINT>(qMax(1, qRound(height() * dpr))),
        DXGI_FORMAT_UNKNOWN,        // keep existing format
        0);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: ResizeBuffers failed, hr=0x%08lx", static_cast<unsigned long>(hr));
    }
#endif
}

void FrameCanvas::paintEvent(QPaintEvent* /*event*/)
{
    // Rendering is driven by m_renderTimer. With WA_PaintOnScreen, Qt won't
    // double-buffer; we ignore paint events and present from the timer instead.
}

void FrameCanvas::mouseMoveEvent(QMouseEvent* event)
{
    emit mouseActivityAt(event->position().y());
    QWidget::mouseMoveEvent(event);
}

#ifdef _WIN32

// Batch 6.2 — classify an HRESULT as a device-lost failure. Covers both
// flavors DXGI surfaces: DEVICE_REMOVED (GPU reset, driver uninstall,
// kernel TDR) and DEVICE_RESET (internal driver reset without adapter
// removal). Pattern cribbed from OBS libobs-d3d11/d3d11-subsystem.cpp
// ~line 44. Other Present failures are logged but not treated as lost.
namespace {
bool isDeviceLost(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED
        || hr == DXGI_ERROR_DEVICE_RESET;
}
}

void FrameCanvas::tearDownD3D()
{
    // Release in reverse creation order so dependent objects don't dangle
    // mid-teardown. Idempotent: safe to call on partially-initialized or
    // already-torn-down state (every pointer is null-checked).
    if (m_importedSrv)    { m_importedSrv->Release();    m_importedSrv    = nullptr; }
    if (m_importedD3DTex) { m_importedD3DTex->Release(); m_importedD3DTex = nullptr; }
    if (m_videoSrv)       { m_videoSrv->Release();       m_videoSrv       = nullptr; }
    if (m_videoTexture)   { m_videoTexture->Release();   m_videoTexture   = nullptr; }
    m_videoTexW = 0;
    m_videoTexH = 0;
    if (m_colorBuffer)  { m_colorBuffer->Release();  m_colorBuffer  = nullptr; }
    if (m_blend)       { m_blend->Release();       m_blend       = nullptr; }
    if (m_rasterizer)  { m_rasterizer->Release();  m_rasterizer  = nullptr; }
    if (m_sampler)     { m_sampler->Release();     m_sampler     = nullptr; }
    if (m_inputLayout) { m_inputLayout->Release(); m_inputLayout = nullptr; }
    if (m_vbuf)        { m_vbuf->Release();        m_vbuf        = nullptr; }
    if (m_psBlob)      { m_psBlob->Release();      m_psBlob      = nullptr; }
    if (m_vsBlob)      { m_vsBlob->Release();      m_vsBlob      = nullptr; }
    if (m_ps)          { m_ps->Release();          m_ps          = nullptr; }
    if (m_vs)          { m_vs->Release();          m_vs          = nullptr; }
    if (m_rtv)         { m_rtv->Release();         m_rtv         = nullptr; }
    if (m_swapChain)   { m_swapChain->Release();   m_swapChain   = nullptr; }
    if (m_context)     { m_context->Release();     m_context     = nullptr; }
    if (m_device)      { m_device->Release();      m_device      = nullptr; }
}

void FrameCanvas::recoverFromDeviceLost()
{
    // Guard against re-entry if initializeD3D's Present path or any child
    // call tripped another device-lost signal during the recreation. One
    // recovery attempt per render-loop visit — if it doesn't stick, the
    // next Present failure on the next tick will fire another attempt.
    if (m_recovering) return;
    m_recovering = true;

    // Log the GPU-reported removal reason — invaluable for post-mortem on
    // repeat device-lost events (driver version, TDR, OOM, etc.). Query
    // BEFORE teardown since the reason APIs live on m_device.
    if (m_device) {
        HRESULT removalHr = m_device->GetDeviceRemovedReason();
        qWarning("FrameCanvas: device lost — GetDeviceRemovedReason hr=0x%08lx",
                 static_cast<unsigned long>(removalHr));
    }

    emit deviceReconnecting();

    // Pause the render loop so teardown isn't racing against renderFrame's
    // own pointer reads. Restart after successful reinit.
    m_renderTimer.stop();

    // Tell any listeners that zero-copy is gone. The sidecar will need to
    // re-publish its shared texture for zero-copy to resume (the sidecar-
    // published NT handle is device-scoped to the sidecar's D3D device,
    // re-import works, but the import slot's contents are lost with the
    // old m_importedD3DTex / m_importedSrv).
    if (m_d3dActive) {
        m_d3dActive = false;
        emit zeroCopyActivated(false);
    }
    m_pendingD3DHandle = 0;
    m_pendingD3DWidth  = 0;
    m_pendingD3DHeight = 0;

    tearDownD3D();

    if (!initializeD3D()) {
        qWarning("FrameCanvas: recovery reinit failed — render loop stays stopped");
        m_recovering = false;
        return;
    }

    qDebug("FrameCanvas: device recovery complete");
    m_recovering = false;
}

void FrameCanvas::releaseBackBufferView()
{
    if (m_rtv) {
        m_rtv->Release();
        m_rtv = nullptr;
    }
}

bool FrameCanvas::ensureBackBufferView()
{
    if (m_rtv || !m_swapChain || !m_device) {
        return m_rtv != nullptr;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr)) {
        qWarning("FrameCanvas: GetBuffer(0) failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
    backBuffer->Release();
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateRenderTargetView failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        m_rtv = nullptr;
        return false;
    }

    return true;
}

void FrameCanvas::renderFrame()
{
    if (!m_swapChain || !m_context) {
        return;
    }
    if (!ensureBackBufferView()) {
        return;
    }

    // Batch 1.2 — lag-aware skip. When the prior tick saw sustained lag,
    // skip this tick's Present(): the previously-presented frame stays on
    // screen for one vsync while the render pipeline catches up, and we
    // don't waste GPU work drawing into a back buffer that won't reach the
    // display on time. The interval timer still restarts so the next
    // sample's latency measurement references this tick's wall clock.
    // A telemetry sample is recorded with frameSkipped=true and no DXGI
    // stats (swap chain wasn't Presented → GetFrameStatistics is stale).
    if (m_skipNextPresent) {
        m_skipNextPresent = false;
        ++m_framesSkippedTotal;
        m_intervalTimer.restart();
        if (m_vsyncLoggingOn) {
            // Still capture clock state on skip samples so the CSV shows
            // whether velocity is drifting during the skip window. Skip
            // tick = no frame chosen; chosen_frame_id=0, fallback_used=0,
            // producer_drops_since_last=0, consumer_late_ms=0 (Batch 2.2:
            // no consume happened on this tick).
            const double ema = m_syncClock ? m_syncClock->latencyEmaMs() : 0.0;
            const double vel = m_syncClock ? m_syncClock->getClockVelocity() : 1.0;
            m_vsyncLogger.recordSampleFromSwapChain(
                nullptr, 0.0, /*frameSkipped=*/true, ema, vel,
                /*chosenFrameId=*/0, /*fallbackUsed=*/false,
                /*producerDropsSinceLast=*/0, /*consumerLateMs=*/0.0);
        }
        return;
    }

    // Batch 2.1 — reset per-tick frame-selection telemetry so a tick where
    // consumeShmFrame never ran (zero-copy D3D11 path or no new frame in
    // the ring) logs as 0 / false rather than leaking the previous tick's
    // values. consumeShmFrame updates these members when it actually picks
    // a frame; the logger at tick end reads the final values.
    m_lastChosenFrameId = 0;
    m_lastFallbackUsed = false;
    // Batch 2.2 — same pattern for overflow-drop telemetry.
    m_lastProducerDrops = 0;
    m_lastConsumerLateMs = 0.0;

    // Black clear — letterbox bars + loading-screen background. Was magenta
    // through Phase 2-6 as a diagnostic for "clear succeeded but draw didn't"
    // failures; swapped to black at Phase 7 (post-cutover) for production
    // appearance now that the pipeline is verified end-to-end.
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_rtv, black);

    drawTexturedQuad();

    HRESULT hr = m_swapChain->Present(1, 0);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: Present failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        // Batch 6.2 — DEVICE_REMOVED / _RESET → kick off recovery. All
        // D3D11 pointers will be released + recreated; return early so
        // the rest of this tick doesn't touch them.
        if (isDeviceLost(hr)) {
            recoverFromDeviceLost();
            return;
        }
    }

    // Batch 1.1 — compute present-to-present interval and derive frame
    // latency (overage vs the expected frame interval). First sample has no
    // prior timestamp → latency = 0 and we seed the timer. Subsequent
    // samples measure how far past the expected vblank we actually landed.
    double frameLatencyMs = 0.0;
    if (m_intervalTimer.isValid()) {
        double intervalMs = m_intervalTimer.nsecsElapsed() / 1.0e6;
        if (intervalMs > m_expectedFrameMs) {
            frameLatencyMs = intervalMs - m_expectedFrameMs;
        }
    }
    m_intervalTimer.restart();

    // Batch 1.2 — forward latency to SyncClock. Dead storage today; Phase 4
    // will read accumulated velocity from the clock and forward to the
    // sidecar as an audio-speed adjustment (Kodi ActiveAE ±5% pattern).
    if (m_syncClock) {
        m_syncClock->reportFrameLatency(frameLatencyMs);
    }

    // Batch 1.2 — sustained-lag → skip-next (OBS obs-video.c:814-827
    // pattern adapted). OBS can fast-forward multiple intervals at once;
    // we only ship one Present() per tick, so the adapted semantic is
    // single-skip: let the render pipeline catch up by pausing Present()
    // for one vsync rather than piling up calls.
    if (frameLatencyMs > kLagThresholdMs) {
        if (++m_lagTickCount >= kLagSustainTicks) {
            m_skipNextPresent = true;
            m_lagTickCount = 0;
        }
    } else {
        m_lagTickCount = 0;
    }

    // Phase 6 — vsync timing telemetry. Owns its own swap chain → DXGI
    // GetFrameStatistics returns valid stats every Present, unlike
    // FrameCanvas's QRhi/output-level path that returns 0 valid samples.
    // This is the entire point of the Path B refactor.
    if (m_vsyncLoggingOn) {
        // Batch 1.3 — snapshot clock state for the CSV. SyncClock is not
        // guaranteed to exist (VideoPlayer wires it in buildUI, but
        // FrameCanvas may be tested without one); fall back to nominal.
        // Batch 2.1 — chosen_frame_id + fallback_used capture this tick's
        // frame-selection outcome (0/false if no SHM frame was consumed —
        // zero-copy path or nothing new in the ring).
        // Batch 2.2 — producer_drops_since_last + consumer_late_ms carry
        // the SHM-boundary overflow-drop metrics (both 0 if no consume).
        const double ema = m_syncClock ? m_syncClock->latencyEmaMs() : 0.0;
        const double vel = m_syncClock ? m_syncClock->getClockVelocity() : 1.0;
        m_vsyncLogger.recordSampleFromSwapChain(
            m_swapChain, frameLatencyMs, /*frameSkipped=*/false, ema, vel,
            m_lastChosenFrameId, m_lastFallbackUsed,
            m_lastProducerDrops, m_lastConsumerLateMs);
    }
}

void FrameCanvas::drawTexturedQuad()
{
    if (!m_vs || !m_ps || !m_inputLayout || !m_vbuf
        || !m_sampler || !m_rasterizer || !m_blend) {
        return;
    }

    // Phase 5 — try the Holy Grail import first. Once active, sidecar fills
    // m_importedD3DTex via CopySubresourceRegion + Flush from its side; we
    // skip the SHM upload entirely (no per-frame BGRA copy on our side).
    processPendingImport();

    ID3D11ShaderResourceView* activeSrv = nullptr;
    if (m_d3dActive && m_importedSrv) {
        activeSrv = m_importedSrv;
    } else {
        // SHM fallback path. consumeShmFrame is gated on m_polling so this
        // is a no-op until VideoPlayer attaches a reader. When nothing's
        // attached (loading screen, between videos), activeSrv stays null
        // and the early-return below skips the draw — black clear shows.
        const bool fresh = consumeShmFrame();
        if (m_videoSrv && (fresh || m_polling)) {
            activeSrv = m_videoSrv;
        }
    }
    if (!activeSrv) {
        return;
    }

    // Aspect-ratio viewport — port of FrameCanvas::render():298-315.
    // Letterboxes the source-aspect quad inside the back buffer so squares
    // stay square. Black bars (or, for now, the magenta clear color) fill
    // the unused space on the short axis.
    // Viewport is in render-target pixels (= physical), so scale by DPR to
    // match the physical-pixel swap chain.
    const qreal dpr = devicePixelRatioF();
    const float widgetW = static_cast<float>(qMax(1, qRound(width()  * dpr)));
    const float widgetH = static_cast<float>(qMax(1, qRound(height() * dpr)));
    const float widgetAspect = widgetW / widgetH;
    const float frameAspect  = (m_forcedAspect > 0.0)
        ? static_cast<float>(m_forcedAspect)
        : ((m_frameW > 0 && m_frameH > 0)
            ? static_cast<float>(m_frameW) / static_cast<float>(m_frameH)
            : widgetAspect);

    float vpX, vpY, vpW, vpH;
    if (frameAspect > widgetAspect) {
        // Frame wider than widget — fit width, bars top/bottom.
        vpW = widgetW;
        vpH = widgetW / frameAspect;
        vpX = 0.0f;
        vpY = (widgetH - vpH) * 0.5f;
    } else {
        // Frame taller than widget — fit height, bars left/right.
        vpH = widgetH;
        vpW = widgetH * frameAspect;
        vpX = (widgetW - vpW) * 0.5f;
        vpY = 0.0f;
    }

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = vpX;
    vp.TopLeftY = vpY;
    vp.Width    = vpW;
    vp.Height   = vpH;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // Aspect diagnostic — prints whenever frame dims OR widget dims change.
    // Widget-dim changes catch fullscreen transitions where the frame stays
    // the same but the viewport target grows/shrinks. Writes directly to
    // _player_debug.txt (not via qDebug, which doesn't land in that file).
    const int widgetWPx = static_cast<int>(widgetW);
    const int widgetHPx = static_cast<int>(widgetH);
    if (m_frameW != m_aspectLoggedForFrameW || m_frameH != m_aspectLoggedForFrameH
        || widgetWPx != m_aspectLoggedForWidgetW || widgetHPx != m_aspectLoggedForWidgetH) {
        m_aspectLoggedForFrameW  = m_frameW;
        m_aspectLoggedForFrameH  = m_frameH;
        m_aspectLoggedForWidgetW = widgetWPx;
        m_aspectLoggedForWidgetH = widgetHPx;
        QFile dbg("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
        if (dbg.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream s(&dbg);
            s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
              << " [FrameCanvas aspect] source=" << m_frameW << "x" << m_frameH
              << " widget=" << widgetW << "x" << widgetH
              << " dpr=" << QString::number(dpr, 'f', 2)
              << " frameAspect=" << QString::number(frameAspect, 'f', 4)
              << " widgetAspect=" << QString::number(widgetAspect, 'f', 4)
              << " vp={" << vpX << "," << vpY << "," << vpW << "," << vpH << "}"
              << " forced=" << QString::number(m_forcedAspect, 'f', 4) << "\n";
        }
    }

    m_context->OMSetRenderTargets(1, &m_rtv, nullptr);

    const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_context->OMSetBlendState(m_blend, blendFactor, 0xFFFFFFFF);
    m_context->RSSetState(m_rasterizer);

    m_context->IASetInputLayout(m_inputLayout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    UINT stride = sizeof(float) * 4;   // pos[2] + uv[2]
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, &m_vbuf, &stride, &offset);

    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    m_context->PSSetShaderResources(0, 1, &activeSrv);

    if (m_colorBuffer) {
        uploadColorParams();
        m_context->PSSetConstantBuffers(0, 1, &m_colorBuffer);
    }

    m_context->Draw(4, 0);
}

bool FrameCanvas::createShaders()
{
    if (!m_device) {
        return false;
    }
    if (m_vs && m_ps) {
        return true;
    }

    QFile srcFile(QStringLiteral(":/shaders/video_d3d11.hlsl"));
    if (!srcFile.open(QIODevice::ReadOnly)) {
        qWarning("FrameCanvas: cannot open :/shaders/video_d3d11.hlsl");
        return false;
    }
    const QByteArray src = srcFile.readAll();
    srcFile.close();

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    auto compileStage = [&](const char* entry, const char* target, ID3D10Blob** outBlob) -> bool {
        ID3D10Blob* errors = nullptr;
        HRESULT hr = D3DCompile(
            src.constData(),
            static_cast<SIZE_T>(src.size()),
            "video_d3d11.hlsl",
            nullptr,                // no #defines
            nullptr,                // no #include handler
            entry,
            target,
            compileFlags,
            0,
            outBlob,
            &errors);
        if (FAILED(hr)) {
            const char* msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "(no diagnostic)";
            qWarning("FrameCanvas: D3DCompile(%s/%s) failed, hr=0x%08lx — %s",
                     entry, target, static_cast<unsigned long>(hr), msg);
            if (errors) errors->Release();
            return false;
        }
        if (errors) errors->Release();    // warnings only
        return true;
    };

    if (!compileStage("vs_main", "vs_5_0", &m_vsBlob)) return false;
    if (!compileStage("ps_main", "ps_5_0", &m_psBlob)) return false;

    HRESULT hr = m_device->CreateVertexShader(
        m_vsBlob->GetBufferPointer(),
        m_vsBlob->GetBufferSize(),
        nullptr,
        &m_vs);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateVertexShader failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    hr = m_device->CreatePixelShader(
        m_psBlob->GetBufferPointer(),
        m_psBlob->GetBufferSize(),
        nullptr,
        &m_ps);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreatePixelShader failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: shaders compiled (vs %lld bytes, ps %lld bytes)",
           static_cast<long long>(m_vsBlob->GetBufferSize()),
           static_cast<long long>(m_psBlob->GetBufferSize()));
    return true;
}

bool FrameCanvas::createVertexBuffer()
{
    if (!m_device || !m_vsBlob) {
        return false;
    }
    if (m_vbuf && m_inputLayout) {
        return true;
    }

    // Fullscreen quad in NDC, triangle strip order: BL, TL, BR, TR.
    // UV's v is flipped (top = 0) because BGRA video frames are stored
    // top-down in source texture memory.
    struct Vertex { float pos[2]; float uv[2]; };
    const Vertex vertices[4] = {
        { { -1.0f, -1.0f }, { 0.0f, 1.0f } },   // bottom-left
        { { -1.0f,  1.0f }, { 0.0f, 0.0f } },   // top-left
        { {  1.0f, -1.0f }, { 1.0f, 1.0f } },   // bottom-right
        { {  1.0f,  1.0f }, { 1.0f, 0.0f } },   // top-right
    };

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(vertices);
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    HRESULT hr = m_device->CreateBuffer(&bd, &initData, &m_vbuf);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateBuffer (vertex) failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_device->CreateInputLayout(
        layoutDesc, ARRAYSIZE(layoutDesc),
        m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(),
        &m_inputLayout);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateInputLayout failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: vertex buffer (%d bytes) + input layout created",
           static_cast<int>(sizeof(vertices)));
    return true;
}

bool FrameCanvas::createStateObjects()
{
    if (!m_device) {
        return false;
    }
    if (m_sampler && m_rasterizer && m_blend) {
        return true;
    }

    // Sampler — linear filter, clamp address. Matches the GLSL frag path.
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD         = 0.0f;
    samplerDesc.MaxLOD         = D3D11_FLOAT32_MAX;

    HRESULT hr = m_device->CreateSamplerState(&samplerDesc, &m_sampler);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateSamplerState failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    // Rasterizer — no cull so triangle-strip winding doesn't matter.
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode              = D3D11_FILL_SOLID;
    rastDesc.CullMode              = D3D11_CULL_NONE;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable       = TRUE;

    hr = m_device->CreateRasterizerState(&rastDesc, &m_rasterizer);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateRasterizerState failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    // Blend — disabled. Quad fills the back buffer opaquely.
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable                 = FALSE;
    blendDesc.IndependentBlendEnable                = FALSE;
    blendDesc.RenderTarget[0].BlendEnable           = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_device->CreateBlendState(&blendDesc, &m_blend);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateBlendState failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: state objects created (sampler + rasterizer + blend)");
    return true;
}

bool FrameCanvas::createColorBuffer()
{
    if (!m_device) {
        return false;
    }
    if (m_colorBuffer) {
        return true;
    }

    static_assert(sizeof(ColorParams) == 32, "ColorParams must be 32 bytes (std140-aligned)");

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = sizeof(ColorParams);
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&bd, nullptr, &m_colorBuffer);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateBuffer (color) failed, hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: color buffer (%d bytes, dynamic) created", static_cast<int>(sizeof(ColorParams)));
    return true;
}

bool FrameCanvas::processPendingImport()
{
    if (m_pendingD3DHandle == 0 || m_d3dActive || !m_device) {
        return false;
    }

    ID3D11Device1* dev1 = nullptr;
    HRESULT hr = m_device->QueryInterface(__uuidof(ID3D11Device1),
                                           reinterpret_cast<void**>(&dev1));
    if (FAILED(hr) || !dev1) {
        // FL_11_0 device — no Device1, no shared NT handle support. Clear
        // pending so we don't retry every frame; SHM path takes over.
        qWarning("FrameCanvas: ID3D11Device1 unavailable, hr=0x%08lx — falling back to SHM",
                 static_cast<unsigned long>(hr));
        m_pendingD3DHandle = 0;
        return false;
    }

    ID3D11Texture2D* tex = nullptr;
    hr = dev1->OpenSharedResource1(
        reinterpret_cast<HANDLE>(m_pendingD3DHandle),
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&tex));
    dev1->Release();

    if (FAILED(hr) || !tex) {
        qWarning("FrameCanvas: OpenSharedResource1 failed, hr=0x%08lx — falling back to SHM",
                 static_cast<unsigned long>(hr));
        m_pendingD3DHandle = 0;
        return false;
    }

    // Build an SRV. Format inferred from the imported texture's actual desc
    // so we don't have to assume BGRA8 (sidecar may eventually publish 10-bit).
    D3D11_TEXTURE2D_DESC td = {};
    tex->GetDesc(&td);

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format                    = td.Format;
    sd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels       = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = m_device->CreateShaderResourceView(tex, &sd, &srv);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateShaderResourceView (imported) failed, hr=0x%08lx",
                 static_cast<unsigned long>(hr));
        tex->Release();
        m_pendingD3DHandle = 0;
        return false;
    }

    // Replace any previous import (defensive — should be null after detach).
    if (m_importedSrv)    m_importedSrv->Release();
    if (m_importedD3DTex) m_importedD3DTex->Release();
    m_importedD3DTex = tex;
    m_importedSrv    = srv;

    // Drive the aspect viewport from imported dims.
    m_frameW = m_pendingD3DWidth;
    m_frameH = m_pendingD3DHeight;

    m_d3dActive        = true;
    m_pendingD3DHandle = 0;   // one-shot: clear so we don't retry every frame

    qDebug("FrameCanvas: D3D11 shared texture imported %dx%d (DXGI fmt 0x%x) — zero-copy active",
           m_pendingD3DWidth, m_pendingD3DHeight, static_cast<unsigned>(td.Format));
    emit zeroCopyActivated(true);
    return true;
}

bool FrameCanvas::consumeShmFrame()
{
    if (!m_polling || !m_reader || !m_reader->isAttached()
        || !m_device || !m_context) {
        return false;
    }

    // Batch 2.1 — prefer clock-aware frame selection (OBS
    // ready_async_frame / get_closest_frame semantic adapted to our SHM
    // ring). readBestForClock walks all slots, filters to
    // fid > watermark AND pts ≤ clock + tolerance, and picks the newest
    // of the eligible — i.e., the freshest frame that isn't "from the
    // future" relative to sidecar's audio clock. Clock comes from the
    // sidecar via SHM header (readClockUs), NOT from our SyncClock — the
    // sidecar owns audio playback today; SyncClock is Phase-4 shadow.
    //
    // Fallback: on startup and immediately post-seek, audio_pts may not
    // have advanced past any frame's PTS yet (all frames look like
    // "future" to an uninitialized clock). In that window,
    // readBestForClock returns invalid; we fall back to readLatest so
    // the display always shows SOMETHING while the clock catches up.
    // m_lastFallbackUsed records this for CSV telemetry.
    const int64_t clockUs = m_reader->readClockUs();
    auto f = m_reader->readBestForClock(clockUs, 8000);
    m_lastFallbackUsed = false;
    if (!f.valid) {
        f = m_reader->readLatest();
        m_lastFallbackUsed = true;
    }
    if (!f.valid) {
        return false;
    }
    m_lastChosenFrameId = f.frameId;

    // Batch 2.2 — overflow-drop telemetry.
    // Producer drops: count of frameIds the sidecar wrote to the ring that
    // we never saw. Inferred from the gap between this tick's consumed id
    // and the previous consumed id; (N - M - 1) ≥ 0. On the very first
    // consume (m_previousConsumedFrameId == 0), we skip the subtraction —
    // the sidecar may legitimately start emitting at id=1 with no gap.
    if (m_previousConsumedFrameId > 0 && f.frameId > m_previousConsumedFrameId) {
        const quint64 gap = f.frameId - m_previousConsumedFrameId;
        m_lastProducerDrops = static_cast<quint32>(gap - 1);
    } else {
        m_lastProducerDrops = 0;
    }
    m_previousConsumedFrameId = f.frameId;

    // Consumer late: how far behind sidecar's audio clock the displayed
    // frame's PTS is. Positive = stale display (we're behind); slightly
    // negative = ahead of clock within the readBestForClock 8ms
    // tolerance (expected; frame is freshly ready). Note: on the fallback
    // path (readLatest) we may have consumed a frame with pts well past
    // clockUs — that's fine at startup, the metric will show a negative
    // late value until the clock catches up, which is informative.
    m_lastConsumerLateMs = (clockUs - f.ptsUs) / 1000.0;

    // Recreate texture + SRV on size change (or first frame).
    if (m_videoTexture == nullptr || f.width != m_videoTexW || f.height != m_videoTexH) {
        if (m_videoSrv)     { m_videoSrv->Release();     m_videoSrv     = nullptr; }
        if (m_videoTexture) { m_videoTexture->Release(); m_videoTexture = nullptr; }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = static_cast<UINT>(f.width);
        td.Height           = static_cast<UINT>(f.height);
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&td, nullptr, &m_videoTexture);
        if (FAILED(hr)) {
            qWarning("FrameCanvas: video CreateTexture2D %dx%d failed, hr=0x%08lx",
                     f.width, f.height, static_cast<unsigned long>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format                    = td.Format;
        sd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MostDetailedMip = 0;
        sd.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_videoTexture, &sd, &m_videoSrv);
        if (FAILED(hr)) {
            qWarning("FrameCanvas: video CreateShaderResourceView failed, hr=0x%08lx",
                     static_cast<unsigned long>(hr));
            m_videoTexture->Release();
            m_videoTexture = nullptr;
            return false;
        }

        m_videoTexW = f.width;
        m_videoTexH = f.height;
        // Drive the aspect-ratio viewport from real frame dims.
        m_frameW    = f.width;
        m_frameH    = f.height;
        qDebug("FrameCanvas: video texture (re)allocated %dx%d (BGRA8, default)",
               f.width, f.height);
    }

    // Upload BGRA pixels — UpdateSubresource handles the row-pitch.
    m_context->UpdateSubresource(m_videoTexture, 0, nullptr,
                                  f.pixels, static_cast<UINT>(f.stride), 0);

    // Phase 4.3 — consumer FID writeback. Tells the sidecar this slot is
    // displayed so it can be recycled. Mirrors FrameCanvas.cpp:294.
    m_reader->writeConsumerFid(f.frameId);
    return true;
}

void FrameCanvas::uploadColorParams()
{
    if (!m_context || !m_colorBuffer) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_context->Map(m_colorBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        // No qWarning — runs every frame; failure here would spam the log.
        return;
    }
    memcpy(mapped.pData, &m_colorParams, sizeof(ColorParams));
    m_context->Unmap(m_colorBuffer, 0);
}

#endif // _WIN32

// Phase 4 — SHM consumer API. Mirror of FrameCanvas.cpp:27-38 (semantics
// identical; our member set is just slightly different). Lives outside the
// _WIN32 guard so non-Windows builds link cleanly even though the actual
// frame upload (consumeShmFrame, gated inside _WIN32) is a no-op there.
void FrameCanvas::resetLagAccounting()
{
    // Invalidate the interval timer so the next tick computes 0 latency
    // (first-sample pattern). Also clear the sustained-lag counter +
    // any pending skip. Called by VideoPlayer around seek dispatch —
    // the wall-clock gap between the last pre-seek Present and the
    // first post-seek Present is normal, not "lag", and shouldn't
    // arm the skip-next-Present guard.
    m_intervalTimer.invalidate();
    m_lagTickCount    = 0;
    m_skipNextPresent = false;
}

QImage FrameCanvas::captureCurrentFrame()
{
    // VIDEO_PLAYER_FIX Batch 3.2 — two paths: D3D11 staging readback when
    // zero-copy is active (sidecar publishes directly to shared texture,
    // SHM ring is empty/stale), SHM direct copy otherwise.

    if (m_d3dActive && m_importedD3DTex && m_device && m_context) {
        // Query source desc so the staging texture matches exactly.
        D3D11_TEXTURE2D_DESC srcDesc = {};
        m_importedD3DTex->GetDesc(&srcDesc);

        // Only handle the formats the sidecar actually publishes today
        // (BGRA8). If a future sidecar publishes 10-bit or HDR, falls
        // through to SHM (which also won't have it — caller sees null).
        if (srcDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM
            && srcDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
            qWarning("FrameCanvas::captureCurrentFrame: unsupported imported format 0x%x",
                     static_cast<unsigned>(srcDesc.Format));
            return {};
        }

        D3D11_TEXTURE2D_DESC stageDesc = srcDesc;
        stageDesc.Usage          = D3D11_USAGE_STAGING;
        stageDesc.BindFlags      = 0;
        stageDesc.MiscFlags      = 0;
        stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        // Staging textures can't have the shared-nt-handle misc flag; we
        // already stripped MiscFlags above.

        ID3D11Texture2D* staging = nullptr;
        HRESULT hr = m_device->CreateTexture2D(&stageDesc, nullptr, &staging);
        if (FAILED(hr) || !staging) {
            qWarning("FrameCanvas::captureCurrentFrame: staging CreateTexture2D failed 0x%08lx",
                     static_cast<unsigned long>(hr));
            if (staging) staging->Release();
            return {};
        }

        // GPU copy from imported texture to staging. Sidecar has already
        // flushed its writes to the shared texture (it does Present-side
        // cross-process synchronization before publishing the handle);
        // our CopyResource here picks up the latest committed state.
        m_context->CopyResource(staging, m_importedD3DTex);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            qWarning("FrameCanvas::captureCurrentFrame: staging Map failed 0x%08lx",
                     static_cast<unsigned long>(hr));
            staging->Release();
            return {};
        }

        // Build a deep-copied QImage. Format_ARGB32 byte order on LE is
        // B,G,R,A, which matches DXGI_FORMAT_B8G8R8A8_UNORM. The source
        // RowPitch may exceed width*4 (alignment), so copy row-by-row
        // into a tight QImage buffer rather than reinterpret the mapped
        // region directly (avoids either carrying padding into the PNG
        // or aliasing mapped memory after Unmap).
        QImage out(srcDesc.Width, srcDesc.Height, QImage::Format_ARGB32);
        const uint8_t* srcRows = static_cast<const uint8_t*>(mapped.pData);
        const int       bytesPerRow = static_cast<int>(srcDesc.Width) * 4;
        for (UINT y = 0; y < srcDesc.Height; ++y) {
            std::memcpy(out.scanLine(static_cast<int>(y)),
                        srcRows + static_cast<size_t>(y) * mapped.RowPitch,
                        static_cast<size_t>(bytesPerRow));
        }

        m_context->Unmap(staging, 0);
        staging->Release();
        return out;
    }

    // SHM path — producer-written BGRA ring.
    if (!m_reader || !m_reader->isAttached())
        return {};
    auto f = m_reader->readLatest();
    if (!f.valid || f.pixels == nullptr || f.width <= 0 || f.height <= 0)
        return {};
    // Qt's Format_ARGB32 has byte order B,G,R,A on little-endian, which
    // matches the sidecar's BGRA layout. Copy the bytes so the returned
    // QImage doesn't alias the ring buffer (which the producer overwrites
    // on the next frame).
    const QImage view(f.pixels, f.width, f.height, f.stride, QImage::Format_ARGB32);
    return view.copy();
}

void FrameCanvas::attachShm(ShmFrameReader* reader) { m_reader = reader; }

void FrameCanvas::detachShm()
{
    m_polling = false;
    m_reader  = nullptr;
#ifdef _WIN32
    // Keep m_videoTexture alive (released in destructor or recreated on next
    // attach). Just stop binding it.
    m_videoTexW = m_videoTexH = 0;
#endif
}

void FrameCanvas::startPolling() { if (m_reader) m_polling = true; }
void FrameCanvas::stopPolling()  { m_polling = false; }

// Phase 5 — Holy Grail attach/detach. Lives outside the _WIN32 guard for
// link symmetry on non-Windows builds; the actual import work runs on the
// GPU thread inside processPendingImport (which is inside the _WIN32 block).
void FrameCanvas::attachD3D11Texture(quintptr ntHandle, int width, int height)
{
    // Stash the handle; actual import happens in processPendingImport()
    // because OpenSharedResource1 must run on the GPU thread that owns
    // m_device. attachD3D11Texture can be called from any thread.
    m_pendingD3DHandle = ntHandle;
    m_pendingD3DWidth  = width;
    m_pendingD3DHeight = height;
    m_d3dActive        = false;  // force re-import on next render
    update();                    // poke the paint cycle
}

void FrameCanvas::detachD3D11Texture()
{
    const bool wasActive = m_d3dActive;
#ifdef _WIN32
    if (m_importedSrv)    { m_importedSrv->Release();    m_importedSrv    = nullptr; }
    if (m_importedD3DTex) { m_importedD3DTex->Release(); m_importedD3DTex = nullptr; }
#endif
    m_pendingD3DHandle = 0;
    m_d3dActive        = false;
    if (wasActive) {
        emit zeroCopyActivated(false);   // sidecar should re-engage CPU pipeline
    }
}

// Phase 6 — vsync logging public API. Lives outside _WIN32 so non-Win builds
// link cleanly. Verbatim mirror of FrameCanvas::setVsyncLogging behavior.
void FrameCanvas::setVsyncLogging(bool enabled, const QString& dumpPath)
{
    if (m_vsyncLoggingOn == enabled) return;
    m_vsyncLoggingOn = enabled;
    m_vsyncDumpPath  = dumpPath;
    if (!enabled) {
        m_vsyncLogger.dumpToCsv(dumpPath);
    }
}

// Phase 7 bake-in fix — aspect ratio override. Lives outside _WIN32 for
// non-Win link symmetry. Read by drawTexturedQuad's viewport math.
void FrameCanvas::setForcedAspectRatio(double aspect)
{
    m_forcedAspect = aspect;
}

void FrameCanvas::setSyncClock(SyncClock* clock)
{
    m_syncClock = clock;
}

void FrameCanvas::setColorParams(float brightness, float contrast,
                                 float saturation, float gamma)
{
    m_colorParams.brightness = brightness;
    m_colorParams.contrast   = contrast;
    m_colorParams.saturation = saturation;
    m_colorParams.gamma      = gamma;
    // Upload happens next frame in drawTexturedQuad → uploadColorParams.
    // (On non-Windows builds this just stores the values; render path is
    // a no-op since the rest of FrameCanvas is _WIN32-gated.)
}

void FrameCanvas::setTonemapMode(int mode)
{
    // Batch 3.4 — clamp out-of-range values to Off. Any future operator we
    // add (EETF, custom LUT, …) gets a new int value; unknown values stay
    // safe. Upload happens next frame in drawTexturedQuad → uploadColorParams.
    if (mode < 0 || mode > 3) mode = 0;
    m_colorParams.tonemapMode = mode;
}

void FrameCanvas::setHdrColorInfo(int colorPrimaries, int colorTrc)
{
    // Batch 3.1 (Player Polish Phase 3) — translate raw FFmpeg AVCOL_* enum
    // values reported by the sidecar's demuxer probe into our shader's
    // colorSpace + transferFunc cbuffer enum. Values cited directly match
    // libavutil/pixfmt.h and are ABI-stable; no libavutil include needed
    // at this layer (keeps the main-app free of FFmpeg headers).
    //
    // Mapping (Batch 3.1 scope):
    //   color_primaries = AVCOL_PRI_BT2020  (9) → colorSpace = 1 (BT.2020)
    //   anything else                            → colorSpace = 0 (BT.709)
    //
    // transferFunc is plumbed here now (Batch 3.2 PQ / 3.3 HLG will light
    // up the shader side); today values ≠ 0 just sit in the cbuffer
    // without a shader branch that reads them. Translating now avoids a
    // second call-site touch in 3.2/3.3.
    //
    //   color_trc = AVCOL_TRC_SMPTE2084   (16) → transferFunc = 1 (PQ, HDR10)
    //   color_trc = AVCOL_TRC_ARIB_STD_B67 (18) → transferFunc = 2 (HLG)
    //   anything else                            → transferFunc = 0 (sRGB/gamma)
    constexpr int kAvColPriBT2020    = 9;
    constexpr int kAvColTrcSmpte2084 = 16;
    constexpr int kAvColTrcAribB67   = 18;

    m_colorParams.colorSpace   = (colorPrimaries == kAvColPriBT2020) ? 1 : 0;
    if      (colorTrc == kAvColTrcSmpte2084) m_colorParams.transferFunc = 1;
    else if (colorTrc == kAvColTrcAribB67)   m_colorParams.transferFunc = 2;
    else                                     m_colorParams.transferFunc = 0;
}
