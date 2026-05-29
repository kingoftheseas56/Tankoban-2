#pragma once

#include <QHash>
#include <QList>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QWidget>

#include "core/book/BookCatalogueResult.h"
#include "core/book/SeriesDetector.h"

class BookCatalogueAggregator;
class BooksCatalogueLibraryStore;
class FictionDbClient;
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
    // Read-only store handle so the right-click menu can label Add vs Remove.
    void setCatalogueStore(BooksCatalogueLibraryStore* store) { m_store = store; }

signals:
    void backRequested();
    // Right-click "Add to / Remove from library" on a result tile. BooksPage
    // owns the mutation (book = file-less record toggle; series = fetch members
    // + add-all / remove-all).
    void bookLibraryToggleRequested(const BookCatalogueResult& book);
    void seriesLibraryToggleRequested(const BookCatalogueResult& seriesStub);
    void bookPicked(const BookCatalogueResult& book, const QString& coverPath);
    // Series tile clicked → BooksPage fetches the series + opens the series view.
    // Carries the series stub whose seriesId is the FictionDB series slug.
    void seriesPicked(const BookCatalogueResult& series);
    // The results page carries its own search bar so the user can refine the
    // query without going back to the library. Routed through BooksPage so the
    // grid's search bar + history stay in sync.
    void searchSubmitted(const QString& query);

private slots:
    void onCatalogueResult(const QString& query,
                           const QList<SeriesDetector::SeriesGroup>& seriesGroups,
                           const QList<BookCatalogueResult>& standalones);
    void onCatalogueFailed(const QString& query, const QString& error);
    // FictionDB search results carry no cover URL (only each book's own page
    // does), so standalone book tiles render as letter placeholders. We fetch
    // each visible book's page on a dedicated client, then download its cover.
    void onCoverBookReady(const QString& bookId, const BookCatalogueResult& book);

private:
    void buildUi();
    void addSeriesCard(const SeriesDetector::SeriesGroup& group);
    void addBookCard(const BookCatalogueResult& book);
    void revealMoreSeries();
    void revealMoreBooks();
    // Right-click handler shared by the series + books strips.
    void showTileContextMenu(TileStrip* strip, const QPoint& pos);
    QString coverPathFor(const QString& catalogueId) const;
    void downloadCover(const QString& catalogueId, const QString& coverUrl, TileCard* card);

    static constexpr int kInitialCap = 6;  // §3.5 — tiles shown per section before "Show more"

    BookCatalogueAggregator* m_aggregator = nullptr;
    BooksCatalogueLibraryStore* m_store = nullptr;   // read-only, for menu labels
    QNetworkAccessManager* m_nam = nullptr;
    // Dedicated client for per-tile cover enrichment — its bookReady stream
    // must not cross the aggregator's Top-N series-resolution fetches.
    FictionDbClient* m_coverClient = nullptr;
    QHash<QString, QPointer<TileCard>> m_coverCardBySlug;  // in-flight cover fetches
    QString m_coverCacheDir;
    QString m_currentQuery;
    bool m_pending = false;

    QPushButton* m_backButton = nullptr;
    QLineEdit* m_searchInput = nullptr;
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
