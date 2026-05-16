#include "core/stream/QualityScorer.h"

#include <QRegularExpression>
#include <cmath>

namespace tankostream::stream {

int QualityScorer::resolutionScore(const QString& filename) {
    static const QRegularExpression re4k(QStringLiteral("(?i)\\b(2160p|4k)\\b"));
    static const QRegularExpression re1440(QStringLiteral("(?i)\\b1440p\\b"));
    static const QRegularExpression re1080(QStringLiteral("(?i)\\b1080p\\b"));
    static const QRegularExpression re720(QStringLiteral("(?i)\\b720p\\b"));
    static const QRegularExpression re480(QStringLiteral("(?i)\\b480p\\b"));
    if (re4k.match(filename).hasMatch())   return 100;
    if (re1440.match(filename).hasMatch()) return 90;
    if (re1080.match(filename).hasMatch()) return 80;
    if (re720.match(filename).hasMatch())  return 60;
    if (re480.match(filename).hasMatch())  return 40;
    return 20;
}

int QualityScorer::sourceScore(const QString& filename) {
    static const QRegularExpression reBlu(QStringLiteral("(?i)\\b(BluRay|BDRip|Blu-Ray)\\b"));
    static const QRegularExpression reWebDL(QStringLiteral("(?i)\\b(WEB-DL|WEBDL)\\b"));
    static const QRegularExpression reHDTV(QStringLiteral("(?i)\\bHDTV\\b"));
    static const QRegularExpression reWebRip(QStringLiteral("(?i)\\b(WEBRip|WEB-RIP)\\b"));
    static const QRegularExpression reDVD(QStringLiteral("(?i)\\b(DVDRip|DVD)\\b"));
    if (reBlu.match(filename).hasMatch())    return 100;
    if (reWebDL.match(filename).hasMatch())  return 80;
    if (reHDTV.match(filename).hasMatch())   return 60;
    if (reWebRip.match(filename).hasMatch()) return 50;
    if (reDVD.match(filename).hasMatch())    return 40;
    return 20;
}

int QualityScorer::qualityScore(const QString& filename) {
    const int res = resolutionScore(filename);
    const int src = sourceScore(filename);
    // Principled rounding (vs truncation) so future scoring-constant tuning
    // doesn't introduce silent off-by-one. std::lround returns long; the
    // result is always in [20, 100] so the int narrowing is safe.
    return static_cast<int>(std::lround(0.7 * res + 0.3 * src));
}

int QualityScorer::healthScore(int seeders) {
    if (seeders < 0) seeders = 0;
    const double score = std::log2(static_cast<double>(seeders) + 1.0) * 10.0;
    if (score > 100.0) return 100;
    return static_cast<int>(score);
}

double QualityScorer::combinedScore(int quality, int health, double wQuality, double wHealth) {
    const double sum = wQuality + wHealth;
    if (sum <= 0.0) return 0.0;
    return (static_cast<double>(quality) * wQuality + static_cast<double>(health) * wHealth) / sum;
}

}  // namespace tankostream::stream
