// src/core/manga/fandom/LocalFandomCatalogIndex.cpp
#include "LocalFandomCatalogIndex.h"
#include "LocalFandomCatalogLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace tankoban::manga::fandom {

LocalFandomCatalogIndex::LocalFandomCatalogIndex(const QString& dataDir)
    : m_dataDir(dataDir.isEmpty() ? LocalFandomCatalogLoader::canonicalDataDir() : dataDir)
{
}

void LocalFandomCatalogIndex::refresh() {
    m_anilistToSlug.clear();
    m_slugToPath.clear();

    QDir dir(m_dataDir);
    if (!dir.exists()) return;

    const QStringList files = dir.entryList(QStringList{"*.json"}, QDir::Files);
    for (const QString& filename : files) {
        const QString path = dir.absoluteFilePath(filename);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject root = doc.object();
        const QString seriesId = root.value("seriesId").toString();
        const int anilistId = root.value("anilistId").toInt(0);
        if (seriesId.isEmpty() || anilistId == 0) continue;

        m_anilistToSlug.insert(anilistId, seriesId);
        m_slugToPath.insert(seriesId, path);
    }
}

QString LocalFandomCatalogIndex::slugForAnilistId(int anilistId) const {
    return m_anilistToSlug.value(anilistId, QString());
}

QString LocalFandomCatalogIndex::filePathForSlug(const QString& seriesId) const {
    return m_slugToPath.value(seriesId, QString());
}

} // namespace tankoban::manga::fandom
