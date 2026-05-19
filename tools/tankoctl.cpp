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

namespace {

constexpr const char* kSocketName = "TankobanDevControl";
constexpr int kConnectTimeoutMs = 1000;
constexpr int kIoTimeoutMs      = 60000;

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
        << "                                        sort_key.<mode>, search.query.<mode>\n";
}

int sendCommand(const QString& cmd, const QJsonObject& payload)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

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
    const QStringList a = app.arguments();
    QTextStream err(stderr);

    if (a.size() < 2) {
        printUsage(err);
        return 64;
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
               || sub == QLatin1String("library-get-settings")) {
        // No payload args.
    } else {
        err << "unknown subcommand: " << sub << "\n\n";
        printUsage(err);
        return 64;
    }

    return sendCommand(cmd, payload);
}
