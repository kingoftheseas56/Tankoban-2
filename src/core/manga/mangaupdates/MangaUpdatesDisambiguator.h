#pragma once

#include "MangaUpdatesTypes.h"
#include "core/manga/anilist/AniListTypes.h"

#include <QList>
#include <QStringList>

namespace tankoban::manga::mangaupdates {

class MangaUpdatesDisambiguator
{
public:
    static qint64 bestMatch(
        const QList<MangaUpdatesSearchHit>& hits,
        const tankoban::manga::anilist::MediaPreview& anilistPreview,
        const QStringList& anilistAuthors);
};

} // namespace tankoban::manga::mangaupdates
