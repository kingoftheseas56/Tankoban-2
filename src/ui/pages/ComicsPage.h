#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QStackedWidget>

class CoreBridge;
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
    void openComic(const QString& cbzPath);

private slots:
    void onSeriesFound(const SeriesInfo& series);
    void onScanFinished(const QList<SeriesInfo>& allSeries);
    void onTileClicked(const QString& seriesPath, const QString& seriesName);
    void showGrid();

private:
    void buildUI();

    CoreBridge*      m_bridge = nullptr;
    QStackedWidget*  m_stack = nullptr;
    TileStrip*       m_tileStrip = nullptr;
    QLabel*          m_statusLabel = nullptr;
    SeriesView*      m_seriesView = nullptr;

    QThread*         m_scanThread = nullptr;
    LibraryScanner*  m_scanner = nullptr;
    bool             m_hasScanned = false;
    bool             m_scanning = false;
};
