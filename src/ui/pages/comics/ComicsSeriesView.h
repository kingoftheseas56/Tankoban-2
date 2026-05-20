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
#include "core/manga/MangaResult.h"

class MangaSourceRegistry;

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class MangaDownloadIndex;

namespace tankoban::manga::bookwalker { class VolumeCoverResolver; }
namespace tankoban::ui::widgets { class ComicsSeriesViewLoadingOverlay; }

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

    // Overload for WeebCentral-sourced series (WEEBCENTRAL_IDENTITY_PIVOT Task 8).
    // seriesKey is derived from wc.source + ":" + wc.id; drives the cover
    // resolver and the detail fetch via MangaSourceRegistry.
    void showSeries(const MangaResult& wc);

    // Setter for MangaSourceRegistry; called by ComicsPage after construction.
    // Non-owning; registry lifetime is owned by ComicsPage.
    void setSourceRegistry(MangaSourceRegistry* registry) { m_sourceRegistry = registry; }

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

    // Task 14: inject the BookWalker per-volume cover resolver. Non-owning.
    // May be called before or after showSeries(); re-wires signals on each call.
    void setVolumeCoverResolver(tankoban::manga::bookwalker::VolumeCoverResolver* resolver);
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

    // WEEBCENTRAL_IDENTITY_PIVOT post-pivot Pass 3-followup fix 2026-05-19:
    // keep m_loadingOverlay sized to the view on parent resize. Without
    // this override, the overlay's geometry only got set once in
    // showLoadingOverlay() — if the parent later resized (window resize,
    // QStackedWidget switch settling), the overlay stayed at its prior
    // (often tiny / corner) size. Hemanth surfaced this 2026-05-19:
    // "loading covers toast went to the top left corner."
    void resizeEvent(QResizeEvent* ev) override;

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

    // Task 14 / Task 8 (WEEBCENTRAL_IDENTITY_PIVOT): BookWalker cover resolver
    // signal handlers. Signatures use seriesKey (QString) — re-keyed from
    // anilistId (int) in Tasks 6+7; stale-request guards fully implemented in
    // Task 8 via m_currentResolvingSeriesKey.
    void onCoverResolverResolved(const QString& seriesKey, const QMap<int, QString>& volumeToCoverUrl);
    void onCoverResolverUnresolved(const QString& seriesKey, const QString& reason);
    void onCoverResolverSkipped(const QString& seriesKey, const QString& reason);
    void onCoverResolverSafetyTimeout();

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

    // Task 15: last-applied cover URL per volume -- populated in
    // loadCoverUrlForVolume, cleared in clearView, exposed via devSnapshot.
    QMap<int, QString> m_lastAppliedCoverUrlByVolume;

    // WEEBCENTRAL_IDENTITY_PIVOT post-pivot Pass 1 fix 2026-05-19: tracks
    // the banner URL currently painted on m_heroBannerLabel. Used by
    // loadBannerUrl to detect series-switch (URL changes) vs same-series
    // re-open (URL stays). On series-switch we wipe the label pixmap before
    // the async load so the prior series' banner doesn't leak into the new
    // view (Hemanth: "Death Note opens with Berserk's banner"). On
    // same-series re-open we skip the wipe so the QPixmapCache hit can
    // replace atomically with no flicker -- preserves the 2026-05-18
    // hero-instant-load contract documented at clearView():732-740.
    // NOT cleared in clearView() because the pixmap survives clearView()
    // by design; the URL tracker must mirror that lifetime.
    QString m_lastBannerUrl;

    // WEEBCENTRAL_IDENTITY_PIVOT post-pivot Pass 3 fix 2026-05-19: counter
    // tracking in-flight async media fetches (banner + per-volume covers).
    // The loading overlay hides only when this counter reaches 0 AFTER
    // resolver completion — gives the user a visible spinner during the
    // cover-image download window. Without this, library-warm path opens
    // (BookWalkerCache hit → resolver emits synchronously → hideOverlay
    // fires before any event-loop tick) had no perceptible spinner even
    // though the 12 volume cover IMAGES were still loading from the
    // network. Reset to 0 in both showSeries overloads; incremented when
    // loadBannerUrl / loadCoverUrlForVolume kick off an async fetch;
    // decremented unconditionally in each finished lambda; hideLoading-
    // Overlay called when counter hits 0 after a decrement.
    int m_pendingMediaLoads = 0;

    // Task 14: BookWalker cover overlay helpers.
    void showLoadingOverlay();
    void hideLoadingOverlay();
    void paintVolumeCovers(const QMap<int, QString>& volumeToCoverUrl);
    void paintVolumeCoversAsFallback();

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

    // Task 14: BookWalker per-volume cover resolver + loading overlay.
    QPointer<tankoban::manga::bookwalker::VolumeCoverResolver> m_coverResolver;
    tankoban::ui::widgets::ComicsSeriesViewLoadingOverlay* m_loadingOverlay  = nullptr;
    QTimer*                                                m_loadingSafetyTimer = nullptr;

    // Task 8 (WEEBCENTRAL_IDENTITY_PIVOT): seriesKey-based identity for the
    // current series and the in-flight resolver request. Replace the old
    // m_currentResolvingAnilistId (int) stale-guard with QString comparison.
    // For AniList-only series the key is synthesized as "anilist:<anilistId>".
    // For WeebCentral series the key is "<source>:<seriesId>" (e.g.
    // "weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4").
    QString m_currentSeriesKey;           // identity of the currently displayed series
    QString m_currentResolvingSeriesKey;  // key stamped when resolver was fired; cleared on clearView

    MangaSourceRegistry* m_sourceRegistry = nullptr;  // non-owning; set by ComicsPage
};

} // namespace tankoban::manga::comics
