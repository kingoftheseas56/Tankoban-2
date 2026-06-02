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
        if (!showTitle.isEmpty() && !titleMatchesShow(cand.title, showTitle)) continue;
        survivors.append(i);
    }
    if (survivors.isEmpty()) return std::nullopt;                 // no source found

    // Rank: seeders desc -> release-type (sourceScore) desc -> lower original
    // index. Deterministic even when seeders tie.
    auto betterRank = [&](int a, int b) {
        const SourceCandidate& ca = candidates.at(a);
        const SourceCandidate& cb = candidates.at(b);
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
