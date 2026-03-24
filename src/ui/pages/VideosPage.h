#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTimer>
#include <QSettings>
#include <QStackedWidget>

class CoreBridge;
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

signals:
    void playVideo(const QString& filePath);

private slots:
    void onShowFound(const ShowInfo& show);
    void onScanFinished(const QList<ShowInfo>& allShows);
    void applySearch();
    void onTileClicked(const QString& showPath, const QString& showName);
    void showGrid();

private:
    void buildUI();
    static QString formatSize(qint64 bytes);

    CoreBridge*      m_bridge = nullptr;
    QStackedWidget*  m_stack = nullptr;
    TileStrip*       m_tileStrip = nullptr;
    QLabel*          m_statusLabel = nullptr;
    QLineEdit*       m_searchBar = nullptr;
    QComboBox*       m_sortCombo = nullptr;
    QTimer*          m_searchTimer = nullptr;
    ShowView*        m_showView = nullptr;

    QThread*         m_scanThread = nullptr;
    VideosScanner*   m_scanner = nullptr;
    bool             m_hasScanned = false;
    bool             m_scanning = false;
};
