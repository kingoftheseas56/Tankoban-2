#include "BookSeriesDetailView.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"

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

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget(scroll);
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
    m_coverLabel->setFixedSize(120, 180);
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

    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

void BookSeriesDetailView::showSeries(const QString& seriesName, const QString& author,
                                      const QString& coverPath,
                                      const QList<BookCatalogueResult>& books)
{
    m_seriesName = seriesName;
    m_author = author;
    m_coverPath = coverPath;
    m_books = books;

    m_titleLabel->setText(seriesName);
    QStringList metaParts;
    if (!author.isEmpty()) metaParts << author;
    metaParts << QStringLiteral("%1 book%2").arg(books.size())
                                            .arg(books.size() == 1 ? QString() : QStringLiteral("s"));
    m_metaLabel->setText(metaParts.join(QStringLiteral("  ·  ")));

    if (!coverPath.isEmpty() && QFile::exists(coverPath)) {
        QPixmap pm(coverPath);
        if (!pm.isNull())
            m_coverLabel->setPixmap(pm.scaled(m_coverLabel->size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
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
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(12, 8, 12, 8);
        h->setSpacing(12);

        const QString label = book.seriesPosition > 0
            ? QStringLiteral("%1.  %2").arg(book.seriesPosition).arg(book.title)
            : book.title;
        auto* name = new QLabel(label, row);
        name->setWordWrap(true);
        name->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 14px; background: transparent;"));
        h->addWidget(name, 1);

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
        h->addWidget(btn, 0);
        m_rowsLayout->addWidget(row);
    }
}
