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
//
// Reads a single <hash.toLower()>.fastresume file from the legacy resume cache
// directory. Filename convention matches TorrentEngine.cpp:154-156 where the
// engine writes resume blobs as `<m_cacheDir>/resume/<hash>.fastresume` with
// `hash = TorrentEngine::hashToHex(handle)` — and `hashToHex` always emits
// lowercase via `%02x` formatting. The .toLower() at this boundary makes the
// reader tolerant of callers that hand us the hash in any case.
//
// Silent-fail contract: every failure mode (missing file, unreadable file,
// missing or non-existent directory, empty file) collapses to an empty
// QByteArray return. The importer treats an empty blob the same as
// "no resume data" — libtorrent will rehash the content on next add — so
// there is no value in surfacing IO errors here. importInto's summary
// tallies resumeBlobsAttached only when the blob is non-empty.
QByteArray LegacyImporter::loadResumeBlob(
    const QString& resumeCacheDir,
    const QString& hash) {
    QFile file(QDir(resumeCacheDir).filePath(hash.toLower() + QStringLiteral(".fastresume")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray bytes = file.readAll();
    file.close();
    return bytes;
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
