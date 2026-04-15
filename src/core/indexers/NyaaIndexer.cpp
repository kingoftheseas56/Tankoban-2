#include "NyaaIndexer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QDebug>

static constexpr int NYAA_PAGE_SIZE = 75;
static constexpr int NYAA_MAX_PAGES = 4;

NyaaIndexer::NyaaIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
    loadPersistedHealth();
}

void NyaaIndexer::search(const QString& query, int limit, const QString& categoryId)
{
    m_accumulated.clear();
    m_seenHashes.clear();
    fetchPage(query, categoryId, 1, qBound(1, limit, 300));
}

void NyaaIndexer::fetchPage(const QString& query, const QString& categoryId, int page, int limit)
{
    QString cat = categoryId.trimmed();
    if (cat.isEmpty()) cat = "0_0";

    QUrl url("https://nyaa.si/");
    QUrlQuery q;
    q.addQueryItem("f", "0");
    q.addQueryItem("c", cat);
    q.addQueryItem("q", query);
    q.addQueryItem("s", "size");
    q.addQueryItem("o", "desc");
    if (page > 1)
        q.addQueryItem("p", QString::number(page));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,*/*");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    startRequestTimer();
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        onPageFetched(reply, query, categoryId, page, limit);
    });
}

void NyaaIndexer::onPageFetched(QNetworkReply* reply, const QString& query,
                                 const QString& categoryId, int page, int limit)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        markError(reply);
        if (m_accumulated.isEmpty())
            emit searchError(reply->errorString());
        else
            emit searchFinished(m_accumulated);
        return;
    }

    markSuccess();

    QString html = QString::fromUtf8(reply->readAll());
    auto pageResults = parseHtml(html);

    // Deduplicate and accumulate
    for (const auto& r : pageResults) {
        static const QRegularExpression btihRe("btih:([a-fA-F0-9]{40})", QRegularExpression::CaseInsensitiveOption);
        auto m = btihRe.match(r.magnetUri);
        QString key = m.hasMatch() ? m.captured(1).toLower() : r.magnetUri.toLower();

        if (m_seenHashes.contains(key))
            continue;
        m_seenHashes.insert(key);
        m_accumulated.append(r);

        if (m_accumulated.size() >= limit) {
            emit searchFinished(m_accumulated);
            return;
        }
    }

    // Fetch next page if needed
    bool hasMore = pageResults.size() >= NYAA_PAGE_SIZE;
    bool underLimit = m_accumulated.size() < limit;
    bool underMaxPages = page < NYAA_MAX_PAGES;

    if (hasMore && underLimit && underMaxPages) {
        fetchPage(query, categoryId, page + 1, limit);
    } else {
        emit searchFinished(m_accumulated);
    }
}

// ── HTML parsing ────────────────────────────────────────────────────────────
QList<TorrentResult> NyaaIndexer::parseHtml(const QString& html)
{
    QList<TorrentResult> results;

    // Find the tbody
    int tbodyStart = html.indexOf("<tbody>");
    int tbodyEnd = html.indexOf("</tbody>");
    if (tbodyStart < 0 || tbodyEnd < 0)
        return results;

    QString tbody = html.mid(tbodyStart, tbodyEnd - tbodyStart);

    // Split into rows
    static const QRegularExpression trRe("<tr[^>]*>");
    auto trParts = tbody.split(trRe, Qt::SkipEmptyParts);

    static const QRegularExpression tdRe("<td[^>]*>(.*?)</td>", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression magnetRe("href=\"(magnet:\\?[^\"]+)\"");
    static const QRegularExpression titleLinkRe("<a[^>]*href=\"(/view/\\d+)\"[^>]*>([^<]+)</a>");
    static const QRegularExpression catLinkRe("c=([^\"&]+)");
    static const QRegularExpression tagStripRe("<[^>]+>");
    static const QRegularExpression dnRe("[?&]dn=([^&]+)");
    static const QRegularExpression btihRe("btih:([a-fA-F0-9]{40})", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression timestampRe("data-timestamp=\"(\\d+)\"");

    for (const auto& row : trParts) {
        // Extract all <td> contents
        auto tdIter = tdRe.globalMatch(row);
        QStringList cells;
        while (tdIter.hasNext())
            cells.append(tdIter.next().captured(1));

        if (cells.size() < 7)
            continue;

        // Column 1: Category
        QString catId;
        auto catMatch = catLinkRe.match(cells[0]);
        if (catMatch.hasMatch())
            catId = catMatch.captured(1);

        // Column 2: Title (find /view/ link, not comments link)
        QString title;
        QString detailPath;
        auto titleMatch = titleLinkRe.match(cells[1]);
        if (titleMatch.hasMatch()) {
            detailPath = titleMatch.captured(1);
            title = titleMatch.captured(2).trimmed();
        }

        // Column 3: Magnet link
        QString magnet;
        auto magnetMatch = magnetRe.match(cells[2]);
        if (magnetMatch.hasMatch())
            magnet = magnetMatch.captured(1);

        if (magnet.isEmpty())
            continue;

        // Fallback title from magnet dn=
        if (title.isEmpty()) {
            auto dnMatch = dnRe.match(magnet);
            if (dnMatch.hasMatch())
                title = QUrl::fromPercentEncoding(dnMatch.captured(1).toUtf8()).replace('+', ' ');
        }
        if (title.isEmpty())
            continue;

        // HTML entity decode
        title.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"");

        // Column 4: Size
        QString sizeText = cells[3];
        sizeText.remove(tagStripRe);
        qint64 sizeBytes = parseSize(sizeText.trimmed());

        // Column 6: Seeders
        QString seedText = cells[5];
        seedText.remove(tagStripRe);
        int seeders = seedText.trimmed().toInt();

        // Column 7: Leechers
        QString leechText = cells[6];
        leechText.remove(tagStripRe);
        int leechers = leechText.trimmed().toInt();

        TorrentResult r;
        r.title      = title;
        r.magnetUri  = magnet;
        r.sizeBytes  = sizeBytes;
        r.seeders    = seeders;
        r.leechers   = leechers;
        r.sourceName = "Nyaa";
        r.sourceKey  = "nyaa";
        r.categoryId = catId;

        auto ihMatch = btihRe.match(magnet);
        if (ihMatch.hasMatch())
            r.infoHash = canonicalizeInfoHash(ihMatch.captured(1));
        if (r.infoHash.isEmpty())
            qDebug() << "[NyaaIndexer] infoHash missing for:" << title;

        auto tsMatch = timestampRe.match(row);
        if (tsMatch.hasMatch())
            r.publishDate = QDateTime::fromSecsSinceEpoch(tsMatch.captured(1).toLongLong());

        if (!detailPath.isEmpty())
            r.detailsUrl = QStringLiteral("https://nyaa.si") + detailPath;

        results.append(r);
    }

    return results;
}

// ── Size string parsing ─────────────────────────────────────────────────────
qint64 NyaaIndexer::parseSize(const QString& text)
{
    static const QRegularExpression sizeRe(
        R"(([\d.,]+)\s*(bytes?|[kmgt]i?b))",
        QRegularExpression::CaseInsensitiveOption
    );

    auto m = sizeRe.match(text);
    if (!m.hasMatch())
        return 0;

    QString numStr = m.captured(1).remove(',');
    double num = numStr.toDouble();
    QString unit = m.captured(2).toLower();

    if (unit.startsWith("k"))       num *= 1024.0;
    else if (unit.startsWith("m"))  num *= 1024.0 * 1024.0;
    else if (unit.startsWith("g"))  num *= 1024.0 * 1024.0 * 1024.0;
    else if (unit.startsWith("t"))  num *= 1024.0 * 1024.0 * 1024.0 * 1024.0;

    return static_cast<qint64>(num);
}
