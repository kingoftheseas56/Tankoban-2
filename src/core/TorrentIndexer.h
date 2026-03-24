#pragma once

#include "TorrentResult.h"
#include <QObject>

class TorrentIndexer : public QObject
{
    Q_OBJECT

public:
    explicit TorrentIndexer(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~TorrentIndexer() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual void search(const QString& query, int limit = 30, const QString& categoryId = {}) = 0;

signals:
    void searchFinished(const QList<TorrentResult>& results);
    void searchError(const QString& error);
};
