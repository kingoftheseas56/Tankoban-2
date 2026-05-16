#pragma once

#include <QCryptographicHash>
#include <QString>

// Shared "cbz absolute path → progress-key" contract for Comics-mode reading
// progress. Used by:
//   - ComicReader::itemIdForPath (delegates here) — writes saveProgress with this key
//   - ComicsPage::ensureTankoyomiChapterInMap — registers a Tankoyomi cbz under
//     the same key just-in-time before reading begins
//   - ComicsPage::refreshContinueStrip — reads the key back via m_bridge->allProgress
//
// Contract: SHA1(utf8(path)).hex().left(20). Same algorithm callers used
// historically — promoted to a free helper to prevent silent drift.
//
// Forward-compat note (TANKOYOMI_CONTINUE_READING design 2026-05-15): if Comics
// mode pivots to Tankoyomi-exclusive in the future and m_progressKeyMap goes
// away in favour of a direct MangaDownloadIndex ↔ JsonStoreBridge join, this
// helper STAYS — it's the persistence-side contract, not a map-side artefact.
inline QString comicProgressKeyForPath(const QString& cbzPath)
{
    return QString(QCryptographicHash::hash(
        cbzPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}
