#include "BookBridge.h"
#include <QFile>

BookBridge::BookBridge(QObject* parent)
    : QObject(parent)
{
}

QByteArray BookBridge::filesRead(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QJsonObject BookBridge::booksProgressGet(const QString& /*bookId*/)
{
    // TODO: persist via CoreBridge / JsonStore
    return {};
}

void BookBridge::booksProgressSave(const QString& /*bookId*/, const QJsonObject& /*data*/)
{
    // TODO: persist via CoreBridge / JsonStore
}

QJsonObject BookBridge::booksSettingsGet(const QString& /*bookId*/)
{
    // TODO: persist via CoreBridge / JsonStore
    return {};
}

void BookBridge::booksSettingsSave(const QString& /*bookId*/, const QJsonObject& /*data*/)
{
    // TODO: persist via CoreBridge / JsonStore
}

void BookBridge::requestClose()
{
    emit closeRequested();
}
