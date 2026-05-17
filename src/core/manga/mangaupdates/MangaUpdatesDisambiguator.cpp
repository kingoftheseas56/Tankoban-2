#include "MangaUpdatesDisambiguator.h"

#include <QSet>
#include <algorithm>
#include <cstdlib>

namespace tankoban::manga::mangaupdates {

namespace {

QSet<QString> surnameTokens(const QStringList& names)
{
    QSet<QString> out;
    for (const QString& name : names) {
        const QString s = name.trimmed();
        if (s.isEmpty()) continue;

        QString surname;
        const int comma = s.indexOf(QLatin1Char(','));
        if (comma >= 0) {
            surname = s.left(comma).trimmed();
        } else {
            const auto parts = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (!parts.isEmpty()) surname = parts.last();
        }
        if (!surname.isEmpty()) out.insert(surname.toLower());
    }
    return out;
}

bool titleMatches(const MangaUpdatesSearchHit& hit, const QString& target)
{
    if (hit.title.trimmed().toLower() == target) return true;
    for (const QString& alt : hit.altTitles) {
        if (alt.trimmed().toLower() == target) return true;
    }
    return false;
}

} // namespace

qint64 MangaUpdatesDisambiguator::bestMatch(
    const QList<MangaUpdatesSearchHit>& hits,
    const tankoban::manga::anilist::MediaPreview& anilistPreview,
    const QStringList& anilistAuthors)
{
    if (hits.isEmpty()) return 0;

    const QString targetTitle = anilistPreview.title.trimmed().toLower();
    QList<MangaUpdatesSearchHit> titleMatchesList;
    for (const auto& hit : hits) {
        if (titleMatches(hit, targetTitle)) titleMatchesList.append(hit);
    }
    if (titleMatchesList.isEmpty()) return 0;
    if (titleMatchesList.size() == 1) return titleMatchesList.first().seriesId;

    const QSet<QString> anilistSurnames = surnameTokens(anilistAuthors);
    QList<MangaUpdatesSearchHit> authorMatches;
    if (!anilistSurnames.isEmpty()) {
        for (const auto& hit : titleMatchesList) {
            const QSet<QString> hitSurnames = surnameTokens(hit.authors);
            if (!(anilistSurnames & hitSurnames).isEmpty()) authorMatches.append(hit);
        }
    }

    QList<MangaUpdatesSearchHit> candidates =
        authorMatches.isEmpty() ? titleMatchesList : authorMatches;
    if (candidates.size() == 1) return candidates.first().seriesId;

    if (anilistPreview.yearStarted > 0) {
        QList<MangaUpdatesSearchHit> yearMatches;
        for (const auto& hit : candidates) {
            if (hit.yearStarted > 0 &&
                std::abs(hit.yearStarted - anilistPreview.yearStarted) <= 1) {
                yearMatches.append(hit);
            }
        }
        if (!yearMatches.isEmpty()) candidates = yearMatches;
        if (candidates.size() == 1) return candidates.first().seriesId;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
        [](const MangaUpdatesSearchHit& a, const MangaUpdatesSearchHit& b) {
            return a.seriesId < b.seriesId;
        });
    return candidates.first().seriesId;
}

} // namespace tankoban::manga::mangaupdates
