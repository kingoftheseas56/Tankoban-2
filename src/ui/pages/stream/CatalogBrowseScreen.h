#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "core/stream/addon/Manifest.h"
#include "core/stream/addon/MetaItem.h"

class QComboBox;
class QHBoxLayout;
class QLabel;
class QNetworkAccessManager;
class QPushButton;

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

    void open(const QString& addonId,
              const QString& type,
              const QString& catalogId);

signals:
    void backRequested();
    // Phase 1 Batch 1.1 — payload changed from (id, type) to the full
    // MetaItemPreview so the receiver (StreamPage::showDetail) can paint
    // the detail view header immediately without a library lookup.
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

    void buildUi();
    void rebuildSelectors();
    void rebuildCatalogCombo();
    void rebuildFilterBar();
    void reload();
    QList<QPair<QString, QString>> gatherSelectedExtras() const;
    QString posterCachePath(const QString& metaId) const;
    void ensurePoster(const QString& metaId, const QUrl& posterUrl, TileCard* card);
    void appendTiles(const QList<tankostream::addon::MetaItemPreview>& items);

    QString currentAddonId() const;
    const CatalogItem* currentCatalog() const;
    void selectAddon(const QString& addonId);
    void selectCatalog(const QString& type, const QString& catalogId);

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    CatalogAggregator* m_aggregator = nullptr;
    QNetworkAccessManager* m_nam = nullptr;

    QComboBox* m_addonCombo = nullptr;
    QComboBox* m_catalogCombo = nullptr;
    QWidget* m_filterRow = nullptr;
    QHBoxLayout* m_filterLayout = nullptr;
    QLabel* m_statusLabel = nullptr;
    TileStrip* m_strip = nullptr;
    QPushButton* m_loadMoreButton = nullptr;

    QList<CatalogItem> m_catalogItems;
    QString m_posterCacheDir;
    bool m_suppressReload = false;
    // Phase 1 Batch 1.1 — previews cached by id for tile-click emission.
    QHash<QString, tankostream::addon::MetaItemPreview> m_previewsById;
};

}
