#include "audio_device_watcher.h"

#include <cstdio>

namespace audio_device_watcher {
// Defined outside _WIN32 guard so non-Windows builds still link a single
// definition for the extern in the header (counter stays at 0 forever
// when init() is the no-op stub).
std::atomic<uint32_t> g_audio_reroute_generation{0};
} // namespace audio_device_watcher

#ifdef _WIN32

// IMMNotificationClient pattern reference:
// https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-events

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>

namespace audio_device_watcher {
namespace {

class DefaultDeviceListener : public IMMNotificationClient {
public:
    // IUnknown — sidecar holds one static instance for process lifetime,
    // so refcount semantics are mostly cosmetic; we never delete via
    // Release. Returning sane values keeps Windows happy if it ever
    // QI's us into a chain.
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) ||
            iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    // Only OnDefaultDeviceChanged is load-bearing for this fix.
    // eCommunications is the voice-call default (Teams/Zoom selector);
    // ignoring it avoids spurious reroutes when the user picks a
    // separate voice device while media plays through the system
    // default. eConsole is the legacy default; eMultimedia is the
    // modern media default. Both should reroute media playback.
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,
                                                    ERole role,
                                                    LPCWSTR new_id) override {
        if (flow != eRender) return S_OK;
        if (role != eConsole && role != eMultimedia) return S_OK;

        uint32_t new_gen = g_audio_reroute_generation.fetch_add(
            1, std::memory_order_acq_rel) + 1;

        // Best-effort UTF-8 conversion of the new device id for
        // diagnostic logging. AudioDecoder names the resolved device
        // explicitly via Pa_GetDeviceInfo on the rebuild side.
        char id_utf8[256] = {0};
        if (new_id) {
            int n = WideCharToMultiByte(CP_UTF8, 0, new_id, -1,
                                        id_utf8, sizeof(id_utf8) - 1,
                                        nullptr, nullptr);
            if (n <= 0) id_utf8[0] = 0;
        }
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_default_device_changed gen=%u role=%d id='%s'\n",
                     new_gen, static_cast<int>(role), id_utf8);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<ULONG> ref_{1};
};

DefaultDeviceListener g_listener;
IMMDeviceEnumerator*  g_enumerator = nullptr;
// CoInitializeEx returns S_OK only when WE initialized COM on this thread.
// S_FALSE / RPC_E_CHANGED_MODE means someone else owns the apartment;
// in that case we must NOT call CoUninitialize at shutdown (would unbalance
// the other initializer).
bool                  g_com_owned  = false;
bool                  g_registered = false;

} // anon namespace

bool init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_OK) {
        g_com_owned = true;
    } else if (hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
        g_com_owned = false;
    } else {
        std::fprintf(stderr,
                     "audio_device_watcher: CoInitializeEx failed hr=0x%08lx\n",
                     static_cast<unsigned long>(hr));
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&g_enumerator));
    if (FAILED(hr) || !g_enumerator) {
        std::fprintf(stderr,
                     "audio_device_watcher: CoCreateInstance(MMDeviceEnumerator) failed hr=0x%08lx\n",
                     static_cast<unsigned long>(hr));
        if (g_com_owned) { CoUninitialize(); g_com_owned = false; }
        return false;
    }

    hr = g_enumerator->RegisterEndpointNotificationCallback(&g_listener);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "audio_device_watcher: RegisterEndpointNotificationCallback failed hr=0x%08lx\n",
                     static_cast<unsigned long>(hr));
        g_enumerator->Release();
        g_enumerator = nullptr;
        if (g_com_owned) { CoUninitialize(); g_com_owned = false; }
        return false;
    }
    g_registered = true;
    std::fprintf(stderr,
                 "audio_device_watcher: registered (sidecar follows Windows default-output-device changes)\n");
    return true;
}

void shutdown() {
    if (g_enumerator) {
        if (g_registered) {
            g_enumerator->UnregisterEndpointNotificationCallback(&g_listener);
            g_registered = false;
        }
        g_enumerator->Release();
        g_enumerator = nullptr;
    }
    if (g_com_owned) {
        CoUninitialize();
        g_com_owned = false;
    }
}

} // namespace audio_device_watcher

#else  // !_WIN32

namespace audio_device_watcher {
bool init() { return false; }
void shutdown() {}
} // namespace audio_device_watcher

#endif
