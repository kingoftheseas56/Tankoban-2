#include "ui/pages/stream/TorrentPackPicker.h"
#include "core/net/NetSeam.h"

#include "core/stream/QualityScorer.h"
#include "core/TorrentIndexer.h"
#include "core/indexers/EztvIndexer.h"
#include "core/indexers/ExtTorrentsIndexer.h"
#include "core/indexers/PirateBayIndexer.h"
#include "core/indexers/X1337xIndexer.h"
#include "core/indexers/YtsIndexer.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace {

bool isCompleteSeriesName(const QString& title)
{
    static const QRegularExpression reComplete(
        QStringLiteral("(?i)\\b(complete[\\s._-]*series|complete[\\s._-]*box[\\s._-]*set|complete[\\s._-]*collection)\\b"));
    static const QRegularExpression reSeasonRange(
        QStringLiteral("(?i)\\bS\\d{1,2}[\\s._-]*[-\\s]?[\\s._-]*S\\d{1,2}\\b"));
    return reComplete.match(title).hasMatch() || reSeasonRange.match(title).hasMatch();
}

QSet<int> detectSeasonsFromTitle(const QString& title)
{
    QSet<int> seasons;
    static const QRegularExpression reRange(
        QStringLiteral("(?i)\\bS(\\d{1,2})[\\s._-]*[-\\s][\\s._-]*S(\\d{1,2})\\b"));
    const auto rangeMatch = reRange.match(title);
    if (rangeMatch.hasMatch()) {
        const int start = rangeMatch.captured(1).toInt();
        const int end = rangeMatch.captured(2).toInt();
        for (int season = start; season <= end; ++season)
            seasons.insert(season);
        return seasons;
    }

    static const QRegularExpression reSingle(QStringLiteral("(?i)\\bS(\\d{1,2})\\b"));
    auto it = reSingle.globalMatch(title);
    while (it.hasNext())
        seasons.insert(it.next().captured(1).toInt());
    return seasons;
}

int detectEpisodeCountFromTitle(const QString& title)
{
    static const QRegularExpression reCount(
        QStringLiteral("(?i)\\b(\\d{1,3})[\\s._-]*(?:eps?|episodes?)\\b"));
    const auto countMatch = reCount.match(title);
    if (countMatch.hasMatch())
        return countMatch.captured(1).toInt();

    static const QRegularExpression reRange(QStringLiteral("\\b(\\d{1,3})-(\\d{1,3})\\b"));
    const auto rangeMatch = reRange.match(title);
    if (rangeMatch.hasMatch())
        return rangeMatch.captured(2).toInt() - rangeMatch.captured(1).toInt() + 1;

    return 0;
}

}  // namespace

TorrentPackPicker::TorrentPackPicker(const QString& imdbId,
                                     const QString& showName,
                                     int season,
                                     QWidget* parent)
    : QDialog(parent)
    , m_imdbId(imdbId)
    , m_showName(showName)
    , m_season(season)
    , m_nam(tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-pack-picker")))
{
    setWindowTitle(tr("Download via Tankorent - %1 %2")
                       .arg(showName)
                       .arg(season > 0
                                ? QStringLiteral("Season %1").arg(season)
                                : QStringLiteral("(whole show)")));
    setMinimumSize(720, 480);
    buildUI();
    launchSearches();
}

void TorrentPackPicker::buildUI()
{
    auto* root = new QVBoxLayout(this);

    m_status = new QLabel(tr("Searching indexers..."), this);
    root->addWidget(m_status);

    m_list = new QListWidget(this);
    root->addWidget(m_list, 1);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_downloadBtn = new QPushButton(tr("Download"), this);
    m_downloadBtn->setEnabled(false);
    connect(m_downloadBtn, &QPushButton::clicked, this, &TorrentPackPicker::onRowDoubleClicked);

    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_downloadBtn);
    root->addLayout(btnRow);

    connect(m_list, &QListWidget::itemDoubleClicked, this, &TorrentPackPicker::onRowDoubleClicked);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_downloadBtn->setEnabled(row >= 0);
    });
}

void TorrentPackPicker::launchSearches()
{
    QStringList queries;
    if (m_season > 0) {
        queries << QStringLiteral("%1 S%2")
                       .arg(m_showName)
                       .arg(m_season, 2, 10, QLatin1Char('0'));
        queries << QStringLiteral("%1 Season %2").arg(m_showName).arg(m_season);
    } else {
        queries << QStringLiteral("%1 Complete").arg(m_showName);
        queries << QStringLiteral("%1 Complete Series").arg(m_showName);
    }

    QSettings settings;
    auto enabled = [&](const QString& id) {
        return settings.value(
            QStringLiteral("tankorent/indexers/%1/enabled").arg(id), true).toBool();
    };

    int dispatched = 0;
    auto dispatch = [&](const QString& id, TorrentIndexer* indexer, const QString& query) {
        if (!enabled(id)) {
            indexer->deleteLater();
            return;
        }

        connect(indexer, &TorrentIndexer::searchFinished,
                this, &TorrentPackPicker::onIndexerResults);
        connect(indexer, &TorrentIndexer::searchError, this, [this](const QString& error) {
            m_status->setText(tr("Some indexers failed: %1").arg(error));
        });
        indexer->search(query, 25);
        ++dispatched;
    };

    for (const QString& query : queries) {
        dispatch(QStringLiteral("piratebay"), new PirateBayIndexer(m_nam, this), query);
        dispatch(QStringLiteral("1337x"), new X1337xIndexer(m_nam, this), query);
        dispatch(QStringLiteral("yts"), new YtsIndexer(m_nam, this), query);
        dispatch(QStringLiteral("eztv"), new EztvIndexer(m_nam, this), query);
        dispatch(QStringLiteral("exttorrents"), new ExtTorrentsIndexer(m_nam, this), query);
    }

    if (dispatched == 0)
        m_status->setText(tr("No enabled Tankorent indexers."));
    else
        m_status->setText(tr("Searching %1 queries across %2 indexers...")
                              .arg(queries.size())
                              .arg(dispatched));
}

void TorrentPackPicker::onIndexerResults(const QList<TorrentResult>& results)
{
    m_allResults.append(results);
    for (const TorrentResult& result : results) {
        EnrichedResult enriched;
        enriched.raw = result;
        enriched.completeSeries = isCompleteSeriesName(result.title);
        enriched.detectedSeasons = detectSeasonsFromTitle(result.title);
        enriched.detectedEpisodeCount = detectEpisodeCountFromTitle(result.title);
        m_enriched.append(enriched);
    }
    rerankAndRender();
}

void TorrentPackPicker::rerankAndRender()
{
    QSettings settings;
    const double wQuality = settings.value(QStringLiteral("theatre/qualityWeight"), 0.6).toDouble();
    const double wHealth = 1.0 - wQuality;

    using tankostream::stream::QualityScorer;
    for (EnrichedResult& result : m_enriched) {
        const int quality = QualityScorer::qualityScore(result.raw.title);
        const int health = QualityScorer::healthScore(result.raw.seeders);
        result.combinedScore = QualityScorer::combinedScore(quality, health, wQuality, wHealth);
    }

    std::stable_sort(m_enriched.begin(), m_enriched.end(),
                     [](const EnrichedResult& a, const EnrichedResult& b) {
                         if (a.isMultiSeason() != b.isMultiSeason())
                             return a.isMultiSeason();
                         return a.combinedScore > b.combinedScore;
                     });

    m_list->clear();
    for (const EnrichedResult& result : m_enriched) {
        const QString suffix = result.isMultiSeason()
            ? tr("  [WHOLE SHOW]")
            : QString();
        m_list->addItem(QStringLiteral("%1%2 - %3 seeders - %4 MB - score %5")
                            .arg(result.raw.title)
                            .arg(suffix)
                            .arg(result.raw.seeders)
                            .arg(result.raw.sizeBytes / 1'000'000)
                            .arg(static_cast<int>(result.combinedScore)));
    }

    m_status->setText(tr("%1 packs (sorted by quality x seeders)").arg(m_enriched.size()));
}

void TorrentPackPicker::onRowDoubleClicked()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_enriched.size())
        return;
    emit packChosen(m_enriched[row].raw, m_imdbId, m_season);
    accept();
}
