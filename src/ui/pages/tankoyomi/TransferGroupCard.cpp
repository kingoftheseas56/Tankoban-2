#include "TransferGroupCard.h"

#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "ChapterDownloadIndicator.h"

TransferGroupCard::TransferGroupCard(MangaDownloader* dl, QWidget* parent)
    : QWidget(parent), m_downloader(dl)
{
    setObjectName("TransferGroupCard");
    buildUI();
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            emit contextMenuRequested(mapToGlobal(pos), m_recordId);
        });

    if (m_downloader) {
        connect(m_downloader, &MangaDownloader::chapterUpdated,
                this, &TransferGroupCard::onChapterUpdated);
        connect(m_downloader, &MangaDownloader::downloadUpdated,
                this, &TransferGroupCard::onDownloadUpdated);
    }
}

void TransferGroupCard::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* headerRow = new QHBoxLayout();
    m_coverLabel = new QLabel(this);
    m_coverLabel->setObjectName("TransferCardCover");
    m_coverLabel->setFixedSize(48, 64);
    m_coverLabel->setScaledContents(true);
    headerRow->addWidget(m_coverLabel);

    auto* headerCol = new QVBoxLayout();
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("TransferCardTitle");
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("TransferCardStatus");
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("TransferCardProgress");
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    headerCol->addWidget(m_titleLabel);
    headerCol->addWidget(m_statusLabel);
    headerCol->addWidget(m_progressBar);
    headerRow->addLayout(headerCol, 1);

    m_pauseToggle = new QPushButton(this);
    m_pauseToggle->setObjectName("TransferCardPauseToggle");
    m_pauseToggle->setIcon(QIcon(QStringLiteral(":/icons/pause-circle.svg")));
    m_pauseToggle->setIconSize(QSize(18, 18));
    m_pauseToggle->setFlat(true);
    m_pauseToggle->setToolTip(tr("Pause series"));
    connect(m_pauseToggle, &QPushButton::clicked,
            this, &TransferGroupCard::onPauseToggleClicked);
    headerRow->addWidget(m_pauseToggle);

    m_cancelBtn = new QToolButton(this);
    m_cancelBtn->setObjectName("TransferCardCancel");
    m_cancelBtn->setIcon(QIcon(QStringLiteral(":/icons/close-x.svg")));
    m_cancelBtn->setIconSize(QSize(16, 16));
    m_cancelBtn->setAutoRaise(true);
    m_cancelBtn->setToolTip(tr("Cancel series"));
    connect(m_cancelBtn, &QToolButton::clicked,
            this, &TransferGroupCard::onCancelClicked);
    headerRow->addWidget(m_cancelBtn);

    root->addLayout(headerRow);

    // Chapter list column (E.3 fills)
    m_chapterColumn = new QVBoxLayout();
    m_chapterColumn->setContentsMargins(56, 0, 0, 0);  // indent under title
    m_chapterColumn->setSpacing(2);
    root->addLayout(m_chapterColumn);
}

void TransferGroupCard::setRecord(const MangaDownloadRecord& rec)
{
    m_recordId = rec.id;
    refreshFromRecord();
    rebuildChapterList(rec);
}

void TransferGroupCard::refreshFromRecord()
{
    if (!m_downloader || m_recordId.isEmpty()) return;
    const auto records = m_downloader->listActive();
    MangaDownloadRecord rec;
    bool found = false;
    for (const auto& r : records) {
        if (r.id == m_recordId) { rec = r; found = true; break; }
    }
    if (!found) return;

    m_titleLabel->setText(rec.seriesTitle);

    // Hemanth-smoke-fix-3 (2026-05-13): render cover when the record
    // carries a coverPath. Legacy records without the field (created
    // before the pipeline shipped) load with empty coverPath and fall
    // through to the empty-fallback branch — no cover rendered, but no
    // crash either (graceful degradation).
    if (!rec.coverPath.isEmpty() && QFile::exists(rec.coverPath)) {
        m_coverLabel->setPixmap(QPixmap(rec.coverPath));
        m_coverLabel->setText({});
    } else {
        m_coverLabel->setPixmap(QPixmap());
        m_coverLabel->setText({});
    }

    // Aggregate state label
    int downloading = 0, queued = 0, completed = 0,
        errored = 0, cancelled = 0;
    for (const auto& ch : rec.chapters) {
        if      (ch.status == "downloading") ++downloading;
        else if (ch.status == "queued")      ++queued;
        else if (ch.status == "completed")   ++completed;
        else if (ch.status == "error")       ++errored;
        else if (ch.status == "cancelled")   ++cancelled;
    }
    const int total = rec.chapters.size();
    QString state;
    if (m_downloader->isSeriesPaused(m_recordId))    state = tr("Paused");
    else if (downloading > 0)                         state = tr("Downloading");
    else if (errored > 0 && downloading == 0)         state = tr("Errored");
    else if (queued > 0)                              state = tr("Queued");
    else if (completed == total)                      state = tr("Completed");
    else if (cancelled == total)                      state = tr("Cancelled");
    else                                              state = tr("Idle");

    m_statusLabel->setText(tr("%1 · %2 of %3 chapters")
        .arg(state).arg(completed).arg(total));

    if (total > 0) {
        m_progressBar->setValue((completed * 100) / total);
    }

    const bool paused = m_downloader->isSeriesPaused(m_recordId);
    m_pauseToggle->setIcon(QIcon(paused
        ? QStringLiteral(":/icons/play-circle.svg")
        : QStringLiteral(":/icons/pause-circle.svg")));
    m_pauseToggle->setToolTip(paused ? tr("Resume series") : tr("Pause series"));

    // Visual muting on paused state via QSS dynamic property
    setProperty("paused", paused);
    style()->unpolish(this); style()->polish(this);
}

void TransferGroupCard::rebuildChapterList(const MangaDownloadRecord& rec)
{
    // Clear existing rows
    while (auto* item = m_chapterColumn->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (const auto& ch : rec.chapters) {
        auto* row = new QWidget(this);
        row->setObjectName("TransferCardChapterRow");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);

        auto* label = new QLabel(tr("Ch %1  %2")
            .arg(ch.chapterNumber, 0, 'f', 1).arg(ch.chapterName), row);
        label->setObjectName("TransferCardChapterLabel");

        label->setTextFormat(Qt::PlainText);

        auto* indicator = new ChapterDownloadIndicator(row);
        indicator->setObjectName(QStringLiteral("TransferCardIndicator_%1")
            .arg(ch.chapterId));
        using S = ChapterDownloadIndicator::State;
        if      (ch.status == "queued")      indicator->setState(S::Queued);
        else if (ch.status == "downloading") {
            indicator->setState(S::Downloading);
            if (ch.totalImages > 0) {
                indicator->setProgress((ch.downloadedImages * 100) /
                                        ch.totalImages);
            }
        }
        else if (ch.status == "completed")   indicator->setState(S::Downloaded);
        else if (ch.status == "error")       indicator->setState(S::Errored);
        else                                  indicator->setState(S::NotDownloaded);

        rl->addWidget(label, 1);
        rl->addWidget(indicator);

        m_chapterColumn->addWidget(row);
    }
}

void TransferGroupCard::onPauseToggleClicked()
{
    if (!m_downloader || m_recordId.isEmpty()) return;
    if (m_downloader->isSeriesPaused(m_recordId)) {
        m_downloader->resumeSeries(m_recordId);
    } else {
        m_downloader->pauseSeries(m_recordId);
    }
}

void TransferGroupCard::onCancelClicked()
{
    emit cancelSeriesRequested(m_recordId);
}

void TransferGroupCard::onChapterUpdated(const QString& seriesId,
                                         const QString& chapterId)
{
    if (seriesId != m_recordId) return;
    refreshFromRecord();  // Header still needs full refresh (aggregate counts may change)

    // Find the indicator for this chapter by objectName
    auto* indicator = findChild<ChapterDownloadIndicator*>(
        QStringLiteral("TransferCardIndicator_%1").arg(chapterId));
    if (!indicator) return;

    // Walk the live record to grab the current chapter state
    const auto records = m_downloader->listActive();
    for (const auto& rec : records) {
        if (rec.id != m_recordId) continue;
        for (const auto& ch : rec.chapters) {
            if (ch.chapterId != chapterId) continue;
            using S = ChapterDownloadIndicator::State;
            if      (ch.status == "queued")      indicator->setState(S::Queued);
            else if (ch.status == "downloading") {
                indicator->setState(S::Downloading);
                if (ch.totalImages > 0)
                    indicator->setProgress((ch.downloadedImages * 100) /
                                            ch.totalImages);
            }
            else if (ch.status == "completed")   indicator->setState(S::Downloaded);
            else if (ch.status == "error")       indicator->setState(S::Errored);
            else                                  indicator->setState(S::NotDownloaded);
            break;
        }
        break;
    }
}

void TransferGroupCard::onDownloadUpdated(const QString& seriesId)
{
    if (seriesId != m_recordId) return;
    refreshFromRecord();
}
