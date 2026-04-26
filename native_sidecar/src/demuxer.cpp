#include "demuxer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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

// STREAM_DURATION_FIX_FOR_PACKS 2026-04-21 — last-resort per-stream scan
// used by the two DISCARD branches in the duration resolution block
// (FROM_BITRATE + subs-contaminated-container) before returning 0.
//
// Rationale: MKV packs whose Segment Duration is FROM_BITRATE or where
// the video stream's own duration is AV_NOPTS_VALUE (Cues parsing
// didn't reach end-of-file during probe) almost always STILL have
// reliable AUDIO stream duration. Audio packets are smaller + more
// densely scanned during avformat_find_stream_info + their AVStream
// ::duration field populates from track-level container metadata that
// libavformat trusts independently of the Segment-level inheritance
// logic. Taking the max over non-subtitle streams gives the authoritative
// content length without trusting the container-level Segment Duration
// that STREAM_DURATION_FIX (commit c27ce5d) rejects on suspicion of
// subtitle contamination.
//
// Subtitle + attachment streams excluded: they're the exact population
// STREAM_DURATION_FIX was designed to ignore — subs-contaminated MKVs
// inherit a stale container duration onto all subtitle tracks, so
// picking up their `duration` field would reintroduce the 1h-as-2h lie.
//
// Sanity threshold: chosen duration >= 60s. Protects against probe-
// sample-only durations that might be set from a 5-10s analyzed window
// at Tier 1. Pack-torrent episodes are 20-60+ minutes; the 60s floor
// excludes any sample-duration artifact without sacrificing legit short
// content (shorts / previews pass this unless < 1 min, which would be
// an odd scope for streaming).
//
// Called only from branches 2 and 4 of the duration resolution hierarchy
// — branches 1 (reliable video-stream duration) and 3 (reliable FROM_PTS
// container duration) never reach this fallback.
static double try_stream_max_duration(AVFormatContext* fmt_ctx)
{
    if (!fmt_ctx) return 0.0;
    double best = 0.0;
    int    winner_idx = -1;
    int    winner_type = AVMEDIA_TYPE_UNKNOWN;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
        AVStream* s = fmt_ctx->streams[i];
        if (!s || !s->codecpar) continue;
        const AVMediaType t = s->codecpar->codec_type;
        if (t != AVMEDIA_TYPE_VIDEO && t != AVMEDIA_TYPE_AUDIO) continue;
        if (s->duration == AV_NOPTS_VALUE) continue;
        if (s->time_base.den <= 0 || s->time_base.num <= 0) continue;
        const double d = static_cast<double>(s->duration) * av_q2d(s->time_base);
        if (d >= 60.0 && d > best) {
            best = d;
            winner_idx = static_cast<int>(i);
            winner_type = t;
        }
    }
    if (best > 0.0 && winner_idx >= 0) {
        const char* type_str = (winner_type == AVMEDIA_TYPE_VIDEO) ? "video"
                             : (winner_type == AVMEDIA_TYPE_AUDIO) ? "audio"
                             : "other";
        std::fprintf(stderr,
            "probe_file: stream-max fallback picked stream=%d type=%s dur=%.1fs "
            "(container duration unreliable — using reliable per-stream value "
            "instead of 0)\n",
            winner_idx, type_str, best);
        return best;
    }
    std::fprintf(stderr,
        "probe_file: stream-max fallback found no stream with duration >= 60s "
        "(nb_streams=%u); duration stays 0\n",
        fmt_ctx->nb_streams);
    return 0.0;
}

// STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — last-resort estimate
// after try_stream_max_duration also returns 0. Computes
//   duration ≈ total_size_bytes * 8 / fmt_ctx->bit_rate
// Target class: pathological pack torrents (e.g. Invincible S01E02
// Torrentio EZTV) where all video + audio streams ALSO lack reliable
// AVStream::duration (only subtitles carry the subs-contaminated stale
// container duration that STREAM_DURATION_FIX was designed to reject).
// Wake 1's per-stream-max fallback correctly declined these; Wake 2
// rescues UX by accepting an approximate bitrate × size estimate,
// flagged as estimate so the main-app HUD prefixes the display with
// `~` (tilde) to keep Hemanth's anti-lie rule intact.
//
// Sanity bounds: 10s to 10h (10 * 3600s). Guards against:
//   - bit_rate=0 or negative (undetected → div-by-zero / negative dur)
//   - avio_size=-1 or 0 (unknown HTTP Content-Length → bogus fraction)
//   - truncated files where bytes × 8 / bitrate gives absurd durations
//   - head-bitrate anomalies on very-small-probesize tiers where the
//     observed bit_rate is wildly non-representative
//
// Accuracy: VBR content can have per-minute bitrate varying 50%+ from
// average, so estimate error is ~10-50%. For ~42min content this means
// the displayed `~42:00` might be actual 35-50min. Acceptable under the
// tilde-prefix UX contract: user knows it's approximate + can seek
// interactively, which is infinitely better than being frozen at 0.
//
// Returns 0.0 if any sanity check fails; non-zero duration otherwise.
// Caller sets result.duration_is_estimate = true when this returns
// non-zero.
static double try_bitrate_filesize_fallback(AVFormatContext* fmt_ctx)
{
    if (!fmt_ctx) return 0.0;
    if (fmt_ctx->bit_rate <= 0) {
        std::fprintf(stderr,
            "probe_file: bitrate fallback skipped (fmt_ctx->bit_rate=%lld, "
            "not positive)\n",
            (long long)fmt_ctx->bit_rate);
        return 0.0;
    }
    if (!fmt_ctx->pb) {
        std::fprintf(stderr,
            "probe_file: bitrate fallback skipped (fmt_ctx->pb is null — no "
            "AVIOContext to query size)\n");
        return 0.0;
    }
    const int64_t size_bytes = avio_size(fmt_ctx->pb);
    if (size_bytes <= 0) {
        std::fprintf(stderr,
            "probe_file: bitrate fallback skipped (avio_size=%lld, "
            "no Content-Length / unknown file size)\n",
            (long long)size_bytes);
        return 0.0;
    }
    const double est = static_cast<double>(size_bytes) * 8.0
                     / static_cast<double>(fmt_ctx->bit_rate);
    constexpr double kMinSec = 10.0;
    constexpr double kMaxSec = 10.0 * 3600.0;  // 10 hours
    if (est < kMinSec || est > kMaxSec) {
        std::fprintf(stderr,
            "probe_file: bitrate fallback rejected est=%.1fs — out of sane "
            "range [%.0fs, %.0fs] (size_bytes=%lld bit_rate=%lld)\n",
            est, kMinSec, kMaxSec, (long long)size_bytes,
            (long long)fmt_ctx->bit_rate);
        return 0.0;
    }
    std::fprintf(stderr,
        "probe_file: bitrate fallback picked dur=%.1fs (size=%lld bytes × 8 "
        "/ bitrate=%lld bps) — ESTIMATE; main-app will prefix HUD with tilde\n",
        est, (long long)size_bytes, (long long)fmt_ctx->bit_rate);
    return est;
}

static std::string dict_get(AVDictionary* d, const char* key) {
    AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
    return e && e->value ? std::string(e->value) : std::string();
}

// STREAM_ENGINE_REBUILD P4 — three-tier HTTP probe escalation. Replaces
// the single-shot 20MB / 10s analyzeduration / 30s rw_timeout that was the
// second Mode-A cold-start latency floor (first floor = StreamHttpServer
// poll-sleep, addressed by P2). Tier budgets per STREAM_ENGINE_REBUILD_TODO
// §4.1 — fast-swarm cases clear Tier 1 in under a second; Tier 3 is the
// last resort before surfacing OPEN_FAILED.
//
// Non-HTTP paths (file://, local FS) stay single-attempt with the pre-P4
// 5MB probesize — kernel FS has no "slow piece" equivalent to escalate for.
std::optional<ProbeResult> probe_file(const std::string& path) {
    const bool is_http = starts_with_ci(path, "http://")
                      || starts_with_ci(path, "https://");

    struct TierSpec {
        int     tier;
        int64_t probesize;         // bytes
        int64_t analyzeduration_us;// microseconds; 0 = leave at ffmpeg default
        int64_t rw_timeout_us;     // microseconds; 0 = leave at ffmpeg default
    };

    TierSpec tiers[3];
    int tier_count = 0;
    if (is_http) {
        // STREAM_DURATION_FIX_FOR_PACKS Wake 2 2026-04-21 — unified
        // rw_timeout across all probe tiers to 30s, mirroring the earlier
        // same-session fix in video_decoder.cpp:233. Same pathology: the
        // 5s Tier-1 rw_timeout was chosen for fast probe-failure but
        // persists on the HTTP AVIOContext through every read for the
        // life of the probe, and TCP backpressure from a frame-rate-
        // limited consumer or a briefly-starved torrent piece can pause
        // reads >5s, tripping rw_timeout → reconnect_streamed loop.
        // Wake 1 smoke (2026-04-21 12:26) observed 45 "Stream ends
        // prematurely" log lines traced to this remaining 5s/15s
        // leftover in probe tiers. Dead-source detection unchanged —
        // still bounded by the `timeout=60s` connect cap set per-tier
        // below. Worst-case probe-through-all-tiers grows 50s → 90s on
        // truly-dead sources; acceptable since Torrentio surfaces only
        // live magnets.
        tiers[0] = {1,   512 * 1024,      750 * 1000,      30 * 1000 * 1000};
        tiers[1] = {2, 2 * 1024 * 1024, 2 * 1000 * 1000,   30 * 1000 * 1000};
        tiers[2] = {3, 5 * 1024 * 1024, 5 * 1000 * 1000,   30 * 1000 * 1000};
        tier_count = 3;
    } else {
        tiers[0] = {1, 5 * 1000 * 1000, 0, 0};
        tier_count = 1;
    }

    AVFormatContext* fmt_ctx = nullptr;
    TierSpec chosen{};
    const auto probe_t0 = std::chrono::steady_clock::now();
    std::string last_err = "probe failed";

    for (int i = 0; i < tier_count; ++i) {
        const TierSpec& t = tiers[i];

        AVDictionary* opts = nullptr;
        char num[32];
        std::snprintf(num, sizeof(num), "%lld", (long long)t.probesize);
        av_dict_set(&opts, "probesize", num, 0);

        if (is_http) {
            av_dict_set(&opts, "reconnect", "1", 0);
            av_dict_set(&opts, "reconnect_streamed", "1", 0);
            av_dict_set(&opts, "reconnect_delay_max", "5", 0);  // STREAM_STALL_FIX Phase 1 — mpv stream-lavf-o parity (was 10)
            av_dict_set(&opts, "timeout", "60000000", 0);  // 60s connect (unchanged)
            std::snprintf(num, sizeof(num), "%lld", (long long)t.rw_timeout_us);
            av_dict_set(&opts, "rw_timeout", num, 0);
            std::snprintf(num, sizeof(num), "%lld", (long long)t.analyzeduration_us);
            av_dict_set(&opts, "analyzeduration", num, 0);
        }

        std::fprintf(stderr,
            "probe_file: Tier %d attempt (probesize=%lld analyzedur=%lldus "
            "rw_timeout=%lldus) %s\n",
            t.tier, (long long)t.probesize, (long long)t.analyzeduration_us,
            (long long)t.rw_timeout_us, path.c_str());

        int ret = avformat_open_input(&fmt_ctx, path.c_str(), nullptr, &opts);
        av_dict_free(&opts);

        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::fprintf(stderr,
                "probe_file: Tier %d avformat_open_input failed: %s\n",
                t.tier, errbuf);
            last_err = std::string("open:") + errbuf;
            if (fmt_ctx) { avformat_close_input(&fmt_ctx); fmt_ctx = nullptr; }
            continue;
        }

        // Mirror AVDictionary probesize/analyzeduration onto fmt_ctx —
        // some demuxers read fields directly rather than via opts (retained
        // from pre-P4 HTTP path).
        if (is_http) {
            fmt_ctx->probesize = t.probesize;
            fmt_ctx->max_analyze_duration = t.analyzeduration_us;
        }

        ret = avformat_find_stream_info(fmt_ctx, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::fprintf(stderr,
                "probe_file: Tier %d avformat_find_stream_info failed: %s\n",
                t.tier, errbuf);
            last_err = std::string("find_stream_info:") + errbuf;
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
            continue;
        }

        // STREAM_DURATION_FIX — fmt_ctx->duration is a MAX over every stream's
        // AVStream::duration. For MKVs where the video+audio stream durations
        // are not yet resolvable (small probesize, no Cues-at-end visit), the
        // max picks up whatever subtitle/attachment streams carry — which is
        // the container's Segment Info Duration field verbatim. Real repro:
        // hemanth's 59-min One Piece EZTV mkv carries Segment Duration =
        // 7192.56s, which libavformat inherits onto all 57 subtitle tracks
        // while the video/audio streams hold AV_NOPTS_VALUE. fmt_ctx->duration
        // then reports the inflated 7192s and duration_estimation_method tags
        // it as FROM_STREAM (honest — it DID come from a stream, just not
        // one that corresponds to playback length).
        //
        // Escalate on HTTP when the passing tier either (a) fell back to
        // FROM_BITRATE (the classic head-bitrate-extrapolation case), or (b)
        // the video stream's own duration is still unknown (AV_NOPTS_VALUE),
        // which forces fmt_ctx->duration to inherit from subs/attachments. A
        // larger probesize frequently lets the matroska demuxer visit Cues at
        // the tail of the segment via HTTP range-request, which populates
        // video->duration directly. Final tier accepts what it gets; the
        // post-loop duration extraction independently guards against trusting
        // FROM_BITRATE or a bare container-level-only duration.
        AVStream* vs_probe = (fmt_ctx->nb_streams > 0)
            ? fmt_ctx->streams[av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO,
                                                   -1, -1, nullptr, 0)]
            : nullptr;
        const bool video_duration_known =
            vs_probe && vs_probe->duration != AV_NOPTS_VALUE;
        const bool from_bitrate =
            fmt_ctx->duration_estimation_method == AVFMT_DURATION_FROM_BITRATE;
        const bool subs_contaminated_container =
            !video_duration_known && fmt_ctx->duration > 0
            && fmt_ctx->duration_estimation_method == AVFMT_DURATION_FROM_STREAM;

        if (is_http && i < tier_count - 1
            && (from_bitrate || subs_contaminated_container)) {
            const char* why = from_bitrate
                ? "FROM_BITRATE (head-bitrate extrapolation)"
                : "video stream duration unknown; container duration inherited "
                  "from subtitle/attachment stream (unreliable)";
            std::fprintf(stderr,
                "probe_file: Tier %d passed but duration-source is unreliable: %s; "
                "escalating to Tier %d\n",
                t.tier, why, tiers[i + 1].tier);
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
            last_err = "duration unreliable";
            continue;
        }

        chosen = t;
        std::fprintf(stderr,
            "probe_file: Tier %d passed (duration_estimation_method=%d, "
            "video_dur_known=%d)\n",
            t.tier, fmt_ctx->duration_estimation_method,
            video_duration_known ? 1 : 0);
        break;
    }

    if (!fmt_ctx) {
        std::fprintf(stderr,
            "probe_file: all %d tier(s) exhausted for %s (last: %s)\n",
            tier_count, path.c_str(), last_err.c_str());
        return std::nullopt;
    }

    const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - probe_t0).count();

    ProbeResult result;
    result.probe_tier           = chosen.tier;
    result.probe_elapsed_ms     = elapsed_ms;
    result.probesize_used       = chosen.probesize;
    result.analyzeduration_used = chosen.analyzeduration_us;

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

    // LIBPLACEBO_SINGLE_RENDERER_FIX P1 2026-04-25 — resolve UNSPECIFIED
    // colorspace fields to industry-standard defaults before they reach the
    // GpuRenderer. Mirrors mpv + swscale behavior: HD content (>=720p)
    // defaults to BT.709 / BT.709 / BT.709; SD defaults to BT.601 / BT.709
    // / BT.601 (matrix follows resolution; transfer keeps BT.709 since SD
    // PAL/NTSC content delivered digitally is almost always re-mastered to
    // 1.961 gamma). Range UNSPECIFIED → TV/MPEG (limited 16-235), the
    // overwhelmingly common consumer case. Only touches values that are
    // AVCOL_*_UNSPECIFIED (==2 for primaries/trc/space, ==0 for range);
    // explicit values from the codecpar pass through unchanged.
    {
        const bool is_hd = (result.height >= 720);
        if (result.color_primaries == AVCOL_PRI_UNSPECIFIED) {
            result.color_primaries = is_hd ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        }
        if (result.color_trc == AVCOL_TRC_UNSPECIFIED) {
            result.color_trc = AVCOL_TRC_BT709;
        }
        if (result.color_space == AVCOL_SPC_UNSPECIFIED) {
            result.color_space = is_hd ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        }
        if (result.color_range == AVCOL_RANGE_UNSPECIFIED) {
            result.color_range = AVCOL_RANGE_MPEG;
        }
        std::fprintf(stderr,
            "[PROBE] colorspace primaries=%d trc=%d space=%d range=%d hdr=%d (%dx%d)\n",
            result.color_primaries, result.color_trc,
            result.color_space, result.color_range,
            result.hdr ? 1 : 0, result.width, result.height);
    }

    // Duration resolution — see STREAM_DURATION_FIX block in the tier loop.
    //
    // Priority order, least-to-most-pathological:
    //   1. Video stream's own AVStream::duration (ground truth for playback
    //      length — libavformat sets this from the real last-packet PTS when
    //      Cues are reachable, or from the container's Segment Duration field
    //      when it correlates with the video track).
    //   2. fmt_ctx->duration — ONLY if the video stream's duration was known.
    //      If video duration is AV_NOPTS_VALUE but fmt_ctx->duration is set,
    //      the max came from audio/sub streams; on many MKVs the subs carry
    //      the container-level Segment Duration which can be wildly wrong
    //      (repro: 59-min One Piece mkv whose Segment Duration = 7192s =
    //      2× real, inherited onto all 57 subtitle tracks).
    //   3. FROM_BITRATE estimates — never trusted; small-probesize head
    //      bitrate is rarely representative.
    //   4. Unknown (result.duration_sec = 0) — HUD shows "—:—" honestly.
    //
    // Frame-count cross-check (nb_frames / avg_frame_rate) is computed only
    // as a secondary sanity check logged to stderr — it's unreliable on MKV
    // (nb_frames is frequently 0 unless Cues are parsed) so it doesn't gate
    // the fallback hierarchy.
    result.duration_estimation_method = fmt_ctx->duration_estimation_method;

    AVStream* vs_dur = (video_idx >= 0) ? fmt_ctx->streams[video_idx] : nullptr;
    const bool video_duration_known =
        vs_dur && vs_dur->duration != AV_NOPTS_VALUE;
    const double video_stream_duration_sec = video_duration_known
        ? static_cast<double>(vs_dur->duration) * av_q2d(vs_dur->time_base)
        : 0.0;

    double frame_count_duration_sec = 0.0;
    if (vs_dur) {
        const double fps = (vs_dur->avg_frame_rate.den > 0)
            ? av_q2d(vs_dur->avg_frame_rate) : 0.0;
        if (vs_dur->nb_frames > 0 && fps > 0.0) {
            frame_count_duration_sec =
                static_cast<double>(vs_dur->nb_frames) / fps;
        }
        std::fprintf(stderr,
            "probe_file: video stream duration=%lldus nb_frames=%lld "
            "avg_fps=%.3f frame_count_est=%.1fs\n",
            (long long)vs_dur->duration, (long long)vs_dur->nb_frames, fps,
            frame_count_duration_sec);
    }

    const double container_duration_sec = (fmt_ctx->duration > 0)
        ? static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE : 0.0;

    if (video_duration_known
        && fmt_ctx->duration_estimation_method != AVFMT_DURATION_FROM_BITRATE) {
        // Preferred path — video stream has its own duration.
        result.duration_sec = video_stream_duration_sec;
        if (container_duration_sec > 0.0
            && std::abs(container_duration_sec - video_stream_duration_sec)
               > 0.05 * std::max(video_stream_duration_sec, 1.0)) {
            std::fprintf(stderr,
                "probe_file: container dur=%.1fs differs from video stream dur "
                "%.1fs (using video stream — subtitle/attachment streams likely "
                "carry a stale container-header duration)\n",
                container_duration_sec, video_stream_duration_sec);
        }
    } else if (fmt_ctx->duration_estimation_method == AVFMT_DURATION_FROM_BITRATE
               && fmt_ctx->duration > 0) {
        std::fprintf(stderr,
            "probe_file: DISCARDING duration=%lldus (estimation=FROM_BITRATE, "
            "unreliable on small-probesize HTTP streams)\n",
            (long long)fmt_ctx->duration);
        // STREAM_DURATION_FIX_FOR_PACKS 2026-04-21 — before giving up,
        // try the per-stream-max fallback. Audio streams typically have
        // reliable AVStream::duration even when the container-level
        // estimate is FROM_BITRATE junk; using that value lets seek-math
        // in VideoPlayer work correctly on pack torrents that would
        // otherwise collapse to durationSec=0 → scrub-bar-to-zero + HUD
        // em-dash-right-side + seek-to-zero.
        result.duration_sec = try_stream_max_duration(fmt_ctx);
        // Wake 2 2026-04-21 — if per-stream-max also returned 0 (no
        // reliable video/audio stream duration), try the bitrate × size
        // estimate. Flagged as estimate so HUD renders `~N:NN`. Applies
        // only to the pathological pack class where all streams lack
        // AVStream::duration but bit_rate + Content-Length are known.
        if (result.duration_sec <= 0.0) {
            const double est = try_bitrate_filesize_fallback(fmt_ctx);
            if (est > 0.0) {
                result.duration_sec = est;
                result.duration_is_estimate = true;
            }
        }
    } else if (fmt_ctx->duration_estimation_method == AVFMT_DURATION_FROM_PTS
               && container_duration_sec > 0.0) {
        // FROM_PTS is libavformat's way of saying "derived by observing packet
        // PTSes across the whole analyzed window". Even when the per-stream
        // AVStream::duration field is unset, FROM_PTS is reliable for the
        // content that was actually scanned. Accept it.
        result.duration_sec = container_duration_sec;
    } else if (!video_duration_known && container_duration_sec > 0.0) {
        std::fprintf(stderr,
            "probe_file: DISCARDING container dur=%.1fs — video stream duration "
            "is AV_NOPTS_VALUE, so fmt_ctx->duration was inherited from audio/"
            "subtitle/attachment streams (unreliable on MKVs whose Segment "
            "Info Duration field disagrees with real playback length)\n",
            container_duration_sec);
        // STREAM_DURATION_FIX_FOR_PACKS 2026-04-21 — same fallback as the
        // FROM_BITRATE branch above. Subs-contamination discards the
        // CONTAINER-level duration (which inherited from subtitle streams)
        // but the per-stream audio duration is still populated from its
        // own track-level metadata and is trustworthy. This rescues the
        // pack-torrent case where Hemanth saw scrub-bar / seek buttons /
        // HUD time all break from durationSec=0 propagation 2026-04-21.
        result.duration_sec = try_stream_max_duration(fmt_ctx);
        // Wake 2 2026-04-21 — if per-stream-max also returned 0 (the
        // pathological Invincible S01E02 pack class where ALL 33 streams
        // lack AVStream::duration), try bitrate × size estimate. Flagged
        // so HUD renders with tilde prefix.
        if (result.duration_sec <= 0.0) {
            const double est = try_bitrate_filesize_fallback(fmt_ctx);
            if (est > 0.0) {
                result.duration_sec = est;
                result.duration_is_estimate = true;
            }
        }
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
