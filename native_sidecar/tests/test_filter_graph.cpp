#include <gtest/gtest.h>
#include "filter_graph.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
}

TEST(FilterGraph, ConstructDestroy) {
    FilterGraph fg;
    EXPECT_FALSE(fg.active());
}

TEST(FilterGraph, VideoNullPassthrough) {
    FilterGraph fg;
    AVRational tb = {1, 25};
    AVRational sar = {1, 1};
    bool ok = fg.init_video(320, 240, AV_PIX_FMT_YUV420P, tb, sar, "null");
    ASSERT_TRUE(ok);
    EXPECT_TRUE(fg.active());

    // Create a test frame
    AVFrame* in = av_frame_alloc();
    in->format = AV_PIX_FMT_YUV420P;
    in->width = 320;
    in->height = 240;
    in->pts = 1000;
    av_frame_get_buffer(in, 32);
    // Fill with a known value
    memset(in->data[0], 128, in->linesize[0] * in->height);
    memset(in->data[1], 64, in->linesize[1] * in->height / 2);
    memset(in->data[2], 64, in->linesize[2] * in->height / 2);

    int ret = fg.push_frame(in);
    EXPECT_EQ(ret, 0);

    AVFrame* out = av_frame_alloc();
    ret = fg.pull_frame(out);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(out->width, 320);
    EXPECT_EQ(out->height, 240);
    EXPECT_EQ(out->pts, 1000);

    av_frame_free(&out);
    av_frame_free(&in);
}

TEST(FilterGraph, DestroyResetsActive) {
    FilterGraph fg;
    AVRational tb = {1, 25};
    AVRational sar = {1, 1};
    fg.init_video(320, 240, AV_PIX_FMT_YUV420P, tb, sar, "null");
    EXPECT_TRUE(fg.active());
    fg.destroy();
    EXPECT_FALSE(fg.active());
}

TEST(FilterGraph, ReinitAfterDestroy) {
    FilterGraph fg;
    AVRational tb = {1, 25};
    AVRational sar = {1, 1};
    fg.init_video(320, 240, AV_PIX_FMT_YUV420P, tb, sar, "null");
    fg.destroy();
    bool ok = fg.init_video(640, 480, AV_PIX_FMT_YUV420P, tb, sar, "null");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(fg.active());
}

TEST(FilterGraph, AudioNullPassthrough) {
    FilterGraph fg;
    AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
    bool ok = fg.init_audio(48000, layout, AV_SAMPLE_FMT_FLTP, "anull");
    ASSERT_TRUE(ok);
    EXPECT_TRUE(fg.active());

    // Create a test audio frame
    AVFrame* in = av_frame_alloc();
    in->format = AV_SAMPLE_FMT_FLTP;
    in->sample_rate = 48000;
    av_channel_layout_copy(&in->ch_layout, &layout);
    in->nb_samples = 1024;
    in->pts = 0;
    av_frame_get_buffer(in, 0);
    memset(in->data[0], 0, sizeof(float) * 1024);
    memset(in->data[1], 0, sizeof(float) * 1024);

    int ret = fg.push_frame(in);
    EXPECT_EQ(ret, 0);

    AVFrame* out = av_frame_alloc();
    ret = fg.pull_frame(out);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(out->nb_samples, 1024);

    av_frame_free(&out);
    av_frame_free(&in);
}

TEST(FilterGraph, InvalidFilterFails) {
    FilterGraph fg;
    AVRational tb = {1, 25};
    AVRational sar = {1, 1};
    bool ok = fg.init_video(320, 240, AV_PIX_FMT_YUV420P, tb, sar, "nonexistent_filter_xyz");
    EXPECT_FALSE(ok);
    EXPECT_FALSE(fg.active());
}

TEST(FilterGraph, PushWithoutInitFails) {
    FilterGraph fg;
    AVFrame* in = av_frame_alloc();
    int ret = fg.push_frame(in);
    EXPECT_NE(ret, 0);
    av_frame_free(&in);
}
