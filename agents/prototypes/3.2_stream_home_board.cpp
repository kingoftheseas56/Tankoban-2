// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 3.2 (Catalog Browsing)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:148
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:151
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:152
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:153
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:81
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:183
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamContinueStrip.h:11
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamContinueStrip.cpp:43
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamLibraryLayout.cpp:27
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamSearchWidget.cpp:25
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:26
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:53
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalogs_with_extra.rs:173
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/continue_watching_preview.rs:29
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/continue_watching_preview.rs:117
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 3.2.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

class CoreBridge;
class StreamLibrary;
class StreamContinueStrip;
class TileStrip;
class TileCard;

namespace tankostream::addon {

struct ManifestCatalog {
    QString id;
    QString type;
    QString name;
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
    QUrl poster;
    QString releaseInfo;
    QString imdbRating;
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

signals:
    void catalogPage(const QList<tankostream::addon::MetaItemPreview>& items, bool hasMore);
    void catalogError(const QString& addonId, const QString& message);

private:
    tankostream::addon::AddonRegistry* m_registry = nullptr;
};

struct HomeCatalogSpec {
    QString addonId;
    QString addonName;
    QString type;
    QString catalogId;
    QString title;
};

class HomeCatalogRow : public QFrame {
    Q_OBJECT

public:
    explicit HomeCatalogRow(const HomeCatalogSpec& spec,
                            tankostream::addon::AddonRegistry* registry,
                            QWidget* parent = nullptr)
        : QFrame(parent)
        , m_spec(spec)
        , m_registry(registry)
        , m_aggregator(new CatalogAggregator(registry, this))
        , m_nam(new QNetworkAccessManager(this))
    {
        m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                           + "/Tankoban/data/stream_posters";
        QDir().mkpath(m_posterCacheDir);
        buildUi();

        connect(m_aggregator, &CatalogAggregator::catalogPage, this,
            [this](const QList<tankostream::addon::MetaItemPreview>& items, bool) {
                render(items);
            });
    }

    void refresh()
    {
        CatalogQuery q;
        q.addonId = m_spec.addonId;
        q.type = m_spec.type;
        q.catalogId = m_spec.catalogId;
        m_aggregator->load(q);
    }

signals:
    void browseRequested(const HomeCatalogSpec& spec);
    void metaActivated(const QString& metaId, const QString& metaType);

private:
    void buildUi()
    {
        setObjectName("StreamHomeCatalogRow");
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(6);

        auto* header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(8);

        auto* label = new QLabel(m_spec.title, this);
        label->setObjectName("StreamHomeCatalogTitle");
        label->setStyleSheet("#StreamHomeCatalogTitle { color: #d1d5db; font-size: 12px; font-weight: 600; }");
        header->addWidget(label);
        header->addStretch();

        auto* openButton = new QPushButton("Browse", this);
        openButton->setObjectName("StreamHomeCatalogBrowse");
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setFixedHeight(24);
        openButton->setStyleSheet(
            "#StreamHomeCatalogBrowse { color: #d1d5db; background: rgba(255,255,255,0.07);"
            " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px; padding: 2px 10px; }"
            "#StreamHomeCatalogBrowse:hover { border-color: rgba(255,255,255,0.22); }");
        connect(openButton, &QPushButton::clicked, this, [this]() { emit browseRequested(m_spec); });
        header->addWidget(openButton);

        root->addLayout(header);

        m_strip = new TileStrip(this);
        root->addWidget(m_strip);

        // Keep card interaction aligned with existing StreamLibraryLayout behavior.
        connect(m_strip, &TileStrip::tileDoubleClicked, this, [this](TileCard* card) {
            const QString id = card->property("metaId").toString();
            const QString type = card->property("metaType").toString();
            if (!id.isEmpty()) {
                emit metaActivated(id, type);
            }
        });
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

    void render(const QList<tankostream::addon::MetaItemPreview>& items)
    {
        m_strip->clear();
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

    HomeCatalogSpec m_spec;
    tankostream::addon::AddonRegistry* m_registry = nullptr;
    CatalogAggregator* m_aggregator = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    TileStrip* m_strip = nullptr;
    QString m_posterCacheDir;
};

class StreamHomeBoard : public QWidget {
    Q_OBJECT

public:
    explicit StreamHomeBoard(CoreBridge* bridge,
                             StreamLibrary* library,
                             tankostream::addon::AddonRegistry* registry,
                             QWidget* parent = nullptr)
        : QWidget(parent)
        , m_bridge(bridge)
        , m_library(library)
        , m_registry(registry)
    {
        buildUi();
    }

    void refresh()
    {
        clearRows();
        const QList<HomeCatalogSpec> all = enumerateCatalogs();
        const QList<HomeCatalogSpec> selected = chooseFeaturedCatalogs(all);

        for (const HomeCatalogSpec& spec : selected) {
            auto* row = new HomeCatalogRow(spec, m_registry, m_rowsHost);
            m_rowsLayout->addWidget(row);
            m_rows.push_back(row);

            connect(row, &HomeCatalogRow::browseRequested, this, [this](const HomeCatalogSpec& s) {
                emit browseCatalogRequested(s.addonId, s.type, s.catalogId, s.title);
            });
            connect(row, &HomeCatalogRow::metaActivated, this, &StreamHomeBoard::metaActivated);
            row->refresh();
        }

        m_rowsLayout->addStretch();
    }

signals:
    void browseCatalogRequested(const QString& addonId,
                                const QString& type,
                                const QString& catalogId,
                                const QString& title);
    void metaActivated(const QString& metaId, const QString& metaType);

private:
    static QString specKey(const HomeCatalogSpec& s)
    {
        return s.addonId + "|" + s.type + "|" + s.catalogId;
    }

    void buildUi()
    {
        setObjectName("StreamHomeBoard");
        setStyleSheet(
            "#StreamHomeBoard { background: transparent; }"
            "#StreamHomeRowsHost { background: transparent; }");

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(20);

        // Existing continue strip remains first element, matching current StreamPage flow.
        m_continueStrip = new StreamContinueStrip(m_bridge, m_library, this);
        root->addWidget(m_continueStrip);

        m_rowsHost = new QWidget(this);
        m_rowsHost->setObjectName("StreamHomeRowsHost");
        m_rowsLayout = new QVBoxLayout(m_rowsHost);
        m_rowsLayout->setContentsMargins(0, 0, 0, 0);
        m_rowsLayout->setSpacing(20);
        root->addWidget(m_rowsHost, 1);
    }

    void clearRows()
    {
        for (HomeCatalogRow* row : m_rows) {
            row->deleteLater();
        }
        m_rows.clear();

        while (QLayoutItem* item = m_rowsLayout->takeAt(0)) {
            delete item;
        }
    }

    QList<HomeCatalogSpec> enumerateCatalogs() const
    {
        QList<HomeCatalogSpec> out;
        if (!m_registry) {
            return out;
        }

        QSet<QString> seen;
        for (const auto& addon : m_registry->list()) {
            if (!addon.flags.enabled) {
                continue;
            }
            for (const auto& catalog : addon.manifest.catalogs) {
                HomeCatalogSpec s;
                s.addonId = addon.manifest.id;
                s.addonName = addon.manifest.name;
                s.type = catalog.type;
                s.catalogId = catalog.id;
                s.title = catalog.name.isEmpty()
                    ? (addon.manifest.name + " / " + catalog.id)
                    : catalog.name;

                const QString key = specKey(s);
                if (seen.contains(key)) {
                    continue;
                }
                seen.insert(key);
                out.push_back(s);
            }
        }
        return out;
    }

    QList<HomeCatalogSpec> chooseFeaturedCatalogs(const QList<HomeCatalogSpec>& all) const
    {
        QHash<QString, HomeCatalogSpec> byKey;
        for (const HomeCatalogSpec& s : all) {
            byKey.insert(specKey(s), s);
        }

        QSettings settings("Tankoban", "Tankoban");
        const QStringList saved = settings.value("stream_home_catalogs").toStringList();
        QList<HomeCatalogSpec> out;

        for (const QString& key : saved) {
            const auto it = byKey.find(key);
            if (it == byKey.end()) {
                continue;
            }
            out.push_back(it.value());
            if (out.size() >= 6) {
                return out;
            }
        }

        if (!out.isEmpty()) {
            return out;
        }

        // First-run default: 4-6 rows.
        const int count = qMin(6, qMax(4, all.size()));
        for (int i = 0; i < all.size() && out.size() < count; ++i) {
            out.push_back(all[i]);
        }

        QStringList keys;
        for (const HomeCatalogSpec& s : out) {
            keys.push_back(specKey(s));
        }
        settings.setValue("stream_home_catalogs", keys);
        return out;
    }

    CoreBridge* m_bridge = nullptr;
    StreamLibrary* m_library = nullptr;
    tankostream::addon::AddonRegistry* m_registry = nullptr;

    StreamContinueStrip* m_continueStrip = nullptr;
    QWidget* m_rowsHost = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    QList<HomeCatalogRow*> m_rows;
};

} // namespace tankostream::stream

// -----------------------------------------------------------------
// StreamPage integration sketch (Batch 3.2 scope only)
// -----------------------------------------------------------------
//
// 1) Replace current browse home content with StreamHomeBoard:
//      m_homeBoard = new StreamHomeBoard(m_bridge, m_library, m_addonRegistry, m_browseLayer);
//      layerLayout->addWidget(m_homeBoard, 1);
//
// 2) Keep existing StreamSearchWidget overlay behavior:
//      Search still hides board and shows search overlay.
//
// 3) Wire interactions:
//      connect(m_homeBoard, &StreamHomeBoard::metaActivated, this, &StreamPage::showDetail);
//      connect(m_homeBoard, &StreamHomeBoard::browseCatalogRequested, this, &StreamPage::showCatalogBrowse);
//
// 4) Refresh path:
//      StreamPage::activate() should call m_homeBoard->refresh().
//
