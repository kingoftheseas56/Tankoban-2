#pragma once

#include <memory>
#include <string>
#include <cstdint>

#ifdef HAS_LIBPLACEBO

extern "C" {
#include <libavutil/frame.h>
}

class GpuRenderer {
public:
    GpuRenderer();
    ~GpuRenderer();

    bool init();
    bool render_frame(AVFrame* frame, uint8_t* bgra_out,
                      int out_width, int out_height, int out_stride);
    void set_tone_mapping(const std::string& algorithm, bool peak_detect);
    void set_hdr_metadata(int color_primaries, int color_trc,
                          double max_lum, double min_lum,
                          int max_cll, int max_fall);
    void load_icc_profile(const std::string& path);  // empty = system default
    bool active() const;
    void destroy();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#else  // !HAS_LIBPLACEBO — stub

class GpuRenderer {
public:
    GpuRenderer() {}
    ~GpuRenderer() {}
    bool init() { return false; }
    bool render_frame(void*, uint8_t*, int, int, int) { return false; }
    void set_tone_mapping(const std::string&, bool) {}
    void set_hdr_metadata(int, int, double, double, int, int) {}
    void load_icc_profile(const std::string&) {}
    bool active() const { return false; }
    void destroy() {}
};

#endif  // HAS_LIBPLACEBO
