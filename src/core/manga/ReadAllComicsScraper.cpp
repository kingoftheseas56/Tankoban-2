#include "ReadAllComicsScraper.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

// readallcomics.com serves page images as plain blogspot <img> URLs — no JS, no
// descramble, no Cloudflare on plain HTTP GET (UA = normal Chrome, Referer self).
// PROVEN 2026-06-03 end-to-end (Invincible #144 -> 55/55 pages -> valid cbz).
// Recipe: docs/superpowers/specs/2026-06-03-readallcomics-source-recipe.md
static const QString BASE = QStringLiteral("https://readallcomics.com");
static const QString USER_AGENT = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");

static QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setRawHeader("Referer", (BASE + "/").toUtf8());
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,*/*");
    // Long series category pages exceed Qt's default 10MB decompressed cap.
    req.setDecompressedSafetyCheckThreshold(-1);
    return req;
}

// "https://readallcomics.com/category/invincible-image-comics/" -> "invincible-image-comics"
static QString seriesSlugFromHref(const QString& href)
{
    static const QRegularExpression re(QStringLiteral("/category/([^/\"]+)/?"));
    const auto m = re.match(href);
    return m.hasMatch() ? m.captured(1) : QString();
}

// ── Search ───────────────────────────────────────────────────────────────────
void ReadAllComicsScraper::search(const QString& query, int /*limit*/)
{
    QUrl url(BASE + "/");
    QUrlQuery q;
    q.addQueryItem("story", query);
    q.addQueryItem("s", QString());
    q.addQueryItem("type", "comic");
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("ReadAllComics search failed: " + reply->errorString());
            return;
        }
        emit searchFinished(parseSearchHtml(QString::fromUtf8(reply->readAll())));
    });
}

QList<MangaResult> ReadAllComicsScraper::parseSearchHtml(const QString& html)
{
    // Result rows: <a href="https://readallcomics.com/category/<slug>/"
    //               class="cat-title">Series (Publisher: X)</a>
    static const QRegularExpression rowRe(
        QStringLiteral(R"RE(<a\s+href="([^"]*?/category/[^"]+)"[^>]*class="[^"]*cat-title[^"]*"[^>]*>\s*(.*?)\s*</a>)RE"),
        QRegularExpression::DotMatchesEverythingOption);

    QList<MangaResult> results;
    QSet<QString> seen;
    auto it = rowRe.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        const QString slug = seriesSlugFromHref(m.captured(1));
        if (slug.isEmpty() || seen.contains(slug))
            continue;
        seen.insert(slug);

        MangaResult r;
        r.id     = slug;
        r.url    = m.captured(1);
        r.title  = m.captured(2).trimmed();
        r.title.remove(QRegularExpression("<[^>]*>"));
        r.title  = r.title.trimmed();
        r.source = "readallcomics";
        r.type   = "comic";
        if (!r.title.isEmpty())
            results.append(r);
    }
    return results;
}

// ── Chapters (issue list) ──────────────────────────────────────────────────
void ReadAllComicsScraper::fetchChapters(const QString& seriesSlug)
{
    // Accept a bare slug ("invincible-image-comics") or a full category href.
    QString slug = seriesSlug;
    if (slug.contains(QLatin1String("/category/")))
        slug = seriesSlugFromHref(slug);
    QUrl url(BASE + "/category/" + slug + "/");

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("ReadAllComics fetchChapters failed: " + reply->errorString());
            return;
        }
        // TODO(pagination): long runs paginate (/page/2/ …); v1 reads page 1.
        emit chaptersReady(parseChaptersHtml(QString::fromUtf8(reply->readAll())));
    });
}

QList<ChapterInfo> ReadAllComicsScraper::parseChaptersHtml(const QString& html)
{
    // Issue links are single-segment: <a href="https://readallcomics.com/<issue-slug>/">Label</a>
    // where issue-slug ~ "invincible-144-2018". The clean separator from nav links
    // (report-error, new-comments, wp-json, privacy-policy, …) is verified live:
    // every issue slug contains a digit (the issue number); no nav slug does. The
    // stop-list is belt-and-suspenders on top of the digit gate.
    static const QSet<QString> kStop = {
        "category", "page", "report", "request", "vip", "comment", "comments",
        "privacy", "dmca", "contact", "tag", "author", "feed", "terms", "about",
        "login", "register", "wp", "wp-login", "wp-admin", "wp-content", "wp-json",
        "search", "new", "legal"
    };
    static const QRegularExpression hasDigit(QStringLiteral(R"(\d)"));
    static const QRegularExpression linkRe(
        QStringLiteral(R"RE(<a\s+href="https://readallcomics\.com/([^"/]+)/"\s*[^>]*>\s*(.*?)\s*</a>)RE"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression numRe(QStringLiteral(R"(#\s*(\d+(?:\.\d+)?))"));
    // Slug number = the issue number that precedes the trailing 4-digit year
    // ("invincible-144-2018" -> 144); fall back to the last number otherwise.
    static const QRegularExpression slugNumYearRe(QStringLiteral(R"(-(\d+(?:\.\d+)?)-\d{4}$)"));
    static const QRegularExpression slugNumRe(QStringLiteral(R"((\d+(?:\.\d+)?)(?!.*\d))"));

    QList<ChapterInfo> chapters;
    QSet<QString> seen;
    auto it = linkRe.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        const QString slug = m.captured(1);
        if (slug.isEmpty() || !hasDigit.match(slug).hasMatch())
            continue;  // nav/utility slugs carry no digit; issues always do
        if (kStop.contains(slug.section('-', 0, 0)) || kStop.contains(slug))
            continue;
        if (seen.contains(slug))
            continue;
        seen.insert(slug);

        ChapterInfo ch;
        ch.id     = slug;                          // issue-slug == chapterId
        ch.url    = BASE + "/" + slug + "/";
        ch.name   = m.captured(2).trimmed();
        ch.name.remove(QRegularExpression("<[^>]*>"));
        ch.name   = ch.name.trimmed();
        ch.source = "readallcomics";

        auto nm = numRe.match(ch.name);
        if (nm.hasMatch()) {
            ch.chapterNumber = nm.captured(1).toDouble();
        } else if (auto sy = slugNumYearRe.match(slug); sy.hasMatch()) {
            ch.chapterNumber = sy.captured(1).toDouble();
        } else if (auto sn = slugNumRe.match(slug); sn.hasMatch()) {
            ch.chapterNumber = sn.captured(1).toDouble();
        }
        chapters.append(ch);
    }

    std::sort(chapters.begin(), chapters.end(),
              [](const ChapterInfo& a, const ChapterInfo& b) {
        return a.chapterNumber < b.chapterNumber;
    });
    return chapters;
}

// ── Pages ────────────────────────────────────────────────────────────────────
void ReadAllComicsScraper::fetchPages(const QString& chapterId)
{
    // chapterId is the issue-slug ("invincible-144-2018"). Accept a full URL too.
    QString slug = chapterId;
    if (slug.startsWith(QLatin1String("http"))) {
        slug = QUrl(slug).path();
        slug = slug.split('/', Qt::SkipEmptyParts).value(0);
    }
    QUrl url(BASE + "/" + slug + "/");

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("ReadAllComics fetchPages failed: " + reply->errorString());
            return;
        }
        emit pagesReady(parsePagesHtml(QString::fromUtf8(reply->readAll())));
    });
}

QList<PageInfo> ReadAllComicsScraper::parsePagesHtml(const QString& html)
{
    // Page images are raw Blogger-hosted <img> tags. readallcomics serves them
    // from TWO interchangeable Blogger CDN hosts depending on the issue's upload
    // era — both are direct, descramble-free image URLs:
    //   • older uploads:  https://<N>.bp.blogspot.com/...           (e.g. Invincible)
    //   • newer uploads:  https://blogger.googleusercontent.com/img/... (e.g. Saga)
    // SIX_MODE_RESTRUCTURE Arc 1 (2026-06-14, Agent 1) — the original regex matched
    // ONLY the bp.blogspot.com host, so any issue served from googleusercontent
    // parsed to ZERO pages and the download stalled at 0% forever (Saga repro).
    // Accept either host. HTML entities (&#038; / &amp;) decode to '&'.
    static const QRegularExpression imgRe(
        QStringLiteral(R"RE(<img[^>]+src="(https://(?:\d+\.bp\.blogspot\.com|blogger\.googleusercontent\.com)/[^"]+)")RE"));

    QList<PageInfo> pages;
    QSet<QString> seen;
    int index = 0;
    auto it = imgRe.globalMatch(html);
    while (it.hasNext()) {
        QString src = it.next().captured(1);
        src.replace(QLatin1String("&#038;"), QLatin1String("&"));
        src.replace(QLatin1String("&amp;"), QLatin1String("&"));
        if (seen.contains(src))
            continue;
        seen.insert(src);

        PageInfo p;
        p.index    = index++;
        p.imageUrl = src;
        pages.append(p);
    }
    return pages;
}

// ── Detail (no-op) ───────────────────────────────────────────────────────────
void ReadAllComicsScraper::fetchDetail(const MangaResult& preview)
{
    // readallcomics is a page-fetch source only; rcostation owns the rich
    // Western detail/catalogue page. Emit a minimal detail so any caller that
    // does route here is not left hanging.
    MangaSeriesDetail detail;
    detail.preview      = preview;
    detail.sourceUrl    = preview.url;
    detail.heroCoverUrl = preview.thumbnailUrl;
    emit detailReady(detail);
}
