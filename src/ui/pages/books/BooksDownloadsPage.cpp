#include "BooksDownloadsPage.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"
#include "ui/pages/BooksPage.h"

namespace {

// Mirror BookCatalogueSearchWidget / BooksPage cover-cache path scheme so a
// cover downloaded by the storefront / series enrichment is found here too.
QString coverCachePath(const QString& dir, const QString& catalogueId)
{
    if (dir.isEmpty() || catalogueId.isEmpty()) return {};
    QString stem = catalogueId;
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                 QStringLiteral("_"));
    return dir + QLatin1Char('/') + stem + QStringLiteral(".jpg");
}

// Whole-row click target (same pattern as BookCatalogueDetailView's rows).
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

}  // namespace

BooksDownloadsPage::BooksDownloadsPage(QWidget* parent)
    : QFrame(parent)
{
    buildUi();
}

void BooksDownloadsPage::setCatalogueStore(BooksCatalogueLibraryStore* store)
{
    if (m_store == store) return;
    m_store = store;
    if (m_store)
        connect(m_store, &BooksCatalogueLibraryStore::recordsChanged,
                this, &BooksDownloadsPage::refresh);
    refresh();
}

void BooksDownloadsPage::setBooksPage(BooksPage* page)
{
    if (m_booksPage == page) return;
    m_booksPage = page;
    if (m_booksPage) {
        m_coverDir = m_booksPage->catalogueCoverDir();
        connect(m_booksPage, &BooksPage::downloadsChanged,
                this, &BooksDownloadsPage::refresh);
    }
    refresh();
}

void BooksDownloadsPage::buildUi()
{
    setObjectName(QStringLiteral("BooksDownloadsPage"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top bar — back.
    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    m_backBtn = new QPushButton(QStringLiteral("< Books"), topRow);
    m_backBtn->setObjectName(QStringLiteral("BooksDownloadsBackButton"));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(QStringLiteral(
        "QPushButton#BooksDownloadsBackButton { background: transparent; border: none;"
        " color: rgba(255,255,255,0.7); font-size: 13px; padding: 0 8px; }"
        "QPushButton#BooksDownloadsBackButton:hover { color: #ffffff; }"));
    connect(m_backBtn, &QPushButton::clicked, this, &BooksDownloadsPage::backRequested);
    topLayout->addWidget(m_backBtn);
    topLayout->addStretch(1);
    root->addWidget(topRow);

    auto* scroll = new QScrollArea(this);
    m_scroll = scroll;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget(scroll);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(24, 8, 24, 24);
    col->setSpacing(10);

    const QString headerCss = QStringLiteral(
        "color: rgba(255,255,255,0.55); font-size: 12px; font-weight: 700;"
        " letter-spacing: 1px; padding-top: 8px;");

    m_downloadingHeader = new QLabel(QStringLiteral("DOWNLOADING"), content);
    m_downloadingHeader->setStyleSheet(headerCss);
    col->addWidget(m_downloadingHeader);
    m_downloadingBody = new QWidget(content);
    m_downloadingLayout = new QVBoxLayout(m_downloadingBody);
    m_downloadingLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadingLayout->setSpacing(6);
    col->addWidget(m_downloadingBody);

    m_downloadedHeader = new QLabel(QStringLiteral("DOWNLOADED"), content);
    m_downloadedHeader->setStyleSheet(headerCss);
    col->addWidget(m_downloadedHeader);
    m_downloadedBody = new QWidget(content);
    m_downloadedLayout = new QVBoxLayout(m_downloadedBody);
    m_downloadedLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadedLayout->setSpacing(6);
    col->addWidget(m_downloadedBody);

    m_emptyState = new QLabel(QStringLiteral("No downloads yet."), content);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(QStringLiteral(
        "color: rgba(255,255,255,0.4); font-size: 14px; padding: 40px 0;"));
    col->addWidget(m_emptyState);

    col->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

QWidget* BooksDownloadsPage::makeRow(QWidget* parent, const QString& coverPath,
                                     const QString& title, const QString& subtitle,
                                     std::function<void()> onClick)
{
    auto* row = new ClickableRow(parent);
    row->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.04); border-radius: 6px;"));
    if (onClick) {
        row->onClick = std::move(onClick);
        row->setCursor(Qt::PointingHandCursor);
    }
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(12, 8, 12, 8);
    h->setSpacing(12);

    auto* thumb = new QLabel(row);
    thumb->setFixedSize(40, 60);
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.06); border-radius: 4px;"));
    if (!coverPath.isEmpty() && QFile::exists(coverPath)) {
        QPixmap pm(coverPath);
        if (!pm.isNull())
            thumb->setPixmap(pm.scaled(thumb->size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    }
    h->addWidget(thumb, 0, Qt::AlignTop);

    auto* textCol = new QWidget(row);
    auto* tv = new QVBoxLayout(textCol);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(2);
    auto* nameLabel = new QLabel(title, textCol);
    nameLabel->setWordWrap(true);
    nameLabel->setStyleSheet(QStringLiteral(
        "color: #ffffff; font-size: 14px; font-weight: 600; background: transparent;"));
    tv->addWidget(nameLabel);
    if (!subtitle.isEmpty()) {
        auto* subLabel = new QLabel(subtitle, textCol);
        subLabel->setStyleSheet(QStringLiteral(
            "color: rgba(255,255,255,0.55); font-size: 12px; background: transparent;"));
        tv->addWidget(subLabel);
    }
    tv->addStretch(1);
    h->addWidget(textCol, 1);
    return row;
}

int BooksDownloadsPage::populateDownloading()
{
    while (QLayoutItem* item = m_downloadingLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    int count = 0;
    if (m_booksPage) {
        const auto active = m_booksPage->activeDownloads();
        for (const auto& d : active) {
            const QString pctText = QStringLiteral("%1%").arg(d.percent);
            const QString sub = d.author.isEmpty()
                ? pctText
                : (d.author + QStringLiteral("  ·  ") + pctText);
            QString cover = (!d.coverPath.isEmpty() && QFile::exists(d.coverPath))
                ? d.coverPath : QString();
            if (cover.isEmpty()) {
                const QString cached = coverCachePath(m_coverDir, d.catalogueId);
                if (QFile::exists(cached)) cover = cached;
            }
            // In-progress rows are not clickable.
            m_downloadingLayout->addWidget(makeRow(m_downloadingBody, cover, d.title, sub));
            ++count;
        }
    }
    const bool any = count > 0;
    m_downloadingHeader->setVisible(any);
    m_downloadingBody->setVisible(any);
    return count;
}

int BooksDownloadsPage::populateDownloaded()
{
    while (QLayoutItem* item = m_downloadedLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    int count = 0;
    if (m_store) {
        QSet<QString> seriesSeen;
        for (const CatalogueRecord& r : m_store->all()) {
            if (!r.seriesId.isEmpty()) {
                if (seriesSeen.contains(r.seriesId)) continue;
                seriesSeen.insert(r.seriesId);
                const int owned = m_store->catalogueIdsForSeries(r.seriesId).size();
                const QString title = r.seriesName.isEmpty() ? r.title : r.seriesName;
                const QString sub = QStringLiteral("%1 book%2").arg(owned)
                                        .arg(owned == 1 ? QString() : QStringLiteral("s"));
                QString cover = QFile::exists(r.cachedCoverPath) ? r.cachedCoverPath : QString();
                if (cover.isEmpty()) {
                    const QString cached = coverCachePath(m_coverDir, r.catalogueId);
                    if (QFile::exists(cached)) cover = cached;
                }
                const QString seriesId = r.seriesId;
                m_downloadedLayout->addWidget(makeRow(m_downloadedBody, cover, title, sub,
                    [this, seriesId]() { emit openSeriesRequested(seriesId); }));
            } else {
                QString cover = QFile::exists(r.cachedCoverPath) ? r.cachedCoverPath : QString();
                if (cover.isEmpty()) {
                    const QString cached = coverCachePath(m_coverDir, r.catalogueId);
                    if (QFile::exists(cached)) cover = cached;
                }
                const QString filePath = r.filePath;
                m_downloadedLayout->addWidget(makeRow(m_downloadedBody, cover, r.title, r.author,
                    [this, filePath]() { if (!filePath.isEmpty()) emit openBookRequested(filePath); }));
            }
            ++count;
        }
    }
    const bool any = count > 0;
    m_downloadedHeader->setVisible(any);
    m_downloadedBody->setVisible(any);
    return count;
}

void BooksDownloadsPage::refresh()
{
    const int downloadingCount = populateDownloading();
    const int downloadedCount = populateDownloaded();
    if (m_emptyState)
        m_emptyState->setVisible(downloadingCount == 0 && downloadedCount == 0);
}
