#include "TransferGroupCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
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

    m_pauseToggle = new QPushButton(tr("Pause"), this);
    m_pauseToggle->setObjectName("TransferCardPauseToggle");
    connect(m_pauseToggle, &QPushButton::clicked,
            this, &TransferGroupCard::onPauseToggleClicked);
    headerRow->addWidget(m_pauseToggle);

    m_cancelBtn = new QToolButton(this);
    m_cancelBtn->setText(QStringLiteral("✕"));   // F.1 may replace with SVG
    m_cancelBtn->setObjectName("TransferCardCancel");
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
    // E.2 fills (header label/progress) — E.1 stubs
}

void TransferGroupCard::rebuildChapterList(const MangaDownloadRecord& rec)
{
    // E.3 fills — E.1 stubs
    Q_UNUSED(rec);
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
    Q_UNUSED(chapterId);
    // E.3 reaches into the specific chapter row and updates its indicator;
    // for E.1 just refresh the header
    refreshFromRecord();
}

void TransferGroupCard::onDownloadUpdated(const QString& seriesId)
{
    if (seriesId != m_recordId) return;
    refreshFromRecord();
}
