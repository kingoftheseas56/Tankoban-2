#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/stream/addon/StreamInfo.h"

namespace tankostream::stream {

// Stream picker choice — the payload StreamPage consumes after the user
// selects a stream card. Previously owned by StreamPickerDialog; moved here
// when the modal dialog was replaced with the inline source panel so the
// struct survives the dialog's deletion and both the panel widgets and
// StreamPage can share one definition.
//
// Display fields (prefixed `display*` + `badges`/`sizeBytes`/`seeders`) are
// populated by `buildPickerChoices` and consumed by `StreamSourceCard`.
// StreamPage only cares about `stream` + `addonId` + `addonName` + the
// magnet-dispatch block; the display fields are there so the card doesn't
// have to reach back into `stream.behaviorHints.other` for everything.
struct StreamPickerChoice {
    tankostream::addon::Stream stream;
    QString addonId;
    QString addonName;
    QString sourceKind;        // "magnet" / "http" / "url" / "youtube"

    QString magnetUri;
    QString infoHash;
    int     fileIndex = -1;
    QString fileNameHint;

    // ── UI display fields ────────────────────────────────────────────────
    QString     displayTitle;     // bold top line — usually addonName, or "Direct"
    QString     displayFilename;  // middle line — parsedFilename / fileNameHint / desc
    QString     displayQuality;   // right-aligned pill — "1080p", "4K HDR", "-"
    qint64      sizeBytes = 0;    // raw; card formats via humanSize
    int         seeders   = 0;    // magnet only; -1 for non-magnet (rendered as "-")
    QStringList badges;           // ["HDR"], ["DV"], ["MULTI-SUB"], etc.
    QString     trackerSource;    // small-caps footer hint when populated
    bool        isDirect  = false; // true for HTTP/URL direct streams
    int         qualitySort = 0;   // 5=2160p, 4=1440p, 3=1080p, 2=720p, 1=480p, 0=unknown
};

// Build the full sorted picker-choice list from aggregator output.
// Sort order: magnets-with-seeders first (by seeder count desc), then by
// quality desc, then by size desc, then by title asc — matches the legacy
// StreamPickerDialog ordering so user expectations are preserved.
QList<StreamPickerChoice> buildPickerChoices(
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>&           addonsById);

// Human-readable size (e.g. "3.9 GB"). Exposed so both the card and any
// future list-header summary can share one formatter.
QString humanSize(qint64 bytes);

// Build a canonical StreamChoice-JSON-compatible key for matching a
// saved-choice record back to its picker card. Mirrors the shape used in
// StreamPage.cpp when saving (addonId + sourceKind + infoHash/url + fileIndex).
QString pickerChoiceKey(const StreamPickerChoice& choice);

}
