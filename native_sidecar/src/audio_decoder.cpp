#include "audio_decoder.h"
#include "filter_graph.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// STREAM_PLAYBACK_FIX audit P2 — case-insensitive HTTP scheme detection.
// Local copy per TU; shared helpers in demuxer.cpp, video_decoder.cpp,
// main.cpp. Self-contained to avoid a new shared header.
namespace {
bool starts_with_ci(const std::string& s, const char* prefix) {
    const size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char a = static_cast<unsigned char>(s[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}
}  // namespace

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
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

// PortAudio parameters (match Python sidecar: latency=0.3, block=1024)
static constexpr int    PA_BLOCK_SIZE    = 1024;
static constexpr double PA_LATENCY_SEC   = 0.3;
static constexpr int    PA_SAMPLE_RATE   = 48000;  // will be overridden by stream
static constexpr int    PA_CHANNELS      = 2;

// ---------------------------------------------------------------------------
// AudioDecoder
// ---------------------------------------------------------------------------

AudioDecoder::AudioDecoder(AVSyncClock* clock, VolumeControl* volume, AudioEventCb on_event,
                           FilterGraph* audio_filter,
                           PaStream* prewarmed_stream, double prewarmed_latency)
    : clock_(clock)
    , volume_(volume)
    , on_event_(std::move(on_event))
    , audio_filter_(audio_filter)
    , prewarmed_stream_(prewarmed_stream)
    , prewarmed_latency_(prewarmed_latency)
{}

AudioDecoder::~AudioDecoder() {
    stop();
}

void AudioDecoder::start(const std::string& path, double start_seconds, int audio_stream_index) {
    stop();
    stop_flag_.store(false);
    paused_.store(false);
    seek_pending_.store(false);
    running_.store(true);
    thread_ = std::thread(&AudioDecoder::audio_thread_func, this,
                          path, start_seconds, audio_stream_index);
}

void AudioDecoder::stop() {
    stop_flag_.store(true);
    // Unblock pause wait
    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        paused_.store(false);
    }
    pause_cv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
}

void AudioDecoder::pause() {
    paused_.store(true);
    if (clock_) clock_->set_paused(true);
}

void AudioDecoder::resume() {
    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        paused_.store(false);
    }
    if (clock_) clock_->set_paused(false);
    pause_cv_.notify_all();
}

void AudioDecoder::seek(double position_sec) {
    std::lock_guard<std::mutex> lock(seek_mutex_);
    seek_target_sec_ = position_sec;
    seek_pending_.store(true);
    // Also unblock pause wait so seek can proceed
    {
        std::lock_guard<std::mutex> plock(pause_mutex_);
    }
    pause_cv_.notify_all();
}

void AudioDecoder::set_speed(double speed) {
    // Batch 4.1 — ±5% clamp matches Kodi ActiveAE m_maxspeedadjust and
    // keeps sample add/drop artifacts below the perceptual threshold.
    // Main-app clamps too before sending, but we defend in-depth here
    // against any stale/rogue caller.
    if (speed < 0.95) speed = 0.95;
    if (speed > 1.05) speed = 1.05;
    speed_.store(speed, std::memory_order_relaxed);
}

void AudioDecoder::set_drc_enabled(bool on) {
    // Batch 4.3 — atomic store; audio thread picks up on its next
    // chunk. No mutex needed because compressor state is thread-local
    // to audio_thread_func.
    drc_enabled_.store(on, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void AudioDecoder::audio_thread_func(
    std::string path,
    double start_seconds,
    int audio_stream_index)
{
#ifdef _WIN32
    // Pro Audio = lowest scheduler latency on Windows. Audio jitter directly
    // maps to audible glitches, so this thread gets the strongest priority
    // hint MMCSS offers. Released by RAII on any return.
    MmcssScope mmcss(L"Pro Audio");
#endif
    auto t0_audio = std::chrono::steady_clock::now();
    auto ms_since = [&t0_audio]() {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0_audio).count();
    };
    std::fprintf(stderr, "AVSYNC_DIAG audio_thread_start +0ms path=%s start=%.3fs\n",
                 path.c_str(), start_seconds);

    // --- Open container ---
    AVFormatContext* fmt_ctx = nullptr;
    // Audit P2 — case-insensitive (uppercase "HTTP://" is legal per RFC 3986).
    bool is_http = starts_with_ci(path, "http://") || starts_with_ci(path, "https://");
    AVDictionary* opts = nullptr;
    if (is_http) {
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);  // STREAM_STALL_FIX Phase 1 — mpv stream-lavf-o parity (was 10)
        av_dict_set(&opts, "timeout", "60000000", 0);
        av_dict_set(&opts, "rw_timeout", "30000000", 0);
        av_dict_set(&opts, "probesize", "20000000", 0);
        av_dict_set(&opts, "analyzeduration", "10000000", 0);
        std::fprintf(stderr, "AudioDecoder: HTTP streaming mode enabled\n");
    }
    int ret = avformat_open_input(&fmt_ctx, path.c_str(), nullptr, is_http ? &opts : nullptr);
    if (opts) av_dict_free(&opts);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::fprintf(stderr, "AudioDecoder: avformat_open_input failed: %s\n", errbuf);
        on_event_("error", std::string("AUDIO_OPEN_FAILED:") + errbuf);
        running_.store(false);
        return;
    }

    if (is_http) {
        fmt_ctx->probesize = 20000000;
        fmt_ctx->max_analyze_duration = 10000000;
    }

    std::fprintf(stderr, "AVSYNC_DIAG audio_open_input_done +%.0fms\n", ms_since());
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmt_ctx);
        on_event_("error", "AUDIO_OPEN_FAILED:find_stream_info failed");
        running_.store(false);
        return;
    }

    // Find audio stream
    int stream_idx = audio_stream_index;
    if (stream_idx < 0) {
        stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    }
    if (stream_idx < 0) {
        std::fprintf(stderr, "AudioDecoder: no audio stream found\n");
        avformat_close_input(&fmt_ctx);
        // Not an error — file may be video-only. Signal ready so open flow continues.
        on_event_("audio_ready", "");
        running_.store(false);
        return;
    }

    AVStream* audio_stream = fmt_ctx->streams[stream_idx];
    const AVCodec* codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        on_event_("error", "AUDIO_DECODE_INIT_FAILED:no decoder found");
        running_.store(false);
        return;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, audio_stream->codecpar);
    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        on_event_("error", std::string("AUDIO_DECODE_INIT_FAILED:") + errbuf);
        running_.store(false);
        return;
    }

    int in_sample_rate = codec_ctx->sample_rate > 0 ? codec_ctx->sample_rate : 48000;
    int channels = codec_ctx->ch_layout.nb_channels;
    if (channels <= 0) channels = 2;
    // Always output stereo — resampler handles mono→stereo upmix
    int out_channels = 2;

    // When using the pre-warmed PortAudio stream, force output to 48kHz
    // (the stream's fixed format). swresample handles the rate conversion
    // for files at 44.1k, 96k, etc. This avoids the 5+ second cold-start
    // we'd hit by closing/reopening PortAudio per file.
    int sample_rate = prewarmed_stream_ ? 48000 : in_sample_rate;

    std::fprintf(stderr, "AudioDecoder: codec=%s in_rate=%d out_rate=%d ch=%d\n",
                 codec->name, in_sample_rate, sample_rate, channels);

    // --- Set up resampler: any input format → float32 interleaved stereo ---
    SwrContext* swr = nullptr;
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    ret = swr_alloc_set_opts2(
        &swr,
        &out_layout,                                    // out layout
        AV_SAMPLE_FMT_FLT,                             // out format (float32 interleaved)
        sample_rate,                                    // out sample rate (48k if prewarmed)
        &codec_ctx->ch_layout,                          // in layout
        codec_ctx->sample_fmt,                          // in format
        in_sample_rate,                                 // in sample rate (file's native)
        0, nullptr
    );
    if (ret < 0 || !swr) {
        std::fprintf(stderr, "AudioDecoder: swr_alloc_set_opts2 failed\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        on_event_("error", "AUDIO_DECODE_INIT_FAILED:resampler init failed");
        running_.store(false);
        return;
    }
    ret = swr_init(swr);
    if (ret < 0) {
        swr_free(&swr);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        on_event_("error", "AUDIO_DECODE_INIT_FAILED:swr_init failed");
        running_.store(false);
        return;
    }

    // --- Initial seek ---
    if (start_seconds > 0.01) {
        int64_t ts = static_cast<int64_t>(start_seconds / av_q2d(audio_stream->time_base));
        av_seek_frame(fmt_ctx, stream_idx, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec_ctx);
    }

    // --- PortAudio stream: use pre-warmed if available, else open lazily ---
    PaStream* pa_stream = nullptr;
    double actual_latency = PA_LATENCY_SEC;
    PaError pa_err = paNoError;  // function-scoped; used below in Pa_WriteStream loop
    if (prewarmed_stream_) {
        // Skip Pa_OpenStream — saves 5+ seconds for Bluetooth devices.
        // The pre-warmed stream is opened at sidecar startup with a fixed
        // 48kHz stereo format; swresample (above) handles converting the
        // file's native rate/channels to that target.
        pa_stream = prewarmed_stream_;
        actual_latency = prewarmed_latency_;
        std::fprintf(stderr, "AVSYNC_DIAG audio_pa_open_done +%.0fms (prewarmed, skipped)\n", ms_since());
        std::fprintf(stderr, "AVSYNC_DIAG audio_pa_start_done +%.0fms (prewarmed, skipped)\n", ms_since());
        std::fprintf(stderr, "AudioDecoder: using pre-warmed stream (latency=%.3fs)\n", actual_latency);
        if (clock_) clock_->set_output_latency(actual_latency);
        std::fprintf(stderr, "AVSYNC_DIAG audio_ready_signal +%.0fms\n", ms_since());
        on_event_("audio_ready", "");
    } else {
        PaStreamParameters output_params{};
        output_params.device = Pa_GetDefaultOutputDevice();
        if (output_params.device == paNoDevice) {
            std::fprintf(stderr, "AudioDecoder: no default audio output device\n");
            swr_free(&swr);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            on_event_("error", "AUDIO_DEVICE_STARTUP_FAILED:no default output device");
            running_.store(false);
            return;
        }
        output_params.channelCount = out_channels;
        output_params.sampleFormat = paFloat32;
        output_params.suggestedLatency = PA_LATENCY_SEC;
        output_params.hostApiSpecificStreamInfo = nullptr;

        pa_err = Pa_OpenStream(
            &pa_stream,
            nullptr,           // no input
            &output_params,
            sample_rate,
            PA_BLOCK_SIZE,
            paClipOff,
            nullptr,           // blocking write, no callback
            nullptr
        );
        if (pa_err != paNoError) {
            std::fprintf(stderr, "AudioDecoder: Pa_OpenStream failed: %s\n", Pa_GetErrorText(pa_err));
            swr_free(&swr);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            on_event_("error", std::string("AUDIO_DEVICE_STARTUP_FAILED:") + Pa_GetErrorText(pa_err));
            running_.store(false);
            return;
        }

        std::fprintf(stderr, "AVSYNC_DIAG audio_pa_open_done +%.0fms\n", ms_since());
        pa_err = Pa_StartStream(pa_stream);
        if (pa_err != paNoError) {
            std::fprintf(stderr, "AudioDecoder: Pa_StartStream failed: %s\n", Pa_GetErrorText(pa_err));
            Pa_CloseStream(pa_stream);
            swr_free(&swr);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            on_event_("error", std::string("AUDIO_DEVICE_STARTUP_FAILED:") + Pa_GetErrorText(pa_err));
            running_.store(false);
            return;
        }

        std::fprintf(stderr, "AVSYNC_DIAG audio_pa_start_done +%.0fms\n", ms_since());
        const PaStreamInfo* stream_info = Pa_GetStreamInfo(pa_stream);
        actual_latency = stream_info ? stream_info->outputLatency : PA_LATENCY_SEC;
        std::fprintf(stderr, "AudioDecoder: PortAudio stream opened (rate=%d ch=%d suggested=%.3fs actual=%.3fs)\n",
                     sample_rate, out_channels, PA_LATENCY_SEC, actual_latency);
        if (clock_) clock_->set_output_latency(actual_latency);
        std::fprintf(stderr, "AVSYNC_DIAG audio_ready_signal +%.0fms\n", ms_since());
        on_event_("audio_ready", "");
    }

    int audio_stall_count = 0;
    // --- Decode loop ---
    bool first_write_logged = false;
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    std::vector<float> out_buf;  // reusable output buffer
    double last_applied_speed = 1.0;  // Batch 4.1 — swr_set_compensation re-arm latch

    // Batch 4.3 — compressor thread-local state. envelope follows the
    // stereo peak; attack/release coefs are one-pole filter constants
    // derived from the output sample rate. attack_sec = 0.010 (10 ms),
    // release_sec = 0.100 (100 ms). The coef formula
    // `exp(-1 / (sample_rate * time_sec))` gives a first-order response
    // that reaches ~63% of target after `time_sec` seconds.
    float drc_envelope       = 0.0f;
    const float drc_attack_coef  = std::exp(-1.0f / (static_cast<float>(sample_rate) * 0.010f));
    const float drc_release_coef = std::exp(-1.0f / (static_cast<float>(sample_rate) * 0.100f));

    while (!stop_flag_.load()) {
        // Handle pending seek
        if (seek_pending_.load()) {
            double seek_sec;
            {
                std::lock_guard<std::mutex> lock(seek_mutex_);
                seek_sec = seek_target_sec_;
                seek_pending_.store(false);
            }
            int64_t ts = static_cast<int64_t>(seek_sec / av_q2d(audio_stream->time_base));
            av_seek_frame(fmt_ctx, stream_idx, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec_ctx);
            if (clock_) {
                clock_->seek_anchor(static_cast<int64_t>(seek_sec * 1000000.0));
            }
            std::fprintf(stderr, "AudioDecoder: seeked to %.3fs\n", seek_sec);
            continue;
        }

        // Handle pause: block via condition variable (efficient, no polling)
        if (paused_.load()) {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this]() {
                return !paused_.load() || stop_flag_.load() || seek_pending_.load();
            });
            if (stop_flag_.load()) break;
            continue;  // re-check seek/stop
        }

        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // Drain remaining frames
                avcodec_send_packet(codec_ctx, nullptr);
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    av_frame_unref(frame);
                }
                if (!stop_flag_.load()) {
                    std::fprintf(stderr, "AudioDecoder: EOF\n");
                    on_event_("eof", "");
                }
                break;
            } else if (is_http && (ret == AVERROR(EAGAIN) || ret == AVERROR(ETIMEDOUT)
                                   || ret == AVERROR(EIO) || ret == AVERROR_EXIT)) {
                ++audio_stall_count;
                if (audio_stall_count > 60) {
                    std::fprintf(stderr, "AudioDecoder: HTTP stall timeout (30s)\n");
                    on_event_("error", "AUDIO_STREAM_TIMEOUT:no data for 30 seconds");
                    break;
                }
                if (audio_stall_count == 1) {
                    std::fprintf(stderr, "AudioDecoder: HTTP stall, retrying...\n");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            } else {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                std::fprintf(stderr, "AudioDecoder: av_read_frame error: %s\n", errbuf);
                on_event_("error", std::string("AUDIO_DECODE_FAILED:") + errbuf);
                break;
            }
        }
        audio_stall_count = 0;

        // Skip non-audio packets
        if (pkt->stream_index != stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) continue;

        while (!stop_flag_.load()) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // Compute PTS in microseconds
            int64_t pts_us = 0;
            if (frame->pts != AV_NOPTS_VALUE) {
                pts_us = av_rescale_q(frame->pts, audio_stream->time_base, {1, 1000000});
            }

            // Lazy-init audio filter graph with actual stream parameters
            if (audio_filter_ && !audio_filter_->active()) {
                auto spec = audio_filter_->take_pending();
                if (!spec.empty()) {
                    audio_filter_->init_audio(codec_ctx->sample_rate,
                                              codec_ctx->ch_layout,
                                              codec_ctx->sample_fmt, spec);
                }
            }

            // Apply audio filter if active
            AVFrame* audio_src = frame;
            AVFrame* filtered_audio = nullptr;
            if (audio_filter_ && audio_filter_->active()) {
                int fret = audio_filter_->push_frame(frame);
                if (fret >= 0) {
                    filtered_audio = av_frame_alloc();
                    fret = audio_filter_->pull_frame(filtered_audio);
                    if (fret >= 0) {
                        audio_src = filtered_audio;
                    } else {
                        av_frame_free(&filtered_audio);
                        filtered_audio = nullptr;
                    }
                }
            }

            // Batch 4.1 — A/V drift correction. Main-app's SyncClock
            // derives a clock velocity from per-frame render latency;
            // a 500ms ticker in VideoPlayer forwards that as the
            // `set_audio_speed` JSON command, which updates `speed_`.
            //
            // Phase 4 REVIEW P1 fix (2026-04-15, Agent 6): swr_set_compensation
            // is one-shot per call per libswresample semantics — it schedules
            // `sample_delta` samples of pad/drop across the NEXT
            // `compensation_distance` output samples then returns to zero.
            // Original gate `fabs(requested_speed - last_applied_speed) > 1e-4`
            // meant steady-state drift triggered ONE re-arm of ~21ms then
            // went silent until speed oscillated — near-nothing over a
            // one-hour drift rather than continuous correction. Kodi's
            // ActiveAEResampleFFMPEG re-arms every Resample when ratio
            // != 1.0; matching that pattern here. Guard is now "only skip
            // when we're explicitly at unity" rather than "skip when
            // unchanged since last chunk." compensation_distance=1024
            // spreads the sample add/drop over ~20ms at 48kHz (imperceptible).
            // Formula: delta = distance * (1/speed - 1), so slower playback
            // (speed<1.0) -> positive delta -> more output samples, faster
            // (speed>1.0) -> negative delta -> fewer.
            const double requested_speed = speed_.load(std::memory_order_relaxed);
            if (std::fabs(requested_speed - 1.0) > 1e-4) {
                constexpr int kCompDistance = 1024;
                const int delta = static_cast<int>(
                    kCompDistance * (1.0 / requested_speed - 1.0));
                swr_set_compensation(swr, delta, kCompDistance);
                last_applied_speed = requested_speed;
            } else if (std::fabs(last_applied_speed - 1.0) > 1e-4) {
                // Dropped back to unity — explicitly zero out any pending
                // compensation so the NEXT chunk is clean (no inherited
                // pad/drop from the prior non-unity re-arm's residual).
                swr_set_compensation(swr, 0, 0);
                last_applied_speed = 1.0;
            }

            // Resample to float32 interleaved
            int out_samples = swr_get_out_samples(swr, audio_src->nb_samples);
            if (out_samples <= 0) {
                if (filtered_audio) av_frame_free(&filtered_audio);
                av_frame_unref(frame);
                continue;
            }
            out_buf.resize(static_cast<size_t>(out_samples) * out_channels);
            uint8_t* out_ptr = reinterpret_cast<uint8_t*>(out_buf.data());
            int converted = swr_convert(
                swr,
                &out_ptr, out_samples,
                const_cast<const uint8_t**>(audio_src->extended_data), audio_src->nb_samples
            );
            if (filtered_audio) av_frame_free(&filtered_audio);
            av_frame_unref(frame);

            if (converted <= 0) continue;

            // Apply volume
            // Batch 4.2 — amp zone (vol > 1.0) uses tanh soft-clip so
            // samples that would clip past ±1.0 compress smoothly
            // instead of hard-limiting (which rings in the ear). tanh is
            // cheap per-sample (~50ns on modern x86) and the branch is
            // only taken in the amp zone — unity and attenuation paths
            // stay on the unchanged multiply. Behavior:
            //   vol = 1.5 → peaks near 0.90 on full-scale input
            //   vol = 2.0 → peaks near 0.96 on full-scale input
            // In all amp cases output stays in [-1, +1] — no hardware clip.
            float vol = volume_ ? volume_->effective_volume() : 1.0f;
            if (vol > 1.0f) {
                int total = converted * out_channels;
                for (int i = 0; i < total; ++i) {
                    out_buf[i] = std::tanh(out_buf[i] * vol);
                }
            } else if (vol != 1.0f) {
                int total = converted * out_channels;
                for (int i = 0; i < total; ++i) {
                    out_buf[i] *= vol;
                }
            }

            // Batch 4.3 — Dynamic Range Compression (post-volume).
            // Simple feed-forward compressor with one-pole envelope
            // follower: threshold -12 dB (0.2512 linear), ratio 3:1,
            // attack 10 ms, release 100 ms. "Loud movie at low volume
            // keeps dialogue audible" — compresses peaks so the mid-
            // tone RMS rises relative to full-scale, letting users set
            // a conservative volume while still hearing quiet dialogue.
            // Off by default; toggled per-file via EqualizerPopover's
            // DRC checkbox. Stereo peak across channels drives a shared
            // gain so the image doesn't wobble between L and R.
            if (drc_enabled_.load(std::memory_order_relaxed)) {
                constexpr float kThreshLin = 0.2512f;  // 10^(-12/20)
                constexpr float kThreshDb  = -12.0f;
                constexpr float kRatio     = 3.0f;
                constexpr float kInvRatio  = 1.0f / kRatio;
                const int total = converted * out_channels;
                for (int i = 0; i < total; i += out_channels) {
                    // Peak across channels for a shared-gain compressor.
                    float peak = 0.0f;
                    for (int c = 0; c < out_channels; ++c) {
                        float a = std::fabs(out_buf[i + c]);
                        if (a > peak) peak = a;
                    }
                    // One-pole envelope follower — attack when rising,
                    // release when falling. Both coefs are fractions of
                    // "how much of previous envelope to retain".
                    const float coef = (peak > drc_envelope)
                        ? drc_attack_coef : drc_release_coef;
                    drc_envelope = coef * drc_envelope + (1.0f - coef) * peak;

                    // Gain reduction only when envelope exceeds threshold.
                    float gain = 1.0f;
                    if (drc_envelope > kThreshLin) {
                        const float env_db = 20.0f * std::log10(drc_envelope + 1e-9f);
                        const float over_db = env_db - kThreshDb;
                        const float reduction_db = over_db * (1.0f - kInvRatio);
                        gain = std::pow(10.0f, -reduction_db / 20.0f);
                    }
                    // Apply shared gain to all channels.
                    for (int c = 0; c < out_channels; ++c) {
                        out_buf[i + c] *= gain;
                    }
                }
            }

            // Write to PortAudio (blocking)
            if (!first_write_logged) {
                std::fprintf(stderr, "AVSYNC_DIAG audio_first_pa_write +%.0fms pts=%.3fs\n",
                             ms_since(), pts_us / 1e6);
            }
            pa_err = Pa_WriteStream(pa_stream, out_buf.data(),
                                    static_cast<unsigned long>(converted));
            if (!first_write_logged) {
                std::fprintf(stderr, "AVSYNC_DIAG audio_first_pa_write_returned +%.0fms\n", ms_since());
                first_write_logged = true;
            }
            if (pa_err != paNoError && pa_err != paOutputUnderflowed) {
                std::fprintf(stderr, "AudioDecoder: Pa_WriteStream error: %s\n",
                             Pa_GetErrorText(pa_err));
                // Don't break on underflow — just continue
                if (pa_err != paOutputUnderflowed) {
                    on_event_("error", std::string("AUDIO_DEVICE_LOST:") + Pa_GetErrorText(pa_err));
                    goto cleanup;
                }
            }

            // Update master clock after device accepted the buffer
            if (clock_) {
                clock_->update(pts_us);
            }
        }
    }

cleanup:
    // Don't abort/close the pre-warmed stream — it's owned by main.cpp and
    // shared across sessions. Closing it would defeat the whole point of
    // pre-warming (we'd pay the 5s cold-start on the next file).
    if (pa_stream && pa_stream != prewarmed_stream_) {
        // Abort (not stop) for immediate silence — Pa_StopStream drains the
        // buffer which causes audible lingering after the player is closed.
        Pa_AbortStream(pa_stream);
        Pa_CloseStream(pa_stream);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    std::fprintf(stderr, "AudioDecoder: thread exiting\n");
    running_.store(false);
}
