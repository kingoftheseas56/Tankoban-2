#include "BooksPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "core/CoreBridge.h"
#include "core/BooksScanner.h"

#include <QVBoxLayout>
#include <QScrollArea>
#include <QMetaObject>

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

    // Scrollable content area
    auto* scroll = new QScrollArea(this);
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
    auto* bookTitle = new QLabel("Books", bookHeader);
    bookTitle->setObjectName("SectionTitle");
    bookHeaderLayout->addWidget(bookTitle);
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
    outerLayout->addWidget(scroll, 1);
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
