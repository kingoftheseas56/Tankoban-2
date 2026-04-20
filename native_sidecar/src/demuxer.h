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

    // PLAYER_UX_FIX Phase 6.2 — IINA-parity metadata for Tracks popover.
    // Sourced from AVStream::disposition flags + AVCodecParameters on
    // audio streams. Optional fields default to neutral (false / 0)
    // so TrackPopover tolerates legacy sidecar binaries that predate
    // Phase 6.2 — just renders without the badge / channel hint.
    bool default_flag = false;           // AV_DISPOSITION_DEFAULT
    bool forced_flag  = false;           // AV_DISPOSITION_FORCED
    int  channels     = 0;               // audio only; 0 = not applicable
    int  sample_rate  = 0;               // audio only, Hz; 0 = not applicable
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

    // STREAM_ENGINE_REBUILD P4 — probe-tier telemetry. 1 for fast-swarm
    // Tier-1 success, 2/3 for escalations. Zero on non-HTTP (single attempt).
    // main.cpp emits probe_tier_passed after probe_done using these fields.
    int     probe_tier           = 1;
    int64_t probe_elapsed_ms     = 0;
    int64_t probesize_used       = 0;
    int64_t analyzeduration_used = 0;

    // STREAM_DURATION_FIX — AVFormatContext::duration_estimation_method snapshot
    // from the tier that passed. Values:
    //   0 = AVFMT_DURATION_FROM_PTS     (derived from stream PTSes — accurate)
    //   1 = AVFMT_DURATION_FROM_STREAM  (read from stream/container field — accurate)
    //   2 = AVFMT_DURATION_FROM_BITRATE (derived from observed bitrate × size — UNRELIABLE)
    // When == 2, duration_sec is forced to 0 upstream because bitrate-estimates
    // on stream-mode HTTP probes with small probesize frequently double the
    // true duration (head-credits bitrate << average content bitrate). HUD
    // then shows "—:—" instead of a wildly-wrong 2h reading for a 1h file.
    int duration_estimation_method = 0;
};

// Probe a media file: find video stream dimensions/codec, enumerate tracks.
// Returns nullopt on failure (logs to stderr).
std::optional<ProbeResult> probe_file(const std::string& path);
