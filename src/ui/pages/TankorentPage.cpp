#include "TankorentPage.h"
#include "core/CoreBridge.h"
#include "core/DebugLogBuffer.h"
#include "core/TankorentSearchService.h"
#include "core/TorrentIndexer.h"
#include "core/indexers/TorrentsCsvIndexer.h"
#include "core/indexers/NyaaIndexer.h"
#include "core/indexers/PirateBayIndexer.h"
#include "core/indexers/YtsIndexer.h"
#include "core/indexers/EztvIndexer.h"
#include "core/indexers/X1337xIndexer.h"
#include "core/indexers/ExtTorrentsIndexer.h"

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "ui/dialogs/AddTorrentDialog.h"
#include "ui/dialogs/SpeedLimitDialog.h"
#include "ui/dialogs/SeedingRulesDialog.h"
#include "ui/dialogs/QueueLimitsDialog.h"
#include "ui/dialogs/HistoryDialog.h"
#include "ui/dialogs/AddFromUrlDialog.h"
#include "ui/pages/IndexerStatusPanel.h"
#include "ui/pages/tankorent/TorrentPropertiesWidget.h"
#include "ui/widgets/Toast.h"

#include <QPainter>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSettings>
#include <QToolButton>
#include <QClipboard>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QNetworkAccessManager>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QRegularExpression>
#include <QTimer>
#include <QIcon>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QUrl>
#include <QMessageBox>
#include <QProgressBar>
#include <QStackedWidget>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QScrollBar>
#include <QTextEdit>
#include <QFileInfo>
#include <algorithm>

#include "ui/ContextMenuHelper.h"

// ══════════════════════════════════════════════════════════════════════════════
// Per-site category options — faithfully ported from _SITE_CATEGORY_OPTIONS
// Each entry: { display_text, value }
// ══════════════════════════════════════════════════════════════════════════════

// T3 — defensive fall-through for muted-secondary-text color. If Theme.cpp ever
// adds a __FG_MUTED__ token + palette.fgMuted field, swap this for a Theme-token
// read. Reads QApplication::palette() Text color at ~55% opacity, which
// approximates the muted-secondary color across every palette without needing
// Agent 5 coordination today.
static inline QColor fgMutedColor()
{
    QColor c = QApplication::palette().color(QPalette::Text);
    c.setAlpha(140);
    return c;
}

// T11 — three-segment title data stored in Qt::UserRole+1 on the Title cell.
struct TitleSegments {
    QString source;
    QString title;
    QString quality;
};
Q_DECLARE_METATYPE(TitleSegments)

struct CategoryOption { const char* label; const char* value; };

static const CategoryOption NYAA_CATEGORIES[] = {
    {"All categories",             "0_0"},
    {"Anime - Music Video",        "1_1"},
    {"Anime - English-translated", "1_2"},
    {"Anime - Non-English",        "1_3"},
    {"Anime - Raw",                "1_4"},
    {"Audio - Lossless",           "2_1"},
    {"Audio - Lossy",              "2_2"},
    {"Literature - English",       "3_1"},
    {"Literature - Non-English",   "3_2"},
    {"Literature - Raw",           "3_3"},
    {"Live Action - English",      "4_1"},
    {"Live Action - Idol/PV",      "4_2"},
    {"Live Action - Non-English",  "4_3"},
    {"Live Action - Raw",          "4_4"},
    {"Pictures - Graphics",        "5_1"},
    {"Pictures - Photos",          "5_2"},
    {"Software - Applications",    "6_1"},
    {"Software - Games",           "6_2"},
    {nullptr, nullptr}
};

static const CategoryOption PIRATEBAY_CATEGORIES[] = {
    {"All categories", ""},
    {"Audio",          "100"},
    {"Video",          "200"},
    {"Applications",   "300"},
    {"Games",          "400"},
    {"Porn",           "500"},
    {"Other",          "600"},
    {nullptr, nullptr}
};

static const CategoryOption EXTTORRENTS_CATEGORIES[] = {
    {"All categories", ""},
    {"Movies",         "Movies"},
    {"TV",             "TV"},
    {"Games",          "Games"},
    {"Music",          "Music"},
    {"Apps",           "Apps"},
    {"Documentaries",  "Documentaries"},
    {"Anime",          "Anime"},
    {"Books",          "Books"},
    {"Other",          "Other"},
    {"XXX",            "XXX"},
    {nullptr, nullptr}
};

static const CategoryOption YTS_CATEGORIES[] = {
    {"All genres",   ""},
    {"Action",       "action"},
    {"Adventure",    "adventure"},
    {"Animation",    "animation"},
    {"Biography",    "biography"},
    {"Comedy",       "comedy"},
    {"Crime",        "crime"},
    {"Documentary",  "documentary"},
    {"Drama",        "drama"},
    {"Family",       "family"},
    {"Fantasy",      "fantasy"},
    {"Film-Noir",    "film-noir"},
    {"History",      "history"},
    {"Horror",       "horror"},
    {"Music",        "music"},
    {"Musical",      "musical"},
    {"Mystery",      "mystery"},
    {"Romance",      "romance"},
    {"Sci-Fi",       "sci-fi"},
    {"Sport",        "sport"},
    {"Thriller",     "thriller"},
    {"War",          "war"},
    {"Western",      "western"},
    {nullptr, nullptr}
};

static const CategoryOption EZTV_CATEGORIES[] = {
    {"All TV", ""},
    {nullptr, nullptr}
};

static const CategoryOption TORRENTSCSV_CATEGORIES[] = {
    {"All categories", ""},
    {nullptr, nullptr}
};

// 1337x has 80+ categories — abbreviated to major groups
static const CategoryOption X1337X_CATEGORIES[] = {
    {"All categories",      ""},
    {"Movies",              "Movies"},
    {"Movies/HD",           "42"},
    {"Movies/Cam/TS",       "43"},
    {"Movies/DVD",          "66"},
    {"Movies/Bollywood",    "68"},
    {"TV",                  "TV"},
    {"TV/HD",               "41"},
    {"TV/SD",               "71"},
    {"TV/Foreign",          "76"},
    {"Anime",               "28"},
    {"Games",               "Games"},
    {"Games/PC",            "10"},
    {"Games/PS",            "43"},
    {"Games/Xbox",          "44"},
    {"Games/Nintendo",      "45"},
    {"Music",               "Music"},
    {"Music/MP3",           "22"},
    {"Music/Lossless",      "23"},
    {"Music/Video",         "47"},
    {"Apps",                "Apps"},
    {"Apps/PC",             "18"},
    {"Apps/Mac",            "19"},
    {"Apps/Mobile",         "21"},
    {"Other",               "Other"},
    {"Books",               "36"},
    {"Comics",              "39"},
    {"Audiobooks",          "52"},
    {"XXX",                 "48"},
    {nullptr, nullptr}
};

static const CategoryOption* categoryOptionsForSite(const QString& siteKey)
{
    if (siteKey == "nyaa")         return NYAA_CATEGORIES;
    if (siteKey == "piratebay")    return PIRATEBAY_CATEGORIES;
    if (siteKey == "exttorrents")  return EXTTORRENTS_CATEGORIES;
    if (siteKey == "yts")          return YTS_CATEGORIES;
    if (siteKey == "eztv")         return EZTV_CATEGORIES;
    if (siteKey == "1337x")        return X1337X_CATEGORIES;
    if (siteKey == "torrentscsv")  return TORRENTSCSV_CATEGORIES;
    return nullptr;
}

// T8 — Reverse-lookup: given a sourceKey + raw category ID emitted by the
// indexer (e.g. "1_2" from nyaa or "200" from piratebay), return the
// human-friendly label from the source's CategoryOption array (e.g.
// "Anime - English-translated", "Video"). Returns empty QString if no
// match — caller falls back to the result's own category string or em-dash.
static QString categoryDisplayName(const QString& sourceKey, const QString& rawId)
{
    if (rawId.isEmpty()) return QString();
    const CategoryOption* opts = categoryOptionsForSite(sourceKey);
    if (!opts) return QString();
    for (int i = 0; opts[i].label != nullptr; ++i) {
        if (rawId == QLatin1String(opts[i].value))
            return QString::fromLatin1(opts[i].label);
    }
    return QString();
}

// T9-followup 2026-05-13 — Title-case the media-category routing key for UI
// display. Data layer stores "videos" / "books" / "audiobooks" / "comics"
// lowercase (used as key into TorrentClient::defaultPaths() lookup);
// the UI should always show Title Case per spec CR.8. Applied at the
// renderTorrentRow Category cell — covers flat torrents + bulk-group
// child rows alike. Bulk-group parent rows hardcode "Videos" directly
// (TankorentPage.cpp:2014 from T9). Returns em-dash for empty input.
static QString prettyCategoryName(const QString& raw)
{
    if (raw.isEmpty())                             return QStringLiteral("-");
    if (raw == QLatin1String("videos"))            return QStringLiteral("Videos");
    if (raw == QLatin1String("books"))             return QStringLiteral("Books");
    if (raw == QLatin1String("audiobooks"))        return QStringLiteral("Audiobooks");
    if (raw == QLatin1String("comics"))            return QStringLiteral("Comics");
    // Fallback for any other category string: capitalize first letter.
    QString out = raw;
    out[0] = out[0].toUpper();
    return out;
}

enum class RowKind {
    FlatTorrent,
    Group,
    GroupChild,
};

QString rowKindToString(RowKind kind)
{
    switch (kind) {
    case RowKind::FlatTorrent: return QStringLiteral("FlatTorrent");
    case RowKind::Group:       return QStringLiteral("Group");
    case RowKind::GroupChild:  return QStringLiteral("GroupChild");
    }
    return QStringLiteral("FlatTorrent");
}

RowKind rowKindFromString(const QString& kind)
{
    if (kind == QLatin1String("Group")) return RowKind::Group;
    if (kind == QLatin1String("GroupChild")) return RowKind::GroupChild;
    return RowKind::FlatTorrent;
}

QVariantMap transferRowMeta(RowKind kind,
                            const QString& infoHash = QString(),
                            const QString& groupId = QString(),
                            const QString& itemKey = QString())
{
    QVariantMap meta;
    meta.insert(QStringLiteral("kind"), rowKindToString(kind));
    if (!infoHash.isEmpty()) meta.insert(QStringLiteral("infoHash"), infoHash);
    if (!groupId.isEmpty()) meta.insert(QStringLiteral("groupId"), groupId);
    if (!itemKey.isEmpty()) meta.insert(QStringLiteral("itemKey"), itemKey);
    return meta;
}

QVariantMap transferRowMeta(const QTableWidget* table, int row)
{
    if (!table || row < 0) return {};
    auto* item = table->item(row, 0);
    return item ? item->data(Qt::UserRole).toMap() : QVariantMap();
}

RowKind transferRowKind(const QVariantMap& meta)
{
    return rowKindFromString(meta.value(QStringLiteral("kind")).toString());
}

QString transferRowSelectionKey(const QVariantMap& meta)
{
    switch (transferRowKind(meta)) {
    case RowKind::FlatTorrent:
        return QStringLiteral("torrent:%1").arg(meta.value(QStringLiteral("infoHash")).toString());
    case RowKind::Group:
        return QStringLiteral("group:%1").arg(meta.value(QStringLiteral("groupId")).toString());
    case RowKind::GroupChild:
        return QStringLiteral("child:%1:%2")
            .arg(meta.value(QStringLiteral("groupId")).toString(),
                 meta.value(QStringLiteral("itemKey")).toString());
    }
    return {};
}

bool isTerminalStreamBulkItemState(const QString& state)
{
    return state == QLatin1String("Published")
        || state == QLatin1String("MissingSource")
        || state == QLatin1String("MetadataFailed")
        || state == QLatin1String("PublishFailed")
        || state == QLatin1String("Failed")
        || state == QLatin1String("Completed")
        || state == QLatin1String("Cancelled")
        || state == QLatin1String("Orphaned");
}

bool isSkippedForWantedBytes(const QString& state)
{
    return state == QLatin1String("Cancelled")
        || state == QLatin1String("MissingSource")
        || state == QLatin1String("MetadataFailed")
        || state == QLatin1String("PublishFailed")
        || state == QLatin1String("Failed")
        || state == QLatin1String("Orphaned");
}

bool isFailedStreamBulkItemState(const QString& state)
{
    return state == QLatin1String("MissingSource")
        || state == QLatin1String("MetadataFailed")
        || state == QLatin1String("PublishFailed")
        || state == QLatin1String("Failed");
}

QString torrentStatusText(const TorrentInfo& t)
{
    if (t.stateString == QLatin1String("downloading")) return QStringLiteral("Downloading");
    if (t.stateString == QLatin1String("paused")) return QStringLiteral("Paused");
    if (t.stateString == QLatin1String("seeding")) return QStringLiteral("Seeding");
    if (t.stateString == QLatin1String("error"))
        return t.errorMessage.isEmpty()
            ? QStringLiteral("Error")
            : QStringLiteral("Error: %1").arg(t.errorMessage.left(40));
    if (t.stateString == QLatin1String("metadata")) return QStringLiteral("Resolving");
    if (t.stateString == QLatin1String("completed")) return QStringLiteral("Completed");
    if (t.stateString == QLatin1String("checking")) return QStringLiteral("Checking");
    return QStringLiteral("Stalled");
}

QString streamBulkItemStatusText(const QJsonObject& item, const TorrentInfo* active)
{
    const QString state = item.value(QStringLiteral("itemState")).toString();
    if (state == QLatin1String("Published")) return QStringLiteral("Published");
    if (state == QLatin1String("Publishing")) return QStringLiteral("Publishing");
    if (state == QLatin1String("MissingSource")) return QStringLiteral("Missing source");
    if (state == QLatin1String("MetadataFailed")) return QStringLiteral("Metadata failed");
    if (state == QLatin1String("PublishFailed")) return QStringLiteral("Publish failed");
    if (state == QLatin1String("Failed")) return QStringLiteral("Failed");
    if (state == QLatin1String("Completed")) return QStringLiteral("Completed");
    if (state == QLatin1String("Cancelled")) return QStringLiteral("Cancelled");
    if (state == QLatin1String("Orphaned")) return QStringLiteral("Orphaned");
    if (active) return torrentStatusText(*active);
    return state.isEmpty() ? QStringLiteral("Pending") : state;
}

int etaSecondsForTorrent(const TorrentInfo& t)
{
    if (t.dlSpeed > 0 && t.totalWanted > t.totalDone)
        return static_cast<int>((t.totalWanted - t.totalDone) / t.dlSpeed);
    return INT_MAX;
}

QString etaTextFromSeconds(int etaSecs)
{
    if (etaSecs == INT_MAX) return QStringLiteral("-");
    const int h = etaSecs / 3600;
    const int m = (etaSecs % 3600) / 60;
    return h > 0 ? QStringLiteral("%1h %2m").arg(h).arg(m)
                 : QStringLiteral("%1m %2s").arg(m).arg(etaSecs % 60);
}

QString destinationFolderForGroupItem(const QJsonObject& group, const QJsonObject& item)
{
    const QString destinationKey = item.value(QStringLiteral("destinationKey")).toString();
    const QString destinationRoot = group.value(QStringLiteral("destinationRoot")).toString();
    if (destinationKey.isEmpty())
        return destinationRoot;
    const QString path = QDir::isAbsolutePath(destinationKey)
        ? destinationKey
        : QDir(destinationRoot).filePath(destinationKey);
    return QFileInfo(path).absolutePath();
}

QString fallbackVideosRoot(TorrentClient* client)
{
    if (!client) return {};
    return client->defaultPaths().value(QStringLiteral("videos"));
}

// ══════════════════════════════════════════════════════════════════════════════
// T11 — TitleCellDelegate implementation
// ══════════════════════════════════════════════════════════════════════════════

TitleCellDelegate::TitleCellDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void TitleCellDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    // Let the base style paint background (selection / hover / alternating)
    // but suppress the default text — we'll draw three colored segments below.
    //
    // Calling QStyledItemDelegate::paint(painter, opt, index) here looks
    // right but is the classic re-init trap: the base method runs
    // initStyleOption(&opt, index) again internally, repopulating opt.text
    // from Qt::DisplayRole and undoing our clear(). The style then paints
    // the full DisplayRole title underneath our segmented draw, producing
    // a horizontally-offset double-text "ghosting" effect (visible on
    // every Title row in the screenshot 2026-05-13 ~6:00pm).
    // Fix: go straight to QStyle::drawControl(CE_ItemViewItem, &opt, ...)
    // so the style sees our cleared opt.text directly.
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    const QWidget* w = option.widget;
    QStyle* s = w ? w->style() : QApplication::style();
    s->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

    const QVariant data = index.data(Qt::UserRole + 1);
    if (!data.canConvert<TitleSegments>()) {
        // Fallback: paint default text if metadata missing.
        painter->save();
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(option.rect.adjusted(8, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }

    const TitleSegments seg = data.value<TitleSegments>();
    painter->save();

    const QFontMetrics fm(option.font);
    const QString sep = QStringLiteral("  ·  ");

    int x = option.rect.left() + 8;
    const int y = option.rect.center().y() + fm.ascent() / 2 - 1;
    const int rightLimit = option.rect.right() - 8;

    const QColor fgMain = option.palette.color(QPalette::Text);
    QColor fgDim = fgMain;
    fgDim.setAlpha(140);

    auto drawSeg = [&](const QString& text, const QColor& col) {
        if (text.isEmpty() || x >= rightLimit) return;
        painter->setPen(col);
        const int avail = rightLimit - x;
        const QString elided = fm.elidedText(text, Qt::ElideRight, avail);
        painter->drawText(x, y, elided);
        x += fm.horizontalAdvance(elided);
    };

    if (!seg.source.isEmpty()) {
        drawSeg(seg.source, fgDim);
        drawSeg(sep, fgDim);
    }
    drawSeg(seg.title, fgMain);
    if (!seg.quality.isEmpty()) {
        drawSeg(sep, fgDim);
        drawSeg(seg.quality, fgDim);
    }

    painter->restore();
}

QSize TitleCellDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    // Hardcoded row height per feedback_qt_sizehintforrow_unreliable_pre_show.md.
    // Table also calls setDefaultSectionSize(32); this is the per-row fallback.
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setHeight(32);
    return s;
}

// ══════════════════════════════════════════════════════════════════════════════
// Constructor
// ══════════════════════════════════════════════════════════════════════════════

TankorentPage::TankorentPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent)
    : QWidget(parent), m_bridge(bridge), m_client(client)
{
    qRegisterMetaType<TorrentResult>();
    qRegisterMetaType<QList<TorrentResult>>();

    m_nam = new QNetworkAccessManager(this);

    // Headless search service consumes the same QNAM the page already uses
    // for per-indexer networking. Wired to the 3-signal contract; the page
    // drops its own dispatch state in favor of the service.
    m_searchService = new TankorentSearchService(m_nam, this);
    connect(m_searchService, &TankorentSearchService::resultsReady,
            this, &TankorentPage::onServiceResultsReady);
    connect(m_searchService, &TankorentSearchService::indexerError,
            this, &TankorentPage::onServiceIndexerError);
    connect(m_searchService, &TankorentSearchService::searchFinished,
            this, &TankorentPage::onServiceSearchFinished);

    setObjectName(QStringLiteral("TankorentPage"));
    setWindowTitle(tr("Direct torrent search"));
    setAcceptDrops(true);
    buildUI();
    m_resultsTable->setItemDelegateForColumn(0, new TitleCellDelegate(this));
    populateSourceCombo();
    {
        const QStringList savedGroups = QSettings()
            .value(QStringLiteral("tankorent/expanded_stream_bulk_groups"))
            .toStringList();
        m_expandedGroupIds = QSet<QString>(savedGroups.cbegin(), savedGroups.cend());
    }

    // A5/C: restore results sort state from QSettings. Validate the column
    // against the post-T7 sortable set (0 Title, 1 Category, 2 Size,
    // 3 Seeders, 4 Leechers); fall back to default (Seeders desc, col 3) on
    // missing, out-of-range, or stale-from-pre-T7 values.
    {
        QSettings s;
        const int   savedCol   = s.value("tankorent/sortCol",   m_resultsSortCol).toInt();
        const int   savedOrder = s.value("tankorent/sortOrder", static_cast<int>(m_resultsSortOrder)).toInt();
        // T7 — Valid sortable cols after Files drop: 0 Title, 1 Category, 2 Size,
        // 3 Seeders, 4 Leechers. (5 Link non-sortable.) Migrate stale pre-T7
        // indices: pre-Files (3) -> Seeders (3); pre-Seeders (4) -> 3;
        // pre-Leechers (5) -> 4; pre-Link (6) -> default fallback.
        int migratedCol = savedCol;
        if (savedCol == 3) migratedCol = 3;
        else if (savedCol == 4) migratedCol = 3;
        else if (savedCol == 5) migratedCol = 4;
        else if (savedCol == 6) migratedCol = m_resultsSortCol;   // Link non-sortable
        const bool  validCol = (migratedCol == 0 || migratedCol == 1 || migratedCol == 2 ||
                                migratedCol == 3 || migratedCol == 4);
        if (validCol) m_resultsSortCol = migratedCol;
        m_resultsSortOrder = (savedOrder == Qt::AscendingOrder)
                                 ? Qt::AscendingOrder : Qt::DescendingOrder;
        if (m_resultsTable && m_resultsTable->horizontalHeader())
            m_resultsTable->horizontalHeader()->setSortIndicator(
                m_resultsSortCol, m_resultsSortOrder);
    }

    // C1: row-level Link buttons handle their own clicks via cellWidget;
    // double-click anywhere on the row still opens the AddTorrentDialog.
    connect(m_resultsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        onAddTorrentClicked(row);
    });

    // Transfers table context menu
    connect(m_transfersTable, &QTableWidget::customContextMenuRequested,
            this, &TankorentPage::showTransfersContextMenu);

    // Storage-move feedback toasts. Fire from libtorrent via TorrentClient's
    // alert handlers when a "Set Location..." invocation completes or fails.
    // Capture the torrent name lazily via listActive() lookup so we don't
    // hold a dangling reference if the row was removed mid-move.
    connect(m_client->engine(), &TorrentEngine::storageMoved, this,
        [this](const QString& infoHash, const QString& newPath) {
            QString name = infoHash.left(10);
            for (const auto& t : m_client->listActive()) {
                if (t.infoHash == infoHash) { name = t.name; break; }
            }
            Toast::show(this, tr("Moved \"%1\" to %2").arg(name, newPath));
        });
    connect(m_client->engine(), &TorrentEngine::storageMoveFailed, this,
        [this](const QString& infoHash, const QString& message) {
            QString name = infoHash.left(10);
            for (const auto& t : m_client->listActive()) {
                if (t.infoHash == infoHash) { name = t.name; break; }
            }
            Toast::show(this, tr("Move failed for \"%1\": %2").arg(name, message));
        });
    connect(m_client, &TorrentClient::groupPublishComplete, this,
        [this](const QString& groupId) {
            const QJsonObject group = m_client->streamBulkGroups()
                .value(groupId).toObject();
            const QString label = group.value(QStringLiteral("label")).toString(groupId);
            const int episodeCount = group.value(QStringLiteral("items")).toArray().size();
            refreshTransfers();
            Toast::show(this, tr("%1 download complete (%2 episodes)")
                              .arg(label).arg(episodeCount));
        });

    // Double-click or Info column click opens TorrentPropertiesWidget.
    auto openPropertiesFor = [this](int row) {
        const QVariantMap meta = transferRowMeta(m_transfersTable, row);
        if (transferRowKind(meta) != RowKind::FlatTorrent) return;
        const QString hash = meta.value(QStringLiteral("infoHash")).toString();
        if (hash.isEmpty()) return;
        auto* dlg = new TorrentPropertiesWidget(m_client, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->showTorrent(hash);
        dlg->show();
    };
    connect(m_transfersTable, &QTableWidget::cellDoubleClicked, this, [openPropertiesFor](int row, int) {
        openPropertiesFor(row);
    });
    connect(m_transfersTable, &QTableWidget::cellClicked, this, [this, openPropertiesFor](int row, int col) {
        const QVariantMap meta = transferRowMeta(m_transfersTable, row);
        if (transferRowKind(meta) == RowKind::Group) {
            const QString groupId = meta.value(QStringLiteral("groupId")).toString();
            if (!groupId.isEmpty()) {
                if (m_expandedGroupIds.contains(groupId))
                    m_expandedGroupIds.remove(groupId);
                else
                    m_expandedGroupIds.insert(groupId);
                saveExpandedStreamBulkGroups();
                refreshTransfers();
            }
            return;
        }
        if (col == 11) openPropertiesFor(row);  // Info column
    });

    // Auto-refresh transfers every 1 second
    m_transferTimer = new QTimer(this);
    connect(m_transferTimer, &QTimer::timeout, this, &TankorentPage::refreshTransfers);
    m_transferTimer->start(1000);
}

// v1.5 Phase D.3 (2026-05-19) — Tankorent-side dispatch layer.
//
// reply["type"] = "reply" + reply["seq"] are pre-set by MainWindow's
// handleDevCommand before forwarding; replyOk preserves those, replyErr
// overrides "type" to "error". See docs/superpowers/specs/
// 2026-05-19-bridge-v1.5-sources-commission.md for the catalog.
namespace {

inline bool tkrReplyOk(QJsonObject& reply, QJsonObject fields)
{
    for (auto it = fields.begin(); it != fields.end(); ++it)
        reply.insert(it.key(), it.value());
    return true;
}

inline bool tkrReplyErr(QJsonObject& reply, const char* code, const QString& msg)
{
    reply["type"]    = QStringLiteral("error");
    reply["code"]    = QString::fromLatin1(code);
    reply["message"] = msg;
    return true;
}

// Built-in indexer id list (mirrors TankorentSearchService's buildIndexersFor addIf calls).
// Used by sources-get-indexer-health to enumerate known sources without
// having to keep an active live indexer instance per id.
static const QStringList kKnownIndexerIds = {
    QStringLiteral("nyaa"),
    QStringLiteral("piratebay"),
    QStringLiteral("1337x"),
    QStringLiteral("yts"),
    QStringLiteral("eztv"),
    QStringLiteral("exttorrents"),
    QStringLiteral("torrentscsv"),
};

// Construct a sentinel indexer for the given id, used only for its
// persisted-health load. Returns nullptr for unknown ids. Caller deletes.
TorrentIndexer* makeSentinelIndexer(const QString& id, QNetworkAccessManager* nam,
                                    QObject* parent)
{
    if (id == QLatin1String("nyaa"))         return new NyaaIndexer(nam, parent);
    if (id == QLatin1String("piratebay"))    return new PirateBayIndexer(nam, parent);
    if (id == QLatin1String("1337x"))        return new X1337xIndexer(nam, parent);
    if (id == QLatin1String("yts"))          return new YtsIndexer(nam, parent);
    if (id == QLatin1String("eztv"))         return new EztvIndexer(nam, parent);
    if (id == QLatin1String("exttorrents"))  return new ExtTorrentsIndexer(nam, parent);
    if (id == QLatin1String("torrentscsv"))  return new TorrentsCsvIndexer(nam, parent);
    return nullptr;
}

}  // namespace

bool TankorentPage::dispatchDevCommand(const QString& cmd,
                                       const QJsonObject& payload,
                                       QJsonObject& reply)
{
    // ── State snapshot ─────────────────────────────────────────────────────
    if (cmd == QLatin1String("sources_get_tankorent_state"))
        return tkrReplyOk(reply, {{"snapshot", devSnapshot()}});

    // ── Search dispatch ────────────────────────────────────────────────────
    if (cmd == QLatin1String("sources_search_tankorent")) {
        const QString query = payload.value("query").toString();
        if (query.trimmed().isEmpty())
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.query required (non-empty)"));
        if (!m_queryEdit || !m_searchTypeCombo || !m_sourceCombo
            || !m_categoryCombo)
            return tkrReplyErr(reply, "INTERNAL",
                QStringLiteral("search controls not constructed"));
        m_queryEdit->setText(query);
        // Optional --type override (videos/books/audiobooks/comics).
        const QString typeOverride = payload.value("type").toString();
        if (!typeOverride.isEmpty()) {
            const int idx = m_searchTypeCombo->findData(typeOverride);
            if (idx >= 0) m_searchTypeCombo->setCurrentIndex(idx);
        }
        startSearch();
        // Post-extraction (HELP.md 2026-05-21): pendingSearches / dispatched
        // counts no longer live on the page. `searchInFlight` is the booled
        // equivalent for callers that just wanted "is a search active?".
        return tkrReplyOk(reply, {
            {"query",         query},
            {"searchHandle",  m_currentSearchHandle},
            {"searchInFlight", !m_currentSearchHandle.isEmpty()
                                && m_searchService
                                && m_searchService->isActive(m_currentSearchHandle)}
        });
    }

    if (cmd == QLatin1String("sources_cancel_search")) {
        const bool wasActive = !m_currentSearchHandle.isEmpty()
                                && m_searchService
                                && m_searchService->isActive(m_currentSearchHandle);
        cancelSearch();
        return tkrReplyOk(reply, {
            {"wasActive",     wasActive},
            {"searchInFlight", false}
        });
    }

    // ── Indexer health ─────────────────────────────────────────────────────
    if (cmd == QLatin1String("sources_get_indexer_health")) {
        QSettings settings;
        QJsonArray arr;
        for (const QString& id : kKnownIndexerIds) {
            TorrentIndexer* sentinel = makeSentinelIndexer(id, m_nam, nullptr);
            QJsonObject obj;
            obj["id"]           = id;
            obj["enabled"]      = settings.value(
                QStringLiteral("tankorent/indexers/%1/enabled").arg(id), true).toBool();
            if (sentinel) {
                obj["displayName"]      = sentinel->displayName();
                obj["health"]           = TorrentIndexer::healthToString(sentinel->health());
                obj["lastSuccess"]      = sentinel->lastSuccess().isValid()
                    ? sentinel->lastSuccess().toString(Qt::ISODate)
                    : QString();
                obj["lastError"]        = sentinel->lastError();
                obj["lastResponseMs"]   = static_cast<double>(sentinel->lastResponseMs());
                obj["requiresCredentials"] = sentinel->requiresCredentials();
                delete sentinel;
            } else {
                obj["displayName"]    = id;
                obj["health"]         = QStringLiteral("unknown");
                obj["lastSuccess"]    = QString();
                obj["lastError"]      = QStringLiteral("no sentinel constructor");
                obj["lastResponseMs"] = 0.0;
            }
            arr.append(obj);
        }
        return tkrReplyOk(reply, {{"indexers", arr}});
    }

    if (cmd == QLatin1String("sources_force_indexer_refresh")) {
        const QString id = payload.value("indexerId").toString();
        if (id.isEmpty())
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.indexerId required"));
        if (!kKnownIndexerIds.contains(id))
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("unknown indexerId '%1'").arg(id));
        TorrentIndexer* sentinel = makeSentinelIndexer(id, m_nam, nullptr);
        if (!sentinel)
            return tkrReplyErr(reply, "INTERNAL",
                QStringLiteral("could not construct sentinel for '%1'").arg(id));
        sentinel->clearPersistedHealth();
        delete sentinel;
        return tkrReplyOk(reply, {
            {"indexerId", id},
            {"refreshed", true},
            {"note",      QStringLiteral("persisted health cleared; live re-query "
                                          "happens on next search")}
        });
    }

    // ── Downloads queue + lifecycle ────────────────────────────────────────
    if (cmd == QLatin1String("sources_get_pending_downloads")) {
        if (!m_client)
            return tkrReplyErr(reply, "INTERNAL", "TorrentClient not wired");
        QJsonArray arr;
        for (const TorrentInfo& t : m_client->listActive()) {
            QJsonObject obj;
            obj["infoHash"]    = t.infoHash;
            obj["name"]        = t.name;
            obj["category"]    = t.category;
            obj["state"]       = t.stateString;
            obj["progress"]    = static_cast<double>(t.progress);
            obj["dlSpeed"]     = t.dlSpeed;
            obj["ulSpeed"]     = t.ulSpeed;
            obj["peers"]       = t.peers;
            obj["seeds"]       = t.seeds;
            obj["totalDone"]   = static_cast<double>(t.totalDone);
            obj["totalWanted"] = static_cast<double>(t.totalWanted);
            obj["savePath"]    = t.savePath;
            obj["dlLimit"]     = t.dlLimit;
            obj["ulLimit"]     = t.ulLimit;
            obj["queuePosition"] = t.queuePosition;
            obj["sequential"]    = t.sequential;
            obj["forceStarted"]  = t.forceStarted;
            obj["errorMessage"]  = t.errorMessage;
            arr.append(obj);
        }
        return tkrReplyOk(reply, {
            {"entries", arr},
            {"count",   arr.size()}
        });
    }

    if (cmd == QLatin1String("sources_cancel_download")
        || cmd == QLatin1String("sources_remove_torrent")) {
        if (!m_client)
            return tkrReplyErr(reply, "INTERNAL", "TorrentClient not wired");
        const QString infoHash = payload.value("infoHash").toString();
        if (infoHash.isEmpty())
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.infoHash required"));
        // deleteFiles defaults to false (leaves files on disk; sources-cancel-
        // download is a queue-abort, not a destructive purge). Callers wanting
        // disk eviction pass payload.deleteFiles=true explicitly.
        const bool deleteFiles = payload.value("deleteFiles").toBool(false);
        m_client->deleteTorrent(infoHash, deleteFiles);
        return tkrReplyOk(reply, {
            {"infoHash",    infoHash},
            {"deleteFiles", deleteFiles},
            {"cancelled",   true}
        });
    }

    if (cmd == QLatin1String("sources_pause_torrent")) {
        if (!m_client)
            return tkrReplyErr(reply, "INTERNAL", "TorrentClient not wired");
        const QString infoHash = payload.value("infoHash").toString();
        if (infoHash.isEmpty())
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.infoHash required"));
        m_client->pauseTorrent(infoHash);
        return tkrReplyOk(reply, {{"infoHash", infoHash}, {"paused", true}});
    }

    if (cmd == QLatin1String("sources_resume_torrent")) {
        if (!m_client)
            return tkrReplyErr(reply, "INTERNAL", "TorrentClient not wired");
        const QString infoHash = payload.value("infoHash").toString();
        if (infoHash.isEmpty())
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.infoHash required"));
        m_client->resumeTorrent(infoHash);
        return tkrReplyOk(reply, {{"infoHash", infoHash}, {"resumed", true}});
    }

    if (cmd == QLatin1String("sources_add_magnet")
        || cmd == QLatin1String("sources_add_url")) {
        if (!m_client)
            return tkrReplyErr(reply, "INTERNAL", "TorrentClient not wired");
        const QString key = (cmd == QLatin1String("sources_add_url"))
            ? QStringLiteral("url")
            : QStringLiteral("magnet");
        const QString uri = payload.value(key).toString();
        if (uri.isEmpty())
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.%1 required").arg(key));
        const QString category    = payload.value("category").toString();
        const QString destination = payload.value("destinationPath").toString();
        const QString hash = m_client->addMagnetHeadless(uri, category, destination);
        if (hash.isEmpty())
            return tkrReplyErr(reply, "ADD_FAILED",
                QStringLiteral("addMagnetHeadless returned empty (duplicate or "
                               "resolve-metadata failure)"));
        return tkrReplyOk(reply, {
            {"infoHash",    hash},
            {"category",    category},
            {"destination", destination}
        });
    }

    if (cmd == QLatin1String("sources_set_speed_limits")) {
        if (!m_client)
            return tkrReplyErr(reply, "INTERNAL", "TorrentClient not wired");
        if (!payload.contains(QStringLiteral("dlLimit"))
            || !payload.contains(QStringLiteral("ulLimit")))
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.dlLimit and payload.ulLimit required (bytes/sec)"));
        const int dl = payload.value("dlLimit").toInt();
        const int ul = payload.value("ulLimit").toInt();
        const QString scope = payload.value("scope").toString(
            QStringLiteral("global"));
        if (scope == QLatin1String("global")) {
            m_client->setGlobalSpeedLimits(dl, ul);
            return tkrReplyOk(reply, {
                {"scope", scope}, {"dlLimit", dl}, {"ulLimit", ul}});
        }
        // Per-torrent: scope == infoHash.
        const QString infoHash = scope;
        m_client->setSpeedLimits(infoHash, dl, ul);
        return tkrReplyOk(reply, {
            {"scope", QStringLiteral("infoHash")},
            {"infoHash", infoHash}, {"dlLimit", dl}, {"ulLimit", ul}});
    }

    if (cmd == QLatin1String("sources_set_queue_limits")) {
        if (!m_client)
            return tkrReplyErr(reply, "INTERNAL", "TorrentClient not wired");
        if (!payload.contains(QStringLiteral("maxDownloads"))
            || !payload.contains(QStringLiteral("maxUploads"))
            || !payload.contains(QStringLiteral("maxActive")))
            return tkrReplyErr(reply, "BAD_REQUEST",
                QStringLiteral("payload.maxDownloads, maxUploads, maxActive required"));
        const int dl = payload.value("maxDownloads").toInt();
        const int ul = payload.value("maxUploads").toInt();
        const int ac = payload.value("maxActive").toInt();
        m_client->setQueueLimits(dl, ul, ac);
        return tkrReplyOk(reply, {
            {"maxDownloads", dl}, {"maxUploads", ul}, {"maxActive", ac}});
    }

    return false;  // unknown sources_* command — caller falls through.
}

QJsonObject TankorentPage::devSnapshot() const
{
    QJsonObject snap;
    snap["activePageId"]   = QStringLiteral("tankorent");
    snap["query"]          = m_queryEdit ? m_queryEdit->text() : QString();
    snap["lastQuery"]      = m_lastQuery;
    snap["mediaType"]      = m_searchTypeCombo
        ? m_searchTypeCombo->currentData().toString() : QString();
    snap["sourceFilter"]   = m_sourceCombo
        ? m_sourceCombo->currentData().toString() : QString();
    snap["categoryFilter"] = m_categoryCombo
        ? m_categoryCombo->currentData().toString() : QString();
    snap["seederFilter"]   = m_filterCombo
        ? m_filterCombo->currentData().toString() : QString();
    snap["resultCount"]    = m_displayedResults.size();
    snap["totalResults"]   = m_allResults.size();
    snap["showAll"]        = m_showAll;
    // Post-extraction (HELP.md 2026-05-21): dispatch state now lives in
    // TankorentSearchService; the page only knows the handle + delegates
    // active-state queries.
    snap["searchHandle"]   = m_currentSearchHandle;
    snap["searchInFlight"] = !m_currentSearchHandle.isEmpty()
                              && m_searchService
                              && m_searchService->isActive(m_currentSearchHandle);
    snap["activeTab"]       = m_tabWidget ? m_tabWidget->currentIndex() : -1;
    snap["activeTransfers"] = m_cachedActive.size();
    return snap;
}

// ══════════════════════════════════════════════════════════════════════════════
// UI
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    buildSearchControls(root);
    buildStatusRow(root);
    buildMainTabs(root);
}

void TankorentPage::buildSearchControls(QVBoxLayout* parent)
{
    // Row 1 — query + its immediate actions. Query bar gets a sensible minimum
    // so it stays prominent at any window width; Search/Cancel sit right next
    // to it so "type query, click Search" is one hand-motion (per Hemanth
    // 2026-04-20 UX ask).
    auto *queryRow = new QHBoxLayout;
    queryRow->setContentsMargins(0, 0, 0, 0);
    queryRow->setSpacing(10);

    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText("Search torrents...");
    m_queryEdit->setFixedHeight(36);
    m_queryEdit->setMinimumWidth(320);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &TankorentPage::startSearch);
    queryRow->addWidget(m_queryEdit, 1);

    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setFixedHeight(36);
    m_searchBtn->setMinimumWidth(90);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &TankorentPage::startSearch);
    queryRow->addWidget(m_searchBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(36);
    m_cancelBtn->setMinimumWidth(90);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TankorentPage::cancelSearch);
    queryRow->addWidget(m_cancelBtn);

    parent->addLayout(queryRow);

    // Row 2 — filter combos + global actions.
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    m_searchTypeCombo = new QComboBox;
    m_searchTypeCombo->setFixedHeight(36);
    m_searchTypeCombo->setMinimumWidth(130);
    m_searchTypeCombo->addItem("Videos",     "videos");
    m_searchTypeCombo->addItem("Books",      "books");
    m_searchTypeCombo->addItem("Audiobooks", "audiobooks");
    m_searchTypeCombo->addItem("Comics",     "comics");
    row->addWidget(m_searchTypeCombo, 1);

    m_sourceCombo = new QComboBox;
    m_sourceCombo->setFixedHeight(36);
    m_sourceCombo->setMinimumWidth(140);
    connect(m_sourceCombo, &QComboBox::currentIndexChanged, this, [this]() {
        reloadCategoryOptions();
    });
    row->addWidget(m_sourceCombo, 1);

    m_categoryCombo = new QComboBox;
    m_categoryCombo->setFixedHeight(36);
    m_categoryCombo->setMinimumWidth(180);
    m_categoryCombo->addItem("All categories", "");
    m_categoryCombo->setEnabled(false);
    row->addWidget(m_categoryCombo, 1);

    // E1: client-side seeder filter. Applied in renderResults between dedup
    // and the soft cap. Persisted to QSettings tankorent/filter.
    m_filterCombo = new QComboBox;
    m_filterCombo->setFixedHeight(36);
    m_filterCombo->setMinimumWidth(130);
    m_filterCombo->setToolTip("Filter results by seeder count");
    m_filterCombo->addItem("All",            "all");
    m_filterCombo->addItem("Hide dead",      "active");   // seeders >= 1
    m_filterCombo->addItem("High seed only", "high");     // seeders >= 20
    {
        const QString saved = QSettings().value("tankorent/filter", "all").toString();
        const int idx = m_filterCombo->findData(saved);
        m_filterCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    connect(m_filterCombo, &QComboBox::currentIndexChanged, this, [this]() {
        QSettings().setValue("tankorent/filter", m_filterCombo->currentData().toString());
        if (!m_allResults.isEmpty()) renderResults();
    });
    row->addWidget(m_filterCombo, 1);

    // A4: Sort combo removed — sort is now driven by clicking column headers
    // (see onResultsHeaderClicked + compareResults). Default = Seeders desc (A3).

    row->addStretch(1);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedHeight(36);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &TankorentPage::refreshTransfers);
    row->addWidget(m_refreshBtn);

    m_sourcesBtn = new QPushButton("Sources");
    m_sourcesBtn->setFixedHeight(36);
    m_sourcesBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sourcesBtn, &QPushButton::clicked, this, &TankorentPage::onSourcesClicked);
    row->addWidget(m_sourcesBtn);

    m_addUrlBtn = new QPushButton("Add URL");
    m_addUrlBtn->setFixedHeight(36);
    m_addUrlBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addUrlBtn, &QPushButton::clicked, this, &TankorentPage::onAddFromUrlClicked);
    row->addWidget(m_addUrlBtn);

    m_moreBtn = new QPushButton("More");
    m_moreBtn->setFixedHeight(36);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    connect(m_moreBtn, &QPushButton::clicked, this, [this]() {
        QMenu* menu = ContextMenuHelper::createMenu(this);
        menu->addAction("Global Speed Limits...", this, [this]() {
            SpeedLimitDialog dlg("Speed Limits (Global)", 0, 0, this);
            if (dlg.exec() == QDialog::Accepted)
                m_client->setGlobalSpeedLimits(dlg.dlLimitBps(), dlg.ulLimitBps());
        });
        menu->addAction("Global Seeding Rules...", this, [this]() {
            SeedingRulesDialog dlg("Seeding Rules (Global)", 0.f, 0, this);
            if (dlg.exec() == QDialog::Accepted)
                m_client->setGlobalSeedingRules(dlg.ratioLimit(), dlg.seedTimeSecs());
        });
        menu->addAction("Queue Limits...", this, [this]() {
            QueueLimitsDialog dlg(this);
            if (dlg.exec() == QDialog::Accepted)
                m_client->setQueueLimits(dlg.maxDownloads(), dlg.maxUploads(), dlg.maxActive());
        });
        menu->addSeparator();
        menu->addAction("Pause All", this, [this]() {
            for (const auto& t : m_cachedActive) m_client->pauseTorrent(t.infoHash);
        });
        menu->addAction("Resume All", this, [this]() {
            for (const auto& t : m_cachedActive) m_client->resumeTorrent(t.infoHash);
        });
        menu->addSeparator();
        menu->addAction("View History...", this, [this]() {
            HistoryDialog dlg(m_client, this);
            dlg.exec();
        });
        menu->exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
        delete menu;
    });
    row->addWidget(m_moreBtn);

    parent->addLayout(row);
}

void TankorentPage::buildStatusRow(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_searchStatus = new QLabel("Ready");
    m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
    row->addWidget(m_searchStatus, 2);

    m_downloadStatus = new QLabel("Active: 0 | History: 0");
    m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
    row->addWidget(m_downloadStatus, 1);

    m_backendStatus = new QLabel;
    m_backendStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
    row->addWidget(m_backendStatus);

    parent->addLayout(row);
}

void TankorentPage::buildMainTabs(QVBoxLayout* parent)
{
    // D1/D2: result count line. Sits above the tab widget so it's visible from
    // both Search Results and Transfers (cleaner than embedding inside the tab
    // and then juggling visibility per tab). Hidden when no results.
    m_resultsCountLabel = new QLabel;
    m_resultsCountLabel->setStyleSheet("color: #a1a1aa; font-size: 13px; padding: 4px 0;");
    m_resultsCountLabel->setOpenExternalLinks(false);
    m_resultsCountLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_resultsCountLabel->hide();
    connect(m_resultsCountLabel, &QLabel::linkActivated, this, [this](const QString&) {
        m_showAll = true;
        renderResults();
    });
    parent->addWidget(m_resultsCountLabel);

    m_tabWidget = new QTabWidget;

    m_resultsTable = createResultsTable();

    // T15 — empty state page
    m_emptyPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_emptyPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);
        m_emptyLabel = new QLabel(
            tr("Direct torrent search - for content that isn't a Cinemeta show "
               "(sports, software, random downloads). To download shows by series "
               "+ season, use Theatre's show-view 'Download via Tankorent' button."),
            m_emptyPage);
        m_emptyLabel->setAlignment(Qt::AlignCenter);
        m_emptyLabel->setWordWrap(true);
        m_emptyLabel->setStyleSheet("color: #a1a1aa; font-size: 15px;");
        v->addWidget(m_emptyLabel);
    }

    // T15 — loading state page
    m_loadingPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_loadingPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);
        m_loadingLabel = new QLabel(QStringLiteral("Searching..."), m_loadingPage);
        m_loadingLabel->setAlignment(Qt::AlignCenter);
        m_loadingLabel->setStyleSheet("color: #cbd5e1; font-size: 15px;");
        v->addWidget(m_loadingLabel);
        auto* bar = new QProgressBar(m_loadingPage);
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        bar->setFixedWidth(220);
        bar->setFixedHeight(4);
        const QColor accent = QApplication::palette().color(QPalette::Highlight);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background: rgba(255,255,255,0.08); border: none; "
            "border-radius: 2px; }"
            "QProgressBar::chunk { background: %1; border-radius: 2px; }").arg(accent.name()));
        v->addWidget(bar, 0, Qt::AlignCenter);
    }

    // T15 — no-results state page
    m_noResultsPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_noResultsPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);
        m_noResultsLabel = new QLabel(m_noResultsPage);
        m_noResultsLabel->setAlignment(Qt::AlignCenter);
        m_noResultsLabel->setWordWrap(true);
        m_noResultsLabel->setStyleSheet("color: #a1a1aa; font-size: 15px;");
        v->addWidget(m_noResultsLabel);
        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(10);
        btnRow->setAlignment(Qt::AlignCenter);
        m_noResultsRetry = new QPushButton(QStringLiteral("Retry"), m_noResultsPage);
        m_noResultsRetry->setFixedHeight(32);
        m_noResultsRetry->setCursor(Qt::PointingHandCursor);
        connect(m_noResultsRetry, &QPushButton::clicked, this, [this]() {
            if (!m_lastQuery.isEmpty()) {
                m_queryEdit->setText(m_lastQuery);
                startSearch();
            }
        });
        btnRow->addWidget(m_noResultsRetry);
        m_noResultsClear = new QPushButton(QStringLiteral("Clear"), m_noResultsPage);
        m_noResultsClear->setFixedHeight(32);
        m_noResultsClear->setCursor(Qt::PointingHandCursor);
        connect(m_noResultsClear, &QPushButton::clicked, this, [this]() {
            m_queryEdit->clear();
            m_lastQuery.clear();
            m_queryEdit->setFocus();
            updateResultsView();
        });
        btnRow->addWidget(m_noResultsClear);
        v->addLayout(btnRow);
    }

    m_resultsStack = new QStackedWidget;
    m_resultsStack->addWidget(m_resultsTable);   // index 0: data view
    m_resultsStack->addWidget(m_emptyPage);      // index 1
    m_resultsStack->addWidget(m_loadingPage);    // index 2
    m_resultsStack->addWidget(m_noResultsPage);  // index 3
    m_resultsStack->setCurrentIndex(1);          // start on empty
    m_tabWidget->addTab(m_resultsStack, "Search Results");

    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");

    parent->addWidget(m_tabWidget, 1);
}

QTableWidget* TankorentPage::createResultsTable()
{
    auto *table = new QTableWidget(0, 6);
    table->setObjectName("SearchResultsTable");
    table->setMinimumHeight(280);

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, &TankorentPage::showResultsContextMenu);

    // C2: Source column is gone — the source is now a "[name]" badge prefixed
    // to the Title cell. C1: Action column ("+") replaced by a Link column
    // hosting download + magnet QToolButtons.
    QStringList headers = { "Title", "Category", "Size", "Seeders", "Leechers", "Link" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(60);

    // T13 — match header alignment to cell-data alignment per column.
    hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (int col : { 2, 3, 4, 5 }) {
        auto* hi = table->horizontalHeaderItem(col);
        if (!hi) {
            hi = new QTableWidgetItem(headers[col]);
            table->setHorizontalHeaderItem(col, hi);
        }
        hi->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    }

    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 6; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 140);   // Category
    hdr->resizeSection(2, 110);   // Size
    hdr->resizeSection(3, 90);    // Seeders
    hdr->resizeSection(4, 90);    // Leechers
    hdr->resizeSection(5, 80);    // Link (two icon buttons)

    // A1: click-to-sort. Manual — we sort m_displayedResults ourselves so the
    // model stays the source of truth. setSortingEnabled(true) would let Qt
    // sort by display strings, which mis-sorts sizes ("1.3 GiB" < "520 MiB").
    hdr->setSectionsClickable(true);
    hdr->setSortIndicatorShown(true);
    hdr->setSortIndicator(m_resultsSortCol, m_resultsSortOrder);
    connect(hdr, &QHeaderView::sectionClicked,
            this, &TankorentPage::onResultsHeaderClicked);

    // F3: right-click the header → menu of checkable column-visibility entries.
    // Always lists every column (incl. currently-hidden ones) so the user
    // can recover from accidentally hiding the column they were aiming at.
    // Persisted to QSettings tankorent/hiddenColumns as a CSV of indices.
    {
        const QStringList saved = QSettings()
            .value("tankorent/hiddenColumns").toString()
            .split(',', Qt::SkipEmptyParts);
        for (const QString& s : saved) {
            bool ok = false; const int c = s.toInt(&ok);
            if (ok && c >= 0 && c < table->columnCount())
                table->setColumnHidden(c, true);
        }
    }
    hdr->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(hdr, &QHeaderView::customContextMenuRequested,
            this, [this, table, headers](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction("Visible columns")->setEnabled(false);
        menu.addSeparator();
        for (int c = 0; c < headers.size(); ++c) {
            const QString label = headers[c].isEmpty()
                ? QStringLiteral("(column %1)").arg(c) : headers[c];
            QAction* act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(!table->isColumnHidden(c));
            connect(act, &QAction::toggled, this, [this, table, c](bool visible) {
                table->setColumnHidden(c, !visible);
                QStringList hiddenCsv;
                for (int k = 0; k < table->columnCount(); ++k)
                    if (table->isColumnHidden(k)) hiddenCsv << QString::number(k);
                QSettings().setValue("tankorent/hiddenColumns", hiddenCsv.join(','));
            });
        }
        menu.exec(table->horizontalHeader()->mapToGlobal(pos));
    });

    table->setStyle(QStyleFactory::create("Fusion"));
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::NoFocus);

    QPalette palR = table->palette();
    palR.setColor(QPalette::Base,            QColor(0x11, 0x11, 0x11));
    palR.setColor(QPalette::AlternateBase,   QColor(0x18, 0x18, 0x18));
    palR.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    palR.setColor(QPalette::Highlight,       QColor(192, 200, 212, 36));
    palR.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    table->setPalette(palR);

    table->setStyleSheet(QStringLiteral(
        "#SearchResultsTable { border: none; outline: none; font-size: 13px; }"
        "#SearchResultsTable::item { padding: 0 8px; }"
        "#SearchResultsTable::item:hover { background: rgba(255,255,255,0.04); }"
        "#SearchResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#SearchResultsTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 6px 8px; font-size: 11px; font-weight: 600; }"
    ));

    return table;
}

QTableWidget* TankorentPage::createTransfersTable()
{
    auto *table = new QTableWidget(0, 12);
    table->setObjectName("TransfersTable");

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->setSortingEnabled(true);

    QStringList headers = { "Name", "Size", "Progress", "Status", "Seeds", "Peers",
                            "Down Speed", "Up Speed", "ETA", "Category", "Queue", "Info" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(40);

    // T13 — match header alignment to cell-data alignment per column.
    hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (int col : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }) {
        auto* hi = table->horizontalHeaderItem(col);
        if (!hi) {
            hi = new QTableWidgetItem(headers[col]);
            table->setHorizontalHeaderItem(col, hi);
        }
        Qt::Alignment a;
        if (col == 1 || col == 6 || col == 7) {
            a = Qt::AlignRight | Qt::AlignVCenter;   // Size, DownSpeed, UpSpeed
        } else if (col == 9) {
            a = Qt::AlignLeft | Qt::AlignVCenter;    // Category (text)
        } else {
            a = Qt::AlignCenter | Qt::AlignVCenter;  // Progress, Status, Seeds, Peers, ETA, Queue, Info
        }
        hi->setTextAlignment(a);
    }

    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 12; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 100);   // Size
    hdr->resizeSection(2, 110);   // Progress
    hdr->resizeSection(3, 120);   // Status
    hdr->resizeSection(4, 70);    // Seeds
    hdr->resizeSection(5, 70);    // Peers
    hdr->resizeSection(6, 110);   // Down Speed
    hdr->resizeSection(7, 110);   // Up Speed
    hdr->resizeSection(8, 80);    // ETA
    hdr->resizeSection(9, 100);   // Category
    hdr->resizeSection(10, 60);   // Queue
    hdr->resizeSection(11, 40);   // Info

    table->setStyle(QStyleFactory::create("Fusion"));
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setFocusPolicy(Qt::NoFocus);

    QPalette palT = table->palette();
    palT.setColor(QPalette::Base,            QColor(0x11, 0x11, 0x11));
    palT.setColor(QPalette::AlternateBase,   QColor(0x18, 0x18, 0x18));
    palT.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    palT.setColor(QPalette::Highlight,       QColor(192, 200, 212, 36));
    palT.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    table->setPalette(palT);

    table->setStyleSheet(QStringLiteral(
        "#TransfersTable { border: none; outline: none; font-size: 13px; }"
        "#TransfersTable::item { padding: 0 8px; }"
        "#TransfersTable::item:hover { background: rgba(255,255,255,0.04); }"
        "#TransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#TransfersTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 6px 8px; font-size: 11px; font-weight: 600; }"
    ));

    return table;
}

// ══════════════════════════════════════════════════════════════════════════════
// Source combo + per-site category system
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::populateSourceCombo()
{
    m_sourceCombo->blockSignals(true);
    m_sourceCombo->clear();
    m_sourceCombo->addItem("All Sources",     "all");
    m_sourceCombo->addItem("Nyaa",            "nyaa");
    m_sourceCombo->addItem("PirateBay",       "piratebay");
    m_sourceCombo->addItem("1337x",           "1337x");
    m_sourceCombo->addItem("YTS",             "yts");
    m_sourceCombo->addItem("EZTV",            "eztv");
    m_sourceCombo->addItem("ExtraTorrents",   "exttorrents");
    m_sourceCombo->addItem("Torrents-CSV",    "torrentscsv");
    m_sourceCombo->blockSignals(false);
    reloadCategoryOptions();
}

void TankorentPage::reloadCategoryOptions()
{
    QString prevValue = m_categoryCombo->currentData().toString();
    QString siteKey = m_sourceCombo->currentData().toString();

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();

    if (siteKey == "all" || siteKey.isEmpty()) {
        m_categoryCombo->addItem("All categories", "");
        m_categoryCombo->setEnabled(false);
    } else {
        const CategoryOption* opts = categoryOptionsForSite(siteKey);
        if (opts) {
            for (int i = 0; opts[i].label != nullptr; ++i)
                m_categoryCombo->addItem(opts[i].label, opts[i].value);
            m_categoryCombo->setEnabled(true);
        } else {
            m_categoryCombo->addItem("All categories", "");
            m_categoryCombo->setEnabled(false);
        }

        // Try to restore previous selection
        int idx = m_categoryCombo->findData(prevValue);
        if (idx >= 0)
            m_categoryCombo->setCurrentIndex(idx);
    }
    m_categoryCombo->blockSignals(false);
}

// ══════════════════════════════════════════════════════════════════════════════
// Search logic
// ══════════════════════════════════════════════════════════════════════════════

// Dispatch + media-type allowlist + per-id QSettings enable read moved into
// TankorentSearchService 2026-05-21 (HELP.md handshake with Agent 2). The
// page is now a consumer of the 3-signal contract; single-flight UX is
// preserved by tracking m_currentSearchHandle and ignoring stale handles
// in the slot bodies.

void TankorentPage::startSearch()
{
    QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty())
        return;

    cancelSearch();

    m_allResults.clear();
    m_resultsTable->setRowCount(0);
    m_showAll = false;                          // D2: re-arm soft cap on each search
    if (m_resultsCountLabel) m_resultsCountLabel->hide();

    QString mediaType  = m_searchTypeCombo->currentData().toString();
    QString sourceId   = m_sourceCombo->currentData().toString();
    QString categoryId = m_categoryCombo->currentData().toString();

    // TANKORENT_QUALITY_AND_QUEUE P2 T2.2 (2026-05-27) — result limit raised
    // 80 -> 300 to restore source-site parity. The parity probe
    // (scripts/nyaa-parity-probe.sh) measured nyaa.si returning 300+ rows for
    // high-volume anime queries ("One Piece", "Naruto") while Tankorent
    // truncated at 80 — barely one page. 300 matches NyaaIndexer's natural
    // NYAA_MAX_PAGES(4) x NYAA_PAGE_SIZE(75) ceiling, so we surface the source's
    // full first-4-pages instead of one. Per-indexer upper bound — sparse
    // sources (YTS / EZTV) still stop early; only high-volume ones go deep.
    // Display soft-cap (kSoftCapRows=100, with "show all") protects the table
    // from rendering all 300 at once.
    static constexpr int kSearchResultLimit = 300;
    m_currentSearchHandle = m_searchService->startSearch(
        mediaType, sourceId, query, kSearchResultLimit, categoryId);

    if (m_currentSearchHandle.isEmpty()) {
        m_searchStatus->setText(
            QStringLiteral("No sources available for %1 search")
                .arg(mediaType.isEmpty() ? QStringLiteral("this") : mediaType));
        return;
    }

    m_searchBtn->setVisible(false);
    m_cancelBtn->setVisible(true);
    m_searchStatus->setText("Searching...");
    m_lastQuery = query;
    updateResultsView();
}

void TankorentPage::cancelSearch()
{
    if (!m_currentSearchHandle.isEmpty()) {
        m_searchService->cancelSearch(m_currentSearchHandle);
        m_currentSearchHandle.clear();
    }
    m_searchBtn->setVisible(true);
    m_cancelBtn->setVisible(false);
}

void TankorentPage::onServiceResultsReady(const QString& handle,
                                          const QList<TorrentResult>& results)
{
    if (handle != m_currentSearchHandle)
        return;  // stale; user fired a newer search

    m_allResults.append(results);
    renderResults();
    m_searchStatus->setText(
        m_searchService->isActive(handle)
            ? QStringLiteral("Searching... %1 Results").arg(m_allResults.size())
            : QStringLiteral("Done: %1 Results").arg(m_allResults.size()));
    updateResultsView();
}

void TankorentPage::onServiceIndexerError(const QString& handle,
                                          const QString& /*indexerId*/,
                                          const QString& error)
{
    if (handle != m_currentSearchHandle)
        return;

    if (m_searchService->isActive(handle))
        return;  // wait for other indexers to settle first

    // All indexers settled and this one was the error edge — if we have no
    // results at all, surface the error; otherwise note partial-success.
    if (m_allResults.isEmpty())
        m_searchStatus->setText("Search Failed: " + error);
    else {
        renderResults();
        m_searchStatus->setText(
            QStringLiteral("%1 Results (Some Sources Failed)").arg(m_allResults.size()));
    }
    updateResultsView();
}

void TankorentPage::onServiceSearchFinished(const QString& handle)
{
    if (handle != m_currentSearchHandle)
        return;

    m_currentSearchHandle.clear();
    m_searchBtn->setVisible(true);
    m_cancelBtn->setVisible(false);

    // Final status-text refresh in case the last edge was an error path
    // that didn't update the count line.
    if (!m_allResults.isEmpty())
        m_searchStatus->setText(QStringLiteral("Done: %1 Results").arg(m_allResults.size()));
    updateResultsView();
}

// ══════════════════════════════════════════════════════════════════════════════
// Result rendering with quality tags + health indicators
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::renderResults()
{
    // A1: apply sort via the column-driven comparator. Both initial render and
    // the click-to-sort handler funnel through here. m_resultsSortCol /
    // m_resultsSortOrder are mutated by onResultsHeaderClicked.
    auto sorted = m_allResults;
    const int   sortCol   = m_resultsSortCol;
    const auto  sortOrder = m_resultsSortOrder;
    std::stable_sort(sorted.begin(), sorted.end(),
        [sortCol, sortOrder](const TorrentResult& a, const TorrentResult& b) {
            return compareResults(sortCol, sortOrder, a, b);
        });

    // Three-tier dedup key: canonical infoHash (populated by the indexer at
    // parse time) → btih-regex on magnet (catches v2 64-hex + base32 magnets
    // that canonicalizeInfoHash intentionally rejects) → whole magnet string
    // as last resort so even unparseable magnets still collapse duplicates
    // against themselves.
    QSet<QString> seen;
    QList<TorrentResult> deduped;
    static const QRegularExpression btihRe(
        "btih:([a-fA-F0-9]{40}(?:[a-fA-F0-9]{24})?|[A-Z2-7]{32})",
        QRegularExpression::CaseInsensitiveOption);
    for (const auto& r : sorted) {
        QString key;
        if (!r.infoHash.isEmpty()) {
            key = r.infoHash;
        } else {
            auto m = btihRe.match(r.magnetUri);
            key = m.hasMatch() ? m.captured(1).toLower() : r.magnetUri.toLower();
        }
        if (seen.contains(key)) continue;
        seen.insert(key);
        deduped.append(r);
    }

    // E1: client-side seeder filter. Applied between dedup and the soft cap so
    // the count line and the cap both operate on the visible-relevant set —
    // a "Hide dead" filter doesn't cap to "100 of 1000" if only 30 survive.
    if (m_filterCombo) {
        const QString filterKey = m_filterCombo->currentData().toString();
        if (filterKey == QLatin1String("active")) {
            QList<TorrentResult> kept;
            kept.reserve(deduped.size());
            for (const auto& r : deduped)
                if (r.seeders >= 1) kept.append(r);
            deduped = std::move(kept);
        } else if (filterKey == QLatin1String("high")) {
            QList<TorrentResult> kept;
            kept.reserve(deduped.size());
            for (const auto& r : deduped)
                if (r.seeders >= 20) kept.append(r);
            deduped = std::move(kept);
        }
    }

    // D2: soft cap. Keep the full deduped count for the label; truncate the
    // visible subset to the first kSoftCapRows when the user hasn't asked to
    // see the rest. m_displayedResults must stay row-index-aligned with the
    // table — clip both at the same point.
    static constexpr int kSoftCapRows = 100;
    const int totalDeduped = deduped.size();
    if (!m_showAll && totalDeduped > kSoftCapRows)
        deduped = deduped.mid(0, kSoftCapRows);

    m_displayedResults = deduped;  // row N in table == m_displayedResults[N] — single source of truth

    // D1: result count line. Source count from m_allResults (pre-dedup) so we
    // attribute correctly even if dedup collapsed cross-source duplicates.
    if (m_resultsCountLabel) {
        if (m_allResults.isEmpty()) {
            m_resultsCountLabel->hide();
        } else {
            QSet<QString> sources;
            for (const auto& r : m_allResults)
                if (!r.sourceKey.isEmpty()) sources.insert(r.sourceKey);

            const int srcCount = sources.size();
            QString text;
            if (!m_showAll && totalDeduped > kSoftCapRows) {
                const auto linkColor = QApplication::palette().color(QPalette::Link).name();
                text = QStringLiteral(
                    "Showing %1 of %2 results from %3 source%4 \u2014 "
                    "<a href=\"show\" style=\"color:%5;text-decoration:none;\">Show all</a>")
                    .arg(kSoftCapRows).arg(totalDeduped).arg(srcCount).arg(linkColor)
                    .arg(srcCount == 1 ? "" : "s");
            } else {
                text = QStringLiteral("Showing %1 result%2 from %3 source%4")
                    .arg(totalDeduped).arg(totalDeduped == 1 ? "" : "s")
                    .arg(srcCount).arg(srcCount == 1 ? "" : "s");
            }
            m_resultsCountLabel->setText(text);
            m_resultsCountLabel->show();
        }
    }

    m_resultsTable->setRowCount(deduped.size());

    for (int i = 0; i < deduped.size(); ++i) {
        const auto& r = deduped[i];

        // C2: Title with [source] badge prefix + quality tags suffix.
        // C3: full title in the tooltip so elision can't hide information.
        // T11 — split into three segments for the custom delegate.
        const QString tags = qualityTagSuffix(r.title);
        TitleSegments seg;
        seg.source = r.sourceName;
        seg.title  = r.title;
        // qualityTagSuffix returns "[1080p]  [HEVC]  [BluRay]" with brackets — strip
        // for the delegate's middle-dot join.
        QString cleanQuality = tags;
        cleanQuality.remove(QLatin1Char('['));
        cleanQuality.remove(QLatin1Char(']'));
        cleanQuality = cleanQuality.simplified();
        seg.quality = cleanQuality;

        auto *titleItem = new QTableWidgetItem;
        // Fallback DisplayRole carries " · " concat for any non-delegate path.
        const QString fallbackDisplay = seg.source.isEmpty()
            ? (seg.quality.isEmpty() ? seg.title : seg.title + QStringLiteral(" · ") + seg.quality)
            : (seg.quality.isEmpty()
                ? seg.source + QStringLiteral(" · ") + seg.title
                : seg.source + QStringLiteral(" · ") + seg.title + QStringLiteral(" · ") + seg.quality);
        titleItem->setData(Qt::DisplayRole, fallbackDisplay);
        titleItem->setData(Qt::UserRole + 1, QVariant::fromValue(seg));
        titleItem->setToolTip(r.title);
        m_resultsTable->setItem(i, 0, titleItem);

        // Category — T8: prefer human-friendly name resolved from per-source map.
        // Falls back to r.category if scraper already gave us a friendly string,
        // else em-dash for "unknown".
        QString categoryText = categoryDisplayName(r.sourceKey, r.categoryId);
        if (categoryText.isEmpty()) categoryText = r.category;
        if (categoryText.isEmpty()) categoryText = QStringLiteral("—");  // em-dash U+2014
        m_resultsTable->setItem(i, 1, new QTableWidgetItem(categoryText));

        // Size — T13 alignment match.
        auto* sizeItem = new QTableWidgetItem(humanSize(r.sizeBytes));
        sizeItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        m_resultsTable->setItem(i, 2, sizeItem);

        // Seeders — T10 trust signal via weight + foreground (no row tint, no color).
        //   Healthy (>=50 seeders): bold count.
        //   Dead    (<5 seeders):   dim gray foreground (fgMutedColor helper from T3).
        //   Mid-range (5-49):       normal weight, normal foreground.
        auto *seedItem = new QTableWidgetItem(QString::number(r.seeders));
        seedItem->setTextAlignment(Qt::AlignCenter);
        if (r.seeders >= 50) {
            QFont f = seedItem->font();
            f.setBold(true);
            seedItem->setFont(f);
        } else if (r.seeders < 5) {
            seedItem->setForeground(QBrush(fgMutedColor()));
        }
        m_resultsTable->setItem(i, 3, seedItem);

        // Leechers
        auto *leechItem = new QTableWidgetItem(QString::number(r.leechers));
        leechItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 4, leechItem);

        // C1: Link column with download + magnet QToolButtons.
        // The download button funnels through onAddTorrentClicked (same path
        // the old "+" Action column used). Magnet copies the URI to clipboard.
        // Backing item is empty so row-tint stamping below still applies.
        m_resultsTable->setItem(i, 5, new QTableWidgetItem(QString()));
        auto *linkCell = new QWidget;
        auto *linkLay  = new QHBoxLayout(linkCell);
        linkLay->setContentsMargins(2, 0, 2, 0);
        linkLay->setSpacing(4);
        linkLay->setAlignment(Qt::AlignCenter);

        auto *dlBtn = new QToolButton(linkCell);
        dlBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
        dlBtn->setIconSize(QSize(16, 16));
        dlBtn->setToolTip("Add torrent");
        dlBtn->setCursor(Qt::PointingHandCursor);
        dlBtn->setAutoRaise(true);
        connect(dlBtn, &QToolButton::clicked, this, [this, i]() {
            onAddTorrentClicked(i);
        });
        linkLay->addWidget(dlBtn);

        auto *magBtn = new QToolButton(linkCell);
        magBtn->setIcon(QIcon(QStringLiteral(":/icons/magnet.svg")));
        magBtn->setIconSize(QSize(16, 16));
        magBtn->setToolTip("Copy magnet link");
        magBtn->setCursor(Qt::PointingHandCursor);
        magBtn->setAutoRaise(true);
        const QString magnet = r.magnetUri;
        connect(magBtn, &QToolButton::clicked, this, [magnet]() {
            QGuiApplication::clipboard()->setText(magnet);
        });
        linkLay->addWidget(magBtn);

        m_resultsTable->setCellWidget(i, 5, linkCell);

    }

    m_tabWidget->setCurrentIndex(0);
}

// ══════════════════════════════════════════════════════════════════════════════
// A1: click-to-sort comparator + header handler
// ══════════════════════════════════════════════════════════════════════════════

bool TankorentPage::compareResults(int col, Qt::SortOrder order,
                                    const TorrentResult& a, const TorrentResult& b)
{
    auto cmpThen = [order](bool less) {
        return order == Qt::AscendingOrder ? less : !less;
    };
    // Post-T7 layout: 0 Title, 1 Category, 2 Size, 3 Seeders, 4 Leechers,
    // 5 Link. Files col removed.
    switch (col) {
    case 0: // Title
        return cmpThen(a.title.compare(b.title, Qt::CaseInsensitive) < 0);
    case 1: // Category
        return cmpThen(a.category.compare(b.category, Qt::CaseInsensitive) < 0);
    case 2: // Size
        return cmpThen(a.sizeBytes < b.sizeBytes);
    case 3: // Seeders
        return cmpThen(a.seeders < b.seeders);
    case 4: // Leechers
        return cmpThen(a.leechers < b.leechers);
    default:
        // Col 5 (Link) has no sortable backing field.
        return false;   // stable_sort keeps original order
    }
}

// T15 — flip the inner Search Results stack between data view / empty / loading / no-results.
void TankorentPage::updateResultsView()
{
    if (!m_resultsStack) return;
    const bool searchInFlight = !m_currentSearchHandle.isEmpty()
                                 && m_searchService
                                 && m_searchService->isActive(m_currentSearchHandle);
    if (searchInFlight) {
        m_resultsStack->setCurrentIndex(2);   // loading
        if (m_loadingLabel)
            m_loadingLabel->setText(QStringLiteral("Searching sources..."));
        return;
    }
    if (m_allResults.isEmpty()) {
        if (m_lastQuery.isEmpty()) {
            m_resultsStack->setCurrentIndex(1);   // pre-search empty
        } else {
            m_resultsStack->setCurrentIndex(3);   // no-results
            if (m_noResultsLabel)
                m_noResultsLabel->setText(QStringLiteral("No results for \"%1\". Try a different query or open Sources to enable more.")
                                          .arg(m_lastQuery));
        }
        return;
    }
    m_resultsStack->setCurrentIndex(0);   // data view (table)
}

void TankorentPage::onResultsHeaderClicked(int col)
{
    // Non-sortable columns: ignore. Header indicator stays where it was.
    // Post-T7: only 5 Link is non-sortable.
    if (col == 5) return;

    if (col == m_resultsSortCol) {
        // Same column: flip direction.
        m_resultsSortOrder = (m_resultsSortOrder == Qt::AscendingOrder)
                                 ? Qt::DescendingOrder : Qt::AscendingOrder;
    } else {
        // New column: pick the column-default direction. Numeric cols default
        // descending (high seeders / large sizes first); strings ascending.
        m_resultsSortCol = col;
        const bool numeric = (col == 2 || col == 3 || col == 4);
        m_resultsSortOrder = numeric ? Qt::DescendingOrder : Qt::AscendingOrder;
    }

    if (m_resultsTable && m_resultsTable->horizontalHeader())
        m_resultsTable->horizontalHeader()->setSortIndicator(
            m_resultsSortCol, m_resultsSortOrder);

    // A5: persist for the next session.
    {
        QSettings s;
        s.setValue("tankorent/sortCol",   m_resultsSortCol);
        s.setValue("tankorent/sortOrder", static_cast<int>(m_resultsSortOrder));
    }

    renderResults();
}

// ══════════════════════════════════════════════════════════════════════════════
// Speed formatting — 2 decimal places, "X.XX MB/s"
// ══════════════════════════════════════════════════════════════════════════════

QString TankorentPage::humanSpeed(int bytesPerSec)
{
    if (bytesPerSec <= 0) return QString();
    const double kb = 1024.0, mb = kb * 1024, gb = mb * 1024;
    double v = bytesPerSec;
    if (v >= gb) return QString::number(v / gb, 'f', 2) + " GB/s";
    if (v >= mb) return QString::number(v / mb, 'f', 2) + " MB/s";
    if (v >= kb) return QString::number(v / kb, 'f', 1) + " KB/s";
    return QString::number(bytesPerSec) + " B/s";
}

// ══════════════════════════════════════════════════════════════════════════════
// Quality tag parsing (ported from sources_ui_helpers.py)
// ══════════════════════════════════════════════════════════════════════════════

QString TankorentPage::qualityTagSuffix(const QString& title)
{
    QStringList tags;

    // Resolution
    static const QRegularExpression resRe("\\b(2160p|4K|UHD|1080p|720p|480p|576p|360p)\\b",
        QRegularExpression::CaseInsensitiveOption);
    auto rm = resRe.match(title);
    if (rm.hasMatch()) {
        QString r = rm.captured(1).toLower();
        if (r == "4k" || r == "uhd" || r == "2160p") tags << "[4K]";
        else tags << "[" + rm.captured(1) + "]";
    }

    // Codec
    static const QRegularExpression codecRe("\\b(x265|HEVC|H\\.?264|x264|AV1|VP9)\\b",
        QRegularExpression::CaseInsensitiveOption);
    auto cm = codecRe.match(title);
    if (cm.hasMatch()) {
        QString c = cm.captured(1).toLower();
        if (c == "x265" || c == "hevc") tags << "[HEVC]";
        else if (c.startsWith("h") || c == "x264") tags << "[H.264]";
        else tags << "[" + cm.captured(1) + "]";
    }

    // Source
    static const QRegularExpression srcRe("\\b(Blu-?Ray|BDRip|BDMV|WEB-DL|WEBRip|HDTV|DVDRip|REMUX|CAMRip)\\b",
        QRegularExpression::CaseInsensitiveOption);
    auto sm = srcRe.match(title);
    if (sm.hasMatch()) {
        QString s = sm.captured(1).toLower();
        if (s.startsWith("blu")) tags << "[BluRay]";
        else tags << "[" + sm.captured(1) + "]";
    }

    return tags.join("  ");
}

// ══════════════════════════════════════════════════════════════════════════════
// B1: trust class for Nyaa-style row tint (replaces the per-cell health dot)
// ══════════════════════════════════════════════════════════════════════════════

QString TankorentPage::trustClass(const TorrentResult& r)
{
    if (r.seeders >= 50) return QStringLiteral("healthy");
    if (r.seeders >= 5)  return QStringLiteral("normal");
    return QStringLiteral("poor");
}

// ══════════════════════════════════════════════════════════════════════════════
// Context menu
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::showResultsContextMenu(const QPoint& pos)
{
    int row = m_resultsTable->rowAt(pos.y());
    if (row < 0 || row >= m_displayedResults.size()) return;

    const auto& result = m_displayedResults[row];

    QMenu* menu = ContextMenuHelper::createMenu(this);

    menu->addAction("Download...", this, [this, row]() {
        onAddTorrentClicked(row);
    });

    menu->addSeparator();

    auto *copyMagnet = menu->addAction("Copy Magnet URI", this, [&result]() {
        ContextMenuHelper::copyToClipboard(result.magnetUri);
    });
    copyMagnet->setEnabled(!result.magnetUri.isEmpty());

    auto *copyTitle = menu->addAction("Copy Title", this, [&result]() {
        ContextMenuHelper::copyToClipboard(result.title);
    });
    copyTitle->setEnabled(!result.title.isEmpty());

    menu->exec(m_resultsTable->viewport()->mapToGlobal(pos));
    delete menu;
}

// ══════════════════════════════════════════════════════════════════════════════
// Add Torrent flow — "+" button or double-click
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::onAddTorrentClicked(int row)
{
    // Row-bounds + empty-magnet validation lives here (caller-context);
    // post-validation, the resolveMetadata + AddTorrentDialog.exec() +
    // startDownload sequence is shared with addMagnetFromExternal via
    // startSingleAddFlow (STREAM_ADD_TO_TANKORENT_DIALOG_FIX 2026-05-06).
    // Diagnostic breadcrumbs route through DebugLogBuffer so
    // `tankoctl logs` surfaces every branch — qDebug alone goes to stderr
    // and never reaches the dev-bridge ring buffer. (Hemanth-reported
    // 2026-04-30: multiple silent-failure rounds before instrumentation
    // could prove the path.)
    auto& dlog = DebugLogBuffer::instance();
    dlog.info("tankorent", QStringLiteral("onAddTorrentClicked entered row=%1").arg(row));

    if (!m_client) {
        dlog.warning("tankorent", "onAddTorrentClicked: m_client is null");
        if (m_searchStatus) m_searchStatus->setText("Torrent client not initialised");
        return;
    }
    if (row < 0 || row >= m_displayedResults.size()) {
        dlog.warning("tankorent",
            QStringLiteral("onAddTorrentClicked: row %1 out of range (size %2)")
                .arg(row).arg(m_displayedResults.size()));
        m_searchStatus->setText(QStringLiteral("Invalid result row (%1)").arg(row));
        return;
    }

    const auto& result = m_displayedResults[row];
    if (result.magnetUri.isEmpty()) {
        dlog.warning("tankorent",
            QStringLiteral("onAddTorrentClicked: row %1 has empty magnetUri (title=%2)")
                .arg(row).arg(result.title));
        m_searchStatus->setText("Result has no magnet link — try refreshing the search");
        return;
    }

    startSingleAddFlow(result.magnetUri, result.title);
}

// STREAM_ADD_TO_TANKORENT_DIALOG_FIX 2026-05-06 — shared single-add body.
// Mirrors the in-Tankorent click flow exactly (resolve metadata, populate
// dialog as engine emits metadataReady, exec the dialog modally, start
// download with the user-picked AddTorrentConfig on Accepted, drop the
// draft on Cancel). Callers handle their own context-specific validation
// (row-bounds for in-Tankorent search clicks; empty-magnet check for
// cross-page hand-off) before delegating here.
void TankorentPage::startSingleAddFlow(const QString& magnetUri,
                                       const QString& title)
{
    auto& dlog = DebugLogBuffer::instance();

    if (!m_client) {
        dlog.warning("tankorent", "startSingleAddFlow: m_client is null");
        if (m_searchStatus) m_searchStatus->setText("Torrent client not initialised");
        return;
    }
    if (magnetUri.isEmpty()) {
        dlog.warning("tankorent",
            QStringLiteral("startSingleAddFlow: empty magnetUri (title=%1)").arg(title));
        if (m_searchStatus) m_searchStatus->setText("Empty magnet — nothing to add");
        return;
    }

    // Dedup check
    if (m_client->isDuplicate(magnetUri)) {
        dlog.info("tankorent",
            QStringLiteral("startSingleAddFlow: duplicate (already in m_records) title=%1")
                .arg(title));
        if (m_searchStatus) m_searchStatus->setText("Torrent Already Added");
        return;
    }

    dlog.info("tankorent",
        QStringLiteral("startSingleAddFlow: resolving metadata title=%1 magnet=%2")
            .arg(title).arg(magnetUri.left(80)));

    // Open the Add Torrent dialog
    auto defaultPaths = m_client->defaultPaths();
    AddTorrentDialog dlg(title, QString(), defaultPaths, this);

    // Start metadata resolution
    const QString hash = m_client->resolveMetadata(magnetUri);
    if (hash.isEmpty()) {
        dlog.warning("tankorent",
            QStringLiteral("startSingleAddFlow: resolveMetadata returned empty hash for %1")
                .arg(magnetUri.left(80)));
        if (m_searchStatus)
            m_searchStatus->setText("Failed to Add Magnet (engine rejected the URI — duplicate draft or invalid)");
        return;
    }

    dlog.info("tankorent",
        QStringLiteral("startSingleAddFlow: hash=%1 — about to dlg.exec()")
            .arg(hash.left(40)));

    // Connect engine's metadataReady to populate the dialog
    auto conn = connect(m_client->engine(), &TorrentEngine::metadataReady,
        &dlg, [&dlg, hash](const QString& h, const QString& name, qint64 size, const QJsonArray& files) {
            if (h == hash)
                dlg.populateFiles(name, size, files);
        });

    // Timeout after 30 seconds
    QTimer::singleShot(30000, &dlg, [&dlg, hash]() {
        if (!dlg.isVisible()) return;
        dlg.showMetadataError("Metadata resolution timed out — no peers found");
    });

    const int execResult = dlg.exec();
    dlog.info("tankorent",
        QStringLiteral("startSingleAddFlow: dlg.exec() returned %1 (Accepted=%2 Rejected=%3)")
            .arg(execResult).arg(QDialog::Accepted).arg(QDialog::Rejected));

    if (execResult == QDialog::Accepted) {
        auto config = dlg.config();
        // STREAM_BULK_DOWNLOAD Phase 1: normal single-add dispatch stays
        // ungrouped. Later bulk dispatch fills streamGroupId before calling
        // TorrentClient::startDownload().
        config.streamGroupId = QString();
        m_client->startDownload(hash, config);
        if (m_tabWidget) m_tabWidget->setCurrentIndex(1); // Switch to Transfers tab
        if (m_searchStatus) m_searchStatus->setText("Download Started");
    } else {
        // User cancelled — clean up the draft torrent
        m_client->deleteTorrent(hash, false);
    }

    disconnect(conn);
}

// ══════════════════════════════════════════════════════════════════════════════
// Transfers tab — auto-refresh
// ══════════════════════════════════════════════════════════════════════════════

void TankorentPage::saveExpandedStreamBulkGroups() const
{
    QStringList groupIds = QStringList(m_expandedGroupIds.cbegin(), m_expandedGroupIds.cend());
    groupIds.sort();
    QSettings().setValue(QStringLiteral("tankorent/expanded_stream_bulk_groups"), groupIds);
}

void TankorentPage::refreshTransfers()
{
    if (!m_client) return;

    m_cachedActive = m_client->listActive();
    const auto& active = m_cachedActive;

    QSet<QString> selectedKeys;
    const auto selectedRows = m_transfersTable->selectionModel()
        ? m_transfersTable->selectionModel()->selectedRows()
        : QModelIndexList();
    for (const QModelIndex& index : selectedRows)
        selectedKeys.insert(transferRowSelectionKey(transferRowMeta(m_transfersTable, index.row())));

    if (m_sortCol < 0 && m_transfersTable->horizontalHeader()->sortIndicatorSection() >= 0) {
        m_sortCol = m_transfersTable->horizontalHeader()->sortIndicatorSection();
        m_sortOrder = m_transfersTable->horizontalHeader()->sortIndicatorOrder();
    }

    QHash<QString, TorrentInfo> activeByHash;
    QHash<QString, QList<TorrentInfo>> activeByGroup;
    QList<QString> groupOrder;
    QList<TorrentInfo> flatRows;
    QSet<QString> knownActiveHashes;
    for (const TorrentInfo& t : active) {
        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — stream-originated transfers are
        // managed by the Stream-mode UI (StreamDetailView inline + Stream
        // Downloads page). Skip rendering entirely here; the underlying
        // libtorrent records and stream_bulk_groups.json state stay intact.
        // Tankorent now hosts ONLY non-stream torrents (manual magnet adds,
        // manual torrent search). Spec §7.6.
        if (!t.streamGroupId.isEmpty())
            continue;

        // THEATRE_DOWNLOAD_SIMPLIFY P2.T5 (2026-05-29) — show-bound downloads
        // (Theatre/stream: non-empty imdbId) are managed by their mode's UI and
        // must not appear in the standalone Tankorent page. Manual torrent adds
        // have an empty imdbId and still render here.
        if (!t.imdbId.isEmpty())
            continue;

        const QString hashKey = t.infoHash.toLower();
        if (!hashKey.isEmpty()) {
            activeByHash.insert(hashKey, t);
            knownActiveHashes.insert(hashKey);
        }
        if (t.streamGroupId.isEmpty()) {
            flatRows.append(t);
        } else {
            if (!activeByGroup.contains(t.streamGroupId))
                groupOrder.append(t.streamGroupId);
            activeByGroup[t.streamGroupId].append(t);
        }
    }
    const QJsonObject groups = m_client->streamBulkGroups();
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        // Skip stream-group rows (they render in Stream Downloads page now).
        if (it.key().startsWith(QStringLiteral("stream:")))
            continue;
        if (groupOrder.contains(it.key()))
            continue;
        const QJsonArray items = it.value().toObject().value(QStringLiteral("items")).toArray();
        bool shouldShowInactiveGroup = false;
        for (const auto& value : items) {
            const QString state = value.toObject().value(QStringLiteral("itemState")).toString();
            // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-10 — Cancelled added.
            // Without it, all-Cancelled groups (user removed underlying
            // torrents from Tankorent → onTorrentRemoved marks items
            // Cancelled with lastError "Torrent removed from Tankorent")
            // had no UI surface — group hidden, no right-click menu,
            // no way to clear the JSON record. Hemanth flagged 2026-05-10
            // ("why can't I remove a batch?"). Now an all-Cancelled group
            // renders as an inactive row so the user can right-click →
            // "Cancel group..." which the cancelStreamBulkGroup hotfix
            // (TorrentClient.cpp) now removes from the store.
            if (state == QLatin1String("Orphaned") ||
                state == QLatin1String("Cancelled") ||
                isFailedStreamBulkItemState(state)) {
                shouldShowInactiveGroup = true;
                break;
            }
        }
        if (shouldShowInactiveGroup)
            groupOrder.append(it.key());
    }
    for (auto it = m_zeroPeerSeedSinceByHash.begin(); it != m_zeroPeerSeedSinceByHash.end(); ) {
        if (!knownActiveHashes.contains(it.key()))
            it = m_zeroPeerSeedSinceByHash.erase(it);
        else
            ++it;
    }

    m_transfersTable->setSortingEnabled(false);
    m_transfersTable->clearContents();
    m_transfersTable->setRowCount(0);

    int totalDl = 0, totalUl = 0;
    int activeCount = 0, seedingCount = 0;
    for (const TorrentInfo& t : active) {
        // Exclude stream-grouped torrents from aggregate counters; they are
        // accounted for in the Stream Downloads page, not Tankorent. Spec §7.6.
        if (!t.streamGroupId.isEmpty())
            continue;
        totalDl += t.dlSpeed;
        totalUl += t.ulSpeed;
        if (t.stateString == QLatin1String("downloading")) ++activeCount;
        else if (t.stateString == QLatin1String("seeding")) ++seedingCount;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kGroupStallThresholdMs = 5LL * 60LL * 1000LL;
    const bool hasGroupedRows = !groupOrder.isEmpty();

    auto appendRow = [this]() {
        const int row = m_transfersTable->rowCount();
        m_transfersTable->insertRow(row);
        return row;
    };
    auto ensureItem = [this](int row, int col) -> QTableWidgetItem* {
        auto* item = m_transfersTable->item(row, col);
        if (!item) {
            item = new QTableWidgetItem;
            m_transfersTable->setItem(row, col, item);
        }
        return item;
    };
    auto setProgressWidget = [this, &ensureItem](int row, const QString& text, double progress) {
        auto* item = ensureItem(row, 2);
        item->setData(Qt::UserRole, progress);
        // STREAM_BULK_DOWNLOAD UI fix 2026-05-10 — leave the item's display
        // text empty when also setting a cell widget. Qt paints the cell
        // widget OVER the item, but the QWidget host + QLabel have
        // transparent areas; setting setText here makes the underlying item
        // text leak through, which produced the "0/10 0/10" duplication
        // Hemanth reported on 2026-05-10. UserRole still carries the
        // progress value for sort/selection.
        item->setText(QString());
        item->setTextAlignment(Qt::AlignCenter);

        auto* host = new QWidget(m_transfersTable);
        auto* layout = new QHBoxLayout(host);
        layout->setContentsMargins(4, 0, 4, 0);
        layout->setSpacing(6);
        auto* label = new QLabel(text, host);
        label->setMinimumWidth(34);
        label->setAlignment(Qt::AlignCenter);
        auto* bar = new QProgressBar(host);
        bar->setRange(0, 1000);
        bar->setValue(qBound(0, static_cast<int>(progress * 1000.0), 1000));
        bar->setTextVisible(false);
        bar->setFixedHeight(6);
        layout->addWidget(label);
        layout->addWidget(bar, 1);
        m_transfersTable->setCellWidget(row, 2, host);
    };
    auto renderTorrentRow = [&](int row, const TorrentInfo& t, bool childRow,
                                const QJsonObject& childItem = QJsonObject(),
                                const QString& groupId = QString()) {
        const QVariantMap meta = childRow
            ? transferRowMeta(RowKind::GroupChild, t.infoHash, groupId,
                              childItem.value(QStringLiteral("itemKey")).toString())
            : transferRowMeta(RowKind::FlatTorrent, t.infoHash);

        auto* nameItem = ensureItem(row, 0);
        QString displayName;
        if (childRow) {
            displayName = childItem.value(QStringLiteral("canonicalFilename")).toString();
            if (displayName.isEmpty())
                displayName = QFileInfo(childItem.value(QStringLiteral("destinationKey")).toString()).fileName();
            if (displayName.isEmpty())
                displayName = t.name.isEmpty() ? t.infoHash.left(8) + "..." : t.name;
            displayName.prepend(QStringLiteral("    "));
        } else {
            displayName = t.name.isEmpty() ? t.infoHash.left(8) + "..." : t.name;
            if (t.forceStarted) displayName.prepend(QStringLiteral("[F] "));
        }
        nameItem->setText(displayName);
        nameItem->setData(Qt::UserRole, meta);

        auto* sizeItem = ensureItem(row, 1);
        sizeItem->setText(t.totalWanted > 0 ? humanSize(t.totalWanted) : QStringLiteral("-"));
        sizeItem->setData(Qt::UserRole, static_cast<qlonglong>(t.totalWanted));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto* progItem = ensureItem(row, 2);
        progItem->setData(Qt::UserRole, t.progress);
        progItem->setText(QString::number(t.progress * 100, 'f', 1) + "%");
        progItem->setTextAlignment(Qt::AlignCenter);

        auto* stateItem = ensureItem(row, 3);
        QString stateIcon;
        if (t.stateString == QLatin1String("downloading")) stateIcon = QStringLiteral(":/icons/download.svg");
        else if (t.stateString == QLatin1String("paused")) stateIcon = QStringLiteral(":/icons/pause.svg");
        else if (t.stateString == QLatin1String("seeding")) stateIcon = QStringLiteral(":/icons/seed.svg");
        else if (t.stateString == QLatin1String("error")) stateIcon = QStringLiteral(":/icons/error.svg");
        else if (t.stateString == QLatin1String("metadata")) stateIcon = QStringLiteral(":/icons/waiting.svg");
        else if (t.stateString == QLatin1String("completed")) stateIcon = QStringLiteral(":/icons/check.svg");
        else if (t.stateString == QLatin1String("checking")) stateIcon = QStringLiteral(":/icons/waiting.svg");
        else stateIcon = QStringLiteral(":/icons/stalled.svg");
        if (!stateIcon.isEmpty())
            stateItem->setIcon(QIcon(stateIcon));
        stateItem->setToolTip(QString());
        if (!childRow && t.stateString == QLatin1String("error") && !t.errorMessage.isEmpty())
            stateItem->setToolTip(t.errorMessage);
        stateItem->setText(childRow ? streamBulkItemStatusText(childItem, &t) : torrentStatusText(t));
        stateItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);

        // TORRENT_PERSISTENCE_COLLAPSE Phase 4.1 (2026-05-20) — legacy rows
        // imported from torrents.json without a magnetUri (per audit D10)
        // can't be resumed automatically. Replace the Status cell with a
        // click-to-re-add button that switches the Tankorent tab to Search
        // and pre-fills the row's name. Stream-side bulk rows are excluded
        // (they have their own group-level recovery via the cohort restart
        // action; they don't carry the legacyNoMagnet flag in practice
        // because bulk rows always shipped with magnetUri).
        if (!childRow && t.legacyNoMagnet) {
            const QString rowName = t.name.isEmpty() ? t.infoHash.left(8) : t.name;
            auto* btn = new QPushButton(tr("Needs re-add"), m_transfersTable);
            btn->setObjectName(QStringLiteral("LegacyReAddButton"));
            btn->setToolTip(tr(
                "This download was migrated from a previous Tankoban version "
                "without a magnet link. Click to switch to Search and look "
                "it up again."));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton#LegacyReAddButton {"
                "  color: #ffffff; background: #d97706;"
                "  border: 0; border-radius: 4px; padding: 2px 10px;"
                "  font-weight: 600;"
                "} QPushButton#LegacyReAddButton:hover { background: #f59e0b; }"));
            connect(btn, &QPushButton::clicked, this, [this, rowName]() {
                if (m_tabWidget) m_tabWidget->setCurrentIndex(0);  // Search tab
                if (m_queryEdit) m_queryEdit->setText(rowName);
                startSearch();
            });
            m_transfersTable->setCellWidget(row, 3, btn);
        } else {
            // Defensive: in case a prior row at this index had a cellWidget,
            // clear it so the icon + text from the QTableWidgetItem above
            // actually paints.
            if (m_transfersTable->cellWidget(row, 3))
                m_transfersTable->removeCellWidget(row, 3);
        }

        auto* seedItem = ensureItem(row, 4);
        seedItem->setText(QString::number(t.seeds));
        seedItem->setData(Qt::UserRole, t.seeds);
        seedItem->setTextAlignment(Qt::AlignCenter);
        auto* peerItem = ensureItem(row, 5);
        peerItem->setText(QString::number(t.peers));
        peerItem->setData(Qt::UserRole, t.peers);
        peerItem->setTextAlignment(Qt::AlignCenter);

        auto* dlItem = ensureItem(row, 6);
        const QString dlText = humanSpeed(t.dlSpeed);
        dlItem->setText(dlText + (t.dlLimit > 0 && !dlText.isEmpty() ? QStringLiteral(" [L]") : QString()));
        dlItem->setToolTip(t.dlLimit > 0 && !dlText.isEmpty()
            ? QStringLiteral("Throttled: %1 max").arg(humanSpeed(t.dlLimit))
            : QString());
        dlItem->setData(Qt::UserRole, t.dlSpeed);
        dlItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto* ulItem = ensureItem(row, 7);
        ulItem->setText(humanSpeed(t.ulSpeed));
        ulItem->setData(Qt::UserRole, t.ulSpeed);
        ulItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        const int etaSecs = etaSecondsForTorrent(t);
        auto* etaItem = ensureItem(row, 8);
        etaItem->setText(etaSecs != INT_MAX ? etaTextFromSeconds(etaSecs)
                                            : (t.stateString == QLatin1String("seeding") ||
                                               t.stateString == QLatin1String("completed") ? QStringLiteral("-") : QStringLiteral("...")));
        etaItem->setData(Qt::UserRole, etaSecs);
        etaItem->setTextAlignment(Qt::AlignCenter);

        auto* catItem9 = ensureItem(row, 9);
        catItem9->setText(prettyCategoryName(t.category));
        catItem9->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* queueItem = ensureItem(row, 10);
        queueItem->setText(t.queuePosition >= 0 ? QString::number(t.queuePosition + 1) : QStringLiteral("-"));
        queueItem->setData(Qt::UserRole, t.queuePosition);
        queueItem->setTextAlignment(Qt::AlignCenter);

        auto* infoItem = ensureItem(row, 11);
        infoItem->setText(QString());
        infoItem->setIcon(childRow ? QIcon() : QIcon(QStringLiteral(":/icons/file.svg")));
        infoItem->setToolTip(childRow ? QString() : QStringLiteral("View Files"));
        infoItem->setTextAlignment(Qt::AlignCenter);
    };

    for (const QString& groupId : groupOrder) {
        const QJsonObject group = groups.value(groupId).toObject();
        const QJsonArray items = group.value(QStringLiteral("items")).toArray();
        int publishedCount = 0, failedCount = 0, cancelledCount = 0, orphanCount = 0;
        int terminalCount = 0, publishingCount = 0;
        qint64 totalWanted = 0, totalDone = 0;
        int groupDl = 0, groupUl = 0, groupSeeds = 0, groupPeers = 0;
        bool anyActiveChild = false, anyPaused = false, anyDownloading = false;
        bool allActiveDownloading = true, anyStalled = false;
        int maxEtaSecs = 0;
        QSet<QString> countedHashes;

        for (const auto& value : items) {
            const QJsonObject item = value.toObject();
            const QString state = item.value(QStringLiteral("itemState")).toString();
            if (state == QLatin1String("Published") || state == QLatin1String("Completed")) ++publishedCount;
            if (isFailedStreamBulkItemState(state)) ++failedCount;
            if (state == QLatin1String("Cancelled")) ++cancelledCount;
            if (state == QLatin1String("Orphaned")) ++orphanCount;
            if (state == QLatin1String("Publishing")) ++publishingCount;
            if (isTerminalStreamBulkItemState(state)) ++terminalCount;

            const QString hash = item.value(QStringLiteral("infoHash")).toString().toLower();
            if (hash.isEmpty() || countedHashes.contains(hash) || isSkippedForWantedBytes(state))
                continue;
            auto activeIt = activeByHash.constFind(hash);
            if (activeIt == activeByHash.cend())
                continue;
            countedHashes.insert(hash);
            const TorrentInfo& t = activeIt.value();
            anyActiveChild = true;
            anyPaused = anyPaused || t.stateString == QLatin1String("paused");
            anyDownloading = anyDownloading || t.stateString == QLatin1String("downloading");
            allActiveDownloading = allActiveDownloading && t.stateString == QLatin1String("downloading");
            if (t.stateString == QLatin1String("downloading") && t.seeds == 0 && t.peers == 0) {
                if (!m_zeroPeerSeedSinceByHash.contains(hash))
                    m_zeroPeerSeedSinceByHash.insert(hash, now);
                anyStalled = anyStalled || (now - m_zeroPeerSeedSinceByHash.value(hash) >= kGroupStallThresholdMs);
            } else {
                m_zeroPeerSeedSinceByHash.remove(hash);
            }
            totalWanted += t.totalWanted;
            totalDone += t.totalDone;
            groupDl += t.dlSpeed;
            groupUl += t.ulSpeed;
            groupSeeds += t.seeds;
            groupPeers += t.peers;
            const int eta = etaSecondsForTorrent(t);
            if (eta != INT_MAX)
                maxEtaSecs = qMax(maxEtaSecs, eta);
        }

        const int totalItems = items.size();
        const bool allTerminal = totalItems > 0 && terminalCount == totalItems;
        const bool allCancelled = totalItems > 0 && cancelledCount == totalItems;
        const bool allOrphaned = totalItems > 0 && orphanCount == totalItems;
        const double progress = totalWanted > 0
            ? qBound(0.0, static_cast<double>(totalDone) / static_cast<double>(totalWanted), 1.0)
            : (totalItems > 0 ? static_cast<double>(terminalCount) / static_cast<double>(totalItems) : 0.0);

        QString statusText;
        if (allOrphaned) statusText = QStringLiteral("Orphaned");
        else if (allCancelled) statusText = QStringLiteral("Cancelled");
        else if (failedCount > 0 && allTerminal) statusText = QStringLiteral("Done - %1 failed").arg(failedCount);
        else if (failedCount > 0) statusText = QStringLiteral("Downloading - %1 failed").arg(failedCount);
        else if (publishingCount > 0) statusText = QStringLiteral("Publishing");
        else if (anyActiveChild && anyPaused && !anyDownloading) statusText = QStringLiteral("Paused (group)");
        else if (allTerminal) statusText = QStringLiteral("Done");
        else if (anyStalled) statusText = QStringLiteral("Downloading - stalled");
        else statusText = QStringLiteral("Downloading");
        if (orphanCount > 0 && !allOrphaned)
            statusText += QStringLiteral(" - %1 orphan").arg(orphanCount);

        const int row = appendRow();
        const QVariantMap meta = transferRowMeta(RowKind::Group, QString(), groupId);
        auto* nameItem = ensureItem(row, 0);
        // STREAM_BULK_DOWNLOAD UI fix 2026-05-10 — leave display text empty;
        // the cell widget below paints the title + chip. Setting both made
        // the underlying item text leak through transparent areas of the
        // host QWidget, producing the visual where "Season 1" appeared to
        // bleed into the STREAM chip on Hemanth's 2026-05-10 screenshot.
        const QString groupLabel = group.value(QStringLiteral("label")).toString(groupId);
        nameItem->setText(QString());
        nameItem->setData(Qt::UserRole, meta);

        auto* host = new QWidget(m_transfersTable);
        auto* layout = new QHBoxLayout(host);
        layout->setContentsMargins(4, 0, 6, 0);
        layout->setSpacing(8);
        auto* label = new QLabel(groupLabel, host);
        label->setTextFormat(Qt::PlainText);
        // Allow the title label to elide when the column is narrower than
        // the natural text width, so the chip stays cleanly on the right
        // instead of being shoved off-screen or visually merging with a
        // truncated title.
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setMinimumWidth(0);
        if (allOrphaned)
            label->setStyleSheet(QStringLiteral("color: #888888;"));
        auto* chip = new QLabel(QStringLiteral("STREAM"), host);
        chip->setObjectName(QStringLiteral("StreamBulkChip"));
        chip->setStyleSheet(QStringLiteral(
            "#StreamBulkChip { color: #eeeeee; border: 1px solid #666666;"
            " border-radius: 3px; padding: 1px 5px; font-size: 9px;"
            " font-weight: 600; letter-spacing: 0px; }"));
        chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(label);
        layout->addStretch(1);
        layout->addWidget(chip);
        m_transfersTable->setCellWidget(row, 0, host);

        auto* sizeItem = ensureItem(row, 1);
        sizeItem->setText(totalWanted > 0 ? humanSize(totalWanted) : QStringLiteral("-"));
        sizeItem->setData(Qt::UserRole, static_cast<qlonglong>(totalWanted));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setProgressWidget(row, QStringLiteral("%1/%2").arg(publishedCount).arg(totalItems), progress);
        // STREAM_BULK_DOWNLOAD UI fix 2026-05-10 — match flat-row column
        // alignment so group rows visually align with the regular torrent
        // rows below. Status stays default-left like flat rows; Seeds/Peers
        // center; DL/UL right-align; ETA/Category/Queue/Info center.
        // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — group-parent row's
        // Status cell now carries a state icon matching the flat-row
        // pattern at lines 1727-1741. Without the icon, the parent
        // status text rendered visually shifted relative to the child
        // rows' icon+text combo. Aggregate state picks the icon in the
        // same priority order as the textual statusText resolution
        // above so icon and text always agree:
        // - allOrphaned → error icon
        // - failedCount > 0 + allTerminal → error icon
        // - any active child downloading → download icon
        // - any stalled → stalled icon
        // - any active child paused → pause icon
        // - allTerminal + all published → check icon
        // - else → waiting icon
        auto* parentStatusItem = ensureItem(row, 3);
        QString parentStatusIcon;
        if (allOrphaned) {
            parentStatusIcon = QStringLiteral(":/icons/error.svg");
        } else if (failedCount > 0 && allTerminal) {
            parentStatusIcon = QStringLiteral(":/icons/error.svg");
        } else if (anyDownloading) {
            parentStatusIcon = QStringLiteral(":/icons/download.svg");
        } else if (anyStalled) {
            parentStatusIcon = QStringLiteral(":/icons/stalled.svg");
        } else if (anyActiveChild && anyPaused) {
            parentStatusIcon = QStringLiteral(":/icons/pause.svg");
        } else if (allTerminal && publishedCount == totalItems) {
            parentStatusIcon = QStringLiteral(":/icons/check.svg");
        } else {
            parentStatusIcon = QStringLiteral(":/icons/waiting.svg");
        }
        parentStatusItem->setIcon(QIcon(parentStatusIcon));
        parentStatusItem->setText(statusText);
        parentStatusItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        auto* seedsItem = ensureItem(row, 4);
        seedsItem->setText(QString::number(groupSeeds));
        seedsItem->setTextAlignment(Qt::AlignCenter);
        auto* peersItem = ensureItem(row, 5);
        peersItem->setText(QString::number(groupPeers));
        peersItem->setTextAlignment(Qt::AlignCenter);
        auto* dlItem = ensureItem(row, 6);
        dlItem->setText(humanSpeed(groupDl));
        dlItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* ulItem = ensureItem(row, 7);
        ulItem->setText(humanSpeed(groupUl));
        ulItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* etaItem = ensureItem(row, 8);
        etaItem->setText(anyActiveChild && allActiveDownloading && maxEtaSecs > 0
                          ? etaTextFromSeconds(maxEtaSecs) : QStringLiteral("-"));
        etaItem->setTextAlignment(Qt::AlignCenter);
        auto* catItem = ensureItem(row, 9);
        catItem->setText(QStringLiteral("Videos"));
        catItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* queueItem = ensureItem(row, 10);
        queueItem->setText(QStringLiteral("-"));
        queueItem->setTextAlignment(Qt::AlignCenter);
        auto* infoItem = ensureItem(row, 11);
        infoItem->setText(QString());
        infoItem->setTextAlignment(Qt::AlignCenter);
        if (allOrphaned) {
            for (int col = 0; col < m_transfersTable->columnCount(); ++col) {
                if (auto* item = m_transfersTable->item(row, col))
                    item->setForeground(QBrush(QColor(QStringLiteral("#888888"))));
            }
        }

        if (m_expandedGroupIds.contains(groupId)) {
            for (const auto& value : items) {
                const QJsonObject item = value.toObject();
                const QString hash = item.value(QStringLiteral("infoHash")).toString().toLower();
                TorrentInfo child = activeByHash.value(hash);
                if (child.infoHash.isEmpty()) {
                    child.infoHash = hash;
                    child.name = item.value(QStringLiteral("canonicalFilename")).toString();
                    const QString state = item.value(QStringLiteral("itemState")).toString();
                    child.progress = (state == QLatin1String("Published") ||
                                      state == QLatin1String("Completed") ||
                                      state == QLatin1String("Failed")) ? 1.0f : 0.0f;
                    child.category = QStringLiteral("videos");
                }
                renderTorrentRow(appendRow(), child, true, item, groupId);
            }
        }
    }

    for (const TorrentInfo& t : flatRows)
        renderTorrentRow(appendRow(), t, false);

    m_transfersTable->setSortingEnabled(!hasGroupedRows);
    if (!hasGroupedRows && m_sortCol >= 0)
        m_transfersTable->sortByColumn(m_sortCol, m_sortOrder);

    m_transfersTable->clearSelection();
    for (int i = 0; i < m_transfersTable->rowCount(); ++i) {
        const QString key = transferRowSelectionKey(transferRowMeta(m_transfersTable, i));
        if (!key.isEmpty() && selectedKeys.contains(key))
            m_transfersTable->selectRow(i);
    }

    const int historyCount = m_client->listHistory().size();
    m_downloadStatus->setText(QStringLiteral("Active: %1 | Seeding: %2 | History: %3")
                                  .arg(activeCount).arg(seedingCount).arg(historyCount));
    m_tabWidget->setTabText(1, active.isEmpty() ? QStringLiteral("Transfers")
                                                : QStringLiteral("Transfers (%1)").arg(active.size()));
    if (totalDl > 0 || totalUl > 0)
        m_backendStatus->setText(QStringLiteral("DL %1  UL %2")
                                  .arg(humanSpeed(totalDl), humanSpeed(totalUl)));
    else
        m_backendStatus->setText(QString());
}


void TankorentPage::showTransfersContextMenu(const QPoint& pos)
{
    if (!m_client) return;

    auto* clickedItem = m_transfersTable->itemAt(pos);
    if (!clickedItem) return;
    const int clickedRow = clickedItem->row();
    const QVariantMap clickedMeta = transferRowMeta(m_transfersTable, clickedRow);
    const RowKind clickedKind = transferRowKind(clickedMeta);
    if (clickedKind == RowKind::Group) {
        showGroupContextMenu(pos, clickedMeta.value(QStringLiteral("groupId")).toString());
        return;
    }
    if (clickedKind == RowKind::GroupChild)
        return;

    QStringList selectedHashes;
    const auto selectedRows = m_transfersTable->selectionModel()
        ? m_transfersTable->selectionModel()->selectedRows()
        : QModelIndexList();
    for (const QModelIndex& index : selectedRows) {
        const QVariantMap meta = transferRowMeta(m_transfersTable, index.row());
        if (transferRowKind(meta) == RowKind::FlatTorrent) {
            const QString h = meta.value(QStringLiteral("infoHash")).toString();
            if (!h.isEmpty() && !selectedHashes.contains(h))
                selectedHashes.append(h);
        }
    }
    if (selectedHashes.isEmpty()) {
        const QString h = clickedMeta.value(QStringLiteral("infoHash")).toString();
        if (!h.isEmpty())
            selectedHashes.append(h);
    }
    if (selectedHashes.isEmpty()) return;

    // Find first selected torrent info
    QString firstHash = selectedHashes.first();
    TorrentInfo firstInfo;
    for (const auto& t : m_cachedActive) {
        if (t.infoHash == firstHash) { firstInfo = t; break; }
    }

    bool anyPaused = false, anyForced = false;
    for (const auto& h : selectedHashes) {
        for (const auto& t : m_cachedActive) {
            if (t.infoHash == h) {
                if (t.stateString == "paused") anyPaused = true;
                if (t.forceStarted) anyForced = true;
                break;
            }
        }
    }

    QMenu* menu = ContextMenuHelper::createMenu(this);

    // Resume / Pause
    if (anyPaused) {
        menu->addAction("Resume", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->resumeTorrent(h);
        });
    } else {
        menu->addAction("Pause", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->pauseTorrent(h);
        });
    }

    // Force Start
    if (anyForced) {
        menu->addAction("Cancel Force Start", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->clearForceStart(h);
        });
    } else {
        menu->addAction("Force Start", this, [this, selectedHashes]() {
            for (const auto& h : selectedHashes) m_client->forceStart(h);
        });
    }

    menu->addSeparator();

    // Queue submenu
    auto* queueMenu = menu->addMenu("Queue");
    queueMenu->addAction("Move Up", this, [this, firstHash]() {
        m_client->queuePositionUp(firstHash);
    });
    queueMenu->addAction("Move Down", this, [this, firstHash]() {
        m_client->queuePositionDown(firstHash);
    });

    // Limits submenu
    auto* limitsMenu = menu->addMenu("Limits");
    limitsMenu->addAction("Speed Limits...", this, [this, firstHash, firstInfo]() {
        int curDl = firstInfo.dlLimit / 1024;
        int curUl = firstInfo.ulLimit / 1024;
        SpeedLimitDialog dlg("Speed Limit: " + firstInfo.name, curDl, curUl, this);
        if (dlg.exec() == QDialog::Accepted)
            m_client->setSpeedLimits(firstHash, dlg.dlLimitBps(), dlg.ulLimitBps());
    });
    limitsMenu->addAction("Seeding Rules...", this, [this, firstHash, firstInfo]() {
        SeedingRulesDialog dlg("Seeding Rules: " + firstInfo.name, 0.f, 0, this);
        if (dlg.exec() == QDialog::Accepted)
            m_client->setSeedingRules(firstHash, dlg.ratioLimit(), dlg.seedTimeSecs());
    });

    // Advanced submenu
    auto* advMenu = menu->addMenu("Advanced");
    advMenu->addAction("Force Recheck", this, [this, firstHash]() {
        m_client->forceRecheck(firstHash);
    });
    advMenu->addAction("Force Reannounce", this, [this, firstHash]() {
        m_client->forceReannounce(firstHash);
    });
    auto* seqAction = advMenu->addAction("Sequential Download");
    seqAction->setCheckable(true);
    seqAction->setChecked(firstInfo.sequential);
    connect(seqAction, &QAction::toggled, this, [this, firstHash](bool on) {
        m_client->engine()->setSequentialDownload(firstHash, on);
    });

    menu->addSeparator();

    // Open Folder
    auto* openFolder = menu->addAction("Open Folder", this, [firstInfo]() {
        if (!firstInfo.savePath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(firstInfo.savePath));
    });
    openFolder->setEnabled(!firstInfo.savePath.isEmpty());

    // Set Location — relocate the torrent's downloaded files to a new folder.
    // Single-selection only; multi-select move would need batched confirmation
    // and per-row destination handling that's out of scope for v1.
    auto* setLocationAction = menu->addAction("Set Location...", this,
        [this, firstHash, firstInfo]() {
            const QString seed = firstInfo.savePath.isEmpty()
                ? QDir::homePath()
                : firstInfo.savePath;
            const QString chosen = QFileDialog::getExistingDirectory(
                this, tr("Set Location: %1").arg(firstInfo.name), seed,
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (chosen.isEmpty()) return;
            if (QDir(chosen).absolutePath().compare(
                    QDir(firstInfo.savePath).absolutePath(),
                    Qt::CaseInsensitive) == 0) {
                return;
            }
            m_client->moveStorage(firstHash, chosen);
            Toast::show(this, tr("Moving \"%1\"...").arg(firstInfo.name));
        });
    setLocationAction->setEnabled(selectedHashes.size() == 1);

    menu->addAction("View Files...", this, [this, firstHash]() {
        auto* dlg = new TorrentPropertiesWidget(m_client, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->showTorrent(firstHash, TorrentPropertiesWidget::TabFiles);
        dlg->show();
    });

    menu->addAction("Properties...", this, [this, firstHash]() {
        auto* dlg = new TorrentPropertiesWidget(m_client, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->showTorrent(firstHash);
        dlg->show();
    });

    menu->addSeparator();

    // Copy submenu
    auto* copyMenu = menu->addMenu("Copy");
    copyMenu->addAction("Copy Name", this, [firstInfo]() {
        ContextMenuHelper::copyToClipboard(firstInfo.name);
    });
    copyMenu->addAction("Copy Info Hash", this, [firstHash]() {
        ContextMenuHelper::copyToClipboard(firstHash);
    });

    menu->addSeparator();

    // Remove (danger actions)
    auto* removeAction = ContextMenuHelper::addDangerAction(menu, "Remove");
    connect(removeAction, &QAction::triggered, this, [this, selectedHashes]() {
        for (const auto& h : selectedHashes) m_client->deleteTorrent(h, false);
    });

    auto* removeWithFiles = ContextMenuHelper::addDangerAction(menu, "Remove + Delete Files");
    connect(removeWithFiles, &QAction::triggered, this, [this, selectedHashes]() {
        if (ContextMenuHelper::confirmRemove(this, "Delete Files",
                QString("Remove %1 torrent(s) and delete all downloaded files?").arg(selectedHashes.size()))) {
            for (const auto& h : selectedHashes) m_client->deleteTorrent(h, true);
        }
    });

    menu->exec(m_transfersTable->viewport()->mapToGlobal(pos));
    delete menu;
}

void TankorentPage::showGroupContextMenu(const QPoint& pos, const QString& groupId)
{
    if (!m_client || groupId.isEmpty()) return;

    const QJsonObject group = m_client->streamBulkGroups().value(groupId).toObject();
    if (group.isEmpty()) return;

    const QJsonArray items = group.value(QStringLiteral("items")).toArray();
    QStringList downloadingHashes;
    QStringList pausedHashes;
    QSet<QString> groupHashes;
    int failedCount = 0;
    int publishedCount = 0;
    int terminalCount = 0;
    int orphanCount = 0;
    for (const auto& value : items) {
        const QJsonObject item = value.toObject();
        const QString state = item.value(QStringLiteral("itemState")).toString();
        if (isFailedStreamBulkItemState(state))
            ++failedCount;
        if (state == QLatin1String("Orphaned"))
            ++orphanCount;
        if (state == QLatin1String("Published") || state == QLatin1String("Completed"))
            ++publishedCount;
        if (isTerminalStreamBulkItemState(state))
            ++terminalCount;
        const QString hash = item.value(QStringLiteral("infoHash")).toString();
        if (!hash.isEmpty())
            groupHashes.insert(hash);
    }

    for (const TorrentInfo& t : m_cachedActive) {
        if (t.streamGroupId != groupId && !groupHashes.contains(t.infoHash))
            continue;
        if (t.stateString == QLatin1String("downloading") && !downloadingHashes.contains(t.infoHash))
            downloadingHashes.append(t.infoHash);
        if (t.stateString == QLatin1String("paused") && !pausedHashes.contains(t.infoHash))
            pausedHashes.append(t.infoHash);
    }

    const QString label = group.value(QStringLiteral("label")).toString(groupId);
    QMenu* menu = ContextMenuHelper::createMenu(this);

    // ── Active operations ──────────────────────────────────────────────
    auto* pauseAction = menu->addAction(QStringLiteral("Pause group"), this, [this, downloadingHashes]() {
        for (const QString& hash : downloadingHashes)
            m_client->pauseTorrent(hash);
    });
    pauseAction->setEnabled(!downloadingHashes.isEmpty());

    auto* resumeAction = menu->addAction(QStringLiteral("Resume group"), this, [this, pausedHashes]() {
        for (const QString& hash : pausedHashes)
            m_client->resumeTorrent(hash);
    });
    resumeAction->setEnabled(!pausedHashes.isEmpty());

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — Restart group is always
    // available. Clears libtorrent error state on stuck items + resets
    // non-Published items to Pending + re-engages the cohort scheduler.
    // Published items are intentionally untouched.
    menu->addAction(QStringLiteral("Restart group"),
        this, [this, groupId]() {
            m_client->restartStreamBulkGroup(groupId);
            refreshTransfers();
        });

    menu->addSeparator();

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — show-in-folder +
    // expand/collapse stay above the danger zone for muscle-memory.
    auto* showFolder = menu->addAction(QStringLiteral("Show in folder"), this, [this, group]() {
        QString folder;
        const QJsonArray groupItems = group.value(QStringLiteral("items")).toArray();
        if (!groupItems.isEmpty())
            folder = destinationFolderForGroupItem(group, groupItems.at(0).toObject());
        if (folder.isEmpty())
            folder = group.value(QStringLiteral("destinationRoot")).toString();
        if (folder.isEmpty())
            folder = fallbackVideosRoot(m_client);
        if (!folder.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    showFolder->setEnabled(!group.value(QStringLiteral("destinationRoot")).toString().isEmpty()
                           || !fallbackVideosRoot(m_client).isEmpty());

    const bool expanded = m_expandedGroupIds.contains(groupId);
    menu->addAction(expanded ? QStringLiteral("Collapse details") : QStringLiteral("Expand details"),
                    this, [this, groupId, expanded]() {
        if (expanded)
            m_expandedGroupIds.remove(groupId);
        else
            m_expandedGroupIds.insert(groupId);
        saveExpandedStreamBulkGroups();
        refreshTransfers();
    });

    if (failedCount > 0) {
        menu->addSeparator();
        menu->addAction(QStringLiteral("Retry failed"), this, [this, groupId]() {
            m_client->retryStreamBulkGroupFailedItems(groupId);
            refreshTransfers();
        });
    }

    menu->addSeparator();

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — danger zone mirrors the
    // flat-row pattern at lines 2163-2174 (Remove / Remove + Delete Files).
    // The bulk equivalents route through cancelStreamBulkGroup with an
    // explicit deleteFiles flag instead of the prior auto-heuristic +
    // confirmation-dialog branching at the old line 2237.
    const int totalItems = items.size();
    auto* removeAction = ContextMenuHelper::addDangerAction(menu, QStringLiteral("Remove"));
    connect(removeAction, &QAction::triggered, this, [this, groupId]() {
        m_client->cancelStreamBulkGroup(groupId, /*deleteFiles=*/false);
        m_expandedGroupIds.remove(groupId);
        saveExpandedStreamBulkGroups();
        refreshTransfers();
    });

    auto* removeWithFiles = ContextMenuHelper::addDangerAction(menu, QStringLiteral("Remove + Delete Files"));
    connect(removeWithFiles, &QAction::triggered, this, [this, groupId, totalItems]() {
        if (ContextMenuHelper::confirmRemove(this, QStringLiteral("Delete Files"),
                QStringLiteral("Remove stream bulk group (%1 item(s)) and delete all downloaded files?").arg(totalItems))) {
            m_client->cancelStreamBulkGroup(groupId, /*deleteFiles=*/true);
            m_expandedGroupIds.remove(groupId);
            saveExpandedStreamBulkGroups();
            refreshTransfers();
        }
    });

    Q_UNUSED(label);
    Q_UNUSED(publishedCount);
    Q_UNUSED(terminalCount);
    Q_UNUSED(orphanCount);
    menu->exec(m_transfersTable->viewport()->mapToGlobal(pos));
    delete menu;
}

void TankorentPage::onSourcesClicked()
{
    IndexerStatusPanel dlg(m_nam, this);
    dlg.exec();
    // configurationChanged fires on enable/credential changes; no live handling
    // needed — the next startSearch re-reads QSettings via TankorentSearchService.
}

QPair<int, int> TankorentPage::addMagnetBatch(const QStringList& magnets,
                                              const QString& category,
                                              bool startImmediately)
{
    const auto defaults = m_client->defaultPaths();
    const QString destPath = defaults.value(category);

    int added = 0;
    int skipped = 0;
    for (const QString& magnet : magnets) {
        if (m_client->isDuplicate(magnet)) {
            ++skipped;
            continue;
        }
        const QString hash = m_client->resolveMetadata(magnet);
        if (hash.isEmpty()) {
            ++skipped;
            continue;
        }

        AddTorrentConfig config;
        config.category        = category;
        config.destinationPath = destPath;
        config.contentLayout   = QStringLiteral("original");
        config.streamGroupId   = QString();
        config.sequential      = false;
        config.startPaused     = !startImmediately;
        m_client->startDownload(hash, config);
        ++added;
    }
    return { added, skipped };
}

void TankorentPage::onAddFromUrlClicked()
{
    AddFromUrlDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto [added, skipped] = addMagnetBatch(
        dlg.magnets(), dlg.category(), dlg.startImmediately());

    if (added > 0) {
        m_tabWidget->setCurrentIndex(1); // Transfers tab
        m_searchStatus->setText(skipped > 0
            ? QStringLiteral("Added %1, skipped %2").arg(added).arg(skipped)
            : QStringLiteral("Added %1 torrent(s)").arg(added));
    } else if (skipped > 0) {
        m_searchStatus->setText(QStringLiteral("All %1 skipped (duplicates or invalid)").arg(skipped));
    }
}

// STREAM_ADD_TO_TANKORENT_DIALOG_FIX 2026-05-06 — cross-page magnet
// hand-off entry point. Was previously routed through addMagnetBatch
// (the BULK path which by design skips the per-magnet AddTorrentDialog —
// popping a dialog 10× during a bulk add would be hostile UX), causing
// a regression where the cross-page right-click → "Add torrent to
// Tankorent" auto-started without the file-selection / priority overlay.
// Hemanth verbatim 2026-05-06: "Why is it adding torrent to tankorent
// without the torrent downloader overlay popping up? The overlay where
// you select or deselect files, set download priorities etc — it needs
// to be there." Now mirrors the single-add path exactly via the shared
// startSingleAddFlow helper. displayName carries the release name from
// StreamPage's onAddToTankorentRequested (extractReleaseName output) and
// becomes the dialog's title — what the user will see on the modal.
void TankorentPage::addMagnetFromExternal(const QString& magnetUri,
                                          const QString& displayName)
{
    auto& dlog = DebugLogBuffer::instance();
    dlog.info("tankorent",
        QStringLiteral("addMagnetFromExternal entered: display='%1' magnet=%2")
            .arg(displayName).arg(magnetUri.left(80)));

    // Caller-context validation; full sequence (m_client null check,
    // duplicate detect, resolveMetadata, dialog exec, startDownload /
    // deleteTorrent) is shared via startSingleAddFlow.
    if (magnetUri.isEmpty()) {
        dlog.warning("tankorent",
            QStringLiteral("addMagnetFromExternal: empty magnet (display='%1')")
                .arg(displayName));
        if (m_searchStatus) m_searchStatus->setText("Empty magnet — nothing to add");
        return;
    }

    // Title fallback: use displayName when present (StreamPage extracts the
    // release name); fall back to a truncated magnet form so the dialog
    // header still has something to render. AddTorrentDialog will overlay
    // the engine-supplied torrent name once metadataReady fires anyway.
    const QString title = !displayName.isEmpty()
                              ? displayName
                              : QStringLiteral("Magnet: %1").arg(magnetUri.left(40));
    startSingleAddFlow(magnetUri, title);
}

void TankorentPage::addMagnetGroupFromExternal(
    const StreamBulkGroupRecord& group,
    const tankostream::stream::BulkPackVerificationResult& verifierOutput,
    const QString& displayLabel)
{
    if (!m_client)
        return;

    m_client->dispatchStreamBulkGroup(group, verifierOutput);
    if (m_tabWidget)
        m_tabWidget->setCurrentIndex(1);

    const QString label = !displayLabel.isEmpty()
        ? displayLabel
        : (!group.label.isEmpty() ? group.label : group.groupId);
    if (m_searchStatus)
        m_searchStatus->setText(QStringLiteral("Stream download started: %1").arg(label));
    refreshTransfers();
}

// ── Drag-drop ───────────────────────────────────────────────────────────────

void TankorentPage::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime) return;

    if (mime->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    if (mime->hasText()) {
        const QString text = mime->text().trimmed();
        if (text.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
            event->acceptProposedAction();
    }
}

void TankorentPage::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime) return;

    QStringList magnets;
    int torrentFileCount = 0;  // local .torrent files + .torrent URLs — unsupported

    auto classify = [&](const QString& raw) {
        const QString item = raw.trimmed();
        if (item.isEmpty()) return;
        if (item.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
            magnets.append(item);
            return;
        }
        // Local file URL or HTTP URL ending in .torrent — both need the
        // add-torrent-file engine path (see Batch 5.1 scope note).
        const QString lower = item.toLower();
        if (lower.endsWith(QLatin1String(".torrent")) ||
            QFileInfo(item).suffix().toLower() == QLatin1String("torrent")) {
            ++torrentFileCount;
        }
    };

    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.scheme() == QLatin1String("magnet"))
                magnets.append(url.toString());
            else if (url.isLocalFile())
                classify(url.toLocalFile());
            else
                classify(url.toString());
        }
    }
    if (mime->hasText()) {
        for (const QString& line : mime->text().split('\n', Qt::SkipEmptyParts))
            classify(line);
    }

    magnets.removeDuplicates();

    if (magnets.isEmpty()) {
        if (torrentFileCount > 0) {
            Toast::show(this, QStringLiteral(
                ".torrent files aren't supported yet — use magnet links."));
        } else {
            Toast::show(this, QStringLiteral("No magnet links found in drop."));
        }
        event->acceptProposedAction();
        return;
    }

    // Surface the magnets through AddFromUrlDialog so the user picks a
    // category + start-immediately flag for the batch. Matches the TODO's
    // "Multiple items → open AddFromUrlDialog pre-filled" contract.
    AddFromUrlDialog dlg(this, magnets.join('\n'));
    if (dlg.exec() == QDialog::Accepted) {
        const auto [added, skipped] = addMagnetBatch(
            dlg.magnets(), dlg.category(), dlg.startImmediately());

        if (added > 0) {
            m_tabWidget->setCurrentIndex(1);
            QString msg = QStringLiteral("Added %1 torrent(s)").arg(added);
            if (skipped > 0)
                msg += QStringLiteral(", skipped %1").arg(skipped);
            if (torrentFileCount > 0)
                msg += QStringLiteral(" (%1 .torrent file%2 ignored)")
                           .arg(torrentFileCount)
                           .arg(torrentFileCount == 1 ? "" : "s");
            Toast::show(this, msg);
        }
    }

    event->acceptProposedAction();
}

void TankorentPage::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Paste)) {
        QWidget* focused = QApplication::focusWidget();
        const bool inTextInput = qobject_cast<QLineEdit*>(focused)
                              || qobject_cast<QTextEdit*>(focused);
        if (!inTextInput) {
            onAddFromUrlClicked();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void TankorentPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- no-op restore. TankorentPage
    // has no deep state participating in the per-mode back stack today; search
    // + results live on the same surface. Phase 1+ may extend.
    Q_UNUSED(target);
}
