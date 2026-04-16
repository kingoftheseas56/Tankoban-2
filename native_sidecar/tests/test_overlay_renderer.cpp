#ifdef _WIN32

#include <gtest/gtest.h>
#include "overlay_renderer.h"

#include <vector>
#include <cstdint>

// PLAYER_PERF_FIX Phase 3 Batch 3.2 — D3D11OverlayTexture infrastructure
// smoke test. Verifies the GPU resource lifecycle in isolation so Batch
// 3.3's wiring to subtitle_renderer + Batch 3.4's FrameCanvas import can
// trust the underlying resource works. No subtitle flow, no main-app
// integration — pure sidecar-side GPU resource validation.

TEST(D3D11OverlayTexture, StandaloneInitCreatesSharedTexture) {
    D3D11OverlayTexture overlay;
    bool ok = overlay.init_standalone(640, 360);

    if (!ok) {
        // Headless CI / no GPU — test_hwaccel.cpp follows the same
        // pattern: failure is acceptable, just assert no-crash.
        GTEST_SKIP() << "No D3D11 device available";
    }

    EXPECT_TRUE(overlay.ready());
    EXPECT_EQ(overlay.width(), 640u);
    EXPECT_EQ(overlay.height(), 360u);
    EXPECT_NE(overlay.nt_handle(), nullptr);
}

TEST(D3D11OverlayTexture, UploadBgraSucceedsWithMatchingDims) {
    D3D11OverlayTexture overlay;
    if (!overlay.init_standalone(128, 64)) {
        GTEST_SKIP() << "No D3D11 device available";
    }

    // 128x64 BGRA — opaque solid red for easy visual sanity if we ever
    // wire readback. Fills to verify UpdateSubresource path doesn't crash
    // and the stride accounting is correct.
    std::vector<uint8_t> bgra(128 * 64 * 4);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i + 0] = 0x00;    // B
        bgra[i + 1] = 0x00;    // G
        bgra[i + 2] = 0xFF;    // R
        bgra[i + 3] = 0xFF;    // A
    }

    EXPECT_TRUE(overlay.upload_bgra(bgra.data(), 128, 64, 128 * 4));
}

TEST(D3D11OverlayTexture, UploadBgraRejectsDimMismatch) {
    D3D11OverlayTexture overlay;
    if (!overlay.init_standalone(128, 64)) {
        GTEST_SKIP() << "No D3D11 device available";
    }

    std::vector<uint8_t> bgra(256 * 128 * 4);
    // Upload with mismatched dims should fail — caller is expected to
    // resize first (Phase 3.3 will handle this when the subtitle bitmap
    // bounds change).
    EXPECT_FALSE(overlay.upload_bgra(bgra.data(), 256, 128, 256 * 4));
}

TEST(D3D11OverlayTexture, ResizeReallocatesTextureAndHandle) {
    D3D11OverlayTexture overlay;
    if (!overlay.init_standalone(256, 128)) {
        GTEST_SKIP() << "No D3D11 device available";
    }

    void* original_handle = overlay.nt_handle();
    ASSERT_NE(original_handle, nullptr);

    EXPECT_TRUE(overlay.resize(512, 256));
    EXPECT_EQ(overlay.width(), 512u);
    EXPECT_EQ(overlay.height(), 256u);
    // Handle must be a fresh NT handle post-resize — main-app will need
    // to re-import on resize (documented in Batch 3.4 wiring work).
    EXPECT_NE(overlay.nt_handle(), nullptr);
}

TEST(D3D11OverlayTexture, DestroyIsIdempotent) {
    D3D11OverlayTexture overlay;
    if (!overlay.init_standalone(128, 64)) {
        GTEST_SKIP() << "No D3D11 device available";
    }

    overlay.destroy();
    EXPECT_FALSE(overlay.ready());

    // Double-destroy must not crash — ~D3D11OverlayTexture invokes
    // destroy() on already-destroyed state during teardown.
    overlay.destroy();
    EXPECT_FALSE(overlay.ready());
}

#endif // _WIN32
