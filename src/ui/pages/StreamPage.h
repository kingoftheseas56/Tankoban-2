#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStack>
#include <QStackedWidget>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>
#include <optional>

#include "core/stream/BulkSourceCollector.h"
#include "core/stream/StreamBulkPlan.h"
#include "core/stream/addon/MetaItem.h"
#include "ui/LayerEntry.h"
// THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) — StreamPlayerController.h removed;
// Theatre no longer constructs or references the streaming controller.
#include "ui/pages/stream/StreamSourceChoice.h"

class CoreBridge;
class QDialog;
class QProgressBar;
class TorrentClient;
class TorrentEngine;
struct StreamBulkGroupRecord;
// THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) — StreamServerEngine forward
// declaration removed; the stream-server subprocess is no longer constructed.
class StreamLibrary;
class StreamLibraryLayout;
class StreamDownloadIndex;
class StreamSearchWidget;
class StreamDetailView;
class StreamContinueStrip;
class AddonManagerScreen;
// TANKORENT_STREAM_INTEGRATION E4 2026-05-15 — Local files row at the bottom
// of the Theatre library: surfaces top-level video-folder subdirs as
// folder-style tile cards, just like Videos mode renders them. Replaces the
// discoverability surface of the removed Videos sidebar entry (Task E5).
class TileStrip;
class QThread;

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {
class BulkPackVerifier;
struct BulkPackVerificationResult;
class StreamHomeBoard;
class CatalogBrowseScreen;
class StreamAggregator;
class MetaAggregator;
class SubtitlesAggregator;
struct StreamPickerChoice;
}

namespace tankoban::stream::theatre {
class TheatreDownloadPanel;
class UnifiedPackSearchEngine;
}

class StreamPage : public QWidget
{
    Q_OBJECT

public:
    explicit StreamPage(CoreBridge* bridge, TorrentClient* torrentClient,
                        QWidget* parent = nullptr);

    void activate();

    // Public entry point for MainWindow::resetActivePageToRoot. Clicking the
    // Theatre topbar pill from any deep sub-view returns to Browse root.
    void resetToRoot();

    // STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) — wire the download
    // index into the home library board (chip rendering on tiles) and into
    // StreamLibrary (so remove() evicts per-episode entries).
    void setStreamDownloadIndex(StreamDownloadIndex* idx);
    StreamDownloadIndex* streamDownloadIndex() const { return m_streamDownloadIndex; }

    // Exposed for VideosPage (HELP.md 2026-04-15 — Agent 5 folder-poster
    // fetch). Sharing the same instance avoids duplicating the addon manifest
    // cache and network transport. Lifetime: owned by StreamPage, which is a
    // sibling under MainWindow's page stack — both pages share the MainWindow
    // lifetime, so this pointer is stable for the app session.
    tankostream::stream::MetaAggregator* metaAggregator() const { return m_metaAggregator; }

    // STREAM_DOWNLOADED_LIBRARY Phase 6 (2026-05-10) — exposed for the
    // first-launch migration scanner (StreamRescueScanner). Same lifetime
    // contract as metaAggregator(): owned by StreamPage, stable for the
    // app session.
    StreamLibrary* streamLibrary() const { return m_library; }

    QJsonObject devSnapshot() const;
    // v1.6 Phase D.4 (2026-05-19) — cross-mode library-section snapshot used
    // by library_get_* commands + embedded in devSnapshot under "library".
    QJsonObject devLibrarySection() const;
    QJsonObject devSearch(const QString& query,
                          const QString& typeFilter,
                          int timeoutMs);
    QJsonObject devDispatchEpisode(const QString& imdbId, int season, int episode);
    QJsonObject devDispatchSeason(const QString& imdbId, int season);

    // v1.3 stream-side bridge expansion (Agent 4 attribution, 2026-05-19).
    // Three commands that programmatically drive what a user does to dispatch
    // a Tankorent-source record (the Phase 2 substrate trigger from
    // TANKORENT_CINEMETA_PACK_MAPPING Tasks 7-10): open detail view → enumerate
    // source cards → trigger directDownloadRequested on the Nth card.
    // Stubs land first; bodies filled in by Agent 4 subordinates A4S1/A4S2/A4S3.
    QJsonObject devOpenDetail(const QString& imdbId);          // A4S1
    QJsonObject devGetSources();                                // A4S2
    QJsonObject devDirectDownload(int sourceIndex);             // A4S3
    Q_INVOKABLE bool dispatchDevCommand(const QString& cmd,
                                        const QJsonObject& payload,
                                        QJsonObject& reply);

signals:
    // STREAM_ADD_TO_TANKORENT (2026-05-06) — emitted when the user
    // right-clicks a magnet stream card and picks "Add torrent to
    // Tankorent". MainWindow listens; it activates Tankorent and calls
    // TankorentPage::addMagnetFromExternal. `displayName` is informational
    // (logged + surfaced in Tankorent's status row); the magnet itself
    // is the load-bearing payload.
    void addToTankorentRequested(const QString& magnetUri,
                                 const QString& displayName);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — renamed from addToTankorentBulkRequested.
    // Routes to MainWindow's onAddToTankorentBulkRequested slot which dispatches
    // WITHOUT a page-switch (no longer activates the Tankorent tab).
    void streamBulkDispatchRequested(
        const StreamBulkGroupRecord& group,
        const tankostream::stream::BulkPackVerificationResult& verifierOutput,
        const QString& displayLabel);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — forwards a
    // StreamDetailView click on a downloaded episode up to MainWindow. The
    // SubtitlesAggregator + per-session bookkeeping run on this side BEFORE
    // re-emitting; MainWindow's slot only handles VideoPlayer open. Spec §6.2.
    void playLocalFileFromStreamRequested(const QString& localPath,
                                          const QString& imdbId,
                                          const QString& showTitle,
                                          int season,
                                          int episode);

    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- emitted BEFORE every
    // user-initiated in-page layer transition. The emitted LayerEntry
    // captures the INCOMING state so the controller can restore it on
    // Back. MainWindow connects this to PerModeNavController::pushLayer.
    // Suppressed during restoreLayer via m_inLayerRestore.
    void enteredLayer(const tankoban::ui::LayerEntry& entry);
    // Emitted when the user closes a deep layer via an in-page affordance
    // (goBack from Detail / Search / Catalog / AddonManager).
    // The controller pops via this signal so the back-stack stays
    // consistent with the in-page state machine.
    void exitedLayer();

public slots:
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- re-render the targeted
    // layer in-place WITHOUT emitting enteredLayer. Called by MainWindow
    // when PerModeNavController::layerRestoreRequested fires for
    // pageId="stream".
    void restoreLayer(const tankoban::ui::LayerEntry& target);

private:
    void buildUI();
    void buildSearchBar();
    void buildBrowseLayer();

    void onSearchSubmit();
    // Phase 4 Batch 4.3 — URL/magnet paste detection. `PasteKind::None`
    // means the input is text → normal search; other kinds route the
    // Enter/button press to player launch or addon install instead of
    // searchCatalog. Detection is regex-guarded on each textChanged
    // from Batch 4.1 so the Search button label can update live.
    enum class PasteKind { None, Magnet, DirectVideo, AddonManifest };
    PasteKind detectPasteKind(const QString& input) const;
    void applyPasteKindToSearchButton(PasteKind kind);
    void handlePasteAction(PasteKind kind, const QString& input);
    // Phase 4 Batch 4.1 — search pipeline. Live-search debounce removed
    // 2026-04-25; `onSearchTextChanged` now only drives paste-kind detection
    // (refreshes the Search button label) + history-dropdown lifecycle
    // (show on empty+focused, hide on non-empty). Search execution itself
    // fires on Enter / Search button / history-row click via
    // `onSearchSubmit`. Spinner toggles via `setSearchBusy` driven by
    // MetaAggregator's catalog result / error.
    void onSearchTextChanged(const QString& text);
    void setSearchBusy(bool busy);
    // Phase 4 Batch 4.2 — search history. QSettings-persisted last-10
    // queries, chronological (most-recent-first), deduped on insert.
    // Dropdown shows the full list on empty-field focus; row click re-runs
    // the search, per-row × removes one entry, footer "Clear search history"
    // wipes the lot. Cap unified to a single value 2026-04-25 (was 20
    // persisted / 10 rendered split — Hemanth flagged the persisted depth
    // as too long).
    void buildSearchHistoryDropdown();
    void loadSearchHistory();
    void saveSearchHistory();
    void pushSearchHistory(const QString& query);
    void removeSearchHistoryEntry(const QString& query);
    void clearSearchHistory();
    void showSearchHistoryDropdown();
    void hideSearchHistoryDropdown();
    void positionSearchHistoryDropdown();
    bool eventFilter(QObject* obj, QEvent* event) override;
    // GLOBAL_NAV_HISTORY Task 14 review fix: emitNav=false on the
    // system-initiated player-exit-fallback path so the global stack
    // doesn't get a spurious entry. User-initiated callers (nav-bar
    // Library button, search-clear) pass the default true.
    void showBrowse(bool emitNav = true);
    void showAddonManager();
    void showCatalogBrowse(const QString& addonId, const QString& type,
                           const QString& catalogId, const QString& title);
    // Stream library UX rework 2026-04-15 — catalog button handler. Opens
    // CatalogBrowseScreen with no preselection (screen falls back to its
    // own default addon + catalog selection if the user hasn't opened it
    // before, or replays the last-used combo state if they have).
    void onCatalogBtnClicked();

    // THEATRE_CLEANUP F2 (2026-05-22) — gear-button menu handler for the
    // Clear Library destructive flow. Two-step confirmation: first a
    // QMessageBox warning, then a QInputDialog requiring the user to type
    // "clear" verbatim. On confirm, invokes m_library->clear() which
    // cascades through evictByImdb + cancelStreamBulkGroup(deleteFiles=true)
    // on every entry. Per Hemanth's Q1-Q4 ratification 2026-05-22 ~2:50pm
    // IST (Theatre-only scope, full cascade including downloaded files,
    // two-step type-clear confirmation, gear icon in topbar).
    void onClearLibraryRequested();
    // Library-tile / continue-strip path — no preview available,
    // StreamLibrary::get(imdbId) is the fallback source for detail-view header.
    void showDetail(const QString& imdbId);
    // Phase 1 Batch 1.1: catalog/home/search path — carries the tile's
    // MetaItemPreview through so the detail view paints title/year/poster/
    // description immediately even when the title is NOT in the user's
    // library. Kicks off the Phase 3 richer meta fetch via fetchMetaItem.
    void showDetail(const tankostream::addon::MetaItemPreview& preview,
                    int preselectSeason  = -1,
                    int preselectEpisode = -1);

    // STREAM_NAV_BACK_STACK 2026-05-06 — depth-first back navigation.
    // Pops one layer off m_navStack and re-shows the previous entry via
    // showEntryRaw (no push). Stack-bottom (Library home) is a no-op
    // re-show. Wired to all 5 child screens' backRequested signals
    // (StreamDetailView / AddonManagerScreen / CatalogBrowseScreen /
    // StreamSearchWidget).
    void goBack();

    // STREAM_NAV_BACK_STACK 2026-05-06 — restore the pre-player view if
    // a snapshot was captured at launch; otherwise fall back to library
    // (legacy default). Called from onStreamStopped UserEnd, the defensive
    // 3s post-close timer in onStreamFailed, and onNextEpisodeCancel
    // case (a). Closes Hemanth-reported "I close the player I find myself
    // on the library rather than the series page" 2026-05-06.
    void restorePlayerExitView();
    void onPlayRequested(const QString& imdbId, const QString& mediaType,
                         int season, int episode);

    // THEATRE_DOWNLOAD_ONLY P1.1 (2026-05-29) — single reroute funnel for the
    // play entry points in download-only Theatre. Owned-on-disk → play local
    // (reuses the playLocalFileFromStreamRequested emit); not-owned → route to
    // the existing download flow (never stream). When `picked` is non-null the
    // not-owned branch downloads that exact picked source via
    // onDirectDownloadRequested; otherwise it opens the per-episode download
    // flow. Does not reimplement download logic.
    void beginPlayOrDownload(const QString& imdbId, const QString& mediaType,
                             int season, int episode,
                             const tankostream::stream::StreamPickerChoice* picked = nullptr);

    // Stream-picker UX rework — user clicked a source card inside
    // StreamDetailView's right pane. Persists the choice and dispatches to
    // StreamPlayerController (takes over what StreamPickerDialog::accept
    // used to trigger post-exec).
    void onSourceActivated(const tankostream::stream::StreamPickerChoice& choice);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — handler for
    // StreamDetailView's playLocalFileFromStreamRequested. Runs the
    // SubtitlesAggregator fan-out + any local bookkeeping, then forwards
    // up to MainWindow via the public signal of the same name. Spec §6.2 + §10.2.
    void onDetailPlayLocalFileFromStream(const QString& localPath,
                                         const QString& imdbId,
                                         const QString& showTitle,
                                         int season,
                                         int episode);

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — user right-clicked a magnet
    // stream card and picked "Add torrent to Tankorent". Defensive guard
    // (sourceKind/magnetUri must be valid — UI-side menu hides for
    // non-magnets, but signal-arrival is still validated), then re-emits
    // the addToTankorentRequested(magnetUri, displayName) signal upward
    // for MainWindow.
    void onAddToTankorentRequested(const tankostream::stream::StreamPickerChoice& choice);
    void triggerBulkSeasonDownload(int season);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — episode/season/selected-set dispatch
    // wired from StreamDetailView. Bypasses the V2 Phase 1 preflight dialog.
    void onSeasonDownloadRequested(int season);
    void onSelectedEpisodesDownloadRequested(int season, const QList<int>& episodes);
    void onSingleEpisodeDownloadRequested(int season, int episode);

    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - movie auto-dispatch
    // fast-path. Receives the pre-picked top-seeded magnet from StreamDetailView
    // and dispatches via TorrentClient::startDownload directly. Bypasses
    // TheatreDownloadPanel entirely.
    void onTheatreTopSeededDownloadRequested(const QString& imdbId,
                                              const QString& showName,
                                              const QString& infoHash,
                                              const QString& magnetUri);

    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - direct dispatch of
    // a specific right-clicked Sources-panel stream into the Theatre library.
    // Companion to onTheatreTopSeededDownloadRequested but takes a specific
    // user-selected stream instead of the auto-picked top-seeded.
    void onDirectDownloadRequested(const tankostream::stream::StreamPickerChoice& choice);

    // THEATRE_DOWNLOAD_SIMPLIFY P1.T2 (2026-05-29) — silent auto-download.
    // Clicking Download fetches Torrentio sources, auto-picks the best 1080p
    // via AutoSourcePicker, and starts the torrent stamped streamGroupId=
    // theatre:<imdbId> (keeps it off the Tankorent page). No picker, no list.
    struct PendingAutoDownload {
        bool    active        = false;
        QString imdbId;
        QString mediaType;    // "series" | "movie"
        int     season        = 0;
        int     episode       = 0;
        int     runtimeMinutes = 0;  // 0 = unknown -> size guardrail skipped
    };
    PendingAutoDownload m_pendingAuto;
    // infoHash -> what that download is for (used by the P1.T3 progress/
    // completion wiring to map a torrent back to its episode).
    QHash<QString, PendingAutoDownload> m_autoDownloadByHash;

    void startAutoDownload(const QString& imdbId, const QString& mediaType,
                           int season, int episode);
    void finishAutoDownloadPick(const QList<tankostream::addon::Stream>& streams,
                                const QHash<QString, QString>& addonsById);

    // Internal helper used by the three slots above. episodeFilter non-empty
    // restricts the dispatch to those episode numbers only (whole-season when empty).
    void triggerBulkSelectedEpisodes(const QString& imdbId, int season,
                                     const QList<int>& episodeFilter);
    QJsonObject devDispatchEpisodes(const QString& imdbId, int season,
                                    const QList<int>& episodeFilter);

    // THEATRE_DOWNLOAD_OVERHAUL stale-panel-on-show-change fix v2 2026-05-17 -
    // imperative dismiss called at every navigation transition site (showEntry
    // call sites + goBack + showBrowse) so the panel never carries prior-show
    // results across show transitions OR show-view exits. The earlier signal-
    // based fix via entryContextChanging missed paths and/or had timing issues
    // that left the panel visible with stale packs. This call is idempotent +
    // safe to invoke when the panel is already hidden.
    void dismissTheatreDownloadPanelIfOpen();

    void retryBulkSeasonDownload(const QString& groupId, const QStringList& itemKeys);
    void cancelBulkSeasonDownload();
    void onBulkSourcesCollected(const tankostream::stream::BulkSourceCollectionPayload& payload);
    void onBulkPackVerified(const tankostream::stream::BulkPackVerificationResult& result);
    void onBulkPackVerificationFailed(const QString& reason);

    // Phase 2 Batch 2.4 — auto-launch orchestration.
    void onAutoLaunchFire();
    void cancelAutoLaunch();

    // Phase 2 Batch 2.5 — end-of-episode pre-fetch + next-episode overlay.
    void startNextEpisodePrefetch(const QString& imdbId,
                                   int currentSeason, int currentEpisode);
    void onNextEpisodePrefetchStreams(
        const QList<tankostream::addon::Stream>& streams,
        const QHash<QString, QString>& addonsById);
    void showNextEpisodeOverlay();
    void hideNextEpisodeOverlay();
    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) — sibling of
    // showNextEpisodeOverlay; reparents the same overlay widget onto the
    // floating VideoPlayer so it renders OVER still-playing video at the
    // last ~30-60s of the current episode (Stremio / Netflix binge UX)
    // instead of waiting until closeRequested. Falls back to the close-
    // path overlay when VideoPlayer isn't visible (defensive). Trigger
    // point is onNextEpisodePrefetchStreams once matchedChoice populates.
    void showNextEpisodeOverlayInPlayer();
    void onNextEpisodeCountdownTick();
    void onNextEpisodePlayNow();
    void onNextEpisodeCancel();
    void resetNextEpisodePrefetch();

    // Phase 2 Batch 2.6 — Shift+N manual next-episode shortcut during
    // stream playback. If a prefetched match is already resolved
    // (user crossed 95% and matchedChoice landed), plays immediately.
    // Otherwise triggers the prefetch on-demand and plays when the
    // stream fan-out + bingeGroup match lands. No-op for non-series
    // playback or when the series has no next unwatched episode.
    void onStreamNextEpisodeShortcut();

    // THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) — the four StreamPlayerController
    // signal handler slots (onBufferUpdate / onReadyToPlay / onStreamFailed /
    // onStreamStopped) are removed along with the controller they served.

    CoreBridge*      m_bridge;
    TorrentClient*   m_torrentClient = nullptr;
    TorrentEngine*   m_torrentEngine;

    // THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) — m_streamEngine member removed;
    // the stream-server subprocess is no longer created.
    StreamLibrary*   m_library   = nullptr;

    // UI layers
    QStackedWidget*  m_mainStack = nullptr;  // browse, detail, player, addons

    // Search bar
    QFrame*     m_searchBarFrame = nullptr;
    QLineEdit*  m_searchInput    = nullptr;
    QPushButton* m_searchBtn     = nullptr;
    QPushButton* m_addonsBtn     = nullptr;
    // Stream library UX rework 2026-04-15 — Catalog button replaces the
    // deleted home-board featured rows as the user-facing entry point
    // into CatalogBrowseScreen.
    QPushButton* m_catalogBtn    = nullptr;
    // THEATRE_CLEANUP F2 (2026-05-22) — gear icon at the right edge of
    // the Theatre topbar. Opens a QMenu with a "Danger zone" section
    // containing the Clear Library action.
    QPushButton* m_settingsBtn   = nullptr;

    // Phase 4 Batch 4.1 — debounce + spinner. Timer is single-shot 300ms,
    // restarted on every textChanged. Busy indicator is an indeterminate
    // QProgressBar (Qt's built-in "busy" mode — range [0,0]) living between
    // the input and the Search button; shown on search fire, hidden on
    // catalogResults / catalogError.
    //
    // Live-search debounce was removed 2026-04-25 per Hemanth: even at 800ms
    // it fired during natural mid-typing pauses. Search now only fires on
    // Enter / Search button / history-row click. textChanged still drives
    // paste-kind detection + history dropdown show/hide on empty input.
    QWidget*     m_searchBusy     = nullptr;   // QProgressBar forward-declared; held as QWidget*

    // Phase 4 Batch 4.2 — search history. Dropdown is a QFrame child of
    // StreamPage (not the search-bar frame) so it can float over the
    // browse/detail layers via raise(). Positioned manually on show to
    // align with m_searchInput's screen geometry. Settings key
    // `stream/searchHistory`; cap unified at 10 entries (persisted ==
    // displayed) — Hemanth 2026-04-25 flagged 20 as too long.
    QFrame*      m_searchHistoryDropdown = nullptr;
    QWidget*     m_searchHistoryList     = nullptr;   // scroll contents
    QTimer*      m_searchHistoryHideTimer = nullptr;  // delays hide on focus-out
    QStringList  m_searchHistory;
    static constexpr int kMaxSearchHistory = 10;

    // Phase 4 Batch 4.3 — current URL-paste detection state (drives
    // Search button label + Enter routing).
    PasteKind    m_pasteKind = PasteKind::None;

    // Addon manager (Phase 2)
    tankostream::addon::AddonRegistry* m_addonRegistry = nullptr;
    AddonManagerScreen* m_addonManager = nullptr;

    // Catalog browse (Phase 3 Batch 3.3)
    tankostream::stream::CatalogBrowseScreen* m_catalogBrowse = nullptr;

    // Stream aggregator (Phase 4 Batch 4.1) — multi-source stream fan-out for onPlayRequested
    tankostream::stream::StreamAggregator* m_streamAggregator = nullptr;

    // Meta aggregator (Phase 4 Batch 4.4) — search + series meta via addon registry
    tankostream::stream::MetaAggregator* m_metaAggregator = nullptr;

    // Subtitles aggregator (Phase 5 Batch 5.1) — multi-addon subtitle fan-out.
    // Fed with the selected Stream on onPlayRequested; result pushed to
    // VideoPlayer via setExternalSubtitleTracks for the Batch 5.3 menu.
    tankostream::stream::SubtitlesAggregator* m_subtitlesAggregator = nullptr;

    // Browse layer
    QWidget*     m_browseLayer   = nullptr;
    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) — m_playerLayer promoted
    // from buildUi local to member so onNextEpisodeCancel can re-parent
    // m_nextEpisodeOverlay back into its original layout after the mid-
    // playback reparent to VideoPlayer. Addition of layout position is
    // skipped; we rely on showNextEpisodeOverlay (legacy close-path) to
    // re-position if it fires.
    QWidget*     m_playerLayer   = nullptr;
    QScrollArea* m_browseScroll  = nullptr;
    QWidget*     m_scrollHome    = nullptr;
    QVBoxLayout* m_scrollLayout  = nullptr;

    // Home board (Phase 3 Batch 3.2) — owns the continue strip + catalog rows
    tankostream::stream::StreamHomeBoard* m_homeBoard = nullptr;

    // Continue watching strip — non-owning pointer, lifetime managed by m_homeBoard
    StreamContinueStrip* m_continueStrip = nullptr;

    // Library grid
    StreamLibraryLayout* m_libraryLayout = nullptr;

    // Search results overlay
    StreamSearchWidget* m_searchWidget = nullptr;

    // Detail view
    StreamDetailView* m_detailView = nullptr;
    QWidget* m_detailRightPaneStack = nullptr;
    QWidget* m_detailSourcesPanel = nullptr;
    StreamDownloadIndex* m_streamDownloadIndex = nullptr;
    tankoban::stream::theatre::TheatreDownloadPanel* m_theatreDownloadPanel = nullptr;
    tankoban::stream::theatre::UnifiedPackSearchEngine* m_unifiedPackSearchEngine = nullptr;
    QDialog* m_bulkProgressDialog = nullptr;
    QLabel* m_bulkProgressLabel = nullptr;
    QProgressBar* m_bulkProgressBar = nullptr;
    tankostream::stream::BulkSourceCollector* m_bulkSourceCollector = nullptr;
    tankostream::stream::BulkPackVerifier* m_bulkPackVerifier = nullptr;
    tankostream::stream::BulkPlanInput m_bulkInput;
    tankostream::stream::BulkPlanResult m_bulkPlanResult;
    tankostream::stream::BulkSourceCollectionPayload m_bulkSourcePayload;
    QString m_bulkVerificationNote;
    QString m_bulkRetryGroupId;
    QStringList m_bulkRetryItemKeys;
    bool m_bulkRetryMode = false;

    // THEATRE_DOWNLOAD_ONLY P1.2 (2026-05-29) — m_playerController member
    // removed; the streaming controller is no longer constructed.

    // Buffer overlay — retained widget (no longer shown during playback); the
    // unreachable next-episode overlay path still references it (dead code).
    QWidget* m_bufferOverlay = nullptr;
    QLabel*  m_bufferLabel   = nullptr;
    QPushButton* m_bufferCancelBtn = nullptr;

    // Stream-picker UX rework — context for the in-flight onPlayRequested.
    // Captured when StreamDetailView emits playRequested; consumed when the
    // user clicks a card and onSourceActivated fires. Replaces what the
    // modal dialog used to keep alive on the stack between exec() and
    // accept().
    struct PendingPlay {
        QString imdbId;
        QString mediaType;
        int     season  = 0;
        int     episode = 0;
        QString epKey;
        bool    valid   = false;
    };
    // STREAM_LIFECYCLE_FIX Phase 1 Batch 1.2 — m_pendingPlay folded into
    // PlaybackSession (see m_session.pending near end of class).

    // Phase 2 Batch 2.4 — auto-launch timer + buffered choice. The timer
    // fires 2s after StreamPage arms it (enough for the user to notice the
    // "Resuming with last-used source" toast and click "Pick different").
    // cancelAutoLaunch() zeroes both cleanly.
    QTimer*                                                m_autoLaunchTimer  = nullptr;
    std::optional<tankostream::stream::StreamPickerChoice> m_autoLaunchChoice;

    // Phase 2 Batch 2.5 — next-episode pre-fetch + end-of-playback overlay.
    // Pre-fetch is kicked off once the current episode crosses 95%. Result
    // lands in m_session.nextPrefetch->matchedChoice when streams + bingeGroup
    // resolve. On closeRequested (after near-end was crossed AND a matched
    // choice is available), the overlay is shown on the player layer with a
    // 10s countdown → auto-opens the next episode via the same
    // onSourceActivated entry point user-click uses. Cancel returns to browse.
    struct NextEpisodePrefetch {
        QString imdbId;
        int     season  = 0;
        int     episode = 0;
        QString epKey;
        std::optional<tankostream::stream::StreamPickerChoice> matchedChoice;
        bool    streamsLoaded     = false;
    };
    // STREAM_LIFECYCLE_FIX Phase 1 Batch 1.3 — m_nextPrefetch + m_nearEndCrossed
    // + m_nextShortcutPending folded into PlaybackSession (see
    // m_session.nextPrefetch / .nearEndCrossed / .nextShortcutPending).

    // STREAM_LIFECYCLE_FIX Phase 1 Batch 1.2 — m_lastDeadlineUpdateMs folded
    // into PlaybackSession (see m_session.lastDeadlineUpdateMs). The
    // 2s-gate rationale (rate-limit libtorrent deadline retargeting vs
    // ~1Hz progress ticks) lives at the consumer site in
    // StreamPage::onReadyToPlay's progressUpdated lambda.

    // STREAM_LIFECYCLE_FIX Phase 1 Batch 1.3 — m_seekRetryState folded into
    // PlaybackSession (see m_session.seekRetry, type
    // std::shared_ptr<SeekRetryState>). The raw-QObject*-identity-token
    // pattern (`retryState != m_seekRetryState` orphan check) was replaced
    // with a generation-check pattern (closure captures currentGeneration()
    // at creation, checks isCurrentGeneration(gen) at fire time). The
    // captured-generation model is the first real consumer of the new API.
    // Original rationale (STREAM_PLAYBACK_FIX Batch 2.4 fix-up 2026-04-15):
    // prior to this state-member, orphan QTimer::singleShot retries could
    // fire a SECOND launchPlayer for the same URL across a user
    // close/re-open, racing the fresh openFile's sidecar boot and killing
    // playback. The generation-check closes that class entirely — a retry
    // scheduled under generation N is inert under any generation != N,
    // regardless of same-URL or different-URL session.

    QFrame*      m_nextEpisodeOverlay        = nullptr;
    QLabel*      m_nextEpisodeTitleLabel     = nullptr;
    QLabel*      m_nextEpisodeCountdownLabel = nullptr;
    QPushButton* m_nextEpisodePlayNowBtn     = nullptr;
    QPushButton* m_nextEpisodeCancelBtn      = nullptr;
    QTimer*      m_nextEpisodeCountdownTimer = nullptr;
    int          m_nextEpisodeCountdownSec   = 10;

    // STREAM_LIFECYCLE_FIX Phase 1 Batch 1.1 — PlaybackSession foundation.
    // Consolidates the 7 session-scoped fields scattered inline today so every
    // async closure / timer / signal callback can check session identity via a
    // single monotonic generation counter. Batch 1.1 introduces the type +
    // boundary API only; Batches 1.2 + 1.3 migrate consumers. Existing state
    // members above stay in place during the migration window.
    //
    // SeekRetryState — Batch 1.3 fleshed. Carries the captured generation
    // and iteration counter for the onReadyToPlay seek-retry closure.
    // Shape adopted from Agent 7's prototype at
    // agents/prototypes/stream_lifecycle/Batch1.1_PlaybackSession_struct_API.cpp.
    // Replaces the raw-QObject*-identity-token pattern that existed pre-1.3
    // (where `retryState != m_seekRetryState` was the orphan check).
    struct SeekRetryState {
        quint64 generation = 0;   // generation captured at seek-retry setup
        int     attempts   = 0;   // iteration counter, capped at 30 (9s)
    };

    struct PlaybackSession {
        quint64 generation = 0;            // 0 reserved: "no active session"
        QString epKey;                      // Batch 1.2 migrated (was `_currentEpKey` dynamic property)
        PendingPlay pending;                // Batch 1.2 migrated (was m_pendingPlay)
        std::optional<NextEpisodePrefetch> nextPrefetch;  // Batch 1.3 migrated (was m_nextPrefetch)
        bool nearEndCrossed = false;        // Batch 1.3 migrated (was m_nearEndCrossed)
        bool nextShortcutPending = false;   // Batch 1.3 migrated (was m_nextShortcutPending)
        // STREAM_CONTINUE_LIBRARY_AND_HUD_AUTOFIRE 2026-05-06 — once-per-session
        // gate so progressUpdated's first successful save auto-adds the
        // current show/movie to StreamLibrary. Stremio behavior: opening +
        // playing a stream pins it in the library so the Continue Watching
        // strip can surface it (StreamContinueStrip::refresh's library-has
        // gate at StreamContinueStrip.cpp:121 was the second blocker on Bug 1
        // — saved progress alone wasn't enough for the tile to appear).
        // Reset implicitly by `m_session = PlaybackSession{}` in resetSession
        // (default-init).
        bool autoLibraryAdded = false;
        qint64 lastDeadlineUpdateMs = 0;    // Batch 1.2 migrated (was m_lastDeadlineUpdateMs)
        std::shared_ptr<SeekRetryState> seekRetry;  // Batch 1.3 migrated (was raw QObject* m_seekRetryState)

        bool isValid() const { return generation != 0 && !epKey.isEmpty(); }
    };

    PlaybackSession m_session;
    quint64 m_nextGeneration = 1;  // Monotonic; never wraps in practical lifetime. 0 reserved.

    // STREAM_NAV_BACK_STACK 2026-05-06 — depth-first navigation history.
    // Each forward navigation pushes a NavEntry; goBack pops + re-shows.
    // Library home is the stack bottom (always entry [0] when non-empty).
    // Per-kind context is preserved in NavEntry so the restore path can
    // reconstitute the view fully without re-running expensive setup.
    // Search restoration uses the cached m_searchWidget state (no
    // re-fetch); CatalogBrowse calls m_catalogBrowse->open(...) which
    // short-circuits via its own m_needsRebuild gate (no re-fetch).
    struct NavEntry {
        enum class Kind {
            Browse,           // library home (stack bottom)
            CatalogBrowse,    // CatalogBrowseScreen home or single-catalog
            Detail,           // StreamDetailView — series or movie
            AddonManager,     // AddonManagerScreen
            Search,           // StreamSearchWidget overlay
        };
        Kind kind = Kind::Browse;

        // CatalogBrowse context (forwarded to m_catalogBrowse->open).
        QString catalogAddonId;
        QString catalogType;
        QString catalogId;
        QString catalogTitle;

        // Detail context. Two flavors:
        //   (a) imdbId-only — library-tile / continue-watching path
        //   (b) preview-hint — catalog / home / search path with full preview
        // detailHasPreview discriminates.
        QString detailImdbId;
        bool    detailHasPreview = false;
        tankostream::addon::MetaItemPreview detailPreview;
        int     detailPreselectSeason  = -1;
        int     detailPreselectEpisode = -1;

        // Search context — query string preserved for diagnostics + future
        // re-run-on-stale escape hatch. Restore reads cached widget state.
        QString searchQuery;
    };
    QStack<NavEntry> m_navStack;

    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) — raised via
    // QScopedValueRollback in restoreLayer to suppress enteredLayer /
    // exitedLayer re-emission on every show* path during a controller-
    // driven Back restore. The existing navigationRequested suppression
    // model (showEntryRaw bypass) remains unchanged.
    bool m_inLayerRestore = false;

    // STREAM_NAV_BACK_STACK 2026-05-06 — view that was top-of-stack at
    // the moment the player overlay was launched. closeRequested handler
    // restores it after teardown so player close lands on the originating
    // Detail view (not the library default). std::optional for the
    // "no playback active" idle state.
    std::optional<NavEntry> m_beforePlayerEntry;

    // STREAM_NAV_BACK_STACK 2026-05-06 — internal view-swap helper. Does
    // the visual transition implied by NavEntry::kind WITHOUT touching
    // m_navStack. show* slots push a NavEntry then call this; goBack
    // pops then calls this; closeRequested restore calls this directly.
    void showEntryRaw(const NavEntry& entry);
    // Search-side helper used by showEntryRaw(Kind::Search) and direct
    // callers (e.g. onSearchSubmit). Restores the visual search overlay
    // without re-running the network search; cached query/results in
    // m_searchWidget are preserved across hide/show cycles.
    void showSearchResults();

    // Accessors — async closures capture currentGeneration() at creation and
    // check isCurrentGeneration(gen) at fire time to reject stale callbacks.
    quint64 currentGeneration() const;
    bool    isCurrentGeneration(quint64 gen) const;

    // Boundaries — single points of session start / teardown. resetSession is
    // pure state teardown: no navigation, no signal emission, no player
    // touches. Callers decide what UI follows. beginSession clears prior
    // state via resetSession first, then stamps the new generation and
    // returns it for the caller to capture in async closures. The optional
    // `reason` arg on beginSession routes into the reset log as
    // `beginSession:<reason>` for finer-grained traceability — adopted from
    // Agent 7's prototype shape.
    quint64 beginSession(const QString& epKey, const PendingPlay& pending,
                         const QString& reason = {});
    void    resetSession(const QString& reason);
};
