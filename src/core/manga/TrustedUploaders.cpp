#include "core/manga/TrustedUploaders.h"

namespace tankoban::manga {

namespace {

// Canonical tier-1 seed list from resources/manga_uploader_trust.json
// (lowercase, no surrounding whitespace).
// Insertion order preserved for names() — keep this list short and
// curated; adding an entry here promotes a Nyaa uploader to the
// "Trusted uploader" badge surface. Keep this in sync with the JSON until
// the badge path reads the shared resource directly.
const QStringList& canonicalNames()
{
    static const QStringList kNames = {
        QStringLiteral("1r0n"),
        QStringLiteral("hox"),
        QStringLiteral("viz digital"),
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
