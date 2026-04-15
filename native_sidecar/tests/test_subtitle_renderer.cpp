#include <gtest/gtest.h>
#include "subtitle_renderer.h"
#include <cstring>
#include <vector>

// Minimal ASS header for testing
static const char* TEST_ASS_HEADER =
    "[Script Info]\r\n"
    "ScriptType: v4.00+\r\n"
    "PlayResX: 384\r\n"
    "PlayResY: 288\r\n"
    "\r\n"
    "[V4+ Styles]\r\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
    "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
    "Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,2,1,2,10,10,20,1\r\n"
    "\r\n"
    "[Events]\r\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";

TEST(SubtitleRenderer, ConstructDestroy) {
    SubtitleRenderer renderer;
    // Should not crash
}

TEST(SubtitleRenderer, SetFrameSize) {
    SubtitleRenderer renderer;
    renderer.set_frame_size(1920, 1080);
    // Should not crash
}

TEST(SubtitleRenderer, RenderNoTrackIsNoop) {
    SubtitleRenderer renderer;
    renderer.set_frame_size(64, 64);

    std::vector<uint8_t> frame(64 * 64 * 4, 0);
    std::vector<uint8_t> original = frame;

    renderer.render_onto_frame(frame.data(), 64, 64, 64 * 4, 1000);

    // Frame should be unchanged — no track loaded
    EXPECT_EQ(frame, original);
}

TEST(SubtitleRenderer, LoadEmbeddedASS) {
    SubtitleRenderer renderer;
    renderer.set_frame_size(384, 288);

    std::vector<uint8_t> extradata(TEST_ASS_HEADER,
                                   TEST_ASS_HEADER + std::strlen(TEST_ASS_HEADER));
    renderer.load_embedded_track("ass", extradata);

    // Feed a subtitle event that appears at 1000ms
    const char* chunk = "0,0,Default,,0,0,0,,Hello World";
    renderer.process_packet(reinterpret_cast<const uint8_t*>(chunk),
                            static_cast<int>(std::strlen(chunk)),
                            1000, 2000);

    // Render at 1500ms (within the event window)
    std::vector<uint8_t> frame(384 * 288 * 4, 0);
    renderer.render_onto_frame(frame.data(), 384, 288, 384 * 4, 1500);

    // Check that at least some pixels were modified (subtitle rendered)
    bool any_nonzero = false;
    for (size_t i = 0; i < frame.size(); i += 4) {
        if (frame[i] != 0 || frame[i + 1] != 0 || frame[i + 2] != 0) {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero) << "Expected subtitle pixels on frame";
}

TEST(SubtitleRenderer, VisibilityToggle) {
    SubtitleRenderer renderer;
    renderer.set_frame_size(384, 288);

    std::vector<uint8_t> extradata(TEST_ASS_HEADER,
                                   TEST_ASS_HEADER + std::strlen(TEST_ASS_HEADER));
    renderer.load_embedded_track("ass", extradata);

    const char* chunk = "0,0,Default,,0,0,0,,Visible Test";
    renderer.process_packet(reinterpret_cast<const uint8_t*>(chunk),
                            static_cast<int>(std::strlen(chunk)),
                            1000, 5000);

    // Hidden: frame should be unchanged
    renderer.set_visible(false);
    std::vector<uint8_t> frame(384 * 288 * 4, 0);
    std::vector<uint8_t> original = frame;
    renderer.render_onto_frame(frame.data(), 384, 288, 384 * 4, 2000);
    EXPECT_EQ(frame, original) << "Frame should be unchanged when visibility is off";

    // Visible again: frame should have subtitle pixels
    renderer.set_visible(true);
    renderer.render_onto_frame(frame.data(), 384, 288, 384 * 4, 2000);
    bool any_nonzero = false;
    for (size_t i = 0; i < frame.size(); i += 4) {
        if (frame[i] != 0 || frame[i + 1] != 0 || frame[i + 2] != 0) {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero) << "Expected subtitle pixels when visible";
}

TEST(SubtitleRenderer, ClearTrackStopsRendering) {
    SubtitleRenderer renderer;
    renderer.set_frame_size(384, 288);

    std::vector<uint8_t> extradata(TEST_ASS_HEADER,
                                   TEST_ASS_HEADER + std::strlen(TEST_ASS_HEADER));
    renderer.load_embedded_track("ass", extradata);

    const char* chunk = "0,0,Default,,0,0,0,,Clear Test";
    renderer.process_packet(reinterpret_cast<const uint8_t*>(chunk),
                            static_cast<int>(std::strlen(chunk)),
                            1000, 5000);

    renderer.clear_track();

    std::vector<uint8_t> frame(384 * 288 * 4, 0);
    std::vector<uint8_t> original = frame;
    renderer.render_onto_frame(frame.data(), 384, 288, 384 * 4, 2000);
    EXPECT_EQ(frame, original) << "Frame should be unchanged after clear_track";
}

TEST(SubtitleRenderer, SRTTextSubtitle) {
    SubtitleRenderer renderer;
    renderer.set_frame_size(384, 288);

    // Load as SRT (text subtitle with default ASS header)
    renderer.load_embedded_track("subrip", {});

    // SRT text packet — raw text like FFmpeg delivers
    const char* text = "Hello SRT";
    renderer.process_packet(reinterpret_cast<const uint8_t*>(text),
                            static_cast<int>(std::strlen(text)),
                            1000, 3000);

    std::vector<uint8_t> frame(384 * 288 * 4, 0);
    renderer.render_onto_frame(frame.data(), 384, 288, 384 * 4, 2000);

    bool any_nonzero = false;
    for (size_t i = 0; i < frame.size(); i += 4) {
        if (frame[i] != 0 || frame[i + 1] != 0 || frame[i + 2] != 0) {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero) << "Expected SRT subtitle pixels on frame";
}
