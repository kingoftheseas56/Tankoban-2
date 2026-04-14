#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QSettings>
#include <QMap>
class QPushButton;
class CoreBridge;
class FadingStackedWidget;
class LibraryListView;
class TileStrip;
class VideosScanner;
class ShowView;
struct ShowInfo;

class VideosPage : public QWidget {
    Q_OBJECT
public:
    explicit VideosPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~VideosPage();

    void activate();
    void triggerScan();
    void refreshContinueOnly();

signals:
    void playVideo(const QString& filePath);

private slots:
    void onShowFound(const ShowInfo& show);
    void onScanFinished(const QList<ShowInfo>& allShows);
    void applySearch();
    void onTileClicked(const QString& showPath, const QString& showName);
    void showGrid();
    void refreshContinueStrip();

private:
    void buildUI();
    void addShowTile(const ShowInfo& show);
    void toggleViewMode();
    void executePendingClick();
    bool eventFilter(QObject* obj, QEvent* event) override;
    static QString formatSize(qint64 bytes);

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
    ShowView*        m_showView = nullptr;
    QPushButton*     m_viewToggle = nullptr;
    QSlider*         m_densitySlider = nullptr;
    bool             m_gridMode = true;

    // 250ms single-click delay (double-click cancels and executes immediately)
    QTimer*          m_clickTimer = nullptr;
    QString          m_pendingClickPath;
    QString          m_pendingClickName;
    bool             m_pendingIsPlay = false;  // true = play video, false = open show view
    bool             m_pendingIsLoose = false;

    // Throttle continue strip refresh during active playback (max once per 5s)
    QTimer*          m_continueRefreshThrottle = nullptr;

    QThread*         m_scanThread = nullptr;
    VideosScanner*   m_scanner = nullptr;
    bool             m_hasScanned = false;
    bool             m_scanning = false;

    // Scan-time durations per show (showPath → {filePath → durationSec})
    QMap<QString, QMap<QString, double>> m_showDurations;

    // File path → show root (for continue strip dedup by show, not by subfolder)
    QMap<QString, QString> m_fileToShowRoot;
    QMap<QString, QString> m_showPathToName;
};
