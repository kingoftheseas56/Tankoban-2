#include "BookCatalogueDetailView.h"

#include "core/TankorentSearchService.h"
#include "core/book/BookScraper.h"
#include "core/book/BookSearchAggregator.h"
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/LibGenScraper.h"
#include "core/book/TankorentBookScraper.h"

#include <QApplication>
#include <QFrame>
#include <QMouseEvent>
#include <functional>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// §5.2 click-capture widget. Plain QWidget subclass — no Q_OBJECT so no moc
// generation needed, no signal plumbing. Caller assigns onClick; mouse-press
// invokes it. Used as the source-row root in renderSourceRow so the entire
// card is clickable + child QLabels still lay out normally (QPushButton with
// child layouts collapses to 0×0 when it has no text/icon — caught in smoke
// 2026-05-27 ~4:45pm when status said "Found 2 results" but rows didn't show).
class ClickableRow : public QWidget
{
public:
    explicit ClickableRow(QWidget* parent = nullptr) : QWidget(parent) {}
    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onClick) onClick();
        QWidget::mousePressEvent(e);
    }
};

QString joinedMeta(const BookCatalogueResult& book)
{
    QStringList parts;
    if (!book.year.isEmpty()) parts << book.year;
    if (!book.language.isEmpty()) parts << book.language.toUpper();
    if (!book.pages.isEmpty()) parts << book.pages + QStringLiteral(" pages");
    if (!book.genres.isEmpty()) parts << book.genres.mid(0, 4).join(QStringLiteral(" / "));
    return parts.join(QStringLiteral(" / "));
}

QString sourceRowMeta(const BookResult& result, const QString& fallbackAuthor)
{
    QStringList parts;
    const QString author = result.author.isEmpty() ? fallbackAuthor : result.author;
    if (!author.isEmpty()) parts << author;
    if (!result.format.isEmpty()) parts << result.format.toUpper();
    if (!result.fileSize.isEmpty()) parts << result.fileSize;
    if (!result.year.isEmpty()) parts << result.year;
    if (!result.language.isEmpty()) parts << result.language;
    return parts.join(QStringLiteral(" / "));
}

} // namespace

BookCatalogueDetailView::BookCatalogueDetailView(QWidget* parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    qRegisterMetaType<BookResult>("BookResult");
    qRegisterMetaType<QList<BookResult>>("QList<BookResult>");
    buildUi();
}

void BookCatalogueDetailView::buildUi()
{
    setObjectName(QStringLiteral("BookCatalogueDetailView"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 20);
    root->setSpacing(16);

    auto* actionRow = new QWidget(this);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);

    m_backButton = new QPushButton(QStringLiteral("< Back"), actionRow);
    m_backButton->setObjectName(QStringLiteral("BookDetailBackButton"));
    m_backButton->setFixedSize(116, 38);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setStyleSheet(QStringLiteral(
        "QPushButton#BookDetailBackButton { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.16); border-radius: 4px;"
        " color: #eeeeee; font-size: 14px; }"
        "QPushButton#BookDetailBackButton:hover { background: rgba(255,255,255,0.08); }"));
    connect(m_backButton, &QPushButton::clicked, this, &BookCatalogueDetailView::backRequested);
    actionLayout->addWidget(m_backButton, 0, Qt::AlignLeft);
    actionLayout->addStretch(1);

    // §5.2 (2026-05-27) — primary CTA. Three states:
    //   - Not in library, idle:        "Search for downloads" → re-fires picker
    //   - Download in flight:           "Downloading XX%"      → disabled
    //   - In library (record exists):   "Read"                 → emits readRequested
    m_primaryCta = new QPushButton(QStringLiteral("Search for downloads"), actionRow);
    m_primaryCta->setObjectName(QStringLiteral("BookDetailPrimaryCta"));
    m_primaryCta->setMinimumSize(180, 38);
    m_primaryCta->setCursor(Qt::PointingHandCursor);
    m_primaryCta->setStyleSheet(QStringLiteral(
        "QPushButton#BookDetailPrimaryCta {"
        " background: #8b5cf6; border: 1px solid #a78bfa; border-radius: 4px;"
        " color: #ffffff; font-size: 14px; font-weight: 600; padding: 0 18px; }"
        "QPushButton#BookDetailPrimaryCta:hover { background: #a78bfa; }"
        "QPushButton#BookDetailPrimaryCta:disabled {"
        " background: rgba(139,92,246,0.32); color: rgba(255,255,255,0.72);"
        " border-color: rgba(167,139,250,0.32); }"
        "QPushButton#BookDetailPrimaryCta[ctaState=\"read\"] {"
        " background: #1e293b; border-color: #334155; color: #f1f5f9; }"
        "QPushButton#BookDetailPrimaryCta[ctaState=\"read\"]:hover { background: #334155; }"));
    connect(m_primaryCta, &QPushButton::clicked,
            this, &BookCatalogueDetailView::onPrimaryCtaClicked);
    actionLayout->addWidget(m_primaryCta, 0, Qt::AlignRight);
    root->addWidget(actionRow);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget(scroll);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(34);

    auto* left = new QWidget(content);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(14);

    auto* hero = new QWidget(left);
    auto* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(20);

    m_coverLabel = new QLabel(hero);
    m_coverLabel->setObjectName(QStringLiteral("BookDetailCover"));
    m_coverLabel->setFixedSize(150, 230);
    m_coverLabel->setAlignment(Qt::AlignCenter);
    m_coverLabel->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailCover { background: rgba(255,255,255,0.05);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        " color: #888888; font-size: 32px; font-weight: bold; }"));
    heroLayout->addWidget(m_coverLabel, 0, Qt::AlignTop);

    auto* textCol = new QWidget(hero);
    auto* textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(8);

    m_titleLabel = new QLabel(textCol);
    m_titleLabel->setObjectName(QStringLiteral("BookDetailTitle"));
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailTitle { color: #eeeeee; font-size: 28px;"
        " font-weight: bold; background: transparent; }"));
    textLayout->addWidget(m_titleLabel);

    m_authorLabel = new QLabel(textCol);
    m_authorLabel->setObjectName(QStringLiteral("BookDetailAuthor"));
    m_authorLabel->setWordWrap(true);
    m_authorLabel->setTextFormat(Qt::PlainText);
    m_authorLabel->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailAuthor { color: #bbbbbb; font-size: 15px;"
        " background: transparent; }"));
    textLayout->addWidget(m_authorLabel);

    m_metaLabel = new QLabel(textCol);
    m_metaLabel->setObjectName(QStringLiteral("BookDetailMeta"));
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setTextFormat(Qt::PlainText);
    m_metaLabel->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailMeta { color: #8f8f8f; font-size: 13px;"
        " background: transparent; }"));
    textLayout->addWidget(m_metaLabel);
    textLayout->addStretch(1);

    heroLayout->addWidget(textCol, 1);
    leftLayout->addWidget(hero);

    m_descriptionLabel = new QLabel(left);
    m_descriptionLabel->setObjectName(QStringLiteral("BookDetailDescription"));
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setTextFormat(Qt::PlainText);
    m_descriptionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_descriptionLabel->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailDescription { color: #d6d6d6; font-size: 14px;"
        " line-height: 1.45; background: transparent; }"));
    leftLayout->addWidget(m_descriptionLabel);
    leftLayout->addStretch(1);

    contentLayout->addWidget(left, 3);

    auto* sources = new QWidget(content);
    sources->setObjectName(QStringLiteral("BookDetailSources"));
    sources->setMinimumWidth(430);
    sources->setMaximumWidth(560);
    sources->setStyleSheet(QStringLiteral(
        "QWidget#BookDetailSources { background: transparent; border: none; }"));
    auto* sourcesLayout = new QVBoxLayout(sources);
    sourcesLayout->setContentsMargins(0, 0, 0, 0);
    sourcesLayout->setSpacing(10);

    auto* sourcesTitle = new QLabel(QStringLiteral("Sources"), sources);
    sourcesTitle->setObjectName(QStringLiteral("BookDetailSourcesTitle"));
    sourcesTitle->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailSourcesTitle { color: #eeeeee; font-size: 16px;"
        " font-weight: bold; background: transparent; }"));
    sourcesLayout->addWidget(sourcesTitle);

    m_sourceStatus = new QLabel(sources);
    m_sourceStatus->setObjectName(QStringLiteral("BookDetailSourceStatus"));
    m_sourceStatus->setWordWrap(true);
    m_sourceStatus->setTextFormat(Qt::PlainText);
    m_sourceStatus->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailSourceStatus { color: #999999; font-size: 13px;"
        " background: transparent; }"));
    sourcesLayout->addWidget(m_sourceStatus);

    const QStringList sourceIds = {
        QStringLiteral("libgen"),
        QStringLiteral("tankorent"),
    };
    for (const QString& sourceId : sourceIds) {
        SourceSection section;
        section.container = new QWidget(sources);
        auto* sectionLayout = new QVBoxLayout(section.container);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(5);

        section.heading = new QLabel(bookSourceDisplayName(sourceId), section.container);
        section.heading->setObjectName(QStringLiteral("BookDetailSourceHeading"));
        section.heading->setStyleSheet(QStringLiteral(
            "QLabel#BookDetailSourceHeading { color: #cccccc; font-size: 13px;"
            " font-weight: bold; background: transparent; padding-top: 6px; }"));
        sectionLayout->addWidget(section.heading);

        section.rows = new QWidget(section.container);
        auto* rowsLayout = new QVBoxLayout(section.rows);
        rowsLayout->setContentsMargins(0, 0, 0, 0);
        rowsLayout->setSpacing(5);
        sectionLayout->addWidget(section.rows);

        section.container->hide();
        sourcesLayout->addWidget(section.container);
        m_sourceSections.insert(sourceId, section);
    }

    sourcesLayout->addStretch(1);
    contentLayout->addWidget(sources, 2);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

void BookCatalogueDetailView::setCatalogueStore(BooksCatalogueLibraryStore* store)
{
    if (m_catalogueStore == store) return;
    if (m_catalogueStore) {
        disconnect(m_catalogueStore, nullptr, this, nullptr);
    }
    m_catalogueStore = store;
    if (m_catalogueStore) {
        // §5.2: CTA needs to morph to [Read] when a record lands for the
        // currently-shown book (download-complete path upserts via BooksPage).
        connect(m_catalogueStore, &BooksCatalogueLibraryStore::recordsChanged,
                this, &BookCatalogueDetailView::refreshPrimaryCta);
    }
    refreshPrimaryCta();
}

void BookCatalogueDetailView::showBook(const BookCatalogueResult& book,
                                       const QString& coverPath)
{
    m_currentBook = book;
    m_currentCatalogueId = book.catalogueId;
    m_currentCoverPath = coverPath;
    ++m_generation;

    m_titleLabel->setText(book.title.isEmpty() ? QStringLiteral("Untitled") : book.title);
    m_authorLabel->setText(book.author.isEmpty()
        ? QStringLiteral("Unknown author")
        : QStringLiteral("by %1").arg(book.author));

    const QString meta = joinedMeta(book);
    m_metaLabel->setText(meta);
    m_metaLabel->setVisible(!meta.isEmpty());

    m_descriptionLabel->setText(book.description.trimmed());
    m_descriptionLabel->setVisible(!book.description.trimmed().isEmpty());

    setCover(coverPath);
    resetSourceSections();
    startSourceSearch();

    // §5.2: each book-show resets per-book download state. Existing in-flight
    // downloads for a different book remain in BooksPage's tracker; the CTA
    // just doesn't reflect them here.
    m_downloadInFlight = false;
    m_downloadPct = 0;
    m_activeHandle.clear();
    m_activeFilePath.clear();
    refreshPrimaryCta();
}

void BookCatalogueDetailView::setCover(const QString& coverPath)
{
    QPixmap pix(coverPath);
    if (!pix.isNull()) {
        m_coverLabel->setPixmap(pix.scaled(m_coverLabel->size(),
                                           Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation));
        m_coverLabel->setText(QString());
        return;
    }

    QString initial;
    for (const QChar& ch : m_currentBook.title) {
        if (ch.isLetter()) {
            initial = ch.toUpper();
            break;
        }
    }
    m_coverLabel->setPixmap(QPixmap());
    m_coverLabel->setText(initial.isEmpty() ? QStringLiteral("?") : initial);
}

void BookCatalogueDetailView::resetSourceSections()
{
    for (auto it = m_sourceSections.begin(); it != m_sourceSections.end(); ++it) {
        SourceSection& section = it.value();
        clearRows(section.rows);
        section.resultCount = 0;
        section.heading->setText(bookSourceDisplayName(it.key()));
        section.container->hide();
    }
    m_sourceStatus->setText(QStringLiteral("Searching sources..."));
    m_sourceStatus->show();
}

void BookCatalogueDetailView::startSourceSearch()
{
    const int generation = m_generation;
    recreateSourceAggregator(generation);
    if (m_sourceAggregator) {
        m_sourceAggregator->searchFor(m_currentBook);
    }
}

void BookCatalogueDetailView::recreateSourceAggregator(int generation)
{
    delete m_sourceAggregator;
    m_sourceAggregator = nullptr;
    qDeleteAll(m_sourceScrapers);
    m_sourceScrapers.clear();
    delete m_tankorentService;
    m_tankorentService = nullptr;
    m_pendingResolves.clear();

    m_tankorentService = new TankorentSearchService(m_nam, this);
    m_sourceScrapers << new LibGenScraper(m_nam, this);
    m_sourceScrapers << new TankorentBookScraper(m_tankorentService, this);
    m_sourceAggregator = new BookSearchAggregator(m_sourceScrapers, this);

    // §5.2 resolve-then-download bridge — listen for fresh URLs coming back
    // from scraper->resolveDownload(md5). LibGen's key rotates ~60s so this
    // step is mandatory for the HTTP path; without it BookDownloader sees a
    // stale URL and the download hangs at 0%.
    for (BookScraper* scraper : m_sourceScrapers) {
        if (!scraper) continue;
        connect(scraper, &BookScraper::downloadResolved,
                this, [this](const QString& md5, const QStringList& urls) {
            auto it = m_pendingResolves.find(md5);
            if (it == m_pendingResolves.end()) return;
            const PendingResolve ctx = it.value();
            m_pendingResolves.erase(it);
            if (urls.isEmpty()) {
                m_downloadInFlight = false;
                refreshPrimaryCta();
                if (m_sourceStatus) m_sourceStatus->setText(
                    QStringLiteral("Source returned no download URL."));
                return;
            }
            BookResult resolved = ctx.result;
            resolved.downloadUrl = urls.first();
            // Pass the FULL mirror list so BookDownloader's intra-row failover
            // actually fires when mirror #1 returns 404 / stale-HTML / network
            // error. Without this, the entire row reports "all mirrors failed"
            // even though we never tried mirrors #2-#5 — root cause of the
            // earlier "Download failed: all mirror URLs failed" smoke finding
            // 2026-05-27 ~5:00pm.
            emit downloadRequested(ctx.sourceId, resolved, urls,
                                   m_currentBook, m_currentCoverPath);
        });
        connect(scraper, &BookScraper::downloadFailed,
                this, [this](const QString& md5, const QString& reason) {
            auto it = m_pendingResolves.find(md5);
            if (it == m_pendingResolves.end()) return;
            m_pendingResolves.erase(it);
            m_downloadInFlight = false;
            refreshPrimaryCta();
            if (m_sourceStatus) m_sourceStatus->setText(
                QStringLiteral("Source resolve failed: %1").arg(reason));
        });
    }

    connect(m_sourceAggregator, &BookSearchAggregator::sourceResultsReady,
            this, [this, generation](const QString& sourceId,
                                     const QList<BookResult>& results) {
        if (generation != m_generation) return;
        auto it = m_sourceSections.find(sourceId);
        if (it == m_sourceSections.end()) return;

        SourceSection& section = it.value();
        clearRows(section.rows);
        section.resultCount = 0;
        if (results.isEmpty()) {
            section.heading->setText(QStringLiteral("%1 (0 results)")
                .arg(bookSourceDisplayName(sourceId)));
            renderSourceMessage(section, sourceId, QStringLiteral("No matches"));
        } else {
            for (const BookResult& result : results) {
                renderSourceRow(section, sourceId, result);
            }
            section.heading->setText(QStringLiteral("%1 (%2 results)")
                .arg(bookSourceDisplayName(sourceId))
                .arg(section.resultCount));
        }
        section.container->show();
    });

    connect(m_sourceAggregator, &BookSearchAggregator::sourceFailed,
            this, [this, generation](const QString& sourceId, const QString& error) {
        if (generation != m_generation) return;
        auto it = m_sourceSections.find(sourceId);
        if (it == m_sourceSections.end()) return;

        SourceSection& section = it.value();
        clearRows(section.rows);
        section.resultCount = 0;
        section.heading->setText(QStringLiteral("%1 (error)")
            .arg(bookSourceDisplayName(sourceId)));
        renderSourceMessage(section, sourceId, error);
        section.container->show();
    });

    connect(m_sourceAggregator, &BookSearchAggregator::allSourcesCompleted,
            this, [this, generation]() {
        if (generation != m_generation) return;

        int resultSources = 0;
        for (const SourceSection& section : m_sourceSections) {
            if (section.resultCount > 0) ++resultSources;
        }

        if (resultSources > 0) {
            m_sourceStatus->setText(QStringLiteral("Found %1 source(s) with downloadable matches.")
                .arg(resultSources));
        } else {
            m_sourceStatus->setText(QStringLiteral("No downloadable matches found."));
        }
    });
}

void BookCatalogueDetailView::renderSourceRow(SourceSection& section,
                                              const QString& sourceId,
                                              const BookResult& result)
{
    // §5.2 (2026-05-27) — row is a ClickableRow QWidget subclass so the entire
    // card is clickable + child QLabels lay out normally. (Previous attempt
    // used QPushButton-as-card, but QPushButton without text/icon has
    // sizeHint = 0 and the button collapsed to 0×0 while child labels never
    // got rendered space — smoke caught it at 4:45pm.)
    auto* row = new ClickableRow(section.rows);
    row->setObjectName(QStringLiteral("BookDetailSourceRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setAttribute(Qt::WA_StyledBackground, true);  // styleSheet bg renders on QWidget
    row->setStyleSheet(QStringLiteral(
        "QWidget#BookDetailSourceRow {"
        " background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.08); border-radius: 4px; }"
        "QWidget#BookDetailSourceRow:hover {"
        " background: rgba(255,255,255,0.08); border-color: rgba(167,139,250,0.6); }"));
    auto* rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(10, 8, 10, 8);
    rowLayout->setSpacing(4);

    auto* title = new QLabel(result.title.isEmpty()
        ? QStringLiteral("(untitled)")
        : result.title, row);
    title->setObjectName(QStringLiteral("BookDetailSourceRowTitle"));
    title->setWordWrap(true);
    title->setTextFormat(Qt::PlainText);
    title->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailSourceRowTitle { color: #eeeeee; font-size: 13px;"
        " font-weight: bold; background: transparent; }"));
    rowLayout->addWidget(title);

    const QString meta = sourceRowMeta(result, m_currentBook.author);
    auto* details = new QLabel(meta, row);
    details->setObjectName(QStringLiteral("BookDetailSourceRowMeta"));
    details->setWordWrap(true);
    details->setTextFormat(Qt::PlainText);
    details->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailSourceRowMeta { color: #999999; font-size: 12px;"
        " background: transparent; }"));
    details->setVisible(!meta.isEmpty());
    rowLayout->addWidget(details);

    // §5.2 click-to-download. Capture sourceId + result by value so the
    // closure stays valid after recreateSourceAggregator clears m_sourceScrapers.
    // Dispatch through handleSourceRowClick which inserts the
    // scraper.resolveDownload step for HTTP sources (LibGen / Anna's).
    row->onClick = [this, sourceId, result]() {
        handleSourceRowClick(sourceId, result);
    };

    section.rows->layout()->addWidget(row);
    ++section.resultCount;
}

void BookCatalogueDetailView::renderSourceMessage(SourceSection& section,
                                                  const QString&,
                                                  const QString& message)
{
    auto* label = new QLabel(message, section.rows);
    label->setObjectName(QStringLiteral("BookDetailSourceMessage"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet(QStringLiteral(
        "QLabel#BookDetailSourceMessage { color: #8f8f8f; font-size: 12px;"
        " background: transparent; padding: 6px 0; }"));
    section.rows->layout()->addWidget(label);
}

// ── §5.2 CTA state machine + download lifecycle (2026-05-27) ──

void BookCatalogueDetailView::refreshPrimaryCta()
{
    if (!m_primaryCta) return;

    const bool noBook = m_currentCatalogueId.isEmpty();
    m_primaryCta->setVisible(!noBook);
    if (noBook) return;

    const bool inLibrary = m_catalogueStore
        && m_catalogueStore->hasRecord(m_currentCatalogueId);

    if (m_downloadInFlight) {
        m_primaryCta->setText(QStringLiteral("Downloading %1%").arg(m_downloadPct));
        m_primaryCta->setEnabled(false);
        m_primaryCta->setProperty("ctaState", QStringLiteral("downloading"));
    } else if (inLibrary) {
        m_primaryCta->setText(QStringLiteral("Read"));
        m_primaryCta->setEnabled(true);
        m_primaryCta->setProperty("ctaState", QStringLiteral("read"));
    } else {
        m_primaryCta->setText(QStringLiteral("Search for downloads"));
        m_primaryCta->setEnabled(true);
        m_primaryCta->setProperty("ctaState", QStringLiteral("idle"));
    }
    m_primaryCta->style()->unpolish(m_primaryCta);
    m_primaryCta->style()->polish(m_primaryCta);
}

void BookCatalogueDetailView::onPrimaryCtaClicked()
{
    if (m_currentCatalogueId.isEmpty()) return;

    const bool inLibrary = m_catalogueStore
        && m_catalogueStore->hasRecord(m_currentCatalogueId);

    if (inLibrary) {
        // [Read] path — emit catalogueId + filePath. Look up filePath from
        // the live record so it stays correct after move/rename.
        QString filePath = m_activeFilePath;
        if (filePath.isEmpty() && m_catalogueStore) {
            const auto rec = m_catalogueStore->recordFor(m_currentCatalogueId);
            if (rec) filePath = rec->filePath;
        }
        emit readRequested(m_currentCatalogueId, filePath);
        return;
    }

    if (m_downloadInFlight) return;

    // [Search for downloads] re-fires the picker — useful if results were
    // empty on first attempt + the user wants to retry. The auto-search
    // already runs on showBook; this is the manual re-run path.
    resetSourceSections();
    startSourceSearch();
}

void BookCatalogueDetailView::handleSourceRowClick(const QString& sourceId,
                                                  const BookResult& result)
{
    if (m_downloadInFlight) return;  // single-flight per detail view

    if (sourceId == QLatin1String("tankorent")) {
        // Magnet path — downloadUrl carries the magnet URI directly, no
        // scraper.resolveDownload step needed (TankorentBookScraper.cpp:65
        // already stuffs the magnet into BookResult.downloadUrl at search
        // time). Magnet "mirror list" = single-element list with the magnet.
        m_downloadInFlight = true;
        m_downloadPct = 0;
        m_activeHandle.clear();
        refreshPrimaryCta();
        emit downloadRequested(sourceId, result,
                               QStringList{result.downloadUrl},
                               m_currentBook, m_currentCoverPath);
        return;
    }

    // HTTP path (libgen / annas-archive). Find the matching scraper, fire
    // resolveDownload, register the click context, wait for downloadResolved
    // (handled in recreateSourceAggregator's connect block above).
    BookScraper* scraper = nullptr;
    for (BookScraper* s : m_sourceScrapers) {
        if (s && s->sourceId() == sourceId) { scraper = s; break; }
    }
    if (!scraper || result.md5.isEmpty()) {
        if (m_sourceStatus) m_sourceStatus->setText(
            QStringLiteral("Source unavailable — pick another row."));
        return;
    }

    m_downloadInFlight = true;
    m_downloadPct = 0;
    m_activeHandle.clear();
    refreshPrimaryCta();
    if (m_sourceStatus) m_sourceStatus->setText(
        QStringLiteral("Resolving download URL from %1…")
            .arg(scraper->sourceName()));

    m_pendingResolves.insert(result.md5, {sourceId, result});
    scraper->resolveDownload(result.md5);
}

void BookCatalogueDetailView::notifyDownloadStarted(const QString& handle)
{
    m_downloadInFlight = true;
    m_downloadPct = 0;
    m_activeHandle = handle;
    refreshPrimaryCta();
    if (m_sourceStatus) {
        m_sourceStatus->setText(QStringLiteral("Downloading from source…"));
    }
}

void BookCatalogueDetailView::notifyDownloadProgress(const QString& handle, int pct)
{
    if (handle != m_activeHandle) return;
    m_downloadPct = qBound(0, pct, 100);
    refreshPrimaryCta();
}

void BookCatalogueDetailView::notifyDownloadComplete(const QString& handle,
                                                     const QString& filePath)
{
    if (handle != m_activeHandle) return;
    m_downloadInFlight = false;
    m_downloadPct = 100;
    m_activeFilePath = filePath;
    m_activeHandle.clear();
    // recordsChanged from the store also drives refreshPrimaryCta; this is
    // the redundant local refresh in case the store mutation lands on a
    // different signal ordering.
    refreshPrimaryCta();
    if (m_sourceStatus) {
        m_sourceStatus->setText(QStringLiteral("Downloaded — opening in reader…"));
    }
}

void BookCatalogueDetailView::notifyDownloadFailed(const QString& handle,
                                                   const QString& reason)
{
    if (handle != m_activeHandle) return;
    m_downloadInFlight = false;
    m_downloadPct = 0;
    m_activeHandle.clear();
    refreshPrimaryCta();
    if (m_sourceStatus) {
        m_sourceStatus->setText(
            QStringLiteral("Download failed: %1 — pick another source.").arg(reason));
    }
}

void BookCatalogueDetailView::clearRows(QWidget* rows)
{
    if (!rows || !rows->layout()) return;
    QLayoutItem* item = nullptr;
    while ((item = rows->layout()->takeAt(0)) != nullptr) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}
