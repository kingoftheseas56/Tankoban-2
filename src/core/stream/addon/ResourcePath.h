#pragma once

#include <QList>
#include <QPair>
#include <QString>

namespace tankostream::addon {

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;

    static ResourceRequest withoutExtra(const QString& resourceName,
                                        const QString& itemType,
                                        const QString& itemId)
    {
        return {resourceName, itemType, itemId, {}};
    }

    static ResourceRequest withExtra(const QString& resourceName,
                                     const QString& itemType,
                                     const QString& itemId,
                                     const QList<QPair<QString, QString>>& extraValues)
    {
        return {resourceName, itemType, itemId, extraValues};
    }
};

}
