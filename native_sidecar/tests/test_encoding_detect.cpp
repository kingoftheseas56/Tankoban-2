#include <gtest/gtest.h>
#include "encoding_detect.h"
#include <cstring>

TEST(EncodingDetect, Utf8PassesThrough) {
    const char* text = "Hello, world! This is plain UTF-8.";
    std::string enc = detect_encoding(text, std::strlen(text));
    EXPECT_EQ(enc, "UTF-8");
}

TEST(EncodingDetect, EmptyReturnsUtf8) {
    EXPECT_EQ(detect_encoding(nullptr, 0), "UTF-8");
    EXPECT_EQ(detect_encoding("", 0), "UTF-8");
}

TEST(EncodingDetect, TranscodeUtf8Unchanged) {
    const char* text = "Plain ASCII / UTF-8 text.";
    std::string result = transcode_to_utf8(text, std::strlen(text));
    EXPECT_EQ(result, text);
}

TEST(EncodingDetect, TranscodeEmptyReturnsEmpty) {
    EXPECT_EQ(transcode_to_utf8(nullptr, 0), "");
    EXPECT_EQ(transcode_to_utf8("", 0), "");
}

// Shift-JIS encoded string: Japanese text that uchardet should detect.
// "日本語テスト" in Shift-JIS:
// 日=93FA 本=967B 語=8CEA テ=8365 ス=8358 ト=8367
TEST(EncodingDetect, DetectsShiftJIS) {
    const unsigned char sjis[] = {
        0x93, 0xFA, 0x96, 0x7B, 0x8C, 0xEA, 0x83, 0x65,
        0x83, 0x58, 0x83, 0x67, 0x0D, 0x0A,
        // Repeat to give uchardet enough data for confident detection
        0x93, 0xFA, 0x96, 0x7B, 0x8C, 0xEA, 0x83, 0x65,
        0x83, 0x58, 0x83, 0x67, 0x0D, 0x0A,
        0x93, 0xFA, 0x96, 0x7B, 0x8C, 0xEA, 0x83, 0x65,
        0x83, 0x58, 0x83, 0x67, 0x0D, 0x0A,
    };
    std::string enc = detect_encoding(reinterpret_cast<const char*>(sjis), sizeof(sjis));
    EXPECT_EQ(enc, "SHIFT_JIS");
}

TEST(EncodingDetect, TranscodesShiftJISToUtf8) {
    // "日本語" in Shift-JIS (3 chars repeated for detection confidence)
    const unsigned char sjis[] = {
        0x93, 0xFA, 0x96, 0x7B, 0x8C, 0xEA, 0x0D, 0x0A,
        0x93, 0xFA, 0x96, 0x7B, 0x8C, 0xEA, 0x0D, 0x0A,
        0x93, 0xFA, 0x96, 0x7B, 0x8C, 0xEA, 0x0D, 0x0A,
        0x93, 0xFA, 0x96, 0x7B, 0x8C, 0xEA, 0x0D, 0x0A,
    };
    std::string result = transcode_to_utf8(
        reinterpret_cast<const char*>(sjis), sizeof(sjis));
    // The result should be valid UTF-8 containing "日本語\r\n" repeated.
    // "日" in UTF-8 = E6 97 A5, "本" = E6 9C AC, "語" = E8 AA 9E
    EXPECT_NE(result, std::string(reinterpret_cast<const char*>(sjis), sizeof(sjis)));
    // Check that UTF-8 for "日" is present
    EXPECT_NE(result.find("\xE6\x97\xA5"), std::string::npos);
    // Check that UTF-8 for "本" is present
    EXPECT_NE(result.find("\xE6\x9C\xAC"), std::string::npos);
}

// UTF-8 with BOM should still be detected as UTF-8
TEST(EncodingDetect, Utf8WithBom) {
    const char bom_text[] = "\xEF\xBB\xBFHello UTF-8 with BOM";
    std::string enc = detect_encoding(bom_text, std::strlen(bom_text));
    EXPECT_EQ(enc, "UTF-8");
}
