// src/ui/pages/comics/ComicsSeriesView.h
#pragma once

// TANKOYOMI_VOLUME_PIVOT Phase 7 -- scoped stub of the post-pivot Comics
// series detail view. Replaces (eventually, in Phase 9) the legacy
// ComicsTankoyomiDetailView. The widget renders a Stream-blueprint-style
// detail surface (banner / hero / volume list / sources placeholder) and
// is wired to AniListClient + AniListCache for metadata.
//
// This is a SCOPED STUB, not a literal fork of the 2810-line
// StreamDetailView. The gate per the plan ("compiles in isolation; Phase
// 9 wires it into ComicsPage") is met with ~500 LOC of native widget
// construction. Future phases fill in the deferred surface (sources
// panel, per-row covers, library state, banner image, navigation):
//   - PHASE 8  : ComicsSourcesPanel replaces m_sourcesPlaceholder; the
//                volume rows route through the sources panel.
//   - PHASE 9  : ComicsPage wires showSeries() + clearView() + openVolume.
//   - PHASE 10 : Progress + Status columns pull from MangaDownloadIndex;
//                openVolume emits cbzPath for downloaded rows.
//   - PHASE 12 : Banner image + per-row cover thumbnails load async via
//                QNetworkAccessManager.

#include "ComicsSourcesPanel.h"
#include "core/manga/anilist/AniListTypes.h"

#include <QHash>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

class QPaintEvent;

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTableWidget;
class QVBoxLayout;
class MangaDownloadIndex;

namespace tankoban::manga {
class NyaaRuntimeSource;
namespace premium {
class PremiumCatalog;
} // namespace premium
} // namespace tankoban::manga

namespace tankoban::manga::anilist {
class AniListClient;
class AniListCache;
} // namespace tankoban::manga::anilist

namespace tankoban::manga::comics {

class ComicsSeriesView : public QWidget
{
    Q_OBJECT
public:
    // PHASE 8: ctor extended with catalog + nyaa pointers for the sources
    // panel. All four pointers are NON-OWNING; the parent (Phase 9
    // ComicsPage) owns lifetimes. catalog / nyaa may be null -- the panel
    // tolerates null gracefully (no catalog hit / no runtime search).
    ComicsSeriesView(anilist::AniListClient* client,
                     anilist::AniListCache*  cache,
                     premium::PremiumCatalog* catalog,
                     NyaaRuntimeSource*       nyaa,
                     MangaDownloadIndex*      downloadIndex,
                     QWidget*                 parent = nullptr);
    ~ComicsSeriesView() override;

    // Entry point: render the series at the given AniList preview. Will
    // consult the cache first, then fire a background refetch.
    void showSeries(const anilist::MediaPreview& preview);

    // PHASE 9: called by ComicsPage when navigating away.
    void clearView();

    // PHASE 12: post-download cbz-extracted cover replaces the AniList-loaded
    // thumb in the volume row's Cover cell. seriesId may be a real catalog
    // seriesId or the synthesized "anilist_<N>" slug; we match by parsing the
    // slug prefix against m_currentAnilistId. Stale events (seriesId for a
    // series not currently displayed) are ignored.
    void setVolumeCoverFromDisk(const QString& seriesId, int volumeNumber,
                                const QString& coverPath);
    int currentAnilistId() const { return m_currentAnilistId; }

public slots:
    void setVolumeDownloadState(int volumeNumber, const QString& cbzPath,
                                bool downloaded);
    void setVolumeStatusText(int volumeNumber, const QString& statusText);

signals:
    // Fired when user clicks Download on a volume row OR clicks a downloaded
    // row to open the reader. cbzPath is empty for the Download case (caller
    // dispatches to the Sources panel for download); non-empty for the Open
    // case (caller dispatches to the comic reader).
    void openVolume(int volumeNumber, const QString& cbzPath);

    // PHASE 8: forwards ComicsSourcesPanel::downloadRequested verbatim.
    // PHASE 9: ComicsPage owns the provider pointers (TorrentVolumeProvider
    // for Catalog + NyaaRuntime kinds; WeebCentralVolumePacker for WC kind)
    // and routes this signal to the appropriate requestVolume() call.
    void downloadDispatchRequested(const UnifiedSourceRow& row,
                                   const QString& seriesTitle,
                                   int            anilistSeriesId,
                                   int            volumeNumber,
                                   const QStringList& chapterIds);

protected:
    // Bug-1 (Stremio-style background) -- paint the AniList banner as a
    // full-viewport wallpaper with a vertical legibility gradient on top,
    // rather than docking it as a top pane. All child widgets sit on
    // transparent backgrounds + render over the wallpaper.
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onSeriesSucceeded(int requestId, const tankoban::manga::anilist::MediaDetail& detail);
    void onSeriesFailed(int requestId, const QString& reason);

    // PHASE 8: volume-row click handler -- populates the sources panel.
    void onVolumeCellClicked(int row, int column);
    void onLibraryButtonClicked();

private:
    void buildUi();
    void renderDetail(const anilist::MediaDetail& detail);
    void renderEmpty(const QString& reason = {});
    void refreshLibraryButton();

    // PHASE 12: async-load a cover URL into a target volume row's Cover cell.
    // Uses QPixmapCache keyed by URL; cache hits paint synchronously.
    // volumeNumber identifies the target row; -1 means the banner.
    void loadCoverUrlForVolume(const QString& url, int volumeNumber);
    void loadBannerUrl(const QString& url);
    void applyPixmapToVolumeRow(int volumeNumber, const QPixmap& pm);
    void setRowOpenIndicator(int tableRow, bool downloaded);

    anilist::AniListClient*  m_client  = nullptr;  // non-owning
    anilist::AniListCache*   m_cache   = nullptr;  // non-owning
    premium::PremiumCatalog* m_catalog = nullptr;  // non-owning; PHASE 8
    NyaaRuntimeSource*       m_nyaa    = nullptr;  // non-owning; PHASE 8
    MangaDownloadIndex*      m_downloadIndex = nullptr; // non-owning; Phase B.3

    // Bug-1 (Stremio-style background): the banner is no longer a widget in
    // the layout -- it's a QPixmap painted in paintEvent across the full
    // widget rect with a vertical gradient overlay for text legibility.
    QPixmap               m_bannerPixmap;
    QLabel*               m_title         = nullptr;
    QLabel*               m_metaLine      = nullptr;
    QLabel*               m_synopsis      = nullptr;
    QPushButton*          m_libraryButton = nullptr;
    QTableWidget*         m_volumesTable  = nullptr;
    ComicsSourcesPanel*   m_sourcesPanel  = nullptr;  // PHASE 8: replaces the placeholder QLabel

    // Cached during renderDetail so onVolumeCellClicked can pass the
    // VolumeRow to the sources panel without re-running the mapper.
    QList<anilist::VolumeRow> m_currentVolumeRows;
    QString                   m_currentSeriesTitle;

    int m_pendingSeriesReqId = -1;
    int m_currentAnilistId   = 0;
    int m_nextRequestId      = 1;
};

} // namespace tankoban::manga::comics
