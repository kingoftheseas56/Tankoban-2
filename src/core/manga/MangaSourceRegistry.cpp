#include "MangaSourceRegistry.h"
#include "MangaScraper.h"
#include "WeebCentralScraper.h"
#include "ReadComicsScraper.h"
#include "ReadAllComicsScraper.h"

MangaSourceRegistry::MangaSourceRegistry(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
{
    m_scrapers.append(new WeebCentralScraper(nam, this));
    m_scrapers.append(new ReadComicsScraper(nam, this));
    // readallcomics: the WORKING Western page-fetch source (rcostation reader is
    // browser-only obfuscation). Drives MangaDownloader -> cbz. (2026-06-03)
    m_scrapers.append(new ReadAllComicsScraper(nam, this));
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
