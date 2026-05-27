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

    m_booksHeader = new QLabel(QStringLiteral("BOOKS"), content);
    m_booksHeader->setObjectName(QStringLiteral("LibraryHeadingSmall"));
    layout->addWidget(m_booksHeader);
    m_booksStrip = new TileStrip(content);
    m_booksStrip->setDensity(0);
    layout->addWidget(m_booksStrip);

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
    if (m_seriesStrip) m_seriesStrip->clear();
    if (m_booksStrip) m_booksStrip->clear();
    if (m_seriesHeader) m_seriesHeader->hide();
    if (m_booksHeader) m_booksHeader->hide();
    if (m_statusLabel) m_statusLabel->clear();
}

void BookCatalogueSearchWidget::onCatalogueResult(
    const QString& query,
    const QList<SeriesDetector::SeriesGroup>& seriesGroups,
    const QList<BookCatalogueResult>& standalones)
{
    if (!m_pending || query != m_currentQuery) return;
    m_pending = false;

    QSet<QString> seenIds;
    int total = 0;
    for (const auto& group : seriesGroups) {
        for (const auto& book : group.books) {
            if (seenIds.contains(book.catalogueId)) continue;
            seenIds.insert(book.catalogueId);
            addBookCard(book);
            ++total;
        }
    }
    for (const auto& book : standalones) {
        if (seenIds.contains(book.catalogueId)) continue;
        seenIds.insert(book.catalogueId);
        addBookCard(book);
        ++total;
    }

    m_seriesHeader->hide();
    m_seriesStrip->hide();
    m_booksHeader->setVisible(m_booksStrip->totalCount() > 0);

    if (total == 0) {
        m_statusLabel->setText(QStringLiteral("No catalogue results for \"%1\"")
                                   .arg(m_currentQuery));
    } else {
        m_statusLabel->setText(QStringLiteral("%1 result%2 for \"%3\"")
                                   .arg(total)
                                   .arg(total == 1 ? QString() : QStringLiteral("s"))
                                   .arg(m_currentQuery));
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
    Q_UNUSED(group);
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
