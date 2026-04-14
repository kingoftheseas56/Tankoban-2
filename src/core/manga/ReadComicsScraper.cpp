#include "ReadComicsScraper.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

static const QString BASE = QStringLiteral("https://readcomicsonline.ru");
static const QString USER_AGENT = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");

static QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setRawHeader("Referer", BASE.toUtf8());
    req.setRawHeader("Accept", "text/html,application/json,*/*");
    return req;
}

// ── Search (JSON API) ───────────────────────────────────────────────────────
void ReadComicsScraper::search(const QString& query, int /*limit*/)
{
    QUrl url(BASE + "/search");
    QUrlQuery q;
    q.addQueryItem("query", query);
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("ReadComicsOnline search failed: " + reply->errorString());
            return;
        }

        auto data = reply->readAll();
        auto doc = QJsonDocument::fromJson(data);
        auto suggestions = doc.object().value("suggestions").toArray();

        QList<MangaResult> results;
        for (const auto& val : suggestions) {
            auto obj = val.toObject();
            MangaResult r;
            r.title  = obj.value("value").toString();
            r.id     = obj.value("data").toString();  // slug
            r.url    = "/comic/" + r.id;
            r.source = "readcomicsonline";
            r.type   = "comic";
            r.thumbnailUrl = BASE + "/uploads/manga/" + r.id + "/cover/cover_250x350.jpg";

            if (!r.title.isEmpty() && !r.id.isEmpty())
                results.append(r);
        }

        emit searchFinished(results);
    });
}

// ── Chapters (HTML scrape) ──────────────────────────────────────────────────
void ReadComicsScraper::fetchChapters(const QString& seriesSlug)
{
    QUrl url(BASE + "/comic/" + seriesSlug);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, seriesSlug]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch chapters: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit chaptersReady(parseChaptersHtml(html, seriesSlug));
    });
}

QList<ChapterInfo> ReadComicsScraper::parseChaptersHtml(const QString& html, const QString& slug)
{
    QList<ChapterInfo> chapters;

    // Pattern: <a href="/comic/{slug}/{issue}">Title #N</a>
    // Can't use static here — pattern depends on slug
    QRegularExpression chRe(
        QStringLiteral(R"RE(<a\s+href="/comic/%1/(\d+)"[^>]*>\s*(.*?)\s*</a>)RE")
            .arg(QRegularExpression::escape(slug)),
        QRegularExpression::DotMatchesEverythingOption);

    static QRegularExpression numRe(R"(#?(\d+(?:\.\d+)?))");

    auto matches = chRe.globalMatch(html);
    while (matches.hasNext()) {
        auto m = matches.next();
        ChapterInfo ch;
        ch.id     = slug + "/" + m.captured(1);  // "slug/issue"
        ch.url    = "/comic/" + ch.id;
        ch.name   = m.captured(2).trimmed();
        ch.name.remove(QRegularExpression("<[^>]*>"));
        ch.name   = ch.name.trimmed();
        ch.source = "readcomicsonline";

        auto nm = numRe.match(ch.name);
        if (nm.hasMatch())
            ch.chapterNumber = nm.captured(1).toDouble();
        else
            ch.chapterNumber = m.captured(1).toDouble();  // issue number from URL

        chapters.append(ch);
    }

    // Sort ascending
    std::sort(chapters.begin(), chapters.end(), [](const ChapterInfo& a, const ChapterInfo& b) {
        return a.chapterNumber < b.chapterNumber;
    });

    return chapters;
}

// ── Pages (JS parse) ────────────────────────────────────────────────────────
void ReadComicsScraper::fetchPages(const QString& chapterId)
{
    // chapterId format: "slug/issue"
    QUrl url(BASE + "/comic/" + chapterId);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, chapterId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch pages: " + reply->errorString());
            return;
        }

        auto parts = chapterId.split('/');
        QString slug = parts.value(0);
        QString issue = parts.value(1);

        auto html = QString::fromUtf8(reply->readAll());
        emit pagesReady(parsePagesHtml(html, slug, issue));
    });
}

QList<PageInfo> ReadComicsScraper::parsePagesHtml(const QString& html, const QString& slug, const QString& issue)
{
    QList<PageInfo> pages;

    // Look for: var pages = [{"page_image":"01.jpg"}, ...]
    static QRegularExpression pagesRe(
        R"(var\s+pages\s*=\s*(\[.*?\]);)",
        QRegularExpression::DotMatchesEverythingOption);

    auto m = pagesRe.match(html);
    if (!m.hasMatch()) return pages;

    auto doc = QJsonDocument::fromJson(m.captured(1).toUtf8());
    auto arr = doc.array();

    for (int i = 0; i < arr.size(); ++i) {
        auto obj = arr[i].toObject();
        QString pageImage = obj.value("page_image").toString();
        if (pageImage.isEmpty()) continue;

        PageInfo p;
        p.index    = i;
        p.imageUrl = BASE + "/uploads/manga/" + slug + "/chapters/" + issue + "/" + pageImage;
        pages.append(p);
    }

    return pages;
}
