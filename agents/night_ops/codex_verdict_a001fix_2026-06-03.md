OpenAI Codex v0.131.0
--------
workdir: C:\Users\Suprabha\Desktop\Tankoban 2
model: gpt-5.5
provider: openai
approval: never
sandbox: read-only
reasoning effort: high
reasoning summaries: none
session id: 019e8c8c-b6d9-7b01-853f-5d5a17e86f56
--------
user
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
 

codex
I’ll review this as a read-only code review and check the actual surrounding implementation, especially connection ownership, reset semantics, and queued lambda behavior.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"quint64 StreamAggregator::load|emitEmptyDeferred|dispatchRequests|completeOne|reset\\(|m_pendingByAddon|m_pendingResponses|streamsReady|resourceReady|resourceFailed\" src/core/stream/StreamAggregator.cpp src/core/stream/StreamAggregator.h" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"currentLoadToken|m_pendingAuto\\.token|startAutoDownload|onPlayRequested|startNextEpisodePrefetch|disconnect\\(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady\" src/ui/pages/StreamPage.cpp src/ui/pages/StreamPage.h" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"class AddonTransport|AddonTransport::|resourceReady|resourceFailed|deleteLater\\(|new AddonTransport|std::make_shared<QMetaObject::Connection>|sameRequest\" src -S" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 3542ms:
src/core/stream/StreamAggregator.h:41:    // reset()). Callers that arm a one-shot on the SHARED streamsReady signal
src/core/stream/StreamAggregator.h:43:    // so a late streamsReady from a SUPERSEDED load() (rapid re-clicks) cannot
src/core/stream/StreamAggregator.h:78:    void streamsReady(const QList<tankostream::addon::Stream>& streams,
src/core/stream/StreamAggregator.h:99:    void dispatchRequests();
src/core/stream/StreamAggregator.h:102:    void completeOne();
src/core/stream/StreamAggregator.h:103:    void reset();
src/core/stream/StreamAggregator.h:104:    // DOWNLOAD BUG 2026-06-03 (review fix) — emit an empty streamsReady on the
src/core/stream/StreamAggregator.h:112:    void emitEmptyDeferred(quint64 generation);
src/core/stream/StreamAggregator.h:118:    QMap<QString, PendingAddon> m_pendingByAddon;
src/core/stream/StreamAggregator.h:122:    int m_pendingResponses = 0;
src/core/stream/StreamAggregator.h:125:    // load() (right after reset()) and returned to the caller so a one-shot
src/core/stream/StreamAggregator.h:126:    // streamsReady handler can correlate the emit back to ITS request and
src/core/stream/StreamAggregator.h:128:    // re-click race where a late streamsReady from an earlier load() fires the
src/core/stream/StreamAggregator.cpp:507:quint64 StreamAggregator::load(const StreamLoadRequest& request)
src/core/stream/StreamAggregator.cpp:509:    reset();
src/core/stream/StreamAggregator.cpp:511:    // reset() that zeroed mid-flight state. The returned token lets the caller
src/core/stream/StreamAggregator.cpp:512:    // gate its one-shot streamsReady handler against currentLoadToken() so a
src/core/stream/StreamAggregator.cpp:518:        emitEmptyDeferred(m_loadGeneration);  // review fix — see emitEmptyDeferred()
src/core/stream/StreamAggregator.cpp:526:        emitEmptyDeferred(m_loadGeneration);  // review fix — see emitEmptyDeferred()
src/core/stream/StreamAggregator.cpp:535:        m_pendingByAddon.insert(pending.addonId, pending);
src/core/stream/StreamAggregator.cpp:539:    dispatchRequests();
src/core/stream/StreamAggregator.cpp:543:void StreamAggregator::emitEmptyDeferred(quint64 generation)
src/core/stream/StreamAggregator.cpp:549:        emit streamsReady({}, {});
src/core/stream/StreamAggregator.cpp:553:void StreamAggregator::dispatchRequests()
src/core/stream/StreamAggregator.cpp:559:    // decrements m_pendingResponses, corrupting accumulation and firing an early
src/core/stream/StreamAggregator.cpp:560:    // streamsReady that the handler-side token gate would then wrongly accept
src/core/stream/StreamAggregator.cpp:565:    for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
src/core/stream/StreamAggregator.cpp:571:        ++m_pendingResponses;
src/core/stream/StreamAggregator.cpp:597:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src/core/stream/StreamAggregator.cpp:611:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream/StreamAggregator.cpp:631:    auto addonIt = m_pendingByAddon.find(addonId);
src/core/stream/StreamAggregator.cpp:632:    if (addonIt == m_pendingByAddon.end()) {
src/core/stream/StreamAggregator.cpp:633:        completeOne();
src/core/stream/StreamAggregator.cpp:662:    completeOne();
src/core/stream/StreamAggregator.cpp:667:    auto addonIt = m_pendingByAddon.find(addonId);
src/core/stream/StreamAggregator.cpp:668:    if (addonIt != m_pendingByAddon.end()) {
src/core/stream/StreamAggregator.cpp:672:    completeOne();
src/core/stream/StreamAggregator.cpp:675:void StreamAggregator::completeOne()
src/core/stream/StreamAggregator.cpp:678:    // a superseded load() (reset() zeroed m_pendingResponses mid-flight) could
src/core/stream/StreamAggregator.cpp:679:    // otherwise drive the counter negative and re-emit streamsReady against the
src/core/stream/StreamAggregator.cpp:681:    if (m_pendingResponses <= 0) {
src/core/stream/StreamAggregator.cpp:682:        m_pendingResponses = 0;
src/core/stream/StreamAggregator.cpp:685:    --m_pendingResponses;
src/core/stream/StreamAggregator.cpp:686:    if (m_pendingResponses > 0) {
src/core/stream/StreamAggregator.cpp:689:    emit streamsReady(m_streams, m_addonsById);
src/core/stream/StreamAggregator.cpp:692:void StreamAggregator::reset()
src/core/stream/StreamAggregator.cpp:695:    m_pendingByAddon.clear();
src/core/stream/StreamAggregator.cpp:699:    m_pendingResponses = 0;

 succeeded in 3604ms:
src/ui/pages/StreamPage.h:260:    void onPlayRequested(const QString& imdbId, const QString& mediaType,
src/ui/pages/StreamPage.h:335:        // currentLoadToken()==token so a late emit from a SUPERSEDED load()
src/ui/pages/StreamPage.h:341:    void startAutoDownload(const QString& imdbId, const QString& mediaType,
src/ui/pages/StreamPage.h:373:    void startNextEpisodePrefetch(const QString& imdbId,
src/ui/pages/StreamPage.h:465:    // Stream aggregator (Phase 4 Batch 4.1) — multi-source stream fan-out for onPlayRequested
src/ui/pages/StreamPage.h:472:    // Fed with the selected Stream on onPlayRequested; result pushed to
src/ui/pages/StreamPage.h:530:    // Stream-picker UX rework — context for the in-flight onPlayRequested.
src/ui/pages/StreamPage.cpp:830:    connect(m_detailView, &StreamDetailView::playRequested, this, &StreamPage::onPlayRequested);
src/ui/pages/StreamPage.cpp:1047:    // so onPlayRequested runs the source-pick aggregator. Spec Â§6.3.
src/ui/pages/StreamPage.cpp:1052:                onPlayRequested(m_detailView->currentImdb(),
src/ui/pages/StreamPage.cpp:1385:            // fires onPlayRequested â†’ loads streams â†’ populates m_detailView
src/ui/pages/StreamPage.cpp:1399:                onPlayRequested(imdbId, mediaType, season, episode);
src/ui/pages/StreamPage.cpp:2236:void StreamPage::onPlayRequested(const QString& imdbId, const QString& mediaType,
src/ui/pages/StreamPage.cpp:2268:                 QStringLiteral("onPlayRequested"));
src/ui/pages/StreamPage.cpp:2316:    // a second onPlayRequested (user clicks another episode mid-countdown)
src/ui/pages/StreamPage.cpp:2323:    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src/ui/pages/StreamPage.cpp:2337:            if (m_streamAggregator->currentLoadToken() != *playToken)
src/ui/pages/StreamPage.cpp:2339:            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src/ui/pages/StreamPage.cpp:2451:void StreamPage::startNextEpisodePrefetch(const QString& imdbId,
src/ui/pages/StreamPage.cpp:2526:                    if (m_streamAggregator->currentLoadToken() != *prefetchToken)
src/ui/pages/StreamPage.cpp:2758:    // this is â€” same shape onPlayRequested would have produced, minus the
src/ui/pages/StreamPage.cpp:2853:// (trailer paste, magnet paste, onPlayRequested, onNextEpisodePlayNow).
src/ui/pages/StreamPage.cpp:2954:    // and arm the shortcut-pending flag. startNextEpisodePrefetch reuses
src/ui/pages/StreamPage.cpp:2959:    startNextEpisodePrefetch(m_session.pending.imdbId,
src/ui/pages/StreamPage.cpp:3111:    startAutoDownload(m_detailView->currentImdb(), QStringLiteral("series"), season, episode);
src/ui/pages/StreamPage.cpp:3258:// Mirrors onPlayRequested's one-shot streamsReady idiom (StreamPage.cpp ~2308-
src/ui/pages/StreamPage.cpp:3262:void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaType,
src/ui/pages/StreamPage.cpp:3269:    // clicks (the logs show 2-6 startAutoDownload within seconds) used to
src/ui/pages/StreamPage.cpp:3293:    qInfo().noquote() << "[auto-dl] startAutoDownload imdb=" << imdbId
src/ui/pages/StreamPage.cpp:3296:    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src/ui/pages/StreamPage.cpp:3305:            if (m_streamAggregator->currentLoadToken() != m_pendingAuto.token)
src/ui/pages/StreamPage.cpp:3307:            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src/ui/pages/StreamPage.cpp:3327:    // Surface a fetch error on the auto-download path (mirrors onPlayRequested's
src/ui/pages/StreamPage.cpp:3345:    m_pendingAuto.token = m_streamAggregator->load(req);
src/ui/pages/StreamPage.cpp:4182:    // future onPlayRequested calls keeps working.
src/ui/pages/StreamPage.cpp:4267:    startAutoDownload(imdbId, mediaType, season, episode);

 succeeded in 6593ms:
src\main.cpp:105:            conn->deleteLater();
src\devtools\DevControlServer.cpp:254:        conn->deleteLater();
src\devtools\DevControlServer.cpp:266:        conn->deleteLater();
src\devtools\DevControlServer.cpp:280:        conn->deleteLater();
src\devtools\DevControlServer.cpp:305:    conn->deleteLater();
src\ui\dialogs\AddAddonDialog.cpp:40:    m_probe = new AddonTransport(this);
src\ui\dialogs\AddAddonDialog.cpp:41:    connect(m_probe, &AddonTransport::manifestReady,
src\ui\dialogs\AddAddonDialog.cpp:43:    connect(m_probe, &AddonTransport::manifestFailed,
src\ui\dialogs\AddAddonDialog.h:14:class AddonTransport;
src\core\book\AbbScraper.cpp:69:    m_activeReply->deleteLater();
src\core\book\AbbScraper.cpp:129:            m_activeReply->deleteLater();
src\core\book\AbbScraper.cpp:140:    reply->deleteLater();
src\core\book\AbbScraper.cpp:356:            m_activeReply->deleteLater();
src\core\book\AbbScraper.cpp:367:    reply->deleteLater();
src\ui\MainWindow.cpp:1359:        m_trayIcon->deleteLater();
src\ui\MainWindow.cpp:1363:        m_trayMenu->deleteLater();
src\ui\pages\BooksPage.cpp:800:        menu->deleteLater();
src\ui\pages\BooksPage.cpp:1430:        if (auto* widget = item->widget()) widget->deleteLater();
src\ui\pages\BooksPage.cpp:1639:        menu->deleteLater();
src\ui\pages\BooksPage.cpp:1715:    menu->deleteLater();
src\ui\pages\BooksPage.cpp:1745:    menu->deleteLater();
src\ui\pages\books\BookCatalogueDetailView.cpp:499:        reply->deleteLater();
src\ui\pages\books\BookCatalogueDetailView.cpp:939:            widget->deleteLater();
src\ui\pages\books\BookSeriesDetailView.cpp:319:        reply->deleteLater();
src\ui\pages\books\BookSeriesDetailView.cpp:458:        if (QWidget* w = item->widget()) w->deleteLater();
src\ui\pages\books\BooksDownloadsPage.cpp:194:        if (QWidget* w = item->widget()) w->deleteLater();
src\ui\pages\books\BooksDownloadsPage.cpp:225:        if (QWidget* w = item->widget()) w->deleteLater();
src\ui\pages\books\BookCatalogueSearchWidget.cpp:250:    menu->deleteLater();
src\ui\pages\books\BookCatalogueSearchWidget.cpp:496:        reply->deleteLater();
src\ui\pages\ComicsPage.cpp:1360:        menu->deleteLater();
src\ui\pages\ComicsPage.cpp:2417:        reply->deleteLater();
src\ui\pages\ComicsPage.cpp:3346:        if (auto* w = item->widget()) w->deleteLater();
src\ui\pages\ComicsPage.cpp:4319:    menu->deleteLater();
src\ui\pages\ComicsPage.cpp:4364:    menu->deleteLater();
src\ui\pages\comics\ComicsDownloadsPage.cpp:262:        reply->deleteLater();
src\ui\pages\comics\ComicsDownloadsPage.cpp:284:        if (auto* w = item->widget()) w->deleteLater();
src\ui\pages\comics\ComicsDownloadsPage.cpp:577:            menu->deleteLater();
src\ui\pages\SeriesView.cpp:307:        menu->deleteLater();
src\ui\pages\SeriesView.cpp:442:            menu->deleteLater();
src\ui\pages\SeriesView.cpp:532:        menu->deleteLater();
src\ui\pages\SeriesView.cpp:669:            w->deleteLater();
src\ui\pages\PosterPickerPopover.h:30:// One-shot lifecycle: deleteLater() on any dismissal path (selection, outside
src\ui\pages\PosterPickerPopover.cpp:185:    deleteLater();
src\ui\widgets\Toast.cpp:19:        t->deleteLater();
src\ui\widgets\Toast.cpp:78:            // deleteLater() can't chew out-from-under us if the action
src\ui\widgets\Toast.cpp:82:            deleteLater();
src\ui\widgets\Toast.cpp:99:        deleteLater();
src\ui\RootFoldersOverlay.cpp:103:            w->deleteLater();
src\core\indexers\YtsIndexer.cpp:46:    reply->deleteLater();
src\core\indexers\X1337xIndexer.cpp:150:    reply->deleteLater();
src\core\indexers\X1337xIndexer.cpp:295:    reply->deleteLater();
src\core\indexers\TorrentsCsvIndexer.cpp:42:    reply->deleteLater();
src\core\indexers\PirateBayIndexer.cpp:44:    reply->deleteLater();
src\core\indexers\NyaaIndexer.cpp:61:    reply->deleteLater();
src\core\indexers\EztvIndexer.cpp:105:    reply->deleteLater();
src\core\indexers\ExtTorrentsIndexer.cpp:65:    reply->deleteLater();
src\core\indexers\ExtTorrentsIndexer.cpp:237:    reply->deleteLater();
src\core\book\BookDownloader.cpp:700:            reply->deleteLater();
src\core\book\BookDownloader.cpp:790:    reply->deleteLater();
src\core\book\BookDownloader.cpp:907:            r->deleteLater();
src\core\book\AnnaArchiveScraper.cpp:669:        m_waitHandler->deleteLater();
src\core\book\AnnaArchiveScraper.cpp:890:                    m_waitHandler->deleteLater();
src\core\book\AnnaArchiveScraper.cpp:1044:                    m_waitHandler->deleteLater();
src\core\net\NetSeam.cpp:25:// (caller owns it, calls deleteLater() when finished).
src\core\net\NetSeam.cpp:65:// they always have, preserving every existing connect()/abort()/deleteLater()
src\core\net\NetSeam.cpp:141:            // Note: no parent → caller owns the reply via deleteLater().
src\core\net\HttpFileDownloader.cpp:31:        m_reply->deleteLater();
src\core\net\HttpFileDownloader.cpp:56:        m_file->deleteLater();
src\core\net\HttpFileDownloader.cpp:94:        m_reply->deleteLater();
src\core\net\HttpFileDownloader.cpp:105:            m_file->deleteLater();
src\core\net\HttpFileDownloader.cpp:117:            m_file->deleteLater();
src\core\net\HttpFileDownloader.cpp:123:        m_file->deleteLater();
src\ui\pages\VideosPage.cpp:705:        menu->deleteLater();
src\ui\pages\VideosPage.cpp:825:            menu->deleteLater();
src\ui\pages\VideosPage.cpp:1128:        menu->deleteLater();
src\ui\pages\TileStrip.cpp:24:        tile->deleteLater();
src\ui\pages\TileCard.cpp:533:    edit->deleteLater();
src\ui\pages\comics\ComicsTankoyomiSearchWidget.cpp:192:            reply->deleteLater();
src\ui\pages\comics\ComicsSourcesPanel.cpp:408:        card->deleteLater();
src\ui\pages\comics\ComicsSeriesView.cpp:2059:        reply->deleteLater();
src\ui\pages\comics\ComicsSeriesView.cpp:2133:        reply->deleteLater();
src\ui\pages\comics\ComicsSeriesView.cpp:2239:        reply->deleteLater();
src\ui\pages\comics\ComicsSeriesView.cpp:2316:        if (QWidget* w = item->widget()) w->deleteLater();
src\ui\pages\comics\ComicsSeriesView.cpp:2344:        if (QWidget* w = item->widget()) w->deleteLater();
src\ui\pages\comics\ComicsSeriesView.cpp:2836:    menu->deleteLater();
src\ui\pages\TankoLibraryPage.cpp:1499:        reply->deleteLater();
src\ui\pages\TankoLibraryPage.cpp:1621:    auto conn    = std::make_shared<QMetaObject::Connection>();
src\ui\pages\TankoLibraryPage.cpp:1622:    auto errConn = std::make_shared<QMetaObject::Connection>();
src\ui\pages\TankoLibraryPage.cpp:1687:        m_coverReply->deleteLater();
src\ui\pages\TankoLibraryPage.cpp:1785:        m_coverReply->deleteLater();
src\ui\pages\TankoLibraryPage.cpp:1843:        reply->deleteLater();
src\core\tts\EdgeTtsClient.cpp:853:    m_socket->deleteLater();
src\ui\pages\StreamPage.cpp:198:        animation->deleteLater();
src\ui\pages\StreamPage.cpp:1734:        if (auto* w = item->widget()) w->deleteLater();
src\ui\pages\StreamPage.cpp:3658:        collector->deleteLater();
src\ui\pages\StreamPage.cpp:3696:        verifier->deleteLater();
src\ui\pages\StreamPage.cpp:3872:        collector->deleteLater();
src\ui\pages\StreamPage.cpp:3879:        verifier->deleteLater();
src\ui\pages\StreamPage.cpp:3888:        dialog->deleteLater();
src\ui\pages\StreamPage.cpp:3905:        m_bulkSourceCollector->deleteLater();
src\ui\pages\StreamPage.cpp:3953:        m_bulkPackVerifier->deleteLater();
src\ui\pages\StreamPage.cpp:3980:        m_bulkPackVerifier->deleteLater();
src\ui\pages\StreamPage.cpp:3990:        dialog->deleteLater();
src\ui\readers\ComicReader.cpp:2324:        m_verticalThumb->deleteLater();
src\ui\readers\ComicReader.cpp:2333:        m_stripCanvas->deleteLater();
src\ui\readers\ComicReader.cpp:3162:                m_thumbsContent->deleteLater();
src\ui\readers\ComicReader.cpp:3776:    menu->deleteLater();
src\core\book\LibGenScraper.cpp:68:    m_activeReply->deleteLater();
src\core\book\LibGenScraper.cpp:140:            m_activeReply->deleteLater();
src\core\book\LibGenScraper.cpp:151:    reply->deleteLater();
src\core\book\LibGenScraper.cpp:423:            m_activeReply->deleteLater();
src\core\book\LibGenScraper.cpp:435:    reply->deleteLater();
src\core\book\LibGenScraper.cpp:465:    reply->deleteLater();
src\core\book\FictionDbClient.cpp:351:    reply->deleteLater();
src\core\book\FictionDbClient.cpp:365:    reply->deleteLater();
src\core\book\FictionDbClient.cpp:379:    reply->deleteLater();
src\core\book\FictionDbClient.cpp:395:    reply->deleteLater();
src\core\PosterFetcher.cpp:108:        reply->deleteLater();
src\core\TankorentSearchService.cpp:166:        idx->deleteLater();
src\ui\pages\stream\CatalogBrowseScreen.cpp:337:            widget->deleteLater();
src\ui\pages\stream\AddonManagerScreen.cpp:293:        reply->deleteLater();
src\ui\pages\stream\AddonDetailPanel.cpp:427:            widget->deleteLater();
src\ui\pages\stream\AddonDetailPanel.cpp:487:                reply->deleteLater();
src\ui\pages\ShowView.cpp:288:        menu->deleteLater();
src\ui\pages\ShowView.cpp:317:        menu->deleteLater();
src\ui\pages\ShowView.cpp:430:                menu->deleteLater();
src\ui\pages\ShowView.cpp:516:        menu->deleteLater();
src\ui\pages\ShowView.cpp:652:            w->deleteLater();
src\ui\pages\stream\StreamDetailView.cpp:2380:            reply->deleteLater();
src\ui\pages\stream\StreamDetailView.cpp:2733:            reply->deleteLater();
src\ui\pages\stream\StreamDownloadsPage.cpp:304:        reply->deleteLater();
src\ui\pages\stream\StreamDownloadsPage.cpp:381:        if (auto* w = item->widget()) w->deleteLater();
src\ui\pages\stream\StreamDownloadsPage.cpp:533:        if (auto* w = item->widget()) w->deleteLater();
src\ui\pages\stream\StreamLibraryLayout.cpp:414:        reply->deleteLater();
src\ui\pages\stream\StreamSearchWidget.cpp:409:        reply->deleteLater();
src\core\manga\anilist\AniListClient.cpp:203:    reply->deleteLater();
src\core\manga\anilist\AniListClient.cpp:237:    reply->deleteLater();
src\core\stream\BulkPackVerifier.cpp:305:        m_timeout->deleteLater();
src\core\stream\addon\AddonRegistry.cpp:290:    , m_transport(transport ? transport : new AddonTransport(this))
src\core\stream\addon\AddonRegistry.cpp:292:    connect(m_transport, &AddonTransport::manifestReady,
src\core\stream\addon\AddonRegistry.cpp:294:    connect(m_transport, &AddonTransport::manifestFailed,
src\ui\player\SidecarProcess.cpp:1006:            reply->deleteLater();
src\core\stream\addon\AddonRegistry.h:12:class AddonTransport;
src\core\stream\addon\AddonTransport.cpp:34:AddonTransport::AddonTransport(QObject* parent)
src\core\stream\addon\AddonTransport.cpp:40:void AddonTransport::fetchManifest(const QUrl& base)
src\core\stream\addon\AddonTransport.cpp:55:        reply->deleteLater();
src\core\stream\addon\AddonTransport.cpp:81:void AddonTransport::fetchResource(const QUrl& base, const ResourceRequest& request)
src\core\stream\addon\AddonTransport.cpp:85:        emit resourceFailed(request, QStringLiteral("Invalid resource URL"));
src\core\stream\addon\AddonTransport.cpp:96:        reply->deleteLater();
src\core\stream\addon\AddonTransport.cpp:98:            emit resourceFailed(request, reply->errorString());
src\core\stream\addon\AddonTransport.cpp:105:            emit resourceFailed(request, QStringLiteral("Resource JSON parse error: ") +
src\core\stream\addon\AddonTransport.cpp:110:        emit resourceReady(request, doc.object());
src\core\stream\addon\AddonTransport.cpp:114:QUrl AddonTransport::normalizeManifestUrl(QUrl base)
src\core\stream\addon\AddonTransport.cpp:132:QUrl AddonTransport::baseRoot(const QUrl& base)
src\core\stream\addon\AddonTransport.cpp:150:QString AddonTransport::encodeExtraSegment(const QList<QPair<QString, QString>>& extra)
src\core\stream\addon\AddonTransport.cpp:161:QUrl AddonTransport::buildResourceUrl(const QUrl& base, const ResourceRequest& request)
src\core\stream\addon\AddonTransport.cpp:182:bool AddonTransport::parseManifest(const QJsonObject& obj, AddonManifest& out)
src\core\stream\BulkSourceCollector.cpp:94:            episode->timeout->deleteLater();
src\core\stream\BulkSourceCollector.cpp:99:            episode->aggregator->deleteLater();
src\core\stream\BulkSourceCollector.cpp:187:        episode->timeout->deleteLater();
src\core\stream\BulkSourceCollector.cpp:190:        episode->aggregator->deleteLater();
src\core\stream\addon\AddonTransport.h:14:class AddonTransport : public QObject
src\core\stream\addon\AddonTransport.h:27:    void resourceReady(const tankostream::addon::ResourceRequest& request,
src\core\stream\addon\AddonTransport.h:29:    void resourceFailed(const tankostream::addon::ResourceRequest& request,
src\core\stream\CatalogAggregator.cpp:192:        auto* worker = new AddonTransport(this);
src\core\stream\CatalogAggregator.cpp:195:        connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\CatalogAggregator.cpp:197:                worker->deleteLater();
src\core\stream\CatalogAggregator.cpp:201:        connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\CatalogAggregator.cpp:203:                worker->deleteLater();
src\ui\pages\stream\StreamSourceList.cpp:139:        card->deleteLater();
src\core\manga\MangaDownloader.cpp:352:    auto conn = std::make_shared<QMetaObject::Connection>();
src\core\manga\MangaDownloader.cpp:353:    auto errConn = std::make_shared<QMetaObject::Connection>();
src\core\manga\MangaDownloader.cpp:656:                reply->deleteLater();
src\ui\pages\stream\TheatreDownloadPanel.cpp:960:        if (auto* w = item->widget()) w->deleteLater();
src\core\stream\MetaAggregator.cpp:36:bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
src\core\stream\MetaAggregator.cpp:366:    auto* worker = new AddonTransport(this);
src\core\stream\MetaAggregator.cpp:368:    auto readyConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:369:    auto failConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:373:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:376:            if (*handled || !sameRequest(request, incoming)) {
src\core\stream\MetaAggregator.cpp:382:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:399:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:402:            if (*handled || !sameRequest(request, incoming)) {
src\core\stream\MetaAggregator.cpp:408:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:452:    auto* worker = new AddonTransport(this);
src\core\stream\MetaAggregator.cpp:454:    auto readyConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:455:    auto failConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:457:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:460:            if (*handled || !sameRequest(request, incoming)) {
src\core\stream\MetaAggregator.cpp:466:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:500:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:503:            if (*handled || !sameRequest(request, incoming)) {
src\core\stream\MetaAggregator.cpp:509:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:594:        m_fribbReply->deleteLater();
src\core\stream\MetaAggregator.cpp:692:    auto* worker = new AddonTransport(this);
src\core\stream\MetaAggregator.cpp:694:    auto readyConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:695:    auto failConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:698:        worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:701:            if (*handled || !sameRequest(request, incoming)) {
src\core\stream\MetaAggregator.cpp:707:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:724:        worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:727:            if (*handled || !sameRequest(request, incoming)) {
src\core\stream\MetaAggregator.cpp:733:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:800:    auto* worker    = new AddonTransport(this);
src\core\stream\MetaAggregator.cpp:802:    auto  readyConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:803:    auto  failConn  = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:805:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:808:            if (*handled || !sameRequest(request, incoming)) return;
src\core\stream\MetaAggregator.cpp:812:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:833:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:836:            if (*handled || !sameRequest(request, incoming)) return;
src\core\stream\MetaAggregator.cpp:840:            worker->deleteLater();
src\core\stream\MetaAggregator.cpp:935:        auto* worker    = new AddonTransport(this);
src\core\stream\MetaAggregator.cpp:937:        auto  readyConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:938:        auto  failConn  = std::make_shared<QMetaObject::Connection>();
src\core\stream\MetaAggregator.cpp:942:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:945:                if (*handled || !sameRequest(request, incoming)) return;
src\core\stream\MetaAggregator.cpp:949:                worker->deleteLater();
src\core\stream\MetaAggregator.cpp:966:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:969:                if (*handled || !sameRequest(request, incoming)) return;
src\core\stream\MetaAggregator.cpp:973:                worker->deleteLater();
src\ui\pages\stream\TorrentPackPicker.cpp:147:            indexer->deleteLater();
src\core\manga\mangafire\MangaFireCatalogClient.cpp:498:        reply->deleteLater();
src\core\manga\mangafire\MangaFireCatalogClient.cpp:533:        reply->deleteLater();
src\core\manga\mangafire\MangaFireCatalogClient.cpp:580:        reply->deleteLater();
src\core\manga\mangafire\MangaFireCatalogClient.cpp:625:        reply->deleteLater();
src\core\manga\mangafire\MangaFireCatalogClient.cpp:659:        reply->deleteLater();
src\core\manga\MangaPosterCache.cpp:78:            reply->deleteLater();
src\ui\player\VideoPlayer.cpp:4058:    menu->deleteLater();
src\core\stream\StreamAggregator.cpp:99:bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
src\core\stream\StreamAggregator.cpp:579:        auto* worker = new AddonTransport(this);
src\core\stream\StreamAggregator.cpp:582:        auto readyConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\StreamAggregator.cpp:583:        auto failConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\StreamAggregator.cpp:593:            worker->deleteLater();
src\core\stream\StreamAggregator.cpp:597:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\StreamAggregator.cpp:603:                if (!sameRequest(req, incoming)) return;
src\core\stream\StreamAggregator.cpp:607:                worker->deleteLater();
src\core\stream\StreamAggregator.cpp:611:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\StreamAggregator.cpp:617:                if (!sameRequest(req, incoming)) return;
src\core\stream\StreamAggregator.cpp:621:                worker->deleteLater();
src\core\stream\StreamAggregator.cpp:792:            indexer->deleteLater();
src\core\stream\StreamAggregator.cpp:800:                        if (idxPtr) idxPtr->deleteLater();
src\core\stream\StreamAggregator.cpp:812:                    if (idxPtr) idxPtr->deleteLater();
src\core\stream\StreamAggregator.cpp:820:                        if (idxPtr) idxPtr->deleteLater();
src\core\stream\StreamAggregator.cpp:823:                    if (idxPtr) idxPtr->deleteLater();
src\core\stream\StreamAggregator.cpp:894:            ctx->timeout->deleteLater();
src\core\stream\StreamAggregator.cpp:902:        ctx->timeout->deleteLater();
src\core\manga\NyaaRuntimeSource.cpp:182:    reply->deleteLater();
src\core\manga\mangaupdates\MangaUpdatesClient.cpp:167:    reply->deleteLater();
src\core\manga\mangaupdates\MangaUpdatesClient.cpp:207:    reply->deleteLater();
src\core\stream\SubtitlesAggregator.cpp:27:bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
src\core\stream\SubtitlesAggregator.cpp:208:        auto* worker = new AddonTransport(this);
src\core\stream\SubtitlesAggregator.cpp:211:        auto readyConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\SubtitlesAggregator.cpp:212:        auto failConn = std::make_shared<QMetaObject::Connection>();
src\core\stream\SubtitlesAggregator.cpp:214:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\SubtitlesAggregator.cpp:218:                if (*handled || !sameRequest(req, incoming)) {
src\core\stream\SubtitlesAggregator.cpp:224:                worker->deleteLater();
src\core\stream\SubtitlesAggregator.cpp:228:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\SubtitlesAggregator.cpp:232:                if (*handled || !sameRequest(req, incoming)) {
src\core\stream\SubtitlesAggregator.cpp:238:                worker->deleteLater();
src\core\manga\ReadComicsScraper.cpp:69:        reply->deleteLater();
src\core\manga\ReadComicsScraper.cpp:127:        reply->deleteLater();
src\core\manga\ReadComicsScraper.cpp:207:        reply->deleteLater();
src\core\manga\ReadComicsScraper.cpp:245:        reply->deleteLater();
src\core\manga\WeebCentralScraper.cpp:47:        reply->deleteLater();
src\core\manga\WeebCentralScraper.cpp:205:        reply->deleteLater();
src\core\manga\WeebCentralScraper.cpp:329:        reply->deleteLater();
src\core\manga\WeebCentralScraper.cpp:380:        reply->deleteLater();
src\core\manga\WeebCentralScraper.cpp:433:        reply->deleteLater();
src\core\manga\WeebCentralVolumePacker.cpp:206:                        reply->deleteLater();
src\core\manga\WesternVolumeDownloader.cpp:255:                    dl->deleteLater();
src\core\manga\WesternVolumeDownloader.cpp:271:                    dl->deleteLater();
src\core\manga\wikipedia\WikipediaResolver.cpp:91:        reply->deleteLater();
src\core\manga\wikidata\WikidataClient.cpp:77:        reply->deleteLater();

codex
The important shape is visible: `load()` now returns a generation and `dispatchRequests()` captures it. I’m checking the nearby code for edge cases the diff alone can hide, like stale workers whose signal request shape does not match.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/StreamAggregator.cpp | Select-Object -Skip 480 -First 230' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 2290 -First 150' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonTransport.cpp | Select-Object -Skip 70 -First 50' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 3240 -First 130' in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1327ms:

        if (!parseManifest(doc.object(), descriptor.manifest)) {
            emit manifestFailed(QStringLiteral("Manifest validation failed (missing id/name/version)"));
            return;
        }

        emit manifestReady(descriptor);
    });
}

void AddonTransport::fetchResource(const QUrl& base, const ResourceRequest& request)
{
    const QUrl url = buildResourceUrl(base, request);
    if (!url.isValid()) {
        emit resourceFailed(request, QStringLiteral("Invalid resource URL"));
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(kTimeoutMs);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit resourceFailed(request, reply->errorString());
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit resourceFailed(request, QStringLiteral("Resource JSON parse error: ") +
                                parseErr.errorString());
            return;
        }

        emit resourceReady(request, doc.object());
    });
}

QUrl AddonTransport::normalizeManifestUrl(QUrl base)
{
    if (!base.isValid() || base.scheme().isEmpty()) {
        return {};
    }

    QString path = base.path();

 succeeded in 1463ms:
    }
    out.source = source;
    out.name = streamObj.value(QStringLiteral("name")).toString().trimmed();
    out.description = streamObj.value(QStringLiteral("description")).toString().trimmed();
    if (out.description.isEmpty()) {
        out.description = streamObj.value(QStringLiteral("title")).toString().trimmed();
    }
    out.thumbnail = QUrl(streamObj.value(QStringLiteral("thumbnail")).toString().trimmed());
    out.subtitles = parseSubtitles(streamObj.value(QStringLiteral("subtitles")).toArray());
    parseBehaviorHints(streamObj.value(QStringLiteral("behaviorHints")).toObject(),
                       out.behaviorHints);

    if (out.name.isEmpty()) {
        out.name = out.description;
    }
    return true;
}

}

StreamAggregator::StreamAggregator(AddonRegistry* registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
}

quint64 StreamAggregator::load(const StreamLoadRequest& request)
{
    reset();
    // DOWNLOAD BUG 2026-06-02 â€” stamp the new generation immediately after the
    // reset() that zeroed mid-flight state. The returned token lets the caller
    // gate its one-shot streamsReady handler against currentLoadToken() so a
    // late emit from a SUPERSEDED load() (rapid re-clicks) is discarded.
    ++m_loadGeneration;
    m_request = request;

    if (!m_registry || request.type.isEmpty() || request.id.isEmpty()) {
        emitEmptyDeferred(m_loadGeneration);  // review fix â€” see emitEmptyDeferred()
        return m_loadGeneration;
    }

    const QList<AddonDescriptor> addons =
        m_registry->findByResourceType(QStringLiteral("stream"), request.type);

    if (addons.isEmpty()) {
        emitEmptyDeferred(m_loadGeneration);  // review fix â€” see emitEmptyDeferred()
        return m_loadGeneration;
    }

    for (const AddonDescriptor& addon : addons) {
        PendingAddon pending;
        pending.addonId = addon.manifest.id;
        pending.addonName = addon.manifest.name;
        pending.transportUrl = addon.transportUrl;
        m_pendingByAddon.insert(pending.addonId, pending);
        m_addonsById.insert(pending.addonId, pending.addonName);
    }

    dispatchRequests();
    return m_loadGeneration;
}

void StreamAggregator::emitEmptyDeferred(quint64 generation)
{
    // See the header for why this is queued rather than emitted synchronously.
    QMetaObject::invokeMethod(this, [this, generation]() {
        if (generation != m_loadGeneration)
            return;  // superseded by a newer load() â€” drop the stale empty emit
        emit streamsReady({}, {});
    }, Qt::QueuedConnection);
}

void StreamAggregator::dispatchRequests()
{
    // DOWNLOAD BUG 2026-06-03 (review fix) â€” capture the generation this dispatch
    // belongs to. A reply from a worker launched by a SUPERSEDED load() (rapid
    // re-clicks) must be dropped at the source: without this, a stale reply with
    // the same request shape lands in the CURRENT generation's m_streams and
    // decrements m_pendingResponses, corrupting accumulation and firing an early
    // streamsReady that the handler-side token gate would then wrongly accept
    // (the current generation IS still active). Correlating here closes the gap
    // that gating only at the handler left open. Mirrors searchPacks()'s epoch
    // suppression already in this file.
    const quint64 gen = m_loadGeneration;
    for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
        PendingAddon& addon = it.value();
        if (addon.inFlight) {
            continue;
        }
        addon.inFlight = true;
        ++m_pendingResponses;

        ResourceRequest req;
        req.resource = QStringLiteral("stream");
        req.type = m_request.type;
        req.id = m_request.id;
        req.extra = m_request.extra;

        auto* worker = new AddonTransport(this);
        const QString addonId = addon.addonId;
        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        // Drop a stale-generation reply (and reap its worker) before it can touch
        // current state; otherwise apply the existing same-request guard.
        auto dropIfStale = [this, gen, handled, readyConn, failConn, worker]() -> bool {
            if (gen == m_loadGeneration)
                return false;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            return true;
        };

        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
            [this, req, addonId, handled, readyConn, failConn, worker, dropIfStale](
                const ResourceRequest& incoming,
                const QJsonObject& payload) {
                if (*handled) return;
                if (dropIfStale()) return;
                if (!sameRequest(req, incoming)) return;
                *handled = true;
                QObject::disconnect(*readyConn);
                QObject::disconnect(*failConn);
                worker->deleteLater();
                onAddonReady(addonId, payload);
            });

        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
            [this, req, addonId, handled, readyConn, failConn, worker, dropIfStale](
                const ResourceRequest& incoming,
                const QString& message) {
                if (*handled) return;
                if (dropIfStale()) return;
                if (!sameRequest(req, incoming)) return;
                *handled = true;
                QObject::disconnect(*readyConn);
                QObject::disconnect(*failConn);
                worker->deleteLater();
                onAddonFailed(addonId, message);
            });

        worker->fetchResource(addon.transportUrl, req);
    }
}

void StreamAggregator::onAddonReady(const QString& addonId, const QJsonObject& payload)
{
    auto addonIt = m_pendingByAddon.find(addonId);
    if (addonIt == m_pendingByAddon.end()) {
        completeOne();
        return;
    }
    addonIt->inFlight = false;

    const QString addonName = addonIt->addonName;
    const QJsonArray streamsArray = payload.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue& streamValue : streamsArray) {
        const QJsonObject streamObj = streamValue.toObject();
        Stream parsed;
        if (!parseStreamRow(streamObj, parsed)) {
            continue;
        }

        enrichTorrentioLikeFields(parsed, streamObj);
        parsed.behaviorHints.other.insert(QStringLiteral("originAddonId"), addonId);
        parsed.behaviorHints.other.insert(QStringLiteral("originAddonName"), addonName);

        const QString identity = streamIdentityKey(parsed);
        if (identity.isEmpty()) {
            continue;
        }
        if (m_seenIdentityKeys.contains(identity)) {
            continue;
        }
        m_seenIdentityKeys.insert(identity);
        m_streams.append(parsed);
    }

    completeOne();
}

void StreamAggregator::onAddonFailed(const QString& addonId, const QString& message)
{
    auto addonIt = m_pendingByAddon.find(addonId);
    if (addonIt != m_pendingByAddon.end()) {
        addonIt->inFlight = false;
    }
    emit streamError(addonId, message);
    completeOne();
}

void StreamAggregator::completeOne()
{
    // DOWNLOAD BUG 2026-06-02 (defense-in-depth) â€” a stale late completion from
    // a superseded load() (reset() zeroed m_pendingResponses mid-flight) could
    // otherwise drive the counter negative and re-emit streamsReady against the
    // wrong request. Bail before decrementing if there is nothing outstanding.
    if (m_pendingResponses <= 0) {
        m_pendingResponses = 0;
        return;
    }
    --m_pendingResponses;
    if (m_pendingResponses > 0) {
        return;
    }
    emit streamsReady(m_streams, m_addonsById);
}

void StreamAggregator::reset()
{
    m_request = {};
    m_pendingByAddon.clear();
    m_addonsById.clear();
    m_seenIdentityKeys.clear();
    m_streams.clear();
    m_pendingResponses = 0;
}

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - per-call pack-search
// state. Owned by std::shared_ptr so multiple concurrent searches don't
// collide and so a still-pending search can outlive the searchPacks()
// return. Post-review (C1) fix: shared_ptr ownership replaces raw new/delete
// so slow indexer callbacks arriving after the 30s timeout no longer
// UB-read freed memory; they keep ctx alive via captured shared_ptr and
// hit the ctx->emitted early-out instead.
struct StreamAggregator::PackSearchContext
{

 succeeded in 1484ms:
    // streamsReady lambda because we need the incoming stream list to find
    // the matching card.
    QString seriesBingeGroup;
    qint64  seriesUpdatedAt = 0;
    if (savedChoiceKey.isEmpty()
        && mediaType == QLatin1String("series")) {
        const QJsonObject seriesChoice = StreamChoices::loadSeriesChoice(imdbId);
        seriesBingeGroup = seriesChoice.value("bingeGroup").toString();
        seriesUpdatedAt  = seriesChoice.value("updatedAt").toInteger(0);
    }

    // Phase 2 Batch 2.4 â€” auto-launch eligibility: either the per-episode
    // saved choice OR the per-series saved choice must be within 10 minutes
    // of the last-watched stamp. The streamsReady lambda uses this to decide
    // whether to fire the resume toast + arm m_autoLaunchTimer.
    constexpr qint64 kAutoLaunchWindowMs = 10LL * 60LL * 1000LL;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 episodeUpdatedAt = savedChoice.value("updatedAt").toInteger(0);
    const bool   perEpisodeRecent  = episodeUpdatedAt > 0
                                       && (nowMs - episodeUpdatedAt) < kAutoLaunchWindowMs;
    const bool   perSeriesRecent   = seriesUpdatedAt > 0
                                       && (nowMs - seriesUpdatedAt) < kAutoLaunchWindowMs;
    const bool   autoLaunchEligible = perEpisodeRecent || perSeriesRecent;

    // Reset any in-flight auto-launch before we kick off the new resolve â€”
    // a second onPlayRequested (user clicks another episode mid-countdown)
    // must replace, not stack.
    cancelAutoLaunch();

    // Fetch streams via StreamAggregator, then push into the right pane of
    // StreamDetailView. No modal â€” the cards live inside the detail view
    // and the user clicks one to play (handled by onSourceActivated).
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
               this, nullptr);

    // DOWNLOAD BUG 2026-06-02 — same correlation token as the auto-download
    // path. The play one-shot is armed BEFORE load() runs, so the token is
    // held in a shared_ptr captured by value and filled from the load() return
    // below. The lambda discards any emit whose token != the current load
    // generation (a late streamsReady from a superseded play/source-load).
    auto playToken = std::make_shared<quint64>(0);

    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
        [this, savedChoiceKey, seriesBingeGroup, autoLaunchEligible, playToken](
            const QList<tankostream::addon::Stream>& streams,
            const QHash<QString, QString>& addonsById) {
            if (m_streamAggregator->currentLoadToken() != *playToken)
                return;
            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);

            const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);

            // Resolve the highlight key + retain a pointer to the matched
            // choice for Batch 2.4's auto-launch path. Per-episode wins;
            // fall through to series-level bingeGroup match.
            QString highlightKey = savedChoiceKey;
            const tankostream::stream::StreamPickerChoice* matchedChoice = nullptr;
            if (!highlightKey.isEmpty()) {
                for (const auto& c : choices) {
                    if (tankostream::stream::pickerChoiceKey(c) == highlightKey) {
                        matchedChoice = &c;
                        break;
                    }
                }
            }
            if (!matchedChoice && !seriesBingeGroup.isEmpty()) {
                for (const auto& c : choices) {
                    if (c.stream.behaviorHints.bingeGroup == seriesBingeGroup) {
                        highlightKey   = tankostream::stream::pickerChoiceKey(c);
                        matchedChoice  = &c;
                        break;
                    }
                }
            }

            if (m_detailView) {
                m_detailView->setStreamSources(choices, highlightKey);
            }

            // Phase 2 Batch 2.4 â€” auto-launch DISABLED 2026-04-16 per
            // Hemanth UX call (Phase 1 telemetry session, post One Piece
            // pack regression). The 2-second countdown was too aggressive â€”
            // entering Sources view would fire playback before the user
            // could meaningfully pick a different source. Manual source
            // selection (user clicks a card) still works via the existing
            // setStreamSources path above. The 10-minute eligibility gate +
            // m_autoLaunchTimer infrastructure are preserved in case future
            // UX iteration wants a longer countdown variant; one-line
            // re-enable point is the `if (false &&` guard below â€” flip to
            // restore (with a kAutoLaunchCountdownMs bump in the timer
            // setInterval at the top of buildUI before re-enabling).
            //
            // Suppressed variables keep clean shape for the re-enable diff:
            (void)matchedChoice;
            (void)autoLaunchEligible;
            if (false && matchedChoice && autoLaunchEligible && m_detailView) {
                m_autoLaunchChoice = *matchedChoice;
                m_detailView->showAutoLaunchToast(
                    tr("Resuming with last-used source..."));
                if (m_autoLaunchTimer) m_autoLaunchTimer->start();
            }
        });

    tankostream::stream::StreamLoadRequest req;
    req.type = (mediaType == "movie") ? QStringLiteral("movie") : QStringLiteral("series");
    // THEATRE_ANIME_CATALOG — anime series resolve to a Kitsu id; Torrentio
    // serves their streams via "kitsu:<id>:<absoluteEpisode>" (no season).
    // Falls back to the standard imdb:season:episode id for everything else.
    const int kitsuId = (mediaType != QLatin1String("movie") && m_metaAggregator)
                            ? m_metaAggregator->kitsuIdForSeries(imdbId)
                            : -1;
    if (mediaType == "movie") {
        req.id = imdbId;
    } else if (kitsuId > 0) {
        req.id = QStringLiteral("kitsu:%1:%2").arg(kitsuId).arg(qMax(1, episode));
    } else {
        req.id = imdbId + QLatin1Char(':') + QString::number(qMax(1, season))
                        + QLatin1Char(':') + QString::number(qMax(1, episode));
    }
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError,
               this, nullptr);
    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError, this,
        [this](const QString& addonId, const QString& message) {
            const QString shown = addonId.isEmpty()
                ? message
                : QStringLiteral("[%1] %2").arg(addonId, message);
            if (m_detailView) {
                m_detailView->setStreamSourcesError(
                    QStringLiteral("Failed to fetch sources: ") + shown);
            }
        });

    // DOWNLOAD BUG 2026-06-02 — fill the correlation token captured by the
    // one-shot above so it can discard a stale emit from a superseded load().
    *playToken = m_streamAggregator->load(req);
}

// Phase 2 Batch 2.4 â€” auto-launch orchestration.

void StreamPage::onAutoLaunchFire()
{
    if (m_detailView) m_detailView->hideAutoLaunchToast();
    if (!m_autoLaunchChoice.has_value()) return;
    const auto choice = *m_autoLaunchChoice;
    m_autoLaunchChoice.reset();
    // Same entry point user-click takes â€” keeps persistence + handoff
    // behavior identical between manual and auto-launch flows.
    onSourceActivated(choice);
}

 succeeded in 1424ms:

    AddTorrentConfig config;
    config.category        = QStringLiteral("videos");
    config.destinationPath = m_torrentClient->defaultPaths().value("videos");
    config.contentLayout   = QStringLiteral("original");
    config.streamGroupId   = QString();
    config.sequential      = false;
    config.startPaused     = false;
    config.imdbId          = imdbId;
    config.season          = season;
    // F9 fix 2026-05-19: pass magnet URI so startDownload can self-defend.
    config.magnetUri       = choice.magnetUri;

    m_torrentClient->startDownload(hash, config);
}

// THEATRE_DOWNLOAD_SIMPLIFY P1.T2 (2026-05-29) — silent auto-download entry.
// Mirrors onPlayRequested's one-shot streamsReady idiom (StreamPage.cpp ~2308-
// 2400): disconnect any prior handler, connect a fresh self-disconnecting
// lambda, build the Torrentio request id, then load(). The handler runs the
// auto-pick + startDownload in finishAutoDownloadPick().
void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaType,
                                   int season, int episode)
{
    if (!m_streamAggregator || !m_torrentClient || imdbId.isEmpty())
        return;

    // DOWNLOAD BUG 2026-06-02 — in-flight dedup. Rapid identical Download
    // clicks (the logs show 2-6 startAutoDownload within seconds) used to
    // re-arm the shared streamsReady one-shot and call load() again, which
    // reset() mid-flight and let a late stale emit deliver the wrong show's
    // streams. If the SAME request is already in flight, ignore the re-click.
    if (m_pendingAuto.active
        && m_pendingAuto.imdbId == imdbId
        && m_pendingAuto.season == season
        && m_pendingAuto.episode == episode) {
        qInfo().noquote() << "[auto-dl] dedup: ignoring re-click for in-flight imdb="
                          << imdbId << "s" << season << "e" << episode;
        return;
    }

    m_pendingAuto = PendingAutoDownload{};
    m_pendingAuto.active         = true;
    m_pendingAuto.imdbId         = imdbId;
    m_pendingAuto.mediaType      = mediaType;
    m_pendingAuto.season         = season;
    m_pendingAuto.episode        = episode;
    m_pendingAuto.runtimeMinutes = 0;  // unknown -> AutoSourcePicker skips size guardrail
    // Capture the show title now (the detail view shows the requested show at
    // click time) so the picker's show-identity gate survives any later
    // navigation while the async source fetch is in flight.
    m_pendingAuto.showTitle      = m_detailView ? m_detailView->currentTitle() : QString();
    qInfo().noquote() << "[auto-dl] startAutoDownload imdb=" << imdbId
                      << "type=" << mediaType << "s" << season << "e" << episode;

    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
               this, nullptr);
    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
        [this](const QList<tankostream::addon::Stream>& streams,
               const QHash<QString, QString>& addonsById) {
            // DOWNLOAD BUG 2026-06-02 — ignore a stale emit from a superseded
            // load(). Without this, a late streamsReady from an EARLIER request
            // fires this one-shot carrying the WRONG show's streams; the picker
            // show-gate then rejects them all -> "No 1080p source found".
            if (m_streamAggregator->currentLoadToken() != m_pendingAuto.token)
                return;
            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);
            finishAutoDownloadPick(streams, addonsById);
        });

    tankostream::stream::StreamLoadRequest req;
    req.type = (mediaType == QLatin1String("movie")) ? QStringLiteral("movie") : QStringLiteral("series");
    const int kitsuId = (mediaType != QLatin1String("movie") && m_metaAggregator)
                            ? m_metaAggregator->kitsuIdForSeries(imdbId)
                            : -1;
    if (mediaType == QLatin1String("movie")) {
        req.id = imdbId;
    } else if (kitsuId > 0) {
        req.id = QStringLiteral("kitsu:%1:%2").arg(kitsuId).arg(qMax(1, episode));
    } else {
        req.id = imdbId + QLatin1Char(':') + QString::number(qMax(1, season))
                        + QLatin1Char(':') + QString::number(qMax(1, episode));
    }
    qInfo().noquote() << "[auto-dl] req.id=" << req.id
                      << "showTitle=" << m_pendingAuto.showTitle;
    // Surface a fetch error on the auto-download path (mirrors onPlayRequested's
    // streamError wiring ~line 2388). Clears the pending state so the tile/UI
    // doesn't hang waiting on a result that will never arrive.
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError,
               this, nullptr);
    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError, this,
        [this](const QString& addonId, const QString& message) {
            m_pendingAuto.active = false;
            const QString shown = addonId.isEmpty()
                ? message : QStringLiteral("[%1] %2").arg(addonId, message);
            if (m_detailView)
                m_detailView->setStreamSourcesError(
                    QStringLiteral("Failed to fetch sources: ") + shown);
        });

    // DOWNLOAD BUG 2026-06-02 — capture the generation token load() stamped so
    // the one-shot above can discard a late emit from a superseded load().
    // Set AFTER m_pendingAuto fields are populated.
    m_pendingAuto.token = m_streamAggregator->load(req);
}

// One-shot streamsReady handler for an active auto-download. Converts the
// Torrentio results into AutoSourcePicker candidates, picks the best 1080p,
// resolves the infoHash, and starts the download stamped theatre:<imdbId>.
void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>& streams,
                                        const QHash<QString, QString>& addonsById)
{
    if (!m_pendingAuto.active) return;
    const PendingAutoDownload ctx = m_pendingAuto;
    m_pendingAuto.active = false;  // consume

    const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);
    qInfo().noquote() << "[auto-dl] finishPick choices=" << choices.size()
                      << "active=" << (m_pendingAuto.active ? "y" : "n");

    QList<tankostream::stream::SourceCandidate> cands;
    cands.reserve(choices.size());
    for (const auto& c : choices) {
        tankostream::stream::SourceCandidate sc;
        sc.title       = c.displayTitle;
        sc.seeders     = c.seeders;
        sc.sizeBytes   = c.sizeBytes;
        sc.qualitySort = c.qualitySort;
        cands.append(sc);

codex
One extra race is worth checking beyond the listed early returns: whether any worker path can still emit synchronously during `load()` before the returned token is stored. That determines whether the fix covers all synchronous `streamsReady` paths or only the two explicit empty returns.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonTransport.cpp | Select-Object -Skip 130 -First 55' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"transportUrl|AddonDescriptor|findByResourceType|register|descriptor\\.transportUrl\" src/core/stream/addon src/core/stream -S" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonTransport.h | Select-Object -First 80' in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 821ms:
src/core/stream/addon\Descriptor.h:9:struct AddonDescriptorFlags {
src/core/stream/addon\Descriptor.h:15:struct AddonDescriptor {
src/core/stream/addon\Descriptor.h:17:    QUrl transportUrl;
src/core/stream/addon\Descriptor.h:18:    AddonDescriptorFlags flags;
src/core/stream/addon\AddonTransport.h:25:    void manifestReady(const tankostream::addon::AddonDescriptor& descriptor);
src/core/stream/addon\AddonTransport.cpp:69:        AddonDescriptor descriptor;
src/core/stream/addon\AddonTransport.cpp:70:        descriptor.transportUrl = manifestUrl;
src/core/stream/addon\AddonRegistry.h:23:    QList<AddonDescriptor> list() const;
src/core/stream/addon\AddonRegistry.h:24:    QList<AddonDescriptor> findByResourceType(const QString& resource,
src/core/stream/addon\AddonRegistry.h:27:    void installByUrl(const QUrl& transportUrlInput);
src/core/stream/addon\AddonRegistry.h:33:    void installSucceeded(const tankostream::addon::AddonDescriptor& descriptor);
src/core/stream/addon\AddonRegistry.h:37:    void onManifestReady(const tankostream::addon::AddonDescriptor& fetched);
src/core/stream/addon\AddonRegistry.h:46:    static bool validateFetchedDescriptor(const AddonDescriptor& descriptor);
src/core/stream/addon\AddonRegistry.h:58:    QList<AddonDescriptor> m_addons;
src/core/stream/addon\AddonRegistry.cpp:26:// `subtitles` resource, so SubtitlesAggregator::findByResourceType always
src/core/stream/addon\AddonRegistry.cpp:249:QJsonObject descriptorToJson(const AddonDescriptor& descriptor)
src/core/stream/addon\AddonRegistry.cpp:252:    root[QStringLiteral("transportUrl")] = descriptor.transportUrl.toString();
src/core/stream/addon\AddonRegistry.cpp:264:AddonDescriptor descriptorFromJson(const QJsonObject& obj)
src/core/stream/addon\AddonRegistry.cpp:266:    AddonDescriptor descriptor;
src/core/stream/addon\AddonRegistry.cpp:267:    descriptor.transportUrl = QUrl(obj.value(QStringLiteral("transportUrl")).toString().trimmed());
src/core/stream/addon\AddonRegistry.cpp:300:QList<AddonDescriptor> AddonRegistry::list() const
src/core/stream/addon\AddonRegistry.cpp:305:QList<AddonDescriptor> AddonRegistry::findByResourceType(const QString& resource,
src/core/stream/addon\AddonRegistry.cpp:308:    QList<AddonDescriptor> out;
src/core/stream/addon\AddonRegistry.cpp:309:    for (const AddonDescriptor& addon : m_addons) {
src/core/stream/addon\AddonRegistry.cpp:320:void AddonRegistry::installByUrl(const QUrl& transportUrlInput)
src/core/stream/addon\AddonRegistry.cpp:323:        emit installFailed(transportUrlInput,
src/core/stream/addon\AddonRegistry.cpp:328:    const QUrl transportUrl = normalizeManifestUrl(transportUrlInput);
src/core/stream/addon\AddonRegistry.cpp:329:    if (!transportUrl.isValid() || transportUrl.scheme().isEmpty()) {
src/core/stream/addon\AddonRegistry.cpp:330:        emit installFailed(transportUrlInput, QStringLiteral("Invalid addon URL"));
src/core/stream/addon\AddonRegistry.cpp:334:    for (const AddonDescriptor& existing : m_addons) {
src/core/stream/addon\AddonRegistry.cpp:335:        if (sameUrl(existing.transportUrl, transportUrl)) {
src/core/stream/addon\AddonRegistry.cpp:340:            emit installFailed(transportUrl, QStringLiteral("Addon already installed"));
src/core/stream/addon\AddonRegistry.cpp:345:    m_pendingInstallUrl = transportUrl;
src/core/stream/addon\AddonRegistry.cpp:346:    m_transport->fetchManifest(transportUrl);
src/core/stream/addon\AddonRegistry.cpp:381:void AddonRegistry::onManifestReady(const AddonDescriptor& fetched)
src/core/stream/addon\AddonRegistry.cpp:395:    AddonDescriptor normalized = fetched;
src/core/stream/addon\AddonRegistry.cpp:396:    normalized.transportUrl = normalizeManifestUrl(installUrl);
src/core/stream/addon\AddonRegistry.cpp:405:        if (!sameHost(m_addons[byId].transportUrl, normalized.transportUrl)) {
src/core/stream/addon\AddonRegistry.cpp:411:        const AddonDescriptorFlags preserve = m_addons[byId].flags;
src/core/stream/addon\AddonRegistry.cpp:422:        const AddonDescriptorFlags preserve = m_addons[byId].flags;
src/core/stream/addon\AddonRegistry.cpp:502:bool AddonRegistry::validateFetchedDescriptor(const AddonDescriptor& descriptor)
src/core/stream/addon\AddonRegistry.cpp:504:    if (!descriptor.transportUrl.isValid()) {
src/core/stream/addon\AddonRegistry.cpp:557:        AddonDescriptor descriptor = descriptorFromJson(value.toObject());
src/core/stream/addon\AddonRegistry.cpp:572:        QList<AddonDescriptor> userInstalled;
src/core/stream/addon\AddonRegistry.cpp:573:        for (const AddonDescriptor& a : m_addons) {
src/core/stream/addon\AddonRegistry.cpp:579:        for (const AddonDescriptor& a : userInstalled) {
src/core/stream/addon\AddonRegistry.cpp:601:    for (const AddonDescriptor& descriptor : m_addons) {
src/core/stream/addon\AddonRegistry.cpp:618:    AddonDescriptor cinemeta;
src/core/stream/addon\AddonRegistry.cpp:619:    cinemeta.transportUrl = QUrl(QStringLiteral("https://v3-cinemeta.strem.io/manifest.json"));
src/core/stream/addon\AddonRegistry.cpp:687:    AddonDescriptor torrentio;
src/core/stream/addon\AddonRegistry.cpp:688:    torrentio.transportUrl = QUrl(QStringLiteral("https://torrentio.strem.fun/manifest.json"));
src/core/stream/addon\AddonRegistry.cpp:710:    AddonDescriptor torrentCatalogs;
src/core/stream/addon\AddonRegistry.cpp:711:    torrentCatalogs.transportUrl = QUrl(QStringLiteral("https://torrent-catalogs.strem.fun/manifest.json"));
src/core/stream/addon\AddonRegistry.cpp:767:    AddonDescriptor opensubs;
src/core/stream/addon\AddonRegistry.cpp:768:    opensubs.transportUrl = QUrl(QStringLiteral("https://opensubtitles-v3.strem.io/manifest.json"));
src/core/stream/addon\AddonRegistry.cpp:794:    AddonDescriptor animeKitsu;
src/core/stream/addon\AddonRegistry.cpp:795:    animeKitsu.transportUrl = QUrl(QStringLiteral("https://anime-kitsu.strem.fun/manifest.json"));
src/core/stream\CatalogAggregator.cpp:13:using tankostream::addon::AddonDescriptor;
src/core/stream\CatalogAggregator.cpp:142:    const QList<AddonDescriptor> addons = m_registry->list();
src/core/stream\CatalogAggregator.cpp:143:    for (const AddonDescriptor& addon : addons) {
src/core/stream\CatalogAggregator.cpp:160:            c.baseUrl = addon.transportUrl;
src/core/stream\BulkSourceCollector.cpp:131:            m_registry->findByResourceType(QStringLiteral("stream"), request.type).size();
src/core/stream\MetaAggregator.cpp:24:using tankostream::addon::AddonDescriptor;
src/core/stream\MetaAggregator.cpp:44:bool hasResourceType(const AddonDescriptor& addon,
src/core/stream\MetaAggregator.cpp:78:QString searchCatalogIdForType(const AddonDescriptor& addon, const QString& type)
src/core/stream\MetaAggregator.cpp:272:    const QList<AddonDescriptor> addons = m_registry->list();
src/core/stream\MetaAggregator.cpp:273:    for (const AddonDescriptor& addon : addons) {
src/core/stream\MetaAggregator.cpp:289:            req.baseUrl   = addon.transportUrl;
src/core/stream\MetaAggregator.cpp:326:    QList<AddonDescriptor> candidates;
src/core/stream\MetaAggregator.cpp:327:    for (const AddonDescriptor& addon : m_registry->list()) {
src/core/stream\MetaAggregator.cpp:339:        [](const AddonDescriptor& a, const AddonDescriptor& b) {
src/core/stream\MetaAggregator.cpp:351:    for (const AddonDescriptor& addon : candidates) {
src/core/stream\MetaAggregator.cpp:352:        dispatchSeriesMeta(addon.transportUrl, addon.manifest.id, imdbId);
src/core/stream\MetaAggregator.cpp:559:    for (const AddonDescriptor& a : m_registry->list()) {
src/core/stream\MetaAggregator.cpp:561:            return a.transportUrl;
src/core/stream\MetaAggregator.cpp:771:    QList<AddonDescriptor> candidates;
src/core/stream\MetaAggregator.cpp:772:    for (const AddonDescriptor& addon : m_registry->list()) {
src/core/stream\MetaAggregator.cpp:781:        [](const AddonDescriptor& a, const AddonDescriptor& b) {
src/core/stream\MetaAggregator.cpp:788:    dispatchMetaItemFetch(candidates.first().transportUrl, imdbId, type);
src/core/stream\MetaAggregator.cpp:912:    for (const AddonDescriptor& addon : m_registry->list()) {
src/core/stream\MetaAggregator.cpp:917:            queue.append({addon.manifest.id, addon.transportUrl, type, catalogId});
src/core/stream\StreamAggregator.h:93:        QUrl transportUrl;
src/core/stream\StreamAggregator.cpp:35:using tankostream::addon::AddonDescriptor;
src/core/stream\StreamAggregator.cpp:522:    const QList<AddonDescriptor> addons =
src/core/stream\StreamAggregator.cpp:523:        m_registry->findByResourceType(QStringLiteral("stream"), request.type);
src/core/stream\StreamAggregator.cpp:530:    for (const AddonDescriptor& addon : addons) {
src/core/stream\StreamAggregator.cpp:534:        pending.transportUrl = addon.transportUrl;
src/core/stream\StreamAggregator.cpp:625:        worker->fetchResource(addon.transportUrl, req);
src/core/stream\addon\AddonRegistry.h:23:    QList<AddonDescriptor> list() const;
src/core/stream\addon\AddonRegistry.h:24:    QList<AddonDescriptor> findByResourceType(const QString& resource,
src/core/stream\addon\AddonRegistry.h:27:    void installByUrl(const QUrl& transportUrlInput);
src/core/stream\addon\AddonRegistry.h:33:    void installSucceeded(const tankostream::addon::AddonDescriptor& descriptor);
src/core/stream\addon\AddonRegistry.h:37:    void onManifestReady(const tankostream::addon::AddonDescriptor& fetched);
src/core/stream\addon\AddonRegistry.h:46:    static bool validateFetchedDescriptor(const AddonDescriptor& descriptor);
src/core/stream\addon\AddonRegistry.h:58:    QList<AddonDescriptor> m_addons;
src/core/stream\addon\AddonRegistry.cpp:26:// `subtitles` resource, so SubtitlesAggregator::findByResourceType always
src/core/stream\addon\AddonRegistry.cpp:249:QJsonObject descriptorToJson(const AddonDescriptor& descriptor)
src/core/stream\addon\AddonRegistry.cpp:252:    root[QStringLiteral("transportUrl")] = descriptor.transportUrl.toString();
src/core/stream\addon\AddonRegistry.cpp:264:AddonDescriptor descriptorFromJson(const QJsonObject& obj)
src/core/stream\addon\AddonRegistry.cpp:266:    AddonDescriptor descriptor;
src/core/stream\addon\AddonRegistry.cpp:267:    descriptor.transportUrl = QUrl(obj.value(QStringLiteral("transportUrl")).toString().trimmed());
src/core/stream\addon\AddonRegistry.cpp:300:QList<AddonDescriptor> AddonRegistry::list() const
src/core/stream\addon\AddonRegistry.cpp:305:QList<AddonDescriptor> AddonRegistry::findByResourceType(const QString& resource,
src/core/stream\addon\AddonRegistry.cpp:308:    QList<AddonDescriptor> out;
src/core/stream\addon\AddonRegistry.cpp:309:    for (const AddonDescriptor& addon : m_addons) {
src/core/stream\addon\AddonRegistry.cpp:320:void AddonRegistry::installByUrl(const QUrl& transportUrlInput)
src/core/stream\addon\AddonRegistry.cpp:323:        emit installFailed(transportUrlInput,
src/core/stream\addon\AddonRegistry.cpp:328:    const QUrl transportUrl = normalizeManifestUrl(transportUrlInput);
src/core/stream\addon\AddonRegistry.cpp:329:    if (!transportUrl.isValid() || transportUrl.scheme().isEmpty()) {
src/core/stream\addon\AddonRegistry.cpp:330:        emit installFailed(transportUrlInput, QStringLiteral("Invalid addon URL"));
src/core/stream\addon\AddonRegistry.cpp:334:    for (const AddonDescriptor& existing : m_addons) {
src/core/stream\addon\AddonRegistry.cpp:335:        if (sameUrl(existing.transportUrl, transportUrl)) {
src/core/stream\addon\AddonRegistry.cpp:340:            emit installFailed(transportUrl, QStringLiteral("Addon already installed"));
src/core/stream\addon\AddonRegistry.cpp:345:    m_pendingInstallUrl = transportUrl;
src/core/stream\addon\AddonRegistry.cpp:346:    m_transport->fetchManifest(transportUrl);
src/core/stream\addon\AddonRegistry.cpp:381:void AddonRegistry::onManifestReady(const AddonDescriptor& fetched)
src/core/stream\addon\AddonRegistry.cpp:395:    AddonDescriptor normalized = fetched;
src/core/stream\addon\AddonRegistry.cpp:396:    normalized.transportUrl = normalizeManifestUrl(installUrl);
src/core/stream\addon\AddonRegistry.cpp:405:        if (!sameHost(m_addons[byId].transportUrl, normalized.transportUrl)) {
src/core/stream\addon\AddonRegistry.cpp:411:        const AddonDescriptorFlags preserve = m_addons[byId].flags;
src/core/stream\addon\AddonRegistry.cpp:422:        const AddonDescriptorFlags preserve = m_addons[byId].flags;
src/core/stream\addon\AddonRegistry.cpp:502:bool AddonRegistry::validateFetchedDescriptor(const AddonDescriptor& descriptor)
src/core/stream\addon\AddonRegistry.cpp:504:    if (!descriptor.transportUrl.isValid()) {
src/core/stream\addon\AddonRegistry.cpp:557:        AddonDescriptor descriptor = descriptorFromJson(value.toObject());
src/core/stream\addon\AddonRegistry.cpp:572:        QList<AddonDescriptor> userInstalled;
src/core/stream\addon\AddonRegistry.cpp:573:        for (const AddonDescriptor& a : m_addons) {
src/core/stream\addon\AddonRegistry.cpp:579:        for (const AddonDescriptor& a : userInstalled) {
src/core/stream\addon\AddonRegistry.cpp:601:    for (const AddonDescriptor& descriptor : m_addons) {
src/core/stream\addon\AddonRegistry.cpp:618:    AddonDescriptor cinemeta;
src/core/stream\addon\AddonRegistry.cpp:619:    cinemeta.transportUrl = QUrl(QStringLiteral("https://v3-cinemeta.strem.io/manifest.json"));
src/core/stream\addon\AddonRegistry.cpp:687:    AddonDescriptor torrentio;
src/core/stream\addon\AddonRegistry.cpp:688:    torrentio.transportUrl = QUrl(QStringLiteral("https://torrentio.strem.fun/manifest.json"));
src/core/stream\addon\AddonRegistry.cpp:710:    AddonDescriptor torrentCatalogs;
src/core/stream\addon\AddonRegistry.cpp:711:    torrentCatalogs.transportUrl = QUrl(QStringLiteral("https://torrent-catalogs.strem.fun/manifest.json"));
src/core/stream\addon\AddonRegistry.cpp:767:    AddonDescriptor opensubs;
src/core/stream\addon\AddonRegistry.cpp:768:    opensubs.transportUrl = QUrl(QStringLiteral("https://opensubtitles-v3.strem.io/manifest.json"));
src/core/stream\addon\AddonRegistry.cpp:794:    AddonDescriptor animeKitsu;
src/core/stream\addon\AddonRegistry.cpp:795:    animeKitsu.transportUrl = QUrl(QStringLiteral("https://anime-kitsu.strem.fun/manifest.json"));
src/core/stream\addon\AddonTransport.h:25:    void manifestReady(const tankostream::addon::AddonDescriptor& descriptor);
src/core/stream\addon\Descriptor.h:9:struct AddonDescriptorFlags {
src/core/stream\addon\Descriptor.h:15:struct AddonDescriptor {
src/core/stream\addon\Descriptor.h:17:    QUrl transportUrl;
src/core/stream\addon\Descriptor.h:18:    AddonDescriptorFlags flags;
src/core/stream\StreamDownloadIndex.cpp:149:void StreamDownloadIndex::registerEpisode(const QString& imdbId, int season, int episode,
src/core/stream\StreamDownloadIndex.cpp:174:        // keep the first registered path so equal-quality redownloads do not
src/core/stream\StreamDownloadIndex.cpp:188:                    // re-register re-publishes a COMPLETE download at the
src/core/stream\StreamDownloadIndex.cpp:217:        QStringLiteral("registerEpisode"),
src/core/stream\StreamDownloadIndex.cpp:225:        QStringLiteral("register_episode"),
src/core/stream\StreamDownloadIndex.cpp:238:        qWarning() << "[StreamDownloadIndex] registerEpisode dropped — repo not yet wired";
src/core/stream\StreamDownloadIndex.cpp:242:void StreamDownloadIndex::registerMovie(const QString& imdbId,
src/core/stream\StreamDownloadIndex.cpp:247:    registerEpisode(imdbId, 0, 0, canonicalPath, sourceGroupId, fileSizeBytes);
src/core/stream\StreamDownloadIndex.cpp:262:    // registerEpisode's own repo write already captured the row at season=0,
src/core/stream\StreamDownloadIndex.cpp:269:void StreamDownloadIndex::registerPendingEpisode(const QString& imdbId, int season,
src/core/stream\StreamDownloadIndex.cpp:286:        e.addedAt = QDateTime::currentMSecsSinceEpoch();  // Phase 3.4 — ms-since-epoch matches registerEpisode for repo round-trip
src/core/stream\StreamDownloadIndex.cpp:299:        qWarning() << "[StreamDownloadIndex] registerPendingEpisode dropped — repo not yet wired";
src/core/stream\StreamDownloadIndex.cpp:304:void StreamDownloadIndex::registerPendingMovie(const QString& imdbId,
src/core/stream\StreamDownloadIndex.cpp:333:        qWarning() << "[StreamDownloadIndex] registerPendingMovie dropped — repo not yet wired";
src/core/stream\StreamDownloadIndex.h:57:    // All mutating methods (registerEpisode/registerMovie/registerPendingEpisode/
src/core/stream\StreamDownloadIndex.h:58:    // registerPendingMovie/updateEpisodeProgress/evictByImdb/evictByPath/
src/core/stream\StreamDownloadIndex.h:71:    // home-open per spec §10.4). registerEpisode and the eviction methods
src/core/stream\StreamDownloadIndex.h:77:    void registerEpisode(const QString& imdbId, int season, int episode,
src/core/stream\StreamDownloadIndex.h:80:    void registerMovie(const QString& imdbId, const QString& canonicalPath,
src/core/stream\StreamDownloadIndex.h:83:    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — register as Pending. canonicalPath
src/core/stream\StreamDownloadIndex.h:86:    void registerPendingEpisode(const QString& imdbId, int season, int episode,
src/core/stream\StreamDownloadIndex.h:91:    // Movie variant — parallels registerMovie() but with state=Pending.
src/core/stream\StreamDownloadIndex.h:92:    void registerPendingMovie(const QString& imdbId,
src/core/stream\StreamDownloadIndex.h:128:    // a transient `.tankoban-partial/` Pending entry registered mid-download, plus
src/core/stream\addon\AddonTransport.cpp:69:        AddonDescriptor descriptor;
src/core/stream\addon\AddonTransport.cpp:70:        descriptor.transportUrl = manifestUrl;
src/core/stream\StreamRescueScanner.cpp:152:        // ── Step 2 — Cinemeta lookup + register per show ───────────────────
src/core/stream\StreamRescueScanner.cpp:238:            // ── Step 3 — register per-episode + StreamLibrary materialize ──
src/core/stream\StreamRescueScanner.cpp:239:            // Both StreamDownloadIndex::registerEpisode and StreamLibrary::add
src/core/stream\StreamRescueScanner.cpp:260:                        m_index->registerEpisode(chosenImdb, c.season, c.episode,
src/core/stream\StreamRescueScanner.cpp:301:            st[QStringLiteral("episodes_registered")] = stats.episodesRegistered;
src/core/stream\StreamRescueScanner.h:5:// queries Cinemeta to resolve each show, registers per-episode entries
src/core/stream\SubtitlesAggregator.cpp:16:using tankostream::addon::AddonDescriptor;
src/core/stream\SubtitlesAggregator.cpp:151:    const QList<AddonDescriptor> addons =
src/core/stream\SubtitlesAggregator.cpp:152:        m_registry->findByResourceType(QStringLiteral("subtitles"), m_request.type);
src/core/stream\SubtitlesAggregator.cpp:169:    for (const AddonDescriptor& addon : addons) {
src/core/stream\SubtitlesAggregator.cpp:173:        pending.baseUrl = addon.transportUrl;

 succeeded in 884ms:

QUrl AddonTransport::baseRoot(const QUrl& base)
{
    if (!base.isValid() || base.scheme().isEmpty()) {
        return {};
    }

    QUrl root = base;
    QString path = root.path();
    if (path.endsWith(QLatin1String("/manifest.json"), Qt::CaseInsensitive)) {
        path.chop(QString(QStringLiteral("/manifest.json")).size());
    }
    if (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    root.setPath(path);
    return root;
}

QString AddonTransport::encodeExtraSegment(const QList<QPair<QString, QString>>& extra)
{
    QStringList parts;
    parts.reserve(extra.size());
    for (const auto& kv : extra) {
        parts.push_back(encodeComponent(kv.first) + QLatin1Char('=') +
                        encodeComponent(kv.second));
    }
    return parts.join(QLatin1Char('&'));
}

QUrl AddonTransport::buildResourceUrl(const QUrl& base, const ResourceRequest& request)
{
    const QUrl root = baseRoot(base);
    if (!root.isValid()) {
        return {};
    }

    QString path = root.path();
    path += QLatin1Char('/') + encodeComponent(request.resource);
    path += QLatin1Char('/') + encodeComponent(request.type);
    path += QLatin1Char('/') + encodeComponent(request.id);
    if (!request.extra.isEmpty()) {
        path += QLatin1Char('/') + encodeExtraSegment(request.extra);
    }
    path += QStringLiteral(".json");

    QUrl url = root;
    url.setPath(path, QUrl::TolerantMode);
    return url;
}

bool AddonTransport::parseManifest(const QJsonObject& obj, AddonManifest& out)
{
    out.id = obj.value(QStringLiteral("id")).toString().trimmed();
    out.version = obj.value(QStringLiteral("version")).toString().trimmed();

 succeeded in 952ms:
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include "Descriptor.h"
#include "ResourcePath.h"

class QNetworkAccessManager;

namespace tankostream::addon {

class AddonTransport : public QObject
{
    Q_OBJECT

public:
    explicit AddonTransport(QObject* parent = nullptr);

    void fetchManifest(const QUrl& base);
    void fetchResource(const QUrl& base, const ResourceRequest& request);

signals:
    void manifestReady(const tankostream::addon::AddonDescriptor& descriptor);
    void manifestFailed(const QString& message);
    void resourceReady(const tankostream::addon::ResourceRequest& request,
                       const QJsonObject& payload);
    void resourceFailed(const tankostream::addon::ResourceRequest& request,
                        const QString& message);

private:
    static QUrl normalizeManifestUrl(QUrl base);
    static QUrl baseRoot(const QUrl& base);
    static QUrl buildResourceUrl(const QUrl& base, const ResourceRequest& request);
    static QString encodeExtraSegment(const QList<QPair<QString, QString>>& extra);
    static bool parseManifest(const QJsonObject& obj, AddonManifest& out);

    QNetworkAccessManager* m_nam = nullptr;
};

}

codex
There is a synchronous `resourceFailed` path inside `AddonTransport::fetchResource()` for invalid built URLs. I’m checking whether registry state can admit such descriptors, because that path would still emit through `completeOne()` inside `load()`.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonRegistry.cpp | Select-Object -Skip 490 -First 95' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonRegistry.cpp | Select-Object -Skip 240 -First 100' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonRegistry.cpp | Select-Object -Skip 585 -First 230' in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1494ms:
        if (item.name.compare(resource, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (item.hasTypes) {
            return item.types.contains(type, Qt::CaseInsensitive);
        }
        return manifest.types.contains(type, Qt::CaseInsensitive);
    }
    return false;
}

bool AddonRegistry::validateFetchedDescriptor(const AddonDescriptor& descriptor)
{
    if (!descriptor.transportUrl.isValid()) {
        return false;
    }
    const AddonManifest& m = descriptor.manifest;
    return !m.id.trimmed().isEmpty()
        && !m.name.trimmed().isEmpty()
        && !m.version.trimmed().isEmpty();
}

int AddonRegistry::indexOfId(const QString& addonId) const
{
    for (int i = 0; i < m_addons.size(); ++i) {
        if (m_addons[i].manifest.id == addonId) {
            return i;
        }
    }
    return -1;
}

QString AddonRegistry::storageFilePath() const
{
    QDir dir(m_dataDir);
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("stream_addons.json"));
}

void AddonRegistry::load()
{
    QFile file(storageFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        seedDefaults();
        save();
        return;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    file.close();

    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        seedDefaults();
        save();
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray addons = root.value(QStringLiteral("addons")).toArray();

    m_addons.clear();
    for (const QJsonValue& value : addons) {
        if (!value.isObject()) {
            continue;
        }
        AddonDescriptor descriptor = descriptorFromJson(value.toObject());
        if (descriptor.manifest.id.isEmpty()) {
            continue;
        }
        m_addons.push_back(descriptor);
    }

    // Schema migration. When kSchemaVersion bumps, reseed protected defaults
    // (Cinemeta, Torrentio) from the code-embedded seed while preserving any
    // user-installed non-protected addons. Without this, an on-disk file
    // written under an older seed shape sticks around forever â€” e.g. the v1
    // seed left Cinemeta's manifest.catalogs empty which broke manifest-driven
    // search even after the code was fixed.
    const int storedVersion = root.value(QStringLiteral("version")).toInt(0);
    if (storedVersion != kSchemaVersion) {
        QList<AddonDescriptor> userInstalled;
        for (const AddonDescriptor& a : m_addons) {
            if (!a.flags.protectedAddon) {
                userInstalled.push_back(a);
            }
        }
        seedDefaults();  // clears m_addons + pushes fresh protected defaults
        for (const AddonDescriptor& a : userInstalled) {
            if (indexOfId(a.manifest.id) >= 0) {
                continue;
            }
            m_addons.push_back(a);
        }
        save();

 succeeded in 1558ms:
        if (!kKnownHints.contains(it.key())) {
            out.behaviorHints.other.insert(it.key(), it.value().toVariant());
        }
    }

    return out;
}

QJsonObject descriptorToJson(const AddonDescriptor& descriptor)
{
    QJsonObject root;
    root[QStringLiteral("transportUrl")] = descriptor.transportUrl.toString();

    QJsonObject flags;
    flags[QStringLiteral("official")] = descriptor.flags.official;
    flags[QStringLiteral("enabled")] = descriptor.flags.enabled;
    flags[QStringLiteral("protected")] = descriptor.flags.protectedAddon;
    root[QStringLiteral("flags")] = flags;

    root[QStringLiteral("manifest")] = manifestToJson(descriptor.manifest);
    return root;
}

AddonDescriptor descriptorFromJson(const QJsonObject& obj)
{
    AddonDescriptor descriptor;
    descriptor.transportUrl = QUrl(obj.value(QStringLiteral("transportUrl")).toString().trimmed());

    const QJsonObject flags = obj.value(QStringLiteral("flags")).toObject();
    descriptor.flags.official = flags.value(QStringLiteral("official")).toBool(false);
    descriptor.flags.enabled = flags.value(QStringLiteral("enabled")).toBool(true);
    descriptor.flags.protectedAddon = flags.value(QStringLiteral("protected")).toBool(false);

    descriptor.manifest = manifestFromJson(obj.value(QStringLiteral("manifest")).toObject());
    return descriptor;
}

bool sameHost(const QUrl& a, const QUrl& b)
{
    return a.host().compare(b.host(), Qt::CaseInsensitive) == 0;
}

}

AddonRegistry::AddonRegistry(const QString& dataDir,
                             AddonTransport* transport,
                             QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_transport(transport ? transport : new AddonTransport(this))
{
    connect(m_transport, &AddonTransport::manifestReady,
            this, &AddonRegistry::onManifestReady);
    connect(m_transport, &AddonTransport::manifestFailed,
            this, &AddonRegistry::onManifestFailed);

    load();
}

QList<AddonDescriptor> AddonRegistry::list() const
{
    return m_addons;
}

QList<AddonDescriptor> AddonRegistry::findByResourceType(const QString& resource,
                                                         const QString& type) const
{
    QList<AddonDescriptor> out;
    for (const AddonDescriptor& addon : m_addons) {
        if (!addon.flags.enabled) {
            continue;
        }
        if (supportsResourceType(addon.manifest, resource, type)) {
            out.push_back(addon);
        }
    }
    return out;
}

void AddonRegistry::installByUrl(const QUrl& transportUrlInput)
{
    if (!m_pendingInstallUrl.isEmpty()) {
        emit installFailed(transportUrlInput,
                           QStringLiteral("Another addon install is already running"));
        return;
    }

    const QUrl transportUrl = normalizeManifestUrl(transportUrlInput);
    if (!transportUrl.isValid() || transportUrl.scheme().isEmpty()) {
        emit installFailed(transportUrlInput, QStringLiteral("Invalid addon URL"));
        return;
    }

    for (const AddonDescriptor& existing : m_addons) {
        if (sameUrl(existing.transportUrl, transportUrl)) {
            if (existing.flags.protectedAddon) {
                emit installSucceeded(existing);
                return;
            }
            emit installFailed(transportUrl, QStringLiteral("Addon already installed"));

 succeeded in 1518ms:
        return;
    }

    if (m_addons.isEmpty()) {
        seedDefaults();
        save();
    }
}

void AddonRegistry::save() const
{
    QJsonObject root;
    root[QStringLiteral("version")] = kSchemaVersion;

    QJsonArray addons;
    for (const AddonDescriptor& descriptor : m_addons) {
        addons.append(descriptorToJson(descriptor));
    }
    root[QStringLiteral("addons")] = addons;

    QSaveFile file(storageFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

void AddonRegistry::seedDefaults()
{
    m_addons.clear();

    AddonDescriptor cinemeta;
    cinemeta.transportUrl = QUrl(QStringLiteral("https://v3-cinemeta.strem.io/manifest.json"));
    cinemeta.flags.official = true;
    cinemeta.flags.enabled = true;
    cinemeta.flags.protectedAddon = true;
    cinemeta.manifest.id = QStringLiteral("com.linvo.cinemeta");
    cinemeta.manifest.version = QStringLiteral("3.0.14");
    cinemeta.manifest.name = QStringLiteral("Cinemeta");
    cinemeta.manifest.types = {QStringLiteral("movie"), QStringLiteral("series")};
    {
        ManifestResource catalog;
        catalog.name = QStringLiteral("catalog");
        ManifestResource meta;
        meta.name = QStringLiteral("meta");
        ManifestResource addonCatalog;
        addonCatalog.name = QStringLiteral("addon_catalog");
        cinemeta.manifest.resources = {catalog, meta, addonCatalog};
    }
    {
        // Cinemeta's upstream manifest declares top/movie + top/series catalogs
        // both supporting the `search` extra prop. Without these entries,
        // MetaAggregator::searchCatalog finds no catalog with matching type
        // and returns an empty queue â€” breaks Stream-mode search end-to-end.
        // Seed matches v3-cinemeta.strem.io/manifest.json shape so the manifest
        // is usable offline; a future refresh-on-startup pass will replace
        // this with a live fetch if one is ever implemented.
        ManifestExtraProp searchExtra;
        searchExtra.name = QStringLiteral("search");
        searchExtra.isRequired = false;
        searchExtra.optionsLimit = 1;

        ManifestExtraProp genreExtra;
        genreExtra.name = QStringLiteral("genre");
        genreExtra.isRequired = false;
        genreExtra.optionsLimit = 1;

        ManifestExtraProp skipExtra;
        skipExtra.name = QStringLiteral("skip");
        skipExtra.isRequired = false;
        skipExtra.optionsLimit = 1;

        ManifestCatalog topMovie;
        topMovie.id    = QStringLiteral("top");
        topMovie.type  = QStringLiteral("movie");
        topMovie.name  = QStringLiteral("Popular");
        topMovie.extra = {searchExtra, genreExtra, skipExtra};

        ManifestCatalog topSeries;
        topSeries.id    = QStringLiteral("top");
        topSeries.type  = QStringLiteral("series");
        topSeries.name  = QStringLiteral("Popular");
        topSeries.extra = {searchExtra, genreExtra, skipExtra};

        ManifestCatalog imdbMovie;
        imdbMovie.id    = QStringLiteral("imdbRating");
        imdbMovie.type  = QStringLiteral("movie");
        imdbMovie.name  = QStringLiteral("Featured");
        imdbMovie.extra = {genreExtra, skipExtra};

        ManifestCatalog imdbSeries;
        imdbSeries.id    = QStringLiteral("imdbRating");
        imdbSeries.type  = QStringLiteral("series");
        imdbSeries.name  = QStringLiteral("Featured");
        imdbSeries.extra = {genreExtra, skipExtra};

        cinemeta.manifest.catalogs = {topMovie, topSeries, imdbMovie, imdbSeries};
    }
    m_addons.push_back(cinemeta);

    AddonDescriptor torrentio;
    torrentio.transportUrl = QUrl(QStringLiteral("https://torrentio.strem.fun/manifest.json"));
    torrentio.flags.official = true;
    torrentio.flags.enabled = true;
    torrentio.flags.protectedAddon = true;
    torrentio.manifest.id = QStringLiteral("com.stremio.torrentio.addon");
    torrentio.manifest.version = QStringLiteral("0.0.15");
    torrentio.manifest.name = QStringLiteral("Torrentio");
    torrentio.manifest.types = {
        QStringLiteral("movie"), QStringLiteral("series"),
        QStringLiteral("anime"), QStringLiteral("other"),
    };
    {
        ManifestResource streamRes;
        streamRes.name = QStringLiteral("stream");
        streamRes.hasTypes = true;
        streamRes.types = {QStringLiteral("movie"), QStringLiteral("series"), QStringLiteral("anime")};
        streamRes.hasIdPrefixes = true;
        streamRes.idPrefixes = {QStringLiteral("tt"), QStringLiteral("kitsu")};
        torrentio.manifest.resources = {streamRes};
    }
    m_addons.push_back(torrentio);

    AddonDescriptor torrentCatalogs;
    torrentCatalogs.transportUrl = QUrl(QStringLiteral("https://torrent-catalogs.strem.fun/manifest.json"));
    torrentCatalogs.flags.official = false;
    torrentCatalogs.flags.enabled = true;
    torrentCatalogs.flags.protectedAddon = false;
    torrentCatalogs.manifest.id = QStringLiteral("com.stremio.torrentio.catalog.addon");
    torrentCatalogs.manifest.version = QStringLiteral("1.0.2");
    torrentCatalogs.manifest.name = QStringLiteral("Torrent Catalogs");
    torrentCatalogs.manifest.types = {
        QStringLiteral("movie"), QStringLiteral("series"), QStringLiteral("anime"),
    };
    {
        ManifestResource catalog;
        catalog.name = QStringLiteral("catalog");
        torrentCatalogs.manifest.resources = {catalog};
    }
    {
        ManifestExtraProp seededWindowExtra;
        seededWindowExtra.name = QStringLiteral("genre");
        seededWindowExtra.options = {
            QStringLiteral("Yesterday"),
            QStringLiteral("This Week"),
            QStringLiteral("Last Week"),
            QStringLiteral("This Month"),
            QStringLiteral("Last Month"),
            QStringLiteral("All Time"),
        };
        seededWindowExtra.optionsLimit = 1;

        ManifestExtraProp skipExtra;
        skipExtra.name = QStringLiteral("skip");
        skipExtra.optionsLimit = 1;

        ManifestCatalog topSeededMovies;
        topSeededMovies.id = QStringLiteral("top-movies");
        topSeededMovies.type = QStringLiteral("movie");
        topSeededMovies.name = QStringLiteral("Top Seeded");
        topSeededMovies.extra = {seededWindowExtra, skipExtra};

        ManifestCatalog topSeededSeries;
        topSeededSeries.id = QStringLiteral("top-series");
        topSeededSeries.type = QStringLiteral("series");
        topSeededSeries.name = QStringLiteral("Top Seeded");
        topSeededSeries.extra = {seededWindowExtra, skipExtra};

        torrentCatalogs.manifest.catalogs = {topSeededMovies, topSeededSeries};
    }
    m_addons.push_back(torrentCatalogs);

    // STREAM_SUBTITLES_NO_ADDON (2026-05-10) â€” OpenSubtitles V3, the canonical
    // Stremio-Inc subtitle addon. Without it seeded, no installed addon
    // advertises `resource:subtitles` and SubtitlesAggregator emits an empty
    // result on every stream pick (no addon-fetched .srt tracks, only mkv-
    // internal). Marked protected so the user can't accidentally uninstall
    // a load-bearing default. Schema v4 reseed migration in load() picks
    // this up retroactively for existing installs without disturbing any
    // user-installed non-protected addons.
    AddonDescriptor opensubs;
    opensubs.transportUrl = QUrl(QStringLiteral("https://opensubtitles-v3.strem.io/manifest.json"));
    opensubs.flags.official = true;
    opensubs.flags.enabled = true;
    opensubs.flags.protectedAddon = true;
    opensubs.manifest.id = QStringLiteral("org.stremio.opensubtitlesv3");
    opensubs.manifest.version = QStringLiteral("1.0.0");
    opensubs.manifest.name = QStringLiteral("OpenSubtitles v3");
    opensubs.manifest.description = QStringLiteral("OpenSubtitles v3 Addon for Stremio");
    opensubs.manifest.types = {QStringLiteral("movie"), QStringLiteral("series")};
    {
        ManifestResource subRes;
        subRes.name = QStringLiteral("subtitles");
        subRes.hasTypes = true;
        subRes.types = {QStringLiteral("movie"), QStringLiteral("series")};
        subRes.hasIdPrefixes = true;
        subRes.idPrefixes = {QStringLiteral("tt")};
        opensubs.manifest.resources = {subRes};
    }
    m_addons.push_back(opensubs);

    // THEATRE_ANIME_CATALOG (2026-05-28) â€” Anime Kitsu, the Kitsu-keyed anime
    // metadata catalog. Serves flat absolute-numbered episode lists for anime
    // (One Piece as one continuous list, not 23 TMDB seasons). MetaAggregator
    // reroutes Animation+Japan series here via an IMDb->Kitsu bridge. Keyed on
    // kitsu/mal/anilist/anidb ids (NOT tt), so it never serves the tt-id
    // candidate fan-out -- the reroute fetches its meta directly by kitsu id.
    AddonDescriptor animeKitsu;
    animeKitsu.transportUrl = QUrl(QStringLiteral("https://anime-kitsu.strem.fun/manifest.json"));
    animeKitsu.flags.official = false;
    animeKitsu.flags.enabled = true;
    animeKitsu.flags.protectedAddon = true;  // load-bearing default for the anime reroute
    animeKitsu.manifest.id = QStringLiteral("community.anime.kitsu");
    animeKitsu.manifest.version = QStringLiteral("0.0.10");
    animeKitsu.manifest.name = QStringLiteral("Anime Kitsu");
    animeKitsu.manifest.types = {
        QStringLiteral("anime"), QStringLiteral("series"), QStringLiteral("movie"),
    };
    {
        ManifestResource metaRes;
        metaRes.name = QStringLiteral("meta");
        metaRes.hasTypes = true;
        metaRes.types = {QStringLiteral("series"), QStringLiteral("movie"),
                         QStringLiteral("anime")};
        metaRes.hasIdPrefixes = true;
        metaRes.idPrefixes = {QStringLiteral("kitsu"), QStringLiteral("mal"),
                              QStringLiteral("anilist"), QStringLiteral("anidb")};

        ManifestResource catalogRes;

codex
F1 is closed for the explicit no-registry / empty-request / no-addons paths, and F2 is closed for stale async addon replies.

New blocker: `AddonTransport::fetchResource()` can still emit `resourceFailed` synchronously for an invalid built URL, and if all pending addons complete that way, `completeOne()` emits `streamsReady` inside `StreamAggregator::load()` before StreamPage stores the returned token. The handler drops it with token `0`, leaving the current request waiting forever. This can happen from persisted descriptors because registry load does not validate `transportUrl` before `findByResourceType()`.

REQUEST-CHANGES: the fix still leaves a synchronous `streamsReady` path through `dispatchRequests()`/`completeOne()` before the caller assigns the returned generation token.
tokens used
46,906
F1 is closed for the explicit no-registry / empty-request / no-addons paths, and F2 is closed for stale async addon replies.

New blocker: `AddonTransport::fetchResource()` can still emit `resourceFailed` synchronously for an invalid built URL, and if all pending addons complete that way, `completeOne()` emits `streamsReady` inside `StreamAggregator::load()` before StreamPage stores the returned token. The handler drops it with token `0`, leaving the current request waiting forever. This can happen from persisted descriptors because registry load does not validate `transportUrl` before `findByResourceType()`.

REQUEST-CHANGES: the fix still leaves a synchronous `streamsReady` path through `dispatchRequests()`/`completeOne()` before the caller assigns the returned generation token.
