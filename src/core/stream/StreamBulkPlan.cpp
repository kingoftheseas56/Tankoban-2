#include "core/stream/StreamBulkPlan.h"
#include "core/stream/BulkSourceCollector.h"

// STREAM_BULK_DOWNLOAD Phase 0A — pure implementation. See header for full
// contract. This .cpp depends only on QtCore (no QFile, no QFileInfo, no
// network, no libtorrent). The `existsFn` predicate keeps the only
// filesystem touch behind a callable that production wires through
// QFileInfo::exists() at the call site (Phase 5+).

#include <QChar>
#include <QDir>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace tankostream::stream {

namespace {

// Windows-invalid filename characters per
// https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
// (excluding the path separators, which the sanitizer treats as control).
// Listed verbatim so the test phase can re-derive the exact set.
const QSet<QChar>& invalidChars() {
    static const QSet<QChar> kInvalid = {
        QChar('<'), QChar('>'), QChar(':'),  QChar('"'),
        QChar('/'), QChar('\\'), QChar('|'), QChar('?'), QChar('*'),
    };
    return kInvalid;
}

// Reserved Windows base names (case-insensitive). When a sanitized path
// segment matches one of these, append a trailing underscore. Includes
// COM1-COM9 + LPT1-LPT9.
bool isReservedWindowsBaseName(const QString& s) {
    static const QStringList kReserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"),
        QStringLiteral("COM4"), QStringLiteral("COM5"), QStringLiteral("COM6"),
        QStringLiteral("COM7"), QStringLiteral("COM8"), QStringLiteral("COM9"),
        QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"),
        QStringLiteral("LPT7"), QStringLiteral("LPT8"), QStringLiteral("LPT9"),
    };
    for (const QString& r : kReserved) {
        if (s.compare(r, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

constexpr int kMaxSegmentLength = 200;

QString twoDigit(int n) {
    return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0'));
}

// Strip a single leading dot from an extension hint (caller may pass ".mkv"
// or "mkv" — normalize to "mkv"). Empty input returns empty.
QString normalizeExtensionHint(const QString& raw) {
    QString ext = raw.trimmed();
    if (ext.startsWith(QLatin1Char('.'))) ext = ext.mid(1);
    return ext;
}

}  // namespace

// ── Forward sanitizer ────────────────────────────────────────────────────

QString sanitizePathSegment(const QString& raw) {
    if (raw.isEmpty()) return {};

    QString out;
    out.reserve(raw.size());

    bool prevReplaced = false;
    for (QChar c : raw) {
        const ushort u = c.unicode();
        const bool isControl = (u < 0x20);
        const bool isInvalid = invalidChars().contains(c);
        if (isControl || isInvalid) {
            // Replace stripped runs with a single space; multiple stripped
            // chars in a row produce one space, not many.
            if (!prevReplaced && !out.isEmpty()) {
                out.append(QLatin1Char(' '));
                prevReplaced = true;
            }
            continue;
        }
        out.append(c);
        prevReplaced = false;
    }

    // Collapse multi-space runs (introduced by stripping or already in input).
    // Done in a second pass for clarity over a single-pass state machine.
    {
        QString collapsed;
        collapsed.reserve(out.size());
        bool inSpace = false;
        for (QChar c : out) {
            if (c.isSpace()) {
                if (!inSpace) {
                    collapsed.append(QLatin1Char(' '));
                    inSpace = true;
                }
            } else {
                collapsed.append(c);
                inSpace = false;
            }
        }
        out = collapsed;
    }

    // Trim leading/trailing whitespace.
    out = out.trimmed();

    // Strip trailing dots (Windows treats "name." as "name" but stores it
    // confusingly; safer to drop the trailing period entirely). Multiple
    // trailing dots all go.
    while (out.endsWith(QLatin1Char('.'))) {
        out.chop(1);
    }

    // After dot-strip, trim again in case a "name. . ." case left whitespace.
    out = out.trimmed();

    if (out.isEmpty()) return {};

    // Reserved Windows base names — disambiguate with trailing underscore.
    // Note: This guards against the segment-as-filename case; for segment-
    // as-folder, Windows rejects these names too at create time.
    if (isReservedWindowsBaseName(out)) {
        out.append(QLatin1Char('_'));
    }

    // Cap length per segment.
    if (out.size() > kMaxSegmentLength) {
        out = out.left(kMaxSegmentLength).trimmed();
    }

    return out;
}

// ── Identity-key helpers ─────────────────────────────────────────────────

QString makeItemKey(const QString& seriesId, int season, int episode) {
    return QStringLiteral("%1:S%2E%3")
        .arg(seriesId, twoDigit(season), twoDigit(episode));
}

QString makeDestinationKey(const QString& showFolderName,
                           const QString& seasonFolderName,
                           const QString& canonicalFilename) {
    // Canonical key uses forward slashes regardless of platform separator
    // so QMap lookups are stable across Windows-native + Posix paths.
    return QStringLiteral("%1/%2/%3")
        .arg(showFolderName, seasonFolderName, canonicalFilename);
}

QString makeTorrentKey(const QString& infoHash) {
    const QString normalized = infoHash.trimmed().toLower();
    if (normalized.size() != 40) return {};
    for (const QChar c : normalized) {
        const bool isHex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                        || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
        if (!isHex) return {};
    }
    return normalized;
}

QString makeFileKey(const QString& infoHash, int fileIndex) {
    const QString torrentKey = makeTorrentKey(infoHash);
    if (torrentKey.isEmpty()) return {};
    return QStringLiteral("%1:%2").arg(torrentKey).arg(fileIndex);
}

// ── Naming functions ─────────────────────────────────────────────────────

QString buildSeasonFolderName(int season) {
    // Season 00 is a known "specials" convention in Plex/Jellyfin; we
    // don't reject it here — caller emits a warning if season<1 escaped
    // input validation.
    return QStringLiteral("Season %1").arg(twoDigit(season));
}

QString buildShowFolderName(const QString& showName, const QString& year) {
    const QString cleanShow = sanitizePathSegment(showName);
    if (cleanShow.isEmpty()) return {};
    const QString cleanYear = sanitizePathSegment(year);
    if (cleanYear.isEmpty()) return cleanShow;
    return QStringLiteral("%1 (%2)").arg(cleanShow, cleanYear);
}

QString buildEpisodeFilename(const QString& showName,
                             int season, int episode,
                             const QString& episodeTitle,
                             const QString& extension) {
    const QString cleanShow  = sanitizePathSegment(showName);
    const QString cleanTitle = sanitizePathSegment(episodeTitle);
    QString ext = normalizeExtensionHint(extension);
    if (ext.isEmpty()) ext = QStringLiteral("mkv");
    // Sanitize extension separately — strip Windows-invalid chars but
    // preserve the literal text otherwise. Extensions like "mp4" / "mkv"
    // pass through cleanly; "mp4 " (with space) trims.
    ext = sanitizePathSegment(ext);
    if (ext.isEmpty()) ext = QStringLiteral("mkv");

    const QString stem = cleanTitle.isEmpty()
        ? QStringLiteral("%1 - S%2E%3")
              .arg(cleanShow, twoDigit(season), twoDigit(episode))
        : QStringLiteral("%1 - S%2E%3 - %4")
              .arg(cleanShow, twoDigit(season), twoDigit(episode), cleanTitle);

    return QStringLiteral("%1.%2").arg(stem, ext);
}

// ── Plan computation ─────────────────────────────────────────────────────

namespace {

bool hasHdrOrDvBadge(const StreamPickerChoice& choice) {
    for (const QString& badge : choice.badges) {
        const QString upper = badge.toUpper();
        if (upper.contains(QStringLiteral("HDR")) ||
            upper.contains(QStringLiteral("DV"))) {
            return true;
        }
    }
    return false;
}

bool isSeededMagnet(const StreamPickerChoice& choice) {
    return choice.sourceKind == QStringLiteral("magnet") &&
           choice.seeders > 0 &&
           !makeTorrentKey(choice.infoHash).isEmpty();
}

bool isPackEligibleUnverified(const StreamPickerChoice& choice) {
    return isSeededMagnet(choice) &&
           choice.packType == QStringLiteral("season") &&
           choice.qualitySort >= 2;
}

int qualityCascadeRank(int qualitySort) {
    if (qualitySort == 3) return 0;
    if (qualitySort == 4 || qualitySort == 5) return 1;
    if (qualitySort == 2) return 2;
    return 3;
}

bool matchesPerEpisodeTier(const StreamPickerChoice& choice, int tierRank) {
    if (!isSeededMagnet(choice)) return false;
    if (tierRank == 0) return choice.qualitySort == 3;
    if (tierRank == 1) return choice.qualitySort == 4 || choice.qualitySort == 5;
    if (tierRank == 2) return choice.qualitySort == 2;
    return true;
}

bool bulkChoiceLess(const StreamPickerChoice& a, const StreamPickerChoice& b) {
    if (a.seeders != b.seeders) return a.seeders > b.seeders;

    const bool aHdr = hasHdrOrDvBadge(a);
    const bool bHdr = hasHdrOrDvBadge(b);
    if (aHdr != bHdr) return !aHdr;

    const bool aKnownSize = a.sizeBytes > 0;
    const bool bKnownSize = b.sizeBytes > 0;
    if (aKnownSize && bKnownSize && a.sizeBytes != b.sizeBytes) {
        return a.sizeBytes < b.sizeBytes;
    }
    if (aKnownSize != bKnownSize) return aKnownSize;
    if (a.sizeBytes != b.sizeBytes) return a.sizeBytes > b.sizeBytes;

    const QString aKey = makeTorrentKey(a.infoHash);
    const QString bKey = makeTorrentKey(b.infoHash);
    const int hashCmp = QString::compare(aKey, bKey, Qt::CaseInsensitive);
    if (hashCmp != 0) return hashCmp < 0;
    if (a.fileIndex != b.fileIndex) return a.fileIndex < b.fileIndex;
    return QString::compare(a.displayTitle, b.displayTitle, Qt::CaseInsensitive) < 0;
}

bool choicesAmbiguousForBulkTie(const StreamPickerChoice& a,
                                const StreamPickerChoice& b) {
    return a.seeders == b.seeders &&
           hasHdrOrDvBadge(a) == hasHdrOrDvBadge(b) &&
           a.sizeBytes == b.sizeBytes &&
           makeTorrentKey(a.infoHash) != makeTorrentKey(b.infoHash);
}

void appendSelectionWarning(QList<BulkSelectionWarning>& warnings,
                            BulkSelectionWarningKind kind,
                            const QString& detail,
                            const QString& relatedItemKey = {}) {
    warnings.push_back(BulkSelectionWarning{kind, detail, relatedItemKey});
}

struct PackCandidate {
    QString torrentKey;
    StreamPickerChoice choice;
    QSet<int> episodeNums;
};

bool packCandidateLess(const PackCandidate& a, const PackCandidate& b) {
    const int aRank = qualityCascadeRank(a.choice.qualitySort);
    const int bRank = qualityCascadeRank(b.choice.qualitySort);
    if (aRank != bRank) return aRank < bRank;
    if (a.choice.seeders != b.choice.seeders) return a.choice.seeders > b.choice.seeders;

    if (hasHdrOrDvBadge(a.choice) != hasHdrOrDvBadge(b.choice)) {
        return !hasHdrOrDvBadge(a.choice);
    }

    const bool aKnownSize = a.choice.sizeBytes > 0;
    const bool bKnownSize = b.choice.sizeBytes > 0;
    if (aKnownSize && bKnownSize && a.choice.sizeBytes != b.choice.sizeBytes) {
        return a.choice.sizeBytes < b.choice.sizeBytes;
    }
    if (aKnownSize != bKnownSize) return aKnownSize;
    return QString::compare(a.torrentKey, b.torrentKey, Qt::CaseInsensitive) < 0;
}

bool packCandidatesAmbiguous(const PackCandidate& a, const PackCandidate& b) {
    return qualityCascadeRank(a.choice.qualitySort) == qualityCascadeRank(b.choice.qualitySort) &&
           a.choice.seeders == b.choice.seeders &&
           hasHdrOrDvBadge(a.choice) == hasHdrOrDvBadge(b.choice) &&
           a.choice.sizeBytes == b.choice.sizeBytes &&
           a.torrentKey != b.torrentKey;
}

QString packDisplayLabel(const StreamPickerChoice& choice) {
    if (!choice.packLabel.trimmed().isEmpty()) return choice.packLabel;
    return choice.displayTitle;
}

void finalizeSelectionSummary(BulkSelectionPlan& selection,
                              const BulkPlanResult& planResult,
                              qint64 estimatedTotalBytes) {
    BulkSelectionPreflightSummary summary;
    summary.totalEpisodes = planResult.items.size();
    summary.packMode = selection.mode == BulkSelectionMode::Pack;
    summary.packLabel = selection.preflight.packLabel;
    summary.estimatedTotalBytes = estimatedTotalBytes;

    for (const BulkPlanItem& item : planResult.items) {
        if (item.status == BulkPlanItemStatus::Skipped) {
            ++summary.alreadyInLibrary;
        }
    }

    for (const BulkSelectionItem& item : selection.items) {
        if (item.reason == BulkSelectionReason::MissingNoSource) {
            ++summary.missingNoSource;
            continue;
        }
        if (item.reason == BulkSelectionReason::Picked ||
            item.reason == BulkSelectionReason::PackCovered) {
            ++summary.qualityBreakdown[item.pickQuality];
        }
    }

    summary.toDownload = summary.totalEpisodes -
                         summary.alreadyInLibrary -
                         summary.missingNoSource;
    selection.preflight = summary;
}

}  // namespace

BulkSelectionPlan buildBulkSelection(const BulkPlanResult& planResult,
                                     const BulkSourceCollectionPayload& sources) {
    BulkSelectionPlan selection;
    selection.mode = BulkSelectionMode::PerEpisode;
    selection.groupShape = QStringLiteral("per-episode");
    selection.preflight.totalEpisodes = planResult.items.size();

    QList<BulkPlanItem> activeItems;
    activeItems.reserve(planResult.items.size());
    for (const BulkPlanItem& item : planResult.items) {
        if (item.status != BulkPlanItemStatus::Skipped) {
            activeItems.push_back(item);
        }
    }

    if (activeItems.isEmpty()) {
        finalizeSelectionSummary(selection, planResult, 0);
        return selection;
    }

    QHash<QString, PackCandidate> packByHash;
    bool sawPackCandidate = false;
    for (const BulkPlanItem& item : activeItems) {
        const int episodeNum = item.input.episode;
        const auto resultIt = sources.byEpisode.constFind(episodeNum);
        if (resultIt == sources.byEpisode.cend()) continue;

        QSet<QString> seenForEpisode;
        for (const StreamPickerChoice& choice : resultIt->choices) {
            if (!isPackEligibleUnverified(choice)) continue;

            const QString torrentKey = makeTorrentKey(choice.infoHash);
            if (torrentKey.isEmpty() || seenForEpisode.contains(torrentKey)) continue;

            sawPackCandidate = true;
            seenForEpisode.insert(torrentKey);

            PackCandidate candidate = packByHash.value(torrentKey);
            if (candidate.torrentKey.isEmpty()) {
                candidate.torrentKey = torrentKey;
                candidate.choice = choice;
            } else {
                PackCandidate challenger{torrentKey, choice, candidate.episodeNums};
                if (packCandidateLess(challenger, candidate)) {
                    candidate.choice = choice;
                }
            }
            candidate.episodeNums.insert(episodeNum);
            packByHash.insert(torrentKey, candidate);
        }
    }

    QList<PackCandidate> coveringPacks;
    bool rejectedPartialPack = false;
    const int activeEpisodeCount = activeItems.size();
    for (const PackCandidate& candidate : packByHash) {
        if (candidate.episodeNums.size() == activeEpisodeCount) {
            coveringPacks.push_back(candidate);
        } else {
            rejectedPartialPack = true;
        }
    }

    if (!sawPackCandidate) {
        appendSelectionWarning(
            selection.warnings,
            BulkSelectionWarningKind::NoPackCandidate,
            QStringLiteral("No season-pack magnet candidate was returned for the requested episodes"));
    }

    if (rejectedPartialPack) {
        appendSelectionWarning(
            selection.warnings,
            BulkSelectionWarningKind::PackUnverified,
            QStringLiteral("One or more season-pack candidates did not appear in every non-skipped episode source list"));
    }

    if (!coveringPacks.isEmpty()) {
        std::sort(coveringPacks.begin(), coveringPacks.end(), packCandidateLess);
        const PackCandidate chosenPack = coveringPacks.first();
        if (coveringPacks.size() > 1 &&
            packCandidatesAmbiguous(chosenPack, coveringPacks.at(1))) {
            appendSelectionWarning(
                selection.warnings,
                BulkSelectionWarningKind::TieBreakAmbiguous,
                QStringLiteral("Multiple covering season-pack candidates tied on the bulk pick policy"));
        }

        appendSelectionWarning(
            selection.warnings,
            BulkSelectionWarningKind::PackUnverified,
            QStringLiteral("Chosen season pack is addon-claim covered; torrent metadata verification is pending"));

        selection.mode = BulkSelectionMode::Pack;
        selection.groupShape = QStringLiteral("pack");
        selection.preflight.packLabel = packDisplayLabel(chosenPack.choice);

        StreamPickerChoice chosenChoice = chosenPack.choice;
        chosenChoice.infoHash = chosenPack.torrentKey;
        chosenChoice.fileIndex = -1;

        if (chosenChoice.qualitySort != 3) {
            appendSelectionWarning(
                selection.warnings,
                BulkSelectionWarningKind::QualityFallbackUsed,
                QStringLiteral("Chosen season pack is not 1080p"));
        }

        for (const BulkPlanItem& item : activeItems) {
            selection.items.push_back(BulkSelectionItem{
                item.itemKey,
                item.input.episode,
                chosenChoice,
                chosenChoice.qualitySort,
                chosenChoice.qualitySort != 3,
                BulkSelectionReason::PackCovered});
        }

        finalizeSelectionSummary(selection, planResult, chosenChoice.sizeBytes);
        return selection;
    }

    qint64 estimatedTotalBytes = 0;
    int missingCount = 0;
    for (const BulkPlanItem& item : activeItems) {
        const int episodeNum = item.input.episode;
        QList<StreamPickerChoice> choices;
        const auto resultIt = sources.byEpisode.constFind(episodeNum);
        if (resultIt != sources.byEpisode.cend()) {
            choices = resultIt->choices;
        }

        StreamPickerChoice chosen;
        bool found = false;
        bool ambiguous = false;
        for (int tier = 0; tier < 4 && !found; ++tier) {
            QList<StreamPickerChoice> tierChoices;
            for (const StreamPickerChoice& choice : choices) {
                if (matchesPerEpisodeTier(choice, tier)) {
                    tierChoices.push_back(choice);
                }
            }
            if (tierChoices.isEmpty()) continue;

            std::sort(tierChoices.begin(), tierChoices.end(), bulkChoiceLess);
            chosen = tierChoices.first();
            found = true;
            if (tierChoices.size() > 1 &&
                choicesAmbiguousForBulkTie(chosen, tierChoices.at(1))) {
                ambiguous = true;
            }
        }

        if (!found) {
            ++missingCount;
            selection.items.push_back(BulkSelectionItem{
                item.itemKey,
                episodeNum,
                StreamPickerChoice{},
                0,
                false,
                BulkSelectionReason::MissingNoSource});
            continue;
        }

        chosen.infoHash = makeTorrentKey(chosen.infoHash);
        const bool fallbackUsed = chosen.qualitySort != 3;
        if (fallbackUsed) {
            appendSelectionWarning(
                selection.warnings,
                BulkSelectionWarningKind::QualityFallbackUsed,
                QStringLiteral("Episode %1 selected non-1080p quality %2")
                    .arg(episodeNum)
                    .arg(chosen.qualitySort),
                item.itemKey);
        }
        if (ambiguous) {
            appendSelectionWarning(
                selection.warnings,
                BulkSelectionWarningKind::TieBreakAmbiguous,
                QStringLiteral("Episode %1 had multiple sources tied on the bulk pick policy")
                    .arg(episodeNum),
                item.itemKey);
        }

        estimatedTotalBytes += chosen.sizeBytes;
        selection.items.push_back(BulkSelectionItem{
            item.itemKey,
            episodeNum,
            chosen,
            chosen.qualitySort,
            fallbackUsed,
            BulkSelectionReason::Picked});
    }

    if (missingCount > 0) {
        appendSelectionWarning(
            selection.warnings,
            BulkSelectionWarningKind::MissingEpisodes,
            QStringLiteral("%1 episode(s) have no seeded magnet source").arg(missingCount));
    }

    finalizeSelectionSummary(selection, planResult, estimatedTotalBytes);
    return selection;
}

BulkPlanResult buildBulkPlan(const BulkPlanInput& input,
                             const PathExistsFn& existsFn) {
    BulkPlanResult result;

    // Show + season folder names + composite season path.
    result.showFolderName   = buildShowFolderName(input.seriesTitle, input.seriesYear);
    result.seasonFolderName = buildSeasonFolderName(input.seasonNumber);

    // Compose absolute season path. Use QDir::cleanPath to normalize but
    // leave separators in native form — the caller (Phase 5+) is the one
    // that converts to native via QDir::toNativeSeparators when handing
    // off to QFile / libtorrent.
    {
        QString basePath = input.videosRootPath;
        if (!result.showFolderName.isEmpty()) {
            basePath = QDir(basePath).filePath(result.showFolderName);
        }
        if (!result.seasonFolderName.isEmpty()) {
            basePath = QDir(basePath).filePath(result.seasonFolderName);
        }
        result.showSeasonAbsolutePath = QDir::cleanPath(basePath);
    }

    // Input-level warnings.
    if (input.videosRootPath.isEmpty()) {
        result.warnings.push_back(BulkPlanWarning{
            BulkPlanWarningKind::InvalidVideosRoot,
            QStringLiteral("videosRootPath is empty"),
            QString()});
    } else if (!QDir(input.videosRootPath).isAbsolute()) {
        result.warnings.push_back(BulkPlanWarning{
            BulkPlanWarningKind::InvalidVideosRoot,
            QStringLiteral("videosRootPath is not absolute: %1").arg(input.videosRootPath),
            QString()});
    }

    if (input.seriesTitle.trimmed().isEmpty()) {
        result.warnings.push_back(BulkPlanWarning{
            BulkPlanWarningKind::EmptySeriesTitle,
            QStringLiteral("seriesTitle is empty for seriesId=%1").arg(input.seriesId),
            QString()});
    }

    // Per-episode item construction + duplicate-name detection.
    QSet<QString> seenCanonicalFilenames;

    for (const BulkPlanEpisodeInput& ep : input.episodes) {
        BulkPlanItem item;
        item.input = ep;
        item.itemKey = makeItemKey(input.seriesId, ep.season, ep.episode);

        // Episode-level warnings.
        if (ep.season < 1 || ep.episode < 1) {
            result.warnings.push_back(BulkPlanWarning{
                BulkPlanWarningKind::SuspiciousEpisodeNumber,
                QStringLiteral("season=%1 episode=%2 outside [1, ∞)").arg(ep.season).arg(ep.episode),
                item.itemKey});
        }

        if (ep.title.trimmed().isEmpty()) {
            result.warnings.push_back(BulkPlanWarning{
                BulkPlanWarningKind::EmptyEpisodeTitle,
                QStringLiteral("episode title empty; canonical falls back to S%1E%2 only")
                    .arg(twoDigit(ep.season), twoDigit(ep.episode)),
                item.itemKey});
        }

        item.canonicalFilename = buildEpisodeFilename(
            input.seriesTitle, ep.season, ep.episode, ep.title, ep.extensionHint);

        // Duplicate-canonical-name detection (e.g. two episodes share a
        // title that sanitizes to the same string, or a season-pack lists
        // E12 twice with different file indices). This is dev-log-only;
        // pre-flight surfaces a warning row.
        if (seenCanonicalFilenames.contains(item.canonicalFilename)) {
            result.warnings.push_back(BulkPlanWarning{
                BulkPlanWarningKind::DuplicateCanonicalName,
                QStringLiteral("canonical filename collides: %1").arg(item.canonicalFilename),
                item.itemKey});
        }
        seenCanonicalFilenames.insert(item.canonicalFilename);

        item.canonicalRelativePath = makeDestinationKey(
            result.showFolderName, result.seasonFolderName, item.canonicalFilename);

        // Compose absolute path. Show + season + filename joined under root.
        item.canonicalAbsolutePath = QDir::cleanPath(
            QDir(result.showSeasonAbsolutePath).filePath(item.canonicalFilename));

        item.destinationKey = item.canonicalRelativePath;

        // Skip-if-exists check (audit Q4 + Improvement 1). The injected
        // predicate is the only filesystem touchpoint in the pure planner.
        if (existsFn && existsFn(item.canonicalAbsolutePath)) {
            item.status = BulkPlanItemStatus::Skipped;
        } else {
            item.status = BulkPlanItemStatus::PendingSource;
        }

        result.items.push_back(item);
    }

    return result;
}

}  // namespace tankostream::stream
