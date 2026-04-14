#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QSettings>
class QPushButton;
class CoreBridge;
class FadingStackedWidget;
class LibraryListView;
class TileCard;
class TileStrip;
class LibraryScanner;
class SeriesView;
struct SeriesInfo;

class ComicsPage : public QWidget {
    Q_OBJECT
public:
    explicit ComicsPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~ComicsPage();

    void activate();
    void triggerScan();

signals:
    void openComic(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName);

private slots:
    void onSeriesFound(const SeriesInfo& series);
    void onScanFinished(const QList<SeriesInfo>& allSeries);
    void onTileClicked(const QString& seriesPath, const QString& seriesName);
    void showGrid();
    void applySearch();
    void refreshContinueStrip();
    void onCardClicked();
    void onTileContextMenu(const QPoint& pos);
    void onMultiSelectContextMenu(const QList<TileCard*>& selected, const QPoint& globalPos);

private:
    void buildUI();
    void addSeriesTile(const SeriesInfo& series);
    void toggleViewMode();

    CoreBridge*             m_bridge = nullptr;
    FadingStackedWidget*    m_stack = nullptr;
    QWidget*         m_continueSection = nullptr;
    TileStrip*       m_continueStrip = nullptr;
    TileStrip*       m_tileStrip = nullptr;
    LibraryListView* m_listView = nullptr;
    QLabel*          m_statusLabel = nullptr;
    QLineEdit*       m_searchBar = nullptr;
    QComboBox*       m_sortCombo = nullptr;
    QTimer*          m_searchTimer = nullptr;
    SeriesView*      m_seriesView = nullptr;
    QPushButton*     m_viewToggle = nullptr;
    QSlider*         m_densitySlider = nullptr;
    bool             m_gridMode = true;

    // Progress key → file info for continue strip
    struct FileRef { QString filePath; QString seriesPath; QString coverPath; };
    QMap<QString, FileRef> m_progressKeyMap;

    QThread*         m_scanThread = nullptr;
    LibraryScanner*  m_scanner = nullptr;
    bool             m_hasScanned = false;
    bool             m_scanning = false;
};
