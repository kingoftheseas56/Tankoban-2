// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 3.3 (Catalog Browsing)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:158
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:160
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:161
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:162
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:163
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:81
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:85
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:125
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:19
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:26
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:30
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:53
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Descriptor.h:15
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamLibraryLayout.cpp:125
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamLibraryLayout.cpp:131
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalog_with_filters.rs:98
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalog_with_filters.rs:101
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalog_with_filters.rs:480
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalogs_with_extra.rs:69
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalogs_with_extra.rs:173
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 3.3.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

class TileStrip;
class TileCard;

namespace tankostream::addon {

struct ManifestExtraProp {
    QString name;
    bool isRequired = false;
    QStringList options;
    int optionsLimit = 1;
};

struct ManifestCatalog {
    QString id;
    QString type;
    QString name;
    QList<ManifestExtraProp> extra;
};

struct AddonManifest {
    QString id;
    QString name;
    QList<ManifestCatalog> catalogs;
};

struct AddonDescriptorFlags {
    bool official = false;
    bool enabled = true;
    bool protectedAddon = false;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;
    AddonDescriptorFlags flags;
};

struct MetaItemPreview {
    QString id;
    QString type;
    QString name;
    QString releaseInfo;
    QString imdbRating;
    QUrl poster;
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(QObject* parent = nullptr) : QObject(parent) {}
    QList<AddonDescriptor> list() const;
};

} // namespace tankostream::addon

namespace tankostream::stream {

struct CatalogQuery {
    QString addonId;
    QString type;
    QString catalogId;
    QList<QPair<QString, QString>> extra;
};

class CatalogAggregator : public QObject {
    Q_OBJECT
public:
    explicit CatalogAggregator(tankostream::addon::AddonRegistry* registry,
                               QObject* parent = nullptr)
        : QObject(parent)
        , m_registry(registry)
    {
    }
    void load(const CatalogQuery& query);
    void loadNextPage();

signals:
    void catalogPage(const QList<tankostream::addon::MetaItemPreview>& items, bool hasMore);
    void catalogError(const QString& addonId, const QString& message);

private:
    tankostream::addon::AddonRegistry* m_registry = nullptr;
};

class CatalogBrowseScreen : public QWidget {
    Q_OBJECT

public:
    explicit CatalogBrowseScreen(tankostream::addon::AddonRegistry* registry,
                                 QWidget* parent = nullptr)
        : QWidget(parent)
        , m_registry(registry)
        , m_aggregator(new CatalogAggregator(registry, this))
        , m_nam(new QNetworkAccessManager(this))
    {
        m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                           + "/Tankoban/data/stream_posters";
        QDir().mkpath(m_posterCacheDir);

        buildUi();
        rebuildSelectors();

        connect(m_aggregator, &CatalogAggregator::catalogPage, this,
            [this](const QList<tankostream::addon::MetaItemPreview>& items, bool hasMore) {
                appendTiles(items);
                m_loadMoreButton->setVisible(hasMore);
                m_statusLabel->setText(items.isEmpty() ? "No results" : "Loaded");
            });

        connect(m_aggregator, &CatalogAggregator::catalogError, this,
            [this](const QString& addonId, const QString& message) {
                m_statusLabel->setText("Catalog error (" + addonId + "): " + message);
            });
    }

    void open(const QString& addonId,
              const QString& type,
              const QString& catalogId)
    {
        selectAddon(addonId);
        selectCatalog(type, catalogId);
        reload();
    }

signals:
    void backRequested();
    void metaActivated(const QString& metaId, const QString& metaType);

private:
    struct CatalogItem {
        QString addonId;
        QString addonName;
        QString type;
        QString id;
        QString title;
        QList<tankostream::addon::ManifestExtraProp> extra;
    };

    void buildUi()
    {
        setObjectName("StreamCatalogBrowseScreen");
        setStyleSheet(
            "#StreamCatalogBrowseScreen { background: transparent; }"
            "#StreamCatalogTopBar { background: transparent; }"
            "#StreamCatalogFilterBar { background: transparent; }"
            "#StreamCatalogStatus { color: #9ca3af; font-size: 11px; }"
            "#StreamCatalogBack { color: #d1d5db; background: rgba(255,255,255,0.07);"
            " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px; padding: 4px 10px; }"
            "#StreamCatalogBack:hover { border-color: rgba(255,255,255,0.22); }"
            "#StreamCatalogLoadMore { color: #d1d5db; background: rgba(255,255,255,0.07);"
            " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px; padding: 6px 14px; }"
            "#StreamCatalogLoadMore:hover { border-color: rgba(255,255,255,0.22); }");

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 8, 16, 12);
        root->setSpacing(8);

        auto* topBar = new QHBoxLayout();
        topBar->setObjectName("StreamCatalogTopBar");
        topBar->setSpacing(8);

        auto* backButton = new QPushButton("Back", this);
        backButton->setObjectName("StreamCatalogBack");
        backButton->setCursor(Qt::PointingHandCursor);
        connect(backButton, &QPushButton::clicked, this, &CatalogBrowseScreen::backRequested);
        topBar->addWidget(backButton);

        m_addonCombo = new QComboBox(this);
        m_addonCombo->setMinimumWidth(220);
        topBar->addWidget(m_addonCombo);

        m_catalogCombo = new QComboBox(this);
        m_catalogCombo->setMinimumWidth(260);
        topBar->addWidget(m_catalogCombo);

        topBar->addStretch();
        root->addLayout(topBar);

        m_filterRow = new QWidget(this);
        m_filterRow->setObjectName("StreamCatalogFilterBar");
        m_filterLayout = new QHBoxLayout(m_filterRow);
        m_filterLayout->setContentsMargins(0, 0, 0, 0);
        m_filterLayout->setSpacing(8);
        root->addWidget(m_filterRow);

        m_statusLabel = new QLabel("Idle", this);
        m_statusLabel->setObjectName("StreamCatalogStatus");
        root->addWidget(m_statusLabel);

        auto* scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto* content = new QWidget(scroll);
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(10);

        m_strip = new TileStrip(content);
        contentLayout->addWidget(m_strip);

        m_loadMoreButton = new QPushButton("Load More", content);
        m_loadMoreButton->setObjectName("StreamCatalogLoadMore");
        m_loadMoreButton->setCursor(Qt::PointingHandCursor);
        m_loadMoreButton->hide();
        connect(m_loadMoreButton, &QPushButton::clicked, this, [this]() {
            m_loadMoreButton->setEnabled(false);
            m_aggregator->loadNextPage();
            m_loadMoreButton->setEnabled(true);
        });
        contentLayout->addWidget(m_loadMoreButton, 0, Qt::AlignCenter);
        contentLayout->addStretch();

        scroll->setWidget(content);
        root->addWidget(scroll, 1);

        connect(m_strip, &TileStrip::tileDoubleClicked, this, [this](TileCard* card) {
            const QString id = card->property("metaId").toString();
            const QString type = card->property("metaType").toString();
            if (!id.isEmpty()) {
                emit metaActivated(id, type);
            }
        });

        connect(m_addonCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            rebuildCatalogCombo();
            rebuildFilterBar();
            reload();
        });
        connect(m_catalogCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            rebuildFilterBar();
            reload();
        });
    }

    void rebuildSelectors()
    {
        m_catalogItems.clear();
        m_addonCombo->clear();

        if (!m_registry) {
            return;
        }

        // Build addon+catalog model from enabled addons.
        QHash<QString, QString> addonLabelById;
        for (const auto& addon : m_registry->list()) {
            if (!addon.flags.enabled) {
                continue;
            }
            addonLabelById.insert(addon.manifest.id, addon.manifest.name);

            for (const auto& c : addon.manifest.catalogs) {
                CatalogItem item;
                item.addonId = addon.manifest.id;
                item.addonName = addon.manifest.name;
                item.type = c.type;
                item.id = c.id;
                item.title = c.name.isEmpty() ? c.id : c.name;
                item.extra = c.extra;
                m_catalogItems.push_back(item);
            }
        }

        const QStringList addonIds = addonLabelById.keys();
        for (const QString& addonId : addonIds) {
            m_addonCombo->addItem(addonLabelById.value(addonId), addonId);
        }

        rebuildCatalogCombo();
        rebuildFilterBar();
    }

    void rebuildCatalogCombo()
    {
        m_catalogCombo->clear();
        const QString addonId = currentAddonId();
        for (int i = 0; i < m_catalogItems.size(); ++i) {
            const CatalogItem& c = m_catalogItems[i];
            if (c.addonId != addonId) {
                continue;
            }
            const QString text = c.title + " (" + c.type + ")";
            m_catalogCombo->addItem(text, i);
        }
    }

    void rebuildFilterBar()
    {
        while (QLayoutItem* item = m_filterLayout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }

        const CatalogItem* c = currentCatalog();
        if (!c) {
            return;
        }

        for (const auto& prop : c->extra) {
            if (prop.name == QStringLiteral("skip")) {
                continue;
            }
            if (prop.options.isEmpty()) {
                continue;
            }

            auto* label = new QLabel(prop.name + ":", m_filterRow);
            label->setStyleSheet("color: #9ca3af; font-size: 11px;");
            m_filterLayout->addWidget(label);

            auto* combo = new QComboBox(m_filterRow);
            combo->setObjectName("StreamCatalogFilterCombo");
            combo->addItem("Any", QString());

            // Respect options_limit by clipping options displayed to user.
            const int limit = qMax(1, prop.optionsLimit);
            const int count = qMin(limit, prop.options.size());
            for (int i = 0; i < count; ++i) {
                combo->addItem(prop.options[i], prop.options[i]);
            }

            combo->setProperty("extraName", prop.name);
            connect(combo, &QComboBox::currentIndexChanged, this, [this](int) { reload(); });
            m_filterLayout->addWidget(combo);
        }

        m_filterLayout->addStretch();
    }

    void reload()
    {
        const CatalogItem* c = currentCatalog();
        if (!c) {
            m_strip->clear();
            m_loadMoreButton->hide();
            return;
        }

        CatalogQuery q;
        q.addonId = c->addonId;
        q.type = c->type;
        q.catalogId = c->id;
        q.extra = gatherSelectedExtras();

        m_strip->clear();
        m_statusLabel->setText("Loading...");
        m_loadMoreButton->hide();
        m_aggregator->load(q);
    }

    QList<QPair<QString, QString>> gatherSelectedExtras() const
    {
        QList<QPair<QString, QString>> out;
        for (int i = 0; i < m_filterLayout->count(); ++i) {
            QWidget* w = m_filterLayout->itemAt(i)->widget();
            auto* combo = qobject_cast<QComboBox*>(w);
            if (!combo) {
                continue;
            }
            const QString key = combo->property("extraName").toString();
            const QString val = combo->currentData().toString();
            if (key.isEmpty() || val.isEmpty()) {
                continue;
            }
            out.push_back(qMakePair(key, val));
        }
        return out;
    }

    QString posterCachePath(const QString& metaId) const
    {
        return m_posterCacheDir + "/" + metaId + ".jpg";
    }

    void ensurePoster(const QString& metaId, const QUrl& posterUrl, TileCard* card)
    {
        if (!posterUrl.isValid() || !card) {
            return;
        }
        const QString path = posterCachePath(metaId);
        if (QFile::exists(path)) {
            card->setThumbPath(path);
            return;
        }

        QNetworkRequest req(posterUrl);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
        req.setTransferTimeout(10000);
        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, path, card]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                return;
            }
            const QByteArray bytes = reply->readAll();
            if (bytes.isEmpty()) {
                return;
            }
            QFile out(path);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(bytes);
                out.close();
                card->setThumbPath(path);
            }
        });
    }

    void appendTiles(const QList<tankostream::addon::MetaItemPreview>& items)
    {
        for (const auto& item : items) {
            if (item.id.isEmpty() || item.name.isEmpty()) {
                continue;
            }

            QString subtitle = item.releaseInfo;
            if (!item.imdbRating.isEmpty()) {
                if (!subtitle.isEmpty()) {
                    subtitle += " · ";
                }
                subtitle += item.imdbRating;
            }

            const QString cached = posterCachePath(item.id);
            auto* card = new TileCard(QFile::exists(cached) ? cached : QString(), item.name, subtitle);
            card->setProperty("metaId", item.id);
            card->setProperty("metaType", item.type);
            m_strip->addTile(card);

            if (!QFile::exists(cached)) {
                ensurePoster(item.id, item.poster, card);
            }
        }
    }

    QString currentAddonId() const
    {
        return m_addonCombo->currentData().toString();
    }

    const CatalogItem* currentCatalog() const
    {
        const int idx = m_catalogCombo->currentData().toInt(-1);
        if (idx < 0 || idx >= m_catalogItems.size()) {
            return nullptr;
        }
        return &m_catalogItems[idx];
    }

    void selectAddon(const QString& addonId)
    {
        for (int i = 0; i < m_addonCombo->count(); ++i) {
            if (m_addonCombo->itemData(i).toString() == addonId) {
                m_addonCombo->setCurrentIndex(i);
                return;
            }
        }
    }

    void selectCatalog(const QString& type, const QString& catalogId)
    {
        for (int i = 0; i < m_catalogCombo->count(); ++i) {
            const int sourceIdx = m_catalogCombo->itemData(i).toInt(-1);
            if (sourceIdx < 0 || sourceIdx >= m_catalogItems.size()) {
                continue;
            }
            const CatalogItem& c = m_catalogItems[sourceIdx];
            if (c.type == type && c.id == catalogId) {
                m_catalogCombo->setCurrentIndex(i);
                return;
            }
        }
    }

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
};

} // namespace tankostream::stream

// -----------------------------------------------------------------
// StreamPage integration sketch (Batch 3.3 scope only)
// -----------------------------------------------------------------
//
// 1) Add member:
//      CatalogBrowseScreen* m_catalogBrowse = nullptr;
//
// 2) In buildUI() after addon-manager layer (if present):
//      m_catalogBrowse = new CatalogBrowseScreen(m_addonRegistry, this);
//      m_mainStack->addWidget(m_catalogBrowse);   // expected index 4 when addons layer exists
//
// 3) Wire navigation:
//      connect(m_catalogBrowse, &CatalogBrowseScreen::backRequested, this, [this]() {
//          m_mainStack->setCurrentIndex(0); // StreamHomeBoard
//      });
//      connect(m_catalogBrowse, &CatalogBrowseScreen::metaActivated, this, &StreamPage::showDetail);
//
// 4) From StreamHomeBoard browse header action:
//      m_catalogBrowse->open(addonId, type, catalogId);
//      m_mainStack->setCurrentWidget(m_catalogBrowse);
//
