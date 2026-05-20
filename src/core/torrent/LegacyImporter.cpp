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
#include <QJsonParseError>
#include <QDateTime>
#include <QtGlobal>

namespace tankoban::torrent {

// ─── parseTorrentsJson — P1.1 (Jr 1) ─────────────────────────────────────────
//
// Reads the legacy torrents.json shape:
//
//   { "active": { "<lowercase-hash>": { state, name, addedAt, category,
//                                       savePath, contentLayout,
//                                       streamGroupId, sequential, imdbId,
//                                       season, errorMessage, ... } } }
//
// Verified live 2026-05-19 against ~/AppData/Local/Tankoban/data/torrents.json
// (21 rows). Notably the legacy schema has NO `magnetUri` field on ANY current
// row — so legacy_no_magnet is true for every imported legacy row in
// practice. Per audit Part C + plan D6 + D10: importer still stores the row
// (preserving user history) and flags it so the UI can surface the
// "Needs re-add" recovery affordance.
//
// State string mapping (legacy → enum):
//   "downloading" → Active        (the live, transferring state)
//   "queued"      → Active        (libtorrent queue state collapses to Active
//                                  in our enum — see plan §D3)
//   "paused"      → Paused
//   "completed"   → Completed
//   "error"       → Error
//   empty/unknown → Active + warning  (defensive default so the row still
//                                      lands; the warning lets the operator
//                                      see what slipped through)
//
// Error semantics (plan §1.1 requirements):
//   - file missing / unreadable  → return empty vector, NO warning raised
//                                  (importInto is expected to skip-and-warn
//                                  at the orchestrator level instead)
//   - malformed JSON             → empty vector + warning
//   - missing/non-object active  → empty vector + warning
//   - per-row parse anomalies    → row still emitted, warning appended
std::vector<TorrentRow> LegacyImporter::parseTorrentsJson(
    const QString& path,
    QStringList* warnings) {
    const auto warn = [warnings](const QString& msg) {
        if (warnings) warnings->append(msg);
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        warn(QStringLiteral("torrents.json: malformed JSON (%1)")
                 .arg(perr.errorString()));
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonValue activeVal = root.value(QStringLiteral("active"));
    if (!activeVal.isObject()) {
        warn(QStringLiteral("torrents.json: top-level 'active' object missing"));
        return {};
    }
    const QJsonObject active = activeVal.toObject();

    std::vector<TorrentRow> rows;
    rows.reserve(static_cast<size_t>(active.size()));

    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
        const QString hashKey = it.key();
        const QJsonValue entryVal = it.value();
        if (!entryVal.isObject()) {
            warn(QStringLiteral("torrents.json: entry for hash %1 is not an object")
                     .arg(hashKey));
            continue;
        }
        const QJsonObject e = entryVal.toObject();

        TorrentRow row;
        row.hash = hashKey.toLower();

        // ── state mapping with aliases ──────────────────────────────────────
        const QString rawState = e.value(QStringLiteral("state")).toString();
        if (rawState == QStringLiteral("downloading") ||
            rawState == QStringLiteral("queued") ||
            rawState == QStringLiteral("active")) {
            row.state = TorrentState::Active;
        } else if (rawState == QStringLiteral("paused")) {
            row.state = TorrentState::Paused;
        } else if (rawState == QStringLiteral("completed")) {
            row.state = TorrentState::Completed;
        } else if (rawState == QStringLiteral("error")) {
            row.state = TorrentState::Error;
        } else {
            row.state = TorrentState::Active;
            warn(QStringLiteral(
                     "torrents.json: unrecognized state '%1' for hash %2")
                     .arg(rawState, row.hash));
        }

        row.name           = e.value(QStringLiteral("name")).toString();
        row.category       = e.value(QStringLiteral("category")).toString();
        row.savePath       = e.value(QStringLiteral("savePath")).toString();
        row.contentLayout  = e.value(QStringLiteral("contentLayout")).toString();
        row.streamGroupId  = e.value(QStringLiteral("streamGroupId")).toString();
        row.imdbId         = e.value(QStringLiteral("imdbId")).toString();
        row.errorMessage   = e.value(QStringLiteral("errorMessage")).toString();
        row.season         = e.value(QStringLiteral("season")).toInt(0);
        row.sequential     = e.value(QStringLiteral("sequential")).toBool(false);

        const QString addedRaw = e.value(QStringLiteral("addedAt")).toString();
        QDateTime added = QDateTime::fromString(addedRaw, Qt::ISODate);
        if (!added.isValid()) {
            added = QDateTime::currentDateTimeUtc();
            warn(QStringLiteral(
                     "torrents.json: invalid addedAt '%1' for hash %2, "
                     "defaulting to now")
                     .arg(addedRaw, row.hash));
        }
        row.addedAt = added;

        // magnetUri is absent on every currently-known legacy row; capture
        // the flag so the UI surfaces "Needs re-add" per plan §D10.
        row.magnetUri      = e.value(QStringLiteral("magnetUri")).toString();
        row.legacyNoMagnet = row.magnetUri.isEmpty();

        rows.push_back(std::move(row));
    }

    return rows;
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
