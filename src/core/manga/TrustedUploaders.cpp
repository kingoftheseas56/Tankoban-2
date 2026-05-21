#include "core/manga/TrustedUploaders.h"

namespace tankoban::manga {

namespace {

// Canonical seed list (lowercase, no surrounding whitespace).
// Insertion order preserved for names() — keep this list short and
// curated; adding an entry here promotes a Fandom uploader to the
// "Trusted uploader" badge surface.
const QStringList& canonicalNames()
{
    static const QStringList kNames = {
        QStringLiteral("antiherogold"),
        QStringLiteral("1r0n"),
        QStringLiteral("danke-empire"),
    };
    return kNames;
}

} // namespace

bool TrustedUploaders::isTrusted(QString uploader)
{
    const QString normalised = uploader.trimmed().toLower();
    if (normalised.isEmpty()) {
        return false;
    }
    return canonicalNames().contains(normalised);
}

QStringList TrustedUploaders::names()
{
    return canonicalNames();
}

} // namespace tankoban::manga
