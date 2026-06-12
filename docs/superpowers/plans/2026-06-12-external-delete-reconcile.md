# External-Delete Reconcile Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Files deleted outside the app stay deleted — on startup, torrents whose data vanished from disk are purged (never silently re-downloaded), and stale download-index entries are evicted so episodes show as yet-to-be-downloaded.

**Architecture:** Two independent layers. (1) A disk-state probe in TorrentEngine (parses the `.fastresume`, reports prior-progress + any-file-present) consulted by TorrentClient's startup restore loop: had progress + zero files on disk + storage available → purge the DB row + resume file instead of re-adding. (2) A deterministic startup call to the already-existing `StreamDownloadIndex::validateAll()` (today it only runs lazily on Theatre-home showEvent), off-thread, evicting Complete entries whose files are gone.

**Tech Stack:** libtorrent RC_2_0 (`read_resume_data`, `add_torrent_params`), Qt6 (QStorageInfo, QtConcurrent).

**Trigger incident (2026-06-12):** Hemanth freed 250 GB by deleting `Desktop\Media` contents outside the app. On relaunch, the restore loop re-added all 23 persisted torrents; libtorrent rechecked, found nothing, and re-downloaded ~10 GB (Community S01–S06 pack at 7 MB/s) before being caught. 26 stale index entries (Community/Mythic Quest/One Piece) still claim Complete for missing files.

**Out of scope (tracked separately):** the queue-promotion vs engine-pause fight discovered the same day (queue auto-resumes engine-paused torrents; no UI pause exists post-revert so not user-facing today). `validateAll()`'s lack of a drive-unplugged guard (pre-existing exposure on the showEvent path; single-C:-drive machine).

---

### Task 1: TorrentEngine::resumeDataDiskState probe

**Files:**
- Modify: `src/core/torrent/TorrentEngine.h` (next to `addFromResume` declaration)
- Modify: `src/core/torrent/TorrentEngine.cpp` (next to `addFromResume` definition, ~line 732)

Engine API is Congress-6 frozen against rename/removal; **additions are allowed** (precedent: T1 promotion gates).

- [ ] **Step 1: Declare the struct + method in TorrentEngine.h**

```cpp
    // EXTERNAL_DELETE_RECONCILE (2026-06-12) — pre-add disk probe for the
    // startup restore loop. Parses a .fastresume WITHOUT touching the session
    // and reports whether the torrent had prior progress and whether any of
    // its files still exist under savePath. TorrentClient uses this to detect
    // "files deleted outside the app" and purge instead of re-download.
    struct ResumeDiskState {
        bool parsed = false;          // resume file existed + read_resume_data ok
        bool hasFileList = false;     // atp.ti present (metadata known)
        bool hadProgress = false;     // any piece downloaded per resume data
        bool anyFilePresent = false;  // >=1 of the torrent's files exists on disk
    };
    ResumeDiskState resumeDataDiskState(const QString& resumePath,
                                        const QString& savePath) const;
```

- [ ] **Step 2: Implement in TorrentEngine.cpp**

```cpp
TorrentEngine::ResumeDiskState TorrentEngine::resumeDataDiskState(
    const QString& resumePath, const QString& savePath) const
{
    ResumeDiskState st;
    QFile file(resumePath);
    if (!file.open(QIODevice::ReadOnly)) return st;
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) return st;

    lt::error_code ec;
    lt::add_torrent_params atp = lt::read_resume_data(
        lt::span<const char>(data.data(), static_cast<int>(data.size())), ec);
    if (ec) return st;
    st.parsed = true;

    // Progress evidence: any verified piece, or recorded download volume.
    st.hadProgress = atp.have_pieces.count() > 0 || atp.total_downloaded > 0;

    if (!atp.ti) return st;  // magnet without metadata — files unknown
    st.hasFileList = true;

    // libtorrent file_path() uses '\' on Windows (see
    // feedback_libtorrent_windows_backslash_separator) — normalize before
    // joining. Early-exit on the first file found: a pack with some episodes
    // deleted but others alive must NOT be treated as externally deleted.
    const lt::file_storage& fs = atp.ti->files();
    const QDir base(savePath);
    for (const lt::file_index_t i : fs.file_range()) {
        QString rel = QString::fromStdString(fs.file_path(i));
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (QFileInfo::exists(base.filePath(rel))) {
            st.anyFilePresent = true;
            break;
        }
    }
    return st;
}
```

- [ ] **Step 3: Verify includes** — `TorrentEngine.cpp` already has `<libtorrent/read_resume_data.hpp>` (line 17) and `<libtorrent/torrent_info.hpp>` (line 20). Confirm `QFileInfo`/`QDir` are included; add `#include <QFileInfo>` / `#include <QDir>` if absent.

- [ ] **Step 4: Compile check** — `build_check.bat` (default `out` lane). Expected: `BUILD OK`.

---

### Task 2: TorrentClient restore-loop purge guard

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp:711-739` (the addFromResume restore loop)

- [ ] **Step 1: Add the storage-availability walk-up helper** (file-local lambda or static, above the loop)

A deleted folder on a present drive must purge; an unplugged drive must NOT. Walk up to the nearest existing ancestor and ask its storage:

```cpp
    // EXTERNAL_DELETE_RECONCILE — distinguishes "user deleted the folder"
    // (ancestor exists on a ready volume -> purge is correct) from "drive
    // not mounted" (no ancestor exists -> keep the record, skip this boot).
    const auto storageReady = [](const QString& path) {
        QString probe = QDir::fromNativeSeparators(path);
        while (!probe.isEmpty()) {
            if (QFileInfo::exists(probe))
                return QStorageInfo(probe).isReady();
            const int slash = probe.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0) break;
            probe.truncate(slash);
        }
        return false;
    };
```

- [ ] **Step 2: Insert the guard before `addFromResume`** (between `const bool shouldPause = ...` and `const QString restored = m_engine->addFromResume(...)`)

```cpp
        // EXTERNAL_DELETE_RECONCILE (2026-06-12): if this torrent HAD
        // progress but none of its files exist anymore, the user deleted
        // them outside the app. Re-adding would make libtorrent recheck,
        // find nothing, and silently re-download the whole thing (the
        // 2026-06-12 incident: 23 torrents, ~10 GB re-pulled). Deleted
        // stays deleted: drop the row + resume data instead.
        const auto disk = m_engine->resumeDataDiskState(resumePath, savePath);
        if (disk.parsed && disk.hasFileList && disk.hadProgress
            && !disk.anyFilePresent) {
            if (!storageReady(savePath)) {
                // Volume not mounted — files may still exist elsewhere.
                // Keep the record but do not add (also avoids re-creating
                // the folder tree on the wrong volume this boot).
                qWarning() << "[TorrentClient] save volume unavailable,"
                           << "skipping restore this boot:" << hash;
                continue;
            }
            qInfo() << "[TorrentClient] files deleted outside the app,"
                    << "purging torrent record:" << hash << row.name;
            m_repo.removeTorrent(hash);
            QFile::remove(resumePath);
            anyChanged = true;
            continue;
        }
```

- [ ] **Step 3: Verify includes** — TorrentClient.cpp needs `<QStorageInfo>` (likely absent — add it). `QFileInfo`/`QDir`/`QFile` almost certainly present; verify.

- [ ] **Step 4: Compile check** — `build_check.bat`. Expected: `BUILD OK`.

---

### Task 3: Deterministic index sweep at startup

**Files:**
- Modify: `src/ui/MainWindow.cpp:814-823` (after `setRepository` + `setStreamDownloadIndex` wiring)

- [ ] **Step 1: Insert the off-thread sweep after the index↔client wiring block (~line 823)**

Same fire-and-forget pattern as `StreamLibraryLayout::showEvent` (StreamLibraryLayout.cpp:102). `validateAll()` snapshots under lock, stats off-lock, evicts under lock — safe off-thread by design.

```cpp
    // EXTERNAL_DELETE_RECONCILE (2026-06-12): sweep download-index entries
    // whose files were deleted outside the app, at every boot — not just
    // when the user happens to open the Theatre library home (the
    // StreamLibraryLayout::showEvent path). Off the GUI thread; ~one stat
    // per entry. Episodes whose files vanished revert to not-downloaded.
    if (m_streamDownloadIndex) {
        StreamDownloadIndex* idx = m_streamDownloadIndex;
        (void) QtConcurrent::run([idx]() { idx->validateAll(); });
    }
```

- [ ] **Step 2: Verify includes** — MainWindow.cpp needs `<QtConcurrent/QtConcurrent>`; check (other call sites in repo use it — copy their include form). CMake already links Qt6::Concurrent (StreamLibraryLayout uses it in the same target).

- [ ] **Step 3: Compile check** — `build_check.bat`. Expected: `BUILD OK`.

---

### Task 4: Gates + live end-to-end verification + commit

No new pure-logic unit (the risk is libtorrent parsing + boot ordering, not branch logic; a truth-table test would only restate the if-condition). Verification is a REAL end-to-end repro via the dev bridge:

- [ ] **Step 1: Full test suite** — `ctest` in `out/` (vcvars scope). Expected: 425/426 (sole pre-existing PickBestBookFileTest failure).

- [ ] **Step 2: Live repro smoke (Layer 2 — uses today's real corpus)**
  1. Launch the new build. The 26 stale index entries (Community tt1439629 ×13, Mythic Quest tt8879940 ×8, One Piece tt0388629 ×5) must be evicted at boot.
  2. Verify: `out\tankoctl.exe get-downloads` → entries whose `canonicalPath` no longer exists are GONE (count drops; remaining entries' paths all exist).

- [ ] **Step 3: Live repro smoke (Layer 1 — manufactured)**
  1. `out\tankoctl.exe sources-add-magnet <well-seeded magnet>` , let it reach >0% (`get-torrents` shows progress), then close the app cleanly.
  2. Delete the torrent's files from `Desktop\Media\...` manually (simulating the user).
  3. Relaunch. Expected log line: `files deleted outside the app, purging torrent record`. `get-torrents` does NOT list it; no re-download starts; the row is gone from the DB on next boot too (purge persisted).
  4. Negative control: a torrent whose files DO exist restores normally.

- [ ] **Step 4: Cross-engine review** — Codex packet: this plan + diff; ask specifically about (a) `have_pieces.count()` semantics on completed-seed resume data, (b) the `continue` paths' interaction with the `PendingEngineAdd` replay loop below, (c) any caller depending on Error-state rows that this now removes.

- [ ] **Step 5: Commit**

```bash
git add src/core/torrent/TorrentEngine.h src/core/torrent/TorrentEngine.cpp \
        src/core/torrent/TorrentClient.cpp src/ui/MainWindow.cpp \
        docs/superpowers/plans/2026-06-12-external-delete-reconcile.md
git commit -m "feat(torrent): externally-deleted files stay deleted — startup purge guard + deterministic index sweep (EXTERNAL_DELETE_RECONCILE)"
```
