Cross-model review for Tankoban 2 (requested by Agent 0). The author of the changes below is Agent 0 (Opus) — you are a DIFFERENT model. Read-only review; do NOT edit.

BACKGROUND: This is the A001 stream-load-correlation fix. A PRIOR version of this code (the StreamPage handler-side "generation token" gate) was reviewed by you earlier and you returned REQUEST-CHANGES with two findings. This diff is the FIX for those findings, made at the aggregator layer. Your job: verify the two findings are actually closed, and that the fix introduces no new bug.

YOUR PRIOR TWO FINDINGS (verify each is now closed):
F1 (blocker): "all three StreamPage one-shots arm the connection BEFORE assigning the returned token; synchronous streamsReady paths in StreamAggregator::load() will be discarded with token 0 and can leave handlers/pending state stuck." — i.e. load() could emit streamsReady SYNCHRONOUSLY on its early-return paths (no registry / empty request / no addons) while the caller's token is still 0, so the legitimate empty result for the CURRENT request was dropped.
F2: "StreamAggregator does not tag addon callbacks/results with a generation, so stale callbacks can still mutate current m_streams / m_pendingResponses; a stale emit after a newer load() sees the newer current token and can pass" — i.e. correlation was only enforced at the handler, not where streams accumulate/emit.

THE FIX (what changed in this diff):
1. load()'s two synchronous `emit streamsReady({}, {})` early-returns are replaced with emitEmptyDeferred(m_loadGeneration), which queues the emit (Qt::QueuedConnection) to the NEXT event-loop turn and drops it if a newer load() has superseded this generation. Intent: the caller's token is assigned from load()'s return value BEFORE the emit fires (closes F1), and a superseded empty-emit is dropped (no wrong delivery).
2. dispatchRequests() captures the generation (gen = m_loadGeneration) for the workers it launches. Each worker reply lambda calls dropIfStale(): if gen != m_loadGeneration the reply is dropped (handled=true, disconnect both conns, worker->deleteLater()) BEFORE it can reach onAddonReady/onAddonFailed. Intent: a reply from a worker launched by a SUPERSEDED load() can never mutate the current generation's m_streams or decrement m_pendingResponses (closes F2 at the source). The existing handler-side token gate in StreamPage stays as defense-in-depth.

VERIFY — do all of:
1. F1 closed? Trace the no-addons / empty-request path: does the current request's empty result now reach the armed one-shot (token assigned first), AND is a superseded load's empty emit correctly dropped (not delivered to the new generation's handler)?
2. F2 closed? Can a stale-generation addon reply still mutate m_streams / m_pendingResponses or trigger an early streamsReady for the current generation? Walk the rapid-re-click race (same and different request shapes).
3. New bugs introduced by the fix: double-emit, an empty result that NEVER arrives, a leak (workers not reaped), use-after-free, a deadlock, or the deferred lambda firing after the aggregator is destroyed (is the queued-to-self lambda safe if `this` is deleted before it runs?).
4. Any DoD item the original A001 intent should have covered but still doesn't.

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason. Be terse; default REQUEST-CHANGES if F1 or F2 is not closed or you are unsure.

================ DIFF UNDER REVIEW (working tree: StreamAggregator + StreamPage) ================
diff --git a/src/core/stream/StreamAggregator.cpp b/src/core/stream/StreamAggregator.cpp
index df43b47..0fdc0ae 100644
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
+        emitEmptyDeferred(m_loadGeneration);  // review fix — see emitEmptyDeferred()
+        return m_loadGeneration;
     }
 
     const QList<AddonDescriptor> addons =
         m_registry->findByResourceType(QStringLiteral("stream"), request.type);
 
     if (addons.isEmpty()) {
-        emit streamsReady({}, {});
-        return;
+        emitEmptyDeferred(m_loadGeneration);  // review fix — see emitEmptyDeferred()
+        return m_loadGeneration;
     }
 
     for (const AddonDescriptor& addon : addons) {
@@ -532,10 +537,31 @@ void StreamAggregator::load(const StreamLoadRequest& request)
     }
 
     dispatchRequests();
+    return m_loadGeneration;
+}
+
+void StreamAggregator::emitEmptyDeferred(quint64 generation)
+{
+    // See the header for why this is queued rather than emitted synchronously.
+    QMetaObject::invokeMethod(this, [this, generation]() {
+        if (generation != m_loadGeneration)
+            return;  // superseded by a newer load() — drop the stale empty emit
+        emit streamsReady({}, {});
+    }, Qt::QueuedConnection);
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
@@ -556,13 +582,25 @@ void StreamAggregator::dispatchRequests()
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
@@ -571,12 +609,12 @@ void StreamAggregator::dispatchRequests()
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
@@ -636,6 +674,14 @@ void StreamAggregator::onAddonFailed(const QString& addonId, const QString& mess
 
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
index 8f3d83f..478f229 100644
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
@@ -90,6 +101,15 @@ private:
     void onAddonFailed(const QString& addonId, const QString& message);
     void completeOne();
     void reset();
+    // DOWNLOAD BUG 2026-06-03 (review fix) — emit an empty streamsReady on the
+    // NEXT event-loop turn instead of synchronously inside load(). Callers arm
+    // their one-shot before load() returns and fill the correlation token FROM
+    // load()'s return value; a synchronous empty emit would fire while that
+    // token is still 0 and be wrongly discarded (the legitimate "no sources"
+    // result for the CURRENT request lost, leaving the UI waiting). Queuing it
+    // guarantees the token is set first; the captured generation drops the emit
+    // if a newer load() has superseded this one.
+    void emitEmptyDeferred(quint64 generation);
     void finalizePackSearch(std::shared_ptr<PackSearchContext> ctx);
 
     tankostream::addon::AddonRegistry* m_registry = nullptr;
@@ -101,6 +121,14 @@ private:
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
 
