#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QUrl>

#include "addon/Manifest.h"
#include "addon/MetaItem.h"

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

struct CatalogQuery {
    QString addonId;
    QString type;
    QString catalogId;
    QList<QPair<QString, QString>> extra;
};

class CatalogAggregator : public QObject
{
    Q_OBJECT

public:
    explicit CatalogAggregator(tankostream::addon::AddonRegistry* registry,
                               QObject* parent = nullptr);

    void load(const CatalogQuery& query);
    void loadNextPage();

signals:
    void catalogPage(const QList<tankostream::addon::MetaItemPreview>& items, bool hasMore);
    void catalogError(const QString& addonId, const QString& message);

private:
    struct AddonCursor {
        QString addonId;
        QUrl baseUrl;
        tankostream::addon::ManifestCatalog catalog;
        int skip = 0;
        bool hasMore = true;
        bool inFlight = false;
    };

    QMap<QString, AddonCursor> planRequests(const CatalogQuery& query) const;
    void dispatchCurrentPage();
    void onAddonReady(const QString& addonId, const QJsonObject& payload);
    void onAddonFailed(const QString& addonId, const QString& message);
    void completeIfReady();
    void resetInternalState();

    tankostream::addon::AddonRegistry* m_registry = nullptr;

    CatalogQuery m_query;
    QSet<QString> m_seenMetaIds;
    QMap<QString, AddonCursor> m_activeByAddon;
    QList<tankostream::addon::MetaItemPreview> m_pageBuffer;
    int m_pendingResponses = 0;
};

}
