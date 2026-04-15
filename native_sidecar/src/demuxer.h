#pragma once

#include <optional>
#include <string>
#include <vector>

// FFmpeg headers (C linkage)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

struct Track {
    std::string id;    // stream index as string
    std::string lang;
    std::string title;
    std::string codec_name;              // e.g. "ass", "subrip", "dvd_subtitle"
    std::vector<uint8_t> extradata;      // codec private data (ASS header for ASS/SSA)
};

struct Chapter {
    double      start_sec = 0.0;
    double      end_sec   = 0.0;
    std::string title;
};

struct ProbeResult {
    int         width        = 0;
    int         height       = 0;
    std::string codec;
    double      duration_sec = 0.0;
    int         video_stream_index = -1;
    // VIDEO_PLAYER_FIX Batch 7.1 — source-stream average frame rate from
    // AVStream::avg_frame_rate. 0.0 when unset or unparseable. Main-app
    // stats badge displays as "— fps" when zero.
    double      fps          = 0.0;

    std::vector<Track> audio;
    std::vector<Track> subs;
    std::vector<Chapter> chapters;

    // Colorspace metadata
    int color_primaries = 0;     // AVCOL_PRI_*
    int color_trc       = 0;     // AVCOL_TRC_*
    int color_space     = 0;     // AVCOL_SPC_*
    int color_range     = 0;     // AVCOL_RANGE_*

    // HDR metadata
    bool   hdr              = false;
    int    max_cll          = 0;
    int    max_fall         = 0;
    double mastering_min_lum = 0.0;
    double mastering_max_lum = 0.0;
};

// Probe a media file: find video stream dimensions/codec, enumerate tracks.
// Returns nullopt on failure (logs to stderr).
std::optional<ProbeResult> probe_file(const std::string& path);
