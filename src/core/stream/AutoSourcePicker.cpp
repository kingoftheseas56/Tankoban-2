#include "core/stream/AutoSourcePicker.h"
#include <QRegularExpression>
#include <algorithm>

namespace tankostream::stream {

bool AutoSourcePicker::isCamRip(const QString& title) {
    // Camcorder-class tags, bounded so ordinary words don't match.
    // "TS"/"TC" intentionally NOT matched (false positives like "GUTS");
    // can be added with care later (spec section 8.2).
    static const QRegularExpression re(
        QStringLiteral("(^|[^a-z0-9])(cam|camrip|hdcam|telesync|telecine|hdts)([^a-z0-9]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(title).hasMatch();
}

double AutoSourcePicker::impliedBitrateMbps(qint64 sizeBytes, int runtimeMinutes) {
    if (sizeBytes <= 0 || runtimeMinutes <= 0) return 0.0;
    return (static_cast<double>(sizeBytes) * 8.0)
         / (static_cast<double>(runtimeMinutes) * 60.0) / 1.0e6;
}

std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidates,
                                          int runtimeMinutes) {
    // Step 1 - hard filters.
    QList<int> survivors;
    for (int i = 0; i < candidates.size(); ++i) {
        const SourceCandidate& cand = candidates.at(i);
        if (cand.qualitySort != kRequiredQualitySort) continue;  // 1080p only
        if (cand.seeders <= 0) continue;                          // dead torrent
        if (isCamRip(cand.title)) continue;                       // camcorder rip
        survivors.append(i);
    }
    if (survivors.isEmpty()) return std::nullopt;                 // no source found

    auto bySeedersDesc = [&](int a, int b) {
        return candidates.at(a).seeders > candidates.at(b).seeders;
    };
    std::sort(survivors.begin(), survivors.end(), bySeedersDesc);

    // Step 2 - well-seeded survivor decides outright; size never consulted.
    if (candidates.at(survivors.first()).seeders >= kLowSeedThreshold)
        return survivors.first();

    // Step 3 - weakly-seeded tail: drop implausible implied-bitrate releases
    // (only when runtime known), then take the best-seeded of the rest.
    QList<int> sane;
    for (int idx : survivors) {
        const double mbps = impliedBitrateMbps(candidates.at(idx).sizeBytes, runtimeMinutes);
        if (mbps == 0.0) { sane.append(idx); continue; }          // unknown -> keep
        if (mbps < kMinBitrateMbps || mbps > kMaxBitrateMbps) continue;  // junk
        sane.append(idx);
    }
    if (sane.isEmpty()) return std::nullopt;
    std::sort(sane.begin(), sane.end(), bySeedersDesc);
    return sane.first();
}

}  // namespace tankostream::stream
