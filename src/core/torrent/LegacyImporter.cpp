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
#include <QPair>
#include <QVector>
#include <QtGlobal>

#include <optional>

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

// ─── parseStreamGroups + parseStreamGroupItems — P1.3 (Jr 3) ─────────────────
//
// Two-parser-one-file design: both parsers walk the same legacy
// stream_bulk_groups.json document. parseStreamGroups returns one
// StreamGroupRow per top-level group; parseStreamGroupItems flattens every
// item across every group, attributing each item to its enclosing group via
// groupId. The shared open/parse helper lives in an anonymous namespace with
// prefixed names to avoid clashing with the other P1.x parsers' helpers in
// this TU.
//
// Field mapping is dual-shape tolerant:
//   - real legacy (TorrentClient::loadStreamBulkGroups at TorrentClient.cpp
//     §504, write path at TorrentClient.cpp:128 streamBulkGroupToJson) writes
//     `groups` as an OBJECT keyed by groupId, with nested sourceIds (seriesId
//     + season), itemKey, itemState, lastError, and qint64 createdAtMs;
//   - the plan §1.3 expected shape uses an ARRAY with flat field names
//     (imdbId/season/itemId/state/errorMessage/createdAt ISO + a packMode
//     boolean that the legacy writer doesn't emit).
// The parsers accept either shape — flat-named keys take precedence; nested
// legacy keys are the fallback. The new schema's group-level state +
// packMode fields come straight from JSON when present, defaulted otherwise.
//
// Verified live 2026-05-19 against ~/AppData/Local/Tankoban/data/
// stream_bulk_groups.json (11 groups including one Breaking Bad S01 cohort
// with 13 items in mixed Cancelled/Downloading/Pending states).

namespace {

// Open + JSON-parse the legacy file. Returns the root object on success, or
// std::nullopt on any read/parse failure. Missing file is silent (legacy stores
// may not exist on a fresh install); other failures append a warning.
std::optional<QJsonObject> openStreamGroupsJsonDoc(
    const QString& path,
    QStringList* warnings) {
    QFile file(path);
    if (!file.exists()) {
        return std::nullopt;  // silent skip — caller treats absence as no rows
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (warnings)
            warnings->append(QStringLiteral(
                "stream_bulk_groups.json: cannot open %1").arg(path));
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        if (warnings)
            warnings->append(QStringLiteral(
                "stream_bulk_groups.json: malformed JSON (%1)")
                    .arg(err.errorString()));
        return std::nullopt;
    }
    if (!doc.isObject()) {
        if (warnings)
            warnings->append(QStringLiteral(
                "stream_bulk_groups.json: top-level value is not an object"));
        return std::nullopt;
    }
    return doc.object();
}

// Extract (groupId, groupObject) pairs from the root document. Accepts both
// the array shape (plan §1.3 expected) and the object-keyed shape (real
// TorrentClient.cpp write path). Returns empty + appended warning when the
// "groups" key is absent. An EMPTY array or EMPTY object is valid (no
// warning, just no rows).
QVector<QPair<QString, QJsonObject>> extractStreamGroupsFromRoot(
    const QJsonObject& root,
    QStringList* warnings) {
    QVector<QPair<QString, QJsonObject>> out;
    if (!root.contains(QStringLiteral("groups"))) {
        if (warnings)
            warnings->append(QStringLiteral(
                "stream_bulk_groups.json: missing \"groups\" key"));
        return out;
    }
    const QJsonValue v = root.value(QStringLiteral("groups"));
    if (v.isArray()) {
        const QJsonArray arr = v.toArray();
        for (const auto& el : arr) {
            if (!el.isObject()) continue;
            const QJsonObject group = el.toObject();
            out.append({group.value(QStringLiteral("groupId")).toString(),
                        group});
        }
    } else if (v.isObject()) {
        const QJsonObject obj = v.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (!it.value().isObject()) continue;
            const QJsonObject group = it.value().toObject();
            QString gid = group.value(QStringLiteral("groupId")).toString();
            if (gid.isEmpty()) gid = it.key();
            out.append({gid, group});
        }
    } else {
        if (warnings)
            warnings->append(QStringLiteral(
                "stream_bulk_groups.json: \"groups\" value is neither array "
                "nor object"));
    }
    return out;
}

// Resolve createdAt from either ISO string (plan §1.3 expected) or epoch-ms
// long (real legacy createdAtMs). Returns an invalid QDateTime when neither
// is present; caller leaves row.createdAt default-constructed.
QDateTime resolveStreamGroupCreatedAt(const QJsonObject& group) {
    const QString iso = group.value(QStringLiteral("createdAt")).toString();
    if (!iso.isEmpty()) {
        QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
        if (dt.isValid()) return dt;
    }
    const QJsonValue msv = group.value(QStringLiteral("createdAtMs"));
    if (msv.isDouble()) {
        return QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(msv.toDouble()));
    }
    return QDateTime();
}

}  // namespace

std::vector<StreamGroupRow> LegacyImporter::parseStreamGroups(
    const QString& path,
    QStringList* warnings) {
    std::vector<StreamGroupRow> rows;
    const auto rootOpt = openStreamGroupsJsonDoc(path, warnings);
    if (!rootOpt) return rows;

    const auto groups = extractStreamGroupsFromRoot(*rootOpt, warnings);
    rows.reserve(static_cast<size_t>(groups.size()));
    for (const auto& pair : groups) {
        const QString& gid = pair.first;
        const QJsonObject& group = pair.second;

        StreamGroupRow row;
        row.groupId = gid;

        // Flat name first; nested sourceIds.* fallback for real legacy shape.
        row.imdbId = group.value(QStringLiteral("imdbId")).toString();
        if (row.imdbId.isEmpty()) {
            row.imdbId = group.value(QStringLiteral("sourceIds")).toObject()
                              .value(QStringLiteral("seriesId")).toString();
        }
        if (group.contains(QStringLiteral("season"))) {
            row.season = group.value(QStringLiteral("season")).toInt(0);
        } else {
            row.season = group.value(QStringLiteral("sourceIds")).toObject()
                              .value(QStringLiteral("season")).toInt(0);
        }
        row.label = group.value(QStringLiteral("label")).toString();
        row.state = group.value(QStringLiteral("state")).toString();
        row.retryGeneration =
            group.value(QStringLiteral("retryGeneration")).toInt(0);

        const QJsonValue spv = group.value(QStringLiteral("stagingPath"));
        row.stagingPath = spv.isNull() ? QString() : spv.toString();

        row.createdAt = resolveStreamGroupCreatedAt(group);
        row.packMode = group.value(QStringLiteral("packMode")).toBool(false);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<StreamGroupItemRow> LegacyImporter::parseStreamGroupItems(
    const QString& path,
    QStringList* warnings) {
    std::vector<StreamGroupItemRow> rows;
    const auto rootOpt = openStreamGroupsJsonDoc(path, warnings);
    if (!rootOpt) return rows;

    const auto groups = extractStreamGroupsFromRoot(*rootOpt, warnings);
    for (const auto& pair : groups) {
        const QString& gid = pair.first;
        const QJsonObject& group = pair.second;
        if (!group.contains(QStringLiteral("items"))) continue;

        const QJsonArray items =
            group.value(QStringLiteral("items")).toArray();
        for (const auto& iv : items) {
            if (!iv.isObject()) continue;
            const QJsonObject item = iv.toObject();

            StreamGroupItemRow ir;
            ir.groupId = gid;

            ir.itemId = item.value(QStringLiteral("itemId")).toString();
            if (ir.itemId.isEmpty())
                ir.itemId = item.value(QStringLiteral("itemKey")).toString();

            ir.episode = item.value(QStringLiteral("episode")).toInt(0);
            ir.infoHash = item.value(QStringLiteral("infoHash")).toString();

            ir.state = item.value(QStringLiteral("state")).toString();
            if (ir.state.isEmpty())
                ir.state = item.value(QStringLiteral("itemState")).toString();

            ir.errorMessage =
                item.value(QStringLiteral("errorMessage")).toString();
            if (ir.errorMessage.isEmpty())
                ir.errorMessage =
                    item.value(QStringLiteral("lastError")).toString();

            ir.fileIndex = item.value(QStringLiteral("fileIndex")).toInt(-1);
            rows.push_back(std::move(ir));
        }
    }
    return rows;
}

// ─── parseStreamDownloads — P1.4 (Jr 4, completed by Agent 4 parent-sweep) ───
//
// Reads the legacy stream_downloads.json on-disk schema written by
// StreamDownloadIndex::save() at src/core/stream/StreamDownloadIndex.cpp:104+:
//
//   { "version": 2,
//     "byPath": {
//       "<lowercase-canonical-path>": {
//         "imdbId": "tt...",
//         "type": "series" | "movie",
//         "season": N,
//         "episode": N,
//         "canonicalPath": "C:/Media/.../X.mkv",
//         "addedAt": <epoch in ms (registerEpisode) OR sec (registerPendingEpisode)>,
//         "sourceGroupId": "...",
//         "fileSizeBytes": N,
//         "state": 0|1|2|3,   // 0=complete 1=pending 2=downloading 3=failed
//         "progressPct": N
//       }
//     } }
//
// State int → string mapping (preserves the playback-index state vocab; not
// mapped onto TorrentState because this index has its own state language):
//   0 → "complete"   1 → "pending"   2 → "downloading"   3 → "failed"
//   anything else → "complete" + warn
//
// addedAt magnitude heuristic: values > 1e11 are treated as ms-since-epoch
// (year ≥ 1973 in ms); smaller positive values are treated as sec-since-epoch.
// Negative/zero → currentDateTimeUtc() + warn.
//
// infoHash is NOT in the legacy schema — leave row.infoHash empty (the new
// stream_downloads_index FK column accepts NULL via ON DELETE SET NULL).
//
// Error semantics match the other parsers:
//   - missing file        → empty vector, no warn (importInto handles at orchestrator)
//   - malformed JSON      → empty vector + warn
//   - missing byPath obj  → empty vector + warn
//   - per-entry anomaly   → skip entry + warn; remaining entries survive
std::vector<StreamDownloadRow> LegacyImporter::parseStreamDownloads(
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
        warn(QStringLiteral("stream_downloads.json: malformed JSON (%1)")
                 .arg(perr.errorString()));
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonValue byPathVal = root.value(QStringLiteral("byPath"));
    if (!byPathVal.isObject()) {
        warn(QStringLiteral("stream_downloads.json: top-level 'byPath' object missing"));
        return {};
    }
    const QJsonObject byPath = byPathVal.toObject();

    std::vector<StreamDownloadRow> rows;
    rows.reserve(static_cast<size_t>(byPath.size()));

    for (auto it = byPath.constBegin(); it != byPath.constEnd(); ++it) {
        const QString key = it.key();
        const QJsonValue entryVal = it.value();
        if (!entryVal.isObject()) {
            warn(QStringLiteral(
                     "stream_downloads.json: entry %1 is not an object")
                     .arg(key));
            continue;
        }
        const QJsonObject e = entryVal.toObject();

        StreamDownloadRow row;
        // canonical_path: prefer the entry's "canonicalPath" field (preserves
        // original case); fall back to the object key (which is lowercased).
        const QString canonical = e.value(QStringLiteral("canonicalPath")).toString();
        row.canonicalPath = canonical.isEmpty() ? key : canonical;
        row.imdbId  = e.value(QStringLiteral("imdbId")).toString();
        row.season  = e.value(QStringLiteral("season")).toInt(0);
        row.episode = e.value(QStringLiteral("episode")).toInt(0);

        // state int → string
        const QJsonValue stateVal = e.value(QStringLiteral("state"));
        if (stateVal.isDouble()) {
            const int rawState = stateVal.toInt(-1);
            switch (rawState) {
                case 0: row.state = QStringLiteral("complete");    break;
                case 1: row.state = QStringLiteral("pending");     break;
                case 2: row.state = QStringLiteral("downloading"); break;
                case 3: row.state = QStringLiteral("failed");      break;
                default:
                    row.state = QStringLiteral("complete");
                    warn(QStringLiteral(
                             "stream_downloads.json: unknown state %1 "
                             "for %2, defaulting to 'complete'")
                             .arg(rawState).arg(key));
                    break;
            }
        } else {
            row.state = QStringLiteral("complete");
        }

        // addedAt decode (numeric epoch — ms vs sec heuristic)
        const QJsonValue addedVal = e.value(QStringLiteral("addedAt"));
        if (addedVal.isDouble()) {
            const double rawAdded = addedVal.toDouble(0.0);
            if (rawAdded <= 0.0) {
                row.addedAt = QDateTime::currentDateTimeUtc();
                warn(QStringLiteral(
                         "stream_downloads.json: non-positive addedAt %1 "
                         "for %2, defaulting to now")
                         .arg(rawAdded).arg(key));
            } else if (rawAdded > 1.0e11) {
                row.addedAt = QDateTime::fromMSecsSinceEpoch(
                    static_cast<qint64>(rawAdded), Qt::UTC);
            } else {
                row.addedAt = QDateTime::fromSecsSinceEpoch(
                    static_cast<qint64>(rawAdded), Qt::UTC);
            }
        } else {
            row.addedAt = QDateTime::currentDateTimeUtc();
            warn(QStringLiteral(
                     "stream_downloads.json: missing/non-numeric addedAt "
                     "for %1, defaulting to now")
                     .arg(key));
        }

        // infoHash absent in legacy schema — leave empty
        rows.push_back(std::move(row));
    }

    return rows;
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
