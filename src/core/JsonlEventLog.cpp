#include "core/JsonlEventLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>

namespace {

QString defaultEventLogPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir.isEmpty() ? QStringLiteral(".") : appDir)
        .absoluteFilePath(QStringLiteral("events.jsonl"));
}

QString categoryForMessageType(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("debug.qt");
    case QtInfoMsg:     return QStringLiteral("info.qt");
    case QtWarningMsg:  return QStringLiteral("error.warning");
    case QtCriticalMsg: return QStringLiteral("error.critical");
    case QtFatalMsg:    return QStringLiteral("error.fatal");
    }
    return QStringLiteral("error.qt");
}

} // namespace

JsonlEventLog& JsonlEventLog::instance()
{
    static JsonlEventLog s_instance;
    return s_instance;
}

void JsonlEventLog::setLogPath(const QString& path)
{
    QMutexLocker lock(&m_mutex);
    m_path = path;
}

QString JsonlEventLog::resolvedPathLocked() const
{
    return m_path.isEmpty() ? defaultEventLogPath() : m_path;
}

void JsonlEventLog::emitEvent(const QString& category,
                              const QString& event,
                              const QJsonObject& data)
{
    if (category.isEmpty() || event.isEmpty())
        return;

    QJsonObject root;
    root[QStringLiteral("ts")] = QDateTime::currentDateTimeUtc()
        .toString(Qt::ISODateWithMs);
    root[QStringLiteral("category")] = category;
    root[QStringLiteral("event")] = event;
    root[QStringLiteral("data")] = data;

    const QByteArray line = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';

    QMutexLocker lock(&m_mutex);
    rotateIfNeededLocked();

    const QString path = resolvedPathLocked();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(line);
}

void JsonlEventLog::rotateIfNeededLocked()
{
    const QString path = resolvedPathLocked();
    QFileInfo info(path);
    if (!info.exists() || info.size() < kRotateBytes)
        return;

    QFile::remove(path + QStringLiteral(".3"));
    if (QFileInfo::exists(path + QStringLiteral(".2")))
        QFile::rename(path + QStringLiteral(".2"), path + QStringLiteral(".3"));
    if (QFileInfo::exists(path + QStringLiteral(".1")))
        QFile::rename(path + QStringLiteral(".1"), path + QStringLiteral(".2"));
    QFile::rename(path, path + QStringLiteral(".1"));
}

void JsonlEventLog::installQtMessageHandler()
{
    JsonlEventLog& log = instance();
    QMutexLocker lock(&log.m_mutex);
    if (log.m_previousHandler)
        return;
    log.m_previousHandler = qInstallMessageHandler(&JsonlEventLog::messageHandler);
}

void JsonlEventLog::messageHandler(QtMsgType type,
                                   const QMessageLogContext& context,
                                   const QString& message)
{
    QJsonObject data;
    data[QStringLiteral("message")] = message;
    if (context.category)
        data[QStringLiteral("qtCategory")] = QString::fromLatin1(context.category);
    if (context.file)
        data[QStringLiteral("file")] = QString::fromLatin1(context.file);
    if (context.function)
        data[QStringLiteral("function")] = QString::fromLatin1(context.function);
    data[QStringLiteral("line")] = context.line;

    instance().emitEvent(categoryForMessageType(type),
                         QStringLiteral("qt_message"),
                         data);

    QtMessageHandler previous = nullptr;
    {
        QMutexLocker lock(&instance().m_mutex);
        previous = instance().m_previousHandler;
    }
    if (previous)
        previous(type, context, message);
}
