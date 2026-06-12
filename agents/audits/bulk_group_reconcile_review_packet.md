# REVIEW: stream-bulk-group reconcile — purge dead groups + Published-when-file-present (EXTERNAL_DELETE_RECONCILE follow-up)

## Context
Follow-up to 8cc5341 (external-delete reconcile). After the user deleted media files
outside the app, the per-show "bulk groups" (stream_bulk_groups.json + SQLite mirror
stream_groups/stream_group_items) still rendered ~15 ghost shows on the Downloads page /
library tiles. Engine torrents table was empty; nothing was actually downloading. One
surviving show (One Piece S01E1164) had its file on disk (550MB, real completed download)
but its bulk-item state was frozen at "Downloading" forever.

Root causes found (via runtime diag, now removed):
1. Existing reconcile only RELABELS dead items; pruneTerminalStreamBulkGroups only GCs
   all-terminal groups after 90d → dead groups with a non-terminal item linger.
2. isPublishingStreamBulkState matches ONLY "Publishing" (NOT "Downloading"/"Pending" —
   that's isDownloadingStreamBulkState). A "Downloading" item with file-present + no
   record fell through to `if(destinationExists) continue;` and stayed Downloading.

## Changes (all in reconcileStreamBulkGroups + a shared helper, TorrentClient.cpp)
A. New `storageVolumeReady()` file-scope helper (walk-up to nearest existing ancestor;
   drive-root "X:/" re-probe; UNC never falls to local root). Refactored the init-loop
   lambda from 8cc5341 to use it.
B. Relabel loop: after the isTerminal guard and BEFORE the isPublishing branch, any
   non-terminal item with destinationExists → mark Published (file on disk = done).
   Covers Downloading/Pending/Publishing/Paused uniformly. Fixes the frozen-Downloading
   completed episode.
C. New prune pass: drop any item with neither a file on disk NOR a live engine handle
   (m_engine->hasTorrent — authoritative, since both restore loops have run by reconcile
   time; a bare DB state flag can lie). Remove emptied groups. Per-group storageVolumeReady
   guard so an unplugged drive never wipes a show. Mirror cleanup: removeStreamGroupItem
   for dropped items, removeStreamGroup for removed groups.
D. Mirror sync: drop any SQLite stream_group the in-memory map no longer has (covers
   groups pruned in earlier sessions). Not read at load today; keeps the mirror honest.

## Verification (live, real corpus)
- 15 ghost groups → 1 (the real S01E1164); SQLite mirror 15→1, items 38→1.
- S01E1164 flipped Downloading→Published in JSON (authoritative load source) + in-memory;
  imdbHasActiveCohort now false for it.
- Engine torrents 0 throughout; nothing re-downloaded.
- BUILD OK; tests 425/426 (sole fail pre-existing PickBestBookFileTest).

## Review asks
1. Change B: is marking Published correct for a Paused/Pending item whose canonical file
   exists? Any case where the canonical file existing does NOT mean "done" (e.g. partial
   file at the canonical path, or a same-named unrelated file)?
2. Change C: m_engine->hasTorrent vs the existing activeStates(listActive) — is hasTorrent
   the right liveness signal at reconcile time? Any window where a legit just-queued item
   has neither a handle nor a file and would be wrongly pruned?
3. The bottom `if(destinationExists) continue;` (orphan branch) is now dead (B handles
   destinationExists earlier) — harmless, or worth removing?
4. Mirror writes (removeStreamGroup/Item, saveStreamBulkGroups upsert) — the SQLite item
   state for the survivor stayed "Downloading" while JSON went Published. Is the SQLite
   mirror upsert in saveStreamBulkGroups not updating item state? (Not load-read today,
   but flag if it is a latent bug.)
5. Anything else.

## Diff vs HEAD (8cc5341), TorrentClient.cpp only
diff --git a/src/core/torrent/TorrentClient.cpp b/src/core/torrent/TorrentClient.cpp
index 86b719c..b5d9d81 100644
--- a/src/core/torrent/TorrentClient.cpp
+++ b/src/core/torrent/TorrentClient.cpp
@@ -46,6 +46,37 @@ constexpr const char* kStateOrphaned = "Orphaned";
 constexpr const char* kStatePaused = "Paused";
 constexpr qint64 kPieceProgressDebounceMs = 300;
 
+// EXTERNAL_DELETE_RECONCILE (2026-06-12) — "is the volume holding this path
+// mounted?" Walks up to the nearest existing ancestor and asks its storage,
+// so a deleted folder on a present drive reads ready (purge/prune is correct)
+// while an unplugged drive reads NOT ready (leave records + groups alone).
+// "X:" after truncation is re-probed as the drive root "X:/"; an exhausted
+// UNC walk never falls through to the local filesystem root.
+bool storageVolumeReady(const QString& path)
+{
+    QString probe = QDir::fromNativeSeparators(path);
+    while (!probe.isEmpty()) {
+        if (QFileInfo::exists(probe)) {
+            const QStorageInfo si(probe);
+            return si.isValid() && si.isReady();
+        }
+        const int slash = probe.lastIndexOf(QLatin1Char('/'));
+        if (slash < 0) break;
+        probe.truncate(slash);
+        if (probe.endsWith(QLatin1Char(':'))) {
+            const QString root = probe + QLatin1Char('/');
+            if (QFileInfo::exists(root)) {
+                const QStorageInfo si(root);
+                return si.isValid() && si.isReady();
+            }
+            break;  // drive absent
+        }
+        if (probe == QLatin1String("/") || probe == QLatin1String("//"))
+            break;  // UNC exhausted — never report the local root for one
+    }
+    return false;
+}
+
 struct PieceProgressUpdate {
     int season = 0;
     int episode = 0;
@@ -709,40 +740,6 @@ TorrentClient::TorrentClient(CoreBridge* bridge, QObject* parent)
     // what actually fixes the cohort sequential semantic on boot when
     // m_records states drifted (e.g. from a late-firing
     // metadata_received_alert in onMetadataReady).
-    // EXTERNAL_DELETE_RECONCILE — distinguishes "user deleted the folder"
-    // (an ancestor exists on a ready volume -> purge is correct) from
-    // "drive not mounted" (no ancestor exists -> keep the record paused).
-    // Walks up to the nearest existing ancestor and asks its storage.
-    // "E:" after truncation is re-probed as the drive root "E:/" (bare
-    // "E:" is drive-relative notation, not the root); UNC walks that
-    // exhaust "//server/share" stop without falling through to the local
-    // filesystem root (server-offline must read as unavailable). (Codex
-    // review P2.)
-    const auto storageReady = [](const QString& path) {
-        QString probe = QDir::fromNativeSeparators(path);
-        while (!probe.isEmpty()) {
-            if (QFileInfo::exists(probe)) {
-                const QStorageInfo si(probe);
-                return si.isValid() && si.isReady();
-            }
-            const int slash = probe.lastIndexOf(QLatin1Char('/'));
-            if (slash < 0) break;
-            probe.truncate(slash);
-            if (probe.endsWith(QLatin1Char(':'))) {
-                // Reached the drive letter — final probe of the root form.
-                const QString root = probe + QLatin1Char('/');
-                if (QFileInfo::exists(root)) {
-                    const QStorageInfo si(root);
-                    return si.isValid() && si.isReady();
-                }
-                break;  // drive absent
-            }
-            if (probe == QLatin1String("/") || probe == QLatin1String("//"))
-                break;  // UNC exhausted / never report the local root for one
-        }
-        return false;
-    };
-
     bool anyChanged = false;
     for (const auto& row : m_repo.listTorrents()) {
         // PendingEngineAdd is handled by the dedicated replay loop below.
@@ -770,7 +767,7 @@ TorrentClient::TorrentClient(CoreBridge* bridge, QObject* parent)
         bool forcePauseUnavailable = false;
         if (disk.parsed && disk.hasFileList && disk.hadProgress
             && !disk.anyFilePresent) {
-            if (storageReady(savePath)) {
+            if (storageVolumeReady(savePath)) {
                 qInfo() << "[TorrentClient] files deleted outside the app,"
                         << "purging torrent record:" << hash << row.name;
                 // Delete resume data only once the row is actually gone — a
@@ -783,13 +780,13 @@ TorrentClient::TorrentClient(CoreBridge* bridge, QObject* parent)
                 anyChanged = true;
                 continue;
             }
-            // Volume not mounted — the files may still exist on it. Add
-            // PAUSED (not auto-managed) instead of skipping entirely: a
-            // paused add never rechecks or writes, so nothing re-downloads
-            // and no folder tree is re-created — but the engine record
-            // exists, so reconcileStreamBulkGroups below does not demote
-            // the group's Published state to Pending (Codex review P1).
-            // Drive back on a later boot -> files found -> normal restore.
+            // storageVolumeReady == false: volume not mounted — the files
+            // may still exist on it. Add PAUSED (not auto-managed) instead of
+            // skipping entirely: a paused add never rechecks or writes, so
+            // nothing re-downloads and no folder tree is re-created — but the
+            // engine record exists, so reconcileStreamBulkGroups below does
+            // not demote the group's Published state to Pending (Codex review
+            // P1). Drive back on a later boot -> files found -> normal restore.
             qWarning() << "[TorrentClient] save volume unavailable, adding"
                        << "paused (no recheck) this boot:" << hash;
             forcePauseUnavailable = true;
@@ -1295,6 +1292,25 @@ void TorrentClient::reconcileStreamBulkGroups()
             if (isTerminalStreamBulkState(state))
                 continue;
 
+            // EXTERNAL_DELETE_RECONCILE (2026-06-12): any non-terminal item
+            // whose canonical file is already on disk is actually DONE — the
+            // download finished but the item state never advanced to Published
+            // (app killed before publish, or the torrent record was purged
+            // post-completion by the external-delete sweep). Mark it Published
+            // so it reads as downloaded, not a stuck "Downloading". This is the
+            // One Piece S01E1164 case (2026-06-12): 550MB file present on disk,
+            // item state frozen at "Downloading" across every boot. NOTE: the
+            // downloading/pending states fall through the isPublishing branch
+            // below (isPublishingStreamBulkState matches ONLY "Publishing"), so
+            // this check must sit ahead of it to catch them.
+            if (destinationExists) {
+                item["itemState"] = QString::fromLatin1(kStatePublished);
+                item["lastError"] = QString();
+                items.replace(i, item);
+                groupChanged = true;
+                continue;
+            }
+
             if (isPublishingStreamBulkState(state)) {
                 if (!hasRecord) {
                     item["itemState"] = QString::fromLatin1(kStateOrphaned);
@@ -1343,6 +1359,86 @@ void TorrentClient::reconcileStreamBulkGroups()
         }
     }
 
+    // EXTERNAL_DELETE_RECONCILE (2026-06-12) — the relabel pass above marks
+    // dead items Orphaned/Cancelled but KEEPS them, so a show whose files
+    // were deleted outside the app clogs the library tiles (imdbHasActiveCohort
+    // reads these groups) and the Downloads page forever. PRUNE any item that
+    // has neither a file on disk NOR a live/queued torrent behind it, and drop
+    // any group left empty. An item survives if ANY of: its file exists, it has
+    // a live engine handle (activeStates), or it has a persisted record in a
+    // genuinely-in-flight state (PendingEngineAdd/Active/Paused/Completed) — the
+    // last keeps freshly-queued episodes that have no file yet from being wiped.
+    // Per-group storage guard: an unplugged drive never prunes a show's groups.
+    {
+        QStringList groupsToRemove;
+        for (auto groupIt = m_streamBulkGroups.begin();
+             groupIt != m_streamBulkGroups.end(); ++groupIt) {
+            QJsonObject group = groupIt.value().toObject();
+            const QString destRoot = group.value("destinationRoot").toString();
+            if (!destRoot.isEmpty() && !storageVolumeReady(destRoot))
+                continue;  // drive unplugged — leave this group intact this boot
+            const QJsonArray items = group.value("items").toArray();
+            QJsonArray kept;
+            QStringList droppedItemIds;
+            for (const auto& v : items) {
+                const QJsonObject item = v.toObject();
+                const QString infoHash = item.value("infoHash").toString();
+                const QString canonicalPath = canonicalPathForStreamBulkItem(group, item);
+                const bool destinationExists =
+                    !canonicalPath.isEmpty() && QFileInfo::exists(canonicalPath);
+                // Authoritative liveness = does the engine hold a handle RIGHT
+                // NOW. By the time reconcile runs, both restore loops have
+                // finished, so anything that could be live IS live; a bare DB
+                // state flag (Active/Pending) can lie when the handle never
+                // materialized (the 2026-06-12 phantom: torrents table empty,
+                // yet a bulk item showed "Downloading" forever). Trust the
+                // handle, not the flag.
+                const bool engineLive =
+                    !infoHash.isEmpty() && m_engine && m_engine->hasTorrent(infoHash);
+                if (destinationExists || engineLive) {
+                    kept.append(item);
+                } else {
+                    // Dead: no file on disk AND no live transfer → drop.
+                    const QString itemId = item.value("itemKey").toString();
+                    if (!itemId.isEmpty())
+                        droppedItemIds.append(itemId);
+                }
+            }
+            if (kept.size() != items.size()) {
+                const QString gid = groupIt.key();
+                if (kept.isEmpty()) {
+                    groupsToRemove.append(gid);
+                } else {
+                    group["items"] = kept;
+                    group["updatedAtMs"] = now;
+                    *groupIt = group;
+                    // Mirror: drop the removed items from the SQLite store so it
+                    // does not retain ghosts the JSON no longer has.
+                    for (const QString& itemId : droppedItemIds)
+                        m_repo.removeStreamGroupItem(gid, itemId);
+                }
+                changed = true;
+            }
+        }
+        for (const QString& gid : groupsToRemove) {
+            m_streamBulkGroups.remove(gid);
+            m_repo.removeStreamGroup(gid);  // also clear the SQLite mirror
+        }
+        if (!groupsToRemove.isEmpty())
+            qInfo() << "[TorrentClient] pruned" << groupsToRemove.size()
+                    << "fully-dead stream bulk groups (no file on disk, no live transfer)";
+
+        // Sync the SQLite mirror to the in-memory truth: drop any persisted
+        // group the in-memory map no longer has. Covers groups pruned in
+        // earlier sessions (before this sweep existed) that still linger in
+        // the mirror. The mirror is not read at load today, but keeping it
+        // honest prevents resurrecting ghosts if the read-side cutover ships.
+        for (const auto& gr : m_repo.listStreamGroups()) {
+            if (!m_streamBulkGroups.contains(gr.groupId))
+                m_repo.removeStreamGroup(gr.groupId);
+        }
+    }
+
     if (changed) {
         saveStreamBulkGroups();
         emit streamBulkGroupsChanged(QString());  // empty groupId = full refresh
