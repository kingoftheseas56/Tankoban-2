#pragma once

// Sidecar-scope watcher for Windows default-output-device changes
// (BT headphones connect, USB headset plug-in, HDMI sink switch).
//
// AUDIO_HOT_DEVICE_REROUTE (2026-05-10): standard WASAPI shared-mode
// streams do NOT auto-follow default-device changes for already-open
// streams. Without this watcher, a stream opened against the laptop
// speaker stays bound to the laptop speaker even after the OS migrates
// every other app to BT headphones. The fix is application-side:
// listen via IMMNotificationClient, signal the audio thread, rebuild
// the PortAudio stream on the new WASAPI default.
//
// Cross-thread mechanism: the COM notification thread bumps a monotonic
// atomic generation counter. The audio thread (audio_decoder.cpp)
// polls the counter between Pa_WriteStream calls and rebuilds its
// stream when the value advances past its captured baseline. main.cpp
// uses the same counter to detect that the sidecar's prewarmed stream
// has gone stale across a reroute that fired while no decoder was
// running.
//
// Why a counter, not a bool flag: rapid hot-plug toggles (user fiddling
// with BT) would race a flag-edge pattern. A monotonic counter is
// idempotent — one rebuild per next poll regardless of how many COM
// notifications fired in between.
//
// The COM notification thread NEVER touches PortAudio state. Only the
// audio thread (decoder) and main.cpp's prewarm path open/close PA
// streams. Cross-thread signaling is the atomic counter only.
//
// Lifetime: init() once at sidecar startup AFTER Pa_Initialize;
// shutdown() once at sidecar exit BEFORE Pa_Terminate (so any
// in-flight notification has already been drained). On non-Windows
// or on COM failure, init() returns false and the counter stays at 0
// forever — playback works, just doesn't follow device reroute.

#include <atomic>
#include <cstdint>

namespace audio_device_watcher {

// Monotonic counter bumped by the COM notification thread on each
// eligible default-output-device change. Read by the audio thread
// (audio_decoder.cpp) and by main.cpp's prewarm-staleness check.
extern std::atomic<uint32_t> g_audio_reroute_generation;

// Initialize COM (multi-threaded apartment) and register an
// IMMNotificationClient against the system MMDeviceEnumerator.
// Returns true on success, false on COM/MMDevice failure or non-Windows
// builds. Failure is non-fatal — sidecar continues without device-
// reroute support.
bool init();

// Unregister + release in reverse order of init(). Idempotent. Safe
// to call even if init() returned false.
void shutdown();

} // namespace audio_device_watcher
