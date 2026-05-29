#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include "core/book/BookCatalogueResult.h"
#include "core/book/BookResult.h"

class BookScraper;
class BookSearchAggregator;
class BooksCatalogueLibraryStore;
class FictionDbClient;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class TankorentSearchService;

class BookCatalogueDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit BookCatalogueDetailView(QWidget* parent = nullptr);

    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    // Inject the shared network manager + cover-cache dir (same dir the search
    // storefront + series view use, so covers are shared). Constructs a private
    // FictionDbClient used to enrich a clicked book's synopsis/cover/year — the
    // search result stub carries only title + author, so a book opened straight
    // from the storefront has no synopsis until this fetch lands. A dedicated
    // client keeps its bookReady stream off the aggregator's Top-N fetches.
    void setNetwork(QNetworkAccessManager* nam, const QString& coverDir);
    void showBook(const BookCatalogueResult& book, const QString& coverPath);

    // BOOKS_STREMIO_PIVOT §5.2 (2026-05-27) — BooksPage owns BookDownloader
    // and forwards lifecycle events here so the CTA can morph through
    // [Search for downloads] → "Downloading XX%" → [Read].
    void notifyDownloadStarted(const QString& handle);
    void notifyDownloadProgress(const QString& handle, int pct);
    void notifyDownloadComplete(const QString& handle, const QString& filePath);
    void notifyDownloadFailed(const QString& handle, const QString& reason);

    QString currentCatalogueId() const { return m_currentCatalogueId; }

signals:
    void backRequested();

    // §5.2 click-to-download. Carries enough context for BooksPage to fire
    // BookDownloader::startDownload (HTTP) or startMagnetDownload (magnet)
    // AND to construct a CatalogueRecord on completion.
    //
    // `urls` is the FULL mirror list. HTTP sources resolve via
    // scraper->resolveDownload() which returns multiple mirrors (LibGen
    // typically returns 2-5 — primary + library.lol + library.gift etc);
    // BookDownloader walks the list for failover. Magnet sources pack a
    // single-element list containing the magnet URI. The first-URL is also
    // duplicated into BookResult.downloadUrl for display + record-construction
    // compatibility (the existing CatalogueRecord stores one canonical URL).
    void downloadRequested(const QString& sourceId,
                           const BookResult& row,
                           const QStringList& urls,
                           const BookCatalogueResult& book,
                           const QString& coverPath);

    // §5.2 Read affordance — primary CTA emits this when the current book
    // is already in the library and the user clicks [Read].
    void readRequested(const QString& catalogueId, const QString& filePath);

private:
    struct SourceSection {
        QWidget* container = nullptr;
        QLabel* heading = nullptr;
        QWidget* rows = nullptr;
        int resultCount = 0;
    };

    void buildUi();
    void resetSourceSections();
    void startSourceSearch();
    void recreateSourceAggregator(int generation);
    void renderSourceRow(SourceSection& section,
                         const QString& sourceId,
                         const BookResult& result);
    void renderSourceMessage(SourceSection& section, const QString& sourceId,
                             const QString& message);
    void clearRows(QWidget* rows);
    void setCover(const QString& coverPath);

    // Metadata enrichment (synopsis / cover / year / pages) for a book opened
    // from the search storefront. fetchBook lands here; we merge into the
    // current book + repaint the hero, guarded by catalogueId so a stale reply
    // from a previously-shown book is ignored.
    void onBookMetaReady(const QString& bookId, const BookCatalogueResult& book);
    void applyEnrichedMeta(const BookCatalogueResult& book);
    QString enrichedCoverPathFor(const QString& catalogueId) const;
    void downloadEnrichedCover(const QString& catalogueId, const QString& coverUrl);

    // §5.2 CTA state machine + click → resolve → emit dispatcher.
    void refreshPrimaryCta();
    void onPrimaryCtaClicked();
    void handleSourceRowClick(const QString& sourceId, const BookResult& result);

    // Add-to-Library (want-to-read bookmark) — a book can be in the library
    // without a downloaded file (record with addedAt but empty filePath).
    void onAddToLibraryClicked();
    void refreshAddLibraryButton();

    QPushButton* m_backButton = nullptr;
    QPushButton* m_addLibraryBtn = nullptr;
    QPushButton* m_primaryCta = nullptr;
    QLabel* m_coverLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_authorLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QLabel* m_sourceStatus = nullptr;

    QHash<QString, SourceSection> m_sourceSections;

    QNetworkAccessManager* m_nam = nullptr;
    TankorentSearchService* m_tankorentService = nullptr;
    BookSearchAggregator* m_sourceAggregator = nullptr;
    QList<BookScraper*> m_sourceScrapers;

    // Metadata enrichment — dedicated FictionDbClient + shared cover cache.
    FictionDbClient* m_metaClient = nullptr;
    QNetworkAccessManager* m_metaNam = nullptr;  // shared NAM for cover fetch
    QString m_coverDir;

    BooksCatalogueLibraryStore* m_catalogueStore = nullptr;
    BookCatalogueResult m_currentBook;
    QString m_currentCatalogueId;
    QString m_currentCoverPath;
    int m_generation = 0;

    // §5.2 active-download tracking. Detail view doesn't own BookDownloader;
    // BooksPage forwards the handle + lifecycle so this surface can reflect
    // progress without driving the transport.
    bool m_downloadInFlight = false;
    int m_downloadPct = 0;
    QString m_activeHandle;
    QString m_activeFilePath;

    // §5.2 resolve-then-download bridge. HTTP sources (libgen / annas-archive)
    // need a scraper->resolveDownload(md5) call to get a fresh /get.php?key=XXX
    // URL (LibGen's key rotates ~60s per reference_libgen_url_params.md). We
    // stash the click context here keyed by md5; when scraper emits
    // downloadResolved we dispatch the real downloadRequested with the
    // resolved URL substituted into BookResult.downloadUrl.
    struct PendingResolve {
        QString sourceId;
        BookResult result;
    };
    QHash<QString, PendingResolve> m_pendingResolves;
};
