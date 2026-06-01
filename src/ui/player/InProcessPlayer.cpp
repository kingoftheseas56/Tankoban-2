#ifdef TANKOBAN_INPROCESS_POC
#include "ui/player/InProcessPlayer.h"
#include "ring_buffer.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

// Unique-per-open ring name suffix so re-opens never collide.
static std::atomic<int> g_inprocCounter{0};

InProcessPlayer::InProcessPlayer(QObject* parent) : QObject(parent) {}
InProcessPlayer::~InProcessPlayer() { stop(); }

bool InProcessPlayer::openFile(const QString& path) {
    stop();
    m_stop.store(false);

    // --- Probe for video dimensions (needed to size the SHM ring) ---
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        std::fprintf(stderr, "INPROC: open_input failed\n");
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        std::fprintf(stderr, "INPROC: find_stream_info failed\n");
        avformat_close_input(&fmt);
        return false;
    }
    int vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vstream < 0) {
        std::fprintf(stderr, "INPROC: no video stream\n");
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecParameters* cp = fmt->streams[vstream]->codecpar;
    m_width = cp->width;
    m_height = cp->height;
    avformat_close_input(&fmt);   // reopened on the decode thread
    if (m_width <= 0 || m_height <= 0) {
        std::fprintf(stderr, "INPROC: bad dims %dx%d\n", m_width, m_height);
        return false;
    }

    m_slotCount = 4;
    m_slotBytes = m_width * m_height * 4;   // BGRA, stride = width*4

    // --- Create the in-process named SHM ring (single process; no boundary) ---
    const size_t total = ring_buffer_size(m_slotCount, m_slotBytes);
#ifdef _WIN32
    m_shmName = QStringLiteral("TankobanInProc_%1_%2")
                    .arg((qulonglong)GetCurrentProcessId())
                    .arg(g_inprocCounter.fetch_add(1));
    m_hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        (DWORD)(((quint64)total) >> 32), (DWORD)(((quint64)total) & 0xFFFFFFFFULL),
        m_shmName.toLocal8Bit().constData());
    if (!m_hMapFile) {
        std::fprintf(stderr, "INPROC: CreateFileMapping failed (err=%lu)\n", GetLastError());
        return false;
    }
    m_mapView = MapViewOfFile((HANDLE)m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, total);
    if (!m_mapView) {
        std::fprintf(stderr, "INPROC: MapViewOfFile failed (err=%lu)\n", GetLastError());
        teardownShm();
        return false;
    }
#else
    std::fprintf(stderr, "INPROC: POC is Windows-only\n");
    return false;
#endif

    std::memset(m_mapView, 0, total);
    m_ringWriter = std::make_unique<FrameRingWriter>(m_mapView, m_slotCount, m_slotBytes);

    std::fprintf(stderr, "INPROC: ring ready name=%s %dx%d slotBytes=%d\n",
                 m_shmName.toLocal8Bit().constData(), m_width, m_height, m_slotBytes);

    m_decodeThread = std::thread(&InProcessPlayer::decodeThreadFunc, this, path.toStdString());
    return true;
}

void InProcessPlayer::decodeThreadFunc(std::string path) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return; }
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) { avformat_close_input(&fmt); return; }

    AVStream* st = fmt->streams[vs];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); return; }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return;
    }

    SwsContext* sws = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    std::vector<uint8_t> bgra(m_slotBytes);
    const int stride = m_width * 4;
    const double tb = av_q2d(st->time_base);

    auto t0 = std::chrono::steady_clock::now();
    bool clockStarted = false;
    int64_t firstPtsUs = 0;
    int64_t frameCount = 0;

    while (!m_stop.load() && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (!m_stop.load() && avcodec_receive_frame(ctx, frame) == 0) {
                    if (!sws) {
                        sws = sws_getContext(frame->width, frame->height,
                                             (AVPixelFormat)frame->format,
                                             m_width, m_height, AV_PIX_FMT_BGRA,
                                             SWS_BILINEAR, nullptr, nullptr, nullptr);
                        if (!sws) { std::fprintf(stderr, "INPROC: sws_getContext failed\n"); break; }
                    }
                    uint8_t* dst[4] = { bgra.data(), nullptr, nullptr, nullptr };
                    int dstStride[4] = { stride, 0, 0, 0 };
                    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);

                    int64_t absPtsUs = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                        ? (int64_t)(frame->best_effort_timestamp * tb * 1e6) : 0;
                    if (!clockStarted) {
                        firstPtsUs = absPtsUs;
                        t0 = std::chrono::steady_clock::now();
                        clockStarted = true;
                    }
                    // Relative-from-start time for BOTH frame pts and clock so
                    // FrameCanvas's clock-vs-pts matching is consistent.
                    int64_t relUs = absPtsUs - firstPtsUs;
                    if (relUs < 0) relUs = 0;

                    // Pace production to real time (no audio master in Phase 1).
                    for (;;) {
                        if (m_stop.load()) break;
                        int64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0).count();
                        if (elapsed >= relUs) break;
                        int64_t waitUs = relUs - elapsed;
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(std::min<int64_t>(waitUs, 15000)));
                    }
                    if (m_stop.load()) break;

                    m_ringWriter->write_clock_us(relUs);
                    m_ringWriter->write_frame(bgra.data(), m_slotBytes,
                                              m_width, m_height, stride, relUs);
                    if (++frameCount <= 3 || (frameCount % 240) == 0) {
                        std::fprintf(stderr, "INPROC: wrote frame #%lld rel=%.2fs\n",
                                     (long long)frameCount, relUs / 1e6);
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (sws) sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    std::fprintf(stderr, "INPROC: decode thread exit (frames=%lld)\n", (long long)frameCount);
}

void InProcessPlayer::stop() {
    m_stop.store(true);
    if (m_decodeThread.joinable()) m_decodeThread.join();
    m_ringWriter.reset();
    teardownShm();
    m_shmName.clear();
}

void InProcessPlayer::teardownShm() {
#ifdef _WIN32
    if (m_mapView) { UnmapViewOfFile(m_mapView); m_mapView = nullptr; }
    if (m_hMapFile) { CloseHandle((HANDLE)m_hMapFile); m_hMapFile = nullptr; }
#endif
}
#endif // TANKOBAN_INPROCESS_POC
