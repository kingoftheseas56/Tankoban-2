// src/core/manga/CanonicalChapterKey.h
#pragma once

#include <QString>

namespace tankoban::manga::premium {

// Stable identity for a chapter across migrations (loose WeebCentral chapter
// later receives volume attribution, etc.). See brainstorm section 23.
//
// Format: "<seriesId>:ch_<chapterNumber>"
// Examples:
//   make("one_piece", "1146")  -> "one_piece:ch_1146"
//   make("berserk",   "234.5") -> "berserk:ch_234.5"
//
// Independent of source URL, independent of file path. Whatever cbz currently
// holds chapter 1146 of One Piece, the canonical key resolves the same way.
inline QString canonicalChapterKey(const QString& seriesId,
                                   const QString& chapterNumber)
{
    return seriesId + QStringLiteral(":ch_") + chapterNumber;
}

} // namespace tankoban::manga::premium
