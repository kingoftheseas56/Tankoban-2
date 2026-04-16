#include "BookBridge.h"
#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "core/tts/EdgeTtsWorker.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QCollator>
#include <QCryptographicHash>
#include <QUuid>
#include <QDateTime>
#include <QThread>
#include <QJsonValue>
#include <QMetaObject>

static const QString SETTINGS_FILE      = QStringLiteral("books_settings.json");
static const QString BOOKMARKS_FILE     = QStringLiteral("books_bookmarks.json");
static const QString ANNOTATIONS_FILE   = QStringLiteral("books_annotations.json");
static const QString DISPLAY_NAMES_FILE = QStringLiteral("books_display_names.json");
static const QString AB_PROGRESS_FILE   = QStringLiteral("audiobook_progress.json");
static const QString AB_PAIRINGS_FILE   = QStringLiteral("audiobook_pairings.json");

static const QStringList AUDIO_EXTS = {
    "mp3", "m4a", "m4b", "ogg", "opus", "flac", "wav", "aac", "wma"
};
static const QStringList COVER_NAMES = {
    "cover.jpg", "cover.png", "folder.jpg", "front.jpg"
};
static const QSet<QString> IGNORE_DIRS = {
    "__macosx", ".git", ".svn", "$recycle.bin", "system volume information", "node_modules"
};

BookBridge::BookBridge(CoreBridge* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
{
    // EDGE_TTS_FIX Phase 1.3: spawn worker thread eagerly. Lifetime = whole
    // BookReader page. Worker is parent-less so moveToThread succeeds; we
    // explicitly delete it after the thread joins in our destructor (avoids
    // the deleteLater-after-event-loop-exit pattern).
    m_ttsThread = new QThread(this);
    m_ttsWorker = new EdgeTtsWorker;
    m_ttsWorker->moveToThread(m_ttsThread);

    // Cross-thread signal wiring (auto Qt::QueuedConnection because src/dst
    // live on different threads).
    connect(m_ttsWorker, &EdgeTtsWorker::probeFinished,
            this, &BookBridge::onWorkerProbeFinished);
    connect(m_ttsWorker, &EdgeTtsWorker::voicesReady,
            this, &BookBridge::onWorkerVoicesReady);
    connect(m_ttsWorker, &EdgeTtsWorker::synthFinished,
            this, &BookBridge::onWorkerSynthFinished);
    connect(m_ttsWorker, &EdgeTtsWorker::streamError,
            this, &BookBridge::onWorkerStreamError);
    connect(m_ttsWorker, &EdgeTtsWorker::streamEnded,
            this, &BookBridge::onWorkerStreamEnded);
    connect(m_ttsWorker, &EdgeTtsWorker::warmupFinished,
            this, &BookBridge::onWorkerWarmupFinished);
    connect(m_ttsWorker, &EdgeTtsWorker::resetFinished,
            this, &BookBridge::onWorkerResetFinished);

    m_ttsThread->setObjectName(QStringLiteral("EdgeTtsThread"));
    m_ttsThread->start();
}

BookBridge::~BookBridge() {
    // Tear down the worker thread cleanly — quit the event loop, wait for
    // exit, then explicitly delete the worker (deleteLater isn't reliable
    // after the thread's event loop has stopped). 5s wait cap protects
    // against destructor deadlock.
    if (m_ttsThread) {
        m_ttsThread->quit();
        m_ttsThread->wait(5000);
    }
    delete m_ttsWorker;
    m_ttsWorker = nullptr;
    // m_ttsThread is parented to this — Qt deletes it as a child after dtor.
}

// ── files ────────────────────────────────────────────────────────────────────

QByteArray BookBridge::filesRead(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// ── booksProgress ────────────────────────────────────────────────────────────

QString BookBridge::progressKey(const QString& absPath) const
{
    QString norm = absPath;
    norm.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QString::fromLatin1(
        QCryptographicHash::hash(norm.toUtf8(), QCryptographicHash::Sha1)
            .toHex().left(20));
}

QJsonObject BookBridge::booksProgressGet(const QString& bookId)
{
    if (!m_core) return {};
    return m_core->progress("books", bookId);
}

void BookBridge::booksProgressSave(const QString& bookId, const QJsonObject& data)
{
    if (!m_core) return;
    m_core->saveProgress("books", bookId, data);
}

// ── booksSettings ────────────────────────────────────────────────────────────
//
// BOOK_FIX 1.2: global flat-settings contract.
//
// Pre-1.2 the bridge keyed settings by an in-JS id argument (`save(id, data)` /
// `get(id)`). The JS calls zero/one-arg per the Tankoban-Max shape, so over
// QWebChannel the `id` arg received the settings object stringified ("") and
// the `data` arg arrived undefined — every save landed under key "" as an
// empty object. books_settings.json degenerated into `{"":{}}`.
//
// The new contract persists a flat settings object at the root of
// books_settings.json. `get()` wraps the disk contents as `{ "settings": ... }`
// to match the Tankoban-Max JS unwrap at reader_state.js:359-360. `save(data)`
// writes the passed object directly. A one-shot migration below discards the
// broken `{"":{}}` record on first read (it carries no recoverable data).

QJsonObject BookBridge::booksSettingsGet()
{
    if (!m_core) return {{"settings", QJsonObject{}}};

    QJsonObject disk = m_core->store().read(SETTINGS_FILE);

    // Migration: 1.2 replaces the per-book-keyed map with a flat root object.
    // If the file still looks like a pre-1.2 map (only keys are "", "<sha1>",
    // or other short hex), treat it as legacy and return empty. First save
    // after 1.2 overwrites the file cleanly.
    const bool legacyEmptyKey = disk.contains(QStringLiteral("")) && disk.size() == 1;
    if (legacyEmptyKey) {
        return {{"settings", QJsonObject{}}};
    }

    return {{"settings", disk}};
}

void BookBridge::booksSettingsSave(const QJsonObject& data)
{
    if (!m_core) return;
    m_core->store().write(SETTINGS_FILE, data);
}

// ── booksBookmarks ───────────────────────────────────────────────────────────

QJsonArray BookBridge::booksBookmarksGet(const QString& bookId)
{
    if (!m_core) return {};
    QJsonObject all = m_core->store().read(BOOKMARKS_FILE);
    return all.value(bookId).toArray();
}

QJsonObject BookBridge::booksBookmarksSave(const QString& bookId, const QJsonObject& bookmark)
{
    if (!m_core) return {{"ok", false}};

    QJsonObject bm = bookmark;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Auto-generate ID if missing
    QString id = bm.value("id").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        bm["id"] = id;
    }

    // Timestamps
    if (!bm.contains("createdAt"))
        bm["createdAt"] = now;
    bm["updatedAt"] = now;

    QJsonObject all = m_core->store().read(BOOKMARKS_FILE);
    QJsonArray arr = all.value(bookId).toArray();

    // Deduplicate: replace if same ID exists
    QJsonArray updated;
    bool replaced = false;
    for (const auto& v : arr) {
        QJsonObject existing = v.toObject();
        if (existing.value("id").toString() == id) {
            // Preserve original createdAt
            bm["createdAt"] = existing.value("createdAt");
            updated.append(bm);
            replaced = true;
        } else {
            updated.append(existing);
        }
    }
    if (!replaced)
        updated.append(bm);

    all[bookId] = updated;
    m_core->store().write(BOOKMARKS_FILE, all);

    return {{"ok", true}, {"id", id}};
}

QJsonObject BookBridge::booksBookmarksDelete(const QString& bookId, const QString& bookmarkId)
{
    if (!m_core) return {{"ok", false}};

    QJsonObject all = m_core->store().read(BOOKMARKS_FILE);
    QJsonArray arr = all.value(bookId).toArray();

    if (bookmarkId.isEmpty()) {
        // Delete all bookmarks for this book
        all.remove(bookId);
    } else {
        QJsonArray filtered;
        for (const auto& v : arr) {
            if (v.toObject().value("id").toString() != bookmarkId)
                filtered.append(v);
        }
        all[bookId] = filtered;
    }

    m_core->store().write(BOOKMARKS_FILE, all);
    return {{"ok", true}};
}

void BookBridge::booksBookmarksClear(const QString& bookId)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(BOOKMARKS_FILE);
    all.remove(bookId);
    m_core->store().write(BOOKMARKS_FILE, all);
}

// ── booksAnnotations ─────────────────────────────────────────────────────────

QJsonArray BookBridge::booksAnnotationsGet(const QString& bookId)
{
    if (!m_core) return {};
    QJsonObject all = m_core->store().read(ANNOTATIONS_FILE);
    return all.value(bookId).toArray();
}

QJsonObject BookBridge::booksAnnotationsSave(const QString& bookId, const QJsonObject& annotation)
{
    if (!m_core) return {{"ok", false}};

    QJsonObject ann = annotation;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Auto-generate ID if missing
    QString id = ann.value("id").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ann["id"] = id;
    }

    // Timestamps
    if (!ann.contains("createdAt"))
        ann["createdAt"] = now;
    ann["updatedAt"] = now;

    QJsonObject all = m_core->store().read(ANNOTATIONS_FILE);
    QJsonArray arr = all.value(bookId).toArray();

    // Replace if same ID exists, otherwise append
    QJsonArray updated;
    bool replaced = false;
    for (const auto& v : arr) {
        QJsonObject existing = v.toObject();
        if (existing.value("id").toString() == id) {
            ann["createdAt"] = existing.value("createdAt");
            updated.append(ann);
            replaced = true;
        } else {
            updated.append(existing);
        }
    }
    if (!replaced)
        updated.append(ann);

    all[bookId] = updated;
    m_core->store().write(ANNOTATIONS_FILE, all);

    return {{"ok", true}, {"id", id}};
}

QJsonObject BookBridge::booksAnnotationsDelete(const QString& bookId, const QString& annotationId)
{
    if (!m_core) return {{"ok", false}};

    QJsonObject all = m_core->store().read(ANNOTATIONS_FILE);
    QJsonArray arr = all.value(bookId).toArray();

    if (annotationId.isEmpty()) {
        all.remove(bookId);
    } else {
        QJsonArray filtered;
        for (const auto& v : arr) {
            if (v.toObject().value("id").toString() != annotationId)
                filtered.append(v);
        }
        all[bookId] = filtered;
    }

    m_core->store().write(ANNOTATIONS_FILE, all);
    return {{"ok", true}};
}

void BookBridge::booksAnnotationsClear(const QString& bookId)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(ANNOTATIONS_FILE);
    all.remove(bookId);
    m_core->store().write(ANNOTATIONS_FILE, all);
}

// ── booksDisplayNames ────────────────────────────────────────────────────────

QJsonObject BookBridge::booksDisplayNamesGetAll()
{
    if (!m_core) return {};
    return m_core->store().read(DISPLAY_NAMES_FILE);
}

void BookBridge::booksDisplayNamesSave(const QString& bookId, const QString& name)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(DISPLAY_NAMES_FILE);
    all[bookId] = name;
    m_core->store().write(DISPLAY_NAMES_FILE, all);
}

void BookBridge::booksDisplayNamesDelete(const QString& bookId)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(DISPLAY_NAMES_FILE);
    all.remove(bookId);
    m_core->store().write(DISPLAY_NAMES_FILE, all);
}

// ── audiobooks ───────────────────────────────────────────────────────────────

static bool isAudioFile(const QString& fileName)
{
    QString ext = QFileInfo(fileName).suffix().toLower();
    return AUDIO_EXTS.contains(ext);
}

static QString findCover(const QDir& dir)
{
    for (const QString& name : COVER_NAMES) {
        if (dir.exists(name))
            return dir.absoluteFilePath(name);
    }
    // Fallback: first jpg/png in folder
    for (const auto& entry : dir.entryInfoList({"*.jpg", "*.jpeg", "*.png"}, QDir::Files)) {
        return entry.absoluteFilePath();
    }
    return {};
}

static QString audiobookId(const QString& folderPath)
{
    return QString(QCryptographicHash::hash(folderPath.toUtf8(),
                   QCryptographicHash::Sha1).toHex().left(20));
}

QJsonObject BookBridge::audiobooksGetState()
{
    if (!m_core) return {{"audiobooks", QJsonArray()}};

    QStringList roots = m_core->rootFolders("audiobooks");
    QJsonArray audiobooks;

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    for (const QString& root : roots) {
        QDir rootDir(root);
        if (!rootDir.exists()) continue;

        // Each immediate subdirectory is a potential audiobook
        for (const auto& entry : rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString dirName = entry.fileName().toLower();
            if (IGNORE_DIRS.contains(dirName)) continue;

            QDir abDir(entry.absoluteFilePath());
            QStringList audioFiles;

            // Recursively collect audio files
            QDirIterator it(abDir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                if (isAudioFile(it.fileName()))
                    audioFiles.append(it.filePath());
            }

            if (audioFiles.isEmpty()) continue;

            // Natural sort for chapter order
            std::sort(audioFiles.begin(), audioFiles.end(),
                      [&collator](const QString& a, const QString& b) {
                          return collator.compare(QFileInfo(a).fileName(),
                                                  QFileInfo(b).fileName()) < 0;
                      });

            // Build chapters array
            QJsonArray chapters;
            for (const QString& filePath : audioFiles) {
                QFileInfo fi(filePath);
                QJsonObject ch;
                ch["file"]  = fi.fileName();
                ch["title"] = fi.completeBaseName();
                ch["path"]  = fi.absoluteFilePath();
                ch["size"]  = fi.size();
                ch["duration"] = 0;
                chapters.append(ch);
            }

            QJsonObject ab;
            ab["id"]            = audiobookId(abDir.absolutePath());
            ab["title"]         = entry.fileName();
            ab["path"]          = abDir.absolutePath();
            ab["chapters"]      = chapters;
            ab["totalDuration"] = 0;
            ab["coverPath"]     = findCover(abDir);
            ab["rootPath"]      = root;
            audiobooks.append(ab);
        }
    }

    return {{"audiobooks", audiobooks}};
}

QJsonObject BookBridge::audiobooksGetProgress(const QString& abId)
{
    if (!m_core) return {};
    QJsonObject all = m_core->store().read(AB_PROGRESS_FILE);
    return all.value(abId).toObject();
}

void BookBridge::audiobooksSaveProgress(const QString& abId, const QJsonObject& data)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(AB_PROGRESS_FILE);
    QJsonObject entry = data;
    entry["updatedAt"] = QDateTime::currentMSecsSinceEpoch();
    all[abId] = entry;
    m_core->store().write(AB_PROGRESS_FILE, all);
}

QJsonObject BookBridge::audiobooksGetPairing(const QString& bookId)
{
    if (!m_core) return {};
    QJsonObject all = m_core->store().read(AB_PAIRINGS_FILE);
    return all.value(bookId).toObject();
}

void BookBridge::audiobooksSavePairing(const QString& bookId, const QJsonObject& data)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(AB_PAIRINGS_FILE);
    QJsonObject entry = data;
    entry["updatedAt"] = QDateTime::currentMSecsSinceEpoch();
    all[bookId] = entry;
    m_core->store().write(AB_PAIRINGS_FILE, all);
}

void BookBridge::audiobooksDeletePairing(const QString& bookId)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(AB_PAIRINGS_FILE);
    all.remove(bookId);
    m_core->store().write(AB_PAIRINGS_FILE, all);
}

// ── window ───────────────────────────────────────────────────────────────────

bool BookBridge::windowIsFullscreen() const
{
    return m_fullscreen;
}

QJsonObject BookBridge::windowToggleFullscreen()
{
    m_fullscreen = !m_fullscreen;
    emit fullscreenRequested(m_fullscreen);
    return {{"ok", true}};
}

void BookBridge::setFullscreen(bool fs)
{
    m_fullscreen = fs;
}

// ── navigation ───────────────────────────────────────────────────────────────

void BookBridge::requestClose()
{
    emit closeRequested();
}

// ── readiness (Batch 1.3) ────────────────────────────────────────────────────

void BookBridge::markReaderReady()
{
    emit readerReady();
}

// ── Edge TTS bridge (EDGE_TTS_FIX Phase 1.3) ─────────────────────────────────
//
// The JS shim issues *Start calls with a JS-generated reqId, then waits on the
// matching *Finished signal carrying the same reqId. We dispatch to the worker
// thread via QMetaObject::invokeMethod (queued); worker emits a result signal
// (sometimes without reqId for single-pending operations); our worker-side
// slot re-packages + re-emits with the stored reqId.
//
// Single-pending discipline: probe / getVoices / warmup / reset are issued
// one-at-a-time by tts_core.js init(); we keep a single pending reqId per
// operation type. synth/synthStream pass reqId/streamId through the worker
// for native multi-in-flight correlation.

void BookBridge::booksTtsEdgeProbeStart(quint64 reqId, const QString& voice)
{
    m_pendingProbeReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "probe", Qt::QueuedConnection,
                              Q_ARG(QString, voice));
}

void BookBridge::booksTtsEdgeGetVoicesStart(quint64 reqId)
{
    m_pendingVoicesReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "getVoices", Qt::QueuedConnection);
}

void BookBridge::booksTtsEdgeSynthStart(quint64 reqId, const QString& text,
                                        const QString& voice, double rate, double pitch)
{
    QMetaObject::invokeMethod(m_ttsWorker, "synth", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId),
                              Q_ARG(QString, text),
                              Q_ARG(QString, voice),
                              Q_ARG(double, rate),
                              Q_ARG(double, pitch));
}

void BookBridge::booksTtsEdgeSynthStreamStart(quint64 reqId, const QString& text,
                                              const QString& voice, double rate, double pitch)
{
    QMetaObject::invokeMethod(m_ttsWorker, "synthStream", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId),
                              Q_ARG(QString, text),
                              Q_ARG(QString, voice),
                              Q_ARG(double, rate),
                              Q_ARG(double, pitch));
}

void BookBridge::booksTtsEdgeCancelStream(quint64 streamId)
{
    QMetaObject::invokeMethod(m_ttsWorker, "cancelStream", Qt::QueuedConnection,
                              Q_ARG(quint64, streamId));
}

void BookBridge::booksTtsEdgeWarmupStart(quint64 reqId)
{
    m_pendingWarmupReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "warmup", Qt::QueuedConnection);
}

void BookBridge::booksTtsEdgeResetStart(quint64 reqId)
{
    m_pendingResetReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "resetInstance", Qt::QueuedConnection);
}

// ── Worker → bridge → JS dispatch ─────────────────────────────────────────────

void BookBridge::onWorkerProbeFinished(bool ok, const QString& reason)
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), ok);
    if (ok) {
        result.insert(QStringLiteral("voiceListAvailable"), true);
    }
    if (!reason.isEmpty()) {
        result.insert(QStringLiteral("reason"), reason);
    }
    const quint64 reqId = m_pendingProbeReqId;
    m_pendingProbeReqId = 0;
    emit booksTtsEdgeProbeFinished(reqId, result);
}

void BookBridge::onWorkerVoicesReady(const QJsonArray& voices)
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("voices"), voices);
    const quint64 reqId = m_pendingVoicesReqId;
    m_pendingVoicesReqId = 0;
    emit booksTtsEdgeVoicesReady(reqId, result);
}

void BookBridge::onWorkerSynthFinished(quint64 requestId, bool ok,
                                       const QByteArray& mp3,
                                       const QJsonArray& boundaries,
                                       const QString& reason)
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), ok);
    if (ok) {
        // Phase 2 will populate audioBase64 + boundaries. Phase 1.3 path
        // returns ok=false reason=phase_2_pending so this branch won't fire
        // until Phase 2.1 lands.
        result.insert(QStringLiteral("audioBase64"),
                      QString::fromLatin1(mp3.toBase64()));
        result.insert(QStringLiteral("boundaries"), boundaries);
    }
    if (!reason.isEmpty()) {
        result.insert(QStringLiteral("reason"), reason);
    }
    emit booksTtsEdgeSynthFinished(requestId, result);
}

void BookBridge::onWorkerStreamError(quint64 streamId, const QString& reason)
{
    // Phase 1.3: synthStream is not yet wired (Phase 4.2). Worker emits
    // streamError(streamId, "phase_4_pending") immediately; we route that
    // back through the SynthStreamFinished signal so the JS Promise resolves
    // with a structured failure.
    QJsonObject result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("reason"), reason);
    emit booksTtsEdgeSynthStreamFinished(streamId, result);
}

void BookBridge::onWorkerStreamEnded(quint64 streamId)
{
    // Phase 1.3: cancelStream is fire-and-forget on the JS side, but the
    // worker emits streamEnded(streamId) for symmetry. Phase 4.2 will use
    // this to drive `end` events on window.__ttsEdgeStream.
    Q_UNUSED(streamId);
}

void BookBridge::onWorkerWarmupFinished(bool ok, const QString& reason)
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), ok);
    if (!reason.isEmpty()) {
        result.insert(QStringLiteral("reason"), reason);
    }
    const quint64 reqId = m_pendingWarmupReqId;
    m_pendingWarmupReqId = 0;
    emit booksTtsEdgeWarmupFinished(reqId, result);
}

void BookBridge::onWorkerResetFinished()
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    const quint64 reqId = m_pendingResetReqId;
    m_pendingResetReqId = 0;
    emit booksTtsEdgeResetFinished(reqId, result);
}
