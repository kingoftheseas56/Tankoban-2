#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ass/ass.h>

// Forward-declare FFmpeg types (avoid pulling avcodec.h into every includer)
struct AVCodecContext;

class SubtitleRenderer {
public:
    // PLAYER_PERF_FIX Phase 3 Batch 3.A — overlay bitmap shape.
    // Infrastructure added as dead code (no caller in the hot path) so the
    // upcoming atomic cutover batch (3.B = former 3.3+3.4) only needs to
    // wire existing pieces. Matches the 3.2 dead-code-prep pattern for
    // D3D11OverlayTexture.
    //
    // Output of the libass/PGS render stage. BGRA, combined-alpha-per-
    // pixel (color premultiplied against libass's inverse-alpha). Consumed
    // either by blend_into_frame (CPU fallback when sidecar uploads to SHM
    // BGRA) or by the atomic-cutover GPU path that uploads to a
    // D3D11OverlayTexture. Same shape, same caller contract.
    struct SubOverlayBitmap {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        std::vector<uint8_t> bgra;  // w*h*4 bytes, BGRA with combined alpha
    };

    SubtitleRenderer();
    ~SubtitleRenderer();

    void set_frame_size(int width, int height);
    void configure_geometry(int video_w, int video_h, int canvas_w, int canvas_h);

    // Load embedded subtitle track from codec extradata.
    // codec_name: "ass", "subrip", etc.  extradata: ASS header for ASS/SSA.
    void load_embedded_track(const std::string& codec_name,
                             const std::vector<uint8_t>& extradata);

    // Feed a subtitle packet from the demuxer (called from decode thread).
    void process_packet(const uint8_t* data, int size,
                        int64_t start_ms, int64_t duration_ms);

    // Load external subtitle file (uses encoding_detect for transcoding).
    bool load_external_file(const std::string& path);

    // Blend subtitles onto a BGRA frame at the given timestamp.
    // IMPORTANT: calls ass_render_frame on a DEDICATED RENDER THREAD
    // (separate from the decode thread that calls process_packet).
    // This matches the MPV/VLC architecture and avoids the libass
    // silent-stop bug we hit when both ran on the same thread.
    // Blocks until blending is complete.
    void render_blend(uint8_t* frame, int width, int height,
                      int stride, int64_t pts_ms);

    // PLAYER_PERF_FIX Phase 3 Batch 3.A — two-stage overlay pipeline
    // (dead code until 3.B atomic cutover wires it in).
    //
    // render_to_bitmaps: produces BGRA overlay tiles from libass or PGS
    // state at the given pts. Acquires mutex_ internally (libass state is
    // shared with process_packet on the decode thread). `out` is cleared
    // before filling; caller should reuse the vector across frames to
    // avoid reallocation. For libass: converts each ASS_Image's alpha-
    // only bitmap + 32-bit color to premultiplied BGRA. For PGS: copies
    // pgs_rects_ (already BGRA) into the overlay vector.
    //
    // Can be called from any thread. Thread-safe via mutex_. Does NOT go
    // through the render_thread_func path — it runs inline. Cost is
    // dominated by the libass ass_render_frame call + per-pixel BGRA
    // conversion; same order of magnitude as the legacy render_blend
    // libass call.
    void render_to_bitmaps(int64_t pts_ms, std::vector<SubOverlayBitmap>& out);

    // blend_into_frame: CPU alpha-blends a list of overlay bitmaps onto
    // a BGRA frame. Stateless — no libass / PGS state touched, no mutex
    // required. Handles clamping so out-of-bounds bitmap coordinates do
    // not write past the frame. Mirrors the legacy blend_image_list +
    // blend_pgs_rects blending math exactly (premultiplied color + src-
    // over alpha); pixel-identical output.
    //
    // Used by 3.B atomic cutover as the CPU fallback when the fast-path
    // D3D11VA → D3D11OverlayTexture GPU upload path is unavailable (e.g.,
    // software-decoded AVIs where the main-app receives an SHM BGRA
    // frame and needs subs baked in).
    static void blend_into_frame(const std::vector<SubOverlayBitmap>& bitmaps,
                                  uint8_t* frame,
                                  int frame_w, int frame_h, int stride);

    // Legacy: same as render_blend (kept for compatibility).
    void render_onto_frame(uint8_t* frame, int width, int height,
                           int stride, int64_t pts_ms);

    void set_visible(bool visible);
    bool visible() const { return visible_.load(std::memory_order_relaxed); }
    bool bitmap_subtitles_active();

    void set_delay_ms(int64_t delay_ms);

    // VIDEO_SUB_POSITION 2026-04-24 — user-facing 0..100 percent slider.
    // 100 = bottom (default, mpv parity), 0 = top. Inverted at libass:
    // `ass_set_line_position(renderer_, 100 - pct)`. PGS path applies an
    // upward Y-shift to dst_y in blend_pgs_rects clamped to video_rect.
    void set_sub_position_pct(int pct);

    void clear_active_subs();
    void clear_track();

    // Override subtitle style (font size, vertical margin, outline width).
    void set_style_override(int font_size, int margin_v, bool outline);

private:
    void blend_image_list(ASS_Image* img, uint8_t* frame, int stride, int height);
    void render_thread_func();

    // --- PGS bitmap subtitle support ---
    // start_ms/end_ms make PGS rects time-aware so bulk preload doesn't
    // flash every subtitle in rapid succession on a mid-file resume.
    // Semantics: a packet with N>0 rects pushes N new PgsRects (all
    // sharing the same start_ms, end_ms = INT64_MAX). Any subsequent
    // packet — whether a 0-rect "hide" or a new "show" — closes any
    // still-open rects (end_ms == INT64_MAX) by setting their end_ms
    // to the new packet's start_ms. Dedup on start_ms prevents preload
    // re-feed (after seek) from double-adding the same event.
    struct PgsRect {
        int x, y, w, h;
        std::vector<uint8_t> bgra;  // pre-converted BGRA pixels (w*h*4 bytes)
        int64_t start_ms = 0;
        int64_t end_ms   = INT64_MAX;
    };
    void blend_pgs_rects(uint8_t* frame, int stride, int frame_w, int frame_h,
                         int64_t pts_ms);
    void cleanup_pgs();
    SubOverlayBitmap map_pgs_rect_to_canvas(const PgsRect& rect) const;

    // --- libass state (protected by mutex_) ---
    std::mutex          mutex_;
    ASS_Library*        library_  = nullptr;
    ASS_Renderer*       renderer_ = nullptr;
    ASS_Track*          track_    = nullptr;
    bool                is_text_sub_ = false;
    bool                is_pgs_      = false;
    AVCodecContext*     pgs_ctx_     = nullptr;
    std::vector<PgsRect> pgs_rects_;           // currently active PGS bitmaps
    std::atomic<bool>   visible_{true};
    std::atomic<int64_t> delay_ms_{0};
    std::atomic<int>    margin_lift_px_{0};  // pixels to shift subs up (HUD visible)
    // VIDEO_SUB_POSITION 2026-04-24 — user-facing baseline (0..100 percent,
    // 100 = bottom). Read each render under mutex_ in render_thread_func
    // for libass (ass_set_line_position) and in blend_pgs_rects for PGS
    // (Y-offset on dst_y). See set_sub_position_pct.
    std::atomic<int>    sub_position_pct_{100};
    // User subtitle-style overrides. font_scale_ is applied via libass's
    // renderer-level ass_set_font_scale — safe mid-playback unlike the
    // ass_set_selective_style_override_* APIs which caused a silent-stop
    // regression previously. The TrackPopover's "font size" slider
    // normalizes against the DEFAULT_ASS_HEADER baseline (Fontsize=20 →
    // scale=1.0), so a UI value of 30 yields scale=1.5.
    // For text subs (srt/mov_text), outline_enabled_ flips the Outline
    // field of the injected Default style at track-load time (0 = hairline,
    // 2 = current bolder style). ASS/SSA tracks carry their own Default
    // style from the file; the outline toggle is a no-op there.
    std::atomic<double> font_scale_{1.0};
    std::atomic<bool>   outline_enabled_{true};
    static constexpr int kBaselineFontSize = 20;  // matches DEFAULT_ASS_HEADER
    int                 frame_w_ = 0;
    int                 frame_h_ = 0;
    int                 video_w_ = 0;
    int                 video_h_ = 0;
    int                 video_rect_x_ = 0;
    int                 video_rect_y_ = 0;
    int                 video_rect_w_ = 0;
    int                 video_rect_h_ = 0;

    // --- Render thread (calls ass_render_frame) ---
    std::thread              render_thread_;
    std::mutex               render_mutex_;
    std::condition_variable  render_cv_;
    std::condition_variable  render_done_cv_;
    bool                     render_stop_ = false;
    bool                     render_pending_ = false;
    bool                     render_complete_ = false;

    // Render request parameters (set by caller, read by render thread)
    uint8_t*  rq_frame_  = nullptr;
    int       rq_width_  = 0;
    int       rq_height_ = 0;
    int       rq_stride_ = 0;
    int64_t   rq_pts_ms_ = 0;
};
