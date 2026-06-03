OpenAI Codex v0.131.0
--------
workdir: C:\Users\Suprabha\Desktop\Tankoban 2
model: gpt-5.5
provider: openai
approval: never
sandbox: read-only
reasoning effort: high
reasoning summaries: none
session id: 019e8c93-f4f6-76e0-a66e-11afe70f135f
--------
user
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

codex
I’ll keep this read-only and trace the aggregator paths against the current tree so the verdict is based on the actual code, not only the pasted diff.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/StreamAggregator.h | Select-Object -Skip 25 -First 125' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/StreamAggregator.cpp | Select-Object -Skip 490 -First 230' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"streamsReady|load\\(|completeOne|emitStreamsReadyDeferred|fetchResource|resourceFailed|resourceReady\" src/core/stream src -g \"*.cpp\" -g \"*.h\"" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'git diff -- src/core/stream/StreamAggregator.cpp src/core/stream/StreamAggregator.h' in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1429ms:
src\ui\dialogs\AddTorrentDialog.h:28:    // the magnet never reaches libtorrent. startDownload() now self-defends by
src\devtools\HangWatchdog.cpp:26:    if (m_run.load(std::memory_order_relaxed))
src\devtools\HangWatchdog.cpp:79:    while (m_run.load(std::memory_order_relaxed)) {
src\devtools\HangWatchdog.cpp:81:        if (!m_run.load(std::memory_order_relaxed))
src\devtools\HangWatchdog.cpp:85:        const qint64 last = m_lastBeatMs.load(std::memory_order_relaxed);
src\devtools\HangWatchdog.cpp:88:        if (age > m_thresholdMs && !m_hangActive.load(std::memory_order_relaxed)) {
src\devtools\HangWatchdog.cpp:104:        } else if (age <= m_thresholdMs && m_hangActive.load(std::memory_order_relaxed)) {
src/core/stream\addon\AddonTransport.h:22:    void fetchResource(const QUrl& base, const ResourceRequest& request);
src/core/stream\addon\AddonTransport.h:27:    void resourceReady(const tankostream::addon::ResourceRequest& request,
src/core/stream\addon\AddonTransport.h:29:    void resourceFailed(const tankostream::addon::ResourceRequest& request,
src\core\CoreBridge.cpp:213:                m_unifiedProgress->episodePayload(id.imdbId, id.season, id.episode);
src\core\CoreBridge.cpp:246:            m_unifiedProgress->setEpisodePayload(id.imdbId, id.season, id.episode, entry);
src\core\CoreBridge.cpp:253:            m_unifiedProgress->setPathPayload(path, entry, itemId);
src\core\book\AbbScraper.h:63:    void resolveDownload(const QString& md5OrId) override;
src\core\book\AbbScraper.cpp:324:void AbbScraper::resolveDownload(const QString& md5OrId)
src\core\book\AnnaArchiveScraper.h:51:    void resolveDownload(const QString& md5OrId) override;
src\core\book\AnnaArchiveScraper.cpp:728:    m_view->load(target);
src\core\book\AnnaArchiveScraper.cpp:846:    m_view->load(target);
src\core\book\AnnaArchiveScraper.cpp:968:void AnnaArchiveScraper::resolveDownload(const QString& md5OrId)
src\core\book\AnnaArchiveScraper.cpp:1002:    m_view->load(target);
src\core\book\AnnaArchiveScraper.cpp:1116:void AnnaArchiveScraper::resolveDownload(const QString& md5OrId)
src\core\book\BookDownloader.h:54:    // null, startMagnetDownload() emits downloadFailed immediately with a clear
src\core\book\BookDownloader.h:67:    QString startDownload(const QString& md5,
src\core\book\BookDownloader.h:87:    QString startMagnetDownload(const QString& magnetUri,
src\core\book\BookDownloader.h:92:    void cancelDownload(const QString& md5);
src\core\book\BookDownloader.cpp:120:QString BookDownloader::startMagnetDownload(const QString& magnetUri,
src\core\book\BookDownloader.cpp:400:        startMagnetDownload(next.magnetUri,
src\core\book\BookDownloader.cpp:465:QString BookDownloader::startDownload(const QString& md5,
src\core\book\BookDownloader.cpp:511:void BookDownloader::cancelDownload(const QString& md5)
src\core\book\BookScraper.h:14://  - resolveDownload(md5OrId) resolves one or more direct HTTP URLs via
src\core\book\BookScraper.h:41:    virtual void resolveDownload(const QString& md5OrId) = 0;
src\core\book\BooksCatalogueLibraryStore.h:82:    void load();
src/core/stream\UnifiedProgressStore.h:32:    void setEpisodePayload(const QString& imdbId, int season, int episode,
src/core/stream\UnifiedProgressStore.h:34:    QJsonObject episodePayload(const QString& imdbId, int season, int episode) const;
src/core/stream\UnifiedProgressStore.h:37:    void setPathPayload(const QString& canonicalPath, const QJsonObject& payload,
src/core/stream\UnifiedProgressStore.h:39:    QJsonObject pathPayload(const QString& canonicalPath) const;
src/core/stream\UnifiedProgressStore.h:61:    void load();
src/core/stream\UnifiedProgressStore.h:65:    static QJsonObject normalizedPayload(const QJsonObject& payload);
src\core\book\BooksCatalogueLibraryStore.cpp:17:    // Lazy load so tests can construct before calling load().
src\core\book\BooksCatalogueLibraryStore.cpp:196:void BooksCatalogueLibraryStore::load()
src\core\book\TankorentBookScraper.h:36:    void resolveDownload(const QString& torrentId) override;
src\core\book\TankorentBookScraper.h:48:    // so resolveDownload(magnetUri) can emit downloadResolved synchronously
src\core\book\BookResult.h:42:    QString downloadUrl;  // resolved direct URL — only set by resolveDownload()
src/core/stream\UnifiedProgressStore.cpp:14:    load();
src/core/stream\UnifiedProgressStore.cpp:33:    setEpisodePayload(imdbId, season, episode, payload);
src/core/stream\UnifiedProgressStore.cpp:52:    setPathPayload(canonicalPath, payload);
src/core/stream\UnifiedProgressStore.cpp:64:void UnifiedProgressStore::setEpisodePayload(const QString& imdbId, int season, int episode,
src/core/stream\UnifiedProgressStore.cpp:74:QJsonObject UnifiedProgressStore::episodePayload(const QString& imdbId,
src/core/stream\UnifiedProgressStore.cpp:96:void UnifiedProgressStore::setPathPayload(const QString& canonicalPath,
src/core/stream\UnifiedProgressStore.cpp:107:QJsonObject UnifiedProgressStore::pathPayload(const QString& canonicalPath) const
src/core/stream\UnifiedProgressStore.cpp:161:void UnifiedProgressStore::load()
src/core/stream\UnifiedProgressStore.cpp:246:QJsonObject UnifiedProgressStore::normalizedPayload(const QJsonObject& payload)
src/core/stream\UnifiedProgressStore.cpp:280:    entry.payload = normalizedPayload(payload);
src/core/stream\UnifiedProgressStore.cpp:305:    entry.payload = normalizedPayload(payload);
src\core\book\TankorentBookScraper.cpp:136:void TankorentBookScraper::resolveDownload(const QString& torrentId)
src\core\book\TankorentBookScraper.cpp:140:    // BookDownloader picks it up via startMagnetDownload(magnetUri, ...).
src/core/stream\UnifiedPackSearchEngine.cpp:37:        // streamsReady (Stremio) subscription was removed; addons are now
src\core\torrent\TorrentRepository.h:99:    bool upsertStreamDownload(const StreamDownloadRow& row);
src\core\torrent\TorrentRepository.h:100:    bool removeStreamDownload(const QString& canonicalPath);
src\core\torrent\TorrentRepository.h:101:    std::optional<StreamDownloadRow> getStreamDownload(const QString& canonicalPath);
src\core\torrent\TorrentRepository.cpp:947:bool TorrentRepository::upsertStreamDownload(const StreamDownloadRow& row) {
src\core\torrent\TorrentRepository.cpp:994:bool TorrentRepository::removeStreamDownload(const QString& canonicalPath) {
src\core\torrent\TorrentRepository.cpp:1007:std::optional<StreamDownloadRow> TorrentRepository::getStreamDownload(const QString& canonicalPath) {
src\core\book\BookSeriesIndex.h:16:// Load order (load()): a refreshed copy in the data dir if present + schema-
src\core\book\BookSeriesIndex.h:29:    void load(const QString& bundledResourcePath);
src\core\torrent\TorrentEngine.h:115:    void    setSequentialDownload(const QString& infoHash, bool sequential);
src/core/stream\SubtitlesAggregator.h:35:    void load(const SubtitleLoadRequest& request);
src/core/stream\SubtitlesAggregator.h:60:    void completeOne();
src\core\book\BookSeriesIndex.cpp:109:void BookSeriesIndex::load(const QString& bundledResourcePath)
src/core/stream\SubtitlesAggregator.cpp:120:void SubtitlesAggregator::load(const SubtitleLoadRequest& request)
src/core/stream\SubtitlesAggregator.cpp:214:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src/core/stream\SubtitlesAggregator.cpp:228:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream\SubtitlesAggregator.cpp:249:        worker->fetchResource(addon.baseUrl, req);
src/core/stream\SubtitlesAggregator.cpp:283:    completeOne();
src/core/stream\SubtitlesAggregator.cpp:299:    completeOne();
src/core/stream\SubtitlesAggregator.cpp:302:void SubtitlesAggregator::completeOne()
src\core\torrent\TorrentEngine.cpp:265:        // crashes before startDownload(), the .fastresume lingers with no
src\core\torrent\TorrentEngine.cpp:797:void TorrentEngine::setSequentialDownload(const QString& infoHash, bool sequential)
src\core\torrent\TorrentEngine.cpp:1827:void TorrentEngine::setSequentialDownload(const QString&, bool) {}
src\core\torrent\TorrentClient.h:164:    void    startDownload(const QString& infoHash, const AddTorrentConfig& config);
src\core\book\LibGenScraper.cpp:389:void LibGenScraper::resolveDownload(const QString& md5OrId)
src/core/stream\StreamRescueScanner.cpp:57:        if (cancelled.load()) return out;
src/core/stream\StreamRescueScanner.cpp:65:            if (cancelled.load()) return out;
src/core/stream\StreamRescueScanner.cpp:74:                if (cancelled.load()) return out;
src/core/stream\StreamRescueScanner.cpp:133:            if (m_cancelled.load()) break;
src/core/stream\StreamRescueScanner.cpp:154:            if (m_cancelled.load()) break;
src/core/stream\StreamRescueScanner.cpp:203:            if (!searchDone.load()) {
src/core/stream\StreamRescueScanner.cpp:291:        if (!m_cancelled.load() && m_metaStore) {
src/core/stream\StreamRescueScanner.cpp:313:                    .arg(m_cancelled.load()).arg(anyShowProcessed));
src\core\indexers\CloudflareCookieHarvester.cpp:161:    m_view->load(target);
src\core\torrent\TorrentClient.cpp:818:            startDownload(transferId, *cfg);
src\core\torrent\TorrentClient.cpp:1381:        startDownload(infoHash, config);
src\core\torrent\TorrentClient.cpp:1415:        startDownload(infoHash, config);
src\core\torrent\TorrentClient.cpp:2832:    // confirms via startDownload(). This prevents ghost entries if the app
src\core\torrent\TorrentClient.cpp:2856:    startDownload(hash, cfg);
src\core\torrent\TorrentClient.cpp:2860:void TorrentClient::startDownload(const QString& infoHash, const AddTorrentConfig& config)
src\core\torrent\TorrentClient.cpp:2963:        m_engine->setSequentialDownload(hash, true);
src/core/stream\addon\AddonTransport.cpp:81:void AddonTransport::fetchResource(const QUrl& base, const ResourceRequest& request)
src/core/stream\addon\AddonTransport.cpp:85:        emit resourceFailed(request, QStringLiteral("Invalid resource URL"));
src/core/stream\addon\AddonTransport.cpp:98:            emit resourceFailed(request, reply->errorString());
src/core/stream\addon\AddonTransport.cpp:105:            emit resourceFailed(request, QStringLiteral("Resource JSON parse error: ") +
src/core/stream\addon\AddonTransport.cpp:110:        emit resourceReady(request, doc.object());
src\core\torrent\LegacyImporter.cpp:668:        if (repo.upsertStreamDownload(d)) {
src/core/stream\StreamLibrary.h:65:    void load();
src/core/stream\addon\AddonRegistry.h:50:    void load();
src/core/stream\StreamLibrary.cpp:14:    load();
src/core/stream\StreamLibrary.cpp:148:void StreamLibrary::load()
src/core/stream\addon\AddonRegistry.cpp:297:    load();
src/core/stream\addon\AddonRegistry.cpp:530:void AddonRegistry::load()
src/core/stream\addon\AddonRegistry.cpp:764:    // a load-bearing default. Schema v4 reseed migration in load() picks
src/core/stream\StreamDownloadIndex.h:50:    // Triggers load() to rebuild the in-memory maps from the repo. Until
src/core/stream\StreamDownloadIndex.h:129:    // the final Complete entry after the file moves to its library path (load()
src/core/stream\StreamDownloadIndex.h:155:    void load();
src/core/stream\StreamDownloadIndex.cpp:82:    // setRepository(), which triggers load() to rebuild the in-memory maps
src/core/stream\StreamDownloadIndex.cpp:90:    load();
src/core/stream\StreamDownloadIndex.cpp:94:void StreamDownloadIndex::load()
src/core/stream\StreamDownloadIndex.cpp:236:        m_repo->upsertStreamDownload(rowFromEntry(e));
src/core/stream\StreamDownloadIndex.cpp:263:    // episode=0 which the next load() will classify as "movie". The emit
src/core/stream\StreamDownloadIndex.cpp:297:        m_repo->upsertStreamDownload(rowFromEntry(persisted));
src/core/stream\StreamDownloadIndex.cpp:331:        m_repo->upsertStreamDownload(rowFromEntry(persisted));
src/core/stream\StreamDownloadIndex.cpp:370:            m_repo->upsertStreamDownload(rowFromEntry(persisted));
src/core/stream\StreamDownloadIndex.cpp:406:            m_repo->removeStreamDownload(p);
src/core/stream\StreamDownloadIndex.cpp:444:                m_repo->removeStreamDownload(p);
src/core/stream\StreamDownloadIndex.cpp:483:            m_repo->removeStreamDownload(evictedDisplayPath);
src/core/stream\StreamDownloadIndex.cpp:552:                m_repo->removeStreamDownload(p);
src/core/stream\StreamAggregator.h:39:    // DOWNLOAD BUG 2026-06-02 — load() now returns the monotonic generation
src/core/stream\StreamAggregator.h:40:    // token it stamped (m_loadGeneration, bumped inside load() right after
src/core/stream\StreamAggregator.h:41:    // reset()). Callers that arm a one-shot on the SHARED streamsReady signal
src/core/stream\StreamAggregator.h:43:    // so a late streamsReady from a SUPERSEDED load() (rapid re-clicks) cannot
src/core/stream\StreamAggregator.h:45:    quint64 load(const StreamLoadRequest& request);
src/core/stream\StreamAggregator.h:47:    // Generation of the most recent load() — see load() above. Used by
src/core/stream\StreamAggregator.h:66:    // existing Stream-flavored load()).
src/core/stream\StreamAggregator.h:78:    void streamsReady(const QList<tankostream::addon::Stream>& streams,
src/core/stream\StreamAggregator.h:102:    void completeOne();
src/core/stream\StreamAggregator.h:105:    // streamsReady. Always queues the emit to the NEXT event-loop turn rather
src/core/stream\StreamAggregator.h:106:    // than firing synchronously, and drops it if a newer load() has superseded
src/core/stream\StreamAggregator.h:107:    // `generation`. Two reasons it must never fire synchronously inside load():
src/core/stream\StreamAggregator.h:108:    //   (1) Callers arm their one-shot before load() returns and fill the
src/core/stream\StreamAggregator.h:109:    //       correlation token FROM load()'s return value; a synchronous emit
src/core/stream\StreamAggregator.h:112:    //   (2) dispatchRequests() runs inside load(), and AddonTransport can emit
src/core/stream\StreamAggregator.h:113:    //       resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon) —
src/core/stream\StreamAggregator.h:114:    //       so completeOne() could otherwise reach this emit before load()
src/core/stream\StreamAggregator.h:117:    // so a subsequent reset()/load() can't mutate them out from under the emit.
src/core/stream\StreamAggregator.h:118:    void emitStreamsReadyDeferred(quint64 generation,
src/core/stream\StreamAggregator.h:133:    // load() (right after reset()) and returned to the caller so a one-shot
src/core/stream\StreamAggregator.h:134:    // streamsReady handler can correlate the emit back to ITS request and
src/core/stream\StreamAggregator.h:135:    // ignore stale emits from a superseded load(). Defends against the rapid
src/core/stream\StreamAggregator.h:136:    // re-click race where a late streamsReady from an earlier load() fires the
src/core/stream\StreamAggregator.cpp:507:quint64 StreamAggregator::load(const StreamLoadRequest& request)
src/core/stream\StreamAggregator.cpp:512:    // gate its one-shot streamsReady handler against currentLoadToken() so a
src/core/stream\StreamAggregator.cpp:513:    // late emit from a SUPERSEDED load() (rapid re-clicks) is discarded.
src/core/stream\StreamAggregator.cpp:518:        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
src/core/stream\StreamAggregator.cpp:526:        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
src/core/stream\StreamAggregator.cpp:543:void StreamAggregator::emitStreamsReadyDeferred(quint64 generation,
src/core/stream\StreamAggregator.cpp:555:                return;  // superseded by a newer load() — drop the stale emit
src/core/stream\StreamAggregator.cpp:556:            emit streamsReady(streams, addonsById);
src/core/stream\StreamAggregator.cpp:564:    // belongs to. A reply from a worker launched by a SUPERSEDED load() (rapid
src/core/stream\StreamAggregator.cpp:568:    // streamsReady that the handler-side token gate would then wrongly accept
src/core/stream\StreamAggregator.cpp:605:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src/core/stream\StreamAggregator.cpp:619:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream\StreamAggregator.cpp:633:        worker->fetchResource(addon.transportUrl, req);
src/core/stream\StreamAggregator.cpp:641:        completeOne();
src/core/stream\StreamAggregator.cpp:670:    completeOne();
src/core/stream\StreamAggregator.cpp:680:    completeOne();
src/core/stream\StreamAggregator.cpp:683:void StreamAggregator::completeOne()
src/core/stream\StreamAggregator.cpp:686:    // a superseded load() (reset() zeroed m_pendingResponses mid-flight) could
src/core/stream\StreamAggregator.cpp:687:    // otherwise drive the counter negative and re-emit streamsReady against the
src/core/stream\StreamAggregator.cpp:698:    // SYNCHRONOUS resourceFailed (invalid addon URL) during dispatchRequests()
src/core/stream\StreamAggregator.cpp:699:    // can't fire streamsReady inside load() before the caller stores its token.
src/core/stream\StreamAggregator.cpp:700:    emitStreamsReadyDeferred(m_loadGeneration, m_streams, m_addonsById);
src\core\manga\ComicsTankoyomiLibrary.h:81:    void load();
src\core\manga\ComicsTankoyomiLibrary.cpp:13:    load();
src\core\manga\ComicsTankoyomiLibrary.cpp:16:void ComicsTankoyomiLibrary::load()
src/core/stream\MetaAggregator.cpp:373:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src/core/stream\MetaAggregator.cpp:399:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream\MetaAggregator.cpp:416:    worker->fetchResource(pending.baseUrl, request);
src/core/stream\MetaAggregator.cpp:457:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src/core/stream\MetaAggregator.cpp:500:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream\MetaAggregator.cpp:513:    worker->fetchResource(baseUrl, request);
src/core/stream\MetaAggregator.cpp:698:        worker, &AddonTransport::resourceReady, this,
src/core/stream\MetaAggregator.cpp:724:        worker, &AddonTransport::resourceFailed, this,
src/core/stream\MetaAggregator.cpp:737:    worker->fetchResource(baseUrl, request);
src/core/stream\MetaAggregator.cpp:805:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src/core/stream\MetaAggregator.cpp:833:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream\MetaAggregator.cpp:846:    worker->fetchResource(baseUrl, request);
src/core/stream\MetaAggregator.cpp:942:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src/core/stream\MetaAggregator.cpp:966:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream\MetaAggregator.cpp:983:        worker->fetchResource(d.baseUrl, request);
src\core\manga\MangaPosterCache.h:16:void download(const MangaResult& preview,
src/core/stream\CatalogAggregator.h:35:    void load(const CatalogQuery& query);
src\core\manga\MangaPosterCache.cpp:53:void download(const MangaResult& preview,
src/core/stream\CatalogAggregator.cpp:98:void CatalogAggregator::load(const CatalogQuery& query)
src/core/stream\CatalogAggregator.cpp:195:        connect(worker, &AddonTransport::resourceReady, this,
src/core/stream\CatalogAggregator.cpp:201:        connect(worker, &AddonTransport::resourceFailed, this,
src/core/stream\CatalogAggregator.cpp:207:        worker->fetchResource(cursor.baseUrl, req);
src\core\book\LibGenScraper.h:57:    void resolveDownload(const QString& md5OrId) override;
src/core/stream\BulkSourceCollector.cpp:144:        aggregator, &StreamAggregator::streamsReady, this,
src/core/stream\BulkSourceCollector.cpp:166:    aggregator->load(request);
src\core\manga\MangaDownloader.cpp:145:QString MangaDownloader::startDownload(const QString& seriesTitle, const QString& source,
src\core\manga\MangaDownloader.cpp:1107:void MangaDownloader::cancelDownload(const QString& id)
src\core\manga\MangaDownloader.cpp:1157:void MangaDownloader::removeDownload(const QString& id)
src\core\stream\UnifiedProgressStore.h:32:    void setEpisodePayload(const QString& imdbId, int season, int episode,
src\core\stream\UnifiedProgressStore.h:34:    QJsonObject episodePayload(const QString& imdbId, int season, int episode) const;
src\core\stream\UnifiedProgressStore.h:37:    void setPathPayload(const QString& canonicalPath, const QJsonObject& payload,
src\core\stream\UnifiedProgressStore.h:39:    QJsonObject pathPayload(const QString& canonicalPath) const;
src\core\stream\UnifiedProgressStore.h:61:    void load();
src\core\stream\UnifiedProgressStore.h:65:    static QJsonObject normalizedPayload(const QJsonObject& payload);
src\core\stream\UnifiedProgressStore.cpp:14:    load();
src\core\stream\UnifiedProgressStore.cpp:33:    setEpisodePayload(imdbId, season, episode, payload);
src\core\stream\UnifiedProgressStore.cpp:52:    setPathPayload(canonicalPath, payload);
src\core\stream\UnifiedProgressStore.cpp:64:void UnifiedProgressStore::setEpisodePayload(const QString& imdbId, int season, int episode,
src\core\stream\UnifiedProgressStore.cpp:74:QJsonObject UnifiedProgressStore::episodePayload(const QString& imdbId,
src\core\stream\UnifiedProgressStore.cpp:96:void UnifiedProgressStore::setPathPayload(const QString& canonicalPath,
src\core\stream\UnifiedProgressStore.cpp:107:QJsonObject UnifiedProgressStore::pathPayload(const QString& canonicalPath) const
src\core\stream\UnifiedProgressStore.cpp:161:void UnifiedProgressStore::load()
src\core\stream\UnifiedProgressStore.cpp:246:QJsonObject UnifiedProgressStore::normalizedPayload(const QJsonObject& payload)
src\core\stream\UnifiedProgressStore.cpp:280:    entry.payload = normalizedPayload(payload);
src\core\stream\UnifiedProgressStore.cpp:305:    entry.payload = normalizedPayload(payload);
src\core\stream\UnifiedPackSearchEngine.cpp:37:        // streamsReady (Stremio) subscription was removed; addons are now
src\core\manga\MangaDownloadIndex.cpp:48:    load();
src\core\manga\MangaDownloadIndex.cpp:51:void MangaDownloadIndex::load()
src\core\manga\MangaDownloader.h:67:    QString startDownload(const QString& seriesTitle, const QString& source,
src\core\manga\MangaDownloader.h:77:    void cancelDownload(const QString& id);
src\core\manga\MangaDownloader.h:79:    void removeDownload(const QString& id);
src\core\manga\MangaDownloadIndex.h:129:    void load();
src\core\stream\SubtitlesAggregator.h:35:    void load(const SubtitleLoadRequest& request);
src\core\stream\SubtitlesAggregator.h:60:    void completeOne();
src\core\manga\TorrentRequestLedger.h:78:    void load();
src\core\stream\SubtitlesAggregator.cpp:120:void SubtitlesAggregator::load(const SubtitleLoadRequest& request)
src\core\stream\SubtitlesAggregator.cpp:214:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\SubtitlesAggregator.cpp:228:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\SubtitlesAggregator.cpp:249:        worker->fetchResource(addon.baseUrl, req);
src\core\stream\SubtitlesAggregator.cpp:283:    completeOne();
src\core\stream\SubtitlesAggregator.cpp:299:    completeOne();
src\core\stream\SubtitlesAggregator.cpp:302:void SubtitlesAggregator::completeOne()
src\core\manga\TorrentRequestLedger.cpp:98:    load();
src\core\manga\TorrentRequestLedger.cpp:103:void TorrentRequestLedger::load()
src\core\stream\StreamRescueScanner.cpp:57:        if (cancelled.load()) return out;
src\core\stream\StreamRescueScanner.cpp:65:            if (cancelled.load()) return out;
src\core\stream\StreamRescueScanner.cpp:74:                if (cancelled.load()) return out;
src\core\stream\StreamRescueScanner.cpp:133:            if (m_cancelled.load()) break;
src\core\stream\StreamRescueScanner.cpp:154:            if (m_cancelled.load()) break;
src\core\stream\StreamRescueScanner.cpp:203:            if (!searchDone.load()) {
src\core\stream\StreamRescueScanner.cpp:291:        if (!m_cancelled.load() && m_metaStore) {
src\core\stream\StreamRescueScanner.cpp:313:                    .arg(m_cancelled.load()).arg(anyShowProcessed));
src\core\stream\StreamLibrary.h:65:    void load();
src\core\JsonStore.cpp:68:    while (!m_shutdown.load(std::memory_order_acquire)) {
src\core\stream\StreamLibrary.cpp:14:    load();
src\core\stream\StreamLibrary.cpp:148:void StreamLibrary::load()
src\core\stream\StreamDownloadIndex.h:50:    // Triggers load() to rebuild the in-memory maps from the repo. Until
src\core\stream\StreamDownloadIndex.h:129:    // the final Complete entry after the file moves to its library path (load()
src\core\stream\StreamDownloadIndex.h:155:    void load();
src\core\stream\StreamDownloadIndex.cpp:82:    // setRepository(), which triggers load() to rebuild the in-memory maps
src\core\stream\StreamDownloadIndex.cpp:90:    load();
src\core\stream\StreamDownloadIndex.cpp:94:void StreamDownloadIndex::load()
src\core\stream\StreamDownloadIndex.cpp:236:        m_repo->upsertStreamDownload(rowFromEntry(e));
src\core\stream\StreamDownloadIndex.cpp:263:    // episode=0 which the next load() will classify as "movie". The emit
src\core\stream\StreamDownloadIndex.cpp:297:        m_repo->upsertStreamDownload(rowFromEntry(persisted));
src\core\stream\StreamDownloadIndex.cpp:331:        m_repo->upsertStreamDownload(rowFromEntry(persisted));
src\core\stream\StreamDownloadIndex.cpp:370:            m_repo->upsertStreamDownload(rowFromEntry(persisted));
src\core\stream\StreamDownloadIndex.cpp:406:            m_repo->removeStreamDownload(p);
src\core\stream\StreamDownloadIndex.cpp:444:                m_repo->removeStreamDownload(p);
src\core\stream\StreamDownloadIndex.cpp:483:            m_repo->removeStreamDownload(evictedDisplayPath);
src\core\stream\StreamDownloadIndex.cpp:552:                m_repo->removeStreamDownload(p);
src\ui\player\VideoPlayer.cpp:576:    // pick triggers a fresh load() that emits subtitlesReady with the new
src\ui\player\SyncClock.h:22:    bool isRunning() const { return m_running.load(); }
src\ui\player\SyncClock.h:47:        if (!m_started.load())
src\ui\player\SyncClock.h:50:        if (m_paused.load())
src\ui\player\SyncClock.h:51:            return m_anchorPtsUs.load();
src\ui\player\SyncClock.h:53:        int64_t elapsed = (nowNs() - m_anchorTimeNs.load()) / 1000;
src\ui\player\SyncClock.h:59:        const double velocity = m_clockVelocity.load();
src\ui\player\SyncClock.h:64:        return m_anchorPtsUs.load() + elapsed;
src\ui\player\SyncClock.h:67:    bool hasStarted() const { return m_started.load(); }
src\ui\player\SyncClock.h:95:            const double prev = m_latencyEmaMs.load();
src\ui\player\SyncClock.h:121:        const double ema = m_latencyEmaMs.load();
src\ui\player\SyncClock.h:131:        return m_lastFrameLatencyMs.load();
src\ui\player\SyncClock.h:140:        return m_clockVelocity.load();
src\ui\player\SyncClock.h:147:        return m_latencyEmaMs.load();
src\core\stream\CatalogAggregator.h:35:    void load(const CatalogQuery& query);
src\core\stream\StreamAggregator.h:39:    // DOWNLOAD BUG 2026-06-02 — load() now returns the monotonic generation
src\core\stream\StreamAggregator.h:40:    // token it stamped (m_loadGeneration, bumped inside load() right after
src\core\stream\StreamAggregator.h:41:    // reset()). Callers that arm a one-shot on the SHARED streamsReady signal
src\core\stream\StreamAggregator.h:43:    // so a late streamsReady from a SUPERSEDED load() (rapid re-clicks) cannot
src\core\stream\StreamAggregator.h:45:    quint64 load(const StreamLoadRequest& request);
src\core\stream\StreamAggregator.h:47:    // Generation of the most recent load() — see load() above. Used by
src\core\stream\StreamAggregator.h:66:    // existing Stream-flavored load()).
src\core\stream\StreamAggregator.h:78:    void streamsReady(const QList<tankostream::addon::Stream>& streams,
src\core\stream\StreamAggregator.h:102:    void completeOne();
src\core\stream\StreamAggregator.h:105:    // streamsReady. Always queues the emit to the NEXT event-loop turn rather
src\core\stream\StreamAggregator.h:106:    // than firing synchronously, and drops it if a newer load() has superseded
src\core\stream\StreamAggregator.h:107:    // `generation`. Two reasons it must never fire synchronously inside load():
src\core\stream\StreamAggregator.h:108:    //   (1) Callers arm their one-shot before load() returns and fill the
src\core\stream\StreamAggregator.h:109:    //       correlation token FROM load()'s return value; a synchronous emit
src\core\stream\StreamAggregator.h:112:    //   (2) dispatchRequests() runs inside load(), and AddonTransport can emit
src\core\stream\StreamAggregator.h:113:    //       resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon) —
src\core\stream\StreamAggregator.h:114:    //       so completeOne() could otherwise reach this emit before load()
src\core\stream\StreamAggregator.h:117:    // so a subsequent reset()/load() can't mutate them out from under the emit.
src\core\stream\StreamAggregator.h:118:    void emitStreamsReadyDeferred(quint64 generation,
src\core\stream\StreamAggregator.h:133:    // load() (right after reset()) and returned to the caller so a one-shot
src\core\stream\StreamAggregator.h:134:    // streamsReady handler can correlate the emit back to ITS request and
src\core\stream\StreamAggregator.h:135:    // ignore stale emits from a superseded load(). Defends against the rapid
src\core\stream\StreamAggregator.h:136:    // re-click race where a late streamsReady from an earlier load() fires the
src\core\stream\StreamAggregator.cpp:507:quint64 StreamAggregator::load(const StreamLoadRequest& request)
src\core\stream\StreamAggregator.cpp:512:    // gate its one-shot streamsReady handler against currentLoadToken() so a
src\core\stream\StreamAggregator.cpp:513:    // late emit from a SUPERSEDED load() (rapid re-clicks) is discarded.
src\core\stream\StreamAggregator.cpp:518:        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
src\core\stream\StreamAggregator.cpp:526:        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix — never emit synchronously
src\core\stream\StreamAggregator.cpp:543:void StreamAggregator::emitStreamsReadyDeferred(quint64 generation,
src\core\stream\StreamAggregator.cpp:555:                return;  // superseded by a newer load() — drop the stale emit
src\core\stream\StreamAggregator.cpp:556:            emit streamsReady(streams, addonsById);
src\core\stream\StreamAggregator.cpp:564:    // belongs to. A reply from a worker launched by a SUPERSEDED load() (rapid
src\core\stream\StreamAggregator.cpp:568:    // streamsReady that the handler-side token gate would then wrongly accept
src\core\stream\StreamAggregator.cpp:605:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\StreamAggregator.cpp:619:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\StreamAggregator.cpp:633:        worker->fetchResource(addon.transportUrl, req);
src\core\stream\StreamAggregator.cpp:641:        completeOne();
src\core\stream\StreamAggregator.cpp:670:    completeOne();
src\core\stream\StreamAggregator.cpp:680:    completeOne();
src\core\stream\StreamAggregator.cpp:683:void StreamAggregator::completeOne()
src\core\stream\StreamAggregator.cpp:686:    // a superseded load() (reset() zeroed m_pendingResponses mid-flight) could
src\core\stream\StreamAggregator.cpp:687:    // otherwise drive the counter negative and re-emit streamsReady against the
src\core\stream\StreamAggregator.cpp:698:    // SYNCHRONOUS resourceFailed (invalid addon URL) during dispatchRequests()
src\core\stream\StreamAggregator.cpp:699:    // can't fire streamsReady inside load() before the caller stores its token.
src\core\stream\StreamAggregator.cpp:700:    emitStreamsReadyDeferred(m_loadGeneration, m_streams, m_addonsById);
src\core\stream\MetaAggregator.cpp:373:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:399:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:416:    worker->fetchResource(pending.baseUrl, request);
src\core\stream\MetaAggregator.cpp:457:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:500:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:513:    worker->fetchResource(baseUrl, request);
src\core\stream\MetaAggregator.cpp:698:        worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:724:        worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:737:    worker->fetchResource(baseUrl, request);
src\core\stream\MetaAggregator.cpp:805:    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:833:    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:846:    worker->fetchResource(baseUrl, request);
src\core\stream\MetaAggregator.cpp:942:        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\MetaAggregator.cpp:966:        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\MetaAggregator.cpp:983:        worker->fetchResource(d.baseUrl, request);
src\ui\player\SidecarProcess.cpp:1174:    snap["nextSeq"]          = m_seq.load();
src\ui\player\FrameCanvas.cpp:601:    while (!m_waitableStop.load(std::memory_order_relaxed)) {
src\ui\player\FrameCanvas.cpp:605:        if (m_waitableStop.load(std::memory_order_relaxed)) break;
src\core\stream\CatalogAggregator.cpp:98:void CatalogAggregator::load(const CatalogQuery& query)
src\core\stream\CatalogAggregator.cpp:195:        connect(worker, &AddonTransport::resourceReady, this,
src\core\stream\CatalogAggregator.cpp:201:        connect(worker, &AddonTransport::resourceFailed, this,
src\core\stream\CatalogAggregator.cpp:207:        worker->fetchResource(cursor.baseUrl, req);
src\ui\player\FfmpegDecoder.cpp:138:    if (m_paused.load()) play(); else pause();
src\ui\player\FfmpegDecoder.cpp:153:    if (m_paused.load()) {
src\ui\player\FfmpegDecoder.cpp:191:    for (int i = 0; i < 200 && !m_clock->hasStarted() && !m_stop.load(); ++i)
src\ui\player\FfmpegDecoder.cpp:194:    while (!m_stop.load()) {
src\ui\player\FfmpegDecoder.cpp:198:            while (m_paused.load() && !m_stop.load())
src\ui\player\FfmpegDecoder.cpp:201:        if (m_stop.load()) break;
src\ui\player\FfmpegDecoder.cpp:228:            if (m_stop.load()) break;
src\ui\player\FfmpegDecoder.cpp:241:                    if (m_stop.load()) break;
src\ui\player\AudioDecoder.cpp:117:    if (m_paused.load()) {
src\ui\player\AudioDecoder.cpp:176:    while (!m_stop.load()) {
src\ui\player\AudioDecoder.cpp:180:            while (m_paused.load() && !m_stop.load()) {
src\ui\player\AudioDecoder.cpp:184:        if (m_stop.load()) break;
src\ui\player\AudioDecoder.cpp:219:            if (m_stop.load()) break;
src\ui\player\AudioDecoder.cpp:237:            float vol = m_volume.load();
src\ui\player\AudioDecoder.cpp:251:            while (samplesWritten < converted && !m_stop.load()) {
src\ui\player\AudioDecoder.cpp:256:                    if (m_stop.load()) break;
src\ui\player\AudioDecoder.cpp:259:                if (m_stop.load()) break;
src\ui\pages\BooksPage.cpp:77:    m_catalogueStore->load();
src\ui\pages\BooksPage.cpp:1923:        handle = m_bookDownloader->startMagnetDownload(
src\ui\pages\BooksPage.cpp:1927:        // from scraper->resolveDownload(); BookDownloader walks them for
src\ui\pages\BooksPage.cpp:1934:        handle = m_bookDownloader->startDownload(
src\core\stream\BulkSourceCollector.cpp:144:        aggregator, &StreamAggregator::streamsReady, this,
src\core\stream\BulkSourceCollector.cpp:166:    aggregator->load(request);
src\ui\player\OverlayShmReader.cpp:87:    f.counter = counter->load(std::memory_order_acquire);
src\core\stream\addon\AddonTransport.h:22:    void fetchResource(const QUrl& base, const ResourceRequest& request);
src\core\stream\addon\AddonTransport.h:27:    void resourceReady(const tankostream::addon::ResourceRequest& request,
src\core\stream\addon\AddonTransport.h:29:    void resourceFailed(const tankostream::addon::ResourceRequest& request,
src\core\stream\addon\AddonTransport.cpp:81:void AddonTransport::fetchResource(const QUrl& base, const ResourceRequest& request)
src\core\stream\addon\AddonTransport.cpp:85:        emit resourceFailed(request, QStringLiteral("Invalid resource URL"));
src\core\stream\addon\AddonTransport.cpp:98:            emit resourceFailed(request, reply->errorString());
src\core\stream\addon\AddonTransport.cpp:105:            emit resourceFailed(request, QStringLiteral("Resource JSON parse error: ") +
src\core\stream\addon\AddonTransport.cpp:110:        emit resourceReady(request, doc.object());
src\core\stream\addon\AddonRegistry.h:50:    void load();
src\core\stream\addon\AddonRegistry.cpp:297:    load();
src\core\stream\addon\AddonRegistry.cpp:530:void AddonRegistry::load()
src\core\stream\addon\AddonRegistry.cpp:764:    // a load-bearing default. Schema v4 reseed migration in load() picks
src\ui\player\KeyBindings.cpp:91:    load();
src\ui\player\KeyBindings.cpp:104:void KeyBindings::load()
src\core\ScannerUtils.h:18:    bool isCancelled() const { return cancelled.load(std::memory_order_acquire); }
src\ui\player\KeyBindings.h:28:    void load();
src\core\PosterFetcher.h:18://   PosterFetcher::download(m_nam, url, "/path/to/out.jpg", this,
src\core\PosterFetcher.h:29:    static void download(QNetworkAccessManager* nam,
src\core\PosterFetcher.cpp:27:void PosterFetcher::download(QNetworkAccessManager* nam,
src\ui\pages\PosterPickerPopover.cpp:240:    PosterFetcher::download(nam, url, path, this,
src\ui\MainWindow.cpp:796:    // triggers a one-time load() that rebuilds the in-memory byPath/byEpisode/
src\ui\MainWindow.cpp:804:    // load() here then CLEARS those in-memory registrations and restores only the
src\ui\MainWindow.cpp:807:    // NotDownloaded. Repo-first → load() restores prior state, THEN reconcile
src\ui\MainWindow.cpp:817:    // registerEpisode calls both persist and survive any later load().
src\ui\pages\ComicsPage.cpp:1111:        m_westernDownloadRecordId = m_mangaDownloader->startDownload(
src\ui\pages\TankoLibraryPage.cpp:1199:    // scaffold-button footprint. Click kicks off scraper.resolveDownload(),
src\ui\pages\TankoLibraryPage.cpp:1434:            if (pix.load(cached) && !pix.isNull()) {
src\ui\pages\TankoLibraryPage.cpp:1796:        if (pix.load(cachedPath) && !pix.isNull()) {
src\ui\pages\TankoLibraryPage.cpp:1873:        if (m_downloader) m_downloader->cancelDownload(m_selectedResult.md5);
src\ui\pages\TankoLibraryPage.cpp:1937:    scraper->resolveDownload(resolveId);
src\ui\pages\TankoLibraryPage.cpp:1946:    // BOTH fields — the slug we passed in resolveDownload() and the info
src\ui\pages\TankoLibraryPage.cpp:2021:        m_torrentClient->startDownload(hash, config);
src\ui\pages\TankoLibraryPage.cpp:2105:    m_downloader->startDownload(md5, urls, destDir, suggestedName, expectedBytes);
src\ui\pages\StreamPage.h:119:    QJsonObject devDirectDownload(int sourceIndex);             // A4S3
src\ui\pages\StreamPage.h:270:    void beginPlayOrDownload(const QString& imdbId, const QString& mediaType,
src\ui\pages\StreamPage.h:297:    void triggerBulkSeasonDownload(int season);
src\ui\pages\StreamPage.h:334:        // StreamAggregator::load(). The one-shot streamsReady handler gates on
src\ui\pages\StreamPage.h:335:        // currentLoadToken()==token so a late emit from a SUPERSEDED load()
src\ui\pages\StreamPage.h:341:    void startAutoDownload(const QString& imdbId, const QString& mediaType,
src\ui\pages\StreamPage.h:362:    void retryBulkSeasonDownload(const QString& groupId, const QStringList& itemKeys);
src\ui\pages\StreamPage.h:363:    void cancelBulkSeasonDownload();
src\ui\pages\TankorentPage.cpp:2154:        // TorrentClient::startDownload().
src\ui\pages\TankorentPage.cpp:2156:        m_client->startDownload(hash, config);
src\ui\pages\TankorentPage.cpp:2815:        m_client->engine()->setSequentialDownload(firstHash, on);
src\ui\pages\TankorentPage.cpp:3068:        m_client->startDownload(hash, config);
src\ui\pages\VideosPage.cpp:1030:                PosterFetcher::download(nam, poster, destPath, selfGuard,
src\ui\pages\stream\CatalogBrowseScreen.h:75:    void reload();
src\ui\pages\comics\ComicsSeriesView.h:181:    void deleteVolumeDownload(int volumeNumber, const QString& cbzPath);
src\ui\pages\stream\CatalogBrowseScreen.cpp:379:                reload();
src\ui\pages\stream\CatalogBrowseScreen.cpp:472:        state->aggregator->load(q);
src\ui\pages\stream\CatalogBrowseScreen.cpp:490:    reload();
src\ui\pages\stream\CatalogBrowseScreen.cpp:567:void CatalogBrowseScreen::reload()
src\ui\pages\stream\CatalogBrowseScreen.cpp:589:    m_aggregator->load(q);
src\ui\pages\stream\CatalogBrowseScreen.cpp:653:        PosterFetcher::download(m_nam, requestUrl, path, this,
src\ui\pages\stream\CatalogBrowseScreen.cpp:681:                    startDownload();
src\ui\pages\stream\CatalogBrowseScreen.cpp:693:    startDownload();
src\ui\pages\comics\ComicsSourcesPanel.h:131:    void emitTopRowDownload();
src\ui\pages\comics\ComicsSourcesPanel.h:132:    void emitRowDownload(const UnifiedSourceRow& row);
src\ui\pages\comics\ComicsSeriesView.cpp:2201:        if (pm.load(localPath)) {
src\ui\pages\comics\ComicsSeriesView.cpp:2833:    if (chosen == del)         deleteVolumeDownload(tile->volumeNumber(), st.cbzPath);
src\ui\pages\comics\ComicsSeriesView.cpp:2839:void ComicsSeriesView::deleteVolumeDownload(int volumeNumber, const QString& cbzPath)
src\ui\pages\comics\ComicsSourcesPanel.cpp:349:    emitRowDownload(m_rows.at(rowIndex));
src\ui\pages\comics\ComicsSourcesPanel.cpp:486:            emitRowDownload(clickedRow);
src\ui\pages\comics\ComicsSourcesPanel.cpp:492:            emitRowDownload(clickedRow);
src\ui\pages\comics\ComicsSourcesPanel.cpp:597:void ComicsSourcesPanel::emitTopRowDownload()
src\ui\pages\comics\ComicsSourcesPanel.cpp:610:    emitRowDownload(row);
src\ui\pages\comics\ComicsSourcesPanel.cpp:613:void ComicsSourcesPanel::emitRowDownload(const UnifiedSourceRow& row)
src\ui\pages\stream\StreamDownloadsPage.cpp:284:    if (QFile::exists(path) && pm.load(path)) {
src\ui\pages\stream\StreamDetailView.h:71:    // episode (or movie opens); `Sources` when StreamAggregator::streamsReady
src\ui\pages\stream\StreamDetailView.cpp:271:        // emit playRequested(movie) because StreamPage's streamsReady path calls
src\ui\pages\books\BookCatalogueDetailView.h:55:    // scraper->resolveDownload() which returns multiple mirrors (LibGen
src\ui\pages\books\BookCatalogueDetailView.h:147:    // need a scraper->resolveDownload(md5) call to get a fresh /get.php?key=XXX
src\ui\pages\books\BookCatalogueDetailView.cpp:552:    // from scraper->resolveDownload(md5). LibGen's key rotates ~60s so this
src\ui\pages\books\BookCatalogueDetailView.cpp:881:    scraper->resolveDownload(result.md5);
src\ui\pages\StreamPage.cpp:727:QJsonObject StreamPage::devDirectDownload(int sourceIndex)
src\ui\pages\StreamPage.cpp:988:                    m_torrentClient->startDownload(infoHash, dispatchConfig);
src\ui\pages\StreamPage.cpp:1902:        cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:1916:        // Invalidate any in-flight play context so a late streamsReady
src\ui\pages\StreamPage.cpp:1933:        cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:1945:        cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:1964:        cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:2111:    // catalog) after rebuildSelectors populates them. reload() then kicks
src\ui\pages\StreamPage.cpp:2249:        beginPlayOrDownload(imdbId, mediaType, season, episode, nullptr);
src\ui\pages\StreamPage.cpp:2260:    // between exec() and accept(). 'valid' guards against late streamsReady
src\ui\pages\StreamPage.cpp:2291:    // streamsReady lambda because we need the incoming stream list to find
src\ui\pages\StreamPage.cpp:2304:    // of the last-watched stamp. The streamsReady lambda uses this to decide
src\ui\pages\StreamPage.cpp:2323:    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:2327:    // path. The play one-shot is armed BEFORE load() runs, so the token is
src\ui\pages\StreamPage.cpp:2328:    // held in a shared_ptr captured by value and filled from the load() return
src\ui\pages\StreamPage.cpp:2330:    // generation (a late streamsReady from a superseded play/source-load).
src\ui\pages\StreamPage.cpp:2333:    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
src\ui\pages\StreamPage.cpp:2339:            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:2425:    // one-shot above so it can discard a stale emit from a superseded load().
src\ui\pages\StreamPage.cpp:2426:    *playToken = m_streamAggregator->load(req);
src\ui\pages\StreamPage.cpp:2510:            // Reuse m_streamAggregator â€” the current episode's streamsReady
src\ui\pages\StreamPage.cpp:2512:            // load() resets internal state cleanly.
src\ui\pages\StreamPage.cpp:2514:                       &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:2518:            // its load(), so the token rides in a shared_ptr filled from the
src\ui\pages\StreamPage.cpp:2519:            // load() return below; a stale emit from a superseded load() is
src\ui\pages\StreamPage.cpp:2523:                    &tankostream::stream::StreamAggregator::streamsReady, this,
src\ui\pages\StreamPage.cpp:2529:                               &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:2538:            *prefetchToken = m_streamAggregator->load(req);
src\ui\pages\StreamPage.cpp:2839:                   &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:2906:                   &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:3000:void StreamPage::triggerBulkSeasonDownload(int season)
src\ui\pages\StreamPage.cpp:3004:    cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:3111:    startAutoDownload(m_detailView->currentImdb(), QStringLiteral("series"), season, episode);
src\ui\pages\StreamPage.cpp:3151:    m_torrentClient->startDownload(hash, config);
src\ui\pages\StreamPage.cpp:3254:    m_torrentClient->startDownload(hash, config);
src\ui\pages\StreamPage.cpp:3258:// Mirrors onPlayRequested's one-shot streamsReady idiom (StreamPage.cpp ~2308-
src\ui\pages\StreamPage.cpp:3260:// lambda, build the Torrentio request id, then load(). The handler runs the
src\ui\pages\StreamPage.cpp:3262:void StreamPage::startAutoDownload(const QString& imdbId, const QString& mediaType,
src\ui\pages\StreamPage.cpp:3270:    // re-arm the shared streamsReady one-shot and call load() again, which
src\ui\pages\StreamPage.cpp:3296:    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:3298:    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
src\ui\pages\StreamPage.cpp:3302:            // load(). Without this, a late streamsReady from an EARLIER request
src\ui\pages\StreamPage.cpp:3307:            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:3342:    // DOWNLOAD BUG 2026-06-02 — capture the generation token load() stamped so
src\ui\pages\StreamPage.cpp:3343:    // the one-shot above can discard a late emit from a superseded load().
src\ui\pages\StreamPage.cpp:3345:    m_pendingAuto.token = m_streamAggregator->load(req);
src\ui\pages\StreamPage.cpp:3348:// One-shot streamsReady handler for an active auto-download. Converts the
src\ui\pages\StreamPage.cpp:3425:    m_torrentClient->startDownload(hash, config);
src\ui\pages\StreamPage.cpp:3437:    cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:3759:void StreamPage::retryBulkSeasonDownload(const QString& groupId, const QStringList& itemKeys)
src\ui\pages\StreamPage.cpp:3763:    cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:3865:void StreamPage::cancelBulkSeasonDownload()
src\ui\pages\StreamPage.cpp:3900:        cancelBulkSeasonDownload();
src\ui\pages\StreamPage.cpp:4121:        m_subtitlesAggregator->load(req);
src\ui\pages\StreamPage.cpp:4173:    // get dropped (preventing a stale streamsReady lambda from landing against
src\ui\pages\StreamPage.cpp:4222:    beginPlayOrDownload(ctx.imdbId, ctx.mediaType, ctx.season, ctx.episode,
src\ui\pages\StreamPage.cpp:4235:void StreamPage::beginPlayOrDownload(const QString& imdbId,
src\ui\pages\StreamPage.cpp:4267:    startAutoDownload(imdbId, mediaType, season, episode);

 succeeded in 1642ms:
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

class StreamAggregator : public QObject
{
    Q_OBJECT

public:
    explicit StreamAggregator(tankostream::addon::AddonRegistry* registry,
                              QObject* parent = nullptr);

    // DOWNLOAD BUG 2026-06-02 â€” load() now returns the monotonic generation
    // token it stamped (m_loadGeneration, bumped inside load() right after
    // reset()). Callers that arm a one-shot on the SHARED streamsReady signal
    // capture this token and gate their handler on currentLoadToken()==token,
    // so a late streamsReady from a SUPERSEDED load() (rapid re-clicks) cannot
    // deliver the wrong show's streams to the currently-connected one-shot.
    quint64 load(const StreamLoadRequest& request);

    // Generation of the most recent load() â€” see load() above. Used by
    // StreamPage's auto-download / play / prefetch one-shots to discard stale
    // responses correlated to an earlier request.
    quint64 currentLoadToken() const { return m_loadGeneration; }

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - season-pack indexer
    // fan-out, exposed for the new UnifiedPackSearchEngine (Task B2). Wraps
    // the existing per-indexer search pattern used by TorrentPackPicker
    // (Nyaa + PirateBay + 1337x + YTS + EZTV + ExtTorrents) and emits a single
    // packsAvailable signal once all indexers have replied or the per-call
    // timeout elapses. Pass season=0 for movies / whole-show "Complete"
    // probes; pass season>0 for "Show SNN" + "Show Season N" queries.
    // showName is taken from the addon-registry / Cinemeta context by the
    // caller (UnifiedPackSearchEngine in B2); imdbId is round-tripped on
    // the signal so callers can filter when multiple searches are in
    // flight. The host class is StreamAggregator (vs a new class) per the
    // THEATRE_DOWNLOAD_OVERHAUL plan; see the chat.md B1 RTC for the
    // host-class rationale (StreamAggregator owns the per-show source-fanout
    // surface; the new method is the TorrentResult-flavored sibling of the
    // existing Stream-flavored load()).
    // sourceFilter "all" -> dispatch all 6 indexers (default).
    // sourceFilter "<id>" -> dispatch only that indexer; valid ids match the
    // dispatch loop's QStringLiteral keys (nyaa, piratebay, 1337x, yts, eztv,
    // exttorrents). Unknown ids dispatch nothing (caller responsibility).
    void searchPacks(const QString& imdbId,
                     const QString& showName,
                     int season,
                     const QString& sourceFilter = QStringLiteral("all"),
                     bool anime = false);

signals:
    void streamsReady(const QList<tankostream::addon::Stream>& streams,
                      const QHash<QString, QString>& addonsById);
    void streamError(const QString& addonId, const QString& message);

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - one terminal emit per
    // searchPacks call, with the aggregated indexer results. Listeners
    // filter on imdbId+season to disambiguate overlapping searches.
    void packsAvailable(const QString& imdbId,
                        int season,
                        const QList<TorrentResult>& results);

private:
    struct PendingAddon {
        QString addonId;
        QString addonName;
        QUrl transportUrl;
        bool inFlight = false;
    };

    struct PackSearchContext;

    void dispatchRequests();
    void onAddonReady(const QString& addonId, const QJsonObject& payload);
    void onAddonFailed(const QString& addonId, const QString& message);
    void completeOne();
    void reset();
    // DOWNLOAD BUG 2026-06-03 (review fix) â€” the SINGLE terminal emit path for
    // streamsReady. Always queues the emit to the NEXT event-loop turn rather
    // than firing synchronously, and drops it if a newer load() has superseded
    // `generation`. Two reasons it must never fire synchronously inside load():
    //   (1) Callers arm their one-shot before load() returns and fill the
    //       correlation token FROM load()'s return value; a synchronous emit
    //       would fire while that token is still 0 and be wrongly discarded
    //       (the result for the CURRENT request lost, leaving the UI waiting).
    //   (2) dispatchRequests() runs inside load(), and AddonTransport can emit
    //       resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon) â€”
    //       so completeOne() could otherwise reach this emit before load()
    //       returns. Queuing makes the post-return ordering unconditional.
    // streams/addonsById are taken by value and captured into the queued lambda
    // so a subsequent reset()/load() can't mutate them out from under the emit.
    void emitStreamsReadyDeferred(quint64 generation,
                                  QList<tankostream::addon::Stream> streams,
                                  QHash<QString, QString> addonsById);
    void finalizePackSearch(std::shared_ptr<PackSearchContext> ctx);

    tankostream::addon::AddonRegistry* m_registry = nullptr;

    StreamLoadRequest m_request;
    QMap<QString, PendingAddon> m_pendingByAddon;
    QHash<QString, QString> m_addonsById;
    QSet<QString> m_seenIdentityKeys;
    QList<tankostream::addon::Stream> m_streams;
    int m_pendingResponses = 0;

    // DOWNLOAD BUG 2026-06-02 â€” monotonic request token. Bumped on every
    // load() (right after reset()) and returned to the caller so a one-shot
    // streamsReady handler can correlate the emit back to ITS request and
    // ignore stale emits from a superseded load(). Defends against the rapid
    // re-click race where a late streamsReady from an earlier load() fires the
    // currently-armed one-shot carrying the wrong request's streams.
    quint64 m_loadGeneration = 0;

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 - lazily-constructed NAM shared
    // across pack-search calls (mirrors TorrentPackPicker's m_nam pattern).
    QNetworkAccessManager* m_packNam = nullptr;

    // TANKORENT audit DEFECT 2 (2026-05-28) - monotonic pack-search epoch.
    // Bumped on every searchPacks(); finalizePackSearch suppresses any context
    // whose epoch is stale so a superseded fan-out (same imdbId/season, e.g.
    // a source-filter change) cannot bleed old results into the new search.
    quint64 m_packEpoch = 0;
};


 succeeded in 1645ms:
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
        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix â€” never emit synchronously
        return m_loadGeneration;
    }

    const QList<AddonDescriptor> addons =
        m_registry->findByResourceType(QStringLiteral("stream"), request.type);

    if (addons.isEmpty()) {
        emitStreamsReadyDeferred(m_loadGeneration, {}, {});  // review fix â€” never emit synchronously
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

void StreamAggregator::emitStreamsReadyDeferred(quint64 generation,
                                                QList<Stream> streams,
                                                QHash<QString, QString> addonsById)
{
    // See the header for why this is always queued rather than emitted
    // synchronously. The queued lambda is bound to `this` as its context object,
    // so Qt discards it automatically if the aggregator is destroyed before it
    // runs (no use-after-free).
    QMetaObject::invokeMethod(this,
        [this, generation, streams = std::move(streams),
         addonsById = std::move(addonsById)]() {
            if (generation != m_loadGeneration)
                return;  // superseded by a newer load() â€” drop the stale emit
            emit streamsReady(streams, addonsById);
        },
        Qt::QueuedConnection);
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
    // DOWNLOAD BUG 2026-06-03 (review fix) â€” deferred + generation-guarded so a
    // SYNCHRONOUS resourceFailed (invalid addon URL) during dispatchRequests()
    // can't fire streamsReady inside load() before the caller stores its token.
    emitStreamsReadyDeferred(m_loadGeneration, m_streams, m_addonsById);
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

 succeeded in 1738ms:
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

codex
The current tree matches the diff in the stream aggregator. I’m checking the surrounding call sites and Qt usage now, because the main remaining risk is not the F3 path itself but whether deferring changes ownership/lifetime or caller assumptions.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/BulkSourceCollector.cpp | Select-Object -Skip 130 -First 45' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 3288 -First 70' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 2318 -First 135' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/StreamAggregator.cpp | Select-Object -First 60' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonTransport.cpp | Select-Object -Skip 70 -First 50' in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1593ms:
            m_registry->findByResourceType(QStringLiteral("stream"), request.type).size();
    }

    auto* aggregator = new StreamAggregator(m_registry, this);
    auto* timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(kPerEpisodeTimeoutMs);

    inFlight->aggregator = aggregator;
    inFlight->timeout = timeout;
    m_inFlightByEpisode.insert(inFlight->episodeNum, inFlight);

    inFlight->readyConn = connect(
        aggregator, &StreamAggregator::streamsReady, this,
        [this, episodeNum = inFlight->episodeNum](
            const QList<tankostream::addon::Stream>& streams,
            const QHash<QString, QString>& addonsById) {
            finishEpisode(episodeNum, streams, addonsById, false);
        });

    inFlight->errorConn = connect(
        aggregator, &StreamAggregator::streamError, this,
        [this, episodeNum = inFlight->episodeNum](
            const QString& addonId, const QString& /*message*/) {
            if (auto* episode = m_inFlightByEpisode.value(episodeNum, nullptr))
                episode->erroredAddonIds.insert(addonId);
        });

    inFlight->timeoutConn = connect(
        timeout, &QTimer::timeout, this,
        [this, episodeNum = inFlight->episodeNum]() {
            finishEpisode(episodeNum, {}, {}, true);
        });

    timeout->start();
    aggregator->load(request);
}

void BulkSourceCollector::finishEpisode(
    int episodeNum,
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>& addonsById,
    bool timedOut)
{
    if (!m_running || m_cancelled)

 succeeded in 1645ms:
#include "StreamAggregator.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <memory>

#include "addon/AddonRegistry.h"
#include "addon/AddonTransport.h"
#include "addon/Descriptor.h"
#include "addon/ResourcePath.h"
#include "core/stream/AnimeCatalogResolver.h"
#include "core/net/NetSeam.h"

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - includes for the
// indexer fan-out wrapped by searchPacks. Mirrors the set used by
// TorrentPackPicker::launchSearches.
#include "core/TorrentIndexer.h"
#include "core/indexers/NyaaIndexer.h"
#include "core/indexers/EztvIndexer.h"
#include "core/indexers/ExtTorrentsIndexer.h"
#include "core/indexers/PirateBayIndexer.h"
#include "core/indexers/X1337xIndexer.h"
#include "core/indexers/YtsIndexer.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ResourceRequest;
using tankostream::addon::Stream;
using tankostream::addon::StreamBehaviorHints;
using tankostream::addon::StreamSource;
using tankostream::addon::SubtitleTrack;

namespace tankostream::stream {

namespace {

constexpr int kMaxTrackers = 16;

const QStringList kFallbackTrackers = {
    QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
    QStringLiteral("udp://open.stealth.si:80/announce"),
    QStringLiteral("udp://tracker.torrent.eu.org:451/announce"),
    QStringLiteral("udp://tracker.openbittorrent.com:6969/announce"),
    QStringLiteral("udp://open.demonii.com:1337/announce"),
    QStringLiteral("udp://tracker.internetwarriors.net:1337/announce"),
    QStringLiteral("udp://tracker.cyberia.is:6969/announce"),
    QStringLiteral("udp://tracker.moeking.me:6969/announce"),
    QStringLiteral("udp://explodie.org:6969/announce"),
    QStringLiteral("udp://tracker-udp.gbitt.info:80/announce"),

 succeeded in 1721ms:

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

void StreamPage::cancelAutoLaunch()
{
    if (m_autoLaunchTimer) m_autoLaunchTimer->stop();
    m_autoLaunchChoice.reset();
    if (m_detailView) m_detailView->hideAutoLaunchToast();
}

// Phase 2 Batch 2.5 â€” next-episode pre-fetch + overlay orchestration.

void StreamPage::startNextEpisodePrefetch(const QString& imdbId,
                                           int currentSeason, int currentEpisode)
{

 succeeded in 1793ms:

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

 succeeded in 1828ms:
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

codex
REQUEST-CHANGES: a synchronous resourceFailed during dispatchRequests() can still drive m_pendingResponses from 1 to 0 before later addons are dispatched, queuing an early streamsReady and possibly duplicate emits for the same generation.
tokens used
34,629
REQUEST-CHANGES: a synchronous resourceFailed during dispatchRequests() can still drive m_pendingResponses from 1 to 0 before later addons are dispatched, queuing an early streamsReady and possibly duplicate emits for the same generation.
