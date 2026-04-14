#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

class CoreBridge;
class KokoroTtsEngine;

// QWebChannel bridge object exposed to JS as "bridge".
// A JS shim maps this to window.electronAPI + window.__ebookNav.

class BookBridge : public QObject {
    Q_OBJECT
public:
    explicit BookBridge(CoreBridge* core, QObject* parent = nullptr);

    // ── files ──
    Q_INVOKABLE QByteArray filesRead(const QString& filePath);

    // ── booksProgress ──
    Q_INVOKABLE QJsonObject booksProgressGet(const QString& bookId);
    Q_INVOKABLE void booksProgressSave(const QString& bookId, const QJsonObject& data);

    // ── booksSettings ──
    Q_INVOKABLE QJsonObject booksSettingsGet(const QString& bookId);
    Q_INVOKABLE void booksSettingsSave(const QString& bookId, const QJsonObject& data);

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

    // ── TTS (Kokoro) ──
    Q_INVOKABLE QJsonObject ttsProbe();
    Q_INVOKABLE QJsonObject ttsGetVoices();
    Q_INVOKABLE QJsonObject ttsSynth(const QJsonObject& params);
    Q_INVOKABLE QJsonObject ttsWarmup();
    Q_INVOKABLE QJsonObject ttsCancelStream(const QString& streamId);
    Q_INVOKABLE QJsonObject ttsResetInstance();

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

    void setFullscreen(bool fs);

signals:
    void closeRequested();
    void fullscreenRequested(bool enter);

private:
    void ensureTts();

    CoreBridge* m_core = nullptr;
    KokoroTtsEngine* m_tts = nullptr;
    bool m_fullscreen = false;
};
