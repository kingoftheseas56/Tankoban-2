#pragma once

#include <atomic>

// Thread-safe volume/mute state shared between main thread and audio thread.
class VolumeControl {
public:
    void set_volume(float v) {
        // Batch 4.2 — upper clamp extended from 1.0 to 2.0 for the amp
        // zone (up to +6 dB linear gain). audio_decoder applies a tanh
        // soft-clip when gain > 1.0 so clipping artifacts stay below the
        // perceptual threshold; the raw clamp here guards against any
        // stale/rogue caller pushing beyond 2.0.
        volume_.store(v < 0.0f ? 0.0f : (v > 2.0f ? 2.0f : v),
                      std::memory_order_relaxed);
    }

    void set_muted(bool m) {
        muted_.store(m, std::memory_order_relaxed);
    }

    float volume() const {
        return volume_.load(std::memory_order_relaxed);
    }

    bool muted() const {
        return muted_.load(std::memory_order_relaxed);
    }

    // Returns 0.0 if muted, else volume.
    float effective_volume() const {
        return muted_.load(std::memory_order_relaxed)
            ? 0.0f
            : volume_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<float> volume_{1.0f};
    std::atomic<bool>  muted_{false};
};
