// src/core/manga/fandom/WikiManifestRegistry.cpp

#include "WikiManifestRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcFandomRegistry, "tankoban.manga.fandom.registry")

namespace tankoban::manga::fandom {

int WikiManifestRegistry::loadFromDirectory(const QString& manifestsDir)
{
    m_bySeriesId.clear();
    m_wikiIdToSeriesId.clear();

    QDir dir(manifestsDir);
    if (!dir.exists()) {
        qCWarning(lcFandomRegistry) << "manifests directory not found:" << manifestsDir;
        return 0;
    }

    const QStringList entries = dir.entryList(
        QStringList{ QStringLiteral("*.json") }, QDir::Files);

    int loaded = 0;
    for (const QString& filename : entries) {
        const QString path = dir.absoluteFilePath(filename);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qCWarning(lcFandomRegistry) << "cannot open" << path;
            continue;
        }
        const QByteArray bytes = f.readAll();
        f.close();

        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcFandomRegistry).noquote()
                << "invalid JSON in" << path << ":" << err.errorString();
            continue;
        }

        WikiManifest m = WikiManifest::fromJson(doc.object());
        if (!m.isValid()) {
            qCWarning(lcFandomRegistry) << "invalid manifest in" << path
                                         << "(missing seriesId or fandomWikiId)";
            continue;
        }
        m_bySeriesId.insert(m.seriesId, m);
        m_wikiIdToSeriesId.insert(m.fandomWikiId, m.seriesId);
        ++loaded;
    }

    qCInfo(lcFandomRegistry) << "loaded" << loaded << "manifest(s) from" << manifestsDir;
    return loaded;
}

WikiManifest WikiManifestRegistry::find(const QString& seriesId) const
{
    return m_bySeriesId.value(seriesId);
}

WikiManifest WikiManifestRegistry::findByFandomWikiId(const QString& wikiId) const
{
    const QString seriesId = m_wikiIdToSeriesId.value(wikiId);
    if (seriesId.isEmpty())
        return {};
    return m_bySeriesId.value(seriesId);
}

} // namespace tankoban::manga::fandom
