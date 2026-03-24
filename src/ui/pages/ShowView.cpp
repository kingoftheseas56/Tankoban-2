#include "ShowView.h"
#include "core/ScannerUtils.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QCollator>
#include <QIcon>
#include <algorithm>

static const QStringList VIDEO_EXTS = {
    "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov", "*.wmv", "*.flv",
    "*.m4v", "*.ts", "*.mpg", "*.mpeg", "*.ogv"
};

ShowView::ShowView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header bar
    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 14, 24, 14);
    headerLayout->setSpacing(12);

    auto* backBtn = new QPushButton(QChar(0x2190), header); // ←
    backBtn->setObjectName("IconButton");
    backBtn->setFixedSize(32, 28);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setToolTip("Back to library");
    connect(backBtn, &QPushButton::clicked, this, &ShowView::backRequested);
    headerLayout->addWidget(backBtn);

    m_titleLabel = new QLabel("Show", header);
    m_titleLabel->setObjectName("SectionTitle");
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    layout->addWidget(header);

    // Scrollable episode list
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("background: transparent;");

    auto* listWidget = new QWidget();
    listWidget->setStyleSheet("background: transparent;");
    m_listLayout = new QVBoxLayout(listWidget);
    m_listLayout->setContentsMargins(24, 8, 24, 24);
    m_listLayout->setSpacing(6);

    scroll->setWidget(listWidget);
    layout->addWidget(scroll, 1);
}

void ShowView::showFolder(const QString& folderPath, const QString& displayName)
{
    // Track root on first call
    if (m_showRootPath.isEmpty()) {
        m_showRootPath = folderPath;
        m_showRootName = displayName;
    }
    m_currentPath = folderPath;
    m_titleLabel->setText(displayName);

    // Clear existing rows
    while (m_listLayout->count()) {
        auto* item = m_listLayout->takeAt(0);
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }

    QCollator collator;
    collator.setNumericMode(true);

    // ".." back row if we're inside a subfolder
    if (m_currentPath != m_showRootPath) {
        auto* upRow = new QPushButton(QString(QChar(0x2190)) + "  ..", this);
        upRow->setObjectName("SidebarAction");
        upRow->setCursor(Qt::PointingHandCursor);
        upRow->setFixedHeight(38);
        upRow->setStyleSheet(
            "QPushButton { text-align: left; color: rgba(255,255,255,0.58);"
            "  background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.06);"
            "  border-radius: 10px; padding: 6px 14px; font-size: 12px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.07);"
            "  border-color: rgba(255,255,255,0.12); }");

        QString parentPath = QFileInfo(m_currentPath).absolutePath();
        QString parentName = (parentPath == m_showRootPath)
            ? m_showRootName
            : m_showRootName + " > " + QDir(parentPath).dirName();

        connect(upRow, &QPushButton::clicked, this, [this, parentPath, parentName]() {
            showFolder(parentPath, parentName);
        });
        m_listLayout->addWidget(upRow);
    }

    // List immediate subdirectories (seasons/folders)
    QStringList subdirs = ScannerUtils::listImmediateSubdirs(folderPath);
    std::sort(subdirs.begin(), subdirs.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(QDir(a).dirName(), QDir(b).dirName()) < 0;
    });

    QIcon folderIcon(":/icons/folder.svg");

    for (const auto& subdir : subdirs) {
        // Count video files in subfolder
        QStringList files = ScannerUtils::walkFiles(subdir, VIDEO_EXTS);
        if (files.isEmpty())
            continue;

        QString dirName = QDir(subdir).dirName();
        QString label = QString("  %1    (%2 %3)")
            .arg(dirName)
            .arg(files.size())
            .arg(files.size() == 1 ? "episode" : "episodes");

        auto* row = new QPushButton(folderIcon, label, this);
        row->setObjectName("SidebarAction");
        row->setCursor(Qt::PointingHandCursor);
        row->setFixedHeight(38);
        row->setIconSize(QSize(18, 18));
        row->setStyleSheet(
            "QPushButton { text-align: left; color: rgba(255,255,255,0.86);"
            "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 10px; padding: 6px 14px; font-size: 12px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.08);"
            "  border-color: rgba(255,255,255,0.14); }");

        QString subdirName = m_showRootName + " > " + dirName;
        connect(row, &QPushButton::clicked, this, [this, subdir, subdirName]() {
            showFolder(subdir, subdirName);
        });
        m_listLayout->addWidget(row);
    }

    // List video files directly in this folder
    QDir dir(folderPath);
    auto fileInfos = dir.entryInfoList(VIDEO_EXTS, QDir::Files);
    std::sort(fileInfos.begin(), fileInfos.end(),
              [&collator](const QFileInfo& a, const QFileInfo& b) {
        return collator.compare(a.fileName(), b.fileName()) < 0;
    });

    for (const auto& fi : fileInfos) {
        QString fullPath = fi.absoluteFilePath();
        QString displayText = ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName());
        QString sizeText = formatSize(fi.size());

        auto* row = new QPushButton(this);
        row->setObjectName("SidebarAction");
        row->setCursor(Qt::PointingHandCursor);
        row->setFixedHeight(38);

        // Use a layout inside the button for left-aligned name + right-aligned size
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(14, 0, 14, 0);
        rowLayout->setSpacing(12);

        auto* nameLabel = new QLabel(displayText, row);
        nameLabel->setStyleSheet("color: rgba(255,255,255,0.86); font-size: 12px; background: transparent; border: none;");
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(nameLabel, 1);

        auto* sizeLabel = new QLabel(sizeText, row);
        sizeLabel->setStyleSheet("color: rgba(255,255,255,0.38); font-size: 11px; background: transparent; border: none;");
        sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(sizeLabel);

        row->setStyleSheet(
            "QPushButton { background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 10px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.08);"
            "  border-color: rgba(255,255,255,0.14); }");

        connect(row, &QPushButton::clicked, this, [this, fullPath]() {
            emit episodeSelected(fullPath);
        });
        m_listLayout->addWidget(row);
    }

    m_listLayout->addStretch();
}

QString ShowView::formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024 * 1024)) + " MB";
    double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return QString::number(gb, 'f', 1) + " GB";
}
