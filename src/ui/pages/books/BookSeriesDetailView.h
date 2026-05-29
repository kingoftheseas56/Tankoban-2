#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include "core/book/BookCatalogueResult.h"

class BooksCatalogueLibraryStore;
class FictionDbClient;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

// Series-shape detail view (BOOKS_FICTIONDB_CATALOGUE §4.4 + #2 metadata).
//
// Hero (series cover + name + author + book count) + the member books in
// reading order. Each row carries a per-book action:
//   - on disk → [Read] (emits bookReadRequested → opens the reader)
//   - not yet → [Get]  (emits bookOpenRequested → BooksPage routes to the
//                        movie-shape BookCatalogueDetailView for that book,
//                        which runs the existing §5.2 source-search + download)
// Rows re-derive Read/Get on the store's recordsChanged.
//
// Metadata (#2, 2026-05-28): the series page is a thin list of book links with
// no covers/synopsis. loadSeries() EAGERLY fetches each member book's page
// (cover + synopsis + year) and downloads covers, then renders the page once,
// fully-formed (no per-row pop-in). Enrichment is cached in-memory per series
// id, so re-opening a series is instant. Owns a private FictionDbClient so its
// bookReady stream never crosses the aggregator's Top-N fetches.
class BookSeriesDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit BookSeriesDetailView(QWidget* parent = nullptr);

    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    // Inject the shared network manager + cover-cache dir (same dir the search
    // storefront uses, so covers are shared). Constructs the private client.
    void setNetwork(QNetworkAccessManager* nam, const QString& coverDir);

    // Entry point — fetch the series, enrich its books, render fully-formed.
    // Cache hit renders instantly; cache miss shows a loading state first.
    void loadSeries(const QString& seriesId);

signals:
    void backRequested();
    void bookOpenRequested(const BookCatalogueResult& book);
    void bookReadRequested(const QString& catalogueId, const QString& filePath);
    // Right-click on a book row — BooksPage shows the owned/get context menu.
    void bookContextMenuRequested(const BookCatalogueResult& book,
                                  const QPoint& globalPos);

private:
    struct CachedSeries {
        QString seriesName;
        QString author;
        QString heroCoverPath;
        QList<BookCatalogueResult> books;  // enriched, ordered by seriesPosition
    };

    void buildUi();
    // Add-whole-series-to-Library (want-to-read): shelves every member book as
    // a file-less record; toggles to remove-all when the series is fully shelved.
    void onAddSeriesToLibraryClicked();
    void refreshAddSeriesButton();
    void setLoading(bool loading);
    void renderSeries(const QString& seriesName, const QString& author,
                      const QString& heroCoverPath,
                      const QList<BookCatalogueResult>& books);
    void rebuildRows();

    // Enrichment pipeline.
    void onSeriesReady(const QString& seriesId, const QString& seriesName,
                       const QList<BookCatalogueResult>& books);
    void onBookEnriched(const QString& bookId, const BookCatalogueResult& book);
    void noteBookSettled();          // page fetch returned (ready or failed)
    void startCoverPhase();
    void finalizeRender();
    void downloadCover(const QString& catalogueId, const QString& coverUrl);
    QString coverPathFor(const QString& catalogueId) const;

    BooksCatalogueLibraryStore* m_store = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    FictionDbClient* m_client = nullptr;
    QString m_coverDir;

    // Currently-rendered series.
    QString m_seriesName;
    QString m_author;
    QString m_heroCoverPath;
    QList<BookCatalogueResult> m_books;   // ordered by seriesPosition

    // In-flight enrichment.
    QString m_loadingSeriesId;
    QString m_loadingSeriesName;
    QList<BookCatalogueResult> m_pending; // accumulates enriched books
    QSet<QString> m_pendingPageIds;       // catalogueIds still awaiting page fetch
    int m_coversRemaining = 0;
    int m_loadToken = 0;                  // bumped per loadSeries; stale guard
    bool m_rendered = false;              // guards double finalize
    QTimer* m_timeout = nullptr;

    QHash<QString, CachedSeries> m_cache;

    QPushButton* m_addSeriesBtn = nullptr;
    QLabel* m_coverLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QScrollArea* m_scroll = nullptr;
    QLabel* m_loadingLabel = nullptr;
    QWidget* m_rowsContainer = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
};
