#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QSlider>

class CoreBridge;
class StreamLibrary;
class TileStrip;
class TileCard;
class QNetworkAccessManager;

class StreamLibraryLayout : public QWidget
{
    Q_OBJECT

public:
    explicit StreamLibraryLayout(CoreBridge* bridge, StreamLibrary* library,
                                 QWidget* parent = nullptr);

    void refresh();

signals:
    void showClicked(const QString& imdbId);
    void showRightClicked(const QString& imdbId, const QPoint& globalPos);

private:
    void buildUI();
    void populateTiles();
    void downloadPoster(const QString& imdbId, const QString& posterUrl);
    void cleanupOrphanPosters();
    QString posterCachePath(const QString& imdbId) const;

    CoreBridge*    m_bridge;
    StreamLibrary* m_library;
    QNetworkAccessManager* m_nam;

    // UI
    QLabel*    m_sectionLabel = nullptr;
    QComboBox* m_sortCombo    = nullptr;
    QSlider*   m_densitySlider = nullptr;
    TileStrip* m_strip        = nullptr;
    QLabel*    m_emptyLabel   = nullptr;

    QString m_posterCacheDir;
};
