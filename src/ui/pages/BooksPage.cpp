#include "BooksPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "BookSeriesView.h"
#include "core/CoreBridge.h"
#include "core/BooksScanner.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMetaObject>
#include <QSettings>

BooksPage::BooksPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("books");
    qRegisterMetaType<BookSeriesInfo>("BookSeriesInfo");
    qRegisterMetaType<AudiobookInfo>("AudiobookInfo");
    qRegisterMetaType<QList<BookSeriesInfo>>("QList<BookSeriesInfo>");
    qRegisterMetaType<QList<AudiobookInfo>>("QList<AudiobookInfo>");

    buildUI();

    m_scanThread = new QThread(this);
    m_scanner = new BooksScanner(m_bridge->dataDir() + "/thumbs");
    m_scanner->moveToThread(m_scanThread);

    connect(m_scanner, &BooksScanner::bookSeriesFound,
            this, &BooksPage::onBookSeriesFound, Qt::QueuedConnection);
    connect(m_scanner, &BooksScanner::audiobookFound,
            this, &BooksPage::onAudiobookFound, Qt::QueuedConnection);
    connect(m_scanner, &BooksScanner::scanFinished,
            this, &BooksPage::onScanFinished, Qt::QueuedConnection);

    m_scanThread->start();

    connect(m_bridge, &CoreBridge::rootFoldersChanged, this, [this](const QString& domain) {
        if (domain == "books" || domain == "audiobooks")
            triggerScan();
    });
}

BooksPage::~BooksPage()
{
    m_scanThread->quit();
    m_scanThread->wait();
    delete m_scanner;
}

void BooksPage::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_stack = new QStackedWidget(this);

    // ── Grid view (index 0) ──
    auto* gridPage = new QWidget();
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);

    // Scrollable content area
    auto* scroll = new QScrollArea(gridPage);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("background: transparent;");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Books section ──
    auto* bookHeader = new QWidget(content);
    auto* bookHeaderLayout = new QVBoxLayout(bookHeader);
    bookHeaderLayout->setContentsMargins(24, 20, 24, 12);
    bookHeaderLayout->setSpacing(8);

    auto* titleRow = new QHBoxLayout();
    auto* bookTitle = new QLabel("Books", bookHeader);
    bookTitle->setObjectName("SectionTitle");
    titleRow->addWidget(bookTitle);
    titleRow->addStretch();

    m_sortCombo = new QComboBox(bookHeader);
    m_sortCombo->setObjectName("LibrarySortCombo");
    m_sortCombo->setFixedWidth(150);
    m_sortCombo->setFixedHeight(28);
    m_sortCombo->addItem("Name A\u2192Z",       "name_asc");
    m_sortCombo->addItem("Name Z\u2192A",       "name_desc");
    m_sortCombo->addItem("Recently updated",     "updated_desc");
    m_sortCombo->addItem("Least recent",         "updated_asc");
    m_sortCombo->addItem("Most items",           "count_desc");
    m_sortCombo->addItem("Fewest items",         "count_asc");
    m_sortCombo->setStyleSheet(
        "QComboBox#LibrarySortCombo {"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox#LibrarySortCombo:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox#LibrarySortCombo::drop-down { border: none; }"
        "QComboBox#LibrarySortCombo QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    QString savedSort = QSettings("Tankoban", "Tankoban").value("library_sort_books", "name_asc").toString();
    for (int i = 0; i < m_sortCombo->count(); ++i) {
        if (m_sortCombo->itemData(i).toString() == savedSort) {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QString key = m_sortCombo->itemData(idx).toString();
        QSettings("Tankoban", "Tankoban").setValue("library_sort_books", key);
        m_bookStrip->sortTiles(key);
    });
    titleRow->addWidget(m_sortCombo);
    bookHeaderLayout->addLayout(titleRow);

    m_searchBar = new QLineEdit(bookHeader);
    m_searchBar->setPlaceholderText("Search books and series\u2026");
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setObjectName("LibrarySearch");
    m_searchBar->setFixedHeight(32);
    m_searchBar->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid rgba(255,255,255,0.3); }");
    bookHeaderLayout->addWidget(m_searchBar);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    connect(m_searchTimer, &QTimer::timeout, this, &BooksPage::applySearch);

    layout->addWidget(bookHeader);

    m_bookStatus = new QLabel("Add a books folder to get started", content);
    m_bookStatus->setObjectName("TileSubtitle");
    m_bookStatus->setAlignment(Qt::AlignCenter);
    m_bookStatus->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 40px;");
    layout->addWidget(m_bookStatus);

    m_bookStrip = new TileStrip(content);
    m_bookStrip->hide();
    m_bookStrip->setMinimumHeight(340);
    layout->addWidget(m_bookStrip);

    // ── Audiobooks section ──
    m_audiobookSection = new QWidget(content);
    auto* abLayout = new QVBoxLayout(m_audiobookSection);
    abLayout->setContentsMargins(0, 0, 0, 0);
    abLayout->setSpacing(0);

    auto* abHeader = new QWidget(m_audiobookSection);
    auto* abHeaderLayout = new QVBoxLayout(abHeader);
    abHeaderLayout->setContentsMargins(24, 20, 24, 12);
    m_audiobookTitle = new QLabel("Audiobooks", abHeader);
    m_audiobookTitle->setObjectName("SectionTitle");
    abHeaderLayout->addWidget(m_audiobookTitle);
    abLayout->addWidget(abHeader);

    m_audiobookStatus = new QLabel("No audiobooks found", m_audiobookSection);
    m_audiobookStatus->setObjectName("TileSubtitle");
    m_audiobookStatus->setAlignment(Qt::AlignCenter);
    m_audiobookStatus->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 40px;");
    abLayout->addWidget(m_audiobookStatus);

    m_audiobookStrip = new TileStrip(m_audiobookSection);
    m_audiobookStrip->hide();
    m_audiobookStrip->setMinimumHeight(340);
    abLayout->addWidget(m_audiobookStrip);

    m_audiobookSection->hide();
    layout->addWidget(m_audiobookSection);

    layout->addStretch(1);
    scroll->setWidget(content);
    gridLayout->addWidget(scroll, 1);

    m_stack->addWidget(gridPage);

    // ── Series view (index 1) ──
    m_seriesView = new BookSeriesView();
    connect(m_seriesView, &BookSeriesView::backRequested, this, &BooksPage::showGrid);
    connect(m_seriesView, &BookSeriesView::bookSelected, this, &BooksPage::openBook);
    m_stack->addWidget(m_seriesView);

    outerLayout->addWidget(m_stack, 1);
}

void BooksPage::activate()
{
    if (!m_hasScanned)
        triggerScan();
}

void BooksPage::triggerScan()
{
    if (m_scanning) return;
    m_scanning = true;

    QStringList bookRoots = m_bridge->rootFolders("books");
    QStringList audiobookRoots = m_bridge->rootFolders("audiobooks");

    if (bookRoots.isEmpty() && audiobookRoots.isEmpty()) {
        m_bookStrip->clear();
        m_bookStrip->hide();
        m_bookStatus->setText("Add a books folder to get started");
        m_bookStatus->show();
        m_audiobookSection->hide();
        m_hasScanned = true;
        return;
    }

    m_bookStrip->clear();
    m_bookStatus->setText("Scanning...");
    m_bookStatus->show();
    m_bookStrip->hide();

    m_audiobookStrip->clear();
    m_audiobookStatus->setText("Scanning...");
    m_audiobookStatus->show();
    m_audiobookStrip->hide();
    m_audiobookSection->show();

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, bookRoots),
                              Q_ARG(QStringList, audiobookRoots));
}

void BooksPage::onBookSeriesFound(const BookSeriesInfo& series)
{
    if (m_bookStatus->isVisible()) {
        m_bookStatus->hide();
        m_bookStrip->show();
    }

    QString subtitle = QString::number(series.fileCount)
                     + (series.fileCount == 1 ? " book" : " books");

    auto* card = new TileCard(series.coverThumbPath, series.seriesName, subtitle);

    // Store series data for click handling and sorting
    card->setProperty("seriesPath", series.seriesPath);
    card->setProperty("seriesName", series.seriesName);
    card->setProperty("fileCount", series.fileCount);
    card->setProperty("newestMtime", series.newestMtimeMs);
    connect(card, &TileCard::clicked, this, [this, card]() {
        onTileClicked(card->property("seriesPath").toString(),
                      card->property("seriesName").toString());
    });

    m_bookStrip->addTile(card);
}

void BooksPage::onAudiobookFound(const AudiobookInfo& audiobook)
{
    if (m_audiobookStatus->isVisible()) {
        m_audiobookStatus->hide();
        m_audiobookStrip->show();
    }

    QString subtitle = QString::number(audiobook.trackCount)
                     + (audiobook.trackCount == 1 ? " track" : " tracks");

    // Audiobooks use a placeholder with a music note initial
    auto* card = new TileCard("", audiobook.name, subtitle);
    m_audiobookStrip->addTile(card);
}

void BooksPage::onScanFinished(const QList<BookSeriesInfo>& allBooks,
                                const QList<AudiobookInfo>& allAudiobooks)
{
    m_hasScanned = true;
    m_scanning = false;

    if (allBooks.isEmpty()) {
        m_bookStrip->hide();
        if (m_bridge->rootFolders("books").isEmpty())
            m_bookStatus->setText("Add a books folder to get started");
        else
            m_bookStatus->setText("No books found in your library folders");
        m_bookStatus->show();
    } else {
        m_bookStrip->sortTiles(m_sortCombo->currentData().toString());
    }

    if (allAudiobooks.isEmpty()) {
        m_audiobookStrip->hide();
        if (m_bridge->rootFolders("audiobooks").isEmpty())
            m_audiobookSection->hide();
        else {
            m_audiobookStatus->setText("No audiobooks found in your library folders");
            m_audiobookStatus->show();
        }
    }
}

void BooksPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    m_seriesView->showSeries(seriesPath, seriesName);
    m_stack->setCurrentIndex(1);
}

void BooksPage::showGrid()
{
    m_stack->setCurrentIndex(0);
}

void BooksPage::applySearch()
{
    QString query = m_searchBar->text();
    m_bookStrip->filterTiles(query);
    m_audiobookStrip->filterTiles(query);

    bool searchActive = !query.trimmed().isEmpty();

    // Books section empty state
    if (m_bookStrip->visibleCount() == 0 && searchActive) {
        m_bookStatus->setText(
            QString("No results for \"%1\"").arg(query.trimmed()));
        m_bookStatus->show();
        m_bookStrip->hide();
    } else if (m_bookStrip->visibleCount() > 0) {
        m_bookStatus->hide();
        m_bookStrip->show();
    }

    // Audiobooks section empty state
    if (m_audiobookStrip->totalCount() > 0) {
        if (m_audiobookStrip->visibleCount() == 0 && searchActive) {
            m_audiobookSection->hide();
        } else if (m_audiobookStrip->visibleCount() > 0) {
            m_audiobookSection->show();
            m_audiobookStatus->hide();
            m_audiobookStrip->show();
        }
    }
}
