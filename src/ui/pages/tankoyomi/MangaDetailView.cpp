// src/ui/pages/tankoyomi/MangaDetailView.cpp
#include "MangaDetailView.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QTableWidget>
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

    QWidget::show();
}

// ---------------------------------------------------------------------
// C.1 slot stubs -- empty bodies satisfy MOC qt_static_metacall linkage.
// Real implementations land in C.2 (show), C.3 (onChaptersReady +
// onScraperError + renderChapters), C.4 (onChapterUpdated).
// ---------------------------------------------------------------------
void MangaDetailView::onChaptersReady(const QList<ChapterInfo>& /*chapters*/) {}
void MangaDetailView::onScraperError(const QString& /*message*/) {}
void MangaDetailView::onChapterUpdated(const QString& /*seriesId*/,
                                       const QString& /*chapterId*/) {}

