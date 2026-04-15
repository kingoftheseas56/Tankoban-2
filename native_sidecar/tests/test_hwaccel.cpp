#include <gtest/gtest.h>

extern "C" {
#include <libavutil/hwcontext.h>
}

TEST(HWAccel, D3D11VADeviceCreateDoesNotCrash) {
    // Smoke test: attempt to create a D3D11VA device.
    // On machines with a GPU, this succeeds. On CI/headless, it fails gracefully.
    AVBufferRef* hw_device_ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&hw_device_ctx,
                                     AV_HWDEVICE_TYPE_D3D11VA,
                                     nullptr, nullptr, 0);
    if (ret == 0) {
        // GPU available — verify context is valid
        EXPECT_NE(hw_device_ctx, nullptr);
        av_buffer_unref(&hw_device_ctx);
    } else {
        // No GPU or driver issue — just verify clean failure (no crash)
        EXPECT_EQ(hw_device_ctx, nullptr);
    }
}

TEST(HWAccel, UnknownDeviceTypeFailsCleanly) {
    // Requesting an invalid device type should fail without crash
    AVBufferRef* hw_device_ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&hw_device_ctx,
                                     static_cast<AVHWDeviceType>(9999),
                                     nullptr, nullptr, 0);
    EXPECT_NE(ret, 0);
    EXPECT_EQ(hw_device_ctx, nullptr);
}
