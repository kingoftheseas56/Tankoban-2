#pragma once

#include <QList>
#include <QObject>
#include <QString>

class MangaScraper;
class QNetworkAccessManager;

// v1: hardcoded list of scrapers. v2 (deferred) can replace the
// hardcoded ctor body with file-system-loaded plugins without
// changing this class's surface (per brainstorm §3.6 / §9 v2
// follow-up note).
//
// Owns the scraper instances. Source IDs are stable string keys
// (e.g. "weebcentral", "readcomicsonline") suitable for persisting
// in comics_library.json records.
class MangaSourceRegistry : public QObject
{
    Q_OBJECT
public:
    explicit MangaSourceRegistry(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaSourceRegistry();

    QList<MangaScraper*> scrapers() const { return m_scrapers; }
    MangaScraper*        find(const QString& sourceId) const;

private:
    QList<MangaScraper*> m_scrapers;
};
