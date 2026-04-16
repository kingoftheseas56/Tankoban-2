#ifdef _WIN32

#include "overlay_renderer.h"
#include <cstdint>
#include <cstdio>

D3D11OverlayTexture::D3D11OverlayTexture() = default;

D3D11OverlayTexture::~D3D11OverlayTexture() {
    destroy();
}

bool D3D11OverlayTexture::init(ID3D11Device* device, uint32_t width, uint32_t height) {
    if (!device || width == 0 || height == 0) return false;

    device_ = device;
    device_->GetImmediateContext(&context_);
    if (!context_) {
        std::fprintf(stderr, "D3D11OverlayTexture: failed to get device context\n");
        return false;
    }

    if (!create_textures(width, height)) {
        return false;
    }

    std::fprintf(stderr, "D3D11OverlayTexture: initialized %ux%u nt_handle=%p\n",
                 width, height, nt_handle_);
    return true;
}

bool D3D11OverlayTexture::init_standalone(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL out_level;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, 1, D3D11_SDK_VERSION,
        &device_, &out_level, &context_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11OverlayTexture: standalone device creation failed hr=0x%lx\n", hr);
        return false;
    }

    if (!create_textures(width, height)) {
        context_.Reset();
        device_.Reset();
        return false;
    }

    std::fprintf(stderr, "D3D11OverlayTexture: standalone initialized %ux%u nt_handle=%p\n",
                 width, height, nt_handle_);
    return true;
}

bool D3D11OverlayTexture::resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) return true;
    if (width < 16 || height < 16) return false;

    release_textures();
    if (!create_textures(width, height)) {
        return false;
    }

    std::fprintf(stderr, "D3D11OverlayTexture: resized to %ux%u nt_handle=%p\n",
                 width, height, nt_handle_);
    return true;
}

bool D3D11OverlayTexture::upload_bgra(const uint8_t* bgra, uint32_t width,
                                      uint32_t height, uint32_t stride) {
    if (!context_ || !overlay_tex_ || !bgra) return false;

    // Batch 3.2 requires exact dimension match — the atlas-pack upload
    // path (Batch 3.3) will handle per-frame resize if the accumulated
    // subtitle bitmap bounds exceed the current overlay size. For now
    // a mismatch is an API-misuse error.
    if (width != width_ || height != height_) {
        std::fprintf(stderr,
            "D3D11OverlayTexture: upload dim mismatch (got %ux%u, have %ux%u)\n",
            width, height, width_, height_);
        return false;
    }

    // UpdateSubresource is the standard CPU→GPU upload path for DEFAULT
    // usage shared textures. Map/Unmap isn't available on textures with
    // SHARED_NTHANDLE + DEFAULT — Map needs USAGE_DYNAMIC which is
    // incompatible with the shared-handle model. D3D11Presenter::present_cpu
    // uses the same UpdateSubresource pattern, so we inherit its
    // performance characteristics.
    context_->UpdateSubresource(
        overlay_tex_.Get(), 0, nullptr,
        bgra, stride, 0);
    context_->Flush();
    return true;
}

void D3D11OverlayTexture::destroy() {
    release_textures();
    context_.Reset();
    device_.Reset();
}

bool D3D11OverlayTexture::create_textures(uint32_t width, uint32_t height) {
    // Mirror D3D11Presenter::create_textures exactly — same flags, same
    // format — so the main-app side can reuse the existing
    // OpenSharedResource1 + d3d11_gl_bridge path unchanged when Phase 3.4
    // imports the overlay.
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags        = D3D11_RESOURCE_MISC_SHARED_NTHANDLE
                          | D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &overlay_tex_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11OverlayTexture: CreateTexture2D failed hr=0x%lx\n", hr);
        return false;
    }

    ComPtr<IDXGIResource1> dxgi_res;
    hr = overlay_tex_.As(&dxgi_res);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11OverlayTexture: QueryInterface IDXGIResource1 failed hr=0x%lx\n", hr);
        overlay_tex_.Reset();
        return false;
    }

    hr = dxgi_res->CreateSharedHandle(
        nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr,
        &nt_handle_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11OverlayTexture: CreateSharedHandle failed hr=0x%lx\n", hr);
        overlay_tex_.Reset();
        return false;
    }

    width_  = width;
    height_ = height;
    return true;
}

void D3D11OverlayTexture::release_textures() {
    if (nt_handle_) {
        CloseHandle(nt_handle_);
        nt_handle_ = nullptr;
    }
    overlay_tex_.Reset();
    width_  = 0;
    height_ = 0;
}

#endif // _WIN32
