#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

#include "core/stream/addon/MetaItem.h"
#include "ui/pages/stream/StreamSourceChoice.h"

class CoreBridge;
class TorrentEngine;
class StreamEngine;
class StreamLibrary;
class StreamLibraryLayout;
class StreamSearchWidget;
class StreamDetailView;
class StreamPlayerController;
class StreamContinueStrip;
class AddonManagerScreen;

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {
class StreamHomeBoard;
class CatalogBrowseScreen;
class StreamAggregator;
class MetaAggregator;
class SubtitlesAggregator;
class CalendarEngine;
class CalendarScreen;
struct StreamPickerChoice;
}

class StreamPage : public QWidget
{
    Q_OBJECT

public:
    explicit StreamPage(CoreBridge* bridge, TorrentEngine* torrentEngine,
                        QWidget* parent = nullptr);

    void activate();

    // Exposed for VideosPage (HELP.md 2026-04-15 — Agent 5 folder-poster
    // fetch). Sharing the same instance avoids duplicating the addon manifest
    // cache and network transport. Lifetime: owned by StreamPage, which is a
    // sibling under MainWindow's page stack — both pages share the MainWindow
    // lifetime, so this pointer is stable for the app session.
    tankostream::stream::MetaAggregator* metaAggregator() const { return m_metaAggregator; }

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
    // Phase 4 Batch 4.1 — live-search pipeline. `onSearchTextChanged` restarts
    // the 300ms debounce on every keystroke; `onSearchDebounceFired` is the
    // deferred executor: empty input restores browse, <2 chars no-ops, >=2
    // chars runs the same path as `onSearchSubmit`. Spinner toggles via
    // `setSearchBusy` driven by MetaAggregator's catalog result / error.
    void onSearchTextChanged(const QString& text);
    void onSearchDebounceFired();
    void setSearchBusy(bool busy);
    // Phase 4 Batch 4.2 — search history. QSettings-persisted last-20
    // queries, chronological (most-recent-first), deduped on insert.
    // Dropdown shows top-10 on empty-field focus; click re-runs the
    // search via the same debounce path, per-row × removes an entry.
    void buildSearchHistoryDropdown();
    void loadSearchHistory();
    void saveSearchHistory();
    void pushSearchHistory(const QString& query);
    void removeSearchHistoryEntry(const QString& query);
    void showSearchHistoryDropdown();
    void hideSearchHistoryDropdown();
    void positionSearchHistoryDropdown();
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showBrowse();
    void showAddonManager();
    void showCalendar();  // Batch 6.2
    void showCatalogBrowse(const QString& addonId, const QString& type,
                           const QString& catalogId, const QString& title);
    // Stream library UX rework 2026-04-15 — catalog button handler. Opens
    // CatalogBrowseScreen with no preselection (screen falls back to its
    // own default addon + catalog selection if the user hasn't opened it
    // before, or replays the last-used combo state if they have).
    void onCatalogBtnClicked();
    // Library-tile / calendar-row / continue-strip path — no preview available,
    // StreamLibrary::get(imdbId) is the fallback source for detail-view header.
    void showDetail(const QString& imdbId);
    // Phase 1 Batch 1.1: catalog/home/search path — carries the tile's
    // MetaItemPreview through so the detail view paints title/year/poster/
    // description immediately even when the title is NOT in the user's
    // library. Kicks off the Phase 3 richer meta fetch via fetchMetaItem.
    void showDetail(const tankostream::addon::MetaItemPreview& preview,
                    int preselectSeason  = -1,
                    int preselectEpisode = -1);
    void onPlayRequested(const QString& imdbId, const QString& mediaType,
                         int season, int episode);

    // Stream-picker UX rework — user clicked a source card inside
    // StreamDetailView's right pane. Persists the choice and dispatches to
    // StreamPlayerController (takes over what StreamPickerDialog::accept
    // used to trigger post-exec).
    void onSourceActivated(const tankostream::stream::StreamPickerChoice& choice);

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

    void onBufferUpdate(const QString& statusText, double percent);
    void onReadyToPlay(const QString& httpUrl);
    void onStreamFailed(const QString& message);
    void onStreamStopped();

    CoreBridge*      m_bridge;
    TorrentEngine*   m_torrentEngine;

    // Core services
    StreamEngine*    m_streamEngine = nullptr;
    StreamLibrary*   m_library   = nullptr;

    // UI layers
    QStackedWidget*  m_mainStack = nullptr;  // browse, detail, player, addons

    // Search bar
    QFrame*     m_searchBarFrame = nullptr;
    QLineEdit*  m_searchInput    = nullptr;
    QPushButton* m_searchBtn     = nullptr;
    QPushButton* m_addonsBtn     = nullptr;
    QPushButton* m_calendarBtn   = nullptr;  // Batch 6.2
    // Stream library UX rework 2026-04-15 — Catalog button replaces the
    // deleted home-board featured rows as the user-facing entry point
    // into CatalogBrowseScreen.
    QPushButton* m_catalogBtn    = nullptr;

    // Phase 4 Batch 4.1 — debounce + spinner. Timer is single-shot 300ms,
    // restarted on every textChanged. Busy indicator is an indeterminate
    // QProgressBar (Qt's built-in "busy" mode — range [0,0]) living between
    // the input and the Search button; shown on search fire, hidden on
    // catalogResults / catalogError.
    QTimer*      m_searchDebounce = nullptr;
    QWidget*     m_searchBusy     = nullptr;   // QProgressBar forward-declared; held as QWidget*

    // Phase 4 Batch 4.2 — search history. Dropdown is a QFrame child of
    // StreamPage (not the search-bar frame) so it can float over the
    // browse/detail layers via raise(). Positioned manually on show to
    // align with m_searchInput's screen geometry. Settings key
    // `stream/searchHistory`; max 20 entries persisted, top 10 rendered.
    QFrame*      m_searchHistoryDropdown = nullptr;
    QWidget*     m_searchHistoryList     = nullptr;   // scroll contents
    QTimer*      m_searchHistoryHideTimer = nullptr;  // delays hide on focus-out
    QStringList  m_searchHistory;
    static constexpr int kMaxSearchHistory    = 20;
    static constexpr int kDisplaySearchHistory = 10;

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

    // Calendar engine (Phase 6 Batch 6.1) — per-library-series meta fan-out
    // over meta-capable addons, filtered to now..now+60d, grouped for the
    // Phase 6 Batch 6.2 CalendarScreen.
    tankostream::stream::CalendarEngine* m_calendarEngine = nullptr;

    // Calendar screen (Phase 6 Batch 6.2) — 6th stack layer (index 5).
    // Shown via m_calendarBtn in the search bar; consumes calendarReady /
    // calendarGroupedReady from m_calendarEngine; double-click routes back
    // to StreamDetailView with the selected episode pre-focused.
    tankostream::stream::CalendarScreen* m_calendarScreen = nullptr;

    // Browse layer
    QWidget*     m_browseLayer   = nullptr;
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

    // Player controller
    StreamPlayerController* m_playerController = nullptr;

    // Buffer overlay
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
    PendingPlay m_pendingPlay;

    // Phase 2 Batch 2.4 — auto-launch timer + buffered choice. The timer
    // fires 2s after StreamPage arms it (enough for the user to notice the
    // "Resuming with last-used source" toast and click "Pick different").
    // cancelAutoLaunch() zeroes both cleanly.
    QTimer*                                                m_autoLaunchTimer  = nullptr;
    std::optional<tankostream::stream::StreamPickerChoice> m_autoLaunchChoice;

    // Phase 2 Batch 2.5 — next-episode pre-fetch + end-of-playback overlay.
    // Pre-fetch is kicked off once the current episode crosses 95%. Result
    // lands in m_nextPrefetch.matchedChoice when streams + bingeGroup resolve.
    // On closeRequested (after near-end was crossed AND a matched choice is
    // available), the overlay is shown on the player layer with a 10s
    // countdown → auto-opens the next episode via the same onSourceActivated
    // entry point user-click uses. Cancel returns to browse.
    struct NextEpisodePrefetch {
        QString imdbId;
        int     season  = 0;
        int     episode = 0;
        QString epKey;
        std::optional<tankostream::stream::StreamPickerChoice> matchedChoice;
        bool    streamsLoaded     = false;
    };
    std::optional<NextEpisodePrefetch> m_nextPrefetch;
    bool    m_nearEndCrossed = false;

    // Phase 2 Batch 2.6 — set by onStreamNextEpisodeShortcut when the
    // prefetch isn't ready at key-press time. onNextEpisodePrefetchStreams
    // checks this flag after resolving matchedChoice and auto-plays
    // (skipping the overlay + countdown) if set. Cleared on fire or on
    // any prefetch reset path.
    bool    m_nextShortcutPending = false;

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 — sliding-window deadline
    // retargeting rate-limiter. progressUpdated fires ~1Hz; without a
    // gate we'd thrash libtorrent's deadline table. 2s matches
    // Stremio-class cadence — reader-frontier rarely moves more than
    // a few MB per 2s at typical bitrates, keeping piece churn bounded.
    qint64  m_lastDeadlineUpdateMs = 0;

    // STREAM_PLAYBACK_FIX Batch 2.4 fix-up 2026-04-15 — the seek pre-gate
    // retry state (QObject parented to StreamPage, holds an iteration
    // counter and a pending QTimer::singleShot). If a prior onReadyToPlay
    // session scheduled a retry and the user closed / re-opened the same
    // stream before the 300ms timer fired, the orphan retry would fire a
    // SECOND launchPlayer for the same URL — racing the fresh openFile's
    // sidecar boot with a stop+shutdown+open and killing playback.
    // Storing the state as a member lets onReadyToPlay cancel any
    // outstanding retry before starting a new one. Single-instance.
    QObject* m_seekRetryState = nullptr;

    QFrame*      m_nextEpisodeOverlay        = nullptr;
    QLabel*      m_nextEpisodeTitleLabel     = nullptr;
    QLabel*      m_nextEpisodeCountdownLabel = nullptr;
    QPushButton* m_nextEpisodePlayNowBtn     = nullptr;
    QPushButton* m_nextEpisodeCancelBtn      = nullptr;
    QTimer*      m_nextEpisodeCountdownTimer = nullptr;
    int          m_nextEpisodeCountdownSec   = 10;
};
