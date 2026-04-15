#pragma once
// D3D11 shared texture presenter — the "Holy Grail" path.
//
// Creates a shared D3D11 texture that can be imported by the Python/Qt
// side for zero-copy GPU presentation. The decoded frame (from D3D11VA)
// is CopyResource'd to this shared texture, and the NT handle is sent
// to Python via the JSON protocol.
//
// Pipeline: D3D11VA decode → CopyResource → shared texture → NT handle → Qt

#ifdef _WIN32

#include <cstdint>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <windows.h>

using Microsoft::WRL::ComPtr;

class D3D11Presenter {
public:
    D3D11Presenter();
    ~D3D11Presenter();

    // Initialize using the D3D11 device from FFmpeg's D3D11VA context.
    // Returns true if the shared texture was created successfully.
    bool init(ID3D11Device* device, uint32_t width, uint32_t height);

    // Initialize with a standalone D3D11 device (for SW decode — no FFmpeg HW ctx).
    bool init_standalone(uint32_t width, uint32_t height);

    // Resize the shared texture (e.g., on fullscreen toggle).
    bool resize(uint32_t width, uint32_t height);

    // Copy a D3D11 texture (decoded frame) to the shared texture.
    // Returns true if the copy succeeded (keyed mutex acquired).
    // Returns false if the consumer still holds the texture (frame skipped).
    bool present(ID3D11Texture2D* src_texture);

    // Copy a specific array slice from a D3D11VA texture array.
    // D3D11VA decodes into texture arrays — each frame is a different slice.
    bool present_slice(ID3D11Texture2D* src_texture, int array_index);

    // Upload a CPU BGRA buffer to the shared texture (SW decode path).
    // Handles resize if frame dimensions changed.
    bool present_cpu(const uint8_t* bgra_data, uint32_t width, uint32_t height, uint32_t stride);

    // Get the NT handle for the shared texture (send to Python once).
    void* nt_handle() const { return nt_handle_; }

    // Get current dimensions.
    uint32_t width()  const { return width_; }
    uint32_t height() const { return height_; }

    // Is the presenter initialized and ready?
    bool ready() const { return device_ != nullptr && external_tex_ != nullptr; }

    void destroy();

private:
    bool create_textures(uint32_t width, uint32_t height);
    void release_textures();

    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext>  context_;
    ComPtr<ID3D11Texture2D>     external_tex_;   // shared texture (consumer reads)
    HANDLE                      nt_handle_ = nullptr;
    uint32_t                    width_  = 0;
    uint32_t                    height_ = 0;
};

#endif // _WIN32
