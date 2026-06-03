Cross-model review for Tankoban 2 (requested by Agent 0, author = Agent 0/Opus). You are a DIFFERENT model. Read-only; do NOT edit. This is the SECOND iteration of the A001 stream-load-correlation fix.

PRIOR ROUNDS:
- You reviewed the original handler-side token gate and returned REQUEST-CHANGES (F1: synchronous empty emit in load() dropped with token 0; F2: stale addon replies could mutate current state).
- I fixed F1/F2 at the aggregator layer; you re-reviewed and confirmed F1/F2 CLOSED, but found a THIRD blocker (F3): "AddonTransport::fetchResource() can emit resourceFailed SYNCHRONOUSLY for an invalid built URL (line 84-87 — confirmed), and if all pending addons complete that way, completeOne() emits streamsReady inside load() before StreamPage stores the returned token → token-0 drop → request waits forever."

THE F3 FIX (this iteration):
Instead of patching individual paths, I made ONE invariant: streamsReady is NEVER emitted synchronously. All three emit sites — load()'s two empty early-returns AND completeOne()'s terminal emit — now route through a single helper emitStreamsReadyDeferred(generation, streams, addonsById), which:
  - queues the emit via QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) so it always fires on a later event-loop turn (after load() has returned and the caller stored its token);
  - drops the emit if generation != m_loadGeneration (superseded load);
  - takes streams/addonsById BY VALUE and moves them into the queued lambda, so a later reset()/load() can't mutate them.
The stale-generation addon-reply drop (dropIfStale in dispatchRequests) from the prior round is unchanged.

VERIFY — do all of:
1. F3 closed? With completeOne() now routing through emitStreamsReadyDeferred, can streamsReady still fire synchronously inside load() via a synchronous resourceFailed during dispatchRequests()? Trace it.
2. No regression from making the terminal (success) emit deferred: is streamsReady still delivered exactly once per load() for the normal async path? Any path where it now NEVER fires (lost emit)? Any double-emit?
3. Safety of the queued-to-self lambda: if the aggregator is destroyed before the queued emit runs, is it safe? (Qt discards queued metacalls to a destroyed receiver — confirm that reasoning holds here.)
4. The by-value capture + std::move of m_streams: correct, or does it leave m_streams in a bad state for any subsequent use within the same generation?

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason. Default REQUEST-CHANGES if F3 is not closed or you are unsure.

================ DIFF UNDER REVIEW (working tree: StreamAggregator.cpp + .h) ================
diff --git a/src/core/stream/StreamAggregator.cpp b/src/core/stream/StreamAggregator.cpp
index df43b47..7afda16 100644
--- a/src/core/stream/StreamAggregator.cpp
+++ b/src/core/stream/StreamAggregator.cpp
@@ -504,22 +504,27 @@ StreamAggregator::StreamAggregator(AddonRegistry* registry, QObject* parent)
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
-        emit streamsReady({}, {});
-        return;
+        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
+        return m_loadGeneration;
     }
 
     const QList<AddonDescriptor> addons =
         m_registry->findByResourceType(QStringLiteral("stream"), request.type);
 
     if (addons.isEmpty()) {
-        emit streamsReady({}, {});
-        return;
+        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
+        return m_loadGeneration;
     }
 
     for (const AddonDescriptor& addon : addons) {
@@ -532,10 +537,39 @@ void StreamAggregator::load(const StreamLoadRequest& request)
     }
 
     dispatchRequests();
+    return m_loadGeneration;
+}
+
+void StreamAggregator::emitStreamsReadyDeferred(quint64 generation,
+                                                QList<Stream> streams,
+                                                QHash<QString, QString> addonsById)
+{
+    // See the header for why this is always queued rather than emitted
+    // synchronously. The queued lambda is bound to `this` as its context object,
+    // so Qt discards it automatically if the aggregator is destroyed before it
+    // runs (no use-after-free).
+    QMetaObject::invokeMethod(this,
+        [this, generation, streams = std::move(streams),
+         addonsById = std::move(addonsById)]() {
+            if (generation != m_loadGeneration)
+                return;  // superseded by a newer load() — drop the stale emit
+            emit streamsReady(streams, addonsById);
+        },
+        Qt::QueuedConnection);
 }
 
 void StreamAggregator::dispatchRequests()
 {
+    // DOWNLOAD BUG 2026-06-03 (review fix) — capture the generation this dispatch
+    // belongs to. A reply from a worker launched by a SUPERSEDED load() (rapid
+    // re-clicks) must be dropped at the source: without this, a stale reply with
+    // the same request shape lands in the CURRENT generation's m_streams and
+    // decrements m_pendingResponses, corrupting accumulation and firing an early
+    // streamsReady that the handler-side token gate would then wrongly accept
+    // (the current generation IS still active). Correlating here closes the gap
+    // that gating only at the handler left open. Mirrors searchPacks()'s epoch
+    // suppression already in this file.
+    const quint64 gen = m_loadGeneration;
     for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
         PendingAddon& addon = it.value();
         if (addon.inFlight) {
@@ -556,13 +590,25 @@ void StreamAggregator::dispatchRequests()
         auto readyConn = std::make_shared<QMetaObject::Connection>();
         auto failConn = std::make_shared<QMetaObject::Connection>();
 
+        // Drop a stale-generation reply (and reap its worker) before it can touch
+        // current state; otherwise apply the existing same-request guard.
+        auto dropIfStale = [this, gen, handled, readyConn, failConn, worker]() -> bool {
+            if (gen == m_loadGeneration)
+                return false;
+            *handled = true;
+            QObject::disconnect(*readyConn);
+            QObject::disconnect(*failConn);
+            worker->deleteLater();
+            return true;
+        };
+
         *readyConn = connect(worker, &AddonTransport::resourceReady, this,
-            [this, req, addonId, handled, readyConn, failConn, worker](
+            [this, req, addonId, handled, readyConn, failConn, worker, dropIfStale](
                 const ResourceRequest& incoming,
                 const QJsonObject& payload) {
-                if (*handled || !sameRequest(req, incoming)) {
-                    return;
-                }
+                if (*handled) return;
+                if (dropIfStale()) return;
+                if (!sameRequest(req, incoming)) return;
                 *handled = true;
                 QObject::disconnect(*readyConn);
                 QObject::disconnect(*failConn);
@@ -571,12 +617,12 @@ void StreamAggregator::dispatchRequests()
             });
 
         *failConn = connect(worker, &AddonTransport::resourceFailed, this,
-            [this, req, addonId, handled, readyConn, failConn, worker](
+            [this, req, addonId, handled, readyConn, failConn, worker, dropIfStale](
                 const ResourceRequest& incoming,
                 const QString& message) {
-                if (*handled || !sameRequest(req, incoming)) {
-                    return;
-                }
+                if (*handled) return;
+                if (dropIfStale()) return;
+                if (!sameRequest(req, incoming)) return;
                 *handled = true;
                 QObject::disconnect(*readyConn);
                 QObject::disconnect(*failConn);
@@ -636,11 +682,22 @@ void StreamAggregator::onAddonFailed(const QString& addonId, const QString& mess
 
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
     }
-    emit streamsReady(m_streams, m_addonsById);
+    // DOWNLOAD BUG 2026-06-03 (review fix) — deferred + generation-guarded so a
+    // SYNCHRONOUS resourceFailed (invalid addon URL) during dispatchRequests()
+    // can't fire streamsReady inside load() before the caller stores its token.
+    emitStreamsReadyDeferred(m_loadGeneration, m_streams, m_addonsById);
 }
 
 void StreamAggregator::reset()
diff --git a/src/core/stream/StreamAggregator.h b/src/core/stream/StreamAggregator.h
index 8f3d83f..0ea220b 100644
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
@@ -90,6 +101,23 @@ private:
     void onAddonFailed(const QString& addonId, const QString& message);
     void completeOne();
     void reset();
+    // DOWNLOAD BUG 2026-06-03 (review fix) — the SINGLE terminal emit path for
+    // streamsReady. Always queues the emit to the NEXT event-loop turn rather
+    // than firing synchronously, and drops it if a newer load() has superseded
+    // `generation`. Two reasons it must never fire synchronously inside load():
+    //   (1) Callers arm their one-shot before load() returns and fill the
+    //       correlation token FROM load()'s return value; a synchronous emit
+    //       would fire while that token is still 0 and be wrongly discarded
+    //       (the result for the CURRENT request lost, leaving the UI waiting).
+    //   (2) dispatchRequests() runs inside load(), and AddonTransport can emit
+    //       resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon) —
+    //       so completeOne() could otherwise reach this emit before load()
+    //       returns. Queuing makes the post-return ordering unconditional.
+    // streams/addonsById are taken by value and captured into the queued lambda
+    // so a subsequent reset()/load() can't mutate them out from under the emit.
+    void emitStreamsReadyDeferred(quint64 generation,
+                                  QList<tankostream::addon::Stream> streams,
+                                  QHash<QString, QString> addonsById);
     void finalizePackSearch(std::shared_ptr<PackSearchContext> ctx);
 
     tankostream::addon::AddonRegistry* m_registry = nullptr;
@@ -101,6 +129,14 @@ private:
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
