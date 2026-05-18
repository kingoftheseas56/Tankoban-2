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
        << "  comics-get-sources       current Comics sources panel snapshot\n";
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
    } else if (sub == QLatin1String("ping") || sub == QLatin1String("get-state")
               || sub == QLatin1String("scan-videos") || sub == QLatin1String("close-player")
               || sub == QLatin1String("get-player") || sub == QLatin1String("get-library")
               || sub == QLatin1String("get-downloads")
               || sub == QLatin1String("get-bulk-groups")
               || sub == QLatin1String("comics-get-state")
               || sub == QLatin1String("comics-get-library")
               || sub == QLatin1String("comics-get-series")
               || sub == QLatin1String("comics-get-downloads")
               || sub == QLatin1String("comics-get-sources")) {
        // No payload args.
    } else {
        err << "unknown subcommand: " << sub << "\n\n";
        printUsage(err);
        return 64;
    }

    return sendCommand(cmd, payload);
}
