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
#include "core/manga/MangaCatalogTypes.h"
#include "core/manga/MangaResult.h"
#include "core/manga/VolumeQualityClassifier.h"  // ClassifiedVolume, VolumeQuality

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
class QScrollArea;
class QTimer;
class QHBoxLayout;
class QVBoxLayout;
class MangaDownloadIndex;

namespace tankoban::ui::widgets { class ComicsSeriesViewLoadingOverlay; }
namespace tankoban::ui::comics { class VolumeTile; }

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

    // Search-result-open staging state: clears the previous series and shows
    // only the opaque loading overlay until ComicsPage has enrichment results.
    void showSearchResultLoading();

    // Fix 3: catalog-tile entry point. Wraps showSeries(MediaPreview) but
    // overrides the resolver/cache key to "fandom_catalog:<seriesId>" when
    // anilistId <= 0, preventing identity collapse to "anilist:0" for every
    // zero-AniList catalog series.
    void showCatalogSeries(const QString& seriesId,
                           const QString& title,
                           int            anilistId);

    // Overload for WeebCentral-sourced series (WEEBCENTRAL_IDENTITY_PIVOT Task 8).
    // seriesKey is derived from wc.source + ":" + wc.id; drives the cover
    // resolver and the detail fetch via MangaSourceRegistry.
    void showSeries(const MangaResult& wc);
    void showSeries(const MangaResult& wc, bool requestEnrichment);

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

    // BookWalker cover resolver removed (COMICS_MANGAFIRE_PIVOT Phase B.2).
    // MangaFire catalog volumes carry direct cover URLs loaded via
    // loadCoverUrlForVolume inside populateVolumeRowsFromCatalog.
    int currentAnilistId() const { return m_currentAnilistId; }

    QJsonObject devSnapshot() const;
    QJsonObject devSelectVolume(int row);
    QJsonObject devSourcesSnapshot() const;
    QJsonObject devDispatchVolume(int volumeNumber, const QString& source);

public slots:
    void setVolumeDownloadState(int volumeNumber, const QString& cbzPath,
                                bool downloaded);
    void setVolumeStatusText(int volumeNumber, const QString& statusText);
    void populateSourcesForVolume(int volumeNumber);
    void onWeebCentralViable(int volumeNumber, const QStringList& chapterIds);

    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). Promoted from private
    // so ComicsPage can re-sync the button state after a deferred AniList
    // search-by-title enrichment lands (success or failure both reset the
    // button via this method).
    void refreshLibraryButton();

    // COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23). Render volume rows from
    // a MangaCatalog loaded from data/mangafire_catalog/. Renamed from
    // populateVolumeRowsFromFandom; parameter type updated to MangaCatalog.
    void populateVolumeRowsFromCatalog(
        const tankoban::manga::MangaCatalog& catalog);

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // Receives classification verdicts from MangaWeebCentralResolver. Stores
    // them in m_classifiedByVolume and re-renders volume rows so RAW SCAN
    // tags + Volume X row are applied.
    void onSeriesClassified(const QString& mangaFireSeriesId,
                            QList<tankoban::manga::ClassifiedVolume> classified);

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // Returns true when the given volume is magazine-sourced (Magazine or
    // Volume X quality). Used by ComicsPage to set needsChapterPairing on
    // the VolumePackRequest before dispatch.
    bool isVolumeMagazineSourced(int volumeNumber) const;

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

    // COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23). Emitted when
    // the force-refresh button is clicked. ComicsPage re-scans the local
    // catalog index and re-resolves for the current series.
    // No payload: ComicsPage tracks the current series identity.
    void forceRefreshRequested();

    // COMICS_MANGAFIRE_ON_DEMAND_FETCH follow-up (2026-05-23). AniList-id
    // opens can start with a placeholder title such as "anilist_87395";
    // notify ComicsPage when the real detail title arrives so MangaFire
    // catalog resolution can retry with a usable title.
    void detailResolvedForCatalog(int anilistId, const QString& title);

    // COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1). Emitted after a volume
    // source panel is populated so ComicsPage can lazily resolve the
    // WeebCentral source for that MangaFire volume.
    void weebCentralResolveRequested(const QString& mangaFireSeriesId,
                                     int volumeNumber);

    // VOLUME_X_DOWNLOAD: like weebCentralResolveRequested but carries an explicit
    // chapter range. Emitted for the synthetic Volume X tile, whose range comes
    // from the classified tail bucket (not catalog.volumes).
    void weebCentralResolveRangeRequested(const QString& mangaFireSeriesId,
                                          int volumeNumber,
                                          int rangeStart,
                                          int rangeEnd);

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

    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). Emitted when the user
    // clicks Add to Library on a series that has no AniList ID (MangaFire-
    // catalog-only). ComicsPage runs a best-effort AniList searchByTitle,
    // and on match bookmarks via the existing AniList-keyed path and
    // re-shows the series with the enriched MediaPreview so the local view
    // sees its new identity (m_currentAnilistId, banner, etc.).
    void addToLibraryByTitleRequested(const QString& title);

    // COMICS_WC_AUTOENRICH 2026-05-24 (Agent 1). Sibling to the above
    // signal: emitted on showSeries(MangaResult) when anilistId is 0 so
    // ComicsPage runs the AniList search-by-title + cache seed + re-show
    // cycle WITHOUT adding a bookmark. This makes search-opened series
    // render the same hero block (banner, poster, synopsis, tags) the
    // bookmarked path does, without committing to the library.
    void enrichSeriesByTitleRequested(const QString& title);

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
    void onVolumeRowActivated(int volumeNumber);
    void onLibraryButtonClicked();

    // STREAM_PORT 2026-05-18 Task 3: toggle between clamped and expanded
    // synopsis text. Mirrors StreamDetailView::onDescShowMoreClicked.
    void onDescShowMoreClicked();

    // STREAM_PORT 2026-05-18 Task 5: checkbox + bulk-download slots.
    void onVolumeCheckboxToggled(int row, bool checked);
    void onDownloadSelectedClicked();

    // BookWalker cover resolver slots removed (COMICS_MANGAFIRE_PIVOT Phase B.2).
    // MangaFire volumes carry direct cover URLs loaded via loadCoverUrlForVolume.

private:
    void buildUi();
    void renderDetail(const anilist::MediaDetail& detail);
    void renderEmpty(const QString& reason = {});
    void populateVolumeRows(const QList<anilist::VolumeRow>& rows,
                            const anilist::MediaDetail* detail);
    // refreshLibraryButton moved to public slots above (COMICS_WC_LIBRARY_ENRICH).

    // F1 (2026-05-18): shared by onVolumeCellClicked (mouse) +
    // onVolumeCurrentChanged (mouse + keyboard); pushes the row's mapped
    // sources into m_sourcesPanel.
    void populateSourcesForRow(int row);

    // PHASE 12: async-load a cover URL into a target volume row's Cover cell.
    // Uses QPixmapCache keyed by URL; cache hits paint synchronously.
    // volumeNumber identifies the target row; -1 means the banner.
    void loadCoverUrlForVolume(const QString& url, int volumeNumber);
    void loadBannerUrl(const QString& url);
    void loadHeroCoverUrl(const QString& url);
    // STREAM_PORT 2026-05-18 Task 1: paint a pixmap onto m_heroBannerLabel,
    // scaled to fit the 140px band via KeepAspectRatioByExpanding. Called
    // from loadBannerUrl on cache-hit OR async-fetch completion.
    void applyBannerPixmap(const QPixmap& pm);
    void applyHeroCoverPixmap(const QPixmap& pm);
    void applyPixmapToVolumeRow(int volumeNumber, const QPixmap& pm);
    void populateHeroTags(const QStringList& genres);
    void populateHeroTags(const QList<tankoban::manga::anilist::RankedTag>& tags);

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

    // Loading overlay helpers (retained for async cover-load spinner).
    void showLoadingOverlay();
    void hideLoadingOverlay();

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
    QWidget*              m_heroBlock       = nullptr;
    QLabel*               m_heroCoverLabel  = nullptr;
    QPushButton*          m_backButton      = nullptr;
    QLabel*               m_title         = nullptr;
    QLabel*               m_mangakaByline = nullptr;
    QLabel*               m_metaLine      = nullptr;
    QLabel*               m_synopsis      = nullptr;
    QWidget*              m_tagChipsRow   = nullptr;
    QHBoxLayout*          m_tagChipsLayout = nullptr;
    QPushButton*          m_libraryButton = nullptr;
    // Fandom catalog redesign Task 19 (Phase 7, 2026-05-20). Force-refresh
    // affordance — invalidates the 7d FandomCatalogCache entry for the
    // current series + re-resolves via FallbackChainResolver. Useful when a
    // wiki has updated since the last cache fetch.
    QPushButton*          m_forceRefreshButton = nullptr;
    // REPLACED: QTableWidget surface is now a QScrollArea of VolumeTile rows.
    // m_volumeTilesByVolumeNumber lets setVolumeDownloadState /
    // setVolumeStatusText / setVolumeCoverFromDisk address rows by volumeNumber
    // without iterating the list.
    QScrollArea*  m_volumesScroll = nullptr;
    QWidget*      m_volumesHost   = nullptr;
    QVBoxLayout*  m_volumesLayout = nullptr;
    QHash<int, tankoban::ui::comics::VolumeTile*> m_volumeTilesByVolumeNumber;
    QList<tankoban::ui::comics::VolumeTile*>      m_volumeTiles;
    ComicsSourcesPanel*   m_sourcesPanel  = nullptr;  // PHASE 8: replaces the placeholder QLabel

    // STREAM_PORT 2026-05-18 Task 3: description clamp + show-more toggle.
    // Mirrors StreamDetailView.cpp:460-472 pattern.
    QPushButton*  m_descShowMoreBtn = nullptr;
    bool          m_descExpanded    = false;
    int           m_descClampLines  = 2;

    // STREAM_PORT 2026-05-18 Task 5: per-row multi-select state.
    // m_selectedRows holds the rows currently checked; m_downloadSelectedBtn
    // shows "Download Selected (N)" when N >= 1, hidden otherwise. Click
    // emits bulkDownloadRequested once with the full selection list. Mirrors
    // StreamDetailView::m_downloadSelectedBtn pattern at line 599.
    QSet<int>      m_selectedRows;
    int            m_lastBulkAnchorVolume = -1;   // -1 = no anchor yet
    QPushButton*   m_downloadSelectedBtn = nullptr;

    // Cached during renderDetail so onVolumeCellClicked can pass the
    // VolumeRow to the sources panel without re-running the mapper.
    QList<anilist::VolumeRow> m_currentVolumeRows;
    QString                   m_currentSeriesTitle;
    tankoban::manga::MangaCatalog m_currentMangaCatalog;

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // Per-volume classification verdicts from the resolver. Populated by
    // onSeriesClassified; consumed by populateVolumeRowsFromCatalog (RAW tags)
    // and the download dispatch path (needsChapterPairing flag).
    QHash<int, tankoban::manga::ClassifiedVolume> m_classifiedByVolume;

    // STREAM_PORT 2026-05-18 Task 6: index of the first volume the user
    // hasn't started reading (proxy: first row whose stashed cbz path is
    // empty). -1 if all rows downloaded OR no rows. Set by
    // populateVolumeRows after the per-row loop completes.
    int            m_nextUnreadRow = -1;

    int m_pendingSeriesReqId = -1;
    int m_currentAnilistId   = 0;
    int m_nextRequestId      = 1;
    bool m_libraryButtonSawPress = false;

    // Loading overlay (retained for async cover-image spinner).
    // BookWalker VolumeCoverResolver removed (COMICS_MANGAFIRE_PIVOT Phase B.2);
    // m_loadingSafetyTimer also removed — the safety-timeout path was only
    // needed for the BookWalker HTTP path. loadCoverUrlForVolume uses
    // m_pendingMediaLoads to hide the overlay when all covers have loaded.
    tankoban::ui::widgets::ComicsSeriesViewLoadingOverlay* m_loadingOverlay  = nullptr;

    // Task 8 (WEEBCENTRAL_IDENTITY_PIVOT): seriesKey-based identity for the
    // current series. For AniList-only series the key is "anilist:<anilistId>".
    // For WeebCentral series the key is "<source>:<seriesId>".
    // For MangaFire catalog series the key is "mangafire:<seriesId>".
    QString m_currentSeriesKey;           // identity of the currently displayed series

    // Fix 3: one-shot override supplied by showCatalogSeries() so that
    // zero-AniList catalog opens don't collapse to "anilist:0". Consumed and
    // cleared by showSeries(MediaPreview) on the same call. Empty = no override.
    QString m_pendingCatalogSeriesKey;

    MangaSourceRegistry* m_sourceRegistry = nullptr;  // non-owning; set by ComicsPage
};

} // namespace tankoban::manga::comics
