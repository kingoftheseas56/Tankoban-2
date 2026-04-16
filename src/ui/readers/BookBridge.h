#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>

class CoreBridge;
class EdgeTtsWorker;
class QThread;

// QWebChannel bridge object exposed to JS as "bridge".
// A JS shim maps this to window.electronAPI + window.__ebookNav.

class BookBridge : public QObject {
    Q_OBJECT
public:
    explicit BookBridge(CoreBridge* core, QObject* parent = nullptr);
    ~BookBridge() override;

    // ── files ──
    Q_INVOKABLE QByteArray filesRead(const QString& filePath);

    // ── booksProgress ──
    // Canonical SHA1[:20] key derived from the forward-slash-normalized absolute
    // path. Matches BooksPage.cpp:623-625 and BookSeriesView.cpp:740 exactly so
    // the reader and the library read/write the same record.
    Q_INVOKABLE QString progressKey(const QString& absPath) const;
    Q_INVOKABLE QJsonObject booksProgressGet(const QString& bookId);
    Q_INVOKABLE void booksProgressSave(const QString& bookId, const QJsonObject& data);

    // ── booksSettings ──
    // Global flat settings object, matching the Tankoban-Max JS contract:
    // get() returns `{ "settings": <flat object read from disk> }`; save(data)
    // writes the flat object at the root of books_settings.json. No per-book
    // keying — the JS reader (reader_state.js) already treats settings as a
    // single global bag.
    Q_INVOKABLE QJsonObject booksSettingsGet();
    Q_INVOKABLE void booksSettingsSave(const QJsonObject& data);

    // ── booksBookmarks ──
    Q_INVOKABLE QJsonArray booksBookmarksGet(const QString& bookId);
    Q_INVOKABLE QJsonObject booksBookmarksSave(const QString& bookId, const QJsonObject& bookmark);
    Q_INVOKABLE QJsonObject booksBookmarksDelete(const QString& bookId, const QString& bookmarkId);
    Q_INVOKABLE void booksBookmarksClear(const QString& bookId);

    // ── booksAnnotations ──
    Q_INVOKABLE QJsonArray booksAnnotationsGet(const QString& bookId);
    Q_INVOKABLE QJsonObject booksAnnotationsSave(const QString& bookId, const QJsonObject& annotation);
    Q_INVOKABLE QJsonObject booksAnnotationsDelete(const QString& bookId, const QString& annotationId);
    Q_INVOKABLE void booksAnnotationsClear(const QString& bookId);

    // ── booksDisplayNames ──
    Q_INVOKABLE QJsonObject booksDisplayNamesGetAll();
    Q_INVOKABLE void booksDisplayNamesSave(const QString& bookId, const QString& name);
    Q_INVOKABLE void booksDisplayNamesDelete(const QString& bookId);

    // ── audiobooks ──
    Q_INVOKABLE QJsonObject audiobooksGetState();
    Q_INVOKABLE QJsonObject audiobooksGetProgress(const QString& abId);
    Q_INVOKABLE void audiobooksSaveProgress(const QString& abId, const QJsonObject& data);
    Q_INVOKABLE QJsonObject audiobooksGetPairing(const QString& bookId);
    Q_INVOKABLE void audiobooksSavePairing(const QString& bookId, const QJsonObject& data);
    Q_INVOKABLE void audiobooksDeletePairing(const QString& bookId);

    // ── window ──
    Q_INVOKABLE bool windowIsFullscreen() const;
    Q_INVOKABLE QJsonObject windowToggleFullscreen();

    // ── navigation ──
    Q_INVOKABLE void requestClose();

    // ── readiness (Batch 1.3) ──
    // Called by engine_foliate.js once Foliate's renderer emits `stabilized`
    // after view.init() completes — first page is fully laid out and any
    // subsequent reflow-on-setting-change has settled. BookReader uses the
    // readerReady() signal to fade out the loading overlay. Multiple calls
    // are harmless — the consumer is idempotent (overlay fade is single-shot).
    Q_INVOKABLE void markReaderReady();

    void setFullscreen(bool fs);

    // ── Edge TTS (EDGE_TTS_FIX Phase 1.3) ──
    // Async-style bridge over a worker QThread. JS shim issues an `*Start`
    // call with a JS-generated request ID + payload; bridge dispatches to
    // worker; worker emits result; bridge re-emits the matching `*Finished`
    // signal carrying the same request ID. JS shim wraps both ends in a
    // Promise via a request-id → resolver map.
    Q_INVOKABLE void booksTtsEdgeProbeStart(quint64 reqId, const QString& voice);
    Q_INVOKABLE void booksTtsEdgeGetVoicesStart(quint64 reqId);
    Q_INVOKABLE void booksTtsEdgeSynthStart(quint64 reqId, const QString& text,
                                            const QString& voice, double rate, double pitch);
    Q_INVOKABLE void booksTtsEdgeSynthStreamStart(quint64 reqId, const QString& text,
                                                  const QString& voice, double rate, double pitch);
    Q_INVOKABLE void booksTtsEdgeCancelStream(quint64 streamId);
    Q_INVOKABLE void booksTtsEdgeWarmupStart(quint64 reqId);
    Q_INVOKABLE void booksTtsEdgeResetStart(quint64 reqId);

signals:
    void closeRequested();
    void fullscreenRequested(bool enter);
    void readerReady();

    // Edge TTS completion signals (carry the JS-generated reqId for promise correlation).
    void booksTtsEdgeProbeFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeVoicesReady(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeSynthFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeSynthStreamFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeWarmupFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeResetFinished(quint64 reqId, const QJsonObject& result);

private slots:
    // Worker-thread signal handlers; re-package + re-emit with the original
    // JS request ID stored in m_pending* members.
    void onWorkerProbeFinished(bool ok, const QString& reason);
    void onWorkerVoicesReady(const QJsonArray& voices);
    void onWorkerSynthFinished(quint64 requestId, bool ok, const QByteArray& mp3,
                               const QJsonArray& boundaries, const QString& reason);
    void onWorkerStreamError(quint64 streamId, const QString& reason);
    void onWorkerStreamEnded(quint64 streamId);
    void onWorkerWarmupFinished(bool ok, const QString& reason);
    void onWorkerResetFinished();

private:
    CoreBridge* m_core = nullptr;
    bool m_fullscreen = false;

    // Edge TTS worker thread + correlation state. Worker is a QObject moved to
    // m_ttsThread; communicates with this bridge via Qt::QueuedConnection
    // signals. Pending IDs are single-slot per operation type (probe / voices /
    // warmup / reset are issued one-at-a-time by the JS engine; queued slot
    // dispatch on the worker thread serializes them naturally). Synth/stream
    // pass through the requestId/streamId so multiple concurrent ops correlate.
    QThread* m_ttsThread = nullptr;
    EdgeTtsWorker* m_ttsWorker = nullptr;
    quint64 m_pendingProbeReqId = 0;
    quint64 m_pendingVoicesReqId = 0;
    quint64 m_pendingWarmupReqId = 0;
    quint64 m_pendingResetReqId = 0;
};
