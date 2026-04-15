/* C wrapper for libplacebo's FFmpeg interop functions.
 * libav_internal.h cannot be included from C++.
 */
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#include <libplacebo/config.h>
#include <libplacebo/gpu.h>

/* Must define before including libav.h so the #if check works */
#define PL_LIBAV_IMPLEMENTATION 1
#include <libplacebo/utils/libav.h>

#include <libavutil/frame.h>

int gpu_renderer_map_avframe(pl_gpu gpu, struct pl_frame* out,
                             pl_tex tex[4], const AVFrame* frame) {
    return pl_map_avframe(gpu, out, tex, frame) ? 1 : 0;
}

void gpu_renderer_unmap_avframe(pl_gpu gpu, struct pl_frame* frame) {
    pl_unmap_avframe(gpu, frame);
}

void gpu_renderer_frame_from_avframe(struct pl_frame* out, const AVFrame* frame) {
    pl_frame_from_avframe(out, frame);
}
