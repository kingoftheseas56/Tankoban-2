#include "WesternComicsPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "core/CoreBridge.h"
#include "core/manga/MangaDownloadIndex.h"
#include "core/manga/MangaDownloader.h"
#include "core/manga/MangaResult.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/MangaCatalogTypes.h"
#include "core/manga/WesternCatalogLoader.h"
#include "core/manga/WesternLibrary.h"
#include "core/manga/WesternIssueKey.h"
#include "core/manga/WesternVolumeDownloader.h"
#include "core/manga/ReadComicsScraper.h"
#include "core/manga/ReadAllComicsScraper.h"
#include "core/torrent/TorrentClient.h"
#include "comics/ComicsSeriesView.h"
#include "comics/ComicsTankoyomiSearchWidget.h"
#include "comics/VolumeTile.h"

#include "ui/readers/comic_progress_key.h"
#include "ui/widgets/FadingStackedWidget.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QPointer>
#include <memory>
#include <algorithm>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QIcon>
#include <QScrollArea>
#include <QSettings>
#include <QDir>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QCollator>
#include <QCryptographicHash>
#include <QShowEvent>
#include <QEvent>
#include <QUrl>
#include <QStyle>
#include <QSize>
#include <QMetaObject>

// COMICS_OPEN_TRACE — sibling of the MangaPage helper; both write to
// %TEMP%/comics_open_trace.log. File scope so ctor-side lambdas can see it.
namespace {
void comicsOpenTrace(const QString& event)
{
    static const QString path = QDir::temp().absoluteFilePath(QStringLiteral("comics_open_trace.log"));
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentMSecsSinceEpoch() << '\t' << event << '\n';
    }
}

// LayerEntry factory. pageId is "western_comics" (the Western page's own nav
// domain; MangaPage keeps the legacy "comics" id — see fork 'history-key'/pageId).
tankoban::ui::LayerEntry makeWesternLayer(const QString& kind, const QString& label,
                                          const QJsonObject& blob = {}) {
    return tankoban::ui::LayerEntry{
        QStringLiteral("western_comics"),
        kind,
        label,
        blob
    };
}

// Normalize a Western series title for cross-source matching: drop a
// "(Publisher: …)" / "(2003)" suffix, lower-case, keep alphanumerics + single
// spaces. "Invincible (Publisher: Image Comics)" and "Invincible" both
// normalize to "invincible".
QString normalizeWesternTitle(const QString& raw)
{
    QString s = raw;
    static const QRegularExpression kParen(QStringLiteral(R"(\s*\([^)]*\))"));
    s.remove(kParen);
    s = s.toLower();
    static const QRegularExpression kNonAlnum(QStringLiteral(R"([^a-z0-9]+)"));
    s.replace(kNonAlnum, QStringLiteral(" "));
    return s.trimmed();
}

static const QStringList COMIC_EXTS = {"*.cbz", "*.cbr", "*.rar"};
static constexpr const char* GETCOMICS_SOURCE_ID = "getcomics";
} // namespace

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------

WesternComicsPage::WesternComicsPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName(QStringLiteral("western_comics"));

    buildUI();

    // The page's OWN series-detail view (the deep decoupling). Constructed with
    // null AniList/premium/nyaa pointers — Western series never use the AniList/
    // mangafire enrichment path (it would corrupt a Western comic's identity).
    // The download index is injected later (setMangaDownloadIndex); the series
    // view tolerates a null index until then. Render is via
    // populateVolumeRowsFromCatalog (pure-render/no-network), never showSeries().
    m_seriesView = new tankoban::manga::comics::ComicsSeriesView(
        /*client=*/nullptr, /*cache=*/nullptr, /*catalog=*/nullptr,
        /*nyaa=*/nullptr, /*downloadIndex=*/nullptr, this);
    m_stack->addWidget(m_seriesView);
    wireSeriesView();

    // Per-user western library store (the only OWNED engine object). Needs the
    // shared JsonStore from CoreBridge. libraryChanged -> re-render My Library.
    m_westernLibrary = new tankoban::manga::WesternLibrary(&m_bridge->store(), this);
    connect(m_westernLibrary, &tankoban::manga::WesternLibrary::libraryChanged,
            this, [this]() { refreshWesternLibrary(); });
    // Back-fill any pre-arc western downloads from disk (grid renders are no-ops
    // until the first showLibraryMode; first activate renders the store).
    reconcileWesternLibraryFromDisk();
}

WesternComicsPage::~WesternComicsPage() = default;

// ---------------------------------------------------------------------------
// injected shared engine
// ---------------------------------------------------------------------------

void WesternComicsPage::setSourceRegistry(MangaSourceRegistry* registry)
{
    if (m_sourceRegistry == registry) return;
    m_sourceRegistry = registry;
    grabScrapersFromRegistry();
    if (m_seriesView) m_seriesView->setSourceRegistry(registry);
    // The search-takeover wires scraper searchFinished at ctor-time, so it can
    // only be built once the registry (and NAM, for poster fetches) have been
    // injected. ensureSearchTakeover() builds once both are present.
    ensureSearchTakeover();
}

void WesternComicsPage::setMangaDownloader(MangaDownloader* downloader)
{
    if (m_mangaDownloader == downloader) return;
    m_mangaDownloader = downloader;
    wireMangaDownloader();
}

void WesternComicsPage::setMangaDownloadIndex(MangaDownloadIndex* index)
{
    m_mangaDownloadIndex = index;
}

void WesternComicsPage::setNetworkManager(QNetworkAccessManager* nam)
{
    m_nam = nam;
    // NAM typically arrives AFTER setSourceRegistry (MainWindow injection order);
    // build the takeover now that both deps are present (no-op if already built).
    ensureSearchTakeover();
}

void WesternComicsPage::setTorrentClient(TorrentClient* client)
{
    if (m_torrentClient == client) return;
    m_torrentClient = client;
    if (!client) return;
    // WesternVolumeDownloader needs a live TorrentClient (magnet path) + the
    // shared QNAM (DDL path). Construct lazily once both arrive.
    if (!m_westernDownloader && m_nam) {
        m_westernDownloader = new tankoban::manga::WesternVolumeDownloader(
            m_nam, client, this);
        wireWesternDownloader();
    }
}

void WesternComicsPage::grabScrapersFromRegistry()
{
    if (!m_sourceRegistry) return;
    for (auto* s : m_sourceRegistry->scrapers()) {
        if (s->sourceId() == QLatin1String("readcomicsonline")) {
            m_readComicsScraper = qobject_cast<ReadComicsScraper*>(s);
        } else if (s->sourceId() == QLatin1String("readallcomics")) {
            m_readAllComicsScraper = qobject_cast<ReadAllComicsScraper*>(s);
        }
    }
    // RCO live-search pick -> render-only open (page-scrape -> schema-v2 JSON).
    if (m_readComicsScraper) {
        connect(m_readComicsScraper, &ReadComicsScraper::westernSeriesReady,
                this, [this](const QJsonObject& seriesJson) {
            setSearchBusy(false);
            const auto catalog =
                tankoban::manga::WesternCatalogLoader::loadFromJsonObject(seriesJson);
            if (catalog.seriesId.isEmpty()) {
                qInfo("WesternComicsPage: westernSeriesReady -> empty/invalid series, returning to grid");
                showLibraryMode();
                return;
            }
            m_pendingWesternJson     = seriesJson;
            m_pendingWesternSeriesId = catalog.seriesId;
            const bool onShelf = m_westernLibrary
                                 && m_westernLibrary->contains(catalog.seriesId);
            openWesternSeriesFromCatalog(catalog, QString(), onShelf);
        });
    }
}

// ---------------------------------------------------------------------------
// page shape
// ---------------------------------------------------------------------------

void WesternComicsPage::activate()
{
    // Landing on the Western library refreshes the My Library grid + the western
    // CONTINUE READING strip from the per-user store. The search bar already
    // targets readallcomics (set in grabScrapersFromRegistry-independent UI).
    showLibraryMode();
}

void WesternComicsPage::resetToRoot()
{
    showLibraryMode();
}

void WesternComicsPage::showLibraryMode()
{
    refreshWesternLibrary();
    refreshWesternContinueStrip_();
    if (m_stack) m_stack->setCurrentIndex(0);
}

// Inline alias kept private (the page's continue refresh). Declared in the
// header as refreshContinueStrip(); the western body lives here.
void WesternComicsPage::refreshContinueStrip()
{
    refreshWesternContinueStrip_();
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void WesternComicsPage::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new FadingStackedWidget(this);
    layout->addWidget(m_stack);

    // ── Western library grid (m_stack index 0), wrapped in a scroll area ──
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("WesternGridScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea#WesternGridScroll { background: transparent; border: none; }");

    auto* page = new QWidget();
    page->setObjectName(QStringLiteral("WesternGridPage"));
    page->setStyleSheet("QWidget#WesternGridPage { background: transparent; }");
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(20, 20, 20, 20);
    v->setSpacing(16);

    // Shared-recipe builder: full manga-parity search chrome (spinner + search
    // icon + history dropdown). Western bar searches LIVE readallcomics.
    {
        QWidget* westernSearchRow = buildSearchRow(
            m_westernSearchBar, m_westernSearchBusy, m_westernSearchBtn,
            QStringLiteral("Search Comics"),
            QStringLiteral("readallcomics"));
        v->addWidget(westernSearchRow);
    }

    // Stream/Theatre-parity search history: load persisted queries + build the
    // floating dropdown once + install the focus event filter.
    loadSearchHistory();
    buildSearchHistoryDropdown();
    if (m_westernSearchBar) m_westernSearchBar->installEventFilter(this);

    // Western CONTINUE READING strip (own; the manga strip excludes western
    // issues). Hidden until there's an in-progress western issue to show.
    m_westernContinueSection = new QWidget(page);
    {
        auto* wcLayout = new QVBoxLayout(m_westernContinueSection);
        wcLayout->setContentsMargins(0, 0, 0, 0);
        wcLayout->setSpacing(8);
        auto* wcLabel = new QLabel(tr("CONTINUE READING"), m_westernContinueSection);
        wcLabel->setStyleSheet(
            "color: rgba(255,255,255,0.6); font-size: 12px; font-weight: 600; letter-spacing: 1px;");
        wcLayout->addWidget(wcLabel);
        m_westernContinueStrip = new TileStrip(m_westernContinueSection);
        m_westernContinueStrip->setMode(QStringLiteral("continue"));
        wcLayout->addWidget(m_westernContinueStrip);
    }
    m_westernContinueSection->hide();
    v->addWidget(m_westernContinueSection);

    // Bare empty state shown when My Library is empty.
    m_westernEmptyLabel = new QLabel(tr("Search to find comics"), page);
    m_westernEmptyLabel->setAlignment(Qt::AlignCenter);
    m_westernEmptyLabel->setStyleSheet(
        "color: rgba(255,255,255,0.45); font-size: 14px; padding: 40px;");
    m_westernEmptyLabel->hide();
    v->addWidget(m_westernEmptyLabel);

    m_westernGrid = new TileStrip(page);
    m_westernGrid->setMode(QStringLiteral("fixedGrid"));
    const int westernDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_westernGrid->setDensity(qBound(0, westernDensity, 2));
    v->addWidget(m_westernGrid);
    v->addStretch();

    // Single-click opens the Western series (collected-edition detail view). My
    // Library tiles carry a curated json path when the series is one of the
    // shipped 14; otherwise fall back to the stored library record (live issues).
    connect(m_westernGrid, &TileStrip::tileSingleClicked, this, [this](TileCard* card) {
        if (!card) return;
        const QString jsonPath = card->property("westernJsonPath").toString();
        if (!jsonPath.isEmpty() && QFile::exists(jsonPath)) {
            openWesternSeriesFromJson(jsonPath);
            return;
        }
        const QString seriesId = card->property("westernSeriesId").toString();
        if (!seriesId.isEmpty()) openWesternSeriesFromLibrary(seriesId);
    });

    scroll->setWidget(page);
    m_gridScroll = scroll;
    m_stack->addWidget(scroll);
}

QWidget* WesternComicsPage::buildSearchRow(QLineEdit*& outBar,
                                           QWidget*&   outBusy,
                                           QPushButton*& outBtn,
                                           const QString& placeholder,
                                           const QString& sourceId)
{
    Q_UNUSED(sourceId);
    auto* container = new QWidget(this);
    auto* hbox = new QHBoxLayout(container);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(8);

    auto* bar = new QLineEdit(container);
    bar->setObjectName(QStringLiteral("LibrarySearch"));
    bar->setPlaceholderText(placeholder);
    bar->setClearButtonEnabled(true);
    bar->setFixedHeight(36);
    bar->setStyleSheet(
        "QLineEdit#LibrarySearch { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit#LibrarySearch:focus { border: 1px solid rgba(255,255,255,0.3); }");
    hbox->addWidget(bar, 1);

    auto* busyWidget = new QProgressBar(container);
    busyWidget->setRange(0, 0);
    busyWidget->setTextVisible(false);
    busyWidget->setFixedSize(16, 16);
    busyWidget->setObjectName(QStringLiteral("ComicsSearchBusy"));
    busyWidget->setStyleSheet(
        "#ComicsSearchBusy { background: transparent; border: none; }"
        "#ComicsSearchBusy::chunk { background: rgba(255,255,255,0.5); }");
    busyWidget->hide();
    hbox->addWidget(busyWidget);

    auto* btn = new QPushButton(container);
    btn->setFixedSize(36, 36);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName(QStringLiteral("ComicsSearchBtn"));
    btn->setIcon(QIcon(QStringLiteral(":/icons/search.svg")));
    btn->setIconSize(QSize(18, 18));
    btn->setToolTip(tr("Search"));
    hbox->addWidget(btn);

    outBar  = bar;
    outBusy = busyWidget;
    outBtn  = btn;

    auto submit = [this, bar]() {
        const QString q = bar->text().trimmed();
        if (q.isEmpty()) return;
        hideSearchHistoryDropdown();
        showSearchMode(q);
    };

    connect(btn, &QPushButton::clicked,    this, submit);
    connect(bar, &QLineEdit::returnPressed, this, submit);
    connect(bar, &QLineEdit::textChanged,   this, [this, bar]() {
        const bool hasText = !bar->text().trimmed().isEmpty();
        bar->setProperty("activeSearch", hasText);
        bar->style()->unpolish(bar);
        bar->style()->polish(bar);
        if (hasText) {
            hideSearchHistoryDropdown();
        } else if (bar->hasFocus()) {
            showSearchHistoryDropdown();
        }
    });

    bar->installEventFilter(this);
    return container;
}

// ---------------------------------------------------------------------------
// search history (Western-only; QSettings key kept VERBATIM)
// ---------------------------------------------------------------------------

void WesternComicsPage::loadSearchHistory()
{
    QSettings s;
    m_searchHistory =
        s.value(QStringLiteral("comics/westernSearchHistory")).toStringList();
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
}

void WesternComicsPage::saveSearchHistory()
{
    QSettings s;
    s.setValue(QStringLiteral("comics/westernSearchHistory"), m_searchHistory);
}

void WesternComicsPage::pushSearchHistory(const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return;
    m_searchHistory.removeAll(q);
    m_searchHistory.prepend(q);
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
    saveSearchHistory();
}

void WesternComicsPage::removeSearchHistoryEntry(const QString& query)
{
    m_searchHistory.removeAll(query);
    saveSearchHistory();
    if (m_searchHistoryDropdown && m_searchHistoryDropdown->isVisible()) {
        showSearchHistoryDropdown();
    }
}

void WesternComicsPage::clearSearchHistory()
{
    if (m_searchHistory.isEmpty()) return;
    m_searchHistory.clear();
    saveSearchHistory();
    hideSearchHistoryDropdown();
}

void WesternComicsPage::setSearchBusy(bool busy)
{
    if (!m_westernSearchBusy) return;
    m_westernSearchBusy->setVisible(busy);
}

void WesternComicsPage::buildSearchHistoryDropdown()
{
    m_searchHistoryDropdown = new QFrame(this);
    m_searchHistoryDropdown->setObjectName(QStringLiteral("ComicsSearchHistory"));
    m_searchHistoryDropdown->setStyleSheet(
        "QFrame#ComicsSearchHistory { background: #1a1a1a; border: 1px solid #3a3a3a;"
        "  border-radius: 6px; }");
    m_searchHistoryDropdown->hide();

    auto* outer = new QVBoxLayout(m_searchHistoryDropdown);
    outer->setContentsMargins(0, 4, 0, 4);
    outer->setSpacing(0);

    m_searchHistoryList = new QWidget(m_searchHistoryDropdown);
    auto* listLayout = new QVBoxLayout(m_searchHistoryList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    outer->addWidget(m_searchHistoryList);

    m_searchHistoryHideTimer = new QTimer(this);
    m_searchHistoryHideTimer->setSingleShot(true);
    m_searchHistoryHideTimer->setInterval(150);
    connect(m_searchHistoryHideTimer, &QTimer::timeout, this, [this]() {
        if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
    });
}

void WesternComicsPage::positionSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_westernSearchBar) return;
    QLineEdit* bar = m_westernSearchBar;
    const QPoint topLeft = bar->mapTo(this, QPoint(0, bar->height() + 2));
    m_searchHistoryDropdown->setGeometry(
        topLeft.x(), topLeft.y(), bar->width(),
        m_searchHistoryDropdown->sizeHint().height());
}

void WesternComicsPage::showSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchHistoryList) return;
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();

    auto* layout = qobject_cast<QVBoxLayout*>(m_searchHistoryList->layout());
    if (!layout) return;
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    const QStringList& hist = m_searchHistory;
    if (hist.isEmpty()) {
        m_searchHistoryDropdown->hide();
        return;
    }

    const int rows = qMin(hist.size(), kMaxSearchHistory);
    const char* kRowBtnStyle =
        "QPushButton { background: transparent; color: #d0d0d0; border: none;"
        "  text-align: left; padding: 6px 10px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); }";
    const char* kRemoveBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.45);"
        "  border: none; font-size: 14px; padding: 0 10px; }"
        "QPushButton:hover { color: #fff; }";
    const char* kClearAllBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.55);"
        "  border: none; text-align: left; padding: 6px 10px;"
        "  font-size: 11px; font-weight: 500; letter-spacing: 0.4px; }"
        "QPushButton:hover { color: #fff; }";

    for (int i = 0; i < rows; ++i) {
        const QString q = hist.at(i);

        auto* row = new QWidget(m_searchHistoryList);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto* queryBtn = new QPushButton(q, row);
        queryBtn->setCursor(Qt::PointingHandCursor);
        queryBtn->setStyleSheet(kRowBtnStyle);
        queryBtn->setFocusPolicy(Qt::NoFocus);
        connect(queryBtn, &QPushButton::clicked, this, [this, q]() {
            if (m_westernSearchBar) m_westernSearchBar->setText(q);
            showSearchMode(q);
            hideSearchHistoryDropdown();
        });
        rowLayout->addWidget(queryBtn, 1);

        auto* removeBtn = new QPushButton(QStringLiteral("×"), row);
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setStyleSheet(kRemoveBtnStyle);
        removeBtn->setFocusPolicy(Qt::NoFocus);
        removeBtn->setToolTip(tr("Remove from history"));
        connect(removeBtn, &QPushButton::clicked, this, [this, q]() {
            removeSearchHistoryEntry(q);
        });
        rowLayout->addWidget(removeBtn);

        layout->addWidget(row);
    }

    auto* divider = new QFrame(m_searchHistoryList);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        "QFrame { border: none; background: rgba(255,255,255,0.08);"
        "  max-height: 1px; min-height: 1px; }");
    layout->addWidget(divider);

    auto* clearAllBtn = new QPushButton(
        QStringLiteral("×  Clear search history"), m_searchHistoryList);
    clearAllBtn->setCursor(Qt::PointingHandCursor);
    clearAllBtn->setStyleSheet(kClearAllBtnStyle);
    clearAllBtn->setFocusPolicy(Qt::NoFocus);
    connect(clearAllBtn, &QPushButton::clicked,
            this, &WesternComicsPage::clearSearchHistory);
    layout->addWidget(clearAllBtn);

    m_searchHistoryDropdown->adjustSize();
    positionSearchHistoryDropdown();
    m_searchHistoryDropdown->show();
    m_searchHistoryDropdown->raise();
}

void WesternComicsPage::hideSearchHistoryDropdown()
{
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();
    if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
}

void WesternComicsPage::showSearchMode(const QString& query)
{
    // Canonical submit funnel (Enter / search-icon / history-row all route here),
    // so push-to-history runs exactly once per submitted query. Mirrors
    // MangaPage::showSearchMode (MangaPage.cpp:3884-3907).
    pushSearchHistory(query);
    hideSearchHistoryDropdown();
    setSearchBusy(true);

    // Emit before the in-page transition so the searchResults layer is recorded
    // in NavHistory (suppressed during Back/restore via m_inNavRestore).
    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("query")] = query;
        emit enteredLayer(makeWesternLayer(QStringLiteral("searchResults"),
                                           QStringLiteral("Search Results"), blob));
    }

    // FAITHFUL Western search (BUG 2 fix, 2026-06-14): drive the page's OWN
    // search-takeover — a RESULTS LIST where clicking a result opens that series
    // (resultPicked -> onSearchResultActivated). This restores MangaPage's
    // original flow (m_searchTakeover->search + setCurrentWidget). The previous
    // STEP-1 copy reimplemented this as a one-shot "open the first result", which
    // was both wrong behavior AND opened an empty series view — the smoke break.
    ensureSearchTakeover();
    if (m_searchTakeover) {
        m_searchTakeover->search(query);            // shows its own "Searching..." status
        m_stack->setCurrentWidget(m_searchTakeover);
        // The takeover surface owns the in-screen status now; drop the bar spinner.
        setSearchBusy(false);
    } else {
        // Registry/NAM not injected yet (should not happen post-STEP-2 wiring).
        setSearchBusy(false);
    }
}

// ---------------------------------------------------------------------------
// event handlers
// ---------------------------------------------------------------------------

void WesternComicsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_mangaDownloadIndex) m_mangaDownloadIndex->validateAll();
}

bool WesternComicsPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_westernSearchBar) {
        if (event->type() == QEvent::FocusIn) {
            if (m_westernSearchBar->text().trimmed().isEmpty())
                showSearchHistoryDropdown();
        } else if (event->type() == QEvent::FocusOut) {
            if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// My Library grid + disk reconcile
// ---------------------------------------------------------------------------

void WesternComicsPage::refreshWesternLibrary()
{
    if (!m_westernGrid) return;
    m_westernGrid->clear();

    const auto records = m_westernLibrary
        ? m_westernLibrary->all()
        : QList<tankoban::manga::WesternLibraryRecord>{};
    for (const auto& r : records) {
        auto* card = new TileCard(QString(), r.title, tr("Western"));
        const QString jsonPath =
            QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                .absoluteFilePath(r.seriesId + QStringLiteral(".json"));
        card->setProperty("westernJsonPath", jsonPath);
        card->setProperty("westernSeriesId", r.seriesId);
        card->setProperty("seriesName", r.title);
        m_westernGrid->addTile(card);
        if (!r.coverUrl.isEmpty()) fetchPosterForTile(card, r.coverUrl);
    }

    const bool empty = records.isEmpty();
    if (m_westernEmptyLabel) m_westernEmptyLabel->setVisible(empty);
    m_westernGrid->setVisible(!empty);
}

void WesternComicsPage::reconcileWesternLibraryFromDisk()
{
    if (!m_westernLibrary || !m_bridge) return;
    const QStringList roots = m_bridge->rootFolders(QStringLiteral("comics"));
    for (const QString& root : roots) {
        QDir rootDir(root);
        if (!rootDir.exists()) continue;
        const QStringList seriesFolders =
            rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& seriesFolder : seriesFolders) {
            QDir seriesDir(rootDir.absoluteFilePath(seriesFolder));
            const QStringList cbzs =
                seriesDir.entryList(QStringList() << QStringLiteral("*.cbz"), QDir::Files);
            bool hasWesternIssue = false;
            for (const QString& f : cbzs) {
                if (tankoban::manga::isWesternIssueCbz(QFileInfo(f).completeBaseName())) {
                    hasWesternIssue = true;
                    break;
                }
            }
            if (!hasWesternIssue) continue;

            QString slug = seriesFolder.toLower().trimmed();
            static const QRegularExpression kNonSlug(QStringLiteral("[^a-z0-9]+"));
            static const QRegularExpression kEdgeDash(QStringLiteral("^-+|-+$"));
            slug.replace(kNonSlug, QStringLiteral("-"));
            slug.replace(kEdgeDash, QString());
            if (slug.isEmpty() || m_westernLibrary->contains(slug)) continue;

            tankoban::manga::WesternLibraryRecord r;
            r.seriesId = slug;
            r.title    = seriesFolder;
            const QString curatedPath =
                QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                    .absoluteFilePath(slug + QStringLiteral(".json"));
            if (QFile::exists(curatedPath)) {
                if (const auto cat = tankoban::manga::WesternCatalogLoader::loadFromFile(curatedPath))
                    r.coverUrl = cat->seriesCover;
            }
            r.addedAt = QDateTime::currentMSecsSinceEpoch();
            m_westernLibrary->addOrUpdate(r);
        }
    }
}

void WesternComicsPage::openWesternSeriesFromLibrary(const QString& seriesId)
{
    if (!m_westernLibrary || !m_seriesView) return;
    const auto recOpt = m_westernLibrary->get(seriesId);
    if (!recOpt) return;
    tankoban::manga::MangaCatalog cat;
    cat.seriesId    = recOpt->seriesId;
    cat.seriesTitle = recOpt->title;
    cat.seriesCover = recOpt->coverUrl;
    cat.source      = QStringLiteral("rco");
    m_pendingWesternJson     = {};
    m_pendingWesternSeriesId = recOpt->seriesId;
    openWesternSeriesFromCatalog(cat, QString(), /*onShelf*/true);
}

// ---------------------------------------------------------------------------
// Western CONTINUE READING
// ---------------------------------------------------------------------------

void WesternComicsPage::ensureWesternIssueInMap(const QString& cbzPath)
{
    if (cbzPath.isEmpty()) return;
    if (!tankoban::manga::isWesternIssueCbz(QFileInfo(cbzPath).completeBaseName())) return;
    const QString key = comicProgressKeyForPath(cbzPath);
    if (m_westernProgressKeyMap.contains(key)) return;
    const QString parentDir = QFileInfo(cbzPath).absolutePath();
    QString coverUrl = m_currentWesternSeriesCover;
    if (coverUrl.isEmpty() && m_westernLibrary && !m_pendingWesternSeriesId.isEmpty()) {
        if (const auto r = m_westernLibrary->get(m_pendingWesternSeriesId))
            coverUrl = r->coverUrl;
    }
    m_westernProgressKeyMap[key] = { cbzPath, parentDir, coverUrl };
}

void WesternComicsPage::refreshWesternContinueStrip_()
{
    if (!m_westernContinueStrip) return;
    m_westernContinueStrip->clear();

    const QJsonObject allProg = m_bridge->allProgress(QStringLiteral("comics"));
    struct WItem { qint64 updatedAt; QString filePath, seriesPath, title, subtitle, coverUrl; };
    QList<WItem> items;
    for (auto it = allProg.begin(); it != allProg.end(); ++it) {
        const QJsonObject prog = it.value().toObject();
        if (prog.value("finished").toBool()) continue;
        const int page = prog.value("page").toInt(0);
        if (page < 0) continue;
        const auto ref = m_westernProgressKeyMap.find(it.key());
        if (ref == m_westernProgressKeyMap.end()) continue;
        const QString base = QFileInfo(ref->filePath).completeBaseName();
        if (!tankoban::manga::isWesternIssueCbz(base)) continue;
        const int issueNo = tankoban::manga::westernIssueNumber(base);
        const int pageCount = prog.value("pageCount").toInt(0);
        const QString seriesName = QDir(ref->seriesPath).dirName();
        const QString pageLabel = pageCount > 0
            ? QStringLiteral("Page %1/%2").arg(page + 1).arg(pageCount)
            : QStringLiteral("Page %1").arg(page + 1);
        items.append({ prog.value("updatedAt").toVariant().toLongLong(),
                       ref->filePath, ref->seriesPath, seriesName,
                       QStringLiteral("Issue %1 · %2").arg(issueNo).arg(pageLabel),
                       ref->coverPath });
    }
    if (items.isEmpty()) { m_westernContinueSection->hide(); return; }

    QMap<QString, int> bestPerSeries;
    for (int i = 0; i < items.size(); ++i) {
        auto it = bestPerSeries.find(items[i].seriesPath);
        if (it == bestPerSeries.end() || items[i].updatedAt > items[it.value()].updatedAt)
            bestPerSeries[items[i].seriesPath] = i;
    }
    QList<WItem> deduped;
    for (int idx : bestPerSeries) deduped.append(items[idx]);
    std::sort(deduped.begin(), deduped.end(),
              [](const WItem& a, const WItem& b){ return a.updatedAt > b.updatedAt; });
    if (deduped.size() > 40) deduped = deduped.mid(0, 40);

    for (const auto& w : deduped) {
        auto* card = new TileCard(QString(), w.title, w.subtitle);
        card->setProperty("filePath", w.filePath);
        card->setProperty("seriesPath", w.seriesPath);
        card->setProperty("seriesName", w.title);
        connect(card, &TileCard::clicked, this, [this, card]() {
            const QString path = card->property("filePath").toString();
            const QString seriesPath = card->property("seriesPath").toString();
            const QString seriesName = card->property("seriesName").toString();
            QDir dir(seriesPath);
            QStringList files = dir.entryList(COMIC_EXTS, QDir::Files);
            QCollator col; col.setNumericMode(true);
            std::sort(files.begin(), files.end(),
                      [&col](const QString& a, const QString& b){ return col.compare(a, b) < 0; });
            QStringList cbzList;
            for (const auto& f : files) cbzList.append(dir.absoluteFilePath(f));
            emit openComic(path, cbzList, seriesName);
        });
        m_westernContinueStrip->addTile(card);
        if (!w.coverUrl.isEmpty()) fetchPosterForTile(card, w.coverUrl);
    }
    m_westernContinueSection->show();
}

// ---------------------------------------------------------------------------
// open a series (render-only; issue-based)
// ---------------------------------------------------------------------------

void WesternComicsPage::openWesternSeriesFromJson(const QString& jsonPath)
{
    if (jsonPath.isEmpty() || !m_seriesView) return;
    const auto catalog = tankoban::manga::WesternCatalogLoader::loadFromFile(jsonPath);
    if (!catalog.has_value()) {
        qInfo("WesternComicsPage::openWesternSeriesFromJson: loadFromFile failed for %s",
              qUtf8Printable(jsonPath));
        return;
    }
    openWesternSeriesFromCatalog(*catalog, jsonPath, /*onShelf*/true);
}

void WesternComicsPage::openWesternSeriesFromCatalog(const tankoban::manga::MangaCatalog& catalog,
                                                     const QString& jsonPath,
                                                     bool onShelf)
{
    if (!m_seriesView) return;

    // Curated enrichment: when this series is one of the shipped 14, merge its
    // richer cover/synopsis over the live metadata. For a baked-json open the
    // catalog already IS the curated data — skip the reload.
    tankoban::manga::MangaCatalog enriched = catalog;
    if (jsonPath.isEmpty()) {
        const QString curatedPath =
            QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                .absoluteFilePath(enriched.seriesId + QStringLiteral(".json"));
        if (QFile::exists(curatedPath)) {
            if (const auto curated =
                    tankoban::manga::WesternCatalogLoader::loadFromFile(curatedPath)) {
                if (!curated->seriesCover.isEmpty())
                    enriched.seriesCover = curated->seriesCover;
                if (!curated->seriesSynopsis.isEmpty())
                    enriched.seriesSynopsis = curated->seriesSynopsis;
            }
        }
    }

    m_pendingWesternSeriesId    = enriched.seriesId;
    m_currentWesternSeriesCover = enriched.seriesCover;
    if (!jsonPath.isEmpty()) {
        QFile jf(jsonPath);
        if (jf.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(jf.readAll());
            if (doc.isObject()) m_pendingWesternJson = doc.object();
        }
    }

    if (!m_inNavRestore) {
        QJsonObject blob;
        blob[QStringLiteral("seriesId")]    = catalog.seriesId;
        blob[QStringLiteral("seriesTitle")] = catalog.seriesTitle;
        blob[QStringLiteral("enteredFrom")] = QStringLiteral("western");
        blob[QStringLiteral("jsonPath")]    = jsonPath;
        emit enteredLayer(makeWesternLayer(QStringLiteral("seriesView"),
                                           catalog.seriesTitle, blob));
    }
    m_currentDetailSeriesTitle = catalog.seriesTitle;

    // GUARD: render DIRECTLY. Do NOT route through showSeries()/dispatchCatalog-
    // Resolve() — those auto-fire AniList + mangafire enrichment that would
    // corrupt a Western comic. populateVolumeRowsFromCatalog is pure-render.
    tankoban::manga::MangaCatalog headerOnly = enriched;
    headerOnly.volumes.clear();
    m_seriesView->setWesternIssuesLoading(true);
    m_seriesView->populateVolumeRowsFromCatalog(headerOnly);
    m_seriesView->setWesternOnShelf(onShelf);
    m_stack->setCurrentWidget(m_seriesView);
    fetchAndRenderWesternIssues(enriched, onShelf);
}

void WesternComicsPage::fetchAndRenderWesternIssues(const tankoban::manga::MangaCatalog& seriesMeta,
                                                    bool onShelf)
{
    // SIX_MODE_RESTRUCTURE Arc 1 smoke fix (2026-06-14, Agent 1) — DECISIVE trace
    // at entry so the next re-smoke shows exactly whether this path runs and
    // whether the page's RCO scraper is live (the prime "empty series view"
    // suspect was an unwired scraper; this proves it either way).
    comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues ENTRY id=%1 title=\"%2\" scraper=%3 seriesView=%4")
                        .arg(seriesMeta.seriesId)
                        .arg(seriesMeta.seriesTitle)
                        .arg(m_readAllComicsScraper ? 1 : 0)
                        .arg(m_seriesView ? 1 : 0));
    if (!m_readAllComicsScraper || !m_seriesView) {
        comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues EARLY-RETURN (null scraper/seriesView)"));
        return;
    }

    const QString seriesTitle = seriesMeta.seriesTitle;
    const QString guardId     = seriesMeta.seriesId;
    auto* scraper = m_readAllComicsScraper;

    auto searchConn = std::make_shared<QMetaObject::Connection>();
    auto chapConn   = std::make_shared<QMetaObject::Connection>();
    auto errConn    = std::make_shared<QMetaObject::Connection>();
    auto cleanup = [searchConn, chapConn, errConn]() {
        QObject::disconnect(*searchConn);
        QObject::disconnect(*chapConn);
        QObject::disconnect(*errConn);
    };
    auto fail = [this, guardId](const QString& why) {
        comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues EMPTY-STATE id=%1 why=%2")
                            .arg(guardId).arg(why));
        qInfo("WesternComicsPage: western issue-list fetch failed - %s", qUtf8Printable(why));
        if (m_seriesView && m_pendingWesternSeriesId == guardId) {
            // setWesternIssuesLoading(false) flips the in-view empty label out of
            // "Loading issues…" to "No issues found yet." — a COMMUNICATED empty
            // state, never a silent blank loading screen (PROBLEM B).
            m_seriesView->setWesternIssuesLoading(false);
            m_seriesView->updateWesternDownloadStatus(
                QString(), tr("No readable issues found for this title on ReadAllComics."));
        }
    };

    // Render the resolved issue list into the page's own series view (shared by
    // the slug fast-path below and the search-then-fetch fallback).
    auto renderIssues = [this, seriesMeta, guardId, onShelf](const QList<ChapterInfo>& chapters) {
        if (m_pendingWesternSeriesId != guardId) return;   // navigated away
        comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues chaptersReady id=%1 count=%2")
                            .arg(guardId).arg(chapters.size()));
        if (chapters.isEmpty()) return;   // caller's fail() handles empties

        QList<ChapterInfo> sorted = chapters;
        std::sort(sorted.begin(), sorted.end(),
            [](const ChapterInfo& a, const ChapterInfo& b) {
                return a.chapterNumber < b.chapterNumber;
            });
        tankoban::manga::MangaCatalog issueCat = seriesMeta;
        issueCat.volumes.clear();
        issueCat.volumes.reserve(sorted.size());
        for (const auto& ch : sorted) {
            tankoban::manga::MangaVolume vol;
            vol.volumeNumber     = qRound(ch.chapterNumber);
            vol.groupingLabel    = QStringLiteral("Issue");
            vol.coverUrlJapanese = seriesMeta.seriesCover;
            issueCat.volumes.append(std::move(vol));
        }
        if (m_seriesView) {
            m_seriesView->populateVolumeRowsFromCatalog(issueCat);
            m_seriesView->setWesternOnShelf(onShelf);
        }
    };

    // SIX_MODE_RESTRUCTURE Arc 1 smoke fix #2 (2026-06-14, Agent 1) — SEARCH-THEN-
    // FETCH FALLBACK. This is the path the ORIGINAL MangaPage always used: search
    // the TITLE, pick the best title-matching result, and fetchChapters() the
    // result's REAL readallcomics slug (e.g. "invincible-image-comics"), NOT a
    // slug derived from the curated/baked seriesId. It is the recovery for the
    // slug fast-path below: a curated id (e.g. "invincible") only coincidentally
    // equals the readallcomics category slug, so category/<curated-id>/ 404s for
    // some titles (Invincible). The title-match guard prevents landing on a wrong
    // sibling slug (e.g. "Saga" vs "Saga of the Swamp Thing"). Terminal on its own
    // failure (shows the COMMUNICATED empty state).
    auto runSearchFallback = [this, scraper, searchConn, chapConn, errConn,
                              cleanup, fail, renderIssues, seriesMeta, seriesTitle, guardId]() {
        if (m_pendingWesternSeriesId != guardId) { cleanup(); return; }   // navigated away
        comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues FALLBACK search title=\"%1\"")
                            .arg(seriesTitle));
        // Re-arm error handling for the fallback's two hops (terminal now).
        *errConn = connect(scraper, &MangaScraper::errorOccurred, this,
            [cleanup, fail](const QString& msg) { cleanup(); fail(msg); });
        *searchConn = connect(scraper, &MangaScraper::searchFinished, this,
            [this, scraper, chapConn, cleanup, fail, renderIssues, seriesMeta, guardId]
            (const QList<MangaResult>& results) {
                if (m_pendingWesternSeriesId != guardId) { cleanup(); return; }
                if (results.isEmpty()) { cleanup(); fail(QStringLiteral("no series match")); return; }

                const QString want = normalizeWesternTitle(seriesMeta.seriesTitle);
                QString slug = results.first().id;
                int bestScore = -1;
                for (const auto& r : results) {
                    const QString got = normalizeWesternTitle(r.title);
                    const int score = (got == want) ? 2
                                    : (got.startsWith(want) || want.startsWith(got)) ? 1 : 0;
                    if (score > bestScore) { bestScore = score; slug = r.id; }
                }
                comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues FALLBACK fetchChapters slug=%1").arg(slug));

                *chapConn = connect(scraper, &MangaScraper::chaptersReady, this,
                    [cleanup, fail, renderIssues](const QList<ChapterInfo>& chapters) {
                        cleanup();
                        if (chapters.isEmpty()) { fail(QStringLiteral("no issues listed")); return; }
                        renderIssues(chapters);
                    });
                scraper->fetchChapters(slug);
            });
        scraper->search(seriesTitle, 60);
    };

    // SIX_MODE_RESTRUCTURE Arc 1 smoke fix (2026-06-14, Agent 1) — SLUG FAST-PATH.
    // A live readallcomics search pick already carries the EXACT series slug in
    // result.id (== seriesMeta.seriesId here), so fetchChapters() can skip the
    // redundant title re-search (a second ~1.5s network hop). When the id is NOT a
    // real readallcomics slug (curated/baked opens such as Invincible, whose
    // category/<id>/ 404s), the fast-path's error/empty handlers FALL BACK to the
    // search-then-fetch path above rather than dead-ending — so a recoverable title
    // still loads. Saga/Walking-Dead keep the fast path (their slug resolves).
    static const QRegularExpression kSlugRe(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    if (!guardId.isEmpty() && kSlugRe.match(guardId).hasMatch()) {
        // Fast-path error -> fall back to search-then-fetch (e.g. category/<slug>/
        // 404). cleanup() first so the fallback re-arms cleanly.
        *errConn = connect(scraper, &MangaScraper::errorOccurred, this,
            [this, guardId, cleanup, runSearchFallback](const QString& msg) {
                cleanup();
                comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues FASTPATH-FAIL id=%1 err=%2 -> fallback")
                                    .arg(guardId).arg(msg));
                runSearchFallback();
            });
        *chapConn = connect(scraper, &MangaScraper::chaptersReady, this,
            [this, guardId, cleanup, renderIssues, runSearchFallback](const QList<ChapterInfo>& chapters) {
                cleanup();
                if (chapters.isEmpty()) {
                    comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues FASTPATH-EMPTY id=%1 -> fallback")
                                        .arg(guardId));
                    runSearchFallback();
                    return;
                }
                renderIssues(chapters);
            });
        comicsOpenTrace(QStringLiteral("WCP::fetchAndRenderWesternIssues FASTPATH fetchChapters slug=%1").arg(guardId));
        scraper->fetchChapters(guardId);
        return;
    }

    // No usable slug — go straight to the faithful search-then-fetch path.
    runSearchFallback();
}

// ---------------------------------------------------------------------------
// download (readallcomics page -> cbz pipeline through the SHARED downloader)
// ---------------------------------------------------------------------------

void WesternComicsPage::startWesternIssueDownload(const QString& seriesTitle, double issueNumber,
                                                  const QString& editionTitle, int volumeNumber,
                                                  const QString& destPath)
{
    if (!m_readAllComicsScraper || !m_mangaDownloader) {
        if (m_seriesView)
            m_seriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
        return;
    }

    m_westernDownloadEdition       = editionTitle;
    m_pendingWesternDownloadVolume = volumeNumber;
    if (m_seriesView)
        m_seriesView->updateWesternDownloadStatus(editionTitle, tr("Finding source..."));

    auto* scraper = m_readAllComicsScraper;

    auto searchConn = std::make_shared<QMetaObject::Connection>();
    auto chapConn   = std::make_shared<QMetaObject::Connection>();
    auto errConn    = std::make_shared<QMetaObject::Connection>();
    auto cleanup = [searchConn, chapConn, errConn]() {
        QObject::disconnect(*searchConn);
        QObject::disconnect(*chapConn);
        QObject::disconnect(*errConn);
    };
    auto fail = [this](const QString& why) {
        qInfo("WesternComicsPage: Western readallcomics resolve failed — %s", qUtf8Printable(why));
        if (m_seriesView)
            m_seriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
    };

    *errConn = connect(scraper, &MangaScraper::errorOccurred, this,
        [cleanup, fail](const QString& msg) { cleanup(); fail(msg); });

    *searchConn = connect(scraper, &MangaScraper::searchFinished, this,
        [this, scraper, chapConn, cleanup, fail, seriesTitle, issueNumber, editionTitle, destPath]
        (const QList<MangaResult>& results) {
            if (results.isEmpty()) { cleanup(); fail("no series match"); return; }

            const QString want = normalizeWesternTitle(seriesTitle);
            QString slug = results.first().id;
            int bestScore = -1;
            for (const auto& r : results) {
                const QString got = normalizeWesternTitle(r.title);
                const int score = (got == want) ? 2
                                : (got.startsWith(want) || want.startsWith(got)) ? 1 : 0;
                if (score > bestScore) { bestScore = score; slug = r.id; }
            }

            *chapConn = connect(scraper, &MangaScraper::chaptersReady, this,
                [this, cleanup, fail, issueNumber, editionTitle, seriesTitle, destPath]
                (const QList<ChapterInfo>& chapters) {
                    cleanup();
                    ChapterInfo match;
                    bool found = false;
                    for (const auto& c : chapters) {
                        if (qAbs(c.chapterNumber - issueNumber) < 0.001) {
                            match = c; found = true; break;
                        }
                    }
                    if (!found) {
                        fail(QStringLiteral("issue %1 not found on readallcomics").arg(issueNumber));
                        return;
                    }

                    ChapterInfo ch;
                    ch.id     = match.id;
                    ch.name   = editionTitle.isEmpty()
                        ? QStringLiteral("%1 #%2").arg(seriesTitle,
                              QString::number(issueNumber, 'g', 12))
                        : editionTitle;
                    ch.source = QStringLiteral("readallcomics");
                    if (m_seriesView)
                        m_seriesView->updateWesternDownloadStatus(
                            editionTitle, tr("Downloading..."));
                    m_westernDownloadRecordId = m_mangaDownloader->startDownload(
                        seriesTitle, QStringLiteral("readallcomics"),
                        { ch }, destPath, QStringLiteral("cbz"));
                });
            scraper->fetchChapters(slug);
        });

    scraper->search(seriesTitle, 60);
}

void WesternComicsPage::updateWesternMangaStatus(const QString& recordId)
{
    if (recordId.isEmpty() || recordId != m_westernDownloadRecordId) return;
    if (!m_mangaDownloader || !m_seriesView) return;

    for (const MangaDownloadRecord& rec : m_mangaDownloader->listActive()) {
        if (rec.id != recordId) continue;
        const ChapterDownload* ch = rec.chapters.isEmpty() ? nullptr : &rec.chapters.first();
        const bool errored = rec.status == QLatin1String("error")
                          || (ch && ch->status == QLatin1String("error"));
        if (errored) {
            m_seriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
            if (m_pendingWesternDownloadVolume > 0)
                m_seriesView->setVolumeStatusText(m_pendingWesternDownloadVolume, tr("Failed"));
            return;
        }
        int pct = 0;
        if (ch && ch->totalImages > 0)
            pct = (ch->downloadedImages * 100) / ch->totalImages;
        const QString panelLine = pct <= 0 ? tr("Finding...") : tr("Downloading %1%").arg(pct);
        m_seriesView->updateWesternDownloadStatus(m_westernDownloadEdition, panelLine);
        if (m_pendingWesternDownloadVolume > 0)
            m_seriesView->setVolumeStatusText(m_pendingWesternDownloadVolume,
                pct <= 0 ? QStringLiteral("...") : QStringLiteral("%1%").arg(pct));
        return;
    }
}

void WesternComicsPage::onWesternChapterCompleted(const QString& source,
                                                  const QString& seriesTitle,
                                                  const QString& finalPath)
{
    if (source != QLatin1String("readallcomics")) return;
    if (m_pendingWesternSeriesId.isEmpty() || m_pendingWesternDownloadVolume <= 0) return;
    if (seriesTitle != m_currentDetailSeriesTitle || finalPath.isEmpty()) return;
    onProviderVolumeCompleted(m_pendingWesternSeriesId, m_pendingWesternDownloadVolume,
        finalPath, static_cast<int>(PendingVolumeSourceKind::WesternGetComics));
    if (m_seriesView)
        m_seriesView->updateWesternDownloadStatus(
            m_westernDownloadEdition, tr("Downloaded - open to read"));
}

// ---------------------------------------------------------------------------
// provider completion / failure (Western-only copies)
// ---------------------------------------------------------------------------

void WesternComicsPage::onProviderVolumeCompleted(const QString& seriesId,
                                                  int volumeNumber,
                                                  const QString& cbzPath,
                                                  int fallbackSourceKind)
{
    Q_UNUSED(fallbackSourceKind);
    const QString sourceId = QString::fromLatin1(GETCOMICS_SOURCE_ID);

    if (m_mangaDownloadIndex) {
        m_mangaDownloadIndex->registerVolume(sourceId, seriesId, volumeNumber, cbzPath,
                                             QFileInfo(cbzPath).size(), QStringList{});
    }

    // download-implies-library (manga parity reflex): a finished western issue
    // adds its series to My Library; addOrUpdate fires libraryChanged ->
    // refreshWesternLibrary surfaces the tile.
    if (m_westernLibrary && !m_pendingWesternSeriesId.isEmpty()) {
        tankoban::manga::WesternLibraryRecord r;
        r.seriesId = m_pendingWesternSeriesId;
        r.title    = m_currentDetailSeriesTitle.isEmpty()
                       ? m_pendingWesternSeriesId : m_currentDetailSeriesTitle;
        r.coverUrl = m_currentWesternSeriesCover;
        r.addedAt  = QDateTime::currentMSecsSinceEpoch();
        m_westernLibrary->addOrUpdate(r);
    }

    const bool currentWesternVolume =
        !m_pendingWesternSeriesId.isEmpty() && seriesId == m_pendingWesternSeriesId;
    if (m_seriesView && currentWesternVolume) {
        m_seriesView->setVolumeDownloadState(volumeNumber, cbzPath, true);
    }
}

void WesternComicsPage::onProviderVolumeFailed(const QString& seriesId,
                                               int volumeNumber,
                                               const QString& errorCode,
                                               const QString& errorMessage,
                                               int fallbackSourceKind)
{
    Q_UNUSED(fallbackSourceKind);
    const bool currentWesternVolume =
        !m_pendingWesternSeriesId.isEmpty() && seriesId == m_pendingWesternSeriesId;
    if (m_seriesView && currentWesternVolume) {
        m_seriesView->setVolumeStatusText(volumeNumber, QStringLiteral("Failed"));
    }
    qDebug().noquote()
        << "[WesternComicsPage volumeFailed]"
        << seriesId
        << QStringLiteral("v%1").arg(volumeNumber)
        << errorCode
        << errorMessage;
}

// ---------------------------------------------------------------------------
// search-result open (Western: readallcomics / readcomicsonline only)
// ---------------------------------------------------------------------------

void WesternComicsPage::onSearchResultActivated(const MangaResult& result)
{
    comicsOpenTrace(QStringLiteral("WCP::onSearchResultActivated ENTRY source=%1 id=%2 title=\"%3\"")
                        .arg(result.source).arg(result.id).arg(result.title));
    m_currentDetailSeriesTitle = result.title;

    // Live readallcomics search pick: open the western series view with live
    // issues. No AniList enrichment.
    if (result.source == QLatin1String("readallcomics") && m_seriesView) {
        tankoban::manga::MangaCatalog cat;
        cat.seriesId    = result.id;
        cat.seriesTitle = result.title;
        cat.seriesCover = result.thumbnailUrl;
        cat.source      = QStringLiteral("rco");
        m_stack->setCurrentWidget(m_seriesView);
        m_seriesView->showSearchResultLoading();
        m_pendingWesternSeriesId = result.id;
        openWesternSeriesFromCatalog(
            cat, QString(),
            m_westernLibrary && m_westernLibrary->contains(result.id));
        setSearchBusy(false);
        return;
    }

    if (result.source == QLatin1String("readcomicsonline") && m_readComicsScraper
        && m_seriesView) {
        m_stack->setCurrentWidget(m_seriesView);
        m_seriesView->showSearchResultLoading();
        setSearchBusy(true);
        m_readComicsScraper->fetchWesternSeries(result.id, result.title,
                                                result.thumbnailUrl);
        return;
    }
}

// ---------------------------------------------------------------------------
// nav restore (Western kinds: library / searchResults / seriesView)
// ---------------------------------------------------------------------------

void WesternComicsPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    QScopedValueRollback<bool> rollback(m_inNavRestore, true);
    const QString kind     = target.kind;
    const QJsonObject blob = target.stateBlob;

    if (kind == QStringLiteral("library")) {
        showLibraryMode();
        return;
    }

    if (kind == QStringLiteral("searchResults")) {
        const QString q = blob.value(QStringLiteral("query")).toString();
        if (m_westernSearchBar) m_westernSearchBar->setText(q);
        showSearchMode(q);
        return;
    }

    if (kind == QStringLiteral("seriesView") && m_seriesView) {
        QString jsonPath = blob.value(QStringLiteral("jsonPath")).toString();
        if (jsonPath.isEmpty()) {
            const QString sid = blob.value(QStringLiteral("seriesId")).toString();
            if (!sid.isEmpty())
                jsonPath = QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                               .absoluteFilePath(sid + QStringLiteral(".json"));
        }
        if (!jsonPath.isEmpty() && QFile::exists(jsonPath)) {
            openWesternSeriesFromJson(jsonPath);
            return;
        }
        // No baked json: re-open from the stored library record (live issues).
        const QString sid = blob.value(QStringLiteral("seriesId")).toString();
        if (!sid.isEmpty()) openWesternSeriesFromLibrary(sid);
        return;
    }
}

// ---------------------------------------------------------------------------
// poster fetch (async; mirrors MangaPage::fetchPosterForTile, URL-only path)
// ---------------------------------------------------------------------------

void WesternComicsPage::fetchPosterForTile(TileCard* card, const QString& coverUrl)
{
    if (!card || coverUrl.isEmpty() || !m_nam) return;
    const QString cacheDir = m_bridge->dataDir() + QStringLiteral("/anilist_posters");
    QDir().mkpath(cacheDir);
    const QString fileName = QString(
        QCryptographicHash::hash(coverUrl.toUtf8(), QCryptographicHash::Sha1)
            .toHex().left(20)) + QStringLiteral(".jpg");
    const QString destPath = QDir(cacheDir).absoluteFilePath(fileName);
    if (QFile::exists(destPath)) {
        card->setThumbPath(destPath);
        return;
    }
    QNetworkRequest req{QUrl(coverUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    QPointer<TileCard> guard(card);
    connect(reply, &QNetworkReply::finished, this, [reply, guard, destPath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) return;
        QFile f(destPath);
        if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); }
        if (guard) guard->setThumbPath(destPath);
    });
}

// ---------------------------------------------------------------------------
// signal wiring (page-owned series view + injected downloader)
// ---------------------------------------------------------------------------

void WesternComicsPage::ensureSearchTakeover()
{
    // Build once, and only once BOTH the registry (drives scraper search) and the
    // NAM (poster thumbnails) have been injected. Mirrors MangaPage's
    // m_searchTakeover construction (MainWindow built the registry/NAM before
    // buildUI there; here they arrive via setters, so we build lazily).
    if (m_searchTakeover || !m_sourceRegistry || !m_nam) return;

    m_searchTakeover = new ComicsTankoyomiSearchWidget(m_sourceRegistry, m_nam, this);
    // Western lane: the takeover searches LIVE readallcomics (find any western
    // comic by name). Results carry source=="readallcomics", routed into the
    // page's own series view by onSearchResultActivated.
    m_searchTakeover->setActiveSourceId(QStringLiteral("readallcomics"));
    m_stack->addWidget(m_searchTakeover);

    // Back out of the results surface -> Western library (defensive reset of the
    // busy spinner + any lingering history dropdown; mirrors MangaPage:253-263).
    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::backRequested,
            this, [this]() {
        setSearchBusy(false);
        hideSearchHistoryDropdown();
        if (!m_inNavRestore) emit exitedLayer();
        showLibraryMode();
    });
    // Pick a result -> open THAT series (the faithful flow; MangaPage:264-265).
    connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::resultPicked,
            this, &WesternComicsPage::onSearchResultActivated);
}

void WesternComicsPage::wireSeriesView()
{
    if (m_seriesViewWired || !m_seriesView) return;
    m_seriesViewWired = true;

    connect(m_seriesView,
            &tankoban::manga::comics::ComicsSeriesView::openVolume,
            this, [this](int volumeNumber, const QString& cbzPath) {
        Q_UNUSED(volumeNumber);
        if (cbzPath.isEmpty()) return;
        // Open the issue in the reader; register its progress key first so the
        // Western CONTINUE strip can resolve it on the next refresh.
        ensureWesternIssueInMap(cbzPath);
        const QString seriesPath = QFileInfo(cbzPath).absolutePath();
        QDir dir(seriesPath);
        QStringList files = dir.entryList(COMIC_EXTS, QDir::Files);
        QCollator col; col.setNumericMode(true);
        std::sort(files.begin(), files.end(),
                  [&col](const QString& a, const QString& b){ return col.compare(a, b) < 0; });
        QStringList cbzList;
        for (const auto& f : files) cbzList.append(dir.absoluteFilePath(f));
        emit openComic(cbzPath, cbzList, QDir(seriesPath).dirName());
    });

    connect(m_seriesView,
            &tankoban::manga::comics::ComicsSeriesView::backRequested,
            this, [this]() {
        if (!m_inNavRestore) emit exitedLayer();
        showLibraryMode();
        if (m_seriesView) m_seriesView->clearView();
        m_currentDetailSeriesTitle.clear();
    });

    // +Add persist: write the per-user WesternLibrary store (NOT a baked json).
    connect(m_seriesView,
            &tankoban::manga::comics::ComicsSeriesView::addWesternToLibraryRequested,
            this, [this]() {
        if (m_pendingWesternSeriesId.isEmpty() || !m_westernLibrary) {
            qInfo("WesternComicsPage: addWesternToLibraryRequested with no pending series");
            return;
        }
        const QString& id = m_pendingWesternSeriesId;
        static const QRegularExpression safeIdRe(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
        if (!safeIdRe.match(id).hasMatch()) {
            qInfo("WesternComicsPage: unsafe Western seriesId '%s', refusing to add",
                  qUtf8Printable(id));
            return;
        }
        tankoban::manga::WesternLibraryRecord r;
        r.seriesId = id;
        r.title    = m_currentDetailSeriesTitle.isEmpty() ? id : m_currentDetailSeriesTitle;
        r.coverUrl = m_currentWesternSeriesCover;
        r.addedAt  = QDateTime::currentMSecsSinceEpoch();
        m_westernLibrary->addOrUpdate(r);
        if (m_seriesView) m_seriesView->setWesternOnShelf(true);
    });

    // Download trigger: the series view emits a clicked issue row. The clicked
    // row IS a readallcomics issue, so the download runs through the
    // readallcomics page->cbz path (startWesternIssueDownload).
    connect(m_seriesView,
            &tankoban::manga::comics::ComicsSeriesView::downloadWesternEditionRequested,
            this,
            [this](int volumeNumber, const QString& editionTitle,
                   const QString& tierLabel, const QString& /*sourceHref*/) {
        Q_UNUSED(tierLabel);
        if (!m_readAllComicsScraper || m_pendingWesternSeriesId.isEmpty()) {
            qInfo("WesternComicsPage: Western download ignored - no readallcomics scraper/series");
            if (m_seriesView)
                m_seriesView->updateWesternDownloadStatus(QString(), tr("No download found"));
            return;
        }

        const QString seriesId    = m_pendingWesternSeriesId;
        const QString seriesTitle = m_currentDetailSeriesTitle;

        QString comicsRoot;
        if (m_torrentClient) {
            comicsRoot = m_torrentClient->defaultPaths().value(QStringLiteral("comics"));
        }
        if (comicsRoot.isEmpty()) {
            comicsRoot = QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
                             .absoluteFilePath(QStringLiteral("../western_downloads"));
        }

        QString safeTitle = seriesTitle;
        static const QRegularExpression kUnsafeDirChars(QStringLiteral("[\\\\/:*?\"<>|]"));
        safeTitle.replace(kUnsafeDirChars, QStringLiteral("_"));
        safeTitle = safeTitle.trimmed();
        if (safeTitle.isEmpty()) safeTitle = seriesId;

        const QString destPath = QDir(comicsRoot).absoluteFilePath(safeTitle);
        if (!QDir().mkpath(destPath)) {
            qInfo("WesternComicsPage: failed to mkpath Western dest %s", qUtf8Printable(destPath));
            return;
        }

        // auto-add to My Library on download START (immediate feedback).
        if (m_westernLibrary && !seriesId.isEmpty()) {
            tankoban::manga::WesternLibraryRecord r;
            r.seriesId = seriesId;
            r.title    = seriesTitle.isEmpty() ? seriesId : seriesTitle;
            r.coverUrl = m_currentWesternSeriesCover;
            r.addedAt  = QDateTime::currentMSecsSinceEpoch();
            m_westernLibrary->addOrUpdate(r);
            if (m_seriesView) m_seriesView->setWesternOnShelf(true);
        }

        qInfo("WesternComicsPage: Western issue download - series=%s edition=%s issue=%d dest=%s",
              qUtf8Printable(seriesId), qUtf8Printable(editionTitle),
              volumeNumber, qUtf8Printable(destPath));

        startWesternIssueDownload(seriesTitle, static_cast<double>(volumeNumber),
                                  editionTitle, volumeNumber, destPath);
    });
}

void WesternComicsPage::wireMangaDownloader()
{
    if (!m_mangaDownloader) return;
    // downloadUpdated -> route the in-flight Western (RCO) status to the panel
    // + tile (no-op for any non-western record).
    connect(m_mangaDownloader, &MangaDownloader::downloadUpdated,
            this, [this](const QString& recordId) {
        updateWesternMangaStatus(recordId);
    });
    // A completed readallcomics chapter that is the in-flight Western download
    // flips the tile to Read + updates the Sources panel.
    connect(m_mangaDownloader, &MangaDownloader::chapterCompleted, this,
            [this](const QString& source, const QString& seriesTitle,
                   const QString& /*chapterId*/, const QString& finalPath, qint64) {
        onWesternChapterCompleted(source, seriesTitle, finalPath);
    });
}

void WesternComicsPage::wireWesternDownloader()
{
    if (m_downloaderWired || !m_westernDownloader) return;
    m_downloaderWired = true;

    // volumeResolved: surface the matched collected edition in the panel.
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeResolved,
            this,
            [this](const QString& seriesId, int /*volNumber*/, const QString& editionTitle) {
        if (!m_seriesView) return;
        if (m_pendingWesternSeriesId.isEmpty() || seriesId != m_pendingWesternSeriesId) return;
        m_westernDownloadEdition = editionTitle;
        m_seriesView->updateWesternDownloadStatus(editionTitle, tr("Downloading..."));
    }, Qt::QueuedConnection);

    // volumeCompleted: register in index + flip tile to Read.
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeCompleted,
            this,
            [this](const QString& seriesId, int volNumber, const QString& cbzPath) {
        onProviderVolumeCompleted(seriesId, volNumber, cbzPath,
            static_cast<int>(PendingVolumeSourceKind::WesternGetComics));
        if (m_seriesView && !m_pendingWesternSeriesId.isEmpty()
            && seriesId == m_pendingWesternSeriesId) {
            m_seriesView->updateWesternDownloadStatus(
                m_westernDownloadEdition, tr("Downloaded - open to read"));
        }
    }, Qt::QueuedConnection);

    // volumeProgress: paint percent on the tile.
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeProgress,
            this,
            [this](const QString& seriesId, int volNumber, int percent) {
        if (!m_seriesView) return;
        const bool isCurrentSeries =
            !m_pendingWesternSeriesId.isEmpty() && seriesId == m_pendingWesternSeriesId;
        if (!isCurrentSeries) return;
        const QString tileLabel = percent <= 0
            ? QStringLiteral("Finding...")
            : QStringLiteral("%1%").arg(percent);
        m_seriesView->setVolumeStatusText(volNumber, tileLabel);
        const QString panelLine = percent <= 0
            ? tr("Finding...")
            : tr("Downloading %1%").arg(percent);
        m_seriesView->updateWesternDownloadStatus(m_westernDownloadEdition, panelLine);
    }, Qt::QueuedConnection);

    // volumeFailed: surface error text on the tile.
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::volumeFailed,
            this,
            [this](const QString& seriesId, int volNumber, const QString& reason) {
        onProviderVolumeFailed(seriesId, volNumber,
                               QStringLiteral("resolve_failed"),
                               reason,
                               static_cast<int>(PendingVolumeSourceKind::WesternGetComics));
        if (m_seriesView && !m_pendingWesternSeriesId.isEmpty()
            && seriesId == m_pendingWesternSeriesId) {
            m_seriesView->updateWesternDownloadStatus(
                QString(), tr("No download found"));
        }
    }, Qt::QueuedConnection);

    // coverReady: per-edition cover (logged; loader is private in the view).
    connect(m_westernDownloader,
            &tankoban::manga::WesternVolumeDownloader::coverReady,
            this,
            [this](const QString& seriesId, int volNumber, const QString& coverUrl) {
        if (!m_seriesView || coverUrl.isEmpty()) return;
        const bool isCurrentSeries =
            !m_pendingWesternSeriesId.isEmpty() && seriesId == m_pendingWesternSeriesId;
        if (!isCurrentSeries) return;
        Q_UNUSED(volNumber);
        qInfo("WesternComicsPage: coverReady for Western series=%s vol=%d url=%s",
              qUtf8Printable(seriesId), volNumber, qUtf8Printable(coverUrl));
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// dev-control bridge (dev-cmd names kept comics_* for back-compat)
// ---------------------------------------------------------------------------

QJsonObject WesternComicsPage::devSnapshot() const
{
    QJsonObject snap;
    const bool onSeries = m_seriesView && m_stack
                          && m_stack->currentWidget() == m_seriesView;
    snap[QStringLiteral("activeLayer")] = onSeries
        ? QStringLiteral("series-view") : QStringLiteral("library");
    snap[QStringLiteral("pendingWesternSeriesId")] = m_pendingWesternSeriesId;
    snap[QStringLiteral("currentSeriesTitle")]     = m_currentDetailSeriesTitle;
    return snap;
}

QJsonObject WesternComicsPage::devSeriesSnapshot() const
{
    if (!m_seriesView || !m_stack || m_stack->currentWidget() != m_seriesView) {
        return QJsonObject{{QStringLiteral("series"), QJsonValue::Null}};
    }
    return QJsonObject{{QStringLiteral("series"), m_seriesView->devSnapshot()}};
}

QJsonObject WesternComicsPage::devOpenWesternSeries(const QString& seriesId)
{
    if (seriesId.trimmed().isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("seriesId required")}};
    }
    const QString path =
        QDir(tankoban::manga::WesternCatalogLoader::canonicalDataDir())
            .absoluteFilePath(seriesId + QLatin1String(".json"));
    if (!QFile::exists(path)) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"),
                            QStringLiteral("catalogue file not found: ") + path}};
    }
    openWesternSeriesFromJson(path);
    const int editionCount = m_seriesView
        ? m_seriesView->devSnapshot().value(QStringLiteral("tileCount")).toInt()
        : 0;
    return QJsonObject{{QStringLiteral("ok"),           true},
                       {QStringLiteral("seriesId"),     m_pendingWesternSeriesId},
                       {QStringLiteral("seriesTitle"),  m_currentDetailSeriesTitle},
                       {QStringLiteral("editionCount"), editionCount}};
}

QJsonObject WesternComicsPage::devDownloadWesternEdition(int volumeNumber)
{
    if (m_pendingWesternSeriesId.isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("no western series open")}};
    }
    if (volumeNumber <= 0) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("volumeNumber must be positive")}};
    }
    if (!m_seriesView) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("series view not ready")}};
    }
    const QJsonArray vols = devSeriesSnapshot()
                                .value(QStringLiteral("series")).toObject()
                                .value(QStringLiteral("volumes")).toArray();
    bool editionExists = false;
    for (const auto& v : vols) {
        if (v.toObject().value(QStringLiteral("volume")).toInt(-1) == volumeNumber) {
            editionExists = true;
            break;
        }
    }
    if (!editionExists) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"),
                            QStringLiteral("edition %1 not found in current series").arg(volumeNumber)}};
    }
    m_seriesView->populateSourcesForVolume(volumeNumber);
    return QJsonObject{{QStringLiteral("ok"),           true},
                       {QStringLiteral("seriesId"),     m_pendingWesternSeriesId},
                       {QStringLiteral("volumeNumber"), volumeNumber}};
}

QJsonObject WesternComicsPage::devWesternDownloadState(int volumeNumber) const
{
    QJsonObject series = devSeriesSnapshot();
    if (series.value(QStringLiteral("series")).isNull()) {
        return QJsonObject{{QStringLiteral("ok"),    false},
                           {QStringLiteral("error"), QStringLiteral("no series view active")}};
    }

    QJsonObject out;
    out[QStringLiteral("ok")]          = true;
    out[QStringLiteral("seriesId")]    = m_pendingWesternSeriesId;
    out[QStringLiteral("seriesTitle")] = m_currentDetailSeriesTitle;

    QJsonArray volumes = series.value(QStringLiteral("series"))
                               .toObject()
                               .value(QStringLiteral("volumes"))
                               .toArray();
    if (m_seriesView) {
        for (int i = 0; i < volumes.size(); ++i) {
            QJsonObject row = volumes.at(i).toObject();
            const int vol = row.value(QStringLiteral("volume")).toInt(-1);
            if (vol <= 0) { volumes.replace(i, row); continue; }
            const auto* tile = m_seriesView->tileForVolume(vol);
            if (tile) {
                const auto st = tile->volumeState();
                row[QStringLiteral("tileState")]   = static_cast<int>(st.state);
                row[QStringLiteral("statusText")]  = st.statusText;
                row[QStringLiteral("progressPct")] = st.progressPct;
            }
            volumes.replace(i, row);
        }
    }

    if (volumeNumber > 0) {
        QJsonArray filtered;
        for (const QJsonValue& v : std::as_const(volumes)) {
            if (v.toObject().value(QStringLiteral("volume")).toInt(-1) == volumeNumber)
                filtered.append(v);
        }
        out[QStringLiteral("volumes")] = filtered;
    } else {
        out[QStringLiteral("volumes")] = volumes;
    }
    return out;
}
