#pragma once

// TANKORENT_STREAM_INTEGRATION 2026-05-15 — quality and health scoring helpers
// shared between TorrentPackPicker sort logic and StreamDownloadIndex
// highest-quality-wins dedup. Pure-logic; no Qt object state; trivially testable
// via tankoban_tests.
//
// All inputs are filename basenames (no directory prefix). Tags detected
// case-insensitively. Functions return 0..100 scores; combinedScore returns
// 0..100 weighted average.

#include <QString>

namespace tankostream::stream {

class QualityScorer {
public:
    // 4K/2160p=100, 1440p=90, 1080p=80, 720p=60, 480p=40, else=20.
    static int resolutionScore(const QString& filename);

    // BluRay=100, WEB-DL=80, HDTV=60, WEBRip=50, DVDRip=40, else=20.
    static int sourceScore(const QString& filename);

    // Weighted combo: 0.7*resolution + 0.3*source.
    static int qualityScore(const QString& filename);

    // log2(seeders + 1) * 10, capped at 100. 0 seeders = 0; 1023 seeders = 100.
    static int healthScore(int seeders);

    // (quality * wQuality + health * wHealth) / (wQuality + wHealth).
    // Caller ensures wQuality >= 0, wHealth >= 0, and (wQuality + wHealth) > 0;
    // otherwise returns 0. Negative weights are not supported (would produce
    // out-of-range scores); the guard only catches the sum-is-zero case.
    static double combinedScore(int quality, int health, double wQuality, double wHealth);
};

}  // namespace tankostream::stream
