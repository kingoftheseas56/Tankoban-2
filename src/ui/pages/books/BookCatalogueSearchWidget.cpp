#include "BookCatalogueSearchWidget.h"

#include "core/book/BookCatalogueAggregator.h"
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/FictionDbClient.h"
#include "ui/ContextMenuHelper.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QAction>
#include <QMenu>

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QSize>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>

namespace {

QString subtitleForBook(const BookCatalogueResult& book)
{
    QStringList parts;
    if (!book.author.isEmpty()) parts << book.author;
    if (!book.year.isEmpty()) parts << book.year;
    return parts.join(QStringLiteral(" / "));
}

QString safeFileStem(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                  QStringLiteral("_"));
    return value;
}

// catalogueId is "fictiondb:<slug>"; recover the slug for fetchBook().
QString slugOf(const QString& catalogueId)
{
    const QString prefix = QStringLiteral("fictiondb:");
    return catalogueId.startsWith(prefix) ? catalogueId.mid(prefix.size()) : QString();
}

} // namespace

BookCatalogueSearchWidget::BookCatalogueSearchWidget(BookCatalogueAggregator* aggregator,
                                                     QNetworkAccessManager* nam,
                                                     const QString& coverCacheDir,
                                                     QWidget* parent)
    : QWidget(parent)
    , m_aggregator(aggregator)
    , m_nam(nam)
    , m_coverCacheDir(coverCacheDir)
{
    buildUi();

    if (m_aggregator) {
        connect(m_aggregator, &BookCatalogueAggregator::aggregateReady,
                this, &BookCatalogueSearchWidget::onCatalogueResult);
        connect(m_aggregator, &BookCatalogueAggregator::aggregateFailed,
                this, &BookCatalogueSearchWidget::onCatalogueFailed);
    }

    if (m_nam) {
        m_coverClient = new FictionDbClient(m_nam, this);
        connect(m_coverClient, &FictionDbClient::bookReady,
                this, &BookCatalogueSearchWidget::onCoverBookReady);
        // bookFailed: leave the placeholder; not worth a retry per tile.
    }
}

void BookCatalogueSearchWidget::buildUi()
{
    setObjectName(QStringLiteral("BookCatalogueSearchWidget"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    topLayout->setSpacing(8);

    m_backButton = new QPushButton(QStringLiteral("< Books"), topRow);
    m_backButton->setObjectName(QStringLiteral("BookSearchBackButton"));
    m_backButton->setFixedHeight(36);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setStyleSheet(QStringLiteral(
        "QPushButton#BookSearchBackButton { background: transparent; border: none;"
        " color: rgba(255,255,255,0.7); font-size: 13px; padding: 0 8px; }"
        "QPushButton#BookSearchBackButton:hover { color: #ffffff; }"));
    connect(m_backButton, &QPushButton::clicked,
            this, &BookCatalogueSearchWidget::backRequested);
    topLayout->addWidget(m_backButton);

    // Persistent search bar — stays on the results page so the user can refine
    // the query without returning to the library grid. Submits go up through
    // BooksPage (searchSubmitted) so the grid bar + history stay in sync.
    m_searchInput = new QLineEdit(topRow);
    m_searchInput->setObjectName(QStringLiteral("LibrarySearch"));
    m_searchInput->setPlaceholderText(QStringLiteral("Search books catalogue"));
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setFixedHeight(36);
    m_searchInput->setStyleSheet(QStringLiteral(
        "QLineEdit#LibrarySearch { background: rgba(255,255,255,0.07);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        " color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit#LibrarySearch:focus { border: 1px solid rgba(255,255,255,0.3); }"));
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this]() {
        const QString q = m_searchInput->text().trimmed();
        if (!q.isEmpty()) emit searchSubmitted(q);
    });
    topLayout->addWidget(m_searchInput, 1);

    auto* searchBtn = new QPushButton(topRow);
    searchBtn->setObjectName(QStringLiteral("BookSearchGoButton"));
    searchBtn->setFixedSize(36, 36);
    searchBtn->setCursor(Qt::PointingHandCursor);
    searchBtn->setIcon(QIcon(QStringLiteral(":/icons/search.svg")));
    searchBtn->setIconSize(QSize(18, 18));
    searchBtn->setToolTip(QStringLiteral("Search"));
    searchBtn->setStyleSheet(QStringLiteral(
        "QPushButton#BookSearchGoButton { background: rgba(255,255,255,0.07);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px; }"
        "QPushButton#BookSearchGoButton:hover { background: rgba(255,255,255,0.11); }"));
    connect(searchBtn, &QPushButton::clicked, this, [this]() {
        const QString q = m_searchInput->text().trimmed();
        if (!q.isEmpty()) emit searchSubmitted(q);
    });
    topLayout->addWidget(searchBtn);
    root->addWidget(topRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("BookSearchStatus"));
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setContentsMargins(16, 0, 16, 0);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel#BookSearchStatus { color: #999999; font-size: 13px;"
        " background: transparent; }"));
    root->addWidget(m_statusLabel);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidgetResizable(true);
    m_scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget(m_scroll);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 8, 16, 20);
    layout->setSpacing(12);

    m_seriesHeader = new QLabel(QStringLiteral("SERIES"), content);
    m_seriesHeader->setObjectName(QStringLiteral("LibraryHeadingSmall"));
    m_seriesHeader->hide();
    layout->addWidget(m_seriesHeader);
    m_seriesStrip = new TileStrip(content);
    m_seriesStrip->setDensity(0);
    m_seriesStrip->hide();
    layout->addWidget(m_seriesStrip);

    m_seriesMoreBtn = new QPushButton(content);
    m_seriesMoreBtn->setObjectName(QStringLiteral("BookSearchShowMore"));
    m_seriesMoreBtn->setCursor(Qt::PointingHandCursor);
    m_seriesMoreBtn->setStyleSheet(QStringLiteral(
        "QPushButton#BookSearchShowMore { background: transparent; border: none;"
        " color: #8a6cff; font-size: 12px; text-align: left; padding: 2px 4px; }"
        "QPushButton#BookSearchShowMore:hover { color: #ffffff; }"));
    m_seriesMoreBtn->hide();
    connect(m_seriesMoreBtn, &QPushButton::clicked,
            this, &BookCatalogueSearchWidget::revealMoreSeries);
    layout->addWidget(m_seriesMoreBtn);

    m_booksHeader = new QLabel(QStringLiteral("BOOKS"), content);
    m_booksHeader->setObjectName(QStringLiteral("LibraryHeadingSmall"));
    layout->addWidget(m_booksHeader);
    m_booksStrip = new TileStrip(content);
    m_booksStrip->setDensity(0);
    layout->addWidget(m_booksStrip);

    m_booksMoreBtn = new QPushButton(content);
    m_booksMoreBtn->setObjectName(QStringLiteral("BookSearchShowMore"));
    m_booksMoreBtn->setCursor(Qt::PointingHandCursor);
    m_booksMoreBtn->setStyleSheet(QStringLiteral(
        "QPushButton#BookSearchShowMore { background: transparent; border: none;"
        " color: #8a6cff; font-size: 12px; text-align: left; padding: 2px 4px; }"
        "QPushButton#BookSearchShowMore:hover { color: #ffffff; }"));
    m_booksMoreBtn->hide();
    connect(m_booksMoreBtn, &QPushButton::clicked,
            this, &BookCatalogueSearchWidget::revealMoreBooks);
    layout->addWidget(m_booksMoreBtn);

    layout->addStretch(1);
    m_scroll->setWidget(content);
    root->addWidget(m_scroll, 1);

    // Right-click a result tile to add/remove it from the library.
    m_seriesStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_seriesStrip, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& p) { showTileContextMenu(m_seriesStrip, p); });
    m_booksStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_booksStrip, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& p) { showTileContextMenu(m_booksStrip, p); });

    clearResults();
}

void BookCatalogueSearchWidget::showTileContextMenu(TileStrip* strip, const QPoint& pos)
{
    if (!strip) return;
    TileCard* card = strip->tileAt(pos);
    if (!card) return;

    const QString catalogueId = card->property("catalogueId").toString();
    const QString type = card->property("catalogueType").toString();
    const auto it = m_resultsById.constFind(catalogueId);
    if (it == m_resultsById.constEnd()) return;
    const BookCatalogueResult result = it.value();

    const bool isSeries = (type == QLatin1String("series"));
    bool inLibrary = false;
    if (m_store) {
        inLibrary = isSeries
            ? !m_store->catalogueIdsForSeries(result.seriesId).isEmpty()
            : m_store->hasRecord(catalogueId);
    }

    QMenu* menu = ContextMenuHelper::createMenu(this);
    const QString what = isSeries ? QStringLiteral("series ") : QString();
    QAction* act = menu->addAction(inLibrary
        ? QStringLiteral("Remove %1from library").arg(what)
        : QStringLiteral("Add %1to library").arg(what));

    connect(act, &QAction::triggered, this, [this, isSeries, result]() {
        if (isSeries) emit seriesLibraryToggleRequested(result);
        else          emit bookLibraryToggleRequested(result);
    });

    menu->exec(strip->mapToGlobal(pos));
    menu->deleteLater();
}

void BookCatalogueSearchWidget::search(const QString& query)
{
    m_currentQuery = query.trimmed();
    // Reflect the active query in the results-page search bar (without firing
    // another search — setText doesn't trigger our returnPressed handler).
    if (m_searchInput && m_searchInput->text().trimmed() != m_currentQuery)
        m_searchInput->setText(m_currentQuery);
    clearResults();

    if (m_currentQuery.isEmpty()) {
        m_statusLabel->clear();
        return;
    }

    if (!m_aggregator) {
        m_statusLabel->setText(QStringLiteral("Catalogue search is unavailable."));
        return;
    }

    m_pending = true;
    m_statusLabel->setText(QStringLiteral("Searching catalogue for \"%1\"...")
                               .arg(m_currentQuery));
    m_aggregator->query(m_currentQuery);
}

void BookCatalogueSearchWidget::clearResults()
{
    m_pending = false;
    m_resultsById.clear();
    m_coverCardBySlug.clear();   // drop any in-flight cover fetches from the prior query
    m_overflowSeries.clear();
    m_overflowBooks.clear();
    if (m_seriesStrip) m_seriesStrip->clear();
    if (m_booksStrip) m_booksStrip->clear();
    if (m_seriesHeader) m_seriesHeader->hide();
    if (m_booksHeader) m_booksHeader->hide();
    if (m_seriesMoreBtn) m_seriesMoreBtn->hide();
    if (m_booksMoreBtn) m_booksMoreBtn->hide();
    if (m_statusLabel) m_statusLabel->clear();
}

void BookCatalogueSearchWidget::onCatalogueResult(
    const QString& query,
    const QList<SeriesDetector::SeriesGroup>& seriesGroups,
    const QList<BookCatalogueResult>& standalones)
{
    if (query != m_currentQuery) return;
    // Single clean paint: the aggregator resolves the series before emitting
    // once, so there's no intermediate flat-results flash. search() shows the
    // "Searching…" state until this lands.
    m_pending = false;

    // Re-render fresh each emit.
    m_resultsById.clear();
    m_overflowSeries.clear();
    m_overflowBooks.clear();
    m_seriesStrip->clear();
    m_booksStrip->clear();
    m_seriesMoreBtn->hide();
    m_booksMoreBtn->hide();

    // ── Series section (capped) ──
    int seriesShown = 0;
    for (const auto& group : seriesGroups) {
        if (group.books.isEmpty()) continue;
        if (seriesShown < kInitialCap) { addSeriesCard(group); ++seriesShown; }
        else m_overflowSeries.append(group);
    }
    const bool hasSeries = seriesShown > 0;
    m_seriesHeader->setVisible(hasSeries);
    m_seriesStrip->setVisible(hasSeries);
    if (!m_overflowSeries.isEmpty()) {
        m_seriesMoreBtn->setText(QStringLiteral("Show %1 more series").arg(m_overflowSeries.size()));
        m_seriesMoreBtn->show();
    }

    // ── Books section (capped, de-duped) ──
    int booksShown = 0;
    QSet<QString> seen;
    for (const auto& book : standalones) {
        if (book.catalogueId.isEmpty() || seen.contains(book.catalogueId)) continue;
        seen.insert(book.catalogueId);
        if (booksShown < kInitialCap) { addBookCard(book); ++booksShown; }
        else m_overflowBooks.append(book);
    }
    const bool hasBooks = booksShown > 0;
    m_booksHeader->setVisible(hasBooks);
    m_booksStrip->setVisible(hasBooks);
    if (!m_overflowBooks.isEmpty()) {
        m_booksMoreBtn->setText(QStringLiteral("Show %1 more books").arg(m_overflowBooks.size()));
        m_booksMoreBtn->show();
    }

    // ── Status ──
    // On a successful search the SERIES/BOOKS sections speak for themselves, so
    // we don't surface a result-count line (Hemanth 2026-05-29: the "N series ·
    // M books" header read as clutter). Only the genuinely-empty case keeps a
    // message, since a blank page with no feedback is worse.
    if (seriesGroups.isEmpty() && standalones.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("No results for \"%1\"").arg(m_currentQuery));
        m_statusLabel->show();
    } else {
        m_statusLabel->clear();
        m_statusLabel->hide();
    }
}

void BookCatalogueSearchWidget::onCatalogueFailed(const QString& query,
                                                  const QString& error)
{
    if (!m_pending || query != m_currentQuery) return;
    m_pending = false;

    m_statusLabel->setText(QStringLiteral("Catalogue search is unavailable right now."));
    m_statusLabel->setToolTip(error);
}

void BookCatalogueSearchWidget::addSeriesCard(const SeriesDetector::SeriesGroup& group)
{
    if (group.books.isEmpty()) return;
    const BookCatalogueResult stub = group.books.first();
    if (stub.seriesId.isEmpty() || stub.catalogueId.isEmpty()) return;

    m_resultsById.insert(stub.catalogueId, stub);

    const QString localCover = coverPathFor(stub.catalogueId);
    const QString thumb = QFile::exists(localCover) ? localCover : QString();

    // Series tiles come from the local index (no cover field); the cover
    // surfaces in the series detail view (v1). Subtitle = author when known.
    const QString subtitle = group.author.isEmpty()
        ? QStringLiteral("Series")
        : group.author;
    auto* card = new TileCard(thumb, group.seriesName, subtitle);
    card->setProperty("catalogueType", QStringLiteral("series"));
    card->setProperty("catalogueId", stub.catalogueId);
    connect(card, &TileCard::clicked, this, [this, card]() {
        const QString catalogueId = card->property("catalogueId").toString();
        const auto it = m_resultsById.constFind(catalogueId);
        if (it == m_resultsById.constEnd()) return;
        emit seriesPicked(it.value());
    });
    m_seriesStrip->addTile(card);

    if (thumb.isEmpty() && !stub.coverUrl.isEmpty())
        downloadCover(stub.catalogueId, stub.coverUrl, card);
}

void BookCatalogueSearchWidget::revealMoreSeries()
{
    const auto overflow = m_overflowSeries;
    m_overflowSeries.clear();
    for (const auto& group : overflow) addSeriesCard(group);
    if (m_seriesMoreBtn) m_seriesMoreBtn->hide();
}

void BookCatalogueSearchWidget::revealMoreBooks()
{
    const auto overflow = m_overflowBooks;
    m_overflowBooks.clear();
    for (const auto& book : overflow) addBookCard(book);
    if (m_booksMoreBtn) m_booksMoreBtn->hide();
}

void BookCatalogueSearchWidget::addBookCard(const BookCatalogueResult& book)
{
    if (book.catalogueId.isEmpty()) return;

    m_resultsById.insert(book.catalogueId, book);

    const QString localCover = coverPathFor(book.catalogueId);
    const QString thumb = QFile::exists(localCover) ? localCover : QString();

    auto* card = new TileCard(thumb, book.title, subtitleForBook(book));
    card->setProperty("catalogueType", QStringLiteral("book"));
    card->setProperty("catalogueId", book.catalogueId);
    connect(card, &TileCard::clicked, this, [this, card]() {
        const QString catalogueId = card->property("catalogueId").toString();
        const auto it = m_resultsById.constFind(catalogueId);
        if (it == m_resultsById.constEnd()) return;
        const QString expectedCoverPath = coverPathFor(catalogueId);
        const QString coverPath = (QFile::exists(expectedCoverPath) || !it->coverUrl.isEmpty())
            ? expectedCoverPath
            : QString();
        emit bookPicked(it.value(), coverPath);
    });
    m_booksStrip->addTile(card);

    if (!thumb.isEmpty()) return;   // already cached on disk

    if (!book.coverUrl.isEmpty()) {
        downloadCover(book.catalogueId, book.coverUrl, card);
        return;
    }

    // No cover URL on the search-result stub — fetch the book's page to get
    // one, then download. Keyed by slug so onCoverBookReady can find the card.
    const QString slug = slugOf(book.catalogueId);
    if (m_coverClient && !slug.isEmpty()) {
        m_coverCardBySlug.insert(slug, card);
        m_coverClient->fetchBook(slug);
    }
}

void BookCatalogueSearchWidget::onCoverBookReady(const QString& bookId,
                                                 const BookCatalogueResult& book)
{
    const auto it = m_coverCardBySlug.constFind(bookId);
    if (it == m_coverCardBySlug.constEnd()) return;
    TileCard* card = it.value();          // QPointer — null if the tile was cleared
    m_coverCardBySlug.erase(it);
    if (!card || book.coverUrl.isEmpty()) return;

    const QString catalogueId = QStringLiteral("fictiondb:%1").arg(bookId);
    downloadCover(catalogueId, book.coverUrl, card);
}

QString BookCatalogueSearchWidget::coverPathFor(const QString& catalogueId) const
{
    if (m_coverCacheDir.isEmpty() || catalogueId.isEmpty()) return {};
    return m_coverCacheDir + QLatin1Char('/') + safeFileStem(catalogueId)
         + QStringLiteral(".jpg");
}

void BookCatalogueSearchWidget::downloadCover(const QString& catalogueId,
                                              const QString& coverUrl,
                                              TileCard* card)
{
    if (!m_nam || catalogueId.isEmpty() || coverUrl.isEmpty() || !card) return;

    QNetworkRequest req{QUrl(coverUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Tankoban/1.0"));
    req.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*");
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    const QString path = coverPathFor(catalogueId);
    QPointer<TileCard> guard(card);
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, path, guard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QByteArray data = reply->readAll();
        if (data.isEmpty()) return;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return;
        file.write(data);
        file.close();

        if (guard) guard->setThumbPath(path);
    });
}
