#include "core/stream/SourceRanker.h"

#include <QtMath>
#include <algorithm>

namespace tankoban::stream {

SourceRanker::SourceRanker(const QSet<QString>& trustedUploaders)
    : m_trustedUploaders(trustedUploaders)
{
}

double SourceRanker::scoreOne(const TorrentResult& r) const
{
    // Phase 1 formula (subset of spec): 0.65 x seeder_score + 0.35 x trust_score.
    // Full formula (quality + size) added in Phase 2.
    const double seederScore = r.seeders > 0
        ? std::min(1.0, std::log10(r.seeders + 1) / 3.0)
        : 0.0;

    // Trust score: 1.0 if uploader tag (from title suffix) is in trusted set,
    // 0.5 otherwise; clamped down for empty-seeder zombie torrents so they
    // can't clear the confidence threshold.
    double trustScore = 0.5;
    for (const QString& uploader : m_trustedUploaders) {
        if (r.title.contains(uploader, Qt::CaseInsensitive)) {
            trustScore = 1.0;
            break;
        }
    }
    if (r.seeders == 0) trustScore = std::min(trustScore, 0.0);

    return 0.65 * seederScore + 0.35 * trustScore;
}

QList<SourceRanker::Scored> SourceRanker::rank(const QList<TorrentResult>& results) const
{
    QList<Scored> scored;
    scored.reserve(results.size());
    for (const auto& r : results) {
        scored.append({r, scoreOne(r)});
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.score > b.score;
    });
    return scored;
}

std::optional<TorrentResult> SourceRanker::pickTop(const QList<TorrentResult>& results) const
{
    const auto ranked = rank(results);
    if (ranked.isEmpty()) return std::nullopt;
    if (ranked.first().score < kConfidenceThreshold) return std::nullopt;
    return ranked.first().result;
}

} // namespace tankoban::stream
