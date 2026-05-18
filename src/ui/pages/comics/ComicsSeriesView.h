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
#include <QJsonObject>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QWidget>

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
    void setVolumeRows(const QList<anilist::VolumeRow>& rows);
    int currentAnilistId() const { return m_currentAnilistId; }

    QJsonObject devSnapshot() const;
    QJsonObject devSelectVolume(int row);
    QJsonObject devSourcesSnapshot() const;
    QJsonObject devDispatchVolume(int volumeNumber, const QString& source);

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

    // STREAM_PORT 2026-05-18 Task 1: emitted when the Back button in the
    // action row is clicked. Renamed 2026-05-18 ~5:35pm IST after first-
    // smoke regression: original name `navigationRequested` collided with
    // ComicsPage's existing same-named signal (page-level push-onto-history
    // emitter), semantically opposite intent. Stream-blueprint parity name:
    // matches StreamDetailView::backRequested at StreamDetailView.h:128.
    // ComicsPage wires this to onDetailBack (which exists since 2026-05-16
    // and was already used by the Esc shortcut + future deep-link recovery
    // -- the comment at ComicsPage.cpp:1739-1743 was literally waiting for
    // this signal to come from the new view).
    void backRequested();

    // STREAM_PORT 2026-05-18 Task 5: multi-volume bulk dispatch.
    // ComicsPage v1.x will route this through the default provider
    // (catalog if present, otherwise Tankoyomi) once bulk routing is wired.
    // Option A chosen: bulk path has no per-source-picked context (unlike the
    // single-volume downloadDispatchRequested which carries a UnifiedSourceRow),
    // so a separate signal avoids fabricating a dummy UnifiedSourceRow.
    // NOTE v1: ComicsPage does NOT yet wire this receiver -- the emit is a
    // no-op in v1. ComicsPage v1.x adds bulk routing alongside the unified
    // download-dispatch refactor.
    void bulkDownloadRequested(int anilistId,
                               const QList<anilist::VolumeRow>& volumes);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onSeriesSucceeded(int requestId, const tankoban::manga::anilist::MediaDetail& detail);
    void onSeriesFailed(int requestId, const QString& reason);

    // PHASE 8: volume-row click handler -- opens cbz if downloaded.
    // F1 (2026-05-18): mouse-only path now; Sources populate lives on
    // onVolumeCurrentChanged so keyboard arrow nav drives the panel too.
    void onVolumeCellClicked(int row, int column);
    void onVolumeCurrentChanged(int currentRow, int currentColumn,
                                int previousRow, int previousColumn);
    void onLibraryButtonClicked();

    // STREAM_PORT 2026-05-18 Task 3: toggle between clamped and expanded
    // synopsis text. Mirrors StreamDetailView::onDescShowMoreClicked.
    void onDescShowMoreClicked();

    // STREAM_PORT 2026-05-18 Task 5: checkbox + bulk-download slots.
    void onVolumeCheckboxToggled(int row, bool checked);
    void onDownloadSelectedClicked();

private:
    void buildUi();
    void renderDetail(const anilist::MediaDetail& detail);
    void renderEmpty(const QString& reason = {});
    void populateVolumeRows(const QList<anilist::VolumeRow>& rows,
                            const anilist::MediaDetail* detail);
    void refreshLibraryButton();

    // F1 (2026-05-18): shared by onVolumeCellClicked (mouse) +
    // onVolumeCurrentChanged (mouse + keyboard); pushes the row's mapped
    // sources into m_sourcesPanel.
    void populateSourcesForRow(int row);

    // PHASE 12: async-load a cover URL into a target volume row's Cover cell.
    // Uses QPixmapCache keyed by URL; cache hits paint synchronously.
    // volumeNumber identifies the target row; -1 means the banner.
    void loadCoverUrlForVolume(const QString& url, int volumeNumber);
    void loadBannerUrl(const QString& url);
    // STREAM_PORT 2026-05-18 Task 1: paint a pixmap onto m_heroBannerLabel,
    // scaled to fit the 140px band via KeepAspectRatioByExpanding. Called
    // from loadBannerUrl on cache-hit OR async-fetch completion.
    void applyBannerPixmap(const QPixmap& pm);
    void applyPixmapToVolumeRow(int volumeNumber, const QPixmap& pm);

    anilist::AniListClient*  m_client  = nullptr;  // non-owning
    anilist::AniListCache*   m_cache   = nullptr;  // non-owning
    premium::PremiumCatalog* m_catalog = nullptr;  // non-owning; PHASE 8
    NyaaRuntimeSource*       m_nyaa    = nullptr;  // non-owning; PHASE 8
    MangaDownloadIndex*      m_downloadIndex = nullptr; // non-owning; Phase B.3

    // STREAM_PORT 2026-05-18 Task 1: hero banner is now a docked QLabel at
    // 140px height instead of a full-viewport paintEvent wallpaper. Banner
    // image is loaded asynchronously via loadBannerUrl() and rendered as a
    // scaled pixmap on the label. Matches StreamDetailView::m_heroLabel
    // (StreamDetailView.cpp:397-406) pattern.
    QLabel*               m_heroBannerLabel = nullptr;
    QPushButton*          m_backButton      = nullptr;
    QLabel*               m_title         = nullptr;
    QLabel*               m_metaLine      = nullptr;
    QLabel*               m_synopsis      = nullptr;
    QPushButton*          m_libraryButton = nullptr;
    QTableWidget*         m_volumesTable  = nullptr;
    ComicsSourcesPanel*   m_sourcesPanel  = nullptr;  // PHASE 8: replaces the placeholder QLabel

    // STREAM_PORT 2026-05-18 Task 3: description clamp + show-more toggle.
    // Mirrors StreamDetailView.cpp:460-472 pattern.
    QPushButton*  m_descShowMoreBtn = nullptr;
    bool          m_descExpanded    = false;
    int           m_descClampLines  = 3;

    // STREAM_PORT 2026-05-18 Task 5: per-row multi-select state.
    // m_selectedRows holds the rows currently checked; m_downloadSelectedBtn
    // shows "Download Selected (N)" when N >= 1, hidden otherwise. Click
    // emits bulkDownloadRequested once with the full selection list. Mirrors
    // StreamDetailView::m_downloadSelectedBtn pattern at line 599.
    QSet<int>      m_selectedRows;
    QPushButton*   m_downloadSelectedBtn = nullptr;

    // Cached during renderDetail so onVolumeCellClicked can pass the
    // VolumeRow to the sources panel without re-running the mapper.
    QList<anilist::VolumeRow> m_currentVolumeRows;
    QString                   m_currentSeriesTitle;

    // STREAM_PORT 2026-05-18 Task 6: index of the first volume the user
    // hasn't started reading (proxy: first row whose stashed cbz path is
    // empty). -1 if all rows downloaded OR no rows. Set by
    // populateVolumeRows after the per-row loop completes.
    int            m_nextUnreadRow = -1;

    int m_pendingSeriesReqId = -1;
    int m_currentAnilistId   = 0;
    int m_nextRequestId      = 1;
    bool m_libraryButtonSawPress = false;
};

} // namespace tankoban::manga::comics
