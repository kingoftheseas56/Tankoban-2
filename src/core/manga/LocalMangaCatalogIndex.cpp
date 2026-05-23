// src/core/manga/LocalMangaCatalogIndex.cpp
//
// Renamed from src/core/manga/fandom/LocalFandomCatalogIndex.cpp
// (Phase B.2, COMICS_MANGAFIRE_PIVOT 2026-05-23).
// Data directory changed from data/fandom_catalog/ to data/mangafire_catalog/.

#include "LocalMangaCatalogIndex.h"
#include "LocalMangaCatalogLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace tankoban::manga {

LocalMangaCatalogIndex::LocalMangaCatalogIndex(const QString& dataDir)
    : m_dataDir(dataDir.isEmpty() ? LocalMangaCatalogLoader::canonicalDataDir() : dataDir)
{
}

QString LocalMangaCatalogIndex::normalizeTitle(const QString& raw) {
    QString out;
    out.reserve(raw.size());
    bool lastWasHyphen = false;
    for (QChar c : raw) {
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
            lastWasHyphen = false;
        } else if (!lastWasHyphen && !out.isEmpty()) {
            out.append('-');
            lastWasHyphen = true;
        }
    }
    while (out.endsWith('-')) out.chop(1);
    return out;
}

void LocalMangaCatalogIndex::refresh() {
    m_anilistToSlug.clear();
    m_titleToSlug.clear();
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
        const QString seriesId    = root.value("seriesId").toString();
        const QString seriesTitle = root.value("seriesTitle").toString();
        const int     anilistId   = root.value("anilistId").toInt(0);
        if (seriesId.isEmpty()) continue;

        m_slugToPath.insert(seriesId, path);

        if (anilistId != 0) {
            m_anilistToSlug.insert(anilistId, seriesId);
        }

        if (!seriesTitle.isEmpty()) {
            const QString norm = normalizeTitle(seriesTitle);
            if (!norm.isEmpty()) {
                m_titleToSlug.insert(norm, seriesId);
            }
        }
    }
}

QString LocalMangaCatalogIndex::slugForAnilistId(int anilistId) const {
    return m_anilistToSlug.value(anilistId, QString());
}

QString LocalMangaCatalogIndex::slugForSeriesTitle(const QString& title) const {
    if (title.isEmpty()) return QString();
    const QString norm = normalizeTitle(title);
    if (norm.isEmpty()) return QString();
    return m_titleToSlug.value(norm, QString());
}

QString LocalMangaCatalogIndex::filePathForSlug(const QString& seriesId) const {
    return m_slugToPath.value(seriesId, QString());
}

} // namespace tankoban::manga
