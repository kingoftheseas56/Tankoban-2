#include "StreamDetailView.h"

#include "core/CoreBridge.h"
#include "core/stream/CinemetaClient.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/StreamProgress.h"
#include "core/stream/TorrentioClient.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>

StreamDetailView::StreamDetailView(CoreBridge* bridge, CinemetaClient* cinemeta,
                                   TorrentioClient* torrentio, StreamLibrary* library,
                                   QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_cinemeta(cinemeta)
    , m_torrentio(torrentio)
    , m_library(library)
{
    buildUI();

    connect(m_cinemeta, &CinemetaClient::seriesMetaReady,
            this, &StreamDetailView::onSeriesMetaReady);
}

void StreamDetailView::showEntry(const QString& imdbId)
{
    m_currentImdb = imdbId;
    m_seasons.clear();
    m_episodeTable->setRowCount(0);
    m_episodeTable->hide();
    m_seasonRow->hide();
    m_playMovieBtn->hide();
    m_statusLabel->setText("Loading...");
    m_statusLabel->show();

    StreamLibraryEntry entry = m_library->get(imdbId);
    m_currentType = entry.type;

    // Title + info
    m_titleLabel->setText(entry.name);

    QStringList info;
    if (!entry.year.isEmpty()) info << entry.year;
    if (!entry.type.isEmpty()) info << (entry.type == "series" ? "Series" : "Movie");
    if (!entry.imdbRating.isEmpty()) info << ("IMDB " + entry.imdbRating);
    m_infoLabel->setText(info.join(" \u00B7 "));

    m_descLabel->setText(entry.description);
    m_descLabel->setVisible(!entry.description.isEmpty());

    if (entry.type == "movie") {
        m_statusLabel->hide();
        m_playMovieBtn->show();
    } else {
        m_cinemeta->fetchSeriesMeta(imdbId);
    }
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void StreamDetailView::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 8, 16, 8);
    root->setSpacing(8);

    // Back button
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    m_backBtn = new QPushButton("\u2190 Back", this);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
        "  font-size: 13px; padding: 0 8px; }"
        "#SidebarAction:hover { color: #fff; }");
    connect(m_backBtn, &QPushButton::clicked, this, &StreamDetailView::backRequested);
    topRow->addWidget(m_backBtn);
    topRow->addStretch();
    root->addLayout(topRow);

    // Title
    m_titleLabel = new QLabel(this);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet("color: #e0e0e0; font-size: 16px; font-weight: bold;");
    root->addWidget(m_titleLabel);

    // Info line
    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-size: 12px;");
    root->addWidget(m_infoLabel);

    // Description
    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    m_descLabel->setMaximumHeight(60);
    m_descLabel->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 11px;");
    root->addWidget(m_descLabel);

    // Season selector row (hidden for movies)
    m_seasonRow = new QWidget(this);
    auto* seasonLayout = new QHBoxLayout(m_seasonRow);
    seasonLayout->setContentsMargins(0, 4, 0, 4);
    seasonLayout->setSpacing(8);

    auto* seasonLabel = new QLabel("Season:", m_seasonRow);
    seasonLabel->setStyleSheet("color: rgba(255,255,255,0.6); font-size: 13px;");
    seasonLayout->addWidget(seasonLabel);

    m_seasonCombo = new QComboBox(m_seasonRow);
    m_seasonCombo->setFixedWidth(120);
    m_seasonCombo->setStyleSheet(
        "QComboBox { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    connect(m_seasonCombo, &QComboBox::currentIndexChanged,
            this, &StreamDetailView::onSeasonChanged);
    seasonLayout->addWidget(m_seasonCombo);
    seasonLayout->addStretch();

    m_seasonRow->hide();
    root->addWidget(m_seasonRow);

    // Play movie button (hidden for series)
    m_playMovieBtn = new QPushButton("Play Movie", this);
    m_playMovieBtn->setFixedHeight(36);
    m_playMovieBtn->setCursor(Qt::PointingHandCursor);
    m_playMovieBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.1); border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 6px; color: #e0e0e0; font-size: 13px; font-weight: bold; padding: 0 24px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.15); }");
    connect(m_playMovieBtn, &QPushButton::clicked, this, &StreamDetailView::onPlayMovieClicked);
    m_playMovieBtn->hide();
    root->addWidget(m_playMovieBtn);

    // Episode table
    m_episodeTable = new QTableWidget(this);
    m_episodeTable->setColumnCount(4);
    m_episodeTable->setHorizontalHeaderLabels({"#", "Title", "Progress", "Status"});
    m_episodeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_episodeTable->setColumnWidth(0, 40);
    m_episodeTable->setColumnWidth(2, 80);
    m_episodeTable->setColumnWidth(3, 60);
    m_episodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_episodeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_episodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_episodeTable->setShowGrid(false);
    m_episodeTable->setAlternatingRowColors(true);
    m_episodeTable->setSortingEnabled(true);
    m_episodeTable->verticalHeader()->hide();
    m_episodeTable->setStyleSheet(
        "QTableWidget { background: transparent; border: none; color: #ccc;"
        "  alternate-background-color: rgba(255,255,255,0.03); }"
        "QTableWidget::item { padding: 4px; }"
        "QTableWidget::item:selected { background: rgba(255,255,255,0.08); }"
        "QHeaderView::section { background: rgba(255,255,255,0.05); color: rgba(255,255,255,0.5);"
        "  border: none; font-size: 11px; padding: 4px; }");

    connect(m_episodeTable, &QTableWidget::cellDoubleClicked,
            this, &StreamDetailView::onEpisodeActivated);
    m_episodeTable->hide();
    root->addWidget(m_episodeTable, 1);

    // Status
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 11px; padding: 20px;");
    root->addWidget(m_statusLabel);
}

// ─── Series metadata ─────────────────────────────────────────────────────────

void StreamDetailView::onSeriesMetaReady(const QString& imdbId,
                                          const QMap<int, QList<StreamEpisode>>& seasons)
{
    if (imdbId != m_currentImdb)
        return;

    m_seasons = seasons;
    m_statusLabel->hide();

    if (m_seasons.isEmpty()) {
        m_statusLabel->setText("No episodes found");
        m_statusLabel->show();
        return;
    }

    // Populate season combo
    m_seasonCombo->blockSignals(true);
    m_seasonCombo->clear();
    for (auto it = m_seasons.begin(); it != m_seasons.end(); ++it)
        m_seasonCombo->addItem("Season " + QString::number(it.key()), it.key());
    m_seasonCombo->blockSignals(false);

    m_seasonRow->show();
    onSeasonChanged(0);
}

void StreamDetailView::onSeasonChanged(int comboIndex)
{
    if (comboIndex < 0 || comboIndex >= m_seasonCombo->count())
        return;

    int season = m_seasonCombo->itemData(comboIndex).toInt();
    populateEpisodeTable(season);
}

void StreamDetailView::populateEpisodeTable(int season)
{
    m_episodeTable->setSortingEnabled(false);
    m_episodeTable->setRowCount(0);

    auto episodes = m_seasons.value(season);
    QJsonObject allProgress = m_bridge->allProgress("stream");

    for (const auto& ep : episodes) {
        int row = m_episodeTable->rowCount();
        m_episodeTable->insertRow(row);

        // Column 0: episode number
        auto* numItem = new QTableWidgetItem(QString::number(ep.episode));
        numItem->setTextAlignment(Qt::AlignCenter);
        numItem->setData(Qt::UserRole, ep.episode);
        m_episodeTable->setItem(row, 0, numItem);

        // Column 1: title
        auto* titleItem = new QTableWidgetItem(ep.title);
        m_episodeTable->setItem(row, 1, titleItem);

        // Column 2: progress %
        QString epKey = StreamProgress::episodeKey(m_currentImdb, season, ep.episode);
        QJsonObject state = allProgress.value(epKey).toObject();
        double pct = StreamProgress::percent(state);
        bool finished = StreamProgress::isFinished(state);

        auto* progItem = new QTableWidgetItem();
        progItem->setTextAlignment(Qt::AlignCenter);
        if (finished)
            progItem->setText("100%");
        else if (pct > 0)
            progItem->setText(QString::number(static_cast<int>(pct)) + "%");
        else
            progItem->setText("-");
        m_episodeTable->setItem(row, 2, progItem);

        // Column 3: status checkmark
        auto* statusItem = new QTableWidgetItem();
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (finished)
            statusItem->setText("\u2713");
        m_episodeTable->setItem(row, 3, statusItem);

        // Store season/episode for activation
        numItem->setData(Qt::UserRole + 1, season);
    }

    m_episodeTable->setSortingEnabled(true);
    m_episodeTable->show();
}

// ─── Play triggers ───────────────────────────────────────────────────────────

void StreamDetailView::onEpisodeActivated(int row, int /*col*/)
{
    auto* numItem = m_episodeTable->item(row, 0);
    if (!numItem) return;

    int episode = numItem->data(Qt::UserRole).toInt();
    int season  = numItem->data(Qt::UserRole + 1).toInt();

    emit playRequested(m_currentImdb, "series", season, episode);
}

void StreamDetailView::onPlayMovieClicked()
{
    emit playRequested(m_currentImdb, "movie", 0, 0);
}

void StreamDetailView::updateProgressColumn()
{
    if (m_currentType != "series" || m_seasonCombo->count() == 0)
        return;

    int season = m_seasonCombo->currentData().toInt();
    populateEpisodeTable(season);
}
