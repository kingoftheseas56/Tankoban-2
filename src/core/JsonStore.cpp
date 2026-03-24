#include "JsonStore.h"

#include <QFile>
#include <QSaveFile>
#include <QDir>

JsonStore::JsonStore(const QString& dataDir)
    : m_dataDir(dataDir)
{
    QDir().mkpath(m_dataDir);
}

QJsonObject JsonStore::read(const QString& filename, const QJsonObject& fallback) const
{
    QMutexLocker lock(&m_mutex);
    QString path = m_dataDir + "/" + filename;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fallback;

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return fallback;

    return doc.object();
}

void JsonStore::write(const QString& filename, const QJsonObject& value)
{
    QMutexLocker lock(&m_mutex);
    QString path = m_dataDir + "/" + filename;

    // Atomic write via QSaveFile
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;

    QJsonDocument doc(value);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.commit();
}
