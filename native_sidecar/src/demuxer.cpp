#include "demuxer.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/mastering_display_metadata.h>
}

// STREAM_PLAYBACK_FIX audit P2 — shared case-insensitive prefix check for
// HTTP scheme detection. All sidecar avformat_open_input call sites use
// this so uppercase URLs like "HTTP://..." (rare but legal per RFC 3986)
// route through the HTTP-options path.
static bool starts_with_ci(const std::string& s, const char* prefix) {
    const size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char a = static_cast<unsigned char>(s[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

static std::string dict_get(AVDictionary* d, const char* key) {
    AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
    return e && e->value ? std::string(e->value) : std::string();
}

std::optional<ProbeResult> probe_file(const std::string& path) {
    AVFormatContext* fmt_ctx = nullptr;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "probesize", "5000000", 0);

    // STREAM_PLAYBACK_FIX hotfix — HTTP options on the probe path.
    //
    // probe_file is the sidecar's open-gatekeeper (main.cpp:272). If it
    // fails, the full VideoDecoder HTTP retry/reconnect logic at
    // video_decoder.cpp:166-180 never runs — the sidecar emits
    // OPEN_FAILED and Qt shows "Cannot open file: probe failed".
    //
    // Pre-fix: probe_file had zero HTTP options. Default ffmpeg HTTP
    // behavior — no reconnect, short implicit read timeouts — made
    // probes over our torrent-backed HTTP server flaky. Any transient
    // waitForPieces stall during probe-read killed the probe before
    // VideoDecoder could be started.
    //
    // Post-fix: mirror VideoDecoder's HTTP options (larger probesize,
    // reconnect enabled, 30s rw_timeout). Probe and decoder now share
    // identical HTTP robustness. Non-HTTP paths (plain file://, local
    // paths) skip the block — default behavior is fine for kernel FS.
    // Audit P2 — case-insensitive (uppercase "HTTP://" is legal per RFC 3986).
    const bool is_http = starts_with_ci(path, "http://")
                      || starts_with_ci(path, "https://");
    if (is_http) {
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "10", 0);
        av_dict_set(&opts, "timeout", "60000000", 0);      // 60s connect
        av_dict_set(&opts, "rw_timeout", "30000000", 0);   // 30s per read
        av_dict_set(&opts, "probesize", "20000000", 0);    // 20 MB probe
        av_dict_set(&opts, "analyzeduration", "10000000", 0); // 10s analyze
        std::fprintf(stderr, "probe_file: HTTP mode enabled for %s\n", path.c_str());
    }

    int ret = avformat_open_input(&fmt_ctx, path.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::fprintf(stderr, "avformat_open_input failed: %s (%s)\n", errbuf, path.c_str());
        return std::nullopt;
    }

    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::fprintf(stderr, "avformat_find_stream_info failed: %s\n", errbuf);
        avformat_close_input(&fmt_ctx);
        return std::nullopt;
    }

    ProbeResult result;

    // Find best video stream
    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx < 0) {
        std::fprintf(stderr, "No video stream found in %s\n", path.c_str());
        avformat_close_input(&fmt_ctx);
        return std::nullopt;
    }

    AVStream* vs = fmt_ctx->streams[video_idx];
    result.video_stream_index = video_idx;
    result.width  = vs->codecpar->width;
    result.height = vs->codecpar->height;

    if (result.width <= 0 || result.height <= 0) {
        std::fprintf(stderr, "Video stream has invalid dimensions: %dx%d\n",
                     result.width, result.height);
        avformat_close_input(&fmt_ctx);
        return std::nullopt;
    }

    // SAR / DAR diagnostic — 2026-04-16. VLC + mpv both report display dim
    // 1920x804 for the Payback HEVC file while our sidecar uses codecpar
    // dims directly. This logs every source of aspect info FFmpeg
    // exposes so we can decide whether to switch source width/height to
    // av_guess_sample_aspect_ratio-derived dims, or leave coded dims
    // alone. Remove after the stuck-top-bar bug closes.
    {
        const AVRational cp_sar  = vs->codecpar->sample_aspect_ratio;
        const AVRational st_sar  = vs->sample_aspect_ratio;
        const AVRational guess   = av_guess_sample_aspect_ratio(fmt_ctx, vs, nullptr);
        int disp_w = result.width;
        int disp_h = result.height;
        if (guess.num > 0 && guess.den > 0 && guess.num != guess.den) {
            if (guess.num > guess.den) {
                disp_w = static_cast<int>(
                    static_cast<int64_t>(result.width) * guess.num / guess.den);
            } else {
                disp_h = static_cast<int>(
                    static_cast<int64_t>(result.height) * guess.den / guess.num);
            }
        }
        std::fprintf(stderr,
            "[ASPECT DIAG] coded=%dx%d codecpar_SAR=%d:%d stream_SAR=%d:%d "
            "guess_SAR=%d:%d sar_derived_display=%dx%d\n",
            result.width, result.height,
            cp_sar.num,  cp_sar.den,
            st_sar.num,  st_sar.den,
            guess.num,   guess.den,
            disp_w,      disp_h);
    }

    const AVCodecDescriptor* desc = avcodec_descriptor_get(vs->codecpar->codec_id);
    result.codec = desc ? desc->name : "unknown";

    // VIDEO_PLAYER_FIX Batch 7.1 — source FPS from AVStream avg_frame_rate.
    // Prefer avg_frame_rate (stable over the whole stream); fall back to
    // r_frame_rate if avg is unset (rare — some demuxers don't populate
    // avg for live streams). Stays 0.0 if both are empty / denominator 0.
    {
        AVRational r = vs->avg_frame_rate;
        if (r.den == 0 || r.num == 0) r = vs->r_frame_rate;
        if (r.den != 0) result.fps = av_q2d(r);
    }

    // Colorspace metadata
    result.color_primaries = vs->codecpar->color_primaries;
    result.color_trc       = vs->codecpar->color_trc;
    result.color_space     = vs->codecpar->color_space;
    result.color_range     = vs->codecpar->color_range;

    // HDR metadata from side data
    for (int sd = 0; sd < vs->codecpar->nb_coded_side_data; ++sd) {
        const AVPacketSideData* side = &vs->codecpar->coded_side_data[sd];
        if (side->type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA) {
            auto* mdm = reinterpret_cast<const AVMasteringDisplayMetadata*>(side->data);
            if (mdm->has_luminance) {
                result.mastering_max_lum = av_q2d(mdm->max_luminance);
                result.mastering_min_lum = av_q2d(mdm->min_luminance);
                result.hdr = (result.mastering_max_lum > 100.0);
            }
        } else if (side->type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL) {
            auto* cll = reinterpret_cast<const AVContentLightMetadata*>(side->data);
            result.max_cll  = cll->MaxCLL;
            result.max_fall = cll->MaxFALL;
            if (result.max_cll > 0) result.hdr = true;
        }
    }
    // Also flag HDR by transfer characteristic (PQ = HDR)
    if (vs->codecpar->color_trc == AVCOL_TRC_SMPTE2084 ||
        vs->codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67) {
        result.hdr = true;
    }

    // Duration
    if (fmt_ctx->duration > 0) {
        result.duration_sec = static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
    }

    // Enumerate audio and subtitle streams
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
        AVStream* s = fmt_ctx->streams[i];
        Track t;
        t.id    = std::to_string(s->index);
        t.lang  = dict_get(s->metadata, "language");
        t.title = dict_get(s->metadata, "title");
        // PLAYER_UX_FIX Phase 6.2 — IINA-parity disposition flags.
        // Default/forced come straight from FFmpeg's demuxer parse of
        // the container's stream disposition bits.
        t.default_flag = (s->disposition & AV_DISPOSITION_DEFAULT) != 0;
        t.forced_flag  = (s->disposition & AV_DISPOSITION_FORCED)  != 0;

        if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // Phase 6.2 — audio-specific richness for the "5.1 · 48kHz"
            // inline hint in TrackPopover. ch_layout.nb_channels is the
            // modern FFmpeg API (post-AVChannelLayout migration); older
            // builds fall back to the deprecated `channels` field which
            // this branch doesn't compile against.
            t.channels    = s->codecpar->ch_layout.nb_channels;
            t.sample_rate = s->codecpar->sample_rate;
            result.audio.push_back(std::move(t));
        } else if (s->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            const AVCodecDescriptor* sdesc = avcodec_descriptor_get(s->codecpar->codec_id);
            t.codec_name = sdesc ? sdesc->name : "unknown";
            if (s->codecpar->extradata && s->codecpar->extradata_size > 0) {
                t.extradata.assign(s->codecpar->extradata,
                                   s->codecpar->extradata + s->codecpar->extradata_size);
            }
            result.subs.push_back(std::move(t));
        }
    }

    // Extract chapters
    for (unsigned i = 0; i < fmt_ctx->nb_chapters; ++i) {
        AVChapter* ch = fmt_ctx->chapters[i];
        Chapter c;
        c.start_sec = ch->start * av_q2d(ch->time_base);
        c.end_sec   = ch->end   * av_q2d(ch->time_base);
        c.title     = dict_get(ch->metadata, "title");
        if (c.title.empty())
            c.title = "Chapter " + std::to_string(i + 1);
        result.chapters.push_back(std::move(c));
    }

    avformat_close_input(&fmt_ctx);
    return result;
}
