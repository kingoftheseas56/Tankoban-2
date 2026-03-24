#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>

class CoreBridge;
class TileStrip;
class VideosScanner;
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

private:
    void buildUI();
    static QString formatSize(qint64 bytes);

    CoreBridge*      m_bridge = nullptr;
    TileStrip*       m_tileStrip = nullptr;
    QLabel*          m_statusLabel = nullptr;

    QThread*         m_scanThread = nullptr;
    VideosScanner*   m_scanner = nullptr;
    bool             m_hasScanned = false;
    bool             m_scanning = false;
};
