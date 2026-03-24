#include "X1337xIndexer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QRegularExpression>

static const QString X1337X_BASE = "https://1337x.to";

X1337xIndexer::X1337xIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
}

void X1337xIndexer::search(const QString& query, int limit, const QString& categoryId)
{
    m_rows.clear();
    m_pendingDetails = 0;
    m_limit = qBound(1, limit, 200);

    QString q = QString::fromUtf8(QUrl::toPercentEncoding(query));

    QString url;
    if (!categoryId.trimmed().isEmpty())
        url = QStringLiteral("%1/category-search/%2/%3/1/").arg(X1337X_BASE, q, categoryId.trimmed());
    else
        url = QStringLiteral("%1/search/%2/1/").arg(X1337X_BASE, q);

    QUrl reqUrl(url);
    QNetworkRequest req{reqUrl};
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "text/html,*/*");

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onListPageFetched(reply, m_limit);
    });
}

void X1337xIndexer::onListPageFetched(QNetworkReply* reply, int limit)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit searchError(reply->errorString());
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    m_rows = parseListPage(html);

    if (m_rows.isEmpty()) {
        emit searchFinished({});
        return;
    }

    fetchDetailPages(m_rows, limit);
}

QList<X1337xIndexer::ListRow> X1337xIndexer::parseListPage(const QString& html)
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
    static const QRegularExpression detailRe("<a[^>]*href=\"(/torrent/[^\"]+)\"[^>]*>([^<]+)</a>");
    static const QRegularExpression magnetRe("href=\"(magnet:\\?[^\"]+)\"");
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

        // Cell 0: name column — title + detail path
        ListRow lr;
        auto dm = detailRe.match(cells[0]);
        if (dm.hasMatch()) {
            lr.detailPath = dm.captured(1);
            lr.title = dm.captured(2).trimmed();
        }
        if (lr.title.isEmpty())
            continue;

        lr.title.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"");

        // Check for inline magnet
        auto mm = magnetRe.match(row);
        if (mm.hasMatch())
            lr.magnetUri = mm.captured(1);

        // Cell 1: seeds
        QString seedText = cells[1];
        seedText.remove(tagStripRe);
        lr.seeders = seedText.trimmed().toInt();

        // Cell 2: leeches
        QString leechText = cells[2];
        leechText.remove(tagStripRe);
        lr.leechers = leechText.trimmed().toInt();

        // Cell 4 or 3: size (varies by page layout)
        for (int si = 3; si < cells.size(); ++si) {
            QString sizeText = cells[si];
            sizeText.remove(tagStripRe);
            qint64 sz = parseSize(sizeText.trimmed());
            if (sz > 0) {
                lr.sizeBytes = sz;
                break;
            }
        }

        rows.append(lr);
    }

    return rows;
}

void X1337xIndexer::fetchDetailPages(const QList<ListRow>& rows, int limit)
{
    // Count how many need detail fetching
    int detailCount = 0;
    for (int i = 0; i < rows.size() && i < limit; ++i) {
        if (rows[i].magnetUri.isEmpty() && !rows[i].detailPath.isEmpty())
            ++detailCount;
    }

    if (detailCount == 0) {
        checkComplete();
        return;
    }

    m_pendingDetails = detailCount;

    for (int i = 0; i < rows.size() && i < limit; ++i) {
        if (!rows[i].magnetUri.isEmpty() || rows[i].detailPath.isEmpty())
            continue;

        QUrl detailUrl(X1337X_BASE + rows[i].detailPath);
        QNetworkRequest req{detailUrl};
        req.setHeader(QNetworkRequest::UserAgentHeader,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
        req.setRawHeader("Accept", "text/html,*/*");

        auto *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, i]() {
            onDetailFetched(reply, i);
        });
    }
}

void X1337xIndexer::onDetailFetched(QNetworkReply* reply, int index)
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

void X1337xIndexer::checkComplete()
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
        r.sourceName = "1337x";
        r.sourceKey  = "1337x";
        r.categoryId = lr.categoryId;
        results.append(r);
    }

    emit searchFinished(results);
}

qint64 X1337xIndexer::parseSize(const QString& text)
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
