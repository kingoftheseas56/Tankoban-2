# REVIEW: EXTERNAL_DELETE_RECONCILE — purge externally-deleted torrents at startup

## Context (incident 2026-06-12)
User freed 250GB by deleting Desktop\Media contents OUTSIDE the app. On relaunch the
restore loop re-added all 23 persisted torrents; libtorrent rechecked, found nothing,
re-downloaded ~10GB before being caught. Separately, 26 StreamDownloadIndex entries
kept claiming Complete for deleted files. User directive: "deleted show files remain
deleted and are shown as yet to be downloaded inside the app."

## Definition of Done
1. Startup restore loop: torrent with prior progress + ZERO files on disk + storage
   volume available => purge (repo row + .fastresume), skip add. Volume NOT available
   => keep record, skip add this boot (drive-unplugged is not deletion).
2. No metadata (magnet-era resume, no ti) or no prior progress => restore exactly as
   before (no behavior change).
3. StreamDownloadIndex::validateAll() (pre-existing, was showEvent-lazy-only) now also
   runs once at startup, off-thread, after setStreamDownloadIndex wiring.
4. TorrentEngine gains resumeDataDiskState() probe — pure file-IO, no session/mutex.
   Engine API is Congress-6 frozen against rename/remove; this is an ADDITION.

## Live verification already done (real end-to-end)
- Layer 2: launched on real corpus — 26 stale entries -> 1 (the survivor's file exists).
- Layer 1: ubuntu ISO magnet, downloaded to 6% (13MB/s), graceful close (resume
  persisted, 546KB), deleted files manually, relaunched: torrent count 0, .fastresume
  deleted, no re-download, folder not re-created. Negative control implicitly: the One
  Piece surviving entry + earlier boots restored fine.
- Full suite: pending (running in parallel with this review).

## Review asks (priority order)
1. resumeDataDiskState: is `atp.have_pieces.count() > 0 || atp.total_downloaded > 0`
   the right "had progress" evidence across resume-data generations (completed seeds,
   partial, magnet-era)? Any case where a HEALTHY torrent has neither but has files?
2. The purge `continue` paths vs the rest of init: PendingEngineAdd replay loop below,
   reconcileStreamBulkGroups / retryStreamBulkPublishing after — anything that expects
   the purged row/hash to still exist (e.g. stream bulk groups referencing the hash)?
3. storageReady walk-up lambda: correctness on UNC paths / relative paths / paths with
   no existing ancestor; QStorageInfo::isReady semantics on Windows.
4. The off-thread validateAll at MainWindow:825ish — any race with the backfill
   reconciles inside setStreamDownloadIndex (they run synchronously before, but do they
   schedule deferred work that registers entries AFTER my sweep snapshots)?
5. Anything else.

## Diff (working tree vs HEAD 6cffb99, my 4 files only)
diff --git a/src/core/torrent/TorrentClient.cpp b/src/core/torrent/TorrentClient.cpp
index cce3c69..365d5f7 100644
--- a/src/core/torrent/TorrentClient.cpp
+++ b/src/core/torrent/TorrentClient.cpp
@@ -23,6 +23,7 @@
 #include <QHash>
 #include <QJsonValue>
 #include <QSet>
+#include <QStorageInfo>  // EXTERNAL_DELETE_RECONCILE — restore-loop volume probe
 #include <QStringList>
 #include <QTimer>
 #include <QDebug>
@@ -708,6 +709,23 @@ TorrentClient::TorrentClient(CoreBridge* bridge, QObject* parent)
     // what actually fixes the cohort sequential semantic on boot when
     // m_records states drifted (e.g. from a late-firing
     // metadata_received_alert in onMetadataReady).
+    // EXTERNAL_DELETE_RECONCILE — distinguishes "user deleted the folder"
+    // (an ancestor exists on a ready volume -> purge is correct) from
+    // "drive not mounted" (no ancestor exists -> keep the record, skip
+    // this boot). Walks up to the nearest existing ancestor and asks its
+    // storage.
+    const auto storageReady = [](const QString& path) {
+        QString probe = QDir::fromNativeSeparators(path);
+        while (!probe.isEmpty()) {
+            if (QFileInfo::exists(probe))
+                return QStorageInfo(probe).isReady();
+            const int slash = probe.lastIndexOf(QLatin1Char('/'));
+            if (slash <= 0) break;
+            probe.truncate(slash);
+        }
+        return false;
+    };
+
     bool anyChanged = false;
     for (const auto& row : m_repo.listTorrents()) {
         // PendingEngineAdd is handled by the dedicated replay loop below.
@@ -725,6 +743,31 @@ TorrentClient::TorrentClient(CoreBridge* bridge, QObject* parent)
         // Paused stays paused; everything else (Active, Completed, Error) resumes
         const bool shouldPause = (row.state == tankoban::torrent::TorrentState::Paused);
 
+        // EXTERNAL_DELETE_RECONCILE (2026-06-12): if this torrent HAD
+        // progress but none of its files exist anymore, the user deleted
+        // them outside the app. Re-adding would make libtorrent recheck,
+        // find nothing, and silently re-download the whole thing (the
+        // 2026-06-12 incident: 23 torrents, ~10 GB re-pulled before being
+        // caught). Deleted stays deleted: drop the row + resume data.
+        const auto disk = m_engine->resumeDataDiskState(resumePath, savePath);
+        if (disk.parsed && disk.hasFileList && disk.hadProgress
+            && !disk.anyFilePresent) {
+            if (!storageReady(savePath)) {
+                // Volume not mounted — the files may still exist on it.
+                // Keep the record; skip the add so nothing re-downloads
+                // (or re-creates the folder tree) this boot.
+                qWarning() << "[TorrentClient] save volume unavailable,"
+                           << "skipping restore this boot:" << hash;
+                continue;
+            }
+            qInfo() << "[TorrentClient] files deleted outside the app,"
+                    << "purging torrent record:" << hash << row.name;
+            m_repo.removeTorrent(hash);
+            QFile::remove(resumePath);
+            anyChanged = true;
+            continue;
+        }
+
         const QString restored = m_engine->addFromResume(resumePath, savePath, shouldPause);
         if (restored.isEmpty()) {
             qWarning() << "Orphaned torrent record (no resume data):" << hash;
diff --git a/src/core/torrent/TorrentEngine.cpp b/src/core/torrent/TorrentEngine.cpp
index d38a194..e0be0fd 100644
--- a/src/core/torrent/TorrentEngine.cpp
+++ b/src/core/torrent/TorrentEngine.cpp
@@ -5,6 +5,7 @@
 
 #include <QDir>
 #include <QFile>
+#include <QFileInfo>
 #include <QJsonObject>
 #include <QDebug>
 #include <QTimer>
@@ -781,6 +782,47 @@ QString TorrentEngine::addFromResume(const QString& resumePath,
     return hash;
 }
 
+// EXTERNAL_DELETE_RECONCILE (2026-06-12) — see header. Pure file-IO probe:
+// parses the .fastresume off-session, so it is safe to call before start()
+// adds any handles and needs no mutex (touches no m_records / m_session).
+TorrentEngine::ResumeDiskState TorrentEngine::resumeDataDiskState(
+    const QString& resumePath, const QString& savePath) const
+{
+    ResumeDiskState st;
+    QFile file(resumePath);
+    if (!file.open(QIODevice::ReadOnly)) return st;
+    const QByteArray data = file.readAll();
+    file.close();
+    if (data.isEmpty()) return st;
+
+    lt::error_code ec;
+    lt::add_torrent_params atp = lt::read_resume_data(
+        lt::span<const char>(data.data(), static_cast<int>(data.size())), ec);
+    if (ec) return st;
+    st.parsed = true;
+
+    // Progress evidence: any verified piece, or recorded download volume.
+    st.hadProgress = atp.have_pieces.count() > 0 || atp.total_downloaded > 0;
+
+    if (!atp.ti) return st;  // magnet without metadata — files unknown
+    st.hasFileList = true;
+
+    // libtorrent file_path() uses '\' on Windows — normalize before joining.
+    // Early-exit on the first file found: a pack with some episodes deleted
+    // but others alive must NOT be treated as externally deleted.
+    const lt::file_storage& fs = atp.ti->files();
+    const QDir base(savePath);
+    for (const lt::file_index_t i : fs.file_range()) {
+        QString rel = QString::fromStdString(fs.file_path(i));
+        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
+        if (QFileInfo::exists(base.filePath(rel))) {
+            st.anyFilePresent = true;
+            break;
+        }
+    }
+    return st;
+}
+
 void TorrentEngine::setFilePriorities(const QString& infoHash, const QVector<int>& priorities)
 {
     QMutexLocker lock(&m_mutex);
diff --git a/src/core/torrent/TorrentEngine.h b/src/core/torrent/TorrentEngine.h
index f0fbe71..ac25d7d 100644
--- a/src/core/torrent/TorrentEngine.h
+++ b/src/core/torrent/TorrentEngine.h
@@ -110,6 +110,19 @@ public:
     // Torrent operations (all thread-safe)
     QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused = true);
     QString addFromResume(const QString& resumePath, const QString& savePath, bool paused);
+    // EXTERNAL_DELETE_RECONCILE (2026-06-12) — pre-add disk probe for the
+    // startup restore loop. Parses a .fastresume WITHOUT touching the session
+    // and reports whether the torrent had prior progress and whether any of
+    // its files still exist under savePath. TorrentClient uses this to detect
+    // "files deleted outside the app" and purge instead of re-download.
+    struct ResumeDiskState {
+        bool parsed = false;          // resume file existed + read_resume_data ok
+        bool hasFileList = false;     // metadata known (file list available)
+        bool hadProgress = false;     // any piece downloaded per resume data
+        bool anyFilePresent = false;  // >=1 of the torrent's files exists on disk
+    };
+    ResumeDiskState resumeDataDiskState(const QString& resumePath,
+                                        const QString& savePath) const;
     void    setFilePriorities(const QString& infoHash, const QVector<int>& priorities);
     void    renameFile(const QString& infoHash, int fileIndex, const QString& newName);
     void    setSequentialDownload(const QString& infoHash, bool sequential);
diff --git a/src/ui/MainWindow.cpp b/src/ui/MainWindow.cpp
index e9c6b18..69f86ab 100644
--- a/src/ui/MainWindow.cpp
+++ b/src/ui/MainWindow.cpp
@@ -52,6 +52,7 @@
 #include <QMouseEvent>
 #include <QMetaObject>
 #include <QSettings>
+#include <QtConcurrent/QtConcurrent>  // EXTERNAL_DELETE_RECONCILE — startup index sweep
 #include <QWindowStateChangeEvent>
 
 #ifdef Q_OS_WIN
@@ -822,6 +823,18 @@ void MainWindow::buildPageStack()
     if (m_streamDownloadIndex)
         torrentClient->setStreamDownloadIndex(m_streamDownloadIndex);
 
+    // EXTERNAL_DELETE_RECONCILE (2026-06-12): sweep download-index entries
+    // whose files were deleted outside the app, at every boot — not just
+    // when the user happens to open the Theatre library home (the
+    // StreamLibraryLayout::showEvent path). Off the GUI thread; ~one stat
+    // per entry. Episodes whose files vanished revert to not-downloaded.
+    // Runs after setStreamDownloadIndex so the backfill/reconcile passes
+    // above have already registered their (existing-file) entries.
+    if (m_streamDownloadIndex) {
+        StreamDownloadIndex* idx = m_streamDownloadIndex;
+        (void) QtConcurrent::run([idx]() { idx->validateAll(); });
+    }
+
     // Stream page — m_streamPage cache (STREAM_ADD_TO_TANKORENT 2026-05-06)
     // so we can wire the magnet-handoff signal without a qobject_cast walk.
     m_streamPage = new StreamPage(m_bridge, torrentClient);
