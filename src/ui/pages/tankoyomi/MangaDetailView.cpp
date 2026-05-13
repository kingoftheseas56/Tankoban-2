// src/ui/pages/tankoyomi/MangaDetailView.cpp
#include "MangaDetailView.h"

#include <QDateTime>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>

#include "ChapterDownloadIndicator.h"
#include "core/manga/MangaDownloader.h"
#include "core/manga/MangaScraper.h"

MangaDetailView::MangaDetailView(QWidget* parent) : QWidget(parent)
{
    setObjectName("MangaDetailView");
    buildUI();
}

void MangaDetailView::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(10);

    // -- Top bar: back button + title + overflow ----------------------
    auto* topRow = new QHBoxLayout();
    m_backBtn = new QPushButton(tr("< back"), this);
    m_backBtn->setObjectName("MangaDetailBackBtn");
    connect(m_backBtn, &QPushButton::clicked,
            this, &MangaDetailView::backRequested);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("MangaDetailTitle");
    m_overflowBtn = new QToolButton(this);
    m_overflowBtn->setText(QString::fromUtf8("\xE2\x8B\xAF"));  // U+22EF horizontal ellipsis; F.4 replaces with SVG
    m_overflowBtn->setObjectName("MangaDetailOverflow");
    m_overflowBtn->setPopupMode(QToolButton::InstantPopup);
    topRow->addWidget(m_backBtn);
    topRow->addWidget(m_titleLabel, 1);
    topRow->addWidget(m_overflowBtn);
    root->addLayout(topRow);

    // -- Hero block: cover (left) + meta (right) ----------------------
    auto* heroRow = new QHBoxLayout();
    m_coverLabel = new QLabel(this);
    m_coverLabel->setObjectName("MangaDetailCover");
    m_coverLabel->setFixedSize(140, 200);
    m_coverLabel->setScaledContents(true);
    heroRow->addWidget(m_coverLabel);

    auto* metaCol = new QVBoxLayout();
    metaCol->setSpacing(4);
    m_authorLabel = new QLabel(this);
    m_authorLabel->setObjectName("MangaDetailAuthor");
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("MangaDetailStatus");
    m_chapterCount = new QLabel(this);
    m_chapterCount->setObjectName("MangaDetailChapterCount");
    metaCol->addWidget(m_authorLabel);
    metaCol->addWidget(m_statusLabel);
    metaCol->addWidget(m_chapterCount);
    metaCol->addStretch();

    // Action row (Download dropdown + ellipsis) -- D.3 fills the dropdown menu
    auto* actionRow = new QHBoxLayout();
    m_downloadDropdown = new QToolButton(this);
    m_downloadDropdown->setText(tr("Download v"));
    m_downloadDropdown->setObjectName("MangaDetailDownloadDropdown");
    m_downloadDropdown->setPopupMode(QToolButton::InstantPopup);
    actionRow->addWidget(m_downloadDropdown);
    actionRow->addStretch();
    metaCol->addLayout(actionRow);

    heroRow->addLayout(metaCol, 1);
    root->addLayout(heroRow);

    // -- Multi-select bar (hidden by default) -------------------------
    m_multiSelectBar = new QWidget(this);
    m_multiSelectBar->setObjectName("MangaDetailMultiSelectBar");
    auto* msLayout = new QHBoxLayout(m_multiSelectBar);
    msLayout->setContentsMargins(8, 6, 8, 6);
    m_multiSelectLabel = new QLabel(m_multiSelectBar);
    m_msDownloadBtn = new QPushButton(tr("Download"), m_multiSelectBar);
    m_msDeleteBtn   = new QPushButton(tr("Delete"),   m_multiSelectBar);
    m_msClearBtn    = new QPushButton(tr("Clear"),    m_multiSelectBar);
    msLayout->addWidget(m_multiSelectLabel, 1);
    msLayout->addWidget(m_msDownloadBtn);
    msLayout->addWidget(m_msDeleteBtn);
    msLayout->addWidget(m_msClearBtn);
    m_multiSelectBar->hide();
    root->addWidget(m_multiSelectBar);

    // -- Chapter table ------------------------------------------------
    m_chapterTable = new QTableWidget(0, 4, this);
    m_chapterTable->setObjectName("MangaDetailChapterTable");
    m_chapterTable->setHorizontalHeaderLabels(
        {tr("#"), tr("Chapter"), tr("Date"), tr("")});
    m_chapterTable->horizontalHeader()->setStretchLastSection(false);
    m_chapterTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_chapterTable->verticalHeader()->hide();
    m_chapterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_chapterTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_chapterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chapterTable->setShowGrid(false);
    m_chapterTable->setContextMenuPolicy(Qt::CustomContextMenu);  // F.2 wires
    root->addWidget(m_chapterTable, 1);

    // -- Loading + error labels (hidden by default) -------------------
    m_loadingLabel = new QLabel(tr("Loading chapters..."), this);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_loadingLabel->hide();
    root->addWidget(m_loadingLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setObjectName("MangaDetailError");
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);
}

void MangaDetailView::show(const MangaResult& result, const QString& coverPath)
{
    m_result = result;
    m_chapters.clear();
    m_selectedChapterIds.clear();
    m_chapterTable->setRowCount(0);

    m_titleLabel->setText(result.title);
    m_authorLabel->setText(result.author.isEmpty() ? tr("—") : result.author);

    const QString status = result.status.isEmpty() ? tr("Unknown") : result.status;
    const QString srcLabel = mangaSourceDisplayName(result.source);
    m_statusLabel->setText(QStringLiteral("%1 · %2").arg(status, srcLabel));

    // Cover
    if (!coverPath.isEmpty() && QFile::exists(coverPath)) {
        m_coverLabel->setPixmap(QPixmap(coverPath));
    } else {
        m_coverLabel->setText(tr("(no cover)"));
        m_coverLabel->setAlignment(Qt::AlignCenter);
    }

    // Chapter count placeholder until scraper returns; C.3 fills in
    m_chapterCount->setText(tr("Loading chapters..."));

    // Enter Loading state
    m_chapterTable->hide();
    m_loadingLabel->show();
    m_errorLabel->hide();

    if (m_scraper) {
        // Disconnect any prior receivers in case show() called twice
        disconnect(m_scraper, &MangaScraper::chaptersReady, this, nullptr);
        disconnect(m_scraper, &MangaScraper::errorOccurred, this, nullptr);

        connect(m_scraper, &MangaScraper::chaptersReady,
                this, &MangaDetailView::onChaptersReady,
                Qt::UniqueConnection);
        connect(m_scraper, &MangaScraper::errorOccurred,
                this, &MangaDetailView::onScraperError,
                Qt::UniqueConnection);

        m_scraper->fetchChapters(result.id);
    }

    QWidget::show();
}

void MangaDetailView::setScraper(MangaScraper* s) { m_scraper = s; }

// ---------------------------------------------------------------------
// C.1 slot stubs -- empty bodies satisfy MOC qt_static_metacall linkage.
// C.3 fills in onChaptersReady + onScraperError + renderChapters +
// onChapterIconClicked. C.4 will fill onChapterUpdated.
// ---------------------------------------------------------------------
void MangaDetailView::onChaptersReady(const QList<ChapterInfo>& chapters)
{
    m_chapters = chapters;
    m_loadingLabel->hide();
    m_chapterTable->show();
    renderChapters();

    // Update chapter-count label with downloaded count (Phase A.7)
    int downloaded = 0;
    if (m_downloader) {
        downloaded = m_downloader->countDownloadedForSeries(
            m_result.title, m_result.source);
    }
    m_chapterCount->setText(tr("%1 chapters · %2 downloaded")
        .arg(chapters.size()).arg(downloaded));
}

void MangaDetailView::onScraperError(const QString& message)
{
    m_loadingLabel->hide();
    m_chapterTable->hide();
    m_errorLabel->setText(tr("Could not load chapters: %1").arg(message));
    m_errorLabel->show();
}

void MangaDetailView::onChapterUpdated(const QString& /*seriesId*/,
                                       const QString& /*chapterId*/) {}

void MangaDetailView::renderChapters()
{
    m_chapterTable->setRowCount(m_chapters.size());
    for (int i = 0; i < m_chapters.size(); ++i) {
        const ChapterInfo& ch = m_chapters[i];

        auto* numItem = new QTableWidgetItem(
            QStringLiteral("Ch %1").arg(ch.chapterNumber, 0, 'f', 1));
        m_chapterTable->setItem(i, 0, numItem);

        auto* nameItem = new QTableWidgetItem(ch.name);
        m_chapterTable->setItem(i, 1, nameItem);

        const QString dateStr = ch.dateUpload > 0
            ? QDateTime::fromMSecsSinceEpoch(ch.dateUpload).toString("yyyy-MM-dd")
            : QString();
        auto* dateItem = new QTableWidgetItem(dateStr);
        m_chapterTable->setItem(i, 2, dateItem);

        // Per-chapter download indicator
        auto* indicator = new ChapterDownloadIndicator();
        m_chapterTable->setCellWidget(i, 3, indicator);

        // Initial state — C.4 will derive from m_downloader records;
        // for C.3 default to NotDownloaded.
        indicator->setState(ChapterDownloadIndicator::State::NotDownloaded);

        connect(indicator, &ChapterDownloadIndicator::clicked, this,
                [this, i]() { onChapterIconClicked(i); });
    }
    m_chapterTable->resizeColumnToContents(0);
    m_chapterTable->resizeColumnToContents(2);
    m_chapterTable->resizeColumnToContents(3);
}

void MangaDetailView::onChapterIconClicked(int row)
{
    if (row < 0 || row >= m_chapters.size()) return;
    if (!m_downloader || !m_destProvider) return;

    const ChapterInfo& ch = m_chapters[row];
    auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
        m_chapterTable->cellWidget(row, 3));
    if (!indicator) return;

    using State = ChapterDownloadIndicator::State;
    switch (indicator->state()) {
        case State::NotDownloaded:
        case State::Errored:
            // Enqueue (start fresh, or retry from errored)
            m_downloader->startDownload(m_result.title, m_result.source,
                                         {ch}, m_destProvider(),
                                         QStringLiteral("cbz"));
            break;
        case State::Queued:
        case State::Downloading:
            // Cancel single chapter — D.2 / F.2 wire the per-chapter cancel.
            // For C.3, no-op (the bar/menu handle cancellation paths).
            break;
        case State::Downloaded:
            // F.2 wires the Delete confirm popover; for C.3, no-op.
            break;
    }
}

