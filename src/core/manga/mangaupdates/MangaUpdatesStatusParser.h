#pragma once

#include <QString>

namespace tankoban::manga::mangaupdates {

class MangaUpdatesStatusParser
{
public:
    static int parseLeadingVolumeCount(const QString& status);
};

} // namespace tankoban::manga::mangaupdates
