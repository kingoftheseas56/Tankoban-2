#include "core/book/FictionDbClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

namespace {

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent",
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/120.0 Safari/537.36");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml");
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

// Content attribute of an og: meta tag.
QString ogTag(const QString& html, const QString& prop)
{
    QRegularExpression re(
        QStringLiteral("<meta\\s+property=\"og:%1\"\\s+content=\"([^\"]*)\"").arg(prop),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(html);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

// Full synopsis from the book page's #description tab-pane. og:description is
// SEO-capped at ~200 chars (cuts mid-sentence); this div carries the whole
// multi-paragraph blurb. Returns empty if the div is absent (caller falls back
// to og:description).
QString descriptionBody(const QString& html)
{
    QRegularExpression divRe(
        QStringLiteral("id=\"description\"[^>]*>(.*?)</div>"),
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    const auto m = divRe.match(html);
    if (!m.hasMatch()) return QString();

    QString body = m.captured(1);
    body.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    body.remove(QRegularExpression(QStringLiteral("<[^>]*>")));  // strip any other tags

    // Decode the HTML entities that show up in prose.
    static const QList<QPair<QString, QString>> ents = {
        {QStringLiteral("&amp;"),    QStringLiteral("&")},
        {QStringLiteral("&lt;"),     QStringLiteral("<")},
        {QStringLiteral("&gt;"),     QStringLiteral(">")},
        {QStringLiteral("&quot;"),   QStringLiteral("\"")},
        {QStringLiteral("&#39;"),    QStringLiteral("'")},
        {QStringLiteral("&#x27;"),   QStringLiteral("'")},
        {QStringLiteral("&nbsp;"),   QStringLiteral(" ")},
        {QStringLiteral("&hellip;"), QStringLiteral("…")},
        {QStringLiteral("&mdash;"),  QStringLiteral("—")},
        {QStringLiteral("&ndash;"),  QStringLiteral("–")},
        {QStringLiteral("&rsquo;"),  QStringLiteral("’")},
        {QStringLiteral("&lsquo;"),  QStringLiteral("‘")},
        {QStringLiteral("&rdquo;"),  QStringLiteral("”")},
        {QStringLiteral("&ldquo;"),  QStringLiteral("“")},
    };
    for (const auto& e : ents) body.replace(e.first, e.second);

    // Normalize whitespace: the source indents each line, so blank "lines"
    // between <br><br> paragraphs carry spaces/tabs and wouldn't collapse on
    // newline-count alone. Strip whitespace around every newline first, then
    // cap paragraph gaps at a single blank line, then collapse inline runs.
    body.replace(QRegularExpression(QStringLiteral("[ \\t]*\\r?\\n[ \\t]*")),
                 QStringLiteral("\n"));
    body.replace(QRegularExpression(QStringLiteral("\\n{2,}")), QStringLiteral("\n\n"));
    body.replace(QRegularExpression(QStringLiteral("[ \\t]{2,}")), QStringLiteral(" "));
    return body.trimmed();
}

// "Herbert, Frank" -> "Frank Herbert"; passthrough if no comma.
QString flipAuthor(const QString& raw)
{
    const QString a = raw.trimmed();
    const int c = a.indexOf(QStringLiteral(", "));
    if (c <= 0) return a;
    return a.mid(c + 2).trimmed() + QLatin1Char(' ') + a.left(c).trimmed();
}

// "dune-messiah~frank-herbert~116215" -> "Dune Messiah" (title-case, stopwords lower).
// Used only as a fallback when the page doesn't expose a clean display title;
// the canonical title is fetched from the book page's og:title on click.
QString titleFromSlug(const QString& slug)
{
    static const QSet<QString> lower = {
        QStringLiteral("of"), QStringLiteral("the"), QStringLiteral("and"),
        QStringLiteral("to"), QStringLiteral("a"), QStringLiteral("in"),
        QStringLiteral("for"), QStringLiteral("on"), QStringLiteral("at")};
    const QString titlePart = slug.section(QLatin1Char('~'), 0, 0);
    const QStringList words = titlePart.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    QStringList out;
    for (int i = 0; i < words.size(); ++i) {
        QString w = words[i];
        if (i != 0 && lower.contains(w)) { out << w; continue; }
        if (!w.isEmpty()) w[0] = w[0].toUpper();
        out << w;
    }
    return out.join(QLatin1Char(' '));
}

// Strip leading/trailing hyphens.
QString trimDashes(QString x)
{
    while (x.startsWith(QLatin1Char('-'))) x.remove(0, 1);
    while (x.endsWith(QLatin1Char('-'))) x.chop(1);
    return x;
}

// Recover the author from a series slug + its display name. Slug shape is
// "<slugified-series-name>-<author-slug>~<id>", e.g.
// "the-marnie-baranuik-files-aj-aalto~42743" + "The Marnie Baranuik Files"
// -> author-slug "aj-aalto" -> "Aj Aalto".
QString authorFromSlug(const QString& slug, const QString& seriesName)
{
    QString s = slug.section(QLatin1Char('~'), 0, 0);  // drop ~id
    QString namePart = seriesName.toLower();
    namePart.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    namePart = trimDashes(namePart);
    QString authorSlug = s;
    if (!namePart.isEmpty() && authorSlug.startsWith(namePart))
        authorSlug = authorSlug.mid(namePart.size());
    authorSlug = trimDashes(authorSlug);
    if (authorSlug.isEmpty()) return QString();
    QStringList words = authorSlug.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    for (auto& w : words)
        if (!w.isEmpty()) w[0] = w[0].toUpper();
    return words.join(QLatin1Char(' '));
}

}  // namespace

FictionDbClient::FictionDbClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

QString FictionDbClient::slugFromHref(const QString& href, const QString& kind)
{
    // Slug ends in ~<id>. Tilde count varies: title slugs are
    // <title>~<author>~<id> (2 tildes), series slugs are <name>~<id> (1 tilde).
    // A flexible "<chars incl ~>~<digits>" pattern handles both; non-slug links
    // like series-lists.htm (no trailing ~<digits>) yield empty.
    QRegularExpression re(
        QStringLiteral("/%1/([a-z0-9~-]+~\\d+)\\.htm").arg(kind),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(href);
    return m.hasMatch() ? m.captured(1) : QString();
}

// ── Pure parsers ──────────────────────────────────────────────────────────

BookCatalogueResult FictionDbClient::parseBookPage(const QString& html, const QString& bookId)
{
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("fictiondb:%1").arg(bookId);
    r.isSeries = false;

    const QString ogTitle = ogTag(html, QStringLiteral("title"));   // "Dune by Frank Herbert"
    const int byIdx = ogTitle.lastIndexOf(QStringLiteral(" by "));
    if (byIdx > 0) {
        r.title = ogTitle.left(byIdx).trimmed();
        r.author = ogTitle.mid(byIdx + 4).trimmed();
    } else {
        r.title = ogTitle;
    }
    r.isbn = ogTag(html, QStringLiteral("isbn"));
    r.coverUrl = ogTag(html, QStringLiteral("image"));
    // Prefer the full #description body; og:description is SEO-capped at ~200 chars.
    r.description = descriptionBody(html);
    if (r.description.isEmpty())
        r.description = ogTag(html, QStringLiteral("description"));

    // Year: JSON-LD style  "datePublished": "1965-01-15"
    QRegularExpression pubRe(QStringLiteral("datePublished\"\\s*:\\s*\"(\\d{4})"));
    const auto pm = pubRe.match(html);
    if (pm.hasMatch()) r.year = pm.captured(1);

    // Series membership: a positioned series link "Dune Chronicles - 1".
    // The slug requires a trailing ~<id> so the nav link (series-lists.htm)
    // and the non-positioned "Dune" series link don't match.
    QRegularExpression serRe(
        QStringLiteral("href=\"\\.\\./series/([a-z0-9-]+~\\d+)\\.htm\"[^>]*>\\s*([^<]+?)\\s*-\\s*(\\d+)\\s*<"),
        QRegularExpression::CaseInsensitiveOption);
    const auto sm = serRe.match(html);
    if (sm.hasMatch()) {
        r.seriesId = sm.captured(1);
        r.seriesName = sm.captured(2).trimmed();
        r.seriesPosition = sm.captured(3).toInt();
    }
    return r;
}

QList<BookCatalogueResult> FictionDbClient::parseSeriesPage(const QString& html,
                                                            const QString& seriesId)
{
    QList<BookCatalogueResult> books;

    QString seriesName;
    QRegularExpression h1Re(QStringLiteral("<h1[^>]*>\\s*([^<]+?)\\s*<"),
                            QRegularExpression::CaseInsensitiveOption);
    const auto h1 = h1Re.match(html);
    if (h1.hasMatch()) seriesName = h1.captured(1).trimmed();

    // Books in document order = reading order. Dedupe (cover img + title text
    // can both link to the same /title/ slug within one row).
    QRegularExpression linkRe(
        QStringLiteral("href=\"\\.\\./title/([a-z0-9-]+~[a-z0-9-]*~\\d+)\\.htm\""),
        QRegularExpression::CaseInsensitiveOption);
    auto it = linkRe.globalMatch(html);
    QSet<QString> seen;
    int pos = 1;
    while (it.hasNext()) {
        const QString slug = it.next().captured(1);
        if (seen.contains(slug)) continue;
        seen.insert(slug);
        BookCatalogueResult b;
        b.catalogueId = QStringLiteral("fictiondb:%1").arg(slug);
        b.title = titleFromSlug(slug);
        b.author = seriesName.isEmpty() ? QString() : QString();  // filled on book fetch
        b.isSeries = false;
        b.seriesId = seriesId;
        b.seriesName = seriesName;
        b.seriesPosition = pos++;
        books.append(b);
    }
    for (auto& b : books) b.seriesTotal = books.size();
    return books;
}

QList<BookCatalogueResult> FictionDbClient::parseSearchPage(const QString& html)
{
    QList<BookCatalogueResult> books;
    // Flat schema.org/Book table. Iterate each <tr ...schema.org/Book...> block.
    QRegularExpression rowRe(
        QStringLiteral("<tr[^>]*itemtype=\"[^\"]*schema\\.org/Book\"[^>]*>(.*?)</tr>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    QRegularExpression urlRe(
        QStringLiteral("itemprop=\"url\"\\s+href=\"\\.\\./title/([a-z0-9-]+~[a-z0-9-]*~\\d+)\\.htm\""),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpression nameRe(QStringLiteral("itemprop=['\"]name['\"]>\\s*([^<]+?)\\s*</span>"),
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpression authRe(QStringLiteral("itemprop=\"author\"[^>]*>\\s*([^<]+?)\\s*</a>"),
                              QRegularExpression::CaseInsensitiveOption);

    auto rows = rowRe.globalMatch(html);
    while (rows.hasNext()) {
        const QString row = rows.next().captured(1);
        const auto um = urlRe.match(row);
        if (!um.hasMatch()) continue;
        BookCatalogueResult b;
        b.catalogueId = QStringLiteral("fictiondb:%1").arg(um.captured(1));
        const auto nm = nameRe.match(row);
        b.title = nm.hasMatch() ? nm.captured(1).trimmed() : titleFromSlug(um.captured(1));
        const auto am = authRe.match(row);
        if (am.hasMatch()) b.author = flipAuthor(am.captured(1));
        b.isSeries = false;
        books.append(b);
    }
    return books;
}

QList<SeriesIndexEntry> FictionDbClient::parseSeriesIndexPage(const QString& html, bool* hasNextPage)
{
    QList<SeriesIndexEntry> out;
    // Each series row: <a class="hghlt" href="../series/<slug>~<id>.htm">Name</a>.
    QRegularExpression rowRe(
        QStringLiteral("href=\"\\.\\./series/([a-z0-9~._-]+~\\d+)\\.htm\"[^>]*>\\s*([^<]+?)\\s*</a>"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = rowRe.globalMatch(html);
    QSet<QString> seen;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString slug = m.captured(1);
        if (slug.startsWith(QLatin1String("author-series"))) continue;  // A-Z pagination nav
        if (seen.contains(slug)) continue;
        seen.insert(slug);
        SeriesIndexEntry e;
        e.seriesId   = slug;
        e.seriesName = m.captured(2).trimmed();
        if (e.seriesName.isEmpty()) continue;
        e.author = authorFromSlug(slug, e.seriesName);
        out.append(e);
    }
    if (hasNextPage) {
        // The "next" control is a »/&raquo; anchor pointing at the next page.
        QRegularExpression nextRe(
            QStringLiteral("href=\"[^\"]*author-series~[a-z0-9]+~\\d+\\.htm\"[^>]*>\\s*(?:&raquo;|»|Next)"),
            QRegularExpression::CaseInsensitiveOption);
        *hasNextPage = nextRe.match(html).hasMatch();
    }
    return out;
}

// ── Network methods ─────────────────────────────────────────────────────────

void FictionDbClient::search(const QString& query)
{
    QUrl url(QStringLiteral("%1/search/searchresults.htm").arg(kBase));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("srchtxt"), query);
    q.addQueryItem(QStringLiteral("styp"), QStringLiteral("5"));
    url.setQuery(q);
    auto* reply = m_nam->get(makeRequest(url));
    reply->setProperty("query", query);
    connect(reply, &QNetworkReply::finished, this, &FictionDbClient::onSearchReply);
}

void FictionDbClient::fetchBook(const QString& bookId)
{
    QUrl url(QStringLiteral("%1/title/%2.htm").arg(kBase, bookId));
    auto* reply = m_nam->get(makeRequest(url));
    reply->setProperty("bookId", bookId);
    connect(reply, &QNetworkReply::finished, this, &FictionDbClient::onBookReply);
}

void FictionDbClient::fetchSeries(const QString& seriesId)
{
    QUrl url(QStringLiteral("%1/series/%2.htm").arg(kBase, seriesId));
    auto* reply = m_nam->get(makeRequest(url));
    reply->setProperty("seriesId", seriesId);
    connect(reply, &QNetworkReply::finished, this, &FictionDbClient::onSeriesReply);
}

void FictionDbClient::fetchSeriesIndexPage(const QString& letter, int page)
{
    const QString path = page <= 1
        ? QStringLiteral("%1/series/author-series~%2.htm").arg(kBase, letter)
        : QStringLiteral("%1/series/author-series~%2~%3.htm").arg(kBase, letter).arg(page);
    auto* reply = m_nam->get(makeRequest(QUrl(path)));
    reply->setProperty("letter", letter);
    reply->setProperty("page", page);
    connect(reply, &QNetworkReply::finished, this, &FictionDbClient::onSeriesIndexReply);
}

void FictionDbClient::onSearchReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString query = reply->property("query").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(query, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    emit searchResults(query, parseSearchPage(html));
}

void FictionDbClient::onBookReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString bookId = reply->property("bookId").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit bookFailed(bookId, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    emit bookReady(bookId, parseBookPage(html, bookId));
}

void FictionDbClient::onSeriesReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString seriesId = reply->property("seriesId").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit seriesFailed(seriesId, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    const QList<BookCatalogueResult> books = parseSeriesPage(html, seriesId);
    const QString seriesName = books.isEmpty() ? QString() : books.first().seriesName;
    emit seriesReady(seriesId, seriesName, books);
}

void FictionDbClient::onSeriesIndexReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString letter = reply->property("letter").toString();
    const int page = reply->property("page").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit seriesIndexPageFailed(letter, page, reply->errorString());
        return;
    }
    bool hasNext = false;
    const QString html = QString::fromUtf8(reply->readAll());
    emit seriesIndexPageReady(letter, page, parseSeriesIndexPage(html, &hasNext), hasNext);
}
