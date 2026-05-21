#pragma once

#include "core/TorrentResult.h"

#include <QList>
#include <QString>
#include <QSet>
#include <optional>

namespace tankoban::stream {

// Pure-logic ranker for Tankorent search results. Scores each result based on
// seeders + uploader trust + (Phase 2) quality + size sanity; returns the
// list re-sorted by descending score, and a pickTop convenience that returns
// nullopt when no result clears the confidence threshold.
//
// Phase 1 scope: seeders + trust only. Phase 2 adds quality + size + pack-
// bonus per the design spec.
class SourceRanker {
public:
    struct Scored {
        TorrentResult result;
        double score = 0.0;
    };

    // Confidence threshold below which pickTop returns nullopt (caller must
    // fall back to manual-pick mode).
    static constexpr double kConfidenceThreshold = 0.30;

    explicit SourceRanker(const QSet<QString>& trustedUploaders);

    // Returns the input list re-sorted descending by score. Original list
    // not mutated.
    QList<Scored> rank(const QList<TorrentResult>& results) const;

    // Returns the highest-scored result if its score >= kConfidenceThreshold,
    // otherwise nullopt.
    std::optional<TorrentResult> pickTop(const QList<TorrentResult>& results) const;

private:
    double scoreOne(const TorrentResult& r) const;

    QSet<QString> m_trustedUploaders;
};

} // namespace tankoban::stream
