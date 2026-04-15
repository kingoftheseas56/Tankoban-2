#include "CatalogBrowseScreen.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLayoutItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "core/stream/CatalogAggregator.h"
#include "core/stream/addon/AddonRegistry.h"
#include "core/stream/addon/Descriptor.h"
#include "core/stream/addon/MetaItem.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::ManifestCatalog;
using tankostream::addon::ManifestExtraProp;
using tankostream::addon::MetaItemPreview;

namespace tankostream::stream {

CatalogBrowseScreen::CatalogBrowseScreen(AddonRegistry* registry, QWidget* parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_aggregator(new CatalogAggregator(registry, this))
    , m_nam(new QNetworkAccessManager(this))
{
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + QStringLiteral("/Tankoban/data/stream_posters");
    QDir().mkpath(m_posterCacheDir);

    buildUi();
    rebuildSelectors();

    connect(m_aggregator, &CatalogAggregator::catalogPage, this,
        [this](const QList<MetaItemPreview>& items, bool hasMore) {
            appendTiles(items);
            m_loadMoreButton->setVisible(hasMore);
            m_statusLabel->setText(items.isEmpty()
                                       ? QStringLiteral("No results")
                                       : QStringLiteral("Loaded"));
        });

    connect(m_aggregator, &CatalogAggregator::catalogError, this,
        [this](const QString& addonId, const QString& message) {
            m_statusLabel->setText(QStringLiteral("Catalog error (") + addonId +
                                   QStringLiteral("): ") + message);
        });
}

void CatalogBrowseScreen::open(const QString& addonId,
                               const QString& type,
                               const QString& catalogId)
{
    rebuildSelectors();

    m_suppressReload = true;
    selectAddon(addonId);
    rebuildCatalogCombo();
    selectCatalog(type, catalogId);
    rebuildFilterBar();
    m_suppressReload = false;

    reload();
}

void CatalogBrowseScreen::buildUi()
{
    setObjectName(QStringLiteral("StreamCatalogBrowseScreen"));
    setStyleSheet(QStringLiteral(
        "#StreamCatalogBrowseScreen { background: transparent; }"
        "#StreamCatalogStatus { color: #9ca3af; font-size: 11px; }"
        "#StreamCatalogBack { color: #d1d5db; background: rgba(255,255,255,0.07);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        " padding: 4px 10px; }"
        "#StreamCatalogBack:hover { border-color: rgba(255,255,255,0.22); }"
        "#StreamCatalogLoadMore { color: #d1d5db; background: rgba(255,255,255,0.07);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        " padding: 6px 14px; }"
        "#StreamCatalogLoadMore:hover { border-color: rgba(255,255,255,0.22); }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 8, 16, 12);
    root->setSpacing(8);

    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    auto* backButton = new QPushButton(QStringLiteral("Back"), this);
    backButton->setObjectName(QStringLiteral("StreamCatalogBack"));
    backButton->setCursor(Qt::PointingHandCursor);
    connect(backButton, &QPushButton::clicked,
            this, &CatalogBrowseScreen::backRequested);
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
    m_filterLayout = new QHBoxLayout(m_filterRow);
    m_filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterLayout->setSpacing(8);
    root->addWidget(m_filterRow);

    m_statusLabel = new QLabel(QStringLiteral("Idle"), this);
    m_statusLabel->setObjectName(QStringLiteral("StreamCatalogStatus"));
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

    m_loadMoreButton = new QPushButton(QStringLiteral("Load More"), content);
    m_loadMoreButton->setObjectName(QStringLiteral("StreamCatalogLoadMore"));
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

    // Phase 1 Batch 1.1 — emit with full MetaItemPreview payload. Both
    // single-click and double-click open detail (paired-fire is filtered
    // by StreamPage::showDetail's idempotency guard).
    auto activate = [this](TileCard* card) {
        const QString id = card->property("metaId").toString();
        if (id.isEmpty()) return;
        const auto it = m_previewsById.constFind(id);
        if (it == m_previewsById.constEnd()) return;
        emit metaActivated(it.value());
    };
    connect(m_strip, &TileStrip::tileSingleClicked, this, activate);
    connect(m_strip, &TileStrip::tileDoubleClicked, this, activate);

    connect(m_addonCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_suppressReload) {
            return;
        }
        rebuildCatalogCombo();
        rebuildFilterBar();
        reload();
    });
    connect(m_catalogCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_suppressReload) {
            return;
        }
        rebuildFilterBar();
        reload();
    });
}

void CatalogBrowseScreen::rebuildSelectors()
{
    m_suppressReload = true;
    m_catalogItems.clear();
    m_addonCombo->clear();

    if (!m_registry) {
        m_suppressReload = false;
        return;
    }

    QHash<QString, QString> addonLabelById;
    for (const AddonDescriptor& addon : m_registry->list()) {
        if (!addon.flags.enabled) {
            continue;
        }
        addonLabelById.insert(addon.manifest.id, addon.manifest.name);
        for (const ManifestCatalog& c : addon.manifest.catalogs) {
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

    for (auto it = addonLabelById.constBegin(); it != addonLabelById.constEnd(); ++it) {
        m_addonCombo->addItem(it.value(), it.key());
    }

    rebuildCatalogCombo();
    rebuildFilterBar();
    m_suppressReload = false;
}

void CatalogBrowseScreen::rebuildCatalogCombo()
{
    const bool wasSuppressed = m_suppressReload;
    m_suppressReload = true;
    m_catalogCombo->clear();

    const QString addonId = currentAddonId();
    for (int i = 0; i < m_catalogItems.size(); ++i) {
        const CatalogItem& c = m_catalogItems[i];
        if (c.addonId != addonId) {
            continue;
        }
        const QString text = c.title + QStringLiteral(" (") + c.type + QStringLiteral(")");
        m_catalogCombo->addItem(text, i);
    }
    m_suppressReload = wasSuppressed;
}

void CatalogBrowseScreen::rebuildFilterBar()
{
    while (QLayoutItem* item = m_filterLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    const CatalogItem* c = currentCatalog();
    if (!c) {
        return;
    }

    for (const ManifestExtraProp& prop : c->extra) {
        if (prop.name == QStringLiteral("skip")) {
            continue;
        }
        if (prop.options.isEmpty()) {
            continue;
        }

        auto* label = new QLabel(prop.name + QStringLiteral(":"), m_filterRow);
        label->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 11px;"));
        m_filterLayout->addWidget(label);

        auto* combo = new QComboBox(m_filterRow);
        combo->setObjectName(QStringLiteral("StreamCatalogFilterCombo"));
        combo->addItem(QStringLiteral("Any"), QString());

        const int limit = qMax(1, prop.optionsLimit);
        const int count = qMin(limit, static_cast<int>(prop.options.size()));
        for (int i = 0; i < count; ++i) {
            combo->addItem(prop.options[i], prop.options[i]);
        }

        combo->setProperty("extraName", prop.name);
        connect(combo, &QComboBox::currentIndexChanged, this, [this](int) {
            if (!m_suppressReload) {
                reload();
            }
        });
        m_filterLayout->addWidget(combo);
    }

    m_filterLayout->addStretch();
}

void CatalogBrowseScreen::reload()
{
    const CatalogItem* c = currentCatalog();
    if (!c) {
        m_strip->clear();
        m_previewsById.clear();
        m_loadMoreButton->hide();
        m_statusLabel->setText(QStringLiteral("No catalog selected"));
        return;
    }

    CatalogQuery q;
    q.addonId = c->addonId;
    q.type = c->type;
    q.catalogId = c->id;
    q.extra = gatherSelectedExtras();

    m_strip->clear();
    m_previewsById.clear();
    m_statusLabel->setText(QStringLiteral("Loading..."));
    m_loadMoreButton->hide();
    m_aggregator->load(q);
}

QList<QPair<QString, QString>> CatalogBrowseScreen::gatherSelectedExtras() const
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
        out.append(qMakePair(key, val));
    }
    return out;
}

QString CatalogBrowseScreen::posterCachePath(const QString& metaId) const
{
    return m_posterCacheDir + QLatin1Char('/') + metaId + QStringLiteral(".jpg");
}

void CatalogBrowseScreen::ensurePoster(const QString& metaId,
                                       const QUrl& posterUrl,
                                       TileCard* card)
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
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                 " AppleWebKit/537.36"));
    req.setTransferTimeout(10000);

    QPointer<TileCard> guard(card);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, path, guard]() {
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
            if (guard) {
                guard->setThumbPath(path);
            }
        }
    });
}

void CatalogBrowseScreen::appendTiles(const QList<MetaItemPreview>& items)
{
    for (const MetaItemPreview& item : items) {
        if (item.id.isEmpty() || item.name.isEmpty()) {
            continue;
        }

        QString subtitle = item.releaseInfo;
        if (!item.imdbRating.isEmpty()) {
            if (!subtitle.isEmpty()) {
                subtitle += QStringLiteral(" \u00B7 ");
            }
            subtitle += item.imdbRating;
        }

        const QString cached = posterCachePath(item.id);
        auto* card = new TileCard(
            QFile::exists(cached) ? cached : QString(),
            item.name,
            subtitle);
        card->setProperty("metaId", item.id);
        card->setProperty("metaType", item.type);
        m_strip->addTile(card);
        m_previewsById.insert(item.id, item);

        if (!QFile::exists(cached)) {
            ensurePoster(item.id, item.poster, card);
        }
    }
}

QString CatalogBrowseScreen::currentAddonId() const
{
    return m_addonCombo->currentData().toString();
}

const CatalogBrowseScreen::CatalogItem* CatalogBrowseScreen::currentCatalog() const
{
    const QVariant v = m_catalogCombo->currentData();
    const int idx = v.canConvert<int>() ? v.toInt() : -1;
    if (idx < 0 || idx >= m_catalogItems.size()) {
        return nullptr;
    }
    return &m_catalogItems[idx];
}

void CatalogBrowseScreen::selectAddon(const QString& addonId)
{
    for (int i = 0; i < m_addonCombo->count(); ++i) {
        if (m_addonCombo->itemData(i).toString() == addonId) {
            m_addonCombo->setCurrentIndex(i);
            return;
        }
    }
}

void CatalogBrowseScreen::selectCatalog(const QString& type, const QString& catalogId)
{
    for (int i = 0; i < m_catalogCombo->count(); ++i) {
        const QVariant itemData = m_catalogCombo->itemData(i);
        const int sourceIdx = itemData.canConvert<int>() ? itemData.toInt() : -1;
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

}
