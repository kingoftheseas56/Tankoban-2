// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 4.2 (Multi-Source Stream Aggregation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:187
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:189
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:190
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:191
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:192
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:193
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/TorrentPickerDialog.h:11
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/TorrentPickerDialog.cpp:56
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/TorrentPickerDialog.cpp:57
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/TorrentPickerDialog.cpp:116
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamProgress.h:69
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamProgress.h:71
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:291
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:305
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamSource.h:10
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/4.1_stream_aggregator.cpp:782
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/4.1_stream_aggregator.cpp:783
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/4.1_stream_aggregator.cpp:587
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 4.2.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QDialog>
#include <QHash>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace tankostream::addon {

struct StreamSource {
    enum class Kind {
        Url,
        Magnet,
        YouTube,
        Http,
    };

    Kind kind = Kind::Url;
    QUrl url;
    QString infoHash;
    int fileIndex = -1;
    QString fileNameHint;
    QStringList trackers;
    QString youtubeId;
};

struct StreamBehaviorHints {
    bool notWebReady = false;
    QString bingeGroup;
    QStringList countryWhitelist;
    QHash<QString, QString> proxyRequestHeaders;
    QHash<QString, QString> proxyResponseHeaders;
    QString filename;
    QString videoHash;
    qint64 videoSize = 0;
    QVariantMap other;
};

struct Stream {
    StreamSource source;
    QString name;
    QString description;
    QUrl thumbnail;
    StreamBehaviorHints behaviorHints;
};

} // namespace tankostream::addon

namespace tankostream::stream {

using tankostream::addon::Stream;
using tankostream::addon::StreamSource;

namespace {

static QString humanSize(qint64 bytes)
{
    if (bytes <= 0) {
        return QStringLiteral("-");
    }
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double val = static_cast<double>(bytes);
    while (val >= 1024.0 && i < 4) {
        val /= 1024.0;
        ++i;
    }
    return QString::number(val, 'f', i > 0 ? 1 : 0) + QLatin1Char(' ') + units[i];
}

static int qualityRank(const QString& qualityText)
{
    const QString q = qualityText.toLower();
    if (q.contains(QStringLiteral("2160")) || q.contains(QStringLiteral("4k"))) {
        return 5;
    }
    if (q.contains(QStringLiteral("1440"))) {
        return 4;
    }
    if (q.contains(QStringLiteral("1080"))) {
        return 3;
    }
    if (q.contains(QStringLiteral("720"))) {
        return 2;
    }
    if (q.contains(QStringLiteral("480"))) {
        return 1;
    }
    return 0;
}

static QString prettyKind(StreamSource::Kind kind)
{
    switch (kind) {
    case StreamSource::Kind::Magnet: return QStringLiteral("magnet");
    case StreamSource::Kind::Http: return QStringLiteral("http");
    case StreamSource::Kind::Url: return QStringLiteral("url");
    case StreamSource::Kind::YouTube: return QStringLiteral("youtube");
    }
    return QStringLiteral("url");
}

static qint64 extractSizeBytes(const Stream& stream)
{
    if (stream.behaviorHints.other.contains(QStringLiteral("sizeBytes"))) {
        return stream.behaviorHints.other.value(QStringLiteral("sizeBytes")).toLongLong();
    }
    return stream.behaviorHints.videoSize;
}

static int extractSeeders(const Stream& stream)
{
    return stream.behaviorHints.other.value(QStringLiteral("seeders")).toInt(0);
}

static QString extractQuality(const Stream& stream)
{
    const QString q = stream.behaviorHints.other.value(QStringLiteral("qualityLabel")).toString().trimmed();
    if (!q.isEmpty()) {
        return q;
    }

    // Fallback for addons that don't set qualityLabel.
    static const QRegularExpression kResolutionRe(QStringLiteral("\\b(2160p|1080p|720p|480p|4k)\\b"),
                                                  QRegularExpression::CaseInsensitiveOption);
    const auto m1 = kResolutionRe.match(stream.name + QLatin1Char(' ') + stream.description);
    if (m1.hasMatch()) {
        return m1.captured(1).toUpper();
    }
    return QStringLiteral("-");
}

} // namespace

struct StreamPickerChoice {
    Stream stream;
    QString addonId;
    QString addonName;
    QString sourceKind;

    // Convenience extraction for existing magnet-based code paths.
    QString magnetUri;
    QString infoHash;
    int fileIndex = -1;
    QString fileNameHint;
};

class StreamPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit StreamPickerDialog(const QList<Stream>& streams,
                                const QHash<QString, QString>& addonsById,
                                QWidget* parent = nullptr)
        : QDialog(parent)
        , m_streams(streams)
        , m_addonsById(addonsById)
    {
        setWindowTitle(QStringLiteral("Select Stream Source"));
        setMinimumSize(1060, 540);
        resize(1060, 540);
        buildUi();
    }

    bool hasSelection() const { return m_selectedRow >= 0; }

    StreamPickerChoice selectedChoice() const
    {
        if (m_selectedRow < 0 || m_selectedRow >= m_rows.size()) {
            return {};
        }
        const RowModel& row = m_rows[m_selectedRow];

        StreamPickerChoice choice;
        choice.stream = row.stream;
        choice.addonId = row.addonId;
        choice.addonName = row.addonName;
        choice.sourceKind = prettyKind(row.stream.source.kind);
        choice.infoHash = row.stream.source.infoHash;
        choice.fileIndex = row.stream.source.fileIndex;
        choice.fileNameHint = row.stream.source.fileNameHint;
        choice.magnetUri = buildMagnetUri(row.stream);
        return choice;
    }

private:
    struct RowModel {
        Stream stream;
        QString addonId;
        QString addonName;
        QString sourceText;
        QString titleText;
        QString qualityText;
        qint64 sizeBytes = 0;
        int seeders = 0;
        int qualitySort = 0;
    };

    static QString addonLabel(const QString& addonName)
    {
        if (addonName.isEmpty()) {
            return QStringLiteral("[?] Unknown");
        }
        const QString glyph = addonName.left(1).toUpper();
        return QStringLiteral("[%1] %2").arg(glyph, addonName);
    }

    static QString buildMagnetUri(const Stream& stream)
    {
        if (stream.source.kind != StreamSource::Kind::Magnet || stream.source.infoHash.isEmpty()) {
            return {};
        }

        QString uri = QStringLiteral("magnet:?xt=urn:btih:") + stream.source.infoHash.toLower();
        for (const QString& tracker : stream.source.trackers) {
            uri += QStringLiteral("&tr=")
                + QString::fromUtf8(QUrl::toPercentEncoding(tracker));
        }
        return uri;
    }

    void buildRows()
    {
        m_rows.clear();
        m_rows.reserve(m_streams.size());

        for (const Stream& stream : m_streams) {
            RowModel row;
            row.stream = stream;
            row.addonId = stream.behaviorHints.other.value(QStringLiteral("originAddonId")).toString().trimmed();
            row.addonName = stream.behaviorHints.other.value(QStringLiteral("originAddonName")).toString().trimmed();
            if (row.addonName.isEmpty() && m_addonsById.contains(row.addonId)) {
                row.addonName = m_addonsById.value(row.addonId);
            }

            const bool isDirect =
                stream.source.kind == StreamSource::Kind::Http
                || stream.source.kind == StreamSource::Kind::Url;

            row.sourceText = isDirect ? QStringLiteral("direct") : addonLabel(row.addonName);
            row.titleText = !stream.name.isEmpty() ? stream.name : stream.description;
            if (row.titleText.isEmpty()) {
                row.titleText = QStringLiteral("(untitled stream)");
            }
            row.qualityText = extractQuality(stream);
            row.sizeBytes = extractSizeBytes(stream);
            row.seeders = stream.source.kind == StreamSource::Kind::Magnet
                ? extractSeeders(stream)
                : 0;
            row.qualitySort = qualityRank(row.qualityText);

            m_rows.push_back(row);
        }

        // Sort primary: magnet seeders desc when present; otherwise quality ladder.
        std::stable_sort(m_rows.begin(), m_rows.end(), [](const RowModel& a, const RowModel& b) {
            const bool aMagWithSeeders =
                a.stream.source.kind == StreamSource::Kind::Magnet && a.seeders > 0;
            const bool bMagWithSeeders =
                b.stream.source.kind == StreamSource::Kind::Magnet && b.seeders > 0;

            if (aMagWithSeeders != bMagWithSeeders) {
                return aMagWithSeeders; // true first
            }
            if (aMagWithSeeders && bMagWithSeeders && a.seeders != b.seeders) {
                return a.seeders > b.seeders;
            }

            if (a.qualitySort != b.qualitySort) {
                return a.qualitySort > b.qualitySort;
            }
            if (a.sizeBytes != b.sizeBytes) {
                return a.sizeBytes > b.sizeBytes;
            }
            return a.titleText.toLower() < b.titleText.toLower();
        });
    }

    void buildUi()
    {
        buildRows();

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(8);

        m_infoLabel = new QLabel(QString::number(m_rows.size()) + QStringLiteral(" streams available"), this);
        m_infoLabel->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.55); font-size: 12px;"));
        root->addWidget(m_infoLabel);

        m_table = new QTableWidget(this);
        m_table->setColumnCount(6);
        m_table->setHorizontalHeaderLabels({
            QStringLiteral("Source"),
            QStringLiteral("Stream"),
            QStringLiteral("Quality"),
            QStringLiteral("Size"),
            QStringLiteral("Seeders"),
            QStringLiteral("Origin"),
        });

        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        m_table->verticalHeader()->hide();
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setShowGrid(false);
        m_table->setAlternatingRowColors(true);
        m_table->setSortingEnabled(false);
        m_table->setStyleSheet(
            "QTableWidget { background: #1a1a1a; border: 1px solid rgba(255,255,255,0.1);"
            " color: #d1d5db; alternate-background-color: rgba(255,255,255,0.03); }"
            "QTableWidget::item { padding: 4px; }"
            "QTableWidget::item:selected { background: rgba(255,255,255,0.11); }"
            "QHeaderView::section { background: rgba(255,255,255,0.06); color: rgba(255,255,255,0.55);"
            " border: none; font-size: 11px; padding: 4px; }");

        for (int i = 0; i < m_rows.size(); ++i) {
            const RowModel& row = m_rows[i];
            const int tableRow = m_table->rowCount();
            m_table->insertRow(tableRow);

            auto* sourceItem = new QTableWidgetItem(row.sourceText);
            sourceItem->setData(Qt::UserRole, i); // row-model index
            m_table->setItem(tableRow, 0, sourceItem);

            auto* titleItem = new QTableWidgetItem(row.titleText);
            m_table->setItem(tableRow, 1, titleItem);

            auto* qualityItem = new QTableWidgetItem(row.qualityText);
            qualityItem->setData(Qt::UserRole, row.qualitySort);
            m_table->setItem(tableRow, 2, qualityItem);

            auto* sizeItem = new QTableWidgetItem(humanSize(row.sizeBytes));
            sizeItem->setData(Qt::UserRole, row.sizeBytes);
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(tableRow, 3, sizeItem);

            auto* seedersItem = new QTableWidgetItem();
            seedersItem->setText(row.stream.source.kind == StreamSource::Kind::Magnet
                ? QString::number(row.seeders)
                : QStringLiteral("-"));
            seedersItem->setData(Qt::UserRole, row.seeders);
            seedersItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(tableRow, 4, seedersItem);

            auto* originItem = new QTableWidgetItem(row.addonId);
            m_table->setItem(tableRow, 5, originItem);
        }

        connect(m_table, &QTableWidget::cellDoubleClicked,
                this, &StreamPickerDialog::onRowDoubleClicked);
        root->addWidget(m_table, 1);

        auto* buttons = new QHBoxLayout();
        buttons->addStretch();

        m_selectBtn = new QPushButton(QStringLiteral("Select"), this);
        m_selectBtn->setFixedHeight(30);
        m_selectBtn->setCursor(Qt::PointingHandCursor);
        connect(m_selectBtn, &QPushButton::clicked,
                this, &StreamPickerDialog::onSelectClicked);
        buttons->addWidget(m_selectBtn);

        m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
        m_cancelBtn->setFixedHeight(30);
        m_cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(m_cancelBtn, &QPushButton::clicked,
                this, &QDialog::reject);
        buttons->addWidget(m_cancelBtn);

        root->addLayout(buttons);
        setStyleSheet(QStringLiteral("QDialog { background: #141414; }"));
    }

    void onRowDoubleClicked(int row, int /*col*/)
    {
        auto* item = m_table->item(row, 0);
        if (!item) {
            return;
        }
        m_selectedRow = item->data(Qt::UserRole).toInt(-1);
        if (m_selectedRow >= 0) {
            accept();
        }
    }

    void onSelectClicked()
    {
        const auto selected = m_table->selectedItems();
        if (selected.isEmpty()) {
            return;
        }
        auto* item = m_table->item(selected.first()->row(), 0);
        if (!item) {
            return;
        }
        m_selectedRow = item->data(Qt::UserRole).toInt(-1);
        if (m_selectedRow >= 0) {
            accept();
        }
    }

    QTableWidget* m_table = nullptr;
    QLabel* m_infoLabel = nullptr;
    QPushButton* m_selectBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    QList<Stream> m_streams;
    QHash<QString, QString> m_addonsById;
    QList<RowModel> m_rows;
    int m_selectedRow = -1;
};

} // namespace tankostream::stream

// -----------------------------------------------------------------
// StreamChoices extension sketch (Batch 4.2 scope)
// -----------------------------------------------------------------
//
// Existing save/load API in StreamProgress.h can stay:
//   StreamChoices::saveChoice(epKey, QJsonObject)
//   StreamChoices::loadChoice(epKey) -> QJsonObject
//
// Add fields to persisted choice payload:
//   choice["sourceKind"] = selected.sourceKind;      // "magnet" | "http" | "url" | "youtube"
//   choice["addonId"]    = selected.addonId;         // origin addon id
//   choice["addonName"]  = selected.addonName;       // optional display helper
//   choice["directUrl"]  = selected.stream.source.url.toString(); // for url/http path
//   choice["youtubeId"]  = selected.stream.source.youtubeId;      // for deferred path
//
// Keep existing fields for backward compatibility:
//   magnetUri, infoHash, fileIndex, quality, trackerSource, fileNameHint
//
// -----------------------------------------------------------------
// StreamPage call-site sketch (Batch 4.2 scope)
// -----------------------------------------------------------------
//
// 1) Include rename:
//      #include "stream/StreamPickerDialog.h"
//
// 2) Replace old dialog construction:
//      StreamPickerDialog dlg(streams, addonsById, this);
//      if (dlg.exec() != QDialog::Accepted || !dlg.hasSelection()) return;
//      StreamPickerChoice selected = dlg.selectedChoice();
//
// 3) Persist extended choice payload:
//      QJsonObject choice;
//      choice["sourceKind"] = selected.sourceKind;
//      choice["addonId"] = selected.addonId;
//      choice["magnetUri"] = selected.magnetUri;
//      choice["infoHash"] = selected.infoHash;
//      choice["fileIndex"] = selected.fileIndex;
//      choice["fileNameHint"] = selected.fileNameHint;
//      choice["directUrl"] = selected.stream.source.url.toString();
//      StreamChoices::saveChoice(epKey, choice);
//
// 4) Player handoff:
//      Batch 4.2 can keep magnet path unchanged.
//      Batch 4.3 consumes selected.sourceKind for direct-url bypass in StreamEngine.
//
