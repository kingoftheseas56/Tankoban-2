#include "ExtTorrentsIndexer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>

static const QString EXT_BASE = "https://extto.org";

ExtTorrentsIndexer::ExtTorrentsIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
}

void ExtTorrentsIndexer::search(const QString& query, int limit, const QString& categoryId)
{
    m_rows.clear();
    m_pendingDetails = 0;
    m_limit = qBound(1, limit, 200);
    m_urlIndex = 0;

    QString q = QString::fromUtf8(QUrl::toPercentEncoding(query));
    QString cat = categoryId.trimmed();
    QString catParam = cat.isEmpty() ? "" : "&cat=" + QString::fromUtf8(QUrl::toPercentEncoding(cat));

    // Multiple candidate URL patterns — try in order until one works
    m_candidateUrls.clear();
    m_candidateUrls << EXT_BASE + "/browse/?q=" + q + catParam;
    m_candidateUrls << EXT_BASE + "/browse/?q=" + q + "&with_adult=1" + catParam;
    m_candidateUrls << EXT_BASE + "/advanced-search/?q=" + q + catParam;
    m_candidateUrls << EXT_BASE + "/search/?q=" + q;
    m_candidateUrls << EXT_BASE + "/search?search=" + q;
    m_candidateUrls << EXT_BASE + "/search/" + q + "/1/";

    tryNextUrl();
}

void ExtTorrentsIndexer::tryNextUrl()
{
    if (m_urlIndex >= m_candidateUrls.size()) {
        emit searchFinished({});
        return;
    }

    QUrl url(m_candidateUrls[m_urlIndex]);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "text/html,*/*");

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onListPageFetched(reply);
    });
}

void ExtTorrentsIndexer::onListPageFetched(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        ++m_urlIndex;
        tryNextUrl();
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    m_rows = parseListPage(html);

    if (m_rows.isEmpty() && m_urlIndex + 1 < m_candidateUrls.size()) {
        ++m_urlIndex;
        tryNextUrl();
        return;
    }

    if (m_rows.isEmpty()) {
        emit searchFinished({});
        return;
    }

    fetchDetailPages();
}

QList<ExtTorrentsIndexer::ListRow> ExtTorrentsIndexer::parseListPage(const QString& html)
{
    QList<ListRow> rows;

    int tbodyStart = html.indexOf("<tbody>");
    int tbodyEnd = html.indexOf("</tbody>");
    if (tbodyStart < 0 || tbodyEnd < 0)
        return rows;

    QString tbody = html.mid(tbodyStart, tbodyEnd - tbodyStart);

    static const QRegularExpression trRe("<tr[^>]*>(.*?)</tr>",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tdRe("<td[^>]*>(.*?)</td>",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression detailRe("<a[^>]*href=\"(/post-detail/[^\"]+)\"[^>]*>([^<]+)</a>");
    static const QRegularExpression magnetRe("href=\"(magnet:\\?[^\"]+)\"");
    static const QRegularExpression cssClassRe("class=\"([^\"]+)\"");
    static const QRegularExpression tagStripRe("<[^>]+>");

    auto trIter = trRe.globalMatch(tbody);
    while (trIter.hasNext()) {
        QString row = trIter.next().captured(1);

        auto tdIter = tdRe.globalMatch(row);
        QStringList cells;
        while (tdIter.hasNext())
            cells.append(tdIter.next().captured(1));

        if (cells.size() < 5)
            continue;

        ListRow lr;

        auto dm = detailRe.match(cells[0]);
        if (dm.hasMatch()) {
            lr.detailPath = dm.captured(1);
            lr.title = dm.captured(2).trimmed();
        }
        if (lr.title.isEmpty())
            continue;

        lr.title.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"");

        auto mm = magnetRe.match(row);
        if (mm.hasMatch())
            lr.magnetUri = mm.captured(1);

        // Category from CSS class
        auto cssIter = cssClassRe.globalMatch(row);
        while (cssIter.hasNext()) {
            QString css = cssIter.next().captured(1);
            QString cat = categoryFromCss(css);
            if (!cat.isEmpty()) {
                lr.category = cat;
                break;
            }
        }

        // Size from cell 1
        QString sizeText = cells[1];
        sizeText.remove(tagStripRe);
        lr.sizeBytes = parseSize(sizeText.trimmed());

        // Seeders from cell 4
        if (cells.size() > 4) {
            QString seedText = cells[4];
            seedText.remove(tagStripRe);
            lr.seeders = seedText.trimmed().toInt();
        }

        // Leechers from cell 5
        if (cells.size() > 5) {
            QString leechText = cells[5];
            leechText.remove(tagStripRe);
            lr.leechers = leechText.trimmed().toInt();
        }

        rows.append(lr);
    }

    return rows;
}

void ExtTorrentsIndexer::fetchDetailPages()
{
    int detailCount = 0;
    int maxDetail = qMin(m_rows.size(), qMin(m_limit, 30)); // Cap at 30 detail fetches
    for (int i = 0; i < maxDetail; ++i) {
        if (m_rows[i].magnetUri.isEmpty() && !m_rows[i].detailPath.isEmpty())
            ++detailCount;
    }

    if (detailCount == 0) {
        checkComplete();
        return;
    }

    m_pendingDetails = detailCount;

    for (int i = 0; i < maxDetail; ++i) {
        if (!m_rows[i].magnetUri.isEmpty() || m_rows[i].detailPath.isEmpty())
            continue;

        QString detailPath = m_rows[i].detailPath;
        QString detailUrl;
        if (detailPath.startsWith("http://") || detailPath.startsWith("https://"))
            detailUrl = detailPath;
        else if (detailPath.startsWith("//"))
            detailUrl = "https:" + detailPath;
        else
            detailUrl = EXT_BASE + detailPath;

        QUrl dUrl(detailUrl);
        QNetworkRequest req{dUrl};
        req.setHeader(QNetworkRequest::UserAgentHeader,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
        req.setRawHeader("Accept", "text/html,*/*");

        auto *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, i]() {
            onDetailFetched(reply, i);
        });
    }
}

void ExtTorrentsIndexer::onDetailFetched(QNetworkReply* reply, int index)
{
    reply->deleteLater();

    if (reply->error() == QNetworkReply::NoError) {
        QString html = QString::fromUtf8(reply->readAll());
        static const QRegularExpression magnetRe("href=\"(magnet:\\?[^\"]+)\"");
        auto m = magnetRe.match(html);
        if (m.hasMatch() && index < m_rows.size())
            m_rows[index].magnetUri = m.captured(1);
    }

    --m_pendingDetails;
    if (m_pendingDetails <= 0)
        checkComplete();
}

void ExtTorrentsIndexer::checkComplete()
{
    QList<TorrentResult> results;
    for (int i = 0; i < m_rows.size() && results.size() < m_limit; ++i) {
        const auto& lr = m_rows[i];
        if (lr.magnetUri.isEmpty())
            continue;

        TorrentResult r;
        r.title      = lr.title;
        r.magnetUri  = lr.magnetUri;
        r.sizeBytes  = lr.sizeBytes;
        r.seeders    = lr.seeders;
        r.leechers   = lr.leechers;
        r.sourceName = "ExtraTorrents";
        r.sourceKey  = "exttorrents";
        r.category   = lr.category;
        r.categoryId = lr.category.toLower().replace(' ', '_');
        results.append(r);
    }

    emit searchFinished(results);
}

QString ExtTorrentsIndexer::categoryFromCss(const QString& cssClass)
{
    static const QHash<QString, QString> map = {
        {"has-movie", "Movies"}, {"has-tv", "TV"}, {"has-game", "Games"},
        {"has-music", "Music"}, {"has-app", "Apps"}, {"has-documentary", "Documentaries"},
        {"has-anime", "Anime"}, {"has-books", "Books"}, {"has-book", "Books"},
        {"has-other", "Other"}, {"has-xxx", "XXX"},
    };

    for (const auto& token : cssClass.split(' ', Qt::SkipEmptyParts)) {
        auto it = map.find(token);
        if (it != map.end())
            return it.value();
    }
    return {};
}

qint64 ExtTorrentsIndexer::parseSize(const QString& text)
{
    static const QRegularExpression re(
        R"(([\d.,]+)\s*(bytes?|[kmgt]i?b))",
        QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(text);
    if (!m.hasMatch()) return 0;
    double num = m.captured(1).remove(',').toDouble();
    QString unit = m.captured(2).toLower();
    if (unit.startsWith("k"))       num *= 1024.0;
    else if (unit.startsWith("m"))  num *= 1024.0 * 1024.0;
    else if (unit.startsWith("g"))  num *= 1024.0 * 1024.0 * 1024.0;
    else if (unit.startsWith("t"))  num *= 1024.0 * 1024.0 * 1024.0 * 1024.0;
    return static_cast<qint64>(num);
}
