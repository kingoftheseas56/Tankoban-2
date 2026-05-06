#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "core/stream/addon/Manifest.h"
#include "core/stream/addon/MetaItem.h"

class QHBoxLayout;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class TileCard;
class TileStrip;

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

class CatalogAggregator;

class CatalogBrowseScreen : public QWidget
{
    Q_OBJECT

public:
    explicit CatalogBrowseScreen(tankostream::addon::AddonRegistry* registry,
                                 QWidget* parent = nullptr);
    ~CatalogBrowseScreen() override;

    void open(const QString& addonId,
              const QString& type,
              const QString& catalogId);

signals:
    void backRequested();
    void metaActivated(const tankostream::addon::MetaItemPreview& preview);

private:
    struct CatalogItem {
        QString addonId;
        QString addonName;
        QString type;
        QString id;
        QString title;
        QList<tankostream::addon::ManifestExtraProp> extra;
    };
    struct RowState;

    void buildUi();
    void rebuildSelectors();
    void rebuildFilterBar();
    void clearFilterBar();
    void showHomeBoard();
    void showDetailForCatalog(int catalogIndex);
    void clearRows();
    QList<int> sortedCatalogIndices() const;
    QString displayTitleForCatalog(const CatalogItem& item) const;
    void reload();
    QList<QPair<QString, QString>> gatherSelectedExtras() const;
    QString posterCachePath(const QString& metaId) const;
    void ensurePoster(const QString& metaId, const QUrl& posterUrl, TileCard* card);
    TileCard* makeTile(const tankostream::addon::MetaItemPreview& item);
    void connectStripActivation(TileStrip* strip);
    void appendRowTiles(RowState* row, const QList<tankostream::addon::MetaItemPreview>& items);
    void appendTiles(const QList<tankostream::addon::MetaItemPreview>& items);

    const CatalogItem* currentCatalog() const;
    int catalogIndexFor(const QString& addonId,
                        const QString& type,
                        const QString& catalogId) const;

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    CatalogAggregator* m_aggregator = nullptr;
    QNetworkAccessManager* m_nam = nullptr;

    QWidget* m_filterRow = nullptr;
    QHBoxLayout* m_filterLayout = nullptr;
    QLabel* m_statusLabel = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    TileStrip* m_strip = nullptr;
    QPushButton* m_loadMoreButton = nullptr;

    QList<CatalogItem> m_catalogItems;
    QList<RowState*> m_rows;
    QString m_posterCacheDir;
    bool m_suppressReload = false;
    int m_activeCatalogIndex = -1;
    int m_generation = 0;
    QHash<QString, tankostream::addon::MetaItemPreview> m_previewsById;
};

}
