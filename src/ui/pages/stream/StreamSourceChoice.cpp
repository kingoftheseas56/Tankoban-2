#include "StreamSourceChoice.h"

#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

using tankostream::addon::Stream;
using tankostream::addon::StreamSource;

namespace tankostream::stream {

namespace {

QString prettyKind(StreamSource::Kind kind)
{
    switch (kind) {
    case StreamSource::Kind::Magnet:  return QStringLiteral("magnet");
    case StreamSource::Kind::Http:    return QStringLiteral("http");
    case StreamSource::Kind::Url:     return QStringLiteral("url");
    case StreamSource::Kind::YouTube: return QStringLiteral("youtube");
    }
    return QStringLiteral("url");
}

int qualityRank(const QString& qualityText)
{
    const QString q = qualityText.toLower();
    if (q.contains(QStringLiteral("2160")) || q.contains(QStringLiteral("4k"))) return 5;
    if (q.contains(QStringLiteral("1440"))) return 4;
    if (q.contains(QStringLiteral("1080"))) return 3;
    if (q.contains(QStringLiteral("720")))  return 2;
    if (q.contains(QStringLiteral("480")))  return 1;
    return 0;
}

qint64 extractSizeBytes(const Stream& stream)
{
    if (stream.behaviorHints.other.contains(QStringLiteral("sizeBytes"))) {
        return stream.behaviorHints.other.value(QStringLiteral("sizeBytes")).toLongLong();
    }
    return stream.behaviorHints.videoSize;
}

int extractSeeders(const Stream& stream)
{
    return stream.behaviorHints.other.value(QStringLiteral("seeders")).toInt(0);
}

QString extractQuality(const Stream& stream)
{
    const QString q = stream.behaviorHints.other
                          .value(QStringLiteral("qualityLabel")).toString().trimmed();
    if (!q.isEmpty()) return q;

    // Fallback: grep the free-text fields for a resolution token.
    static const QRegularExpression kResolutionRe(
        QStringLiteral("\\b(2160p|1080p|720p|480p|4k)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = kResolutionRe.match(stream.name + QLatin1Char(' ') + stream.description);
    if (m.hasMatch()) return m.captured(1).toUpper();
    return QStringLiteral("-");
}

// Scan title + description + parsed filename for HDR / Dolby Vision / multi-sub
// markers. Order matches the badge priority in the card layout (HDR first).
QStringList extractBadges(const Stream& stream, const QString& qualityLabel)
{
    const QString haystack = (stream.name + QLatin1Char(' ') + stream.description
                             + QLatin1Char(' ') + qualityLabel).toLower();
    QStringList badges;

    // Avoid double-tagging "HDR" when the quality pill already carries it.
    const bool qualityHasHdr = qualityLabel.contains(QStringLiteral("HDR"),
                                                     Qt::CaseInsensitive);
    const bool qualityHasDv  = qualityLabel.contains(QStringLiteral("DV"),
                                                     Qt::CaseInsensitive)
                            || qualityLabel.contains(QStringLiteral("DOVI"),
                                                     Qt::CaseInsensitive);

    if (!qualityHasHdr && haystack.contains(QStringLiteral("hdr")))
        badges << QStringLiteral("HDR");
    if (!qualityHasDv && (haystack.contains(QStringLiteral("dolby vision"))
                          || haystack.contains(QStringLiteral("dovi"))))
        badges << QStringLiteral("DV");
    if (haystack.contains(QStringLiteral("10bit"))
     || haystack.contains(QStringLiteral("10-bit")))
        badges << QStringLiteral("10BIT");
    if (haystack.contains(QStringLiteral("multi sub"))
     || haystack.contains(QStringLiteral("multi-sub"))
     || haystack.contains(QStringLiteral("multisub")))
        badges << QStringLiteral("MULTI-SUB");
    return badges;
}

QString buildMagnetUri(const Stream& stream)
{
    if (stream.source.kind != StreamSource::Kind::Magnet
     || stream.source.infoHash.isEmpty()) return {};

    QString uri = QStringLiteral("magnet:?xt=urn:btih:") + stream.source.infoHash.toLower();
    for (const QString& tracker : stream.source.trackers) {
        uri += QStringLiteral("&tr=")
             + QString::fromUtf8(QUrl::toPercentEncoding(tracker));
    }
    return uri;
}

// Pick the best available "filename" to display on the card middle line.
// Priority: parsedFilename (from the Torrentio enrichment) → source.fileNameHint
// (Stremio spec field or already-promoted parsedFilename) → behaviorHints.filename
// → stream.description → stream.name → "(untitled stream)".
QString bestFilename(const Stream& stream)
{
    const QString parsed = stream.behaviorHints.other
                               .value(QStringLiteral("parsedFilename")).toString().trimmed();
    if (!parsed.isEmpty()) return parsed;

    if (!stream.source.fileNameHint.isEmpty()) return stream.source.fileNameHint;
    if (!stream.behaviorHints.filename.isEmpty()) return stream.behaviorHints.filename;
    if (!stream.description.isEmpty()) return stream.description;
    if (!stream.name.isEmpty()) return stream.name;
    return QStringLiteral("(untitled stream)");
}

// Resolve the best addon label. Aggregator threads addon metadata through
// behaviorHints.other["originAddonId"/"originAddonName"]; fall back to the
// caller-provided addonsById map, then to a generic "Unknown addon" string.
void resolveAddonLabel(const Stream& stream,
                       const QHash<QString, QString>& addonsById,
                       QString& outId,
                       QString& outName)
{
    outId = stream.behaviorHints.other
                .value(QStringLiteral("originAddonId")).toString().trimmed();
    outName = stream.behaviorHints.other
                  .value(QStringLiteral("originAddonName")).toString().trimmed();
    if (outName.isEmpty() && addonsById.contains(outId)) {
        outName = addonsById.value(outId);
    }
    if (outName.isEmpty()) {
        outName = QStringLiteral("Unknown addon");
    }
}

}

QString humanSize(qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("-");
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double val = static_cast<double>(bytes);
    while (val >= 1024.0 && i < 4) {
        val /= 1024.0;
        ++i;
    }
    return QString::number(val, 'f', i > 0 ? 1 : 0) + QLatin1Char(' ') + units[i];
}

QString pickerChoiceKey(const StreamPickerChoice& choice)
{
    return choice.addonId + QLatin1Char('|')
         + choice.sourceKind + QLatin1Char('|')
         + (choice.sourceKind == QLatin1String("magnet")
                ? choice.infoHash.toLower()
                : choice.stream.source.url.toString(QUrl::FullyEncoded))
         + QLatin1Char('|')
         + QString::number(choice.fileIndex);
}

QList<StreamPickerChoice> buildPickerChoices(
    const QList<Stream>&            streams,
    const QHash<QString, QString>&  addonsById)
{
    QList<StreamPickerChoice> out;
    out.reserve(streams.size());

    for (const Stream& stream : streams) {
        StreamPickerChoice c;
        c.stream      = stream;
        c.sourceKind  = prettyKind(stream.source.kind);
        c.infoHash    = stream.source.infoHash;
        c.fileIndex   = stream.source.fileIndex;
        c.fileNameHint = stream.source.fileNameHint;
        c.magnetUri   = buildMagnetUri(stream);
        c.isDirect    = (stream.source.kind == StreamSource::Kind::Http
                      || stream.source.kind == StreamSource::Kind::Url);

        resolveAddonLabel(stream, addonsById, c.addonId, c.addonName);

        c.displayTitle    = c.isDirect ? QStringLiteral("Direct") : c.addonName;
        c.displayFilename = bestFilename(stream);
        c.displayQuality  = extractQuality(stream);
        c.sizeBytes       = extractSizeBytes(stream);
        c.seeders         = (stream.source.kind == StreamSource::Kind::Magnet)
                                ? extractSeeders(stream)
                                : -1;
        c.badges          = extractBadges(stream, c.displayQuality);
        c.trackerSource   = stream.behaviorHints.other
                                .value(QStringLiteral("trackerSource")).toString().trimmed();
        c.qualitySort     = qualityRank(c.displayQuality);

        out.push_back(c);
    }

    // Same sort policy the legacy dialog used — preserves muscle memory for
    // any user with a saved choice that was top-of-list before.
    std::stable_sort(out.begin(), out.end(),
        [](const StreamPickerChoice& a, const StreamPickerChoice& b) {
            const bool aMagWithSeeders =
                a.stream.source.kind == StreamSource::Kind::Magnet && a.seeders > 0;
            const bool bMagWithSeeders =
                b.stream.source.kind == StreamSource::Kind::Magnet && b.seeders > 0;
            if (aMagWithSeeders != bMagWithSeeders)            return aMagWithSeeders;
            if (aMagWithSeeders && bMagWithSeeders
                && a.seeders != b.seeders)                     return a.seeders > b.seeders;
            if (a.qualitySort != b.qualitySort)                return a.qualitySort > b.qualitySort;
            if (a.sizeBytes != b.sizeBytes)                    return a.sizeBytes > b.sizeBytes;
            return a.displayFilename.toLower() < b.displayFilename.toLower();
        });

    return out;
}

}
