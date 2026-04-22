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
class CoreBridge;
class FadingStackedWidget;
class LibraryListView;
class TileStrip;
class BooksScanner;
class BookSeriesView;
class AudiobookDetailView;
struct BookSeriesInfo;
struct AudiobookInfo;

class BooksPage : public QWidget {
    Q_OBJECT
public:
    explicit BooksPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~BooksPage();

    void activate();
    void triggerScan();

signals:
    void openBook(const QString& filePath);

private slots:
    void onBookSeriesFound(const BookSeriesInfo& series);
    void onAudiobookFound(const AudiobookInfo& audiobook);
    void onScanFinished(const QList<BookSeriesInfo>& allBooks,
                        const QList<AudiobookInfo>& allAudiobooks);
    void onTileClicked(const QString& seriesPath, const QString& seriesName);
    void showGrid();
    void applySearch();
    void refreshContinueStrip();

private:
    void buildUI();
    void addBookSeriesTile(const BookSeriesInfo& series);
    void addAudiobookTile(const AudiobookInfo& audiobook);

    CoreBridge*    m_bridge = nullptr;

    // Navigation
    FadingStackedWidget* m_stack = nullptr;
    BookSeriesView* m_seriesView = nullptr;
    AudiobookDetailView* m_audiobookDetailView = nullptr;

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

    // Audiobooks section
    QWidget*       m_audiobookSection = nullptr;
    QLabel*        m_audiobookTitle = nullptr;
    TileStrip*     m_audiobookStrip = nullptr;
    QLabel*        m_audiobookStatus = nullptr;

    // Scanner
    QThread*       m_scanThread = nullptr;
    BooksScanner*  m_scanner = nullptr;
    bool           m_hasScanned = false;
    bool           m_scanning = false;
};
