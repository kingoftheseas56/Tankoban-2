#pragma once

#include "MangaResult.h"
#include <QObject>
#include <QList>

class QNetworkAccessManager;

class MangaScraper : public QObject
{
    Q_OBJECT

public:
    explicit MangaScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : QObject(parent), m_nam(nam) {}

    virtual QString sourceId() const = 0;
    virtual QString sourceName() const = 0;

    virtual void search(const QString& query, int limit = 60) = 0;
    virtual void fetchChapters(const QString& seriesId) = 0;
    virtual void fetchPages(const QString& chapterId) = 0;

signals:
    void searchFinished(const QList<MangaResult>& results);
    void chaptersReady(const QList<ChapterInfo>& chapters);
    void pagesReady(const QList<PageInfo>& pages);
    void errorOccurred(const QString& message);

protected:
    QNetworkAccessManager* m_nam;
};
