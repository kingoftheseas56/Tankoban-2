#pragma once

#include "MangaScraper.h"

class ReadComicsScraper : public MangaScraper
{
    Q_OBJECT

public:
    explicit ReadComicsScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : MangaScraper(nam, parent) {}

    QString sourceId() const override { return QStringLiteral("readcomicsonline"); }
    QString sourceName() const override { return QStringLiteral("ReadComicsOnline"); }

    void search(const QString& query, int limit = 60) override;
    void fetchChapters(const QString& seriesSlug) override;
    void fetchPages(const QString& chapterId) override;

private:
    static QList<ChapterInfo> parseChaptersHtml(const QString& html, const QString& slug);
    static QList<PageInfo>    parsePagesHtml(const QString& html, const QString& slug, const QString& issue);
};
