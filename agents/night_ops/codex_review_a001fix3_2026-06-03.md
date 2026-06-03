Cross-model review for Tankoban 2 (requested by Agent 0, author = Agent 0/Opus). DIFFERENT model. Read-only; do NOT edit. THIRD iteration of the A001 stream-load-correlation fix.

PRIOR ROUNDS (all confirmed by you):
- F1 (sync empty emit dropped with token 0) — CLOSED.
- F2 (stale addon replies mutate current state) — CLOSED via dropIfStale generation guard.
- F3 (completeOne emits inside load() via SYNCHRONOUS resourceFailed) — addressed by routing ALL streamsReady through emitStreamsReadyDeferred (always queued).
- F4 (your last finding): "a synchronous resourceFailed during dispatchRequests() can still drive m_pendingResponses from 1 to 0 before later addons are dispatched, queuing an early streamsReady and possibly duplicate emits for the same generation."

THE F4 FIX (this iteration), in dispatchRequests():
The counter is no longer incremented one-at-a-time inside the dispatch loop. Instead:
  1. First pass: walk m_pendingByAddon, mark each not-yet-inFlight addon inFlight=true, and collect its id into a local QList<QString> toDispatch.
  2. m_pendingResponses += toDispatch.size()  — count the ENTIRE batch up-front, before any fetchResource().
  3. Second pass: iterate toDispatch (a separate list, NOT the map) and for each id create the worker + connect + fetchResource().
So a synchronous resourceFailed for an early addon decrements from the full batch count (N -> N-1, still > 0 while others remain); the counter can only reach 0 after the LAST completion → emitStreamsReadyDeferred fires exactly once. Iterating the id list (not the map) also avoids mutate-during-iteration if a synchronous reply touches m_pendingByAddon (it only flips inFlight, never erases, but the separate list is defensive).

VERIFY — do all of:
1. F4 closed? Walk: 3 addons, addon #1's fetchResource fails SYNCHRONOUSLY. Does m_pendingResponses stay > 0 until all 3 complete? Is streamsReady emitted exactly once (no premature, no duplicate)?
2. The all-synchronous-failure case: all 3 fail synchronously in the second loop — exactly one deferred emit, with the correct (empty) result, for the correct generation?
3. Mixed: some sync-fail, some async-succeed — single terminal emit after the last async completion, carrying the successful streams?
4. Any NEW issue from the two-pass restructure: an addon counted in m_pendingResponses but never dispatched (counter never reaches 0 → emit never fires)? Double-count? The `addonIt == end()` defensive continue leaving the count too high? Iterator/reference validity of `addon` (addonIt.value()) across a synchronous reply within the same loop iteration?
5. Generation correctness still holds (dropIfStale + the deferred emit's generation guard) under a rapid re-click that supersedes mid-dispatch.

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason. Default REQUEST-CHANGES if F4 not closed or unsure.

================ DIFF UNDER REVIEW (working tree: StreamAggregator.cpp + .h) ================
diff --git a/src/core/stream/StreamAggregator.cpp b/src/core/stream/StreamAggregator.cpp
index df43b47..e290399 100644
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
@@ -532,17 +537,63 @@ void StreamAggregator::load(const StreamLoadRequest& request)
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
+
+    // DOWNLOAD BUG 2026-06-03 (review fix) — count EVERY addon we are about to
+    // dispatch BEFORE firing any request. AddonTransport::fetchResource() can
+    // emit resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon), so
+    // incrementing m_pendingResponses one-at-a-time inside the dispatch loop let
+    // an early addon's synchronous failure drive the counter to 0 before later
+    // addons were dispatched — firing a premature streamsReady (empty), and then
+    // a SECOND one when the rest completed. Pre-counting makes the terminal emit
+    // fire exactly once, after the last completion. Iterating a separate id list
+    // (not the map) also avoids any mutate-during-iteration hazard if a
+    // synchronous reply touches m_pendingByAddon.
+    QList<QString> toDispatch;
     for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
-        PendingAddon& addon = it.value();
-        if (addon.inFlight) {
-            continue;
-        }
-        addon.inFlight = true;
-        ++m_pendingResponses;
+        if (it.value().inFlight) continue;
+        it.value().inFlight = true;
+        toDispatch.append(it.key());
+    }
+    if (toDispatch.isEmpty()) return;
+    m_pendingResponses += toDispatch.size();
+
+    for (const QString& addonId : toDispatch) {
+        auto addonIt = m_pendingByAddon.find(addonId);
+        if (addonIt == m_pendingByAddon.end()) continue;  // defensive
+        PendingAddon& addon = addonIt.value();
 
         ResourceRequest req;
         req.resource = QStringLiteral("stream");
@@ -551,18 +602,29 @@ void StreamAggregator::dispatchRequests()
         req.extra = m_request.extra;
 
         auto* worker = new AddonTransport(this);
-        const QString addonId = addon.addonId;
         auto handled = std::make_shared<bool>(false);
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
@@ -571,12 +633,12 @@ void StreamAggregator::dispatchRequests()
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
@@ -636,11 +698,22 @@ void StreamAggregator::onAddonFailed(const QString& addonId, const QString& mess
 
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
