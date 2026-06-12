#pragma once

// STREAM_DOWNLOADS_SIDEBAR_PAGE 2026-05-25 (Agent 4 commission via Trigger D
// to Agent 7) - aggregate Downloads page accessible from the SidebarDrawer's
// new "Downloads" entry. Renders two sections:
//   - Active: in-flight Theatre downloads, grouped by IMDb show, sourced from
//     TorrentClient::streamBulkGroups() + streamBulkSnapshotForImdbSeason.
//   - History: completed Theatre downloads, grouped by IMDb show, sourced
//     from StreamDownloadIndex::all().
//
// Closes the 2026-05-12 STREAM_DOWNLOADS_NETFLIX_OVERHAUL spec gap that the
// original arc marked closed without shipping. Read-only display in v1;
// inline cancel/pause/resume controls deferred to v1.x.

#include <QFrame>
#include <QHash>
#include <QString>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;
class QUrl;
class QNetworkAccessManager;
class TorrentClient;
class StreamDownloadIndex;

namespace tankostream::stream { class MetaAggregator; }
namespace tankostream::addon { struct MetaItem; }

class StreamDownloadsPage : public QFrame
{
    Q_OBJECT
public:
    explicit StreamDownloadsPage(QWidget* parent = nullptr);
    ~StreamDownloadsPage() override = default;

    // Injection points. Mirror the TorrentClient + StreamDownloadIndex
    // wire-up pattern from StreamLibraryLayout (see StreamPage.cpp's
    // setStreamDownloadIndex call). Same-pointer guard prevents
    // re-subscribing on repeat calls. Disconnects from the previous
    // pointer (if any) before binding the new one.
    void setTorrentClient(TorrentClient* client);
    void setStreamDownloadIndex(StreamDownloadIndex* index);

    // THEATRE/COMICS Downloads redesign 2026-05-29 — read-time metadata
    // enrichment source. fetchMetaItem(imdbId,"series") yields both the poster
    // and per-episode titles; reentrant + 60s-cached, so safe to drive here
    // without disturbing the detail-view series fetch.
    void setMetaAggregator(tankostream::stream::MetaAggregator* agg);

signals:
    // Topbar back-button click - MainWindow's slot returns to the
    // previously-active page (it tracks lastActivePage in activatePage).
    void backRequested();

    // Clicking a History row emits this signal so MainWindow can route
    // through its existing onPlayLocalFileFromStreamRequested slot
    // (parameter parity with StreamDetailView::playLocalFileFromStreamRequested).
    void playLocalFileRequested(const QString& canonicalPath,
                                const QString& imdbId,
                                const QString& showTitle,
                                int season,
                                int episode);

private slots:
    void refreshActive();
    void refreshHistory();
    void onMetaItemReady(const tankostream::addon::MetaItem& item);

private:
    void buildUi();
    void updateEmptyState();
    QWidget* makePosterWidget(const QString& imdbId, const QString& title);
    void savePosterFrom(const QString& imdbId, const QUrl& posterUrl);

    // Per-show widget handles, keyed by imdbId, so async metadata can repaint
    // the right card after the synchronous (placeholder) render.
    struct DownloadCardRefs {
        QFrame*  card         = nullptr;
        QWidget* posterWidget = nullptr;   // QLabel painting poster / placeholder
        QLabel*  titleLabel   = nullptr;   // show title
        // "<season>:<episode>" (or "movie") -> the clickable row button whose
        // text is rebuilt to the real title on enrichment.
        QHash<QString, QPushButton*> rowTitleByKey;
    };

    TorrentClient*       m_torrentClient = nullptr;
    StreamDownloadIndex* m_streamDownloadIndex = nullptr;
    tankostream::stream::MetaAggregator* m_metaAggregator = nullptr;
    QNetworkAccessManager* m_posterNam = nullptr;
    QHash<QString, DownloadCardRefs> m_historyCards;
    QHash<QString, DownloadCardRefs> m_activeCards;

    QPushButton*  m_backBtn        = nullptr;
    QLabel*       m_titleLabel     = nullptr;
    QScrollArea*  m_scroll         = nullptr;
    QWidget*      m_scrollContent  = nullptr;
    QVBoxLayout*  m_contentLayout  = nullptr;
    QLabel*       m_activeHeader   = nullptr;
    QWidget*      m_activeBody     = nullptr;
    QVBoxLayout*  m_activeBodyLayout = nullptr;
    QLabel*       m_historyHeader  = nullptr;
    QWidget*      m_historyBody    = nullptr;
    QVBoxLayout*  m_historyBodyLayout = nullptr;
    QLabel*       m_emptyState     = nullptr;
};
