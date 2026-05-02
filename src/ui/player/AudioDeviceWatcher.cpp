#include "AudioDeviceWatcher.h"

#include <QMetaObject>

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <atomic>

namespace {

// Minimal IMMNotificationClient subclass. We only act on
// OnDefaultDeviceChanged for the eRender + eConsole role; every other
// callback returns S_OK with no work. COM owns the object's lifetime
// via AddRef/Release; the Impl holds one ref it took on creation and
// drops it on unregister-then-Release.
class DeviceNotifyImpl : public IMMNotificationClient {
public:
    explicit DeviceNotifyImpl(AudioDeviceWatcher* owner) : m_owner(owner) {}

    // ── IUnknown ───────────────────────────────────────────────────────────
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++m_refCount;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    // ── IMMNotificationClient (no-op events we don't act on) ───────────────
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    // ── Active callback: default render device changed ─────────────────────
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR deviceId) override {
        // We only care about user-facing audio output (eRender + eConsole);
        // eMultimedia / eCommunications can fire for the same physical
        // change — gating on eConsole keeps us to one event per switch.
        if (flow != eRender || role != eConsole) return S_OK;
        if (!deviceId) return S_OK;

        // Resolve friendly name. The audio engine thread we're on already
        // has COM initialized (audio engine sets up its own apartment),
        // so CoCreateInstance + IMMDevice property reads work directly.
        // Any failure path silently emits an empty name; VideoPlayer slot
        // skips recall on empty (no key to look up).
        QString friendlyName;
        IMMDeviceEnumerator* enumerator = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                       CLSCTX_INPROC_SERVER,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&enumerator)))) {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(enumerator->GetDevice(deviceId, &device)) && device) {
                IPropertyStore* store = nullptr;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
                    PROPVARIANT name;
                    PropVariantInit(&name);
                    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name))
                        && name.vt == VT_LPWSTR && name.pwszVal) {
                        friendlyName = QString::fromWCharArray(name.pwszVal);
                    }
                    PropVariantClear(&name);
                    store->Release();
                }
                device->Release();
            }
            enumerator->Release();
        }

        // Marshal back to GUI thread via queued invoke. m_owner is cleared
        // by detachOwner() before unregister so a late-arrival callback
        // can't poke a freed QObject.
        AudioDeviceWatcher* owner = m_owner;
        if (owner) {
            QMetaObject::invokeMethod(owner,
                                       "onDeviceChangedQueued",
                                       Qt::QueuedConnection,
                                       Q_ARG(QString, friendlyName));
        }
        return S_OK;
    }

    // Owner-pointer detach. Called by Pimpl destructor BEFORE unregister
    // so any in-flight callback that races us reads nullptr and skips.
    void detachOwner() { m_owner = nullptr; }

private:
    std::atomic<ULONG> m_refCount{1};
    AudioDeviceWatcher* m_owner = nullptr;
};

} // namespace

class AudioDeviceWatcher::Impl {
public:
    explicit Impl(AudioDeviceWatcher* owner) {
        // QApplication initializes COM on the GUI thread (OleInitialize
        // via QGuiApplicationPrivate). CoCreateInstance from this thread
        // works without explicit CoInitializeEx.
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&m_enumerator)))) {
            m_enumerator = nullptr;
            return;
        }
        m_notify = new DeviceNotifyImpl(owner);
        if (FAILED(m_enumerator->RegisterEndpointNotificationCallback(m_notify))) {
            // Registration failed — release the notify object and bail.
            // The watcher remains constructed but never fires; callers
            // see no-op behavior, which is acceptable for an opt-in
            // recall feature on hardware where COM declined to register.
            m_notify->detachOwner();
            m_notify->Release();
            m_notify = nullptr;
        }
    }

    ~Impl() {
        // Order matters: unregister BEFORE Release so no fresh callback
        // fires after we've signaled ownership of the notify object back
        // to COM. detachOwner additionally guards against a callback
        // already in flight on the audio engine thread.
        if (m_enumerator && m_notify) {
            m_enumerator->UnregisterEndpointNotificationCallback(m_notify);
        }
        if (m_notify) {
            m_notify->detachOwner();
            m_notify->Release();
            m_notify = nullptr;
        }
        if (m_enumerator) {
            m_enumerator->Release();
            m_enumerator = nullptr;
        }
    }

private:
    IMMDeviceEnumerator* m_enumerator = nullptr;
    DeviceNotifyImpl*    m_notify     = nullptr;
};

#else // !_WIN32

// Non-Windows stub. Builds clean, never fires the signal — the audio-
// device-change recall feature is Windows-only by design (Task 8.B
// is anchored on Windows IMMNotificationClient).
class AudioDeviceWatcher::Impl {
public:
    explicit Impl(AudioDeviceWatcher* /*owner*/) {}
    ~Impl() = default;
};

#endif // _WIN32

AudioDeviceWatcher::AudioDeviceWatcher(QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this))
{
}

AudioDeviceWatcher::~AudioDeviceWatcher() = default;

void AudioDeviceWatcher::onDeviceChangedQueued(const QString& friendlyName)
{
    // Re-emit on the GUI thread. The QueuedConnection from the COM
    // callback already marshaled execution here.
    emit defaultDeviceChanged(friendlyName);
}
