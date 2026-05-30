#pragma once

#include <QComboBox>
#include <QHash>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "../LayerEntry.h"
#include "core/book/SeriesDetector.h"

class BookCatalogueAggregator;
class BookCatalogueDetailView;
class BookCatalogueSearchWidget;
class BookSeriesDetailView;
class BookDownloader;
class BookSeriesIndex;
class BookSeriesIndexBuilder;
class BooksCatalogueLibraryStore;
class FictionDbClient;
class CoreBridge;
class FadingStackedWidget;
class LibraryListView;
class QEvent;
class QFrame;
class QNetworkAccessManager;
class QScrollArea;
class TileCard;
class TileStrip;
struct BookCatalogueResult;
struct BookResult;
struct CatalogueRecord;

class BooksPage : public QWidget {
    Q_OBJECT
public:
    explicit BooksPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~BooksPage();

    void activate();
    Q_INVOKABLE bool dispatchDevCommand(const QString& cmd,
                                        const QJsonObject& payload,
                                        QJsonObject& reply);

    QJsonObject devSnapshot() const;
    QJsonObject devLibrarySnapshot() const;
    QJsonObject devLibrarySection() const;

    // BOOKS_DOWNLOADS_SIDEBAR_PAGE (2026-05-28) — projection of an in-flight
    // download for the Books Downloads page.
    struct ActiveDownloadInfo {
        QString catalogueId;
        QString title;
        QString author;
        QString coverPath;
        int     percent = 0;
    };
    QList<ActiveDownloadInfo> activeDownloads() const;
    BooksCatalogueLibraryStore* catalogueStore() const { return m_catalogueStore; }
    QString catalogueCoverDir() const { return m_catalogueCoverDir; }

    // Open a series' detail view (used by the library grid tile + the Books
    // Downloads page). Loads + switches the internal stack to the series view.
    void openSeries(const QString& seriesId);

signals:
    void openBook(const QString& filePath);
    void enteredLayer(const tankoban::ui::LayerEntry& entry);
    void exitedLayer();
    // Fired on download start / progress / complete / fail so the Books
    // Downloads page can refresh its in-progress section.
    void downloadsChanged();

public slots:
    void restoreLayer(const tankoban::ui::LayerEntry& target);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void showGrid();
    void showCatalogueSearchMode(const QString& query);
    void applySearch();
    void refreshContinueStrip();
    void rebuildBookGrid();

    // BOOKS_LIBRARY_CONTEXT_MENU (2026-05-28) — right-click menu on m_bookStrip
    // tiles. Branches on the tile's catalogueSeries / catalogueRecord property.
    void showBookContextMenu(const QPoint& pos);
    // Right-click on a book row inside the series detail view.
    void onSeriesBookContextMenu(const BookCatalogueResult& book,
                                 const QPoint& globalPos);

    // §5.2 (2026-05-27) — catalogue download lifecycle. Detail view emits
    // downloadRequested when a source row gets clicked; BooksPage owns
    // BookDownloader + creates the CatalogueRecord on completion.
    //
    // `urls` is the full mirror list (HTTP path: multiple LibGen mirrors;
    // magnet path: single-element list with the magnet URI). Passed
    // straight to BookDownloader so its intra-row failover walks all
    // mirrors before reporting failure.
    void onCatalogueDownloadRequested(const QString& sourceId,
                                      const BookResult& row,
                                      const QStringList& urls,
                                      const BookCatalogueResult& book,
                                      const QString& coverPath);
    void onBookDownloadProgress(const QString& handle,
                                qint64 bytesReceived,
                                qint64 bytesTotal);
    void onBookDownloadComplete(const QString& handle, const QString& filePath);
    void onBookDownloadFailed(const QString& handle, const QString& reason);
    void onCatalogueReadRequested(const QString& catalogueId,
                                  const QString& filePath);

    // Right-click "Add/Remove library" from the search storefront.
    // Book: toggle a single file-less want-to-read record.
    // Series: remove-all if any member is shelved, else fetch members + add all.
    void onSearchBookLibraryToggle(const BookCatalogueResult& book);
    void onSearchSeriesLibraryToggle(const BookCatalogueResult& seriesStub);

private:
    void buildUI();
    // Build a file-less "want to read" record (addedAt set, no filePath) from a
    // catalogue result; used by the search-storefront add-to-library paths.
    CatalogueRecord wishlistRecordFromResult(const BookCatalogueResult& b) const;
    void addCatalogueRecordTile(const CatalogueRecord& record);
    void addLibrarySeriesTile(const CatalogueRecord& rep, const QString& author,
                              const QString& seriesName, int totalCount);
    // Shared owned-book context menu (Read / Mark / Rename / Remove / Reveal /
    // Copy), used by both the library grid and the series detail rows.
    void showOwnedBookMenu(const QString& catalogueId, const QPoint& globalPos);
    // Shared 3-way remove dialog (library-only / delete-file / cancel) applied
    // to one book or every owned book of a series.
    void removeFromLibrary(const QStringList& catalogueIds,
                           const QString& subjectLabel);
    BookCatalogueResult catalogueRecordToResult(const CatalogueRecord& record) const;
    void loadSearchHistory();
    void saveSearchHistory();
    void pushSearchHistory(const QString& query);
    void removeSearchHistoryEntry(const QString& query);
    void clearSearchHistory();
    void buildSearchHistoryDropdown();
    void showSearchHistoryDropdown();
    void hideSearchHistoryDropdown();
    void positionSearchHistoryDropdown();

    CoreBridge* m_bridge = nullptr;

    FadingStackedWidget* m_stack = nullptr;
    BookCatalogueDetailView* m_catalogueDetailView = nullptr;
    BookCatalogueSearchWidget* m_catalogueSearchView = nullptr;
    BookSeriesDetailView* m_seriesDetailView = nullptr;
    bool m_catalogueDetailReturnToSearch = false;
    bool m_catalogueDetailReturnToSeries = false;

    QWidget* m_continueSection = nullptr;
    TileStrip* m_continueStrip = nullptr;

    QLineEdit* m_searchBar = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QTimer* m_searchTimer = nullptr;
    QFrame* m_searchHistoryDropdown = nullptr;
    QWidget* m_searchHistoryList = nullptr;
    QTimer* m_searchHistoryHideTimer = nullptr;
    QStringList m_searchHistory;
    static constexpr int kMaxSearchHistory = 10;

    TileStrip* m_bookStrip = nullptr;
    QLabel* m_bookStatus = nullptr;

    QWidget* m_bookHitsSection = nullptr;
    TileStrip* m_bookHitsStrip = nullptr;

    QNetworkAccessManager* m_catalogueNam = nullptr;
    FictionDbClient* m_fictiondb = nullptr;
    BookSeriesIndex* m_seriesIndex = nullptr;
    BookSeriesIndexBuilder* m_indexBuilder = nullptr;
    BookCatalogueAggregator* m_catalogueAggregator = nullptr;
    BooksCatalogueLibraryStore* m_catalogueStore = nullptr;
    QString m_catalogueCoverDir;

    // §5.2 catalogue download infrastructure. m_bookDownloader is lazy —
    // constructed on first onCatalogueDownloadRequested so window()-mediated
    // TorrentClient lookup works (page is parented + visible by then).
    BookDownloader* m_bookDownloader = nullptr;
    struct ActiveCatalogueDownload {
        QString sourceId;
        BookCatalogueResult book;
        QString coverPath;
        QString format;
        QString filePath;  // set on completion
        int     percent = 0;  // latest progress %, updated in onBookDownloadProgress
    };
    QHash<QString, ActiveCatalogueDownload> m_activeDownloads;
    static CatalogueRecord buildRecordFromContext(
        const ActiveCatalogueDownload& ctx, const QString& filePath);

    // SeriesIds awaiting a fetchSeries reply to add all members to the library
    // (right-click "Add series to library" from the search storefront).
    QSet<QString> m_pendingSeriesLibraryAdd;

    LibraryListView* m_listView = nullptr;
    QPushButton* m_viewToggle = nullptr;
    QSlider* m_densitySlider = nullptr;
    bool m_gridMode = true;

    QScrollArea* m_gridScroll = nullptr;
};
