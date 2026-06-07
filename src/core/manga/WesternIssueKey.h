#pragma once
#include <QString>
#include <QRegularExpression>

namespace tankoban::manga {

// A western (readallcomics) issue cbz is named "<Series> #<N>". Manga cbzs use
// "Volume N" / "vNN" and never this " #<digit>" form, so this is the single
// safe marker that the Manga strips EXCLUDE and the Western strips INCLUDE.
// `basename` is the cbz completeBaseName (no extension, no path).
inline bool isWesternIssueCbz(const QString& basename) {
    static const QRegularExpression re(QStringLiteral(" #\\d"));
    return re.match(basename).hasMatch();
}

// Returns the issue number from a western issue basename, or -1 if it isn't one.
inline int westernIssueNumber(const QString& basename) {
    static const QRegularExpression re(QStringLiteral(" #(\\d+)"));
    const auto m = re.match(basename);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

} // namespace tankoban::manga
