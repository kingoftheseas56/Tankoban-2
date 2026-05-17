#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QSettings>
#include <QPushButton>
#include "../LayerEntry.h"
class QScrollArea;
class CoreBridge;
class FadingStackedWidget;
class LibraryListView;
class TileStrip;
class BooksScanner;
class BookSeriesView;
struct BookSeriesInfo;

class BooksPage : public QWidget {
    Q_OBJECT
public:
    explicit BooksPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~BooksPage();

    void activate();
    void triggerScan();

signals:
    void openBook(const QString& filePath);
    void enteredLayer(const tankoban::ui::LayerEntry& entry);
    void exitedLayer();

public slots:
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- no-op restore. This
    // page has no deep state today so there's nothing to restore beyond
    // ensuring the page is on its landing view (which it always is).
    void restoreLayer(const tankoban::ui::LayerEntry& target);

private slots:
    void onBookSeriesFound(const BookSeriesInfo& series);
    void onScanFinished(const QList<BookSeriesInfo>& allBooks);
    void onTileClicked(const QString& seriesPath, const QString& seriesName);
    void showGrid();
    void applySearch();
    void refreshContinueStrip();

private:
    void buildUI();
    void addBookSeriesTile(const BookSeriesInfo& series);

    CoreBridge*    m_bridge = nullptr;

    // Navigation
    FadingStackedWidget* m_stack = nullptr;
    BookSeriesView* m_seriesView = nullptr;

    // Continue Reading
    QWidget*       m_continueSection = nullptr;
    TileStrip*     m_continueStrip = nullptr;
    struct FileRef { QString filePath; QString seriesPath; QString coverPath; };
    QMap<QString, FileRef> m_progressKeyMap;

    // Search & Sort
    QLineEdit*     m_searchBar = nullptr;
    QComboBox*     m_sortCombo = nullptr;
    QTimer*        m_searchTimer = nullptr;

    // Books section
    TileStrip*     m_bookStrip = nullptr;
    QLabel*        m_bookStatus = nullptr;

    // Book Hits section (scored search — individual book tiles)
    QWidget*       m_bookHitsSection = nullptr;
    TileStrip*     m_bookHitsStrip = nullptr;

    // Per-series file list for scored book search
    struct BookFile { QString filePath; QString title; };
    QMap<QString, QList<BookFile>> m_seriesFiles; // seriesPath -> files

    // List view
    LibraryListView* m_listView = nullptr;
    QPushButton*     m_viewToggle = nullptr;
    QSlider*         m_densitySlider = nullptr;
    bool             m_gridMode = true;

    // Scanner
    QThread*       m_scanThread = nullptr;
    BooksScanner*  m_scanner = nullptr;
    bool           m_hasScanned = false;
    bool           m_scanning = false;
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — buffer-not-drop rescan flag.
    bool           m_rescanPending = false;

    // GLOBAL_NAV_HISTORY Task 9: cache the grid QScrollArea pointer so
    // capture/restore don't pay an O(n) findChild walk on every Back/Forward.
    // Mirrors ComicsPage::m_gridScroll (Task 8 caching fix, commit 66b7e34).
    QScrollArea*   m_gridScroll = nullptr;
};
