#include "core/stream/AutoSourcePicker.h"
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include "core/stream/QualityScorer.h"

namespace tankostream::stream {

bool AutoSourcePicker::isCamRip(const QString& title) {
    // Camcorder-class tags, bounded so ordinary words don't match.
    // "TS"/"TC" intentionally NOT matched (false positives like "GUTS");
    // can be added with care later (spec section 8.2).
    static const QRegularExpression re(
        QStringLiteral("\\b(cam|camrip|hdcam|telesync|telecine|hdts)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(title).hasMatch();
}

bool AutoSourcePicker::isBatchRelease(const QString& title) {
    // Tilde ranges ("1089~1104", "E1123~E1133") — near-exclusively anime batches.
    static const QRegularExpression tildeRange(QStringLiteral("\\d\\s*~\\s*[eE]?\\d"));
    // Dash ranges ("001-574", "(0996-1069)"). Guarded so codec / bit-depth tokens
    // like "x265-10bit" don't read as a range: no alphanumeric immediately before
    // the first number, no letter immediately after the second.
    static const QRegularExpression dashRange(
        QStringLiteral("(?<![A-Za-z0-9])\\d{2,4}\\s*-\\s*\\d{2,4}(?![A-Za-z])"));
    // Season ranges ("S01-S05").
    static const QRegularExpression seasonRange(
        QStringLiteral("\\b[sS]\\d{1,2}\\s*-\\s*[sS]?\\d{1,2}\\b"));
    // Explicit completeness markers (NOT bare "batch" — Amatsu tags everything).
    static const QRegularExpression completeRe(
        QStringLiteral("\\b(complete\\s*series|complete|season\\s*pack|all\\s*episodes)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return tildeRange.match(title).hasMatch()
        || dashRange.match(title).hasMatch()
        || seasonRange.match(title).hasMatch()
        || completeRe.match(title).hasMatch();
}

bool AutoSourcePicker::hasDualAudio(const QString& title) {
    // "dual"/"dual audio"/"multi-audio" — but never "multisub"/"multiple
    // subtitle" (subtitles, not audio).
    static const QRegularExpression re(
        QStringLiteral("\\bdual\\b|\\bdual[\\s._-]?audio\\b|\\bmulti[\\s._-]?audio\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(title).hasMatch();
}

double AutoSourcePicker::impliedBitrateMbps(qint64 sizeBytes, int runtimeMinutes) {
    if (sizeBytes <= 0 || runtimeMinutes <= 0) return 0.0;
    return (static_cast<double>(sizeBytes) * 8.0)
         / (static_cast<double>(runtimeMinutes) * 60.0) / 1.0e6;
}

bool AutoSourcePicker::titleMatchesShow(const QString& candidateTitle,
                                        const QString& showTitle) {
    auto norm = [](const QString& s) {
        static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
        QString t = s.toLower();
        t.replace(nonAlnum, QStringLiteral(" "));
        return t.simplified();
    };
    const QString show = norm(showTitle);
    if (show.isEmpty()) return true;  // nothing to gate on

    static const QSet<QString> kStop = {
        QStringLiteral("the"),    QStringLiteral("a"),      QStringLiteral("an"),
        QStringLiteral("of"),     QStringLiteral("and"),    QStringLiteral("to"),
        QStringLiteral("in"),     QStringLiteral("season"), QStringLiteral("series")};

    const QStringList candWords =
        norm(candidateTitle).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QSet<QString> candSet(candWords.begin(), candWords.end());

    int significant = 0;
    int matched = 0;
    for (const QString& w : show.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (w.size() < 2 || kStop.contains(w)) continue;  // skip noise tokens
        ++significant;
        if (candSet.contains(w)) ++matched;
    }
    if (significant == 0) return true;     // show title was all stopwords/short
    return matched == significant;          // every significant show token present
}

std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidates,
                                          int runtimeMinutes) {
    return pick(candidates, QString(), runtimeMinutes);  // no show gate
}

std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidates,
                                          const QString& showTitle,
                                          int runtimeMinutes) {
    // Step 1 - hard filters.
    QList<int> survivors;
    for (int i = 0; i < candidates.size(); ++i) {
        const SourceCandidate& cand = candidates.at(i);
        if (cand.qualitySort != kRequiredQualitySort) continue;  // 1080p only
        if (cand.seeders <= 0) continue;                          // dead torrent
        if (isCamRip(cand.title)) continue;                       // camcorder rip
        // Show-identity gate: never pick a release that isn't the requested
        // show, no matter how well-seeded (the One Piece -> Community bug).
        // DOWNLOAD BUG 2026-06-03 — gate on matchText (full raw identity blob)
        // when present; some addons put a stats badge in `title`, which lacks
        // the show name and rejected every real result. Falls back to `title`
        // so callers/tests that only set `title` are unaffected.
        const QString& idText = cand.matchText.isEmpty() ? cand.title : cand.matchText;
        if (!showTitle.isEmpty() && !titleMatchesShow(idText, showTitle)) continue;
        survivors.append(i);
    }
    if (survivors.isEmpty()) return std::nullopt;                 // no source found

    // Rank: single-episode before batch -> dual-audio before single-audio ->
    // seeders desc -> release-type (sourceScore) desc -> lower original index.
    // The first two tiers serve "watch a specific episode": never pull a season
    // pack when a single exists, and prefer a dual-audio release among equals.
    // Batch/dual detection reads the full identity blob (matchText) when present
    // so it sees the filename wherever the addon stashed it (mirrors the gate).
    auto betterRank = [&](int a, int b) {
        const SourceCandidate& ca = candidates.at(a);
        const SourceCandidate& cb = candidates.at(b);
        const QString& ida = ca.matchText.isEmpty() ? ca.title : ca.matchText;
        const QString& idb = cb.matchText.isEmpty() ? cb.title : cb.matchText;
        const bool batchA = isBatchRelease(ida);
        const bool batchB = isBatchRelease(idb);
        if (batchA != batchB) return !batchA;   // non-batch (single) ranks first
        const bool dualA = hasDualAudio(ida);
        const bool dualB = hasDualAudio(idb);
        if (dualA != dualB) return dualA;        // dual-audio ranks first
        if (ca.seeders != cb.seeders) return ca.seeders > cb.seeders;
        const int sa = QualityScorer::sourceScore(ca.title);
        const int sb = QualityScorer::sourceScore(cb.title);
        if (sa != sb) return sa > sb;
        return a < b;
    };
    std::sort(survivors.begin(), survivors.end(), betterRank);

    // Step 2 - well-seeded survivor decides outright; size never consulted.
    if (candidates.at(survivors.first()).seeders >= kLowSeedThreshold)
        return survivors.first();

    // Step 3 - weakly-seeded tail. Runtime unknown -> cannot apply the size
    // guardrail; return the best-ranked survivor.
    if (runtimeMinutes <= 0)
        return survivors.first();

    // Drop implausible implied-bitrate releases, then take the best-ranked of
    // the rest. A candidate with unknown size (sizeBytes <= 0 -> 0.0) is kept.
    QList<int> sane;
    for (int idx : survivors) {
        const double mbps = impliedBitrateMbps(candidates.at(idx).sizeBytes, runtimeMinutes);
        if (mbps <= 0.0) { sane.append(idx); continue; }          // size unknown -> keep
        if (mbps < kMinBitrateMbps || mbps > kMaxBitrateMbps) continue;  // junk
        sane.append(idx);
    }
    if (sane.isEmpty()) return std::nullopt;
    std::sort(sane.begin(), sane.end(), betterRank);
    return sane.first();
}

}  // namespace tankostream::stream
