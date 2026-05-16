#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 - pure-logic title->ScopeEstimate
// parser. Powers the scope-picker tile grid's INSTANT render BEFORE
// libtorrent fetches real torrent metadata. Real metadata replaces this
// estimate when it arrives via TorrentEngine::metadataReady.

#include <QList>
#include <QString>

namespace tankoban::stream::theatre {

struct EpisodeEstimate {
    int  season  = 0;
    int  episode = 0;
    // Title is best-effort if derivable from the title string; otherwise
    // empty (real metadata will populate filename-based titles when it
    // arrives).
    QString title;
};

struct ScopeEstimate {
    QList<int>             detectedSeasons;       // sorted ascending
    QList<EpisodeEstimate> episodes;              // sorted by (season, episode)
    bool                   isCompleteSeries = false;
    bool                   hasExplicitEpisodeCount = false;  // true if title had eAA-eBB or "N Eps"
};

// Estimate pack contents from title alone. For Complete Series with no
// embedded episode count, returns an empty episodes list - the panel
// renders only per-season headers and waits for real metadata.
//
// For single-season packs ("S01.COMPLETE"), guesses a default of 10
// episodes (matches the median season length across modern TV; refined
// by real metadata when it arrives).
ScopeEstimate estimate(const QString& title);

}  // namespace tankoban::stream::theatre
