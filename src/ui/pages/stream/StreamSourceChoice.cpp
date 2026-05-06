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

// STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — replaced bestFilename with
// stricter extractReleaseName contract: returns ONE LINE suitable for the
// card's primary identifier label (Stremio parity).
//
// Priority (addon-agnostic — Torrentio, Comet, MediaFusion, Jackett-relayed
// addons all emit roughly this shape, with the exact field varying per
// addon-version):
//   1. behaviorHints.other["parsedFilename"] — Torrentio-style enrichment;
//      already a clean single line.
//   2. source.fileNameHint — Stremio spec field; already clean.
//   3. behaviorHints.filename — Stremio spec field, alternate carrier.
//   4. stream.name — addon-supplied; reject if it's just an addon brand
//      tag with optional resolution echo ("Torrentio", "Torrentio 1080p")
//      since that's the failure mode we're fixing. Otherwise take
//      first line.
//   5. stream.description first line — Torrentio packs the release name
//      on line 1 with size/seeders/source on subsequent lines marked by
//      glyphs (💾 4.2 GB, 👤 152, etc). Reject the first line if it's
//      ITSELF a metadata row (starts with a glyph or "Size:"/"Seeders:").
//   6. "(unnamed release)" — distinct from the legacy "(untitled stream)"
//      so smoke can grep for the new failure mode separately.
QString extractReleaseName(const Stream& stream)
{
    auto firstLine = [](const QString& s) -> QString {
        const int nl = s.indexOf(QLatin1Char('\n'));
        return (nl < 0 ? s : s.left(nl)).trimmed();
    };

    const QString parsed = stream.behaviorHints.other
                               .value(QStringLiteral("parsedFilename")).toString().trimmed();
    if (!parsed.isEmpty()) return firstLine(parsed);

    if (!stream.source.fileNameHint.isEmpty())
        return firstLine(stream.source.fileNameHint);
    if (!stream.behaviorHints.filename.isEmpty())
        return firstLine(stream.behaviorHints.filename);

    // stream.name — reject if it's just the addon brand. The pattern
    // captures "Torrentio", "Comet", "MediaFusion", "Cinemeta" optionally
    // followed by a resolution token ("Torrentio 1080p"). Anchored so
    // longer names like "Torrentio Plus +" still pass through.
    if (!stream.name.isEmpty()) {
        static const QRegularExpression kAddonBrandOnly(
            QStringLiteral("^(torrentio|comet|mediafusion|cinemeta|opensubtitles)"
                           "\\s*(2160p|1440p|1080p|720p|480p|4k)?\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        const QString candidate = firstLine(stream.name);
        if (!kAddonBrandOnly.match(candidate).hasMatch()) {
            return candidate;
        }
    }

    // stream.description — first line, but reject if it's a metadata row.
    if (!stream.description.isEmpty()) {
        const QString line0 = firstLine(stream.description);
        // Heuristic for metadata-only first lines: starts with a known
        // metadata glyph (Stremio-style 💾 ≈ U+1F4BE, 👤 ≈ U+1F464) or
        // a "Size:"/"Seeders:" prefix, OR is purely numeric/sizing data.
        static const QRegularExpression kMetadataRow(
            QStringLiteral("^(\\p{So}|\\p{Sc}|\\p{Cs}|size:|seeders:|seeds:|peers:|"
                           "\\d+(\\.\\d+)?\\s*(b|kb|mb|gb|tb)\\b)"),
            QRegularExpression::CaseInsensitiveOption);
        if (!line0.isEmpty() && !kMetadataRow.match(line0).hasMatch()) {
            return line0;
        }
        // First line was metadata — try the second line. Torrentio
        // sometimes flips order across addon versions (release name on
        // line 2, metadata on line 1).
        const int firstNl = stream.description.indexOf(QLatin1Char('\n'));
        if (firstNl > 0 && firstNl + 1 < stream.description.size()) {
            const QString line1 = firstLine(stream.description.mid(firstNl + 1));
            if (!line1.isEmpty() && !kMetadataRow.match(line1).hasMatch()) {
                return line1;
            }
        }
    }

    return QStringLiteral("(unnamed release)");
}

// STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — release-shape detector.
// Walks the release name + (as a fallback) the full description blob for
// episode/season/series patterns. Returns {packType, packLabel}; both
// empty on no match (movies, ad-hoc HTTP streams, anything without a
// recognizable shape token).
//
// Priority: episode > season > series. A season pack that name-drops one
// of its included episodes ("Season 3 (incl S03E04)") will trip the
// episode detector first — that's fine, Stremio behaves the same way.
QPair<QString, QString> detectPackType(const QString& releaseName,
                                        const Stream& stream)
{
    // Search both the release name AND the full description blob — some
    // addons stash the shape token only in description (not in the name).
    const QString haystack = releaseName + QLatin1Char('\n') + stream.description
                           + QLatin1Char('\n') + stream.name;

    // 1) Single episode — S03E04, S3E4, 3x04, season 3 episode 4.
    static const QRegularExpression kEpisodeSE(
        QStringLiteral("\\bS(\\d{1,2})\\s*[xE]\\s*(\\d{1,3})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kEpisodeXForm(
        QStringLiteral("\\b(\\d{1,2})x(\\d{1,3})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kEpisodeSE.match(haystack); m.hasMatch()) {
        return { QStringLiteral("episode"),
                 QStringLiteral("S%1E%2")
                     .arg(m.captured(1).toInt(), 2, 10, QChar('0'))
                     .arg(m.captured(2).toInt(), 2, 10, QChar('0')) };
    }
    if (auto m = kEpisodeXForm.match(haystack); m.hasMatch()) {
        return { QStringLiteral("episode"),
                 QStringLiteral("S%1E%2")
                     .arg(m.captured(1).toInt(), 2, 10, QChar('0'))
                     .arg(m.captured(2).toInt(), 2, 10, QChar('0')) };
    }

    // 2) Multi-season pack — "S01-S05", "Season 1-5", "Seasons 1 to 5".
    //    Detected before single-season because "Season 1-5" contains
    //    "Season 1" as a substring.
    static const QRegularExpression kMultiSeason(
        QStringLiteral("\\b(?:S(\\d{1,2})\\s*-\\s*S(\\d{1,2})|"
                       "Seasons?\\s*(\\d{1,2})\\s*(?:-|to)\\s*(\\d{1,2}))\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kMultiSeason.match(haystack); m.hasMatch()) {
        return { QStringLiteral("series"),
                 QStringLiteral("Complete Series") };
    }

    // 3) Complete-series free-text markers.
    static const QRegularExpression kCompleteSeries(
        QStringLiteral("\\b(complete\\s+(series|show|collection)|all\\s+seasons)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (kCompleteSeries.match(haystack).hasMatch()) {
        return { QStringLiteral("series"),
                 QStringLiteral("Complete Series") };
    }

    // 4) Single season — "Season 3", "S03 (without episode)", "Complete Season 3".
    static const QRegularExpression kSeasonWord(
        QStringLiteral("\\b(?:complete\\s+)?Seasons?\\s*(\\d{1,2})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kSeasonWord.match(haystack); m.hasMatch()) {
        return { QStringLiteral("season"),
                 QStringLiteral("Season %1").arg(m.captured(1).toInt()) };
    }
    // "S03" alone (no episode token following) — but be careful: "S03E04"
    // would have matched kEpisodeSE above, so by the time we reach here
    // a bare S\d+ implies a season pack. Use a negative-lookahead to
    // ensure we don't grab the S of an unrecognized SXX*EXX form.
    static const QRegularExpression kSeasonShort(
        QStringLiteral("\\bS(\\d{1,2})(?![\\dxE])\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kSeasonShort.match(haystack); m.hasMatch()) {
        return { QStringLiteral("season"),
                 QStringLiteral("Season %1").arg(m.captured(1).toInt()) };
    }

    // No shape detected — movie, ad-hoc HTTP stream, or unparseable.
    return { QString(), QString() };
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

        // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — primary line is now
        // the release name (Stremio parity); addon name moves to the
        // card footer line. Direct streams without a resolvable release
        // name fall back to "Direct stream" so the row still reads.
        const QString release = extractReleaseName(stream);
        if (c.isDirect && release == QLatin1String("(unnamed release)")) {
            c.displayTitle = QStringLiteral("Direct stream");
        } else {
            c.displayTitle = release;
        }
        const auto pack   = detectPackType(release, stream);
        c.packType        = pack.first;
        c.packLabel       = pack.second;
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

    // STREAM_HTTP_PREFER Phase 0.1 — rank direct HTTP/Url sources ABOVE
    // magnet sources in the stream picker so debrid-backed CDN links land
    // first. Cold-open on a libtorrent magnet can take minutes on a fresh
    // swarm (piece scheduler starvation despite healthy peer + bandwidth
    // state — see STREAM_HTTP_PREFER_FIX_TODO.md "Created" block for the
    // log evidence); the same title's HTTP stream via Real-Debrid /
    // Premiumize / etc is CDN-bound and resolves in seconds. When the
    // user's addons expose both kinds for a title, HTTP wins the default
    // selection. Within each tier (direct / magnet / other) the legacy
    // quality-size-name tiebreak is preserved so a user's muscle memory
    // for a specific 1080p pick inside a tier still ranks the same.
    std::stable_sort(out.begin(), out.end(),
        [](const StreamPickerChoice& a, const StreamPickerChoice& b) {
            // Tier 1: direct HTTP/Url first.
            if (a.isDirect != b.isDirect)                      return a.isDirect;
            // Tier 2 (within non-direct): magnet-with-seeders before
            // magnet-without-seeders-or-other-non-direct. Preserves the
            // legacy ordering *below* the new direct tier.
            const bool aMagWithSeeders =
                a.stream.source.kind == StreamSource::Kind::Magnet && a.seeders > 0;
            const bool bMagWithSeeders =
                b.stream.source.kind == StreamSource::Kind::Magnet && b.seeders > 0;
            if (aMagWithSeeders != bMagWithSeeders)            return aMagWithSeeders;
            if (aMagWithSeeders && bMagWithSeeders
                && a.seeders != b.seeders)                     return a.seeders > b.seeders;
            // Tier-internal tiebreaks (apply inside direct tier too —
            // higher-quality / bigger-file HTTP stream wins over lower-
            // quality HTTP stream of the same title).
            if (a.qualitySort != b.qualitySort)                return a.qualitySort > b.qualitySort;
            if (a.sizeBytes != b.sizeBytes)                    return a.sizeBytes > b.sizeBytes;
            // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — alphabetical
            // tiebreak now keys on displayTitle (release name) since
            // displayFilename was removed. Same intent — stable
            // ordering within a tier.
            return a.displayTitle.toLower() < b.displayTitle.toLower();
        });

    return out;
}

}
