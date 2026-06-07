# Theatre rqbit Revival — Phase 1 Implementation Plan (engine + streaming)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make clicking a Theatre episode **stream** it via a headless rqbit subprocess (plays while downloading), instead of auto-downloading via libtorrent.

**Architecture:** New `src/core/stream/rqbit/` layer — `RqbitProcess` (subprocess lifecycle), `RqbitClient` (HTTP REST to rqbit's API), `RqbitEngine` (orchestrator). A restored, trimmed `StreamPlayerController` opens the player on rqbit's `GET /torrents/{id}/stream/{idx}` URL. The existing auto-pick (`AutoSourcePicker`, top Torrentio source) supplies the magnet for Phase 1; the Sources pane (manual pick) is Phase 2. libtorrent downloads, the frozen `TorrentEngine`, and Comics/Tankorent paths are untouched.

**Tech Stack:** C++17 / Qt6 (Core, Network), CMake+Ninja+vcpkg, GoogleTest (`tankoban_tests`), rqbit prebuilt Rust binary (HTTP API). Spec: `docs/superpowers/specs/2026-06-07-theatre-rqbit-revival-design.md`. Restore reference commit: `eeedc92`.

**Reference (deleted Stremio classes to model on — pull with git):**
- `git show 64213b5^:src/core/stream/stremio/StreamServerProcess.cpp` (and `.h`) — subprocess template for `RqbitProcess`
- `git show 64213b5^:src/core/stream/stremio/StreamServerClient.cpp` (and `.h`) — REST-client template for `RqbitClient`
- `git show 64213b5^:src/core/stream/stremio/StreamServerEngine.cpp` (and `.h`) — orchestrator template for `RqbitEngine`
- `git show 64213b5^:src/ui/pages/stream/StreamPlayerController.cpp` (and `.h`) — controller to restore

---

## File Structure

**Create:**
- `resources/rqbit/rqbit.exe` — vendored prebuilt Windows binary (not in git LFS; gitignored like the sidecar, deployed by build)
- `docs/superpowers/specs/rqbit-api-contract.md` — captured exact CLI flags + JSON shapes (Task 1 output; reference for later tasks)
- `src/core/stream/rqbit/RqbitClient.h` / `.cpp` — REST adapter (+ pure `RqbitStats` parse, `pickPrimaryVideoFile` helper)
- `src/core/stream/rqbit/RqbitProcess.h` / `.cpp` — subprocess lifecycle
- `src/core/stream/rqbit/RqbitEngine.h` / `.cpp` — orchestrator
- `src/ui/pages/stream/StreamPlayerController.h` / `.cpp` — restored, trimmed for rqbit
- `tests/core/stream/test_rqbit_client.cpp` — unit tests (JSON parse + file pick)

**Modify:**
- `cmake/TankobanSources.cmake` — add the 4 new source pairs to the app target
- `CMakeLists.txt` — POST_BUILD copy of `resources/rqbit/` beside `Tankoban.exe`; register `test_rqbit_client.cpp` in `tankoban_tests`
- `src/ui/pages/StreamPage.cpp` — route the episode-play trigger to `RqbitEngine::startStream` + open `StreamPlayerController`
- `src/ui/pages/StreamPage.h` — own `RqbitEngine*` + `StreamPlayerController*` members

---

## Task 1: Vendor + ground-truth the rqbit binary

**Files:**
- Create: `resources/rqbit/rqbit.exe`, `docs/superpowers/specs/rqbit-api-contract.md`

- [ ] **Step 1: Download the prebuilt Windows binary**

Get the latest Windows x64 release from `https://github.com/ikatson/rqbit/releases` (file named like `rqbit-windows-x86_64.exe`). Save as `resources/rqbit/rqbit.exe`.

Run: `out\..\resources\rqbit\rqbit.exe --version`
Expected: prints a version (e.g. `rqbit 8.x.x`).

- [ ] **Step 2: Capture the server CLI flags**

Run: `resources\rqbit\rqbit.exe server start --help`
Record into `docs/superpowers/specs/rqbit-api-contract.md`: the exact flag for the HTTP listen address (expected `--http-api-listen-addr <ip:port>`) and the positional download-dir arg. Confirm headless server command shape:
`rqbit.exe --http-api-listen-addr 127.0.0.1:3030 server start C:\temp\rqbit_probe`

- [ ] **Step 3: Probe the HTTP API and capture exact JSON**

Start the server (Step 2 command) in one shell. In another, capture real responses into the contract doc:
```
curl -s -X POST -d "magnet:?xt=urn:btih:<a well-seeded test infohash>" http://127.0.0.1:3030/torrents
curl -s http://127.0.0.1:3030/torrents
curl -s http://127.0.0.1:3030/torrents/0/stats/v1
curl -s -I "http://127.0.0.1:3030/torrents/0/stream/0" -H "Range: bytes=0-1023"
```
Record verbatim in `rqbit-api-contract.md`: (a) the add-torrent response shape + the **id field name** (numeric `id` vs `info_hash`); (b) whether the magnet goes in the body (raw) or as JSON/query; (c) the stats JSON field names for **progress** (bytes done / total, or a percentage) and **state**; (d) the file list location (in add-response or stats) with `name`/`length`/index so file-selection can pick the largest video; (e) that the stream endpoint returns `206 Partial Content` for a Range request.

- [ ] **Step 4: Commit the binary + contract**

```bash
git add resources/rqbit/.gitkeep docs/superpowers/specs/rqbit-api-contract.md
git commit -m "chore(theatre): vendor rqbit binary location + capture API contract (rqbit phase1 T1)"
```
(Add `resources/rqbit/*.exe` to `.gitignore` — the exe is a build-deployed artifact, not committed, mirroring the sidecar. Commit a `.gitkeep` so the dir exists.)

---

## Task 2: `RqbitClient` — stats parse + file-pick (pure logic, TDD)

**Files:**
- Create: `src/core/stream/rqbit/RqbitClient.h`, `tests/core/stream/test_rqbit_client.cpp`
- Modify: later (the QNAM calls) in Task 3

These two functions are pure (no network) so they are unit-tested first. **Use the exact field names captured in Task 1** — the JSON below uses the documented shape; adjust the keys in both the test fixture and the parser to match the contract doc.

- [ ] **Step 1: Write the failing test**

`tests/core/stream/test_rqbit_client.cpp`:
```cpp
#include <gtest/gtest.h>
#include "core/stream/rqbit/RqbitClient.h"
#include <QJsonDocument>

using tankostream::rqbit::RqbitClient;
using tankostream::rqbit::RqbitStats;

static QJsonObject obj(const char* json) {
    return QJsonDocument::fromJson(json).object();
}

TEST(RqbitClientTest, ParsesProgressAndStateFromStats) {
    // Shape per rqbit-api-contract.md (Task 1). Adjust keys to the captured contract.
    const auto j = obj(R"({"state":"live","total_bytes":1000,"progress_bytes":250,"finished":false})");
    const RqbitStats s = RqbitClient::parseStats(j);
    EXPECT_EQ(s.totalBytes, 1000);
    EXPECT_EQ(s.downloadedBytes, 250);
    EXPECT_NEAR(s.progressFraction(), 0.25, 1e-6);
    EXPECT_FALSE(s.finished);
}

TEST(RqbitClientTest, PicksLargestVideoFile) {
    // files array per contract: name + length + index
    const auto files = QJsonDocument::fromJson(
        R"([{"name":"readme.txt","length":10},
            {"name":"Show.S01E01.mkv","length":900},
            {"name":"sample.mkv","length":50}])").array();
    EXPECT_EQ(RqbitClient::pickPrimaryVideoFile(files), 1);
}

TEST(RqbitClientTest, PickReturnsMinusOneWhenNoVideo) {
    const auto files = QJsonDocument::fromJson(
        R"([{"name":"a.txt","length":10},{"name":"b.nfo","length":5}])").array();
    EXPECT_EQ(RqbitClient::pickPrimaryVideoFile(files), -1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build out --config Release --target tankoban_tests` then `out\tankoban_tests.exe --gtest_filter=RqbitClientTest.*`
Expected: FAIL to compile — `RqbitClient.h` not found. (After Task 6 registers the test; until then, expect a build error on the missing header — that is the failing state.)

- [ ] **Step 3: Write the header with the pure helpers**

`src/core/stream/rqbit/RqbitClient.h`:
```cpp
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace tankostream::rqbit {

struct RqbitStats {
    QString state;
    qint64  totalBytes = 0;
    qint64  downloadedBytes = 0;
    bool    finished = false;
    double  progressFraction() const {
        return totalBytes > 0 ? double(downloadedBytes) / double(totalBytes) : 0.0;
    }
};

class RqbitClient : public QObject {
    Q_OBJECT
public:
    explicit RqbitClient(QObject* parent = nullptr) : QObject(parent) {}

    // Pure parsers (unit-tested; no network). Field names per rqbit-api-contract.md.
    static RqbitStats parseStats(const QJsonObject& statsJson);
    // Returns the index of the largest file with a video extension, or -1.
    static int pickPrimaryVideoFile(const QJsonArray& files);

    // Network methods land in Task 3.
};

} // namespace tankostream::rqbit
```

- [ ] **Step 4: Implement the pure helpers**

`src/core/stream/rqbit/RqbitClient.cpp` (create; network methods added Task 3):
```cpp
#include "core/stream/rqbit/RqbitClient.h"
#include <QFileInfo>
#include <QStringList>

namespace tankostream::rqbit {

RqbitStats RqbitClient::parseStats(const QJsonObject& j) {
    RqbitStats s;
    s.state           = j.value(QStringLiteral("state")).toString();
    s.totalBytes      = j.value(QStringLiteral("total_bytes")).toVariant().toLongLong();
    s.downloadedBytes = j.value(QStringLiteral("progress_bytes")).toVariant().toLongLong();
    s.finished        = j.value(QStringLiteral("finished")).toBool();
    return s;
}

int RqbitClient::pickPrimaryVideoFile(const QJsonArray& files) {
    static const QStringList kVideoExt = {
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("webm"),
        QStringLiteral("ts"),  QStringLiteral("flv")
    };
    int best = -1; qint64 bestLen = -1;
    for (int i = 0; i < files.size(); ++i) {
        const QJsonObject f = files.at(i).toObject();
        const QString name = f.value(QStringLiteral("name")).toString();
        const QString ext  = QFileInfo(name).suffix().toLower();
        if (!kVideoExt.contains(ext)) continue;
        const qint64 len = f.value(QStringLiteral("length")).toVariant().toLongLong();
        if (len > bestLen) { bestLen = len; best = i; }
    }
    return best;
}

} // namespace tankostream::rqbit
```

- [ ] **Step 5: Run the tests (after Task 6 wires CMake) to verify they pass**

Run: `out\tankoban_tests.exe --gtest_filter=RqbitClientTest.*`
Expected: 3 PASS. (If field names differ from Task 1's contract, fix the parser + fixture together.)

- [ ] **Step 6: Commit**

```bash
git add src/core/stream/rqbit/RqbitClient.h src/core/stream/rqbit/RqbitClient.cpp tests/core/stream/test_rqbit_client.cpp
git commit -m "feat(theatre): RqbitClient stats-parse + primary-video-file pick + tests (rqbit phase1 T2)"
```

---

## Task 3: `RqbitClient` — HTTP methods (add / streamUrl / stats / delete)

**Files:**
- Modify: `src/core/stream/rqbit/RqbitClient.h`, `src/core/stream/rqbit/RqbitClient.cpp`

Model on `git show 64213b5^:src/core/stream/stremio/StreamServerClient.cpp`. Reuse the project's QNAM via NetSeam (`NetSeam::createManager(this)` — see existing stream QNAM sites). Requests target `http://127.0.0.1:<port>`; **force HTTP/1.1 is unnecessary here (localhost)** but set a short transfer timeout.

- [ ] **Step 1: Add the network surface to the header**

Add to `RqbitClient` (header), keeping the pure helpers:
```cpp
    void setBaseUrl(const QString& base);   // e.g. "http://127.0.0.1:3030"
    // Adds a magnet; emits torrentAdded(requestTag, torrentId) or requestFailed(requestTag, msg).
    void addTorrent(const QString& requestTag, const QString& magnet);
    // Builds the stream URL for a resolved torrent + file index (pure once base+id known).
    QString streamUrl(const QString& torrentId, int fileIndex) const;
    void fetchStats(const QString& torrentId);     // emits statsReady(torrentId, RqbitStats)
    void deleteTorrent(const QString& torrentId);

signals:
    void torrentAdded(const QString& requestTag, const QString& torrentId, const QJsonArray& files);
    void statsReady(const QString& torrentId, const tankostream::rqbit::RqbitStats& stats);
    void requestFailed(const QString& requestTag, const QString& message);

private:
    QString m_base = QStringLiteral("http://127.0.0.1:3030");
    QNetworkAccessManager* m_nam = nullptr;
```

- [ ] **Step 2: Implement the methods**

In `.cpp` (use the add-torrent request format + id field captured in Task 1):
```cpp
void RqbitClient::setBaseUrl(const QString& base) { m_base = base; }

QString RqbitClient::streamUrl(const QString& torrentId, int fileIndex) const {
    return QStringLiteral("%1/torrents/%2/stream/%3").arg(m_base, torrentId).arg(fileIndex);
}

void RqbitClient::addTorrent(const QString& tag, const QString& magnet) {
    if (!m_nam) m_nam = NetSeam::createManager(this);
    QNetworkRequest req(QUrl(m_base + QStringLiteral("/torrents")));
    req.setTransferTimeout(15000);
    // Per Task 1 contract: magnet posted as the raw body (adjust if contract says JSON/query).
    QNetworkReply* reply = m_nam->post(req, magnet.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, tag]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(tag, reply->errorString()); return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        // id + files field names per contract.
        const QString id = o.value(QStringLiteral("id")).toVariant().toString();
        const QJsonArray files = o.value(QStringLiteral("details")).toObject()
                                  .value(QStringLiteral("files")).toArray();
        if (id.isEmpty()) { emit requestFailed(tag, QStringLiteral("rqbit add: no id in response")); return; }
        emit torrentAdded(tag, id, files);
    });
}
// fetchStats: GET m_base/torrents/{id}/stats/v1 -> parseStats -> emit statsReady
// deleteTorrent: POST m_base/torrents/{id}/delete
```
(Write `fetchStats` and `deleteTorrent` following the same finished-lambda pattern; include `<QNetworkAccessManager>`, `<QNetworkReply>`, `<QJsonDocument>`, and the NetSeam header used by sibling stream files.)

- [ ] **Step 3: Build**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/stream/rqbit/RqbitClient.h src/core/stream/rqbit/RqbitClient.cpp
git commit -m "feat(theatre): RqbitClient HTTP methods — add/streamUrl/stats/delete (rqbit phase1 T3)"
```

---

## Task 4: `RqbitProcess` — subprocess lifecycle

**Files:**
- Create: `src/core/stream/rqbit/RqbitProcess.h`, `src/core/stream/rqbit/RqbitProcess.cpp`

Model directly on `git show 64213b5^:src/core/stream/stremio/StreamServerProcess.cpp`. Use `QProcess`. Binary path = `applicationDirPath()/rqbit.exe` (deployed beside the app by Task 6), fallback to `resources/rqbit/rqbit.exe`.

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
namespace tankostream::rqbit {
class RqbitProcess : public QObject {
    Q_OBJECT
public:
    explicit RqbitProcess(QObject* parent = nullptr);
    void start(const QString& downloadDir);   // spawns rqbit headless on a free 127.0.0.1 port
    void stop();
    int  port() const { return m_port; }
    bool isReady() const { return m_ready; }
signals:
    void ready(int port);          // emitted after health-check passes
    void failed(const QString& message);
private:
    static QString binaryPath();
    static int     pickFreePort();
    void           healthCheck(); // poll GET /torrents until 200, then emit ready
    QProcess* m_proc = nullptr;
    int  m_port = 0;
    bool m_ready = false;
};
} // namespace
```

- [ ] **Step 2: Implement**

`.cpp`: `binaryPath()` checks `QCoreApplication::applicationDirPath()+"/rqbit.exe"` then the source `resources/rqbit/rqbit.exe`; `pickFreePort()` binds a `QTcpServer` to port 0 and reads `serverPort()`; `start()` runs `m_proc->start(binaryPath(), {"--http-api-listen-addr", QString("127.0.0.1:%1").arg(m_port), "server", "start", downloadDir})`, connects `errorOccurred`→`failed`, and on `started` kicks `healthCheck()` (a `QTimer` polling `GET http://127.0.0.1:port/torrents` up to ~10s; on first 200 set `m_ready=true` + emit `ready(port)`; on timeout emit `failed`). `stop()` calls `m_proc->terminate()` then `kill()` after a grace period. (Mirror StreamServerProcess's exact lifecycle + logging.)

- [ ] **Step 3: Build**

Run: `build_check.bat`  → Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/stream/rqbit/RqbitProcess.h src/core/stream/rqbit/RqbitProcess.cpp
git commit -m "feat(theatre): RqbitProcess — headless rqbit subprocess lifecycle + health-check (rqbit phase1 T4)"
```

---

## Task 5: `RqbitEngine` — orchestrator

**Files:**
- Create: `src/core/stream/rqbit/RqbitEngine.h`, `src/core/stream/rqbit/RqbitEngine.cpp`

Model on `git show 64213b5^:src/core/stream/stremio/StreamServerEngine.cpp`. Owns a `RqbitProcess` + `RqbitClient`. Lazy-starts the process on first `startStream`.

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <QObject>
#include <QString>
#include "core/stream/rqbit/RqbitClient.h"
namespace tankostream::rqbit {
class RqbitProcess;
class RqbitEngine : public QObject {
    Q_OBJECT
public:
    explicit RqbitEngine(const QString& downloadDir, QObject* parent = nullptr);
    // Adds the magnet, selects the primary video file, resolves the stream URL.
    void startStream(const QString& magnet);
    void stop(const QString& torrentId);
signals:
    void streamReady(const QString& streamUrl, const QString& torrentId, int fileIndex);
    void streamError(const QString& message);
private:
    RqbitProcess* m_proc = nullptr;
    RqbitClient*  m_client = nullptr;
    QString m_downloadDir;
    QString m_pendingMagnet;   // held while the process warms up
};
} // namespace
```

- [ ] **Step 2: Implement the flow**

`startStream(magnet)`: if `!m_proc->isReady()`, store `m_pendingMagnet`, start the process, and on `RqbitProcess::ready` set `m_client->setBaseUrl("http://127.0.0.1:"+port)` and replay the add; else add immediately. On `RqbitClient::torrentAdded(tag, id, files)` → `int idx = RqbitClient::pickPrimaryVideoFile(files); if (idx<0) emit streamError("no video file in torrent"); else emit streamReady(m_client->streamUrl(id, idx), id, idx);`. On `RqbitClient::requestFailed` / `RqbitProcess::failed` → `emit streamError(msg)`.

- [ ] **Step 3: Build**

Run: `build_check.bat` → Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/core/stream/rqbit/RqbitEngine.h src/core/stream/rqbit/RqbitEngine.cpp
git commit -m "feat(theatre): RqbitEngine — add magnet -> pick file -> stream URL (rqbit phase1 T5)"
```

---

## Task 6: CMake wiring (sources + binary deploy + test registration)

**Files:**
- Modify: `cmake/TankobanSources.cmake`, `CMakeLists.txt`

- [ ] **Step 1: Add the new sources to the app target**

In `cmake/TankobanSources.cmake`, add to the app source list (find the existing `src/core/stream/...` entries and append):
```cmake
    src/core/stream/rqbit/RqbitClient.cpp
    src/core/stream/rqbit/RqbitProcess.cpp
    src/core/stream/rqbit/RqbitEngine.cpp
    src/ui/pages/stream/StreamPlayerController.cpp   # added in Task 7
```

- [ ] **Step 2: Deploy the rqbit binary beside the exe**

In `CMakeLists.txt`, near the ffmpeg-sidecar POST_BUILD copy (see the comment block around the sidecar deploy), add:
```cmake
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/rqbit/rqbit.exe")
    add_custom_command(TARGET Tankoban POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/rqbit/rqbit.exe"
            "$<TARGET_FILE_DIR:Tankoban>/rqbit.exe")
endif()
```

- [ ] **Step 3: Register the unit test**

In `CMakeLists.txt`, add `tests/core/stream/test_rqbit_client.cpp` to the `tankoban_tests` source list (find the existing `tests/core/stream/...` entries).

- [ ] **Step 4: Configure + build both targets**

Run: `build_check.bat` (app) then `cmake --build out --config Release --target tankoban_tests`
Expected: both `BUILD OK`; `out\rqbit.exe` exists beside `out\Tankoban.exe`.

- [ ] **Step 5: Run the Task 2 unit tests**

Run: `out\tankoban_tests.exe --gtest_filter=RqbitClientTest.*`
Expected: 3 PASS.

- [ ] **Step 6: Commit**

```bash
git add cmake/TankobanSources.cmake CMakeLists.txt
git commit -m "build(theatre): wire rqbit sources + binary deploy + RqbitClient tests (rqbit phase1 T6)"
```

---

## Task 7: Restore `StreamPlayerController` (trimmed for rqbit)

**Files:**
- Create: `src/ui/pages/stream/StreamPlayerController.h`, `.cpp`

- [ ] **Step 1: Pull the deleted controller as the base**

Run: `git show 64213b5^:src/ui/pages/stream/StreamPlayerController.h > src/ui/pages/stream/StreamPlayerController.h`
and the same for `.cpp`. This is the starting point — then trim in Step 2.

- [ ] **Step 2: Trim to the rqbit URL path**

Remove anything that referenced `StreamServerEngine`/`StreamServerClient` directly. Keep: a `playUrl(const QString& httpUrl, const QString& title)` entry that opens the existing player (mirror how `MainWindow` opens the video player for a local file, but pass the HTTP URL — the ffmpeg sidecar already accepts HTTP URLs). Keep teardown that emits a `closed()` signal the caller uses to `RqbitEngine::stop(torrentId)`. Delete dead auto-launch/telemetry that depended on removed types.

- [ ] **Step 3: Build**

Run: `build_check.bat` → Expected: `BUILD OK` (the file is in the source list from Task 6 Step 1).

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/stream/StreamPlayerController.h src/ui/pages/stream/StreamPlayerController.cpp
git commit -m "feat(theatre): restore StreamPlayerController, trimmed to play an HTTP stream URL (rqbit phase1 T7)"
```

---

## Task 8: Wire episode-play → stream via rqbit

**Files:**
- Modify: `src/ui/pages/StreamPage.h`, `src/ui/pages/StreamPage.cpp`

Phase-1 trigger: reuse today's auto-pick. Where an episode click currently calls `startAutoDownload(...)` / `finishAutoDownloadPick(...)`, after `AutoSourcePicker::pick(...)` resolves the chosen source's **magnet**, branch to **stream** instead of `m_torrentClient->startDownload(...)`.

- [ ] **Step 1: Own the engine + controller in StreamPage**

In `StreamPage.h` add members:
```cpp
    tankostream::rqbit::RqbitEngine* m_rqbit = nullptr;
    StreamPlayerController*          m_streamPlayer = nullptr;
```
In `StreamPage.cpp` ctor, construct `m_rqbit = new tankostream::rqbit::RqbitEngine(m_torrentClient->defaultPaths().value("videos"), this);` and `m_streamPlayer = new StreamPlayerController(this);` and connect `m_rqbit->streamReady` → `m_streamPlayer->playUrl(url, title)`, `m_rqbit->streamError` → `m_detailView->setStreamSourcesError("Stream failed: "+msg)`.

- [ ] **Step 2: Branch the pick to streaming**

In `finishAutoDownloadPick(...)`, where `chosen` is resolved (the `StreamPickerChoice` with `magnetUri`), replace the `m_torrentClient->startDownload(hash, config)` call (for the play intent) with:
```cpp
    if (!chosen.magnetUri.isEmpty())
        m_rqbit->startStream(chosen.magnetUri);
    else
        m_detailView->setStreamSourcesError(tr("Source has no magnet for streaming"));
```
(Keep `startDownload` available for the explicit download action — that stays libtorrent. Phase 1 only re-routes the *play* path.)

- [ ] **Step 3: Build**

Run: `build_check.bat` → Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/StreamPage.h src/ui/pages/StreamPage.cpp
git commit -m "feat(theatre): route episode play to rqbit streaming (auto-picked source) (rqbit phase1 T8)"
```

---

## Task 9: Live smoke (the real gate)

**Files:** none (verification only)

- [ ] **Step 1: Kill + build + launch**

Run: `taskkill //F //IM Tankoban.exe` ; `build_and_run.bat`
Expected: app launches; `out\rqbit.exe` present beside `out\Tankoban.exe`.

- [ ] **Step 2: Confirm rqbit subprocess + stream**

Open Theatre → One Piece → click an episode to play. Watch:
```
out\tankoctl.exe log-grep "rqbit"        # process start + ready(port)
out\tankoctl.exe log-grep "streamReady"  # stream URL resolved
```
Expected: rqbit subprocess starts, `streamReady` fires with an `http://127.0.0.1:<port>/torrents/<id>/stream/<idx>` URL, the player opens and **playback begins before the download is complete**; seeking works.

- [ ] **Step 3: Confirm teardown**

Close the player. Expected: `RqbitEngine::stop` called; no orphaned rqbit.exe after `scripts/stop-tankoban.ps1`.

- [ ] **Step 4: Hemanth visual gate**

Hand the running app to Hemanth: click a One Piece episode, confirm it plays while downloading. This is the Phase-1 "done" gate. Then a different-engine review of the Phase-1 diff against the spec before merge.

---

## Self-Review

**Spec coverage (Phase 1 scope):** rqbit engine layer (T2-T5 ✓), vendored binary + deploy (T1, T6 ✓), restored StreamPlayerController (T7 ✓), episode-play→stream via auto-pick (T8 ✓), live smoke (T9 ✓). Error handling: process `failed`→`streamError`, no-video→`streamError`, add-fail→`streamError` (T3-T5, T8 ✓). Out of Phase-1 scope (deferred to P2/P3 per spec): Sources pane, Tankorent surfaces, offline promotion — not in this plan. ✓

**Placeholder scan:** The rqbit JSON field names (`state`/`total_bytes`/`progress_bytes`/`finished`, `id`, `details.files`) are the *documented* shape and are explicitly reconciled against the captured contract in Task 1; Task 2 Step 5 instructs fixing parser+fixture together if they differ. This is a deliberate ground-truth-first dependency, not an unfilled placeholder. No "TBD/TODO/handle edge cases" steps. ✓

**Type consistency:** `RqbitStats` (T2) used in `RqbitClient::parseStats`/`statsReady` (T2/T3). `pickPrimaryVideoFile` (T2) used in `RqbitEngine` (T5). `RqbitClient::streamUrl` (T3) used in `RqbitEngine` (T5). `RqbitEngine::startStream/streamReady/streamError` (T5) used in `StreamPage` (T8). `RqbitProcess::ready/failed/isReady/port` (T4) used in `RqbitEngine` (T5). `StreamPlayerController::playUrl/closed` (T7) used in `StreamPage` (T8). Consistent. ✓
