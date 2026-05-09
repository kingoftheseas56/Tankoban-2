#pragma once

// STREAM_BULK_DOWNLOAD Phase 0 — pure planning types + forward sanitizer +
// canonical naming + skip-decision builder for the bulk-season-download
// feature. No UI, no Qt event loop, no network, no libtorrent. Logic-only;
// every function in this header is deterministic given its inputs.
//
// Spec: docs/superpowers/specs/2026-05-07-stream-bulk-download-design.md
// Audit: agents/audits/stream_bulk_download_2026-05-07.md
// Action plan: agents/audits/stream_bulk_download_action_plan_2026-05-07.md
//
// Phase 0A scope (this file pair): types + sanitizer + naming + buildBulkPlan.
// Phase 0B scope (follow-up RTC): test harness resurrection + tests.

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace tankostream::stream {

// ── Inputs ────────────────────────────────────────────────────────────────

// Per-episode planning input — what we know about an episode before any
// source-pick is run. Built from StreamDetailView's m_seasons[s] entries.
struct BulkPlanEpisodeInput {
    int     season   = 0;     // 1-based; 0 = invalid sentinel
    int     episode  = 0;     // 1-based; 0 = invalid sentinel
    QString title;            // raw episode title from addon meta; may be empty

    // Optional file extension hint (without the leading dot — e.g. "mkv").
    // Phase 0 ignores this; Phase 4 fills it in once torrent metadata
    // resolves the actual file. Default "mkv" matches the spec §6.2 default.
    QString extensionHint;
};

// Whole-bulk planning input — the immutable seed for a bulk-download
// operation. Built at trigger-time on the GUI thread; passed through to
// the planner unchanged.
struct BulkPlanInput {
    QString seriesId;                       // imdbId, e.g. "tt1190634"
    QString seriesTitle;                    // raw show name from addon meta
    QString seriesYear;                     // optional, e.g. "2019"; "" if unknown
    int     seasonNumber = 0;               // 1-based; matches BulkPlanEpisodeInput.season
    QList<BulkPlanEpisodeInput> episodes;   // ordered by episode#; size>=1
    QString videosRootPath;                 // absolute path; e.g. "C:/Users/.../Videos"
};

// ── Outputs ───────────────────────────────────────────────────────────────

// Per-episode plan status. Phase 0 only emits Skipped or PendingSource;
// Missing + Planned are written by Phase 2/3 (source collector + bulk-pick
// policy).
enum class BulkPlanItemStatus {
    PendingSource,    // Phase 0 default; awaiting source-pick fan-out
    Skipped,          // existing canonical file at destination (Q4 skip-if-exists)
    Missing,          // no source available after fan-out (Phase 2)
    Planned,          // ready to download (Phase 3)
};

// Per-episode plan row. Ordered 1:1 with BulkPlanInput.episodes.
struct BulkPlanItem {
    BulkPlanEpisodeInput input;             // copy of the episode input
    QString  canonicalFilename;             // "Show - SxxEyy - Title.ext"
    QString  canonicalRelativePath;         // "Show (year)/Season NN/<canonicalFilename>"
    QString  canonicalAbsolutePath;         // <videosRoot>/<canonicalRelativePath>
    BulkPlanItemStatus status = BulkPlanItemStatus::PendingSource;

    // Identity keys per audit Improvement 8. Phase 0 populates the two
    // planning-side keys; torrentKey + fileKey land in Phase 2/3 once a
    // source is picked.
    QString  itemKey;                       // "<seriesId>:S<NN>E<NN>"
    QString  destinationKey;                // canonicalRelativePath, normalized
};

// Plan warning — non-fatal issues surfaced for the pre-flight dialog and
// dev logs. Tier-1 audit refinement: pre-flight should not pretend all
// risk is known (Improvement 7).
enum class BulkPlanWarningKind {
    DuplicateCanonicalName,    // two episodes resolve to the same canonical name
    InvalidVideosRoot,         // videosRootPath empty or not absolute
    EmptyEpisodeTitle,         // input title was empty; canonical falls back to S/E only
    EmptySeriesTitle,          // seriesTitle was empty; show folder name will be a placeholder
    SuspiciousEpisodeNumber,   // season<1 or episode<1 — sentinel inputs that escaped validation
};

struct BulkPlanWarning {
    BulkPlanWarningKind kind;
    QString detail;            // human-readable; not user-facing yet, dev/log only
    QString relatedItemKey;    // "" if not item-specific
};

// Whole-bulk planning result — Phase 0's output. Phases 2/3 mutate items[]
// in place to add Missing/Planned/source choices; Phase 0 leaves them at
// PendingSource (or Skipped for canonical-file matches).
struct BulkPlanResult {
    QList<BulkPlanItem>    items;                  // 1:1 with input.episodes
    QList<BulkPlanWarning> warnings;
    QString                showFolderName;         // "Show Name (year)" or "Show Name"
    QString                seasonFolderName;       // "Season NN"
    QString                showSeasonAbsolutePath; // <root>/<show>/<season>
};

// ── Identity-key helpers (audit Improvement 8) ────────────────────────────

// Stable item key for one planned episode. Format: "<seriesId>:S<NN>E<NN>".
// season + episode are zero-padded to 2 digits (S01E04, S15E09).
QString makeItemKey(const QString& seriesId, int season, int episode);

// Stable destination key — canonical relative path with forward slashes,
// suitable for QMap keys + cross-platform persistence. Format:
// "<showFolderName>/<seasonFolderName>/<canonicalFilename>".
QString makeDestinationKey(const QString& showFolderName,
                           const QString& seasonFolderName,
                           const QString& canonicalFilename);

// ── Forward sanitizer (audit A7) ──────────────────────────────────────────

// Single source of truth for converting raw text into a Windows-safe path
// segment. Used by canonical-filename construction, show-folder + season-
// folder names, and the skip-if-exists check. Per audit A7, the bulk
// planner MUST NOT use ScannerUtils::cleanMediaFolderTitle() — that's a
// scanner-side cleanup pass for read-back display, not a forward output
// naming policy.
//
// Behavior:
//   - Strips Windows-invalid chars: < > : " / \ | ? *
//   - Strips control characters (codepoints < 0x20)
//   - Replaces stripped runs with a single space
//   - Trims leading/trailing whitespace
//   - Strips trailing dots (Windows reserves)
//   - Collapses internal multi-whitespace runs to a single space
//   - Reserved Windows base names (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
//     get a trailing "_" appended to disambiguate
//   - Caps length to 200 chars (per-segment safety; full path budget is
//     a higher concern in Phase 1+)
//
// Empty / all-stripped input returns an empty string. Callers decide
// whether empty is fatal; buildBulkPlan emits an EmptySeriesTitle or
// EmptyEpisodeTitle warning rather than substituting a placeholder.
QString sanitizePathSegment(const QString& raw);

// ── Naming functions (Plex/Jellyfin canonical convention) ─────────────────

// Build the season folder name. Format: "Season <NN>" with 2-digit
// zero-padded number. season<1 returns "Season 00" + caller emits a
// SuspiciousEpisodeNumber warning.
QString buildSeasonFolderName(int season);

// Build the show folder name. When `year` is non-empty, appends " (year)".
// Both `showName` and `year` are sanitized via sanitizePathSegment.
//   buildShowFolderName("The Boys", "2019") -> "The Boys (2019)"
//   buildShowFolderName("The Boys", "")     -> "The Boys"
//   buildShowFolderName("", "2019")         -> "" (caller emits warning)
QString buildShowFolderName(const QString& showName, const QString& year);

// Build the canonical episode filename per Plex/Jellyfin convention:
// "<showName> - S<NN>E<NN> - <episodeTitle>.<ext>"
//
//   buildEpisodeFilename("The Boys", 3, 4, "Glorious Five Year Plan", "mkv")
//     -> "The Boys - S03E04 - Glorious Five Year Plan.mkv"
//
//   buildEpisodeFilename("The Boys", 3, 4, "", "mkv")
//     -> "The Boys - S03E04.mkv"           (empty title omits the segment)
//
//   buildEpisodeFilename("The Boys", 3, 4, "Title", "")
//     -> "The Boys - S03E04 - Title.mkv"   (empty extension defaults mkv)
//
// All segments are sanitized via sanitizePathSegment; the dot before the
// extension is hard-coded (extension itself is not sanitized beyond
// stripping a leading dot if the caller provided one).
QString buildEpisodeFilename(const QString& showName,
                             int season, int episode,
                             const QString& episodeTitle,
                             const QString& extension);

// ── Plan computation ──────────────────────────────────────────────────────

// Predicate the planner uses to decide whether a canonical destination
// already exists on disk. Phase 0's production caller passes a thin
// QFileInfo::exists() lambda; tests inject a fake over an in-memory
// path-set (Phase 0B scope).
using PathExistsFn = std::function<bool(const QString& absolutePath)>;

// Top-level Phase 0 entry point. Computes a BulkPlanResult from a
// BulkPlanInput. Pure — no network, no libtorrent, no filesystem I/O
// beyond the injected `existsFn` predicate. Phases 2/3 mutate the result
// in place to fill in source choices; this function's contract is "given
// these inputs and a way to check file existence, here is the plan."
//
// `existsFn` may be empty/null — in that case, the skip-if-exists check
// is a no-op (all items remain PendingSource). Production callers always
// supply one; tests use this null-default for sanitizer-only coverage.
BulkPlanResult buildBulkPlan(const BulkPlanInput& input,
                             const PathExistsFn& existsFn = {});

}  // namespace tankostream::stream
