#pragma once

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <QString>
#include <QDir>

class JsonStore {
public:
    explicit JsonStore(const QString& dataDir);

    QJsonObject read(const QString& filename, const QJsonObject& fallback = {}) const;
    void write(const QString& filename, const QJsonObject& value);

    QString dataDir() const { return m_dataDir; }

private:
    QString m_dataDir;
    mutable QMutex m_mutex;
};
