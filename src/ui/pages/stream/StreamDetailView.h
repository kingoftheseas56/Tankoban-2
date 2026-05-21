#pragma once

#include <QWidget>
#include <QComboBox>
#include <QDateTime>
#include <QHash>
#include <QLabel>
#include <QJsonObject>
#include <QPair>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QMap>
#include <QList>

#include <optional>

class QNetworkAccessManager;
class QStackedLayout;
class QTimer;

#include "core/stream/MetaAggregator.h"
#include "core/stream/addon/MetaItem.h"
#include "StreamSourceChoice.h"

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — full include (not fwd-decl)
// required because renderEpisodeStateChip uses StreamDownloadIndex::Entry::State
// in its signature and EpisodeTileState::Provenance from the theatre tile.
#include "core/stream/StreamDownloadIndex.h"
#include "ui/pages/stream/EpisodeTile.h"

class CoreBridge;
class StreamLibrary;
struct StreamLibraryEntry;
class TorrentClient;
class TankorentSearchService;
struct TorrentResult;

namespace tankostream::stream {
class MetaAggregator;
class StreamSourceList;
}

class StreamDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit StreamDetailView(CoreBridge* bridge,
                              tankostream::stream::MetaAggregator* meta,
                              StreamLibrary* library,
                              QWidget* parent = nullptr);

    // Default invocation shows the entry without pre-selecting any episode.
    // Batch 6.2: Calendar → Detail nav passes (preselectSeason, preselectEpisode)
    // to auto-switch the season combo + focus the matching episode row once
    // series meta resolves.
    //
    // Phase 1 Batch 1.1: `previewHint` carries the MetaItemPreview from a
    // catalog/home/search tile so the detail view can paint title/year/poster/
    // description immediately without the title appearing in the library. When
    // unset (the library-tile path), the StreamLibrary::get(imdbId) lookup
    // remains the source — existing callers keep working unchanged.
    void showEntry(const QString& imdbId,
                   int            preselectSeason  = -1,
                   int            preselectEpisode = -1,
                   const std::optional<tankostream::addon::MetaItemPreview>& previewHint = std::nullopt);

    // Stream-picker UX rework — right-pane source list state transitions.
    // StreamPage drives these: `Loading` the instant the user clicks an
    // episode (or movie opens); `Sources` when StreamAggregator::streamsReady
    // fires; `Error` / `Placeholder` for edge states.
    void setStreamSourcesLoading();
    void setStreamSources(const QList<tankostream::stream::StreamPickerChoice>& choices,
                          const QString&                                        savedChoiceKey = {});
    void setStreamSourcesError(const QString& message);
    void setStreamSourcesPlaceholder(const QString& message);

    // Phase 2 Batch 2.4 — passthroughs to the embedded StreamSourceList's
    // auto-launch toast. StreamPage arms the toast when a resume-match fires
    // and hides it on cancel / navigation-away.
    void showAutoLaunchToast(const QString& label);
    void hideAutoLaunchToast();

    // Read-only accessor used by StreamPage::showDetail to dedupe back-to-back
    // open requests for the same imdbId (e.g. the single-click + double-click
    // combination that fires both TileStrip::tileSingleClicked and
    // tileDoubleClicked for one user double-click gesture).
    const QString& currentImdb() const { return m_currentImdb; }
    QString currentType() const { return m_currentType; }
    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - exposes active
    // season from m_seasonCombo for StreamPage's onDirectDownloadRequested
    // slot. Returns 0 for movies / unpopulated combo (consistent with
    // existing season=0 convention for non-series content).
    int currentSeason() const {
        return m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    }
    QString currentTitle() const;
    QString currentYear() const;
    QList<tankostream::stream::StreamEpisode> episodesForSeason(int season) const;
    QJsonObject devSnapshot() const;

    // STREAM_CONTINUE_LIBRARY_AND_HUD_AUTOFIRE 2026-05-06 — auto-add the
    // currently-shown show/movie to StreamLibrary (no-op if already present).
    // Called from StreamPage's progressUpdated lambda on the FIRST successful
    // save in a session, gated by m_session.autoLibraryAdded. Idempotent —
    // exists-check guards against duplicate adds; preserves any user-added
    // entry's existing fields (StreamLibrary::add itself dedups by imdb).
    // Builds the StreamLibraryEntry from m_lastPreviewHint, the same source
    // onLibraryButtonClicked uses on the explicit-button path. No-op if no
    // preview hint is cached (defensive — showEntry stashes one on every
    // open).
    void autoAddToLibrary();

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — wires the download
    // index in so the episode list can paint per-row "on disk" markers
    // and onEpisodeActivated can branch to local-file playback. Optional;
    // when null the detail view behaves exactly as before (source-pick
    // for every episode click).
    void setStreamDownloadIndex(StreamDownloadIndex* idx);

    // STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) — wires the torrent
    // client so the Remove-from-Library path can detect active bulk
    // groups for this show and require explicit user confirmation before
    // canceling-and-removing. Optional; when null the dialog short-
    // circuits and Remove proceeds as before. Spec §10.10.
    // Task 12: also subscribes to streamBulkGroupsChanged so action icons
    // repaint immediately on any cohort state change (not only on the 1Hz
    // poll tick).
    void setTorrentClient(TorrentClient* client);

    // TANKORENT_CINEMATA P1.T8 (2026-05-21) — wires the Tankorent headless
    // search service so the new [Find sources for Season N] button can
    // fire identity-baked searches + receive topResultPicked auto-pick
    // emits. MainWindow (T9) injects the singleton service after creating
    // the detail view. Non-owning pointer; lifetime is MainWindow's.
    void setSearchService(TankorentSearchService* svc);

    // THEATRE_DOWNLOAD_OVERHAUL Phase E: the detail view owns the right pane.
    // StreamPage mounts TheatreDownloadPanel into this host beside Sources.
    QWidget* rightPaneStack() const { return m_rightPaneStack; }
    QWidget* sourcesPanel() const { return m_sourcesPanel; }

signals:
    void backRequested();

    // Emitted when the user clicks an episode row (single-click) or a movie
    // detail opens. StreamPage listens and runs the StreamAggregator fan-out
    // to backfill the right pane via setStreamSources.
    void playRequested(const QString& imdbId, const QString& mediaType,
                       int season, int episode);

    // Emitted when the user clicks a source card in the right pane.
    // StreamPage saves the choice and hands off to StreamPlayerController.
    void sourceActivated(const tankostream::stream::StreamPickerChoice& choice);

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — re-emitted from the embedded
    // StreamSourceList's addToTankorentRequested signal. StreamPage owns
    // the next upstream hop into MainWindow.
    void addToTankorentRequested(const tankostream::stream::StreamPickerChoice& choice);

    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - re-emitted from
    // StreamSourceList's directDownloadRequested. StreamPage handles it via
    // onDirectDownloadRequested which dispatches the right-clicked stream
    // directly to TorrentClient (Theatre library route).
    void directDownloadRequested(const tankostream::stream::StreamPickerChoice& choice);

    // THEATRE_DOWNLOAD_OVERHAUL stale-panel-on-show-change fix 2026-05-17 -
    // emitted from showEntry() when the user navigates from one show to a
    // DIFFERENT show (m_currentImdb changing). StreamPage handles this by
    // dismissing the TheatreDownloadPanel if it's open + sliding the Sources
    // sidebar back in. Without this, the panel persisted with stale prior-
    // show packs when the user navigated between shows.
    void entryContextChanging();
    void bulkDownloadRequested(int season);

    // Phase 2 Batch 2.4 — forwarded from StreamSourceList's Pick-different
    // button; StreamPage listens to abort the auto-launch timer.
    void autoLaunchCancelRequested();

    // Phase 3 Batch 3.5 (deferred ship) — direct-URL trailer playback.
    // StreamPage consumes this by synthesizing an ad-hoc play through
    // StreamPlayerController (same pattern as Batch 4.3 URL-paste). YouTube
    // trailers are handled directly in StreamDetailView via QDesktopServices
    // and do NOT emit this signal.
    void trailerDirectPlayRequested(const QUrl& url);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — single-episode dispatch ask.
    // Routed to StreamPage in Task 15.
    void singleEpisodeDownloadRequested(int season, int episode);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — lateral paths to StreamPage.
    // seasonDownloadRequested fires when the season-header button is clicked
    // in the "no active cohort" state (Idle → Download Season).
    // selectedEpisodesDownloadRequested fires when the user clicks
    // "Download Selected (N)" after checking individual episode rows.
    void seasonDownloadRequested(int season);
    void selectedEpisodesDownloadRequested(int season, const QList<int>& episodes);

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 - unified Download button. Host
    // (StreamPage) opens TheatreDownloadPanel with this context.
    void theatreDownloadRequested(const QString& imdbId,
                                  const QString& showName,
                                  int season,
                                  const QString& mediaType);

    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - movie-row primary
    // Download fast-path. Host (StreamPage) auto-picks the top-seeded torrent
    // from already-loaded movie streams and dispatches via TorrentClient
    // directly. Does NOT open TheatreDownloadPanel (movies use the existing
    // Sources right-side panel as the "alternate streams" picker instead).
    void theatreTopSeededDownloadRequested(const QString& imdbId,
                                            const QString& showName,
                                            const QString& infoHash,
                                            const QString& magnetUri);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — episode click
    // resolved to a local file. StreamPage forwards through to
    // MainWindow::onPlayLocalFileFromStreamRequested. Spec §6.2.
    void playLocalFileFromStreamRequested(const QString& localPath,
                                          const QString& imdbId,
                                          const QString& showTitle,
                                          int season,
                                          int episode);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — right-click on any
    // episode row → "Show alternate streams". StreamPage handles by
    // re-firing the existing source-pick flow. Spec §6.3.
    void alternateStreamRequested(int season, int episode);

protected:
    // F13 fix 2026-05-19: start/stop m_progressRefreshTimer scoped to
    // visibility so we only poll while the user can actually see the badges.
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUI();
    void onSeriesMetaReady(const QString& imdbId,
                           const QMap<int, QList<tankostream::stream::StreamEpisode>>& seasons);
    void onSeasonChanged(int comboIndex);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — season-header morphing button.
    void onDownloadSeasonClicked();
    void onDownloadSelectedClicked();
    void onSeasonHeaderRightClick(const QPoint& pos);
    // Repaints the morphing button label/icon based on cohort state for
    // the active season. Called whenever the cohort state changes.
    void refreshSeasonHeaderButton();
    void populateEpisodeTable(int season);
    void onEpisodeActivated(int row, int col);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — action-icon click dispatch.
    // Resolves current row state and routes to download / pause /
    // resume / retry / (Published) open actions menu. Spec §7.1 row
    // state map. Post-NETFLIX_OVERHAUL P3 revision (2026-05-12): the
    // optional globalAnchorPos is used by the Published branch to
    // anchor the actions menu; non-Published branches ignore it.
    void onActionIconClicked(int episode, const QPoint& globalAnchorPos = QPoint());

    // Post-NETFLIX_OVERHAUL P3 revision (2026-05-12) — builds + pops
    // the row's actions menu (Remove/Cancel + Show alternate streams).
    // Shared by onActionIconClicked's Published branch and
    // onEpisodeContextMenu so right-click and left-click converge on
    // one builder. Cancel label morphs to Remove for Published rows.
    void showRowActionsMenu(int season, int episode, const QPoint& globalAnchorPos);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — helpers used by onActionIconClicked.
    QString findInfoHashForEpisode(int season, int episode) const;
    QString findGroupIdForCohort(int season) const;
    // STREAM_DOWNLOADED_LIBRARY Phase 4 — right-click → "Show alternate
    // streams" context menu on the episode table.
    void onEpisodeContextMenu(const QPoint& pos);
    // STREAM_DOWNLOADED_LIBRARY Phase 4 — repaint per-row on-disk markers.
    // Called both at the tail of populateEpisodeTable() and in response to
    // StreamDownloadIndex::entriesChanged so a bulk-completion lights up
    // the rows in place.
    void refreshEpisodeMarkers();

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — shared chip render for the
    // season-row table's kColStatus item. Mirrors EpisodeTile's state-driven
    // chip contract so both surfaces converge on one look. Provenance-driven
    // amber-tint lands in Task 20 once tone is ratified.
    void renderEpisodeStateChip(
        int row,
        StreamDownloadIndex::Entry::State state,
        int progressPct,
        tankoban::stream::theatre::EpisodeTileState::Provenance provenance);

    // PHASE3_CHIP_VISIBILITY_FIX 2026-05-19 — initial-paint helper. Walks the
    // episode table, looks up each row's episode in the substrate, and calls
    // renderEpisodeStateChip for tracked rows. Called from populateEpisodeTable,
    // setStreamDownloadIndex, and onSeasonChanged so substrate state is visible
    // on view-open, signal-wire, and season-switch (entryStateChanged covers
    // subsequent in-flight transitions).
    void refreshSubstrateStatesForActiveSeason();
    void refreshMovieLocalChip();
    void refreshMovieDownloadState();
    void updateProgressColumn();
    void updateBulkDownloadButton();

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — updates m_downloadSelectedBtn
    // visibility + label based on m_selectedEpisodes.size(). Called
    // whenever a checkbox toggles or the season changes.
    void updateDownloadSelectedButton();

    // Stream async-race follow-up 2026-05-18: mark the exact user action
    // that is about to create a bulk cohort. The earlier stamp lived on a
    // panel-open button, which could expire before the real dispatch.
    void startBulkProgressGraceWindow();

    // STREAM_BULK_DOWNLOAD_V2 Phase 3 — refresh the per-row download-state
    // text in the episode table's Status column from the TorrentClient
    // bulk-snapshot for current imdb+season. 1Hz polled via m_bulkPollTimer
    // while the detail view is visible. Falls through to the watched-
    // checkmark when no bulk activity for the row.
    void refreshEpisodeBulkProgress();

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 12 — paint helper. Combines
    // Layer 3 index hit + Layer 2 cohort state into the right action-icon
    // glyph for a single row. Called from refreshEpisodeBulkProgress on
    // every tick and from populateEpisodeTable for the initial paint pass.
    void repaintActionIconForRow(int row, int episode, int season,
                                  const QHash<int, QPair<QString, int>>& cohortSnap);

    // Phase 3 Batch 3.1 — MetaItem arrival handler; paints hero image +
    // enriches the metadata chip row (runtime, genres) once fetchMetaItem
    // resolves. Best-effort: missing fields are hidden; initial paint from
    // showEntry's preview hint stays authoritative for the title/year/desc
    // surface. Filtered by imdbId match to ignore stale meta callbacks that
    // arrive after the user has navigated to a different title.
    void onMetaItemReady(const tankostream::addon::MetaItem& item);
    void applyHeroImage(const QString& imdbId, const QUrl& backgroundUrl,
                        const QUrl& posterFallbackUrl);
    void downloadBackgroundArt(const QString& imdbId, const QUrl& url,
                               bool usePosterFallback);
    QString heroCachePath(const QString& imdbId) const;
    void renderHeroPixmap(const QString& imagePath);
    void clearHero();
    void applyChips(const QString& year, const QString& runtime,
                    const QStringList& genres, const QString& rating,
                    const QString& type);

    // Phase 3 Batch 3.4 — episode thumbnail fetch + cache helpers. Reuses
    // the hero-pipeline NAM; writes to m_episodeThumbsCacheDir. Returns the
    // cached-disk-path on hit so the caller can paint synchronously.
    QString episodeThumbPath(const QString& imdbId, int season, int episode) const;
    void fetchEpisodeThumbnail(const QString& imdbId, int season, int episode,
                               const QUrl& url, QLabel* target);
    void applyEpisodeThumbnail(QLabel* target, const QString& imagePath);

    // Phase 3 Batch 3.2 (deferred ship) — director + cast row population.
    // Walks MetaItem.preview.links filtering on category=="Director"/"Cast";
    // renders a single "Director: X · Cast: A, B, C..." row with elided
    // overflow on the cast list.
    void applyCastDirector(const QList<tankostream::addon::MetaLink>& links);

    // Phase 3 Batch 3.5 (deferred ship) — trailer button state. Walks
    // MetaItem.preview.trailerStreams, prefers Url/Http kinds (direct play),
    // falls back to first YouTube-kind (browser open). Hides button when
    // neither is available.
    void applyTrailerButton(
        const QList<tankostream::addon::Stream>& trailerStreams);
    void onTrailerClicked();

    // TANKORENT_CINEMATA P1.T8 (2026-05-21) — [Find sources for Season N]
    // click handler + auto-pick + completion callbacks. onFindSourcesClicked
    // fires the identity-baked search through m_searchService; onSearchTopPicked
    // receives the ranker's pickTop result and dispatches via
    // m_torrentClient->addMagnetHeadless; onSearchFinished restores button text
    // + re-enables on terminal handle settlement.
    void onFindSourcesClicked();
    void onSearchTopPicked(const QString& handle, const TorrentResult& result);
    void onSearchFinished(const QString& handle);

    // Phase 3 Batch 3.3 — description clamp + show-more toggle.
    // `setDescription` rewrites text, resets expanded state, runs
    // `updateDescriptionClamp` to decide whether the toggle button should
    // surface. `updateDescriptionClamp` computes whether the text overflows
    // the 3-line height at the label's current width via QFontMetrics.
    void setDescription(const QString& text);
    void updateDescriptionClamp();
    void onDescShowMoreClicked();

    CoreBridge*                        m_bridge;
    tankostream::stream::MetaAggregator* m_meta;
    StreamLibrary*                     m_library;

    // Current state
    QString m_currentImdb;
    QString m_currentType;
    QMap<int, QList<tankostream::stream::StreamEpisode>> m_seasons;

    // UI — left column
    QPushButton*  m_backBtn       = nullptr;
    QLabel*       m_titleLabel    = nullptr;
    // 2026-04-15 — removed m_infoLabel. Chips row (year + type + rating)
    // conveys the same data; the info line was redundant and eats
    // vertical space from the episode table below.
    QLabel*       m_descLabel     = nullptr;
    // Phase 3 Batch 3.3 — "Show more / Show less" toggle under the
    // description. Hidden when the description fits within the 3-line clamp;
    // shown when clamping hides content. Click toggles m_descExpanded.
    QPushButton*  m_descShowMoreBtn = nullptr;
    bool          m_descExpanded    = false;

    // Phase 3 Batch 3.2 (deferred → shipped later) — director + cast row
    // rendered as a single word-wrapped QLabel below the description.
    // Hidden when both fields are empty.
    QLabel*       m_castDirectorLabel = nullptr;

    // Phase 3 Batch 3.1 — hero background art (full-width, 240px tall)
    // sits above the left/right columns. Fallback chain:
    //   1. MetaItem.preview.background (from fetchMetaItem)
    //   2. Poster (preview hint or library entry) — scaled + darkened
    //   3. Hidden if neither is available
    QLabel*       m_heroLabel     = nullptr;
    // STREAM_DETAIL_METADATA_POLISH 2026-05-06 — single inline metadata
    // line (year · runtime · genres · type · IMDb rating) replacing the
    // earlier 5-chip row. Stremio-parity tight muted-gray text below the
    // title; no boxes, no borders. Hidden via empty-text branch when no
    // fields populate (e.g. before preview hint lands). Composed by
    // applyChips() — name + signature retained for caller compat with
    // showEntry's first-paint and onMetaItemReady's richer-meta paint.
    QLabel*       m_metaLine      = nullptr;
    // Phase 1 Batch 1.2 — Add/Remove Library toggle in the header area.
    // Text + styling refresh on every showEntry + on libraryChanged. Phase 3
    // Batch 3.1 will restyle when the hero image lands.
    QPushButton*  m_libraryBtn    = nullptr;
    // Phase 3 Batch 3.5 (deferred ship) — Watch Trailer button. Visible
    // only when the current MetaItem has a Url/Http-kind trailer OR a
    // YouTube-kind trailer. Direct-URL trailers play in-app via an emitted
    // signal; YouTube opens in the default browser.
    QPushButton*  m_trailerBtn    = nullptr;
    QWidget*      m_movieActionRow = nullptr;
    QPushButton*  m_movieDownloadBtn = nullptr;
    QLabel*       m_movieLocalChip = nullptr;
    QLabel*       m_movieDownloadChip = nullptr;
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — inline trigger UX.
    // Per-(show, season) selection state. Reset whenever the season-combo
    // changes (showEntry / setSeason path). NOT persisted; per-launch only.
    QSet<int> m_selectedEpisodes;

    // Season-header primary fast-path Download button. THEATRE_DOWNLOAD_OVERHAUL
    // E1 UX refinement 2026-05-17 — click auto-dispatches the per-episode
    // highest-seeded torrent for the active season (restores the pre-E1
    // fast path that Codex's unified-button E1 had replaced with a panel
    // open). The 10% pack-based flow lives on the adjacent m_packOptionsBtn.
    QPushButton*  m_downloadBtn = nullptr;

    // Season-header secondary Layers-3 icon-only button adjacent to
    // m_downloadBtn. Opens the TheatreDownloadPanel via the existing
    // theatreDownloadRequested signal for pack-based advanced selection.
    QPushButton*  m_packOptionsBtn = nullptr;

    // Season-header secondary button: visible only when m_selectedEpisodes
    // is non-empty. Label "Download Selected (N)".
    QPushButton*  m_downloadSelectedBtn = nullptr;

    // THEATRE_DOWNLOAD_OVERHAUL E1 UX refinement 2026-05-17 — last source
    // choice list passed to setStreamSources(). Cached so the movie-row
    // Download button can auto-dispatch the top-seeded magnet without
    // round-tripping through the host. Cleared on showEntry().
    QList<tankostream::stream::StreamPickerChoice> m_lastChoices;

    QUrl          m_currentTrailerDirectUrl;   // populated from Url/Http trailer
    QString       m_currentTrailerYouTubeId;   // populated from YouTube trailer
    QWidget*      m_seasonRow     = nullptr;
    QComboBox*    m_seasonCombo   = nullptr;
    QTableWidget* m_episodeTable  = nullptr;
    QLabel*       m_statusLabel   = nullptr;

    // UI — right column (stream-picker UX rework)
    QWidget*                               m_rightPaneStack = nullptr;
    QWidget*                               m_sourcesPanel   = nullptr;
    QLabel*                                m_sourcesHeader = nullptr;
    tankostream::stream::StreamSourceList* m_sourcesList   = nullptr;

    // Batch 6.2 — preselection staged between showEntry and onSeriesMetaReady.
    // Consumed once, then cleared so a second showEntry without preselect
    // doesn't re-apply stale values.
    int m_pendingPreselectSeason  = -1;
    int m_pendingPreselectEpisode = -1;

    // Phase 1 Batch 1.2 — MetaItemPreview from the most-recent showEntry
    // call. Used when the user clicks "Add to Library" from a non-library
    // title — we construct a StreamLibraryEntry from these fields.
    std::optional<tankostream::addon::MetaItemPreview> m_lastPreviewHint;

    // Phase 3 Batch 3.1 — network manager + on-disk cache for background art.
    // Shape mirrors StreamLibraryLayout's poster cache:
    //   {AppLocalDataLocation}/Tankoban/data/stream_backgrounds/{imdb}.jpg
    // Shared across all titles; NAM reused for the view lifetime.
    QNetworkAccessManager* m_nam             = nullptr;
    QString                m_heroCacheDir;
    // Phase 3 Batch 3.4 — episode-thumbnail disk cache:
    //   {AppLocalDataLocation}/Tankoban/data/stream_episode_thumbnails/
    //       {imdb}_{season}_{episode}.jpg
    // Same NAM as the hero fetcher; download lifetimes are QPointer-guarded
    // against view destruction + imdb-mismatch stale-callback guarded.
    QString                m_episodeThumbsCacheDir;

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — non-owning pointer.
    // Wired by StreamPage::setStreamDownloadIndex; lifetime is the
    // MainWindow's StreamDownloadIndex member, which outlives this view.
    StreamDownloadIndex*   m_downloadIndex = nullptr;

    // STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) — non-owning pointer
    // to the TorrentClient (lifetime is MainWindow's). Used by
    // onLibraryButtonClicked to gate Remove-from-Library on active bulk
    // groups (Spec §10.10) and by refreshEpisodeBulkProgress to poll
    // per-episode bulk-download state (V2 Phase 3).
    TorrentClient*         m_torrentClient = nullptr;

    // STREAM_BULK_DOWNLOAD_V2 Phase 3 — 1Hz timer driving Status-column
    // download-state repaints. Started in populateEpisodeTable when bulk
    // activity exists for the show+season; idle otherwise.
    QTimer*                m_bulkPollTimer = nullptr;

    // TANKORENT_CINEMATA P1.T8 (2026-05-21) — Tankorent headless search
    // service for the [Find sources for Season N] flow. Non-owning;
    // injected via setSearchService from MainWindow (T9).
    TankorentSearchService* m_searchService = nullptr;
    // Tracks the in-flight Find-sources handle for handle-vs-other-handle
    // dispatch checks in the topResultPicked / searchFinished slots.
    // Empty when no search is pending.
    QString                 m_currentSearchHandle;
    // The new [Find sources for Season N] button. Lives in the seasonLayout
    // beside the existing m_downloadBtn + m_packOptionsBtn pair.
    QPushButton*            m_findSourcesBtn = nullptr;

    // F13 fix 2026-05-19: periodic refresh of movie + episode download badges
    // during active downloads. The substrate's state-change signals only fire on
    // transitions (Pending→Downloading→Complete), not on per-piece progress, so
    // the badge would stay frozen at the dispatch-time percent without this poll.
    // Timer is started in showEvent + stopped in hideEvent; cheap (~1Hz against
    // in-process QHash). Connected to refreshMovieDownloadState +
    // refreshSubstrateStatesForActiveSeason.
    QTimer*                m_progressRefreshTimer = nullptr;

    // STREAM_ASYNC_RACE_FIXES 2026-05-18 Task B - stamped when a download
    // dispatch fires (movie or season-header pack). refreshEpisodeBulkProgress
    // checks this to avoid stopping the poll timer during the cross-thread
    // window where the torrent client hasn't yet registered the new bulk
    // group in its snapshot. Default-constructed (invalid) until first
    // dispatch.
    QDateTime              m_lastBulkDispatchTime;

private:
    void refreshLibraryButton();
    void onLibraryButtonClicked();
};
