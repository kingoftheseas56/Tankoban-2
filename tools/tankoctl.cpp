// tankoctl — REPO_HYGIENE Phase 3 console client for the Tankoban dev-control
// bridge (DevControlServer at src/devtools/DevControlServer.{h,cpp}).
//
// Usage:
//   tankoctl ping
//   tankoctl get-state
//   tankoctl open-page <comics|books|videos|stream|sources>
//   tankoctl scan-videos
//   tankoctl get-videos [limit]
//   tankoctl play-file <path>
//   tankoctl close-player
//   tankoctl get-player
//   tankoctl logs [limit]
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
constexpr int kIoTimeoutMs      = 5000;

void printUsage(QTextStream& err)
{
    err << "usage: tankoctl <subcommand> [args...]\n"
        << "\n"
        << "  ping                     liveness probe (returns schema + commands)\n"
        << "  get-state                MainWindow snapshot\n"
        << "  open-page <pageId>       activate page (comics/books/videos/stream/sources)\n"
        << "  scan-videos              trigger VideosPage rescan\n"
        << "  get-videos [limit]       VideosPage snapshot (default limit 50)\n"
        << "  play-file <path>         open VideoPlayer on path\n"
        << "  close-player             close VideoPlayer\n"
        << "  get-player               VideoPlayer snapshot or null\n"
        << "  logs [limit]             ring buffer entries (default limit 100)\n";
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
        payload["pageId"] = a[2];
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
    } else if (sub == QLatin1String("ping") || sub == QLatin1String("get-state")
               || sub == QLatin1String("scan-videos") || sub == QLatin1String("close-player")
               || sub == QLatin1String("get-player")) {
        // No payload args.
    } else {
        err << "unknown subcommand: " << sub << "\n\n";
        printUsage(err);
        return 64;
    }

    return sendCommand(cmd, payload);
}
