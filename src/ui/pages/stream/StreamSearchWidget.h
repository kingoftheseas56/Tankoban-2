#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>

class CinemetaClient;
class StreamLibrary;
class TileStrip;
class TileCard;
struct CinemetaEntry;
class QNetworkAccessManager;

class StreamSearchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StreamSearchWidget(CinemetaClient* cinemeta, StreamLibrary* library,
                                QWidget* parent = nullptr);

    void search(const QString& query);

signals:
    void backRequested();
    void libraryChanged();

private:
    void buildUI();
    void clearResults();
    void onCatalogResults(const QList<CinemetaEntry>& results);
    void onCatalogError(const QString& message);
    void addResultCard(const CinemetaEntry& entry);
    void downloadPoster(const QString& imdbId, const QString& posterUrl, TileCard* card);
    void updateInLibraryBadge(TileCard* card);

    CinemetaClient* m_cinemeta;
    StreamLibrary*  m_library;
    QNetworkAccessManager* m_nam;

    // UI
    QPushButton* m_backBtn     = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QScrollArea* m_scroll      = nullptr;
    TileStrip*   m_strip       = nullptr;

    QString m_posterCacheDir;
};
