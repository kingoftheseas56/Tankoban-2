#include "filter_graph.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
}

FilterGraph::FilterGraph() {}

FilterGraph::~FilterGraph() {
    destroy();
}

bool FilterGraph::init_video(int width, int height, enum AVPixelFormat pix_fmt,
                             AVRational time_base, AVRational sar,
                             const std::string& filter_descr) {
    destroy();

    graph_ = avfilter_graph_alloc();
    if (!graph_) return false;

    // Source: buffer
    char args[512];
    std::snprintf(args, sizeof(args),
                  "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                  width, height, static_cast<int>(pix_fmt),
                  time_base.num, time_base.den,
                  sar.num > 0 ? sar.num : 1, sar.den > 0 ? sar.den : 1);

    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");

    int ret = avfilter_graph_create_filter(&src_ctx_, buffersrc, "in", args, nullptr, graph_);
    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot create buffer source: %d\n", ret);
        destroy();
        return false;
    }

    ret = avfilter_graph_create_filter(&sink_ctx_, buffersink, "out", nullptr, nullptr, graph_);
    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot create buffer sink: %d\n", ret);
        destroy();
        return false;
    }

    // Parse the filter description between src and sink
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs  = avfilter_inout_alloc();

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = src_ctx_;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = sink_ctx_;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;

    ret = avfilter_graph_parse_ptr(graph_, filter_descr.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot parse filter '%s': %d\n",
                     filter_descr.c_str(), ret);
        destroy();
        return false;
    }

    ret = avfilter_graph_config(graph_, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot configure graph: %d\n", ret);
        destroy();
        return false;
    }

    std::fprintf(stderr, "FilterGraph: video filter '%s' initialized (%dx%d fmt=%d)\n",
                 filter_descr.c_str(), width, height, static_cast<int>(pix_fmt));
    return true;
}

bool FilterGraph::init_audio(int sample_rate, const AVChannelLayout& ch_layout,
                             enum AVSampleFormat sample_fmt,
                             const std::string& filter_descr) {
    destroy();

    graph_ = avfilter_graph_alloc();
    if (!graph_) return false;

    char ch_layout_str[64] = {};
    av_channel_layout_describe(&ch_layout, ch_layout_str, sizeof(ch_layout_str));

    char args[512];
    std::snprintf(args, sizeof(args),
                  "sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                  sample_rate,
                  av_get_sample_fmt_name(sample_fmt),
                  ch_layout_str);

    const AVFilter* abuffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");

    int ret = avfilter_graph_create_filter(&src_ctx_, abuffersrc, "in", args, nullptr, graph_);
    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot create abuffer source: %d\n", ret);
        destroy();
        return false;
    }

    ret = avfilter_graph_create_filter(&sink_ctx_, abuffersink, "out", nullptr, nullptr, graph_);
    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot create abuffer sink: %d\n", ret);
        destroy();
        return false;
    }

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs  = avfilter_inout_alloc();

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = src_ctx_;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = sink_ctx_;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;

    ret = avfilter_graph_parse_ptr(graph_, filter_descr.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot parse audio filter '%s': %d\n",
                     filter_descr.c_str(), ret);
        destroy();
        return false;
    }

    ret = avfilter_graph_config(graph_, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "FilterGraph: cannot configure audio graph: %d\n", ret);
        destroy();
        return false;
    }

    std::fprintf(stderr, "FilterGraph: audio filter '%s' initialized (rate=%d fmt=%s ch=%s)\n",
                 filter_descr.c_str(), sample_rate,
                 av_get_sample_fmt_name(sample_fmt), ch_layout_str);
    return true;
}

int FilterGraph::push_frame(AVFrame* frame) {
    if (!src_ctx_) return -1;
    return av_buffersrc_add_frame_flags(src_ctx_, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
}

int FilterGraph::pull_frame(AVFrame* filtered) {
    if (!sink_ctx_) return -1;
    return av_buffersink_get_frame(sink_ctx_, filtered);
}

void FilterGraph::destroy() {
    if (graph_) {
        avfilter_graph_free(&graph_);
    }
    graph_ = nullptr;
    src_ctx_ = nullptr;
    sink_ctx_ = nullptr;
}

void FilterGraph::set_pending(const std::string& spec) {
    std::lock_guard<std::mutex> lock(spec_mutex_);
    pending_spec_ = spec;
}

std::string FilterGraph::take_pending() {
    std::lock_guard<std::mutex> lock(spec_mutex_);
    std::string spec;
    spec.swap(pending_spec_);
    return spec;
}
