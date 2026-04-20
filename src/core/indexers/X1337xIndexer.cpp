#include "X1337xIndexer.h"
#include "CloudflareCookieHarvester.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QSettings>
#include <QDateTime>

static const QString X1337X_BASE = "https://1337x.to";

X1337xIndexer::X1337xIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
    loadPersistedHealth();

    // Harvester is a singleton parented to QCoreApplication; connections
    // auto-disconnect when this indexer is destroyed. Both slots filter
    // on indexerId so other subscribers (a future harvest target) don't
    // false-trigger.
    auto* harvester = CloudflareCookieHarvester::instance();
    connect(harvester, &CloudflareCookieHarvester::cookieHarvested,
            this, &X1337xIndexer::onCookieHarvested);
    connect(harvester, &CloudflareCookieHarvester::harvestFailed,
            this, &X1337xIndexer::onHarvestFailed);
}

void X1337xIndexer::setCredential(const QString& key, const QString& value)
{
    if (key != QLatin1String("cf_clearance"))
        return;
    QSettings s;
    s.setValue(QStringLiteral("tankorent/indexers/1337x/cf_clearance"), value);
    s.setValue(QStringLiteral("tankorent/indexers/1337x/cf_clearance_expires"),
               QDateTime::currentDateTime().addDays(7));
}

QString X1337xIndexer::credential(const QString& key) const
{
    if (key != QLatin1String("cf_clearance"))
        return {};
    return CloudflareCookieHarvester::cachedClearance(QStringLiteral("1337x"));
}

bool X1337xIndexer::haveValidClearance() const
{
    const QString cf = CloudflareCookieHarvester::cachedClearance(QStringLiteral("1337x"));
    if (cf.isEmpty())
        return false;
    const QDateTime expires = CloudflareCookieHarvester::clearanceExpires(QStringLiteral("1337x"));
    if (!expires.isValid() || expires < QDateTime::currentDateTime())
        return false;
    return true;
}

void X1337xIndexer::invalidateClearance()
{
    QSettings s;
    s.remove(QStringLiteral("tankorent/indexers/1337x/cf_clearance"));
    s.remove(QStringLiteral("tankorent/indexers/1337x/cf_clearance_expires"));
}

void X1337xIndexer::kickOffHarvest()
{
    m_health = IndexerHealth::CloudflareBlocked;
    savePersistedHealth();
    CloudflareCookieHarvester::instance()->harvest(
        QUrl(QStringLiteral("https://1337x.to")),
        QStringLiteral("1337x"));
}

void X1337xIndexer::onCookieHarvested(const QString& indexerId,
                                     const QString& /*cfClearance*/,
                                     const QString& /*userAgent*/)
{
    if (indexerId != QLatin1String("1337x") || !m_pendingSearch)
        return;
    m_pendingSearch = false;
    performSearch(m_pendingQuery, m_pendingLimit, m_pendingCategoryId);
}

void X1337xIndexer::onHarvestFailed(const QString& indexerId, const QString& reason)
{
    if (indexerId != QLatin1String("1337x") || !m_pendingSearch)
        return;
    m_pendingSearch = false;
    m_lastError = QStringLiteral("Cloudflare challenge unsolved: %1").arg(reason);
    savePersistedHealth();
    emit searchError(m_lastError);
}

void X1337xIndexer::search(const QString& query, int limit, const QString& categoryId)
{
    m_rows.clear();
    m_pendingDetails = 0;
    m_limit = qBound(1, limit, 200);
    m_retryAttempted = false;

    // Always stash — used both by the harvest-then-search path and by the
    // 503 retry-once path.
    m_pendingQuery      = query;
    m_pendingCategoryId = categoryId;
    m_pendingLimit      = m_limit;

    if (haveValidClearance()) {
        performSearch(query, m_limit, categoryId);
        return;
    }

    m_pendingSearch = true;
    kickOffHarvest();
}

void X1337xIndexer::performSearch(const QString& query, int limit, const QString& categoryId)
{
    const QString q = QString::fromUtf8(QUrl::toPercentEncoding(query));

    QString url;
    if (!categoryId.trimmed().isEmpty())
        url = QStringLiteral("%1/category-search/%2/%3/1/").arg(X1337X_BASE, q, categoryId.trimmed());
    else
        url = QStringLiteral("%1/search/%2/1/").arg(X1337X_BASE, q);

    QUrl reqUrl(url);
    QNetworkRequest req{reqUrl};

    const QString cf = CloudflareCookieHarvester::cachedClearance(QStringLiteral("1337x"));
    const QString ua = CloudflareCookieHarvester::cachedUserAgent(QStringLiteral("1337x"));
    const QByteArray userAgent = ua.isEmpty()
        ? QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)")
        : ua.toUtf8();
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    req.setRawHeader("Accept", "text/html,*/*");
    if (!cf.isEmpty())
        req.setRawHeader("Cookie", "cf_clearance=" + cf.toUtf8());
    req.setTransferTimeout(15000);

    startRequestTimer();
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, limit]() {
        onListPageFetched(reply, limit);
    });
}

void X1337xIndexer::onListPageFetched(QNetworkReply* reply, int limit)
{
    reply->deleteLater();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // CF can invalidate the clearance cookie server-side before our TTL
    // expires. The server responds with 503 (often with a fresh challenge
    // page). Invalidate + re-harvest + retry exactly once per search.
    if (httpStatus == 503 && !m_retryAttempted) {
        m_retryAttempted = true;
        invalidateClearance();
        m_pendingLimit  = limit;
        m_pendingSearch = true;
        kickOffHarvest();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        markError(reply);
        emit searchError(reply->errorString());
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    m_rows = parseListPage(html);

    if (m_rows.isEmpty()) {
        emit searchFinished({});
        return;
    }

    markSuccess();
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
        req.setTransferTimeout(15000);

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
    static const QRegularExpression btihRe("btih:([a-fA-F0-9]{40})",
        QRegularExpression::CaseInsensitiveOption);

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

        auto ihMatch = btihRe.match(lr.magnetUri);
        if (ihMatch.hasMatch())
            r.infoHash = canonicalizeInfoHash(ihMatch.captured(1));
        if (r.infoHash.isEmpty())
            qDebug() << "[X1337xIndexer] infoHash missing for:" << lr.title;

        if (!lr.detailPath.isEmpty())
            r.detailsUrl = X1337X_BASE + lr.detailPath;

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
