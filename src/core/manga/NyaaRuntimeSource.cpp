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

QString buildQueryString(const QString& seriesTitle, int volumeNumber,
                         const QSet<QString>& tier1, const QSet<QString>& tier2)
{
    QStringList uploaders;
    for (const auto& u : tier1) uploaders.append(u);
    for (const auto& u : tier2) uploaders.append(u);
    QString q = seriesTitle + QStringLiteral(" v") + QString::number(volumeNumber);
    if (!uploaders.isEmpty()) {
        q += QStringLiteral(" (") + uploaders.join(QStringLiteral(" | ")) + QChar(')');
    }
    return q;
}

QString infoHashFromMagnet(const QString& magnet)
{
    // magnet:?xt=urn:btih:HEX&...
    static const QRegularExpression re(QStringLiteral("xt=urn:btih:([0-9a-fA-F]{40})"));
    const auto m = re.match(magnet);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
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
    for (const auto& v : root.value("tier1").toArray())   m_tier1.insert(v.toString());
    for (const auto& v : root.value("tier2").toArray())   m_tier2.insert(v.toString());
    for (const auto& v : root.value("blocked").toArray()) m_blocked.insert(v.toString());
}

int NyaaRuntimeSource::tierForUploader(const QString& uploader) const
{
    if (m_blocked.contains(uploader)) return -1; // skip entirely
    if (m_tier1.contains(uploader))   return 1;
    if (m_tier2.contains(uploader))   return 2;
    return 99;
}

void NyaaRuntimeSource::search(const QString& seriesTitle, int volumeNumber, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("network manager unavailable"));
        return;
    }
    QUrl url(QString::fromLatin1(kNyaaRssEndpoint));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("page"), QStringLiteral("rss"));
    q.addQueryItem(QStringLiteral("c"),    QStringLiteral("3_1")); // Literature - English-translated category
    q.addQueryItem(QStringLiteral("s"),    QStringLiteral("seeders"));
    q.addQueryItem(QStringLiteral("o"),    QStringLiteral("desc"));
    q.addQueryItem(QStringLiteral("q"),    buildQueryString(seriesTitle, volumeNumber, m_tier1, m_tier2));
    url.setQuery(q);

    auto* reply = m_nam->get(QNetworkRequest(url));
    reply->setProperty("nyaa_requestId", requestId);
    connect(reply, &QNetworkReply::finished,
            this, &NyaaRuntimeSource::onReplyFinished);
}

void NyaaRuntimeSource::onReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property("nyaa_requestId").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }

    QList<NyaaSourceCandidate> out;
    QXmlStreamReader xml(reply->readAll());
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

    emit searchSucceeded(requestId, out);
}

} // namespace tankoban::manga
