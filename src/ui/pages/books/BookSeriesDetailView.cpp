#include "BookSeriesDetailView.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"
#include "core/book/FictionDbClient.h"

namespace {

constexpr int kEnrichTimeoutMs = 9000;
const QString kIdPrefix = QStringLiteral("fictiondb:");

// Mirror BookCatalogueSearchWidget::safeFileStem so both share the cover cache.
QString safeFileStem(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                  QStringLiteral("_"));
    return value;
}

// catalogueId "fictiondb:<slug>" -> "<slug>" for FictionDbClient::fetchBook.
QString bookIdFromCatalogueId(const QString& catalogueId)
{
    return catalogueId.startsWith(kIdPrefix) ? catalogueId.mid(kIdPrefix.size())
                                             : catalogueId;
}

}  // namespace

BookSeriesDetailView::BookSeriesDetailView(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void BookSeriesDetailView::setCatalogueStore(BooksCatalogueLibraryStore* store)
{
    m_store = store;
    if (m_store)
        connect(m_store, &BooksCatalogueLibraryStore::recordsChanged,
                this, [this] { rebuildRows(); });
}

void BookSeriesDetailView::setNetwork(QNetworkAccessManager* nam, const QString& coverDir)
{
    m_nam = nam;
    m_coverDir = coverDir;
    if (!m_coverDir.isEmpty()) QDir().mkpath(m_coverDir);

    if (!m_client && m_nam) {
        m_client = new FictionDbClient(m_nam, this);
        connect(m_client, &FictionDbClient::seriesReady,
                this, &BookSeriesDetailView::onSeriesReady);
        connect(m_client, &FictionDbClient::bookReady,
                this, &BookSeriesDetailView::onBookEnriched);
        connect(m_client, &FictionDbClient::bookFailed, this,
                [this](const QString& bookId, const QString&) {
                    // A failed page leaves the book bare; still settle it.
                    if (m_pending.isEmpty()) return;
                    const QString cid = kIdPrefix + bookId;
                    if (m_pendingPageIds.remove(cid))
                        noteBookSettled();
                });
        connect(m_client, &FictionDbClient::seriesFailed, this,
                [this](const QString& seriesId, const QString&) {
                    if (seriesId != m_loadingSeriesId) return;
                    // Nothing to enrich — render whatever (likely empty).
                    finalizeRender();
                });
    }

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (!m_rendered) finalizeRender();
    });
}

void BookSeriesDetailView::buildUi()
{
    setObjectName(QStringLiteral("BookSeriesDetailView"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top bar — back.
    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    auto* back = new QPushButton(QStringLiteral("< Books"), topRow);
    back->setObjectName(QStringLiteral("BookSeriesBackButton"));
    back->setCursor(Qt::PointingHandCursor);
    back->setStyleSheet(QStringLiteral(
        "QPushButton#BookSeriesBackButton { background: transparent; border: none;"
        " color: rgba(255,255,255,0.7); font-size: 13px; padding: 0 8px; }"
        "QPushButton#BookSeriesBackButton:hover { color: #ffffff; }"));
    connect(back, &QPushButton::clicked, this, &BookSeriesDetailView::backRequested);
    topLayout->addWidget(back);
    topLayout->addStretch(1);
    root->addWidget(topRow);

    // Loading state (shown while enrichment is gathering book pages + covers).
    m_loadingLabel = new QLabel(QStringLiteral("Loading series…"), this);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_loadingLabel->setStyleSheet(QStringLiteral(
        "color: rgba(255,255,255,0.5); font-size: 14px;"));
    m_loadingLabel->hide();
    root->addWidget(m_loadingLabel, 1);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget(m_scroll);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(24, 8, 24, 24);
    col->setSpacing(16);

    // Hero — cover + title + meta.
    auto* hero = new QWidget(content);
    auto* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(16);

    m_coverLabel = new QLabel(hero);
    m_coverLabel->setFixedSize(170, 255);
    m_coverLabel->setAlignment(Qt::AlignCenter);
    m_coverLabel->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.06); border-radius: 6px; color: rgba(255,255,255,0.3);"));
    heroLayout->addWidget(m_coverLabel, 0, Qt::AlignTop);

    auto* heroText = new QWidget(hero);
    auto* heroTextLayout = new QVBoxLayout(heroText);
    heroTextLayout->setContentsMargins(0, 0, 0, 0);
    heroTextLayout->setSpacing(6);
    m_titleLabel = new QLabel(heroText);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 22px; font-weight: 600;"));
    m_metaLabel = new QLabel(heroText);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.6); font-size: 13px;"));
    heroTextLayout->addWidget(m_titleLabel);
    heroTextLayout->addWidget(m_metaLabel);
    heroTextLayout->addStretch(1);
    heroLayout->addWidget(heroText, 1);
    col->addWidget(hero);

    // Books-in-series rows.
    m_rowsContainer = new QWidget(content);
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(6);
    col->addWidget(m_rowsContainer);
    col->addStretch(1);

    m_scroll->setWidget(content);
    root->addWidget(m_scroll, 1);
}

void BookSeriesDetailView::setLoading(bool loading)
{
    if (m_loadingLabel) m_loadingLabel->setVisible(loading);
    if (m_scroll) m_scroll->setVisible(!loading);
}

void BookSeriesDetailView::loadSeries(const QString& seriesId)
{
    if (seriesId.isEmpty() || !m_client) return;

    ++m_loadToken;

    // Cache hit — render instantly, no fetch.
    const auto cached = m_cache.constFind(seriesId);
    if (cached != m_cache.constEnd()) {
        renderSeries(cached->seriesName, cached->author, cached->heroCoverPath,
                     cached->books);
        setLoading(false);
        return;
    }

    // Cache miss — show loading, fetch the series, then enrich each book.
    m_loadingSeriesId = seriesId;
    m_loadingSeriesName.clear();
    m_pending.clear();
    m_pendingPageIds.clear();
    m_coversRemaining = 0;
    m_rendered = false;
    setLoading(true);
    if (m_timeout) m_timeout->start(kEnrichTimeoutMs);
    m_client->fetchSeries(seriesId);
}

void BookSeriesDetailView::onSeriesReady(const QString& seriesId,
                                         const QString& seriesName,
                                         const QList<BookCatalogueResult>& books)
{
    if (seriesId != m_loadingSeriesId) return;  // stale reply from a prior load

    m_loadingSeriesName = seriesName;
    m_pending = books;
    m_pendingPageIds.clear();

    if (m_pending.isEmpty()) {
        finalizeRender();
        return;
    }

    for (const BookCatalogueResult& b : m_pending)
        if (!b.catalogueId.isEmpty()) m_pendingPageIds.insert(b.catalogueId);

    if (m_pendingPageIds.isEmpty()) {
        startCoverPhase();  // nothing fetchable — skip straight to covers/render
        return;
    }

    // Snapshot the ids so completions over the shared client can't race the loop.
    const QSet<QString> ids = m_pendingPageIds;
    for (const QString& cid : ids)
        m_client->fetchBook(bookIdFromCatalogueId(cid));
}

void BookSeriesDetailView::onBookEnriched(const QString& /*bookId*/,
                                          const BookCatalogueResult& book)
{
    // Match against the current load's pending set; ignore stale/dup replies.
    if (!m_pendingPageIds.remove(book.catalogueId)) return;

    for (BookCatalogueResult& p : m_pending) {
        if (p.catalogueId != book.catalogueId) continue;
        // Overlay the rich book-page fields; keep series ordering from the
        // series page (authoritative reading order).
        if (!book.title.isEmpty()) p.title = book.title;
        if (!book.author.isEmpty()) p.author = book.author;
        if (!book.coverUrl.isEmpty()) p.coverUrl = book.coverUrl;
        if (!book.description.isEmpty()) p.description = book.description;
        if (!book.year.isEmpty()) p.year = book.year;
        if (!book.isbn.isEmpty()) p.isbn = book.isbn;
        break;
    }
    noteBookSettled();
}

void BookSeriesDetailView::noteBookSettled()
{
    if (m_pendingPageIds.isEmpty()) startCoverPhase();
}

void BookSeriesDetailView::startCoverPhase()
{
    QList<BookCatalogueResult> toFetch;
    for (const BookCatalogueResult& b : m_pending) {
        if (b.coverUrl.isEmpty()) continue;
        if (QFile::exists(coverPathFor(b.catalogueId))) continue;
        toFetch.append(b);
    }

    m_coversRemaining = toFetch.size();
    if (m_coversRemaining == 0) {
        finalizeRender();
        return;
    }
    for (const BookCatalogueResult& b : toFetch)
        downloadCover(b.catalogueId, b.coverUrl);
}

void BookSeriesDetailView::downloadCover(const QString& catalogueId,
                                         const QString& coverUrl)
{
    if (!m_nam || catalogueId.isEmpty() || coverUrl.isEmpty()) return;

    QNetworkRequest req{QUrl(coverUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Tankoban/1.0"));
    req.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*");
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    const QString path = coverPathFor(catalogueId);
    const int token = m_loadToken;
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, token]() {
        reply->deleteLater();
        if (token != m_loadToken) return;  // a newer load owns the view now

        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            if (!data.isEmpty()) {
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(data);
                    file.close();
                }
            }
        }
        if (--m_coversRemaining <= 0) finalizeRender();
    });
}

void BookSeriesDetailView::finalizeRender()
{
    if (m_rendered) return;
    m_rendered = true;
    if (m_timeout) m_timeout->stop();

    const QString author = m_pending.isEmpty() ? QString() : m_pending.first().author;

    // Hero cover = the first member book (reading order) with a cached cover.
    QString heroCover;
    for (const BookCatalogueResult& b : m_pending) {
        const QString p = coverPathFor(b.catalogueId);
        if (QFile::exists(p)) { heroCover = p; break; }
    }

    m_cache.insert(m_loadingSeriesId,
                   {m_loadingSeriesName, author, heroCover, m_pending});

    renderSeries(m_loadingSeriesName, author, heroCover, m_pending);
    setLoading(false);
}

QString BookSeriesDetailView::coverPathFor(const QString& catalogueId) const
{
    if (m_coverDir.isEmpty() || catalogueId.isEmpty()) return {};
    return m_coverDir + QLatin1Char('/') + safeFileStem(catalogueId)
         + QStringLiteral(".jpg");
}

void BookSeriesDetailView::renderSeries(const QString& seriesName, const QString& author,
                                        const QString& heroCoverPath,
                                        const QList<BookCatalogueResult>& books)
{
    m_seriesName = seriesName;
    m_author = author;
    m_heroCoverPath = heroCoverPath;
    m_books = books;

    m_titleLabel->setText(seriesName);
    QStringList metaParts;
    if (!author.isEmpty()) metaParts << author;
    metaParts << QStringLiteral("%1 book%2").arg(books.size())
                                            .arg(books.size() == 1 ? QString() : QStringLiteral("s"));
    m_metaLabel->setText(metaParts.join(QStringLiteral("  ·  ")));

    m_coverLabel->setPixmap(QPixmap());
    if (!heroCoverPath.isEmpty() && QFile::exists(heroCoverPath)) {
        QPixmap pm(heroCoverPath);
        if (!pm.isNull()) {
            m_coverLabel->setPixmap(pm.scaled(m_coverLabel->size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
            m_coverLabel->setText(QString());
        }
    } else {
        m_coverLabel->setText(QStringLiteral("No cover"));
    }

    rebuildRows();
}

void BookSeriesDetailView::rebuildRows()
{
    if (!m_rowsLayout) return;
    while (QLayoutItem* item = m_rowsLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (const BookCatalogueResult& book : m_books) {
        auto* row = new QWidget(m_rowsContainer);
        row->setStyleSheet(QStringLiteral(
            "background: rgba(255,255,255,0.04); border-radius: 6px;"));
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        const BookCatalogueResult ctxBook = book;
        connect(row, &QWidget::customContextMenuRequested, this,
                [this, row, ctxBook](const QPoint& p) {
                    emit bookContextMenuRequested(ctxBook, row->mapToGlobal(p));
                });
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(14, 14, 14, 14);
        h->setSpacing(16);

        // Cover thumbnail (from the per-book enrichment fetch).
        auto* thumb = new QLabel(row);
        thumb->setFixedSize(104, 156);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(QStringLiteral(
            "background: rgba(255,255,255,0.06); border-radius: 4px;"));
        const QString coverPath = coverPathFor(book.catalogueId);
        if (!coverPath.isEmpty() && QFile::exists(coverPath)) {
            QPixmap pm(coverPath);
            if (!pm.isNull())
                thumb->setPixmap(pm.scaled(thumb->size(), Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
        }
        h->addWidget(thumb, 0, Qt::AlignTop);

        // Title + author/year + synopsis.
        auto* textCol = new QWidget(row);
        auto* tv = new QVBoxLayout(textCol);
        tv->setContentsMargins(0, 0, 0, 0);
        tv->setSpacing(2);

        const QString label = book.seriesPosition > 0
            ? QStringLiteral("%1.  %2").arg(book.seriesPosition).arg(book.title)
            : book.title;
        auto* name = new QLabel(label, textCol);
        name->setWordWrap(true);
        name->setStyleSheet(QStringLiteral(
            "color: #ffffff; font-size: 14px; font-weight: 600; background: transparent;"));
        tv->addWidget(name);

        QStringList sub;
        if (!book.author.isEmpty()) sub << book.author;
        if (!book.year.isEmpty()) sub << book.year;
        if (!sub.isEmpty()) {
            // Byline — accent-tinted, letter-spaced, distinct from the prose.
            auto* subLabel = new QLabel(sub.join(QStringLiteral("   ·   ")), textCol);
            subLabel->setStyleSheet(QStringLiteral(
                "color: rgba(170,158,214,0.95); font-size: 11px; font-weight: 600;"
                " letter-spacing: 0.7px; background: transparent;"
                " padding: 1px 0 5px 0;"));
            tv->addWidget(subLabel);
        }
        if (!book.description.isEmpty()) {
            // Synopsis in its own inset backdrop panel so it reads as a distinct
            // blurb block, set apart from the title + byline above.
            auto* desc = new QLabel(book.description, textCol);
            desc->setWordWrap(true);
            desc->setStyleSheet(QStringLiteral(
                "color: rgba(228,228,234,0.78); font-size: 12px;"
                " background: rgba(0,0,0,0.22); border: 1px solid rgba(255,255,255,0.05);"
                " border-radius: 8px; padding: 11px 14px;"));
            tv->addWidget(desc);
        }
        tv->addStretch(1);
        h->addWidget(textCol, 1);

        const bool onDisk = m_store && m_store->hasRecord(book.catalogueId);
        auto* btn = new QPushButton(row);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        if (onDisk) {
            btn->setText(QStringLiteral("Read"));
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background: #2d8a4e; color: #fff; border: none;"
                " border-radius: 4px; padding: 0 14px; font-size: 12px; }"
                "QPushButton:hover { background: #34a05c; }"));
            QString filePath;
            if (auto rec = m_store->recordFor(book.catalogueId)) filePath = rec->filePath;
            const QString catalogueId = book.catalogueId;
            connect(btn, &QPushButton::clicked, this, [this, catalogueId, filePath] {
                emit bookReadRequested(catalogueId, filePath);
            });
        } else {
            btn->setText(QStringLiteral("Get"));
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background: #8a6cff; color: #fff; border: none;"
                " border-radius: 4px; padding: 0 14px; font-size: 12px; }"
                "QPushButton:hover { background: #9d83ff; }"));
            const BookCatalogueResult b = book;
            connect(btn, &QPushButton::clicked, this, [this, b] {
                emit bookOpenRequested(b);
            });
        }
        h->addWidget(btn, 0, Qt::AlignTop);
        m_rowsLayout->addWidget(row);
    }
}
