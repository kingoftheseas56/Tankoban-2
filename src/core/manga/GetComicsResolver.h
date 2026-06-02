// src/core/manga/GetComicsResolver.h
#pragma once

#include "GetComicsParse.h"
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace tankoban::manga {

// One resolved edition's downloads + cover, ready for WesternVolumeDownloader.
struct EditionDownload {
    QString postUrl;
    QString coverUrl;                       // per-edition og:image (may be empty)
    QList<getcomics::DownloadLink> links;   // ordered as found; pickBest applied by caller
    getcomics::DownloadLink best;           // pickBest(links)
};

// Live GetComics resolver: search -> fuzzy-match -> fetch post -> emit downloads.
// QNAM is created via NetSeam (non-owning here; caller passes the shared one).
class GetComicsResolver : public QObject {
    Q_OBJECT
public:
    explicit GetComicsResolver(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // editionTitle/year/tierLabel come from the Western edition (label + catalog
    // year + grouping tier). Emits resolved() on a confident match with a usable
    // download, else resolveFailed() (fail safe).
    void resolve(const QString& editionTitle, int year, const QString& tierLabel);

signals:
    void resolved(const EditionDownload& dl);
    void resolveFailed(const QString& reason);

private:
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga
