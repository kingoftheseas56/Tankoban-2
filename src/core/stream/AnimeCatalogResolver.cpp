#include "core/stream/AnimeCatalogResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace tankostream::stream {

bool isAnimeSeries(const QStringList& genres, const QString& country) {
    const bool animation = std::any_of(
        genres.cbegin(), genres.cend(), [](const QString& g) {
            return g.compare(QStringLiteral("Animation"), Qt::CaseInsensitive) == 0;
        });
    const bool japan =
        country.trimmed().compare(QStringLiteral("Japan"), Qt::CaseInsensitive) == 0;
    return animation && japan;
}

void AnimeIdMap::loadFromJson(const QByteArray& json) {
    m_imdbToKitsu.clear();
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        const QString imdb =
            o.value(QStringLiteral("imdb_id")).toString().trimmed();
        const QJsonValue kitsu = o.value(QStringLiteral("kitsu_id"));
        if (imdb.isEmpty() || !kitsu.isDouble()) {
            continue;
        }
        m_imdbToKitsu.insert(imdb, kitsu.toInt());
    }
}

std::optional<int> AnimeIdMap::kitsuIdForImdb(const QString& imdbId) const {
    const auto it = m_imdbToKitsu.constFind(imdbId);
    if (it == m_imdbToKitsu.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

bool confirmsKitsuMatch(const QString& wantedImdb, const QString& kitsuMetaImdb) {
    return !wantedImdb.isEmpty() && wantedImdb == kitsuMetaImdb;
}

}  // namespace tankostream::stream
