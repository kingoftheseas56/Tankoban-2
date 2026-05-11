#include "audio_device_watcher.h"

#include <cwchar>
#include <cstdio>
#include <string>

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
#include <pa_win_wasapi.h>

namespace audio_device_watcher {
namespace {

static void set_reason(std::string* out, const std::string& text) {
    if (out) *out = text;
}

static std::string wide_to_utf8(LPCWSTR text) {
    if (!text) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    int written = WideCharToMultiByte(CP_UTF8, 0, text, -1,
                                      out.data(), needed, nullptr, nullptr);
    if (written <= 0) return {};
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

class ScopedComInit {
public:
    ScopedComInit() {
        hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        // S_FALSE still increments this thread's COM init count; balance it.
        uninit_ = (hr_ == S_OK || hr_ == S_FALSE);
    }
    ~ScopedComInit() {
        if (uninit_) CoUninitialize();
    }
    bool ok() const {
        // RPC_E_CHANGED_MODE means this thread already has a different COM
        // apartment. MMDevice calls still work from that initialized apartment;
        // we just must not call CoUninitialize for someone else's init.
        return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE;
    }
    HRESULT hr() const { return hr_; }

private:
    HRESULT hr_ = E_FAIL;
    bool uninit_ = false;
};

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

bool resolve_current_wasapi_default_device_index(PaDeviceIndex* out_device,
                                                 std::string* out_device_name,
                                                 std::string* out_endpoint_id,
                                                 std::string* out_reason) {
    if (out_device) *out_device = paNoDevice;
    if (out_device_name) out_device_name->clear();
    if (out_endpoint_id) out_endpoint_id->clear();

    ScopedComInit com;
    if (!com.ok()) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "CoInitializeEx failed hr=0x%08lx",
                      static_cast<unsigned long>(com.hr()));
        set_reason(out_reason, buf);
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "CoCreateInstance(MMDeviceEnumerator) failed hr=0x%08lx",
                      static_cast<unsigned long>(hr));
        set_reason(out_reason, buf);
        return false;
    }

    IMMDevice* live_default = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &live_default);
    enumerator->Release();
    if (FAILED(hr) || !live_default) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "GetDefaultAudioEndpoint(eRender,eMultimedia) failed hr=0x%08lx",
                      static_cast<unsigned long>(hr));
        set_reason(out_reason, buf);
        return false;
    }

    LPWSTR live_id_raw = nullptr;
    hr = live_default->GetId(&live_id_raw);
    live_default->Release();
    if (FAILED(hr) || !live_id_raw) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "IMMDevice::GetId failed hr=0x%08lx",
                      static_cast<unsigned long>(hr));
        set_reason(out_reason, buf);
        return false;
    }

    const std::wstring live_id(live_id_raw);
    const std::string live_id_utf8 = wide_to_utf8(live_id_raw);
    CoTaskMemFree(live_id_raw);
    if (out_endpoint_id) *out_endpoint_id = live_id_utf8;

    const int count = Pa_GetDeviceCount();
    if (count < 0) {
        set_reason(out_reason, std::string("Pa_GetDeviceCount failed: ") +
                                  Pa_GetErrorText(count));
        return false;
    }

    int wasapi_output_candidates = 0;
    int immdevice_query_failures = 0;
    for (PaDeviceIndex i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels <= 0) continue;
        const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
        if (!api || api->type != paWASAPI) continue;
        ++wasapi_output_candidates;

        void* raw_imm = nullptr;
        const PaError pa_err = PaWasapi_GetIMMDevice(i, &raw_imm);
        if (pa_err != paNoError || !raw_imm) {
            ++immdevice_query_failures;
            continue;
        }

        IMMDevice* pa_device = static_cast<IMMDevice*>(raw_imm);
        LPWSTR pa_id_raw = nullptr;
        hr = pa_device->GetId(&pa_id_raw);
        pa_device->Release();
        if (SUCCEEDED(hr) && pa_id_raw) {
            const bool match = (std::wcscmp(pa_id_raw, live_id.c_str()) == 0);
            CoTaskMemFree(pa_id_raw);
            if (match) {
                if (out_device) *out_device = i;
                if (out_device_name) {
                    *out_device_name = (info->name ? info->name : "");
                }
                set_reason(out_reason, "matched PortAudio WASAPI IMMDevice endpoint ID");
                return true;
            }
        } else if (pa_id_raw) {
            CoTaskMemFree(pa_id_raw);
        }
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "no PortAudio WASAPI device matched live endpoint id '%s' (candidates=%d immdevice_failures=%d)",
                  live_id_utf8.c_str(), wasapi_output_candidates, immdevice_query_failures);
    set_reason(out_reason, buf);
    return false;
}

} // namespace audio_device_watcher

#else  // !_WIN32

namespace audio_device_watcher {
bool init() { return false; }
void shutdown() {}
bool resolve_current_wasapi_default_device_index(PaDeviceIndex* out_device,
                                                 std::string* out_device_name,
                                                 std::string* out_endpoint_id,
                                                 std::string* out_reason) {
    if (out_device) *out_device = paNoDevice;
    if (out_device_name) out_device_name->clear();
    if (out_endpoint_id) out_endpoint_id->clear();
    if (out_reason) *out_reason = "WASAPI default resolver unavailable on non-Windows build";
    return false;
}
} // namespace audio_device_watcher

#endif
