#include "ReadComicsScraper.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

// Repointed 2026-05-31 (Phase 4, Western catalogue arc): the old
// readcomicsonline.ru host went dead. rcostation.xyz is the live successor,
// but it is a DIFFERENT CMS (ASP.NET) — not a drop-in. Live-probed deltas:
//   * search    : POST/GET /Search/Comic?keyword=<q> (HTML results), NOT the
//                 old /search JSON suggestions API (404 here).
//   * series/list: /Comic/<Name> (capital C). Item links are
//                 /Comic/<Name>/<Item-Slug>?id=<n> with NAMED slugs
//                 (Issue-144, TPB-25-...), not bare numbers.
//   * covers    : /Uploads/... (date-pathed) — pulled from page HTML, NOT
//                 reconstructable from the slug.
//   * reader    : obfuscated by an external 21wiz.com/s.js injector — dead on
//                 plain HTTP. fetchPages stays best-effort (returns empty).
// The source identity key stays "readcomicsonline" — it's referenced across
// the registry / library / display-name layer; only the host + URL shapes move.
// TODO(follow-up): rename the sourceId "readcomicsonline" → "rcostation" in its
// own PR once the ~10-file registry/library/display blast radius is handled.
static const QString BASE = QStringLiteral("https://rcostation.xyz");
static const QString USER_AGENT = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");

static QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setRawHeader("Referer", BASE.toUtf8());
    req.setRawHeader("Accept", "text/html,application/json,*/*");
    // Long series chapter lists exceed Qt's default 10MB decompressed cap.
    req.setDecompressedSafetyCheckThreshold(-1);
    return req;
}

// Strip the "/Comic/" prefix off a series href → bare slug ("Invincible").
static QString slugFromComicHref(const QString& href)
{
    QString s = href;
    if (s.startsWith(QLatin1String("/Comic/")))
        s.remove(0, QStringLiteral("/Comic/").size());
    return s;
}

// ── Search (HTML results page) ──────────────────────────────────────────────
void ReadComicsScraper::search(const QString& query, int /*limit*/)
{
    QUrl url(BASE + "/Search/Comic");
    QUrlQuery q;
    q.addQueryItem("keyword", query);
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("ReadComicsOnline search failed: " + reply->errorString());
            return;
        }

        const QString html = QString::fromUtf8(reply->readAll());

        // Each result row: <a href="/Comic/<Name>"><img src="/Uploads/..."/>
        //                  <span class="title">Title</span></a>
        // The series href is single-segment (no further "/") — that excludes
        // item/issue links. The empty class="hot-label" dupe anchor has no
        // inner <img>: the mandatory <img> in the pattern IS the dupe filter
        // (it drops the label-only dupes), and the seen-set is the backup guard
        // against any residual duplicate href.
        static const QRegularExpression rowRe(
            QStringLiteral(
                R"RE(<a\s+href="(/Comic/[^"/]+)"[^>]*>\s*<img\s+src="([^"]+)"[^>]*>\s*<span\s+class="title">\s*(.*?)\s*</span>)RE"),
            QRegularExpression::DotMatchesEverythingOption);

        QList<MangaResult> results;
        QSet<QString> seen;
        auto it = rowRe.globalMatch(html);
        while (it.hasNext()) {
            auto m = it.next();
            const QString href = m.captured(1);
            if (seen.contains(href))
                continue;
            seen.insert(href);

            MangaResult r;
            r.url   = href;
            r.id    = slugFromComicHref(href);
            r.title = m.captured(3).trimmed();
            r.title.remove(QRegularExpression("<[^>]*>"));
            r.title = r.title.trimmed();

            const QString cover = m.captured(2).trimmed();
            r.thumbnailUrl = cover.startsWith(QLatin1String("http")) ? cover : (BASE + cover);

            r.source = "readcomicsonline";
            r.type   = "comic";

            if (!r.title.isEmpty() && !r.id.isEmpty())
                results.append(r);
        }

        emit searchFinished(results);
    });
}

// ── Chapters / editions (HTML scrape) ───────────────────────────────────────
void ReadComicsScraper::fetchChapters(const QString& seriesSlug)
{
    QUrl url(BASE + "/Comic/" + seriesSlug);

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

    // Item link shape (capital-C scheme):
    //   <a  href="/Comic/<slug>/<Item-Slug>?id=<n>" title="...">Label</a>
    // cap(1)=item slug ("Issue-144" / "TPB-25-..."), cap(2)=optional "?id=n",
    // cap(3)=label. Item slugs are NAMED, not numeric — the chapter number is
    // recovered from the "#N" in the label, then from a number in the slug.
    QRegularExpression chRe(
        QStringLiteral(
            R"RE(<a\s+href="/Comic/%1/([^"?/]+)(\?[^"]*)?"[^>]*>\s*(.*?)\s*</a>)RE")
            .arg(QRegularExpression::escape(slug)),
        QRegularExpression::DotMatchesEverythingOption);

    // Must match the explicit "#N" form. Optional-# version matched the
    // year inside the series title prefix (e.g. "Invincible (2003) #32"
    // matched 2003). When no "#N" is present, the slug-number fallback applies.
    static const QRegularExpression numRe(R"(#(\d+(?:\.\d+)?))");
    static const QRegularExpression slugNumRe(R"((\d+(?:\.\d+)?))");

    QSet<QString> seen;  // items appear twice (cover-thumb link + text link)
    auto matches = chRe.globalMatch(html);
    while (matches.hasNext()) {
        auto m = matches.next();
        const QString itemSlug = m.captured(1);
        const QString query    = m.captured(2);  // e.g. "?id=130552"

        ChapterInfo ch;
        ch.id  = slug + "/" + itemSlug;            // clean, no query
        ch.url = "/Comic/" + slug + "/" + itemSlug + query;
        if (seen.contains(ch.id))
            continue;
        seen.insert(ch.id);

        ch.name = m.captured(3).trimmed();
        ch.name.remove(QRegularExpression("<[^>]*>"));
        ch.name = ch.name.trimmed();
        ch.source = "readcomicsonline";

        auto nm = numRe.match(ch.name);
        if (nm.hasMatch()) {
            ch.chapterNumber = nm.captured(1).toDouble();
        } else {
            auto sm = slugNumRe.match(itemSlug);
            ch.chapterNumber = sm.hasMatch() ? sm.captured(1).toDouble() : 0.0;
        }

        chapters.append(ch);
    }

    // Sort ascending
    std::sort(chapters.begin(), chapters.end(), [](const ChapterInfo& a, const ChapterInfo& b) {
        return a.chapterNumber < b.chapterNumber;
    });

    return chapters;
}

// ── Pages (reader — dead on plain HTTP, kept best-effort) ────────────────────
void ReadComicsScraper::fetchPages(const QString& chapterId)
{
    // chapterId format: "slug/item-slug". Reader images on rcostation are
    // injected at runtime by an external obfuscation script (21wiz.com/s.js)
    // and are invisible to a plain HTTP fetch — parsePagesHtml returns empty.
    // Kept wired so the source degrades gracefully rather than erroring; the
    // Western catalogue path downloads via GetComics, not RCO's reader.
    QUrl url(BASE + "/Comic/" + chapterId);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, chapterId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch pages: " + reply->errorString());
            return;
        }

        auto parts = chapterId.split('/');
        QString slug  = parts.value(0);
        QString issue = parts.value(1);

        auto html = QString::fromUtf8(reply->readAll());
        emit pagesReady(parsePagesHtml(html, slug, issue));
    });
}

QList<PageInfo> ReadComicsScraper::parsePagesHtml(const QString& html, const QString& slug, const QString& issue)
{
    QList<PageInfo> pages;

    // Legacy readcomicsonline shape: var pages = [{"page_image":"01.jpg"}, ...].
    // rcostation obfuscates this (21wiz); the match simply fails → empty list.
    static const QRegularExpression pagesRe(
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

// ── Detail (v1 merger) ──────────────────────────────────────────────────────
void ReadComicsScraper::fetchDetail(const MangaResult& preview)
{
    // preview.url is stored relative ("/Comic/<slug>" — see search()).
    // Reconstruct against BASE so we hit the actual host.
    const QString fullUrl = preview.url.startsWith(QLatin1String("http"))
                                ? preview.url
                                : (BASE + preview.url);
    auto* reply = m_nam->get(makeRequest(QUrl(fullUrl)));
    QPointer<ReadComicsScraper> self(this);
    connect(reply, &QNetworkReply::finished, this, [reply, self, preview]() {
        reply->deleteLater();
        if (!self) return;
        if (reply->error() != QNetworkReply::NoError) {
            emit self->errorOccurred(QString("readcomicsonline fetchDetail: %1")
                                     .arg(reply->errorString()));
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());

        MangaSeriesDetail detail;
        detail.preview   = preview;
        detail.sourceUrl = preview.url;

        // Selectors pinned against a live rcostation series page (Invincible),
        // 2026-05-31. Metadata sits in <span class="info">Label:</span> rows.

        // Summary: <span class="info">Summary:</span> <p>TEXT</p>
        static const QRegularExpression kSummary(
            R"RX(Summary:\s*</span>\s*<p>([\s\S]*?)</p>)RX");
        auto sm = kSummary.match(html);
        if (sm.hasMatch()) {
            QString s = sm.captured(1).trimmed();
            s.remove(QRegularExpression("<[^>]*>"));
            detail.synopsis = s.trimmed();
        }

        // Genres: anchors into /Genre/<slug>.
        static const QRegularExpression kGenre(
            R"RX(<a[^>]+href="/Genre/[^"]+"[^>]*>([^<]+)</a>)RX");
        auto gi = kGenre.globalMatch(html);
        while (gi.hasNext()) detail.genres.append(gi.next().captured(1).trimmed());

        // Author: the Writer credit (anchor into /Writer/<slug>).
        static const QRegularExpression kWriter(
            R"RX(Writer:\s*</span>\s*(?:&nbsp;)?\s*<a[^>]*>([^<]+)</a>)RX");
        auto wm = kWriter.match(html);
        if (wm.hasMatch()) detail.author = wm.captured(1).trimmed();

        // Year: "Publication date:" → first 4-digit run ("January 2003").
        static const QRegularExpression kYear(
            R"RX(Publication date:\s*</span>[\s\S]*?(\d{4}))RX");
        auto ym = kYear.match(html);
        if (ym.hasMatch()) detail.year = ym.captured(1);

        // Status: "Status:" → the bare word that follows.
        static const QRegularExpression kStatus(
            R"RX(Status:\s*</span>\s*(?:&nbsp;)?\s*([A-Za-z]+))RX");
        auto stm = kStatus.match(html);
        if (stm.hasMatch()) detail.status = stm.captured(1).trimmed();

        // Hero cover: <link rel="image_src" href="/Uploads/..."/>.
        static const QRegularExpression kHero(
            R"RX(<link[^>]+rel="image_src"[^>]+href="([^"]+)")RX");
        auto hm = kHero.match(html);
        if (hm.hasMatch()) {
            const QString cover = hm.captured(1).trimmed();
            detail.heroCoverUrl = cover.startsWith(QLatin1String("http")) ? cover : (BASE + cover);
        } else {
            detail.heroCoverUrl = preview.thumbnailUrl;
        }

        // The series page already carries the full edition list — populate
        // cachedChapters so the detail view can skip a second round-trip.
        detail.cachedChapters = ReadComicsScraper::parseChaptersHtml(html, preview.id);

        emit self->detailReady(detail);
    });
}
