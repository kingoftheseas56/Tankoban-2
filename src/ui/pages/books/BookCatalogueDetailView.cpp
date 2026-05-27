#include "BookCatalogueDetailView.h"

#include "core/TankorentSearchService.h"
#include "core/book/BookScraper.h"
#include "core/book/BookSearchAggregator.h"
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/LibGenScraper.h"
#include "core/book/TankorentBookScraper.h"

#include <QApplication>
#include <QFrame>
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
    m_catalogueStore = store;
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

    m_tankorentService = new TankorentSearchService(m_nam, this);
    m_sourceScrapers << new LibGenScraper(m_nam, this);
    m_sourceScrapers << new TankorentBookScraper(m_tankorentService, this);
    m_sourceAggregator = new BookSearchAggregator(m_sourceScrapers, this);

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
                renderSourceRow(section, result);
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
                                              const BookResult& result)
{
    auto* row = new QWidget(section.rows);
    row->setObjectName(QStringLiteral("BookDetailSourceRow"));
    row->setStyleSheet(QStringLiteral(
        "QWidget#BookDetailSourceRow { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.08); border-radius: 4px; }"));
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
