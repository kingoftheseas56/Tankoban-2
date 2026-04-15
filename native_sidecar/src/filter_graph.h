#pragma once

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#include <mutex>
#include <string>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

class FilterGraph {
public:
    FilterGraph();
    ~FilterGraph();

    // Build a video filter graph. filter_descr e.g. "yadif=mode=0" or "eq=brightness=0.1".
    bool init_video(int width, int height, enum AVPixelFormat pix_fmt,
                    AVRational time_base, AVRational sar,
                    const std::string& filter_descr);

    // Build an audio filter graph. filter_descr e.g. "loudnorm=I=-16".
    bool init_audio(int sample_rate, const AVChannelLayout& ch_layout,
                    enum AVSampleFormat sample_fmt,
                    const std::string& filter_descr);

    // Push a decoded frame into the filter. Returns 0 on success.
    int push_frame(AVFrame* frame);

    // Pull a filtered frame. Returns 0 on success, AVERROR(EAGAIN) if buffering.
    int pull_frame(AVFrame* filtered);

    // Tear down (safe to call multiple times).
    void destroy();

    bool active() const { return graph_ != nullptr; }

    // Deferred init: store a filter spec to be applied lazily when actual
    // stream parameters are known (called from main thread).
    void set_pending(const std::string& spec);

    // Consume pending spec (called from decode thread). Returns empty if none.
    std::string take_pending();

private:
    AVFilterGraph*   graph_    = nullptr;
    AVFilterContext* src_ctx_  = nullptr;
    AVFilterContext* sink_ctx_ = nullptr;

    std::mutex       spec_mutex_;
    std::string      pending_spec_;
};
