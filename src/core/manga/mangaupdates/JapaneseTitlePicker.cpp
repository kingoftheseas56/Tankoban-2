#include "JapaneseTitlePicker.h"

namespace tankoban::manga::mangaupdates {

namespace {
bool containsJapanese(const QString& s)
{
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) ||  // CJK Unified Ideographs
            (u >= 0x3040 && u <= 0x309F) ||  // Hiragana
            (u >= 0x30A0 && u <= 0x30FF)) {  // Katakana
            return true;
        }
    }
    return false;
}
} // namespace

QString JapaneseTitlePicker::pickFirstJapanese(const QStringList& titles)
{
    for (const QString& t : titles) {
        if (containsJapanese(t)) return t;
    }
    return QString();
}

} // namespace tankoban::manga::mangaupdates
