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
class BooksCatalogueLibraryStore;
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

private:
    void buildUI();
    void addCatalogueRecordTile(const CatalogueRecord& record);
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
    bool m_catalogueDetailReturnToSearch = false;

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
    BookCatalogueAggregator* m_catalogueAggregator = nullptr;
    BooksCatalogueLibraryStore* m_catalogueStore = nullptr;
    QString m_catalogueCoverDir;

    LibraryListView* m_listView = nullptr;
    QPushButton* m_viewToggle = nullptr;
    QSlider* m_densitySlider = nullptr;
    bool m_gridMode = true;

    QScrollArea* m_gridScroll = nullptr;
};
