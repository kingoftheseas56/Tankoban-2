#pragma once

#include <QString>
#include <QStringList>

namespace tankoban::manga::mangaupdates {

class JapaneseTitlePicker
{
public:
    // Scans the input list and returns the FIRST entry that contains any
    // CJK Unified Ideographs (U+4E00–U+9FFF), Hiragana (U+3040–U+309F),
    // or Katakana (U+30A0–U+30FF) character. Returns an empty QString if
    // no entry contains Japanese-script characters.
    //
    // Used by VolumeCoverResolver to extract the Japanese title from a
    // MangaUpdates `associated` array for BookWalker JP search input.
    static QString pickFirstJapanese(const QStringList& titles);
};

} // namespace tankoban::manga::mangaupdates
