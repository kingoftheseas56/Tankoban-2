#include "gpu_renderer.h"

#ifdef HAS_LIBPLACEBO

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// libplacebo uses C compound literals heavily — we must avoid the convenience macros
// and construct params structs manually in C++.
#include <libplacebo/config.h>
#include <libplacebo/log.h>
#include <libplacebo/renderer.h>
#include <libplacebo/tone_mapping.h>
#include <libplacebo/gpu.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/shaders/icc.h>
#include <libplacebo/utils/upload.h>

// C wrapper for libplacebo's FFmpeg interop (C-only headers)
extern "C" {
int gpu_renderer_map_avframe(pl_gpu gpu, struct pl_frame* out, pl_tex tex[4], const AVFrame* frame);
void gpu_renderer_unmap_avframe(pl_gpu gpu, struct pl_frame* frame);
void gpu_renderer_frame_from_avframe(struct pl_frame* out, const AVFrame* frame);
}

extern "C" {
#include <libavutil/pixfmt.h>
}

struct GpuRenderer::Impl {
    pl_log          log      = nullptr;
    pl_vulkan       vk       = nullptr;
    pl_gpu          gpu      = nullptr;
    pl_renderer     renderer = nullptr;
    pl_tex          target   = nullptr;

    struct pl_render_params      render_params;
    struct pl_color_map_params   color_map;
    struct pl_peak_detect_params peak_params;
    struct pl_color_space        src_csp;
    int                          src_color_space = 0;  // AVCOL_SPC_* stash (P1; P2 consumes)
    pl_icc_object                icc_obj = nullptr;
    struct pl_icc_profile        target_icc;
    std::vector<uint8_t>         icc_data;  // owns the ICC profile bytes

    int last_w = 0;
    int last_h = 0;

    Impl() {
        render_params = pl_render_default_params;
        color_map     = pl_color_map_default_params;
        peak_params   = pl_peak_detect_default_params;
        std::memset(&src_csp, 0, sizeof(src_csp));
        // Enable peak detection and point to owned structs by default
        render_params.color_map_params    = &color_map;
        render_params.peak_detect_params  = &peak_params;
    }
};

GpuRenderer::GpuRenderer() : impl_(std::make_unique<Impl>()) {}

GpuRenderer::~GpuRenderer() {
    destroy();
}

bool GpuRenderer::init() {
    destroy();

    // Create log — construct params manually (no C compound literal)
    struct pl_log_params log_p;
    std::memset(&log_p, 0, sizeof(log_p));
    log_p.log_cb    = pl_log_color;
    log_p.log_level = PL_LOG_WARN;

    impl_->log = pl_log_create(PL_API_VER, &log_p);
    if (!impl_->log) {
        std::fprintf(stderr, "GpuRenderer: pl_log_create failed\n");
        return false;
    }

    // Create Vulkan instance — construct params manually
    struct pl_vulkan_params vk_p;
    std::memset(&vk_p, 0, sizeof(vk_p));
    vk_p.allow_software = false;

    impl_->vk = pl_vulkan_create(impl_->log, &vk_p);
    if (!impl_->vk) {
        std::fprintf(stderr, "GpuRenderer: pl_vulkan_create failed (no Vulkan?)\n");
        destroy();
        return false;
    }

    impl_->gpu = impl_->vk->gpu;
    impl_->renderer = pl_renderer_create(impl_->log, impl_->gpu);
    if (!impl_->renderer) {
        std::fprintf(stderr, "GpuRenderer: pl_renderer_create failed\n");
        destroy();
        return false;
    }

    // High quality scaling — reset to defaults then re-attach owned structs
    impl_->render_params = pl_render_default_params;
    impl_->render_params.upscaler          = &pl_filter_ewa_lanczossharp;
    impl_->render_params.downscaler        = &pl_filter_hermite;
    impl_->render_params.color_map_params   = &impl_->color_map;
    impl_->render_params.peak_detect_params = &impl_->peak_params;

    std::fprintf(stderr, "GpuRenderer: Vulkan GPU initialized\n");
    return true;
}

bool GpuRenderer::render_frame(AVFrame* frame, uint8_t* bgra_out,
                                int out_width, int out_height, int out_stride) {
    if (!impl_->renderer || !impl_->gpu) return false;

    // (Re)create target texture if dimensions changed
    if (out_width != impl_->last_w || out_height != impl_->last_h) {
        pl_tex_destroy(impl_->gpu, &impl_->target);

        const pl_fmt fmt = pl_find_fmt(impl_->gpu, PL_FMT_UNORM, 4, 8, 0,
                                        static_cast<pl_fmt_caps>(PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE));
        if (!fmt) {
            std::fprintf(stderr, "GpuRenderer: no suitable BGRA format found\n");
            return false;
        }

        struct pl_tex_params tp;
        std::memset(&tp, 0, sizeof(tp));
        tp.w             = out_width;
        tp.h             = out_height;
        tp.format        = fmt;
        tp.renderable    = true;
        tp.host_readable = true;

        impl_->target = pl_tex_create(impl_->gpu, &tp);
        if (!impl_->target) {
            std::fprintf(stderr, "GpuRenderer: pl_tex_create failed for %dx%d\n",
                         out_width, out_height);
            return false;
        }
        impl_->last_w = out_width;
        impl_->last_h = out_height;
    }

    // Upload AVFrame to GPU via libplacebo's FFmpeg helpers
    struct pl_frame pl_src;
    std::memset(&pl_src, 0, sizeof(pl_src));

    // Map AVFrame to GPU textures (via C wrapper)
    pl_tex src_tex[4] = {nullptr};
    int mapped_ok = gpu_renderer_map_avframe(impl_->gpu, &pl_src, src_tex, frame);
    if (!mapped_ok) {
        std::fprintf(stderr, "GpuRenderer: pl_map_avframe failed\n");
        return false;
    }

    // Apply source colorspace from HDR metadata if set
    if (impl_->src_csp.primaries != PL_COLOR_PRIM_UNKNOWN) {
        pl_src.color = impl_->src_csp;
    }

    // Target frame: BGRA output
    struct pl_frame pl_target;
    std::memset(&pl_target, 0, sizeof(pl_target));
    pl_target.num_planes = 1;
    pl_target.planes[0].texture = impl_->target;
    pl_target.planes[0].components = 4;
    pl_target.planes[0].component_mapping[0] = PL_CHANNEL_B;
    pl_target.planes[0].component_mapping[1] = PL_CHANNEL_G;
    pl_target.planes[0].component_mapping[2] = PL_CHANNEL_R;
    pl_target.planes[0].component_mapping[3] = PL_CHANNEL_A;
    pl_target.repr.sys    = PL_COLOR_SYSTEM_RGB;
    pl_target.repr.levels = PL_COLOR_LEVELS_FULL;
    pl_target.repr.bits.sample_depth = 8;
    pl_target.repr.bits.color_depth  = 8;
    pl_target.color = pl_color_space_srgb;

    // Apply ICC profile if loaded
    if (impl_->icc_obj) {
        pl_target.icc = impl_->icc_obj;
    }

    // Render with tone mapping
    bool ok = pl_render_image(impl_->renderer, &pl_src, &pl_target,
                              &impl_->render_params);

    // Unmap the AVFrame planes
    gpu_renderer_unmap_avframe(impl_->gpu, &pl_src);

    if (!ok) {
        std::fprintf(stderr, "GpuRenderer: pl_render_image failed\n");
        return false;
    }

    // Download rendered result to CPU
    struct pl_tex_transfer_params dl;
    std::memset(&dl, 0, sizeof(dl));
    dl.tex       = impl_->target;
    dl.row_pitch = out_stride;
    dl.ptr       = bgra_out;

    ok = pl_tex_download(impl_->gpu, &dl);
    if (!ok) {
        std::fprintf(stderr, "GpuRenderer: pl_tex_download failed\n");
        return false;
    }

    return true;
}

void GpuRenderer::set_tone_mapping(const std::string& algorithm, bool peak_detect) {
    if (!impl_) return;

    const struct pl_tone_map_function* fn = &pl_tone_map_hable;
    if (algorithm == "reinhard")    fn = &pl_tone_map_reinhard;
    else if (algorithm == "bt2390") fn = &pl_tone_map_bt2390;
    else if (algorithm == "clip")   fn = &pl_tone_map_clip;
    else if (algorithm == "mobius") fn = &pl_tone_map_mobius;
    else if (algorithm == "linear") fn = &pl_tone_map_linear;
    else if (algorithm == "hable")  fn = &pl_tone_map_hable;

    // Update owned color_map member (no dangling pointer)
    impl_->color_map.tone_mapping_function = fn;
    impl_->render_params.color_map_params = &impl_->color_map;

    // Wire peak detection
    impl_->render_params.peak_detect_params = peak_detect ? &impl_->peak_params : nullptr;

    std::fprintf(stderr, "GpuRenderer: tone mapping set to '%s' peak_detect=%d\n",
                 algorithm.c_str(), peak_detect);
}

void GpuRenderer::set_hdr_metadata(int color_primaries, int color_trc, int color_space,
                                    double max_lum, double min_lum,
                                    int max_cll, int max_fall) {
    if (!impl_) return;

    auto& csp = impl_->src_csp;
    std::memset(&csp, 0, sizeof(csp));

    switch (color_primaries) {
        case 1:  csp.primaries = PL_COLOR_PRIM_BT_709;  break;
        case 9:  csp.primaries = PL_COLOR_PRIM_BT_2020; break;
        case 11: csp.primaries = PL_COLOR_PRIM_DCI_P3;  break;
        case 12: csp.primaries = PL_COLOR_PRIM_DCI_P3;  break;
        default: csp.primaries = PL_COLOR_PRIM_UNKNOWN;  break;
    }

    switch (color_trc) {
        case 1:  csp.transfer = PL_COLOR_TRC_BT_1886; break;
        case 13: csp.transfer = PL_COLOR_TRC_SRGB;    break;
        case 16: csp.transfer = PL_COLOR_TRC_PQ;      break;
        case 18: csp.transfer = PL_COLOR_TRC_HLG;     break;
        default: csp.transfer = PL_COLOR_TRC_UNKNOWN;  break;
    }

    csp.hdr.max_luma = static_cast<float>(max_lum);
    csp.hdr.min_luma = static_cast<float>(min_lum);
    csp.hdr.max_cll  = static_cast<float>(max_cll);
    csp.hdr.max_fall = static_cast<float>(max_fall);

    // LIBPLACEBO_SINGLE_RENDERER_FIX P1 2026-04-25 — stash AVCOL_SPC_*
    // (BT.709/BT.601/BT.2020 matrix) for P2's SDR render path. HDR rendering
    // today maps the YUV→RGB matrix from the AVFrame in render_frame's
    // gpu_renderer_map_avframe call (gpu_renderer.cpp:157), so this stash
    // is forward-only; P2 will seed pl_src.repr.sys explicitly when SDR
    // bypasses the AVFrame-derived matrix path.
    impl_->src_color_space = color_space;

    std::fprintf(stderr, "GpuRenderer: HDR metadata set — primaries=%d trc=%d space=%d max_lum=%.0f max_cll=%d\n",
                 color_primaries, color_trc, color_space, max_lum, max_cll);
}

void GpuRenderer::load_icc_profile(const std::string& path) {
    if (!impl_) return;

    std::string profile_path = path;

#ifdef _WIN32
    // Auto-detect system ICC profile if no path given
    if (profile_path.empty()) {
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            DWORD buf_size = MAX_PATH;
            char icc_path[MAX_PATH] = {};
            if (GetICMProfileA(hdc, &buf_size, icc_path)) {
                profile_path = icc_path;
            }
            ReleaseDC(nullptr, hdc);
        }
    }
#endif

    if (profile_path.empty()) {
        std::fprintf(stderr, "GpuRenderer: no ICC profile found\n");
        return;
    }

    // Read the profile file
    std::ifstream f(profile_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::fprintf(stderr, "GpuRenderer: cannot open ICC profile '%s'\n", profile_path.c_str());
        return;
    }
    auto size = f.tellg();
    f.seekg(0);
    impl_->icc_data.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(impl_->icc_data.data()), size);

    // Close previous ICC object BEFORE resizing icc_data, since target_icc.data
    // points into icc_data and pl_icc_close may reference it during teardown.
    if (impl_->icc_obj) {
        pl_icc_close(&impl_->icc_obj);
    }

    // Set up pl_icc_profile — target_icc.data points into icc_data which was
    // just populated above. Do NOT resize/modify icc_data after this point.
    std::memset(&impl_->target_icc, 0, sizeof(impl_->target_icc));
    impl_->target_icc.data = impl_->icc_data.data();
    impl_->target_icc.len  = impl_->icc_data.size();
    pl_icc_profile_compute_signature(&impl_->target_icc);

    impl_->icc_obj = pl_icc_open(impl_->log, &impl_->target_icc, nullptr);

    if (!impl_->icc_obj) {
        std::fprintf(stderr, "GpuRenderer: pl_icc_open failed for '%s' (%zu bytes) — "
                     "file may be corrupt or not a valid ICC profile. "
                     "Color management disabled.\n",
                     profile_path.c_str(), impl_->icc_data.size());
    } else {
        std::fprintf(stderr, "GpuRenderer: loaded ICC profile '%s' (%zu bytes)\n",
                     profile_path.c_str(), impl_->icc_data.size());
    }
}

bool GpuRenderer::active() const {
    return impl_ && impl_->renderer != nullptr;
}

void GpuRenderer::destroy() {
    if (!impl_) return;
    if (impl_->icc_obj) {
        pl_icc_close(&impl_->icc_obj);
    }
    if (impl_->target) {
        pl_tex_destroy(impl_->gpu, &impl_->target);
        impl_->target = nullptr;
    }
    if (impl_->renderer) {
        pl_renderer_destroy(&impl_->renderer);
    }
    if (impl_->vk) {
        pl_vulkan_destroy(&impl_->vk);
        impl_->gpu = nullptr;
    }
    if (impl_->log) {
        pl_log_destroy(&impl_->log);
    }
    impl_->last_w = impl_->last_h = 0;
}

#endif  // HAS_LIBPLACEBO
