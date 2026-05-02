#pragma once

#include <QObject>
#include <QString>

#include <memory>

// MAKE_MPV_SOLO Task 8.B (2026-05-02) — Windows audio device-change
// watcher. Closes the deferred deliverable from Task 8 ("per-device
// audio-delay recall (#3) DEFERRED to Task 8.B (Windows
// IMMNotificationClient watcher, ~100 LOC)").
//
// Listens for IMMNotificationClient::OnDefaultDeviceChanged callbacks
// via the WASAPI device enumerator and emits a Qt signal back on the
// GUI thread. VideoPlayer connects to a slot that recalls the saved
// per-device audio-delay (mirroring the file-open recall logic at
// VideoPlayer.cpp:3970).
//
// Lifecycle: construction registers the COM callback. Destruction
// unregisters it BEFORE freeing the COM object so no callback fires
// into a deleted Impl.
//
// Threading: IMMNotificationClient callbacks fire on Windows audio
// engine threads. The Impl marshals via QMetaObject::invokeMethod with
// Qt::QueuedConnection so the public defaultDeviceChanged signal is
// always delivered on the thread the QObject was created on (typically
// the GUI thread).
//
// Cross-platform: on non-Windows builds the Impl is a no-op stub that
// never fires the signal. The class compiles cleanly so VideoPlayer
// doesn't need #ifdef guards at every call site.
class AudioDeviceWatcher : public QObject
{
    Q_OBJECT

public:
    explicit AudioDeviceWatcher(QObject* parent = nullptr);
    ~AudioDeviceWatcher() override;

signals:
    // Emitted when Windows default audio render device changes (eRender
    // + eConsole role). friendlyName is the user-readable device name —
    // matches the shape the sidecar's mediaInfo reports for
    // makeDeviceKey() input parity. Empty string if the system
    // couldn't resolve a name (rare; usually disconnected device).
    void defaultDeviceChanged(const QString& friendlyName);

private slots:
    // Internal queued slot — Impl posts here from the COM thread; this
    // method runs on the GUI thread and re-emits the public signal.
    // Q_INVOKABLE-style by virtue of being a slot.
    void onDeviceChangedQueued(const QString& friendlyName);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
