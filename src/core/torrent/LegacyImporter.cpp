// LegacyImporter implementation (skeleton).
//
// P1.0 skeleton commit — stubs all parser bodies so the four Trigger E Jrs
// can implement one parser each in parallel without colliding on this
// translation unit's namespace + symbol declarations.
//
// Bodies filled by:
//   P1.1 — parseTorrentsJson      (Jr 1)
//   P1.2 — loadResumeBlob         (Jr 2)
//   P1.3 — parseStreamGroups + parseStreamGroupItems (Jr 3)
//   P1.4 — parseStreamDownloads   (Jr 4)
//   P1.5 — importInto             (in-line, after parser fanout)
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md
// Phase 1.

#include "LegacyImporter.h"

#include "TorrentRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QtGlobal>

namespace tankoban::torrent {

// ─── parseTorrentsJson — P1.1 (Jr 1) ─────────────────────────────────────────
std::vector<TorrentRow> LegacyImporter::parseTorrentsJson(
    const QString& path,
    QStringList* warnings) {
    Q_UNUSED(path);
    Q_UNUSED(warnings);
    return {};
}

// ─── loadResumeBlob — P1.2 (Jr 2) ────────────────────────────────────────────
QByteArray LegacyImporter::loadResumeBlob(
    const QString& resumeCacheDir,
    const QString& hash) {
    Q_UNUSED(resumeCacheDir);
    Q_UNUSED(hash);
    return {};
}

// ─── parseStreamGroups — P1.3 (Jr 3) ─────────────────────────────────────────
std::vector<StreamGroupRow> LegacyImporter::parseStreamGroups(
    const QString& path,
    QStringList* warnings) {
    Q_UNUSED(path);
    Q_UNUSED(warnings);
    return {};
}

// ─── parseStreamGroupItems — P1.3 (Jr 3) ─────────────────────────────────────
std::vector<StreamGroupItemRow> LegacyImporter::parseStreamGroupItems(
    const QString& path,
    QStringList* warnings) {
    Q_UNUSED(path);
    Q_UNUSED(warnings);
    return {};
}

// ─── parseStreamDownloads — P1.4 (Jr 4) ──────────────────────────────────────
std::vector<StreamDownloadRow> LegacyImporter::parseStreamDownloads(
    const QString& path,
    QStringList* warnings) {
    Q_UNUSED(path);
    Q_UNUSED(warnings);
    return {};
}

// ─── importInto — P1.5 (in-line) ─────────────────────────────────────────────
LegacyImportSummary LegacyImporter::importInto(
    TorrentRepository& repo,
    const LegacySources& sources) {
    Q_UNUSED(repo);
    Q_UNUSED(sources);
    LegacyImportSummary summary;
    summary.warnings.append(
        QStringLiteral("LegacyImporter::importInto not yet implemented (P1.5 pending)"));
    return summary;
}

} // namespace tankoban::torrent
