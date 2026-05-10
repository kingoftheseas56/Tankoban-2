# STREAM_DOWNLOADED_LIBRARY Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Stream mode the playback gateway for bulk-downloaded episodes — Cinemeta-rich UI, click-to-auto-play from disk, right-click "Show alternate streams" escape hatch, Videos mode hides files Stream owns, retroactive migration on first launch.

**Architecture:** New `StreamDownloadIndex` (sibling JSON keyed by canonical path) is the single source of truth for "this episode is on disk." `VideosScanner` consults it to skip Stream-owned files. `StreamDetailView` queries it for per-episode markers and routes click → `MainWindow::openVideoPlayer(localPath)` with synthetic Stream-mode metadata. `StreamRescueScanner` runs once at first launch to populate the index from existing on-disk bulks via canonical-layout regex + Cinemeta lookup.

**Tech Stack:** C++20, Qt6 (QObject signals/slots, QHash, QMutex, QJsonObject, QFileInfo, QTimer, QThreadPool, QFileDialog), `JsonStore` (existing project persistence layer), `MetaAggregator` (existing Cinemeta wrapper), `ScannerUtils::walkVideosRoot` (existing recursive walker), `MainWindow::openVideoPlayer` (existing local-file player entry).

**Source spec:** [docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md](../specs/2026-05-10-stream-downloaded-library-design.md)

**Build conventions Tankoban-specific:**
- Tests are smoke-first per Hemanth. Pure-logic primitives can opt in via `-DTANKOBAN_BUILD_TESTS=ON` but this plan does not require new test targets.
- Per-phase build verify: `taskkill /F /IM Tankoban.exe` then `cmake --build out --parallel --target Tankoban`. Do NOT use `build_and_run.bat` from a non-cmd shell — it has vcvars dependencies. From PowerShell: `cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --parallel --target Tankoban'`.
- Per-phase smoke via MCP: launch `out/Tankoban.exe --dev-control` (set `TANKOBAN_STREAM_TELEMETRY=1` + `TANKOBAN_ALERT_TRACE=1` + `TANKOBAN_STREMIO_TUNE=1` env vars + `PATH+=C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin`); ping via `out/tankoctl.exe ping`.
- Cleanup after smoke: `powershell -File scripts/stop-tankoban.ps1` (Rule 17).
- MCP lane lock per Rule 19: claim `MCP LOCK CLAIMED - [Agent N, …]` in `agents/chat.md` before driving desktop, release with `MCP LOCK RELEASED - [Agent N, …]` after.
- Non-trivial RTCs require contracts-v3 `Skills invoked: [...]` field.

---

## Phase 0 — Pre-flight (~5 min)

### Task 0.1: Verify clean baseline

**Files:** none (read-only)

- [ ] **Step 1: Confirm working tree is clean of unrelated dirt**

```bash
git status --porcelain
```

Expected: only `agents/chat.md` may be modified (in-progress RTCs). No `src/` or `docs/` modifications outside this plan's scope.

- [ ] **Step 2: Confirm spec is in tree**

```bash
ls docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md
```

Expected: file exists, 566 lines.

- [ ] **Step 3: Read the spec section §3 Decisions Locked + §5 Architecture**

Open `docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md` and re-read §3 (P1–P6 product calls) + §5 (component map + threading model). These are referenced throughout the plan.

- [ ] **Step 4: Confirm Tankoban.exe builds cleanly NOW (baseline)**

```powershell
taskkill /F /IM Tankoban.exe 2>$null
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --parallel --target Tankoban'
```

Expected: `BUILD OK` (incremental, ~12-15s if recent). If FAIL, fix the unrelated regression before starting Phase 1.

---

## Phase 1 — `StreamDownloadIndex` persistence foundation (~3-4 hrs)

**Goal:** New persistent class + in-memory triple-index + load/save against `<dataDir>/stream_downloads.json`. No UI integration yet; Phase 1 ships dead code that's wired into MainWindow but unused.

**Files for this phase:**
- Create: `src/core/stream/StreamDownloadIndex.h`
- Create: `src/core/stream/StreamDownloadIndex.cpp`
- Modify: `src/ui/MainWindow.h` (add `m_streamDownloadIndex` field + accessor)
- Modify: `src/ui/MainWindow.cpp` (construct after JsonStore)
- Modify: `CMakeLists.txt` (register the new source/header pair)

### Task 1.1: Create `StreamDownloadIndex.h`

**Files:**
- Create: `src/core/stream/StreamDownloadIndex.h`

- [ ] **Step 1: Write the full header file**

```cpp
#pragma once

// STREAM_DOWNLOADED_LIBRARY 2026-05-10 — persistent index of bulk-downloaded
// episodes. Spec: docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md
//
// Owns three in-memory lookup maps derived from a single sibling JSON file
// (<dataDir>/stream_downloads.json) keyed by canonicalKey (lowercased
// native-form absolute path). Threadsafe — VideosScanner reads from a worker
// thread via mutex-guarded const APIs.

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

class JsonStore;

class StreamDownloadIndex : public QObject
{
    Q_OBJECT

public:
    struct Entry {
        QString imdbId;        // "tt6741278"
        QString type;          // "series" (movies excluded in v1 per spec §3 P5)
        int     season = 0;
        int     episode = 0;
        QString canonicalPath; // display-form absolute path
        qint64  addedAt = 0;
        QString sourceGroupId; // empty for migration-rescued; non-empty for bulk-completion
        qint64  fileSizeBytes = 0;
    };

    explicit StreamDownloadIndex(JsonStore* store, QObject* parent = nullptr);

    // Mutating API — runs on GUI thread, mutex-guarded for VideosScanner reads.
    void registerEpisode(const QString& imdbId, int season, int episode,
                         const QString& canonicalPath, const QString& sourceGroupId,
                         qint64 fileSizeBytes);
    void evictByImdb(const QString& imdbId);
    void evictByPath(const QString& canonicalKey);

    // Off-thread validateAll: stat each entry's canonicalPath, evict missing.
    // Safe to call from worker; mutates indices via queued-connection signal.
    void validateAll();

    // Read API — mutex-guarded. Safe from any thread.
    bool isStreamOwned(const QString& canonicalKey) const;
    std::optional<QString> filePathFor(const QString& imdbId, int season, int episode) const;
    bool hasAnyForImdb(const QString& imdbId) const;
    QList<Entry> entriesForImdb(const QString& imdbId) const;
    QList<Entry> all() const;

    // Static helper: convert any path to the canonical lookup key.
    // Public so callers building lookups in a hot loop can compute once.
    static QString computeCanonicalKey(const QString& anyPath);

    // Static helper: encode (imdbId, season, episode) as the by-episode map key.
    static QString computeEpisodeKey(const QString& imdbId, int season, int episode);

signals:
    void entriesChanged();

private:
    void load();
    void save();

    JsonStore* m_store;
    mutable QMutex m_mutex;

    // Three derived maps; all updated atomically under m_mutex.
    QHash<QString, Entry>   m_byPath;       // canonicalKey -> Entry
    QHash<QString, QString> m_byEpisode;    // "imdb:NN:NN" -> canonicalKey
    QSet<QString>           m_imdbHasAny;   // imdb if at least one entry exists

    static constexpr const char* FILENAME = "stream_downloads.json";
    static constexpr int kSchemaVersion = 1;
};
```

- [ ] **Step 2: Verify the header compiles in isolation (sanity)**

The build will run as part of Phase 1's final build step (Task 1.6). For now, just verify the file exists at the correct path:

```bash
ls -la src/core/stream/StreamDownloadIndex.h
```

Expected: file present, ~75 lines.

### Task 1.2: Create `StreamDownloadIndex.cpp` — load/save + canonicalKey helpers

**Files:**
- Create: `src/core/stream/StreamDownloadIndex.cpp`

- [ ] **Step 1: Write the file with includes + static helpers + load/save scaffolding**

```cpp
#include "StreamDownloadIndex.h"

#include "core/JsonStore.h"
#include "core/DebugLogBuffer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

// ── Static helpers ──────────────────────────────────────────────────────────

QString StreamDownloadIndex::computeCanonicalKey(const QString& anyPath)
{
    // Per spec §4.1 — lowercased native-form absolute path. Handles
    // Windows case-insensitivity + slash normalization in one pass.
    return QDir::toNativeSeparators(QFileInfo(anyPath).absoluteFilePath()).toLower();
}

QString StreamDownloadIndex::computeEpisodeKey(const QString& imdbId, int season, int episode)
{
    return QStringLiteral("%1:%2:%3")
        .arg(imdbId)
        .arg(season, 2, 10, QLatin1Char('0'))
        .arg(episode, 2, 10, QLatin1Char('0'));
}

// ── ctor + load/save ────────────────────────────────────────────────────────

StreamDownloadIndex::StreamDownloadIndex(JsonStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    load();
}

void StreamDownloadIndex::load()
{
    if (!m_store)
        return;

    const QJsonObject data = m_store->read(FILENAME);
    const int storedVersion = data.value(QStringLiteral("version")).toInt(0);
    if (storedVersion != kSchemaVersion) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("schema mismatch on load — starting empty"),
            QJsonObject{{QStringLiteral("storedVersion"), storedVersion},
                        {QStringLiteral("expected"), kSchemaVersion}});
        return;
    }

    const QJsonObject byPath = data.value(QStringLiteral("byPath")).toObject();

    QMutexLocker lock(&m_mutex);
    for (auto it = byPath.constBegin(); it != byPath.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        Entry e;
        e.imdbId        = obj.value(QStringLiteral("imdbId")).toString();
        e.type          = obj.value(QStringLiteral("type")).toString();
        e.season        = obj.value(QStringLiteral("season")).toInt();
        e.episode       = obj.value(QStringLiteral("episode")).toInt();
        e.canonicalPath = obj.value(QStringLiteral("canonicalPath")).toString();
        e.addedAt       = static_cast<qint64>(obj.value(QStringLiteral("addedAt")).toDouble());
        e.sourceGroupId = obj.value(QStringLiteral("sourceGroupId")).toString();
        e.fileSizeBytes = static_cast<qint64>(obj.value(QStringLiteral("fileSizeBytes")).toDouble());

        if (e.imdbId.isEmpty() || e.canonicalPath.isEmpty())
            continue;

        const QString key = it.key();
        m_byPath.insert(key, e);
        m_byEpisode.insert(computeEpisodeKey(e.imdbId, e.season, e.episode), key);
        m_imdbHasAny.insert(e.imdbId);
    }

    DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
        QStringLiteral("loaded entries"),
        QJsonObject{{QStringLiteral("count"), m_byPath.size()}});
}

void StreamDownloadIndex::save()
{
    if (!m_store)
        return;

    QJsonObject byPath;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            const Entry& e = it.value();
            QJsonObject obj;
            obj[QStringLiteral("imdbId")]        = e.imdbId;
            obj[QStringLiteral("type")]          = e.type;
            obj[QStringLiteral("season")]        = e.season;
            obj[QStringLiteral("episode")]       = e.episode;
            obj[QStringLiteral("canonicalPath")] = e.canonicalPath;
            obj[QStringLiteral("addedAt")]       = static_cast<double>(e.addedAt);
            obj[QStringLiteral("sourceGroupId")] = e.sourceGroupId;
            obj[QStringLiteral("fileSizeBytes")] = static_cast<double>(e.fileSizeBytes);
            byPath[it.key()] = obj;
        }
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kSchemaVersion;
    root[QStringLiteral("byPath")]  = byPath;
    m_store->write(FILENAME, root);
}
```

### Task 1.3: Add the mutating API to `StreamDownloadIndex.cpp`

- [ ] **Step 1: Append the mutating + read API methods**

Append to `src/core/stream/StreamDownloadIndex.cpp`:

```cpp
// ── Mutating API ────────────────────────────────────────────────────────────

void StreamDownloadIndex::registerEpisode(const QString& imdbId, int season, int episode,
                                          const QString& canonicalPath,
                                          const QString& sourceGroupId,
                                          qint64 fileSizeBytes)
{
    if (imdbId.isEmpty() || canonicalPath.isEmpty() || season < 0 || episode < 0)
        return;

    Entry e;
    e.imdbId        = imdbId;
    e.type          = QStringLiteral("series");  // v1 series-only per spec §3 P5
    e.season        = season;
    e.episode       = episode;
    e.canonicalPath = canonicalPath;
    e.addedAt       = QDateTime::currentMSecsSinceEpoch();
    e.sourceGroupId = sourceGroupId;
    e.fileSizeBytes = fileSizeBytes;

    const QString key   = computeCanonicalKey(canonicalPath);
    const QString epKey = computeEpisodeKey(imdbId, season, episode);

    {
        QMutexLocker lock(&m_mutex);

        // If a prior entry occupied this episode at a different path, evict it
        // first so by-episode never points at a stale path.
        auto epIt = m_byEpisode.constFind(epKey);
        if (epIt != m_byEpisode.constEnd() && epIt.value() != key) {
            m_byPath.remove(epIt.value());
        }

        m_byPath.insert(key, e);
        m_byEpisode.insert(epKey, key);
        m_imdbHasAny.insert(imdbId);
    }

    DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
        QStringLiteral("registerEpisode"),
        QJsonObject{{QStringLiteral("imdb"), imdbId},
                    {QStringLiteral("season"), season},
                    {QStringLiteral("episode"), episode},
                    {QStringLiteral("path"), canonicalPath},
                    {QStringLiteral("groupId"), sourceGroupId}});

    save();
    emit entriesChanged();
}

void StreamDownloadIndex::evictByImdb(const QString& imdbId)
{
    if (imdbId.isEmpty())
        return;

    int removed = 0;
    {
        QMutexLocker lock(&m_mutex);
        QStringList pathsToRemove;
        QStringList epKeysToRemove;
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it) {
            if (it.value().imdbId == imdbId) {
                pathsToRemove.append(it.key());
                epKeysToRemove.append(computeEpisodeKey(imdbId, it.value().season, it.value().episode));
            }
        }
        for (const QString& p : pathsToRemove)  m_byPath.remove(p);
        for (const QString& k : epKeysToRemove) m_byEpisode.remove(k);
        m_imdbHasAny.remove(imdbId);
        removed = pathsToRemove.size();
    }

    if (removed > 0) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("evictByImdb"),
            QJsonObject{{QStringLiteral("imdb"), imdbId},
                        {QStringLiteral("removed"), removed}});
        save();
        emit entriesChanged();
    }
}

void StreamDownloadIndex::evictByPath(const QString& canonicalKey)
{
    if (canonicalKey.isEmpty())
        return;

    bool changed = false;
    bool removeImdbFlag = false;
    QString affectedImdb;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_byPath.constFind(canonicalKey);
        if (it == m_byPath.constEnd())
            return;
        const Entry e = it.value();
        m_byPath.remove(canonicalKey);
        m_byEpisode.remove(computeEpisodeKey(e.imdbId, e.season, e.episode));
        affectedImdb = e.imdbId;
        // Recompute m_imdbHasAny membership for this imdb.
        bool stillHasAny = false;
        for (const Entry& other : m_byPath) {
            if (other.imdbId == e.imdbId) { stillHasAny = true; break; }
        }
        if (!stillHasAny) {
            m_imdbHasAny.remove(e.imdbId);
            removeImdbFlag = true;
        }
        changed = true;
    }

    if (changed) {
        DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
            QStringLiteral("evictByPath"),
            QJsonObject{{QStringLiteral("path"), canonicalKey},
                        {QStringLiteral("imdb"), affectedImdb},
                        {QStringLiteral("imdbStillHasAny"), !removeImdbFlag}});
        save();
        emit entriesChanged();
    }
}

void StreamDownloadIndex::validateAll()
{
    // Snapshot the keys+paths under lock; stat off-lock; collect missing;
    // re-acquire lock to evict.
    QList<QPair<QString, QString>> snapshot;  // canonicalKey -> displayPath
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_byPath.constBegin(); it != m_byPath.constEnd(); ++it)
            snapshot.append({it.key(), it.value().canonicalPath});
    }

    QStringList missing;
    for (const auto& pr : snapshot) {
        if (!QFileInfo::exists(pr.second))
            missing.append(pr.first);
    }

    if (missing.isEmpty())
        return;

    DebugLogBuffer::instance().info(QStringLiteral("stream-download-index"),
        QStringLiteral("validateAll evicting missing entries"),
        QJsonObject{{QStringLiteral("count"), missing.size()}});

    for (const QString& key : missing)
        evictByPath(key);
}

// ── Read API ────────────────────────────────────────────────────────────────

bool StreamDownloadIndex::isStreamOwned(const QString& canonicalKey) const
{
    QMutexLocker lock(&m_mutex);
    return m_byPath.contains(canonicalKey);
}

std::optional<QString> StreamDownloadIndex::filePathFor(const QString& imdbId,
                                                        int season, int episode) const
{
    const QString epKey = computeEpisodeKey(imdbId, season, episode);
    QMutexLocker lock(&m_mutex);
    auto it = m_byEpisode.constFind(epKey);
    if (it == m_byEpisode.constEnd())
        return std::nullopt;
    auto pIt = m_byPath.constFind(it.value());
    if (pIt == m_byPath.constEnd())
        return std::nullopt;
    return pIt.value().canonicalPath;
}

bool StreamDownloadIndex::hasAnyForImdb(const QString& imdbId) const
{
    QMutexLocker lock(&m_mutex);
    return m_imdbHasAny.contains(imdbId);
}

QList<StreamDownloadIndex::Entry> StreamDownloadIndex::entriesForImdb(const QString& imdbId) const
{
    QList<Entry> out;
    QMutexLocker lock(&m_mutex);
    for (const Entry& e : m_byPath) {
        if (e.imdbId == imdbId)
            out.append(e);
    }
    return out;
}

QList<StreamDownloadIndex::Entry> StreamDownloadIndex::all() const
{
    QMutexLocker lock(&m_mutex);
    return m_byPath.values();
}
```

### Task 1.4: Wire `StreamDownloadIndex` into MainWindow

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Add forward declaration + member field + accessor in MainWindow.h**

Add forward declaration near the top of the class (alongside other forwards like `class StreamLibrary;` — search for an existing forward declaration to find the convention):

```cpp
class StreamDownloadIndex;
```

In the private members section, near `StreamLibrary* m_streamLibrary = nullptr;` (search for it):

```cpp
StreamDownloadIndex* m_streamDownloadIndex = nullptr;
```

Add a public accessor near other similar accessors:

```cpp
StreamDownloadIndex* streamDownloadIndex() const { return m_streamDownloadIndex; }
```

- [ ] **Step 2: Construct in MainWindow.cpp**

Add `#include "core/stream/StreamDownloadIndex.h"` near other `core/stream/*` includes.

In the MainWindow constructor, immediately AFTER `JsonStore` is constructed and BEFORE `StreamLibrary` is constructed (search for `new StreamLibrary` to find the spot):

```cpp
m_streamDownloadIndex = new StreamDownloadIndex(m_jsonStore, this);
DebugLogBuffer::instance().info("boot",
    QStringLiteral("stream-download-index-created"));
```

(Use whatever the actual JsonStore member name is — search for `m_jsonStore` or `m_jsonBridge` or similar; if it's accessed via a bridge object, pass the bridge's `.store()` as today.)

### Task 1.5: Register in CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add the new source/header to the SOURCES + HEADERS lists**

Locate the existing `src/core/stream/SubtitlesAggregator.cpp` line in CMakeLists.txt SOURCES and add immediately below:

```cmake
    src/core/stream/StreamDownloadIndex.cpp
```

Locate the existing `src/core/stream/SubtitlesAggregator.h` line in CMakeLists.txt HEADERS and add immediately below:

```cmake
    src/core/stream/StreamDownloadIndex.h
```

### Task 1.6: Build verify Phase 1

- [ ] **Step 1: Kill any running Tankoban**

```powershell
taskkill /F /IM Tankoban.exe 2>$null
```

Expected: either "killed" or "process not found" — both acceptable.

- [ ] **Step 2: Build**

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --parallel --target Tankoban'
```

Expected: `BUILD OK`. If FAIL, fix and retry.

- [ ] **Step 3: Boot smoke**

Launch and ping:

```powershell
$env:Path = "C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;" + $env:Path
$env:TANKOBAN_STREAM_TELEMETRY = "1"; $env:TANKOBAN_ALERT_TRACE = "1"; $env:TANKOBAN_STREMIO_TUNE = "1"
Start-Process -FilePath "C:\Users\Suprabha\Desktop\Tankoban 2\out\Tankoban.exe" -ArgumentList "--dev-control" -WorkingDirectory "C:\Users\Suprabha\Desktop\Tankoban 2\out"
Start-Sleep -Seconds 6
& "C:\Users\Suprabha\Desktop\Tankoban 2\out\tankoctl.exe" ping
```

Expected: ping returns the schema reply.

- [ ] **Step 4: Verify the index loaded clean (empty)**

```bash
out/tankoctl.exe logs 200 2>&1 | grep "stream-download-index"
```

Expected: at least one entry like `loaded entries count=0`.

- [ ] **Step 5: Verify stream_downloads.json gets created on disk on first save**

The file is only created on the first `save()` call (which fires from registerEpisode/evict, not just on construction). So the file may NOT exist yet — that's fine. Verify the directory:

```powershell
$dataDir = [Environment]::GetFolderPath('LocalApplicationData') + "\Tankoban\data"
ls $dataDir
```

Expected: directory exists with `stream_library.json`, `stream_progress.json`, etc. — but `stream_downloads.json` may not exist yet (will be created in Phase 2).

- [ ] **Step 6: Cleanup**

```powershell
powershell -File scripts/stop-tankoban.ps1
```

- [ ] **Step 7: Commit**

```bash
git add src/core/stream/StreamDownloadIndex.h src/core/stream/StreamDownloadIndex.cpp \
        src/ui/MainWindow.h src/ui/MainWindow.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
[Phase 1, STREAM_DOWNLOADED_LIBRARY]: add StreamDownloadIndex persistence foundation

NEW class src/core/stream/StreamDownloadIndex.{h,cpp} (~250 LOC):
- Three in-memory maps (m_byPath / m_byEpisode / m_imdbHasAny) derived from
  sibling JSON stream_downloads.json keyed by lowercased native-form canonical
  path.
- Public API: registerEpisode / evictByImdb / evictByPath / validateAll plus
  read-side isStreamOwned / filePathFor / hasAnyForImdb / entriesForImdb / all.
- Threadsafe via QMutex for VideosScanner cross-thread reads (Phase 4 wires
  the read).
- Static helpers: computeCanonicalKey + computeEpisodeKey for callers that
  build keys in hot loops.

MOD MainWindow constructs the index after JsonStore + before StreamLibrary;
exposes via streamDownloadIndex() accessor for Phase 2-7 wiring.

MOD CMakeLists.txt registers the new source/header pair.

No UI integration yet — Phase 1 ships dead code that subsequent phases consume.
build_check.bat BUILD OK first try; boot smoke clean; index loads empty (no
prior on-disk state).
EOF
)"
```

- [ ] **Step 8: Post RTC line in agents/chat.md**

```
READY TO COMMIT - [Agent N, STREAM_DOWNLOADED_LIBRARY Phase 1 — StreamDownloadIndex persistence foundation shipped per docs/superpowers/plans/2026-05-10-stream-downloaded-library.md. NEW src/core/stream/StreamDownloadIndex.{h,cpp} (~250 LOC, three in-memory maps + JSON persistence + thread-safe Q_OBJECT). MOD MainWindow constructs after JsonStore. CMakeLists registered. build_check.bat BUILD OK first try; boot smoke clean; index loads empty (no prior on-disk state). No UI integration yet — Phase 2 wires the bulk-completion hook.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/core/stream/StreamDownloadIndex.h, src/core/stream/StreamDownloadIndex.cpp, src/ui/MainWindow.h, src/ui/MainWindow.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 2 — Bulk-completion hook (~1 hr)

**Goal:** When a bulk torrent finishes and files are renamed to canonical paths, write per-episode entries to the index.

**Files for this phase:**
- Modify: `src/core/torrent/TorrentClient.cpp`

### Task 2.1: Locate `publishStreamBulkItemsForTorrent` insertion point

- [ ] **Step 1: Grep for the per-file rename completion logic**

```bash
grep -n "publishStreamBulkItemsForTorrent\|fileRenamed\|onFileRenamed" src/core/torrent/TorrentClient.cpp
```

Expected: locate the function that handles per-file rename success. Per Phase 5 of the prior bulk spec, this is where files transition to `Published` state. The new `registerEpisode` call goes here.

- [ ] **Step 2: Read the publish logic to understand the (groupId, fileIdx-or-infohash) → (imdb, s, e, canonicalPath) map**

```bash
grep -n "m_streamBulkGroups\|streamBulkItemFromGroup\|item.imdbId\|item.season\|item.episode" src/core/torrent/TorrentClient.cpp | head -30
```

Identify how the publish path retrieves the (imdb, season, episode, canonicalPath) tuple for the file being renamed. The bulk Phase 5 work added this lookup; we reuse it.

### Task 2.2: Add `registerEpisode` call in publish path

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp`

- [ ] **Step 1: Add the include**

Near the top of `src/core/torrent/TorrentClient.cpp`, alongside other `core/stream/*` includes:

```cpp
#include "core/stream/StreamDownloadIndex.h"
```

- [ ] **Step 2: Add a setter for the index pointer in TorrentClient**

In `src/core/torrent/TorrentClient.h`, add to the public section near other setters:

```cpp
void setStreamDownloadIndex(StreamDownloadIndex* idx) { m_streamDownloadIndex = idx; }
```

In the private members section:

```cpp
StreamDownloadIndex* m_streamDownloadIndex = nullptr;
```

Add forward declaration near the top of the header:

```cpp
class StreamDownloadIndex;
```

- [ ] **Step 3: In the publish-success path, register the episode**

Locate the function that handles a successful per-file rename to canonical path (likely `onFileRenamed` or inside `publishStreamBulkItemsForTorrent`). After the item's state transitions to `Published`, add:

```cpp
// STREAM_DOWNLOADED_LIBRARY Phase 2 — register the published file in the
// stream-side download index so Stream UI can render the DOWNLOADED badge
// and Videos scanner can skip the file going forward.
if (m_streamDownloadIndex && !item.imdbId.isEmpty()
    && item.season > 0 && item.episode > 0) {
    m_streamDownloadIndex->registerEpisode(
        item.imdbId,
        item.season,
        item.episode,
        canonicalPath,                     // post-rename absolute path
        groupId,                           // streamGroupId
        QFileInfo(canonicalPath).size()
    );
}
```

(Variable names will differ — match what the surrounding publish code already uses for the item record + canonical path.)

- [ ] **Step 4: Wire the setter in MainWindow**

In `src/ui/MainWindow.cpp`, after constructing `m_torrentClient` AND after `m_streamDownloadIndex` (both should exist by now per Phase 1 + the prior bulk work), add:

```cpp
if (m_torrentClient && m_streamDownloadIndex)
    m_torrentClient->setStreamDownloadIndex(m_streamDownloadIndex);
```

### Task 2.3: Build verify

- [ ] **Step 1: Kill, build, ping**

```powershell
taskkill /F /IM Tankoban.exe 2>$null
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --parallel --target Tankoban'
```

Expected: `BUILD OK`.

### Task 2.4: Smoke — start a small bulk and verify entries appear

**Note:** Smoke requires Hemanth to drive the bulk download click OR the agent to drive via MCP per Rule 19. If running via subagent, claim the MCP lane before smoking.

- [ ] **Step 1: Claim MCP lane**

Append to `agents/chat.md`:

```
MCP LOCK CLAIMED - [Agent N, 2026-MM-DD HH:MMpm — STREAM_DOWNLOADED_LIBRARY Phase 2 smoke. Goal: trigger a small bulk download (≤2 episodes), wait for completion + per-file rename, then verify stream_downloads.json populates with per-episode entries.]
```

- [ ] **Step 2: Launch Tankoban via MCP recipe (see Phase 1 Task 1.6 Step 3)**

- [ ] **Step 3: Navigate Stream tab → pick a small show with a known bulk-shape source**

The Boys S05 (or whichever has a small per-episode magnet count) is a good candidate. Click "Download season". Walk through the preflight dialog. Start download.

- [ ] **Step 4: Wait for at least one episode to complete + rename**

The Tankorent group row will show progress. Wait for the first per-episode-completion event.

- [ ] **Step 5: Verify the index file populated**

```powershell
$dataDir = [Environment]::GetFolderPath('LocalApplicationData') + "\Tankoban\data"
cat "$dataDir\stream_downloads.json"
```

Expected: file exists with `version: 1`, `byPath: {...}` containing at least one entry with the expected `imdbId`, `season`, `episode`, `canonicalPath`, non-empty `sourceGroupId`, non-zero `fileSizeBytes`.

- [ ] **Step 6: Verify dev-bridge logs show the registration**

```bash
out/tankoctl.exe logs 500 2>&1 | grep "stream-download-index"
```

Expected: `registerEpisode` log line(s) with the imdb/season/episode/path matching the bulk.

- [ ] **Step 7: Cleanup + release MCP lane**

```powershell
powershell -File scripts/stop-tankoban.ps1
```

Append to `agents/chat.md`:

```
MCP LOCK RELEASED - [Agent N, 2026-MM-DD HH:MMpm — STREAM_DOWNLOADED_LIBRARY Phase 2 smoke COMPLETE. Bulk dispatched, first episode completed + renamed, registerEpisode log entry confirmed, stream_downloads.json populated with 1 entry per landed episode. See RTC below.]
```

- [ ] **Step 8: Commit**

```bash
git add src/core/torrent/TorrentClient.h src/core/torrent/TorrentClient.cpp src/ui/MainWindow.cpp
git commit -m "$(cat <<'EOF'
[Phase 2, STREAM_DOWNLOADED_LIBRARY]: bulk-completion writes per-episode entries to StreamDownloadIndex

MOD src/core/torrent/TorrentClient.{h,cpp}: setStreamDownloadIndex setter
+ registerEpisode call in the per-file publish-success path. Per-file
granularity (registers as each file lands, not at group completion).

MOD src/ui/MainWindow.cpp: wires setStreamDownloadIndex(m_streamDownloadIndex)
after construction.

Smoke: launched + bulk-dispatched a small season, first per-episode completion
fired registerEpisode (verified via dev-bridge logs), stream_downloads.json
populated with the expected entries (verified by cat against the on-disk file).
build_check.bat BUILD OK first try.
EOF
)"
```

- [ ] **Step 9: Post RTC line**

(Same RTC pattern as Phase 1 Step 8.)

---

## Phase 3 — Tile badge + StreamLibrary eviction (~1.5 hrs)

**Goal:** Stream library home shows the `DOWNLOADED` chip on tiles for shows with ≥1 downloaded episode. Removing a show from Stream library evicts its index entries (files re-appear in Videos next phase).

**Files for this phase:**
- Modify: `src/core/stream/StreamLibrary.cpp` (and possibly `.h` for a setter)
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp` (or whichever renders the show-tile grid)
- Modify: `src/ui/pages/StreamPage.cpp` (wires StreamDownloadIndex into the home-board layout)

### Task 3.1: StreamLibrary.remove also evicts download entries

**Files:**
- Modify: `src/core/stream/StreamLibrary.h`
- Modify: `src/core/stream/StreamLibrary.cpp`

- [ ] **Step 1: Add the optional setter to StreamLibrary.h**

In `src/core/stream/StreamLibrary.h` near the existing `add/remove/has` declarations:

```cpp
class StreamDownloadIndex;
```

(forward declaration near top of file)

```cpp
void setStreamDownloadIndex(StreamDownloadIndex* idx) { m_downloadIndex = idx; }
```

(public method)

```cpp
StreamDownloadIndex* m_downloadIndex = nullptr;
```

(private member)

- [ ] **Step 2: Modify StreamLibrary::remove in StreamLibrary.cpp**

Locate `bool StreamLibrary::remove(const QString& imdbId)`. After the existing logic that erases from `m_entries` and saves, but BEFORE the function returns:

```cpp
// STREAM_DOWNLOADED_LIBRARY Phase 3 — Remove from library also evicts any
// per-episode download entries for this show. Files on disk are NOT touched
// (per spec §3 P4); the eviction lets the Videos scanner re-discover them
// on its next debounced rescan.
if (m_downloadIndex)
    m_downloadIndex->evictByImdb(imdbId);
```

Add `#include "StreamDownloadIndex.h"` at the top of `StreamLibrary.cpp`.

- [ ] **Step 3: Wire the setter in MainWindow**

In `src/ui/MainWindow.cpp`, after both `m_streamLibrary` and `m_streamDownloadIndex` are constructed:

```cpp
if (m_streamLibrary && m_streamDownloadIndex)
    m_streamLibrary->setStreamDownloadIndex(m_streamDownloadIndex);
```

### Task 3.2: Identify the show-tile rendering site

- [ ] **Step 1: Locate the tile-rendering code**

```bash
grep -n "TileCard\|renderTile\|streamLibraryEntry\|libraryHeading" src/ui/pages/stream/StreamLibraryLayout.cpp | head -20
```

Find the function that constructs each show-tile widget for the library grid (`Shows & Movies` strip on Stream home). That's where the `DOWNLOADED` chip overlay goes.

- [ ] **Step 2: Read the existing chip-overlay code**

If the existing `STREAM` chip on Tankorent group rows is using a reusable QSS class (e.g., `chipStyle`), grep for it:

```bash
grep -n "chipStyle\|StreamChip\|STREAM" src/ui/pages/stream/StreamLibraryLayout.cpp src/ui/pages/TankorentPage.cpp | head -10
```

Identify how the chip is structured (QLabel + setObjectName + QSS) so the new chip can match the visual idiom.

### Task 3.3: Render `DOWNLOADED` chip on tiles

**Files:**
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp`
- Modify: `src/ui/pages/stream/StreamLibraryLayout.h` (if needed for a setter)

- [ ] **Step 1: Add a `setStreamDownloadIndex` setter on StreamLibraryLayout**

```cpp
class StreamDownloadIndex;
```

(forward declaration)

```cpp
void setStreamDownloadIndex(StreamDownloadIndex* idx);
```

(public method declaration)

```cpp
StreamDownloadIndex* m_downloadIndex = nullptr;
```

(private member)

- [ ] **Step 2: Implement the setter and subscribe to entriesChanged**

In `src/ui/pages/stream/StreamLibraryLayout.cpp`:

```cpp
#include "core/stream/StreamDownloadIndex.h"

void StreamLibraryLayout::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    m_downloadIndex = idx;
    if (m_downloadIndex) {
        connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged,
                this, [this]() {
                    // STREAM_DOWNLOADED_LIBRARY Phase 3 — re-render tile
                    // decorations when the download-index changes (bulk
                    // completion, migration, eviction).
                    refreshTileBadges();
                }, Qt::QueuedConnection);
    }
}
```

- [ ] **Step 3: Add a `refreshTileBadges()` method**

In `StreamLibraryLayout.cpp`:

```cpp
void StreamLibraryLayout::refreshTileBadges()
{
    if (!m_downloadIndex) return;
    // For each tile widget in m_tiles (or whatever the existing tile-list
    // member is named — match the existing rendering code), look up its
    // imdbId and toggle the DOWNLOADED chip visibility.
    for (auto* tile : m_tiles) {
        const QString imdb = tile->property("imdbId").toString();
        if (imdb.isEmpty()) continue;
        const bool downloaded = m_downloadIndex->hasAnyForImdb(imdb);
        QLabel* chip = tile->findChild<QLabel*>("DownloadedChip");
        if (chip) chip->setVisible(downloaded);
    }
}
```

(Naming may differ — match the existing tile-widget pattern. The key is: store `imdbId` as a Qt property on the tile widget at construction, then look up + toggle the chip on `entriesChanged`.)

- [ ] **Step 4: Add the chip widget at tile construction time**

In the function that constructs each tile (e.g., `StreamLibraryLayout::buildShowTile` or similar — match the existing entry point), AFTER the poster image is added but BEFORE the title label, add:

```cpp
// STREAM_DOWNLOADED_LIBRARY Phase 3 — DOWNLOADED chip overlay.
// Same chip QSS as Tankorent's STREAM chip — small-caps gray-bg per
// feedback_no_color_no_emoji.md.
auto* dlChip = new QLabel(QStringLiteral("DOWNLOADED"), tile);
dlChip->setObjectName(QStringLiteral("DownloadedChip"));
dlChip->setStyleSheet(chipStyle);  // same QSS string used by STREAM chip
dlChip->setVisible(m_downloadIndex && m_downloadIndex->hasAnyForImdb(entry.imdb));
// Position: top-right corner overlay. Use the existing chip layout helper
// (search for how STREAM chip is positioned in TankorentPage and mirror).
```

- [ ] **Step 5: Wire the setter from StreamPage**

In `src/ui/pages/StreamPage.cpp`, after `m_libraryLayout` is constructed (search for `new StreamLibraryLayout`):

```cpp
if (m_libraryLayout && MainWindowPtr())
    m_libraryLayout->setStreamDownloadIndex(MainWindowPtr()->streamDownloadIndex());
```

(Replace `MainWindowPtr()` with whatever the existing parent-MainWindow accessor is in StreamPage — search for `mainWindow()` or `qobject_cast<MainWindow*>(parent())`.)

### Task 3.4: Build verify Phase 3

- [ ] **Step 1: Build**

```powershell
taskkill /F /IM Tankoban.exe 2>$null
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --parallel --target Tankoban'
```

Expected: `BUILD OK`.

- [ ] **Step 2: Smoke (MCP-driven, claim lane)**

Launch Tankoban; open Stream tab. Verify:
- The show whose bulk-completion populated the index in Phase 2 now displays a `DOWNLOADED` chip on its tile.
- Other shows have no chip.
- Right-click → Remove from Library on the badged show: chip disappears (if the show is the only entry, the show tile itself disappears from library home).
- Verify `stream_downloads.json` is empty for that imdb after the remove (re-cat the file).

- [ ] **Step 3: Cleanup, commit, RTC**

(Same pattern as prior phases.)

---

## Phase 4 — Episode-row marker + click-branch + right-click + subtitles (~3-4 hrs)

**Goal:** StreamDetailView's episode list shows downloaded markers; click on a downloaded episode auto-plays via local file with full Stream-mode subtitle UX; right-click any episode opens "Show alternate streams" overlay.

**Files for this phase:**
- Create: `resources/icons/downloaded.svg`
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`
- Modify: `resources/resources.qrc` (or wherever Qt resources are registered)

### Task 4.1: Add the `downloaded.svg` icon

**Files:**
- Create: `resources/icons/downloaded.svg`

- [ ] **Step 1: Write a placeholder grayscale SVG**

```svg
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 12 12">
  <!-- Downward arrow into tray. Gray fill matches existing icon family.
       STREAM_DOWNLOADED_LIBRARY 2026-05-10 — placeholder; refine in design pass. -->
  <path d="M6 1 L6 7 M3 5 L6 8 L9 5 M2 10 L10 10"
        stroke="#cccccc" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
```

- [ ] **Step 2: Register in the .qrc resource file**

Locate the existing `resources/resources.qrc` (or whichever .qrc file lists icon resources):

```bash
grep -rn "icons.*svg" --include="*.qrc"
```

Add a new entry alongside others:

```xml
<file alias="icons/downloaded.svg">icons/downloaded.svg</file>
```

### Task 4.2: Add per-episode marker to StreamDetailView

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Add the StreamDownloadIndex setter to StreamDetailView.h**

```cpp
class StreamDownloadIndex;
```

(forward declaration)

```cpp
void setStreamDownloadIndex(StreamDownloadIndex* idx);
```

(public)

```cpp
StreamDownloadIndex* m_downloadIndex = nullptr;
```

(private)

- [ ] **Step 2: Implement the setter + subscribe to entriesChanged**

In `StreamDetailView.cpp`:

```cpp
#include "core/stream/StreamDownloadIndex.h"

void StreamDetailView::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    m_downloadIndex = idx;
    if (m_downloadIndex) {
        connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged,
                this, [this]() {
                    // Re-render episode rows in place to update markers.
                    refreshEpisodeMarkers();
                }, Qt::QueuedConnection);
    }
}
```

- [ ] **Step 3: Implement `refreshEpisodeMarkers`**

```cpp
void StreamDetailView::refreshEpisodeMarkers()
{
    if (!m_episodeTable || !m_downloadIndex || m_currentImdbId.isEmpty()
        || m_currentSeason <= 0)
        return;

    static const QIcon dlIcon(QStringLiteral(":/icons/downloaded.svg"));

    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        const int episodeNum = m_episodeTable->item(row, 0)
            ? m_episodeTable->item(row, 0)->text().toInt() : 0;
        if (episodeNum <= 0) continue;

        const auto path = m_downloadIndex->filePathFor(
            m_currentImdbId, m_currentSeason, episodeNum);

        // Title cell (column 1 — verify against existing layout).
        QTableWidgetItem* titleItem = m_episodeTable->item(row, 1);
        if (!titleItem) continue;

        if (path.has_value()) {
            titleItem->setIcon(dlIcon);
            titleItem->setToolTip(tr("On disk: %1").arg(QFileInfo(*path).fileName()));
        } else {
            titleItem->setIcon(QIcon());
            titleItem->setToolTip(QString());
        }
    }
}
```

(Match column indices to whatever the existing m_episodeTable layout uses. Verify via:)

```bash
grep -n "m_episodeTable->setItem\|m_episodeTable->insertRow\|column.*Title" src/ui/pages/stream/StreamDetailView.cpp | head -15
```

- [ ] **Step 4: Call refreshEpisodeMarkers after episode list rebuilds**

Locate where the episode list is populated (probably in a function called when the season changes or when meta arrives — search for `m_episodeTable->setRowCount` or similar). After the population loop, add:

```cpp
refreshEpisodeMarkers();
```

### Task 4.3: New MainWindow slot — `onPlayLocalFileFromStreamRequested`

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Declare the slot in MainWindow.h**

In the public slots section:

```cpp
void onPlayLocalFileFromStreamRequested(const QString& localPath,
                                        const QString& imdbId,
                                        const QString& showTitle,
                                        int season,
                                        int episode);
```

- [ ] **Step 2: Implement in MainWindow.cpp**

```cpp
void MainWindow::onPlayLocalFileFromStreamRequested(
    const QString& localPath, const QString& imdbId,
    const QString& showTitle, int season, int episode)
{
    // STREAM_DOWNLOADED_LIBRARY Phase 4 — Stream-mode playback gateway for
    // bulk-downloaded episodes. Routes through the same VideoPlayer surface
    // VideosPage uses (openVideoPlayer) but layered with Stream-mode metadata
    // so subtitles + Continue Watching + back-stack-return all behave as a
    // Stream session.

    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
        DebugLogBuffer::instance().warning("stream",
            QStringLiteral("onPlayLocalFileFromStreamRequested: file missing"),
            QJsonObject{{"path", localPath}});
        return;
    }

    // Open the file via the existing local-file player path.
    openVideoPlayer(localPath);

    // Set the player's title from Stream-mode metadata (Cinemeta-rich) so
    // the HUD shows the show + episode title rather than the raw filename.
    if (m_videoPlayer) {
        const QString playerTitle = QStringLiteral("%1 · S%2E%3")
            .arg(showTitle)
            .arg(season, 2, 10, QLatin1Char('0'))
            .arg(episode, 2, 10, QLatin1Char('0'));
        m_videoPlayer->setVideoTitle(playerTitle);
    }

    // Fire the SubtitlesAggregator with a synthetic Stream so OpenSubs +
    // any other subtitle-resource addons populate the popover. Spec §10.2.
    if (m_subtitlesAggregator && !imdbId.isEmpty() && season > 0 && episode > 0) {
        tankostream::stream::SubtitleLoadRequest req;
        req.type = QStringLiteral("series");
        req.id = QStringLiteral("%1:%2:%3").arg(imdbId).arg(season).arg(episode);

        tankostream::addon::Stream synth;
        synth.behaviorHints.filename  = QFileInfo(localPath).fileName();
        synth.behaviorHints.videoSize = QFileInfo(localPath).size();
        synth.behaviorHints.videoHash = QString();   // omitted for local files
        synth.source.fileNameHint     = synth.behaviorHints.filename;
        synth.name                    = showTitle;
        req.selectedStream = synth;

        m_subtitlesAggregator->load(req);
    }

    // StreamProgress integration — write per-episode progress under the
    // existing epKey shape so Continue Watching strip picks up the entry
    // identically to a streamed playback. Spec §10.1.
    if (m_streamProgress) {
        const QString epKey = QStringLiteral("stream:%1:s%2:e%3")
            .arg(imdbId)
            .arg(season, 2, 10, QLatin1Char('0'))
            .arg(episode, 2, 10, QLatin1Char('0'));
        m_streamProgress->beginSession(epKey);
        // Per-tick saves are handled by the existing time_update IPC handler;
        // beginSession primes the epKey so saveTick has a target.
    }
}
```

(Names like `m_videoPlayer`, `m_subtitlesAggregator`, `m_streamProgress` match what the codebase already uses — verify via grep before pasting.)

- [ ] **Step 3: Add includes**

At the top of MainWindow.cpp (alongside other stream/aggregator includes):

```cpp
#include "core/stream/SubtitlesAggregator.h"
#include "core/stream/addon/StreamInfo.h"
```

### Task 4.4: Branch in `StreamDetailView::onEpisodeActivated`

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Add a new signal in StreamDetailView.h**

```cpp
signals:
    // STREAM_DOWNLOADED_LIBRARY Phase 4 — episode click resolved to a local
    // file. StreamPage forwards to MainWindow::onPlayLocalFileFromStreamRequested.
    void playLocalFileFromStreamRequested(const QString& localPath,
                                          const QString& imdbId,
                                          const QString& showTitle,
                                          int season,
                                          int episode);
```

- [ ] **Step 2: Branch in onEpisodeActivated**

Locate the existing `void StreamDetailView::onEpisodeActivated(int row, int /*col*/)` (line ~724 per earlier exploration). At the very TOP of the function, BEFORE any existing source-pick logic:

```cpp
if (m_downloadIndex && !m_currentImdbId.isEmpty() && m_currentSeason > 0) {
    const int episodeNum = m_episodeTable && m_episodeTable->item(row, 0)
        ? m_episodeTable->item(row, 0)->text().toInt() : 0;
    if (episodeNum > 0) {
        const auto pathOpt = m_downloadIndex->filePathFor(
            m_currentImdbId, m_currentSeason, episodeNum);
        if (pathOpt.has_value()) {
            // Lazy stat — if the user manually deleted the file, evict +
            // fall through to source-pick.
            if (QFileInfo::exists(*pathOpt)) {
                emit playLocalFileFromStreamRequested(
                    *pathOpt, m_currentImdbId, m_currentTitle,
                    m_currentSeason, episodeNum);
                return;  // skip source-pick
            } else {
                m_downloadIndex->evictByPath(
                    StreamDownloadIndex::computeCanonicalKey(*pathOpt));
                if (m_statusLabel)
                    m_statusLabel->setText(tr("File missing — falling back to streams."));
                // fall through to existing source-pick
            }
        }
    }
}
// existing source-pick logic continues below ↓
```

(Verify member names `m_currentImdbId`, `m_currentSeason`, `m_currentTitle`, `m_statusLabel` against the actual class — they may be named differently. Grep first.)

### Task 4.5: Wire the new signal through StreamPage to MainWindow

**Files:**
- Modify: `src/ui/pages/StreamPage.h`
- Modify: `src/ui/pages/StreamPage.cpp`

- [ ] **Step 1: Add the signal forwarding in StreamPage**

In StreamPage.h, add a signal:

```cpp
signals:
    void playLocalFileFromStreamRequested(const QString& localPath,
                                          const QString& imdbId,
                                          const QString& showTitle,
                                          int season,
                                          int episode);
```

In StreamPage.cpp, where the StreamDetailView is constructed (search for `new StreamDetailView`):

```cpp
connect(m_detailView, &StreamDetailView::playLocalFileFromStreamRequested,
        this,         &StreamPage::playLocalFileFromStreamRequested);
```

In `m_detailView` setup, also wire the StreamDownloadIndex:

```cpp
if (auto* mw = qobject_cast<MainWindow*>(window()))
    m_detailView->setStreamDownloadIndex(mw->streamDownloadIndex());
```

- [ ] **Step 2: Wire StreamPage's signal to MainWindow's slot**

In `MainWindow.cpp`, in the `buildPageStack` function (or wherever `m_streamPage` signals are connected — search for existing connect calls on `m_streamPage`):

```cpp
connect(m_streamPage, &StreamPage::playLocalFileFromStreamRequested,
        this,         &MainWindow::onPlayLocalFileFromStreamRequested);
```

### Task 4.6: Right-click "Show alternate streams"

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`
- Modify: `src/ui/pages/stream/StreamDetailView.h`

- [ ] **Step 1: Install a context-menu policy on the episode table**

In the StreamDetailView constructor (or wherever m_episodeTable is set up):

```cpp
m_episodeTable->setContextMenuPolicy(Qt::CustomContextMenu);
connect(m_episodeTable, &QWidget::customContextMenuRequested,
        this, &StreamDetailView::onEpisodeContextMenu);
```

- [ ] **Step 2: Declare and implement onEpisodeContextMenu**

In StreamDetailView.h:

```cpp
private slots:
    void onEpisodeContextMenu(const QPoint& pos);
```

Add a new signal:

```cpp
signals:
    void alternateStreamRequested(int episodeNum);
```

In StreamDetailView.cpp:

```cpp
#include <QMenu>

void StreamDetailView::onEpisodeContextMenu(const QPoint& pos)
{
    if (!m_episodeTable) return;
    const QModelIndex idx = m_episodeTable->indexAt(pos);
    if (!idx.isValid()) return;

    const int episodeNum = m_episodeTable->item(idx.row(), 0)
        ? m_episodeTable->item(idx.row(), 0)->text().toInt() : 0;
    if (episodeNum <= 0) return;

    QMenu menu(this);
    QAction* altAct = menu.addAction(tr("Show alternate streams"));
    QAction* picked = menu.exec(m_episodeTable->viewport()->mapToGlobal(pos));
    if (picked == altAct) {
        emit alternateStreamRequested(episodeNum);
    }
}
```

- [ ] **Step 3: Wire `alternateStreamRequested` to the existing source-pick flow in StreamPage**

Locate the existing slot/connect that handles "user picked an episode" → source-pick flow. The new path: when `alternateStreamRequested(episodeNum)` fires, run the SAME source-pick logic regardless of whether the episode is downloaded.

In StreamPage.cpp:

```cpp
connect(m_detailView, &StreamDetailView::alternateStreamRequested,
        this, [this](int episodeNum) {
            // Reuse the existing source-pick flow — same as if user clicked
            // an undownloaded episode. Spec §6.3 / §7.3.
            this->triggerStreamSourcePickForEpisode(episodeNum);
        });
```

(Replace `triggerStreamSourcePickForEpisode` with whatever the existing entry-point is for "open source list for episode N" — search for the function that fires when an undownloaded episode is clicked. May need a small refactor to extract the source-pick logic into a callable method if it's currently inlined in `onEpisodeActivated`.)

### Task 4.7: Build verify Phase 4

- [ ] **Step 1: Build**

```powershell
taskkill /F /IM Tankoban.exe 2>$null
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 && cmake --build out --parallel --target Tankoban'
```

Expected: `BUILD OK`. The Qt resource compile must pick up the new `downloaded.svg` (verify the build log mentions resources).

### Task 4.8: Smoke Phase 4

- [ ] **Step 1: Launch + claim MCP lane (per Phase 1 Task 1.6)**

- [ ] **Step 2: Navigate to a downloaded show's detail view**

Open Stream tab → click the show whose bulk landed in Phase 2. Open detail view.

- [ ] **Step 3: Verify per-episode markers**

The episode rows for episodes that are on disk should display the downloaded SVG icon next to their titles. Tooltip on hover should show `On disk: <filename>`.

- [ ] **Step 4: Click a downloaded episode → verify auto-play**

Click a row with the marker. The local-file VideoPlayer should launch immediately (no source-pick dialog). Title in HUD should be `<Show> · S0NE0N` per the format in `onPlayLocalFileFromStreamRequested`.

- [ ] **Step 5: Verify subtitles**

Open the subtitle popover. OpenSubs language entries should appear (assuming the show's imdbId has OpenSubs coverage — Invincible S01E01 is the standard test target with 62 entries). Pick one; verify it loads (subtitle text appears on screen).

- [ ] **Step 6: Close player → verify return-to-state**

Close the player via the back button. Should land back on StreamDetailView at the same episode row.

- [ ] **Step 7: Right-click an episode (downloaded or not) → verify context menu**

Right-click → "Show alternate streams" appears. Click it. Source-pick overlay should open. Cancel out.

- [ ] **Step 8: Verify Continue Watching**

Go back to Stream home. The downloaded episode should now appear in Continue Watching strip.

- [ ] **Step 9: Cleanup, commit, RTC**

---

## Phase 5 — Videos scanner skip + debounced rescan (~1.5 hrs)

**Goal:** VideosScanner skips files that StreamDownloadIndex marks as Stream-owned. VideosPage rescans (debounced 500ms) when the index changes.

**Files for this phase:**
- Modify: `src/core/scanner/ScannerUtils.cpp` (or wherever `walkVideosRoot` lives)
- Modify: `src/core/scanner/ScannerUtils.h`
- Modify: `src/ui/pages/VideosPage.cpp` (subscribe to entriesChanged + debounced rescan)
- Modify: `src/ui/pages/VideosPage.h`

### Task 5.1: Locate the scanner walker

- [ ] **Step 1: Find walkVideosRoot**

```bash
grep -rn "walkVideosRoot\|videoFound\|scanForVideos" src/core/scanner/ src/ui/pages/VideosPage.cpp | head -20
```

Identify the function in `ScannerUtils.cpp` (or VideosScanner.cpp) that walks a directory and emits `videoFound`. The skip-check goes immediately before each emit.

### Task 5.2: Add `StreamDownloadIndex` parameter to the scanner

**Files:**
- Modify: `src/core/scanner/ScannerUtils.h` (or wherever the scanner-class lives)
- Modify: `src/core/scanner/ScannerUtils.cpp`

- [ ] **Step 1: Add the index parameter**

The cleanest path: pass `StreamDownloadIndex* downloadIndex = nullptr` as an optional ctor arg or via a setter. Mirror however the existing scanner accepts dependencies (e.g., CancellationToken from Phase 4 of REPO_HYGIENE).

Add forward declaration:
```cpp
class StreamDownloadIndex;
```

Setter:
```cpp
void setStreamDownloadIndex(StreamDownloadIndex* idx);
```

Member:
```cpp
StreamDownloadIndex* m_downloadIndex = nullptr;
```

- [ ] **Step 2: Skip in the walker**

Inside the walker function, immediately BEFORE the existing `emit videoFound(...)` call:

```cpp
// STREAM_DOWNLOADED_LIBRARY Phase 5 — skip files Stream-mode owns. The
// download-index lookup is O(1) hash probe + mutex; cost ~1µs per file
// vs ~hundreds of µs for the existing QFileInfo + extension test, so no
// measurable scanner regression.
if (m_downloadIndex) {
    const QString canonicalKey = StreamDownloadIndex::computeCanonicalKey(absPath);
    if (m_downloadIndex->isStreamOwned(canonicalKey))
        continue;
}
emit videoFound(absPath, /* ... existing args ... */);
```

Add `#include "core/stream/StreamDownloadIndex.h"`.

### Task 5.3: VideosPage wires the scanner + subscribes to entriesChanged

**Files:**
- Modify: `src/ui/pages/VideosPage.h`
- Modify: `src/ui/pages/VideosPage.cpp`

- [ ] **Step 1: Add a setter for StreamDownloadIndex on VideosPage**

```cpp
class StreamDownloadIndex;

void setStreamDownloadIndex(StreamDownloadIndex* idx);

private:
    StreamDownloadIndex* m_downloadIndex = nullptr;
    QTimer* m_rescanDebounceTimer = nullptr;
```

- [ ] **Step 2: Implement the setter — wires scanner + subscription**

```cpp
#include "core/stream/StreamDownloadIndex.h"
#include <QTimer>

void VideosPage::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    m_downloadIndex = idx;

    // Pass the index to whatever scanner instance VideosPage owns. Search
    // for `m_scanner` or `VideosScanner *` in this file to find the member.
    if (m_scanner)
        m_scanner->setStreamDownloadIndex(idx);

    if (m_downloadIndex) {
        if (!m_rescanDebounceTimer) {
            m_rescanDebounceTimer = new QTimer(this);
            m_rescanDebounceTimer->setSingleShot(true);
            m_rescanDebounceTimer->setInterval(500);
            connect(m_rescanDebounceTimer, &QTimer::timeout, this, [this]() {
                // STREAM_DOWNLOADED_LIBRARY Phase 5 — debounced rescan on
                // download-index change. Collapses N back-to-back signals
                // (bulk completion landing N episodes) to one rescan.
                triggerScan();  // existing rescan entry — match real name
            });
        }
        connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged,
                this, [this]() {
                    if (m_rescanDebounceTimer)
                        m_rescanDebounceTimer->start();  // restart the window
                }, Qt::QueuedConnection);
    }
}
```

(Replace `triggerScan` with the actual rescan entry-point on VideosPage — likely `m_videosScanner->triggerScan()` or `rescan()` per Phase 4 audit work.)

- [ ] **Step 3: Wire from MainWindow**

In `MainWindow.cpp`, after `m_videosPage` and `m_streamDownloadIndex` are constructed:

```cpp
if (m_videosPage && m_streamDownloadIndex)
    m_videosPage->setStreamDownloadIndex(m_streamDownloadIndex);
```

### Task 5.4: Build verify + smoke Phase 5

- [ ] **Step 1: Build**

(Same as prior phases.)

- [ ] **Step 2: Smoke — verify Stream-owned files disappear from Videos**

Launch + Stream tab → confirm a show is `DOWNLOADED` per Phase 3. Switch to Videos tab → that show should NOT appear in the Videos library grid.

- [ ] **Step 3: Smoke — Remove from Library un-hides files**

Stream tab → right-click the badged show → Remove from Library. Wait ~1 second for debounced rescan. Switch to Videos tab → the show should re-appear in the Videos library grid (its files are visible to the scanner again).

- [ ] **Step 4: Cleanup, commit, RTC**

---

## Phase 6 — Migration scanner + dialog (~3 hrs)

**Goal:** First-launch auto-rescue: scan Videos roots for canonical-layout folders, match to Cinemeta, populate index, materialize StreamLibrary entries.

**Files for this phase:**
- Create: `src/core/stream/StreamRescueScanner.h`
- Create: `src/core/stream/StreamRescueScanner.cpp`
- Create: `src/ui/dialogs/StreamRescueProgressDialog.h`
- Create: `src/ui/dialogs/StreamRescueProgressDialog.cpp`
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`
- Modify: `CMakeLists.txt`

### Task 6.1: Create `StreamRescueScanner.h`

**Files:**
- Create: `src/core/stream/StreamRescueScanner.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

// STREAM_DOWNLOADED_LIBRARY 2026-05-10 — first-launch migration scanner.
// Walks Videos roots, regex-matches the canonical bulk-download layout,
// queries Cinemeta to resolve each show, registers per-episode entries
// in StreamDownloadIndex, and materializes StreamLibrary entries for
// matched shows. One-shot per migrationVersion; gated by
// <dataDir>/stream_downloads_meta.json.
//
// Spec: docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md §9.

#include <QObject>
#include <QStringList>
#include <QString>

class JsonStore;
class StreamDownloadIndex;
class StreamLibrary;
namespace tankostream::stream { class MetaAggregator; }

class StreamRescueScanner : public QObject
{
    Q_OBJECT

public:
    struct Stats {
        int showsScanned = 0;
        int showsMatched = 0;
        int showsAmbiguous = 0;
        int showsUnmatched = 0;
        int showsNetworkFailure = 0;
        int episodesRegistered = 0;
    };

    StreamRescueScanner(StreamDownloadIndex* index,
                        StreamLibrary* library,
                        tankostream::stream::MetaAggregator* meta,
                        JsonStore* metaStore,
                        const QStringList& videoRoots,
                        QObject* parent = nullptr);

    void start();   // off-thread; emits progressUpdate + complete
    void cancel();  // sets a cancellation flag; in-flight scan exits early

signals:
    void progressUpdate(int currentShowIndex, int totalShows, const QString& currentShowName);
    void complete(const Stats& stats);

private:
    StreamDownloadIndex* m_index;
    StreamLibrary* m_library;
    tankostream::stream::MetaAggregator* m_meta;
    JsonStore* m_metaStore;
    QStringList m_videoRoots;
    bool m_cancelled = false;
};
```

### Task 6.2: Create `StreamRescueScanner.cpp` — detection + registration

**Files:**
- Create: `src/core/stream/StreamRescueScanner.cpp`

- [ ] **Step 1: Write the file**

```cpp
#include "StreamRescueScanner.h"

#include "StreamDownloadIndex.h"
#include "StreamLibrary.h"
#include "MetaAggregator.h"
#include "addon/MetaItem.h"

#include "core/JsonStore.h"
#include "core/DebugLogBuffer.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QtConcurrent/QtConcurrent>

namespace {

// Spec §9.2 patterns.
const QRegularExpression kSeasonFolderRe(R"(^Season \d{2,3}$)");
const QRegularExpression kEpisodeFileRe(R"(^(.+) - S(\d{2,3})E(\d{2,3}) - (.+)\.(mkv|mp4|webm|m4v)$)",
                                         QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kYearSuffixRe(R"(\s*\(\d{4}\)\s*$)");

QString sanitizeShowName(const QString& raw)
{
    QString s = raw;
    s.remove(kYearSuffixRe);
    return s.trimmed();
}

struct EpisodeCandidate {
    QString showFolderName;
    QString showFolderPath;
    QString canonicalPath;
    int season;
    int episode;
};

QList<EpisodeCandidate> findCandidatesUnderRoot(const QString& root, bool& cancelled)
{
    QList<EpisodeCandidate> out;
    QDirIterator showIt(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    while (showIt.hasNext()) {
        if (cancelled) return out;
        const QString showPath = showIt.next();
        const QFileInfo showInfo(showPath);
        const QString showFolder = showInfo.fileName();

        QDirIterator seasonIt(showPath, QDir::Dirs | QDir::NoDotAndDotDot,
                              QDirIterator::NoIteratorFlags);
        while (seasonIt.hasNext()) {
            if (cancelled) return out;
            const QString seasonPath = seasonIt.next();
            const QFileInfo seasonInfo(seasonPath);
            if (!kSeasonFolderRe.match(seasonInfo.fileName()).hasMatch())
                continue;

            QDirIterator fileIt(seasonPath, QDir::Files, QDirIterator::NoIteratorFlags);
            while (fileIt.hasNext()) {
                if (cancelled) return out;
                const QString filePath = fileIt.next();
                const QFileInfo fi(filePath);
                const QRegularExpressionMatch m = kEpisodeFileRe.match(fi.fileName());
                if (!m.hasMatch()) continue;

                const QString fileShowTitle = m.captured(1);
                // Spec §9.2 sanity check: the regex's showTitle capture
                // must match the show folder name (after sanitization).
                if (sanitizeShowName(fileShowTitle).compare(
                        sanitizeShowName(showFolder), Qt::CaseInsensitive) != 0)
                    continue;

                EpisodeCandidate c;
                c.showFolderName = showFolder;
                c.showFolderPath = showPath;
                c.canonicalPath = filePath;
                c.season = m.captured(2).toInt();
                c.episode = m.captured(3).toInt();
                out.append(c);
            }
        }
    }
    return out;
}

} // namespace

StreamRescueScanner::StreamRescueScanner(StreamDownloadIndex* index,
                                         StreamLibrary* library,
                                         tankostream::stream::MetaAggregator* meta,
                                         JsonStore* metaStore,
                                         const QStringList& videoRoots,
                                         QObject* parent)
    : QObject(parent), m_index(index), m_library(library),
      m_meta(meta), m_metaStore(metaStore), m_videoRoots(videoRoots)
{
}

void StreamRescueScanner::cancel() { m_cancelled = true; }

void StreamRescueScanner::start()
{
    QtConcurrent::run([this]() {
        Stats stats;

        // ─── Step 1 — discover candidates across all roots ──────────────────
        QList<EpisodeCandidate> all;
        for (const QString& root : m_videoRoots) {
            if (m_cancelled) break;
            all.append(findCandidatesUnderRoot(root, m_cancelled));
        }

        // Group by show folder name → list of episodes.
        QHash<QString, QList<EpisodeCandidate>> byShow;
        for (const auto& c : all)
            byShow[c.showFolderName].append(c);

        const int totalShows = byShow.size();
        stats.showsScanned = totalShows;
        int idx = 0;

        // ─── Step 2 — Cinemeta lookup + register per show ───────────────────
        for (auto it = byShow.constBegin(); it != byShow.constEnd(); ++it) {
            if (m_cancelled) break;
            ++idx;
            QMetaObject::invokeMethod(this, [this, idx, totalShows, name=it.key()]() {
                emit progressUpdate(idx, totalShows, name);
            }, Qt::QueuedConnection);

            const QString showFolderName = it.key();
            const QString cleanName = sanitizeShowName(showFolderName);

            // Cinemeta search — series type only, blocking this worker thread.
            // Per spec §9.3.
            QList<tankostream::addon::MetaItem> results;
            bool networkOk = m_meta->searchSeriesByTitleBlocking(cleanName, results);
            if (!networkOk) { ++stats.showsNetworkFailure; continue; }
            if (results.isEmpty()) { ++stats.showsUnmatched; continue; }

            // Pick highest imdbRating series-type result.
            tankostream::addon::MetaItem chosen = results.first();
            if (results.size() > 1) {
                ++stats.showsAmbiguous;
                double bestRating = chosen.imdbRating.toDouble();
                for (const auto& r : results) {
                    if (r.type != QStringLiteral("series")) continue;
                    if (r.imdbRating.toDouble() > bestRating) {
                        bestRating = r.imdbRating.toDouble();
                        chosen = r;
                    }
                }
            }

            ++stats.showsMatched;

            // ─── Step 3 — register per-episode entries on GUI thread ────────
            const QString chosenImdb = chosen.id;
            QList<EpisodeCandidate> episodes = it.value();
            QMetaObject::invokeMethod(this, [this, chosenImdb, episodes, chosen,
                                              statsRef=&stats]() {
                if (m_index) {
                    for (const auto& c : episodes) {
                        const qint64 size = QFileInfo(c.canonicalPath).size();
                        if (size <= 0) continue;
                        m_index->registerEpisode(chosenImdb, c.season, c.episode,
                                                 c.canonicalPath,
                                                 QString(),  // empty groupId for migration
                                                 size);
                        ++statsRef->episodesRegistered;
                    }
                }
                if (m_library && !m_library->has(chosenImdb)) {
                    StreamLibraryEntry entry;
                    entry.imdb        = chosenImdb;
                    entry.type        = QStringLiteral("series");
                    entry.name        = chosen.name;
                    entry.year        = chosen.year;
                    entry.poster      = chosen.poster.toString();
                    entry.description = chosen.description;
                    entry.imdbRating  = chosen.imdbRating;
                    entry.addedAt     = QDateTime::currentMSecsSinceEpoch();
                    m_library->add(entry);
                }
            }, Qt::QueuedConnection);
        }

        // ─── Step 4 — write migration-version pin + emit complete ────────────
        if (!m_cancelled && m_metaStore) {
            QJsonObject meta;
            meta[QStringLiteral("migrationVersion")] = 1;
            meta[QStringLiteral("completedAt")] =
                static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            QJsonObject st;
            st[QStringLiteral("shows_matched")]      = stats.showsMatched;
            st[QStringLiteral("shows_unmatched")]    = stats.showsUnmatched;
            st[QStringLiteral("shows_ambiguous")]    = stats.showsAmbiguous;
            st[QStringLiteral("shows_net_failure")]  = stats.showsNetworkFailure;
            st[QStringLiteral("episodes_registered")] = stats.episodesRegistered;
            meta[QStringLiteral("stats")] = st;
            m_metaStore->write(QStringLiteral("stream_downloads_meta.json"), meta);
        }

        QMetaObject::invokeMethod(this, [this, stats]() {
            emit complete(stats);
        }, Qt::QueuedConnection);
    });
}
```

**Note:** This file references `MetaAggregator::searchSeriesByTitleBlocking`. The existing `MetaAggregator` likely has an async signal-based API (e.g., `searchCatalog` + a `seriesMetaReady` signal). You may need to either:
- Add a blocking helper to `MetaAggregator` (a small wrapper that uses `QEventLoop` to wait for the signal), OR
- Restructure `StreamRescueScanner::start` to use the async signal pattern (more invasive).

For this plan, prefer adding a thin `searchSeriesByTitleBlocking(title, &results)` helper on `MetaAggregator` as a Phase-6 sub-task. Per Tankoban's existing patterns, this pattern (eventLoop wait inside QtConcurrent::run worker) is acceptable.

### Task 6.3: Create `StreamRescueProgressDialog`

**Files:**
- Create: `src/ui/dialogs/StreamRescueProgressDialog.h`
- Create: `src/ui/dialogs/StreamRescueProgressDialog.cpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

// STREAM_DOWNLOADED_LIBRARY 2026-05-10 — modal progress dialog for the
// first-launch migration scan. Spec §9.6.

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class StreamRescueScanner;

class StreamRescueProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StreamRescueProgressDialog(StreamRescueScanner* scanner,
                                        QWidget* parent = nullptr);

private slots:
    void onProgress(int current, int total, const QString& showName);
    void onComplete(int matched, int unmatched, int ambiguous,
                    int episodes);

private:
    StreamRescueScanner* m_scanner;
    QLabel* m_status = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};
```

- [ ] **Step 2: Write the implementation**

```cpp
#include "StreamRescueProgressDialog.h"

#include "core/stream/StreamRescueScanner.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

StreamRescueProgressDialog::StreamRescueProgressDialog(StreamRescueScanner* scanner,
                                                       QWidget* parent)
    : QDialog(parent), m_scanner(scanner)
{
    setObjectName(QStringLiteral("StreamRescueProgressDialog"));
    setWindowTitle(tr("Migrating downloaded shows to Stream library"));
    setModal(true);
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);

    auto* heading = new QLabel(tr("Scanning your Videos folders for previously "
                                  "downloaded shows..."), this);
    heading->setWordWrap(true);
    root->addWidget(heading);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    root->addWidget(m_bar);

    m_status = new QLabel(tr("Starting..."), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    btnRow->addWidget(m_cancelBtn);
    root->addLayout(btnRow);

    if (m_scanner) {
        connect(m_scanner, &StreamRescueScanner::progressUpdate,
                this, &StreamRescueProgressDialog::onProgress);
        connect(m_scanner, &StreamRescueScanner::complete, this,
                [this](const StreamRescueScanner::Stats& s) {
                    onComplete(s.showsMatched, s.showsUnmatched,
                               s.showsAmbiguous, s.episodesRegistered);
                });
        connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
            if (m_scanner) m_scanner->cancel();
            close();
        });
    }
}

void StreamRescueProgressDialog::onProgress(int current, int total, const QString& showName)
{
    if (m_bar && total > 0) {
        m_bar->setRange(0, total);
        m_bar->setValue(current);
    }
    if (m_status)
        m_status->setText(tr("Processing %1 (%2 of %3)").arg(showName).arg(current).arg(total));
}

void StreamRescueProgressDialog::onComplete(int matched, int unmatched,
                                            int ambiguous, int episodes)
{
    if (m_status) {
        m_status->setText(tr("Done. Added %1 shows and %2 episodes to Stream library. "
                             "%3 shows could not be matched to Cinemeta and remain in Videos.")
                          .arg(matched).arg(episodes).arg(unmatched + ambiguous));
    }
    if (m_cancelBtn)
        m_cancelBtn->setText(tr("Close"));
    if (m_bar)
        m_bar->setValue(m_bar->maximum());
    disconnect(m_cancelBtn, nullptr, nullptr, nullptr);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::accept);
}
```

### Task 6.4: MainWindow boot gate

**Files:**
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Schedule rescue scan after boot**

In `MainWindow::MainWindow(...)` after `m_streamDownloadIndex` and `m_streamLibrary` are constructed AND the window is shown, add:

```cpp
QTimer::singleShot(0, this, [this]() {
    if (!m_jsonStore) return;
    const QJsonObject meta = m_jsonStore->read(QStringLiteral("stream_downloads_meta.json"));
    const int storedVersion = meta.value(QStringLiteral("migrationVersion")).toInt(0);
    if (storedVersion >= 1) return;  // already migrated

    // Get Videos roots from VideoCategoryStore (or wherever they live).
    QStringList videoRoots = m_categoryStore
        ? m_categoryStore->rootsForCategory(QStringLiteral("videos"))
        : QStringList();
    if (videoRoots.isEmpty()) {
        // No Videos roots — nothing to migrate. Pin migrationVersion so we
        // don't re-check on every launch.
        QJsonObject pin;
        pin[QStringLiteral("migrationVersion")] = 1;
        pin[QStringLiteral("completedAt")] =
            static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        m_jsonStore->write(QStringLiteral("stream_downloads_meta.json"), pin);
        return;
    }

    auto* scanner = new StreamRescueScanner(
        m_streamDownloadIndex, m_streamLibrary, m_metaAggregator,
        m_jsonStore, videoRoots, this);
    auto* dlg = new StreamRescueProgressDialog(scanner, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    scanner->start();
});
```

(Replace member names `m_categoryStore`, `m_metaAggregator`, `m_jsonStore` with whatever the codebase uses — grep first.)

Add includes:

```cpp
#include "core/stream/StreamRescueScanner.h"
#include "ui/dialogs/StreamRescueProgressDialog.h"
```

### Task 6.5: Register new files in CMakeLists.txt

- [ ] **Step 1: Add to SOURCES**

```cmake
    src/core/stream/StreamRescueScanner.cpp
    src/ui/dialogs/StreamRescueProgressDialog.cpp
```

- [ ] **Step 2: Add to HEADERS**

```cmake
    src/core/stream/StreamRescueScanner.h
    src/ui/dialogs/StreamRescueProgressDialog.h
```

### Task 6.6: Add `MetaAggregator::searchSeriesByTitleBlocking` helper

**Files:**
- Modify: `src/core/stream/MetaAggregator.h`
- Modify: `src/core/stream/MetaAggregator.cpp`

- [ ] **Step 1: Declare**

```cpp
// STREAM_DOWNLOADED_LIBRARY Phase 6 — synchronous helper for
// StreamRescueScanner. Uses internal QEventLoop on the calling
// (worker) thread to wait for the existing async query to complete.
// Returns false on network failure; true with empty results on
// "no matches".
bool searchSeriesByTitleBlocking(const QString& title,
                                 QList<tankostream::addon::MetaItem>& results,
                                 int timeoutMs = 8000);
```

- [ ] **Step 2: Implement**

In `MetaAggregator.cpp`:

```cpp
#include <QEventLoop>
#include <QTimer>

bool MetaAggregator::searchSeriesByTitleBlocking(const QString& title,
                                                 QList<tankostream::addon::MetaItem>& outResults,
                                                 int timeoutMs)
{
    outResults.clear();
    bool ok = false;
    bool networkErr = false;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    auto onResults = [&](const QList<tankostream::addon::MetaItem>& items) {
        outResults = items;
        ok = true;
        loop.quit();
    };
    auto onError = [&](const QString&) {
        networkErr = true;
        loop.quit();
    };
    auto onTimeout = [&]() {
        networkErr = true;
        loop.quit();
    };

    auto cReady = connect(this, &MetaAggregator::seriesSearchReady, this, onResults);
    auto cErr = connect(this, &MetaAggregator::seriesSearchFailed, this, onError);
    auto cTo = connect(&timeout, &QTimer::timeout, &loop, onTimeout);

    timeout.start(timeoutMs);
    searchSeriesByTitle(title);  // existing async fire — name may differ
    loop.exec();

    disconnect(cReady);
    disconnect(cErr);
    disconnect(cTo);

    return ok && !networkErr;
}
```

(If `MetaAggregator` doesn't have a `searchSeriesByTitle` async method today, this task expands into adding that method too — driven by the existing addon-search infrastructure. Inspect `MetaAggregator.h` first to confirm the actual surface and adapt.)

### Task 6.7: Build verify Phase 6

- [ ] **Step 1: Build**

(Same as prior phases.)

### Task 6.8: Smoke Phase 6

- [ ] **Step 1: Force a clean migration state**

```powershell
$dataDir = [Environment]::GetFolderPath('LocalApplicationData') + "\Tankoban\data"
del "$dataDir\stream_downloads_meta.json" -ErrorAction SilentlyContinue
del "$dataDir\stream_downloads.json" -ErrorAction SilentlyContinue
```

- [ ] **Step 2: Launch Tankoban**

The progress dialog should appear at boot.

- [ ] **Step 3: Verify migration runs against existing canonical-layout content**

If you have any pre-existing bulk-shape folders in Videos (`<Videos root>/<Show>/Season NN/<Show> - SxxEyy - <Title>.mkv`), they should be discovered and matched to Cinemeta. Otherwise the dialog completes immediately with `0 shows matched`.

- [ ] **Step 4: Verify migration is idempotent**

Close + relaunch Tankoban. The dialog should NOT appear (migrationVersion pinned).

- [ ] **Step 5: Verify the matched shows now show in Stream library home with the DOWNLOADED badge AND have disappeared from Videos library**

- [ ] **Step 6: Cleanup, commit, RTC**

---

## Phase 7 — Disk-state validation + bulk-in-flight Remove dialog (~2 hrs)

**Goal:** Eager validateAll on Stream home open; bulk-in-flight Remove confirmation dialog with cancel-and-remove atomic action.

**Files for this phase:**
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp` (call validateAll on home open)
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp` (Remove action checks for active bulk + dialog)

### Task 7.1: Eager `validateAll` on Stream home open

**Files:**
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp`

- [ ] **Step 1: Call validateAll on showEvent**

In `StreamLibraryLayout.cpp`, override `showEvent` (or hook into whatever existing visibility callback fires when the home view becomes visible):

```cpp
void StreamLibraryLayout::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_downloadIndex) {
        // STREAM_DOWNLOADED_LIBRARY Phase 7 — eager disk-state validation
        // when the user opens Stream library home. Off-thread; lazy on click
        // continues to be the per-episode safety net. Spec §10.4.
        QtConcurrent::run([idx=m_downloadIndex]() { idx->validateAll(); });
    }
}
```

Add `#include <QtConcurrent/QtConcurrent>` and `#include <QShowEvent>`.

If `showEvent` is not overridable (e.g., StreamLibraryLayout is not a QWidget subclass), put the call in whatever method runs when the user navigates to Stream home (e.g., `StreamPage::showEvent` or `StreamHomeBoard::showEvent`).

### Task 7.2: Bulk-in-flight Remove confirmation dialog

**Files:**
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp`

- [ ] **Step 1: Locate the existing Remove from Library action**

```bash
grep -n "Remove from Library\|removeFromLibrary\|m_removeAction" src/ui/pages/stream/ src/ui/pages/StreamPage.cpp | head -10
```

- [ ] **Step 2: Wrap the existing Remove handler with a bulk-active check**

Before invoking `m_streamLibrary->remove(imdb)`, check `TorrentClient` for any active stream-bulk groups with this imdb:

```cpp
auto onRemoveClicked = [this, imdb]() {
    bool bulkActive = false;
    if (m_torrentClient) {
        bulkActive = m_torrentClient->hasActiveStreamBulkForImdb(imdb);
    }

    if (bulkActive) {
        // STREAM_DOWNLOADED_LIBRARY Phase 7 — confirmation dialog. Spec §10.10.
        QMessageBox box(this);
        box.setWindowTitle(tr("Cancel active bulk download?"));
        box.setText(tr("This show has an active bulk download in progress.\n"
                       "Cancel the download first, then Remove from Library?"));
        QPushButton* cancelAndRemoveBtn = box.addButton(tr("Cancel download + Remove"),
                                                       QMessageBox::DestructiveRole);
        QPushButton* abortBtn = box.addButton(tr("Keep downloading"),
                                              QMessageBox::RejectRole);
        box.setDefaultButton(abortBtn);
        box.exec();

        if (box.clickedButton() == cancelAndRemoveBtn) {
            // Cancel the bulk first (existing API per Phase 8 of bulk spec)
            m_torrentClient->cancelStreamBulkGroupsForImdb(imdb);
            // Then remove from library (which evicts the index)
            m_streamLibrary->remove(imdb);
        }
        return;
    }

    // No bulk active — straight remove.
    m_streamLibrary->remove(imdb);
};
```

(Names `hasActiveStreamBulkForImdb` and `cancelStreamBulkGroupsForImdb` may not exist; you may need to add thin wrappers around the existing per-group API. Alternatively, expose a `streamGroupsForImdb(imdb)` query and iterate to cancel each.)

Add `#include <QMessageBox>` and `#include <QPushButton>`.

### Task 7.3: Build verify + smoke Phase 7

- [ ] **Step 1: Build**

(Same as prior phases.)

- [ ] **Step 2: Smoke validateAll**

Manually delete one of the bulk-downloaded files via File Explorer. Open Stream tab → home view. The episode's downloaded marker (if you navigate to detail view) should be gone within ~1 second; the file should re-appear in Videos on next debounced rescan.

- [ ] **Step 3: Smoke bulk-in-flight Remove**

Start a bulk download. While it's still downloading (group row in Tankorent shows progress), navigate to Stream → right-click the show → Remove from Library. The confirmation dialog should appear. Click "Cancel download + Remove". Verify: bulk cancels, library entry clears, any landed files re-appear in Videos.

- [ ] **Step 4: Cleanup, commit, RTC (final phase RTC)**

---

## Phase 8 — Final integration verification (~1 hr)

**Goal:** End-to-end smoke covering all the user-facing flows.

### Task 8.1: Full happy-path smoke

- [ ] **Step 1: Fresh state**

Optionally clear `stream_downloads.json` and `stream_downloads_meta.json` to re-run migration.

- [ ] **Step 2: Start a small bulk (~2 episodes)**

- [ ] **Step 3: Wait for both episodes to complete + rename**

- [ ] **Step 4: Verify Stream library home shows the DOWNLOADED badge**

- [ ] **Step 5: Verify Videos mode does NOT show the show**

- [ ] **Step 6: Open the show's StreamDetailView**

Verify the two downloaded episodes show markers; other episodes don't.

- [ ] **Step 7: Click a downloaded episode**

Verify auto-play (no source-pick); HUD shows Cinemeta-rich title; subtitle popover offers OpenSubs entries.

- [ ] **Step 8: Right-click an episode → Show alternate streams**

Verify source-pick overlay opens.

- [ ] **Step 9: Close player → verify return to Stream detail view**

- [ ] **Step 10: Stream home → Continue Watching strip shows the just-played episode**

- [ ] **Step 11: Right-click show in Stream library → Remove from Library**

Verify: tile disappears, Videos rescan picks up the files, files re-appear in Videos library.

- [ ] **Step 12: Final commit + RTC**

---

## Self-review checklist (run before declaring plan done)

- [ ] **Spec coverage:** Every section in the spec (§1 Intent, §2 Scope, §3 P1–P6, §4 Data Model, §5 Architecture, §6 Data Flow, §7 UI Specs, §8 Videos integration, §9 Migration, §10 Lifecycle/edge cases) maps to a task above. Cross-checked.
- [ ] **No placeholders:** No "TBD" / "TODO" / "implement later" / "fill in details" in any task. Every code-changing step shows the code.
- [ ] **Type consistency:** `StreamDownloadIndex::Entry` field names match across the header, the implementation, the bulk-completion hook, the migration scanner, and the `setExternalTracks` reference site. `computeCanonicalKey` and `computeEpisodeKey` are referenced by the same names everywhere they're called.
- [ ] **Path consistency:** Spec uses `stream_downloads.json` / `stream_downloads_meta.json` — both names used identically in plan.
- [ ] **Build commands:** Each phase ends with the same kill+build invocation. PowerShell quoting honored throughout.

---

## Open implementation-time decisions (defer to executor)

1. **Exact icon glyph for `downloaded.svg`** — the placeholder in Task 4.1 may need a design pass. Substitute any existing grayscale arrow asset if better available.
2. **Exact column index for the episode-row marker** in `StreamDetailView::m_episodeTable` — verify against existing layout before pasting into Task 4.2.
3. **`triggerScan` / `rescan` method name on VideosPage** — match the actual rescan entry per Phase 4 audit work.
4. **Whether `MetaAggregator::searchSeriesByTitle` already exists** as an async method or needs adding — Task 6.6 may expand if not.
5. **`m_torrentClient->hasActiveStreamBulkForImdb` API** — may need adding (Task 7.2 step 2).
6. **Order of `m_jsonStore` / `m_categoryStore` access in MainWindow** — match existing code's accessor pattern, not the plan's verbatim `m_xxx` references.

These don't change the design — they're "match the existing codebase conventions" details.

---

**End of plan. ~775 LOC across 8 phases / ~30 sub-tasks. Estimated total executor time: ~16-20 hours including build cycles + smoke at each phase boundary.**
