OpenAI Codex v0.131.0
--------
workdir: C:\Users\Suprabha\Desktop\Tankoban 2
model: gpt-5.5
provider: openai
approval: never
sandbox: read-only
reasoning effort: high
reasoning summaries: none
session id: 019e8c78-0c39-7ca3-a3f4-e862a88f164e
--------
user
Cross-model review for Tankoban 2 (requested by Agent 0). You are a DIFFERENT model than the authors (these fixes were written by Agents 1/2/4/5 and overnight autonomous sessions — NOT by you). Your job is to check this working-tree diff against the written Definition of Done below, not just read the code. This is a read-only review — do NOT edit any files; just report.

CONTEXT: This is the "freeze-fix + observability" bundle from an overnight session. It is currently uncommitted in the working tree and compiled into a responsive build (out/Tankoban.exe). Several fixes move work between threads (sync->async), which is exactly the class where a clean compile + a short smoke can still hide a data race, use-after-free, or lifetime bug. That is why it is being reviewed before it lands on master.

DEFINITION OF DONE (verify the diff against EACH item):

A001 — StreamDetailView episode-list refresh storm (StreamAggregator.{cpp,h}, StreamPage.{cpp,h}, StreamDetailView.{cpp,h}, MainWindow.cpp):
  A1. Rapid re-clicks between two episodes in the Theatre stream list no longer apply late emits from superseded loads (no "ghost rows").
  A2. A load-generation token gates the result handler: results from a stale generation are discarded, not applied.
  A3. No new thread-safety hole introduced by the token mechanism (token read/write race, dangling QPointer, etc.).

A003+A004 — Books + TankoLibrary GUI-thread blocking (BooksCatalogueLibraryStore.{cpp,h}, BooksPage.cpp, TankoLibraryPage.cpp):
  B1. validateAll() stat-checking every book is moved OFF the GUI thread (QtConcurrent::run) — large library load no longer freezes the UI.
  B2. Per-row cover-glob scanning is replaced by a pre-computed QHash scanned once (no per-row disk glob on the GUI thread).
  B3. BooksCatalogueLibraryStore.save() no longer blocks the GUI thread on disk write — it uses a coalescing async writer.
  B4. CRITICAL for the async writer: no data loss / torn writes / lost-update under rapid successive saves; writes are atomic; no use-after-free if the store is destroyed with a write in flight; concurrent save() calls are safe.
  B5. The async results that touch UI are marshaled back to the GUI thread safely (no cross-thread widget access).

A005 — StreamLibraryLayout cleanupOrphanPosters off GUI thread (StreamLibraryLayout.cpp):
  C1. cleanupOrphanPosters() (QDir::entryList + per-file remove) is moved off the GUI thread via QtConcurrent::run during library refresh.
  C2. A QPointer (or equivalent) guard prevents use-after-free / cross-thread widget access if the page is torn down while the cleanup task is in flight.
  C3. No double-delete or race if refresh() is called repeatedly (multiple overlapping cleanup tasks).

OBS-1 — Timer-census diagnostic bridge (SystemIntrospection.{cpp,h}, tankoctl.cpp):
  D1. Adds a `diag-timer-census` command that enumerates active QTimers (interval + owning object) in the process.
  D2. ZERO behavior change to normal runtime — read-only introspection, no mutation of timers or app state.
  D3. Safe to call while the app is running (no race walking the QObject tree from the bridge).

OBS-2a — Hang watchdog (NEW src/devtools/HangWatchdog.{h,cpp}, main.cpp, cmake/TankobanSources.cmake):
  E1. A background thread monitors a GUI heartbeat (atomic bumped ~every 16ms by a QTimer on the GUI thread).
  E2. If the GUI goes silent >750ms it writes out/HANG_DETECTED.json (timestamp + stall duration); it auto-clears on recovery.
  E3. Detection-only: no effect on normal runtime behavior; gated behind --dev-control only.
  E4. The watchdog thread shuts down cleanly on app exit (joined/stopped — no crash-on-exit, no detached thread touching freed state).
  E5. The atomic heartbeat is correctly synchronized (proper memory ordering; no torn reads across threads).

YOUR REVIEW — do all four:
1. For EACH Definition-of-Done item (A1..A3, B1..B5, C1..C3, D1..D3, E1..E5): state MET / NOT-MET / PARTIAL with one line of evidence from the diff.
2. Flag anything the diff DOES that the DoD never asked for (scope creep / unrequested behavior change).
3. Correctness + security pass: real bugs, regressions (especially threading: data races, use-after-free, lifetime/ownership across QtConcurrent boundaries, unjoined threads), leaked secrets, unsafe input/file handling.
4. Anything the DoD SHOULD have specified but didn't (gap in intent itself).

END with exactly one line: APPROVE or REQUEST-CHANGES, plus a one-sentence reason. Be terse; default to REQUEST-CHANGES if any DoD item is NOT-MET or you are unsure.

================ DIFF UNDER REVIEW (working tree, code files only) ================
diff --git a/cmake/TankobanSources.cmake b/cmake/TankobanSources.cmake
index 28e49f1..83d88c8 100644
--- a/cmake/TankobanSources.cmake
+++ b/cmake/TankobanSources.cmake
@@ -11,6 +11,7 @@ set(SOURCES
     src/devtools/DevControlServer.cpp
     src/devtools/UiInteractionDispatcher.cpp
     src/devtools/SystemIntrospection.cpp
+    src/devtools/HangWatchdog.cpp
     src/core/JsonStore.cpp
     src/core/CoreBridge.cpp
     src/core/LibraryScanner.cpp
@@ -250,6 +251,7 @@ set(HEADERS
     src/devtools/DevControlServer.h
     src/devtools/UiInteractionDispatcher.h
     src/devtools/SystemIntrospection.h
+    src/devtools/HangWatchdog.h
     src/core/JsonStore.h
     src/core/CoreBridge.h
     src/core/LibraryScanner.h
diff --git a/src/core/book/BooksCatalogueLibraryStore.cpp b/src/core/book/BooksCatalogueLibraryStore.cpp
index e30589a..b6b3df4 100644
--- a/src/core/book/BooksCatalogueLibraryStore.cpp
+++ b/src/core/book/BooksCatalogueLibraryStore.cpp
@@ -8,6 +8,7 @@
 #include <QJsonObject>
 #include <QMutexLocker>
 #include <QSaveFile>
+#include <QtConcurrent/QtConcurrent>
 
 BooksCatalogueLibraryStore::BooksCatalogueLibraryStore(const QString& dataDir,
                                                        QObject* parent)
@@ -16,6 +17,25 @@ BooksCatalogueLibraryStore::BooksCatalogueLibraryStore(const QString& dataDir,
     // Lazy load so tests can construct before calling load().
 }
 
+BooksCatalogueLibraryStore::~BooksCatalogueLibraryStore()
+{
+    // The async save writer captures `this`; block until it finishes so it
+    // never writes through a destroyed store. Any bytes queued after the writer
+    // started are flushed by its internal drain loop before it returns. Copy the
+    // future out under the lock, then wait WITHOUT holding m_saveMutex — the
+    // writer needs that lock to make progress (waiting under it would deadlock).
+    QFuture<void> fut;
+    {
+        QMutexLocker wlk(&m_saveMutex);
+        fut = m_saveFuture;
+    }
+    // Always wait (not just when isRunning()): a task that is queued in the
+    // thread pool but not yet scheduled is not "running" yet, and skipping the
+    // wait there would let it run through a destroyed store. waitForFinished()
+    // is a no-op on a default-constructed or already-finished future.
+    fut.waitForFinished();
+}
+
 // ── Mutate ──────────────────────────────────────────────────────────────────
 
 void BooksCatalogueLibraryStore::upsertRecord(const CatalogueRecord& r)
@@ -77,22 +97,33 @@ void BooksCatalogueLibraryStore::evictByCatalogueId(const QString& catalogueId)
 
 void BooksCatalogueLibraryStore::validateAll()
 {
-    QList<QString> toEvict;
+    // Snapshot (catalogueId, relative filePath) under the lock, then stat the
+    // disk OFF the lock. validateAll() now runs on a background thread
+    // (BooksPage migrates its call sites to QtConcurrent::run); holding m_mutex
+    // across the per-record QFileInfo::exists() disk stats would block every
+    // GUI-thread read (all()/recordFor()/…) on file I/O — the exact hitch the
+    // off-thread move is meant to remove. Mirrors StreamDownloadIndex's
+    // snapshot-under-lock / stat-off-lock validateAll contract.
+    struct Pending { QString id; QString rel; };
+    QList<Pending> snapshot;
     {
         QMutexLocker lk(&m_mutex);
+        snapshot.reserve(m_byId.size());
         for (auto it = m_byId.constBegin(); it != m_byId.constEnd(); ++it) {
             const auto& rec = it.value();
             if (rec.filePath.isEmpty()) continue;
-            // CatalogueRecord.filePath is the canonical relative path under
-            // Books root; the absolute resolution against m_dataDir is the
-            // only existence-check signal. Mirrors StreamDownloadIndex's
-            // single-path validateAll pattern.
-            const QString abs = QDir(m_dataDir).absoluteFilePath(rec.filePath);
-            if (!QFileInfo::exists(abs)) {
-                toEvict.append(it.key());
-            }
+            snapshot.append({it.key(), rec.filePath});
         }
     }
+
+    QList<QString> toEvict;
+    for (const auto& p : snapshot) {
+        // CatalogueRecord.filePath is the canonical relative path under Books
+        // root; the absolute resolution against m_dataDir is the only
+        // existence-check signal.
+        const QString abs = QDir(m_dataDir).absoluteFilePath(p.rel);
+        if (!QFileInfo::exists(abs)) toEvict.append(p.id);
+    }
     for (const auto& id : toEvict) evictByCatalogueId(id);
 }
 
@@ -191,6 +222,9 @@ void BooksCatalogueLibraryStore::load()
 
 void BooksCatalogueLibraryStore::save()
 {
+    // 1) Serialize the CURRENT state to bytes synchronously under m_mutex. This
+    //    is in-memory only (fast); the slow part — the disk write — is handed
+    //    off below so the calling (usually GUI) thread never blocks on file I/O.
     QJsonArray records;
     {
         QMutexLocker lk(&m_mutex);
@@ -201,13 +235,51 @@ void BooksCatalogueLibraryStore::save()
     QJsonObject root;
     root[QStringLiteral("schemaVersion")] = kSchemaVersion;
     root[QStringLiteral("records")] = records;
+    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
 
-    QDir().mkpath(m_dataDir);
-    const QString path = QDir(m_dataDir).absoluteFilePath(QString::fromLatin1(FILENAME));
-    QSaveFile f(path);
-    if (!f.open(QIODevice::WriteOnly)) return;
-    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
-    f.commit();
+    // 2) Publish the newest snapshot as the pending write. If a background
+    //    writer is already running it will pick these bytes up on its next loop
+    //    (coalescing bursts of saves into one write of the latest state). Only
+    //    the first caller launches the writer. m_pendingSaveBytes is never empty
+    //    for real data, so emptiness is a safe "nothing queued" sentinel.
+    //
+    //    save() can run concurrently (GUI thread + the async validateAll worker
+    //    that evicts → save), so the launch decision, the QtConcurrent dispatch,
+    //    and the m_saveFuture store all happen INSIDE one m_saveMutex section —
+    //    that keeps m_saveFuture in lock-step with m_saveInFlight (the destructor
+    //    relies on it). QtConcurrent::run only enqueues here; the worker body
+    //    runs on a pool thread and blocks on m_saveMutex until we release, so
+    //    dispatching under the lock cannot deadlock.
+    QMutexLocker wlk(&m_saveMutex);
+    m_pendingSaveBytes = bytes;
+    if (m_saveInFlight) return;
+    m_saveInFlight = true;
+
+    const QString dataDir = m_dataDir;
+    m_saveFuture = QtConcurrent::run([this, dataDir]() {
+        QDir().mkpath(dataDir);
+        const QString path =
+            QDir(dataDir).absoluteFilePath(QString::fromLatin1(FILENAME));
+        // Drain loop: keep writing while newer bytes keep arriving, then clear
+        // the in-flight flag under the lock so a subsequent save() relaunches.
+        for (;;) {
+            QByteArray out;
+            {
+                QMutexLocker dlk(&m_saveMutex);
+                if (m_pendingSaveBytes.isEmpty()) {
+                    m_saveInFlight = false;
+                    return;
+                }
+                out = m_pendingSaveBytes;
+                m_pendingSaveBytes.clear();
+            }
+            QSaveFile f(path);
+            if (f.open(QIODevice::WriteOnly)) {
+                f.write(out);
+                f.commit();
+            }
+        }
+    });
 }
 
 void BooksCatalogueLibraryStore::rebuildDerivedMapsLocked()
diff --git a/src/core/book/BooksCatalogueLibraryStore.h b/src/core/book/BooksCatalogueLibraryStore.h
index cf7e64c..742e4b1 100644
--- a/src/core/book/BooksCatalogueLibraryStore.h
+++ b/src/core/book/BooksCatalogueLibraryStore.h
@@ -6,6 +6,8 @@
 #include <QMutex>
 #include <QObject>
 #include <QString>
+#include <QByteArray>
+#include <QFuture>
 #include <optional>
 
 #include "CatalogueRecord.h"
@@ -27,7 +29,10 @@
 // Threadsafe — const APIs are mutex-guarded for cross-thread reads. Mutating
 // methods (upsertRecord / evict / validateAll / updateReadProgress) execute
 // synchronously on the calling thread, acquire m_mutex around map mutations,
-// then call save() and emit recordsChanged() OFF the lock.
+// then emit recordsChanged() OFF the lock. save() serializes the current state
+// synchronously under m_mutex but hands the actual disk write to a single
+// coalescing background task (m_saveMutex / m_saveFuture) so the GUI thread
+// never blocks on file I/O; the destructor drains any in-flight write.
 // (BooksScanner reference dropped 2026-05-27 — class removed in
 // BOOKS_STREMIO_PIVOT §3.8 backout; this store is the sole library data source now.)
 class BooksCatalogueLibraryStore : public QObject
@@ -39,6 +44,10 @@ public:
     // Production callsite passes CoreBridge::dataDir(); tests pass a QTemporaryDir.
     explicit BooksCatalogueLibraryStore(const QString& dataDir, QObject* parent = nullptr);
 
+    // Drains any in-flight async save so the background writer never touches a
+    // destroyed store (the writer captures `this`).
+    ~BooksCatalogueLibraryStore() override;
+
     // ── Mutate ────────────────────────────────────────────────────────────
     // Upsert (insert or replace by catalogueId). Updates all three derived maps
     // and persists. Emits recordsChanged() after save returns.
@@ -89,4 +98,14 @@ private:
     QHash<QString, CatalogueRecord> m_byId;
     QHash<QString, QSet<QString>>   m_bySeries;       // seriesId -> {catalogueId}
     QHash<QString, QString>         m_byFilePath;     // filePath -> catalogueId
+
+    // Async-save coalescing writer (see save() in the .cpp). save() serializes
+    // the current state under m_mutex, then a single background task writes the
+    // newest pending bytes to disk so the GUI thread never blocks on file I/O.
+    // m_saveMutex guards the writer's pending/in-flight state, independent of
+    // m_mutex (never held together). m_pendingSaveBytes empty == nothing queued.
+    QMutex        m_saveMutex;
+    QByteArray    m_pendingSaveBytes;
+    bool          m_saveInFlight = false;
+    QFuture<void> m_saveFuture;
 };
diff --git a/src/core/stream/StreamAggregator.cpp b/src/core/stream/StreamAggregator.cpp
index df43b47..af1b257 100644
--- a/src/core/stream/StreamAggregator.cpp
+++ b/src/core/stream/StreamAggregator.cpp
@@ -504,14 +504,19 @@ StreamAggregator::StreamAggregator(AddonRegistry* registry, QObject* parent)
 {
 }
 
-void StreamAggregator::load(const StreamLoadRequest& request)
+quint64 StreamAggregator::load(const StreamLoadRequest& request)
 {
     reset();
+    // DOWNLOAD BUG 2026-06-02 — stamp the new generation immediately after the
+    // reset() that zeroed mid-flight state. The returned token lets the caller
+    // gate its one-shot streamsReady handler against currentLoadToken() so a
+    // late emit from a SUPERSEDED load() (rapid re-clicks) is discarded.
+    ++m_loadGeneration;
     m_request = request;
 
     if (!m_registry || request.type.isEmpty() || request.id.isEmpty()) {
         emit streamsReady({}, {});
-        return;
+        return m_loadGeneration;
     }
 
     const QList<AddonDescriptor> addons =
@@ -519,7 +524,7 @@ void StreamAggregator::load(const StreamLoadRequest& request)
 
     if (addons.isEmpty()) {
         emit streamsReady({}, {});
-        return;
+        return m_loadGeneration;
     }
 
     for (const AddonDescriptor& addon : addons) {
@@ -532,6 +537,7 @@ void StreamAggregator::load(const StreamLoadRequest& request)
     }
 
     dispatchRequests();
+    return m_loadGeneration;
 }
 
 void StreamAggregator::dispatchRequests()
@@ -636,6 +642,14 @@ void StreamAggregator::onAddonFailed(const QString& addonId, const QString& mess
 
 void StreamAggregator::completeOne()
 {
+    // DOWNLOAD BUG 2026-06-02 (defense-in-depth) — a stale late completion from
+    // a superseded load() (reset() zeroed m_pendingResponses mid-flight) could
+    // otherwise drive the counter negative and re-emit streamsReady against the
+    // wrong request. Bail before decrementing if there is nothing outstanding.
+    if (m_pendingResponses <= 0) {
+        m_pendingResponses = 0;
+        return;
+    }
     --m_pendingResponses;
     if (m_pendingResponses > 0) {
         return;
diff --git a/src/core/stream/StreamAggregator.h b/src/core/stream/StreamAggregator.h
index 8f3d83f..56e6e48 100644
--- a/src/core/stream/StreamAggregator.h
+++ b/src/core/stream/StreamAggregator.h
@@ -36,7 +36,18 @@ public:
     explicit StreamAggregator(tankostream::addon::AddonRegistry* registry,
                               QObject* parent = nullptr);
 
-    void load(const StreamLoadRequest& request);
+    // DOWNLOAD BUG 2026-06-02 — load() now returns the monotonic generation
+    // token it stamped (m_loadGeneration, bumped inside load() right after
+    // reset()). Callers that arm a one-shot on the SHARED streamsReady signal
+    // capture this token and gate their handler on currentLoadToken()==token,
+    // so a late streamsReady from a SUPERSEDED load() (rapid re-clicks) cannot
+    // deliver the wrong show's streams to the currently-connected one-shot.
+    quint64 load(const StreamLoadRequest& request);
+
+    // Generation of the most recent load() — see load() above. Used by
+    // StreamPage's auto-download / play / prefetch one-shots to discard stale
+    // responses correlated to an earlier request.
+    quint64 currentLoadToken() const { return m_loadGeneration; }
 
     // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - season-pack indexer
     // fan-out, exposed for the new UnifiedPackSearchEngine (Task B2). Wraps
@@ -101,6 +112,14 @@ private:
     QList<tankostream::addon::Stream> m_streams;
     int m_pendingResponses = 0;
 
+    // DOWNLOAD BUG 2026-06-02 — monotonic request token. Bumped on every
+    // load() (right after reset()) and returned to the caller so a one-shot
+    // streamsReady handler can correlate the emit back to ITS request and
+    // ignore stale emits from a superseded load(). Defends against the rapid
+    // re-click race where a late streamsReady from an earlier load() fires the
+    // currently-armed one-shot carrying the wrong request's streams.
+    quint64 m_loadGeneration = 0;
+
     // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 - lazily-constructed NAM shared
     // across pack-search calls (mirrors TorrentPackPicker's m_nam pattern).
     QNetworkAccessManager* m_packNam = nullptr;
diff --git a/src/devtools/SystemIntrospection.cpp b/src/devtools/SystemIntrospection.cpp
index e749c81..385f956 100644
--- a/src/devtools/SystemIntrospection.cpp
+++ b/src/devtools/SystemIntrospection.cpp
@@ -6,6 +6,7 @@
 #include "ui/MainWindow.h"
 #include "ui/Theme.h"
 
+#include <QAbstractEventDispatcher>
 #include <QApplication>
 #include <QDateTime>
 #include <QDialog>
@@ -25,6 +26,8 @@
 #include <QShortcut>
 #include <QStandardPaths>
 #include <QTextStream>
+#include <QThread>
+#include <QTimer>
 #include <QVariant>
 #include <QWidget>
 
@@ -240,6 +243,8 @@ QStringList SystemIntrospection::commandList()
         // dev-* (2)
         QStringLiteral("dev_inject_error"),
         QStringLiteral("dev_toggle_feature"),
+        // diag-* (1) — v1.13 observability cluster
+        QStringLiteral("diag_timer_census"),
     };
 }
 
@@ -266,6 +271,7 @@ bool SystemIntrospection::dispatch(const QString& cmd,
     if (cmd.startsWith(QLatin1String("font_")))      return handleFont(cmd, payload, reply);
     if (cmd.startsWith(QLatin1String("perf_")))      return handlePerf(cmd, payload, reply);
     if (cmd.startsWith(QLatin1String("dev_")))       return handleDev(cmd, payload, reply);
+    if (cmd.startsWith(QLatin1String("diag_")))      return handleDiag(cmd, payload, reply);
     return false;
 }
 
@@ -1078,3 +1084,53 @@ bool SystemIntrospection::handleDev(const QString& cmd, const QJsonObject& p, QJ
     setError(r, "UNKNOWN_CMD", QStringLiteral("dev-* command '%1' not handled").arg(cmd));
     return true;
 }
+
+// ── diag-* ────────────────────────────────────────────────────────────────────
+
+// Read-only enumeration of every live GUI-thread timer — QTimer objects PLUS
+// raw startTimer()/QBasicTimer ids (only the event-dispatcher sees the latter).
+// One call surfaces e.g. a StreamDetailView 1000 ms repeating refresh timer
+// that should have been paused. Pure in-memory reads, no I/O.
+bool SystemIntrospection::handleDiag(const QString& cmd, const QJsonObject&, QJsonObject& r)
+{
+    if (cmd == QLatin1String("diag_timer_census")) {
+        auto typeName = [](Qt::TimerType t){ return t==Qt::PreciseTimer?QStringLiteral("PreciseTimer")
+            : t==Qt::CoarseTimer?QStringLiteral("CoarseTimer"):QStringLiteral("VeryCoarseTimer"); };
+        QJsonArray timers; int qc=0, rc=0; QSet<int> activeQTimerIds; QSet<const QTimer*> seen;
+        // (a) QTimer objects — build the roots list correctly (QWidgetList<<qApp does NOT compile):
+        QList<QObject*> roots;
+        for (QWidget* w : QApplication::topLevelWidgets()) roots.append(w);
+        roots.append(qApp);                                   // catches qApp-parented / unparented QTimers
+        for (QObject* root : roots)
+            for (QTimer* t : root->findChildren<QTimer*>()) {
+                if (seen.contains(t)) continue; seen.insert(t);
+                QObject* o = t->parent() ? t->parent() : t;
+                if (t->isActive()) activeQTimerIds.insert(t->timerId());   // guard: inactive timerId()==-1
+                timers.append(QJsonObject{{"source","qtimer"},{"timer_id",t->timerId()},
+                    {"interval_ms",t->interval()},{"single_shot",t->isSingleShot()},{"active",t->isActive()},
+                    {"timer_type",typeName(t->timerType())},{"owner_object_name",o->objectName()},
+                    {"owner_class_name",QString::fromLatin1(o->metaObject()->className())}}); ++qc;
+            }
+        // (b) raw startTimer()/QBasicTimer ids NOT backed by a QTimer:
+        QAbstractEventDispatcher* d = QAbstractEventDispatcher::instance(QThread::currentThread());
+        if (!d) { setError(r,"INTERNAL",QStringLiteral("no event dispatcher for GUI thread")); return true; }
+        QSet<QObject*> recv; recv.insert(qApp); for (QWidget* w : QApplication::allWidgets()) recv.insert(w);
+        for (QObject* o : recv)
+#if QT_VERSION < QT_VERSION_CHECK(7,0,0)
+            for (const auto& ti : d->registeredTimers(o)) { if (activeQTimerIds.contains(ti.timerId)) continue;
+                timers.append(QJsonObject{{"source","raw"},{"timer_id",ti.timerId},{"interval_ms",ti.interval},
+                    {"single_shot",false},{"active",true},{"timer_type",typeName(ti.timerType)},
+                    {"owner_object_name",o->objectName()},{"owner_class_name",QString::fromLatin1(o->metaObject()->className())}}); ++rc; }
+#else  // Qt7+: timersForObject(); interval is std::chrono::nanoseconds, id is Qt::TimerId
+            for (const auto& ti : d->timersForObject(o)) { const int id=int(qToUnderlying(ti.timerId));
+                if (activeQTimerIds.contains(id)) continue;
+                timers.append(QJsonObject{{"source","raw"},{"timer_id",id},
+                    {"interval_ms",int(std::chrono::duration_cast<std::chrono::milliseconds>(ti.interval).count())},
+                    {"single_shot",false},{"active",true},{"timer_type",typeName(ti.timerType)},
+                    {"owner_object_name",o->objectName()},{"owner_class_name",QString::fromLatin1(o->metaObject()->className())}}); ++rc; }
+#endif
+        mergeReply(r, QJsonObject{{"thread","main"},{"qtimer_count",qc},{"raw_timer_count",rc},{"timers",timers}});
+        return true;
+    }
+    setError(r,"UNKNOWN_CMD",QStringLiteral("diag-* command '%1' not handled").arg(cmd)); return true;
+}
diff --git a/src/devtools/SystemIntrospection.h b/src/devtools/SystemIntrospection.h
index 5286de9..d1573ff 100644
--- a/src/devtools/SystemIntrospection.h
+++ b/src/devtools/SystemIntrospection.h
@@ -71,6 +71,10 @@ private:
     bool handleFont(const QString& cmd, const QJsonObject& p, QJsonObject& r);
     bool handlePerf(const QString& cmd, const QJsonObject& p, QJsonObject& r);
     bool handleDev(const QString& cmd, const QJsonObject& p, QJsonObject& r);
+    // diag-* (v1.13 observability cluster) — read-only introspection of live
+    // runtime state. timer-census enumerates every GUI-thread timer (QTimer
+    // objects + raw startTimer/QBasicTimer ids). No state members.
+    bool handleDiag(const QString& cmd, const QJsonObject& p, QJsonObject& r);
 
     QPointer<MainWindow> m_window;
 
diff --git a/src/main.cpp b/src/main.cpp
index b3bfd82..cf9683d 100644
--- a/src/main.cpp
+++ b/src/main.cpp
@@ -10,11 +10,14 @@
 #include <QPixmapCache>
 #include <QStringList>
 #include <QTimer>
+#include <optional>
+
 #include "core/CoreBridge.h"
 #include "core/DebugLogBuffer.h"
 #include "core/JsonlEventLog.h"
 #include "core/manga/ComicsPrePivotMigrator.h"
 #include "ui/MainWindow.h"
+#include "devtools/HangWatchdog.h"
 #include "ui/Theme.h"
 
 #ifdef Q_OS_WIN
@@ -297,6 +300,12 @@ int main(int argc, char *argv[])
     MainWindow window(&bridge);
     dbg("5-mainwindow-created");
 
+    // OBS-2a (Track D observability) — heartbeat watchdog. Declared at function
+    // scope AFTER `window` so it destructs FIRST: stop()+join the off-GUI worker
+    // while `window` and the parented heartbeat QTimer are still alive. Started
+    // only under --dev-control (below), so production builds carry no watchdog.
+    std::optional<HangWatchdog> hangWatchdog;
+
     // Single-instance: claim the local socket so subsequent launches signal us.
     QLocalServer* instanceServer = createInstanceServer(&window);
     Q_UNUSED(instanceServer);  // window-parented, dies with window
@@ -309,6 +318,8 @@ int main(int argc, char *argv[])
     const bool devControlEnv  = qEnvironmentVariableIntValue("TANKOBAN_DEV_CONTROL") == 1;
     if (devControlFlag || devControlEnv) {
         window.enableDevControl();
+        hangWatchdog.emplace(&window);   // OBS-2a — off-GUI heartbeat -> out/HANG_DETECTED.json on a >750ms GUI stall
+        hangWatchdog->start();
         dbg("6a-devcontrol-enabled");
     }
 
diff --git a/src/ui/MainWindow.cpp b/src/ui/MainWindow.cpp
index 0dad84c..8f882e9 100644
--- a/src/ui/MainWindow.cpp
+++ b/src/ui/MainWindow.cpp
@@ -1941,7 +1941,7 @@ QJsonObject MainWindow::handleDevCommand(const QString& cmd, int seq, const QJso
         for (const QString& c : SystemIntrospection::commandList())
             cmds.append(c);
         return reply({
-            {"schema",     "tankoban.dev.v1.11"},
+            {"schema",     "tankoban.dev.v1.13"},
             {"appVersion", QApplication::applicationVersion()},
             {"commands",   cmds},
             {"features",   QJsonArray{}}
@@ -2672,7 +2672,8 @@ QJsonObject MainWindow::handleDevCommand(const QString& cmd, int seq, const QJso
             || cmd.startsWith(QLatin1String("theme_"))
             || cmd.startsWith(QLatin1String("font_"))
             || cmd.startsWith(QLatin1String("perf_"))
-            || cmd.startsWith(QLatin1String("dev_"));
+            || cmd.startsWith(QLatin1String("dev_"))
+            || cmd.startsWith(QLatin1String("diag_"));
         if (isSysCmd) {
             if (!m_systemIntrospection) {
                 return err("INTERNAL",
diff --git a/src/ui/pages/BooksPage.cpp b/src/ui/pages/BooksPage.cpp
index ffb7fe2..756f972 100644
--- a/src/ui/pages/BooksPage.cpp
+++ b/src/ui/pages/BooksPage.cpp
@@ -40,6 +40,7 @@
 #include <QJsonArray>
 #include <QJsonObject>
 #include <QFileInfo>
+#include <QtConcurrent/QtConcurrent>
 #include <QShortcut>
 #include <QPushButton>
 #include <QIcon>
@@ -1016,7 +1017,14 @@ void BooksPage::buildUI()
 
     auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
     connect(f5Shortcut, &QShortcut::activated, this, [this]() {
-        if (m_catalogueStore) m_catalogueStore->validateAll();
+        // validateAll() stats every record's file on disk — run it off the GUI
+        // thread so a manual F5 refresh never hitches. Eviction emits
+        // recordsChanged() via AutoConnection, which queues back to this thread.
+        // Mirrors StreamLibraryLayout::showEvent (StreamLibraryLayout.cpp:102).
+        if (m_catalogueStore) {
+            BooksCatalogueLibraryStore* store = m_catalogueStore;
+            (void) QtConcurrent::run([store]() { store->validateAll(); });
+        }
     });
 
     auto* refreshShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
@@ -1048,7 +1056,11 @@ void BooksPage::buildUI()
 
 void BooksPage::activate()
 {
-    if (m_catalogueStore) m_catalogueStore->validateAll();
+    // Off-thread disk validation (see showEvent / StreamLibraryLayout.cpp:102).
+    if (m_catalogueStore) {
+        BooksCatalogueLibraryStore* store = m_catalogueStore;
+        (void) QtConcurrent::run([store]() { store->validateAll(); });
+    }
 }
 
 // §3.8 burn-the-ships backout (2026-05-27) — orphan-record check on show
@@ -1058,7 +1070,15 @@ void BooksPage::activate()
 void BooksPage::showEvent(QShowEvent* event)
 {
     QWidget::showEvent(event);
-    if (m_catalogueStore) m_catalogueStore->validateAll();
+    // §6.2 orphan-record check: validateAll() walks every record and stats its
+    // file path on disk. Run it off the GUI thread (mirrors
+    // StreamLibraryLayout::showEvent, StreamLibraryLayout.cpp:102) so opening
+    // Books never hitches on a large library. Eviction emits recordsChanged()
+    // via the default AutoConnection, which Qt queues back to the GUI thread.
+    if (m_catalogueStore) {
+        BooksCatalogueLibraryStore* store = m_catalogueStore;
+        (void) QtConcurrent::run([store]() { store->validateAll(); });
+    }
     // Returning from the reader: reading progress was written to the JsonStore,
     // not the store, so refresh the continue strip to pick it up.
     refreshContinueStrip();
diff --git a/src/ui/pages/StreamPage.cpp b/src/ui/pages/StreamPage.cpp
index 5d6a30d..f5a856f 100644
--- a/src/ui/pages/StreamPage.cpp
+++ b/src/ui/pages/StreamPage.cpp
@@ -2323,10 +2323,19 @@ void StreamPage::onPlayRequested(const QString& imdbId, const QString& mediaType
     disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                this, nullptr);
 
+    // DOWNLOAD BUG 2026-06-02 — same correlation token as the auto-download
+    // path. The play one-shot is armed BEFORE load() runs, so the token is
+    // held in a shared_ptr captured by value and filled from the load() return
+    // below. The lambda discards any emit whose token != the current load
+    // generation (a late streamsReady from a superseded play/source-load).
+    auto playToken = std::make_shared<quint64>(0);
+
     connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
-        [this, savedChoiceKey, seriesBingeGroup, autoLaunchEligible](
+        [this, savedChoiceKey, seriesBingeGroup, autoLaunchEligible, playToken](
             const QList<tankostream::addon::Stream>& streams,
             const QHash<QString, QString>& addonsById) {
+            if (m_streamAggregator->currentLoadToken() != *playToken)
+                return;
             disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                        this, nullptr);
 
@@ -2412,7 +2421,9 @@ void StreamPage::onPlayRequested(const QString& imdbId, const QString& mediaType
             }
         });
 
-    m_streamAggregator->load(req);
+    // DOWNLOAD BUG 2026-06-02 — fill the correlation token captured by the
+    // one-shot above so it can discard a stale emit from a superseded load().
+    *playToken = m_streamAggregator->load(req);
 }
 
 // Phase 2 Batch 2.4 â€” auto-launch orchestration.
@@ -2502,10 +2513,18 @@ void StreamPage::startNextEpisodePrefetch(const QString& imdbId,
             disconnect(m_streamAggregator,
                        &tankostream::stream::StreamAggregator::streamsReady,
                        this, nullptr);
+            // DOWNLOAD BUG 2026-06-02 — same correlation token guard as the
+            // play/auto-download paths. The prefetch one-shot is armed before
+            // its load(), so the token rides in a shared_ptr filled from the
+            // load() return below; a stale emit from a superseded load() is
+            // discarded instead of feeding the wrong show into the prefetch.
+            auto prefetchToken = std::make_shared<quint64>(0);
             connect(m_streamAggregator,
                     &tankostream::stream::StreamAggregator::streamsReady, this,
-                [this](const QList<tankostream::addon::Stream>& streams,
+                [this, prefetchToken](const QList<tankostream::addon::Stream>& streams,
                        const QHash<QString, QString>& addonsById) {
+                    if (m_streamAggregator->currentLoadToken() != *prefetchToken)
+                        return;
                     disconnect(m_streamAggregator,
                                &tankostream::stream::StreamAggregator::streamsReady,
                                this, nullptr);
@@ -2516,7 +2535,7 @@ void StreamPage::startNextEpisodePrefetch(const QString& imdbId,
             req.type = QStringLiteral("series");
             req.id   = imdbId + QLatin1Char(':') + QString::number(qMax(1, next.first))
                               + QLatin1Char(':') + QString::number(qMax(1, next.second));
-            m_streamAggregator->load(req);
+            *prefetchToken = m_streamAggregator->load(req);
         });
 
     m_metaAggregator->fetchSeriesMeta(imdbId);
@@ -3246,6 +3265,20 @@ void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaTy
     if (!m_streamAggregator || !m_torrentClient || imdbId.isEmpty())
         return;
 
+    // DOWNLOAD BUG 2026-06-02 — in-flight dedup. Rapid identical Download
+    // clicks (the logs show 2-6 startAutoDownload within seconds) used to
+    // re-arm the shared streamsReady one-shot and call load() again, which
+    // reset() mid-flight and let a late stale emit deliver the wrong show's
+    // streams. If the SAME request is already in flight, ignore the re-click.
+    if (m_pendingAuto.active
+        && m_pendingAuto.imdbId == imdbId
+        && m_pendingAuto.season == season
+        && m_pendingAuto.episode == episode) {
+        qInfo().noquote() << "[auto-dl] dedup: ignoring re-click for in-flight imdb="
+                          << imdbId << "s" << season << "e" << episode;
+        return;
+    }
+
     m_pendingAuto = PendingAutoDownload{};
     m_pendingAuto.active         = true;
     m_pendingAuto.imdbId         = imdbId;
@@ -3265,6 +3298,12 @@ void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaTy
     connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
         [this](const QList<tankostream::addon::Stream>& streams,
                const QHash<QString, QString>& addonsById) {
+            // DOWNLOAD BUG 2026-06-02 — ignore a stale emit from a superseded
+            // load(). Without this, a late streamsReady from an EARLIER request
+            // fires this one-shot carrying the WRONG show's streams; the picker
+            // show-gate then rejects them all -> "No 1080p source found".
+            if (m_streamAggregator->currentLoadToken() != m_pendingAuto.token)
+                return;
             disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                        this, nullptr);
             finishAutoDownloadPick(streams, addonsById);
@@ -3283,6 +3322,8 @@ void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaTy
         req.id = imdbId + QLatin1Char(':') + QString::number(qMax(1, season))
                         + QLatin1Char(':') + QString::number(qMax(1, episode));
     }
+    qInfo().noquote() << "[auto-dl] req.id=" << req.id
+                      << "showTitle=" << m_pendingAuto.showTitle;
     // Surface a fetch error on the auto-download path (mirrors onPlayRequested's
     // streamError wiring ~line 2388). Clears the pending state so the tile/UI
     // doesn't hang waiting on a result that will never arrive.
@@ -3298,7 +3339,10 @@ void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaTy
                     QStringLiteral("Failed to fetch sources: ") + shown);
         });
 
-    m_streamAggregator->load(req);
+    // DOWNLOAD BUG 2026-06-02 — capture the generation token load() stamped so
+    // the one-shot above can discard a late emit from a superseded load().
+    // Set AFTER m_pendingAuto fields are populated.
+    m_pendingAuto.token = m_streamAggregator->load(req);
 }
 
 // One-shot streamsReady handler for an active auto-download. Converts the
@@ -3326,6 +3370,17 @@ void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>&
         cands.append(sc);
     }
 
+    // DOWNLOAD BUG 2026-06-02 — diagnostic: dump every candidate with whether
+    // it would pass the show-identity gate. If the pick still fails, this tells
+    // us conclusively whether real One Piece titles are being wrongly rejected
+    // (gate too strict) vs the choices being the wrong show entirely
+    // (correlation still broken — wrong streams reached this handler).
+    for (const auto& ch : choices)
+        qInfo().noquote() << "[auto-dl] cand q=" << ch.qualitySort << "seed=" << ch.seeders
+                          << "gate=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
+                                            ch.displayTitle, ctx.showTitle)
+                          << "title=" << ch.displayTitle.left(80);
+
     const std::optional<int> picked =
         tankostream::stream::AutoSourcePicker::pick(cands, ctx.showTitle, ctx.runtimeMinutes);
     qInfo().noquote() << "[auto-dl] picked="
diff --git a/src/ui/pages/StreamPage.h b/src/ui/pages/StreamPage.h
index 72043ce..3279014 100644
--- a/src/ui/pages/StreamPage.h
+++ b/src/ui/pages/StreamPage.h
@@ -330,6 +330,11 @@ private:
         int     runtimeMinutes = 0;  // 0 = unknown -> size guardrail skipped
         QString showTitle;    // captured at request time for AutoSourcePicker's
                               // show-identity gate (never download the wrong show)
+        // DOWNLOAD BUG 2026-06-02 — generation token returned by
+        // StreamAggregator::load(). The one-shot streamsReady handler gates on
+        // currentLoadToken()==token so a late emit from a SUPERSEDED load()
+        // (rapid Download re-clicks) can't deliver the wrong show's streams.
+        quint64 token = 0;
     };
     PendingAutoDownload m_pendingAuto;
 
diff --git a/src/ui/pages/TankoLibraryPage.cpp b/src/ui/pages/TankoLibraryPage.cpp
index a2e1302..9e2b74c 100644
--- a/src/ui/pages/TankoLibraryPage.cpp
+++ b/src/ui/pages/TankoLibraryPage.cpp
@@ -1392,6 +1392,27 @@ void TankoLibraryPage::populateGridThumbnails()
     if (!m_grid) return;
     const QList<BookResult> view = filteredResults();
     auto* libgen = qobject_cast<LibGenScraper*>(scraperFor(QStringLiteral("libgen")));
+
+    // Cover-cache disk index — built ONCE per refresh instead of globbing the
+    // cache dir for every tile. The old per-row existingCachedCoverPath() call
+    // ran a QDir::entryInfoList scan for each of N rows (O(N) directory scans
+    // on the GUI thread). Here we scan the dir a single time and map stem ->
+    // absolute path. Keyed by QFileInfo::baseName() (text before the first dot)
+    // which equals the cover key, since keys (md5 hex / URL slugs) never contain
+    // dots; first-by-name wins to match the prior entryInfoList(QDir::Name) order.
+    QHash<QString, QString> diskCovers;
+    {
+        const QFileInfoList cacheFiles = QDir(tankoLibraryCoverCacheDir())
+            .entryInfoList(QStringList() << QStringLiteral("*.*"),
+                           QDir::Files | QDir::Readable, QDir::Name);
+        diskCovers.reserve(cacheFiles.size());
+        for (const QFileInfo& fi : cacheFiles) {
+            const QString stem = fi.baseName();
+            if (!diskCovers.contains(stem))
+                diskCovers.insert(stem, fi.absoluteFilePath());
+        }
+    }
+
     for (int i = 0; i < view.size(); ++i) {
         const BookResult& r = view[i];
         // TANKOLIBRARY_ABB Track B2 — use coverKeyFor() which falls back to
@@ -1405,7 +1426,9 @@ void TankoLibraryPage::populateGridThumbnails()
             continue;
         }
         // Fast path 2: disk cache (prior session or detail-view fetch).
-        const QString cached = existingCachedCoverPath(key);
+        // Looked up from the one-shot diskCovers index above (was a per-row
+        // existingCachedCoverPath() glob).
+        const QString cached = diskCovers.value(key);
         if (!cached.isEmpty()) {
             QPixmap pix;
             if (pix.load(cached) && !pix.isNull()) {
diff --git a/src/ui/pages/stream/StreamDetailView.cpp b/src/ui/pages/stream/StreamDetailView.cpp
index a474ba6..3a5f7f9 100644
--- a/src/ui/pages/stream/StreamDetailView.cpp
+++ b/src/ui/pages/stream/StreamDetailView.cpp
@@ -42,6 +42,7 @@
 #include <QJsonArray>
 #include <QJsonObject>
 #include <QPushButton>
+#include <QScrollBar>
 #include <QSize>
 #include <QStandardPaths>
 #include <QStackedLayout>
@@ -119,6 +120,8 @@ StreamDetailView::StreamDetailView(CoreBridge* bridge,
         refreshMovieDownloadState();
         refreshAllEpisodeRows();
         refreshSeasonHeaderButton();
+        // PERF (2026-06-02): self-stop once no visible row is in flight.
+        maybeStopProgressTimer();
     });
 
     if (m_meta) {
@@ -710,6 +713,11 @@ void StreamDetailView::buildUI()
     m_episodeTable->setContextMenuPolicy(Qt::CustomContextMenu);
     connect(m_episodeTable, &QWidget::customContextMenuRequested,
             this, &StreamDetailView::onEpisodeContextMenu);
+    // PERF (2026-06-02): repaint on scroll so rows revealed by scrolling a long
+    // list (One Piece 1000+ eps) derive their correct state — refreshAllEpisodeRows
+    // is now viewport-scoped, so newly-visible rows would otherwise stay stale.
+    connect(m_episodeTable->verticalScrollBar(), &QScrollBar::valueChanged,
+            this, [this](int){ refreshAllEpisodeRows(); });
     m_episodeTable->hide();
     leftCol->addWidget(m_episodeTable, 1);
 
@@ -1306,7 +1314,7 @@ void StreamDetailView::onEpisodeActivated(int row, int /*col*/)
 // cohort RowState cluster (deleted in P1.T5).
 
 tankostream::stream::EpisodeDisplayState
-StreamDetailView::episodeDisplayState(int season, int episode) const
+StreamDetailView::episodeDisplayState(int season, int episode, const QHash<int, QPair<QString,int>>& snap) const
 {
     using tankostream::stream::EpisodeStateInputs;
     EpisodeStateInputs in;
@@ -1363,8 +1371,9 @@ StreamDetailView::episodeDisplayState(int season, int episode) const
     }
     // ALWAYS consult the live engine snapshot (it carries the Paused flag + the
     // live %, and an active transfer must beat a pre-allocated on-disk file).
-    if (m_torrentClient && !m_currentImdb.isEmpty()) {
-        const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
+    // PERF: the cohort snapshot is fetched ONCE per pass by the caller and
+    // threaded in — no per-row streamBulkSnapshotForImdbSeason scan here.
+    if (!snap.isEmpty()) {
         const auto it = snap.constFind(episode);
         if (it != snap.constEnd()) {
             const QString st = it.value().first;            // cohort state string
@@ -1396,19 +1405,18 @@ StreamDetailView::episodeDisplayState(int season, int episode) const
     return tankostream::stream::deriveEpisodeDisplayState(in);
 }
 
-void StreamDetailView::refreshEpisodeRow(int row, int season, int episode)
+void StreamDetailView::refreshEpisodeRow(int row, int season, int episode, const QHash<int, QPair<QString,int>>& snap)
 {
     if (!m_episodeTable || row < 0 || row >= m_episodeTable->rowCount())
         return;
     using S = tankostream::stream::EpisodeDisplayState;
-    const S state = episodeDisplayState(season, episode);
+    const S state = episodeDisplayState(season, episode, snap);
 
-    // Progress % for in-progress states (cheap re-read of the in-process snapshot;
-    // clamped so a terminal -1 never leaks into the label).
+    // Progress % for in-progress states — O(1) lookup into the cohort snapshot
+    // threaded in by the caller (clamped so a terminal -1 never leaks into the
+    // label). PERF: no per-row streamBulkSnapshotForImdbSeason scan here.
     int pct = 0;
-    if ((state == S::Downloading || state == S::Paused)
-        && m_torrentClient && !m_currentImdb.isEmpty()) {
-        const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
+    if (state == S::Downloading || state == S::Paused) {
         const auto it = snap.constFind(episode);
         if (it != snap.constEnd())
             pct = qMax(0, it.value().second);
@@ -1473,14 +1481,76 @@ void StreamDetailView::refreshAllEpisodeRows()
     if (!m_episodeTable) return;
     const int season = currentSeason();
     if (season <= 0) return;
-    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
+    // PERF (2026-06-02): fetch the cohort snapshot ONCE per pass and thread it
+    // through episodeDisplayState/refreshEpisodeRow; repaint only on-screen rows
+    // (One Piece = 1000+ rows; a full-table O(rows) snapshot/SQL scan per 1Hz
+    // tick on the GUI thread froze the app).
+    const auto snap = (m_torrentClient && !m_currentImdb.isEmpty())
+        ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
+        : QHash<int, QPair<QString,int>>();
+    int first = 0, last = m_episodeTable->rowCount() - 1;
+    visibleRowRange(&first, &last);
+    if (first < 0 || last < first) return;
+    for (int row = first; row <= last; ++row) {
         auto* numItem = m_episodeTable->item(row, kColEpisode);
         if (!numItem) continue;
         const int episode = numItem->data(Qt::UserRole).toInt();
-        if (episode > 0) refreshEpisodeRow(row, season, episode);
+        if (episode > 0) refreshEpisodeRow(row, season, episode, snap);
     }
 }
 
+// PERF (2026-06-02): inclusive [first,last] span of rows currently in the
+// viewport, with one row of overscan each side so a partially-revealed row at
+// the edge paints correctly. Falls back to [0, rowCount-1] when rowAt() can't
+// resolve (empty/short table).
+void StreamDetailView::visibleRowRange(int* first, int* last) const
+{
+    *first = 0; *last = -1;
+    if (!m_episodeTable) return;
+    const int rc = m_episodeTable->rowCount();
+    if (rc == 0) return;
+    const int top = m_episodeTable->rowAt(0);
+    const int bot = m_episodeTable->rowAt(m_episodeTable->viewport()->height() - 1);
+    int f = (top < 0) ? 0 : top;
+    int l = (bot < 0) ? rc - 1 : bot;
+    f = qMax(0, f - 1);
+    l = qMin(rc - 1, l + 1);
+    *first = f; *last = l;
+}
+
+// PERF (2026-06-02): does any VISIBLE row need a live progress tick? Fetches the
+// cohort snapshot once and checks only the viewport span. Drives the timer gate.
+bool StreamDetailView::anyVisibleRowInFlight() const
+{
+    if (!m_episodeTable) return false;
+    const int season = currentSeason();
+    if (season <= 0) return false;
+    const auto snap = (m_torrentClient && !m_currentImdb.isEmpty())
+        ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
+        : QHash<int, QPair<QString,int>>();
+    int first = 0, last = -1; visibleRowRange(&first, &last);
+    if (last < first) return false;
+    using S = tankostream::stream::EpisodeDisplayState;
+    for (int row = first; row <= last; ++row) {
+        auto* numItem = m_episodeTable->item(row, kColEpisode);
+        if (!numItem) continue;
+        const int ep = numItem->data(Qt::UserRole).toInt();
+        if (ep <= 0) continue;
+        const S st = episodeDisplayState(season, ep, snap);
+        if (st == S::Downloading || st == S::Paused) return true;
+    }
+    return false;
+}
+
+// PERF (2026-06-02): self-stop the 1Hz poll when nothing visible is in flight.
+// Re-armed by the state-change handlers (entriesChanged/entryStateChanged/
+// streamBulkGroupsChanged/torrentCompleted) and showEvent.
+void StreamDetailView::maybeStopProgressTimer()
+{
+    if (m_progressRefreshTimer && m_progressRefreshTimer->isActive() && !anyVisibleRowInFlight())
+        m_progressRefreshTimer->stop();
+}
+
 void StreamDetailView::filterEpisodesByNumber(const QString& text)
 {
     if (!m_episodeTable) return;
@@ -1524,9 +1594,13 @@ void StreamDetailView::setStreamDownloadIndex(StreamDownloadIndex* idx)
         // QtConcurrent on home open, spec §10.4).
         connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged, this,
                 [this]() {
+                    if (m_currentImdb.isEmpty() || !isVisible()) return;
                     refreshAllEpisodeRows();
                     refreshMovieLocalChip();
                     refreshMovieDownloadState();
+                    // PERF (2026-06-02): re-arm the gated 1Hz poll on real state change.
+                    if (anyVisibleRowInFlight() && m_progressRefreshTimer && !m_progressRefreshTimer->isActive())
+                        m_progressRefreshTimer->start();
                 },
                 Qt::QueuedConnection);
         connect(m_downloadIndex, &StreamDownloadIndex::entryStateChanged, this,
@@ -1534,6 +1608,9 @@ void StreamDetailView::setStreamDownloadIndex(StreamDownloadIndex* idx)
                     if (imdbId != m_currentImdb) return;
                     if (season == 0 && episode == 0) { refreshMovieDownloadState(); return; }
                     if (season == currentSeason()) refreshAllEpisodeRows();
+                    // PERF (2026-06-02): re-arm the gated 1Hz poll on real state change.
+                    if (anyVisibleRowInFlight() && isVisible() && m_progressRefreshTimer && !m_progressRefreshTimer->isActive())
+                        m_progressRefreshTimer->start();
                 },
                 Qt::QueuedConnection);
         // Repaint immediately if the table already has rows from a prior
@@ -1565,7 +1642,11 @@ void StreamDetailView::onEpisodeContextMenu(const QPoint& pos)
     if (episode <= 0 || season <= 0 || m_currentImdb.isEmpty()) return;
 
     using S = tankostream::stream::EpisodeDisplayState;
-    const S state = episodeDisplayState(season, episode);
+    // PERF (2026-06-02): cohort snapshot fetched once and threaded through.
+    const auto snap = (m_torrentClient && !m_currentImdb.isEmpty())
+        ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
+        : QHash<int, QPair<QString,int>>();
+    const S state = episodeDisplayState(season, episode, snap);
 
     QMenu menu(this);
     QAction* cancelAct = nullptr;
@@ -1613,7 +1694,12 @@ void StreamDetailView::onEpisodeContextMenu(const QPoint& pos)
             QDesktopServices::openUrl(
                 QUrl::fromLocalFile(QFileInfo(best->canonicalPath).absolutePath()));
     }
-    refreshEpisodeRow(row, season, episode);
+    // Re-fetch a fresh snapshot — the actions above (cancel/delete/evict) may
+    // have mutated engine/index state, so the pre-menu snap is stale here.
+    const auto postSnap = (m_torrentClient && !m_currentImdb.isEmpty())
+        ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
+        : QHash<int, QPair<QString,int>>();
+    refreshEpisodeRow(row, season, episode, postSnap);
 }
 
 void StreamDetailView::refreshMovieLocalChip()
@@ -1761,6 +1847,9 @@ void StreamDetailView::setTorrentClient(TorrentClient* client)
                 this, [this](const QString& /*groupId*/) {
                     refreshAllEpisodeRows();
                     refreshMovieDownloadState();
+                    // PERF (2026-06-02): re-arm the gated 1Hz poll on real state change.
+                    if (anyVisibleRowInFlight() && isVisible() && m_progressRefreshTimer && !m_progressRefreshTimer->isActive())
+                        m_progressRefreshTimer->start();
                 }, Qt::QueuedConnection);
         connect(m_torrentClient, &TorrentClient::torrentAdded,
                 this, [this](const QString& /*infoHash*/) {
@@ -1781,6 +1870,9 @@ void StreamDetailView::setTorrentClient(TorrentClient* client)
                     // badge from 'Downloading 99%' to 'Downloaded'.
                     refreshMovieDownloadState();
                     refreshAllEpisodeRows();
+                    // PERF (2026-06-02): re-arm the gated 1Hz poll on real state change.
+                    if (anyVisibleRowInFlight() && isVisible() && m_progressRefreshTimer && !m_progressRefreshTimer->isActive())
+                        m_progressRefreshTimer->start();
                 }, Qt::QueuedConnection);
     }
     refreshMovieDownloadState();
@@ -1851,7 +1943,11 @@ void StreamDetailView::onActionIconClicked(int episode, const QPoint& /*globalAn
     if (season <= 0 || m_currentImdb.isEmpty() || episode <= 0) return;
 
     using S = tankostream::stream::EpisodeDisplayState;
-    switch (episodeDisplayState(season, episode)) {
+    // PERF (2026-06-02): cohort snapshot fetched once and threaded through.
+    const auto snap = (m_torrentClient && !m_currentImdb.isEmpty())
+        ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
+        : QHash<int, QPair<QString,int>>();
+    switch (episodeDisplayState(season, episode, snap)) {
     case S::Downloaded:
         // Play from disk — same path as a row click (re-checks disk + evicts
         // a vanished file before falling back to streams).
@@ -1884,7 +1980,12 @@ void StreamDetailView::onActionIconClicked(int episode, const QPoint& /*globalAn
         emit singleEpisodeDownloadRequested(season, episode);
         break;
     }
-    refreshEpisodeRow(rowForEpisode(episode), season, episode);
+    // Re-fetch a fresh snapshot — pause/resume/setStreamBulkItemPaused above
+    // mutate the cohort state, so the pre-action snap is stale for this repaint.
+    const auto postSnap = (m_torrentClient && !m_currentImdb.isEmpty())
+        ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
+        : QHash<int, QPair<QString,int>>();
+    refreshEpisodeRow(rowForEpisode(episode), season, episode, postSnap);
 }
 
 // ─── STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — season-header morphing slots ─
@@ -2001,7 +2102,12 @@ void StreamDetailView::refreshSeasonHeaderButton()
 void StreamDetailView::showEvent(QShowEvent* event)
 {
     QWidget::showEvent(event);
-    if (m_progressRefreshTimer && !m_progressRefreshTimer->isActive())
+    // PERF (2026-06-02): one immediate on-screen refresh, then arm the 1Hz poll
+    // ONLY if a visible row is actually downloading/paused. The gate self-stops
+    // it again once nothing visible is in flight (maybeStopProgressTimer).
+    refreshAllEpisodeRows();
+    refreshMovieDownloadState();
+    if (anyVisibleRowInFlight() && m_progressRefreshTimer && !m_progressRefreshTimer->isActive())
         m_progressRefreshTimer->start();
 }
 
diff --git a/src/ui/pages/stream/StreamDetailView.h b/src/ui/pages/stream/StreamDetailView.h
index dc9b6cc..d2a24a1 100644
--- a/src/ui/pages/stream/StreamDetailView.h
+++ b/src/ui/pages/stream/StreamDetailView.h
@@ -287,11 +287,19 @@ private:
     // the legacy cohort RowState cluster (resolveRowState / refreshEpisodeMarkers
     // / refreshSubstrateStatesForActiveSeason / repaintActionIconForRow / the
     // m_bulkPollTimer poll), all removed in P1.T5.
-    tankostream::stream::EpisodeDisplayState episodeDisplayState(int season, int episode) const;
+    tankostream::stream::EpisodeDisplayState episodeDisplayState(int season, int episode, const QHash<int, QPair<QString,int>>& snap) const;
     // Repaints ONE row's status cell + action control from episodeDisplayState.
-    void refreshEpisodeRow(int row, int season, int episode);
+    void refreshEpisodeRow(int row, int season, int episode, const QHash<int, QPair<QString,int>>& snap);
     // Repaints every visible row of the active season via refreshEpisodeRow.
     void refreshAllEpisodeRows();
+    // PERF (2026-06-02): on-screen-only refresh + self-stopping 1Hz timer.
+    // visibleRowRange returns the inclusive [first,last] viewport row span
+    // (+/-1 overscan); anyVisibleRowInFlight fetches the cohort snapshot ONCE
+    // and reports whether any visible row is Downloading/Paused; the timer
+    // self-stops when nothing visible is in flight (re-armed on state changes).
+    void visibleRowRange(int* first, int* last) const;
+    bool anyVisibleRowInFlight() const;
+    void maybeStopProgressTimer();
     // Episode-number jump/filter — hides rows whose episode number doesn't start
     // with `text` (empty = show all) + scrolls to the first match. For long anime
     // (One Piece, 1000+ eps) where scrolling the list is impractical.
diff --git a/src/ui/pages/stream/StreamLibraryLayout.cpp b/src/ui/pages/stream/StreamLibraryLayout.cpp
index b14767b..b2eef8f 100644
--- a/src/ui/pages/stream/StreamLibraryLayout.cpp
+++ b/src/ui/pages/stream/StreamLibraryLayout.cpp
@@ -15,6 +15,7 @@
 #include <QNetworkAccessManager>
 #include <QNetworkReply>
 #include <QNetworkRequest>
+#include <QPointer>
 #include <QSettings>
 #include <QShowEvent>
 #include <QStandardPaths>
@@ -438,13 +439,31 @@ void StreamLibraryLayout::downloadPoster(const QString& imdbId, const QString& p
 
 void StreamLibraryLayout::cleanupOrphanPosters()
 {
-    QDir dir(m_posterCacheDir);
-    if (!dir.exists()) return;
-
-    QStringList files = dir.entryList({"*.jpg"}, QDir::Files);
-    for (const QString& file : files) {
-        QString imdbId = QFileInfo(file).baseName(); // "tt1234567"
-        if (!m_library->has(imdbId))
-            QFile::remove(dir.filePath(file));
-    }
+    // A005 (night-watch app-heaviness, 2026-06-03) — the orphan sweep is a
+    // QDir::entryList + per-file QFile::remove loop: bounded O(N-orphan) but
+    // fully synchronous I/O that previously ran on the GUI thread during every
+    // library refresh(). Move it off-thread so a refresh never hitches.
+    //
+    // Capture the cache dir + library pointer by value: StreamLibrary::has()
+    // is mutex-protected (safe off-thread) and the library outlives this
+    // widget. A QPointer guard leashes the job to this widget's lifetime so a
+    // teardown mid-sweep bails fast — we never dereference the widget across
+    // the thread boundary, so the by-value captures are what keep it safe.
+    // Mirrors the showEvent() validateAll fire-and-forget pattern above.
+    const QString cacheDir = m_posterCacheDir;
+    StreamLibrary* library = m_library;
+    QPointer<StreamLibraryLayout> guard(this);
+    (void) QtConcurrent::run([cacheDir, library, guard]() {
+        if (guard.isNull() || !library) return;
+        QDir dir(cacheDir);
+        if (!dir.exists()) return;
+
+        const QStringList files = dir.entryList({"*.jpg"}, QDir::Files);
+        for (const QString& file : files) {
+            if (guard.isNull()) return; // widget gone mid-sweep — stop early
+            const QString imdbId = QFileInfo(file).baseName(); // "tt1234567"
+            if (!library->has(imdbId))
+                QFile::remove(dir.filePath(file));
+        }
+    });
 }
diff --git a/tools/tankoctl.cpp b/tools/tankoctl.cpp
index 5b48e17..636fee2 100644
--- a/tools/tankoctl.cpp
+++ b/tools/tankoctl.cpp
@@ -1639,7 +1639,9 @@ int main(int argc, char** argv)
                || sub == QLatin1String("lease-list")
                // v1.12 network observability (Congress 9).
                || sub == QLatin1String("net-list-requests")
-               || sub == QLatin1String("net-list-rules")) {
+               || sub == QLatin1String("net-list-rules")
+               // v1.13 observability cluster — diag-* no-payload reads.
+               || sub == QLatin1String("diag-timer-census")) {
         // No payload args.
     } else {
         err << "unknown subcommand: " << sub << "\n\n";

codex
I’m treating this as a code-review pass against the supplied diff only, with no file edits. I’m focusing on the DoD language first, then threading/lifetime regressions in the changes.
codex
**DoD Matrix**
A1 NOT-MET - `StreamAggregator` does not tag addon callbacks/results with a generation, so stale callbacks can still mutate current `m_streams` / `m_pendingResponses`.

A2 NOT-MET - handlers compare captured token to `currentLoadToken()`, but a stale emit after a newer `load()` sees the newer current token and can pass.

A3 PARTIAL - plain `quint64` is probably GUI-thread-only in shown call sites, but no affinity/assertion protects against cross-thread use.

B1 MET - BooksPage F5/activate/showEvent now call `QtConcurrent::run([store]{ store->validateAll(); })`.

B2 MET - `TankoLibraryPage::populateGridThumbnails()` builds `diskCovers` once instead of per-row globbing.

B3 MET - `BooksCatalogueLibraryStore::save()` serializes bytes then dispatches disk write via `QtConcurrent::run`.

B4 PARTIAL - coalescing + `QSaveFile` + destructor wait cover the intended writer path, but write/commit failures drop pending bytes with no retry/reporting.

B5 PARTIAL - UI updates appear queued through signals, but fire-and-forget `validateAll()` captures a raw store pointer with no lifetime guard.

C1 MET - `cleanupOrphanPosters()` now runs the directory scan/remove loop in `QtConcurrent::run`.

C2 PARTIAL - it uses `QPointer<StreamLibraryLayout>`, but checks it from the worker thread and also captures raw `StreamLibrary*`.

C3 NOT-MET - repeated `refresh()` can launch overlapping cleanup jobs; no in-flight guard/coalescing exists.

D1 PARTIAL - `diag-timer-census` is wired, but census only searches top-level widgets/qApp/allWidgets and misses timers owned by non-widget QObject trees.

D2 MET - implementation is read-only enumeration and reply construction.

D3 PARTIAL - safe only if dispatch is guaranteed on GUI thread; diff does not marshal/assert before walking `QApplication`/QObject trees.

E1 PARTIAL - `main.cpp` starts `HangWatchdog`, but `HangWatchdog.{h,cpp}` bodies are absent from supplied diff.

E2 NOT-MET - no visible evidence of `out/HANG_DETECTED.json`, timestamp/stall duration, or auto-clear behavior.

E3 PARTIAL - start is gated behind dev-control in `main.cpp`, but watchdog internals are not reviewable.

E4 NOT-MET - no visible evidence the watchdog thread is stopped/joined.

E5 NOT-MET - no visible evidence of heartbeat atomic or memory ordering.

**Scope Creep**
`StreamDetailView` viewport-only row refresh, scroll repaint, and self-stopping progress timer are broad performance behavior changes not requested by A001. `StreamPage` adds auto-download dedup and verbose `[auto-dl]` candidate logging beyond the stated ghost-row fix.

**Correctness / Security**
The main correctness blocker is A001: current-token checking does not correlate the emitted result to the load generation that produced it. Also, all three `StreamPage` one-shots arm the connection before assigning the returned token; synchronous `streamsReady` paths in `StreamAggregator::load()` will be discarded with token `0` and can leave handlers/pending state stuck. No leaked secrets seen.

**DoD Gaps**
The DoD should require generation tagging at the aggregator callback/result level, lifetime handling for every fire-and-forget `QtConcurrent` task, explicit cleanup-task coalescing semantics, GUI-thread marshalling/assertions for diagnostics, and a save failure/retry policy.

REQUEST-CHANGES: A001 is not actually generation-safe, C3 is not met, and OBS-2a cannot be verified from the supplied diff.
tokens used
38,776
**DoD Matrix**
A1 NOT-MET - `StreamAggregator` does not tag addon callbacks/results with a generation, so stale callbacks can still mutate current `m_streams` / `m_pendingResponses`.

A2 NOT-MET - handlers compare captured token to `currentLoadToken()`, but a stale emit after a newer `load()` sees the newer current token and can pass.

A3 PARTIAL - plain `quint64` is probably GUI-thread-only in shown call sites, but no affinity/assertion protects against cross-thread use.

B1 MET - BooksPage F5/activate/showEvent now call `QtConcurrent::run([store]{ store->validateAll(); })`.

B2 MET - `TankoLibraryPage::populateGridThumbnails()` builds `diskCovers` once instead of per-row globbing.

B3 MET - `BooksCatalogueLibraryStore::save()` serializes bytes then dispatches disk write via `QtConcurrent::run`.

B4 PARTIAL - coalescing + `QSaveFile` + destructor wait cover the intended writer path, but write/commit failures drop pending bytes with no retry/reporting.

B5 PARTIAL - UI updates appear queued through signals, but fire-and-forget `validateAll()` captures a raw store pointer with no lifetime guard.

C1 MET - `cleanupOrphanPosters()` now runs the directory scan/remove loop in `QtConcurrent::run`.

C2 PARTIAL - it uses `QPointer<StreamLibraryLayout>`, but checks it from the worker thread and also captures raw `StreamLibrary*`.

C3 NOT-MET - repeated `refresh()` can launch overlapping cleanup jobs; no in-flight guard/coalescing exists.

D1 PARTIAL - `diag-timer-census` is wired, but census only searches top-level widgets/qApp/allWidgets and misses timers owned by non-widget QObject trees.

D2 MET - implementation is read-only enumeration and reply construction.

D3 PARTIAL - safe only if dispatch is guaranteed on GUI thread; diff does not marshal/assert before walking `QApplication`/QObject trees.

E1 PARTIAL - `main.cpp` starts `HangWatchdog`, but `HangWatchdog.{h,cpp}` bodies are absent from supplied diff.

E2 NOT-MET - no visible evidence of `out/HANG_DETECTED.json`, timestamp/stall duration, or auto-clear behavior.

E3 PARTIAL - start is gated behind dev-control in `main.cpp`, but watchdog internals are not reviewable.

E4 NOT-MET - no visible evidence the watchdog thread is stopped/joined.

E5 NOT-MET - no visible evidence of heartbeat atomic or memory ordering.

**Scope Creep**
`StreamDetailView` viewport-only row refresh, scroll repaint, and self-stopping progress timer are broad performance behavior changes not requested by A001. `StreamPage` adds auto-download dedup and verbose `[auto-dl]` candidate logging beyond the stated ghost-row fix.

**Correctness / Security**
The main correctness blocker is A001: current-token checking does not correlate the emitted result to the load generation that produced it. Also, all three `StreamPage` one-shots arm the connection before assigning the returned token; synchronous `streamsReady` paths in `StreamAggregator::load()` will be discarded with token `0` and can leave handlers/pending state stuck. No leaked secrets seen.

**DoD Gaps**
The DoD should require generation tagging at the aggregator callback/result level, lifetime handling for every fire-and-forget `QtConcurrent` task, explicit cleanup-task coalescing semantics, GUI-thread marshalling/assertions for diagnostics, and a save failure/retry policy.

REQUEST-CHANGES: A001 is not actually generation-safe, C3 is not met, and OBS-2a cannot be verified from the supplied diff.
