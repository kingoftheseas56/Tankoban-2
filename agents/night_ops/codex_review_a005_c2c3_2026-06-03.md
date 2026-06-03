Cross-model review for Tankoban 2 (requested by Agent 4, author = Agent 4/Opus). You are a DIFFERENT model than the author. Read-only review — do NOT edit any file; just report.

CONTEXT: This is the A005 hardening follow-up. The core A005 fix (moving StreamLibraryLayout::cleanupOrphanPosters() off the GUI thread via QtConcurrent::run) already landed on master (commit d911af2). Your earlier review of that landing flagged two low-severity items that were deliberately deferred:

- C2 (PARTIAL): the off-thread cleanup task read a QPointer<StreamLibraryLayout> guard FROM the worker thread. QPointer is not safe to read concurrently with GUI-thread deletion of the pointee. In practice it was only an early-bail hint and the object actually dereferenced (StreamLibrary*, captured raw) is documented to outlive the widget — but the cross-thread QPointer read is still UB-adjacent and should go.
- C3 (NOT-MET): repeated refresh() can launch multiple concurrent QtConcurrent poster sweeps; two worker threads can race to QFile::remove the same orphan file (one wins, the other gets a benign failure). No crash / no data loss, but unguarded. Wanted: an in-flight guard to coalesce.

THE FIX UNDER REVIEW (this change, StreamLibraryLayout.{cpp,h}):
A single std::shared_ptr<std::atomic_bool> m_orphanSweepRunning (default-initialized to false in the header) addresses BOTH:
  - C3: cleanupOrphanPosters() does `if (m_orphanSweepRunning->exchange(true)) return;` at entry — atomically claims the in-flight slot; if a sweep is already running, skip launching a second. The worker clears the flag via an RAII ClearOnExit on every return path (early bails + normal completion).
  - C2: the worker lambda no longer captures or reads a QPointer. It captures ONLY by value: cacheDir (QString), library (StreamLibrary*, raw — mutex-protected has() and documented to outlive the widget; UNCHANGED from the landed A005), and `running` (a shared_ptr copy of the atomic flag, so its lifetime is independent of the widget). Nothing in the worker dereferences the widget. The QPointer include was removed.

VERIFY — do all of:
1. C2 closed? Confirm the worker thread no longer reads any QPointer / never touches the widget (`this`). Confirm the only raw pointer it dereferences is `library`, whose outlives-the-widget contract is unchanged from the already-landed A005 (i.e. this change does not newly introduce that assumption — it inherits it).
2. C3 closed? Walk: refresh() called 3× in rapid succession while the first sweep is mid-loop. Does exactly one worker run, with the other two short-circuiting at the exchange()? Confirm no two threads can both pass the guard for the same generation.
3. Lifetime/teardown: the widget is destroyed while a sweep is still running. Is m_orphanSweepRunning->store(false) in ClearOnExit safe (writing through the shared_ptr copy, not a freed member)? Is the default-member-initializer + shared_ptr the right lifetime model, or is there a hole (e.g. flag never created, double-launch at construction)?
4. Flag-leak / stuck-true: any path where exchange(true) succeeds but ClearOnExit never runs (exception thrown before the guard is constructed; QtConcurrent::run failing to enqueue; the lambda never invoked)? If the flag could get stuck true, cleanup would silently stop forever — is that possible here?
5. Memory ordering: is the default seq_cst exchange/store correct for a coalescing flag (no missed-wakeup or torn-state concern), or is there a subtle ordering requirement?
6. Coalescing correctness: is it acceptable that an orphan created between the running sweep's directory snapshot and a skipped refresh() lingers until the next post-sweep refresh()? (The author asserts yes — best-effort cosmetic cleanup, caught next cycle.) Flag if you disagree.
7. Anything the change does that wasn't asked for (scope creep), or any NEW bug the restructure introduces (the RAII struct, the include swap, the member initializer).

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason. Default REQUEST-CHANGES if C2 or C3 is not closed or you are unsure.

================ DIFF UNDER REVIEW (working tree: StreamLibraryLayout.cpp + .h) ================
diff --git a/src/ui/pages/stream/StreamLibraryLayout.cpp b/src/ui/pages/stream/StreamLibraryLayout.cpp
index b2eef8f..9e1a8c4 100644
--- a/src/ui/pages/stream/StreamLibraryLayout.cpp
+++ b/src/ui/pages/stream/StreamLibraryLayout.cpp
@@ -15,7 +15,6 @@
 #include <QNetworkAccessManager>
 #include <QNetworkReply>
 #include <QNetworkRequest>
-#include <QPointer>
 #include <QSettings>
 #include <QShowEvent>
 #include <QStandardPaths>
@@ -444,23 +443,41 @@ void StreamLibraryLayout::cleanupOrphanPosters()
     // fully synchronous I/O that previously ran on the GUI thread during every
     // library refresh(). Move it off-thread so a refresh never hitches.
     //
-    // Capture the cache dir + library pointer by value: StreamLibrary::has()
-    // is mutex-protected (safe off-thread) and the library outlives this
-    // widget. A QPointer guard leashes the job to this widget's lifetime so a
-    // teardown mid-sweep bails fast — we never dereference the widget across
-    // the thread boundary, so the by-value captures are what keep it safe.
+    // A005 hardening C3 (2026-06-03) — coalesce overlapping sweeps. exchange(true)
+    // atomically claims the in-flight slot; if a sweep is already running we skip
+    // (return) instead of launching a second one. That removes the rapid-refresh
+    // race where two worker threads raced to QFile::remove the same orphan (one
+    // wins, the other gets a benign failure). A poster orphaned between the
+    // running sweep's start and this skipped call simply lingers until the next
+    // refresh() after the current sweep finishes — best-effort cosmetic cleanup,
+    // no correctness cost. The worker clears the flag on every exit path.
+    if (m_orphanSweepRunning->exchange(true))
+        return;
+
+    // A005 hardening C2 (2026-06-03) — capture ONLY by value: cacheDir (QString),
+    // library (StreamLibrary::has() is mutex-protected and the library outlives
+    // this widget), and `running` (the shared in-flight flag, lifetime-safe).
+    // The old QPointer guard was read from the worker thread (unsafe concurrent
+    // read against GUI-thread deletion); dropping it is sound because the worker
+    // never dereferences the widget — the by-value captures are what keep it safe.
     // Mirrors the showEvent() validateAll fire-and-forget pattern above.
     const QString cacheDir = m_posterCacheDir;
     StreamLibrary* library = m_library;
-    QPointer<StreamLibraryLayout> guard(this);
-    (void) QtConcurrent::run([cacheDir, library, guard]() {
-        if (guard.isNull() || !library) return;
+    auto running = m_orphanSweepRunning;
+    (void) QtConcurrent::run([cacheDir, library, running]() {
+        // Clear the in-flight flag on EVERY return path so a later refresh() can
+        // relaunch (RAII — covers the early bails below and normal completion).
+        struct ClearOnExit {
+            std::shared_ptr<std::atomic_bool> flag;
+            ~ClearOnExit() { flag->store(false); }
+        } clearOnExit{running};
+
+        if (!library) return;
         QDir dir(cacheDir);
         if (!dir.exists()) return;
 
         const QStringList files = dir.entryList({"*.jpg"}, QDir::Files);
         for (const QString& file : files) {
-            if (guard.isNull()) return; // widget gone mid-sweep — stop early
             const QString imdbId = QFileInfo(file).baseName(); // "tt1234567"
             if (!library->has(imdbId))
                 QFile::remove(dir.filePath(file));
diff --git a/src/ui/pages/stream/StreamLibraryLayout.h b/src/ui/pages/stream/StreamLibraryLayout.h
index 175cd1a..d9b7921 100644
--- a/src/ui/pages/stream/StreamLibraryLayout.h
+++ b/src/ui/pages/stream/StreamLibraryLayout.h
@@ -5,6 +5,9 @@
 #include <QLabel>
 #include <QSlider>
 
+#include <atomic>
+#include <memory>
+
 class CoreBridge;
 class StreamLibrary;
 class StreamDownloadIndex;
@@ -69,4 +72,15 @@ private:
     QLabel*    m_emptyLabel   = nullptr;
 
     QString m_posterCacheDir;
+
+    // A005 hardening C3 (2026-06-03) — coalesces overlapping orphan sweeps so a
+    // rapid refresh() burst can't launch multiple concurrent QtConcurrent poster
+    // sweeps racing to QFile::remove the same orphan. A shared_ptr (not a plain
+    // member) so the flag OUTLIVES this widget if a sweep is still running at
+    // teardown: the worker clears it through the shared copy it captured, never
+    // writing through a freed member. This shared atomic also replaces the old
+    // cross-thread QPointer guard read (C2) — the worker now touches only
+    // by-value captures, so nothing is read across the GUI/worker boundary.
+    std::shared_ptr<std::atomic_bool> m_orphanSweepRunning =
+        std::make_shared<std::atomic_bool>(false);
 };
