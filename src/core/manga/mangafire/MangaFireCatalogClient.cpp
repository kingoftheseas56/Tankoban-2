// src/core/manga/mangafire/MangaFireCatalogClient.cpp
//
// Implementation notes:
//   - Each fetchByTitle() instantiates a PendingFetch held by shared_ptr;
//     reply lambdas capture the pointer so concurrent fetches don't trample
//     each other.
//   - Regex-based HTML parsing is deliberate: the MangaFire fragments are
//     small, well-structured, and stable per the Keiyoushi extension source.
//     Adding a full HTML parser dependency for ~200 lines of scrape is not
//     groundwork-justified.
//   - Output JSON shape mirrors scripts/mangafire_scraper/mangafire_ingest.py
//     bit-for-bit so LocalMangaCatalogLoader consumes it identically to the
//     batch-script output.

#include "MangaFireCatalogClient.h"
#include "core/manga/LocalMangaCatalogLoader.h"

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <limits>
#include <optional>

namespace tankoban::manga::mangafire {

namespace {

constexpr const char* kBaseUrl = "https://mangafire.to";
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0 Safari/537.36";
constexpr const char* kNoImageMarker =
    "/assets/t2/s1/images/no-image.jpg";

QString slugify(const QString& title) {
    const QString lower = title.toLower().trimmed();
    QString out;
    bool lastWasHyphen = false;
    for (QChar c : lower) {
        const ushort u = c.unicode();
        const bool isAlnum = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
        if (isAlnum) {
            out.append(c);
            lastWasHyphen = false;
        } else if (!lastWasHyphen && !out.isEmpty()) {
            out.append(QChar('-'));
            lastWasHyphen = true;
        }
    }
    while (out.endsWith(QChar('-'))) out.chop(1);
    return out;
}

QString sitemapMatchKey(const QString& raw) {
    QString out;
    out.reserve(raw.size());
    for (QChar c : raw.toLower()) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')) {
            out.append(c);
        }
    }

    // MangaFire commonly appends one extra trailing character to slugs
    // (berserkk, yuyuhakushoo, onepiecee). Normalize that artifact so a
    // title-derived key can match sitemap entries without the /filter page.
    if (out.size() >= 2 && out.at(out.size() - 1) == out.at(out.size() - 2)) {
        out.chop(1);
    }
    return out;
}

QString titleSitemapKey(const QString& title) {
    QString normalized = title;
    static const QRegularExpression bracketed(
        QStringLiteral("\\s*\\([^)]*\\)\\s*$"));
    normalized.remove(bracketed);
    return sitemapMatchKey(normalized);
}

QString iso8601Now() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void applyStandardHeaders(QNetworkRequest& req) {
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept",
        "text/html,application/xhtml+xml,application/xml;q=0.9,"
        "application/json;q=0.8,*/*;q=0.7");
    req.setRawHeader("Referer", QByteArray(kBaseUrl) + "/");
}

void applyAjaxHeaders(QNetworkRequest& req) {
    applyStandardHeaders(req);
    req.setRawHeader("Accept",
        "application/json, text/javascript, */*; q=0.01");
    req.setRawHeader("X-Requested-With", "XMLHttpRequest");
}

bool isPlaceholderCover(const QString& url) {
    if (url.isEmpty()) return true;
    return url.contains(QLatin1String(kNoImageMarker));
}

QString normalizeUrl(const QString& maybeRel) {
    if (maybeRel.isEmpty()) return {};
    if (maybeRel.startsWith(QLatin1String("http://")) ||
        maybeRel.startsWith(QLatin1String("https://"))) {
        return maybeRel;
    }
    return QString::fromLatin1(kBaseUrl) +
           (maybeRel.startsWith(QLatin1Char('/')) ? maybeRel
                                                  : QStringLiteral("/") + maybeRel);
}

struct FilterMatch {
    QString slug;
    QString hash;
    QString seriesUrl;
    int score = 0;
};

// Extract first /manga/<slug>.<hash> link from the /filter HTML response.
// MangaFire's nav and footer don't link to /manga/* — the first match in body
// order is the top search result card.
std::optional<FilterMatch> parseFilterFirstResult(const QString& html) {
    static const QRegularExpression rx(
        QStringLiteral("href=\"/manga/([a-z0-9\\-]+)\\.([a-z0-9]+)\""),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(html);
    if (!m.hasMatch()) return std::nullopt;
    FilterMatch out;
    out.slug      = m.captured(1);
    out.hash      = m.captured(2);
    out.seriesUrl = QString::fromLatin1(kBaseUrl) +
                    QStringLiteral("/manga/") + out.slug +
                    QChar('.') + out.hash;
    out.score     = 1000;
    return out;
}

QStringList parseSitemapListUrls(const QString& xml) {
    QStringList out;
    QSet<QString> seen;
    static const QRegularExpression rx(
        QStringLiteral("(?:https://mangafire\\.to/)?sitemap-list-\\d+\\.xml"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = rx.globalMatch(xml);
    while (it.hasNext()) {
        QString url = it.next().captured(0);
        if (!url.startsWith(QLatin1String("http"))) {
            url = QString::fromLatin1(kBaseUrl) + QChar('/') + url;
        }
        if (!seen.contains(url)) {
            seen.insert(url);
            out.append(url);
        }
    }
    return out;
}

std::optional<FilterMatch> parseSitemapBestMatch(const QString& xml,
                                                 const QString& title) {
    const QString wanted = titleSitemapKey(title);
    if (wanted.isEmpty()) return std::nullopt;

    std::optional<FilterMatch> best;
    static const QRegularExpression rx(
        QStringLiteral("https://mangafire\\.to/manga/([a-z0-9\\-]+)\\.([a-z0-9]+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = rx.globalMatch(xml);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString slug = m.captured(1);
        const QString candidate = sitemapMatchKey(slug);
        int score = 0;
        if (candidate == wanted) {
            score = 1000;
        } else if (candidate.startsWith(wanted)
                   && (candidate.size() - wanted.size()) <= 2) {
            score = 900;
        } else if (wanted.startsWith(candidate) && candidate.size() >= 6) {
            // Alias case: AniList/WeebCentral may say "Grand Blue Dreaming"
            // while MangaFire's canonical slug is just grand-bluee.
            score = 500 + candidate.size();
        }
        if (score <= 0) continue;
        if (best.has_value() && best->score >= score) continue;

        FilterMatch out;
        out.slug      = slug;
        out.hash      = m.captured(2);
        out.seriesUrl = QString::fromLatin1(kBaseUrl) +
                        QStringLiteral("/manga/") + out.slug +
                        QChar('.') + out.hash;
        out.score     = score;
        best = out;
    }
    return best;
}

// Parse {"status": 200, "result": "<html>..."} ajax envelope from MangaFire.
std::optional<QString> unwrapAjaxResult(const QByteArray& body) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("status")).toInt(0) != 200) return std::nullopt;
    return obj.value(QStringLiteral("result")).toString();
}

struct VolumeEntry {
    int     number = 0;
    QString coverUrl;
};

// Parse volume rows from /ajax/manga/<hash>/volume/<lang> result HTML.
// Each row looks like:
//   <div class="unit item" data-number="42">
//     <a ...><div class="poster"><div><img src="..."></div></div>
//     <span>Vol 42</span></a>
//   </div>
QList<VolumeEntry> parseVolumeList(const QString& html) {
    QList<VolumeEntry> out;
    static const QRegularExpression rx(
        QStringLiteral(
            "class=\"unit item\"[^>]*data-number=\"(\\d+)\""
            "[\\s\\S]*?<img[^>]*src=\"([^\"]+)\""),
        QRegularExpression::DotMatchesEverythingOption);
    auto it = rx.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        VolumeEntry v;
        v.number = m.captured(1).toInt();
        if (v.number <= 0) continue;
        const QString cover = normalizeUrl(m.captured(2));
        v.coverUrl = isPlaceholderCover(cover) ? QString() : cover;
        out.append(v);
    }
    std::sort(out.begin(), out.end(),
              [](const VolumeEntry& a, const VolumeEntry& b) {
                  return a.number < b.number;
              });
    return out;
}

struct ChapterRange {
    QString first;
    QString last;
};

// Parse chapter rows from /ajax/manga/<hash>/chapter/<lang> result HTML.
// Each <li.item> has an <a title="Vol N -  Chap M"> we use to bucket by volume.
QHash<int, ChapterRange> parseChapterRanges(const QString& html) {
    QHash<int, QStringList> grouped;
    static const QRegularExpression rx(
        QStringLiteral(
            "<li[^>]*class=\"item\"[^>]*data-number=\"([^\"]*)\"[\\s\\S]*?"
            "<a[^>]*href=\"([^\"]+)\"[^>]*title=\"Vol\\s+(-?\\d+)[^\"]*\""),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression chRx(QStringLiteral("/chapter-([^/#?\"]+)"));
    auto it = rx.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString dataNumber = m.captured(1);
        const QString href       = m.captured(2);
        const int volNum         = m.captured(3).toInt();
        QString chapterId;
        const auto chm = chRx.match(href);
        if (chm.hasMatch()) chapterId = chm.captured(1);
        else                chapterId = dataNumber;
        if (!chapterId.isEmpty()) grouped[volNum].append(chapterId);
    }

    auto sortKey = [](const QString& raw) -> double {
        static const QRegularExpression r(
            QStringLiteral("-?\\d+(?:\\.\\d+)?"));
        const auto m = r.match(raw);
        if (!m.hasMatch()) return std::numeric_limits<double>::infinity();
        bool ok = false;
        const double v = m.captured(0).toDouble(&ok);
        return ok ? v : std::numeric_limits<double>::infinity();
    };

    QHash<int, ChapterRange> ranges;
    for (auto it2 = grouped.constBegin(); it2 != grouped.constEnd(); ++it2) {
        QStringList chs = it2.value();
        QStringList unique;
        QSet<QString> seen;
        for (const QString& c : chs) {
            if (!seen.contains(c)) {
                seen.insert(c);
                unique.append(c);
            }
        }
        std::sort(unique.begin(), unique.end(),
                  [&](const QString& a, const QString& b) {
                      return sortKey(a) < sortKey(b);
                  });
        if (unique.isEmpty()) continue;
        ChapterRange r;
        r.first = unique.first();
        r.last  = unique.last();
        ranges.insert(it2.key(), r);
    }
    return ranges;
}

// Mirror mangafire_ingest.py's emit shape so LocalMangaCatalogLoader ingests
// on-demand JSON identically to the batch-script output.
QString writeCatalogJson(const tankoban::manga::MangaCatalog& cat) {
    QJsonObject root;
    root.insert(QStringLiteral("seriesId"), cat.seriesId);
    root.insert(QStringLiteral("seriesTitle"), cat.seriesTitle);
    QJsonArray alt;
    for (const QString& a : cat.seriesTitleAlt) alt.append(a);
    root.insert(QStringLiteral("seriesTitleAlt"), alt);
    root.insert(QStringLiteral("anilistId"), cat.anilistId);
    root.insert(QStringLiteral("mangafireUrl"), cat.mangafireUrl);
    root.insert(QStringLiteral("mangafireId"), cat.mangafireId);
    root.insert(QStringLiteral("author"), cat.author);
    root.insert(QStringLiteral("studio"), cat.studio);
    QJsonArray gen;
    for (const QString& g : cat.genres) gen.append(g);
    root.insert(QStringLiteral("genres"), gen);
    root.insert(QStringLiteral("status"), cat.status);
    root.insert(QStringLiteral("publishedYearStart"),
        cat.publishedYearStart > 0
            ? QJsonValue(cat.publishedYearStart)
            : QJsonValue(QJsonValue::Null));
    root.insert(QStringLiteral("publishedYearEnd"),
        cat.publishedYearEnd > 0
            ? QJsonValue(cat.publishedYearEnd)
            : QJsonValue(QJsonValue::Null));
    root.insert(QStringLiteral("mangazine"), cat.mangazine);
    root.insert(QStringLiteral("malScoreRaw"),
        cat.malScoreRaw > 0.0
            ? QJsonValue(cat.malScoreRaw)
            : QJsonValue(QJsonValue::Null));
    root.insert(QStringLiteral("scrapedAt"), iso8601Now());
    root.insert(QStringLiteral("source"), cat.source);
    root.insert(QStringLiteral("notes"), cat.notes);
    root.insert(QStringLiteral("synopsis"), cat.seriesSynopsis);

    QJsonArray volumes;
    for (const auto& v : cat.volumes) {
        QJsonObject vo;
        vo.insert(QStringLiteral("number"), v.volumeNumber);
        vo.insert(QStringLiteral("title"), v.titleEnglish);
        vo.insert(QStringLiteral("synopsis"), v.synopsis);
        vo.insert(QStringLiteral("coverUrl"), v.coverUrlJapanese);
        vo.insert(QStringLiteral("chapterStart"), v.chapterStartRaw);
        vo.insert(QStringLiteral("chapterEnd"), v.chapterEndRaw);
        volumes.append(vo);
    }
    root.insert(QStringLiteral("volumes"), volumes);

    const QString dir = LocalMangaCatalogLoader::canonicalDataDir();
    QDir().mkpath(dir);
    const QString path = QDir(dir).absoluteFilePath(
        cat.seriesId + QStringLiteral(".json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return path;
}

} // namespace

struct MangaFireCatalogClient::PendingFetch {
    QString                title;
    QString                slug;
    QString                hash;
    QString                seriesUrl;
    QList<VolumeEntry>     volumes;
    QHash<int, ChapterRange> ranges;
    std::optional<FilterMatch> bestSitemapMatch;
};

MangaFireCatalogClient::MangaFireCatalogClient(QNetworkAccessManager* nam,
                                                QObject* parent)
    : QObject(parent), m_nam(nam)
{}

QString MangaFireCatalogClient::patchWeebCentralBlock(
    const QString& seriesId,
    const tankoban::manga::WeebCentralCacheBlock& block)
{
    if (seriesId.isEmpty()) return {};

    const QString dir = LocalMangaCatalogLoader::canonicalDataDir();
    const QString path = QDir(dir).absoluteFilePath(
        seriesId + QStringLiteral(".json"));

    QFile in(path);
    if (!in.exists() || !in.open(QIODevice::ReadOnly)) return {};

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &err);
    in.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};

    QJsonObject root = doc.object();
    QJsonObject wc;
    wc.insert(QStringLiteral("seriesId"), block.seriesId);

    if (block.chaptersFetchedAt.isValid()) {
        wc.insert(QStringLiteral("chaptersFetchedAt"),
                  block.chaptersFetchedAt.toUTC().toString(Qt::ISODate));
    }

    if (!block.volumeChapterIds.isEmpty()) {
        QJsonObject volsObj;
        for (auto it = block.volumeChapterIds.constBegin();
             it != block.volumeChapterIds.constEnd(); ++it) {
            QJsonArray arr;
            for (const QString& id : it.value()) {
                if (!id.isEmpty()) arr.append(id);
            }
            if (!arr.isEmpty()) {
                volsObj.insert(QString::number(it.key()), arr);
            }
        }
        if (!volsObj.isEmpty()) {
            wc.insert(QStringLiteral("volumeChapterIds"), volsObj);
        }
    }

    root.insert(QStringLiteral("weebCentral"), wc);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) return {};
    return path;
}

MangaFireCatalogClient::~MangaFireCatalogClient() = default;

void MangaFireCatalogClient::fetchByTitle(const QString& title)
{
    if (!m_nam) {
        emit catalogFailed(title, QStringLiteral("network manager unset"));
        return;
    }
    if (title.trimmed().isEmpty()) {
        emit catalogFailed(title, QStringLiteral("empty title"));
        return;
    }
    auto pending = std::make_shared<PendingFetch>();
    pending->title = title.trimmed();
    stepFilter(pending);
}

void MangaFireCatalogClient::emitFailure(PendingFetchPtr pending,
                                          const QString& reason)
{
    emit catalogFailed(pending ? pending->title : QString(), reason);
}

void MangaFireCatalogClient::stepFilter(PendingFetchPtr pending)
{
    QUrl url(QString::fromLatin1(kBaseUrl) + QStringLiteral("/filter"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("keyword"), pending->title);
    url.setQuery(q);

    QNetworkRequest req(url);
    applyStandardHeaders(req);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            stepSitemapIndex(pending,
                             QStringLiteral("filter HTTP error: %1")
                                 .arg(reply->errorString()));
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());
        const auto match = parseFilterFirstResult(html);
        if (!match.has_value()) {
            stepSitemapIndex(pending,
                             QStringLiteral("filter returned no /manga/* link"));
            return;
        }
        pending->slug      = match->slug;
        pending->hash      = match->hash;
        pending->seriesUrl = match->seriesUrl;
        stepVolume(pending);
    });
}

void MangaFireCatalogClient::stepSitemapIndex(PendingFetchPtr pending,
                                               const QString& fallbackReason)
{
    QUrl url(QString::fromLatin1(kBaseUrl) + QStringLiteral("/sitemap.xml"));
    QNetworkRequest req(url);
    applyStandardHeaders(req);
    req.setRawHeader("Accept", "application/xml,text/xml,*/*;q=0.7");

    qInfo("MangaFireCatalogClient: falling back to sitemap discovery for \"%s\" after %s",
          qUtf8Printable(pending ? pending->title : QString()),
          qUtf8Printable(fallbackReason));

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending, fallbackReason]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emitFailure(pending,
                        QStringLiteral("%1; sitemap index HTTP error: %2")
                            .arg(fallbackReason, reply->errorString()));
            return;
        }
        const QStringList listUrls = parseSitemapListUrls(QString::fromUtf8(reply->readAll()));
        if (listUrls.isEmpty()) {
            emitFailure(pending,
                        QStringLiteral("%1; sitemap index had no list URLs")
                            .arg(fallbackReason));
            return;
        }
        stepSitemapList(pending, listUrls, 0);
    });
}

void MangaFireCatalogClient::stepSitemapList(PendingFetchPtr pending,
                                              const QStringList& listUrls,
                                              int index)
{
    if (index >= listUrls.size()) {
        if (pending && pending->bestSitemapMatch.has_value()) {
            pending->slug      = pending->bestSitemapMatch->slug;
            pending->hash      = pending->bestSitemapMatch->hash;
            pending->seriesUrl = pending->bestSitemapMatch->seriesUrl;
            qInfo("MangaFireCatalogClient: sitemap fuzzy matched \"%s\" -> %s.%s score=%d",
                  qUtf8Printable(pending->title),
                  qUtf8Printable(pending->slug),
                  qUtf8Printable(pending->hash),
                  pending->bestSitemapMatch->score);
            stepVolume(pending);
            return;
        }
        emitFailure(pending,
                    QStringLiteral("sitemap discovery found no match for \"%1\"")
                        .arg(pending ? pending->title : QString()));
        return;
    }

    QNetworkRequest req(QUrl{listUrls.at(index)});
    applyStandardHeaders(req);
    req.setRawHeader("Accept", "application/xml,text/xml,*/*;q=0.7");

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending, listUrls, index]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            stepSitemapList(pending, listUrls, index + 1);
            return;
        }

        const auto match = parseSitemapBestMatch(QString::fromUtf8(reply->readAll()),
                                                 pending->title);
        if (!match.has_value()) {
            stepSitemapList(pending, listUrls, index + 1);
            return;
        }
        if (!pending->bestSitemapMatch.has_value()
            || pending->bestSitemapMatch->score < match->score) {
            pending->bestSitemapMatch = match;
        }

        if (match->score < 900) {
            stepSitemapList(pending, listUrls, index + 1);
            return;
        }

        pending->slug      = match->slug;
        pending->hash      = match->hash;
        pending->seriesUrl = match->seriesUrl;
        qInfo("MangaFireCatalogClient: sitemap matched \"%s\" -> %s.%s score=%d",
              qUtf8Printable(pending->title),
              qUtf8Printable(pending->slug),
              qUtf8Printable(pending->hash),
              match->score);
        stepVolume(pending);
    });
}

void MangaFireCatalogClient::stepVolume(PendingFetchPtr pending)
{
    const QString url = QString::fromLatin1(kBaseUrl) +
                        QStringLiteral("/ajax/manga/") + pending->hash +
                        QStringLiteral("/volume/en");
    QNetworkRequest req(QUrl{url});
    applyAjaxHeaders(req);
    req.setRawHeader("Referer", pending->seriesUrl.toUtf8());

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emitFailure(pending,
                        QStringLiteral("volume HTTP error: %1")
                            .arg(reply->errorString()));
            return;
        }
        const auto inner = unwrapAjaxResult(reply->readAll());
        if (!inner.has_value()) {
            emitFailure(pending,
                        QStringLiteral("volume ajax envelope unparseable"));
            return;
        }
        pending->volumes = parseVolumeList(*inner);
        if (pending->volumes.isEmpty()) {
            emitFailure(pending,
                        QStringLiteral("no volumes parsed from /ajax/manga/.../volume/en"));
            return;
        }
        stepChapter(pending);
    });
}

void MangaFireCatalogClient::stepChapter(PendingFetchPtr pending)
{
    const QString url = QString::fromLatin1(kBaseUrl) +
                        QStringLiteral("/ajax/manga/") + pending->hash +
                        QStringLiteral("/chapter/en");
    QNetworkRequest req(QUrl{url});
    applyAjaxHeaders(req);
    req.setRawHeader("Referer", pending->seriesUrl.toUtf8());

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emitFailure(pending,
                        QStringLiteral("chapter HTTP error: %1")
                            .arg(reply->errorString()));
            return;
        }
        const auto inner = unwrapAjaxResult(reply->readAll());
        if (!inner.has_value()) {
            emitFailure(pending,
                        QStringLiteral("chapter ajax envelope unparseable"));
            return;
        }
        pending->ranges = parseChapterRanges(*inner);
        finish(pending);
    });
}

void MangaFireCatalogClient::finish(PendingFetchPtr pending)
{
    MangaCatalog cat;
    cat.seriesId      = slugify(pending->title);
    cat.seriesTitle   = pending->title;
    cat.mangafireUrl  = pending->seriesUrl;
    cat.mangafireId   = pending->slug + QChar('.') + pending->hash;
    cat.fetchedAt     = QDateTime::currentDateTimeUtc();
    cat.schemaVersion = kMangaCatalogSchemaVersion;
    cat.source        = QStringLiteral("MangaFireCatalogClient v1 (on-demand)");

    for (const auto& v : pending->volumes) {
        MangaVolume mv;
        mv.volumeNumber     = v.number;
        mv.coverUrlJapanese = v.coverUrl;
        const auto it = pending->ranges.constFind(v.number);
        if (it != pending->ranges.constEnd()) {
            mv.chapterStartRaw = it.value().first;
            mv.chapterEndRaw   = it.value().last;
            bool okA = false, okB = false;
            const double a = mv.chapterStartRaw.toDouble(&okA);
            const double b = mv.chapterEndRaw.toDouble(&okB);
            if (okA) mv.chapterRangeStart = static_cast<int>(a);
            if (okB) mv.chapterRangeEnd   = static_cast<int>(b);
        }
        cat.volumes.append(std::move(mv));
    }

    if (cat.volumes.isEmpty() || cat.seriesId.isEmpty()) {
        emitFailure(pending,
                    QStringLiteral("assembled catalog failed validity check"));
        return;
    }

    const QString path = writeCatalogJson(cat);
    if (path.isEmpty()) {
        emitFailure(pending,
                    QStringLiteral("write to data/mangafire_catalog/%1.json failed")
                        .arg(cat.seriesId));
        return;
    }

    emit catalogReady(cat, path);
}

} // namespace tankoban::manga::mangafire
