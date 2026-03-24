#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

// QWebChannel bridge object exposed to JS as "bridge".
// A JS shim maps this to window.electronAPI + window.__ebookNav.

class BookBridge : public QObject {
    Q_OBJECT
public:
    explicit BookBridge(QObject* parent = nullptr);

    // ── files ──
    Q_INVOKABLE QByteArray filesRead(const QString& filePath);

    // ── booksProgress ──
    Q_INVOKABLE QJsonObject booksProgressGet(const QString& bookId);
    Q_INVOKABLE void booksProgressSave(const QString& bookId, const QJsonObject& data);

    // ── booksSettings ──
    Q_INVOKABLE QJsonObject booksSettingsGet(const QString& bookId);
    Q_INVOKABLE void booksSettingsSave(const QString& bookId, const QJsonObject& data);

    // ── navigation ──
    Q_INVOKABLE void requestClose();

signals:
    void closeRequested();
};
