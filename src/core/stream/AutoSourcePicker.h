#pragma once
#include <QList>
#include <QString>
#include <optional>

namespace tankostream::stream {

// One Torrentio candidate reduced to the fields auto-pick needs.
// Pure data; no Qt-UI dependency (mirrors QualityScorer's layering).
struct SourceCandidate {
    QString title;            // release title (CAM detection + tiebreak)
    int     seeders = 0;
    qint64  sizeBytes = 0;
    int     qualitySort = 0;  // 5=2160p 4=1440p 3=1080p 2=720p 1=480p 0=unknown

    // DOWNLOAD BUG 2026-06-03 — text the show-identity gate matches against.
    // `title` is the cleaned one-line display name, but some addons (NyaaSi /
    // anime-shaped Torrentio) put a stats badge ("[2176 seeders | 1.34 GB |
    // NyaaSi]") there instead of the filename, so gating on `title` rejected every
    // One Piece result ("No 1080p source found"). matchText carries the full
    // raw identity blob (name + description + fileNameHint + …) so the gate can
    // find the show name wherever the addon stashed it. Empty == fall back to
    // `title` (preserves every existing caller + test that only sets `title`).
    QString matchText;
};

// Silent best-source selection for Theatre's one-tap download.
// Filter (1080p / seeders>0 / not-CAM) -> rank by seeders -> size guardrail
// only for the weakly-seeded tail. Returns the index into `candidates` of
// the chosen source, or std::nullopt for "no source found".
class AutoSourcePicker {
public:
    // runtimeMinutes <= 0 means "unknown" -> size guardrail skipped.
    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
                                   int runtimeMinutes = 0);

    // Show-identity gate (DOWNLOAD BUG 2026-06-02): when showTitle is non-empty,
    // candidates whose release title lacks the show's significant tokens are
    // rejected BEFORE ranking — so a One Piece request can never download a
    // 'Community' pack just because it is better-seeded. Empty showTitle = no gate
    // (identical to the runtime-only overload above).
    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
                                   const QString& showTitle,
                                   int runtimeMinutes = 0);

    // True iff every significant token of showTitle appears as a whole word in
    // candidateTitle (both normalized: lowercased, separators->space). Stopwords
    // and 1-char tokens are ignored; an all-stopword show title never blocks.
    static bool   titleMatchesShow(const QString& candidateTitle,
                                   const QString& showTitle);

    static bool   isCamRip(const QString& title);
    static double impliedBitrateMbps(qint64 sizeBytes, int runtimeMinutes);

    static constexpr int    kRequiredQualitySort = 3;     // 1080p only
    static constexpr int    kLowSeedThreshold     = 30;    // below = tail
    static constexpr double kMinBitrateMbps       = 1.5;   // re-encode floor
    static constexpr double kMaxBitrateMbps       = 20.0;  // remux/4K ceiling
};

}  // namespace tankostream::stream
