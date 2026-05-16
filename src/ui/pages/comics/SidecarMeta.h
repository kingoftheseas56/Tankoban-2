#pragma once

#include <QString>
#include <optional>

// Reads and writes the hidden .tankoyomi-meta.json sidecar inside each
// Tankoyomi-origin series folder on disk. The sidecar is a RECOVERY
// HINT, not a source of truth (per Codex §16 invariant). It exists so
// that:
//   1. LibraryScanner can skip a folder it has already claimed
//      without round-tripping to comics_library.json on every walk.
//   2. Folder-renames and root-moves can be reconciled by scanning
//      for sidecar (sourceId, seriesId) tuples and re-pointing the
//      library record to the new path.
//
// File name uses a leading dot so it's hidden on POSIX; on Windows the
// dot does not hide but it stays out of the user's typical view.
struct SidecarMeta {
    QString sourceId;
    QString seriesId;
    QString title;
    qint64  createdAt = 0;
    int     schemaVersion = 1;
};

namespace sidecar {

constexpr const char* kFileName = ".tankoyomi-meta.json";

// Read meta from <seriesFolder>/.tankoyomi-meta.json. Returns
// nullopt if missing or malformed.
std::optional<SidecarMeta> read(const QString& seriesFolder);

// Write meta into <seriesFolder>/.tankoyomi-meta.json. Returns
// true on success.
bool write(const QString& seriesFolder, const SidecarMeta& meta);

// True iff the sidecar file exists at the expected path.
bool exists(const QString& seriesFolder);

} // namespace sidecar
