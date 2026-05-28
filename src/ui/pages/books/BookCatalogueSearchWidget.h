#pragma once

#include <QHash>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QWidget>

#include "core/book/BookCatalogueResult.h"
#include "core/book/SeriesDetector.h"

class BookCatalogueAggregator;
class QNetworkAccessManager;
class TileCard;
class TileStrip;

class BookCatalogueSearchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BookCatalogueSearchWidget(BookCatalogueAggregator* aggregator,
                                       QNetworkAccessManager* nam,
                                       const QString& coverCacheDir,
                                       QWidget* parent = nullptr);

    void search(const QString& query);
    void clearResults();

signals:
    void backRequested();
    void bookPicked(const BookCatalogueResult& book, const QString& coverPath);
    // Series tile clicked → BooksPage fetches the series + opens the series view.
    // Carries the series stub whose seriesId is the FictionDB series slug.
    void seriesPicked(const BookCatalogueResult& series);

private slots:
    void onCatalogueResult(const QString& query,
                           const QList<SeriesDetector::SeriesGroup>& seriesGroups,
                           const QList<BookCatalogueResult>& standalones);
    void onCatalogueFailed(const QString& query, const QString& error);

private:
    void buildUi();
    void addSeriesCard(const SeriesDetector::SeriesGroup& group);
    void addBookCard(const BookCatalogueResult& book);
    void revealMoreSeries();
    void revealMoreBooks();
    QString coverPathFor(const QString& catalogueId) const;
    void downloadCover(const QString& catalogueId, const QString& coverUrl, TileCard* card);

    static constexpr int kInitialCap = 6;  // §3.5 — tiles shown per section before "Show more"

    BookCatalogueAggregator* m_aggregator = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QString m_coverCacheDir;
    QString m_currentQuery;
    bool m_pending = false;

    QPushButton* m_backButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QScrollArea* m_scroll = nullptr;
    QLabel* m_seriesHeader = nullptr;
    TileStrip* m_seriesStrip = nullptr;
    QLabel* m_booksHeader = nullptr;
    TileStrip* m_booksStrip = nullptr;
    QPushButton* m_seriesMoreBtn = nullptr;
    QPushButton* m_booksMoreBtn = nullptr;

    QHash<QString, BookCatalogueResult> m_resultsById;
    QList<SeriesDetector::SeriesGroup> m_overflowSeries;  // series beyond kInitialCap
    QList<BookCatalogueResult> m_overflowBooks;           // books beyond kInitialCap
};
