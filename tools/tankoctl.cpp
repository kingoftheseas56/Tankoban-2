// tankoctl — REPO_HYGIENE Phase 3 console client for the Tankoban dev-control
// bridge (DevControlServer at src/devtools/DevControlServer.{h,cpp}).
//
// Usage:
//   tankoctl ping
//   tankoctl get-state
//   tankoctl open-page <comics|books|videos|stream|theatre|sources>
//   tankoctl scan-videos
//   tankoctl get-videos [limit]
//   tankoctl play-file <path>
//   tankoctl close-player
//   tankoctl get-player
//   tankoctl logs [limit]
//   tankoctl get-torrents [--active|--all]
//   tankoctl get-library
//   tankoctl get-downloads
//   tankoctl get-bulk-groups
//   tankoctl search <query> [--type movie|series]
//   tankoctl dispatch-episode <imdbId> <season> <episode>
//   tankoctl dispatch-season <imdbId> <season>
//   tankoctl dump-ui [pageId]
//   tankoctl comics-get-state
//   tankoctl comics-get-library
//   tankoctl comics-get-series
//   tankoctl comics-select-volume <row>
//   tankoctl comics-open-series <seriesId|anilistId>
//   tankoctl comics-open-chapter <seriesId|anilistId> <volume> <chapter>
//   tankoctl comics-search-tankoyomi <query> [--timeout ms]
//   tankoctl comics-get-downloads
//   tankoctl comics-dispatch-volume <seriesId|anilistId> <volume> [--source kind|index]
//   tankoctl comics-get-sources
//   tankoctl books-get-state
//   tankoctl books-get-library
//   tankoctl books-refresh-library
//   tankoctl books-search-library <query>
//   tankoctl books-clear-search
//   tankoctl books-open-book <absPath>
//   tankoctl books-open-series <seriesPath|--title name>
//   tankoctl books-get-series-state
//   tankoctl books-set-sort <key>
//   tankoctl books-set-density <0|1|2>
//   tankoctl books-get-progress
//   tankoctl books-seek-page <n>
//   tankoctl books-set-layout <single|double-page|columns>
//   tankoctl books-get-chapters
//   tankoctl books-open-chapter <id>
//   tankoctl books-tts-state
//   tankoctl books-get-listen-state
//   tankoctl books-tts-play
//   tankoctl books-tts-pause
//   tankoctl books-tts-resume
//   tankoctl books-tts-stop
//   tankoctl books-tts-set-voice <voice>
//   tankoctl books-tts-set-speed <speed>
//   tankoctl books-tts-cancel-stream <streamId>
//   tankoctl sources-search-tankorent <query> [--type videos|books|audiobooks|comics]
//   tankoctl sources-search-tankolibrary <query>
//   tankoctl sources-cancel-search
//   tankoctl sources-get-tankorent-state
//   tankoctl sources-get-tankolibrary-state
//   tankoctl sources-get-indexer-health
//   tankoctl sources-force-indexer-refresh <indexer-id>
//   tankoctl sources-get-pending-downloads
//   tankoctl sources-cancel-download <infoHash> [--delete-files]
//   tankoctl sources-pause-torrent <infoHash>
//   tankoctl sources-resume-torrent <infoHash>
//   tankoctl sources-remove-torrent <infoHash> [--delete-files]
//   tankoctl sources-add-magnet <magnet> [--category cat] [--dest path]
//   tankoctl sources-add-url <url> [--category cat] [--dest path]
//   tankoctl sources-set-speed-limits <down-bps> <up-bps> [--scope global|<infoHash>]
//   tankoctl sources-set-queue-limits <max-downloads> <max-uploads> <max-active>
//   tankoctl sources-get-tankolibrary-results
//   tankoctl sources-open-tankolibrary-detail <md5>
//   tankoctl sources-download-tankolibrary-selected
//   tankoctl sources-set-tankolibrary-filters <json>
//   tankoctl library-get-continue-reading <mode>
//   tankoctl library-get-recently-added <mode>
//   tankoctl library-get-search-state <mode>
//   tankoctl library-get-scan-state <mode>
//   tankoctl library-trigger-scan <mode>
//   tankoctl library-get-root-folders <mode>
//   tankoctl library-get-active-layer <mode>
//   tankoctl library-reset-mode <mode>
//   tankoctl library-get-sort <mode>
//   tankoctl library-set-sort <mode> <key>
//   tankoctl library-set-density <mode> <0|1|2>
//   tankoctl library-get-selected-items <mode>
//   tankoctl library-apply-theme <theme-id>
//   tankoctl library-get-active-theme
//   tankoctl library-get-active-mode-pill
//   tankoctl library-get-settings
//   tankoctl library-set-setting <key> <value>
//   tankoctl player-get-audio-tracks
//   tankoctl player-get-subtitle-tracks
//   tankoctl player-select-audio-track <id>
//   tankoctl player-select-subtitle-track <id>      (-1 disables subs)
//   tankoctl player-set-audio-delay <ms>
//   tankoctl player-set-sub-delay <ms>
//   tankoctl player-set-sub-size <delta>            (double, +/- 0.1 = +/-10%)
//   tankoctl player-set-sub-position <pct>          (0..100, 100 = bottom)
//   tankoctl player-get-chapters
//   tankoctl player-seek-chapter <id>
//   tankoctl player-set-volume <0-200>
//   tankoctl player-set-speed <0.25-4.0>
//   tankoctl player-get-hud-state
//   tankoctl player-get-decoder-stats
//   tankoctl player-get-canvas-size
//   tankoctl player-screenshot <path>
//   tankoctl player-simulate-seek-drag <position>
//   tankoctl player-pause
//   tankoctl player-resume
//   tankoctl player-toggle-play
//   tankoctl player-seek <seconds>
//   tankoctl player-frame-step <forward|back>
//   tankoctl player-stop
//   tankoctl player-set-mute <true|false>
//   tankoctl player-get-volume-state
//   tankoctl player-set-aspect <original|4:3|16:9|2.35:1|1.85:1>
//   tankoctl player-set-crop <none|16:9|2.35:1|2.39:1|1.85:1|4:3>
//   tankoctl player-get-loading-overlay
//   tankoctl player-get-buffering-state
//   tankoctl player-get-keybindings
//   tankoctl sidecar-get-process-state
//   tankoctl sidecar-get-current-stream-info
//   tankoctl sidecar-get-decoder-queue              (NYI in v1.7)
//   tankoctl sidecar-get-render-queue               (NYI in v1.7)
//   tankoctl sidecar-restart
//   tankoctl sidecar-get-ipc-latency
//   tankoctl subs-get-active-track
//   tankoctl subs-get-positioning
//   tankoctl subs-get-fonts-loaded
//   tankoctl osd-get-state
//
//   --- v1.8 synthetic UI interaction layer (Phase D.5, 2026-05-19) ---
//   tankoctl ui-query-widget <objectName>
//   tankoctl ui-query-focus
//   tankoctl ui-active-layer
//   tankoctl ui-list-widgets [filter] [--limit N]   (default filter "*", limit 100)
//   tankoctl ui-dry-run <innerCmd> <objectName>      (resolves target, no fire)
//   tankoctl ui-click <objectName>
//   tankoctl ui-keypress <objectName> <key>         (e.g. Qt.Key_Down)
//   tankoctl ui-text-input <objectName> <text>
//   tankoctl ui-simulate-scroll <objectName> <delta>
//   tankoctl ui-simulate-mouse <objectName> <press|release|move|double-click> [x] [y]
//   tankoctl ui-wait-for <condition> [--timeout ms]  (default 5000ms, cap 30000ms)
//                                                    condition forms:
//                                                      <name>            (defaults to :visible)
//                                                      <name>:visible
//                                                      <name>:enabled
//                                                      <name>:text-matches:<regex>
//   tankoctl ui-set-checkbox <objectName> <true|false>
//   tankoctl ui-set-combo <objectName> <value>
//   tankoctl ui-select-table-row <objectName> <row>
//   write-capable ui-* commands (click/keypress/text-input/scroll/mouse/wait/
//   set-checkbox/set-combo/select-table-row) require TANKOBAN_DEV_UI_SIM=1 on
//   the Tankoban server's environment or return UI_SIM_DISABLED.
//
//   --- v1.9 system state + introspection layer (Phase D.6, 2026-05-19) ---
//   tankoctl app-get-active-modals
//   tankoctl app-get-window-list
//   tankoctl app-get-shortcut-table
//   tankoctl settings-get <key>
//   tankoctl settings-set <key> <value>            (WRITE flag)
//   tankoctl settings-dump [group]
//   tankoctl settings-reset <key>                  (WRITE flag)
//   tankoctl jsonstore-get <path>
//   tankoctl jsonstore-set <path> <jsonObject>     (WRITE flag)
//   tankoctl cache-list
//   tankoctl cache-clear <layer>                   (WRITE flag)
//   tankoctl cache-get-stats                       (poster LRU hits/misses/evictions/hit_rate)
//   tankoctl scanner-get-status
//   tankoctl scanner-list-watched
//   tankoctl log-tail <component> [n]              (sidecar|telemetry|events|ipc|tankoctl)
//   tankoctl log-grep <pattern> [maxPerFile]
//   tankoctl log-mark <label>                       (writes to all 4 active log streams)
//   tankoctl log-set-level <component> <level>     (WRITE flag)
//   tankoctl events-tail [n]
//   tankoctl theme-get-palette
//   tankoctl theme-get-applied-stylesheet [objectName]
//   tankoctl theme-reload                          (WRITE flag)
//   tankoctl font-list-loaded
//   tankoctl perf-mark-start <label>
//   tankoctl perf-mark-end <label>
//   tankoctl perf-dump-counters
//   tankoctl dev-inject-error <code> [note]        (WRITE flag)
//   tankoctl dev-toggle-feature <flag> [true|false] (WRITE flag)
//   write-capable v1.9 commands require TANKOBAN_DEV_WRITE=1 on server env or
//   return DEV_WRITE_DISABLED — SEPARATE flag from TANKOBAN_DEV_UI_SIM.
//
//   --- v1.10 lease registry (2026-05-21) ---
//   tankoctl lease-acquire <lane> --holder <agent-id> --purpose <text> --ttl-sec <n>
//   tankoctl lease-release <lane> --token <token>
//   tankoctl lease-heartbeat <lane> --token <token> [--ttl-sec <n>]
//   tankoctl lease-get <lane>
//   tankoctl lease-list
//
// Connects to the named pipe `TankobanDevControl`. Tankoban must be running
// with --dev-control or TANKOBAN_DEV_CONTROL=1.
//
// Exit codes:
//   0 — reply received with type="reply"
//   1 — reply received with type="error"
//   2 — could not connect to the dev-control socket (Tankoban not running
//       with --dev-control, or stale pipe)
//   3 — reply timeout
//   64 — usage error (unknown subcommand or missing required argument)

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStringList>
#include <QTextStream>

#include "tankoctl_scenario.h"

namespace {

constexpr const char* kSocketName = "TankobanDevControl";
constexpr int kConnectTimeoutMs = 1000;
constexpr int kIoTimeoutMs      = 60000;

// Set by the `record <file> ...` wrapper in main(); when non-empty, sendCommand
// appends the wire form of each issued command to this scenario file.
QString g_recordPath;

void printUsage(QTextStream& err)
{
    err << "usage: tankoctl <subcommand> [args...]\n"
        << "\n"
        << "  ping                     liveness probe (returns schema + commands)\n"
        << "  get-state                MainWindow snapshot\n"
        << "  open-page <pageId>       activate page (comics/books/videos/stream/theatre/sources)\n"
        << "  scan-videos              trigger VideosPage rescan\n"
        << "  get-videos [limit]       VideosPage snapshot (default limit 50)\n"
        << "  play-file <path>         open VideoPlayer on path\n"
        << "  close-player             close VideoPlayer\n"
        << "  get-player               VideoPlayer snapshot or null\n"
        << "  logs [limit]             ring buffer entries (default limit 100)\n"
        << "  get-torrents [--active|--all]\n"
        << "                           live torrent records and engine status\n"
        << "  get-library              Stream library + playable episodes\n"
        << "  get-downloads            StreamDownloadIndex entries\n"
        << "  get-bulk-groups          Stream bulk cohort snapshot\n"
        << "  search <query> [--type movie|series]\n"
        << "                           programmatic Stream search\n"
        << "  dispatch-episode <imdbId> <season> <episode>\n"
        << "                           dispatch one Theatre episode download\n"
        << "  dispatch-season <imdbId> <season>\n"
        << "                           dispatch active season download\n"
        << "  dump-ui [pageId]         structured page snapshot\n"
        << "  comics-get-state         ComicsPage snapshot\n"
        << "  comics-get-library       Comics library entries\n"
        << "  comics-get-series        active ComicsSeriesView snapshot or null\n"
        << "  comics-select-volume <row>\n"
        << "                           select active ComicsSeriesView volume row\n"
        << "  comics-open-series <seriesId|anilistId>\n"
        << "                           open AniList-backed Comics series view\n"
        << "  comics-open-chapter <seriesId|anilistId> <volume> <chapter>\n"
        << "                           open downloaded volume in comic reader\n"
        << "  comics-search-tankoyomi <query> [--timeout ms]\n"
        << "                           programmatic Comics/Tankoyomi search\n"
        << "  comics-get-downloads     Manga download index + active queue\n"
        << "  comics-dispatch-volume <seriesId|anilistId> <volume> [--source kind|index]\n"
        << "                           dispatch active volume source\n"
        << "  comics-get-sources       current Comics sources panel snapshot\n"
        << "  comics-open-western-series <seriesId>\n"
        << "                           open a baked Western series from data/western_catalogue/\n"
        << "  comics-download-western-edition <volumeNumber>\n"
        << "                           trigger download of edition #N of the open Western series\n"
        << "  comics-get-western-download-state [<volumeNumber>]\n"
        << "                           volume rows + tile download state for the open Western series\n"
        << "\n"
        << "  v1.3 stream-side bridge expansion (Agent 4, 2026-05-19):\n"
        << "  stream-open-detail <imdbId>\n"
        << "                           navigate Stream mode to a detail view\n"
        << "  stream-get-sources       active detail view's source-card list\n"
        << "  stream-direct-download <sourceIndex>\n"
        << "                           fire directDownloadRequested on Nth source-card\n"
        << "\n"
        << "  v1.3 books-side bridge (Phase D.1, 2026-05-19):\n"
        << "  books-get-state          BooksPage snapshot\n"
        << "  books-get-library        per-series entries + file roster\n"
        << "  books-refresh-library    trigger BooksPage::triggerScan()\n"
        << "  books-search-library <query>\n"
        << "                           drive the library search bar\n"
        << "  books-clear-search       reset the library search bar\n"
        << "  books-open-book <absPath>\n"
        << "                           open a book in the reader\n"
        << "  books-open-series <seriesPath|--title name>\n"
        << "                           navigate to BookSeriesView\n"
        << "  books-get-series-state   BookSeriesView snapshot\n"
        << "  books-set-sort <key>     name_asc / name_desc / updated_desc / ...\n"
        << "  books-set-density <0|1|2>\n"
        << "                           cover-density slider value\n"
        << "  books-get-progress       current reader file + booksProgress entry\n"
        << "  books-tts-state          Qt-side Edge TTS worker snapshot\n"
        << "  books-tts-cancel-stream <streamId>\n"
        << "                           fire EdgeTtsWorker::cancelStream\n"
        << "  (books-seek-page / books-set-layout / books-get-chapters /\n"
        << "   books-open-chapter / books-tts-{play,pause,resume,stop,\n"
        << "   set-voice,set-speed} / books-get-listen-state — JS-resident;\n"
        << "   return structured JS_RESIDENT_NOT_IMPLEMENTED reply)\n"
        << "\n"
        << "  v1.5 sources-side bridge (Phase D.3, 2026-05-19):\n"
        << "  sources-search-tankorent <query> [--type videos|books|audiobooks|comics]\n"
        << "                           fire a Tankorent search\n"
        << "  sources-search-tankolibrary <query>\n"
        << "                           fire a TankoLibrary search (active media tab)\n"
        << "  sources-cancel-search    abort the active Tankorent search\n"
        << "  sources-get-tankorent-state    TankorentPage snapshot\n"
        << "  sources-get-tankolibrary-state TankoLibraryPage snapshot\n"
        << "  sources-get-indexer-health     per-indexer status, response time, error rate\n"
        << "  sources-force-indexer-refresh <indexer-id>\n"
        << "                           clear persisted health for an indexer\n"
        << "  sources-get-pending-downloads  current torrent queue\n"
        << "  sources-cancel-download <infoHash> [--delete-files]\n"
        << "                           cancel a queued torrent download\n"
        << "  sources-pause-torrent <infoHash>\n"
        << "  sources-resume-torrent <infoHash>\n"
        << "  sources-remove-torrent <infoHash> [--delete-files]\n"
        << "                           full TorrentClient lifecycle controls\n"
        << "  sources-add-magnet <magnet-uri> [--category cat] [--dest path]\n"
        << "  sources-add-url <url> [--category cat] [--dest path]\n"
        << "                           headless add via TorrentClient::addMagnetHeadless\n"
        << "  sources-set-speed-limits <down-bps> <up-bps> [--scope global|<infoHash>]\n"
        << "                           global or per-torrent speed limits\n"
        << "  sources-set-queue-limits <max-downloads> <max-uploads> <max-active>\n"
        << "  sources-get-tankolibrary-results\n"
        << "                           current BookResult list + selection state\n"
        << "  sources-open-tankolibrary-detail <md5>\n"
        << "                           navigate to the detail page for a search result\n"
        << "  sources-download-tankolibrary-selected\n"
        << "                           kick off download for the open detail\n"
        << "  sources-set-tankolibrary-filters <json>\n"
        << "                           media_tab / epub / pdf / mobi / english_only /\n"
        << "                           sort / audio_format -- pass JSON subset\n"
        << "\n"
        << "  v1.6 library-side bridge (Phase D.4, 2026-05-19):\n"
        << "  library-get-continue-reading <mode>\n"
        << "                           continue-reading strip state (mode = comics|books|videos|stream)\n"
        << "  library-get-recently-added <mode>     recently-added strip state\n"
        << "  library-get-search-state <mode>       library search overlay state\n"
        << "  library-get-scan-state <mode>         scanning state per mode\n"
        << "  library-trigger-scan <mode>           kick a triggerScan() on the mode's landing page\n"
        << "  library-get-root-folders <mode>       inspect the root folder list via CoreBridge\n"
        << "  library-get-active-layer <mode>       current sub-view layer for the mode\n"
        << "  library-reset-mode <mode>             reset the mode's topbar pill to root\n"
        << "  library-get-sort <mode>               per-mode sort combo key\n"
        << "  library-set-sort <mode> <key>         set the per-mode sort combo (UI-bound combo data)\n"
        << "  library-set-density <mode> <0|1|2>    set the tile density slider\n"
        << "  library-get-selected-items <mode>     per-mode selection roster (empty when no multi-select)\n"
        << "  library-apply-theme <theme-id>        runtime theme apply (dark|nord|solarized|gruvbox|catppuccin, noir alias dark)\n"
        << "  library-get-active-theme              current theme id\n"
        << "  library-get-active-mode-pill          which top-bar mode pill is active\n"
        << "  library-get-settings                  composite settings dump + writableKeys allowlist\n"
        << "  library-set-setting <key> <value>     allowlisted writes: theme.id, density.<mode>,\n"
        << "                                        sort_key.<mode>, search.query.<mode>\n"
        << "\n"
        << "  v1.7 player-side deeper bridge (Phase D.2, 2026-05-19):\n"
        << "  player-get-audio-tracks               list audio tracks + activeId\n"
        << "  player-get-subtitle-tracks            list subtitle tracks + activeId + visible flag\n"
        << "  player-select-audio-track <id>        switch active audio track (id from get-audio-tracks)\n"
        << "  player-select-subtitle-track <id>     switch subtitle (id can be -1 to disable)\n"
        << "  player-set-audio-delay <ms>           int ms; sidecar set_audio_delay\n"
        << "  player-set-sub-delay <ms>             int ms; sidecar set_sub_delay\n"
        << "  player-set-sub-size <delta>           double; +/- 0.1 = +/- 10%\n"
        << "  player-set-sub-position <pct>         0..100; 100 = bottom (mpv sub-pos parity)\n"
        << "  player-get-chapters                   chapter list from current file\n"
        << "  player-seek-chapter <id>              seek to chapter index (0-based)\n"
        << "  player-set-volume <0-200>             percent; 100 = unity, 200 = +6 dB amp\n"
        << "  player-set-speed <0.25-4.0>           double; sidecar set_rate\n"
        << "  player-get-hud-state                  HUD chips/title/controls visibility/fullscreen\n"
        << "  player-get-decoder-stats              codec + resolution + fps + zero-copy flag\n"
        << "  player-get-canvas-size                FrameCanvas pixel dimensions\n"
        << "  player-screenshot <path>              capture current frame to PNG path\n"
        << "  player-simulate-seek-drag <position>  drive SeekSlider value as if mouse-drag\n"
        << "  player-pause                          pause (no-op if already paused)\n"
        << "  player-resume                         resume (no-op if already playing)\n"
        << "  player-toggle-play                    flip pause state\n"
        << "  player-seek <seconds>                 absolute seek; double seconds\n"
        << "  player-frame-step <forward|back>      sidecar frame-step\n"
        << "  player-stop                           sidecar stop without closing player window\n"
        << "  player-set-mute <true|false>          mute toggle\n"
        << "  player-get-volume-state               volume + muted + audioDelayMs\n"
        << "  player-set-aspect <mode>              original|4:3|16:9|2.35:1|1.85:1\n"
        << "  player-set-crop <mode>                none|16:9|2.35:1|2.39:1|1.85:1|4:3\n"
        << "  player-get-loading-overlay            LoadingOverlay visibility + opacity\n"
        << "  player-get-buffering-state            streamStalled + sidecarBuffering\n"
        << "  player-get-keybindings                keybinding snapshot (full map deferred to v1.8)\n"
        << "  sidecar-get-process-state             pid + alive + sessionId + seq counter\n"
        << "  sidecar-get-current-stream-info       codec/width/height/fps from cached mediaInfo\n"
        << "  sidecar-get-decoder-queue             (NYI v1.7: needs sidecar push-event surface)\n"
        << "  sidecar-get-render-queue              (NYI v1.7: needs sidecar push-event surface)\n"
        << "  sidecar-restart                       graceful diagnostic restart via resetAndRestart()\n"
        << "  sidecar-get-ipc-latency               p50/p99/max per-command from live tracker\n"
        << "  subs-get-active-track                 active sub id + delay + position + size\n"
        << "  subs-get-positioning                  SubtitleOverlay full positioning state\n"
        << "  subs-get-fonts-loaded                 overlay font + sidecar-side font roster note\n"
        << "  osd-get-state                         loading/toast/volume/center/sub/stats overlay visibility\n"
        << "\n"
        << "  v1.8 synthetic UI interaction layer (Phase D.5, 2026-05-19):\n"
        << "  ui-query-widget <objectName>          {visible, enabled, geometry, text, className}\n"
        << "  ui-query-focus                        focused QWidget objectName + class\n"
        << "  ui-active-layer                       focused widget + parent chain + visible top-levels\n"
        << "  ui-list-widgets [filter] [--limit N]  glob over objectNames (default '*', limit 100)\n"
        << "  ui-dry-run <innerCmd> <objectName>    resolve target + planned event, no fire\n"
        << "  ui-click <objectName>                 animateClick / invokeMethod(click) / center mouse\n"
        << "  ui-keypress <objectName> <key>        e.g. Qt.Key_Down (or numeric Qt::Key)\n"
        << "  ui-text-input <objectName> <text>     QLineEdit/QTextEdit/QComboBox-editable setText\n"
        << "  ui-simulate-scroll <objectName> <delta>\n"
        << "                                        QWheelEvent angleDelta on widget center\n"
        << "  ui-simulate-mouse <objectName> <press|release|move|double-click> [x] [y]\n"
        << "                                        QMouseEvent at (x,y) or widget center\n"
        << "  ui-wait-for <condition> [--timeout ms]\n"
        << "                                        condition: <name>[:visible|:enabled|:text-matches:<regex>]\n"
        << "                                        default 5000ms, cap 30000ms\n"
        << "  ui-set-checkbox <objectName> <true|false>\n"
        << "  ui-set-combo <objectName> <value>     findText then setCurrentIndex; else setCurrentText\n"
        << "  ui-select-table-row <objectName> <row>\n"
        << "                                        QAbstractItemView setCurrentIndex(row,0)\n"
        << "  (write-capable ui-* commands require TANKOBAN_DEV_UI_SIM=1 on server env\n"
        << "   or return UI_SIM_DISABLED. Read-only commands: query-widget, query-focus,\n"
        << "   active-layer, list-widgets, dry-run.)\n"
        << "\n"
        << "  v1.9 system state + introspection layer (Phase D.6, 2026-05-19):\n"
        << "  app-get-active-modals          active modal + visible QDialog instances\n"
        << "  app-get-window-list            top-level windows + geometry + state flags\n"
        << "  app-get-shortcut-table         registered QShortcut bindings (key/owner/enabled)\n"
        << "  settings-get <key>             QSettings(\"Tankoban\") value lookup\n"
        << "  settings-set <key> <value>     write to QSettings (allowlist-gated, WRITE flag)\n"
        << "  settings-dump [group]          QSettings dump, optionally scoped to a group\n"
        << "  settings-reset <key>           remove a QSettings key (allowlist-gated, WRITE flag)\n"
        << "  jsonstore-get <path>           read JsonStore file (e.g. \"prefs.json\")\n"
        << "  jsonstore-set <path> <json>    write JsonStore file (JSON object, WRITE flag)\n"
        << "  cache-list                     known cache layers + per-layer clear support note\n"
        << "  cache-clear <layer>            clear a layer (WRITE flag; many layers unsupported)\n"
        << "  scanner-get-status             VideosScanner duration-cache state proxy\n"
        << "  scanner-list-watched           per-domain root-folders the scanners walk\n"
        << "  log-tail <component> [n]       last n lines of sidecar|telemetry|events|ipc|tankoctl\n"
        << "  log-grep <pattern> [maxPerFile]\n"
        << "                                 search recent log lines across all 4 streams\n"
        << "  log-mark <label>               THE unlock — write correlation marker into ALL\n"
        << "                                 4 active log streams simultaneously\n"
        << "  log-set-level <component> <level>\n"
        << "                                 record an in-memory log-level override (WRITE flag)\n"
        << "  events-tail [n]                last n parsed rows of out/events.jsonl\n"
        << "  theme-get-palette              current applied ThemePalette tokens + blob colors\n"
        << "  theme-get-applied-stylesheet [objectName]\n"
        << "                                 stylesheet on a target widget (or app default)\n"
        << "  theme-reload                   re-apply theme from QSettings (WRITE flag)\n"
        << "  font-list-loaded               QFontDatabase::families() snapshot\n"
        << "  perf-mark-start <label>        open a named timing region (in-memory)\n"
        << "  perf-mark-end <label>          close a region; reports elapsed + per-label total\n"
        << "  perf-dump-counters             all open + closed perf regions\n"
        << "  dev-inject-error <code> [note] register an in-memory fault-injection code (WRITE flag)\n"
        << "  dev-toggle-feature <flag> [true|false]\n"
        << "                                 toggle / set an in-memory feature flag (WRITE flag)\n"
        << "  (write-capable v1.9 commands require TANKOBAN_DEV_WRITE=1 on server env or return\n"
        << "   DEV_WRITE_DISABLED. This is a SEPARATE flag from D.5's TANKOBAN_DEV_UI_SIM.)\n"
        << "  Twelve spec-catalogue commands deferred — see DevControlServer.h v1.9 block.\n";
    tankoctl_scenario::printUsage(err);
}

int sendCommand(const QString& cmd, const QJsonObject& payload)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (!g_recordPath.isEmpty())
        tankoctl_scenario::appendRecordedStep(g_recordPath, cmd, payload);

    QLocalSocket sock;
    sock.connectToServer(QString::fromLatin1(kSocketName));
    if (!sock.waitForConnected(kConnectTimeoutMs)) {
        err << "ERROR: cannot connect to " << kSocketName
            << " — is Tankoban running with --dev-control?\n";
        return 2;
    }

    QJsonObject req;
    req["cmd"]     = cmd;
    req["seq"]     = 1;
    req["payload"] = payload;

    sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    if (!sock.waitForBytesWritten(kIoTimeoutMs)) {
        err << "ERROR: write timeout (" << kIoTimeoutMs << "ms)\n";
        return 3;
    }

    if (!sock.waitForReadyRead(kIoTimeoutMs)) {
        err << "ERROR: no reply within " << kIoTimeoutMs << "ms\n";
        return 3;
    }

    const QByteArray bytes = sock.readAll();
    out << bytes;
    if (!bytes.endsWith('\n'))
        out << '\n';

    const QJsonObject reply =
        QJsonDocument::fromJson(bytes.trimmed()).object();
    return reply.value("type").toString() == QLatin1String("error") ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStringList a = app.arguments();
    QTextStream err(stderr);

    if (a.size() < 2) {
        printUsage(err);
        return 64;
    }

    // P1 test-harness verbs (client-side orchestration over sendCommand).
    if (a[1] == QLatin1String("expect"))   return tankoctl_scenario::runExpect(a);
    if (a[1] == QLatin1String("run"))      return tankoctl_scenario::runScenario(a);
    if (a[1] == QLatin1String("wait-for")) return tankoctl_scenario::runWaitFor(a);

    // `record <file> <subcommand> [args...]` — capture the wrapped invocation's
    // wire form into a replayable scenario, then run it through the normal path.
    if (a[1] == QLatin1String("record")) {
        if (a.size() < 4) {
            err << "record requires <file> <subcommand> [args...]\n";
            return 64;
        }
        g_recordPath = a[2];
        a = QStringList{ a[0] } + a.mid(3);  // strip "record <file>"
    }

    const QString sub = a[1];

    // Map kebab-case subcommand to snake_case wire command.
    QString cmd = sub;
    cmd.replace('-', '_');

    QJsonObject payload;
    if (sub == QLatin1String("open-page")) {
        if (a.size() < 3) {
            err << "open-page requires <pageId>\n";
            return 64;
        }
        QString pageArg = a[2];
        if (pageArg == QLatin1String("theatre"))
            pageArg = QStringLiteral("stream");
        payload["pageId"] = pageArg;
    } else if (sub == QLatin1String("play-file")) {
        if (a.size() < 3) {
            err << "play-file requires <path>\n";
            return 64;
        }
        payload["path"] = a[2];
    } else if (sub == QLatin1String("logs") || sub == QLatin1String("get-videos")) {
        if (a.size() >= 3) {
            bool ok = false;
            const int n = a[2].toInt(&ok);
            if (!ok) {
                err << sub << " limit must be an integer (got '" << a[2] << "')\n";
                return 64;
            }
            payload["limit"] = n;
        }
    } else if (sub == QLatin1String("get-torrents")) {
        bool activeOnly = true;
        if (a.size() >= 3) {
            if (a[2] == QLatin1String("--all"))
                activeOnly = false;
            else if (a[2] == QLatin1String("--active"))
                activeOnly = true;
            else {
                err << "get-torrents accepts only --active or --all\n";
                return 64;
            }
        }
        payload["activeOnly"] = activeOnly;
    } else if (sub == QLatin1String("search")) {
        if (a.size() < 3) {
            err << "search requires <query>\n";
            return 64;
        }
        payload["query"] = a[2];
        for (int i = 3; i < a.size(); ++i) {
            if (a[i] != QLatin1String("--type") || i + 1 >= a.size()) {
                err << "search optional args: --type movie|series\n";
                return 64;
            }
            const QString type = a[++i];
            if (type != QLatin1String("movie") && type != QLatin1String("series")) {
                err << "search --type must be movie or series\n";
                return 64;
            }
            payload["type"] = type;
        }
    } else if (sub == QLatin1String("dispatch-episode")) {
        if (a.size() < 5) {
            err << "dispatch-episode requires <imdbId> <season> <episode>\n";
            return 64;
        }
        bool okSeason = false;
        bool okEpisode = false;
        const int season = a[3].toInt(&okSeason);
        const int episode = a[4].toInt(&okEpisode);
        if (!okSeason || !okEpisode) {
            err << "dispatch-episode season and episode must be integers\n";
            return 64;
        }
        payload["imdbId"] = a[2];
        payload["season"] = season;
        payload["episode"] = episode;
    } else if (sub == QLatin1String("dispatch-season")) {
        if (a.size() < 4) {
            err << "dispatch-season requires <imdbId> <season>\n";
            return 64;
        }
        bool okSeason = false;
        const int season = a[3].toInt(&okSeason);
        if (!okSeason) {
            err << "dispatch-season season must be an integer\n";
            return 64;
        }
        payload["imdbId"] = a[2];
        payload["season"] = season;
    } else if (sub == QLatin1String("dump-ui")) {
        if (a.size() >= 3)
            payload["pageId"] = a[2];
    } else if (sub == QLatin1String("comics-select-volume")) {
        if (a.size() < 3) {
            err << "comics-select-volume requires <row>\n";
            return 64;
        }
        bool ok = false;
        const int row = a[2].toInt(&ok);
        if (!ok) {
            err << "comics-select-volume row must be an integer\n";
            return 64;
        }
        payload["row"] = row;
    } else if (sub == QLatin1String("comics-open-series")) {
        if (a.size() < 3) {
            err << "comics-open-series requires <seriesId|anilistId>\n";
            return 64;
        }
        payload["seriesId"] = a[2];
    } else if (sub == QLatin1String("comics-open-chapter")) {
        if (a.size() < 5) {
            err << "comics-open-chapter requires <seriesId|anilistId> <volume> <chapter>\n";
            return 64;
        }
        bool okVol = false;
        bool okChapter = false;
        const int volume = a[3].toInt(&okVol);
        const int chapter = a[4].toInt(&okChapter);
        if (!okVol || !okChapter) {
            err << "comics-open-chapter volume and chapter must be integers\n";
            return 64;
        }
        payload["seriesId"] = a[2];
        payload["volume"] = volume;
        payload["chapter"] = chapter;
    } else if (sub == QLatin1String("comics-search-tankoyomi")) {
        if (a.size() < 3) {
            err << "comics-search-tankoyomi requires <query>\n";
            return 64;
        }
        payload["query"] = a[2];
        for (int i = 3; i < a.size(); ++i) {
            if (a[i] != QLatin1String("--timeout") || i + 1 >= a.size()) {
                err << "comics-search-tankoyomi optional args: --timeout ms\n";
                return 64;
            }
            bool ok = false;
            const int timeout = a[++i].toInt(&ok);
            if (!ok) {
                err << "comics-search-tankoyomi --timeout must be an integer\n";
                return 64;
            }
            payload["timeout"] = timeout;
        }
    } else if (sub == QLatin1String("comics-dispatch-volume")) {
        if (a.size() < 4) {
            err << "comics-dispatch-volume requires <seriesId|anilistId> <volume> [--source kind|index]\n";
            return 64;
        }
        bool okVol = false;
        const int volume = a[3].toInt(&okVol);
        if (!okVol) {
            err << "comics-dispatch-volume volume must be an integer\n";
            return 64;
        }
        payload["seriesId"] = a[2];
        payload["volume"] = volume;
        for (int i = 4; i < a.size(); ++i) {
            if (a[i] != QLatin1String("--source") || i + 1 >= a.size()) {
                err << "comics-dispatch-volume optional args: --source kind|index\n";
                return 64;
            }
            payload["source"] = a[++i];
        }
    } else if (sub == QLatin1String("comics-open-western-series")) {
        if (a.size() < 3) {
            err << "comics-open-western-series requires <seriesId>\n";
            return 64;
        }
        payload["seriesId"] = a[2];
    } else if (sub == QLatin1String("comics-download-western-edition")) {
        if (a.size() < 3) {
            err << "comics-download-western-edition requires <volumeNumber>\n";
            return 64;
        }
        bool ok = false;
        const int volume = a[2].toInt(&ok);
        if (!ok || volume <= 0) {
            err << "comics-download-western-edition volumeNumber must be a positive integer\n";
            return 64;
        }
        payload["volumeNumber"] = volume;
    } else if (sub == QLatin1String("comics-get-western-download-state")) {
        // volumeNumber is optional; omit to get all volumes.
        if (a.size() >= 3) {
            bool ok = false;
            const int volume = a[2].toInt(&ok);
            if (!ok || volume <= 0) {
                err << "comics-get-western-download-state volumeNumber must be a positive integer\n";
                return 64;
            }
            payload["volumeNumber"] = volume;
        }
    } else if (sub == QLatin1String("stream-open-detail")) {
        // v1.3 stream-side bridge expansion (Agent 4, 2026-05-19).
        if (a.size() < 3) {
            err << "stream-open-detail requires <imdbId>\n";
            return 64;
        }
        payload["imdbId"] = a[2];
    } else if (sub == QLatin1String("stream-direct-download")) {
        // v1.3 stream-side bridge expansion (Agent 4, 2026-05-19).
        if (a.size() < 3) {
            err << "stream-direct-download requires <sourceIndex>\n";
            return 64;
        }
        bool ok = false;
        const int sourceIndex = a[2].toInt(&ok);
        if (!ok || sourceIndex < 0) {
            err << "stream-direct-download sourceIndex must be a non-negative integer\n";
            return 64;
        }
        payload["sourceIndex"] = sourceIndex;
    } else if (sub == QLatin1String("books-search-library")) {
        // v1.3 books-side bridge (Phase D.1, 2026-05-19).
        if (a.size() < 3) {
            err << "books-search-library requires <query>\n";
            return 64;
        }
        payload["query"] = a[2];
    } else if (sub == QLatin1String("books-open-book")) {
        if (a.size() < 3) {
            err << "books-open-book requires <absPath>\n";
            return 64;
        }
        payload["path"] = a[2];
    } else if (sub == QLatin1String("books-open-series")) {
        if (a.size() < 3) {
            err << "books-open-series requires <seriesPath> or --title <name>\n";
            return 64;
        }
        if (a[2] == QLatin1String("--title")) {
            if (a.size() < 4) {
                err << "books-open-series --title requires <name>\n";
                return 64;
            }
            payload["title"] = a[3];
        } else {
            payload["seriesPath"] = a[2];
        }
    } else if (sub == QLatin1String("books-set-sort")) {
        if (a.size() < 3) {
            err << "books-set-sort requires <key> "
                   "(name_asc|name_desc|updated_desc|updated_asc|count_desc|count_asc)\n";
            return 64;
        }
        payload["key"] = a[2];
    } else if (sub == QLatin1String("books-set-density")) {
        if (a.size() < 3) {
            err << "books-set-density requires <0|1|2>\n";
            return 64;
        }
        bool ok = false;
        const int val = a[2].toInt(&ok);
        if (!ok || val < 0 || val > 2) {
            err << "books-set-density value must be 0, 1, or 2\n";
            return 64;
        }
        payload["value"] = val;
    } else if (sub == QLatin1String("books-seek-page")) {
        if (a.size() < 3) {
            err << "books-seek-page requires <n>\n";
            return 64;
        }
        bool ok = false;
        const int n = a[2].toInt(&ok);
        if (!ok || n < 0) {
            err << "books-seek-page n must be a non-negative integer\n";
            return 64;
        }
        payload["page"] = n;
    } else if (sub == QLatin1String("books-set-layout")) {
        if (a.size() < 3) {
            err << "books-set-layout requires <single|double-page|columns>\n";
            return 64;
        }
        const QString layout = a[2];
        if (layout != QLatin1String("single")
            && layout != QLatin1String("double-page")
            && layout != QLatin1String("columns")) {
            err << "books-set-layout must be single, double-page, or columns\n";
            return 64;
        }
        payload["layout"] = layout;
    } else if (sub == QLatin1String("books-open-chapter")) {
        if (a.size() < 3) {
            err << "books-open-chapter requires <id>\n";
            return 64;
        }
        payload["chapterId"] = a[2];
    } else if (sub == QLatin1String("books-tts-set-voice")) {
        if (a.size() < 3) {
            err << "books-tts-set-voice requires <voice>\n";
            return 64;
        }
        payload["voice"] = a[2];
    } else if (sub == QLatin1String("books-tts-set-speed")) {
        if (a.size() < 3) {
            err << "books-tts-set-speed requires <speed>\n";
            return 64;
        }
        bool ok = false;
        const double speed = a[2].toDouble(&ok);
        if (!ok) {
            err << "books-tts-set-speed speed must be a number\n";
            return 64;
        }
        payload["speed"] = speed;
    } else if (sub == QLatin1String("books-tts-cancel-stream")) {
        if (a.size() < 3) {
            err << "books-tts-cancel-stream requires <streamId>\n";
            return 64;
        }
        bool ok = false;
        const qulonglong streamId = a[2].toULongLong(&ok);
        if (!ok) {
            err << "books-tts-cancel-stream streamId must be a positive integer\n";
            return 64;
        }
        payload["streamId"] = static_cast<double>(streamId);
    } else if (sub == QLatin1String("sources-search-tankorent")) {
        // v1.5 Phase D.3 (2026-05-19) — Tankorent + TankoLibrary surface.
        if (a.size() < 3) {
            err << "sources-search-tankorent requires <query>\n";
            return 64;
        }
        payload["query"] = a[2];
        for (int i = 3; i < a.size(); ++i) {
            if (a[i] != QLatin1String("--type") || i + 1 >= a.size()) {
                err << "sources-search-tankorent optional args: --type videos|books|audiobooks|comics\n";
                return 64;
            }
            payload["type"] = a[++i];
        }
    } else if (sub == QLatin1String("sources-search-tankolibrary")) {
        if (a.size() < 3) {
            err << "sources-search-tankolibrary requires <query>\n";
            return 64;
        }
        payload["query"] = a[2];
    } else if (sub == QLatin1String("sources-force-indexer-refresh")) {
        if (a.size() < 3) {
            err << "sources-force-indexer-refresh requires <indexer-id>\n";
            return 64;
        }
        payload["indexerId"] = a[2];
    } else if (sub == QLatin1String("sources-cancel-download")
               || sub == QLatin1String("sources-remove-torrent")) {
        if (a.size() < 3) {
            err << sub << " requires <infoHash> [--delete-files]\n";
            return 64;
        }
        payload["infoHash"] = a[2];
        for (int i = 3; i < a.size(); ++i) {
            if (a[i] == QLatin1String("--delete-files")) {
                payload["deleteFiles"] = true;
            } else {
                err << sub << " optional args: --delete-files\n";
                return 64;
            }
        }
    } else if (sub == QLatin1String("sources-pause-torrent")
               || sub == QLatin1String("sources-resume-torrent")) {
        if (a.size() < 3) {
            err << sub << " requires <infoHash>\n";
            return 64;
        }
        payload["infoHash"] = a[2];
    } else if (sub == QLatin1String("sources-add-magnet")
               || sub == QLatin1String("sources-add-url")) {
        const QString key = (sub == QLatin1String("sources-add-url"))
            ? QStringLiteral("url")
            : QStringLiteral("magnet");
        if (a.size() < 3) {
            err << sub << " requires <" << key << "> [--category cat] [--dest path]\n";
            return 64;
        }
        payload[key] = a[2];
        for (int i = 3; i < a.size(); ++i) {
            if ((a[i] == QLatin1String("--category") && i + 1 < a.size())) {
                payload["category"] = a[++i];
            } else if (a[i] == QLatin1String("--dest") && i + 1 < a.size()) {
                payload["destinationPath"] = a[++i];
            } else {
                err << sub << " optional args: --category <cat> --dest <path>\n";
                return 64;
            }
        }
    } else if (sub == QLatin1String("sources-set-speed-limits")) {
        if (a.size() < 4) {
            err << "sources-set-speed-limits requires <down-bps> <up-bps> [--scope global|<infoHash>]\n";
            return 64;
        }
        bool okDl = false, okUl = false;
        const int dl = a[2].toInt(&okDl);
        const int ul = a[3].toInt(&okUl);
        if (!okDl || !okUl) {
            err << "sources-set-speed-limits down-bps and up-bps must be integers (0 = unlimited)\n";
            return 64;
        }
        payload["dlLimit"] = dl;
        payload["ulLimit"] = ul;
        for (int i = 4; i < a.size(); ++i) {
            if (a[i] == QLatin1String("--scope") && i + 1 < a.size()) {
                payload["scope"] = a[++i];
            } else {
                err << "sources-set-speed-limits optional args: --scope global|<infoHash>\n";
                return 64;
            }
        }
    } else if (sub == QLatin1String("sources-set-queue-limits")) {
        if (a.size() < 5) {
            err << "sources-set-queue-limits requires <max-downloads> <max-uploads> <max-active>\n";
            return 64;
        }
        bool okD = false, okU = false, okA = false;
        const int md = a[2].toInt(&okD);
        const int mu = a[3].toInt(&okU);
        const int mac = a[4].toInt(&okA);
        if (!okD || !okU || !okA) {
            err << "sources-set-queue-limits values must be integers\n";
            return 64;
        }
        payload["maxDownloads"] = md;
        payload["maxUploads"]   = mu;
        payload["maxActive"]    = mac;
    } else if (sub == QLatin1String("sources-open-tankolibrary-detail")) {
        if (a.size() < 3) {
            err << "sources-open-tankolibrary-detail requires <md5>\n";
            return 64;
        }
        payload["md5"] = a[2];
    } else if (sub == QLatin1String("sources-set-tankolibrary-filters")) {
        if (a.size() < 3) {
            err << "sources-set-tankolibrary-filters requires <json>\n";
            return 64;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(a[2].toUtf8());
        if (!doc.isObject()) {
            err << "sources-set-tankolibrary-filters: json must be an object\n";
            return 64;
        }
        payload = doc.object();
    } else if (sub == QLatin1String("library-get-continue-reading")
               || sub == QLatin1String("library-get-recently-added")
               || sub == QLatin1String("library-get-search-state")
               || sub == QLatin1String("library-get-scan-state")
               || sub == QLatin1String("library-trigger-scan")
               || sub == QLatin1String("library-get-root-folders")
               || sub == QLatin1String("library-get-active-layer")
               || sub == QLatin1String("library-reset-mode")
               || sub == QLatin1String("library-get-sort")
               || sub == QLatin1String("library-get-selected-items")) {
        // v1.6 Phase D.4 (2026-05-19) — library-side bridge cross-mode reads.
        if (a.size() < 3) {
            err << sub << " requires <mode> (comics|books|videos|stream)\n";
            return 64;
        }
        payload["mode"] = a[2];
    } else if (sub == QLatin1String("library-set-sort")) {
        if (a.size() < 4) {
            err << "library-set-sort requires <mode> <key>\n";
            return 64;
        }
        payload["mode"] = a[2];
        payload["key"]  = a[3];
    } else if (sub == QLatin1String("library-set-density")) {
        if (a.size() < 4) {
            err << "library-set-density requires <mode> <0|1|2>\n";
            return 64;
        }
        bool ok = false;
        const int v = a[3].toInt(&ok);
        if (!ok || v < 0 || v > 2) {
            err << "library-set-density value must be 0, 1, or 2\n";
            return 64;
        }
        payload["mode"]  = a[2];
        payload["value"] = v;
    } else if (sub == QLatin1String("library-apply-theme")) {
        if (a.size() < 3) {
            err << "library-apply-theme requires <theme-id> (dark|nord|solarized|gruvbox|catppuccin)\n";
            return 64;
        }
        payload["themeId"] = a[2];
    } else if (sub == QLatin1String("library-set-setting")) {
        if (a.size() < 4) {
            err << "library-set-setting requires <key> <value>\n";
            return 64;
        }
        payload["key"] = a[2];
        // value: try parsing as int first (density slots want int); otherwise
        // pass through as string. Theme + sort + search keys are strings, so
        // the fallback string path is correct for those.
        bool isInt = false;
        const int n = a[3].toInt(&isInt);
        if (isInt) payload["value"] = n;
        else       payload["value"] = a[3];
    } else if (sub == QLatin1String("player-select-audio-track")) {
        // v1.7 Phase D.2 (2026-05-19) — player-side deeper bridge.
        if (a.size() < 3) {
            err << "player-select-audio-track requires <id>\n";
            return 64;
        }
        payload["id"] = a[2];
    } else if (sub == QLatin1String("player-select-subtitle-track")) {
        if (a.size() < 3) {
            err << "player-select-subtitle-track requires <id> (-1 to disable)\n";
            return 64;
        }
        // id can be a numeric string ("-1", "0", "1") — pass through as
        // string; VideoPlayer parses to int with explicit -1 disable path.
        payload["id"] = a[2];
    } else if (sub == QLatin1String("player-set-audio-delay")
               || sub == QLatin1String("player-set-sub-delay")) {
        if (a.size() < 3) {
            err << sub << " requires <ms>\n";
            return 64;
        }
        bool ok = false;
        const int ms = a[2].toInt(&ok);
        if (!ok) { err << sub << " ms must be an integer\n"; return 64; }
        payload["ms"] = ms;
    } else if (sub == QLatin1String("player-set-sub-size")) {
        if (a.size() < 3) {
            err << "player-set-sub-size requires <delta> (e.g. 0.1 / -0.1)\n";
            return 64;
        }
        bool ok = false;
        const double d = a[2].toDouble(&ok);
        if (!ok) { err << "player-set-sub-size delta must be a number\n"; return 64; }
        payload["delta"] = d;
    } else if (sub == QLatin1String("player-set-sub-position")) {
        if (a.size() < 3) {
            err << "player-set-sub-position requires <pct> (0..100)\n";
            return 64;
        }
        bool ok = false;
        const int pct = a[2].toInt(&ok);
        if (!ok || pct < 0 || pct > 100) {
            err << "player-set-sub-position pct must be 0..100\n";
            return 64;
        }
        payload["pct"] = pct;
    } else if (sub == QLatin1String("player-seek-chapter")) {
        if (a.size() < 3) {
            err << "player-seek-chapter requires <id> (0-based index)\n";
            return 64;
        }
        bool ok = false;
        const int id = a[2].toInt(&ok);
        if (!ok || id < 0) {
            err << "player-seek-chapter id must be a non-negative integer\n";
            return 64;
        }
        payload["id"] = id;
    } else if (sub == QLatin1String("player-set-volume")) {
        if (a.size() < 3) {
            err << "player-set-volume requires <0-200>\n";
            return 64;
        }
        bool ok = false;
        const int v = a[2].toInt(&ok);
        if (!ok || v < 0 || v > 200) {
            err << "player-set-volume volume must be 0..200\n";
            return 64;
        }
        payload["volume"] = v;
    } else if (sub == QLatin1String("player-set-speed")) {
        if (a.size() < 3) {
            err << "player-set-speed requires <speed> (0.25..4.0)\n";
            return 64;
        }
        bool ok = false;
        const double s = a[2].toDouble(&ok);
        if (!ok || s < 0.25 || s > 4.0) {
            err << "player-set-speed speed must be 0.25..4.0\n";
            return 64;
        }
        payload["speed"] = s;
    } else if (sub == QLatin1String("player-screenshot")) {
        if (a.size() < 3) {
            err << "player-screenshot requires <path> (PNG output target)\n";
            return 64;
        }
        payload["path"] = a[2];
    } else if (sub == QLatin1String("player-simulate-seek-drag")) {
        if (a.size() < 3) {
            err << "player-simulate-seek-drag requires <position> (slider int)\n";
            return 64;
        }
        bool ok = false;
        const int p = a[2].toInt(&ok);
        if (!ok || p < 0) {
            err << "player-simulate-seek-drag position must be a non-negative integer\n";
            return 64;
        }
        payload["position"] = p;
    } else if (sub == QLatin1String("player-seek")) {
        if (a.size() < 3) {
            err << "player-seek requires <seconds>\n";
            return 64;
        }
        bool ok = false;
        const double s = a[2].toDouble(&ok);
        if (!ok || s < 0.0) {
            err << "player-seek seconds must be a non-negative number\n";
            return 64;
        }
        payload["seconds"] = s;
    } else if (sub == QLatin1String("player-frame-step")) {
        if (a.size() < 3) {
            err << "player-frame-step requires <forward|back>\n";
            return 64;
        }
        const QString dir = a[2];
        if (dir != QLatin1String("forward") && dir != QLatin1String("back")
            && dir != QLatin1String("backward")) {
            err << "player-frame-step direction must be forward or back\n";
            return 64;
        }
        payload["direction"] = dir;
    } else if (sub == QLatin1String("player-set-mute")) {
        if (a.size() < 3) {
            err << "player-set-mute requires <true|false>\n";
            return 64;
        }
        const QString v = a[2].toLower();
        if (v != QLatin1String("true") && v != QLatin1String("false")) {
            err << "player-set-mute argument must be true or false\n";
            return 64;
        }
        payload["muted"] = (v == QLatin1String("true"));
    } else if (sub == QLatin1String("player-set-aspect")
               || sub == QLatin1String("player-set-crop")) {
        if (a.size() < 3) {
            err << sub << " requires <mode>\n";
            return 64;
        }
        payload["mode"] = a[2];
    } else if (sub == QLatin1String("ui-query-widget")
               || sub == QLatin1String("ui-click")) {
        // v1.8 Phase D.5 (2026-05-19) — synthetic UI interaction layer.
        if (a.size() < 3) {
            err << sub << " requires <objectName>\n";
            return 64;
        }
        payload["objectName"] = a[2];
    } else if (sub == QLatin1String("ui-list-widgets")) {
        // ui-list-widgets [filter] [--limit N]
        if (a.size() >= 3 && !a[2].startsWith(QLatin1String("--")))
            payload["filter"] = a[2];
        for (int i = (a.size() >= 3 && !a[2].startsWith(QLatin1String("--"))) ? 3 : 2;
             i < a.size(); ++i) {
            if (a[i] == QLatin1String("--limit") && i + 1 < a.size()) {
                bool ok = false;
                const int n = a[++i].toInt(&ok);
                if (!ok || n <= 0) {
                    err << "ui-list-widgets --limit must be a positive integer\n";
                    return 64;
                }
                payload["limit"] = n;
            } else {
                err << "ui-list-widgets optional args: [filter] [--limit N]\n";
                return 64;
            }
        }
    } else if (sub == QLatin1String("ui-dry-run")) {
        // ui-dry-run <innerCmd> <objectName>
        if (a.size() < 4) {
            err << "ui-dry-run requires <innerCmd> <objectName>\n";
            return 64;
        }
        QString inner = a[2];
        inner.replace('-', '_');
        payload["innerCmd"] = inner;
        QJsonObject innerPayload;
        innerPayload["objectName"] = a[3];
        payload["innerPayload"] = innerPayload;
    } else if (sub == QLatin1String("ui-keypress")) {
        if (a.size() < 4) {
            err << "ui-keypress requires <objectName> <key> (e.g. Qt.Key_Down)\n";
            return 64;
        }
        payload["objectName"] = a[2];
        payload["key"] = a[3];
    } else if (sub == QLatin1String("ui-text-input")) {
        if (a.size() < 4) {
            err << "ui-text-input requires <objectName> <text>\n";
            return 64;
        }
        payload["objectName"] = a[2];
        payload["text"] = a[3];
    } else if (sub == QLatin1String("ui-simulate-scroll")) {
        if (a.size() < 4) {
            err << "ui-simulate-scroll requires <objectName> <delta>\n";
            return 64;
        }
        bool ok = false;
        const int delta = a[3].toInt(&ok);
        if (!ok) {
            err << "ui-simulate-scroll delta must be an integer\n";
            return 64;
        }
        payload["objectName"] = a[2];
        payload["delta"] = delta;
    } else if (sub == QLatin1String("ui-simulate-mouse")) {
        if (a.size() < 4) {
            err << "ui-simulate-mouse requires <objectName> <press|release|move|double-click> [x] [y]\n";
            return 64;
        }
        const QString ev = a[3];
        if (ev != QLatin1String("press") && ev != QLatin1String("release")
            && ev != QLatin1String("move") && ev != QLatin1String("double-click")) {
            err << "ui-simulate-mouse eventType must be press|release|move|double-click\n";
            return 64;
        }
        payload["objectName"] = a[2];
        payload["eventType"] = ev;
        if (a.size() >= 5) {
            bool ok = false;
            const int x = a[4].toInt(&ok);
            if (!ok) { err << "ui-simulate-mouse x must be an integer\n"; return 64; }
            payload["x"] = x;
        }
        if (a.size() >= 6) {
            bool ok = false;
            const int y = a[5].toInt(&ok);
            if (!ok) { err << "ui-simulate-mouse y must be an integer\n"; return 64; }
            payload["y"] = y;
        }
    } else if (sub == QLatin1String("ui-wait-for")) {
        if (a.size() < 3) {
            err << "ui-wait-for requires <condition> [--timeout ms]\n";
            return 64;
        }
        payload["condition"] = a[2];
        for (int i = 3; i < a.size(); ++i) {
            if (a[i] == QLatin1String("--timeout") && i + 1 < a.size()) {
                bool ok = false;
                const int ms = a[++i].toInt(&ok);
                if (!ok || ms < 0) {
                    err << "ui-wait-for --timeout must be a non-negative integer\n";
                    return 64;
                }
                payload["timeoutMs"] = ms;
            } else {
                err << "ui-wait-for optional args: --timeout <ms>\n";
                return 64;
            }
        }
    } else if (sub == QLatin1String("ui-set-checkbox")) {
        if (a.size() < 4) {
            err << "ui-set-checkbox requires <objectName> <true|false>\n";
            return 64;
        }
        const QString v = a[3].toLower();
        if (v != QLatin1String("true") && v != QLatin1String("false")) {
            err << "ui-set-checkbox value must be true or false\n";
            return 64;
        }
        payload["objectName"] = a[2];
        payload["checked"] = (v == QLatin1String("true"));
    } else if (sub == QLatin1String("ui-set-combo")) {
        if (a.size() < 4) {
            err << "ui-set-combo requires <objectName> <value>\n";
            return 64;
        }
        payload["objectName"] = a[2];
        payload["value"] = a[3];
    } else if (sub == QLatin1String("ui-select-table-row")) {
        if (a.size() < 4) {
            err << "ui-select-table-row requires <objectName> <row>\n";
            return 64;
        }
        bool ok = false;
        const int row = a[3].toInt(&ok);
        if (!ok || row < 0) {
            err << "ui-select-table-row row must be a non-negative integer\n";
            return 64;
        }
        payload["objectName"] = a[2];
        payload["row"] = row;
    }
    // ── v1.9 system state + introspection layer (Phase D.6, 2026-05-19) ─────
    // 17 commands take args; 10 take none and route through the no-payload
    // OR-chain below. Wire format is snake_case; CLI is the natural kebab-
    // case (the main.cpp `cmd.replace('-', '_')` line handles the mapping).
    else if (sub == QLatin1String("lease-acquire")) {
        if (a.size() < 3) {
            err << "lease-acquire requires <lane> --holder <agent-id> --purpose <text> --ttl-sec <n>\n";
            return 64;
        }
        payload["lane"] = a[2];
        bool haveHolder = false;
        bool havePurpose = false;
        bool haveTtl = false;
        for (int i = 3; i < a.size(); ++i) {
            if (a[i] == QLatin1String("--holder") && i + 1 < a.size()) {
                payload["holder"] = a[++i];
                haveHolder = true;
            } else if (a[i] == QLatin1String("--purpose") && i + 1 < a.size()) {
                payload["purpose"] = a[++i];
                havePurpose = true;
            } else if (a[i] == QLatin1String("--ttl-sec") && i + 1 < a.size()) {
                bool ok = false;
                const int ttl = a[++i].toInt(&ok);
                if (!ok || ttl <= 0) {
                    err << "lease-acquire --ttl-sec must be a positive integer\n";
                    return 64;
                }
                payload["ttl_sec"] = ttl;
                haveTtl = true;
            } else {
                err << "lease-acquire args: <lane> --holder <agent-id> --purpose <text> --ttl-sec <n>\n";
                return 64;
            }
        }
        if (!haveHolder || !havePurpose || !haveTtl) {
            err << "lease-acquire requires --holder, --purpose, and --ttl-sec\n";
            return 64;
        }
    } else if (sub == QLatin1String("lease-release")) {
        if (a.size() < 5 || a[3] != QLatin1String("--token")) {
            err << "lease-release requires <lane> --token <token>\n";
            return 64;
        }
        payload["lane"] = a[2];
        payload["token"] = a[4];
    } else if (sub == QLatin1String("lease-heartbeat")) {
        if (a.size() < 5) {
            err << "lease-heartbeat requires <lane> --token <token> [--ttl-sec <n>]\n";
            return 64;
        }
        payload["lane"] = a[2];
        bool haveToken = false;
        for (int i = 3; i < a.size(); ++i) {
            if (a[i] == QLatin1String("--token") && i + 1 < a.size()) {
                payload["token"] = a[++i];
                haveToken = true;
            } else if (a[i] == QLatin1String("--ttl-sec") && i + 1 < a.size()) {
                bool ok = false;
                const int ttl = a[++i].toInt(&ok);
                if (!ok || ttl <= 0) {
                    err << "lease-heartbeat --ttl-sec must be a positive integer\n";
                    return 64;
                }
                payload["ttl_sec"] = ttl;
            } else {
                err << "lease-heartbeat args: <lane> --token <token> [--ttl-sec <n>]\n";
                return 64;
            }
        }
        if (!haveToken) {
            err << "lease-heartbeat requires --token <token>\n";
            return 64;
        }
    } else if (sub == QLatin1String("lease-get")) {
        if (a.size() < 3) {
            err << "lease-get requires <lane>\n";
            return 64;
        }
        payload["lane"] = a[2];
    }
    else if (sub == QLatin1String("settings-get")
          || sub == QLatin1String("settings-reset")) {
        if (a.size() < 3) {
            err << sub << " requires <key>\n";
            return 64;
        }
        payload["key"] = a[2];
    } else if (sub == QLatin1String("settings-set")) {
        if (a.size() < 4) {
            err << "settings-set requires <key> <value>\n";
            return 64;
        }
        payload["key"] = a[2];
        payload["value"] = a[3];
    } else if (sub == QLatin1String("settings-dump")) {
        if (a.size() >= 3) payload["group"] = a[2];
    } else if (sub == QLatin1String("jsonstore-get")) {
        if (a.size() < 3) {
            err << "jsonstore-get requires <path>\n";
            return 64;
        }
        payload["path"] = a[2];
    } else if (sub == QLatin1String("jsonstore-set")) {
        if (a.size() < 4) {
            err << "jsonstore-set requires <path> <jsonObject>\n";
            return 64;
        }
        const QByteArray rawJson = a[3].toUtf8();
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(rawJson, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            err << "jsonstore-set <jsonObject> must parse as a JSON object (parse error: "
                << perr.errorString() << ")\n";
            return 64;
        }
        payload["path"]  = a[2];
        payload["value"] = doc.object();
    } else if (sub == QLatin1String("cache-clear")) {
        if (a.size() < 3) {
            err << "cache-clear requires <layer> (see cache-list for catalogue)\n";
            return 64;
        }
        payload["layer"] = a[2];
    } else if (sub == QLatin1String("log-tail")) {
        if (a.size() < 3) {
            err << "log-tail requires <component> [n]\n";
            return 64;
        }
        payload["component"] = a[2];
        if (a.size() >= 4) {
            bool ok = false;
            const int n = a[3].toInt(&ok);
            if (!ok || n <= 0) {
                err << "log-tail n must be a positive integer\n";
                return 64;
            }
            payload["n"] = n;
        }
    } else if (sub == QLatin1String("log-grep")) {
        if (a.size() < 3) {
            err << "log-grep requires <pattern> [maxPerFile]\n";
            return 64;
        }
        payload["pattern"] = a[2];
        if (a.size() >= 4) {
            bool ok = false;
            const int m = a[3].toInt(&ok);
            if (!ok || m <= 0) {
                err << "log-grep maxPerFile must be a positive integer\n";
                return 64;
            }
            payload["maxPerFile"] = m;
        }
    } else if (sub == QLatin1String("log-mark")) {
        if (a.size() < 3) {
            err << "log-mark requires <label> — written to all 4 active log streams\n";
            return 64;
        }
        payload["label"] = a[2];
    } else if (sub == QLatin1String("log-set-level")) {
        if (a.size() < 4) {
            err << "log-set-level requires <component> <level>\n";
            return 64;
        }
        payload["component"] = a[2];
        payload["level"]     = a[3];
    } else if (sub == QLatin1String("events-tail")) {
        if (a.size() >= 3) {
            bool ok = false;
            const int n = a[2].toInt(&ok);
            if (!ok || n <= 0) {
                err << "events-tail n must be a positive integer\n";
                return 64;
            }
            payload["n"] = n;
        }
    } else if (sub == QLatin1String("theme-get-applied-stylesheet")) {
        if (a.size() >= 3) payload["objectName"] = a[2];
    } else if (sub == QLatin1String("perf-mark-start")
            || sub == QLatin1String("perf-mark-end")) {
        if (a.size() < 3) {
            err << sub << " requires <label>\n";
            return 64;
        }
        payload["label"] = a[2];
    } else if (sub == QLatin1String("dev-inject-error")) {
        if (a.size() < 3) {
            err << "dev-inject-error requires <code> [note]\n";
            return 64;
        }
        payload["code"] = a[2];
        if (a.size() >= 4) payload["note"] = a[3];
    } else if (sub == QLatin1String("dev-toggle-feature")) {
        if (a.size() < 3) {
            err << "dev-toggle-feature requires <flag> [true|false] (omit to flip)\n";
            return 64;
        }
        payload["flag"] = a[2];
        if (a.size() >= 4) {
            const QString v = a[3].toLower();
            if (v != QLatin1String("true") && v != QLatin1String("false")) {
                err << "dev-toggle-feature value must be true or false\n";
                return 64;
            }
            payload["value"] = (v == QLatin1String("true"));
        }
    } else if (sub == QLatin1String("net-block-host")) {
        if (a.size() < 3) {
            err << "net-block-host requires <host>\n";
            return 64;
        }
        payload["host"] = a[2];
    } else if (sub == QLatin1String("net-unblock-host")) {
        if (a.size() < 3) {
            err << "net-unblock-host requires <host>\n";
            return 64;
        }
        payload["host"] = a[2];
    } else if (sub == QLatin1String("net-throttle-set")) {
        if (a.size() < 4) {
            err << "net-throttle-set requires <host|global> <latency-ms>\n";
            return 64;
        }
        payload["host"] = a[2];
        bool ok = false;
        const int ms = a[3].toInt(&ok);
        if (!ok || ms < 0) {
            err << "net-throttle-set latency-ms must be a non-negative integer\n";
            return 64;
        }
        payload["latencyMs"] = ms;
    } else if (sub == QLatin1String("net-throttle-clear")) {
        if (a.size() < 3) {
            err << "net-throttle-clear requires <host|global>\n";
            return 64;
        }
        payload["host"] = a[2];
    } else if (sub == QLatin1String("ping") || sub == QLatin1String("get-state")
               || sub == QLatin1String("scan-videos") || sub == QLatin1String("close-player")
               || sub == QLatin1String("get-player") || sub == QLatin1String("get-library")
               || sub == QLatin1String("get-downloads")
               || sub == QLatin1String("get-bulk-groups")
               || sub == QLatin1String("comics-get-state")
               || sub == QLatin1String("comics-get-library")
               || sub == QLatin1String("comics-get-series")
               || sub == QLatin1String("comics-get-downloads")
               || sub == QLatin1String("comics-get-sources")
               || sub == QLatin1String("stream-get-sources")
               || sub == QLatin1String("books-get-state")
               || sub == QLatin1String("books-get-library")
               || sub == QLatin1String("books-refresh-library")
               || sub == QLatin1String("books-clear-search")
               || sub == QLatin1String("books-get-series-state")
               || sub == QLatin1String("books-get-progress")
               || sub == QLatin1String("books-get-chapters")
               || sub == QLatin1String("books-tts-state")
               || sub == QLatin1String("books-get-listen-state")
               || sub == QLatin1String("books-tts-play")
               || sub == QLatin1String("books-tts-pause")
               || sub == QLatin1String("books-tts-resume")
               || sub == QLatin1String("books-tts-stop")
               // v1.5 sources-side bridge (Phase D.3, 2026-05-19).
               || sub == QLatin1String("sources-cancel-search")
               || sub == QLatin1String("sources-get-tankorent-state")
               || sub == QLatin1String("sources-get-tankolibrary-state")
               || sub == QLatin1String("sources-get-indexer-health")
               || sub == QLatin1String("sources-get-pending-downloads")
               || sub == QLatin1String("sources-get-tankolibrary-results")
               || sub == QLatin1String("sources-download-tankolibrary-selected")
               // v1.6 library-side bridge — no-payload globals.
               || sub == QLatin1String("library-get-active-theme")
               || sub == QLatin1String("library-get-active-mode-pill")
               || sub == QLatin1String("library-get-settings")
               // v1.7 player-side deeper bridge — no-payload reads + lifecycle controls.
               || sub == QLatin1String("player-get-audio-tracks")
               || sub == QLatin1String("player-get-subtitle-tracks")
               || sub == QLatin1String("player-get-chapters")
               || sub == QLatin1String("player-get-hud-state")
               || sub == QLatin1String("player-get-decoder-stats")
               || sub == QLatin1String("player-get-canvas-size")
               || sub == QLatin1String("player-pause")
               || sub == QLatin1String("player-resume")
               || sub == QLatin1String("player-toggle-play")
               || sub == QLatin1String("player-stop")
               || sub == QLatin1String("player-get-volume-state")
               || sub == QLatin1String("player-get-loading-overlay")
               || sub == QLatin1String("player-get-buffering-state")
               || sub == QLatin1String("player-get-keybindings")
               || sub == QLatin1String("sidecar-get-process-state")
               || sub == QLatin1String("sidecar-get-current-stream-info")
               || sub == QLatin1String("sidecar-get-decoder-queue")
               || sub == QLatin1String("sidecar-get-render-queue")
               || sub == QLatin1String("sidecar-restart")
               || sub == QLatin1String("sidecar-get-ipc-latency")
               || sub == QLatin1String("subs-get-active-track")
               || sub == QLatin1String("subs-get-positioning")
               || sub == QLatin1String("subs-get-fonts-loaded")
               || sub == QLatin1String("osd-get-state")
               // v1.8 synthetic UI layer — no-payload reads.
               || sub == QLatin1String("ui-query-focus")
               || sub == QLatin1String("ui-active-layer")
               // v1.9 system state + introspection — no-payload commands.
               || sub == QLatin1String("app-get-active-modals")
               || sub == QLatin1String("app-get-window-list")
               || sub == QLatin1String("app-get-shortcut-table")
               || sub == QLatin1String("cache-list")
               || sub == QLatin1String("cache-get-stats")
               || sub == QLatin1String("scanner-get-status")
               || sub == QLatin1String("scanner-list-watched")
               || sub == QLatin1String("theme-get-palette")
               || sub == QLatin1String("theme-reload")
               || sub == QLatin1String("font-list-loaded")
               || sub == QLatin1String("perf-dump-counters")
               // v1.10 lease registry.
               || sub == QLatin1String("lease-list")
               // v1.12 network observability (Congress 9).
               || sub == QLatin1String("net-list-requests")
               || sub == QLatin1String("net-list-rules")) {
        // No payload args.
    } else {
        err << "unknown subcommand: " << sub << "\n\n";
        printUsage(err);
        return 64;
    }

    return sendCommand(cmd, payload);
}
