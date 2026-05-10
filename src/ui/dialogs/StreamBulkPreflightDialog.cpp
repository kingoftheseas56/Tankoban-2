#include "StreamBulkPreflightDialog.h"

#include "ui/pages/stream/StreamSourceChoice.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStorageInfo>
#include <QVBoxLayout>

using namespace tankostream::stream;

namespace {

QString qualityLabel(int qualitySort)
{
    switch (qualitySort) {
    case 5: return QStringLiteral("4K");
    case 4: return QStringLiteral("1440p");
    case 3: return QStringLiteral("1080p");
    case 2: return QStringLiteral("720p");
    case 1: return QStringLiteral("480p");
    default: return QStringLiteral("Unknown");
    }
}

QLabel* makeLine(const QString& text, QWidget* parent, bool warning = false)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet(warning
        ? QStringLiteral("color: #ffb4b4; font-size: 12px;")
        : QStringLiteral("color: #dddddd; font-size: 12px;"));
    return label;
}

QString sizeText(qint64 bytes)
{
    return bytes > 0 ? humanSize(bytes) : QStringLiteral("unknown size");
}

bool isDownloadableReason(BulkSelectionReason reason)
{
    return reason == BulkSelectionReason::Picked ||
           reason == BulkSelectionReason::PackCovered;
}

QString seasonEpisodePrefix(int season, int episode)
{
    return QStringLiteral("S%1E%2")
        .arg(season,  2, 10, QChar('0'))
        .arg(episode, 2, 10, QChar('0'));
}

} // namespace

StreamBulkPreflightDialog::StreamBulkPreflightDialog(
    const BulkSelectionPlan& plan,
    const BulkPackVerificationResult& verifierResult,
    const BulkSourceCollectionPayload& sourcePayload,
    const BulkPlanResult& planResult,
    const QString& displayLabel,
    const QString& videosRootPath,
    const QString& verificationNote,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Start stream download"));
    setModal(true);
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 14);
    root->setSpacing(10);

    auto* title = new QLabel(displayLabel, this);
    title->setTextFormat(Qt::PlainText);
    title->setStyleSheet(QStringLiteral(
        "color: #f0f0f0; font-size: 16px; font-weight: 600;"));
    root->addWidget(title);

    // STREAM_BULK_DOWNLOAD_V2 Phase 1 — episode selection picker. Renders
    // one row per planResult.items[] entry (ordered by episode), with
    // tickbox state derived from the selection plan + plan-item status:
    //   - Picked / PackCovered:  enabled, default checked
    //   - MissingNoSource:       disabled, "[no source]" annotation
    //   - Skipped (already on disk): disabled, "[in library]" annotation
    auto* selectHeader = new QLabel(tr("Select episodes"), this);
    selectHeader->setTextFormat(Qt::PlainText);
    selectHeader->setStyleSheet(QStringLiteral(
        "color: #f0f0f0; font-size: 13px; font-weight: 600; margin-top: 4px;"));
    root->addWidget(selectHeader);

    m_episodeList = new QListWidget(this);
    m_episodeList->setMinimumHeight(160);
    m_episodeList->setStyleSheet(QStringLiteral(
        "QListWidget { background: #161616; color: #dddddd; "
        "border: 1px solid #2a2a2a; font-size: 12px; }"
        "QListWidget::item { padding: 4px 6px; }"
        "QListWidget::item:selected { background: #2a2a2a; }"
        "QListWidget::item:disabled { color: #6a6a6a; }"));

    QHash<QString, BulkSelectionItem> selectionByKey;
    for (const BulkSelectionItem& item : plan.items)
        selectionByKey.insert(item.itemKey, item);

    const QString sourceTag = (plan.mode == BulkSelectionMode::Pack)
        ? QStringLiteral("pack")
        : QStringLiteral("per-episode");

    for (const BulkPlanItem& planItem : planResult.items) {
        const int season  = planItem.input.season;
        const int episode = planItem.input.episode;
        const QString prefix = seasonEpisodePrefix(season, episode);
        const QString titleSeg = planItem.input.title.isEmpty()
            ? QString()
            : QStringLiteral(" — %1").arg(planItem.input.title);

        QString rowText;
        bool enabled = false;
        Qt::CheckState defaultState = Qt::Unchecked;

        if (planItem.status == BulkPlanItemStatus::Skipped) {
            rowText = QStringLiteral("%1%2  ·  [in library]").arg(prefix, titleSeg);
        } else {
            const auto it = selectionByKey.constFind(planItem.itemKey);
            if (it != selectionByKey.cend() && isDownloadableReason(it->reason)) {
                rowText = QStringLiteral("%1%2  ·  %3  ·  %4  ·  %5")
                    .arg(prefix,
                         titleSeg,
                         qualityLabel(it->choice.qualitySort),
                         sizeText(it->choice.sizeBytes),
                         sourceTag);
                enabled = true;
                defaultState = Qt::Checked;
            } else {
                rowText = QStringLiteral("%1%2  ·  [no source]").arg(prefix, titleSeg);
            }
        }

        qint64 rowSizeBytes = 0;
        if (enabled) {
            const auto it = selectionByKey.constFind(planItem.itemKey);
            if (it != selectionByKey.cend() && it->choice.sizeBytes > 0)
                rowSizeBytes = it->choice.sizeBytes;
        }
        auto* lwItem = new QListWidgetItem(rowText, m_episodeList);
        lwItem->setData(Qt::UserRole,     planItem.itemKey);
        lwItem->setData(Qt::UserRole + 1, rowSizeBytes);
        if (enabled) {
            lwItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        } else {
            // Disabled rows: visible but tickbox is non-interactive. Keep
            // the checkable role so the box renders, but no ItemIsEnabled
            // means clicks are ignored and Qt grays the row out.
            lwItem->setFlags(Qt::ItemIsUserCheckable);
        }
        lwItem->setCheckState(defaultState);
    }

    root->addWidget(m_episodeList);

    auto* selButtonRow = new QHBoxLayout();
    selButtonRow->setContentsMargins(0, 0, 0, 0);
    selButtonRow->setSpacing(8);
    auto* selectAllBtn = new QPushButton(tr("Select all"), this);
    auto* deselectAllBtn = new QPushButton(tr("Deselect all"), this);
    selectAllBtn->setCursor(Qt::PointingHandCursor);
    deselectAllBtn->setCursor(Qt::PointingHandCursor);
    selButtonRow->addStretch();
    selButtonRow->addWidget(selectAllBtn);
    selButtonRow->addWidget(deselectAllBtn);
    root->addLayout(selButtonRow);

    connect(selectAllBtn,   &QPushButton::clicked, this,
            [this]() { setAllCheckStates(Qt::Checked); });
    connect(deselectAllBtn, &QPushButton::clicked, this,
            [this]() { setAllCheckStates(Qt::Unchecked); });
    connect(m_episodeList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem*) { rebuildSelectedSummary(); });

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color: #2a2a2a;"));
    root->addWidget(divider);

    const BulkSelectionPreflightSummary& summary = plan.preflight;
    root->addWidget(makeLine(QStringLiteral("%1 episodes total").arg(summary.totalEpisodes), this));
    root->addWidget(makeLine(QStringLiteral("%1 to download").arg(summary.toDownload), this));
    if (summary.alreadyInLibrary > 0) {
        root->addWidget(makeLine(
            QStringLiteral("%1 already in library").arg(summary.alreadyInLibrary), this));
    }
    if (summary.missingNoSource > 0) {
        root->addWidget(makeLine(
            QStringLiteral("%1 unavailable (no source found)").arg(summary.missingNoSource),
            this,
            true));
    }

    if (plan.mode == BulkSelectionMode::Pack) {
        StreamPickerChoice chosen;
        for (const BulkSelectionItem& item : plan.items) {
            if (item.reason == BulkSelectionReason::PackCovered) {
                chosen = item.choice;
                break;
            }
        }
        int covered = 0;
        for (const BulkSelectionItem& item : verifierResult.updatedPlan.items) {
            if (item.reason == BulkSelectionReason::PackCovered &&
                verifierResult.fileIndexByEpisode.contains(item.episodeNum)) {
                ++covered;
            }
        }
        const bool verified = covered > 0 && covered == summary.toDownload;
        root->addWidget(makeLine(
            QStringLiteral("Source: pack (1 torrent, %1 seeders, ~%2) - %3")
                .arg(chosen.seeders)
                .arg(sizeText(chosen.sizeBytes))
                .arg(verified ? QStringLiteral("verified") : QStringLiteral("unverified")),
            this));
    } else {
        root->addWidget(makeLine(
            QStringLiteral("Source: %1 per-episode magnets").arg(summary.toDownload),
            this));
    }
    if (!verificationNote.isEmpty())
        root->addWidget(makeLine(verificationNote, this, true));

    QStringList qualityParts;
    for (auto it = summary.qualityBreakdown.cbegin(); it != summary.qualityBreakdown.cend(); ++it)
        qualityParts << QStringLiteral("%1 at %2").arg(it.value()).arg(qualityLabel(it.key()));
    if (!qualityParts.isEmpty())
        root->addWidget(makeLine(QStringLiteral("Quality: %1").arg(qualityParts.join(QStringLiteral(", "))), this));

    int hdrCount = 0;
    int dvCount = 0;
    int unknownSizeCount = 0;
    for (const BulkSelectionItem& item : plan.items) {
        if (!isDownloadableReason(item.reason))
            continue;
        if (item.choice.sizeBytes <= 0)
            ++unknownSizeCount;
        for (const QString& badge : item.choice.badges) {
            if (badge.compare(QStringLiteral("HDR"), Qt::CaseInsensitive) == 0)
                ++hdrCount;
            if (badge.compare(QStringLiteral("DV"), Qt::CaseInsensitive) == 0)
                ++dvCount;
        }
    }
    if (hdrCount > 0)
        root->addWidget(makeLine(QStringLiteral("%1 HDR").arg(hdrCount), this));
    if (dvCount > 0)
        root->addWidget(makeLine(QStringLiteral("%1 DV").arg(dvCount), this));
    if (unknownSizeCount > 0) {
        root->addWidget(makeLine(
            QStringLiteral("%1 sources have unknown size").arg(unknownSizeCount),
            this));
    }

    int addonsErrored = 0;
    int addonsEmpty = 0;
    int timedOut = 0;
    for (auto it = sourcePayload.byEpisode.cbegin(); it != sourcePayload.byEpisode.cend(); ++it) {
        addonsErrored += it->addonsErrored;
        addonsEmpty += it->addonsEmpty;
        if (it->timedOut)
            ++timedOut;
    }
    if (addonsErrored > 0 || addonsEmpty > 0 || timedOut > 0) {
        root->addWidget(makeLine(
            QStringLiteral("Addon issues: %1 errors, %2 empty, %3 timed out")
                .arg(addonsErrored)
                .arg(addonsEmpty)
                .arg(timedOut),
            this));
    }
    if (!verifierResult.unclassifiedVideoFiles.isEmpty()) {
        root->addWidget(makeLine(
            QStringLiteral("%1 large video files were unclassified in the pack")
                .arg(verifierResult.unclassifiedVideoFiles.size()),
            this));
    }

    if (!videosRootPath.isEmpty()) {
        const QFileInfo rootInfo(videosRootPath);
        if (!rootInfo.exists() || !rootInfo.isWritable()) {
            root->addWidget(makeLine(
                QStringLiteral("Videos library root unwritable: %1").arg(videosRootPath),
                this,
                true));
        }
        root->addWidget(makeLine(
            QStringLiteral("Estimated size: %1").arg(sizeText(summary.estimatedTotalBytes)),
            this));
        const QStorageInfo storage(videosRootPath);
        if (storage.isValid() && storage.bytesAvailable() > 0 &&
            summary.estimatedTotalBytes > storage.bytesAvailable()) {
            root->addWidget(makeLine(
                QStringLiteral("Estimated size exceeds available free space"),
                this,
                true));
        }
    }

    // Bottom: dynamic selected-summary line. Updated whenever a tickbox
    // changes; OK button enable mirrors "≥1 selected".
    m_selectedSummaryLabel = new QLabel(this);
    m_selectedSummaryLabel->setWordWrap(true);
    m_selectedSummaryLabel->setTextFormat(Qt::PlainText);
    m_selectedSummaryLabel->setStyleSheet(QStringLiteral(
        "color: #f0f0f0; font-size: 12px; font-weight: 600; margin-top: 4px;"));
    root->addWidget(m_selectedSummaryLabel);

    auto* buttons = new QDialogButtonBox(this);
    auto* cancelBtn = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    auto* startBtn = buttons->addButton(tr("Start download"), QDialogButtonBox::AcceptRole);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setCursor(Qt::PointingHandCursor);
    m_acceptButton = startBtn;

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    rebuildSelectedSummary();
}

QSet<QString> StreamBulkPreflightDialog::selectedItemKeys() const
{
    QSet<QString> keys;
    if (!m_episodeList)
        return keys;
    for (int i = 0; i < m_episodeList->count(); ++i) {
        QListWidgetItem* lwItem = m_episodeList->item(i);
        if (!(lwItem->flags() & Qt::ItemIsEnabled))
            continue;  // disabled rows are never dispatched, regardless of state
        if (lwItem->checkState() == Qt::Checked) {
            const QString itemKey = lwItem->data(Qt::UserRole).toString();
            if (!itemKey.isEmpty())
                keys.insert(itemKey);
        }
    }
    return keys;
}

void StreamBulkPreflightDialog::rebuildSelectedSummary()
{
    if (!m_selectedSummaryLabel || !m_episodeList)
        return;

    int selectedCount = 0;
    int eligibleTotal = 0;
    qint64 selectedBytes = 0;
    for (int i = 0; i < m_episodeList->count(); ++i) {
        QListWidgetItem* lwItem = m_episodeList->item(i);
        if (!(lwItem->flags() & Qt::ItemIsEnabled))
            continue;
        ++eligibleTotal;
        if (lwItem->checkState() == Qt::Checked) {
            ++selectedCount;
            selectedBytes += lwItem->data(Qt::UserRole + 1).toLongLong();
        }
    }
    m_estimatedBytesSelected = selectedBytes;

    if (selectedCount <= 0) {
        m_selectedSummaryLabel->setText(
            tr("Selected: 0 of %1 episodes — pick at least one to start").arg(eligibleTotal));
    } else if (selectedBytes > 0) {
        m_selectedSummaryLabel->setText(
            tr("Selected: %1 of %2 episodes  ·  ~%3")
                .arg(selectedCount)
                .arg(eligibleTotal)
                .arg(humanSize(selectedBytes)));
    } else {
        m_selectedSummaryLabel->setText(
            tr("Selected: %1 of %2 episodes").arg(selectedCount).arg(eligibleTotal));
    }

    if (m_acceptButton)
        m_acceptButton->setEnabled(selectedCount > 0);
}

void StreamBulkPreflightDialog::setAllCheckStates(Qt::CheckState state)
{
    if (!m_episodeList)
        return;
    QSignalBlocker blocker(m_episodeList);
    for (int i = 0; i < m_episodeList->count(); ++i) {
        QListWidgetItem* lwItem = m_episodeList->item(i);
        if (!(lwItem->flags() & Qt::ItemIsEnabled))
            continue;
        lwItem->setCheckState(state);
    }
    rebuildSelectedSummary();
}
