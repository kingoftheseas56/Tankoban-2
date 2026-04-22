#include "AudiobookDetailView.h"
#include "core/BooksScanner.h"       // AudiobookInfo
#include "core/AudiobookMetaCache.h"

#include <QChar>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

// Column indices for the chapter table.
namespace {
enum Col { ColNum = 0, ColChapter, ColDuration, ColProgress, ColCount };

// Stylesheet for the "← Back" button — copied from BookSeriesView so both
// detail views visually match.
constexpr const char* kBackBtnStyle =
    "QPushButton { text-align: left; color: rgba(255,255,255,0.72);"
    "  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);"
    "  border-radius: 8px; padding: 4px 12px; font-size: 12px; }"
    "QPushButton:hover { background: rgba(255,255,255,0.08);"
    "  border-color: rgba(255,255,255,0.14); color: rgba(255,255,255,0.9); }";

// Pill-shaped "In-reader only" badge — monochrome per
// feedback_no_color_no_emoji; text-only (no unicode symbols / no SVG).
// Slightly tighter inner padding + a touch more border contrast vs. the 2.1
// scaffold so the badge reads as a first-class pill element rather than a
// plain rounded label.
constexpr const char* kBadgeStyle =
    "QLabel#AudiobookInReaderBadge {"
    "  background: rgba(255,255,255,0.07);"
    "  border: 1px solid rgba(255,255,255,0.14);"
    "  border-radius: 11px;"
    "  color: rgba(255,255,255,0.78);"
    "  padding: 3px 11px;"
    "  font-size: 11px;"
    "  font-weight: 500;"
    "  letter-spacing: 0.3px;"
    "}";
}  // namespace

AudiobookDetailView::AudiobookDetailView(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

void AudiobookDetailView::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Top bar: Back button + title (right-aligned) ──
    auto* topBar = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(16, 8, 24, 8);
    topLayout->setSpacing(8);

    m_backBtn = new QPushButton(QString::fromUtf8("←  Back"), topBar);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(kBackBtnStyle);
    connect(m_backBtn, &QPushButton::clicked,
            this, &AudiobookDetailView::backRequested);
    topLayout->addWidget(m_backBtn);

    topLayout->addStretch(1);

    m_titleLabel = new QLabel(topBar);
    m_titleLabel->setObjectName("AudiobookDetailTitle");
    m_titleLabel->setStyleSheet(
        "color: rgba(255,255,255,0.92); font-size: 15px; font-weight: 500;");
    m_titleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    topLayout->addWidget(m_titleLabel);

    layout->addWidget(topBar);

    // ── Header row: cover (left) + metadata column (right) ──
    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 16, 24, 16);
    headerLayout->setSpacing(18);

    m_coverLabel = new QLabel(header);
    m_coverLabel->setFixedSize(140, 210);
    m_coverLabel->setAlignment(Qt::AlignCenter);
    m_coverLabel->setStyleSheet(
        "background: rgba(255,255,255,0.04);"
        "border: 1px solid rgba(255,255,255,0.08);"
        "border-radius: 6px;");
    headerLayout->addWidget(m_coverLabel, 0, Qt::AlignTop);

    auto* metaCol = new QWidget(header);
    auto* metaLayout = new QVBoxLayout(metaCol);
    metaLayout->setContentsMargins(0, 4, 0, 0);
    metaLayout->setSpacing(10);

    auto* chaptersHeading = new QLabel("CHAPTERS", metaCol);
    chaptersHeading->setStyleSheet(
        "color: rgba(255,255,255,0.55); font-size: 11px;"
        "font-weight: 600; letter-spacing: 1.2px;");
    metaLayout->addWidget(chaptersHeading);

    m_metaLabel = new QLabel(metaCol);
    m_metaLabel->setStyleSheet(
        "color: rgba(255,255,255,0.85); font-size: 16px; font-weight: 500;");
    metaLayout->addWidget(m_metaLabel);

    m_badgeLabel = new QLabel(QStringLiteral("In-reader only"), metaCol);
    m_badgeLabel->setObjectName("AudiobookInReaderBadge");
    m_badgeLabel->setStyleSheet(kBadgeStyle);
    m_badgeLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    metaLayout->addWidget(m_badgeLabel, 0, Qt::AlignLeft);

    metaLayout->addStretch(1);

    headerLayout->addWidget(metaCol, 1);

    layout->addWidget(header);

    // ── Chapter table ──
    m_chapterTable = new QTableWidget(0, ColCount, this);
    m_chapterTable->setHorizontalHeaderLabels({
        QString::fromUtf8("#"),
        QString::fromUtf8("CHAPTER"),
        QString::fromUtf8("DURATION"),
        QString::fromUtf8("PROGRESS"),
    });
    m_chapterTable->setShowGrid(false);
    m_chapterTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_chapterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chapterTable->setFocusPolicy(Qt::NoFocus);
    m_chapterTable->setAlternatingRowColors(true);
    m_chapterTable->verticalHeader()->setVisible(false);
    m_chapterTable->setStyleSheet(
        "QTableWidget { background: transparent; color: rgba(255,255,255,0.85);"
        "  border: 0; gridline-color: transparent;"
        "  alternate-background-color: rgba(255,255,255,0.03); }"
        "QHeaderView::section { background: transparent;"
        "  color: rgba(255,255,255,0.55); border: 0; padding: 8px 12px;"
        "  font-size: 11px; font-weight: 600; letter-spacing: 1.2px; }"
        "QTableWidget::item { padding: 8px 12px; }");

    auto* hdr = m_chapterTable->horizontalHeader();
    hdr->setSectionResizeMode(ColNum,      QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColChapter,  QHeaderView::Stretch);
    hdr->setSectionResizeMode(ColDuration, QHeaderView::Fixed);
    hdr->setSectionResizeMode(ColProgress, QHeaderView::Fixed);
    m_chapterTable->setColumnWidth(ColNum,      60);
    m_chapterTable->setColumnWidth(ColDuration, 110);
    m_chapterTable->setColumnWidth(ColProgress, 120);

    layout->addWidget(m_chapterTable, 1);
}

QString AudiobookDetailView::formatDuration(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("?");
    const qint64 totalSec = ms / 1000;
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 s = totalSec % 60;
    if (h > 0) {
        return QString("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QString("%1:%2")
        .arg(m)
        .arg(s, 2, 10, QChar('0'));
}

void AudiobookDetailView::showAudiobook(const AudiobookInfo& audiobook)
{
    m_audiobookPath = audiobook.path;

    // Title on top bar
    m_titleLabel->setText(audiobook.name);

    // Cover
    if (!audiobook.coverPath.isEmpty()) {
        QPixmap pm(audiobook.coverPath);
        if (!pm.isNull()) {
            m_coverLabel->setPixmap(
                pm.scaled(m_coverLabel->size(),
                          Qt::KeepAspectRatio,
                          Qt::SmoothTransformation));
        } else {
            m_coverLabel->setPixmap(QPixmap());
            m_coverLabel->setText(QString());
        }
    } else {
        m_coverLabel->setPixmap(QPixmap());
        m_coverLabel->setText(QString());
    }

    // Meta line: "N chapters · HH:MM:SS"
    const int n = audiobook.trackCount;
    QString meta = QString("%1 %2").arg(n).arg(n == 1 ? "chapter" : "chapters");
    if (audiobook.totalDurationMs > 0) {
        meta += QString::fromUtf8("  ·  ") + formatDuration(audiobook.totalDurationMs);
    }
    m_metaLabel->setText(meta);

    populateChapters(audiobook);
}

void AudiobookDetailView::populateChapters(const AudiobookInfo& audiobook)
{
    m_chapterTable->setRowCount(0);
    m_chapterTable->setRowCount(audiobook.tracks.size());

    for (int row = 0; row < audiobook.tracks.size(); ++row) {
        const QString& trackPath = audiobook.tracks.at(row);

        // # column — 1-based, right-aligned.
        auto* numItem = new QTableWidgetItem(QString::number(row + 1));
        numItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chapterTable->setItem(row, ColNum, numItem);

        // Chapter column — filename without extension. For wrapper
        // audiobooks this strips the subdir prefix too (end-user sees
        // just the chapter title, not "0.5 Edgedancer/01"). Natural
        // ordering is preserved because the tracks[] array was already
        // natural-sorted by Phase 1.2's walker.
        const QFileInfo fi(trackPath);
        auto* chapterItem = new QTableWidgetItem(fi.completeBaseName());
        m_chapterTable->setItem(row, ColChapter, chapterItem);

        // Duration column — re-query AudiobookMetaCache (cache-hit after
        // Phase 1 scan → microseconds per row). Dim the cell when the
        // duration is unknown ("?" placeholder) so the reader sees it as a
        // missing-metadata signal rather than a valid value.
        const qint64 ms = AudiobookMetaCache::durationMsFor(audiobook.path, trackPath);
        auto* durationItem = new QTableWidgetItem(formatDuration(ms));
        durationItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (ms <= 0)
            durationItem->setForeground(QBrush(QColor(255, 255, 255, 110)));
        m_chapterTable->setItem(row, ColDuration, durationItem);

        // Progress column — "-" placeholder. Phase 4 fills this with
        // per-chapter listened-ms from audiobook_progress.json.
        auto* progressItem = new QTableWidgetItem(QStringLiteral("-"));
        progressItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        progressItem->setForeground(QBrush(QColor(255, 255, 255, 110)));
        m_chapterTable->setItem(row, ColProgress, progressItem);
    }
}
