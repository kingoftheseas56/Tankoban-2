#include "BookCatalogueSearchWidget.h"

#include "core/book/BookCatalogueAggregator.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
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
    m_backButton->setFixedHeight(30);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setStyleSheet(QStringLiteral(
        "QPushButton#BookSearchBackButton { background: transparent; border: none;"
        " color: rgba(255,255,255,0.7); font-size: 13px; padding: 0 8px; }"
        "QPushButton#BookSearchBackButton:hover { color: #ffffff; }"));
    connect(m_backButton, &QPushButton::clicked,
            this, &BookCatalogueSearchWidget::backRequested);
    topLayout->addWidget(m_backButton);

    m_statusLabel = new QLabel(topRow);
    m_statusLabel->setObjectName(QStringLiteral("BookSearchStatus"));
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel#BookSearchStatus { color: #999999; font-size: 13px;"
        " background: transparent; }"));
    topLayout->addWidget(m_statusLabel, 1);
    root->addWidget(topRow);

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

    clearResults();
}

void BookCatalogueSearchWidget::search(const QString& query)
{
    m_currentQuery = query.trimmed();
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
    if (seriesGroups.isEmpty() && standalones.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("No catalogue results for \"%1\"").arg(m_currentQuery));
    } else {
        QString parts;
        if (!seriesGroups.isEmpty())
            parts = QStringLiteral("%1 series").arg(seriesGroups.size());
        if (!standalones.isEmpty()) {
            if (!parts.isEmpty()) parts += QStringLiteral(" · ");
            parts += QStringLiteral("%1 books").arg(standalones.size());
        }
        m_statusLabel->setText(parts + QStringLiteral(" for \"%1\"").arg(m_currentQuery));
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

    if (thumb.isEmpty() && !book.coverUrl.isEmpty())
        downloadCover(book.catalogueId, book.coverUrl, card);
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
