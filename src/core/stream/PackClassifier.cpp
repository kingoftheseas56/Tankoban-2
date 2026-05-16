#include "core/stream/PackClassifier.h"

#include <QRegularExpression>
#include <QSet>

namespace tankoban::stream::theatre {

namespace {

// "Complete Series" / "Complete Box Set" / "Complete Collection" - the
// canonical multi-season marker. Case-insensitive, whitespace/dot/dash
// flexible separators.
static const QRegularExpression kReCompleteSeries(
    "(?i)\\b(complete[\\s._-]*series|complete[\\s._-]*box[\\s._-]*set|complete[\\s._-]*collection)\\b");

// Season range like S01-S05 or S01 S05 - captures both bounds.
static const QRegularExpression kReSeasonRange(
    "(?i)\\bS(\\d{1,2})[\\s._-]*[-\\s][\\s._-]*S(\\d{1,2})\\b");

// Single season tag SxxNN - used both for single-season classification AND
// for collecting all season tokens in a multi-season title (e.g.
// "S01.S02.S03.S04" - 4 hits).
static const QRegularExpression kReSingleSeason("(?i)\\bS(\\d{1,2})\\b");

// Single episode tag - SxxEyy with no range. Captures season + episode.
static const QRegularExpression kReSingleEpisode(
    "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})\\b");

// Episode-range / multi-episode tag - SxxEyy-Ezz or SxxEyy.Ezz or
// Season.NN.eAA.eBB (per Daredevil sample observed in the smoke).
static const QRegularExpression kReEpisodeRange(
    "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})[\\s._-]*[-\\s.][\\s._-]*E(\\d{1,3})\\b");

// Lowercase "season NN e AA . e BB" - dotted lowercase episode list under a
// Season.NN prefix (matches "Season.01.e01.e02").
static const QRegularExpression kReLowerCaseDottedEpisodes(
    "(?i)\\bseason[\\s._-]*0?(\\d{1,2})[\\s._-]*e(\\d{1,3})[\\s._-]*\\.[\\s._-]*e(\\d{1,3})\\b");

// Lowercase "season NN" prefix that doesn't carry the SxxNN tag (e.g.
// "Daredevil Born Again.2025.Season.01.e03").
static const QRegularExpression kReLowerSeason(
    "(?i)\\bseason[\\s._-]*0?(\\d{1,2})\\b");

// Lowercase eXX after Season.NN (matches "Season.01.e03").
static const QRegularExpression kReLowerEpisode(
    "(?i)\\bseason[\\s._-]*0?(\\d{1,2})[\\s._-]*e(\\d{1,3})\\b");

}  // namespace

PackClassification classify(const QString& title) {
    PackClassification out;

    if (title.isEmpty())
        return out;

    // 1. Complete Series literal - highest-priority marker; short-circuit.
    if (kReCompleteSeries.match(title).hasMatch()) {
        out.type = PackType::CompleteSeries;
        out.isCompleteSeries = true;
        return out;
    }

    // 2. Season range Sxx-Syy - MultiSeason; populate detectedSeasons with
    // every season in the inclusive range.
    if (auto m = kReSeasonRange.match(title); m.hasMatch()) {
        const int lo = m.captured(1).toInt();
        const int hi = m.captured(2).toInt();
        if (lo > 0 && hi > 0 && hi >= lo) {
            for (int s = lo; s <= hi; ++s)
                out.detectedSeasons.insert(s);
            out.type = PackType::MultiSeason;
            return out;
        }
    }

    // 3. Collect all season tags. Used by:
    //    - MultiSeason fallback (>=2 distinct seasons via dotted enumeration)
    //    - SeasonPack / SingleEpisode classification (==1 season tag)
    auto seasonIt = kReSingleSeason.globalMatch(title);
    while (seasonIt.hasNext())
        out.detectedSeasons.insert(seasonIt.next().captured(1).toInt());

    // Also fold in lowercase "season NN" prefix patterns that don't carry
    // the SxxNN tag (e.g. "Daredevil Born Again.2025.Season.01.e03").
    auto lowerSeasonIt = kReLowerSeason.globalMatch(title);
    while (lowerSeasonIt.hasNext())
        out.detectedSeasons.insert(lowerSeasonIt.next().captured(1).toInt());

    if (out.detectedSeasons.size() >= 2) {
        out.type = PackType::MultiSeason;
        return out;
    }

    // 4. Episode-range patterns - MultiEpisode.
    if (auto m = kReEpisodeRange.match(title); m.hasMatch()) {
        const int lo = m.captured(2).toInt();
        const int hi = m.captured(3).toInt();
        if (lo > 0 && hi > 0 && hi >= lo)
            out.detectedEpisodeCount = hi - lo + 1;
        // Backfill season from the SxxEyy capture. The \b regex in
        // kReSingleSeason won't match S01 inside S01E01 because the E is a
        // word character; recover it from the episode-range capture.
        if (out.detectedSeasons.isEmpty())
            out.detectedSeasons.insert(m.captured(1).toInt());
        out.type = PackType::MultiEpisode;
        return out;
    }
    if (auto m = kReLowerCaseDottedEpisodes.match(title); m.hasMatch()) {
        if (out.detectedSeasons.isEmpty())
            out.detectedSeasons.insert(m.captured(1).toInt());
        out.type = PackType::MultiEpisode;
        return out;
    }

    // 5. SxxExx single-episode tag - SingleEpisode.
    if (auto m = kReSingleEpisode.match(title); m.hasMatch()) {
        out.type = PackType::SingleEpisode;
        // Backfill seasons set if globalMatch missed.
        if (out.detectedSeasons.isEmpty())
            out.detectedSeasons.insert(m.captured(1).toInt());
        return out;
    }

    if (kReLowerEpisode.match(title).hasMatch()) {
        out.type = PackType::SingleEpisode;
        return out;
    }

    // 6. Season tag with no episode - SeasonPack. The "complete"/"full"
    // keyword is a strong signal but not strictly required (a bare "S02"
    // pack is almost always a season pack in practice).
    if (out.detectedSeasons.size() == 1) {
        out.type = PackType::SeasonPack;
        return out;
    }

    // 7. No signal - Unknown.
    out.type = PackType::Unknown;
    return out;
}

QString labelForType(PackType type) {
    switch (type) {
    case PackType::SingleEpisode:  return QStringLiteral("Single Episode");
    case PackType::MultiEpisode:   return QStringLiteral("Multi-Episode");
    case PackType::SeasonPack:     return QStringLiteral("Season Pack");
    case PackType::MultiSeason:    return QStringLiteral("Multi-Season");
    case PackType::CompleteSeries: return QStringLiteral("Complete Series");
    case PackType::Unknown:        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

}  // namespace tankoban::stream::theatre
