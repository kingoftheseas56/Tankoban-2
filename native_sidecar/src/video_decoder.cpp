#include "video_decoder.h"
#include "d3d11_presenter.h"
#include "overlay_shm.h"
#include "filter_graph.h"
#include "gpu_renderer.h"
#include "subtitle_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#ifdef _WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif
}

#ifdef _WIN32
#include <windows.h>
#include <avrt.h>

// MMCSS RAII guard. Tells Windows scheduler this thread does timing-critical
// media work (reduces preemption + clock throttling). Released on scope exit
// so all early returns in the thread function are safe.
namespace {
class MmcssScope {
public:
    explicit MmcssScope(const wchar_t* task) {
        DWORD idx = 0;
        h_ = AvSetMmThreadCharacteristicsW(task, &idx);
    }
    ~MmcssScope() { if (h_) AvRevertMmThreadCharacteristics(h_); }
    MmcssScope(const MmcssScope&) = delete;
    MmcssScope& operator=(const MmcssScope&) = delete;
private:
    HANDLE h_ = nullptr;
};
}
#endif

// Fallback drop threshold used only when the stream's frame rate can't
// be determined (degenerate demuxer output). Matches the previous static
// value (42 ms ≈ 1 frame at 24 fps). Normal playback uses the adaptive
// threshold computed from avg_frame_rate / r_frame_rate — see
// compute_drop_threshold_us.
static constexpr int64_t FRAME_DROP_BEHIND_US_FALLBACK = 42000;

// Adaptive late-frame drop threshold. The old static 42 ms was fine for
// 24 fps (≈1 frame late) but over-permissive at 60 fps (2.5 frames late)
// and 120 fps (5 frames late) — those users see visible stutter long
// before we drop. Formula: 1.5 × frame_duration, floored at 25 ms so
// extreme high-fps content still gets a reasonable decode budget.
// At 24 fps → 62 ms; 30 fps → 50 ms; 60 fps → 25 ms; 120 fps → 25 ms
// (floor); 12 fps → 125 ms. 24 fps is slightly relaxed vs the old 42 ms
// to give decoder extra catch-up room on seek resume, where late frames
// are expected and flooding the log with drops is unhelpful.
static int64_t compute_drop_threshold_us(const AVStream* stream) {
    if (!stream) return FRAME_DROP_BEHIND_US_FALLBACK;
    AVRational r = stream->avg_frame_rate;
    if (r.den == 0 || r.num == 0) r = stream->r_frame_rate;
    if (r.den == 0 || r.num == 0) return FRAME_DROP_BEHIND_US_FALLBACK;
    const double fps = av_q2d(r);
    if (fps <= 0.0) return FRAME_DROP_BEHIND_US_FALLBACK;
    const double frame_us = 1e6 / fps;
    int64_t threshold = static_cast<int64_t>(frame_us * 1.5);
    if (threshold < 25000) threshold = 25000;
    return threshold;
}

// Hardware pixel format negotiated during hw accel init (AV_PIX_FMT_NONE = disabled)
static thread_local enum AVPixelFormat tl_hw_pix_fmt = AV_PIX_FMT_NONE;

static enum AVPixelFormat get_hw_format(AVCodecContext* /*ctx*/,
                                        const enum AVPixelFormat* pix_fmts) {
    for (const auto* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        std::fprintf(stderr, "VideoDecoder: get_hw_format offered: %s\n",
                     av_get_pix_fmt_name(*p));
        if (*p == tl_hw_pix_fmt) {
            std::fprintf(stderr, "VideoDecoder: get_hw_format -> %s (hw accel)\n",
                         av_get_pix_fmt_name(*p));
            return *p;
        }
    }
    std::fprintf(stderr, "VideoDecoder: hw format not offered, falling back to sw\n");
    return pix_fmts[0];
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_high_bit_depth(enum AVPixelFormat fmt) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    if (!desc) return false;
    // comp[0].depth > 8 means 10/12/16-bit
    return desc->nb_components > 0 && desc->comp[0].depth > 8;
}

// ---------------------------------------------------------------------------
// VideoDecoder
// ---------------------------------------------------------------------------

VideoDecoder::VideoDecoder(FrameRingWriter* ring_writer, DecoderEventCb on_event,
                           AVSyncClock* clock, int slot_bytes,
                           SubtitleRenderer* sub_renderer,
                           FilterGraph* video_filter,
                           GpuRenderer* gpu_renderer)
    : ring_writer_(ring_writer)
    , on_event_(std::move(on_event))
    , clock_(clock)
    , slot_bytes_(slot_bytes)
    , sub_renderer_(sub_renderer)
    , video_filter_(video_filter)
    , gpu_renderer_(gpu_renderer)
{}

VideoDecoder::~VideoDecoder() {
    stop();
}

void VideoDecoder::start(const std::string& path, double start_seconds,
                         int video_stream_index,
                         const std::vector<int>& sub_stream_indices) {
    stop();  // ensure previous thread is done
    stop_flag_.store(false);
    seek_pending_.store(false);
    sub_stream_indices_ = sub_stream_indices;
    running_.store(true);
    thread_ = std::thread(&VideoDecoder::decode_thread_func, this,
                          path, start_seconds, video_stream_index);
}

void VideoDecoder::stop() {
    stop_flag_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
}

void VideoDecoder::seek(double position_sec) {
    std::lock_guard<std::mutex> lock(seek_mutex_);
    seek_target_sec_ = position_sec;
    seek_pending_.store(true);
}

void VideoDecoder::set_overlay_canvas_size(int width, int height) {
    if (width <= 0 || height <= 0) return;
    overlay_canvas_w_.store(width, std::memory_order_release);
    overlay_canvas_h_.store(height, std::memory_order_release);

    std::lock_guard<std::mutex> lock(overlay_mutex_);
    if (!overlay_shm_) return;
    const bool changed = overlay_shm_->width() != width || overlay_shm_->height() != height;
    if (!overlay_shm_->resize(width, height)) return;
    if (changed && overlay_shm_->ready()) {
        char ov_info[256];
        const std::string name = overlay_shm_->name();
        std::snprintf(ov_info, sizeof(ov_info), "%s:%d:%d",
                      name.c_str(), overlay_shm_->width(), overlay_shm_->height());
        on_event_("overlay_shm", std::string(ov_info));
    }
}

void VideoDecoder::step_forward() {
    // Unpause briefly to decode one frame, then re-pause.
    // The decode loop checks step_pending_ and processes exactly one frame.
    step_pending_.store(true);
    paused_.store(false);  // wake the decode loop from its pause sleep
}

void VideoDecoder::step_backward(double current_pos_sec) {
    // Seek back ~one frame duration (~42ms for 24fps, use 50ms to be safe)
    // then step forward one frame.
    double target = current_pos_sec - 0.05;
    if (target < 0) target = 0;
    {
        std::lock_guard<std::mutex> lock(seek_mutex_);
        seek_target_sec_ = target;
        seek_pending_.store(true);
    }
    step_pending_.store(true);
    paused_.store(false);
}

// ---------------------------------------------------------------------------
// Decode thread
// ---------------------------------------------------------------------------

void VideoDecoder::decode_thread_func(
    std::string path,
    double start_seconds,
    int video_stream_index)
{
#ifdef _WIN32
    MmcssScope mmcss(L"Playback");  // released on any return
#endif
    std::fprintf(stderr, "VideoDecoder: starting decode path=%s start=%.3fs stream=%d\n",
                 path.c_str(), start_seconds, video_stream_index);

    // --- Open container ---
    //
    // STREAM_ENGINE_REBUILD P4 — three-tier HTTP probe escalation. Same
    // tier budgets as demuxer.cpp::probe_file so a failed probe tier 1
    // doesn't leave the decoder-side open stuck on the same short budget.
    // Non-HTTP paths skip escalation (single 5MB attempt).
    AVFormatContext* fmt_ctx = nullptr;
    bool is_http = path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0;

    struct OpenTier {
        int     tier;
        int64_t probesize;
        int64_t analyzeduration_us;
        int64_t rw_timeout_us;
    };
    OpenTier tiers[3];
    int tier_count = 0;
    if (is_http) {
        tiers[0] = {1,   512 * 1024,      750 * 1000,       5 * 1000 * 1000};
        tiers[1] = {2, 2 * 1024 * 1024, 2 * 1000 * 1000,   15 * 1000 * 1000};
        tiers[2] = {3, 5 * 1024 * 1024, 5 * 1000 * 1000,   30 * 1000 * 1000};
        tier_count = 3;
    } else {
        tiers[0] = {1, 5 * 1000 * 1000, 0, 0};
        tier_count = 1;
    }

    std::string last_err = "open failed";
    int chosen_tier = 0;
    int64_t chosen_probesize = 0;
    int64_t chosen_analyzedur = 0;

    for (int i = 0; i < tier_count; ++i) {
        const OpenTier& t = tiers[i];

        AVDictionary* opts = nullptr;
        char num[32];
        std::snprintf(num, sizeof(num), "%lld", (long long)t.probesize);
        av_dict_set(&opts, "probesize", num, 0);

        if (is_http) {
            av_dict_set(&opts, "reconnect", "1", 0);
            av_dict_set(&opts, "reconnect_streamed", "1", 0);
            av_dict_set(&opts, "reconnect_delay_max", "10", 0);
            av_dict_set(&opts, "timeout", "60000000", 0);
            std::snprintf(num, sizeof(num), "%lld", (long long)t.rw_timeout_us);
            av_dict_set(&opts, "rw_timeout", num, 0);
            std::snprintf(num, sizeof(num), "%lld", (long long)t.analyzeduration_us);
            av_dict_set(&opts, "analyzeduration", num, 0);
        }

        std::fprintf(stderr,
            "VideoDecoder: Tier %d attempt (probesize=%lld analyzedur=%lldus "
            "rw_timeout=%lldus)\n",
            t.tier, (long long)t.probesize, (long long)t.analyzeduration_us,
            (long long)t.rw_timeout_us);

        int ret = avformat_open_input(&fmt_ctx, path.c_str(), nullptr, &opts);
        av_dict_free(&opts);

        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::fprintf(stderr,
                "VideoDecoder: Tier %d avformat_open_input failed: %s\n",
                t.tier, errbuf);
            last_err = std::string("open:") + errbuf;
            if (fmt_ctx) { avformat_close_input(&fmt_ctx); fmt_ctx = nullptr; }
            continue;
        }

        if (is_http) {
            fmt_ctx->probesize = t.probesize;
            fmt_ctx->max_analyze_duration = t.analyzeduration_us;
        }

        ret = avformat_find_stream_info(fmt_ctx, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::fprintf(stderr,
                "VideoDecoder: Tier %d find_stream_info failed: %s\n",
                t.tier, errbuf);
            last_err = std::string("find_stream_info:") + errbuf;
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
            continue;
        }

        chosen_tier = t.tier;
        chosen_probesize = t.probesize;
        chosen_analyzedur = t.analyzeduration_us;
        std::fprintf(stderr, "VideoDecoder: Tier %d passed\n", t.tier);
        break;
    }

    if (!fmt_ctx) {
        std::fprintf(stderr,
            "VideoDecoder: all %d tier(s) exhausted (last: %s)\n",
            tier_count, last_err.c_str());
        char msg[320];
        std::snprintf(msg, sizeof(msg), "OPEN_FAILED:tier%d_exhausted:%s",
                      tier_count, last_err.c_str());
        on_event_("error", msg);
        running_.store(false);
        return;
    }

    (void)chosen_tier;
    (void)chosen_probesize;
    (void)chosen_analyzedur;

    if (video_stream_index < 0 || video_stream_index >= static_cast<int>(fmt_ctx->nb_streams)) {
        avformat_close_input(&fmt_ctx);
        on_event_("error", "UNSUPPORTED_CODEC:invalid video stream index");
        running_.store(false);
        return;
    }

    AVStream* video_stream = fmt_ctx->streams[video_stream_index];

    // Frame-rate-adaptive late-drop threshold (replaces the old fixed
    // 42 ms constant). Captured once here so the inner decode loop just
    // reads the local instead of recomputing / branching each frame.
    const int64_t frame_drop_behind_us = compute_drop_threshold_us(video_stream);
    std::fprintf(stderr, "VideoDecoder: adaptive drop threshold = %lld us "
                         "(avg_fps=%.3f r_fps=%.3f)\n",
                 static_cast<long long>(frame_drop_behind_us),
                 video_stream->avg_frame_rate.den
                     ? av_q2d(video_stream->avg_frame_rate) : 0.0,
                 video_stream->r_frame_rate.den
                     ? av_q2d(video_stream->r_frame_rate) : 0.0);

    // --- Open codec ---
    const AVCodec* codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        on_event_("error", "DECODE_INIT_FAILED:no decoder found");
        running_.store(false);
        return;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);

    // --- Hardware-accelerated decode (D3D11VA on Windows) ---
    // Must configure threading BEFORE avcodec_open2. When FF_THREAD_FRAME is
    // active, ffmpeg creates multiple internal codec contexts and excludes
    // hardware pixel formats from get_format — D3D11VA never gets offered.
    // GPU handles parallelism internally, so slice threading is sufficient.
    AVBufferRef* hw_device_ctx = nullptr;
    bool hw_accel_active = false;
#ifdef _WIN32
    if (!hwaccel_disabled_) {
        int hw_ret = av_hwdevice_ctx_create(&hw_device_ctx,
                                            AV_HWDEVICE_TYPE_D3D11VA,
                                            nullptr, nullptr, 0);
        if (hw_ret == 0) {
            codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
            tl_hw_pix_fmt = AV_PIX_FMT_D3D11;
            codec_ctx->get_format = get_hw_format;
            // Disable frame threading — incompatible with hw accel.
            // Slice threading still provides CPU-side parallelism for
            // packet parsing while the GPU does the heavy decode.
            codec_ctx->thread_count = 1;
            codec_ctx->thread_type  = FF_THREAD_SLICE;
            hw_accel_active = true;
            std::fprintf(stderr, "VideoDecoder: D3D11VA hw accel enabled (frame threading disabled)\n");
        } else {
            std::fprintf(stderr, "VideoDecoder: D3D11VA init failed (%d), using software decode\n", hw_ret);
        }
    } else {
        std::fprintf(stderr, "VideoDecoder: hwaccel disabled by user preference\n");
    }
#endif

    // Multi-threaded software decode fallback
    if (!hw_accel_active) {
        codec_ctx->thread_count = 0;  // auto
        codec_ctx->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
    hw_accel_active_.store(hw_accel_active, std::memory_order_relaxed);

    // --- D3D11 Holy Grail presenter (shared texture for zero-copy GPU path) ---
#ifdef _WIN32
    D3D11Presenter* d3d_presenter = nullptr;
    if (hw_accel_active && hw_device_ctx) {
        auto* hw_ctx = reinterpret_cast<AVHWDeviceContext*>(hw_device_ctx->data);
        auto* d3d_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(hw_ctx->hwctx);
        if (d3d_ctx && d3d_ctx->device) {
            d3d_presenter = new D3D11Presenter();
            int probe_w = video_stream->codecpar->width;
            int probe_h = video_stream->codecpar->height;
            if (probe_w > 0 && probe_h > 0 && d3d_presenter->init(d3d_ctx->device, probe_w, probe_h)) {
                std::fprintf(stderr, "VideoDecoder: D3D11 Holy Grail presenter ready (%dx%d handle=%p)\n",
                             probe_w, probe_h, d3d_presenter->nt_handle());
            } else {
                std::fprintf(stderr, "VideoDecoder: D3D11 presenter init failed, using SHM path\n");
                delete d3d_presenter;
                d3d_presenter = nullptr;
            }
        }
    }

    // If HW accel didn't activate, create a standalone D3D11 presenter
    // so SW-decoded files can still use the D3D11 shared texture canvas.
    if (!d3d_presenter) {
        int probe_w = video_stream->codecpar->width;
        int probe_h = video_stream->codecpar->height;
        if (probe_w > 0 && probe_h > 0) {
            d3d_presenter = new D3D11Presenter();
            if (d3d_presenter->init_standalone(probe_w, probe_h)) {
                std::fprintf(stderr, "VideoDecoder: D3D11 standalone presenter for SW decode (%dx%d handle=%p)\n",
                             probe_w, probe_h, d3d_presenter->nt_handle());
            } else {
                std::fprintf(stderr, "VideoDecoder: D3D11 standalone init failed, SHM-only path\n");
                delete d3d_presenter;
                d3d_presenter = nullptr;
            }
        }
    }

    // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — subtitle overlay SHM.
    // Replaces the reverted cross-process D3D11 shared-texture attempt
    // (which stalled main-app draws due to no-keyed-mutex sync). SHM
    // carries the CPU-rendered overlay BGRA bytes; main-app uploads them
    // into a locally-owned D3D11 texture — no cross-process GPU resource
    // sharing, no sync pitfalls. Only allocated when d3d_presenter is
    // ready (zero-copy path available); otherwise subtitles still come
    // baked into the slow-path SHM BGRA frame.
    {
        std::lock_guard<std::mutex> overlay_lock(overlay_mutex_);
        overlay_shm_.reset();
    }
    if (d3d_presenter && d3d_presenter->ready()) {
        int probe_w = video_stream->codecpar->width;
        int probe_h = video_stream->codecpar->height;
        const int canvas_w = overlay_canvas_w_.load(std::memory_order_acquire);
        const int canvas_h = overlay_canvas_h_.load(std::memory_order_acquire);
        const int overlay_w = canvas_w > 0 ? canvas_w : probe_w;
        const int overlay_h = canvas_h > 0 ? canvas_h : probe_h;
        if (overlay_w > 0 && overlay_h > 0) {
            std::lock_guard<std::mutex> overlay_lock(overlay_mutex_);
            overlay_shm_ = std::make_unique<OverlayShm>();
            if (!overlay_shm_->create(overlay_w, overlay_h)) {
                std::fprintf(stderr, "VideoDecoder: overlay SHM create failed, subtitle fast path disabled\n");
                overlay_shm_.reset();
            }
        }
    }
#endif

    int ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::fprintf(stderr, "VideoDecoder: avcodec_open2 failed: %s\n", errbuf);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        on_event_("error", std::string("DECODE_INIT_FAILED:") + errbuf);
        running_.store(false);
        return;
    }

    std::fprintf(stderr, "VideoDecoder: codec=%s %dx%d pix_fmt=%s\n",
                 codec->name, codec_ctx->width, codec_ctx->height,
                 av_get_pix_fmt_name(codec_ctx->pix_fmt));

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — decoder_open_done event.
    // Fires after avcodec_open2 succeeds; the decoder is fully initialized
    // and about to seek (if start_seconds > 0.01) then enter the read loop.
    // Main-side on_video_event lambda computes t_ms_from_open from its
    // captured open_start_time anchor.
    on_event_("decoder_open_done", "");

    // --- Initial seek ---
    if (start_seconds > 0.01) {
        int64_t ts = static_cast<int64_t>(start_seconds * AV_TIME_BASE);
        av_seek_frame(fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec_ctx);
    }

    // --- Allocate frames ---
    AVFrame* frame     = av_frame_alloc();
    AVFrame* bgra_frame = av_frame_alloc();
    AVPacket* pkt      = av_packet_alloc();

    // BGRA frame buffer — allocated lazily on first decoded frame
    // (actual dimensions may differ from codec_ctx due to cropping)
    int bgra_w = 0, bgra_h = 0;

    // swscale contexts — (re)created when source dimensions or format change
    SwsContext* sws_ctx      = nullptr;
    SwsContext* sws_10bit    = nullptr;
    AVFrame*    intermediate = nullptr;
    enum AVPixelFormat last_src_fmt = AV_PIX_FMT_NONE;
    int last_src_w = 0, last_src_h = 0;

    bool first_frame_fired = false;
    int64_t frames_written = 0;
    int64_t frames_dropped = 0;

    // [PERF] 1 Hz per-phase timing accumulator. Samples are kept for at most
    // one second, then p50/p99 is emitted to stderr and the buffers clear.
    // Zero-alloc per frame (emplace_back into reserved vectors). Kept simple
    // — we're after order-of-magnitude answers, not microsecond fidelity.
    std::vector<double> perf_blend_ms;     perf_blend_ms.reserve(120);
    std::vector<double> perf_present_ms;   perf_present_ms.reserve(120);
    std::vector<double> perf_total_ms;     perf_total_ms.reserve(120);
    int64_t perf_drops_window = 0;
    auto perf_window_start = std::chrono::steady_clock::now();

    auto perf_percentile = [](std::vector<double>& v, double pct) -> double {
        if (v.empty()) return 0.0;
        size_t idx = static_cast<size_t>(pct * (v.size() - 1));
        std::nth_element(v.begin(), v.begin() + idx, v.end());
        return v[idx];
    };

    // HTTP streaming stall detection
    bool buffering_emitted = false;
    int stall_count = 0;

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — first-milestone gate flags.
    // These latch true on the first successful av_read_frame and the first
    // successful avcodec_receive_frame respectively, so the event fires
    // exactly once per decode session. Event routing is through on_event_
    // to main.cpp's on_video_event lambda, which adds t_ms_from_open +
    // session scoping + writes the final JSON event to stdout.
    bool first_packet_read_fired = false;
    bool first_decoder_receive_fired = false;

#ifdef _WIN32
    auto overlay_shm_snapshot = [this]() -> OverlayShm* {
        std::lock_guard<std::mutex> overlay_lock(overlay_mutex_);
        return overlay_shm_.get();
    };

    auto write_overlay_frame = [&](bool sub_blend_needed, int64_t pts_ms) -> bool {
        OverlayShm* shm = overlay_shm_snapshot();
        if (!shm || !shm->ready()) return false;

        const int ov_w = shm->width();
        const int ov_h = shm->height();
        if (ov_w <= 0 || ov_h <= 0) return false;

        if (sub_blend_needed && sub_renderer_) {
            static thread_local std::vector<uint8_t> overlay_frame;
            const size_t ov_bytes = static_cast<size_t>(ov_w) * ov_h * 4;
            if (overlay_frame.size() != ov_bytes) {
                overlay_frame.assign(ov_bytes, 0);
            } else {
                std::memset(overlay_frame.data(), 0, ov_bytes);
            }

            if (sub_renderer_->bitmap_subtitles_active()) {
                static thread_local std::vector<SubtitleRenderer::SubOverlayBitmap> tiles;
                sub_renderer_->render_to_bitmaps(pts_ms, tiles);
                SubtitleRenderer::blend_into_frame(
                    tiles, overlay_frame.data(), ov_w, ov_h, ov_w * 4);
            } else {
                sub_renderer_->render_blend(
                    overlay_frame.data(), ov_w, ov_h, ov_w * 4, pts_ms);
            }
            shm->write(overlay_frame.data());
        } else {
            shm->write_empty();
        }
        return true;
    };
#endif

    // --- Frame processing helper (shared by normal loop and EOF drain) ---
    // Uses actual decoded frame dimensions (not codec_ctx) to match Python sidecar.
    // Returns true if the frame was processed, false if stop was requested.
    auto process_frame = [&](AVFrame* raw_frame) -> bool {
        // Actual decoded dimensions (may differ from codec_ctx due to cropping)
        int fw = raw_frame->width;
        int fh = raw_frame->height;
        int fstride = fw * 4;  // BGRA

        // Compute PTS in microseconds
        int64_t pts_us = 0;
        if (raw_frame->pts != AV_NOPTS_VALUE) {
            AVRational tb = video_stream->time_base;
            pts_us = av_rescale_q(raw_frame->pts, tb, {1, 1000000});
        }

        // Pre-start skip: when resuming mid-file, the seek lands on a
        // keyframe BEFORE the target. Drop those early frames — they'd
        // display with no matching audio and cause a perceived A/V gap.
        // Allow a small 200ms tolerance so we don't cut the first visible frame.
        if (!first_frame_fired && start_seconds > 0.1) {
            int64_t start_us = static_cast<int64_t>(start_seconds * 1e6);
            if (pts_us < start_us - 200000) {
                std::fprintf(stderr,
                    "AVSYNC_DIAG video_skip_prestart pts=%.3fs target=%.3fs\n",
                    pts_us / 1e6, start_seconds);
                return true;  // frame processed (skipped), keep going
            }
        }

        // Late-frame drop: skip expensive color conversion for frames
        // already behind the A/V clock (decode overshot budget).
        // Threshold is frame-rate-adaptive (see compute_drop_threshold_us):
        // 24 fps → 62 ms, 60 fps → 25 ms, 120 fps → 25 ms (floor).
        if (first_frame_fired && clock_ && clock_->started()) {
            int64_t behind_us = clock_->position_us() - pts_us;
            if (behind_us > frame_drop_behind_us) {
                ++frames_dropped;
                if (frames_dropped <= 5 || frames_dropped % 30 == 0) {
                    std::fprintf(stderr,
                        "VideoDecoder: dropped late frame pts=%.3fs clock=%.3fs behind=%.0fms (total=%lld)\n",
                        pts_us / 1e6, clock_->position_us() / 1e6,
                        behind_us / 1e3, static_cast<long long>(frames_dropped));
                }
                return true;
            }
        }

        // Guard: skip frames that exceed SHM slot size
        int data_len = fstride * fh;
        if (slot_bytes_ > 0 && data_len > slot_bytes_) {
            std::fprintf(stderr, "VideoDecoder: frame %dx%d (%d bytes) exceeds slot_bytes %d, skipping\n",
                         fw, fh, data_len, slot_bytes_);
            return true;
        }

        // --- D3D11 Holy Grail: copy decoded frame to shared texture (GPU→GPU) ---
        bool d3d_gpu_copied = false;
#ifdef _WIN32
        if (d3d_presenter && hw_accel_active && raw_frame->format == tl_hw_pix_fmt) {
            auto* src_tex = reinterpret_cast<ID3D11Texture2D*>(raw_frame->data[0]);
            int tex_index = static_cast<int>(reinterpret_cast<intptr_t>(raw_frame->data[1]));
            if (src_tex) {
                d3d_gpu_copied = d3d_presenter->present_slice(src_tex, tex_index);
                if (!first_frame_fired && d3d_gpu_copied) {
                    std::fprintf(stderr, "HOLY_GRAIL: first frame copied to shared texture (slice=%d)\n",
                                 tex_index);
                }
            }
        }
#endif

        // ── Zero-copy short-circuit ──────────────────────────────────────
        // When Qt is consuming via the imported D3D11 shared texture AND we
        // did the GPU→GPU copy, skip the CPU pipeline (hwframe_transfer +
        // sws_scale + SHM write). The shared texture already has the new
        // frame; consumer's next vsync renders it. Producer per-frame cost
        // drops from ~20ms to ~1ms.
        //
        // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — sub_blend_needed no
        // longer blocks fast_path when overlay_shm is ready. Subtitle
        // bitmaps render into overlay SHM; main-app uploads to its local
        // D3D11 texture and draws an alpha-blended overlay quad after the
        // video quad. HEVC 10-bit + subs stays on zero-copy fast path
        // without cross-process GPU sync (the 3.B D3D11-shared-texture
        // approach was reverted because it stalled main-app draws).
        bool sub_blend_needed = sub_renderer_ && sub_renderer_->visible()
                                && active_sub_stream_.load() >= 0;
        bool overlay_ready = false;
#ifdef _WIN32
        if (OverlayShm* shm = overlay_shm_snapshot()) {
            overlay_ready = shm->ready();
        }
#endif
        bool fast_path = zero_copy_active_.load() && d3d_gpu_copied
#ifdef _WIN32
                         && (!sub_blend_needed || overlay_ready)
#else
                         && !sub_blend_needed
#endif
                         ;

        if (fast_path) {
            // Wait for audio clock to start (same as full path)
            if (clock_ && !clock_->started()) {
                auto clock_deadline = std::chrono::steady_clock::now()
                                    + std::chrono::seconds(10);
                while (!stop_flag_.load() && !clock_->started()) {
                    if (std::chrono::steady_clock::now() >= clock_deadline) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (stop_flag_.load()) return false;
            }

            // A/V sync wait — same algorithm as the full path
            if (clock_ && clock_->started()) {
                while (paused_.load() && !stop_flag_.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (stop_flag_.load()) return false;

                constexpr int64_t SYNC_TOLERANCE_US = 15000;
                constexpr double  MAX_WAIT_SEC      = 0.5;
                auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds((int)(MAX_WAIT_SEC * 1000));
                while (!stop_flag_.load()) {
                    int64_t ahead_us = pts_us - clock_->position_us();
                    if (ahead_us <= SYNC_TOLERANCE_US) break;
                    if (std::chrono::steady_clock::now() >= deadline) break;
                    double wait_sec = std::min(ahead_us / 1e6 * 0.5, 0.02);
                    std::this_thread::sleep_for(std::chrono::microseconds(
                        (int64_t)(wait_sec * 1e6)));
                }
                if (stop_flag_.load()) return false;
            } else {
                while (paused_.load() && !stop_flag_.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (stop_flag_.load()) return false;
            }

            // Write audio clock to SHM header (consumer reads this for diagnostics)
            if (clock_ && clock_->started())
                ring_writer_->write_clock_us(clock_->position_us());

            // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay write.
            // When subs are active, render bitmaps into a frame-sized BGRA
            // buffer and push to overlay_shm. Main-app uploads to its
            // local D3D11 texture and composites as a separate draw.
#ifdef _WIN32
            write_overlay_frame(sub_blend_needed, pts_us / 1000);
#endif

            ++frames_written;

            // Frame stepping: re-pause after one frame
            if (step_pending_.load()) {
                step_pending_.store(false);
                paused_.store(true);
                char step_info[64];
                std::snprintf(step_info, sizeof(step_info), "%.6f", pts_us / 1e6);
                on_event_("frame_stepped", std::string(step_info));
            }

            // first_frame event (in case zero-copy was already on at frame 1,
            // which happens on subsequent files within the same sidecar session)
            if (!first_frame_fired) {
                first_frame_fired = true;
                char info[256];
                std::snprintf(info, sizeof(info), "%d:%d:%s:%lld:1",
                              fw, fh, codec->name, (long long)pts_us);
                on_event_("first_frame", std::string(info));
#ifdef _WIN32
                if (d3d_presenter && d3d_presenter->ready()) {
                    char d3d_info[128];
                    std::snprintf(d3d_info, sizeof(d3d_info), "%llu:%u:%u",
                                  (unsigned long long)(uintptr_t)d3d_presenter->nt_handle(),
                                  d3d_presenter->width(), d3d_presenter->height());
                    on_event_("d3d11_texture", std::string(d3d_info));
                }
                // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — emit overlay
                // SHM name + dims for main-app to open and upload from.
                {
                    std::lock_guard<std::mutex> overlay_lock(overlay_mutex_);
                if (overlay_shm_ && overlay_shm_->ready()) {
                    char ov_info[256];
                    const std::string name = overlay_shm_->name();
                    std::snprintf(ov_info, sizeof(ov_info), "%s:%d:%d",
                                  name.c_str(),
                                  overlay_shm_->width(), overlay_shm_->height());
                    on_event_("overlay_shm", std::string(ov_info));
                }
                }
#endif
            }
            return true;
        }

        // Transfer hw frame to CPU (SHM path — still needed until Qt consumes shared texture)
        AVFrame* sw_frame = nullptr;
        if (hw_accel_active && raw_frame->format == tl_hw_pix_fmt) {
            sw_frame = av_frame_alloc();
            int tr = av_hwframe_transfer_data(sw_frame, raw_frame, 0);
            if (tr < 0) {
                std::fprintf(stderr, "VideoDecoder: hw frame transfer failed (%d)\n", tr);
                av_frame_free(&sw_frame);
                return true;
            }
            sw_frame->pts = raw_frame->pts;
            raw_frame = sw_frame;
            fw = raw_frame->width;
            fh = raw_frame->height;
            fstride = fw * 4;
            data_len = fstride * fh;
        }

        // Re-allocate BGRA output buffer if frame dimensions changed
        if (fw != bgra_w || fh != bgra_h) {
            if (bgra_frame->data[0]) av_freep(&bgra_frame->data[0]);
            bgra_frame->format = AV_PIX_FMT_BGRA;
            bgra_frame->width  = fw;
            bgra_frame->height = fh;
            av_image_alloc(bgra_frame->data, bgra_frame->linesize,
                           fw, fh, AV_PIX_FMT_BGRA, 32);
            bgra_w = fw;
            bgra_h = fh;
            std::fprintf(stderr, "VideoDecoder: BGRA buffer (re)allocated %dx%d\n", fw, fh);
        }

        // Determine source pixel format
        enum AVPixelFormat src_fmt = static_cast<enum AVPixelFormat>(raw_frame->format);

        // 10-bit two-step conversion: 10bit -> yuv420p -> bgra
        // Skip this step when GPU renderer is active — libplacebo handles
        // 10-bit natively and needs the full precision for HDR tone mapping.
        AVFrame* convert_src = raw_frame;
        AVFrame* pre_10bit_src = raw_frame;  // preserved for GPU path
        bool skip_10bit_for_gpu = is_high_bit_depth(src_fmt)
                                  && gpu_renderer_ && gpu_renderer_->active();

        if (is_high_bit_depth(src_fmt) && !skip_10bit_for_gpu) {
            // Re-allocate intermediate if dimensions changed
            if (!intermediate || intermediate->width != fw || intermediate->height != fh) {
                if (intermediate) {
                    av_freep(&intermediate->data[0]);
                    av_frame_free(&intermediate);
                }
                intermediate = av_frame_alloc();
                intermediate->format = AV_PIX_FMT_YUV420P;
                intermediate->width  = fw;
                intermediate->height = fh;
                av_image_alloc(intermediate->data, intermediate->linesize,
                               fw, fh, AV_PIX_FMT_YUV420P, 32);
            }

            // Re-create sws_10bit if format or dimensions changed
            if (!sws_10bit || src_fmt != last_src_fmt
                || raw_frame->width != last_src_w || raw_frame->height != last_src_h) {
                if (sws_10bit) sws_freeContext(sws_10bit);
                sws_10bit = sws_getContext(
                    raw_frame->width, raw_frame->height, src_fmt,
                    fw, fh, AV_PIX_FMT_YUV420P,
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            }

            if (sws_10bit) {
                sws_scale(sws_10bit,
                          raw_frame->data, raw_frame->linesize, 0, raw_frame->height,
                          intermediate->data, intermediate->linesize);
                convert_src = intermediate;
                src_fmt = AV_PIX_FMT_YUV420P;
            }
        }

        // Lazy-init video filter graph with actual frame parameters
        if (video_filter_ && !video_filter_->active()) {
            auto spec = video_filter_->take_pending();
            if (!spec.empty()) {
                AVRational tb = video_stream->time_base;
                AVRational sar = video_stream->codecpar->sample_aspect_ratio;
                if (sar.num <= 0 || sar.den <= 0) { sar.num = 1; sar.den = 1; }
                video_filter_->init_video(convert_src->width, convert_src->height,
                                          src_fmt, tb, sar, spec);
            }
        }

        // Apply video filter graph if active
        AVFrame* filtered_video = nullptr;
        if (video_filter_ && video_filter_->active()) {
            int fret = video_filter_->push_frame(convert_src);
            if (fret >= 0) {
                filtered_video = av_frame_alloc();
                fret = video_filter_->pull_frame(filtered_video);
                if (fret >= 0) {
                    convert_src = filtered_video;
                    src_fmt = static_cast<enum AVPixelFormat>(filtered_video->format);
                } else {
                    av_frame_free(&filtered_video);
                    filtered_video = nullptr;
                    // Filter is buffering (e.g. yadif needs 2 frames) — skip
                    // this frame rather than displaying unfiltered content.
                    if (sw_frame) av_frame_free(&sw_frame);
                    return true;
                }
            }
        }

        // Final conversion: src -> BGRA
        bool gpu_ok = false;
        if (gpu_renderer_ && gpu_renderer_->active()) {
            // Use original 10-bit frame for GPU path (libplacebo handles conversion)
            AVFrame* gpu_src = skip_10bit_for_gpu ? pre_10bit_src : convert_src;
            gpu_ok = gpu_renderer_->render_frame(
                gpu_src, bgra_frame->data[0], fw, fh, fw * 4);
        }

        if (!gpu_ok) {
            // Software fallback: sws_scale
            if (!sws_ctx || src_fmt != last_src_fmt
                || convert_src->width != last_src_w || convert_src->height != last_src_h) {
                if (sws_ctx) sws_freeContext(sws_ctx);
                sws_ctx = sws_getContext(
                    convert_src->width, convert_src->height, src_fmt,
                    fw, fh, AV_PIX_FMT_BGRA,
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                last_src_fmt = src_fmt;
                last_src_w = convert_src->width;
                last_src_h = convert_src->height;
            }

            if (!sws_ctx) {
                std::fprintf(stderr, "VideoDecoder: sws_getContext failed\n");
                if (filtered_video) av_frame_free(&filtered_video);
                if (sw_frame) av_frame_free(&sw_frame);
                return false;
            }

            sws_scale(sws_ctx,
                       convert_src->data, convert_src->linesize, 0, convert_src->height,
                       bgra_frame->data, bgra_frame->linesize);
        }

        if (filtered_video) av_frame_free(&filtered_video);

        // Handle stride padding: if libswscale's output linesize differs from
        // our expected stride (fw*4), copy row-by-row to strip padding.
        const uint8_t* write_ptr = bgra_frame->data[0];
        int av_stride = bgra_frame->linesize[0];

        if (!gpu_ok && av_stride != fstride && av_stride > fstride) {
            static thread_local std::vector<uint8_t> compact_buf;
            compact_buf.resize(static_cast<size_t>(data_len));
            for (int row = 0; row < fh; ++row) {
                std::memcpy(compact_buf.data() + row * fstride,
                            bgra_frame->data[0] + row * av_stride,
                            static_cast<size_t>(fstride));
            }
            write_ptr = compact_buf.data();
        }

        // Wait for audio clock to start before first frame.
        // 10s timeout: Bluetooth audio devices (AirPods etc.) can take 3-5s
        // for PortAudio cold start. The old 2s timeout caused video to race
        // ahead of audio on slow audio init.
        if (clock_ && !clock_->started()) {
            if (!first_frame_fired) {
                std::fprintf(stderr, "AVSYNC_DIAG video_waiting_for_clock pts=%.3fs\n",
                             pts_us / 1e6);
            }
            auto clock_deadline = std::chrono::steady_clock::now()
                                + std::chrono::seconds(10);
            while (!stop_flag_.load() && !clock_->started()) {
                if (std::chrono::steady_clock::now() >= clock_deadline) {
                    std::fprintf(stderr,
                        "AVSYNC_DIAG video_clock_wait_TIMEOUT (2s, no audio clock)\n");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (stop_flag_.load()) { if (sw_frame) av_frame_free(&sw_frame); return false; }
        }

        // A/V sync: wait until audio clock catches up to this frame's PTS.
        if (clock_ && clock_->started()) {
            while (paused_.load() && !stop_flag_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (stop_flag_.load()) { if (sw_frame) av_frame_free(&sw_frame); return false; }

            constexpr int64_t SYNC_TOLERANCE_US = 15000;  // 15ms
            constexpr double  MAX_WAIT_SEC      = 0.5;
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(static_cast<int>(MAX_WAIT_SEC * 1000));

            while (!stop_flag_.load()) {
                int64_t ahead_us = pts_us - clock_->position_us();
                if (ahead_us <= SYNC_TOLERANCE_US) break;
                if (std::chrono::steady_clock::now() >= deadline) break;
                double wait_sec = std::min(ahead_us / 1e6 * 0.5, 0.02);
                std::this_thread::sleep_for(std::chrono::microseconds(
                    static_cast<int64_t>(wait_sec * 1e6)));
            }
            if (stop_flag_.load()) { if (sw_frame) av_frame_free(&sw_frame); return false; }
        } else {
            // No clock — just handle pause
            while (paused_.load() && !stop_flag_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (stop_flag_.load()) { if (sw_frame) av_frame_free(&sw_frame); return false; }
        }

        // [PERF] Phase timestamps — start AFTER the A/V sync wait so the
        // measurement reflects real work, not clock-wait dwell.
        const auto perf_t0 = std::chrono::steady_clock::now();

        // Prefer the canvas-sized overlay plane whenever it exists. Baking
        // canvas-coordinate subtitles into a 1920x806 video frame clips the
        // black-bar subtitles before the main app can composite them.
        bool wrote_overlay_subs = false;
#ifdef _WIN32
        const bool slow_sub_blend_needed = sub_renderer_ && sub_renderer_->visible()
                                           && active_sub_stream_.load() >= 0;
        wrote_overlay_subs = write_overlay_frame(slow_sub_blend_needed, pts_us / 1000);
#endif
        if (sub_renderer_ && !wrote_overlay_subs) {
            int64_t pts_ms = pts_us / 1000;
            uint8_t* blend_target = const_cast<uint8_t*>(write_ptr);
            sub_renderer_->render_blend(blend_target, fw, fh, fstride, pts_ms);
        }
        const auto perf_t1 = std::chrono::steady_clock::now();

        // --- D3D11 Holy Grail: upload CPU frame to shared texture (SW path) ---
#ifdef _WIN32
        if (d3d_presenter && d3d_presenter->ready() && !d3d_gpu_copied) {
            d3d_presenter->present_cpu(write_ptr, fw, fh, fstride);
        }
#endif

        // Write audio clock position to SHM for display-side pacing
        if (clock_ && clock_->started()) {
            ring_writer_->write_clock_us(clock_->position_us());
        }

        // Write to ring buffer with ACTUAL frame dimensions
        int64_t fid = ring_writer_->write_frame(
            write_ptr, data_len,
            fw, fh, fstride,
            pts_us);

        const auto perf_t2 = std::chrono::steady_clock::now();

        ++frames_written;

        // [PERF] Record this frame's timings and flush once per second.
        {
            const double blend_ms = std::chrono::duration<double, std::milli>(
                perf_t1 - perf_t0).count();
            const double present_ms = std::chrono::duration<double, std::milli>(
                perf_t2 - perf_t1).count();
            const double total_ms = std::chrono::duration<double, std::milli>(
                perf_t2 - perf_t0).count();
            perf_blend_ms.push_back(blend_ms);
            perf_present_ms.push_back(present_ms);
            perf_total_ms.push_back(total_ms);

            const auto elapsed = std::chrono::duration<double>(
                perf_t2 - perf_window_start).count();
            if (elapsed >= 1.0 && !perf_blend_ms.empty()) {
                const double blend_p50   = perf_percentile(perf_blend_ms, 0.50);
                const double blend_p99   = perf_percentile(perf_blend_ms, 0.99);
                const double present_p50 = perf_percentile(perf_present_ms, 0.50);
                const double present_p99 = perf_percentile(perf_present_ms, 0.99);
                const double total_p50   = perf_percentile(perf_total_ms, 0.50);
                const double total_p99   = perf_percentile(perf_total_ms, 0.99);
                std::fprintf(stderr,
                    "[PERF] frames=%zu drops=%lld/s "
                    "blend p50/p99=%.2f/%.2f ms "
                    "present p50/p99=%.2f/%.2f ms "
                    "total p50/p99=%.2f/%.2f ms\n",
                    perf_total_ms.size(),
                    static_cast<long long>(frames_dropped - perf_drops_window),
                    blend_p50, blend_p99,
                    present_p50, present_p99,
                    total_p50, total_p99);
                perf_blend_ms.clear();
                perf_present_ms.clear();
                perf_total_ms.clear();
                perf_drops_window = frames_dropped;
                perf_window_start = perf_t2;
            }
        }

        // Frame stepping: after writing one frame, re-pause and emit event
        if (step_pending_.load()) {
            step_pending_.store(false);
            paused_.store(true);
            char step_info[64];
            std::snprintf(step_info, sizeof(step_info), "%.6f", pts_us / 1e6);
            on_event_("frame_stepped", std::string(step_info));
        }

        if (!first_frame_fired) {
            first_frame_fired = true;

            char info[256];
            std::snprintf(info, sizeof(info), "%d:%d:%s:%lld:%lld",
                          fw, fh, codec->name,
                          static_cast<long long>(pts_us),
                          static_cast<long long>(fid));
            on_event_("first_frame", std::string(info));

            // Emit D3D11 shared texture handle if Holy Grail is active
#ifdef _WIN32
            if (d3d_presenter && d3d_presenter->ready()) {
                char d3d_info[128];
                std::snprintf(d3d_info, sizeof(d3d_info), "%llu:%u:%u",
                              static_cast<unsigned long long>(
                                  reinterpret_cast<uintptr_t>(d3d_presenter->nt_handle())),
                              d3d_presenter->width(),
                              d3d_presenter->height());
                std::fprintf(stderr, "HOLY_GRAIL: emitting d3d11_texture event: %s\n", d3d_info);
                on_event_("d3d11_texture", std::string(d3d_info));
            }
            // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay handle
            // emission on the slow-path first_frame site too, so main-app
            // has the overlay SHM name if fast path activates later.
            {
                std::lock_guard<std::mutex> overlay_lock(overlay_mutex_);
            if (overlay_shm_ && overlay_shm_->ready()) {
                char ov_info[256];
                const std::string name = overlay_shm_->name();
                std::snprintf(ov_info, sizeof(ov_info), "%s:%d:%d",
                              name.c_str(),
                              overlay_shm_->width(), overlay_shm_->height());
                std::fprintf(stderr, "HOLY_GRAIL: emitting overlay_shm event: %s\n", ov_info);
                on_event_("overlay_shm", std::string(ov_info));
            }
            }
#endif
            std::fprintf(stderr, "VideoDecoder: first frame %dx%d pts=%lldus fid=%lld\n",
                         fw, fh,
                         static_cast<long long>(pts_us),
                         static_cast<long long>(fid));
        }

        if (sw_frame) av_frame_free(&sw_frame);
        return true;
    };

    // --- Decode loop ---
    while (!stop_flag_.load()) {
        // Check for pending seek
        if (seek_pending_.load()) {
            double seek_sec;
            {
                std::lock_guard<std::mutex> lock(seek_mutex_);
                seek_sec = seek_target_sec_;
                seek_pending_.store(false);
            }
            int64_t ts = static_cast<int64_t>(seek_sec * AV_TIME_BASE);
            av_seek_frame(fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec_ctx);
            std::fprintf(stderr, "VideoDecoder: seeked to %.3fs\n", seek_sec);
            continue;
        }

        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // Drain remaining frames from the codec
                avcodec_send_packet(codec_ctx, nullptr);
                while (!stop_flag_.load() &&
                       avcodec_receive_frame(codec_ctx, frame) == 0) {
                    process_frame(frame);
                    av_frame_unref(frame);
                }
                if (!stop_flag_.load()) {
                    std::fprintf(stderr, "VideoDecoder: EOF reached\n");
                    on_event_("eof", "");
                }
                break;
            } else if (is_http && (ret == AVERROR(EAGAIN) || ret == AVERROR(ETIMEDOUT)
                                   || ret == AVERROR(EIO) || ret == AVERROR_EXIT)) {
                // HTTP stall — torrent piece not ready yet, retry
                if (!buffering_emitted) {
                    on_event_("buffering", "");
                    buffering_emitted = true;
                    std::fprintf(stderr, "VideoDecoder: HTTP stall, entering buffering state\n");
                }
                ++stall_count;
                if (stall_count > 60) {
                    std::fprintf(stderr, "VideoDecoder: HTTP stall timeout (30s), giving up\n");
                    on_event_("error", "STREAM_TIMEOUT:no data for 30 seconds");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            } else {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                std::fprintf(stderr, "VideoDecoder: av_read_frame error: %s\n", errbuf);
                on_event_("error", std::string("DECODE_FAILED:") + errbuf);
                break;
            }
        }
        // Successful read — clear buffering state
        if (buffering_emitted) {
            on_event_("playing", "");
            buffering_emitted = false;
            stall_count = 0;
            std::fprintf(stderr, "VideoDecoder: HTTP stall resolved, resuming playback\n");
        }

        // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — first_packet_read event.
        // Signals that demuxer + I/O (network or disk) are flowing. Fires
        // once per decode session on the very first successful av_read_frame
        // return (any stream — video / audio / subtitle). Detail format:
        // "<stream_index>:<pkt_size>" — main-side lambda parses into payload
        // fields and adds t_ms_from_open.
        if (!first_packet_read_fired) {
            first_packet_read_fired = true;
            char info[64];
            std::snprintf(info, sizeof(info), "%d:%d",
                          pkt->stream_index, pkt->size);
            on_event_("first_packet_read", std::string(info));
        }

        // Intercept subtitle packets — feed to libass via SubtitleRenderer
        if (pkt->stream_index != video_stream_index) {
            int active_sub = active_sub_stream_.load();
            if (active_sub >= 0 && pkt->stream_index == active_sub && sub_renderer_) {
                AVStream* ss = fmt_ctx->streams[pkt->stream_index];
                int64_t s_ms = av_rescale_q(pkt->pts, ss->time_base, {1, 1000});
                int64_t d_ms = av_rescale_q(pkt->duration, ss->time_base, {1, 1000});
                // Feed raw packet to libass (process_packet runs on decode thread,
                // render_blend runs on the subtitle render thread — matching MPV/VLC)
                sub_renderer_->process_packet(pkt->data, pkt->size, s_ms, d_ms);
            }
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codec_ctx, pkt);
        av_packet_unref(pkt);
        // Batch 6.3 (Player Polish Phase 6) — non-fatal: corrupt/unsupported
        // packet, codec reports error but can keep going. Emit decode_error
        // event for main-app toast and skip this packet (continue).
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::fprintf(stderr, "VideoDecoder: avcodec_send_packet error: %s\n", errbuf);
            on_event_("decode_error", std::string("DECODE_SKIP_PACKET:") + errbuf);
            continue;
        }

        while (!stop_flag_.load()) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                // Batch 6.3 — receive_frame error without EAGAIN/EOF: drop
                // this frame, let next packet drive recovery. Same class
                // as send_packet error above.
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                std::fprintf(stderr, "VideoDecoder: avcodec_receive_frame error: %s\n", errbuf);
                on_event_("decode_error", std::string("DECODE_SKIP_FRAME:") + errbuf);
                break;
            }

            // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — first_decoder_receive
            // event. Honest "decoder is actually making forward progress"
            // signal — distinguished from first_packet_read which only
            // confirms demuxer / I/O motion. On slow-open paths this can
            // lag first_packet_read by seconds (decoder back-pressure,
            // keyframe wait, internal libavcodec buffering). Detail format:
            // "<pts_us>" — main-side lambda converts to pts_ms. Fires once.
            if (!first_decoder_receive_fired) {
                first_decoder_receive_fired = true;
                int64_t pts_us = 0;
                if (frame->pts != AV_NOPTS_VALUE) {
                    AVRational tb = video_stream->time_base;
                    pts_us = av_rescale_q(frame->pts, tb, {1, 1000000});
                }
                char info[64];
                std::snprintf(info, sizeof(info), "%lld",
                              static_cast<long long>(pts_us));
                on_event_("first_decoder_receive", std::string(info));
            }

            process_frame(frame);
            av_frame_unref(frame);
        }
    }

    // Cleanup
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (sws_10bit) sws_freeContext(sws_10bit);
    if (intermediate) {
        av_freep(&intermediate->data[0]);
        av_frame_free(&intermediate);
    }
    av_freep(&bgra_frame->data[0]);
    av_frame_free(&bgra_frame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
#ifdef _WIN32
    if (d3d_presenter) {
        d3d_presenter->destroy();
        delete d3d_presenter;
        d3d_presenter = nullptr;
    }
    {
        std::lock_guard<std::mutex> overlay_lock(overlay_mutex_);
        if (overlay_shm_) {
            overlay_shm_->destroy();
            overlay_shm_.reset();
        }
    }
#endif
    avformat_close_input(&fmt_ctx);

    std::fprintf(stderr, "VideoDecoder: thread exiting (wrote %lld frames, dropped %lld hw=%s)\n",
                 static_cast<long long>(frames_written),
                 static_cast<long long>(frames_dropped),
                 hw_accel_active ? "yes" : "no");
    running_.store(false);
}
