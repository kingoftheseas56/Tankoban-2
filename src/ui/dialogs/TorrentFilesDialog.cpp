#include "TorrentFilesDialog.h"
#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "core/TorrentResult.h"  // for humanSize()

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>

static const QString GLASS_BG = QStringLiteral("rgba(12, 12, 12, 0.95)");
static const QString BORDER   = QStringLiteral("rgba(255,255,255,0.08)");

TorrentFilesDialog::TorrentFilesDialog(const QString& torrentName, const QString& infoHash,
                                       TorrentClient* client, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Torrent Files");
    setMinimumSize(700, 450);
    resize(900, 560);
    setStyleSheet(QStringLiteral(
        "TorrentFilesDialog { background: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(GLASS_BG, BORDER));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    // Title
    auto* titleLabel = new QLabel(torrentName);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #eee;");
    titleLabel->setWordWrap(true);
    root->addWidget(titleLabel);

    // Tree
    m_tree = new QTreeWidget;
    m_tree->setObjectName("FilesTree");
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({"Name", "Size", "Progress"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 120);
    m_tree->header()->resizeSection(2, 100);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setStyleSheet(QStringLiteral(
        "#FilesTree { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); "
        "border-radius: 6px; color: #eee; font-size: 12px; }"
        "#FilesTree::item { padding: 2px 4px; }"
        "#FilesTree QHeaderView::section { background: #1a1a1a; color: #888; border: none; "
        "border-right: 1px solid #222; border-bottom: 1px solid #222; padding: 4px 8px; font-size: 11px; }"
    ));
    root->addWidget(m_tree, 1);

    // Close button
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setFixedHeight(28);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    // Populate
    QJsonArray files = client->engine()->torrentFiles(infoHash);
    buildFileTree(files);
}

void TorrentFilesDialog::buildFileTree(const QJsonArray& files)
{
    // Build a flat list — group by directory
    QMap<QString, QTreeWidgetItem*> dirItems;

    for (const auto& f : files) {
        QJsonObject obj = f.toObject();
        QString path = obj["name"].toString();
        qint64 size = obj["size"].toVariant().toLongLong();
        double progress = obj["progress"].toDouble();

        // Split into dir + filename
        int sep = path.lastIndexOf('/');
        QString dir = sep > 0 ? path.left(sep) : QString();
        QString name = sep > 0 ? path.mid(sep + 1) : path;

        QTreeWidgetItem* parent = nullptr;
        if (!dir.isEmpty()) {
            if (!dirItems.contains(dir)) {
                auto* dirItem = new QTreeWidgetItem(m_tree);
                dirItem->setText(0, dir);
                dirItem->setIcon(0, QIcon(":/icons/folder.svg"));
                dirItems[dir] = dirItem;
                dirItem->setExpanded(true);
            }
            parent = dirItems[dir];
        }

        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, name);
        item->setIcon(0, QIcon(":/icons/file.svg"));
        item->setText(1, humanSize(size));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, QString::number(progress * 100, 'f', 1) + "%");
        item->setTextAlignment(2, Qt::AlignCenter);
    }
}
