#pragma once

#include "MangaScraper.h"
#include <QJsonObject>

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
    void fetchDetail(const MangaResult& preview) override;

    // Live Western-catalogue fetch: GET /Comic/<slug>, classify collected
    // editions + pull synopsis, assemble a schema-v2 Western JSON object, emit
    // westernSeriesReady. title + coverFromSearch come from the search result
    // (used as fallbacks when the page parse is thin).
    void fetchWesternSeries(const QString& seriesSlug,
                            const QString& title,
                            const QString& coverFromSearch);

signals:
    void westernSeriesReady(const QJsonObject& seriesJson);

private:
    static QList<ChapterInfo> parseChaptersHtml(const QString& html, const QString& slug);
    static QList<PageInfo>    parsePagesHtml(const QString& html, const QString& slug, const QString& issue);
};
