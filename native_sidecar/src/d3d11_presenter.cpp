#ifdef _WIN32

#include "d3d11_presenter.h"
#include <cstdint>
#include <cstdio>

D3D11Presenter::D3D11Presenter() = default;

D3D11Presenter::~D3D11Presenter() {
    destroy();
}

bool D3D11Presenter::init(ID3D11Device* device, uint32_t width, uint32_t height) {
    if (!device || width == 0 || height == 0) return false;

    device_ = device;
    device_->GetImmediateContext(&context_);
    if (!context_) {
        std::fprintf(stderr, "D3D11Presenter: failed to get device context\n");
        return false;
    }

    if (!create_textures(width, height)) {
        return false;
    }

    std::fprintf(stderr, "D3D11Presenter: initialized %ux%u nt_handle=%p\n",
                 width, height, nt_handle_);
    return true;
}

bool D3D11Presenter::init_standalone(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL out_level;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, 1, D3D11_SDK_VERSION,
        &device_, &out_level, &context_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11Presenter: standalone device creation failed hr=0x%lx\n", hr);
        return false;
    }

    if (!create_textures(width, height)) {
        context_.Reset();
        device_.Reset();
        return false;
    }

    std::fprintf(stderr, "D3D11Presenter: standalone initialized %ux%u nt_handle=%p\n",
                 width, height, nt_handle_);
    return true;
}

bool D3D11Presenter::resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) return true;
    if (width < 16 || height < 16) return false;

    release_textures();
    if (!create_textures(width, height)) {
        return false;
    }

    std::fprintf(stderr, "D3D11Presenter: resized to %ux%u nt_handle=%p\n",
                 width, height, nt_handle_);
    return true;
}

bool D3D11Presenter::present(ID3D11Texture2D* src_texture) {
    if (!context_ || !external_tex_ || !src_texture) return false;

    context_->CopyResource(external_tex_.Get(), src_texture);
    context_->Flush();
    return true;
}

bool D3D11Presenter::present_slice(ID3D11Texture2D* src_texture, int array_index) {
    if (!context_ || !external_tex_ || !src_texture) return false;

    // Copy single slice from D3D11VA texture array to our shared texture.
    // No keyed mutex — sync is via Flush + wglDXLock/Unlock on the consumer.
    context_->CopySubresourceRegion(
        external_tex_.Get(),
        0, 0, 0, 0,
        src_texture,
        D3D11CalcSubresource(0, array_index, 1),
        nullptr);

    context_->Flush();
    return true;
}

bool D3D11Presenter::present_cpu(const uint8_t* bgra_data, uint32_t width, uint32_t height, uint32_t stride) {
    if (!context_ || !external_tex_ || !bgra_data) return false;

    // Resize shared texture if frame dimensions changed
    if (width != width_ || height != height_) {
        if (!resize(width, height)) return false;
    }

    context_->UpdateSubresource(
        external_tex_.Get(), 0, nullptr,
        bgra_data, stride, 0);
    context_->Flush();
    return true;
}

void D3D11Presenter::destroy() {
    release_textures();
    context_.Reset();
    device_.Reset();
}

bool D3D11Presenter::create_textures(uint32_t width, uint32_t height) {
    // Create the shared external texture.
    // Using SHARED_NTHANDLE only (no SHARED_KEYEDMUTEX) because
    // wglDXRegisterObjectNV doesn't support keyed mutex textures.
    // Synchronization is handled by D3D11 Flush + wglDXLock/Unlock.
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

    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &external_tex_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11Presenter: CreateTexture2D failed hr=0x%lx\n", hr);
        return false;
    }

    // Export NT handle
    ComPtr<IDXGIResource1> dxgi_res;
    hr = external_tex_.As(&dxgi_res);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11Presenter: QueryInterface IDXGIResource1 failed hr=0x%lx\n", hr);
        external_tex_.Reset();
        return false;
    }

    hr = dxgi_res->CreateSharedHandle(
        nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr,
        &nt_handle_);
    if (FAILED(hr)) {
        std::fprintf(stderr, "D3D11Presenter: CreateSharedHandle failed hr=0x%lx\n", hr);
        external_tex_.Reset();
        return false;
    }

    width_  = width;
    height_ = height;
    return true;
}

void D3D11Presenter::release_textures() {
    if (nt_handle_) {
        CloseHandle(nt_handle_);
        nt_handle_ = nullptr;
    }
    external_tex_.Reset();
    width_  = 0;
    height_ = 0;
}

#endif // _WIN32
