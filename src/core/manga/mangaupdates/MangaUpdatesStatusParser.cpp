#include "MangaUpdatesStatusParser.h"

#include <QRegularExpression>

namespace tankoban::manga::mangaupdates {

int MangaUpdatesStatusParser::parseLeadingVolumeCount(const QString& status)
{
    if (status.isEmpty()) return 0;

    static const QRegularExpression re(
        QStringLiteral("(\\d+)\\s+Volume[s]?\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(status);
    if (!match.hasMatch()) return 0;

    bool ok = false;
    const int count = match.captured(1).toInt(&ok);
    return ok ? count : 0;
}

} // namespace tankoban::manga::mangaupdates
