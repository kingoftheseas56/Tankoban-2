#include "SeriesView.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QCollator>
#include <algorithm>

SeriesView::SeriesView(QWidget* parent)
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
    connect(backBtn, &QPushButton::clicked, this, &SeriesView::backRequested);
    headerLayout->addWidget(backBtn);

    m_titleLabel = new QLabel("Series", header);
    m_titleLabel->setObjectName("SectionTitle");
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    layout->addWidget(header);

    // Scrollable issue list
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

void SeriesView::showSeries(const QString& seriesPath, const QString& seriesName)
{
    m_seriesPath = seriesPath;
    m_titleLabel->setText(seriesName);

    // Clear existing rows
    while (m_listLayout->count()) {
        auto* item = m_listLayout->takeAt(0);
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }

    // Scan for .cbz files
    QDir dir(seriesPath);
    QStringList files = dir.entryList({"*.cbz"}, QDir::Files);

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(a, b) < 0;
    });

    for (const auto& filename : files) {
        QString fullPath = dir.absoluteFilePath(filename);
        QString displayName = QFileInfo(filename).completeBaseName();

        auto* row = new QPushButton(displayName);
        row->setObjectName("SidebarAction");
        row->setCursor(Qt::PointingHandCursor);
        row->setFixedHeight(38);
        row->setStyleSheet(
            "QPushButton { text-align: left; color: rgba(255,255,255,0.86);"
            "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 10px; padding: 6px 14px; font-size: 12px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.08);"
            "  border-color: rgba(255,255,255,0.14); }"
        );

        connect(row, &QPushButton::clicked, this, [this, fullPath]() {
            emit issueSelected(fullPath);
        });

        m_listLayout->addWidget(row);
    }

    m_listLayout->addStretch();
}
