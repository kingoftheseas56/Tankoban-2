#include "subtitle_renderer.h"
#include "encoding_detect.h"

#include <ass/ass.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

extern "C" {
#include <libavcodec/avcodec.h>
}

#ifdef _WIN32
#include <windows.h>
#include <avrt.h>
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

// Diagnostic log file — writes directly, bypasses pipe issues
static FILE* sub_log() {
    static FILE* f = nullptr;
    if (!f) {
        f = fopen("C:\\Users\\Suprabha\\Desktop\\TankobanQTGroundWork\\sub_debug.log", "w");
    }
    return f;
}
#define SUB_LOG(...) do { FILE* _f = sub_log(); if (_f) { std::fprintf(_f, __VA_ARGS__); std::fflush(_f); } } while(0)

namespace {
struct FitRect {
    int x = 0;
    int y = 0;
    int w = 1;
    int h = 1;
};

FitRect fit_aspect_rect(int canvas_w, int canvas_h, double video_aspect) {
    canvas_w = std::max(1, canvas_w);
    canvas_h = std::max(1, canvas_h);
    if (video_aspect <= 0.0) {
        video_aspect = static_cast<double>(canvas_w) / static_cast<double>(canvas_h);
    }

    FitRect r;
    const double canvas_aspect = static_cast<double>(canvas_w) / static_cast<double>(canvas_h);
    if (video_aspect > canvas_aspect) {
        r.w = canvas_w;
        r.h = std::clamp(static_cast<int>(std::lround(canvas_w / video_aspect)), 1, canvas_h);
        int spare = canvas_h - r.h;
        if ((spare & 1) != 0 && r.h > 1) {
            --r.h;
            spare = canvas_h - r.h;
        }
        r.y = spare / 2;
    } else {
        r.h = canvas_h;
        r.w = std::clamp(static_cast<int>(std::lround(canvas_h * video_aspect)), 1, canvas_w);
        int spare = canvas_w - r.w;
        if ((spare & 1) != 0 && r.w > 1) {
            --r.w;
            spare = canvas_w - r.w;
        }
        r.x = spare / 2;
    }
    return r;
}
}

// Default ASS header for SRT/text subtitles
static const char* DEFAULT_ASS_HEADER =
    "[Script Info]\r\n"
    "ScriptType: v4.00+\r\n"
    "PlayResX: 384\r\n"
    "PlayResY: 288\r\n"
    "\r\n"
    "[V4+ Styles]\r\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
    "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
    "Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,2,1,2,10,10,20,1\r\n"
    "\r\n"
    "[Events]\r\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";

static void ass_log_cb(int level, const char* fmt, va_list args, void* /*data*/) {
    if (level > 6) return;
    FILE* f = sub_log();
    if (f) {
        std::fprintf(f, "[libass L%d] ", level);
        std::vfprintf(f, fmt, args);
        std::fprintf(f, "\n");
        std::fflush(f);
    }
}

SubtitleRenderer::SubtitleRenderer() {
    library_ = ass_library_init();
    if (!library_) {
        std::fprintf(stderr, "SubtitleRenderer: ass_library_init failed\n");
        return;
    }
    ass_set_message_cb(library_, ass_log_cb, nullptr);
    ass_set_extract_fonts(library_, 1);

    renderer_ = ass_renderer_init(library_);
    if (!renderer_) {
        std::fprintf(stderr, "SubtitleRenderer: ass_renderer_init failed\n");
        return;
    }

#ifdef _WIN32
    ass_set_fonts(renderer_, nullptr, "Arial",
                  ASS_FONTPROVIDER_DIRECTWRITE, nullptr, 0);
#else
    ass_set_fonts(renderer_, nullptr, "Arial",
                  ASS_FONTPROVIDER_FONTCONFIG, nullptr, 0);
#endif

    // Start the dedicated render thread.
    // ass_render_frame will ONLY be called from this thread,
    // while ass_process_chunk runs on the decode thread.
    // This matches the MPV/VLC architecture.
    render_thread_ = std::thread(&SubtitleRenderer::render_thread_func, this);
    std::fprintf(stderr, "SubtitleRenderer: render thread started\n");
}

SubtitleRenderer::~SubtitleRenderer() {
    // Signal render thread to stop
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        render_stop_ = true;
    }
    render_cv_.notify_all();
    if (render_thread_.joinable())
        render_thread_.join();

    clear_track();
    cleanup_pgs();
    if (renderer_) ass_renderer_done(renderer_);
    if (library_)  ass_library_done(library_);
}

// ---------------------------------------------------------------------------
// Render thread — the ONLY place ass_render_frame is called
// ---------------------------------------------------------------------------

void SubtitleRenderer::render_thread_func() {
#ifdef _WIN32
    MmcssScope mmcss(L"Playback");  // released when thread exits
#endif
    std::fprintf(stderr, "SubtitleRenderer: render thread running (tid=%p)\n",
                 (void*)render_thread_.native_handle());

    while (true) {
        // Wait for a render request or stop signal
        std::unique_lock<std::mutex> lock(render_mutex_);
        render_cv_.wait(lock, [this]() {
            return render_pending_ || render_stop_;
        });

        if (render_stop_) break;
        if (!render_pending_) continue;

        // Grab the request parameters
        uint8_t* frame  = rq_frame_;
        int width       = rq_width_;
        int height      = rq_height_;
        int stride      = rq_stride_;
        int64_t pts_ms  = rq_pts_ms_;
        render_pending_ = false;
        lock.unlock();

        // Do the actual render (under mutex_, not render_mutex)
        {
            std::lock_guard<std::mutex> ass_lock(mutex_);
            if (visible_.load(std::memory_order_relaxed)) {
                if (is_pgs_ && !pgs_rects_.empty()) {
                    // PGS bitmap path — alpha-blend pre-converted BGRA rects
                    blend_pgs_rects(frame, stride, width, height);
                } else if (renderer_ && track_) {
                    // libass text path
                    int64_t render_time = pts_ms + delay_ms_.load(std::memory_order_relaxed);
                    if (render_time < 0) render_time = 0;

                    int changed = 0;
                    ASS_Image* images = ass_render_frame(renderer_, track_,
                                                          static_cast<long long>(render_time),
                                                          &changed);

                    static int64_t last_log_ms = -5000;
                    if (changed || (render_time - last_log_ms >= 2000)) {
                        SUB_LOG("render[thread]: time=%lldms events=%d changed=%d images=%s\n",
                                static_cast<long long>(render_time),
                                track_->n_events, changed,
                                images ? "YES" : "no");
                        last_log_ms = render_time;
                    }

                    if (images) {
                        blend_image_list(images, frame, stride, height);
                    }
                }
            }
        }

        // Signal completion
        {
            std::lock_guard<std::mutex> done_lock(render_mutex_);
            render_complete_ = true;
        }
        render_done_cv_.notify_one();
    }

    std::fprintf(stderr, "SubtitleRenderer: render thread exiting\n");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SubtitleRenderer::set_frame_size(int width, int height) {
    configure_geometry(width, height, width, height);
}

void SubtitleRenderer::configure_geometry(int video_w, int video_h,
                                          int canvas_w, int canvas_h) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (video_w <= 0 || video_h <= 0) return;
    if (canvas_w <= 0 || canvas_h <= 0) {
        canvas_w = video_w;
        canvas_h = video_h;
    }

    video_w_ = video_w;
    video_h_ = video_h;
    frame_w_ = canvas_w;
    frame_h_ = canvas_h;

    const FitRect rect = fit_aspect_rect(
        canvas_w, canvas_h,
        static_cast<double>(video_w) / static_cast<double>(video_h));
    video_rect_x_ = rect.x;
    video_rect_y_ = rect.y;
    video_rect_w_ = rect.w;
    video_rect_h_ = rect.h;

    if (renderer_) {
        ass_set_frame_size(renderer_, canvas_w, canvas_h);
        // PLAYER_UX_FIX Phase 4.1 — storage_size must be the UNSCALED
        // source video dimensions for correct aspect / blur / transforms
        // / VSFilter-compatible behavior per libass docs. Prior code
        // passed (0, 0) which disables storage-aware rendering —
        // anamorphic sources rendered with wrong aspect, rotated/blurred
        // ASS styles misaligned. In today's setup frame_size == video
        // size == canvas size for the overlay, so `width, height` here
        // is the correct video-stream storage. Phase 4.2 will decouple
        // frame_size (canvas dims) from storage_size (video dims); the
        // call site will move to use a separately-stored video-stream
        // size. mpv reference: sd_ass.c:767-771. VLC: libass.c:431-438.
        ass_set_storage_size(renderer_, video_w, video_h);
        ass_set_margins(renderer_,
                        video_rect_y_,
                        std::max(0, canvas_h - video_rect_y_ - video_rect_h_),
                        video_rect_x_,
                        std::max(0, canvas_w - video_rect_x_ - video_rect_w_));
        ass_set_use_margins(renderer_, 1);
        const double src_aspect =
            static_cast<double>(video_w) / static_cast<double>(video_h);
        const double dst_aspect =
            static_cast<double>(video_rect_w_) / static_cast<double>(video_rect_h_);
        const double pixel_aspect = dst_aspect / src_aspect;
        if (std::isfinite(pixel_aspect) && pixel_aspect > 0.0) {
            ass_set_pixel_aspect(renderer_, pixel_aspect);
        }
    }
    SUB_LOG("configure_geometry: video=%dx%d canvas=%dx%d rect=%d,%d %dx%d margins=%d,%d,%d,%d\n",
            video_w_, video_h_, frame_w_, frame_h_,
            video_rect_x_, video_rect_y_, video_rect_w_, video_rect_h_,
            video_rect_y_,
            std::max(0, frame_h_ - video_rect_y_ - video_rect_h_),
            video_rect_x_,
            std::max(0, frame_w_ - video_rect_x_ - video_rect_w_));
}

void SubtitleRenderer::load_embedded_track(const std::string& codec_name,
                                           const std::vector<uint8_t>& extradata) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!library_) return;

    if (track_) {
        ass_free_track(track_);
        track_ = nullptr;
    }

    is_text_sub_ = false;

    if (codec_name == "ass" || codec_name == "ssa") {
        track_ = ass_new_track(library_);
        if (track_ && !extradata.empty()) {
            std::string header(reinterpret_cast<const char*>(extradata.data()),
                               extradata.size());
            ass_process_codec_private(track_, header.data(),
                                      static_cast<int>(header.size()));
        }
    } else if (codec_name == "subrip" || codec_name == "srt" ||
               codec_name == "mov_text" || codec_name == "text") {
        track_ = ass_new_track(library_);
        if (track_) {
            std::string header(DEFAULT_ASS_HEADER);
            ass_process_codec_private(track_, header.data(),
                                      static_cast<int>(header.size()));
            is_text_sub_ = true;
        }
    } else if (codec_name == "hdmv_pgs_subtitle" || codec_name == "pgssub" ||
               codec_name == "dvd_subtitle"    || codec_name == "dvdsub") {
        // PGS / DVD bitmap subtitles — decode with avcodec, blend as bitmaps.
        cleanup_pgs();
        enum AVCodecID cid = (codec_name == "dvd_subtitle" || codec_name == "dvdsub")
            ? AV_CODEC_ID_DVD_SUBTITLE : AV_CODEC_ID_HDMV_PGS_SUBTITLE;
        const AVCodec* dec = avcodec_find_decoder(cid);
        if (dec) {
            pgs_ctx_ = avcodec_alloc_context3(dec);
            if (!extradata.empty()) {
                pgs_ctx_->extradata = static_cast<uint8_t*>(
                    av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
                std::memcpy(pgs_ctx_->extradata, extradata.data(), extradata.size());
                pgs_ctx_->extradata_size = static_cast<int>(extradata.size());
            }
            if (video_w_ > 0 && video_h_ > 0) {
                pgs_ctx_->width  = video_w_;
                pgs_ctx_->height = video_h_;
            } else if (frame_w_ > 0 && frame_h_ > 0) {
                pgs_ctx_->width  = frame_w_;
                pgs_ctx_->height = frame_h_;
            }
            if (avcodec_open2(pgs_ctx_, dec, nullptr) == 0) {
                is_pgs_ = true;
                std::fprintf(stderr, "SubtitleRenderer: PGS bitmap decoder opened (%s)\n",
                             codec_name.c_str());
            } else {
                std::fprintf(stderr, "SubtitleRenderer: avcodec_open2 failed for %s\n",
                             codec_name.c_str());
                avcodec_free_context(&pgs_ctx_);
                pgs_ctx_ = nullptr;
            }
        } else {
            std::fprintf(stderr, "SubtitleRenderer: no decoder found for %s\n",
                         codec_name.c_str());
        }
    } else {
        std::fprintf(stderr, "SubtitleRenderer: unsupported codec '%s'\n",
                     codec_name.c_str());
    }
}

void SubtitleRenderer::process_packet(const uint8_t* data, int size,
                                      int64_t start_ms, int64_t duration_ms) {
    // Called from the DECODE thread.
    std::lock_guard<std::mutex> lock(mutex_);

    // --- PGS bitmap path: decode with avcodec ---
    if (is_pgs_ && pgs_ctx_) {
        if (!data || size <= 0) return;

        AVPacket* pkt = av_packet_alloc();
        pkt->data = const_cast<uint8_t*>(data);
        pkt->size = size;
        pkt->pts  = start_ms;  // pkt_timebase is {1,1000} (ms)

        AVSubtitle sub = {};
        int got_sub = 0;
        int ret = avcodec_decode_subtitle2(pgs_ctx_, &sub, &got_sub, pkt);
        av_packet_free(&pkt);

        if (ret >= 0 && got_sub) {
            pgs_rects_.clear();
            for (unsigned i = 0; i < sub.num_rects; ++i) {
                AVSubtitleRect* r = sub.rects[i];
                if (r->type != SUBTITLE_BITMAP || r->w <= 0 || r->h <= 0)
                    continue;

                // Convert palette-indexed bitmap to BGRA for direct blending
                PgsRect pr;
                pr.x = r->x;
                pr.y = r->y;
                pr.w = r->w;
                pr.h = r->h;
                pr.bgra.resize(static_cast<size_t>(pr.w) * pr.h * 4);

                const uint32_t* palette = reinterpret_cast<const uint32_t*>(r->data[1]);
                const uint8_t*  bitmap  = r->data[0];
                int bmp_stride = r->linesize[0];

                for (int y = 0; y < pr.h; ++y) {
                    for (int x = 0; x < pr.w; ++x) {
                        uint8_t idx = bitmap[y * bmp_stride + x];
                        uint32_t rgba = palette[idx];
                        // FFmpeg palette is RGBA; our frame is BGRA — swap R and B
                        uint8_t R = (rgba >>  0) & 0xFF;
                        uint8_t G = (rgba >>  8) & 0xFF;
                        uint8_t B = (rgba >> 16) & 0xFF;
                        uint8_t A = (rgba >> 24) & 0xFF;
                        uint8_t* dst = &pr.bgra[(y * pr.w + x) * 4];
                        dst[0] = B;
                        dst[1] = G;
                        dst[2] = R;
                        dst[3] = A;
                    }
                }
                pgs_rects_.push_back(std::move(pr));
            }
            SUB_LOG("PGS: decoded %u rects at %lldms\n",
                    sub.num_rects, static_cast<long long>(start_ms));
            avsubtitle_free(&sub);
        }
        return;
    }

    // --- Text/ASS path ---
    if (!track_ || !data || size <= 0) return;

    int events_before = track_->n_events;

    if (is_text_sub_) {
        std::string text(reinterpret_cast<const char*>(data), size);

        std::string clean;
        clean.reserve(text.size());
        bool in_tag = false;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '<') { in_tag = true; continue; }
            if (text[i] == '>') { in_tag = false; continue; }
            if (!in_tag) clean += text[i];
        }

        std::string ass_text;
        ass_text.reserve(clean.size());
        for (size_t i = 0; i < clean.size(); ++i) {
            if (clean[i] == '\r') continue;
            if (clean[i] == '\n') {
                ass_text += "\\N";
            } else {
                ass_text += clean[i];
            }
        }

        while (!ass_text.empty() && (ass_text.back() == ' ' || ass_text.back() == '\t'))
            ass_text.pop_back();

        // ReadOrder must be unique per packet. libass dedupes events by
        // ReadOrder, so a hardcoded 0 made every packet after the first a
        // silent drop — the track ended with exactly one event (the first),
        // and ass_render_frame returned images=no at every other timestamp.
        // Using start_ms as ReadOrder is stable across seek re-delivery
        // (same packet → same ms → libass dedupes correctly) and gives
        // every distinct line a distinct slot.
        std::string chunk = std::to_string(static_cast<int>(start_ms))
                          + ",0,Default,,0,0,0,," + ass_text;
        ass_process_chunk(track_, chunk.data(), static_cast<int>(chunk.size()),
                          start_ms, duration_ms);
    } else {
        ass_process_chunk(track_,
                          reinterpret_cast<const char*>(data), size,
                          start_ms, duration_ms);
    }

    int events_after = track_->n_events;
    SUB_LOG("process_packet: start=%lldms dur=%lldms events=%d->%d text_sub=%d\n",
            static_cast<long long>(start_ms),
            static_cast<long long>(duration_ms),
            events_before, events_after,
            is_text_sub_ ? 1 : 0);
}

bool SubtitleRenderer::load_external_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!library_) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::fprintf(stderr, "SubtitleRenderer: cannot open '%s'\n", path.c_str());
        return false;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string raw = oss.str();

    if (raw.empty()) {
        std::fprintf(stderr, "SubtitleRenderer: file is empty '%s'\n", path.c_str());
        return false;
    }

    std::string utf8 = transcode_to_utf8(raw.data(), raw.size());

    if (track_) {
        ass_free_track(track_);
        track_ = nullptr;
    }

    track_ = ass_read_memory(library_, utf8.data(), utf8.size(), nullptr);
    if (!track_) {
        std::fprintf(stderr, "SubtitleRenderer: ass_read_memory failed for '%s'\n",
                     path.c_str());
        return false;
    }

    is_text_sub_ = false;
    return true;
}

bool SubtitleRenderer::bitmap_subtitles_active() {
    std::lock_guard<std::mutex> lock(mutex_);
    return is_pgs_;
}

void SubtitleRenderer::render_blend(uint8_t* frame, int width, int height,
                                     int stride, int64_t pts_ms) {
    if (!visible_.load(std::memory_order_relaxed)) return;

    // Submit render request to the dedicated thread
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        rq_frame_  = frame;
        rq_width_  = width;
        rq_height_ = height;
        rq_stride_ = stride;
        rq_pts_ms_ = pts_ms;
        render_pending_  = true;
        render_complete_ = false;
    }
    render_cv_.notify_one();

    // Block until the render thread finishes blending
    {
        std::unique_lock<std::mutex> lock(render_mutex_);
        render_done_cv_.wait(lock, [this]() {
            return render_complete_ || render_stop_;
        });
    }
}

void SubtitleRenderer::render_onto_frame(uint8_t* frame, int width, int height,
                                         int stride, int64_t pts_ms) {
    // Delegate to the thread-safe render_blend
    render_blend(frame, width, height, stride, pts_ms);
}

void SubtitleRenderer::set_visible(bool visible) {
    SUB_LOG("set_visible: %s\n", visible ? "true" : "false");
    visible_.store(visible, std::memory_order_relaxed);
}

void SubtitleRenderer::set_delay_ms(int64_t delay_ms) {
    delay_ms_.store(delay_ms, std::memory_order_relaxed);
}

void SubtitleRenderer::clear_track() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (track_) {
        ass_free_track(track_);
        track_ = nullptr;
    }
    is_text_sub_ = false;
    is_pgs_ = false;
    pgs_rects_.clear();
    cleanup_pgs();
}

void SubtitleRenderer::set_style_override(int font_size, int margin_v, bool outline) {
    // Do NOT call any ass_set_selective_style_override* APIs here —
    // changing renderer config mid-playback corrupts libass internal
    // state and causes the silent-stop rendering bug.
    //
    // Instead, we store the margin offset and apply it during blending
    // by shifting ASS_Image Y coordinates upward.
    int default_margin = 40;
    int offset = (margin_v > default_margin) ? (margin_v - default_margin) : 0;
    margin_lift_px_.store(offset, std::memory_order_relaxed);

    std::fprintf(stderr, "SubtitleRenderer: margin lift=%dpx (margin_v=%d)\n",
                 offset, margin_v);
}

// ---------------------------------------------------------------------------
// PLAYER_PERF_FIX Phase 3 Batch 3.A — two-stage overlay pipeline (dead code
// until 3.B atomic cutover wires render_to_bitmaps + blend_into_frame in).
// ---------------------------------------------------------------------------
//
// render_thread_func + render_blend are intentionally NOT refactored to use
// these functions. Keeping the legacy path's single-pass blend preserves
// Phase 2 baseline perf; the 3.A→3.B transition happens atomically in one
// future batch so there's no intermediate regression window.

void SubtitleRenderer::render_to_bitmaps(int64_t pts_ms,
                                          std::vector<SubOverlayBitmap>& out) {
    // Acquires mutex_ since it reads libass state (ass_render_frame)
    // and pgs_rects_, both shared with process_packet on the decode
    // thread. Independent of render_thread_func's render_mutex_ /
    // render_cv_ pipeline — callers invoke this inline.
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();

    if (!visible_.load(std::memory_order_relaxed)) return;

    if (is_pgs_ && !pgs_rects_.empty()) {
        // PGS path — pgs_rects_ are already pre-converted to BGRA by
        // process_packet. Copy into the overlay vector shape.
        out.reserve(pgs_rects_.size());
        for (const auto& rect : pgs_rects_) {
            out.push_back(map_pgs_rect_to_canvas(rect));
        }
        return;
    }

    if (!renderer_ || !track_) return;

    int64_t render_time = pts_ms + delay_ms_.load(std::memory_order_relaxed);
    if (render_time < 0) render_time = 0;

    int changed = 0;
    ASS_Image* images = ass_render_frame(renderer_, track_,
                                          static_cast<long long>(render_time),
                                          &changed);
    if (!images) return;

    // Convert each ASS_Image (alpha-only bitmap + 32-bit color) into a
    // premultiplied-color BGRA tile. Matches the exact math that
    // blend_image_list uses — per-pixel alpha =
    // (src_alpha * (255 - color.a) + 127) / 255, RGB = color's R/G/B
    // channels (stored as BGRA in output).
    for (ASS_Image* img = images; img; img = img->next) {
        if (img->w == 0 || img->h == 0) continue;

        uint8_t r      = (img->color >> 24) & 0xFF;
        uint8_t g      = (img->color >> 16) & 0xFF;
        uint8_t b_col  = (img->color >>  8) & 0xFF;
        uint8_t a_base = 255 - (img->color & 0xFF);
        if (a_base == 0) continue;

        SubOverlayBitmap tile;
        tile.x = img->dst_x;
        tile.y = img->dst_y;
        tile.w = img->w;
        tile.h = img->h;
        tile.bgra.resize(static_cast<size_t>(tile.w) * tile.h * 4);

        const uint8_t* src = img->bitmap;
        uint8_t* dst = tile.bgra.data();
        for (int yy = 0; yy < tile.h; ++yy) {
            const uint8_t* src_row = src + yy * img->stride;
            uint8_t* dst_row = dst + yy * tile.w * 4;
            for (int xx = 0; xx < tile.w; ++xx) {
                uint8_t alpha = static_cast<uint8_t>(
                    (static_cast<uint16_t>(src_row[xx]) * a_base + 127) / 255);
                uint8_t* p = dst_row + xx * 4;
                p[0] = b_col;
                p[1] = g;
                p[2] = r;
                p[3] = alpha;
            }
        }

        out.push_back(std::move(tile));
    }
}

SubtitleRenderer::SubOverlayBitmap
SubtitleRenderer::map_pgs_rect_to_canvas(const PgsRect& rect) const {
    SubOverlayBitmap b;
    if (rect.w <= 0 || rect.h <= 0 || rect.bgra.empty()) return b;

    const int src_w = video_w_ > 0 ? video_w_ : frame_w_;
    const int src_h = video_h_ > 0 ? video_h_ : frame_h_;
    const int dst_rect_w = video_rect_w_ > 0 ? video_rect_w_ : frame_w_;
    const int dst_rect_h = video_rect_h_ > 0 ? video_rect_h_ : frame_h_;
    const double sx = (src_w > 0) ? static_cast<double>(dst_rect_w) / src_w : 1.0;
    const double sy = (src_h > 0) ? static_cast<double>(dst_rect_h) / src_h : 1.0;

    b.x = video_rect_x_ + static_cast<int>(std::lround(rect.x * sx));
    b.y = video_rect_y_ + static_cast<int>(std::lround(rect.y * sy));
    b.w = std::max(1, static_cast<int>(std::lround(rect.w * sx)));
    b.h = std::max(1, static_cast<int>(std::lround(rect.h * sy)));

    if (b.w == rect.w && b.h == rect.h) {
        b.bgra = rect.bgra;
        return b;
    }

    b.bgra.resize(static_cast<size_t>(b.w) * b.h * 4);
    for (int y = 0; y < b.h; ++y) {
        const int src_y = std::clamp(
            static_cast<int>((static_cast<int64_t>(y) * rect.h) / b.h),
            0, rect.h - 1);
        for (int x = 0; x < b.w; ++x) {
            const int src_x = std::clamp(
                static_cast<int>((static_cast<int64_t>(x) * rect.w) / b.w),
                0, rect.w - 1);
            const uint8_t* src = rect.bgra.data()
                + (static_cast<size_t>(src_y) * rect.w + src_x) * 4;
            uint8_t* dst = b.bgra.data()
                + (static_cast<size_t>(y) * b.w + x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }
    return b;
}

void SubtitleRenderer::blend_into_frame(const std::vector<SubOverlayBitmap>& bitmaps,
                                         uint8_t* frame,
                                         int frame_w, int frame_h, int stride) {
    // Standard src-over alpha blend over the caller's BGRA frame. Clamps
    // each bitmap to frame bounds so negative dst coords or oversized
    // tiles cannot write past the frame. Pixel-identical output to the
    // legacy blend_image_list + blend_pgs_rects paths.
    for (const auto& tile : bitmaps) {
        if (tile.bgra.empty() || tile.w <= 0 || tile.h <= 0) continue;

        int src_x0 = 0, src_y0 = 0;
        int dst_x = tile.x, dst_y = tile.y;
        int blit_w = tile.w, blit_h = tile.h;

        if (dst_x < 0) { src_x0 = -dst_x; blit_w += dst_x; dst_x = 0; }
        if (dst_y < 0) { src_y0 = -dst_y; blit_h += dst_y; dst_y = 0; }
        if (dst_x + blit_w > frame_w) blit_w = frame_w - dst_x;
        if (dst_y + blit_h > frame_h) blit_h = frame_h - dst_y;
        if (blit_w <= 0 || blit_h <= 0) continue;

        for (int yy = 0; yy < blit_h; ++yy) {
            uint8_t* dst_row = frame + (dst_y + yy) * stride + dst_x * 4;
            const uint8_t* src_row = tile.bgra.data()
                + (static_cast<size_t>(src_y0 + yy) * tile.w + src_x0) * 4;

            for (int xx = 0; xx < blit_w; ++xx) {
                const uint8_t* src = src_row + xx * 4;
                uint8_t alpha = src[3];
                if (alpha == 0) continue;

                uint8_t* dst = dst_row + xx * 4;
                if (alpha == 255) {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 255;
                } else {
                    uint8_t inv = 255 - alpha;
                    dst[0] = static_cast<uint8_t>((src[0] * alpha + dst[0] * inv + 127) / 255);
                    dst[1] = static_cast<uint8_t>((src[1] * alpha + dst[1] * inv + 127) / 255);
                    dst[2] = static_cast<uint8_t>((src[2] * alpha + dst[2] * inv + 127) / 255);
                    dst[3] = 255;
                }
            }
        }
    }
}

void SubtitleRenderer::blend_image_list(ASS_Image* img, uint8_t* frame,
                                        int stride, int frame_h) {
    for (; img; img = img->next) {
        if (img->w == 0 || img->h == 0) continue;

        uint8_t r = (img->color >> 24) & 0xFF;
        uint8_t g = (img->color >> 16) & 0xFF;
        uint8_t b = (img->color >>  8) & 0xFF;
        uint8_t a_base = 255 - (img->color & 0xFF);

        if (a_base == 0) continue;

        for (int y = 0; y < img->h; ++y) {
            int dst_y = img->dst_y + y;
            if (dst_y < 0 || dst_y >= frame_h) continue;

            uint8_t* dst_row = frame + dst_y * stride + img->dst_x * 4;
            const uint8_t* src_row = img->bitmap + y * img->stride;

            for (int x = 0; x < img->w; ++x) {
                uint8_t alpha = static_cast<uint8_t>(
                    (static_cast<uint16_t>(src_row[x]) * a_base + 127) / 255);
                if (alpha == 0) continue;

                uint8_t* px = dst_row + x * 4;
                uint8_t inv = 255 - alpha;
                px[0] = static_cast<uint8_t>((b * alpha + px[0] * inv + 127) / 255);
                px[1] = static_cast<uint8_t>((g * alpha + px[1] * inv + 127) / 255);
                px[2] = static_cast<uint8_t>((r * alpha + px[2] * inv + 127) / 255);
                px[3] = 255;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// PGS bitmap subtitle blending
// ---------------------------------------------------------------------------

void SubtitleRenderer::blend_pgs_rects(uint8_t* frame, int stride,
                                        int frame_w, int frame_h) {
    // Called from the render thread under mutex_.
    for (const auto& rect : pgs_rects_) {
        if (rect.bgra.empty()) continue;

        // Clamp rect to frame bounds
        int src_x0 = 0, src_y0 = 0;
        int dst_x = rect.x, dst_y = rect.y;
        int blit_w = rect.w, blit_h = rect.h;

        if (dst_x < 0) { src_x0 = -dst_x; blit_w += dst_x; dst_x = 0; }
        if (dst_y < 0) { src_y0 = -dst_y; blit_h += dst_y; dst_y = 0; }
        if (dst_x + blit_w > frame_w) blit_w = frame_w - dst_x;
        if (dst_y + blit_h > frame_h) blit_h = frame_h - dst_y;
        if (blit_w <= 0 || blit_h <= 0) continue;

        for (int y = 0; y < blit_h; ++y) {
            uint8_t* dst_row = frame + (dst_y + y) * stride + dst_x * 4;
            const uint8_t* src_row = rect.bgra.data()
                + (static_cast<size_t>(src_y0 + y) * rect.w + src_x0) * 4;

            for (int x = 0; x < blit_w; ++x) {
                const uint8_t* src = src_row + x * 4;
                uint8_t alpha = src[3];
                if (alpha == 0) continue;

                uint8_t* dst = dst_row + x * 4;
                if (alpha == 255) {
                    dst[0] = src[0];  // B
                    dst[1] = src[1];  // G
                    dst[2] = src[2];  // R
                    dst[3] = 255;
                } else {
                    uint8_t inv = 255 - alpha;
                    dst[0] = static_cast<uint8_t>((src[0] * alpha + dst[0] * inv + 127) / 255);
                    dst[1] = static_cast<uint8_t>((src[1] * alpha + dst[1] * inv + 127) / 255);
                    dst[2] = static_cast<uint8_t>((src[2] * alpha + dst[2] * inv + 127) / 255);
                    dst[3] = 255;
                }
            }
        }
    }
}

void SubtitleRenderer::cleanup_pgs() {
    // Called under mutex_ or from destructor.
    if (pgs_ctx_) {
        avcodec_free_context(&pgs_ctx_);
        pgs_ctx_ = nullptr;
    }
    pgs_rects_.clear();
    is_pgs_ = false;
}
