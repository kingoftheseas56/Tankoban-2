#include "FrameCanvas.h"
#include "ShmFrameReader.h"
#include "OverlayShmReader.h"
#include "SyncClock.h"

#include <QImage>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
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
    m_canvasSizeDebounce.setSingleShot(true);
    m_canvasSizeDebounce.setInterval(75);
    connect(&m_canvasSizeDebounce, &QTimer::timeout, this, [this]() {
        const QSize px = canvasPixelSize();
        if (px == m_lastCanvasPixelSize) return;
        m_lastCanvasPixelSize = px;
        emit canvasPixelSizeSettled(px.width(), px.height());
    });
}

FrameCanvas::~FrameCanvas()
{
#ifdef _WIN32
    m_renderTimer.stop();
    tearDownD3D();
#endif
    // Delete the overlay reader after tearDownD3D so it outlives any
    // in-flight uploads. Cleaned up in ~OverlayShmReader.
    if (m_overlayReader) {
        delete m_overlayReader;
        m_overlayReader = nullptr;
    }
}

void FrameCanvas::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

#ifdef _WIN32
    if (m_device) {
        // PLAYER_PERF_FIX Phase 1 Batch 1.3 — only restart the QTimer
        // fallback if we don't have the waitable driving the loop. Waitable
        // thread stays running across hide/show; no re-start needed.
        if (!m_waitableHandle && !m_renderTimer.isActive()) {
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
    // PLAYER_PERF_FIX Phase 1 Batch 1.2 — DXGI 1.3 waitable swap chain.
    // The flag itself is inert at creation time; Batch 1.3 calls
    // IDXGISwapChain2::GetFrameLatencyWaitableObject() and replaces the
    // QTimer(16) wake loop with WaitForSingleObject on the returned handle.
    // Microsoft doc: https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains
    // Empirical justification: [PERF] log Batch 1.1 showed timer_interval
    // p99 = 45-75 ms against a 16.67 ms vsync budget — Qt wall-clock
    // timer jitter. Waitable swap chain replaces that with vsync-aligned
    // wake-ups.
    desc.Flags              = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

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

    // PLAYER_PERF_FIX Phase 1 Batch 1.3 — try to acquire the DXGI waitable.
    // If this succeeds, startWaitableLoop drives renderFrame from the
    // DXGI frame-latency signal and m_renderTimer stays dormant. If it
    // fails (older DXGI, driver quirk), fall back to m_renderTimer.start()
    // for the QTimer(16) path (Batch 1.2 rollback).
    startWaitableLoop();
    if (!m_waitableHandle) {
        qWarning("FrameCanvas: DXGI waitable unavailable, falling back to QTimer(16)");
        m_renderTimer.start();
    }
    return true;
}
#endif

void FrameCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_canvasSizeDebounce.start();

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

struct AspectFitRect {
    int x = 0;
    int y = 0;
    int w = 1;
    int h = 1;
};

AspectFitRect fitAspectRect(int canvasW, int canvasH, double frameAspect)
{
    canvasW = qMax(1, canvasW);
    canvasH = qMax(1, canvasH);
    if (frameAspect <= 0.0) {
        frameAspect = static_cast<double>(canvasW) / static_cast<double>(canvasH);
    }

    AspectFitRect r;
    const double canvasAspect = static_cast<double>(canvasW) / static_cast<double>(canvasH);
    if (frameAspect > canvasAspect) {
        r.w = canvasW;
        r.h = std::clamp(static_cast<int>(std::lround(canvasW / frameAspect)), 1, canvasH);
        int spare = canvasH - r.h;
        if ((spare & 1) != 0 && r.h > 1) {
            --r.h;
            spare = canvasH - r.h;
        }
        r.x = 0;
        r.y = spare / 2;
    } else {
        r.h = canvasH;
        r.w = std::clamp(static_cast<int>(std::lround(canvasH * frameAspect)), 1, canvasW);
        int spare = canvasW - r.w;
        if ((spare & 1) != 0 && r.w > 1) {
            --r.w;
            spare = canvasW - r.w;
        }
        r.x = spare / 2;
        r.y = 0;
    }
    return r;
}
}

void FrameCanvas::tearDownD3D()
{
    // PLAYER_PERF_FIX Phase 1 Batch 1.3 — stop the waitable loop BEFORE
    // releasing the swap chain. stopWaitableLoop closes the handle, wakes
    // the WAIT, joins the thread. Guarantees no dangling thread reads of
    // m_waitableHandle or pending renderFrame queued invocations after
    // m_swapChain goes away.
    stopWaitableLoop();

    // Release in reverse creation order so dependent objects don't dangle
    // mid-teardown. Idempotent: safe to call on partially-initialized or
    // already-torn-down state (every pointer is null-checked).
    if (m_importedSrv)    { m_importedSrv->Release();    m_importedSrv    = nullptr; }
    if (m_importedD3DTex) { m_importedD3DTex->Release(); m_importedD3DTex = nullptr; }
    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay GPU resources.
    // m_overlayReader / m_overlayTexW/H stay so attachOverlayShm can
    // reattach cleanly after device-lost recovery.
    if (m_overlaySrv)     { m_overlaySrv->Release();     m_overlaySrv     = nullptr; }
    if (m_overlayTex)     { m_overlayTex->Release();     m_overlayTex     = nullptr; }
    if (m_overlayPs)      { m_overlayPs->Release();      m_overlayPs      = nullptr; }
    if (m_overlayPsBlob)  { m_overlayPsBlob->Release();  m_overlayPsBlob  = nullptr; }
    if (m_overlayBlend)   { m_overlayBlend->Release();   m_overlayBlend   = nullptr; }
    m_overlayLastCounter = 0;
    m_overlayCurrentlyVisible = false;
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

// PLAYER_PERF_FIX Phase 1 Batch 1.3 — waitable-swap-chain cadence.
// Replaces QTimer(16) wall-clock wake with DXGI's FRAME_LATENCY_WAITABLE
// vsync signal. QueryInterface IDXGISwapChain2 for the frame-latency API,
// pin the in-flight frame count to 1 (video-playback standard), grab the
// waitable handle, and spawn a lightweight thread that just waits on it.
// On each signal the thread posts a Qt::QueuedConnection invocation of
// renderFrame() back to the main thread — preserving the single-threaded
// D3D11-access topology we already have, only the tick driver changes.
void FrameCanvas::startWaitableLoop()
{
    if (!m_swapChain) return;

    IDXGISwapChain2* swapChain2 = nullptr;
    HRESULT hr = m_swapChain->QueryInterface(__uuidof(IDXGISwapChain2),
                                              reinterpret_cast<void**>(&swapChain2));
    if (FAILED(hr) || !swapChain2) {
        qWarning("FrameCanvas: IDXGISwapChain2 unavailable, hr=0x%08lx — waitable disabled",
                 static_cast<unsigned long>(hr));
        return;
    }

    // Video-playback standard: MaximumFrameLatency = 1. DXGI's default is 3,
    // which introduces multi-frame queueing latency that is undesirable for
    // a media player.
    hr = swapChain2->SetMaximumFrameLatency(1);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: SetMaximumFrameLatency(1) failed, hr=0x%08lx",
                 static_cast<unsigned long>(hr));
        // Non-fatal — we can still use the waitable at the default latency.
    }

    HANDLE raw = swapChain2->GetFrameLatencyWaitableObject();
    swapChain2->Release();

    if (!raw) {
        qWarning("FrameCanvas: GetFrameLatencyWaitableObject returned null");
        return;
    }

    m_waitableHandle = raw;
    m_waitableStop.store(false);
    m_waitableThread = std::thread(&FrameCanvas::waitableLoop, this);
    qDebug("FrameCanvas: DXGI waitable render loop started");
}

void FrameCanvas::stopWaitableLoop()
{
    if (!m_waitableHandle && !m_waitableThread.joinable()) return;

    m_waitableStop.store(true);

    // Closing the handle wakes WaitForSingleObjectEx with WAIT_FAILED on
    // older kernels or simply unblocks on a timeout (we use 100ms as the
    // worst-case wait). Join is bounded by that 100ms in the worst case.
    HANDLE h = static_cast<HANDLE>(m_waitableHandle);
    m_waitableHandle = nullptr;
    if (h) CloseHandle(h);

    if (m_waitableThread.joinable()) {
        m_waitableThread.join();
    }
}

void FrameCanvas::waitableLoop()
{
    // Runs on a dedicated std::thread. The ONLY work done here is wait on
    // the DXGI frame-latency handle and post a QueuedConnection call back
    // to the Qt main thread for the actual renderFrame. D3D11 device +
    // context are never touched from this thread.
    while (!m_waitableStop.load(std::memory_order_relaxed)) {
        HANDLE h = static_cast<HANDLE>(m_waitableHandle);
        if (!h) break;
        DWORD r = WaitForSingleObjectEx(h, 100, FALSE);
        if (m_waitableStop.load(std::memory_order_relaxed)) break;
        // On WAIT_OBJECT_0 (signaled) OR WAIT_TIMEOUT (keepalive), post a
        // renderFrame invocation. Timeout path keeps the Qt event loop
        // getting frames even if DXGI stops signaling (e.g. window hidden).
        // Failed wait (handle closed) exits the loop.
        if (r != WAIT_OBJECT_0 && r != WAIT_TIMEOUT) break;
        // Use lambda-style invokeMethod — renderFrame is a private helper,
        // not a Q_INVOKABLE slot, so the string-name overload can't see
        // it. Qt's queued-connection metadata works fine with a lambda
        // that captures the member-function call.
        QMetaObject::invokeMethod(this, [this]() { renderFrame(); },
                                   Qt::QueuedConnection);
    }
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

    // PLAYER_PERF_FIX Batch 1.1 — capture time between consecutive
    // renderFrame invocations. Measures actual wake cadence vs the
    // QTimer(16) request, to prove whether Qt's wall-clock timer is
    // introducing jitter (Agent 7 audit P0-2). Seed on the first call;
    // push the interval on subsequent calls.
    double perfIntervalMs = 0.0;
    if (m_perfInvocationTimer.isValid()) {
        perfIntervalMs = m_perfInvocationTimer.nsecsElapsed() / 1.0e6;
    }
    m_perfInvocationTimer.restart();

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

    // PLAYER_PERF_FIX Batch 1.1 — time the clear + draw phase. Bracketed
    // around Clear through drawTexturedQuad (the GPU submission work),
    // separate from Present (which is where driver/vsync waits land).
    QElapsedTimer perfDrawTimer;
    perfDrawTimer.start();

    m_context->ClearRenderTargetView(m_rtv, black);

    drawTexturedQuad();

    const double perfDrawMs = perfDrawTimer.nsecsElapsed() / 1.0e6;

    // PLAYER_PERF_FIX Batch 1.1 — time Present separately. On fixed-QTimer
    // + non-waitable swap chain (current state), Present(1, 0) is where
    // Windows throttles to vsync — its p99 is the cadence-jitter signal.
    QElapsedTimer perfPresentTimer;
    perfPresentTimer.start();

    HRESULT hr = m_swapChain->Present(1, 0);

    const double perfPresentMs = perfPresentTimer.nsecsElapsed() / 1.0e6;

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

    // PLAYER_PERF_FIX Batch 1.1 — always-on 1 Hz [PERF] diagnostic. Lives
    // outside the opt-in m_vsyncLoggingOn gate so Hemanth captures
    // cadence evidence without flipping flags. Skips the first
    // invocation's interval (perfIntervalMs==0 — seed sample).
    if (perfIntervalMs > 0.0) {
        m_perfTimerIntervalMs.push_back(perfIntervalMs);
    }
    m_perfDrawMs.push_back(perfDrawMs);
    m_perfPresentMs.push_back(perfPresentMs);

    if (!m_perfWindowTimer.isValid()) {
        m_perfWindowTimer.start();
        m_perfSkippedPresentsBase = m_framesSkippedTotal;
        m_perfDxgiBaseValid = false;
    }

    if (m_perfWindowTimer.elapsed() >= 1000 && !m_perfPresentMs.empty()) {
        auto pct = [](std::vector<double>& v, double p) -> double {
            if (v.empty()) return 0.0;
            size_t idx = static_cast<size_t>(p * (v.size() - 1));
            std::nth_element(v.begin(), v.begin() + idx, v.end());
            return v[idx];
        };
        const double tiP50 = pct(m_perfTimerIntervalMs, 0.50);
        const double tiP99 = pct(m_perfTimerIntervalMs, 0.99);
        const double drP50 = pct(m_perfDrawMs, 0.50);
        const double drP99 = pct(m_perfDrawMs, 0.99);
        const double prP50 = pct(m_perfPresentMs, 0.50);
        const double prP99 = pct(m_perfPresentMs, 0.99);
        const quint64 skippedDelta = m_framesSkippedTotal - m_perfSkippedPresentsBase;

        // DXGI frame statistics — delta since last flush. Tells us
        // presents_queued (PresentCount - PresentRefreshCount == backlog)
        // and vsync cadence (SyncQPCTime delta / PresentRefreshCount delta).
        // May fail on some Windows configurations; log zeros in that case.
        unsigned int dxgiPresentsQueued = 0;
        double dxgiVsyncIntervalUs = 0.0;
        long long dxgiSyncQpc = 0;
#ifdef _WIN32
        DXGI_FRAME_STATISTICS stats = {};
        if (m_swapChain && SUCCEEDED(m_swapChain->GetFrameStatistics(&stats))) {
            dxgiSyncQpc = stats.SyncQPCTime.QuadPart;
            if (m_perfDxgiBaseValid
                && stats.PresentRefreshCount > m_perfDxgiPresentRefreshBase) {
                const unsigned int refreshDelta =
                    stats.PresentRefreshCount - m_perfDxgiPresentRefreshBase;
                const long long qpcDelta = dxgiSyncQpc - m_perfDxgiSyncQpcBase;
                LARGE_INTEGER qpcFreq;
                if (QueryPerformanceFrequency(&qpcFreq) && qpcFreq.QuadPart > 0
                    && refreshDelta > 0) {
                    dxgiVsyncIntervalUs =
                        (static_cast<double>(qpcDelta) * 1.0e6 /
                         static_cast<double>(qpcFreq.QuadPart)) /
                        static_cast<double>(refreshDelta);
                }
                dxgiPresentsQueued =
                    (stats.PresentCount > stats.PresentRefreshCount)
                        ? (stats.PresentCount - stats.PresentRefreshCount)
                        : 0;
            }
            m_perfDxgiPresentCountBase   = stats.PresentCount;
            m_perfDxgiPresentRefreshBase = stats.PresentRefreshCount;
            m_perfDxgiSyncQpcBase        = dxgiSyncQpc;
            m_perfDxgiBaseValid          = true;
        }
#endif

        // Write directly to _player_debug.txt (qDebug doesn't land there —
        // same pattern the aspect diagnostic at line 789 uses).
        QFile dbg("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
        if (dbg.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream s(&dbg);
            s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
              << QString::asprintf(
                  " [PERF] frames=%zu timer_interval p50/p99=%.2f/%.2f ms "
                  "draw p50/p99=%.2f/%.2f ms present p50/p99=%.2f/%.2f ms "
                  "skipped=%llu [DXGI] queued=%u vsync_us=%.1f\n",
                  m_perfPresentMs.size(),
                  tiP50, tiP99, drP50, drP99, prP50, prP99,
                  static_cast<unsigned long long>(skippedDelta),
                  dxgiPresentsQueued, dxgiVsyncIntervalUs);
        }

        m_perfTimerIntervalMs.clear();
        m_perfDrawMs.clear();
        m_perfPresentMs.clear();
        m_perfSkippedPresentsBase = m_framesSkippedTotal;
        m_perfWindowTimer.restart();
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
    bool shmFresh = false;
    if (m_d3dActive && m_importedSrv) {
        activeSrv = m_importedSrv;
    } else {
        // SHM fallback path. consumeShmFrame is gated on m_polling so this
        // is a no-op until VideoPlayer attaches a reader. When nothing's
        // attached (loading screen, between videos), activeSrv stays null
        // and the early-return below skips the draw — black clear shows.
        shmFresh = consumeShmFrame();
        if (m_videoSrv && (shmFresh || m_polling)) {
            activeSrv = m_videoSrv;
        }
    }
    if (!activeSrv) {
        return;
    }

    // STREAM_AUTOCROP — scan on FRESH frames only. Render runs at display
    // cadence (60 Hz) but video is 24 fps, so 60% of ticks read stale
    // m_videoTexture data from a previous frame (possibly a prior scene
    // with different baked aspect). Reading stale data caused detection
    // to lock onto an earlier scene's crop that doesn't match what's
    // currently rendering — which is exactly the "black bar still
    // visible" symptom Hemanth reports on variable-aspect Netflix
    // content. Gating on shmFresh ensures each scan sees the actual
    // CURRENT frame being displayed. Cost: ~1 ms per scan × 24 fps =
    // ~2.4% CPU overhead. Starts at frame 120 (≈2 s in) to skip
    // cold-open fade-to-black frames.
    if (m_frameW > 0 && m_frameH > 0) {
        ++m_framesSinceImport;
        if (m_framesSinceImport >= 120 && (shmFresh || m_d3dActive)) {
            scanBakedLetterbox();
        }
    }

    // Aspect-ratio viewport — port of FrameCanvas::render():298-315.
    // Letterboxes the source-aspect quad inside the back buffer so squares
    // stay square. Black bars (or, for now, the magenta clear color) fill
    // the unused space on the short axis.
    // Viewport is in render-target pixels (= physical), so scale by DPR to
    // match the physical-pixel swap chain.
    const qreal dpr = devicePixelRatioF();
    const int canvasW = qMax(1, qRound(width()  * dpr));
    const int canvasH = qMax(1, qRound(height() * dpr));
    const double widgetAspect = static_cast<double>(canvasW) / static_cast<double>(canvasH);

    // Effective content dims after baked-letterbox crop (auto-detected in
    // scanBakedLetterbox on frame 30+). For clean sources all four crops
    // are 0 and effFrameW/H == m_frameW/m_frameH (no behavior change).
    // For Netflix 1920x1080 encodes with 66+0 asymmetric baked bars,
    // effFrameH = 1014 and frameAspect reflects the real content aspect
    // (1.894) rather than the container aspect (1.778).
    const int effFrameW = qMax(1, m_frameW - m_srcCropLeft - m_srcCropRight);
    const int effFrameH = qMax(1, m_frameH - m_srcCropTop  - m_srcCropBottom);

    const double frameAspect  = (m_forcedAspect > 0.0)
        ? m_forcedAspect
        : ((m_frameW > 0 && m_frameH > 0)
            ? static_cast<double>(effFrameW) / static_cast<double>(effFrameH)
            : widgetAspect);
    const AspectFitRect videoRect = fitAspectRect(canvasW, canvasH, frameAspect);

    // Crop zoom: when the user picks "Crop → 2.35:1" (etc.) on content
    // that has encoder-baked letterbox/pillarbox pixels, enlarge the
    // video viewport uniformly so only the crop-target aspect content
    // fills the original videoRect — the baked bars get pushed past
    // the render-target bounds and D3D11 clips them.
    //
    // Zoom factor = ratio of the wider aspect to the narrower. That's
    // the scale at which the source's content-aspect area fills the
    // video rect's matching dimension. Uniform X+Y zoom preserves the
    // content's own aspect (no vertical stretch / pixel squish); the
    // trade-off is some edge content on the opposite axis gets cropped.
    double cropZoom = 1.0;
    if (m_cropAspect > 0.0 && frameAspect > 0.0 && m_cropAspect != frameAspect) {
        cropZoom = (m_cropAspect > frameAspect)
            ? m_cropAspect / frameAspect
            : frameAspect / m_cropAspect;
    }
    const int croppedW = static_cast<int>(std::lround(videoRect.w * cropZoom));
    const int croppedH = static_cast<int>(std::lround(videoRect.h * cropZoom));

    // Baked-letterbox source crop — asymmetric viewport math. The quad
    // samples the FULL source texture (UV 0..1), so to make only the
    // CONTENT region (effFrameW x effFrameH) fill croppedW x croppedH,
    // we scale the viewport up by (frameDim / effFrameDim) and shift it
    // by -(cropPx / frameDim * vpDim) so the baked rows fall off-screen
    // via D3D11 scissoring. All src-crop values 0 → scales = 1 and
    // offsets = 0, identical to pre-fix viewport. Asymmetric crops
    // (e.g. 66 top + 0 bottom) produce an asymmetric shift that pushes
    // the baked bar fully off the top edge while the content's vertical
    // center remains aligned with videoRect's vertical center.
    const double srcScaleX = (effFrameW > 0)
        ? (static_cast<double>(m_frameW) / static_cast<double>(effFrameW)) : 1.0;
    const double srcScaleY = (effFrameH > 0)
        ? (static_cast<double>(m_frameH) / static_cast<double>(effFrameH)) : 1.0;
    const double vpW = static_cast<double>(croppedW) * srcScaleX;
    const double vpH = static_cast<double>(croppedH) * srcScaleY;
    const double srcOffsetX = (m_frameW > 0)
        ? (static_cast<double>(m_srcCropLeft) / static_cast<double>(m_frameW)) * vpW : 0.0;
    const double srcOffsetY = (m_frameH > 0)
        ? (static_cast<double>(m_srcCropTop)  / static_cast<double>(m_frameH)) * vpH : 0.0;

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = static_cast<float>(videoRect.x - (croppedW - videoRect.w) / 2 - srcOffsetX);
    vp.TopLeftY = static_cast<float>(videoRect.y - (croppedH - videoRect.h) / 2 - srcOffsetY);
    vp.Width    = static_cast<float>(vpW);
    vp.Height   = static_cast<float>(vpH);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // STREAM_AUTOCROP — scissor clips rasterization to the content area.
    // Without this, baked letterbox rows outside videoRect still render
    // to the canvas when vp == canvas (fullscreen mode). With scissor
    // set to videoRect, the expanded vp that stretches the source past
    // videoRect boundaries gets clipped — baked rows outside videoRect
    // produce no fragments. Canvas clear-color fills the small symmetric
    // letterbox bars that remain. For content with no src crop
    // (m_srcCropTop/Bottom all 0), videoRect == canvas and this scissor
    // is a no-op.
    D3D11_RECT scissor = {};
    scissor.left   = videoRect.x;
    scissor.top    = videoRect.y;
    scissor.right  = videoRect.x + videoRect.w;
    scissor.bottom = videoRect.y + videoRect.h;
    m_context->RSSetScissorRects(1, &scissor);

    // Diag: log scissor once per state change (same gate as aspect log).
    static int s_lastScissorLoggedTop = -1;
    static int s_lastScissorLoggedBottom = -1;
    if (s_lastScissorLoggedTop != scissor.top || s_lastScissorLoggedBottom != scissor.bottom) {
        s_lastScissorLoggedTop = scissor.top;
        s_lastScissorLoggedBottom = scissor.bottom;
        QFile dbg("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
        if (dbg.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream s(&dbg);
            s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
              << " [FrameCanvas scissor] rect={" << scissor.left << ","
              << scissor.top << "," << scissor.right << "," << scissor.bottom
              << "} canvas=" << canvasW << "x" << canvasH << "\n";
        }
    }

    // Aspect diagnostic — prints whenever frame dims OR widget dims change.
    // Widget-dim changes catch fullscreen transitions where the frame stays
    // the same but the viewport target grows/shrinks. Writes directly to
    // _player_debug.txt (not via qDebug, which doesn't land in that file).
    const int widgetWPx = canvasW;
    const int widgetHPx = canvasH;
    // Fire-predicate includes m_forcedAspect so aspect-override menu
    // changes land in the log. Closes Observation G3 from Agent 7's
    // cinemascope audit (2026-04-16): the prior predicate keyed only on
    // frame/widget dims, leaving the override path totally unlogged
    // (221 lines captured, zero with non-zero forced=). Without this,
    // diagnosing "forced 16:9 leaves a stuck top bar" has no empirical
    // record of what videoRect the forced path produced.
    static int s_lastLoggedCropTop = -1;
    static int s_lastLoggedCropBottom = -1;
    if (m_frameW != m_aspectLoggedForFrameW || m_frameH != m_aspectLoggedForFrameH
        || widgetWPx != m_aspectLoggedForWidgetW || widgetHPx != m_aspectLoggedForWidgetH
        || m_forcedAspect != m_aspectLoggedForForced
        || s_lastLoggedCropTop != m_srcCropTop
        || s_lastLoggedCropBottom != m_srcCropBottom) {
        m_aspectLoggedForFrameW  = m_frameW;
        m_aspectLoggedForFrameH  = m_frameH;
        m_aspectLoggedForWidgetW = widgetWPx;
        m_aspectLoggedForWidgetH = widgetHPx;
        m_aspectLoggedForForced  = m_forcedAspect;
        s_lastLoggedCropTop      = m_srcCropTop;
        s_lastLoggedCropBottom   = m_srcCropBottom;
        QFile dbg("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
        if (dbg.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream s(&dbg);
            s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
              << " [FrameCanvas aspect] source=" << m_frameW << "x" << m_frameH
              << " widget=" << canvasW << "x" << canvasH
              << " dpr=" << QString::number(dpr, 'f', 2)
              << " frameAspect=" << QString::number(frameAspect, 'f', 4)
              << " widgetAspect=" << QString::number(widgetAspect, 'f', 4)
              << " videoRect={" << videoRect.x << "," << videoRect.y << ","
              << videoRect.w << "," << videoRect.h << "}"
              << " d3dVp={" << vp.TopLeftX << "," << vp.TopLeftY << ","
              << vp.Width << "," << vp.Height << "}"
              << " forced=" << QString::number(m_forcedAspect, 'f', 4)
              << " srcCrop={" << m_srcCropTop << "," << m_srcCropBottom << ","
              << m_srcCropLeft << "," << m_srcCropRight << "}\n";
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

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay draw pass.
    // After the video quad, poll the overlay SHM for fresh bytes (if the
    // atomic counter advanced), upload to the local overlay texture, and
    // draw an alpha-blended quad at the same viewport. Subtitles render
    // on top of the video without knocking HEVC 10-bit off zero-copy.
    // No cross-process GPU sync — both the overlay BGRA read (from SHM)
    // and the texture live on main-app's own D3D11 device.
    pollOverlayShm();
    if (m_overlayCurrentlyVisible && m_overlaySrv && m_overlayPs && m_overlayBlend) {
        // Overlay SHM is sized to the source video (see sidecar Option A
        // rollback). Draw it at the same letterboxed videoRect as the
        // video quad so sub coordinates map 1:1 onto the video on screen.
        // Previously the overlay used the full canvas viewport while the
        // video used videoRect — for any content whose aspect didn't
        // match the widget (cinemascope, pillarboxed 1080p in a wide
        // window, etc.) the overlay stretched vertically past the video,
        // dropping subtitles into the letterbox bars / clipping at
        // screen edge.
        //
        // m_subtitleLiftPx shifts the whole overlay viewport upward so
        // subtitles clear the HUD when it's visible. The overlay texture
        // is transparent above the sub baseline, so shifting doesn't
        // change what's visible at the top — only moves the sub content
        // upward by the specified number of physical pixels.
        // STREAM_AUTOCROP — overlay uses the same asymmetric viewport as the
        // video quad so sub bitmaps placed at source-coord y (e.g. y=950 of
        // 1080) land on the same physical screen row as the equivalent video
        // content. For clean sources (no src-crop), this reduces to the
        // pre-fix videoRect-based overlay. For Netflix 66+0 asymmetric baked
        // letterbox, the expanded vp + srcOffsetY keeps subs aligned with
        // the video content region even after the baked top rows are
        // pushed off-screen.
        D3D11_VIEWPORT overlayVp = {};
        overlayVp.TopLeftX = static_cast<float>(videoRect.x - (croppedW - videoRect.w) / 2 - srcOffsetX);
        overlayVp.TopLeftY = static_cast<float>(videoRect.y - (croppedH - videoRect.h) / 2 - srcOffsetY - m_subtitleLiftPx);
        overlayVp.Width    = static_cast<float>(vpW);
        overlayVp.Height   = static_cast<float>(vpH);
        overlayVp.MinDepth = 0.0f;
        overlayVp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &overlayVp);
        m_context->OMSetBlendState(m_overlayBlend, blendFactor, 0xFFFFFFFF);
        m_context->PSSetShader(m_overlayPs, nullptr, 0);
        m_context->PSSetShaderResources(0, 1, &m_overlaySrv);
        m_context->Draw(4, 0);
    }
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
    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay pixel shader.
    if (!compileStage("ps_overlay", "ps_5_0", &m_overlayPsBlob)) return false;

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

    hr = m_device->CreatePixelShader(
        m_overlayPsBlob->GetBufferPointer(),
        m_overlayPsBlob->GetBufferSize(),
        nullptr,
        &m_overlayPs);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreatePixelShader (overlay) failed, hr=0x%08lx",
                 static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: shaders compiled (vs %lld bytes, ps %lld bytes, ps_overlay %lld bytes)",
           static_cast<long long>(m_vsBlob->GetBufferSize()),
           static_cast<long long>(m_psBlob->GetBufferSize()),
           static_cast<long long>(m_overlayPsBlob->GetBufferSize()));
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
    // ScissorEnable is TRUE so STREAM_AUTOCROP can clip the quad to the
    // videoRect content area when baked letterbox is detected. For the
    // default case (no crop) we still call RSSetScissorRects with the
    // full canvas rect every draw — it's a cheap cmd-list add.
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode              = D3D11_FILL_SOLID;
    rastDesc.CullMode              = D3D11_CULL_NONE;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable       = TRUE;
    rastDesc.ScissorEnable         = TRUE;

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

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay alpha-blend state.
    // Src-over: RGB_out = src.rgb * src.a + dst.rgb * (1 - src.a). Used by
    // the overlay draw pass after the video quad. Alpha-channel of the
    // back buffer is preserved (we don't composite alpha onto the display).
    D3D11_BLEND_DESC overlayBlendDesc = {};
    overlayBlendDesc.AlphaToCoverageEnable  = FALSE;
    overlayBlendDesc.IndependentBlendEnable = FALSE;
    overlayBlendDesc.RenderTarget[0].BlendEnable           = TRUE;
    overlayBlendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
    overlayBlendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    overlayBlendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    overlayBlendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    overlayBlendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
    overlayBlendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    overlayBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_device->CreateBlendState(&overlayBlendDesc, &m_overlayBlend);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: CreateBlendState (overlay) failed, hr=0x%08lx",
                 static_cast<unsigned long>(hr));
        return false;
    }

    qDebug("FrameCanvas: state objects created (sampler + rasterizer + blend + overlay blend)");
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

// STREAM_AUTOCROP — one-shot scan of the imported shared texture for
// baked-in letterbox. Detects fully-black pixel rows at top/bottom that
// aren't player-added letterbox but are encoded in the source video
// (e.g. Netflix 1920x1080 H.264 with 66+0 asymmetric baked bars — an
// unusual encode choice that breaks the viewport-fills-screen assumption).
// When detected, stores the crop in m_srcCropTop/Bottom/Left/Right; the
// drawTexturedQuad viewport math then uses effective content dims for
// fitAspectRect and offsets the D3D viewport so baked rows fall off-screen.
// No shader changes — all work is viewport/scissor-based.
//
// Returns true once the scan ran (latches m_bakedScanDone regardless of
// detection outcome). Robustness:
//   - Skips the scan if the imported texture format isn't BGRA8.
//   - Requires top+bottom ≥ 8 rows AND ≤ 25% of frame height to accept
//     detection (rejects all-black frames from intro fade-ins).
//   - Uses luma ≤ 2 threshold — Netflix-encoded baked bars are exact
//     (0,0,0) but dark scene content always has ≥3-value codec noise.
//   - Scans only the top 20% + bottom 20% (not full frame) for speed.
//   - One-shot: never re-scans after m_bakedScanDone latches true.
bool FrameCanvas::scanBakedLetterbox()
{
#ifndef _WIN32
    m_bakedScanDone = true;
    return true;
#else
    if (m_bakedScanDone) return true;
    if (!m_device || !m_context) return false;

    int W = 0, H = 0;
    int stride = 0;
    int topBlack = 0, bottomBlack = 0;
    const char* path = "none";

    // Per-pixel black check retained for the strict threshold used inside
    // the per-row uniformity scan below.
    auto pixelIsBlack = [](int b, int g, int r) -> bool {
        const int lum = (299 * r + 587 * g + 114 * b) / 1000;
        return lum <= 5;
    };

    // Pick the best D3D11 source texture available this tick: zero-copy
    // shared import if it's active, otherwise the SHM-uploaded video
    // texture. Both are BGRA on our device so staging readback is uniform.
    // Previous SHM-via-m_reader approach hit the "no new frame" early-return
    // on most scan ticks (reader state depends on consumeShmFrame's prior
    // consumption); reading from m_videoTexture instead gives us whatever
    // is currently bound for render — which is what the user sees.
    ID3D11Texture2D* scanSrc = m_importedD3DTex ? m_importedD3DTex : m_videoTexture;

    if (scanSrc) {
        path = m_importedD3DTex ? "d3d11-zc" : "d3d11-shm";
        D3D11_TEXTURE2D_DESC srcDesc = {};
        scanSrc->GetDesc(&srcDesc);
        if (srcDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM
            && srcDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
            m_bakedScanDone = true;
            return true;
        }

        D3D11_TEXTURE2D_DESC stageDesc = srcDesc;
        stageDesc.Usage          = D3D11_USAGE_STAGING;
        stageDesc.BindFlags      = 0;
        stageDesc.MiscFlags      = 0;
        stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* staging = nullptr;
        HRESULT hr = m_device->CreateTexture2D(&stageDesc, nullptr, &staging);
        if (FAILED(hr) || !staging) {
            if (staging) staging->Release();
            return false;
        }

        m_context->CopyResource(staging, scanSrc);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            staging->Release();
            return false;
        }

        W = static_cast<int>(srcDesc.Width);
        H = static_cast<int>(srcDesc.Height);
        stride = static_cast<int>(mapped.RowPitch);
        const int scanLimit = qMax(4, H / 5);
        const uint8_t* base = static_cast<const uint8_t*>(mapped.pData);

        // Baked letterbox rows are both (1) very dark AND (2) spatially
        // uniform — every sampled pixel carries the encoder's padding
        // constant. Dark scene content (night sky, shadow, dark shirt
        // texture) can pass the "very dark" test but will carry codec
        // noise + chroma variation that breaks uniformity. The max-min
        // luma guard rejects content rows that have even a small
        // gradient across the row. This is the critical fix for
        // false-positive detection of dark content as baked letterbox
        // (which was cropping ~59 bottom rows on scenes where the
        // actual baked region was 0 rows — clipping real content
        // off-screen per Hemanth's 2026-04-20 report).
        auto rowIsBlack = [&](int y) -> bool {
            const uint8_t* row = base + static_cast<size_t>(y) * stride;
            int minLum = 255;
            int maxLum = 0;
            for (int x = 0; x < W; x += 8) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4;
                if (!pixelIsBlack(p[0], p[1], p[2])) return false;
                const int lum = (299 * p[2] + 587 * p[1] + 114 * p[0]) / 1000;
                if (lum < minLum) minLum = lum;
                if (lum > maxLum) maxLum = lum;
            }
            return (maxLum - minLum) <= 2;
        };

        for (int y = 0; y < scanLimit; ++y) {
            if (rowIsBlack(y)) ++topBlack; else break;
        }
        for (int y = H - 1; y >= H - scanLimit; --y) {
            if (rowIsBlack(y)) ++bottomBlack; else break;
        }

        m_context->Unmap(staging, 0);
        staging->Release();
    } else {
        // Neither zero-copy nor SHM video texture is populated — don't
        // latch; retry next tick.
        return false;
    }

    // Only apply TOP crop — bottom crop is intentionally disabled. Netflix
    // mastered content commonly renders burned-in title cards (e.g.
    // "Loguetown / Place of Execution") INTO the lower baked letterbox
    // area, where the text pixels can be too sparse for the every-8
    // pixel scan stride to reliably catch them. Reported on One Piece
    // S02E01 2026-04-20: detection over-cropped bottom + clipped the
    // second-line title text off-screen. A cosmetic black bar at the
    // bottom when a scene has real unused-bottom letterbox is a far
    // smaller visual sin than losing in-video text — and the asymmetric
    // top-only crop still fixes the primary symptom Hemanth reported
    // (top black bar with video pushed down).
    const int perEdgeCeiling = (H * 20) / 100;
    const int newTop = (topBlack <= perEdgeCeiling)
                       ? (topBlack >= 4 ? topBlack : 0)
                       : m_srcCropTop;
    const bool appliedAny = (newTop != m_srcCropTop) || (m_srcCropBottom != 0);
    m_srcCropTop    = newTop;
    m_srcCropBottom = 0;
    m_srcCropLeft   = 0;
    m_srcCropRight  = 0;
    // bottomBlack used below only for the diagnostic log.
    const int bottomBlackDetected = bottomBlack;

    QFile dbg("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
    if (dbg.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream s(&dbg);
        s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
          << " [FrameCanvas autocrop] path=" << path
          << " source=" << W << "x" << H
          << " stride=" << stride
          << " detected_top=" << topBlack
          << " detected_bottom_ignored=" << bottomBlackDetected
          << " applied_top=" << m_srcCropTop
          << " applied_bottom=" << m_srcCropBottom
          << " applied_any=" << appliedAny
          << "\n";
    }
    return true;
#endif
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
    // STREAM_AUTOCROP — new video session; reset the baked-letterbox scan
    // state so the next play re-detects encoding-specific letterbox.
    m_srcCropTop       = 0;
    m_srcCropBottom    = 0;
    m_srcCropLeft      = 0;
    m_srcCropRight     = 0;
    m_bakedScanDone    = false;
    m_framesSinceImport = 0;
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
    // STREAM_AUTOCROP — clear scan state on detach so a re-attach starts fresh.
    m_srcCropTop       = 0;
    m_srcCropBottom    = 0;
    m_srcCropLeft      = 0;
    m_srcCropRight     = 0;
    m_bakedScanDone    = false;
    m_framesSinceImport = 0;
    if (wasActive) {
        emit zeroCopyActivated(false);   // sidecar should re-engage CPU pipeline
    }
}

// PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay SHM attach/detach.
// Opens the named SHM created by the sidecar, (re)creates the locally-
// owned overlay D3D11 texture at the announced dims. Called from
// VideoPlayer's SidecarProcess::overlayShm signal handler.
void FrameCanvas::attachOverlayShm(const QString& shmName, int width, int height)
{
    detachOverlayShm();
    if (shmName.isEmpty() || width <= 0 || height <= 0) return;

    m_overlayReader = new OverlayShmReader();
    if (!m_overlayReader->attach(shmName, width, height)) {
        qWarning("FrameCanvas: overlay SHM attach failed (%s %dx%d)",
                 shmName.toUtf8().constData(), width, height);
        delete m_overlayReader;
        m_overlayReader = nullptr;
        return;
    }

#ifdef _WIN32
    if (!m_device) {
        qWarning("FrameCanvas: overlay SHM opened but device not ready — deferred until next renderFrame");
        return;
    }

    // LOCAL (non-shared) texture. DYNAMIC usage so UpdateSubresource is
    // the fast CPU→GPU upload path. Format BGRA matches sidecar payload.
    D3D11_TEXTURE2D_DESC td = {};
    td.Width              = static_cast<UINT>(width);
    td.Height             = static_cast<UINT>(height);
    td.MipLevels          = 1;
    td.ArraySize          = 1;
    td.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count   = 1;
    td.Usage              = D3D11_USAGE_DEFAULT;
    td.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags     = 0;
    td.MiscFlags          = 0;

    HRESULT hr = m_device->CreateTexture2D(&td, nullptr, &m_overlayTex);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: overlay CreateTexture2D failed, hr=0x%08lx",
                 static_cast<unsigned long>(hr));
        detachOverlayShm();
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format                    = td.Format;
    sd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels       = 1;

    hr = m_device->CreateShaderResourceView(m_overlayTex, &sd, &m_overlaySrv);
    if (FAILED(hr)) {
        qWarning("FrameCanvas: overlay CreateShaderResourceView failed, hr=0x%08lx",
                 static_cast<unsigned long>(hr));
        detachOverlayShm();
        return;
    }

    m_overlayTexW = width;
    m_overlayTexH = height;
    m_overlayLastCounter = 0;
    m_overlayCurrentlyVisible = false;
    qDebug("FrameCanvas: overlay SHM attached %dx%d", width, height);
#endif
}

void FrameCanvas::detachOverlayShm()
{
#ifdef _WIN32
    if (m_overlaySrv) { m_overlaySrv->Release(); m_overlaySrv = nullptr; }
    if (m_overlayTex) { m_overlayTex->Release(); m_overlayTex = nullptr; }
#endif
    if (m_overlayReader) {
        m_overlayReader->detach();
        delete m_overlayReader;
        m_overlayReader = nullptr;
    }
    m_overlayTexW = 0;
    m_overlayTexH = 0;
    m_overlayLastCounter = 0;
    m_overlayCurrentlyVisible = false;
}

// Poll overlay SHM for a fresh frame. Returns true if the texture got a
// fresh upload this tick; caller uses m_overlayCurrentlyVisible to decide
// whether to issue the overlay draw call.
bool FrameCanvas::pollOverlayShm()
{
#ifdef _WIN32
    if (!m_overlayReader || !m_overlayTex || !m_context) return false;

    OverlayShmReader::Frame f = m_overlayReader->read();
    if (f.counter == m_overlayLastCounter) return false;  // no change

    m_overlayLastCounter      = f.counter;
    m_overlayCurrentlyVisible = f.valid;

    if (!f.valid || !f.bgra) return false;
    if (f.width != m_overlayTexW || f.height != m_overlayTexH) {
        qWarning("FrameCanvas: overlay frame dims %dx%d do not match texture %dx%d; waiting for reattach",
                 f.width, f.height, m_overlayTexW, m_overlayTexH);
        m_overlayCurrentlyVisible = false;
        return false;
    }

    // Upload the BGRA bytes into the locally-owned overlay texture. All
    // intra-device — no cross-process GPU sync issues.
    const UINT rowPitch = static_cast<UINT>(f.width) * 4;
    m_context->UpdateSubresource(m_overlayTex, 0, nullptr, f.bgra, rowPitch, 0);
    return true;
#else
    return false;
#endif
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

void FrameCanvas::setCropAspect(double aspect)
{
    if (aspect < 0.0) aspect = 0.0;
    m_cropAspect = aspect;
}

void FrameCanvas::setSubtitleLift(int physicalPx)
{
    if (physicalPx < 0) physicalPx = 0;
    m_subtitleLiftPx = physicalPx;
}

QSize FrameCanvas::canvasPixelSize() const
{
    const qreal dpr = devicePixelRatioF();
    return QSize(qMax(1, qRound(width() * dpr)),
                 qMax(1, qRound(height() * dpr)));
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
