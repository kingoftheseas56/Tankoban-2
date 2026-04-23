#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QSize>
#include <atomic>
#include <thread>
#include <vector>

#include "VsyncTimingLogger.h"

class ShmFrameReader;
class OverlayShmReader;
class SyncClock;

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11InputLayout;
struct ID3D11SamplerState;
struct ID3D11RasterizerState;
struct ID3D11BlendState;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D10Blob;            // ID3DBlob is a typedef for ID3D10Blob
struct IDXGISwapChain1;
#endif

class FrameCanvas : public QWidget {
    Q_OBJECT

public:
    explicit FrameCanvas(QWidget* parent = nullptr);
    ~FrameCanvas() override;

    // GPU color processing — brightness/contrast/saturation/gamma applied in
    // the pixel shader via a per-frame dynamic constant buffer.
    void setColorParams(float brightness, float contrast,
                        float saturation, float gamma);

    // Batch 3.1 (Player Polish Phase 3) — source color metadata from the
    // sidecar's demuxer probe (AVCOL_PRI_* + AVCOL_TRC_* values, raw
    // FFmpeg constants). Translated inside to our shader's colorSpace +
    // transferFunc cbuffer fields. Currently Batch 3.1 only uses
    // colorPrimaries to drive the BT.2020 → BT.709 gamut matrix;
    // transferFunc (PQ / HLG / tonemap) lights up in Batches 3.2 – 3.4.
    // Calling with SDR values (BT.709 primaries, BT.709 / sRGB trc) sets
    // the nominal path — identical to the pre-3.1 behavior.
    void setHdrColorInfo(int colorPrimaries, int colorTrc);

    // Batch 3.4 (Player Polish Phase 3) — tonemap operator selection for
    // the final HDR-to-SDR compression step in the pixel shader. Values:
    //   0 = Off (saturate / hard clip — the pre-3.4 default; correct for SDR)
    //   1 = Reinhard (x / (1 + x), simple + gentle, slightly dim highlights)
    //   2 = ACES (Narkowicz "ACES filmic" fit — punchy, film-like)
    //   3 = Hable (Uncharted 2 filmic — smooth shadows, strong highlight roll-off)
    // Out-of-range values fall back to 0 (Off). Called from VideoPlayer's
    // existing FilterPopover::toneMappingChanged handler after mapping the
    // popover's string identifier to this int enum.
    void setTonemapMode(int mode);

    // SHM frame consumption — VideoPlayer attaches the ShmFrameReader after
    // the sidecar publishes its shared memory region. startPolling enables
    // per-frame readLatest in the render loop; stopPolling halts it.
    void attachShm(ShmFrameReader* reader);
    void detachShm();
    void startPolling();
    void stopPolling();

    // Zero-copy display of the sidecar's shared D3D11 texture. Pass the NT
    // handle from the sidecar's d3d11_texture event. We open it in our own
    // D3D11 device and wrap as an SRV, eliminating the SHM upload and CPU
    // sws_scale work. On failure (unsupported, wrong GPU, etc.), silently
    // falls back to SHM.
    void attachD3D11Texture(quintptr ntHandle, int width, int height);
    void detachD3D11Texture();

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — subtitle overlay SHM.
    // Sidecar writes libass/PGS BGRA overlay bytes into the named SHM
    // region; we open it and upload per-frame (when the atomic frame
    // counter advances) into a LOCALLY-owned D3D11 overlay texture.
    // drawTexturedQuad composites the overlay as a second alpha-blended
    // quad after the video quad. Strictly intra-device — no cross-process
    // GPU resource sharing, no keyed-mutex sync pitfalls (the architectural
    // problem that forced the reverted cross-process-shared-texture 3.B).
    void attachOverlayShm(const QString& shmName, int width, int height);
    void detachOverlayShm();

    // VIDEO_PLAYER_FIX Batch 3.2 — snapshot current displayed frame as a
    // QImage at source resolution. Handles both paths: D3D11 zero-copy
    // (CreateTexture2D(USAGE_STAGING + CPU_ACCESS_READ) + CopyResource
    // from m_importedD3DTex + Map/memcpy row-by-row + Unmap) and SHM
    // (readLatest + deep-copy QImage). Returns null QImage only when no
    // source frame is available at all (no zero-copy texture AND no SHM
    // attachment AND no valid SHM frame).
    QImage captureCurrentFrame();

    // Returns true when the D3D11 zero-copy path is active. Exposed for
    // diagnostics / informational use; captureCurrentFrame handles both
    // paths transparently.
    bool isZeroCopyActive() const { return m_d3dActive; }

    // VIDEO_PLAYER_FIX Batch 7.1 — total Presents skipped by the
    // sustained-lag guard (m_framesSkippedTotal, Batch 1.2 pattern).
    // Surfaced to the StatsBadge; a running "drops" counter since the
    // current file opened. Reset implicitly by tearDownD3D() on device
    // recovery, which is the intended behavior — a fresh device starts
    // fresh accounting.
    quint64 framesSkipped() const { return m_framesSkippedTotal; }

    // Clear the sustained-lag accumulator. Called by VideoPlayer around
    // seek dispatch so the wall-clock Present-interval spike inherent to
    // seek doesn't mis-trigger the 3-in-a-row skip-next-Present guard,
    // which otherwise produces a visible pause on arrow-key seek.
    void resetLagAccounting();

    // Vsync timing instrumentation. Toggle on, play for N seconds, toggle
    // off — buffered samples dump to <dumpPath> as CSV. Owns the swap chain
    // so DXGI GetFrameStatistics returns valid stats every Present (the
    // entire point of the Path B refactor over the legacy QRhiWidget path).
    void setVsyncLogging(bool enabled, const QString& dumpPath);
    bool vsyncLoggingEnabled() const { return m_vsyncLoggingOn; }
    int  vsyncSampleCount() const { return m_vsyncLogger.sampleCount(); }

    // Phase 7 bake-in fix — aspect ratio override. 0.0 (or any non-positive
    // value) means "use the natural aspect from the source frame" (default,
    // Phase 2.5 behavior). Positive values override: e.g., 16.0/9.0 forces
    // a 16:9 letterbox even on 4:3 sources. Used by the right-click menu's
    // Aspect Ratio submenu — that menu existed pre-Phase-7 but its handler
    // in VideoPlayer was a no-op, so the option silently did nothing.
    void setForcedAspectRatio(double aspect);
    // Crop-to-aspect. Zooms the video uniformly so the portion of the
    // source matching the given aspect ratio fills the fitted videoRect;
    // overflow past the render-target bounds gets clipped by D3D11.
    // Use case: 1920x1080 containers with baked cinemascope (2.35 / 2.39)
    // letterbox pixels — cropping to the content aspect discards the
    // baked bars. 0.0 means no crop (default). Subtitle overlay is NOT
    // zoomed; subs stay at natural size anchored to the un-cropped rect.
    void setCropAspect(double aspect);
    void setUserZoom(double zoom);
    QSize canvasPixelSize() const;

    // Lift the subtitle overlay upward by N physical pixels. Used by
    // VideoPlayer to push subtitles above the HUD control bar when it's
    // visible, and drop them back to natural position when the HUD hides.
    // Acts on the overlay viewport only — video quad unaffected. No-op on
    // subtitles the sidecar draws into letterbox bars (lift > visible bar
    // is clamped out visually since the overlay texture has no content
    // above the subtitle baseline).
    void setSubtitleLift(int physicalPx);

    // Batch 1.2 — SyncClock feedback hook. VideoPlayer owns a SyncClock and
    // hands a pointer in at construction. FrameCanvas reports per-frame
    // present latency into it after each Present (see m_intervalTimer below).
    // Today no consumer reads the accumulated latency — Phase 4 will use it
    // to drive a sidecar-side audio-speed adjustment command. The wire is
    // built now so Phase 4 has nothing to thread later.
    void setSyncClock(SyncClock* clock);

signals:
    // Emitted when the D3D11 zero-copy path becomes active or inactive.
    // VideoPlayer relays this to the sidecar so it can short-circuit its
    // CPU/SHM pipeline (saves ~15ms per frame in the producer thread).
    void zeroCopyActivated(bool active);

    // Batch 6.2 (Player Polish Phase 6) — emitted at the start of a
    // device-lost recovery cycle (DXGI_ERROR_DEVICE_REMOVED / _RESET
    // observed on Present). VideoPlayer surfaces "Reconnecting display…"
    // in ToastHud. Recovery is internal to FrameCanvas — no action
    // required from the consumer beyond the optional toast.
    void deviceReconnecting();

    // Emitted on every mouse move over the widget. Required because
    // WA_PaintOnScreen + WA_NativeWindow makes this a separate native HWND
    // — mouse events do NOT bubble up to VideoPlayer the way they did with
    // FrameCanvas (QRhiWidget). VideoPlayer uses the y coord to decide:
    // cursor unhide on any move, but HUD reveal only when y is in the
    // bottom-edge zone (matches Hemanth's UX preference vs the reference's
    // any-move-shows-HUD behavior).
    void mouseActivityAt(int y);
    void canvasPixelSizeSettled(int width, int height);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
#ifdef _WIN32
    // Batch 6.2 — device lifecycle helpers. initializeD3D runs the full
    // device + swap chain + shader + state object creation (originally
    // inlined in showEvent); tearDownD3D releases every D3D11 object in
    // reverse creation order (originally inlined in the destructor);
    // recoverFromDeviceLost orchestrates teardown → reinit → zero-copy
    // resignal when Present reports DXGI_ERROR_DEVICE_REMOVED/_RESET.
    bool initializeD3D();
    void tearDownD3D();
    void recoverFromDeviceLost();

    void renderFrame();
    void releaseBackBufferView();
    bool ensureBackBufferView();
    bool createShaders();
    bool createVertexBuffer();
    bool createStateObjects();
    bool createColorBuffer();
    void uploadColorParams();
    bool consumeShmFrame();        // returns true if m_videoSrv updated this tick
    bool processPendingImport();   // returns true if m_importedSrv updated this tick
    bool pollOverlayShm();         // returns true if m_overlayTex got a fresh upload this tick
    void drawTexturedQuad();

    ID3D11Device*           m_device    = nullptr;
    ID3D11DeviceContext*    m_context   = nullptr;
    IDXGISwapChain1*        m_swapChain = nullptr;
    ID3D11RenderTargetView* m_rtv       = nullptr;

    // Phase 2.1 shaders. m_vsBlob is retained because Phase 2.2 needs the
    // vertex bytecode for CreateInputLayout; m_psBlob is retained for parity
    // and freed in the destructor.
    ID3D11VertexShader*     m_vs          = nullptr;
    ID3D11PixelShader*      m_ps          = nullptr;
    ID3D10Blob*             m_vsBlob      = nullptr;
    ID3D10Blob*             m_psBlob      = nullptr;

    // Phase 2.2: fullscreen quad vertex buffer + input layout matching vs_main.
    ID3D11Buffer*           m_vbuf        = nullptr;
    ID3D11InputLayout*      m_inputLayout = nullptr;

    // Phase 2.3: pipeline state objects. Sampler is linear+clamp (matches the
    // GLSL fragment shader path). Rasterizer is no-cull so triangle winding
    // order is irrelevant. Blend is disabled — the textured quad fills the
    // back buffer opaquely.
    ID3D11SamplerState*       m_sampler     = nullptr;
    ID3D11RasterizerState*    m_rasterizer  = nullptr;
    ID3D11BlendState*         m_blend       = nullptr;

    // Source frame dimensions (used by aspect-ratio viewport).
    // Set to test texture dims now; Phase 4 will overwrite per SHM frame.
    int                       m_frameW      = 0;
    int                       m_frameH      = 0;

    // Phase 3.1: dynamic constant buffer fed from m_colorParams once per
    // frame via Map(WRITE_DISCARD) / Unmap.
    ID3D11Buffer*             m_colorBuffer = nullptr;

    // Phase 4 — SHM video frame texture. Distinct from m_testTexture because
    // we need USAGE_DEFAULT (UpdateSubresource per frame), not IMMUTABLE.
    // Recreated on size change. Bound in place of m_testSrv when SHM frames
    // are flowing.
    ID3D11Texture2D*          m_videoTexture = nullptr;
    ID3D11ShaderResourceView* m_videoSrv     = nullptr;
    int                       m_videoTexW    = 0;
    int                       m_videoTexH    = 0;

    // Phase 5 — Holy Grail D3D11 shared texture import. The pointers live
    // inside the _WIN32 guard; the platform-agnostic state (handle, dims,
    // active flag) lives outside so the public attach/detach API can be
    // implemented without #ifdef in the cpp.
    ID3D11Texture2D*          m_importedD3DTex = nullptr;
    ID3D11ShaderResourceView* m_importedSrv    = nullptr;

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay render resources.
    // m_overlayTex is LOCAL (created with CreateTexture2D on m_device, not
    // a shared-handle import). m_overlayPs is the ps_overlay entry in
    // video_d3d11.hlsl — no color transforms on subtitle pixels.
    // m_overlayBlend is alpha-blend state (SRC_ALPHA / INV_SRC_ALPHA).
    // m_overlayCurrentlyVisible tracks the sidecar's valid flag from the
    // last counter bump — suppresses the draw pass on no-subs frames.
    ID3D11Texture2D*          m_overlayTex               = nullptr;
    ID3D11ShaderResourceView* m_overlaySrv               = nullptr;
    ID3D11PixelShader*        m_overlayPs                = nullptr;
    ID3D10Blob*               m_overlayPsBlob            = nullptr;
    ID3D11BlendState*         m_overlayBlend             = nullptr;
    int                       m_overlayTexW              = 0;
    int                       m_overlayTexH              = 0;
    uint64_t                  m_overlayLastCounter       = 0;
    bool                      m_overlayCurrentlyVisible  = false;

    QTimer                  m_renderTimer;

    // Batch 6.2 — re-entry guard for recoverFromDeviceLost. Prevents a
    // cascading recovery if Present fails again during the recreation
    // phase of a prior recovery (e.g., GPU driver still unstable).
    bool                    m_recovering = false;

    // Aspect-debug — prints source / widget / viewport dims whenever any
    // of {frame-dim, widget-dim} change. Lets us diagnose "aspect wrong
    // on fullscreen" reports without spamming every Present.
    int                     m_aspectLoggedForFrameW  = 0;
    int                     m_aspectLoggedForFrameH  = 0;
    int                     m_aspectLoggedForWidgetW = 0;
    int                     m_aspectLoggedForWidgetH = 0;
    double                  m_aspectLoggedForForced  = -1.0;  // -1 = never logged
#endif

    // Phase 6 — vsync timing instrumentation. Lives outside _WIN32 so the
    // public API is implementable on any platform (samples are zero-filled
    // / dxgiValid=false on non-Win); the actual GetFrameStatistics call is
    // gated inside _WIN32 in renderFrame.
    VsyncTimingLogger         m_vsyncLogger;
    bool                      m_vsyncLoggingOn = false;
    QString                   m_vsyncDumpPath;

    // Batch 1.1 — per-frame latency measurement. m_intervalTimer measures
    // present-to-present wall-clock; overage vs m_expectedFrameMs is the
    // latency reported to the vsync logger and SyncClock.
    // Batch 1.2 — m_expectedFrameMs is now queried from the monitor at
    // swap-chain creation via GetDeviceCaps(VREFRESH); 60Hz is fallback only.
    QElapsedTimer             m_intervalTimer;
    double                    m_expectedFrameMs = 16.6667;

    // PLAYER_PERF_FIX Phase 1 Batch 1.3 — DXGI waitable-driven cadence.
    // Replaces QTimer(16)'s wall-clock wake with DXGI's own vsync-aligned
    // signal. The waitable is allocated by CreateSwapChainForHwnd when
    // DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT is set (Batch 1.2)
    // and returned by IDXGISwapChain2::GetFrameLatencyWaitableObject().
    // m_waitableThread loops WaitForSingleObjectEx(m_waitable, 100ms) and
    // posts a Qt::QueuedConnection renderFrame call to the main thread on
    // each signal. Main-thread renderFrame topology is preserved — only
    // the driver of the loop changes. On shutdown, m_waitableStop is set,
    // CloseHandle on the waitable wakes the WAIT immediately, join.
    // Stored as void* so this header compiles without windows.h.
    void*                     m_waitableHandle = nullptr;
    std::thread               m_waitableThread;
    std::atomic<bool>         m_waitableStop{false};
    void startWaitableLoop();
    void stopWaitableLoop();
    void waitableLoop();

    // Batch 1.2 — SyncClock feedback + lag-aware skip-ahead (OBS video_sleep
    // pattern adapted). When m_frameLatencyMs > m_lagThresholdMs for
    // m_lagSustainTicks consecutive ticks, we skip the next Present() — old
    // frame stays on screen for one vsync, letting the render pipeline catch
    // up instead of accumulating backlog. Skipped-frame count is a lifetime
    // total exposed via the VsyncTimingLogger CSV column frame_skipped.
    SyncClock*                m_syncClock          = nullptr;
    int                       m_lagTickCount       = 0;
    bool                      m_skipNextPresent    = false;
    quint64                   m_framesSkippedTotal = 0;
    static constexpr double   kLagThresholdMs      = 25.0;  // one vsync over 60Hz nominal
    static constexpr int      kLagSustainTicks     = 3;     // three-in-a-row → skip

    // PLAYER_PERF_FIX Phase 1 Batch 1.1 — 1 Hz [PERF] diagnostic log.
    // Captures per-frame timer_interval_ms (actual wake cadence vs
    // requested 16ms), draw_ms (clear + textured-quad draw), and
    // present_ms (time inside IDXGISwapChain::Present). Flushed once per
    // second as p50/p99 with skipped_presents delta + DXGI frame-stats
    // deltas. Lives here so Phase 1 Batches 1.2/1.3 can measure pre/post
    // cadence impact empirically. Mirrors the sidecar [PERF] log shape so
    // tooling unifies.
    std::vector<double> m_perfTimerIntervalMs;
    std::vector<double> m_perfDrawMs;
    std::vector<double> m_perfPresentMs;
    QElapsedTimer       m_perfInvocationTimer;    // between renderFrame calls
    QElapsedTimer       m_perfWindowTimer;        // 1 Hz flush gate
    quint64             m_perfSkippedPresentsBase = 0;
    // DXGI GetFrameStatistics is monotonic-counter based; we log deltas
    // per window. Base captured when a window starts, compared at flush.
    // Store as primitive types so this header compiles without windows.h.
    unsigned int        m_perfDxgiPresentCountBase   = 0;
    unsigned int        m_perfDxgiPresentRefreshBase = 0;
    long long           m_perfDxgiSyncQpcBase        = 0;
    bool                m_perfDxgiBaseValid          = false;

    // Batch 2.1 — clock-aware frame selection telemetry. consumeShmFrame
    // prefers ShmFrameReader::readBestForClock (clock-aware, OBS
    // ready_async_frame semantic) and falls back to readLatest only when
    // the clock-aware read returns no eligible frame (startup + post-seek
    // race windows). m_lastChosenFrameId is the frame ID used this tick
    // (0 if no SHM frame was consumed — zero-copy D3D11 path or no-frame-
    // available tick). m_lastFallbackUsed is true when the readLatest
    // fallback kicked in. Both are reset to nominal at the top of each
    // renderFrame tick and stamped into the vsync CSV at tick end so
    // Agent 6 can verify "no dupes, no skips during steady-state" by
    // inspection of chosen_frame_id monotonicity.
    quint64                   m_lastChosenFrameId  = 0;
    bool                      m_lastFallbackUsed   = false;

    // Batch 2.2 — overflow-drop telemetry at the SHM boundary.
    // m_previousConsumedFrameId persists across ticks (0 → first consumed
    // id → next consumed id → ...). When consumeShmFrame picks a frame
    // whose id is N and the prior consumed id was M, m_lastProducerDrops
    // is set to max(0, N - M - 1) — the count of frameIds the sidecar
    // produced in the ring that we never saw (got overwritten before we
    // got to them, or the clock-aware selector skipped over as stale).
    // m_lastConsumerLateMs is (sidecarClockUs - framePtsUs) / 1000.0 —
    // positive = the displayed frame's PTS is BEHIND the clock (stale);
    // negative = ahead of clock (within readBestForClock's 8ms tolerance).
    // Both reset to 0 at the top of each renderFrame tick; set only when
    // consumeShmFrame actually picks a frame. Stamped into the vsync CSV.
    quint64                   m_previousConsumedFrameId = 0;
    quint32                   m_lastProducerDrops       = 0;
    double                    m_lastConsumerLateMs      = 0.0;

    // Phase 5 — pending-import slot. attachD3D11Texture (any thread) writes
    // these; processPendingImport (GPU thread, inside drawTexturedQuad)
    // consumes them. One-shot: m_pendingD3DHandle is cleared after each
    // process attempt regardless of success/failure.
    quintptr                  m_pendingD3DHandle = 0;
    int                       m_pendingD3DWidth  = 0;
    int                       m_pendingD3DHeight = 0;
    bool                      m_d3dActive        = false;

    // Phase 4 — SHM consumer state. Lives outside the _WIN32 guard so the
    // public attach/detach/startPolling/stopPolling API can be implemented
    // on any platform (FrameCanvas is Windows-only at runtime, but the
    // pointer + flag are platform-agnostic).
    ShmFrameReader*           m_reader       = nullptr;
    bool                      m_polling      = false;

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay SHM reader.
    // Owned here; created on attachOverlayShm, destroyed on detach/ destructor.
    OverlayShmReader*         m_overlayReader = nullptr;
    QTimer                    m_canvasSizeDebounce;
    QSize                     m_lastCanvasPixelSize;

    // Phase 7 bake-in fix — aspect ratio override. 0.0 = use natural aspect
    // from m_frameW/m_frameH; > 0 = force this w/h ratio. Read by the
    // viewport math in drawTexturedQuad.
    double                    m_forcedAspect = 0.0;
    // Crop target aspect (see setCropAspect). 0.0 = no crop.
    double                    m_cropAspect   = 0.0;
    double                    m_userZoom     = 1.0;
    // Subtitle overlay vertical lift in physical pixels. Updated by
    // VideoPlayer::showControls/hideControls to clear the HUD control
    // bar when it's visible. Read by drawTexturedQuad's overlay pass.
    int                       m_subtitleLiftPx = 0;

    // Baked-letterbox source crop — auto-detected asymmetric black-pixel
    // rows at source edges (e.g. Netflix 1920x1080 H.264 encodes with
    // 66+0 baked bars rather than symmetric cinemascope letterbox). Unlike
    // m_cropAspect (uniform zoom), these shift + scale the video quad's
    // viewport so the baked bars fall off-screen and the CONTENT region
    // (frameW - L - R, frameH - T - B) drives the aspect fit math.
    // Set by scanBakedLetterbox() on first valid frame after import.
    int                       m_srcCropTop     = 0;
    int                       m_srcCropBottom  = 0;
    int                       m_srcCropLeft    = 0;
    int                       m_srcCropRight   = 0;
    bool                      m_bakedScanDone  = false;
    int                       m_framesSinceImport = 0;
    int                       m_bakedScanCandidateTop = -1;
    int                       m_bakedScanCandidateCount = 0;
    // Scans the current shared texture for fully-black rows at top/bottom.
    // Returns true once scan ran and produced a valid result (whether or
    // not crop was detected); callers use this to latch one-shot behavior.
    bool scanBakedLetterbox();

    // ColorParams (32 bytes, std140-aligned) — matches the cbuffer layout
    // in resources/shaders/video_d3d11.hlsl. Lives outside the _WIN32 guard
    // so the public setColorParams API can be implemented on any platform
    // (FrameCanvas itself is Windows-only at runtime, but the colour
    // parameters are platform-agnostic data).
    struct alignas(16) ColorParams {
        float brightness = 0.0f;
        float contrast   = 1.0f;
        float saturation = 1.0f;
        float gamma      = 1.0f;
        int   colorSpace   = 0;  // Batch 3.1: 0 = BT.709 (no xform), 1 = BT.2020
        int   transferFunc = 0;  // Batch 3.2+3.3: 0 = sRGB, 1 = PQ, 2 = HLG
        int   tonemapMode  = 0;  // Batch 3.4: 0 = Off, 1 = Reinhard, 2 = ACES, 3 = Hable
        int   hdrOutput    = 0;  // Batch 3.5: 0 = SDR swap chain, 1 = scRGB (R16G16B16A16_FLOAT)
    } m_colorParams;
};
