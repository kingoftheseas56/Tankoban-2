#pragma once
// D3D11 overlay texture — subtitle overlay resource for mpv-parity GPU path.
//
// PLAYER_PERF_FIX Phase 3 Batch 3.2 (infrastructure only — not wired).
//
// Owns a second shared D3D11 BGRA texture alongside D3D11Presenter's
// video texture. The sidecar uploads CPU-rendered libass/PGS subtitle
// bitmaps into this texture; main-app FrameCanvas imports the NT handle
// and draws it as a textured quad over the video quad (alpha-blended).
//
// Pattern mirrors D3D11Presenter: same shared-handle export
// (IDXGIResource1::CreateSharedHandle), same NT handle model, same
// UpdateSubresource upload path — keeps the import protocol unified
// so main-app's d3d11_gl_bridge can reuse existing handle-open code
// when Phase 3.4 wires the overlay into FrameCanvas::renderFrame.
//
// Scope constraint for Batch 3.2 (no regression window per Hemanth
// 2026-04-16): this class is infrastructure only. Nothing in the
// playback pipeline references it. The sidecar_tests harness exercises
// init/upload/destroy in isolation. Phase 3.3 wires video_decoder to
// upload real subtitle bitmaps; Phase 3.4 wires FrameCanvas to import
// and draw the overlay. Until then, this is dead-but-compiled code.

#ifdef _WIN32

#include <cstdint>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <windows.h>

using Microsoft::WRL::ComPtr;

class D3D11OverlayTexture {
public:
    D3D11OverlayTexture();
    ~D3D11OverlayTexture();

    // Initialize with an existing D3D11 device (video_decoder path). The
    // overlay texture is created on the same device as D3D11Presenter's
    // video texture so they can share a keyed-lock-free NT-handle model.
    bool init(ID3D11Device* device, uint32_t width, uint32_t height);

    // Standalone device creation — used by sidecar_tests when there is
    // no D3D11VA decoder device to attach to. Mirrors
    // D3D11Presenter::init_standalone.
    bool init_standalone(uint32_t width, uint32_t height);

    // Resize the overlay texture. Main-app's imported handle becomes
    // invalid on resize — Phase 3.4 will need to re-import on size
    // change. Not used from the hot path in 3.2; included so the class
    // is symmetric with D3D11Presenter.
    bool resize(uint32_t width, uint32_t height);

    // Upload a CPU BGRA buffer of matching (width x height) into the
    // overlay texture via UpdateSubresource. Returns false if the
    // presenter isn't initialized or if dims mismatch.
    //
    // Phase 3.3 will add an atlas-pack upload that accepts a
    // std::vector<SubOverlayBitmap> and memcpys into a single large
    // surface; 3.2's single-buffer path is sufficient to validate the
    // GPU resource lifecycle + NT handle export.
    bool upload_bgra(const uint8_t* bgra, uint32_t width, uint32_t height,
                     uint32_t stride);

    // Get the NT handle for the shared texture (send to main-app once).
    void* nt_handle() const { return nt_handle_; }

    uint32_t width() const  { return width_; }
    uint32_t height() const { return height_; }

    bool ready() const { return device_ != nullptr && overlay_tex_ != nullptr; }

    void destroy();

private:
    bool create_textures(uint32_t width, uint32_t height);
    void release_textures();

    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D>     overlay_tex_;  // shared texture (main-app reads)
    HANDLE                      nt_handle_ = nullptr;
    uint32_t                    width_  = 0;
    uint32_t                    height_ = 0;
};

#endif // _WIN32
