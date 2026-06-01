// In-Process Player POC — Phase 0 build-seam probe.
//
// Temporary spike artifact. Its only job is to PROVE that FFmpeg's
// libavformat/libavcodec link into the main MSVC Tankoban target and run
// in-process (open the One Piece fixture, report stream count). If this builds
// and the runtime call succeeds, the make-or-break prerequisite for the
// in-process player is met. Deleted at the end of Phase 0.
//
// Gated entirely by TANKOBAN_INPROCESS_POC so it is inert in normal builds.
#ifdef TANKOBAN_INPROCESS_POC
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <cstdio>

// Called once at startup from main.cpp when TANKOBAN_INPROCESS_POC is set in the
// environment. Proves: (1) the MSVC link resolved av* symbols, (2) the runtime
// DLLs load, (3) in-process demux of a real file works.
void inproc_probe(const char* path) {
    std::fprintf(stderr, "INPROC_PROBE: avcodec build=%u path=%s\n",
                 avcodec_version(), path ? path : "(null)");
    AVFormatContext* fmt = nullptr;
    int rc = avformat_open_input(&fmt, path, nullptr, nullptr);
    if (rc < 0 || !fmt) {
        std::fprintf(stderr, "INPROC_PROBE: avformat_open_input FAILED rc=%d\n", rc);
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        std::fprintf(stderr, "INPROC_PROBE: find_stream_info failed\n");
    }
    int v = -1, a = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters* p = fmt->streams[i]->codecpar;
        if (p->codec_type == AVMEDIA_TYPE_VIDEO && v < 0) v = (int)i;
        if (p->codec_type == AVMEDIA_TYPE_AUDIO && a < 0) a = (int)i;
    }
    std::fprintf(stderr,
        "INPROC_PROBE: OPEN OK nb_streams=%d video_stream=%d audio_stream=%d "
        "duration=%.1fs\n",
        (int)fmt->nb_streams, v, a,
        fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : -1.0);
    avformat_close_input(&fmt);
    std::fprintf(stderr, "INPROC_PROBE: DONE — in-process FFmpeg link+runtime PROVEN\n");
}
#endif
