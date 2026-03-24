#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>

class CoreBridge;
class TileStrip;
class LibraryScanner;
struct SeriesInfo;

class ComicsPage : public QWidget {
    Q_OBJECT
public:
    explicit ComicsPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~ComicsPage();

    void activate();
    void triggerScan();

private slots:
    void onSeriesFound(const SeriesInfo& series);
    void onScanFinished(const QList<SeriesInfo>& allSeries);

private:
    void buildUI();

    CoreBridge*      m_bridge = nullptr;
    TileStrip*       m_tileStrip = nullptr;
    QLabel*          m_statusLabel = nullptr;

    QThread*         m_scanThread = nullptr;
    LibraryScanner*  m_scanner = nullptr;
    bool             m_hasScanned = false;
};
