#pragma once

#include <QComboBox>
#include <QHash>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QPushButton>
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

signals:
    void openBook(const QString& filePath);
    void enteredLayer(const tankoban::ui::LayerEntry& entry);
    void exitedLayer();

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

private:
    void buildUI();
    void addCatalogueRecordTile(const CatalogueRecord& record);
    void addLibrarySeriesTile(const CatalogueRecord& rep, int ownedCount);
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
    };
    QHash<QString, ActiveCatalogueDownload> m_activeDownloads;
    static CatalogueRecord buildRecordFromContext(
        const ActiveCatalogueDownload& ctx, const QString& filePath);

    LibraryListView* m_listView = nullptr;
    QPushButton* m_viewToggle = nullptr;
    QSlider* m_densitySlider = nullptr;
    bool m_gridMode = true;

    QScrollArea* m_gridScroll = nullptr;
};
