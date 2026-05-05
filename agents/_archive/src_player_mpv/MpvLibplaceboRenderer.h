#pragma once

#include <atomic>
#include <functional>
#include <memory>

struct mpv_handle;

typedef const struct pl_log_t* pl_log;
typedef const struct pl_gpu_t* pl_gpu;
struct pl_swapchain_frame;

extern "C" {
#include <libplacebo/colorspace.h>
}

class MpvLibplaceboRenderer {
public:
    using RenderScheduler = std::function<void()>;

    MpvLibplaceboRenderer();
    ~MpvLibplaceboRenderer();

    MpvLibplaceboRenderer(const MpvLibplaceboRenderer&) = delete;
    MpvLibplaceboRenderer& operator=(const MpvLibplaceboRenderer&) = delete;

    bool attachMpv(mpv_handle* mpv);
    void detachMpv();

    void setRenderScheduler(RenderScheduler scheduler);
    void detachGpu(pl_gpu gpu = nullptr);
    void resetFrameState();

    bool renderToSwapchain(pl_log log,
                           pl_gpu gpu,
                           const pl_swapchain_frame& frame,
                           int width,
                           int height);
    void finishPresentedFrame(pl_gpu gpu);

    bool hasRenderedFrame() const;
    bool swContextReady() const;

    // MAKE_MPV_BEAT_FFMPEG Task 6 step 5 — push HDR color metadata from
    // MpvBackend (which queries mpv's video-params/{primaries,gamma,sig-peak}
    // after probe) into the renderer's per-frame color-space cache. Threadsafe
    // (cache is mutex-protected). Called from GUI thread; consumed on whichever
    // thread invokes pl_render_image.
    //
    // Pass an all-zero pl_color_space (with primaries == PL_COLOR_PRIM_UNKNOWN)
    // to clear — the renderer falls back to pl_color_space_srgb in that case
    // (Task 3.5/4 default behavior).
    void setSourceColorSpace(const pl_color_space& csp);

private:
    struct State;
    std::unique_ptr<State> m_state;
};
