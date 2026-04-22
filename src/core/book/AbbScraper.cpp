#include "AbbScraper.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {

// Base URL. ABB oscillates between .com/.li/.nu/.lu as takedowns redirect
// it; .lu is the 2026-04-22 active canonical per Hemanth screenshot + probe.
// If domain shifts, update here only - all URL construction flows through
// this constant.
constexpr const char* kAbbBase = "https://audiobookbay.lu";

// User-Agent - realistic Chrome string. ABB doesn't enforce UA strictly
// (probe worked with curl's default too), but a realistic UA reduces edge
// heuristic flags if ABB ever adds CF. Kept in sync with LibGenScraper's UA.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// HTTP timeout per request. ABB returned in ~3s on probe; 15s matches
// LibGen budget for consistent cross-source cancel cadence.
constexpr int kRequestTimeoutMs = 15'000;

// Decode a minimal set of HTML entities that land in ABB title / metadata
// text. Not a full decoder - good enough for the common cases on ABB.
QString decodeEntities(const QString& s)
{
    QString out = s;
    out.replace(QStringLiteral("&amp;"),   QStringLiteral("&"))
       .replace(QStringLiteral("&quot;"),  QStringLiteral("\""))
       .replace(QStringLiteral("&apos;"),  QStringLiteral("'"))
       .replace(QStringLiteral("&#039;"),  QStringLiteral("'"))
       .replace(QStringLiteral("&#8211;"), QStringLiteral("-"))
       .replace(QStringLiteral("&#8212;"), QStringLiteral("-"))
       .replace(QStringLiteral("&#8230;"), QStringLiteral("..."))
       .replace(QStringLiteral("&nbsp;"),  QStringLiteral(" "))
       .replace(QStringLiteral("&lt;"),    QStringLiteral("<"))
       .replace(QStringLiteral("&gt;"),    QStringLiteral(">"));
    return out.trimmed();
}

} // namespace

AbbScraper::AbbScraper(QNetworkAccessManager* nam, QObject* parent)
    : BookScraper(nam, parent)
{
}

AbbScraper::~AbbScraper()
{
    cancelActiveReply();
}

void AbbScraper::cancelActiveReply()
{
    if (!m_activeReply) return;
    disconnect(m_activeReply, nullptr, this, nullptr);
    m_activeReply->abort();
    m_activeReply->deleteLater();
    m_activeReply = nullptr;
}

void AbbScraper::reset()
{
    m_mode = Mode::Idle;
    m_currentQuery.clear();
    m_currentSlug.clear();
    cancelActiveReply();
}

void AbbScraper::fail(const QString& reason)
{
    reset();
    emit errorOccurred(reason);
}

void AbbScraper::failResolve(const QString& slug, const QString& reason)
{
    reset();
    emit downloadFailed(slug, reason);
}

void AbbScraper::search(const QString& query, int limit)
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

    // ABB search is the WordPress `?s=<query>` pattern. Percent-encoding
    // via QUrlQuery - spaces become '+' naturally in GET query strings.
    QUrl target(QStringLiteral("%1/").arg(kAbbBase));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("s"), trimmed);
    target.setQuery(q);

    qDebug() << "[AbbScraper] search ->" << target.toString();

    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &AbbScraper::onSearchReplyFinished);
}

void AbbScraper::onSearchReplyFinished()
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
        fail(QStringLiteral("AudioBookBay search failed: %1").arg(reply->errorString()));
        return;
    }

    QList<BookResult> results = parseSearchHtml(body);

    // Trim to caller limit. ABB returns ~10 rows per page; higher limits
    // would require `/page/N/?s=...` pagination - deferred to Track B.
    if (results.size() > m_currentLimit) {
        results = results.mid(0, m_currentLimit);
    }

    qInfo() << "[AbbScraper] search extracted" << results.size() << "rows";

    reset();
    emit searchFinished(results);
}

QList<BookResult> AbbScraper::parseSearchHtml(const QByteArray& html) const
{
    QList<BookResult> results;
    const QString text = QString::fromUtf8(html);

    // Find all starting positions of real post rows.
    // EXACT match on `<div class="post">` - the trailing `">` discriminator
    // cleanly excludes the `<div class="post re-ab" ...>` honeypot decoys
    // that carry base64-encoded fake row markup with ad-redirect links.
    // Honeypot filter per probe FINDINGS.md section 3.
    static const QRegularExpression kPostStartRe(
        QStringLiteral(R"RX(<div class="post">)RX"));

    QList<int> starts;
    auto postIt = kPostStartRe.globalMatch(text);
    while (postIt.hasNext()) {
        starts.append(postIt.next().capturedStart());
    }

    if (starts.isEmpty()) {
        qWarning() << "[AbbScraper] no post rows found";
        return results;
    }

    // Per-block extraction patterns (operate on a per-row slice defined by
    // the window between consecutive post-start positions).
    static const QRegularExpression kTitleRe(
        QStringLiteral(R"RX(<h2><a href="([^"]+)"[^>]*>([^<]+)</a></h2>)RX"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression kCoverRe(
        QStringLiteral(R"RX(<img src="([^"]+)"[^>]*width="250")RX"));
    static const QRegularExpression kLanguageRe(
        QStringLiteral(R"RX(Language:\s*([^<]+?)(?:<span|<br))RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFormatRe(
        QStringLiteral(R"RX(Format:\s*<span[^>]*>([^<]+)</span>)RX"),
        QRegularExpression::CaseInsensitiveOption);
    // File size "File Size: <span ...>1.53</span> GBs" -> capture number + unit.
    // Unit can be "GBs" / "MBs" / "KBs" per ABB's convention (the trailing
    // 's' suffix is idiomatic on ABB - we keep it verbatim for display).
    static const QRegularExpression kSizeRe(
        QStringLiteral(R"RX(File Size:\s*<span[^>]*>([^<]+)</span>\s*([GMK]?Bs?))RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kPostedRe(
        QStringLiteral(R"RX(Posted:\s*([^<]+?)<br)RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kSlugRe(
        QStringLiteral(R"RX(/abss/([^/]+)/?)RX"));

    for (int i = 0; i < starts.size(); ++i) {
        const int blockStart = starts[i];
        const int blockEnd   = (i + 1 < starts.size()) ? starts[i + 1] : text.size();
        const QString block  = text.mid(blockStart, blockEnd - blockStart);

        const QRegularExpressionMatch titleM = kTitleRe.match(block);
        if (!titleM.hasMatch()) continue;  // malformed row, skip

        BookResult r;
        r.source = sourceId();

        // Normalize detail URL to absolute. ABB anchors are typically
        // absolute already, but defense-in-depth.
        const QString rawHref = titleM.captured(1).trimmed();
        r.detailUrl = rawHref.startsWith(QLatin1String("http"))
                          ? rawHref
                          : QStringLiteral("%1%2").arg(kAbbBase, rawHref);

        // Slug = last non-empty segment of /abss/<slug>/ - used as
        // sourceId. ABB doesn't surface a content hash in search rows (the
        // info hash lives on the detail page); at M2 we populate md5 from
        // the Info Hash <td> at fetchDetail time.
        const QRegularExpressionMatch slugM = kSlugRe.match(r.detailUrl);
        if (slugM.hasMatch()) r.sourceId = slugM.captured(1);

        // Title + author split on " - ". ABB title convention is
        // "<Book Title> - <Author Name>" (verified across 10 probe results).
        // Conservative heuristic: accept post-dash text as author only if
        // it's a plausible name shape (<= 60 chars, no URL chars, no
        // leading digit). Otherwise keep full string as title.
        QString workingTitle = decodeEntities(titleM.captured(2));
        const int dashIdx = workingTitle.lastIndexOf(QStringLiteral(" - "));
        if (dashIdx > 0 && dashIdx < workingTitle.size() - 3) {
            const QString afterDash = workingTitle.mid(dashIdx + 3).trimmed();
            const bool looksLikeAuthor =
                !afterDash.isEmpty() &&
                afterDash.size() <= 60 &&
                !afterDash.contains(QChar('/')) &&
                !afterDash.contains(QChar('[')) &&
                !afterDash[0].isDigit();
            if (looksLikeAuthor) {
                r.author = afterDash;
                r.title  = workingTitle.left(dashIdx).trimmed();
            } else {
                r.title = workingTitle;
            }
        } else {
            r.title = workingTitle;
        }

        // Cover - first <img width="250"> in the block (search rows have
        // exactly one). Detail-page covers may be higher resolution; M2
        // can upgrade at detail-fetch time if useful.
        const QRegularExpressionMatch coverM = kCoverRe.match(block);
        if (coverM.hasMatch()) r.coverUrl = coverM.captured(1).trimmed();

        // Language (e.g. "English")
        const QRegularExpressionMatch langM = kLanguageRe.match(block);
        if (langM.hasMatch()) r.language = decodeEntities(langM.captured(1));

        // Format (M4B / MP3 / Mixed / ...)
        const QRegularExpressionMatch fmtM = kFormatRe.match(block);
        if (fmtM.hasMatch()) r.format = fmtM.captured(1).trimmed();

        // File size - concat number + unit with a space ("1.53 GBs").
        // Display-only; BookResult.fileSize is documented as human-readable.
        const QRegularExpressionMatch sizeM = kSizeRe.match(block);
        if (sizeM.hasMatch()) {
            r.fileSize = QStringLiteral("%1 %2")
                             .arg(sizeM.captured(1).trimmed(),
                                  sizeM.captured(2).trimmed());
        }

        // Posted date -> year slot. BookResult.year is a display-only
        // string (not int) so lodging the full ABB posted date ("17 Nov
        // 2020") gives the grid "Year" column something honest. For
        // client-side year-sort (parseYearInt in TankoLibraryPage), the
        // existing regex grabs the first 4-digit run - works on ABB dates.
        const QRegularExpressionMatch postedM = kPostedRe.match(block);
        if (postedM.hasMatch()) r.year = postedM.captured(1).trimmed();

        results.append(r);
    }

    return results;
}

void AbbScraper::fetchDetail(const QString& md5OrId)
{
    // M2 real detail fetch. The caller passes our sourceId for this row,
    // which is the URL slug (set during parseSearchHtml). GET /abss/<slug>/
    // and parse the Info Hash <td> - that's all we need to construct a
    // magnet URI.
    if (m_mode != Mode::Idle) reset();
    if (md5OrId.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("AudioBookBay: empty slug"));
        return;
    }

    m_mode         = Mode::FetchingDetail;
    m_currentSlug  = md5OrId.trimmed();

    const QUrl target(QStringLiteral("%1/abss/%2/").arg(kAbbBase, m_currentSlug));
    qDebug() << "[AbbScraper] fetchDetail ->" << target.toString();

    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &AbbScraper::onDetailReplyFinished);
}

void AbbScraper::resolveDownload(const QString& md5OrId)
{
    // M2 real resolve. Same network fetch as fetchDetail, but different
    // terminal emit. Reuses onDetailReplyFinished which discriminates by
    // m_mode on completion (ResolvingDownload emits downloadResolved with
    // a single-element magnet-URI list; FetchingDetail emits detailReady).
    if (m_mode != Mode::Idle) reset();
    if (md5OrId.trimmed().isEmpty()) {
        emit downloadFailed(md5OrId, QStringLiteral("AudioBookBay: empty slug"));
        return;
    }

    m_mode         = Mode::ResolvingDownload;
    m_currentSlug  = md5OrId.trimmed();

    const QUrl target(QStringLiteral("%1/abss/%2/").arg(kAbbBase, m_currentSlug));
    qDebug() << "[AbbScraper] resolveDownload ->" << target.toString();

    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(kRequestTimeoutMs);

    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &AbbScraper::onDetailReplyFinished);
}

void AbbScraper::onDetailReplyFinished()
{
    if (!m_activeReply ||
        (m_mode != Mode::FetchingDetail && m_mode != Mode::ResolvingDownload)) {
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

    const QString slug = m_currentSlug;
    const Mode phase = m_mode;

    if (err != QNetworkReply::NoError) {
        if (phase == Mode::ResolvingDownload) {
            failResolve(slug, QStringLiteral(
                "AudioBookBay detail fetch failed: %1").arg(reply->errorString()));
        } else {
            fail(QStringLiteral(
                "AudioBookBay detail fetch failed: %1").arg(reply->errorString()));
        }
        return;
    }

    const QString infoHash = parseInfoHash(body);
    if (infoHash.isEmpty()) {
        const QString msg = QStringLiteral(
            "AudioBookBay detail parse failed: info hash not found in detail page "
            "(slug=%1). The post may have been taken down or the page structure "
            "changed - refetch the search or try a different title.").arg(slug);
        if (phase == Mode::ResolvingDownload) {
            failResolve(slug, msg);
        } else {
            fail(msg);
        }
        return;
    }

    qInfo() << "[AbbScraper] detail ok - slug=" << slug << "info_hash=" << infoHash;

    // Construct the magnet URI. Title used as display name (dn=) falls
    // back to the slug if we have no better signal - the slug at least
    // lets the user recognize the torrent in the Transfers view before
    // metadata arrives with the real torrent name.
    const QString magnet = constructMagnet(infoHash, slug);

    if (phase == Mode::ResolvingDownload) {
        reset();
        emit downloadResolved(slug, QStringList{ magnet });
    } else {
        BookResult detail;
        detail.source      = sourceId();
        detail.sourceId    = slug;
        detail.md5         = infoHash;    // info hash used as the cross-source dedup key
        detail.downloadUrl = magnet;      // magnet URI parked here for future reuse
        reset();
        emit detailReady(detail);
    }
}

QString AbbScraper::parseInfoHash(const QByteArray& html) const
{
    // "Info Hash:" row shape (verified via probe artifact detail_rhythm.html):
    //   <td>Info Hash:</td>\s*<td>1d32f8b449ebe0d63b9d08ba6e86525ff17baa3d</td>
    // The 40-hex-char value is the torrent info hash (SHA1), exactly what
    // libtorrent wants in magnet:?xt=urn:btih:<hex40>.
    static const QRegularExpression kRe(
        QStringLiteral(R"RX(<td>Info Hash:</td>\s*<td[^>]*>\s*([0-9a-fA-F]{40})\s*</td>)RX"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = kRe.match(QString::fromUtf8(html));
    if (!m.hasMatch()) return QString();
    return m.captured(1).toLower();
}

QString AbbScraper::constructMagnet(const QString& infoHash,
                                    const QString& title) const
{
    // VERBATIM replication of ABB's /js/main.js construction logic
    // (captured in agents/prototypes/tankolibrary_abb_probe_2026-04-22/
    // main.js). The 7-tracker list is copied exactly including the
    // typo'd `:69691337` port on the opentrackr.org entry - libtorrent
    // gracefully ignores invalid trackers and uses the other 6, and
    // matching ABB's exact set maximizes peer-discovery parity with
    // anyone else who clicked ABB's own Magnet button.
    //
    // dn= (display name) is added per BEP-9; libtorrent falls back to
    // this before metadata arrives. We use the post title (or slug if
    // title is empty) url-encoded; libtorrent's parser tolerates liberal
    // percent-encoding in dn=.
    const QString displayName = title.isEmpty()
        ? QStringLiteral("audiobookbay")
        : QString::fromUtf8(QUrl::toPercentEncoding(title));

    return QStringLiteral(
        "magnet:?xt=urn:btih:%1"
        "&dn=%2"
        "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.open-internet.nl%3A6969%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A69691337%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.baravik.org%3A6970%2Fannounce"
        "&tr=http%3A%2F%2Fretracker.telecom.by%3A80%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.vanitycore.co%3A6969%2Fannounce"
    ).arg(infoHash, displayName);
}
