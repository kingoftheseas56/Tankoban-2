#include "EztvIndexer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QSettings>

static const QStringList EZTV_BASES = {
    "https://eztvx.to",
    "https://eztv.wf",
    "https://eztv.tf",
};

// Default cookie used when the user hasn't supplied one via the Sources
// panel. EZTV's site-provided sort/filter toggles live in cookies; without
// them the default listing excludes many releases.
static const char* kEztvDefaultCookie =
    "sort_no=100; q_filter=all; q_filter_web=on; q_filter_reality=on; q_filter_x265=on; layout=def_wlinks";

EztvIndexer::EztvIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
    loadPersistedHealth();
}

void EztvIndexer::setCredential(const QString& key, const QString& value)
{
    if (key != QLatin1String("cookie"))
        return;
    QSettings().setValue(QStringLiteral("tankorent/indexers/eztv/credentials/cookie"), value);
}

QString EztvIndexer::credential(const QString& key) const
{
    if (key != QLatin1String("cookie"))
        return {};
    return QSettings().value(QStringLiteral("tankorent/indexers/eztv/credentials/cookie")).toString();
}

QString EztvIndexer::normalizeSlug(const QString& query)
{
    QString q = query.trimmed();
    static const QRegularExpression seasonRe("\\bS\\d{2,3}\\b(?!E\\d{2,3})", QRegularExpression::CaseInsensitiveOption);
    q.replace(seasonRe, " ");
    q.replace('&', ' ');
    static const QRegularExpression nonAlphaRe("[^A-Za-z0-9\\s-]+");
    q.replace(nonAlphaRe, " ");
    static const QRegularExpression spacesRe("\\s+");
    q.replace(spacesRe, "-");
    while (q.startsWith('-')) q.remove(0, 1);
    while (q.endsWith('-')) q.chop(1);
    return q.toLower();
}

void EztvIndexer::search(const QString& query, int limit, const QString& /*categoryId*/)
{
    m_slug = normalizeSlug(query);
    m_limit = limit;
    m_mirrors = EZTV_BASES;
    m_mirrorIndex = 0;

    if (m_slug.isEmpty()) {
        emit searchFinished({});
        return;
    }

    tryNextMirror();
}

void EztvIndexer::tryNextMirror()
{
    if (m_mirrorIndex >= m_mirrors.size()) {
        emit searchError("All EZTV mirrors failed");
        return;
    }

    QString base = m_mirrors[m_mirrorIndex];
    QUrl url(QStringLiteral("%1/search/%2").arg(base, m_slug));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "text/html,*/*");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    QString userCookie = credential(QStringLiteral("cookie"));
    const QByteArray cookie = userCookie.isEmpty()
        ? QByteArray(kEztvDefaultCookie)
        : userCookie.toUtf8();
    req.setRawHeader("Cookie", cookie);

    startRequestTimer();
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

void EztvIndexer::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        markError(reply);
        ++m_mirrorIndex;
        tryNextMirror();
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    auto results = parseHtml(html, m_limit);

    if (results.isEmpty() && m_mirrorIndex + 1 < m_mirrors.size()) {
        ++m_mirrorIndex;
        tryNextMirror();
        return;
    }

    markSuccess();
    emit searchFinished(results);
}

QList<TorrentResult> EztvIndexer::parseHtml(const QString& html, int limit)
{
    QList<TorrentResult> results;

    static const QRegularExpression rowRe(
        R"(<tr\s+name="hover"[^>]*class="[^"]*forum_header_border[^"]*"[^>]*>(.*?)</tr>)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tdRe("<td[^>]*>(.*?)</td>",
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression magnetRe("href=\"(magnet:\\?[^\"]+)\"");
    static const QRegularExpression titleAttrRe("<a[^>]*title=\"([^\"]+)\"");
    static const QRegularExpression titleAnchorRe("<a[^>]*>([^<]+)</a>");
    static const QRegularExpression tagStripRe("<[^>]+>");
    static const QRegularExpression nonDigitRe("[^\\d]");
    static const QRegularExpression btihRe("btih:([a-fA-F0-9]{40})", QRegularExpression::CaseInsensitiveOption);

    auto rowIter = rowRe.globalMatch(html);
    while (rowIter.hasNext() && results.size() < limit) {
        QString row = rowIter.next().captured(1);

        auto tdIter = tdRe.globalMatch(row);
        QStringList cells;
        while (tdIter.hasNext())
            cells.append(tdIter.next().captured(1));

        if (cells.size() < 6)
            continue;

        // Title from cell 1 (title attribute first, then anchor text)
        QString title;
        auto tm = titleAttrRe.match(cells[1]);
        if (tm.hasMatch())
            title = tm.captured(1).trimmed();
        if (title.isEmpty()) {
            auto ta = titleAnchorRe.match(cells[1]);
            if (ta.hasMatch())
                title = ta.captured(1).trimmed();
        }

        // Magnet from cell 2 or cell 1
        QString magnet;
        auto mm = magnetRe.match(cells[2]);
        if (mm.hasMatch())
            magnet = mm.captured(1);
        if (magnet.isEmpty()) {
            mm = magnetRe.match(cells[1]);
            if (mm.hasMatch())
                magnet = mm.captured(1);
        }

        if (title.isEmpty() || magnet.isEmpty())
            continue;

        title.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"");

        // Size from cell 3
        QString sizeText = cells[3];
        sizeText.remove(tagStripRe);
        qint64 sizeBytes = parseSize(sizeText.trimmed());

        // Seeders from cell 5
        QString seedText = cells[5];
        seedText.remove(tagStripRe);
        seedText.remove(nonDigitRe);
        int seeders = seedText.toInt();

        // Leechers from cell 6
        int leechers = 0;
        if (cells.size() > 6) {
            QString leechText = cells[6];
            leechText.remove(tagStripRe);
            leechText.remove(nonDigitRe);
            leechers = leechText.toInt();
        }

        TorrentResult r;
        r.title      = title;
        r.magnetUri  = magnet;
        r.sizeBytes  = sizeBytes;
        r.seeders    = seeders;
        r.leechers   = leechers;
        r.sourceName = "EZTV";
        r.sourceKey  = "eztv";
        r.categoryId = "tv";
        r.category   = "TV";

        auto ihMatch = btihRe.match(magnet);
        if (ihMatch.hasMatch())
            r.infoHash = canonicalizeInfoHash(ihMatch.captured(1));
        if (r.infoHash.isEmpty())
            qDebug() << "[EztvIndexer] infoHash missing for:" << title;

        results.append(r);
    }

    return results;
}

qint64 EztvIndexer::parseSize(const QString& text)
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
