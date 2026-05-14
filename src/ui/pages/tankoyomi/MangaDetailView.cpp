// src/ui/pages/tankoyomi/MangaDetailView.cpp
#include "MangaDetailView.h"

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QHeaderView>

#include <algorithm>
#include <climits>
#include <utility>

#include "ChapterDownloadIndicator.h"
#include "ChapterRangeDialog.h"
#include "core/manga/MangaDownloader.h"
#include "core/manga/MangaScraper.h"

namespace {
// Mirror of StreamDetailView.cpp:53-60 column-constants pattern. Tankoyomi's
// 2-column model: title (stretch) + action (fixed). Hemanth flagged the
// # and Date columns as dead in post-overhaul smoke; the per-chapter
// download indicator is already the only meaningful action surface.
constexpr int kColTitle    = 0;
constexpr int kColAction   = 1;
constexpr int kColumnCount = 2;

}  // namespace

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
    m_backBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x90 Back"), this);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
        "  font-size: 13px; padding: 0 8px; }"
        "#SidebarAction:hover { color: #fff; }");
    connect(m_backBtn, &QPushButton::clicked,
            this, &MangaDetailView::backRequested);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("MangaDetailTitle");
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet("color: #e0e0e0; font-size: 16px; font-weight: bold;");
    m_overflowBtn = new QToolButton(this);
    m_overflowBtn->setText(QString::fromUtf8("\xE2\x8B\xAF"));  // U+22EF horizontal ellipsis; F.4 replaces with SVG
    m_overflowBtn->setObjectName("MangaDetailOverflow");
    m_overflowBtn->setPopupMode(QToolButton::InstantPopup);

    auto* overflowMenu = new QMenu(m_overflowBtn);

    auto* refreshAct = overflowMenu->addAction(tr("Refresh chapter list"));
    connect(refreshAct, &QAction::triggered, this, [this]() {
        if (m_scraper && !m_result.id.isEmpty()) {
            m_loadingLabel->show();
            m_chapterTable->hide();
            m_errorLabel->hide();
            m_scraper->fetchChapters(m_result.id);
        }
    });

    auto* browserAct = overflowMenu->addAction(tr("Open source page in browser"));
    connect(browserAct, &QAction::triggered, this, [this]() {
        if (!m_result.url.isEmpty())
            QDesktopServices::openUrl(QUrl(m_result.url));
    });

    auto* showFolderAct = overflowMenu->addAction(tr("Show series folder"));
    connect(showFolderAct, &QAction::triggered, this, [this]() {
        emit showInFolderRequested(m_result.title, m_result.source);
    });

    overflowMenu->addSeparator();

    auto* copyTitleAct = overflowMenu->addAction(tr("Copy series title"));
    connect(copyTitleAct, &QAction::triggered, this, [this]() {
        QGuiApplication::clipboard()->setText(m_result.title);
    });
    auto* copyUrlAct = overflowMenu->addAction(tr("Copy source URL"));
    connect(copyUrlAct, &QAction::triggered, this, [this]() {
        QGuiApplication::clipboard()->setText(m_result.url);
    });

    m_overflowBtn->setMenu(overflowMenu);

    // Enable/disable "Show series folder" based on whether any chapter is downloaded
    connect(overflowMenu, &QMenu::aboutToShow, this, [this, showFolderAct]() {
        const int n = m_downloader
            ? m_downloader->countDownloadedForSeries(m_result.title, m_result.source)
            : 0;
        showFolderAct->setEnabled(n > 0);
    });

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
    metaCol->setSpacing(6);

    m_metaLine = new QLabel(this);
    m_metaLine->setObjectName("MangaDetailMetaLine");
    m_metaLine->setWordWrap(true);
    m_metaLine->setStyleSheet(
        "QLabel#MangaDetailMetaLine {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.62);"
        "  font-size: 12px;"
        "  font-weight: 400;"
        "}");
    metaCol->addWidget(m_metaLine);
    metaCol->addStretch();

    // Action row (Download dropdown + ellipsis) -- D.3 fills the dropdown menu
    auto* actionRow = new QHBoxLayout();
    m_downloadDropdown = new QToolButton(this);
    m_downloadDropdown->setText(QString::fromUtf8("Download \xE2\x96\xBE"));   // U+25BE ▾
    m_downloadDropdown->setObjectName("MangaDetailDownloadDropdown");
    m_downloadDropdown->setPopupMode(QToolButton::InstantPopup);
    m_downloadDropdown->setCursor(Qt::PointingHandCursor);
    m_downloadDropdown->setStyleSheet(
        "#MangaDetailDownloadDropdown {"
        "  background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px; color: #e0e0e0; font-size: 12px; padding: 6px 14px; }"
        "#MangaDetailDownloadDropdown:hover { background: rgba(255,255,255,0.14);"
        "  border-color: rgba(255,255,255,0.28); }"
        "#MangaDetailDownloadDropdown::menu-indicator { image: none; width: 0px; }");

    auto* dlMenu = new QMenu(m_downloadDropdown);
    QAction* allAction = dlMenu->addAction(tr("Download all"));
    connect(allAction, &QAction::triggered, this, [this]() {
        downloadNextN(INT_MAX);
    });

    auto addNextN = [this, dlMenu](int n) {
        QAction* a = dlMenu->addAction(tr("Download next %1 not-downloaded").arg(n));
        connect(a, &QAction::triggered, this, [this, n]() {
            downloadNextN(n);
        });
    };
    addNextN(5);
    addNextN(10);
    addNextN(25);

    dlMenu->addSeparator();

    QAction* customAction = dlMenu->addAction(tr("Custom range..."));
    connect(customAction, &QAction::triggered, this, [this]() {
        openRangeDialog();
    });

    m_downloadDropdown->setMenu(dlMenu);

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

    connect(m_msClearBtn, &QPushButton::clicked, this, [this]() {
        m_chapterTable->clearSelection();
    });

    connect(m_msDownloadBtn, &QPushButton::clicked, this, [this]() {
        if (!m_downloader || !m_destProvider) return;
        QList<ChapterInfo> selected;
        for (const auto& ch : m_chapters) {
            if (m_selectedChapterIds.contains(ch.id)) selected.append(ch);
        }
        if (selected.isEmpty()) return;
        m_downloader->startDownload(m_result.title, m_result.source,
                                     selected, m_destProvider(),
                                     QStringLiteral("cbz"), m_coverPath);
        m_chapterTable->clearSelection();
    });

    connect(m_msDeleteBtn, &QPushButton::clicked, this, [this]() {
        if (!m_downloader) return;
        // Confirm popover before destructive action
        const int n = m_selectedChapterIds.size();
        const auto ans = QMessageBox::question(this, tr("Delete chapters?"),
            tr("Delete %1 selected chapter(s) from disk?").arg(n),
            QMessageBox::Yes | QMessageBox::No);
        if (ans != QMessageBox::Yes) return;

        // Per spec §11 + Phase-2 follow-up MIHON_OVERHAUL_FU.2: per-chapter
        // delete engine API not present in v1. Emit signal for TankoyomiPage
        // to handle (currently a placeholder logging path; FU.2 will wire to
        // a future MangaDownloader::deleteChapters(id, chapterIds) API).
        const QList<QString> ids = m_selectedChapterIds.values();
        emit deleteChaptersRequested(m_result.title, m_result.source, ids);
        m_chapterTable->clearSelection();
    });

    m_multiSelectBar->hide();
    root->addWidget(m_multiSelectBar);

    // -- Chapter table (2-col model mirror of StreamDetailView) -------
    m_chapterTable = new QTableWidget(0, kColumnCount, this);
    m_chapterTable->setObjectName("MangaDetailChapterTable");
    m_chapterTable->setHorizontalHeaderLabels({
        tr("Chapter"),
        QString(),                       // action — no header text
    });
    m_chapterTable->horizontalHeader()->setSectionResizeMode(kColTitle,  QHeaderView::Stretch);
    m_chapterTable->horizontalHeader()->setSectionResizeMode(kColAction, QHeaderView::Fixed);
    m_chapterTable->setColumnWidth(kColAction, 36);
    m_chapterTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_chapterTable->verticalHeader()->setDefaultSectionSize(56);
    m_chapterTable->verticalHeader()->hide();
    m_chapterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_chapterTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_chapterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chapterTable->setShowGrid(false);
    m_chapterTable->setAlternatingRowColors(true);
    m_chapterTable->setSortingEnabled(false);
    m_chapterTable->setContextMenuPolicy(Qt::CustomContextMenu);  // F.2 wires
    m_chapterTable->setStyleSheet(
        "QTableWidget { background: transparent; border: none; color: #ccc;"
        "  alternate-background-color: rgba(255,255,255,0.03); }"
        "QTableWidget::item { padding: 4px; }"
        "QTableWidget::item:selected { background: rgba(255,255,255,0.08); }"
        "QHeaderView::section { background: rgba(255,255,255,0.05); color: rgba(255,255,255,0.5);"
        "  border: none; font-size: 11px; padding: 4px; }");
    connect(m_chapterTable, &QTableWidget::itemSelectionChanged,
            this, &MangaDetailView::updateMultiSelectBar);
    connect(m_chapterTable, &QTableWidget::customContextMenuRequested,
            this, &MangaDetailView::showChapterContextMenu);
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
    m_coverPath = coverPath;
    m_chapters.clear();
    m_selectedChapterIds.clear();
    m_chapterTable->setRowCount(0);

    m_titleLabel->setText(result.title);

    // Inline dot-separated meta: [author · ]status · source · placeholder.
    // onChaptersReady rewrites this once the scraper returns.
    QStringList parts;
    if (!result.author.isEmpty()) {
        parts.append(result.author);
    }
    const QString status = result.status.isEmpty() ? tr("Unknown") : result.status;
    parts.append(status);
    parts.append(mangaSourceDisplayName(result.source));
    parts.append(tr("Loading chapters..."));
    m_metaLine->setText(parts.join(QStringLiteral(" · ")));

    // Cover
    if (!coverPath.isEmpty() && QFile::exists(coverPath)) {
        m_coverLabel->setPixmap(QPixmap(coverPath));
    } else {
        m_coverLabel->setText(tr("(no cover)"));
        m_coverLabel->setAlignment(Qt::AlignCenter);
    }

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

void MangaDetailView::setDownloader(MangaDownloader* dl)
{
    if (m_downloader == dl) return;
    if (m_downloader) {
        disconnect(m_downloader, nullptr, this, nullptr);
    }
    m_downloader = dl;
    if (m_downloader) {
        connect(m_downloader, &MangaDownloader::chapterUpdated,
                this, &MangaDetailView::onChapterUpdated);
    }
}

void MangaDetailView::setBridgeDestinationProvider(std::function<QString()> destProvider)
{
    m_destProvider = std::move(destProvider);
}

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

    // Rebuild inline meta line with real chapter count + downloaded count.
    int downloaded = 0;
    if (m_downloader) {
        downloaded = m_downloader->countDownloadedForSeries(
            m_result.title, m_result.source);
    }
    QStringList parts;
    if (!m_result.author.isEmpty()) {
        parts.append(m_result.author);
    }
    const QString status = m_result.status.isEmpty() ? tr("Unknown") : m_result.status;
    parts.append(status);
    parts.append(mangaSourceDisplayName(m_result.source));
    parts.append(tr("%1 chapters").arg(chapters.size()));
    if (downloaded > 0) {
        parts.append(tr("%1 downloaded").arg(downloaded));
    }
    m_metaLine->setText(parts.join(QStringLiteral(" · ")));
}

void MangaDetailView::onScraperError(const QString& message)
{
    m_loadingLabel->hide();
    m_chapterTable->hide();
    m_errorLabel->setText(tr("Could not load chapters: %1").arg(message));
    m_errorLabel->show();
}

void MangaDetailView::onChapterUpdated(const QString& seriesId,
                                       const QString& chapterId)
{
    // Gate by the downloader's record: only act if seriesId maps to our current
    // (m_result.title, m_result.source). Prevents foreign-series updates from
    // poisoning the row indicator state — the downloader emits this signal
    // globally across all tracked series.
    if (!m_downloader) return;
    bool isOurSeries = false;
    const auto records = m_downloader->listActive();
    for (const auto& rec : records) {
        if (rec.id != seriesId) continue;
        if (rec.seriesTitle == m_result.title && rec.source == m_result.source) {
            isOurSeries = true;
        }
        break;
    }
    if (!isOurSeries) return;

    // Find the row for this chapter
    int row = -1;
    for (int i = 0; i < m_chapters.size(); ++i) {
        if (m_chapters[i].id == chapterId) { row = i; break; }
    }
    if (row < 0) return;

    auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
        m_chapterTable->cellWidget(row, kColAction));
    if (!indicator) return;

    deriveChapterState(chapterId, *indicator);
}

void MangaDetailView::updateMultiSelectBar()
{
    m_selectedChapterIds.clear();
    QList<int> rows;
    for (const auto& idx : m_chapterTable->selectionModel()->selectedRows()) {
        rows.append(idx.row());
    }
    std::sort(rows.begin(), rows.end());

    for (int r : rows) {
        if (r >= 0 && r < m_chapters.size()) {
            m_selectedChapterIds.insert(m_chapters[r].id);
        }
    }

    if (rows.isEmpty()) {
        m_multiSelectBar->hide();
        return;
    }

    QString label;
    // Detect contiguous range
    bool contiguous = true;
    for (int i = 1; i < rows.size(); ++i) {
        if (rows[i] != rows[i-1] + 1) { contiguous = false; break; }
    }
    if (contiguous && rows.size() > 1) {
        const int firstNum = int(m_chapters[rows.first()].chapterNumber);
        const int lastNum  = int(m_chapters[rows.last()].chapterNumber);
        label = tr("Chapters %1\xE2\x80\x93%2 (%3 chapters) selected")
            .arg(qMin(firstNum, lastNum))
            .arg(qMax(firstNum, lastNum))
            .arg(rows.size());
    } else if (rows.size() == 1) {
        const int num = int(m_chapters[rows.first()].chapterNumber);
        label = tr("Chapter %1 (1 chapter) selected").arg(num);
    } else {
        label = tr("%1 chapters selected").arg(rows.size());
    }
    m_multiSelectLabel->setText(label);
    m_multiSelectBar->show();
}

void MangaDetailView::hideEvent(QHideEvent* event)
{
    // Drop scraper subscriptions while hidden — prevents foreign-manga chapter
    // updates from poisoning our state when another consumer (e.g. AddMangaDialog
    // or a future detail-view for a different manga) calls fetchChapters on the
    // same scraper instance.
    if (m_scraper) {
        disconnect(m_scraper, &MangaScraper::chaptersReady, this, nullptr);
        disconnect(m_scraper, &MangaScraper::errorOccurred, this, nullptr);
    }
    QWidget::hideEvent(event);
}

void MangaDetailView::deriveChapterState(const QString& chapterId,
                                          ChapterDownloadIndicator& indicator) const
{
    using State = ChapterDownloadIndicator::State;
    State state = State::NotDownloaded;
    int progress = 0;

    if (m_downloader) {
        const auto records = m_downloader->listActive();
        for (const auto& rec : records) {
            if (rec.seriesTitle != m_result.title) continue;
            if (rec.source != m_result.source) continue;
            for (const auto& ch : rec.chapters) {
                if (ch.chapterId != chapterId) continue;
                if (ch.status == "queued")           state = State::Queued;
                else if (ch.status == "downloading") {
                    state = State::Downloading;
                    if (ch.totalImages > 0) {
                        progress = (ch.downloadedImages * 100) / ch.totalImages;
                    }
                }
                else if (ch.status == "completed")   state = State::Downloaded;
                else if (ch.status == "error")       state = State::Errored;
                else if (ch.status == "cancelled")   state = State::NotDownloaded;
                break;
            }
            if (state != State::NotDownloaded) break;
        }
    }

    indicator.setState(state);
    if (state == State::Downloading) indicator.setProgress(progress);
}

void MangaDetailView::renderChapters()
{
    m_chapterTable->setRowCount(m_chapters.size());
    for (int i = 0; i < m_chapters.size(); ++i) {
        const ChapterInfo& ch = m_chapters[i];

        // kColTitle — stacked title + optional inline date subtitle.
        // Mirror of StreamDetailView.cpp:920-950 title-cell shape.
        auto* titleCell = new QWidget(m_chapterTable);
        auto* titleLayout = new QVBoxLayout(titleCell);
        titleLayout->setContentsMargins(8, 6, 8, 6);
        titleLayout->setSpacing(2);

        auto* nameLabel = new QLabel(ch.name, titleCell);
        nameLabel->setWordWrap(false);
        nameLabel->setStyleSheet(
            "color: #e0e0e0; font-size: 12px; font-weight: 500; background: transparent;");
        nameLabel->setTextFormat(Qt::PlainText);
        titleLayout->addWidget(nameLabel);

        if (ch.dateUpload > 0) {
            const QString dateStr =
                QDateTime::fromMSecsSinceEpoch(ch.dateUpload).toString("yyyy-MM-dd");
            auto* dateLabel = new QLabel(dateStr, titleCell);
            dateLabel->setStyleSheet(
                "color: rgba(255,255,255,0.45); font-size: 10px; background: transparent;");
            dateLabel->setTextFormat(Qt::PlainText);
            titleLayout->addWidget(dateLabel);
        } else {
            titleLayout->addStretch();
        }
        m_chapterTable->setCellWidget(i, kColTitle, titleCell);

        // Hidden QTableWidgetItem so selection / shift-click multi-select
        // mechanics still work — cellWidget() alone doesn't satisfy the
        // selection model. UserRole carries the chapter id for
        // context-menu lookup (mirror of StreamDetailView.cpp:891).
        auto* titleItem = new QTableWidgetItem();
        titleItem->setData(Qt::UserRole, ch.id);
        m_chapterTable->setItem(i, kColTitle, titleItem);

        // kColAction — ChapterDownloadIndicator (B.1–B.3, 5-state animated).
        auto* indicator = new ChapterDownloadIndicator(m_chapterTable);
        m_chapterTable->setCellWidget(i, kColAction, indicator);

        deriveChapterState(ch.id, *indicator);

        connect(indicator, &ChapterDownloadIndicator::clicked, this,
                [this, i]() { onChapterIconClicked(i); });
    }
}

void MangaDetailView::onChapterIconClicked(int row)
{
    if (row < 0 || row >= m_chapters.size()) return;
    if (!m_downloader || !m_destProvider) return;

    const ChapterInfo& ch = m_chapters[row];
    auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
        m_chapterTable->cellWidget(row, kColAction));
    if (!indicator) return;

    using State = ChapterDownloadIndicator::State;
    switch (indicator->state()) {
        case State::NotDownloaded:
        case State::Errored:
            // Enqueue (start fresh, or retry from errored)
            m_downloader->startDownload(m_result.title, m_result.source,
                                         {ch}, m_destProvider(),
                                         QStringLiteral("cbz"), m_coverPath);
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

void MangaDetailView::downloadNextN(int n)
{
    if (!m_downloader || !m_destProvider) {
        return;
    }

    // Build set of chapter IDs that are NOT already downloaded/queued/downloading
    QList<ChapterInfo> picks;
    const auto records = m_downloader->listActive();
    for (const auto& ch : m_chapters) {
        bool skip = false;
        for (const auto& rec : records) {
            if (rec.seriesTitle != m_result.title) continue;
            if (rec.source != m_result.source) continue;
            for (const auto& chd : rec.chapters) {
                if (chd.chapterId != ch.id) continue;
                if (chd.status == "queued" || chd.status == "downloading" ||
                    chd.status == "completed") {
                    skip = true;
                }
                break;
            }
            if (skip) break;
        }
        if (!skip) picks.append(ch);
        if (picks.size() >= n) break;
    }
    if (picks.isEmpty()) {
        return;
    }
    m_downloader->startDownload(m_result.title, m_result.source,
                                 picks, m_destProvider(),
                                 QStringLiteral("cbz"), m_coverPath);
}

void MangaDetailView::openRangeDialog()
{
    if (!m_downloader || !m_destProvider) {
        return;
    }

    // Build set of chapter IDs already handled (queued/downloading/completed)
    QSet<QString> handled;
    const auto records = m_downloader->listActive();
    for (const auto& rec : records) {
        if (rec.seriesTitle != m_result.title) continue;
        if (rec.source != m_result.source) continue;
        for (const auto& chd : rec.chapters) {
            if (chd.status == "queued" || chd.status == "downloading" ||
                chd.status == "completed") {
                handled.insert(chd.chapterId);
            }
        }
    }
    ChapterRangeDialog dlg(m_chapters, handled, this);
    const int rc = dlg.exec();
    if (rc != QDialog::Accepted) {
        return;
    }
    const QList<ChapterInfo> picks = dlg.selectedChapters();
    if (picks.isEmpty()) {
        return;
    }
    m_downloader->startDownload(m_result.title, m_result.source,
                                 picks, m_destProvider(),
                                 QStringLiteral("cbz"), m_coverPath);
}

void MangaDetailView::showChapterContextMenu(const QPoint& pos)
{
    const int row = m_chapterTable->rowAt(pos.y());
    if (row < 0 || row >= m_chapters.size()) return;
    const ChapterInfo& ch = m_chapters[row];

    auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
        m_chapterTable->cellWidget(row, kColAction));
    if (!indicator) return;
    const ChapterDownloadIndicator::State state = indicator->state();

    QMenu menu(this);
    using S = ChapterDownloadIndicator::State;

    switch (state) {
        case S::NotDownloaded:
            menu.addAction(tr("Start download"), this, [this, ch]() {
                if (!m_downloader || !m_destProvider) return;
                m_downloader->startDownload(m_result.title, m_result.source,
                    {ch}, m_destProvider(), "cbz", m_coverPath);
            });
            menu.addAction(tr("Add to top of queue"), this, [this, ch]() {
                if (!m_downloader || !m_destProvider) return;
                // Enqueue, then bump to front via startChapterNow
                m_downloader->startDownload(m_result.title, m_result.source,
                    {ch}, m_destProvider(), "cbz", m_coverPath);
                const auto records = m_downloader->listActive();
                for (const auto& rec : records) {
                    if (rec.seriesTitle == m_result.title &&
                        rec.source == m_result.source) {
                        m_downloader->startChapterNow(rec.id, ch.id);
                        break;
                    }
                }
            });
            break;

        case S::Queued:
        case S::Downloading:
            menu.addAction(tr("Start now (jump queue)"), this, [this, ch]() {
                if (!m_downloader) return;
                const auto records = m_downloader->listActive();
                for (const auto& rec : records) {
                    if (rec.seriesTitle == m_result.title &&
                        rec.source == m_result.source) {
                        m_downloader->startChapterNow(rec.id, ch.id);
                        break;
                    }
                }
            });
            menu.addAction(tr("Cancel chapter"), this, [ch]() {
                // PHASE 2 FOLLOW-UP MIHON_OVERHAUL_FU.1 — per-chapter cancel
                // engine API not in v1. Stub logs intent.
                qDebug() << "TODO MIHON_OVERHAUL_FU.1: cancel chapter" << ch.id;
            });
            break;

        case S::Downloaded:
            menu.addAction(tr("Open folder"), this, [this, ch]() {
                emit openChapterFolderRequested(m_result.title, m_result.source, ch.id);
            });
            menu.addAction(tr("Show file in folder"), this, [this, ch]() {
                emit showChapterFileRequested(m_result.title, m_result.source, ch.id);
            });
            menu.addSeparator();
            menu.addAction(tr("Delete from disk"), this, [this, ch]() {
                const auto ans = QMessageBox::question(this,
                    tr("Delete chapter?"),
                    tr("Delete chapter \"%1\" from disk?").arg(ch.name),
                    QMessageBox::Yes | QMessageBox::No);
                if (ans == QMessageBox::Yes) {
                    emit deleteChaptersRequested(m_result.title, m_result.source,
                                                   {ch.id});
                }
            });
            break;

        case S::Errored:
            menu.addAction(tr("Retry download"), this, [this, ch]() {
                if (!m_downloader) return;
                const auto records = m_downloader->listActive();
                for (const auto& rec : records) {
                    if (rec.seriesTitle == m_result.title &&
                        rec.source == m_result.source) {
                        m_downloader->retryFailedChapters(rec.id);
                        break;
                    }
                }
            });
            menu.addAction(tr("Cancel"), this, [ch]() {
                qDebug() << "TODO MIHON_OVERHAUL_FU.1: cancel errored chapter" << ch.id;
            });
            break;
    }

    menu.addSeparator();
    menu.addAction(tr("Copy chapter URL"), this, [ch]() {
        QGuiApplication::clipboard()->setText(ch.url);
    });
    menu.addAction(tr("Copy chapter name"), this, [ch]() {
        QGuiApplication::clipboard()->setText(ch.name);
    });

    menu.exec(m_chapterTable->viewport()->mapToGlobal(pos));
}

