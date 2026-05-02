// BackendFactory.cpp — Phase 7 of MPV_RENDER_API_INTEGRATION.
//
// Centralizes backend-selection logic that used to live as inline
// branching at VideoPlayer.cpp:190-199. Pure logic + QSettings; no UI
// dependencies. Compiles regardless of HAS_LIBMPV — when libmpv isn't
// available, Mpv requests gracefully fall back to SidecarProcess.

#include "BackendFactory.h"

#include "ui/player/SidecarProcess.h"
#ifdef HAS_LIBMPV
#include "ui/player/MpvBackend.h"
#include <cstdlib>
#endif

#include <QSettings>

namespace {
constexpr const char* kKey        = "player/videoBackend";
constexpr const char* kFfmpegSlug = "ffmpeg";
constexpr const char* kMpvSlug    = "mpv";
} // namespace

BackendFactory::Type BackendFactory::chooseFor(std::optional<Type> explicitOverride)
{
#ifdef HAS_LIBMPV
    // TANKOBAN_FORCE_MPV is a dev override that forces mpv even when the
    // saved preference is ffmpeg. Daily users never set this env var.
    if (const char* force = std::getenv("TANKOBAN_FORCE_MPV");
        force && force[0] == '1' && force[1] == '\0') {
        return Type::Mpv;
    }
    if (explicitOverride.has_value()) return *explicitOverride;
    return readPreference();
#else
    (void)explicitOverride;
    return Type::Ffmpeg;  // libmpv not compiled in
#endif
}

BackendFactory::Type BackendFactory::readPreference()
{
    QSettings s("Tankoban", "Tankoban");
    // MAKE_MPV_SOLO Task 11 (2026-05-02) — default-when-key-absent flipped
    // from `ffmpeg` to `mpv`. Hemanth-paced cutover: new installs (or any
    // launch where the QSettings key isn't present yet) get mpv as the
    // default backend. EXISTING users with a stored value of "ffmpeg"
    // are NOT touched — their saved preference still wins, and they
    // opt into mpv by manually flipping via the existing right-click
    // set-default mechanism. The right-click "Play with ffmpeg" /
    // "Play with mpv" per-file overrides remain unchanged on both UX
    // and code paths (no menu work in Task 11; stays for the cutover
    // validation window in Task 12).
    //
    // Fences/gates honored before this flip landed: Tasks 7-10 (mpv HUD
    // parity / Pattern C accumulator / mpv telemetry) all closed; Task
    // 10.5 (hwdec=no default for the stutter floor) closed; Task 10.7
    // Tier 0 (separable scalers for picture quality) shipped pending
    // Hemanth eyeball verdict (deferred per Hemanth "deal stutter after
    // all the tasks are done if it's still there"); Task 8.B (audio
    // device-change watcher) shipped same wake. Per the MAKE_MPV_SOLO
    // dependency clause: "Tasks 7-10 must close GREEN before firing"
    // — met.
    const QString slug = s.value(kKey, kMpvSlug).toString();
    return slug == kMpvSlug ? Type::Mpv : Type::Ffmpeg;
}

void BackendFactory::writePreference(Type t)
{
    QSettings s("Tankoban", "Tankoban");
    s.setValue(kKey, toString(t));
}

QString BackendFactory::toString(Type t)
{
    return t == Type::Mpv ? QStringLiteral("mpv") : QStringLiteral("ffmpeg");
}

IPlayerBackend* BackendFactory::create(Type t, QObject* parent)
{
#ifdef HAS_LIBMPV
    if (t == Type::Mpv) return new MpvBackend(parent);
#endif
    return new SidecarProcess(parent);
}
