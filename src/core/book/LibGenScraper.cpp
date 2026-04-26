#include "LibGenScraper.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

namespace {

// Primary mirror per 2026-04-21 reachability probe — libgen.li returns real
// HTML in ~1s with no CF challenge. Other mirrors (libgen.rs/.is/.st) time
// out from this network. Mirror failover deferred to a later batch.
constexpr const char* kLibGenBase = "https://libgen.li";

// User-Agent set on every request. LibGen doesn't enforce UA strictly, but
// a realistic string reduces the chance of heuristic flags from CF at the
// edge. Kept in sync with the UA string used elsewhere in Tankoban.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// HTTP timeout per request — LibGen is fast (1-3s typical) but a slow
// network blip shouldn't lock the UI. 15s matches Tankorent's indexer budget.
constexpr int kRequestTimeoutMs = 15'000;

// Strip HTML tags from a cell's innerHTML to get plain text. Not a full
// HTML parser — good enough for the small amount of markup inside <td>
// elements (mostly <a>/<span>/<br>/<font>).
QString stripTags(const QString& html)
{
    static const QRegularExpression kTagRe(QStringLiteral("<[^>]*>"));
    QString text = html;
    text.remove(kTagRe);
    // collapse whitespace runs
    static const QRegularExpression kWsRe(QStringLiteral("\\s+"));
    text = text.replace(kWsRe, QStringLiteral(" ")).trimmed();
    // decode a few common HTML entities that land in LibGen rows
    text.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    return text.trimmed();
}

} // namespace

LibGenScraper::LibGenScraper(QNetworkAccessManager* nam, QObject* parent)
    : BookScraper(nam, parent)
{
}

LibGenScraper::~LibGenScraper()
{
    cancelActiveReply();
}

void LibGenScraper::cancelActiveReply()
{
    if (!m_activeReply) return;
    disconnect(m_activeReply, nullptr, this, nullptr);
    m_activeReply->abort();
    m_activeReply->deleteLater();
    m_activeReply = nullptr;
}

void LibGenScraper::reset()
{
    m_mode = Mode::Idle;
    m_currentQuery.clear();
    m_currentMd5OrId.clear();
    cancelActiveReply();
}

void LibGenScraper::fail(const QString& reason)
{
    reset();
    emit errorOccurred(reason);
}

void LibGenScraper::failResolve(const QString& md5, const QString& reason)
{
    reset();
    emit downloadFailed(md5, reason);
}

void LibGenScraper::failCover(const QString& md5, const QString& reason)
{
    emit coverUrlFailed(md5, reason);
}

void LibGenScraper::search(const QString& query, int limit)
{
    if (m_mode != Mode::Idle) reset();

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(QStringLiteral("Empty query"));
        return;
    }

    m_mode         = Mode::Searching;
    m_currentQuery = trimmed;
    m_currentLimit = limit;

    QUrl target(QStringLiteral("%1/index.php").arg(kLibGenBase));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("req"), trimmed);
    // Novels-only default filter — excludes topic=c (comics/CBZ-CBR),
    // topic=s (scientific articles), topic=m (magazines), topic=r
    // (references). topic=l is LibGen-Books (main nonfiction + fiction
    // mixed collection); topic=f is Fiction (narrow novels subset).
    // Empirically verified 2026-04-22: topics=l+f drops all 9 comic
    // rows from "sapiens" result set, retains 18 EPUB + 6 FB2 book rows.
    // QUrlQuery percent-encodes [] to %5B%5D automatically.
    q.addQueryItem(QStringLiteral("topics[]"), QStringLiteral("l"));
    q.addQueryItem(QStringLiteral("topics[]"), QStringLiteral("f"));
    target.setQuery(q);

    qDebug() << "[LibGenScraper] search ->" << target.toString();

    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &LibGenScraper::onSearchReplyFinished);
}

void LibGenScraper::onSearchReplyFinished()
{
    if (!m_activeReply || m_mode != Mode::Searching) {
        if (m_activeReply) {
            m_activeReply->deleteLater();
            m_activeReply = nullptr;
        }
        return;
    }

    QNetworkReply* reply = m_activeReply;
    m_activeReply = nullptr;

    const QNetworkReply::NetworkError err = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        fail(QStringLiteral("LibGen search failed: %1").arg(reply->errorString()));
        return;
    }

    QList<BookResult> results = parseSearchHtml(body);

    // Trim to caller-specified limit. LibGen returns up to 25 rows per page
    // by default; higher limits would require pagination handling.
    if (results.size() > m_currentLimit) {
        results = results.mid(0, m_currentLimit);
    }

    qInfo() << "[LibGenScraper] search extracted" << results.size() << "rows";

    reset();
    emit searchFinished(results);
}

QList<BookResult> LibGenScraper::parseSearchHtml(const QByteArray& html) const
{
    QList<BookResult> results;
    const QString text = QString::fromUtf8(html);

    // Locate the results table. LibGen's search page renders it under
    // <table class="table table-striped" id="tablelibgen">. We find the
    // <tbody> and iterate <tr> rows.
    static const QRegularExpression kTableRe(
        QStringLiteral(R"(<table[^>]*id="tablelibgen"[^>]*>(.*?)</table>)"),
        QRegularExpression::DotMatchesEverythingOption |
            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch tableMatch = kTableRe.match(text);
    if (!tableMatch.hasMatch()) {
        qWarning() << "[LibGenScraper] results table not found";
        return results;
    }

    const QString tableBody = tableMatch.captured(1);

    // tbody may or may not be explicit — some pages have <thead>...</thead>
    // immediately followed by <tr> without a wrapping <tbody>. Take the
    // content after the last </thead> to be safe.
    int bodyStart = tableBody.lastIndexOf(QStringLiteral("</thead>"),
                                          -1, Qt::CaseInsensitive);
    QString body = (bodyStart >= 0)
        ? tableBody.mid(bodyStart + 8)
        : tableBody;

    // Pre-strip inline void tags globally before row/cell/anchor parsing.
    // LibGen templates embed `<br>` unescaped inside `title="..."` tooltips
    // (e.g. title="Add/Edit: 2023-01-27<br>Real-title"), which breaks both
    // [^>]* anchor matching AND the stripTags regex — leaking tag fragments
    // like `href="..."><text>` into extracted text. Removing these tags
    // upfront flattens attribute content so downstream regex sees clean
    // structure. <br>/<wbr>/<hr> carry zero semantic value for our parse.
    static const QRegularExpression kInlineVoidTagRe(
        QStringLiteral("<(br|wbr|hr)\\s*/?>"),
        QRegularExpression::CaseInsensitiveOption);
    body.remove(kInlineVoidTagRe);

    static const QRegularExpression kRowRe(
        QStringLiteral(R"(<tr[^>]*>(.*?)</tr>)"),
        QRegularExpression::DotMatchesEverythingOption |
            QRegularExpression::CaseInsensitiveOption);
    // Cells can contain additional <td> inside nested tables / badges in rare
    // cases — but on libgen.li in practice they don't. Simple greedy match
    // stops at the first </td> which is correct for this template.
    static const QRegularExpression kCellRawRe(
        QStringLiteral(R"(<td[^>]*>(.*?)</td>)"),
        QRegularExpression::DotMatchesEverythingOption |
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kMd5Re(
        QStringLiteral(R"(md5=([a-fA-F0-9]{32}))"));
    // Title-bearing anchors live inside col[0] and have href="edition.php".
    // Each row has 1-3 such anchors (series-volume marker + title + optional
    // ISBN). Picking the best-looking text happens post-match.
    static const QRegularExpression kEditionAnchorRe(
        QStringLiteral(R"(<a[^>]*href="edition\.php[^"]*"[^>]*>(.*?)</a>)"),
        QRegularExpression::DotMatchesEverythingOption |
            QRegularExpression::CaseInsensitiveOption);

    auto rowIt = kRowRe.globalMatch(body);
    while (rowIt.hasNext()) {
        const QRegularExpressionMatch rowMatch = rowIt.next();
        const QString row = rowMatch.captured(1);

        // MD5 lives in col[8] Mirrors column hrefs on libgen.li. Skip any
        // row that doesn't have one (pagination / decorative rows).
        const QRegularExpressionMatch md5Match = kMd5Re.match(row);
        if (!md5Match.hasMatch()) continue;
        const QString md5 = md5Match.captured(1).toLower();

        // Capture raw HTML per cell so title extraction can search the
        // first cell's nested anchors.
        QStringList cellHtmls;
        QStringList cellTexts;
        auto cellIt = kCellRawRe.globalMatch(row);
        while (cellIt.hasNext()) {
            const QRegularExpressionMatch cellMatch = cellIt.next();
            cellHtmls << cellMatch.captured(1);
            cellTexts << stripTags(cellMatch.captured(1));
        }

        if (cellTexts.size() < 3) continue;

        // libgen.li actual column layout (verified 2026-04-22):
        //   [0] compound: ID + Time-added + Series-anchor + Title-anchor +
        //                 ISBN-anchor + type-badge + category-id-badge +
        //                 (sometimes) dimensions footer
        //   [1] Author(s)
        //   [2] Publisher
        //   [3] Year
        //   [4] Language
        //   [5] Pages
        //   [6] Size         — "<a href=file.php?id=...>52 MB</a>"
        //   [7] Extension    — "epub" | "cbz" | "pdf" etc
        //   [8] Mirrors      — list of <a href=get.php?md5=...> + externals
        BookResult r;
        r.source    = sourceId();
        r.md5       = md5;
        r.sourceId  = md5;
        r.detailUrl = QStringLiteral("%1/ads.php?md5=%2").arg(kLibGenBase, md5);

        // Title extraction: iterate all edition.php anchors in col[0]; pick
        // the one whose stripped text is the best title candidate (>=3
        // chars, not pure digits/ISBN, not a volume-number marker like "#9").
        // If no edition anchor yields a usable title (e.g. rows where the
        // only edition anchor holds a date/volume marker), fall back to the
        // series.php anchor which carries the book-group name.
        static const QRegularExpression kSeriesAnchorRe(
            QStringLiteral(R"(<a[^>]*href="series\.php[^"]*"[^>]*>(.*?)</a>)"),
            QRegularExpression::DotMatchesEverythingOption |
                QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression kIsbnLikeRe(
            QStringLiteral("^[0-9; \\-Xx]+$"));

        const QString cell0 = cellHtmls.value(0);
        QString bestTitle;

        auto tryCandidate = [&](const QString& raw) {
            QString txt = stripTags(raw).trimmed();
            if (txt.size() < 3) return;
            if (kIsbnLikeRe.match(txt).hasMatch()) return;
            if (txt.startsWith(QChar('#'))) return;
            // Date-marker pattern: "2005 Janvier", "2014 Mars", "2005-jan",
            // "2014-mar", "1982 Avril" — volume-publication-date stamps.
            static const QRegularExpression kDateMarkerRe(
                QStringLiteral(
                    R"(^\d{4}([ -](jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)\w*|[ ](?:janvier|f[eé]vrier|mars|avril|mai|juin|juillet|ao[uû]t|septembre|octobre|novembre|d[eé]cembre))$)"),
                QRegularExpression::CaseInsensitiveOption);
            if (kDateMarkerRe.match(txt).hasMatch()) return;
            if (txt.size() > bestTitle.size()) bestTitle = txt;
        };

        auto editionIt = kEditionAnchorRe.globalMatch(cell0);
        while (editionIt.hasNext()) {
            tryCandidate(editionIt.next().captured(1));
        }
        if (bestTitle.isEmpty()) {
            auto seriesIt = kSeriesAnchorRe.globalMatch(cell0);
            while (seriesIt.hasNext()) {
                tryCandidate(seriesIt.next().captured(1));
            }
        }

        if (!bestTitle.isEmpty()) {
            r.title = bestTitle;
        } else {
            // Last-resort fallback: stripped full cell[0] text, first line.
            r.title = cellTexts[0].section(QChar('\n'), 0, 0).trimmed();
        }

        auto safeAt = [&](int i) -> QString {
            return (i >= 0 && i < cellTexts.size()) ? cellTexts[i] : QString();
        };

        r.author    = safeAt(1);
        r.publisher = safeAt(2);
        r.year      = safeAt(3);
        r.language  = safeAt(4);
        r.pages     = safeAt(5);
        r.fileSize  = safeAt(6);
        r.format    = safeAt(7).toLower();

        if (r.title.isEmpty() || r.md5.isEmpty()) continue;

        results.append(r);
    }

    return results;
}

void LibGenScraper::fetchDetail(const QString& md5OrId)
{
    // M2.3 scope — minimal detail fetch. Search-row data already populates
    // the core fields (title/author/publisher/year/language/pages/size/
    // format); richer description + ISBN + cover enrichment from the
    // /json.php?object=e&md5=<md5>&fields=* endpoint is deferred. The page
    // already has the snapshot from the search row at this point, so we
    // satisfy the detailReady contract by emitting what we know.
    //
    // The page's onDetailReady merge is a no-op in this path since the
    // emitted BookResult has identical fields to the snapshot, which is
    // honest and stable. When M2.4+ adds /json.php enrichment, this method
    // grows a real network call.
    BookResult r;
    r.source    = sourceId();
    r.md5       = md5OrId;
    r.sourceId  = md5OrId;
    r.detailUrl = QStringLiteral("%1/ads.php?md5=%2").arg(kLibGenBase, md5OrId);

    qInfo() << "[LibGenScraper] fetchDetail (snapshot-echo) for" << md5OrId;
    emit detailReady(r);
}

void LibGenScraper::fetchCoverUrl(const QString& md5)
{
    const QString trimmed = md5.trimmed().toLower();
    if (trimmed.isEmpty()) {
        emit coverUrlFailed(trimmed, QStringLiteral("empty md5 passed to fetchCoverUrl"));
        return;
    }

    const QUrl target(QStringLiteral("%1/ads.php?md5=%2").arg(kLibGenBase, trimmed));
    qDebug() << "[LibGenScraper] fetchCoverUrl ->" << target.toString();

    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply* reply = m_nam->get(req);
    m_coverReplies.insert(reply, trimmed);
    connect(reply, &QNetworkReply::finished,
            this, &LibGenScraper::onCoverReplyFinished);
}

void LibGenScraper::resolveDownload(const QString& md5OrId)
{
    if (m_mode != Mode::Idle) {
        emit downloadFailed(md5OrId.trimmed(),
                            QStringLiteral("LibGen scraper busy"));
        return;
    }

    const QString trimmed = md5OrId.trimmed();
    if (trimmed.isEmpty()) {
        emit downloadFailed(trimmed,
                            QStringLiteral("empty md5 passed to resolveDownload"));
        return;
    }

    m_mode           = Mode::ResolvingDownload;
    m_currentMd5OrId = trimmed;

    const QUrl target(QStringLiteral("%1/ads.php?md5=%2").arg(kLibGenBase, trimmed));
    qDebug() << "[LibGenScraper] resolveDownload ->" << target.toString();

    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &LibGenScraper::onResolveReplyFinished);
}

void LibGenScraper::onResolveReplyFinished()
{
    if (!m_activeReply || m_mode != Mode::ResolvingDownload) {
        if (m_activeReply) {
            m_activeReply->deleteLater();
            m_activeReply = nullptr;
        }
        return;
    }

    QNetworkReply* reply = m_activeReply;
    m_activeReply = nullptr;

    const QString md5 = m_currentMd5OrId;
    const QNetworkReply::NetworkError err = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        failResolve(md5, QStringLiteral("LibGen /ads.php fetch failed: %1")
                         .arg(reply->errorString()));
        return;
    }

    const QStringList urls = parseResolveHtml(body);

    if (urls.isEmpty()) {
        failResolve(md5, QStringLiteral("LibGen /ads.php returned no get.php links"));
        return;
    }

    qInfo() << "[LibGenScraper] resolveDownload extracted"
            << urls.size() << "mirror URL(s) for" << md5;

    reset();
    emit downloadResolved(md5, urls);
}

void LibGenScraper::onCoverReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    const QString md5 = m_coverReplies.take(reply);
    const QNetworkReply::NetworkError err = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (md5.isEmpty()) return;

    if (err != QNetworkReply::NoError) {
        failCover(md5, QStringLiteral("LibGen cover fetch failed: %1")
                       .arg(reply->errorString()));
        return;
    }

    const QString absoluteUrl = parseCoverUrl(body);
    if (absoluteUrl.isEmpty()) {
        failCover(md5, QStringLiteral("LibGen /ads.php returned no cover image"));
        return;
    }

    emit coverUrlReady(md5, absoluteUrl);
}

QStringList LibGenScraper::parseResolveHtml(const QByteArray& html) const
{
    const QString text = QString::fromUtf8(html);

    // Primary direct-download link lives in <a href="get.php?md5=X&key=Y">.
    // Secondary mirrors may point to library.lol/main/<md5> or direct .pdf
    // at download.library.lol/...; capture both patterns so BookDownloader
    // can HEAD-probe failover in M2.4.
    // Custom delimiters RX( ... )RX to prevent the embedded `)"` (capture-
    // group close + href-attribute close) from terminating the raw string
    // early. Default R"(...)" cannot express regex with those paired.
    static const QRegularExpression kGetRe(
        QStringLiteral(R"RX(<a[^>]*href="(get\.php\?[^"]*md5=[a-fA-F0-9]{32}[^"]*)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kLibraryLolRe(
        QStringLiteral(R"RX(<a[^>]*href="(https?://[^"]*library\.lol[^"]+)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList urls;
    QSet<QString> seen;

    auto pushUnique = [&](const QString& candidate) {
        if (candidate.isEmpty()) return;
        if (seen.contains(candidate)) return;
        seen.insert(candidate);
        urls.append(candidate);
    };

    auto getIt = kGetRe.globalMatch(text);
    while (getIt.hasNext()) {
        QString rel = getIt.next().captured(1);
        // decode &amp; so URLs are usable
        rel.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        const QString abs = QStringLiteral("%1/%2").arg(kLibGenBase, rel);
        pushUnique(abs);
    }

    auto lolIt = kLibraryLolRe.globalMatch(text);
    while (lolIt.hasNext()) {
        QString url = lolIt.next().captured(1);
        url.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        pushUnique(url);
    }

    return urls;
}

QString LibGenScraper::parseCoverUrl(const QByteArray& html) const
{
    const QString text = QString::fromUtf8(html);
    static const QRegularExpression kCoverRe(
        QStringLiteral(R"RX(<img[^>]*src="(/covers/[^"]+\.(?:jpg|png|webp))"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = kCoverRe.match(text);
    if (!match.hasMatch()) return QString();

    return QStringLiteral("%1%2").arg(kLibGenBase, match.captured(1));
}
