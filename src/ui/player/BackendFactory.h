#pragma once

// BackendFactory — Phase 7 of MPV_RENDER_API_INTEGRATION.
//
// Encapsulates the backend selection logic that was previously a
// TANKOBAN_FORCE_MPV env var gate at VideoPlayer.cpp:190-199. Reads/writes
// the persisted preference under QSettings key `player/videoBackend`
// ("ffmpeg" | "mpv", default "ffmpeg" per Phase 0 §Q2).
//
// Per-invocation explicit override (added 2026-04-30): VideosPage and
// ShowView right-click menus emit "Play with ffmpeg" / "Play with mpv" as
// direct openers carrying an explicit backend choice for THIS playback.
// chooseFor honors that override above the saved preference but below
// TANKOBAN_FORCE_MPV. The saved preference is not mutated by these menu
// actions (one-shot semantics).
//
// MAKE_MPV_SOLO Task 2 (2026-05-01): the §Q4 stream-mode lock that
// previously forced ffmpeg on streams was removed. Streams now honor the
// saved preference and explicit override the same way library files do.

#include <QString>
#include <optional>

class IPlayerBackend;
class QObject;

class BackendFactory {
public:
    enum class Type { Ffmpeg, Mpv };

    // Returns the backend type to use for a given playback context.
    // Precedence (highest wins):
    //   1. TANKOBAN_FORCE_MPV=1    → Mpv   (dev override)
    //   2. explicitOverride.value()→ as specified (per-click "Play with X")
    //   3. QSettings player/videoBackend (default Ffmpeg per §Q2)
    //   4. !HAS_LIBMPV             → Ffmpeg (mpv backend not compiled in)
    static Type chooseFor(std::optional<Type> explicitOverride = std::nullopt);

    // Persisted preference accessors (used by VideosPage right-click handlers).
    static Type readPreference();
    static void writePreference(Type t);

    // String <-> Type round-trip for QSettings storage.
    static QString toString(Type t);   // "ffmpeg" | "mpv"

    // Factory: instantiate the right backend. Caller owns the returned
    // IPlayerBackend* (parented to `parent`). When HAS_LIBMPV is unset,
    // Mpv requests gracefully fall back to SidecarProcess.
    static IPlayerBackend* create(Type t, QObject* parent);
};
