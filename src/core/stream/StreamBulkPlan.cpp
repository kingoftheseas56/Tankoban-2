#include "core/stream/StreamBulkPlan.h"

// STREAM_BULK_DOWNLOAD Phase 0A — pure implementation. See header for full
// contract. This .cpp depends only on QtCore (no QFile, no QFileInfo, no
// network, no libtorrent). The `existsFn` predicate keeps the only
// filesystem touch behind a callable that production wires through
// QFileInfo::exists() at the call site (Phase 5+).

#include <QChar>
#include <QDir>
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
