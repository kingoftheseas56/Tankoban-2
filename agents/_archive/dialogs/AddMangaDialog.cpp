#include "AddMangaDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QDateTime>
#include <QFrame>
#include <QPixmap>
#include <QFileInfo>

static const QString GOLD = QStringLiteral("#c7a76b");
static constexpr int COVER_W = 240;
static constexpr int COVER_H = 360;
static constexpr int LEFT_PANEL_W = 260;

AddMangaDialog::AddMangaDialog(const QString& seriesTitle, const QString& source,
                               const QString& defaultDest, QWidget* parent)
    : QDialog(parent), m_seriesTitle(seriesTitle), m_source(source)
{
    setWindowTitle("Download \u2014 " + seriesTitle);
    setMinimumSize(920, 560);      // C3: wider + taller to accommodate left panel
    resize(1120, 700);
    buildUI();

    m_destEdit->setText(defaultDest);
    m_statusLabel->setText("Loading chapters...");
    m_downloadBtn->setEnabled(false);
}

void AddMangaDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    // C3: body is now a two-column layout. Left = static metadata/cover panel,
    // Right = existing chapter-picker UI. Previous top-of-dialog title/source
    // labels are subsumed into the left panel — avoids duplication.
    auto* body = new QHBoxLayout;
    body->setSpacing(14);

    // ── Left panel (detail) ────────────────────────────────────────────────
    auto* leftPanel = new QWidget;
    leftPanel->setFixedWidth(LEFT_PANEL_W);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    m_coverLabel = new QLabel;
    m_coverLabel->setObjectName("AddMangaCover");
    m_coverLabel->setFixedSize(COVER_W, COVER_H);
    m_coverLabel->setAlignment(Qt::AlignCenter);
    m_coverLabel->setStyleSheet(
        "#AddMangaCover { background: rgba(255,255,255,0.05); "
        "  border: 1px solid rgba(255,255,255,0.08); border-radius: 6px; "
        "  color: #666; font-size: 11px; }");
    m_coverLabel->setText("No cover");
    leftLayout->addWidget(m_coverLabel, 0, Qt::AlignHCenter);

    m_titleLabel = new QLabel(m_seriesTitle);
    m_titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #eee;");
    m_titleLabel->setWordWrap(true);
    leftLayout->addWidget(m_titleLabel);

    m_authorLabel = new QLabel;
    m_authorLabel->setStyleSheet("font-size: 11px; color: #bbb;");
    m_authorLabel->setWordWrap(true);
    m_authorLabel->hide();
    leftLayout->addWidget(m_authorLabel);

    m_sourceLabel = new QLabel("Source: " + mangaSourceDisplayName(m_source));
    m_sourceLabel->setStyleSheet("font-size: 11px; color: #888;");
    leftLayout->addWidget(m_sourceLabel);

    m_mangaStatusLabel = new QLabel;
    m_mangaStatusLabel->setStyleSheet("font-size: 11px; color: #888;");
    m_mangaStatusLabel->hide();
    leftLayout->addWidget(m_mangaStatusLabel);

    leftLayout->addStretch();
    body->addWidget(leftPanel);

    // ── Right panel (chapter picker) ───────────────────────────────────────
    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // Chapter count / error status
    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("font-size: 11px; color: #888;");
    rightLayout->addWidget(m_statusLabel);

    // Controls row: Select All, Deselect All, Range
    auto* ctrlRow = new QHBoxLayout;
    ctrlRow->setSpacing(8);

    auto* selectAllBtn = new QPushButton("Select All");
    selectAllBtn->setFixedHeight(28);
    selectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(selectAllBtn, &QPushButton::clicked, this, &AddMangaDialog::selectAll);
    ctrlRow->addWidget(selectAllBtn);

    auto* deselectAllBtn = new QPushButton("Deselect All");
    deselectAllBtn->setFixedHeight(28);
    deselectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(deselectAllBtn, &QPushButton::clicked, this, &AddMangaDialog::deselectAll);
    ctrlRow->addWidget(deselectAllBtn);

    ctrlRow->addStretch();

    ctrlRow->addWidget(new QLabel("From"));
    m_rangeFrom = new QSpinBox;
    m_rangeFrom->setFixedWidth(70);
    m_rangeFrom->setRange(1, 9999);
    m_rangeFrom->setValue(1);
    ctrlRow->addWidget(m_rangeFrom);

    ctrlRow->addWidget(new QLabel("To"));
    m_rangeTo = new QSpinBox;
    m_rangeTo->setFixedWidth(70);
    m_rangeTo->setRange(1, 9999);
    m_rangeTo->setValue(9999);
    ctrlRow->addWidget(m_rangeTo);

    auto* rangeBtn = new QPushButton("Select Range");
    rangeBtn->setFixedHeight(28);
    rangeBtn->setCursor(Qt::PointingHandCursor);
    connect(rangeBtn, &QPushButton::clicked, this, &AddMangaDialog::selectRange);
    ctrlRow->addWidget(rangeBtn);

    rightLayout->addLayout(ctrlRow);

    // Chapter table
    m_chapterTable = new QTableWidget(0, 3);
    m_chapterTable->setHorizontalHeaderLabels({"Select", "Chapter", "Date"});
    m_chapterTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_chapterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chapterTable->verticalHeader()->setVisible(false);

    auto* hdr = m_chapterTable->horizontalHeader();
    hdr->resizeSection(0, 60);
    hdr->setSectionResizeMode(1, QHeaderView::Stretch);
    hdr->resizeSection(2, 140);
    hdr->setMinimumSectionSize(50);

    rightLayout->addWidget(m_chapterTable, 1);

    // Destination row
    auto* destRow = new QHBoxLayout;
    destRow->addWidget(new QLabel("Destination"));
    m_destEdit = new QLineEdit;
    m_destEdit->setPlaceholderText("Select download path...");
    destRow->addWidget(m_destEdit, 1);
    auto* browseBtn = new QPushButton("...");
    browseBtn->setFixedSize(32, 28);
    browseBtn->setCursor(Qt::PointingHandCursor);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Destination", m_destEdit->text());
        if (!dir.isEmpty()) m_destEdit->setText(dir);
    });
    destRow->addWidget(browseBtn);
    rightLayout->addLayout(destRow);

    // Format row
    auto* fmtRow = new QHBoxLayout;
    fmtRow->addWidget(new QLabel("Format"));
    m_formatCombo = new QComboBox;
    m_formatCombo->addItem("CBZ Archive", "cbz");
    m_formatCombo->addItem("Image Folder", "folder");
    fmtRow->addWidget(m_formatCombo);
    fmtRow->addStretch();
    rightLayout->addLayout(fmtRow);

    body->addWidget(rightPanel, 1);
    root->addLayout(body, 1);

    // Buttons
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setFixedHeight(32);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    m_downloadBtn = new QPushButton("Download");
    m_downloadBtn->setFixedHeight(32);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    m_downloadBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: #111; font-weight: bold; border-radius: 6px; "
        "padding: 4px 24px; border: none; }"
        "QPushButton:hover { background: #d4b87a; }"
        "QPushButton:disabled { background: rgba(255,255,255,0.08); color: #555; }").arg(GOLD));
    connect(m_downloadBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(m_downloadBtn);
    root->addLayout(btnRow);
}

void AddMangaDialog::populateChapters(const QList<ChapterInfo>& chapters)
{
    m_chapters = chapters;
    m_chapterTable->setRowCount(chapters.size());

    for (int i = 0; i < chapters.size(); ++i) {
        const auto& ch = chapters[i];

        // Checkbox
        auto* checkItem = new QTableWidgetItem;
        checkItem->setCheckState(Qt::Checked);
        checkItem->setFlags(checkItem->flags() | Qt::ItemIsUserCheckable);
        m_chapterTable->setItem(i, 0, checkItem);

        // Chapter name
        auto* nameItem = new QTableWidgetItem(ch.name);
        m_chapterTable->setItem(i, 1, nameItem);

        // Date
        QString dateStr = "-";
        if (ch.dateUpload > 0) {
            dateStr = QDateTime::fromMSecsSinceEpoch(ch.dateUpload).toString("yyyy-MM-dd");
        }
        auto* dateItem = new QTableWidgetItem(dateStr);
        m_chapterTable->setItem(i, 2, dateItem);
    }

    m_downloadBtn->setEnabled(!chapters.isEmpty());
    updateStatus();
}

void AddMangaDialog::showError(const QString& message)
{
    m_statusLabel->setText("Error: " + message);
    m_statusLabel->setStyleSheet("font-size: 11px; color: rgb(220,50,50);");
}

void AddMangaDialog::selectAll()
{
    for (int i = 0; i < m_chapterTable->rowCount(); ++i)
        m_chapterTable->item(i, 0)->setCheckState(Qt::Checked);
    updateStatus();
}

void AddMangaDialog::deselectAll()
{
    for (int i = 0; i < m_chapterTable->rowCount(); ++i)
        m_chapterTable->item(i, 0)->setCheckState(Qt::Unchecked);
    updateStatus();
}

void AddMangaDialog::selectRange()
{
    int from = m_rangeFrom->value();
    int to   = m_rangeTo->value();
    for (int i = 0; i < m_chapterTable->rowCount(); ++i) {
        int num = i + 1; // 1-based
        m_chapterTable->item(i, 0)->setCheckState(
            (num >= from && num <= to) ? Qt::Checked : Qt::Unchecked);
    }
    updateStatus();
}

void AddMangaDialog::updateStatus()
{
    int selected = 0;
    for (int i = 0; i < m_chapterTable->rowCount(); ++i) {
        if (m_chapterTable->item(i, 0)->checkState() == Qt::Checked)
            ++selected;
    }
    m_statusLabel->setText(QString("%1 chapters found, %2 selected").arg(m_chapters.size()).arg(selected));
    m_statusLabel->setStyleSheet("font-size: 11px; color: #888;");
}

QList<ChapterInfo> AddMangaDialog::selectedChapters() const
{
    QList<ChapterInfo> selected;
    for (int i = 0; i < m_chapterTable->rowCount(); ++i) {
        if (m_chapterTable->item(i, 0)->checkState() == Qt::Checked && i < m_chapters.size())
            selected.append(m_chapters[i]);
    }
    return selected;
}

QString AddMangaDialog::destinationPath() const
{
    return m_destEdit->text().trimmed();
}

QString AddMangaDialog::format() const
{
    return m_formatCombo->currentData().toString();
}

// ── C3: detail panel ────────────────────────────────────────────────────────
void AddMangaDialog::setMangaMetadata(const MangaResult& result)
{
    if (!result.title.isEmpty())
        m_titleLabel->setText(result.title);

    if (!result.author.isEmpty()) {
        m_authorLabel->setText("by " + result.author);
        m_authorLabel->show();
    } else {
        m_authorLabel->hide();
    }

    if (!result.source.isEmpty())
        m_sourceLabel->setText("Source: " + mangaSourceDisplayName(result.source));

    if (!result.status.isEmpty()) {
        m_mangaStatusLabel->setText("Status: " + result.status);
        m_mangaStatusLabel->show();
    } else {
        m_mangaStatusLabel->hide();
    }
}

void AddMangaDialog::setCoverPath(const QString& path)
{
    if (!m_coverLabel) return;
    if (path.isEmpty() || !QFileInfo::exists(path)) return;   // keep placeholder
    QPixmap px(path);
    if (px.isNull()) return;
    m_coverLabel->setPixmap(
        px.scaled(COVER_W, COVER_H, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_coverLabel->setText(QString());   // clear placeholder
}
