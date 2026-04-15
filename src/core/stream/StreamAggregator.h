#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QUrl>

#include "addon/StreamInfo.h"

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

struct StreamLoadRequest {
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

class StreamAggregator : public QObject
{
    Q_OBJECT

public:
    explicit StreamAggregator(tankostream::addon::AddonRegistry* registry,
                              QObject* parent = nullptr);

    void load(const StreamLoadRequest& request);

signals:
    void streamsReady(const QList<tankostream::addon::Stream>& streams,
                      const QHash<QString, QString>& addonsById);
    void streamError(const QString& addonId, const QString& message);

private:
    struct PendingAddon {
        QString addonId;
        QString addonName;
        QUrl transportUrl;
        bool inFlight = false;
    };

    void dispatchRequests();
    void onAddonReady(const QString& addonId, const QJsonObject& payload);
    void onAddonFailed(const QString& addonId, const QString& message);
    void completeOne();
    void reset();

    tankostream::addon::AddonRegistry* m_registry = nullptr;

    StreamLoadRequest m_request;
    QMap<QString, PendingAddon> m_pendingByAddon;
    QHash<QString, QString> m_addonsById;
    QSet<QString> m_seenIdentityKeys;
    QList<tankostream::addon::Stream> m_streams;
    int m_pendingResponses = 0;
};

}
