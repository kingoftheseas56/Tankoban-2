#include "BookBridge.h"
#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "core/tts/KokoroTtsEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QCollator>
#include <QCryptographicHash>
#include <QUuid>
#include <QDateTime>

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

QJsonObject BookBridge::booksSettingsGet(const QString& bookId)
{
    if (!m_core) return {};
    QJsonObject all = m_core->store().read(SETTINGS_FILE);
    return all.value(bookId).toObject();
}

void BookBridge::booksSettingsSave(const QString& bookId, const QJsonObject& data)
{
    if (!m_core) return;
    QJsonObject all = m_core->store().read(SETTINGS_FILE);
    all[bookId] = data;
    m_core->store().write(SETTINGS_FILE, all);
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

// ── TTS (Kokoro) ─────────────────────────────────────────────────────────────

static QByteArray pcmToWav(const QByteArray& pcmFloat32, int sampleRate)
{
    int dataSize = pcmFloat32.size();
    QByteArray wav;
    wav.reserve(44 + dataSize);

    // RIFF header
    wav.append("RIFF", 4);
    quint32 fileSize = 36 + dataSize;
    wav.append(reinterpret_cast<const char*>(&fileSize), 4);
    wav.append("WAVE", 4);

    // fmt chunk — IEEE float
    wav.append("fmt ", 4);
    quint32 fmtSize = 16;
    wav.append(reinterpret_cast<const char*>(&fmtSize), 4);
    quint16 format = 3; // IEEE float
    wav.append(reinterpret_cast<const char*>(&format), 2);
    quint16 channels = 1;
    wav.append(reinterpret_cast<const char*>(&channels), 2);
    quint32 sr = sampleRate;
    wav.append(reinterpret_cast<const char*>(&sr), 4);
    quint32 byteRate = sampleRate * 4;
    wav.append(reinterpret_cast<const char*>(&byteRate), 4);
    quint16 blockAlign = 4;
    wav.append(reinterpret_cast<const char*>(&blockAlign), 2);
    quint16 bitsPerSample = 32;
    wav.append(reinterpret_cast<const char*>(&bitsPerSample), 2);

    // data chunk
    wav.append("data", 4);
    quint32 ds = dataSize;
    wav.append(reinterpret_cast<const char*>(&ds), 4);
    wav.append(pcmFloat32);

    return wav;
}

static QString ttsModelDir()
{
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/models/kokoro/kokoro-int8-multi-lang-v1_1");
}

void BookBridge::ensureTts()
{
    if (m_tts) return;
    QString dir = ttsModelDir();
    if (!QDir(dir).exists()) return;
    m_tts = new KokoroTtsEngine(dir, this);
}

QJsonObject BookBridge::ttsProbe()
{
    QString dir = ttsModelDir();
    bool available = QDir(dir).exists() && QFileInfo(dir + "/model.int8.onnx").exists();
    if (available) {
        ensureTts();
        available = m_tts && m_tts->isReady();
    }
    QJsonObject r;
    r["ok"] = true;
    r["available"] = available;
    if (!available) r["reason"] = QStringLiteral("Kokoro model not found or failed to load");
    return r;
}

QJsonObject BookBridge::ttsGetVoices()
{
    ensureTts();
    if (!m_tts || !m_tts->isReady())
        return {{"ok", false}, {"voices", QJsonArray()}, {"error", "TTS not available"}};

    QJsonArray voices;
    for (const auto& v : m_tts->englishVoices()) {
        QJsonObject vo;
        vo["name"]       = v.name;
        vo["voiceURI"]   = v.id;
        vo["lang"]       = v.lang;
        vo["gender"]     = v.gender;
        vo["friendlyName"] = v.name + " (" + v.gender + ", " + v.lang + ")";
        voices.append(vo);
    }
    return {{"ok", true}, {"voices", voices}, {"cached", true}};
}

QJsonObject BookBridge::ttsSynth(const QJsonObject& params)
{
    ensureTts();
    if (!m_tts || !m_tts->isReady())
        return {{"ok", false}, {"error", "TTS not available"}};

    QString text  = params.value("text").toString().trimmed();
    QString voice = params.value("voice").toString();
    double  rate  = params.value("rate").toDouble(1.0);

    if (text.isEmpty())
        return {{"ok", false}, {"error", "empty text"}};

    // Map voice URI to speaker ID
    int sid = 0;
    if (!voice.isEmpty())
        sid = m_tts->speakerIdForVoice(voice);
    if (sid < 0) sid = 0;

    float speed = qBound(0.5f, static_cast<float>(rate), 3.0f);

    QByteArray pcm = m_tts->synthesize(text, sid, speed);
    if (pcm.isEmpty())
        return {{"ok", false}, {"error", "synthesis failed"}};

    QByteArray wav = pcmToWav(pcm, m_tts->sampleRate());
    QString b64 = QString::fromLatin1(wav.toBase64());

    // Estimate word boundaries by distributing offsets linearly across audio duration
    int sampleCount = pcm.size() / int(sizeof(float));
    double durationMs = (double(sampleCount) / double(m_tts->sampleRate())) * 1000.0;

    QJsonArray boundaries;
    // Split on whitespace, track character positions
    int pos = 0;
    int wordCount = 0;
    struct WordSpan { int charIndex; int charLen; QString word; };
    QVector<WordSpan> words;

    while (pos < text.size()) {
        // Skip whitespace
        while (pos < text.size() && text[pos].isSpace()) pos++;
        if (pos >= text.size()) break;
        // Find word end
        int start = pos;
        while (pos < text.size() && !text[pos].isSpace()) pos++;
        words.append({start, pos - start, text.mid(start, pos - start)});
    }

    if (!words.isEmpty()) {
        double msPerWord = durationMs / double(words.size());
        for (int i = 0; i < words.size(); ++i) {
            QJsonObject b;
            b["offsetMs"] = msPerWord * i;
            b["text"] = words[i].word;
            boundaries.append(b);
        }
    }

    return {{"ok", true}, {"audioBase64", b64}, {"boundaries", boundaries}};
}

QJsonObject BookBridge::ttsWarmup()
{
    ensureTts();
    bool available = m_tts && m_tts->isReady();
    int count = available ? m_tts->englishVoices().size() : 0;
    return {{"ok", true}, {"available", available}, {"voicesCount", count}};
}

QJsonObject BookBridge::ttsCancelStream(const QString& /*streamId*/)
{
    return {{"ok", true}};
}

QJsonObject BookBridge::ttsResetInstance()
{
    return {{"ok", true}};
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
