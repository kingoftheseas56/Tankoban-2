#pragma once

#include <atomic>
#include <functional>
#include <memory>

struct mpv_handle;

typedef const struct pl_log_t* pl_log;
typedef const struct pl_gpu_t* pl_gpu;
struct pl_swapchain_frame;

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

private:
    struct State;
    std::unique_ptr<State> m_state;
};
