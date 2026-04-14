#include "WeebCentralScraper.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

static const QString BASE = QStringLiteral("https://weebcentral.com");
static const QString USER_AGENT = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");

static QNetworkRequest makeRequest(const QUrl& url, bool isHtmx = false)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setRawHeader("Referer", BASE.toUtf8());
    req.setRawHeader("Accept", "text/html,*/*");
    if (isHtmx) {
        req.setRawHeader("HX-Request", "true");
        req.setRawHeader("HX-Target", "search-results");
    }
    return req;
}

// ── Search ──────────────────────────────────────────────────────────────────
void WeebCentralScraper::search(const QString& query, int /*limit*/)
{
    QUrl url(BASE + "/search/data");
    QUrlQuery q;
    q.addQueryItem("text", query);
    q.addQueryItem("sort", "Best Match");
    q.addQueryItem("order", "Descending");
    q.addQueryItem("official", "Any");
    q.addQueryItem("display_mode", "Full Display");
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url, true));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("WeebCentral search failed: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit searchFinished(parseSearchHtml(html));
    });
}

QList<MangaResult> WeebCentralScraper::parseSearchHtml(const QString& html)
{
    QList<MangaResult> results;

    // Match <article> blocks that have both "flex" and "gap-4" CSS classes —
    // these are the result cards. Mirrors Groundwork's _WeebCentralSearchParser
    // which checks: tag == "article" AND "flex" in cls AND "gap-4" in cls.
    static QRegularExpression articleRe(
        R"re(<article\b([^>]*)>(.*?)</article>)re",
        QRegularExpression::DotMatchesEverythingOption);

    // All <a> tags within a block
    static QRegularExpression anyLinkRe(
        R"re(<a\b([^>]*)>(.*?)</a>)re",
        QRegularExpression::DotMatchesEverythingOption);

    // Series href: /series/{ULID}/{slug}
    static QRegularExpression hrefRe(
        R"re(href="(/series/([^/"]+)/[^"]*)")re");

    // class="..." extractor (used on both article attrs and link attrs)
    static QRegularExpression classAttrRe(
        R"re(class="([^"]*)")re");

    static QRegularExpression imgSrcRe(
        R"re(<img\b[^>]*src="([^"]+)"[^>]*>)re");

    static QRegularExpression spanRe(
        R"re(<span\b[^>]*>(.*?)</span>)re",
        QRegularExpression::DotMatchesEverythingOption);

    static QRegularExpression stripTagsRe(R"re(<[^>]*>)re");

    auto articleMatches = articleRe.globalMatch(html);
    while (articleMatches.hasNext()) {
        auto am = articleMatches.next();
        QString articleAttrs = am.captured(1);

        // Filter: must have both "flex" and "gap-4" classes
        auto ac = classAttrRe.match(articleAttrs);
        QString articleCls = ac.hasMatch() ? ac.captured(1) : QString();
        if (!articleCls.contains(QLatin1String("flex")) ||
            !articleCls.contains(QLatin1String("gap-4")))
            continue;

        QString block = am.captured(2);

        MangaResult r;
        r.source = "weebcentral";
        r.type = "manga";

        // Cover image
        auto im = imgSrcRe.match(block);
        if (im.hasMatch())
            r.thumbnailUrl = im.captured(1);

        // Scan all <a> links in this block
        auto linkMatches = anyLinkRe.globalMatch(block);
        while (linkMatches.hasNext()) {
            auto lm = linkMatches.next();
            QString attrs = lm.captured(1);
            QString inner = lm.captured(2);
            inner.remove(stripTagsRe);
            inner = inner.trimmed();

            // Series ID from first /series/{ULID}/... href
            auto hm = hrefRe.match(attrs);
            if (hm.hasMatch() && r.id.isEmpty()) {
                r.id  = hm.captured(2);
                r.url = hm.captured(1);
            }

            if (inner.isEmpty()) continue;

            auto lc = classAttrRe.match(attrs);
            QString linkCls = lc.hasMatch() ? lc.captured(1) : QString();

            // Title: link-hover WITHOUT link-info
            // Mirrors Groundwork: "link-hover" in cls and "link-info" not in cls
            if (linkCls.contains(QLatin1String("link-hover")) &&
                !linkCls.contains(QLatin1String("link-info")) &&
                r.title.isEmpty()) {
                r.title = inner;
            }

            // Author: BOTH link-info AND link-hover
            // Mirrors Groundwork: "link-info" in cls and "link-hover" in cls
            if (linkCls.contains(QLatin1String("link-info")) &&
                linkCls.contains(QLatin1String("link-hover"))) {
                r.author = inner;
            }
        }

        // Status from span text
        auto spanMatches = spanRe.globalMatch(block);
        while (spanMatches.hasNext()) {
            QString text = spanMatches.next().captured(1);
            text.remove(stripTagsRe);
            text = text.trimmed();
            if (text == QLatin1String("Ongoing") || text == QLatin1String("Completed") ||
                text == QLatin1String("Hiatus")  || text == QLatin1String("Cancelled")) {
                r.status = text;
                break;
            }
        }

        if (!r.title.isEmpty() && !r.id.isEmpty())
            results.append(r);
    }

    return results;
}

// ── Chapters ────────────────────────────────────────────────────────────────
void WeebCentralScraper::fetchChapters(const QString& seriesId)
{
    QUrl url(BASE + "/series/" + seriesId + "/full-chapter-list");

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch chapters: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit chaptersReady(parseChaptersHtml(html, "weebcentral"));
    });
}

QList<ChapterInfo> WeebCentralScraper::parseChaptersHtml(const QString& html, const QString& source)
{
    QList<ChapterInfo> chapters;

    // Pattern: <a href="/chapters/{ULID}">Chapter N</a>
    static QRegularExpression chapterRe(
        R"RE(<a\s+href="/chapters/([^"]+)"[^>]*>\s*(.*?)\s*</a>)RE",
        QRegularExpression::DotMatchesEverythingOption);

    static QRegularExpression numRe(R"((\d+(?:\.\d+)?))", QRegularExpression::NoPatternOption);

    auto matches = chapterRe.globalMatch(html);
    while (matches.hasNext()) {
        auto m = matches.next();
        ChapterInfo ch;
        ch.id     = m.captured(1);
        ch.url    = "/chapters/" + ch.id;
        ch.name   = m.captured(2).trimmed();
        ch.name.remove(QRegularExpression("<[^>]*>"));
        ch.name   = ch.name.trimmed();
        ch.source = source;

        // Extract chapter number from name
        auto nm = numRe.match(ch.name);
        if (nm.hasMatch())
            ch.chapterNumber = nm.captured(1).toDouble();

        if (!ch.id.isEmpty())
            chapters.append(ch);
    }

    // Sort ascending by chapter number
    std::sort(chapters.begin(), chapters.end(), [](const ChapterInfo& a, const ChapterInfo& b) {
        return a.chapterNumber < b.chapterNumber;
    });

    return chapters;
}

// ── Pages ───────────────────────────────────────────────────────────────────
void WeebCentralScraper::fetchPages(const QString& chapterId)
{
    QUrl url(BASE + "/chapters/" + chapterId + "/images");
    QUrlQuery q;
    q.addQueryItem("is_prev", "False");
    q.addQueryItem("current_page", "1");
    q.addQueryItem("reading_style", "long_strip");
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch pages: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit pagesReady(parsePagesHtml(html));
    });
}

QList<PageInfo> WeebCentralScraper::parsePagesHtml(const QString& html)
{
    QList<PageInfo> pages;

    // Pattern: <img src="https://scans-hot.planeptune.us/...">
    static QRegularExpression imgRe(
        R"RE(<img[^>]+src="(https://[^"]*planeptune[^"]*)"[^>]*>)RE");

    auto matches = imgRe.globalMatch(html);
    int idx = 0;
    while (matches.hasNext()) {
        auto m = matches.next();
        PageInfo p;
        p.index    = idx++;
        p.imageUrl = m.captured(1);
        pages.append(p);
    }

    return pages;
}
