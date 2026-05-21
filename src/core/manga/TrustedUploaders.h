#pragma once

// TrustedUploaders — case-insensitive, whitespace-trimmed whitelist of
// uploader names whose Fandom-sourced volume packs we vouch for. Drives
// the "Trusted uploader" badge surface on ComicsSourceCard / sources panel.
//
// Seeded 2026-05-20 (Task 3 of Comics Series Page Polish) with three
// initial trusted uploaders. Canonical forms are stored lowercase; lookup
// trims whitespace + lowercases the candidate before matching so callers
// can pass raw scraped strings without pre-normalisation.
//
// All methods are static — the class is a namespace-with-state shim around
// a const-initialised set. No instances, no mutable state, no Qt-meta
// registration needed.

#include <QString>
#include <QStringList>

namespace tankoban::manga {

class TrustedUploaders {
public:
    // Returns true if the given uploader name (after whitespace-trim +
    // case-fold) matches one of the canonical trusted names.
    // Empty / whitespace-only input returns false.
    static bool isTrusted(QString uploader);

    // Returns the canonical (lowercase) list of trusted uploader names,
    // sorted in insertion order. Useful for UI surfaces that want to
    // render the whitelist (e.g., a "Trusted uploaders" settings page).
    static QStringList names();
};

} // namespace tankoban::manga
