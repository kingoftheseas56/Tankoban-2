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
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>

#include "core/PosterFetcher.h"
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
namespace {

constexpr int kHomeRowInitialCap = 18;

class CatalogRow final : public QWidget
{
public:
    CatalogRow(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("StreamCatalogRow"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(4);

        auto* header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(8);

        m_title = new QLabel(title, this);
        m_title->setObjectName(QStringLiteral("StreamCatalogRowTitle"));
        header->addWidget(m_title);
        header->addStretch();

        m_seeAll = new QPushButton(QStringLiteral("See all"), this);
        m_seeAll->setObjectName(QStringLiteral("StreamCatalogSeeAll"));
        m_seeAll->setCursor(Qt::PointingHandCursor);
        m_seeAll->setFocusPolicy(Qt::NoFocus);
        header->addWidget(m_seeAll);
        root->addLayout(header);

        m_status = new QLabel(QStringLiteral("Loading..."), this);
        m_status->setObjectName(QStringLiteral("StreamCatalogRowStatus"));
        root->addWidget(m_status);

        m_strip = new TileStrip(this);
        m_strip->setMode(QStringLiteral("continue"));
        m_strip->setDensity(0);
        root->addWidget(m_strip);
    }

    TileStrip* strip() const { return m_strip; }
    QPushButton* seeAllButton() const { return m_seeAll; }

    void setLoading()
    {
        m_strip->clear();
        m_status->setText(QStringLiteral("Loading..."));
        m_status->show();
        m_seeAll->setEnabled(false);
    }

    void setLoaded(int count)
    {
        if (count > 0) {
            m_status->hide();
            m_seeAll->setEnabled(true);
            return;
        }
        m_status->setText(QStringLiteral("No results"));
        m_status->show();
        m_seeAll->setEnabled(false);
    }

    void setError(const QString& message)
    {
        m_strip->clear();
        m_status->setText(message);
        m_status->show();
        m_seeAll->setEnabled(false);
    }

private:
    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_seeAll = nullptr;
    TileStrip* m_strip = nullptr;
};

QString normalizedCatalogText(const QString& text)
{
    return text.trimmed().toLower();
}

} // namespace

struct CatalogBrowseScreen::RowState {
    int catalogIndex = -1;
    CatalogItem item;
    CatalogRow* row = nullptr;
    CatalogAggregator* aggregator = nullptr;
};

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

CatalogBrowseScreen::~CatalogBrowseScreen()
{
    clearRows();
}

void CatalogBrowseScreen::open(const QString& addonId,
                               const QString& type,
                               const QString& catalogId)
{
    rebuildSelectors();

    const int index = catalogIndexFor(addonId, type, catalogId);
    if (index >= 0) {
        showDetailForCatalog(index);
        return;
    }

    showHomeBoard();
}

void CatalogBrowseScreen::buildUi()
{
    setObjectName(QStringLiteral("StreamCatalogBrowseScreen"));
    setStyleSheet(QStringLiteral(
        "#StreamCatalogBrowseScreen { background: transparent; }"
        "#StreamCatalogStatus, #StreamCatalogRowStatus { color: #9ca3af; font-size: 11px; }"
        "#StreamCatalogTitle { color: #f3f4f6; font-size: 16px; font-weight: 700; }"
        "#StreamCatalogRowTitle { color: #f3f4f6; font-size: 13px; font-weight: 700; }"
        "#StreamCatalogBack, #StreamCatalogSeeAll, #StreamCatalogLoadMore {"
        " color: #d1d5db; background: rgba(255,255,255,0.07);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        " padding: 4px 10px; }"
        "#StreamCatalogLoadMore { padding: 6px 14px; }"
        "#StreamCatalogBack:hover, #StreamCatalogSeeAll:hover, #StreamCatalogLoadMore:hover {"
        " border-color: rgba(255,255,255,0.22); }"
        "#StreamCatalogSeeAll:disabled { color: rgba(255,255,255,0.28);"
        " border-color: rgba(255,255,255,0.07); background: rgba(255,255,255,0.03); }"));

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

    auto* title = new QLabel(QStringLiteral("Catalog"), this);
    title->setObjectName(QStringLiteral("StreamCatalogTitle"));
    topBar->addWidget(title);
    topBar->addStretch();
    root->addLayout(topBar);

    m_filterRow = new QWidget(this);
    m_filterLayout = new QHBoxLayout(m_filterRow);
    m_filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterLayout->setSpacing(8);
    m_filterRow->hide();
    root->addWidget(m_filterRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("StreamCatalogStatus"));
    root->addWidget(m_statusLabel);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_scrollContent = new QWidget(m_scroll);
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(12);

    m_strip = new TileStrip(m_scrollContent);
    m_strip->setDensity(0);
    m_strip->hide();
    m_contentLayout->addWidget(m_strip);

    m_loadMoreButton = new QPushButton(QStringLiteral("Load More"), m_scrollContent);
    m_loadMoreButton->setObjectName(QStringLiteral("StreamCatalogLoadMore"));
    m_loadMoreButton->setCursor(Qt::PointingHandCursor);
    m_loadMoreButton->hide();
    connect(m_loadMoreButton, &QPushButton::clicked, this, [this]() {
        if (m_activeCatalogIndex < 0) {
            return;
        }
        m_loadMoreButton->setEnabled(false);
        m_aggregator->loadNextPage();
        m_loadMoreButton->setEnabled(true);
    });
    m_contentLayout->addWidget(m_loadMoreButton, 0, Qt::AlignCenter);
    m_contentLayout->addStretch();

    m_scroll->setWidget(m_scrollContent);
    root->addWidget(m_scroll, 1);

    connectStripActivation(m_strip);
}

void CatalogBrowseScreen::rebuildSelectors()
{
    m_catalogItems.clear();

    if (!m_registry) {
        return;
    }

    for (const AddonDescriptor& addon : m_registry->list()) {
        if (!addon.flags.enabled) {
            continue;
        }
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
}

void CatalogBrowseScreen::clearFilterBar()
{
    while (QLayoutItem* item = m_filterLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void CatalogBrowseScreen::rebuildFilterBar()
{
    clearFilterBar();

    const CatalogItem* c = currentCatalog();
    if (!c) {
        m_filterRow->hide();
        return;
    }

    int controlCount = 0;
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
        ++controlCount;
    }

    m_filterLayout->addStretch();
    m_filterRow->setVisible(controlCount > 0);
}

void CatalogBrowseScreen::showHomeBoard()
{
    ++m_generation;
    m_activeCatalogIndex = -1;
    m_previewsById.clear();
    clearRows();
    clearFilterBar();
    m_filterRow->hide();
    m_strip->clear();
    m_strip->hide();
    m_loadMoreButton->hide();

    const QList<int> indices = sortedCatalogIndices();
    if (indices.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No catalogs available"));
        return;
    }
    m_statusLabel->clear();

    const int generation = m_generation;
    const int insertBefore = m_contentLayout->indexOf(m_strip);
    for (const int index : indices) {
        if (index < 0 || index >= m_catalogItems.size()) {
            continue;
        }

        auto* state = new RowState;
        state->catalogIndex = index;
        state->item = m_catalogItems.at(index);
        state->row = new CatalogRow(displayTitleForCatalog(state->item), m_scrollContent);
        state->aggregator = new CatalogAggregator(m_registry, this);
        m_rows.append(state);

        m_contentLayout->insertWidget(insertBefore + m_rows.size() - 1, state->row);
        connectStripActivation(state->row->strip());
        connect(state->row->seeAllButton(), &QPushButton::clicked, this,
                [this, index]() { showDetailForCatalog(index); });

        state->row->setLoading();

        connect(state->aggregator, &CatalogAggregator::catalogPage, this,
            [this, state, generation](const QList<MetaItemPreview>& items, bool) {
                if (generation != m_generation || !m_rows.contains(state)) {
                    return;
                }
                appendRowTiles(state, items);
            });
        connect(state->aggregator, &CatalogAggregator::catalogError, this,
            [this, state, generation](const QString& addonId, const QString& message) {
                if (generation != m_generation || !m_rows.contains(state) || !state->row) {
                    return;
                }
                state->row->setError(QStringLiteral("Catalog error (") + addonId +
                                     QStringLiteral("): ") + message);
            });

        CatalogQuery q;
        q.addonId = state->item.addonId;
        q.type = state->item.type;
        q.catalogId = state->item.id;
        state->aggregator->load(q);
    }
}

void CatalogBrowseScreen::showDetailForCatalog(int catalogIndex)
{
    if (catalogIndex < 0 || catalogIndex >= m_catalogItems.size()) {
        showHomeBoard();
        return;
    }

    ++m_generation;
    clearRows();
    m_activeCatalogIndex = catalogIndex;
    m_previewsById.clear();
    m_strip->clear();
    m_strip->show();
    rebuildFilterBar();
    reload();
}

void CatalogBrowseScreen::clearRows()
{
    for (RowState* row : m_rows) {
        if (!row) {
            continue;
        }
        if (row->aggregator) {
            row->aggregator->disconnect(this);
            delete row->aggregator;
        }
        delete row->row;
        delete row;
    }
    m_rows.clear();
}

QList<int> CatalogBrowseScreen::sortedCatalogIndices() const
{
    QList<int> indices;
    indices.reserve(m_catalogItems.size());
    for (int i = 0; i < m_catalogItems.size(); ++i) {
        indices.append(i);
    }

    auto rankFor = [this](int index) {
        const CatalogItem& item = m_catalogItems.at(index);
        const QString type = item.type.toLower();
        const QString text = normalizedCatalogText(displayTitleForCatalog(item) + QLatin1Char(' ') + item.id);
        const bool movie = type == QLatin1String("movie");
        const bool series = type == QLatin1String("series");
        if (movie && text.contains(QStringLiteral("popular"))) return 0;
        if (series && text.contains(QStringLiteral("popular"))) return 1;
        if (movie && text.contains(QStringLiteral("featured"))) return 2;
        if (series && text.contains(QStringLiteral("featured"))) return 3;
        if (movie && text.contains(QStringLiteral("seeded"))) return 4;
        if (series && text.contains(QStringLiteral("seeded"))) return 5;
        return 1000 + index;
    };

    std::stable_sort(indices.begin(), indices.end(),
        [&](int a, int b) {
            const int ra = rankFor(a);
            const int rb = rankFor(b);
            if (ra != rb) {
                return ra < rb;
            }
            return a < b;
        });
    return indices;
}

QString CatalogBrowseScreen::displayTitleForCatalog(const CatalogItem& item) const
{
    const QString raw = item.title.trimmed().isEmpty() ? item.id : item.title.trimmed();
    const QString lowered = raw.toLower();
    if (lowered.contains(QStringLiteral("movie"))
        || lowered.contains(QStringLiteral("show"))
        || lowered.contains(QStringLiteral("series"))) {
        return raw;
    }

    const QString type = item.type.toLower();
    if (type == QLatin1String("movie")) {
        return QStringLiteral("Movies ") + raw;
    }
    if (type == QLatin1String("series")) {
        const QString prefix = lowered.contains(QStringLiteral("seeded"))
                                   ? QStringLiteral("Series ")
                                   : QStringLiteral("Shows ");
        return prefix + raw;
    }
    return item.type.toUpper() + QLatin1Char(' ') + raw;
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
    m_statusLabel->setText(QStringLiteral("Loading ") + displayTitleForCatalog(*c) +
                           QStringLiteral("..."));
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
    if (!card) {
        return;
    }

    const QString path = posterCachePath(metaId);
    if (QFile::exists(path)) {
        if (!QPixmap(path).isNull()) {
            card->setThumbPath(path);
            return;
        }
        QFile::remove(path);
    }

    if (!posterUrl.isValid() || posterUrl.isEmpty()) {
        qInfo("CatalogBrowseScreen: poster url empty for %s",
              qUtf8Printable(metaId));
        return;
    }

    QPointer<TileCard> guard(card);
    PosterFetcher::download(m_nam, posterUrl, path, this,
        [guard, path](bool ok) {
            if (ok && guard) {
                guard->setThumbPath(path);
            }
        });
}

TileCard* CatalogBrowseScreen::makeTile(const MetaItemPreview& item)
{
    if (item.id.isEmpty() || item.name.isEmpty()) {
        return nullptr;
    }

    QString subtitle = item.releaseInfo;
    if (!item.imdbRating.isEmpty()) {
        if (!subtitle.isEmpty()) {
            subtitle += QStringLiteral(" \u00B7 ");
        }
        subtitle += QStringLiteral("IMDb ") + item.imdbRating;
    }

    const QString cached = posterCachePath(item.id);
    const bool hasUsableCachedPoster = QFile::exists(cached) && !QPixmap(cached).isNull();
    if (QFile::exists(cached) && !hasUsableCachedPoster) {
        QFile::remove(cached);
    }

    auto* card = new TileCard(hasUsableCachedPoster ? cached : QString(),
                              item.name,
                              subtitle);
    card->setProperty("metaId", item.id);
    card->setProperty("metaType", item.type);
    m_previewsById.insert(item.id, item);

    if (!hasUsableCachedPoster) {
        ensurePoster(item.id, item.poster, card);
    }

    return card;
}

void CatalogBrowseScreen::connectStripActivation(TileStrip* strip)
{
    if (!strip) {
        return;
    }

    auto activate = [this](TileCard* card) {
        if (!card) {
            return;
        }
        const QString id = card->property("metaId").toString();
        if (id.isEmpty()) {
            return;
        }
        const auto it = m_previewsById.constFind(id);
        if (it == m_previewsById.constEnd()) {
            return;
        }
        emit metaActivated(it.value());
    };
    connect(strip, &TileStrip::tileSingleClicked, this, activate);
    connect(strip, &TileStrip::tileDoubleClicked, this, activate);
}

void CatalogBrowseScreen::appendRowTiles(RowState* row,
                                         const QList<MetaItemPreview>& items)
{
    if (!row || !row->row) {
        return;
    }

    row->row->strip()->clear();
    int rendered = 0;
    for (const MetaItemPreview& item : items) {
        if (rendered >= kHomeRowInitialCap) {
            break;
        }
        TileCard* card = makeTile(item);
        if (!card) {
            continue;
        }
        row->row->strip()->addTile(card);
        ++rendered;
    }
    row->row->setLoaded(rendered);
}

void CatalogBrowseScreen::appendTiles(const QList<MetaItemPreview>& items)
{
    for (const MetaItemPreview& item : items) {
        TileCard* card = makeTile(item);
        if (!card) {
            continue;
        }
        m_strip->addTile(card);
    }
}

const CatalogBrowseScreen::CatalogItem* CatalogBrowseScreen::currentCatalog() const
{
    if (m_activeCatalogIndex < 0 || m_activeCatalogIndex >= m_catalogItems.size()) {
        return nullptr;
    }
    return &m_catalogItems[m_activeCatalogIndex];
}

int CatalogBrowseScreen::catalogIndexFor(const QString& addonId,
                                         const QString& type,
                                         const QString& catalogId) const
{
    if (addonId.isEmpty() && type.isEmpty() && catalogId.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_catalogItems.size(); ++i) {
        const CatalogItem& c = m_catalogItems.at(i);
        if (!addonId.isEmpty() && c.addonId != addonId) {
            continue;
        }
        if (!type.isEmpty() && c.type != type) {
            continue;
        }
        if (!catalogId.isEmpty() && c.id != catalogId) {
            continue;
        }
        return i;
    }

    return -1;
}

} // namespace tankostream::stream
