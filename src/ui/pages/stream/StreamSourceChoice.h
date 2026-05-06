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
    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — displayTitle semantic
    // changed: WAS the addon name (Stremio shows release name primary,
    // we were showing "Torrentio" on every card — useless as
    // disambiguation when most results come from one addon). Now carries
    // the extracted release name (e.g. "The.Boys.S03E04.1080p..."); the
    // addon name moves to the card's footer line via addonName above.
    // For direct streams with no resolvable release name, falls back to
    // "Direct stream" so the row still reads.
    QString     displayTitle;     // primary line — release name (addon-agnostic)
    QString     displayQuality;   // right-aligned pill — "1080p", "4K HDR", "-"
    qint64      sizeBytes = 0;    // raw; card formats via humanSize
    int         seeders   = 0;    // magnet only; -1 for non-magnet (rendered as "-")
    QStringList badges;           // ["HDR"], ["DV"], ["MULTI-SUB"], etc.
    QString     trackerSource;    // small-caps footer hint when populated
    bool        isDirect  = false; // true for HTTP/URL direct streams
    int         qualitySort = 0;   // 5=2160p, 4=1440p, 3=1080p, 2=720p, 1=480p, 0=unknown

    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — release-shape chip data,
    // Stremio parity. Surfaced inline on the chip row (between title and
    // footer) so a glance at a card answers "single episode? season pack?
    // full series?" without parsing the release name. Empty when no
    // pattern matches (e.g. movies — no S/E shape to detect, no chip
    // shown). detectPackType is the populator; see StreamSourceChoice.cpp.
    QString     packType;         // "" | "episode" | "season" | "series"
    QString     packLabel;        // "" | "S03E04" | "Season 3" | "Complete Series"
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
