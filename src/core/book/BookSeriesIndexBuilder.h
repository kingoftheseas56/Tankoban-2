#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "core/book/FictionDbClient.h"  // SeriesIndexEntry

class BookSeriesIndex;

// One-shot crawler that walks FictionDB's A–Z series directory
// (author-series~<a..z>~<page>.htm) and writes the accumulated series into a
// BookSeriesIndex (BOOKS_FICTIONDB_CATALOGUE §4.2). Sequential + self-throttling
// (one request at a time, chained off each reply + a small delay) so FictionDB
// is never hammered. Page failures skip to the next letter. Used to generate the
// bundled resource and for periodic background refresh.
class BookSeriesIndexBuilder : public QObject
{
    Q_OBJECT

public:
    BookSeriesIndexBuilder(FictionDbClient* client, BookSeriesIndex* index,
                           QObject* parent = nullptr);

    void start();
    bool running() const { return m_running; }

signals:
    void progress(int letterIdx, int page, int entriesSoFar);
    void finished(int totalEntries);

private:
    void requestNext();
    void onPage(const QString& letter, int page,
                const QList<SeriesIndexEntry>& entries, bool hasNext);
    void onPageFailed(const QString& letter, int page, const QString& error);
    void finish();

    FictionDbClient* m_client = nullptr;
    BookSeriesIndex* m_index = nullptr;
    QList<SeriesIndexEntry> m_accum;
    int m_letterIdx = 0;   // 0='a' .. 25='z'
    int m_page = 1;
    bool m_running = false;
};
