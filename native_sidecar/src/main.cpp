#include "audio_decoder.h"
#include "av_sync_clock.h"
#include "d3d11_presenter.h"
#include "demuxer.h"
#include "filter_graph.h"
#include "gpu_renderer.h"
#include "heartbeat.h"
#include "protocol.h"
#include "ring_buffer.h"
#include "shm_helpers.h"
#include "state_machine.h"
#include "subtitle_renderer.h"
#include "video_decoder.h"
#include "volume_control.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <portaudio.h>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

// ---------------------------------------------------------------------------
// Diagnostic log — writes AVSYNC_DIAG lines to a file for debugging
// ---------------------------------------------------------------------------
static FILE* g_diag_file = nullptr;

static void diag_open() {
    if (g_diag_file) return;
    // Write next to the executable
    g_diag_file = std::fopen("avsync_diag.log", "w");
    if (g_diag_file) std::setvbuf(g_diag_file, nullptr, _IONBF, 0);  // unbuffered
}

static void diag_log(const char* fmt, ...) {
    if (!g_diag_file) return;
    // Timestamp
    auto now = std::chrono::steady_clock::now();
    static auto start = now;
    double ms = std::chrono::duration<double, std::milli>(now - start).count();
    std::fprintf(g_diag_file, "[%8.1fms] ", ms);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_diag_file, fmt, args);
    va_end(args);
    std::fprintf(g_diag_file, "\n");
    // Also to stderr for good measure
    std::fprintf(stderr, "AVSYNC_DIAG ");
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

// Forward declarations
static void preload_subtitle_packets(SubtitleRenderer* renderer,
                                      const std::string& path,
                                      int stream_index);

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static StateMachine  g_state;
static Heartbeat     g_heartbeat;
static AVSyncClock   g_clock;
static VolumeControl g_volume;

// Audio device fingerprint — captured once at startup, emitted in media_info
// so Qt can recall per-device A/V offsets (Bluetooth/HDMI hidden latency).
static std::string g_audio_device_name;
static std::string g_audio_host_api_name;

// Zero-copy short-circuit. Set to true by Qt after it successfully imports
// the D3D11 shared texture (Holy Grail). When active AND the frame is
// HW-decoded AND no subtitle blending is needed, the video decoder skips
// the entire CPU pipeline (hwframe_transfer, sws_scale, SHM write) and only
// does the GPU→GPU CopySubresourceRegion. Producer per-frame cost drops
// from ~20ms to ~1ms, eliminating residual stutter from CPU spikes.
static std::atomic<bool> g_zero_copy_active{false};

// Pre-warmed PortAudio stream. Opened once at sidecar startup so the
// 5+ second cold-start (especially Bluetooth devices like AirPods) doesn't
// freeze the user's first video for several seconds. AudioDecoder uses this
// stream and routes file audio through swresample to its fixed format.
static PaStream* g_pa_stream = nullptr;
static double    g_pa_actual_latency = 0.0;
static constexpr int PREWARM_SAMPLE_RATE = 48000;
static constexpr int PREWARM_CHANNELS    = 2;

static constexpr int DECODE_RING_SLOT_COUNT = 4;

// Active session state (protected by g_session_mutex)
static std::mutex           g_session_mutex;
static ShmRegion            g_shm;
static FrameRingWriter*     g_ring_writer  = nullptr;
static VideoDecoder*        g_video_dec    = nullptr;
static AudioDecoder*        g_audio_dec    = nullptr;
static double               g_probe_duration = 0.0;
static std::string          g_current_path;
static std::string          g_active_audio_id;
static std::string          g_active_sub_id;
static SubtitleRenderer*    g_sub_renderer = nullptr;
static std::vector<Track>   g_probe_subs;
static int                  g_video_storage_w = 0;
static int                  g_video_storage_h = 0;
static std::atomic<int>     g_canvas_w{0};
static std::atomic<int>     g_canvas_h{0};
static FilterGraph*         g_video_filter = nullptr;
static FilterGraph*         g_audio_filter = nullptr;
static GpuRenderer*         g_gpu_renderer = nullptr;
#ifdef _WIN32
static D3D11Presenter*      g_d3d_presenter = nullptr;
#endif

// Open runs on a worker thread so the main stdin loop stays responsive to pings
static std::thread          g_open_thread;

// time_update emitter
static std::atomic<bool>    g_tu_stop{true};
static std::thread          g_tu_thread;

// ---------------------------------------------------------------------------
// Time-update emitter thread — uses AVSyncClock when audio is active
// ---------------------------------------------------------------------------

static void time_update_thread_func(std::string sid, double duration_sec) {
    while (!g_tu_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_tu_stop.load()) break;

        State st = g_state.state();
        if (st != State::PLAYING && st != State::PAUSED) continue;

        // Use clock position (anchored by audio PTS updates)
        double pos_sec;
        if (g_clock.started()) {
            pos_sec = static_cast<double>(g_clock.position_us()) / 1000000.0;
        } else {
            // Fallback: no audio → wall-clock estimate (shouldn't happen with audio)
            continue;
        }

        nlohmann::json p;
        p["positionSec"]  = static_cast<int>(pos_sec * 1000) / 1000.0;
        if (duration_sec > 0.0)
            p["durationSec"] = static_cast<int>(duration_sec * 1000) / 1000.0;
        write_event("time_update", sid, -1, p);
    }
}

static void stop_time_update() {
    g_tu_stop.store(true);
    if (g_tu_thread.joinable())
        g_tu_thread.join();
}

static void start_time_update(const std::string& sid, double duration_sec) {
    stop_time_update();
    g_tu_stop.store(false);
    g_tu_thread = std::thread(time_update_thread_func, sid, duration_sec);
}

static void join_open_thread() {
    if (g_open_thread.joinable())
        g_open_thread.join();
}

// ---------------------------------------------------------------------------
// Teardown — stop audio + video + clean SHM
// ---------------------------------------------------------------------------

static void teardown_decode() {
    join_open_thread();
    stop_time_update();

    // Stop audio and video concurrently for bounded teardown time
    std::thread audio_stop_thread;
    std::thread video_stop_thread;

    {
        std::lock_guard<std::mutex> lock(g_session_mutex);

        if (g_audio_dec) {
            AudioDecoder* ad = g_audio_dec;
            g_audio_dec = nullptr;
            audio_stop_thread = std::thread([ad]() {
                ad->stop();
                delete ad;
            });
        }

        if (g_video_dec) {
            VideoDecoder* vd = g_video_dec;
            g_video_dec = nullptr;
            video_stop_thread = std::thread([vd]() {
                vd->stop();
                delete vd;
            });
        }
    }

    if (audio_stop_thread.joinable()) audio_stop_thread.join();
    if (video_stop_thread.joinable()) video_stop_thread.join();

    {
        std::lock_guard<std::mutex> lock(g_session_mutex);

        if (g_ring_writer) {
            delete g_ring_writer;
            g_ring_writer = nullptr;
        }

        if (g_shm.ptr) {
            cleanup_shm(g_shm);
        }

        g_probe_duration = 0.0;
        g_current_path.clear();
        g_active_audio_id.clear();
        g_active_sub_id.clear();
        g_probe_subs.clear();
        g_video_storage_w = 0;
        g_video_storage_h = 0;

        if (g_sub_renderer) {
            delete g_sub_renderer;
            g_sub_renderer = nullptr;
        }
        if (g_video_filter) {
            delete g_video_filter;
            g_video_filter = nullptr;
        }
        if (g_audio_filter) {
            delete g_audio_filter;
            g_audio_filter = nullptr;
        }
        if (g_gpu_renderer) {
            delete g_gpu_renderer;
            g_gpu_renderer = nullptr;
        }
    }

    g_clock.reset();
}

// ---------------------------------------------------------------------------
// open command handler
// ---------------------------------------------------------------------------

// The heavy open work runs on a background thread so the main stdin loop
// stays responsive to pings/heartbeats during probe + audio startup.
static void open_worker(Command cmd) {
    const std::string sid = cmd.sessionId;

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — wall-clock anchor for Phase 1
    // diagnostic events (probe_start/done, decoder_open_start/done,
    // first_packet_read, first_decoder_receive, first_frame additive delta).
    // Anchored at open_worker entry to capture the full user-visible open
    // interval. Independent of the t0_open at the audio-init site below,
    // which measures audio-startup latency from mid-open_worker.
    const auto open_start_time = std::chrono::steady_clock::now();
    auto t_ms_from_open = [&open_start_time]() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - open_start_time).count();
    };

    // Extract payload
    std::string path = cmd.payload.value("path", "");
    double start_sec = cmd.payload.value("startSeconds", 0.0);
    int open_canvas_w = cmd.payload.value("canvasWidth", 0);
    int open_canvas_h = cmd.payload.value("canvasHeight", 0);

    if (path.empty()) {
        write_error("OPEN_FAILED", "No path in open payload", sid);
        g_state.set_state(State::IDLE);
        write_event("state_changed", sid, -1, {{"state", "idle"}});
        return;
    }

    // --- 1. Probe ---
    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — probe_start event. Fires
    // immediately before probe_file() to anchor the probe-window measurement.
    write_event("probe_start", sid, -1, {
        {"t_ms_from_open", t_ms_from_open()}
    });

    std::fprintf(stderr, "TIMING open start sid=%s target=%.3fs\n", sid.c_str(), start_sec);
    const auto probe_call_start = std::chrono::steady_clock::now();
    auto probe = probe_file(path);
    const int64_t analyze_duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - probe_call_start).count();
    if (!probe.has_value()) {
        write_error("OPEN_FAILED", "Cannot open file: probe failed", sid);
        g_state.set_state(State::IDLE);
        write_event("state_changed", sid, -1, {{"state", "idle"}});
        return;
    }

    if (g_state.state() != State::OPEN_PENDING) {
        std::fprintf(stderr, "Open cancelled after probe\n");
        return;
    }

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — probe_done event. Fires after
    // probe_file returns successfully and the cancellation check passes.
    // analyze_duration_ms measures time spent in probe_file (inclusive of
    // avformat_open_input + avformat_find_stream_info). stream_count is the
    // total track count (1 video + N audio + M subtitle).
    write_event("probe_done", sid, -1, {
        {"t_ms_from_open", t_ms_from_open()},
        {"analyze_duration_ms", analyze_duration_ms},
        {"stream_count", static_cast<int>(1 + probe->audio.size() + probe->subs.size())},
        {"duration_ms", static_cast<int64_t>(probe->duration_sec * 1000.0)}
    });

    // STREAM_ENGINE_REBUILD P4 — probe_tier_passed event. Reports which
    // of the three HTTP probe tiers (512KB/2MB/5MB) succeeded plus the
    // budgets used, so stream_telemetry.log grep can separate fast-swarm
    // Tier-1 passes from slow-swarm Tier-2/3 escalations. Non-HTTP paths
    // emit tier=1 with a 5MB probesize and zero analyzeduration (single
    // attempt, no escalation).
    write_event("probe_tier_passed", sid, -1, {
        {"t_ms_from_open", t_ms_from_open()},
        {"tier", probe->probe_tier},
        {"elapsed_ms", probe->probe_elapsed_ms},
        {"probesize", probe->probesize_used},
        {"analyzeduration_us", probe->analyzeduration_used}
    });

    int width  = probe->width;
    int height = probe->height;
    // Option A rollback (canvas-plane neutralized). Subtitle overlay stays
    // at video dimensions; main app draws the overlay at the video rect on
    // screen, so sub positioning is traditional and aspect-agnostic. Canvas
    // payload is accepted for protocol compatibility but ignored.
    (void)open_canvas_w;
    (void)open_canvas_h;
    g_canvas_w.store(0, std::memory_order_release);
    g_canvas_h.store(0, std::memory_order_release);
    int stride = width * 4;  // BGRA
    int slot_bytes = stride * height;
    g_probe_duration = probe->duration_sec;

    std::fprintf(stderr, "Probe ok: %dx%d codec=%s dur=%.1fs tracks=a%zu s%zu\n",
                 width, height, probe->codec.c_str(), probe->duration_sec,
                 probe->audio.size(), probe->subs.size());

    // --- 2. Create SHM ring buffer ---
    std::string shm_name = generate_shm_name();
    size_t buf_size = ring_buffer_size(DECODE_RING_SLOT_COUNT, slot_bytes);

    ShmRegion shm = create_shm(shm_name, buf_size);
    if (!shm.ptr) {
        write_error("OPEN_FAILED", "SHM create failed", sid);
        g_state.set_state(State::IDLE);
        write_event("state_changed", sid, -1, {{"state", "idle"}});
        return;
    }

    FrameRingWriter* ring = new FrameRingWriter(shm.ptr, DECODE_RING_SLOT_COUNT, slot_bytes);

    // --- 3. Build tracks_changed payload ---
    std::string codec_name = probe->codec;

    nlohmann::json tracks_payload;
    {
        nlohmann::json audio_arr = nlohmann::json::array();
        nlohmann::json sub_arr   = nlohmann::json::array();
        std::string active_audio;
        std::string active_sub;

        // PLAYER_UX_FIX Phase 6.2 — IINA-parity payload enrichment.
        // Adds default/forced flags (both tracks), channels/sample rate
        // (audio only). TrackPopover consumes via QJsonObject::value()
        // which returns default-constructed defaults for missing keys —
        // so pre-Phase-6.2 Qt builds receiving a new sidecar payload
        // just ignore the extra fields harmlessly.
        for (const auto& t : probe->audio) {
            audio_arr.push_back({
                {"id", t.id}, {"lang", t.lang}, {"title", t.title},
                {"default", t.default_flag}, {"forced", t.forced_flag},
                {"channels", t.channels}, {"sample_rate", t.sample_rate}
            });
            if (active_audio.empty()) active_audio = t.id;
        }
        for (const auto& t : probe->subs) {
            sub_arr.push_back({
                {"id", t.id}, {"lang", t.lang}, {"title", t.title},
                {"codec", t.codec_name},
                {"default", t.default_flag}, {"forced", t.forced_flag}
            });
            if (active_sub.empty()) active_sub = t.id;
        }
        tracks_payload["audio"]           = audio_arr;
        tracks_payload["subtitle"]        = sub_arr;
        tracks_payload["active_audio_id"] = active_audio;
        tracks_payload["active_sub_id"]   = active_sub;
    }

    // --- 3a. PLAYER_UX_FIX Phase 1.1 — emit tracks + media info pre-first-frame.
    // The probe-derived payloads are fully known at this point; hoisting the
    // emission out of the on_video_event first-frame block unblocks the Qt
    // HUD on slow-open paths (HEVC 10-bit init, large files, network URLs)
    // where first_frame may not arrive for seconds. Matches IINA's .loaded-
    // state lifecycle: tracks + duration deliver at MPV_EVENT_FILE_LOADED;
    // .playing waits for MPV_EVENT_VIDEO_RECONFIG. Ordering vs the earlier
    // state_changed{opening} emitted from handle_open is naturally preserved
    // by the stdin-thread → worker-thread happens-before: opening always
    // reaches stdout before these worker-thread writes.
    write_event("tracks_changed", sid, -1, tracks_payload);

    nlohmann::json mi;
    mi["hdr"]             = probe->hdr;
    mi["color_primaries"] = probe->color_primaries;
    mi["color_trc"]       = probe->color_trc;
    mi["max_cll"]         = probe->max_cll;
    mi["max_fall"]        = probe->max_fall;
    nlohmann::json ch_arr = nlohmann::json::array();
    for (const auto& ch : probe->chapters) {
        ch_arr.push_back({{"start", ch.start_sec}, {"end", ch.end_sec}, {"title", ch.title}});
    }
    mi["chapters"]       = ch_arr;
    mi["audio_device"]   = g_audio_device_name;
    mi["audio_host_api"] = g_audio_host_api_name;
    write_event("media_info", sid, -1, mi);

    // Video decoder on_event callback. Phase 1.1 capture list trimmed —
    // tracks_payload and probe_* locals are no longer needed in the lambda
    // since their emissions now fire above. The lambda's remaining captures
    // are the shm/dimension metadata needed to synthesize the first_frame
    // event payload. STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 adds
    // open_start_time by value so in-lambda Phase 1 events
    // (decoder_open_done / first_packet_read / first_decoder_receive / the
    // additive wall_clock_delta_from_open_ms field on first_frame) compute
    // their t_ms_from_open locally via steady_clock::now() - open_start_time.
    // open_start_time is std::chrono::steady_clock::time_point, trivially
    // copyable; captured by value so the lambda stays valid if open_worker
    // returns before the decoder thread fires first_frame.
    auto on_video_event = [sid, shm_name, width, height, stride, slot_bytes,
                     codec_name, open_start_time](const std::string& event, const std::string& detail) {
        auto lambda_t_ms_from_open = [&open_start_time]() -> int64_t {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - open_start_time).count();
        };
        if (event == "first_frame") {
            // Parse "fw:fh:codec:pts_us:fid" from decoder callback
            int actual_w = width, actual_h = height;
            int64_t pts_us = 0;
            int64_t fid = 0;
            size_t p1 = detail.find(':');
            size_t p2 = (p1 != std::string::npos) ? detail.find(':', p1 + 1) : std::string::npos;
            size_t p3 = (p2 != std::string::npos) ? detail.find(':', p2 + 1) : std::string::npos;
            size_t p4 = (p3 != std::string::npos) ? detail.find(':', p3 + 1) : std::string::npos;
            if (p1 != std::string::npos) {
                actual_w = std::atoi(detail.substr(0, p1).c_str());
                actual_h = std::atoi(detail.substr(p1 + 1, p2 - p1 - 1).c_str());
            }
            if (p3 != std::string::npos && p4 != std::string::npos) {
                pts_us = std::atoll(detail.substr(p3 + 1, p4 - p3 - 1).c_str());
                fid    = std::atoll(detail.substr(p4 + 1).c_str());
            }
            double pts_sec = static_cast<double>(pts_us) / 1e6;

            // A/V sync: seek audio to match the first displayed video frame.
            // When resuming mid-file, video seeks to a keyframe BEFORE the
            // target and skips forward. During that skip, audio was playing
            // ahead.  Seeking audio back to the video's PTS re-syncs them.
            {
                std::lock_guard<std::mutex> lock(g_session_mutex);
                if (g_audio_dec && g_audio_dec->running()) {
                    double clock_pos = g_clock.position_us() / 1e6;
                    double drift = clock_pos - pts_sec;
                    if (drift > 0.1) {  // only if audio drifted >100ms ahead
                        std::fprintf(stderr,
                            "AVSYNC_DIAG first_frame_audio_seek video=%.3fs audio_clock=%.3fs drift=%.0fms\n",
                            pts_sec, clock_pos, drift * 1000);
                        g_audio_dec->seek(pts_sec);
                    }
                }
            }

            nlohmann::json p;
            p["width"]       = actual_w;
            p["height"]      = actual_h;
            p["codec"]       = codec_name;
            p["ptsSec"]      = pts_sec;
            p["shmName"]     = shm_name;
            p["slotCount"]   = DECODE_RING_SLOT_COUNT;
            p["slotBytes"]   = slot_bytes;
            p["pixelFormat"] = "bgra8";
            // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — additive
            // wall_clock_delta_from_open_ms for end-to-end Phase-1
            // timing correlation. Backward-compatible: Qt consumers
            // that don't read this field ignore it.
            p["wall_clock_delta_from_open_ms"] = lambda_t_ms_from_open();
            write_event("first_frame", sid, -1, p);

            // PLAYER_UX_FIX Phase 1.1 (2026-04-16): tracks_changed +
            // media_info are now emitted earlier, immediately after
            // probe_file returns in open_worker (pre-first-frame). This
            // block previously contained those two emissions; hoisting
            // them unblocks the HUD on slow-open paths so the user sees
            // duration + tracks + HDR metadata before the first decoded
            // frame appears. See main.cpp section "3a" above.

            // Emit hwaccel status
            {
                bool hw_active = g_video_dec && g_video_dec->hw_accel_active();
                write_event("hwaccel_status", sid, -1,
                            {{"active", hw_active},
                             {"device", hw_active ? "D3D11VA" : "none"}});
            }

            std::fprintf(stderr, "TIMING first video frame fid=%lld\n",
                         static_cast<long long>(fid));

            g_state.set_state(State::PLAYING);
            write_event("state_changed", sid, -1, {{"state", "playing"}});

        } else if (event == "eof") {
            write_event("eof", sid);
            g_state.set_state(State::IDLE);
            write_event("state_changed", sid, -1, {{"state", "idle"}});
            stop_time_update();

        } else if (event == "error") {
            size_t colon = detail.find(':');
            std::string code = (colon != std::string::npos) ? detail.substr(0, colon) : "DECODE_FAILED";
            std::string msg  = (colon != std::string::npos) ? detail.substr(colon + 1) : detail;
            write_error(code, msg, sid);
            g_state.set_state(State::IDLE);
            write_event("state_changed", sid, -1, {{"state", "idle"}});

        } else if (event == "decoder_open_done") {
            // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — VideoDecoder fires
            // this immediately after avcodec_open2 succeeds on the decoder
            // thread. Marks decoder-init completion; the next packet-read
            // is imminent. Empty detail; t_ms_from_open computed locally
            // from the captured open_start_time anchor.
            write_event("decoder_open_done", sid, -1, {
                {"t_ms_from_open", lambda_t_ms_from_open()}
            });

        } else if (event == "first_packet_read") {
            // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — VideoDecoder fires
            // on the first successful av_read_frame in its decode loop.
            // Signals demuxer / network / disk I/O is flowing. Detail
            // format: "<stream_index>:<pkt_size>" (both optional; safe to
            // treat as empty). Useful for ranking probe-stall vs
            // post-probe-read-stall when the open takes longer than
            // expected.
            nlohmann::json p;
            p["t_ms_from_open"] = lambda_t_ms_from_open();
            size_t colon = detail.find(':');
            if (colon != std::string::npos) {
                p["stream_index"] = std::atoi(detail.substr(0, colon).c_str());
                p["packet_size"]  = std::atoi(detail.substr(colon + 1).c_str());
            }
            write_event("first_packet_read", sid, -1, p);

        } else if (event == "first_decoder_receive") {
            // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — VideoDecoder fires
            // on the first successful avcodec_receive_frame. This is the
            // honest "decoder is actually making forward progress" signal
            // vs first_packet_read (which only confirms network/disk I/O).
            // Rule-14 design pick #3 (Agent 3, chat.md HELP ACK): use
            // first_decoder_receive — not first_packet_read — to drive
            // Phase 2.1 setStage(DecodingFirstFrame) in the LoadingOverlay,
            // because packet-read success before receive-frame success can
            // stall indefinitely on decoder back-pressure or blocking
            // inside libavcodec. Detail format: "<pts_us>" (zero/empty if
            // no PTS — some keyframes). decode_latency_ms is not computed
            // yet; Phase 2 can add if empirically useful.
            nlohmann::json p;
            p["t_ms_from_open"] = lambda_t_ms_from_open();
            if (!detail.empty()) {
                int64_t pts_us = std::atoll(detail.c_str());
                p["pts_ms"] = pts_us / 1000;
            }
            write_event("first_decoder_receive", sid, -1, p);

        } else if (event == "buffering") {
            // PLAYER_UX_FIX Phase 2.1 — HTTP-stall retry path emits this
            // from video_decoder.cpp:984 when av_read_frame hits EAGAIN /
            // ETIMEDOUT / EIO for a stream URL. Previously dropped at this
            // dispatch boundary (no case) → never reached Qt → user saw
            // a black canvas for up to 30 seconds with no feedback before
            // the decoder finally gave up. Empty payload; presence of the
            // event is the signal. Session-scoped so Phase 1's Qt-side
            // sessionId filter treats it correctly.
            write_event("buffering", sid, -1, {});

        } else if (event == "playing") {
            // PLAYER_UX_FIX Phase 2.1 — companion to "buffering": fires
            // from video_decoder.cpp:1006 when a stalled read clears.
            // Empty payload; Qt uses presence as the "stall resolved,
            // dismiss buffering indicator" signal. Distinct from
            // state_changed{playing} which is emitted once at first_frame
            // — this fires on EVERY stall-clear transition.
            write_event("playing", sid, -1, {});

        } else if (event == "decode_error") {
            // Batch 6.3 (Player Polish Phase 6) — non-fatal avcodec error
            // caught by VideoDecoder; decode loop already continued past
            // the bad packet/frame. Surface to main-app for a throttled
            // "Skipping corrupt frame…" toast. State is NOT changed here
            // — the sidecar is still playing.
            size_t colon = detail.find(':');
            std::string code = (colon != std::string::npos) ? detail.substr(0, colon) : "DECODE_SKIP";
            std::string msg  = (colon != std::string::npos) ? detail.substr(colon + 1) : detail;
            nlohmann::json p;
            p["code"] = code;
            p["message"] = msg;
            p["recoverable"] = true;
            write_event("decode_error", sid, -1, p);

        } else if (event == "d3d11_texture") {
            // detail format: "nt_handle:width:height"
            size_t c1 = detail.find(':');
            size_t c2 = detail.find(':', c1 + 1);
            if (c1 != std::string::npos && c2 != std::string::npos) {
                nlohmann::json p;
                p["ntHandle"] = std::stoull(detail.substr(0, c1));
                p["width"]    = std::stoul(detail.substr(c1 + 1, c2 - c1 - 1));
                p["height"]   = std::stoul(detail.substr(c2 + 1));
                p["format"]   = "bgra8";
                write_event("d3d11_texture", sid, -1, p);
                std::fprintf(stderr, "HOLY_GRAIL: texture event emitted handle=%s %sx%s\n",
                             detail.substr(0, c1).c_str(),
                             detail.substr(c1 + 1, c2 - c1 - 1).c_str(),
                             detail.substr(c2 + 1).c_str());
            }

        } else if (event == "overlay_shm") {
            // PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — overlay SHM
            // name + dims. detail format: "<shm_name>:<width>:<height>".
            // Shm name contains no colons (generate_shm_name produces
            // "tankoban_frame_<pid>_<n>" — underscores only), so splitting
            // on the last two colons is unambiguous.
            size_t c2 = detail.rfind(':');
            size_t c1 = (c2 == std::string::npos) ? std::string::npos
                                                   : detail.rfind(':', c2 - 1);
            if (c1 != std::string::npos && c2 != std::string::npos) {
                nlohmann::json p;
                p["name"]   = detail.substr(0, c1);
                p["width"]  = std::stoul(detail.substr(c1 + 1, c2 - c1 - 1));
                p["height"] = std::stoul(detail.substr(c2 + 1));
                write_event("overlay_shm", sid, -1, p);
                std::fprintf(stderr, "HOLY_GRAIL: overlay_shm event emitted name=%s %sx%s\n",
                             detail.substr(0, c1).c_str(),
                             detail.substr(c1 + 1, c2 - c1 - 1).c_str(),
                             detail.substr(c2 + 1).c_str());
            }

        } else if (event == "frame_stepped") {
            // detail = "pts_sec" as string
            double pts = std::stod(detail);
            write_event("frame_stepped", sid, -1, {{"positionSec", pts}});

        } else if (event == "subtitle_text") {
            // detail format: "start_ms:end_ms:text"
            size_t c1 = detail.find(':');
            size_t c2 = detail.find(':', c1 + 1);
            if (c1 != std::string::npos && c2 != std::string::npos) {
                nlohmann::json p;
                p["start_ms"] = std::stoll(detail.substr(0, c1));
                p["end_ms"]   = std::stoll(detail.substr(c1 + 1, c2 - c1 - 1));
                p["text"]     = detail.substr(c2 + 1);
                write_event("subtitle_text", sid, -1, p);
            }
        }
    };

    // Audio decoder on_event callback
    auto on_audio_event = [sid](const std::string& event, const std::string& detail) {
        if (event == "error") {
            size_t colon = detail.find(':');
            std::string code = (colon != std::string::npos) ? detail.substr(0, colon) : "AUDIO_DECODE_FAILED";
            std::string msg  = (colon != std::string::npos) ? detail.substr(colon + 1) : detail;
            write_error(code, msg, sid);
        }
        // audio_ready and eof handled via synchronization, not events to host
    };

    // --- Subtitle renderer ---
    SubtitleRenderer* sub_ren = new SubtitleRenderer();
    // Option A rollback — canvas == video. Phase 4.1 shape: libass frame_size
    // and storage_size both track the source video. fit_aspect_rect returns
    // the full-video rect, margins all zero, pixel_aspect == 1.0. Subs render
    // inside the video area at traditional libass default position.
    sub_ren->configure_geometry(width, height, width, height);
    if (!probe->subs.empty()) {
        sub_ren->load_embedded_track(probe->subs[0].codec_name, probe->subs[0].extradata);
    }

    // --- Filter graphs (initially inactive, configured via set_filters command) ---
    FilterGraph* vfilt = new FilterGraph();
    FilterGraph* afilt = new FilterGraph();

    // --- GPU renderer (libplacebo, optional — only for HDR content) ---
    GpuRenderer* gpu_ren = nullptr;
    if (probe->hdr) {
        gpu_ren = new GpuRenderer();
        if (!gpu_ren->init()) {
            std::fprintf(stderr, "open_worker: GPU renderer unavailable for HDR, software path\n");
            delete gpu_ren;
            gpu_ren = nullptr;
        } else {
            gpu_ren->set_hdr_metadata(
                probe->color_primaries, probe->color_trc,
                probe->mastering_max_lum, probe->mastering_min_lum,
                probe->max_cll, probe->max_fall);
            gpu_ren->load_icc_profile("");  // auto-detect system display profile
        }
    }

    VideoDecoder* vdec = new VideoDecoder(ring, on_video_event, &g_clock, slot_bytes, sub_ren, vfilt, gpu_ren);
    // Option A rollback — do not override overlay canvas. Decoder will size
    // the overlay SHM to probe video dimensions.

    // --- 4. Start audio first (audio anchors clock before video) ---
    AudioDecoder* adec = nullptr;
    bool has_audio = !probe->audio.empty();
    auto t0_open = std::chrono::steady_clock::now();
    auto open_ms = [&t0_open]() {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0_open).count();
    };

    if (has_audio) {
        std::fprintf(stderr, "AVSYNC_DIAG open_audio_start +%.0fms\n", open_ms());
        adec = new AudioDecoder(&g_clock, &g_volume, on_audio_event, afilt,
                                g_pa_stream, g_pa_actual_latency);
        int audio_idx = -1;
        if (!probe->audio.empty()) {
            audio_idx = std::stoi(probe->audio[0].id);
        }
        adec->start(path, start_sec, audio_idx);

        // Wait up to 500ms for audio clock to anchor
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline) {
            if (g_clock.started() || !adec->running()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::fprintf(stderr, "AVSYNC_DIAG open_audio_wait_done +%.0fms clock_started=%d\n",
                     open_ms(), g_clock.started() ? 1 : 0);
    }

    // Store in globals
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        g_shm         = shm;
        g_ring_writer = ring;
        g_video_dec   = vdec;
        g_audio_dec   = adec;
        g_current_path = path;
        g_active_audio_id = tracks_payload.value("active_audio_id", "");
        // Default to the first subtitle stream's ID so that language
        // preference matching (which sends set_tracks shortly after open)
        // won't clear+reload the track if it selects the same default stream.
        g_active_sub_id   = tracks_payload.value("active_sub_id", "");
        if (g_active_sub_id.empty() && !probe->subs.empty()) {
            g_active_sub_id = probe->subs[0].id;
        }
        g_sub_renderer    = sub_ren;
        g_video_storage_w = width;
        g_video_storage_h = height;
        g_video_filter    = vfilt;
        g_audio_filter    = afilt;
        g_gpu_renderer    = gpu_ren;
        g_probe_subs      = probe->subs;
    }

    // --- 5. Start video decode ---
    std::fprintf(stderr, "AVSYNC_DIAG open_video_start +%.0fms\n", open_ms());
    std::vector<int> sub_indices;
    for (const auto& t : probe->subs)
        sub_indices.push_back(std::stoi(t.id));

    // STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 — decoder_open_start event.
    // Fires immediately before spawning the decoder worker thread. Inside
    // VideoDecoder::decode_thread_func, avformat_open_input +
    // avformat_find_stream_info run a second time (the demuxer re-probes
    // the URL for the decoder's own AVFormatContext — D-12 two-probes
    // scenario). decoder_open_done will fire from inside that worker
    // thread after avcodec_open2 succeeds.
    write_event("decoder_open_start", sid, -1, {
        {"t_ms_from_open", t_ms_from_open()}
    });

    vdec->start(path, start_sec, probe->video_stream_index, sub_indices);

    // Set default active subtitle stream + preload packets.
    //
    // Player Polish Batch 5.1 fix (2026-04-15): the original comment claimed
    // "no preload needed on open because video decoder will feed subtitle
    // packets naturally" — true for SRT-style text subs but FALSE for
    // libass-rendered ASS/SSA tracks, which need the full event list
    // pre-built into ASS_Track at startup or render_blend silently emits
    // nothing for the first event window. Symptom: subtitles inconsistently
    // appear across files (text-sub files work, ASS-sub files don't until
    // a track switch happens). Fix: preload on first open the same way
    // handle_set_tracks does on mid-playback switches.
    if (!probe->subs.empty() && g_sub_renderer && !g_current_path.empty()) {
        const int sub_idx = std::stoi(probe->subs[0].id);
        vdec->set_active_sub_stream(sub_idx);
        preload_subtitle_packets(g_sub_renderer, g_current_path, sub_idx);
    } else if (!probe->subs.empty()) {
        vdec->set_active_sub_stream(std::stoi(probe->subs[0].id));
    }

    // --- 6. Start time_update emitter ---
    start_time_update(sid, probe->duration_sec);
}

// Dispatched from main thread: ack immediately, run heavy work on background thread
static void handle_open(const Command& cmd) {
    // If already playing, teardown first (joins previous open thread)
    if (g_state.state() == State::PLAYING || g_state.state() == State::PAUSED) {
        teardown_decode();
    }
    join_open_thread();

    // Ack immediately on the main thread (keeps stdin loop responsive)
    write_ack(cmd.seq, cmd.sessionId);

    g_state.setSessionId(cmd.sessionId);
    g_state.set_state(State::OPEN_PENDING);
    write_event("state_changed", cmd.sessionId, -1, {{"state", "opening"}});

    // Spawn background thread for probe + decode setup
    g_open_thread = std::thread(open_worker, cmd);
}

// ---------------------------------------------------------------------------
// stop command handler
// ---------------------------------------------------------------------------

static void handle_stop(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    teardown_decode();
    g_state.set_state(State::IDLE);
    write_event("state_changed", cmd.sessionId, -1, {{"state", "idle"}});
    // PLAYER_LIFECYCLE_FIX Phase 2 — emit stop_ack AFTER full teardown so
    // Qt's openFile fence (SidecarProcess::sendStopWithCallback) can fire
    // a fresh sendOpen on the same process without racing a still-running
    // decoder. seqAck = stop seq for correlation; Qt matches against its
    // m_pendingStopSeq and fires the stored callback. Session-scoped
    // (sessionId populated) so Phase 1's Qt-side event filter passes it
    // through cleanly (current session at stop-time matches at stop_ack-
    // receive-time because Qt regenerates m_sessionId only on the next
    // sendOpen, which fires from within the stop_ack callback).
    write_event("stop_ack", cmd.sessionId, cmd.seq, {});
}

// ---------------------------------------------------------------------------
// seek command handler
// ---------------------------------------------------------------------------

static void handle_seek(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    double pos = cmd.payload.value("positionSec", 0.0);
    // PLAYER_STREMIO_PARITY Phase 3 — optional per-call seek-mode override.
    // Payload field "mode" accepts "fast" or "exact"; absent = sticky default
    // last set via set_seek_mode (Fast unless changed). Audio decoder is
    // mode-agnostic — it has no keyframe concept; Swr drift correction
    // handles re-sync. Only g_video_dec honors the mode.
    const std::string mode_str = cmd.payload.value("mode", std::string{});
    std::lock_guard<std::mutex> lock(g_session_mutex);
    if (g_audio_dec) {
        g_audio_dec->seek(pos);
    }
    if (g_video_dec) {
        if (mode_str == "exact") {
            g_video_dec->seek(pos, VideoDecoder::SeekMode::Exact);
        } else if (mode_str == "fast") {
            g_video_dec->seek(pos, VideoDecoder::SeekMode::Fast);
        } else {
            g_video_dec->seek(pos);
        }
    }
}

static void handle_set_seek_mode(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    const std::string mode_str = cmd.payload.value("mode", std::string{"fast"});
    VideoDecoder::SeekMode mode = (mode_str == "exact")
        ? VideoDecoder::SeekMode::Exact
        : VideoDecoder::SeekMode::Fast;
    std::lock_guard<std::mutex> lock(g_session_mutex);
    if (g_video_dec) {
        g_video_dec->set_seek_mode(mode);
    }
    std::fprintf(stderr, "set_seek_mode: %s\n", mode_str.c_str());
}

// ---------------------------------------------------------------------------
// pause / resume handlers
// ---------------------------------------------------------------------------

static void handle_pause(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        // AudioDecoder::pause() also calls clock_.set_paused(true) internally
        if (g_audio_dec) g_audio_dec->pause();
        if (g_video_dec) g_video_dec->pause();
    }
    // Fallback for video-only mode (no audio decoder to drive the clock)
    if (!g_audio_dec) g_clock.set_paused(true);
    g_state.set_state(State::PAUSED);
    write_event("state_changed", cmd.sessionId, -1, {{"state", "paused"}});
}

static void handle_frame_step(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    bool backward = cmd.payload.value("backward", false);
    double current_pos = cmd.payload.value("positionSec", 0.0);
    std::lock_guard<std::mutex> lock(g_session_mutex);
    if (g_video_dec) {
        if (backward)
            g_video_dec->step_backward(current_pos);
        else
            g_video_dec->step_forward();
    }
    // Ensure paused state (frame stepping implies pause)
    if (!g_audio_dec) g_clock.set_paused(true);
    g_state.set_state(State::PAUSED);
    write_event("state_changed", cmd.sessionId, -1, {{"state", "paused"}});
}

static void handle_resume(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        // AudioDecoder::resume() also calls clock_.set_paused(false) internally
        if (g_audio_dec) g_audio_dec->resume();
        if (g_video_dec) g_video_dec->resume();
    }
    if (!g_audio_dec) g_clock.set_paused(false);
    g_state.set_state(State::PLAYING);
    write_event("state_changed", cmd.sessionId, -1, {{"state", "playing"}});
}

// ---------------------------------------------------------------------------
// volume / mute handlers
// ---------------------------------------------------------------------------

static void handle_set_volume(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    double vol = cmd.payload.value("volume", 1.0);
    g_volume.set_volume(static_cast<float>(vol));
}

static void handle_set_mute(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    bool muted = cmd.payload.value("muted", false);
    g_volume.set_muted(muted);
}

// ---------------------------------------------------------------------------
// set_rate handler
// ---------------------------------------------------------------------------

static void handle_set_rate(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    double rate = cmd.payload.value("rate", 1.0);
    if (rate < 0.0625) rate = 0.0625;
    if (rate > 16.0) rate = 16.0;
    g_clock.set_rate(rate);
}

// ---------------------------------------------------------------------------
// Preload all subtitle packets for a given stream from a temporary demuxer.
// This is necessary because the video decoder only feeds subtitle packets
// going forward — after a track switch, all previous events would be lost.
// ---------------------------------------------------------------------------

static void preload_subtitle_packets(SubtitleRenderer* renderer,
                                      const std::string& path,
                                      int stream_index) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        std::fprintf(stderr, "preload_subtitle_packets: failed to open %s\n", path.c_str());
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return;
    }
    if (stream_index < 0 || stream_index >= static_cast<int>(fmt->nb_streams)) {
        avformat_close_input(&fmt);
        return;
    }

    // Tell the demuxer to discard all streams except the target subtitle
    // stream.  For MKV/MP4 this lets the demuxer skip over gigabytes of
    // video/audio data instead of reading every packet sequentially.
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        fmt->streams[i]->discard =
            (static_cast<int>(i) == stream_index) ? AVDISCARD_NONE : AVDISCARD_ALL;
    }

    AVPacket* pkt = av_packet_alloc();
    int count = 0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == stream_index) {
            AVStream* ss = fmt->streams[stream_index];
            int64_t s_ms = av_rescale_q(pkt->pts, ss->time_base, {1, 1000});
            int64_t d_ms = av_rescale_q(pkt->duration, ss->time_base, {1, 1000});
            renderer->process_packet(pkt->data, pkt->size, s_ms, d_ms);
            ++count;
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    std::fprintf(stderr, "preload_subtitle_packets: loaded %d packets from stream %d\n",
                 count, stream_index);
}

// ---------------------------------------------------------------------------
// set_tracks handler (audio + subtitle stream switching)
// ---------------------------------------------------------------------------

static void handle_set_tracks(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);

    std::string new_audio_id = cmd.payload.value("audio_id", "");
    std::string new_sub_id   = cmd.payload.value("sub_id", "");

    std::lock_guard<std::mutex> lock(g_session_mutex);

    bool audio_changed = !new_audio_id.empty() && new_audio_id != g_active_audio_id;

    if (audio_changed && g_audio_dec && !g_current_path.empty()) {
        // Stop current audio decoder
        g_audio_dec->stop();
        delete g_audio_dec;
        g_audio_dec = nullptr;

        // Capture position, then force-anchor the clock at that position.
        // seek_anchor re-establishes started_=true at the exact PTS so the
        // new audio decoder's first update() won't cause a drift.
        double pos_sec = static_cast<double>(g_clock.position_us()) / 1000000.0;
        int64_t pos_us = static_cast<int64_t>(pos_sec * 1000000.0);
        g_clock.seek_anchor(pos_us);

        // Create and start new audio decoder targeting new stream
        int audio_idx = std::stoi(new_audio_id);
        auto on_audio_event = [sid = cmd.sessionId](const std::string& event, const std::string& detail) {
            if (event == "error") {
                size_t colon = detail.find(':');
                std::string code = (colon != std::string::npos) ? detail.substr(0, colon) : "AUDIO_DECODE_FAILED";
                std::string msg  = (colon != std::string::npos) ? detail.substr(colon + 1) : detail;
                write_error(code, msg, sid);
            }
        };
        g_audio_dec = new AudioDecoder(&g_clock, &g_volume, on_audio_event, g_audio_filter,
                                       g_pa_stream, g_pa_actual_latency);
        g_audio_dec->start(g_current_path, pos_sec, audio_idx);
        g_active_audio_id = new_audio_id;

        std::fprintf(stderr, "set_tracks: switched audio to stream %s at %.3fs\n",
                     new_audio_id.c_str(), pos_sec);
    }

    // Handle subtitle visibility from set_tracks payload
    bool sub_vis = cmd.payload.value("sub_visibility", true);
    if (g_sub_renderer) {
        g_sub_renderer->set_visible(sub_vis);
    }

    if (!new_sub_id.empty() && new_sub_id != g_active_sub_id) {
        g_active_sub_id = new_sub_id;
        int sub_stream_idx = std::stoi(new_sub_id);
        if (g_sub_renderer) {
            for (const auto& t : g_probe_subs) {
                if (t.id == new_sub_id) {
                    g_sub_renderer->clear_track();
                    g_sub_renderer->load_embedded_track(t.codec_name, t.extradata);
                    // Preload all subtitle events so mid-playback switches
                    // don't lose events before the current decode position.
                    if (!g_current_path.empty()) {
                        preload_subtitle_packets(g_sub_renderer, g_current_path, sub_stream_idx);
                    }
                    break;
                }
            }
        }
        if (g_video_dec) {
            g_video_dec->set_active_sub_stream(sub_stream_idx);
        }
        std::fprintf(stderr, "set_tracks: switched subtitle to stream %s\n",
                     new_sub_id.c_str());
    }

    // Emit tracks_changed with updated active IDs
    nlohmann::json p;
    p["active_audio_id"] = g_active_audio_id;
    p["active_sub_id"]   = g_active_sub_id;
    write_event("tracks_changed", cmd.sessionId, -1, p);

    // Also emit sub_visibility_changed so the Python side can track
    // visibility state — previously only handle_set_sub_visibility emitted
    // this, leaving set_tracks visibility changes invisible to the UI.
    write_event("sub_visibility_changed", cmd.sessionId, -1, {{"visible", sub_vis}});
}

// ---------------------------------------------------------------------------
// set_sub_visibility handler
// ---------------------------------------------------------------------------

static void handle_set_sub_visibility(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    bool visible = cmd.payload.value("visible", true);
    if (g_sub_renderer) {
        g_sub_renderer->set_visible(visible);
    }
    write_event("sub_visibility_changed", cmd.sessionId, -1, {{"visible", visible}});
}

// ---------------------------------------------------------------------------
// set_audio_speed handler — Player Polish Phase 4 Batch 4.1
// ---------------------------------------------------------------------------
// Main-app's SyncClock derives a clock velocity from per-frame render
// latency (accumulated EMA in [0.995, 1.000]); a 500ms ticker in
// VideoPlayer forwards the value here as `speed`. AudioDecoder applies
// swr_set_compensation on change, closing the Kodi-DVDClock-style A/V
// feedback loop end-to-end. ±5% range matches Kodi ActiveAE
// m_maxspeedadjust.

static void handle_set_audio_speed(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    double speed = cmd.payload.value("speed", 1.0);
    if (g_audio_dec) {
        g_audio_dec->set_speed(speed);
    }
    // No event echo — main-app drives the loop and already knows what it sent.
    // (We could emit an `audio_speed_changed` event if telemetry wanted it;
    // not added now to keep the Phase 4 scaffolding minimal.)
}

// ---------------------------------------------------------------------------
// set_drc_enabled handler — Player Polish Phase 4 Batch 4.3
// ---------------------------------------------------------------------------
// Main-app's EqualizerPopover exposes a DRC toggle checkbox. When user
// toggles, VideoPlayer forwards the bool here. AudioDecoder's audio
// thread picks up the atomic and enables/disables its feed-forward
// compressor (threshold -12 dB, ratio 3:1, attack 10 ms, release 100 ms).

static void handle_set_drc_enabled(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    bool enabled = cmd.payload.value("enabled", false);
    if (g_audio_dec) {
        g_audio_dec->set_drc_enabled(enabled);
    }
    write_event("drc_enabled_changed", cmd.sessionId, -1, {{"enabled", enabled}});
}

// ---------------------------------------------------------------------------
// set_sub_delay handler
// ---------------------------------------------------------------------------

static void handle_set_sub_delay(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    int64_t delay_ms = cmd.payload.value("delay_ms", 0);
    if (g_sub_renderer) {
        g_sub_renderer->set_delay_ms(delay_ms);
    }
    write_event("sub_delay_changed", cmd.sessionId, -1, {{"delay_ms", delay_ms}});
}

// ---------------------------------------------------------------------------
// load_external_sub handler
// ---------------------------------------------------------------------------

static void handle_load_external_sub(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    std::string path = cmd.payload.value("path", "");
    if (path.empty() || !g_sub_renderer) {
        write_error("INVALID_ARGS", "No path or no subtitle renderer", cmd.sessionId);
        return;
    }
    bool ok = g_sub_renderer->load_external_file(path);
    if (!ok) {
        write_error("SUB_LOAD_FAILED", "Failed to load external subtitle file", cmd.sessionId);
        return;
    }
    // External sub = no demuxer packets needed
    if (g_video_dec) {
        g_video_dec->set_active_sub_stream(-2);
    }
    write_event("external_sub_loaded", cmd.sessionId, -1, {{"path", path}});
}

// ---------------------------------------------------------------------------
// set_sub_style handler
// ---------------------------------------------------------------------------

static void handle_set_sub_style(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    int font_size = cmd.payload.value("font_size", 24);
    int margin_v  = cmd.payload.value("margin_v", 40);
    bool outline  = cmd.payload.value("outline", true);
    if (g_sub_renderer) {
        g_sub_renderer->set_style_override(font_size, margin_v, outline);
    }
    write_event("sub_style_changed", cmd.sessionId, -1,
                {{"font_size", font_size}, {"margin_v", margin_v}, {"outline", outline}});
}

// ---------------------------------------------------------------------------
// set_zero_copy_active — Qt enables this once it successfully imports the
// D3D11 shared texture. Producer then skips the CPU/SHM pipeline for HW frames
// when no subtitle blending is required.
// ---------------------------------------------------------------------------

static void handle_set_zero_copy_active(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    bool active = cmd.payload.value("active", false);
    g_zero_copy_active.store(active);
    // Push the flag into the active VideoDecoder so its decode loop sees it
    // immediately (no need to restart the session).
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        if (g_video_dec) g_video_dec->set_zero_copy_active(active);
    }
    std::fprintf(stderr, "set_zero_copy_active: %s\n", active ? "ON" : "OFF");
    write_event("zero_copy_changed", cmd.sessionId, -1, {{"active", active}});
}

// ---------------------------------------------------------------------------
// set_audio_delay handler — user-adjustable A/V offset for hidden device latency
// (Bluetooth/HDMI/etc. that PortAudio's outputLatency doesn't account for).
// Positive ms = delay audio (video waits longer); negative = pull audio earlier.
// ---------------------------------------------------------------------------

static void handle_set_canvas_size(const Command& cmd) {
    // Option A rollback — no-op. Qt-side still sends set_canvas_size on
    // widget resize / HUD toggle / fullscreen; we acknowledge to keep the
    // protocol clean but do not mutate subtitle geometry or overlay SHM
    // size. Canvas-plane caused 1080p video squeeze + sub-position drift +
    // SHM thrash on every HUD toggle. Video-sized overlay is always correct;
    // main app positions the overlay quad at the video rect on screen.
    write_ack(cmd.seq, cmd.sessionId);
    (void)cmd;
}

static void handle_set_audio_delay(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    int delay_ms = cmd.payload.value("delay_ms", 0);
    g_clock.set_extra_latency_ms(delay_ms);
    write_event("audio_delay_changed", cmd.sessionId, -1, {{"delay_ms", delay_ms}});
}

// ---------------------------------------------------------------------------
// set_filters handler
// ---------------------------------------------------------------------------

static void handle_set_filters(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    std::string vf = cmd.payload.value("video", "");
    std::string af = cmd.payload.value("audio", "");

    // Destroy active graphs and set pending specs. The decode threads will
    // lazily init the filter graphs with actual stream parameters.
    if (g_video_filter) {
        g_video_filter->destroy();
        g_video_filter->set_pending(vf);
    }
    if (g_audio_filter) {
        g_audio_filter->destroy();
        g_audio_filter->set_pending(af);
    }

    write_event("filters_changed", cmd.sessionId, -1, {{"video", vf}, {"audio", af}});
}

// ---------------------------------------------------------------------------
// set_tone_mapping handler
// ---------------------------------------------------------------------------

static void handle_set_tone_mapping(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    std::string algo = cmd.payload.value("algorithm", "hable");
    bool peak = cmd.payload.value("peak_detect", true);
    if (g_gpu_renderer) {
        g_gpu_renderer->set_tone_mapping(algo, peak);
    }
    write_event("tone_mapping_changed", cmd.sessionId, -1,
                {{"algorithm", algo}, {"peak_detect", peak}});
}

// ---------------------------------------------------------------------------
// set_icc_profile handler
// ---------------------------------------------------------------------------

static void handle_set_icc_profile(const Command& cmd) {
    write_ack(cmd.seq, cmd.sessionId);
    std::string path = cmd.payload.value("path", "");
    if (g_gpu_renderer) {
        g_gpu_renderer->load_icc_profile(path);
    }
    write_event("icc_profile_changed", cmd.sessionId, -1, {{"path", path}});
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    bool no_heartbeat = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-heartbeat") == 0)
            no_heartbeat = true;
        else if (std::strcmp(argv[i], "--version") == 0) {
            std::fprintf(stdout, "ffmpeg_sidecar 0.3.0 (8N.3)\n");
            return 0;
        }
    }

    // Redirect stderr to log file (matches Python sidecar behaviour)
    std::freopen("sidecar_debug_live.log", "a", stderr);
    std::fprintf(stderr, "=== native sidecar 8N.3 starting ===\n");

#ifdef _WIN32
    // Request 1ms timer resolution. Windows defaults to ~15.6ms which destroys
    // the precision of std::this_thread::sleep_for in the A/V sync wait loop.
    // Without this, every sleep_for(<15ms) takes ~15.6ms, causing per-frame
    // jitter and visible video stutter at 24fps content.
    timeBeginPeriod(1);
    std::fprintf(stderr, "Windows: timer resolution set to 1ms\n");
#endif

    // Initialize PortAudio
    PaError pa_err = Pa_Initialize();
    if (pa_err != paNoError) {
        std::fprintf(stderr, "Pa_Initialize failed: %s\n", Pa_GetErrorText(pa_err));
        // Continue anyway — video-only mode will work
    } else {
        PaDeviceIndex dev = Pa_GetDefaultOutputDevice();
        if (dev != paNoDevice) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(dev);
            std::fprintf(stderr, "PortAudio: default output device: %s (%.3fs latency)\n",
                         info ? info->name : "unknown",
                         info ? info->defaultLowOutputLatency : 0.0);
            // Capture device fingerprint for media_info emission
            if (info) {
                g_audio_device_name = info->name;
                const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
                g_audio_host_api_name = api ? api->name : "";
            }

            // Pre-warm: open + start a PortAudio stream now so the slow
            // device init (5+ seconds for Bluetooth) happens at sidecar
            // startup, not when the user clicks their first video. The
            // stream stays open for the sidecar's lifetime; AudioDecoder
            // writes into it and uses swresample to convert any source
            // audio to PREWARM_SAMPLE_RATE/PREWARM_CHANNELS.
            PaStreamParameters out_params{};
            out_params.device = dev;
            out_params.channelCount = PREWARM_CHANNELS;
            out_params.sampleFormat = paFloat32;
            out_params.suggestedLatency = info ? info->defaultLowOutputLatency : 0.3;
            out_params.hostApiSpecificStreamInfo = nullptr;
            PaError oerr = Pa_OpenStream(&g_pa_stream, nullptr, &out_params,
                                          PREWARM_SAMPLE_RATE, 1024,
                                          paClipOff, nullptr, nullptr);
            if (oerr == paNoError) {
                Pa_StartStream(g_pa_stream);
                const PaStreamInfo* si = Pa_GetStreamInfo(g_pa_stream);
                g_pa_actual_latency = si ? si->outputLatency : 0.32;
                std::fprintf(stderr, "Audio pre-warm: opened %dHz %dch (latency=%.3fs)\n",
                             PREWARM_SAMPLE_RATE, PREWARM_CHANNELS, g_pa_actual_latency);
            } else {
                std::fprintf(stderr, "Audio pre-warm failed: %s — falling back to lazy open\n",
                             Pa_GetErrorText(oerr));
                g_pa_stream = nullptr;
            }
        } else {
            std::fprintf(stderr, "PortAudio: no default output device\n");
        }
    }

    // Start non-blocking stdout writer (prevents pipe-buffer deadlocks)
    start_stdout_writer();

    // Emit ready — host waits for this before sending commands
    write_event("ready");
    g_state.set_state(State::READY);
    std::fprintf(stderr, "State: INIT -> READY\n");

    // Start heartbeat thread
    if (!no_heartbeat)
        g_heartbeat.start(2.0);

    // stdin command loop (main thread)
    char buf[65536];
    while (std::fgets(buf, sizeof(buf), stdin) != nullptr) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.empty())
            continue;

        auto cmd = parse_command(line);
        if (!cmd.has_value())
            continue;

        const auto& name = cmd->name;
        const auto& sid  = cmd->sessionId;
        int seq          = cmd->seq;

        // Session-id guard
        if (!StateMachine::isSessionFree(name)) {
            std::string current = g_state.sessionId();
            if (!current.empty() && sid != current) {
                std::fprintf(stderr, "Stale sessionId %s (current=%s) -- dropping %s\n",
                             sid.c_str(), current.c_str(), name.c_str());
                continue;
            }
        }

        // Dispatch
        if (name == "ping") {
            write_ack(seq, sid);

        } else if (name == "shutdown") {
            g_heartbeat.stop();
            teardown_decode();
            write_event("closed", sid, seq);
            stop_stdout_writer();
            std::fprintf(stderr, "Shutdown received -- exiting\n");
            Pa_Terminate();
#ifdef _WIN32
            timeEndPeriod(1);
#endif
            return 0;

        } else if (name == "open") {
            handle_open(*cmd);

        } else if (name == "stop") {
            handle_stop(*cmd);

        } else if (name == "seek") {
            handle_seek(*cmd);

        } else if (name == "set_seek_mode") {
            handle_set_seek_mode(*cmd);

        } else if (name == "frame_step") {
            handle_frame_step(*cmd);

        } else if (name == "pause") {
            handle_pause(*cmd);

        } else if (name == "resume") {
            handle_resume(*cmd);

        } else if (name == "set_volume") {
            handle_set_volume(*cmd);

        } else if (name == "set_mute") {
            handle_set_mute(*cmd);

        } else if (name == "set_rate") {
            handle_set_rate(*cmd);

        } else if (name == "set_tracks") {
            handle_set_tracks(*cmd);

        } else if (name == "set_sub_visibility") {
            handle_set_sub_visibility(*cmd);

        } else if (name == "set_sub_delay") {
            handle_set_sub_delay(*cmd);

        } else if (name == "load_external_sub") {
            handle_load_external_sub(*cmd);

        } else if (name == "set_sub_style") {
            handle_set_sub_style(*cmd);

        } else if (name == "set_zero_copy_active") {
            handle_set_zero_copy_active(*cmd);

        } else if (name == "set_canvas_size") {
            handle_set_canvas_size(*cmd);

        } else if (name == "set_audio_delay") {
            handle_set_audio_delay(*cmd);

        } else if (name == "set_audio_speed") {
            handle_set_audio_speed(*cmd);

        } else if (name == "set_drc_enabled") {
            handle_set_drc_enabled(*cmd);

        } else if (name == "set_filters") {
            handle_set_filters(*cmd);

        } else if (name == "set_tone_mapping") {
            handle_set_tone_mapping(*cmd);

        } else if (name == "set_icc_profile") {
            handle_set_icc_profile(*cmd);

        } else if (name == "set_hwaccel" || name == "set_color_management") {
            // Preference-only commands — ack and store Python-side.
            // set_hwaccel takes effect on next open. set_color_management
            // is a wrapper around set_icc_profile handled Python-side.
            write_ack(cmd->seq, cmd->sessionId);

        } else {
            std::fprintf(stderr, "Unknown command: %s\n", name.c_str());
            write_error("NOT_IMPLEMENTED",
                        std::string("command '") + name + "' not implemented",
                        sid, seq);
        }
    }

    // stdin closed (host process gone)
    g_heartbeat.stop();
    teardown_decode();
    stop_stdout_writer();
    std::fprintf(stderr, "stdin closed -- exiting\n");
    if (g_pa_stream) { Pa_StopStream(g_pa_stream); Pa_CloseStream(g_pa_stream); g_pa_stream = nullptr; }
    Pa_Terminate();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return 0;
}
