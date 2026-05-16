#include "MangaSourceRegistry.h"
#include "MangaScraper.h"
#include "WeebCentralScraper.h"
#include "ReadComicsScraper.h"

MangaSourceRegistry::MangaSourceRegistry(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
{
    m_scrapers.append(new WeebCentralScraper(nam, this));
    m_scrapers.append(new ReadComicsScraper(nam, this));
}

MangaSourceRegistry::~MangaSourceRegistry()
{
    // QObject parent-chain owns the scrapers; nothing to do.
}

MangaScraper* MangaSourceRegistry::find(const QString& sourceId) const
{
    for (auto* s : m_scrapers)
        if (s->sourceId() == sourceId) return s;
    return nullptr;
}
