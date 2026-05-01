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
    const QString slug = s.value(kKey, kFfmpegSlug).toString();
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
