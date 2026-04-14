#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QMap>
#include <QList>

class CoreBridge;
class CinemetaClient;
class TorrentioClient;
class StreamLibrary;
struct StreamEpisode;
struct StreamLibraryEntry;

class StreamDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit StreamDetailView(CoreBridge* bridge, CinemetaClient* cinemeta,
                              TorrentioClient* torrentio, StreamLibrary* library,
                              QWidget* parent = nullptr);

    void showEntry(const QString& imdbId);

signals:
    void backRequested();
    void playRequested(const QString& imdbId, const QString& mediaType,
                       int season, int episode);

private:
    void buildUI();
    void onSeriesMetaReady(const QString& imdbId,
                           const QMap<int, QList<StreamEpisode>>& seasons);
    void onSeasonChanged(int comboIndex);
    void populateEpisodeTable(int season);
    void onEpisodeActivated(int row, int col);
    void onPlayMovieClicked();
    void updateProgressColumn();

    CoreBridge*      m_bridge;
    CinemetaClient*  m_cinemeta;
    TorrentioClient* m_torrentio;
    StreamLibrary*   m_library;

    // Current state
    QString m_currentImdb;
    QString m_currentType;
    QMap<int, QList<StreamEpisode>> m_seasons;

    // UI
    QPushButton* m_backBtn       = nullptr;
    QLabel*      m_titleLabel    = nullptr;
    QLabel*      m_infoLabel     = nullptr;
    QLabel*      m_descLabel     = nullptr;
    QWidget*     m_seasonRow     = nullptr;
    QComboBox*   m_seasonCombo   = nullptr;
    QPushButton* m_playMovieBtn  = nullptr;
    QTableWidget* m_episodeTable = nullptr;
    QLabel*      m_statusLabel   = nullptr;
};
