#pragma once

#include "MangaScraper.h"

class WeebCentralScraper : public MangaScraper
{
    Q_OBJECT

public:
    explicit WeebCentralScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : MangaScraper(nam, parent) {}

    QString sourceId() const override { return QStringLiteral("weebcentral"); }
    QString sourceName() const override { return QStringLiteral("WeebCentral"); }

    void search(const QString& query, int limit = 60) override;
    void fetchChapters(const QString& seriesId) override;
    void fetchPages(const QString& chapterId) override;

private:
    static QList<MangaResult> parseSearchHtml(const QString& html);
    static QList<ChapterInfo> parseChaptersHtml(const QString& html, const QString& source);
    static QList<PageInfo>    parsePagesHtml(const QString& html);
};
