OpenAI Codex v0.131.0
--------
workdir: C:\Users\Suprabha\Desktop\Tankoban 2
model: gpt-5.5
provider: openai
approval: never
sandbox: read-only
reasoning effort: high
reasoning summaries: none
session id: 019e8c99-259f-7843-a479-abd7d697b75f
--------
user
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

codex
I’ll review this as a read-only code path walk against the provided diff. I’m going to focus on counter invariants, queued emission ordering, and stale-generation behavior rather than repo-wide style.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/StreamAggregator.cpp' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/StreamAggregator.h' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg \"load\\(|currentLoadToken|streamsReady\" src -n" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1531ms:
#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QUrl>

#include <memory>

#include "addon/StreamInfo.h"
#include "core/TorrentResult.h"

class QNetworkAccessManager;

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

struct StreamLoadRequest {
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

}

 succeeded in 1832ms:
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
    QStringLiteral("udp://tracker.uw0.xyz:6969/announce"),
    QStringLiteral("udp://tracker.bittor.pw:1337/announce"),
};

const QRegularExpression kHashRe(QStringLiteral("^[a-f0-9]{40}$"));
const QRegularExpression kResRe(QStringLiteral("\\b(2160p|1080p|720p|480p|4[Kk])\\b"));
const QRegularExpression kSrcRe(
    QStringLiteral("\\b(WEB[-\\s]?DL|WEBRip|BluRay|BDRip|BRRip|HDRip|HDTV|DVDRip|PDTV|AMZN|NF)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kHdrRe(
    QStringLiteral("\\b(Dolby\\s*Vision|DV|HDR10\\+?|HDR|SDR|10[Bb]it)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kCodecRe(
    QStringLiteral("\\b(H\\.?265|x265|HEVC|H\\.?264|x264|AV1|VP9)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kAudioRe(
    QStringLiteral("\\b(Atmos|DDP?\\s*5\\.1|TrueHD|DTS[-\\s]?HD|AAC|FLAC|AC3)\\b"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kSeedersRe(QStringLiteral("\\x{1F464}\\s*(\\d+)"));
const QRegularExpression kSizeRe(
    QStringLiteral("\\x{1F4BE}\\s*([\\d.,]+\\s*[KMGT]?i?B)"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kTrackerRe(
    QStringLiteral("\\x{2699}\\x{FE0F}?\\s*(.+)$"),
    QRegularExpression::MultilineOption);
const QRegularExpression kMagnetTrackerRe(
    QStringLiteral("^(tracker|dht):"),
    QRegularExpression::CaseInsensitiveOption);

const QChar kBustHi = QChar(0xD83D);
const QChar kBustLo = QChar(0xDC64);
const QChar kDiskHi = QChar(0xD83D);
const QChar kDiskLo = QChar(0xDCBE);
const QChar kGear = QChar(0x2699);
const QChar kFlagHi = QChar(0xD83C);
constexpr uint kFlagLoMin = 0xDDE0;
constexpr uint kFlagLoMax = 0xDDFF;

bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
{
    return a.resource == b.resource
        && a.type == b.type
        && a.id == b.id
        && a.extra == b.extra;
}

bool containsBust(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i) {
        if (s[i] == kBustHi && s[i + 1] == kBustLo) {
            return true;
        }
    }
    return false;
}

bool containsDisk(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i) {
        if (s[i] == kDiskHi && s[i + 1] == kDiskLo) {
            return true;
        }
    }
    return false;
}

bool containsGear(const QString& s)
{
    return s.contains(kGear);
}

bool containsFlag(const QString& s)
{
    for (int i = 0; i < s.size() - 1; ++i) {
        if (s[i] == kFlagHi) {
            const uint lo = s[i + 1].unicode();
            if (lo >= kFlagLoMin && lo <= kFlagLoMax) {
                return true;
            }
        }
    }
    return false;
}

bool looksLikeFilename(const QString& s)
{
    return s.contains('.') && s.length() <= 300 && !s.startsWith(QStringLiteral("http"));
}

QString extractFilename(const QString& path)
{
    const int slash = path.lastIndexOf('/');
    const int backslash = path.lastIndexOf('\\');
    const int pos = qMax(slash, backslash);
    return pos >= 0 ? path.mid(pos + 1) : path;
}

qint64 parseSize(const QString& sizeStr)
{
    const QString normalized = sizeStr.trimmed().toUpper().replace(',', '.');
    static const QRegularExpression kNumRe(QStringLiteral("^([\\d.]+)\\s*([KMGT]?I?B?)$"));
    const auto m = kNumRe.match(normalized);
    if (!m.hasMatch()) {
        return 0;
    }
    bool ok = false;
    double value = m.captured(1).toDouble(&ok);
    if (!ok || value < 0) {
        return 0;
    }
    const QString unit = m.captured(2);
    if (unit.startsWith('T')) {
        value *= 1099511627776.0;
    } else if (unit.startsWith('G')) {
        value *= 1073741824.0;
    } else if (unit.startsWith('M')) {
        value *= 1048576.0;
    } else if (unit.startsWith('K')) {
        value *= 1024.0;
    }
    return static_cast<qint64>(value);
}

QString parseQuality(const QString& rawTitle)
{
    QStringList tags;
    auto tryMatch = [&](const QRegularExpression& re) {
        const auto m = re.match(rawTitle);
        if (m.hasMatch()) {
            tags.append(m.captured(1));
        }
    };
    tryMatch(kResRe);
    tryMatch(kSrcRe);
    tryMatch(kHdrRe);
    tryMatch(kCodecRe);
    tryMatch(kAudioRe);
    return tags.join(QStringLiteral(" / "));
}

QString stripTrackerPrefix(QString source)
{
    source = source.trimmed();
    source.remove(kMagnetTrackerRe);
    return source.trimmed();
}

void parseBehaviorHints(const QJsonObject& obj, StreamBehaviorHints& out)
{
    out.notWebReady = obj.value(QStringLiteral("notWebReady")).toBool(false);
    out.bingeGroup = obj.value(QStringLiteral("bingeGroup")).toString().trimmed();
    out.filename = obj.value(QStringLiteral("filename")).toString().trimmed();
    out.videoHash = obj.value(QStringLiteral("videoHash")).toString().trimmed();
    if (obj.contains(QStringLiteral("videoSize"))) {
        out.videoSize = static_cast<qint64>(
            obj.value(QStringLiteral("videoSize")).toDouble(0.0));
    }

    for (const QJsonValue& value : obj.value(QStringLiteral("countryWhitelist")).toArray()) {
        const QString country = value.toString().trimmed();
        if (!country.isEmpty()) {
            out.countryWhitelist.append(country);
        }
    }

    const QJsonObject proxyHeaders = obj.value(QStringLiteral("proxyHeaders")).toObject();
    const QJsonObject requestHeaders = proxyHeaders.value(QStringLiteral("request")).toObject();
    for (auto it = requestHeaders.constBegin(); it != requestHeaders.constEnd(); ++it) {
        out.proxyRequestHeaders.insert(it.key(), it.value().toString());
    }
    const QJsonObject responseHeaders = proxyHeaders.value(QStringLiteral("response")).toObject();
    for (auto it = responseHeaders.constBegin(); it != responseHeaders.constEnd(); ++it) {
        out.proxyResponseHeaders.insert(it.key(), it.value().toString());
    }

    static const QSet<QString> kKnown {
        QStringLiteral("notWebReady"),
        QStringLiteral("bingeGroup"),
        QStringLiteral("countryWhitelist"),
        QStringLiteral("proxyHeaders"),
        QStringLiteral("filename"),
        QStringLiteral("videoHash"),
        QStringLiteral("videoSize"),
    };
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!kKnown.contains(it.key())) {
            out.other.insert(it.key(), it.value().toVariant());
        }
    }
}

QList<SubtitleTrack> parseSubtitles(const QJsonArray& subtitlesArray)
{
    QList<SubtitleTrack> tracks;
    for (const QJsonValue& value : subtitlesArray) {
        const QJsonObject subtitleObj = value.toObject();
        const QUrl subtitleUrl(subtitleObj.value(QStringLiteral("url")).toString().trimmed());
        if (!subtitleUrl.isValid()) {
            continue;
        }
        SubtitleTrack track;
        track.url = subtitleUrl;
        track.id = subtitleObj.value(QStringLiteral("id")).toString().trimmed();
        if (track.id.isEmpty()) {
            track.id = subtitleUrl.toString();
        }
        track.lang = subtitleObj.value(QStringLiteral("lang")).toString().trimmed();
        if (track.lang.isEmpty()) {
            track.lang = subtitleObj.value(QStringLiteral("language")).toString().trimmed();
        }
        track.label = subtitleObj.value(QStringLiteral("label")).toString().trimmed();
        if (track.label.isEmpty()) {
            track.label = subtitleObj.value(QStringLiteral("title")).toString().trimmed();
        }
        tracks.append(track);
    }
    return tracks;
}

QStringList parseTrackerSources(const QJsonArray& sourcesArray)
{
    QStringList trackers;
    for (const QJsonValue& value : sourcesArray) {
        const QString source = value.toString().trimmed();
        if (source.isEmpty()) {
            continue;
        }
        if (!source.startsWith(QStringLiteral("tracker:"), Qt::CaseInsensitive)
            && !source.startsWith(QStringLiteral("dht:"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString tracker = stripTrackerPrefix(source);
        if (tracker.isEmpty()) {
            continue;
        }
        if (trackers.contains(tracker, Qt::CaseInsensitive)) {
            continue;
        }
        trackers.append(tracker);
        if (trackers.size() >= kMaxTrackers) {
            break;
        }
    }
    return trackers;
}

bool parseStreamSource(const QJsonObject& streamObj, StreamSource& sourceOut)
{
    const QString infoHash = streamObj.value(QStringLiteral("infoHash"))
                                 .toString().trimmed().toLower();
    if (!infoHash.isEmpty() && kHashRe.match(infoHash).hasMatch()) {
        int fileIdx = -1;
        if (streamObj.contains(QStringLiteral("fileIdx"))) {
            fileIdx = streamObj.value(QStringLiteral("fileIdx")).toInt(-1);
        } else if (streamObj.contains(QStringLiteral("fileIndex"))) {
            fileIdx = streamObj.value(QStringLiteral("fileIndex")).toInt(-1);
        }
        sourceOut = StreamSource::magnetSource(
            infoHash,
            parseTrackerSources(streamObj.value(QStringLiteral("sources")).toArray()),
            fileIdx);
        return true;
    }

    const QString ytId = streamObj.value(QStringLiteral("ytId")).toString().trimmed();
    if (!ytId.isEmpty()) {
        sourceOut = StreamSource::youtubeSource(ytId);
        return true;
    }

    QString urlValue = streamObj.value(QStringLiteral("url")).toString().trimmed();
    if (urlValue.isEmpty()) {
        urlValue = streamObj.value(QStringLiteral("externalUrl")).toString().trimmed();
    }
    if (urlValue.isEmpty()) {
        urlValue = streamObj.value(QStringLiteral("playerFrameUrl")).toString().trimmed();
    }
    const QUrl url(urlValue);
    if (!url.isValid() || url.scheme().isEmpty()) {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
        sourceOut = StreamSource::httpSource(url);
    } else {
        sourceOut = StreamSource::urlSource(url);
    }
    return true;
}

void enrichTorrentioLikeFields(Stream& stream, const QJsonObject& streamObj)
{
    if (stream.source.kind != StreamSource::Kind::Magnet) {
        return;
    }
    if (stream.behaviorHints.bingeGroup.isEmpty()) {
        return;
    }

    const QString rawTitle = streamObj.value(QStringLiteral("title")).toString();
    if (rawTitle.isEmpty()) {
        return;
    }

    const QStringList lines = rawTitle.split('\n');
    QString trackerSource;
    QString tracker;
    QString languages;
    QString fileHintFromTitle;
    int seeders = 0;
    qint64 sizeBytes = 0;

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (i == 0) {
            trackerSource = line;
            continue;
        }
        if (containsBust(line)) {
            const auto m = kSeedersRe.match(line);
            if (m.hasMatch()) {
                seeders = m.captured(1).toInt();
            }
        }
        if (containsDisk(line)) {
            const auto m = kSizeRe.match(line);
            if (m.hasMatch()) {
                sizeBytes = parseSize(m.captured(1));
            }
        }
        if (containsGear(line)) {
            const auto m = kTrackerRe.match(line);
            if (m.hasMatch()) {
                tracker = m.captured(1).trimmed();
            }
        }
        if (containsFlag(line)) {
            languages = line;
        }
        if (fileHintFromTitle.isEmpty() && looksLikeFilename(line)) {
            fileHintFromTitle = extractFilename(line);
        }
    }

    const QString quality = parseQuality(rawTitle);
    if (!quality.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("qualityLabel"), quality);
    }
    if (!trackerSource.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("trackerSource"), trackerSource);
    }
    if (!tracker.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("tracker"), tracker);
    }
    if (!languages.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("languages"), languages);
    }
    if (seeders > 0) {
        stream.behaviorHints.other.insert(QStringLiteral("seeders"), seeders);
    }
    if (sizeBytes > 0) {
        stream.behaviorHints.other.insert(QStringLiteral("sizeBytes"),
                                          QVariant::fromValue<qint64>(sizeBytes));
    }

    if (!fileHintFromTitle.isEmpty() && stream.source.fileNameHint.isEmpty()) {
        stream.source.fileNameHint = fileHintFromTitle;
    }
    if (!stream.behaviorHints.filename.isEmpty()) {
        stream.source.fileNameHint = stream.behaviorHints.filename;
    }

    // Stream-picker UX rework â€” preserve the parsed filename as a UI-readable
    // field. Stremio-style source cards render this as the primary line under
    // the addon name; without it the table showed "Torrentio..." for every
    // Torrentio row because neither stream.name nor stream.description carry
    // anything useful per Torrentio payload. Mirror into behaviorHints.filename
    // when empty so Stremio-compliant addons that populate `filename` directly
    // and Torrentio-style addons that embed it in `title` look identical to
    // the card layer.
    if (!fileHintFromTitle.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("parsedFilename"),
                                          fileHintFromTitle);
        if (stream.behaviorHints.filename.isEmpty()) {
            stream.behaviorHints.filename = fileHintFromTitle;
        }
    }

    if (stream.source.trackers.isEmpty()) {
        stream.source.trackers = kFallbackTrackers.mid(0, kMaxTrackers);
    }
}

QString streamIdentityKey(const Stream& stream)
{
    switch (stream.source.kind) {
    case StreamSource::Kind::Magnet:
        return QStringLiteral("magnet|%1|%2|%3")
            .arg(stream.source.infoHash.toLower())
            .arg(stream.source.fileIndex)
            .arg(stream.source.fileNameHint);
    case StreamSource::Kind::Http:
        return QStringLiteral("http|%1").arg(stream.source.url.toString(QUrl::FullyEncoded));
    case StreamSource::Kind::Url:
        return QStringLiteral("url|%1").arg(stream.source.url.toString(QUrl::FullyEncoded));
    case StreamSource::Kind::YouTube:
        return QStringLiteral("yt|%1").arg(stream.source.youtubeId);
    }
    return {};
}

bool parseStreamRow(const QJsonObject& streamObj, Stream& out)
{
    StreamSource source;
    if (!parseStreamSource(streamObj, source)) {
        return false;
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

    // DOWNLOAD BUG 2026-06-03 (review fix) â€” count EVERY addon we are about to
    // dispatch BEFORE firing any request. AddonTransport::fetchResource() can
    // emit resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon), so
    // incrementing m_pendingResponses one-at-a-time inside the dispatch loop let
    // an early addon's synchronous failure drive the counter to 0 before later
    // addons were dispatched â€” firing a premature streamsReady (empty), and then
    // a SECOND one when the rest completed. Pre-counting makes the terminal emit
    // fire exactly once, after the last completion. Iterating a separate id list
    // (not the map) also avoids any mutate-during-iteration hazard if a
    // synchronous reply touches m_pendingByAddon.
    QList<QString> toDispatch;
    for (auto it = m_pendingByAddon.begin(); it != m_pendingByAddon.end(); ++it) {
        if (it.value().inFlight) continue;
        it.value().inFlight = true;
        toDispatch.append(it.key());
    }
    if (toDispatch.isEmpty()) return;
    m_pendingResponses += toDispatch.size();

    for (const QString& addonId : toDispatch) {
        auto addonIt = m_pendingByAddon.find(addonId);
        if (addonIt == m_pendingByAddon.end()) continue;  // defensive
        PendingAddon& addon = addonIt.value();

        ResourceRequest req;
        req.resource = QStringLiteral("stream");
        req.type = m_request.type;
        req.id = m_request.id;
        req.extra = m_request.extra;

        auto* worker = new AddonTransport(this);
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
{
    QString imdbId;
    int     season = 0;
    int     outstanding = 0;
    QList<TorrentResult> results;
    QSet<QString>        seenInfoHashes;
    QTimer* timeout = nullptr;
    bool    emitted = false;
    // TANKORENT audit DEFECT 2 (2026-05-28) â€” monotonic search epoch. Each
    // searchPacks() call bumps StreamAggregator::m_packEpoch and stamps it
    // here. finalizePackSearch suppresses any ctx whose epoch is no longer
    // current, so a superseded search (e.g. "All Sources" still in flight
    // when the user switches to "Nyaa") cannot emit stale results that would
    // bleed into the new search â€” its imdbId/season match the new search so
    // the downstream imdbId/season guards alone would NOT catch it.
    quint64 epoch = 0;
};

namespace {

constexpr int kPackSearchTimeoutMs = 30 * 1000;
constexpr int kPackSearchPerIndexerLimit = 25;
// THEATRE_ANIME_CATALOG â€” anime batch search needs a wider net (big batch
// torrents bury individual titles); lift the per-indexer cap for that path.
constexpr int kAnimeBatchPerIndexerLimit = 100;

bool packSearchIndexerEnabled(const QString& id)
{
    QSettings settings;
    return settings.value(
        QStringLiteral("tankorent/indexers/%1/enabled").arg(id), true).toBool();
}

}  // namespace

void StreamAggregator::searchPacks(const QString& imdbId,
                                   const QString& showName,
                                   int season,
                                   const QString& sourceFilter,
                                   bool anime)
{
    if (!m_packNam) {
        m_packNam = tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-pack-search"));
    }

    // THEATRE_ANIME_CATALOG â€” anime is torrented as big multi-episode batches,
    // never "Season N", and needs a wider net than the 25-cap. Broaden both.
    const int perIndexerLimit =
        anime ? kAnimeBatchPerIndexerLimit : kPackSearchPerIndexerLimit;

    QStringList queries;
    if (anime) {
        queries = buildAnimePackQueries(showName);
    } else if (season > 0) {
        queries << QStringLiteral("%1 S%2")
                       .arg(showName)
                       .arg(season, 2, 10, QLatin1Char('0'));
        queries << QStringLiteral("%1 Season %2").arg(showName).arg(season);
    } else {
        queries << QStringLiteral("%1 Complete").arg(showName);
        queries << QStringLiteral("%1 Complete Series").arg(showName);
    }

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1 post-review fix) -
    // shared_ptr ownership so a slow indexer responding after the 30s timeout
    // can no-op cleanly on the ctx->emitted early-out instead of UB-reading
    // freed memory. Every lambda below captures ctx BY VALUE (shared_ptr copy
    // bumps refcount); the context dies when the last lambda is destroyed.
    auto ctx = std::make_shared<PackSearchContext>();
    ctx->imdbId = imdbId;
    ctx->season = season;
    ctx->epoch  = ++m_packEpoch;  // DEFECT 2 â€” supersede any prior in-flight pack search

    QPointer<StreamAggregator> self(this);

    // Explicit-only capture (post-review I1 fix): ctx is a shared_ptr by value,
    // self is a QPointer by value, this is needed for connect()'s receiver
    // argument and for indexer instantiation. No `&` capture - searchPacks's
    // stack frame is gone by the time async indexer callbacks fire.
    auto dispatch = [ctx, self, this, perIndexerLimit](const QString& id,
                                      TorrentIndexer* indexer, const QString& query) {
        if (!packSearchIndexerEnabled(id)) {
            indexer->deleteLater();
            return;
        }
        ++ctx->outstanding;
        QPointer<TorrentIndexer> idxPtr(indexer);
        connect(indexer, &TorrentIndexer::searchFinished, this,
                [self, ctx, idxPtr](const QList<TorrentResult>& results) {
                    if (!self || ctx->emitted) {
                        if (idxPtr) idxPtr->deleteLater();
                        return;
                    }
                    for (const TorrentResult& r : results) {
                        const QString key = r.infoHash.isEmpty()
                            ? QStringLiteral("magnet::") + r.magnetUri
                            : r.infoHash;
                        if (ctx->seenInfoHashes.contains(key))
                            continue;
                        ctx->seenInfoHashes.insert(key);
                        ctx->results.append(r);
                    }
                    if (idxPtr) idxPtr->deleteLater();
                    if (--ctx->outstanding <= 0) {
                        self->finalizePackSearch(ctx);
                    }
                });
        connect(indexer, &TorrentIndexer::searchError, this,
                [self, ctx, idxPtr](const QString&) {
                    if (!self || ctx->emitted) {
                        if (idxPtr) idxPtr->deleteLater();
                        return;
                    }
                    if (idxPtr) idxPtr->deleteLater();
                    if (--ctx->outstanding <= 0) {
                        self->finalizePackSearch(ctx);
                    }
                });
        indexer->search(query, perIndexerLimit);
    };

    // THEATRE_SOURCE_PICKER 2026-05-17: gate each indexer by sourceFilter.
    // "all" (default) preserves the existing fan-out shape. A specific id
    // skips siblings, letting the Theatre source-combo UI route to a single
    // indexer (e.g. "nyaa" for anime).
    auto wants = [&](const QString& id) {
        return sourceFilter == QStringLiteral("all") || sourceFilter == id;
    };
    for (const QString& query : queries) {
        if (wants(QStringLiteral("nyaa")))
            dispatch(QStringLiteral("nyaa"),
                     new NyaaIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("piratebay")))
            dispatch(QStringLiteral("piratebay"),
                     new PirateBayIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("1337x")))
            dispatch(QStringLiteral("1337x"),
                     new X1337xIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("yts")))
            dispatch(QStringLiteral("yts"),
                     new YtsIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("eztv")))
            dispatch(QStringLiteral("eztv"),
                     new EztvIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("exttorrents")))
            dispatch(QStringLiteral("exttorrents"),
                     new ExtTorrentsIndexer(m_packNam, this), query);
    }

    if (ctx->outstanding == 0) {
        // No enabled indexers; emit empty result on the next event-loop tick
        // so listeners that connect() right after this call still receive it.
        QTimer::singleShot(0, this, [self, ctx]() {
            if (self) self->finalizePackSearch(ctx);
            // shared_ptr drops automatically if !self; no manual delete needed.
        });
        return;
    }

    ctx->timeout = new QTimer(this);
    ctx->timeout->setSingleShot(true);
    connect(ctx->timeout, &QTimer::timeout, this, [self, ctx]() {
        if (self && !ctx->emitted) {
            self->finalizePackSearch(ctx);
        }
    });
    ctx->timeout->start(kPackSearchTimeoutMs);
}

void StreamAggregator::finalizePackSearch(std::shared_ptr<PackSearchContext> ctx)
{
    if (!ctx || ctx->emitted) {
        return;
    }
    // DEFECT 2 (2026-05-28) â€” suppress superseded searches. If a newer
    // searchPacks() has bumped m_packEpoch since this ctx was created, its
    // results are stale (e.g. the old "All Sources" fan-out finishing after
    // the user switched to "Nyaa"). Mark emitted so captured lambdas no-op,
    // but do NOT emit packsAvailable â€” that would bleed old-source rows into
    // the current search.
    if (ctx->epoch != m_packEpoch) {
        ctx->emitted = true;
        if (ctx->timeout) {
            ctx->timeout->stop();
            ctx->timeout->deleteLater();
            ctx->timeout = nullptr;
        }
        return;
    }
    ctx->emitted = true;
    if (ctx->timeout) {
        ctx->timeout->stop();
        ctx->timeout->deleteLater();
        ctx->timeout = nullptr;
    }
    emit packsAvailable(ctx->imdbId, ctx->season, ctx->results);
    // No manual delete: shared_ptr destroys the context when the last lambda
    // (and this local) goes out of scope. Stale post-timeout callbacks keep
    // ctx alive but hit the ctx->emitted early-out cleanly.
}

}

 succeeded in 2163ms:
src\core\CoreBridge.cpp:213:                m_unifiedProgress->episodePayload(id.imdbId, id.season, id.episode);
src\core\CoreBridge.cpp:246:            m_unifiedProgress->setEpisodePayload(id.imdbId, id.season, id.episode, entry);
src\core\CoreBridge.cpp:253:            m_unifiedProgress->setPathPayload(path, entry, itemId);
src\core\book\AbbScraper.cpp:324:void AbbScraper::resolveDownload(const QString& md5OrId)
src\core\book\AbbScraper.h:63:    void resolveDownload(const QString& md5OrId) override;
src\core\book\AnnaArchiveScraper.h:51:    void resolveDownload(const QString& md5OrId) override;
src\core\book\AnnaArchiveScraper.cpp:728:    m_view->load(target);
src\core\book\AnnaArchiveScraper.cpp:846:    m_view->load(target);
src\core\book\AnnaArchiveScraper.cpp:968:void AnnaArchiveScraper::resolveDownload(const QString& md5OrId)
src\core\book\AnnaArchiveScraper.cpp:1002:    m_view->load(target);
src\core\book\AnnaArchiveScraper.cpp:1116:void AnnaArchiveScraper::resolveDownload(const QString& md5OrId)
src\core\book\BookResult.h:42:    QString downloadUrl;  // resolved direct URL — only set by resolveDownload()
src\core\book\BookSeriesIndex.cpp:109:void BookSeriesIndex::load(const QString& bundledResourcePath)
src\core\book\BookDownloader.h:54:    // null, startMagnetDownload() emits downloadFailed immediately with a clear
src\core\book\BookDownloader.h:67:    QString startDownload(const QString& md5,
src\core\book\BookDownloader.h:87:    QString startMagnetDownload(const QString& magnetUri,
src\core\book\BookDownloader.h:92:    void cancelDownload(const QString& md5);
src\core\book\BookDownloader.cpp:120:QString BookDownloader::startMagnetDownload(const QString& magnetUri,
src\core\book\BookDownloader.cpp:400:        startMagnetDownload(next.magnetUri,
src\core\book\BookDownloader.cpp:465:QString BookDownloader::startDownload(const QString& md5,
src\core\book\BookDownloader.cpp:511:void BookDownloader::cancelDownload(const QString& md5)
src\core\indexers\CloudflareCookieHarvester.cpp:161:    m_view->load(target);
src\core\book\BookScraper.h:14://  - resolveDownload(md5OrId) resolves one or more direct HTTP URLs via
src\core\book\BookScraper.h:41:    virtual void resolveDownload(const QString& md5OrId) = 0;
src\core\book\BooksCatalogueLibraryStore.h:82:    void load();
src\core\book\BooksCatalogueLibraryStore.cpp:17:    // Lazy load so tests can construct before calling load().
src\core\book\BooksCatalogueLibraryStore.cpp:196:void BooksCatalogueLibraryStore::load()
src\core\book\TankorentBookScraper.h:36:    void resolveDownload(const QString& torrentId) override;
src\core\book\TankorentBookScraper.h:48:    // so resolveDownload(magnetUri) can emit downloadResolved synchronously
src\core\book\TankorentBookScraper.cpp:136:void TankorentBookScraper::resolveDownload(const QString& torrentId)
src\core\book\TankorentBookScraper.cpp:140:    // BookDownloader picks it up via startMagnetDownload(magnetUri, ...).
src\devtools\HangWatchdog.cpp:26:    if (m_run.load(std::memory_order_relaxed))
src\devtools\HangWatchdog.cpp:79:    while (m_run.load(std::memory_order_relaxed)) {
src\devtools\HangWatchdog.cpp:81:        if (!m_run.load(std::memory_order_relaxed))
src\devtools\HangWatchdog.cpp:85:        const qint64 last = m_lastBeatMs.load(std::memory_order_relaxed);
src\devtools\HangWatchdog.cpp:88:        if (age > m_thresholdMs && !m_hangActive.load(std::memory_order_relaxed)) {
src\devtools\HangWatchdog.cpp:104:        } else if (age <= m_thresholdMs && m_hangActive.load(std::memory_order_relaxed)) {
src\core\book\LibGenScraper.h:57:    void resolveDownload(const QString& md5OrId) override;
src\core\book\BookSeriesIndex.h:16:// Load order (load()): a refreshed copy in the data dir if present + schema-
src\core\book\BookSeriesIndex.h:29:    void load(const QString& bundledResourcePath);
src\core\PosterFetcher.h:18://   PosterFetcher::download(m_nam, url, "/path/to/out.jpg", this,
src\core\PosterFetcher.h:29:    static void download(QNetworkAccessManager* nam,
src\core\book\LibGenScraper.cpp:389:void LibGenScraper::resolveDownload(const QString& md5OrId)
src\core\JsonStore.cpp:68:    while (!m_shutdown.load(std::memory_order_acquire)) {
src\core\PosterFetcher.cpp:27:void PosterFetcher::download(QNetworkAccessManager* nam,
src\core\stream\CLAUDE.md:56:- `feedback_stream_failed_hypotheses.md` — do NOT retry: request_queue_time 10→3 + setSequentialDownload(false) both regressed
src\core\torrent\TorrentRepository.h:99:    bool upsertStreamDownload(const StreamDownloadRow& row);
src\core\torrent\TorrentRepository.h:100:    bool removeStreamDownload(const QString& canonicalPath);
src\core\torrent\TorrentRepository.h:101:    std::optional<StreamDownloadRow> getStreamDownload(const QString& canonicalPath);
src\core\stream\addon\AddonRegistry.h:50:    void load();
src\core\stream\CatalogAggregator.h:35:    void load(const CatalogQuery& query);
src\core\stream\addon\AddonRegistry.cpp:297:    load();
src\core\stream\addon\AddonRegistry.cpp:530:void AddonRegistry::load()
src\core\stream\addon\AddonRegistry.cpp:764:    // a load-bearing default. Schema v4 reseed migration in load() picks
src\core\stream\UnifiedProgressStore.h:32:    void setEpisodePayload(const QString& imdbId, int season, int episode,
src\core\stream\UnifiedProgressStore.h:34:    QJsonObject episodePayload(const QString& imdbId, int season, int episode) const;
src\core\stream\UnifiedProgressStore.h:37:    void setPathPayload(const QString& canonicalPath, const QJsonObject& payload,
src\core\stream\UnifiedProgressStore.h:39:    QJsonObject pathPayload(const QString& canonicalPath) const;
src\core\stream\UnifiedProgressStore.h:61:    void load();
src\core\stream\UnifiedProgressStore.h:65:    static QJsonObject normalizedPayload(const QJsonObject& payload);
src\core\torrent\TorrentRepository.cpp:947:bool TorrentRepository::upsertStreamDownload(const StreamDownloadRow& row) {
src\core\torrent\TorrentRepository.cpp:994:bool TorrentRepository::removeStreamDownload(const QString& canonicalPath) {
src\core\torrent\TorrentRepository.cpp:1007:std::optional<StreamDownloadRow> TorrentRepository::getStreamDownload(const QString& canonicalPath) {
src\core\ScannerUtils.h:18:    bool isCancelled() const { return cancelled.load(std::memory_order_acquire); }
src\core\stream\CatalogAggregator.cpp:98:void CatalogAggregator::load(const CatalogQuery& query)
src\ui\pages\VideosPage.cpp:1030:                PosterFetcher::download(nam, poster, destPath, selfGuard,
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
src\core\torrent\TorrentEngine.h:115:    void    setSequentialDownload(const QString& infoHash, bool sequential);
src\core\stream\StreamLibrary.cpp:14:    load();
src\core\stream\StreamLibrary.cpp:148:void StreamLibrary::load()
src\core\stream\BulkSourceCollector.cpp:144:        aggregator, &StreamAggregator::streamsReady, this,
src\core\stream\BulkSourceCollector.cpp:166:    aggregator->load(request);
src\core\stream\StreamDownloadIndex.h:50:    // Triggers load() to rebuild the in-memory maps from the repo. Until
src\core\stream\StreamDownloadIndex.h:129:    // the final Complete entry after the file moves to its library path (load()
src\core\stream\StreamDownloadIndex.h:155:    void load();
src\core\torrent\TorrentEngine.cpp:265:        // crashes before startDownload(), the .fastresume lingers with no
src\core\torrent\TorrentEngine.cpp:797:void TorrentEngine::setSequentialDownload(const QString& infoHash, bool sequential)
src\core\torrent\TorrentEngine.cpp:1827:void TorrentEngine::setSequentialDownload(const QString&, bool) {}
src\core\stream\StreamRescueScanner.cpp:57:        if (cancelled.load()) return out;
src\core\stream\StreamRescueScanner.cpp:65:            if (cancelled.load()) return out;
src\core\stream\StreamRescueScanner.cpp:74:                if (cancelled.load()) return out;
src\core\stream\StreamRescueScanner.cpp:133:            if (m_cancelled.load()) break;
src\core\stream\StreamRescueScanner.cpp:154:            if (m_cancelled.load()) break;
src\core\stream\StreamRescueScanner.cpp:203:            if (!searchDone.load()) {
src\core\stream\StreamRescueScanner.cpp:291:        if (!m_cancelled.load() && m_metaStore) {
src\core\stream\StreamRescueScanner.cpp:313:                    .arg(m_cancelled.load()).arg(anyShowProcessed));
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
src\core\torrent\TorrentClient.h:164:    void    startDownload(const QString& infoHash, const AddTorrentConfig& config);
src\core\stream\UnifiedPackSearchEngine.cpp:37:        // streamsReady (Stremio) subscription was removed; addons are now
src\core\stream\StreamLibrary.h:65:    void load();
src\core\torrent\TorrentClient.cpp:818:            startDownload(transferId, *cfg);
src\core\torrent\TorrentClient.cpp:1381:        startDownload(infoHash, config);
src\core\torrent\TorrentClient.cpp:1415:        startDownload(infoHash, config);
src\core\torrent\TorrentClient.cpp:2832:    // confirms via startDownload(). This prevents ghost entries if the app
src\core\torrent\TorrentClient.cpp:2856:    startDownload(hash, cfg);
src\core\torrent\TorrentClient.cpp:2860:void TorrentClient::startDownload(const QString& infoHash, const AddTorrentConfig& config)
src\core\torrent\TorrentClient.cpp:2963:        m_engine->setSequentialDownload(hash, true);
src\core\stream\SubtitlesAggregator.h:35:    void load(const SubtitleLoadRequest& request);
src\core\torrent\CLAUDE.md:55:- `feedback_stream_failed_hypotheses.md` — do NOT retry: request_queue_time 10→3 + setSequentialDownload(false) both regressed empirically
src\core\stream\StreamAggregator.h:39:    // DOWNLOAD BUG 2026-06-02 — load() now returns the monotonic generation
src\core\stream\StreamAggregator.h:40:    // token it stamped (m_loadGeneration, bumped inside load() right after
src\core\stream\StreamAggregator.h:41:    // reset()). Callers that arm a one-shot on the SHARED streamsReady signal
src\core\stream\StreamAggregator.h:42:    // capture this token and gate their handler on currentLoadToken()==token,
src\core\stream\StreamAggregator.h:43:    // so a late streamsReady from a SUPERSEDED load() (rapid re-clicks) cannot
src\core\stream\StreamAggregator.h:45:    quint64 load(const StreamLoadRequest& request);
src\core\stream\StreamAggregator.h:47:    // Generation of the most recent load() — see load() above. Used by
src\core\stream\StreamAggregator.h:50:    quint64 currentLoadToken() const { return m_loadGeneration; }
src\core\stream\StreamAggregator.h:66:    // existing Stream-flavored load()).
src\core\stream\StreamAggregator.h:78:    void streamsReady(const QList<tankostream::addon::Stream>& streams,
src\core\stream\StreamAggregator.h:105:    // streamsReady. Always queues the emit to the NEXT event-loop turn rather
src\core\stream\StreamAggregator.h:106:    // than firing synchronously, and drops it if a newer load() has superseded
src\core\stream\StreamAggregator.h:107:    // `generation`. Two reasons it must never fire synchronously inside load():
src\core\stream\StreamAggregator.h:108:    //   (1) Callers arm their one-shot before load() returns and fill the
src\core\stream\StreamAggregator.h:109:    //       correlation token FROM load()'s return value; a synchronous emit
src\core\stream\StreamAggregator.h:112:    //   (2) dispatchRequests() runs inside load(), and AddonTransport can emit
src\core\stream\StreamAggregator.h:114:    //       so completeOne() could otherwise reach this emit before load()
src\core\stream\StreamAggregator.h:117:    // so a subsequent reset()/load() can't mutate them out from under the emit.
src\core\stream\StreamAggregator.h:133:    // load() (right after reset()) and returned to the caller so a one-shot
src\core\stream\StreamAggregator.h:134:    // streamsReady handler can correlate the emit back to ITS request and
src\core\stream\StreamAggregator.h:135:    // ignore stale emits from a superseded load(). Defends against the rapid
src\core\stream\StreamAggregator.h:136:    // re-click race where a late streamsReady from an earlier load() fires the
src\core\stream\StreamAggregator.cpp:507:quint64 StreamAggregator::load(const StreamLoadRequest& request)
src\core\stream\StreamAggregator.cpp:512:    // gate its one-shot streamsReady handler against currentLoadToken() so a
src\core\stream\StreamAggregator.cpp:513:    // late emit from a SUPERSEDED load() (rapid re-clicks) is discarded.
src\core\stream\StreamAggregator.cpp:555:                return;  // superseded by a newer load() — drop the stale emit
src\core\stream\StreamAggregator.cpp:556:            emit streamsReady(streams, addonsById);
src\core\stream\StreamAggregator.cpp:564:    // belongs to. A reply from a worker launched by a SUPERSEDED load() (rapid
src\core\stream\StreamAggregator.cpp:568:    // streamsReady that the handler-side token gate would then wrongly accept
src\core\stream\StreamAggregator.cpp:579:    // addons were dispatched — firing a premature streamsReady (empty), and then
src\core\stream\StreamAggregator.cpp:702:    // a superseded load() (reset() zeroed m_pendingResponses mid-flight) could
src\core\stream\StreamAggregator.cpp:703:    // otherwise drive the counter negative and re-emit streamsReady against the
src\core\stream\StreamAggregator.cpp:715:    // can't fire streamsReady inside load() before the caller stores its token.
src\core\torrent\LegacyImporter.cpp:668:        if (repo.upsertStreamDownload(d)) {
src\core\stream\SubtitlesAggregator.cpp:120:void SubtitlesAggregator::load(const SubtitleLoadRequest& request)
src\ui\pages\BooksPage.cpp:77:    m_catalogueStore->load();
src\ui\pages\BooksPage.cpp:1923:        handle = m_bookDownloader->startMagnetDownload(
src\ui\pages\BooksPage.cpp:1927:        // from scraper->resolveDownload(); BookDownloader walks them for
src\ui\pages\BooksPage.cpp:1934:        handle = m_bookDownloader->startDownload(
src\ui\pages\ComicsPage.cpp:1111:        m_westernDownloadRecordId = m_mangaDownloader->startDownload(
src\ui\pages\StreamPage.h:119:    QJsonObject devDirectDownload(int sourceIndex);             // A4S3
src\ui\pages\StreamPage.h:270:    void beginPlayOrDownload(const QString& imdbId, const QString& mediaType,
src\ui\pages\StreamPage.h:297:    void triggerBulkSeasonDownload(int season);
src\ui\pages\StreamPage.h:334:        // StreamAggregator::load(). The one-shot streamsReady handler gates on
src\ui\pages\StreamPage.h:335:        // currentLoadToken()==token so a late emit from a SUPERSEDED load()
src\ui\pages\StreamPage.h:341:    void startAutoDownload(const QString& imdbId, const QString& mediaType,
src\ui\pages\StreamPage.h:362:    void retryBulkSeasonDownload(const QString& groupId, const QStringList& itemKeys);
src\ui\pages\StreamPage.h:363:    void cancelBulkSeasonDownload();
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
src\ui\pages\StreamPage.cpp:2337:            if (m_streamAggregator->currentLoadToken() != *playToken)
src\ui\pages\StreamPage.cpp:2339:            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:2425:    // one-shot above so it can discard a stale emit from a superseded load().
src\ui\pages\StreamPage.cpp:2426:    *playToken = m_streamAggregator->load(req);
src\ui\pages\StreamPage.cpp:2510:            // Reuse m_streamAggregator â€” the current episode's streamsReady
src\ui\pages\StreamPage.cpp:2512:            // load() resets internal state cleanly.
src\ui\pages\StreamPage.cpp:2514:                       &tankostream::stream::StreamAggregator::streamsReady,
src\ui\pages\StreamPage.cpp:2518:            // its load(), so the token rides in a shared_ptr filled from the
src\ui\pages\StreamPage.cpp:2519:            // load() return below; a stale emit from a superseded load() is
src\ui\pages\StreamPage.cpp:2523:                    &tankostream::stream::StreamAggregator::streamsReady, this,
src\ui\pages\StreamPage.cpp:2526:                    if (m_streamAggregator->currentLoadToken() != *prefetchToken)
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
src\ui\pages\StreamPage.cpp:3305:            if (m_streamAggregator->currentLoadToken() != m_pendingAuto.token)
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
src\ui\pages\TankorentPage.cpp:2154:        // TorrentClient::startDownload().
src\ui\pages\TankorentPage.cpp:2156:        m_client->startDownload(hash, config);
src\ui\pages\TankorentPage.cpp:2815:        m_client->engine()->setSequentialDownload(firstHash, on);
src\ui\pages\TankorentPage.cpp:3068:        m_client->startDownload(hash, config);
src\ui\pages\books\BookCatalogueDetailView.h:55:    // scraper->resolveDownload() which returns multiple mirrors (LibGen
src\ui\pages\books\BookCatalogueDetailView.h:147:    // need a scraper->resolveDownload(md5) call to get a fresh /get.php?key=XXX
src\ui\pages\books\BookCatalogueDetailView.cpp:552:    // from scraper->resolveDownload(md5). LibGen's key rotates ~60s so this
src\ui\pages\books\BookCatalogueDetailView.cpp:881:    scraper->resolveDownload(result.md5);
src\ui\dialogs\AddTorrentDialog.h:28:    // the magnet never reaches libtorrent. startDownload() now self-defends by
src\ui\MainWindow.cpp:796:    // triggers a one-time load() that rebuilds the in-memory byPath/byEpisode/
src\ui\MainWindow.cpp:804:    // load() here then CLEARS those in-memory registrations and restores only the
src\ui\MainWindow.cpp:807:    // NotDownloaded. Repo-first → load() restores prior state, THEN reconcile
src\ui\MainWindow.cpp:817:    // registerEpisode calls both persist and survive any later load().
src\ui\pages\comics\ComicsSourcesPanel.h:131:    void emitTopRowDownload();
src\ui\pages\comics\ComicsSourcesPanel.h:132:    void emitRowDownload(const UnifiedSourceRow& row);
src\ui\pages\comics\ComicsSeriesView.h:181:    void deleteVolumeDownload(int volumeNumber, const QString& cbzPath);
src\ui\pages\comics\ComicsSourcesPanel.cpp:349:    emitRowDownload(m_rows.at(rowIndex));
src\ui\pages\comics\ComicsSourcesPanel.cpp:486:            emitRowDownload(clickedRow);
src\ui\pages\comics\ComicsSourcesPanel.cpp:492:            emitRowDownload(clickedRow);
src\ui\pages\comics\ComicsSourcesPanel.cpp:597:void ComicsSourcesPanel::emitTopRowDownload()
src\ui\pages\comics\ComicsSourcesPanel.cpp:610:    emitRowDownload(row);
src\ui\pages\comics\ComicsSourcesPanel.cpp:613:void ComicsSourcesPanel::emitRowDownload(const UnifiedSourceRow& row)
src\ui\pages\comics\ComicsSeriesView.cpp:2201:        if (pm.load(localPath)) {
src\ui\pages\comics\ComicsSeriesView.cpp:2833:    if (chosen == del)         deleteVolumeDownload(tile->volumeNumber(), st.cbzPath);
src\ui\pages\comics\ComicsSeriesView.cpp:2839:void ComicsSeriesView::deleteVolumeDownload(int volumeNumber, const QString& cbzPath)
src\ui\pages\stream\CatalogBrowseScreen.h:75:    void reload();
src\ui\pages\TankoLibraryPage.cpp:1199:    // scaffold-button footprint. Click kicks off scraper.resolveDownload(),
src\ui\pages\TankoLibraryPage.cpp:1434:            if (pix.load(cached) && !pix.isNull()) {
src\ui\pages\TankoLibraryPage.cpp:1796:        if (pix.load(cachedPath) && !pix.isNull()) {
src\ui\pages\TankoLibraryPage.cpp:1873:        if (m_downloader) m_downloader->cancelDownload(m_selectedResult.md5);
src\ui\pages\TankoLibraryPage.cpp:1937:    scraper->resolveDownload(resolveId);
src\ui\pages\TankoLibraryPage.cpp:1946:    // BOTH fields — the slug we passed in resolveDownload() and the info
src\ui\pages\TankoLibraryPage.cpp:2021:        m_torrentClient->startDownload(hash, config);
src\ui\pages\TankoLibraryPage.cpp:2105:    m_downloader->startDownload(md5, urls, destDir, suggestedName, expectedBytes);
src\ui\pages\stream\CatalogBrowseScreen.cpp:379:                reload();
src\ui\pages\stream\CatalogBrowseScreen.cpp:472:        state->aggregator->load(q);
src\ui\pages\stream\CatalogBrowseScreen.cpp:490:    reload();
src\ui\pages\stream\CatalogBrowseScreen.cpp:567:void CatalogBrowseScreen::reload()
src\ui\pages\stream\CatalogBrowseScreen.cpp:589:    m_aggregator->load(q);
src\ui\pages\stream\CatalogBrowseScreen.cpp:653:        PosterFetcher::download(m_nam, requestUrl, path, this,
src\ui\pages\stream\CatalogBrowseScreen.cpp:681:                    startDownload();
src\ui\pages\stream\CatalogBrowseScreen.cpp:693:    startDownload();
src\ui\player\FfmpegDecoder.cpp:138:    if (m_paused.load()) play(); else pause();
src\ui\player\FfmpegDecoder.cpp:153:    if (m_paused.load()) {
src\ui\player\FfmpegDecoder.cpp:191:    for (int i = 0; i < 200 && !m_clock->hasStarted() && !m_stop.load(); ++i)
src\ui\player\FfmpegDecoder.cpp:194:    while (!m_stop.load()) {
src\ui\player\FfmpegDecoder.cpp:198:            while (m_paused.load() && !m_stop.load())
src\ui\player\FfmpegDecoder.cpp:201:        if (m_stop.load()) break;
src\ui\player\FfmpegDecoder.cpp:228:            if (m_stop.load()) break;
src\ui\player\FfmpegDecoder.cpp:241:                    if (m_stop.load()) break;
src\ui\pages\stream\StreamDownloadsPage.cpp:284:    if (QFile::exists(path) && pm.load(path)) {
src\ui\pages\stream\StreamDetailView.h:71:    // episode (or movie opens); `Sources` when StreamAggregator::streamsReady
src\ui\pages\stream\StreamDetailView.cpp:271:        // emit playRequested(movie) because StreamPage's streamsReady path calls
src\ui\pages\PosterPickerPopover.cpp:240:    PosterFetcher::download(nam, url, path, this,
src\ui\player\KeyBindings.cpp:91:    load();
src\ui\player\KeyBindings.cpp:104:void KeyBindings::load()
src\ui\player\AudioDecoder.cpp:117:    if (m_paused.load()) {
src\ui\player\AudioDecoder.cpp:176:    while (!m_stop.load()) {
src\ui\player\AudioDecoder.cpp:180:            while (m_paused.load() && !m_stop.load()) {
src\ui\player\AudioDecoder.cpp:184:        if (m_stop.load()) break;
src\ui\player\AudioDecoder.cpp:219:            if (m_stop.load()) break;
src\ui\player\AudioDecoder.cpp:237:            float vol = m_volume.load();
src\ui\player\AudioDecoder.cpp:251:            while (samplesWritten < converted && !m_stop.load()) {
src\ui\player\AudioDecoder.cpp:256:                    if (m_stop.load()) break;
src\ui\player\AudioDecoder.cpp:259:                if (m_stop.load()) break;
src\ui\player\KeyBindings.h:28:    void load();
src\ui\player\FrameCanvas.cpp:601:    while (!m_waitableStop.load(std::memory_order_relaxed)) {
src\ui\player\FrameCanvas.cpp:605:        if (m_waitableStop.load(std::memory_order_relaxed)) break;
src\core\manga\TorrentRequestLedger.h:78:    void load();
src\ui\player\OverlayShmReader.cpp:87:    f.counter = counter->load(std::memory_order_acquire);
src\core\manga\TorrentRequestLedger.cpp:98:    load();
src\core\manga\TorrentRequestLedger.cpp:103:void TorrentRequestLedger::load()
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
src\ui\player\SidecarProcess.cpp:1174:    snap["nextSeq"]          = m_seq.load();
src\core\manga\MangaDownloadIndex.h:129:    void load();
src\core\manga\MangaDownloader.cpp:145:QString MangaDownloader::startDownload(const QString& seriesTitle, const QString& source,
src\core\manga\MangaDownloader.cpp:1107:void MangaDownloader::cancelDownload(const QString& id)
src\core\manga\MangaDownloader.cpp:1157:void MangaDownloader::removeDownload(const QString& id)
src\ui\player\VideoPlayer.cpp:576:    // pick triggers a fresh load() that emits subtitlesReady with the new
src\core\manga\MangaDownloadIndex.cpp:48:    load();
src\core\manga\MangaDownloadIndex.cpp:51:void MangaDownloadIndex::load()
src\core\manga\MangaPosterCache.h:16:void download(const MangaResult& preview,
src\core\manga\MangaPosterCache.cpp:53:void download(const MangaResult& preview,
src\core\manga\ComicsTankoyomiLibrary.h:81:    void load();
src\core\manga\MangaDownloader.h:67:    QString startDownload(const QString& seriesTitle, const QString& source,
src\core\manga\MangaDownloader.h:77:    void cancelDownload(const QString& id);
src\core\manga\MangaDownloader.h:79:    void removeDownload(const QString& id);
src\core\manga\ComicsTankoyomiLibrary.cpp:13:    load();
src\core\manga\ComicsTankoyomiLibrary.cpp:16:void ComicsTankoyomiLibrary::load()

codex
I’ve got the actual file and call sites open now. The primary F4 counter path looks sound, but I’m also checking whether the new defensive `continue` or any synchronous signal reentrancy can strand the pre-counted counter.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'rg "streamError" src -n' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/BulkSourceCollector.cpp' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 3270 -First 90' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 2310 -First 150' in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1634ms:
src\ui\pages\StreamPage.cpp:2411:    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError,
src\ui\pages\StreamPage.cpp:2413:    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError, this,
src\ui\pages\StreamPage.cpp:3328:    // streamError wiring ~line 2388). Clears the pending state so the tile/UI
src\ui\pages\StreamPage.cpp:3330:    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError,
src\ui\pages\StreamPage.cpp:3332:    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError, this,
src\ui\readers\BookBridge.cpp:40:    connect(m_ttsWorker, &EdgeTtsWorker::streamError,
src\ui\readers\BookBridge.cpp:517:    // streamError(streamId, "phase_4_pending") immediately; we route that
src\core\tts\EdgeTtsWorker.h:37:    // Phase 4 signals (wired Phase 4.2; Phase 1.2 emits phase_4_pending via streamError).
src\core\tts\EdgeTtsWorker.h:42:    void streamError(quint64 streamId, const QString& reason);
src\core\tts\EdgeTtsWorker.cpp:47:    emit streamError(streamId, QStringLiteral("phase_4_pending"));
src\core\stream\BulkSourceCollector.cpp:152:        aggregator, &StreamAggregator::streamError, this,
src\core\stream\StreamAggregator.h:80:    void streamError(const QString& addonId, const QString& message);
src\core\stream\StreamAggregator.cpp:695:    emit streamError(addonId, message);

 succeeded in 1744ms:
#include "core/stream/BulkSourceCollector.h"

#include "core/stream/StreamAggregator.h"
#include "core/stream/addon/AddonRegistry.h"
#include "core/stream/addon/StreamInfo.h"

#include <QDateTime>
#include <QMetaType>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace tankostream::stream {

struct BulkSourceCollector::InFlightEpisode {
    int episodeNum = 0;
    int addonsQueried = 0;
    StreamAggregator* aggregator = nullptr;
    QTimer* timeout = nullptr;
    QSet<QString> erroredAddonIds;
    QMetaObject::Connection readyConn;
    QMetaObject::Connection errorConn;
    QMetaObject::Connection timeoutConn;
};

BulkSourceCollector::BulkSourceCollector(tankostream::addon::AddonRegistry* registry,
                                         QObject* parent,
                                         int concurrencyCap)
    : QObject(parent)
    , m_registry(registry)
    , m_concurrencyCap(std::max(1, concurrencyCap))
{
    qRegisterMetaType<tankostream::stream::BulkSourceCollectionResult>(
        "tankostream::stream::BulkSourceCollectionResult");
    qRegisterMetaType<tankostream::stream::BulkSourceCollectionPayload>(
        "tankostream::stream::BulkSourceCollectionPayload");
}

BulkSourceCollector::~BulkSourceCollector()
{
    cancelInternal(false);
}

void BulkSourceCollector::begin(const BulkPlanInput& input)
{
    reset();
    m_input = input;
    m_pendingEpisodes = input.episodes;
    m_episodesTotal = m_pendingEpisodes.size();
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_running = true;
    m_cancelled = false;

    if (m_episodesTotal == 0) {
        maybeComplete();
        return;
    }

    startNextEpisodes();
}

void BulkSourceCollector::cancel()
{
    cancelInternal(true);
}

void BulkSourceCollector::reset()
{
    cancelInternal(false);
    m_input = {};
    m_pendingEpisodes.clear();
    m_resultsByEpisode.clear();
    m_startedAtMs = 0;
    m_episodesTotal = 0;
    m_running = false;
    m_cancelled = false;
}

void BulkSourceCollector::cancelInternal(bool emitSignal)
{
    const bool wasRunning = m_running && !m_cancelled;
    m_cancelled = true;
    m_running = false;
    m_pendingEpisodes.clear();

    const auto inFlight = m_inFlightByEpisode;
    m_inFlightByEpisode.clear();
    for (InFlightEpisode* episode : inFlight) {
        if (!episode) continue;
        if (episode->timeout) {
            QObject::disconnect(episode->timeoutConn);
            episode->timeout->stop();
            episode->timeout->deleteLater();
        }
        if (episode->aggregator) {
            QObject::disconnect(episode->readyConn);
            QObject::disconnect(episode->errorConn);
            episode->aggregator->deleteLater();
        }
        delete episode;
    }

    if (emitSignal && wasRunning)
        emit cancelled();
}

void BulkSourceCollector::startNextEpisodes()
{
    if (!m_running || m_cancelled)
        return;

    while (!m_pendingEpisodes.isEmpty()
           && m_inFlightByEpisode.size() < m_concurrencyCap) {
        const BulkPlanEpisodeInput episode = m_pendingEpisodes.takeFirst();
        startEpisode(episode);
    }
}

void BulkSourceCollector::startEpisode(const BulkPlanEpisodeInput& episode)
{
    auto* inFlight = new InFlightEpisode;
    inFlight->episodeNum = episode.episode;

    StreamLoadRequest request;
    request.type = QStringLiteral("series");
    request.id = streamRequestIdForEpisode(episode);

    if (m_registry) {
        inFlight->addonsQueried =
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
        return;

    InFlightEpisode* episode = m_inFlightByEpisode.take(episodeNum);
    if (!episode)
        return;

    QObject::disconnect(episode->readyConn);
    QObject::disconnect(episode->errorConn);
    QObject::disconnect(episode->timeoutConn);
    if (episode->timeout) {
        episode->timeout->stop();
        episode->timeout->deleteLater();
    }
    if (episode->aggregator)
        episode->aggregator->deleteLater();

    BulkSourceCollectionResult result;
    result.episodeNum = episodeNum;
    result.timedOut = timedOut;
    result.addonsErrored = episode->erroredAddonIds.size();
    result.addonsQueried = !addonsById.isEmpty()
        ? addonsById.size()
        : episode->addonsQueried;

    if (!timedOut) {
        result.choices = buildPickerChoices(streams, addonsById);

        QSet<QString> addonsWithChoices;
        for (const StreamPickerChoice& choice : result.choices) {
            if (!choice.addonId.isEmpty())
                addonsWithChoices.insert(choice.addonId);
        }
        result.addonsEmpty = std::max(
            0,
            result.addonsQueried - result.addonsErrored
                - static_cast<int>(addonsWithChoices.size()));
    } else {
        result.addonsEmpty = 0;
    }

    m_resultsByEpisode.insert(episodeNum, result);
    delete episode;

    emit progressTick(m_resultsByEpisode.size(), m_episodesTotal);

    startNextEpisodes();
    maybeComplete();
}

void BulkSourceCollector::maybeComplete()
{
    if (!m_running || m_cancelled)
        return;
    if (m_resultsByEpisode.size() < m_episodesTotal)
        return;
    if (!m_inFlightByEpisode.isEmpty() || !m_pendingEpisodes.isEmpty())
        return;

    BulkSourceCollectionPayload payload;
    payload.byEpisode = m_resultsByEpisode;
    payload.elapsedMs = m_startedAtMs > 0
        ? QDateTime::currentMSecsSinceEpoch() - m_startedAtMs
        : 0;
    payload.cancelled = false;

    m_running = false;
    emit collectionComplete(payload);
}

QString BulkSourceCollector::streamRequestIdForEpisode(
    const BulkPlanEpisodeInput& episode) const
{
    const int season = episode.season > 0 ? episode.season : m_input.seasonNumber;
    return QStringLiteral("%1:%2:%3")
        .arg(m_input.seriesId)
        .arg(season)
        .arg(episode.episode);
}

}  // namespace tankostream::stream

 succeeded in 1771ms:
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
    // Skip movies â€” no next episode concept.
    if (currentSeason <= 0 || currentEpisode <= 0) return;
    if (!m_metaAggregator || !m_streamAggregator) return;

    // Use the cached series meta (populated when the user opened detail view
    // for this series). fetchSeriesMeta emits synchronously on cache hit.
    // Re-wire once per fetch to avoid leaking stale connections.

 succeeded in 1801ms:
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

codex
The only suspicious branch is the counted-then-`continue` path. In the current aggregator callbacks, synchronous replies do not erase `m_pendingByAddon`, so that branch should be unreachable unless there is broader reentrant reset/load behavior outside this path.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonTransport.cpp' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 2500 -First 55' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/core/stream/addon/AddonTransport.h' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'Get-Content -Path src/ui/pages/StreamPage.cpp | Select-Object -Skip 2390 -First 45' in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1262ms:
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

 succeeded in 1295ms:
#include "AddonTransport.h"
#include "core/net/NetSeam.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStringList>

namespace tankostream::addon {

namespace {

constexpr int kTimeoutMs = 10000;
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

QString encodeComponent(const QString& value)
{
    // Match Stremio's URI_COMPONENT_ENCODE_SET (stremio-core/src/constants.rs:54-63):
    // preserve the RFC 3986 sub-delim chars ! * ' ( ) that Stremio leaves unencoded,
    // plus ':' so Torrentio series ids like "tt0944947:1:1" pass through.
    // Qt already preserves the unreserved set [A-Za-z0-9-_.~]; the include set
    // below names only the additional Stremio-compatible preservations.
    return QString::fromUtf8(QUrl::toPercentEncoding(value, "!*'():"));
}

}

AddonTransport::AddonTransport(QObject* parent)
    : QObject(parent)
    , m_nam(tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-addon-transport")))
{
}

void AddonTransport::fetchManifest(const QUrl& base)
{
    const QUrl manifestUrl = normalizeManifestUrl(base);
    if (!manifestUrl.isValid()) {
        emit manifestFailed(QStringLiteral("Invalid addon URL"));
        return;
    }

    QNetworkRequest req(manifestUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(kTimeoutMs);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manifestUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit manifestFailed(reply->errorString());
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit manifestFailed(QStringLiteral("Manifest JSON parse error: ") +
                                parseErr.errorString());
            return;
        }

        AddonDescriptor descriptor;
        descriptor.transportUrl = manifestUrl;

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
    if (path.endsWith(QLatin1String("/manifest.json"), Qt::CaseInsensitive)) {
        return base;
    }
    if (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    path += QStringLiteral("/manifest.json");
    base.setPath(path);
    return base;
}

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
    out.name = obj.value(QStringLiteral("name")).toString().trimmed();
    out.contactEmail = obj.value(QStringLiteral("contactEmail")).toString().trimmed();
    out.description = obj.value(QStringLiteral("description")).toString().trimmed();
    out.logo = QUrl(obj.value(QStringLiteral("logo")).toString().trimmed());
    out.background = QUrl(obj.value(QStringLiteral("background")).toString().trimmed());

    if (out.id.isEmpty() || out.name.isEmpty() || out.version.isEmpty()) {
        return false;
    }

    for (const QJsonValue& item : obj.value(QStringLiteral("types")).toArray()) {
        const QString type = item.toString().trimmed();
        if (!type.isEmpty()) {
            out.types.push_back(type);
        }
    }

    if (obj.contains(QStringLiteral("idPrefixes"))) {
        out.hasIdPrefixes = true;
        for (const QJsonValue& item : obj.value(QStringLiteral("idPrefixes")).toArray()) {
            const QString prefix = item.toString().trimmed();
            if (!prefix.isEmpty()) {
                out.idPrefixes.push_back(prefix);
            }
        }
    }

    for (const QJsonValue& item : obj.value(QStringLiteral("resources")).toArray()) {
        ManifestResource resource;
        if (item.isString()) {
            resource.name = item.toString().trimmed();
        } else {
            const QJsonObject ro = item.toObject();
            resource.name = ro.value(QStringLiteral("name")).toString().trimmed();
            if (ro.contains(QStringLiteral("types"))) {
                resource.hasTypes = true;
                for (const QJsonValue& t : ro.value(QStringLiteral("types")).toArray()) {
                    const QString type = t.toString().trimmed();
                    if (!type.isEmpty()) {
                        resource.types.push_back(type);
                    }
                }
            }
            if (ro.contains(QStringLiteral("idPrefixes"))) {
                resource.hasIdPrefixes = true;
                for (const QJsonValue& p : ro.value(QStringLiteral("idPrefixes")).toArray()) {
                    const QString prefix = p.toString().trimmed();
                    if (!prefix.isEmpty()) {
                        resource.idPrefixes.push_back(prefix);
                    }
                }
            }
        }
        if (!resource.name.isEmpty()) {
            out.resources.push_back(resource);
        }
    }

    for (const QJsonValue& item : obj.value(QStringLiteral("catalogs")).toArray()) {
        const QJsonObject catObj = item.toObject();
        ManifestCatalog catalog;
        catalog.id = catObj.value(QStringLiteral("id")).toString().trimmed();
        catalog.type = catObj.value(QStringLiteral("type")).toString().trimmed();
        catalog.name = catObj.value(QStringLiteral("name")).toString().trimmed();

        for (const QJsonValue& extraItem : catObj.value(QStringLiteral("extra")).toArray()) {
            const QJsonObject propObj = extraItem.toObject();
            ManifestExtraProp prop;
            prop.name = propObj.value(QStringLiteral("name")).toString().trimmed();
            if (prop.name.isEmpty()) {
                continue;
            }
            prop.isRequired = propObj.value(QStringLiteral("isRequired")).toBool(false);
            for (const QJsonValue& opt : propObj.value(QStringLiteral("options")).toArray()) {
                const QString optStr = opt.toString().trimmed();
                if (!optStr.isEmpty()) {
                    prop.options.push_back(optStr);
                }
            }
            if (propObj.contains(QStringLiteral("optionsLimit"))) {
                prop.optionsLimit = propObj.value(QStringLiteral("optionsLimit")).toInt(1);
            }
            catalog.extra.push_back(prop);
        }

        if (!catalog.id.isEmpty() && !catalog.type.isEmpty()) {
            out.catalogs.push_back(catalog);
        }
    }

    const QJsonObject hints = obj.value(QStringLiteral("behaviorHints")).toObject();
    out.behaviorHints.adult = hints.value(QStringLiteral("adult")).toBool(false);
    out.behaviorHints.p2p = hints.value(QStringLiteral("p2p")).toBool(false);
    out.behaviorHints.configurable = hints.value(QStringLiteral("configurable")).toBool(false);
    out.behaviorHints.configurationRequired =
        hints.value(QStringLiteral("configurationRequired")).toBool(false);

    static const QSet<QString> kKnownHints = {
        QStringLiteral("adult"),
        QStringLiteral("p2p"),
        QStringLiteral("configurable"),
        QStringLiteral("configurationRequired"),
    };
    for (auto it = hints.constBegin(); it != hints.constEnd(); ++it) {
        if (!kKnownHints.contains(it.key())) {
            out.behaviorHints.other.insert(it.key(), it.value().toVariant());
        }
    }

    return true;
}

}

 succeeded in 1422ms:
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

 succeeded in 1488ms:

            // Stash the prefetch context and fire a stream load.
            NextEpisodePrefetch prefetch;
            prefetch.imdbId   = imdbId;
            prefetch.season   = next.first;
            prefetch.episode  = next.second;
            prefetch.epKey    = StreamProgress::episodeKey(imdbId, next.first, next.second);
            m_session.nextPrefetch    = prefetch;

            // Reuse m_streamAggregator â€” the current episode's streamsReady
            // has already fired (we're at 95%), so nothing in-flight. Its
            // load() resets internal state cleanly.
            disconnect(m_streamAggregator,
                       &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);
            // DOWNLOAD BUG 2026-06-02 — same correlation token guard as the
            // play/auto-download paths. The prefetch one-shot is armed before
            // its load(), so the token rides in a shared_ptr filled from the
            // load() return below; a stale emit from a superseded load() is
            // discarded instead of feeding the wrong show into the prefetch.
            auto prefetchToken = std::make_shared<quint64>(0);
            connect(m_streamAggregator,
                    &tankostream::stream::StreamAggregator::streamsReady, this,
                [this, prefetchToken](const QList<tankostream::addon::Stream>& streams,
                       const QHash<QString, QString>& addonsById) {
                    if (m_streamAggregator->currentLoadToken() != *prefetchToken)
                        return;
                    disconnect(m_streamAggregator,
                               &tankostream::stream::StreamAggregator::streamsReady,
                               this, nullptr);
                    onNextEpisodePrefetchStreams(streams, addonsById);
                });

            tankostream::stream::StreamLoadRequest req;
            req.type = QStringLiteral("series");
            req.id   = imdbId + QLatin1Char(':') + QString::number(qMax(1, next.first))
                              + QLatin1Char(':') + QString::number(qMax(1, next.second));
            *prefetchToken = m_streamAggregator->load(req);
        });

    m_metaAggregator->fetchSeriesMeta(imdbId);
}

void StreamPage::onNextEpisodePrefetchStreams(
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>& addonsById)
{
    if (!m_session.nextPrefetch.has_value()) return;

    const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);
    if (choices.isEmpty()) return;

    // Match priority: per-episode saved choice > per-series bingeGroup.
    // We only fire auto-play for the next episode when one of these matches;
    // the overlay won't show otherwise.

codex
APPROVE
tokens used
76,257
APPROVE
