#pragma once

#include <QObject>
#include <QStringList>
#include <QJsonObject>
#include <memory>

class JsonStore;

class CoreBridge : public QObject {
    Q_OBJECT
public:
    explicit CoreBridge(const QString& dataDir, QObject* parent = nullptr);
    ~CoreBridge();

    static QString resolveDataDir();

    // ── Root folders per domain ──
    QStringList rootFolders(const QString& domain) const;
    void addRootFolder(const QString& domain, const QString& path);
    void removeRootFolder(const QString& domain, const QString& path);

    // ── Shell prefs ──
    QJsonObject prefs() const;
    void savePrefs(const QJsonObject& patch);

    // ── Progress (keyed by item id) ──
    QJsonObject allProgress(const QString& domain) const;
    QJsonObject progress(const QString& domain, const QString& itemId) const;
    void saveProgress(const QString& domain, const QString& itemId, const QJsonObject& data);
    void clearProgress(const QString& domain, const QString& itemId);

    // ── Data dir access ──
    QString dataDir() const;
    JsonStore& store();

signals:
    void rootFoldersChanged(const QString& domain);

private:
    QString stateFile(const QString& domain) const;

    std::unique_ptr<JsonStore> m_store;
};
