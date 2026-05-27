// src/core/manga/NyaaRuntimeSource.cpp
#include "NyaaRuntimeSource.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <algorithm>

namespace tankoban::manga {

namespace {

constexpr const char* kNyaaRssEndpoint = "https://nyaa.si";

QStringList buildQueryStrings(const QString& seriesTitle, int volumeNumber)
{
    QStringList out;
    const auto add = [&out](const QString& q) {
        const QString trimmed = q.simplified();
        if (!trimmed.isEmpty() && !out.contains(trimmed)) {
            out.append(trimmed);
        }
    };

    add(QStringLiteral("%1 %2").arg(seriesTitle).arg(volumeNumber));
    add(QStringLiteral("%1 %2").arg(seriesTitle).arg(volumeNumber, 2, 10, QChar('0')));
    add(QStringLiteral("%1 %2").arg(seriesTitle).arg(volumeNumber, 3, 10, QChar('0')));
    add(QStringLiteral("%1 Vol %2").arg(seriesTitle).arg(volumeNumber));
    add(seriesTitle);
    return out;
}

QString normaliseUploader(QString uploader)
{
    return uploader.trimmed().toLower();
}

bool titleMentionsVolume(const QString& title, int volumeNumber)
{
    if (volumeNumber <= 0) return true;

    const QString vol = QString::number(volumeNumber);
    const QString padded2 = QStringLiteral("0") + vol;
    const QString padded3 = QStringLiteral("00") + vol;
    const QString escapedVol = QStringLiteral("(?:%1|%2|%3)")
        .arg(QRegularExpression::escape(vol),
             QRegularExpression::escape(padded2.right(2)),
             QRegularExpression::escape(padded3.right(3)));

    const QRegularExpression direct(
        QStringLiteral(R"((?:\bv|vol\.?\s*|volume\s*|\b)%1\b)").arg(escapedVol),
        QRegularExpression::CaseInsensitiveOption);
    if (direct.match(title).hasMatch()) {
        return true;
    }

    const QRegularExpression range(
        QStringLiteral(R"(\bv0*(\d{1,3})\s*-\s*0*(\d{1,3})\b)"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = range.globalMatch(title);
    while (it.hasNext()) {
        const auto m = it.next();
        const int start = m.captured(1).toInt();
        const int end = m.captured(2).toInt();
        if (start <= volumeNumber && volumeNumber <= end) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

NyaaRuntimeSource::NyaaRuntimeSource(QNetworkAccessManager* nam,
                                    const QString& trustJsonPath,
                                    QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadTrustJson(trustJsonPath);
}

NyaaRuntimeSource::~NyaaRuntimeSource() = default;

void NyaaRuntimeSource::loadTrustJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral("[NyaaRuntimeSource] cannot open trust json:") << path;
        // PHASE 13: surface this failure to UI; v1 silently degrades to all-untrusted + no uploader filter.
        return;
    }
    QJsonParseError jsonErr{};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &jsonErr).object();
    if (jsonErr.error != QJsonParseError::NoError) {
        qWarning().noquote() << QStringLiteral("[NyaaRuntimeSource] trust json parse error:") << jsonErr.errorString();
        return;
    }
    // PHASE 13: when v2 schema lands (chapter-mode uploaders, etc.), enforce schemaVersion strictly.
    for (const auto& v : root.value("tier1").toArray()) {
        const QString uploader = normaliseUploader(v.toString());
        if (!uploader.isEmpty()) m_tier1.insert(uploader);
    }
    for (const auto& v : root.value("tier2").toArray()) {
        const QString uploader = normaliseUploader(v.toString());
        if (!uploader.isEmpty()) m_tier2.insert(uploader);
    }
    for (const auto& v : root.value("blocked").toArray()) {
        const QString uploader = normaliseUploader(v.toString());
        if (!uploader.isEmpty()) m_blocked.insert(uploader);
    }
}

int NyaaRuntimeSource::tierForUploader(const QString& uploader) const
{
    const QString normalised = normaliseUploader(uploader);
    if (m_blocked.contains(normalised)) return -1; // skip entirely
    if (m_tier1.contains(normalised))   return 1;
    if (m_tier2.contains(normalised))   return 2;
    return 99;
}

QString NyaaRuntimeSource::inferUploaderFromTitle(const QString& title) const
{
    const QString haystack = normaliseUploader(title);
    for (const QString& uploader : m_tier1) {
        if (haystack.contains(uploader)) return uploader;
    }
    for (const QString& uploader : m_tier2) {
        if (haystack.contains(uploader)) return uploader;
    }
    return QString();
}

void NyaaRuntimeSource::search(const QString& seriesTitle, int volumeNumber, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("network manager unavailable"));
        return;
    }
    // Query broad volume-title variants and rank/filter after RSS parse.
    // Nyaa's search parser often misses uploader OR clauses and "vNN"
    // forms; post-result trust ranking is more reliable than over-
    // constraining the search string.
    const QStringList queries = buildQueryStrings(seriesTitle, volumeNumber);
    PendingSearch pending;
    pending.requestId = requestId;
    pending.volumeNumber = volumeNumber;
    pending.pendingReplies = queries.size();
    m_pending.insert(requestId, pending);

    for (const QString& query : queries) {
        QUrl url(QString::fromLatin1(kNyaaRssEndpoint));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("page"), QStringLiteral("rss"));
        q.addQueryItem(QStringLiteral("c"),    QStringLiteral("3_1")); // Literature - English-translated category
        q.addQueryItem(QStringLiteral("s"),    QStringLiteral("seeders"));
        q.addQueryItem(QStringLiteral("o"),    QStringLiteral("desc"));
        q.addQueryItem(QStringLiteral("q"),    query);
        url.setQuery(q);

        auto* reply = m_nam->get(QNetworkRequest(url));
        reply->setProperty("nyaa_requestId", requestId);
        reply->setProperty("nyaa_query", query);
        connect(reply, &QNetworkReply::finished,
                this, &NyaaRuntimeSource::onReplyFinished);
    }
}

void NyaaRuntimeSource::onReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    finishBatchedReply(reply);
}

QList<NyaaSourceCandidate> NyaaRuntimeSource::parseCandidates(const QByteArray& payload) const
{
    QList<NyaaSourceCandidate> out;
    QXmlStreamReader xml(payload);
    NyaaSourceCandidate cur;
    bool inItem = false;
    QString currentTag;
    // NOTE: QXmlStreamReader has namespace processing enabled by default;
    // xml.name() returns the LOCAL name only ("infoHash") not the prefixed
    // form ("nyaa:infoHash"). Element compares below use the local names so
    // nyaa: extension elements still match. <author> + <title> + <link> +
    // <item> live in the default RSS namespace, also stripped to local.
    while (!xml.atEnd() && !xml.hasError()) {
        const auto t = xml.readNext();
        if (t == QXmlStreamReader::StartElement) {
            currentTag = xml.name().toString();
            if (currentTag == QStringLiteral("item")) {
                cur = NyaaSourceCandidate{};
                inItem = true;
            }
        } else if (t == QXmlStreamReader::EndElement) {
            if (xml.name() == QStringLiteral("item") && inItem) {
                if (cur.uploader.isEmpty()) {
                    cur.uploader = inferUploaderFromTitle(cur.title);
                }
                cur.tier = tierForUploader(cur.uploader);
                if (cur.tier >= 0) out.append(cur);
                inItem = false;
            }
            currentTag.clear();
        } else if (t == QXmlStreamReader::Characters && inItem && !xml.isWhitespace()) {
            const QString text = xml.text().toString();
            if (currentTag == QStringLiteral("title")) {
                cur.title = text;
            } else if (currentTag == QStringLiteral("infoHash")) {
                cur.infoHash = text.toLower();
                cur.magnetUri = QStringLiteral("magnet:?xt=urn:btih:") + cur.infoHash;
            } else if (currentTag == QStringLiteral("seeders")) {
                cur.seeders = text.toInt();
            } else if (currentTag == QStringLiteral("leechers")) {
                cur.leechers = text.toInt();
            } else if (currentTag == QStringLiteral("size")) {
                // PHASE 13: parse "1.4 GiB"-format string into qint64 bytes; v1 leaves 0.
            } else if (currentTag == QStringLiteral("uploader") ||
                       currentTag == QStringLiteral("author")) {
                // Prefer the namespaced uploader element; <author> in default RSS
                // namespace is the fallback. nyaa's RSS emits the namespaced tag
                // after <author> in document order, so the first-write-wins guard
                // preserves the preferred value.
                if (cur.uploader.isEmpty()) cur.uploader = text.trimmed();
            }
        }
    }

    // Rank: tier asc, seeders desc within tier. stable_sort so equal-tier-equal-seeders
    // candidates retain document order, keeping smoke evidence reproducible.
    std::stable_sort(out.begin(), out.end(),
              [](const NyaaSourceCandidate& a, const NyaaSourceCandidate& b) {
                  if (a.tier != b.tier) return a.tier < b.tier;
                  return a.seeders > b.seeders;
              });

    return out;
}

void NyaaRuntimeSource::finishBatchedReply(QNetworkReply* reply)
{
    const int requestId = reply->property("nyaa_requestId").toInt();
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QString query = reply->property("nyaa_query").toString();
        it->errors.append(QStringLiteral("%1: %2").arg(query, reply->errorString()));
    } else {
        const QList<NyaaSourceCandidate> parsed = parseCandidates(reply->readAll());
        for (const NyaaSourceCandidate& cand : parsed) {
            if (!titleMentionsVolume(cand.title, it->volumeNumber)) {
                continue;
            }
            if (!cand.infoHash.isEmpty()) {
                if (it->seenInfoHashes.contains(cand.infoHash)) {
                    continue;
                }
                it->seenInfoHashes.insert(cand.infoHash);
            }
            it->results.append(cand);
        }
    }

    it->pendingReplies -= 1;
    if (it->pendingReplies > 0) {
        return;
    }

    QList<NyaaSourceCandidate> results = it->results;
    const QStringList errors = it->errors;
    m_pending.erase(it);

    std::stable_sort(results.begin(), results.end(),
              [](const NyaaSourceCandidate& a, const NyaaSourceCandidate& b) {
                  if (a.tier != b.tier) return a.tier < b.tier;
                  return a.seeders > b.seeders;
              });

    if (results.isEmpty() && !errors.isEmpty()) {
        emit searchFailed(requestId, errors.join(QStringLiteral("; ")));
        return;
    }
    emit searchSucceeded(requestId, results);
}

} // namespace tankoban::manga
