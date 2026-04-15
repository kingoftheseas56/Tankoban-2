#include "MangaTransferDialog.h"
#include "core/manga/MangaDownloader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>

static const QString GLASS_BG = QStringLiteral("rgba(12, 12, 12, 0.95)");
static const QString BORDER   = QStringLiteral("rgba(255,255,255,0.08)");

static QString statusColor(const QString& status)
{
    if (status == QLatin1String("completed")) return "#4CAF50";
    if (status == QLatin1String("downloading")) return "#60a5fa";
    if (status == QLatin1String("error"))     return "#ef4444";
    if (status == QLatin1String("cancelled")) return "#a1a1aa";
    return "#888";
}

// ── Per-chapter status icon delegate (A4) ───────────────────────────────────
// Mirrors the groundwork progress-icon palette: green check for completed,
// slate outline for queued, filled blue dot for downloading, red X for error,
// gray bar for cancelled. Text lives in ToolTipRole; DisplayRole is empty.
namespace {
class ChapterStatusIconDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        QStyledItemDelegate::paint(p, opt, idx);

        const QString status = idx.data(Qt::UserRole).toString();
        if (status.isEmpty()) return;

        const int size = 12;
        const QRect r  = opt.rect;
        const int cx   = r.x() + (r.width()  - size) / 2;
        const int cy   = r.y() + (r.height() - size) / 2;
        const QRect iconRect(cx, cy, size, size);

        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        if (status == QLatin1String("completed")) {
            p->setBrush(QColor("#4CAF50"));
            p->setPen(Qt::NoPen);
            p->drawEllipse(iconRect);
            QPainterPath check;
            check.moveTo(cx + 3.0,  cy + 6.5);
            check.lineTo(cx + 5.0,  cy + 8.5);
            check.lineTo(cx + 9.0,  cy + 4.0);
            p->setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(check);
        }
        else if (status == QLatin1String("downloading")) {
            p->setPen(QPen(QColor("#60a5fa"), 1.4));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(iconRect.adjusted(1, 1, -1, -1));
            p->setPen(Qt::NoPen);
            p->setBrush(QColor("#60a5fa"));
            p->drawEllipse(iconRect.center() + QPoint(1, 1), 2, 2);
        }
        else if (status == QLatin1String("queued")) {
            p->setPen(QPen(QColor("#94a3b8"), 1.4));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(iconRect.adjusted(1, 1, -1, -1));
        }
        else if (status == QLatin1String("error")) {
            p->setPen(QPen(QColor("#ef4444"), 1.6, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(cx + 3, cy + 3, cx + 9, cy + 9);
            p->drawLine(cx + 9, cy + 3, cx + 3, cy + 9);
        }
        else if (status == QLatin1String("cancelled")) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor("#6b7280"));
            p->drawRoundedRect(QRect(cx, cy + 5, size, 2), 1, 1);
        }

        p->restore();
    }
};
} // namespace

MangaTransferDialog::MangaTransferDialog(const QString& recordId, MangaDownloader* downloader,
                                         QWidget* parent)
    : QDialog(parent)
    , m_recordId(recordId)
    , m_downloader(downloader)
{
    setWindowTitle("Download Details");
    setMinimumSize(720, 480);
    resize(900, 600);
    setStyleSheet(QStringLiteral(
        "MangaTransferDialog { background: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(GLASS_BG, BORDER));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    m_titleLabel = new QLabel;
    m_titleLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #eee;");
    m_titleLabel->setWordWrap(true);
    root->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel;
    m_subtitleLabel->setStyleSheet("font-size: 11px; color: #a1a1aa;");
    m_subtitleLabel->setWordWrap(true);
    root->addWidget(m_subtitleLabel);

    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("font-size: 12px; color: #ccc;");
    root->addWidget(m_statusLabel);

    m_tree = new QTreeWidget;
    m_tree->setObjectName("MangaTransferTree");
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({"Chapter", "Status", "Images", "Notes"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 70);
    m_tree->header()->resizeSection(2, 90);
    m_tree->header()->resizeSection(3, 260);
    m_tree->setItemDelegateForColumn(1, new ChapterStatusIconDelegate(m_tree));
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setRootIsDecorated(false);
    m_tree->setStyleSheet(QStringLiteral(
        "#MangaTransferTree { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); "
        "border-radius: 6px; color: #eee; font-size: 12px; }"
        "#MangaTransferTree::item { padding: 2px 4px; }"
        "#MangaTransferTree QHeaderView::section { background: #1a1a1a; color: #888; border: none; "
        "border-right: 1px solid #222; border-bottom: 1px solid #222; padding: 4px 8px; font-size: 11px; }"
    ));
    root->addWidget(m_tree, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setFixedHeight(28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    refresh();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MangaTransferDialog::refresh);
    m_refreshTimer->start(1000);
}

void MangaTransferDialog::refresh()
{
    if (!m_downloader) return;

    const auto active = m_downloader->listActive();
    const MangaDownloadRecord* rec = nullptr;
    for (const auto& r : active) {
        if (r.id == m_recordId) { rec = &r; break; }
    }
    if (!rec) {
        m_titleLabel->setText("(download no longer active)");
        m_subtitleLabel->clear();
        m_statusLabel->clear();
        m_tree->clear();
        if (m_refreshTimer) m_refreshTimer->stop();
        return;
    }

    m_titleLabel->setText(rec->seriesTitle);
    m_subtitleLabel->setText(QStringLiteral("Source: %1  •  %2  •  %3")
        .arg(rec->source, rec->format.toUpper(), rec->destinationPath));

    QString progressLine = QStringLiteral("<span style='color:%1'>%2</span>  •  %3 / %4 chapters  •  %5%")
        .arg(statusColor(rec->status),
             rec->status,
             QString::number(rec->completedChapters),
             QString::number(rec->totalChapters),
             QString::number(int(rec->progress * 100)));
    m_statusLabel->setText(progressLine);

    // Rebuild the tree (cheap — it's just a refresh every 1s).
    m_tree->clear();
    for (const auto& ch : rec->chapters) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, ch.chapterName.isEmpty() ? ch.chapterId : ch.chapterName);

        // Column 1: 12x12 icon painted by ChapterStatusIconDelegate.
        // DisplayRole stays empty; UserRole carries the status string the
        // delegate reads, ToolTipRole surfaces the readable word on hover.
        item->setData(1, Qt::UserRole, ch.status);
        item->setToolTip(1, ch.status);

        QString imgs;
        if (ch.totalImages > 0)
            imgs = QStringLiteral("%1 / %2").arg(ch.downloadedImages).arg(ch.totalImages);
        else if (ch.status == QLatin1String("queued"))
            imgs = QStringLiteral("—");
        else
            imgs = QString::number(ch.downloadedImages);
        item->setText(2, imgs);

        QString notes;
        if (!ch.error.isEmpty())
            notes = ch.error;
        else if (ch.failedImages > 0)
            notes = QStringLiteral("%1 image(s) failed").arg(ch.failedImages);
        else if (ch.status == QLatin1String("completed") && !ch.finalPath.isEmpty())
            notes = ch.finalPath;
        item->setText(3, notes);
        item->setToolTip(3, notes);
    }
}
