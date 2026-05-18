#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QtMessageHandler>

class JsonlEventLog
{
public:
    static JsonlEventLog& instance();

    void setLogPath(const QString& path);
    void emitEvent(const QString& category,
                   const QString& event,
                   const QJsonObject& data = {});

    static void installQtMessageHandler();

private:
    JsonlEventLog() = default;
    JsonlEventLog(const JsonlEventLog&) = delete;
    JsonlEventLog& operator=(const JsonlEventLog&) = delete;

    void rotateIfNeededLocked();
    QString resolvedPathLocked() const;

    static void messageHandler(QtMsgType type,
                               const QMessageLogContext& context,
                               const QString& message);

    mutable QMutex m_mutex;
    QString m_path;
    QtMessageHandler m_previousHandler = nullptr;

    static constexpr qint64 kRotateBytes = 100LL * 1024LL * 1024LL;
};
