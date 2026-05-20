#pragma once

// LegacyImporter — one-shot migration from Tankoban's pre-collapse persistence
// surfaces into the new TorrentRepository.
//
// Reads four legacy sources verbatim, normalises them into typed row structs,
// and writes them into the repository inside a single transaction:
//
//   1. torrents.json                    → TorrentRow vector
//   2. torrent_cache/resume/<hash>.fastresume → resume blob bytes
//   3. stream_bulk_groups.json          → StreamGroupRow + StreamGroupItemRow
//                                          vectors
//   4. stream_downloads.json            → StreamDownloadRow vector
//
// Phase 1 of TORRENT_PERSISTENCE_COLLAPSE. Per-source parsers are unit-tested
// in isolation with fixture inputs (no live filesystem state required).
//
// Integration: TorrentClient::start() invokes importInto() on first boot when
// torrents.db is absent but legacy JSON files exist. Legacy files are not
// deleted; they get renamed to .legacy-imported-*.bak after two clean reboots
// (D8 retention window).
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase 1
// and agents/audits/torrent_persistence_collapse_2026-05-19.md Part C for
// migration design rationale.

#include "TorrentRow.h"

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <vector>

namespace tankoban::torrent {

class TorrentRepository;

// File paths the importer reads. All four can be empty strings — the importer
// skips missing files without failing (importInto reports the skip via the
// summary warnings list).
struct LegacySources {
    QString torrentsJsonPath;
    QString streamBulkGroupsJsonPath;
    QString streamDownloadsJsonPath;
    QString resumeCacheDir;   // directory containing <hash>.fastresume files
};

// Result of a full import pass.
struct LegacyImportSummary {
    int torrentsImported          = 0;
    int torrentsLegacyNoMagnet    = 0;  // rows with empty magnet_uri at import
    int streamGroupsImported      = 0;
    int streamGroupItemsImported  = 0;
    int streamDownloadsImported   = 0;
    int resumeBlobsAttached       = 0;
    QStringList warnings;
};

class LegacyImporter {
public:
    LegacyImporter() = default;

    // ─── per-source parsers ─────────────────────────────────────────────────
    // Each parser:
    //   - reads from `path`,
    //   - returns a vector of typed rows (empty on missing/unreadable file),
    //   - appends parse anomalies to *warnings if provided (no aborts).
    //
    // Implementations land via Trigger E fanout — see Wave 3 prompts for
    // ownership per parser:
    //   P1.1 — parseTorrentsJson      (Jr 1)
    //   P1.2 — loadResumeBlob         (Jr 2)
    //   P1.3 — parseStreamGroups,
    //          parseStreamGroupItems  (Jr 3)
    //   P1.4 — parseStreamDownloads   (Jr 4)

    std::vector<TorrentRow> parseTorrentsJson(
        const QString& path,
        QStringList* warnings = nullptr);

    QByteArray loadResumeBlob(
        const QString& resumeCacheDir,
        const QString& hash);

    std::vector<StreamGroupRow> parseStreamGroups(
        const QString& path,
        QStringList* warnings = nullptr);

    std::vector<StreamGroupItemRow> parseStreamGroupItems(
        const QString& path,
        QStringList* warnings = nullptr);

    std::vector<StreamDownloadRow> parseStreamDownloads(
        const QString& path,
        QStringList* warnings = nullptr);

    // ─── orchestrator ───────────────────────────────────────────────────────
    // Runs the full import inside a single repository transaction. Stamps
    // schema_meta `migration_completed_at` on commit. Caller must repo.open()
    // BEFORE invoking.
    //
    // Implementation lands in P1.5 (in-line after parser fanout).

    LegacyImportSummary importInto(
        TorrentRepository& repo,
        const LegacySources& sources);
};

} // namespace tankoban::torrent
