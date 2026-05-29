#include "StreamDownloadsPage.h"

#include "core/torrent/TorrentClient.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/addon/MetaItem.h"

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Mirrors the on-disk poster cache used by StreamLibraryLayout / StreamDetailView /
// StreamSearchWidget / StreamContinueStrip: <GenericData>/Tankoban/data/stream_posters/<imdbId>.jpg.
// The ".jpg" extension is load-bearing — those consumers enumerate "*.jpg" and
// cleanupOrphanPosters only GCs .jpg files, so an extensionless file would be a
// private, never-shared, never-collected cache.
QString posterCachePath(const QString& imdbId)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/Tankoban/data/stream_posters/") + imdbId
           + QStringLiteral(".jpg");
}

// " · " separator built from a code point so the source stays ASCII (the build
// does not force MSVC /utf-8, so raw UTF-8 in literals would be misread).
QString dotSep()
{
    return QStringLiteral("  ") + QChar(0x00B7) + QStringLiteral("  ");
}

QString prettifyFilenameTitle(QString text)
{
    text = QFileInfo(text).completeBaseName();
    text.replace(QRegularExpression(QStringLiteral("[._]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text = text.trimmed();

    const QRegularExpression episodeRe(QStringLiteral("\\bS\\d{1,2}E\\d{1,3}\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression seasonRe(QStringLiteral("\\bS\\d{1,2}\\b"),
                                      QRegularExpression::CaseInsensitiveOption);

    int cutAt = -1;
    const QRegularExpressionMatch episodeMatch = episodeRe.match(text);
    if (episodeMatch.hasMatch()) {
        cutAt = episodeMatch.capturedStart();
    } else {
        const QRegularExpressionMatch seasonMatch = seasonRe.match(text);
        if (seasonMatch.hasMatch())
            cutAt = seasonMatch.capturedStart();
    }

    if (cutAt > 0)
        text = text.left(cutAt).trimmed();

    text.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]")));
    text.remove(QRegularExpression(QStringLiteral("\\([^\\)]*\\)")));
    text = text.trimmed();
    return text;
}

QString bestTitleFromEntries(const QList<StreamDownloadIndex::Entry>& entries,
                             const QString& fallback)
{
    for (const auto& entry : entries) {
        const QString title = prettifyFilenameTitle(entry.canonicalPath);
        if (!title.isEmpty() && title != fallback)
            return title;
    }
    return fallback;
}

QString bestTitleFromGroups(const QList<QJsonObject>& groups, const QString& fallback)
{
    for (const QJsonObject& group : groups) {
        const QString direct = group.value(QStringLiteral("showTitle")).toString(
            group.value(QStringLiteral("title")).toString());
        if (!direct.isEmpty())
            return direct;

        const QJsonArray items = group.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& value : items) {
            const QJsonObject item = value.toObject();
            const QString filename = item.value(QStringLiteral("canonicalFilename")).toString(
                item.value(QStringLiteral("canonicalPath")).toString());
            const QString title = prettifyFilenameTitle(filename);
            if (!title.isEmpty() && title != fallback)
                return title;
        }
    }
    return fallback;
}

} // namespace

StreamDownloadsPage::StreamDownloadsPage(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("StreamDownloadsPage");
    buildUi();
}

void StreamDownloadsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Topbar: back button + title
    auto* topbar = new QFrame(this);
    topbar->setObjectName("StreamDownloadsTopbar");
    topbar->setFixedHeight(48);
    auto* topbarLayout = new QHBoxLayout(topbar);
    topbarLayout->setContentsMargins(14, 6, 14, 6);
    topbarLayout->setSpacing(10);

    m_backBtn = new QPushButton(tr("< Back"), topbar);
    m_backBtn->setObjectName("StreamDownloadsBackBtn");
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFixedHeight(28);
    connect(m_backBtn, &QPushButton::clicked, this, &StreamDownloadsPage::backRequested);

    m_titleLabel = new QLabel(tr("Downloads"), topbar);
    m_titleLabel->setObjectName("StreamDownloadsTitle");
    m_titleLabel->setStyleSheet(
        "QLabel#StreamDownloadsTitle { font-size: 16pt; font-weight: 600; color: #eeeeee; }");

    topbarLayout->addWidget(m_backBtn, 0);
    topbarLayout->addWidget(m_titleLabel, 0);
    topbarLayout->addStretch(1);

    root->addWidget(topbar, 0);

    // Scrollable body
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("StreamDownloadsScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_scrollContent = new QWidget(m_scroll);
    m_scrollContent->setObjectName("StreamDownloadsScrollContent");
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(20, 12, 20, 20);
    m_contentLayout->setSpacing(18);

    // Active section
    m_activeHeader = new QLabel(tr("ACTIVE"), m_scrollContent);
    m_activeHeader->setObjectName("StreamDownloadsSectionHeader");
    m_activeHeader->setStyleSheet(
        "QLabel#StreamDownloadsSectionHeader { font-size: 9pt; font-weight: 700;"
        " color: rgba(255,255,255,0.55); letter-spacing: 1.2px; }");

    m_activeBody = new QWidget(m_scrollContent);
    m_activeBody->setObjectName("StreamDownloadsActiveBody");
    m_activeBodyLayout = new QVBoxLayout(m_activeBody);
    m_activeBodyLayout->setContentsMargins(0, 0, 0, 0);
    m_activeBodyLayout->setSpacing(8);

    // History section
    m_historyHeader = new QLabel(tr("HISTORY"), m_scrollContent);
    m_historyHeader->setObjectName("StreamDownloadsSectionHeader");
    m_historyHeader->setStyleSheet(
        "QLabel#StreamDownloadsSectionHeader { font-size: 9pt; font-weight: 700;"
        " color: rgba(255,255,255,0.55); letter-spacing: 1.2px; }");

    m_historyBody = new QWidget(m_scrollContent);
    m_historyBody->setObjectName("StreamDownloadsHistoryBody");
    m_historyBodyLayout = new QVBoxLayout(m_historyBody);
    m_historyBodyLayout->setContentsMargins(0, 0, 0, 0);
    m_historyBodyLayout->setSpacing(8);

    // Empty state placeholder (shown when both sections have zero rows).
    m_emptyState = new QLabel(
        tr("No downloads yet.\n\nDispatch a season pack from any Theatre detail view."),
        m_scrollContent);
    m_emptyState->setObjectName("StreamDownloadsEmptyState");
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(
        "QLabel#StreamDownloadsEmptyState { color: rgba(255,255,255,0.45);"
        " font-size: 11pt; padding: 60px 20px; }");

    m_contentLayout->addWidget(m_activeHeader, 0);
    m_contentLayout->addWidget(m_activeBody, 0);
    m_contentLayout->addWidget(m_historyHeader, 0);
    m_contentLayout->addWidget(m_historyBody, 0);
    m_contentLayout->addWidget(m_emptyState, 0);
    m_contentLayout->addStretch(1);

    m_scroll->setWidget(m_scrollContent);
    root->addWidget(m_scroll, 1);
}

void StreamDownloadsPage::setTorrentClient(TorrentClient* client)
{
    if (m_torrentClient == client)
        return;
    if (m_torrentClient) {
        disconnect(m_torrentClient, nullptr, this, nullptr);
    }
    m_torrentClient = client;
    if (m_torrentClient) {
        connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
                this, [this](const QString&) { refreshActive(); },
                Qt::QueuedConnection);
    }
    refreshActive();
}

void StreamDownloadsPage::setStreamDownloadIndex(StreamDownloadIndex* index)
{
    if (m_streamDownloadIndex == index)
        return;
    if (m_streamDownloadIndex) {
        disconnect(m_streamDownloadIndex, nullptr, this, nullptr);
    }
    m_streamDownloadIndex = index;
    if (m_streamDownloadIndex) {
        connect(m_streamDownloadIndex, &StreamDownloadIndex::entriesChanged,
                this, &StreamDownloadsPage::refreshHistory,
                Qt::QueuedConnection);
    }
    refreshHistory();
}

void StreamDownloadsPage::setMetaAggregator(tankostream::stream::MetaAggregator* agg)
{
    if (m_metaAggregator == agg)
        return;
    if (m_metaAggregator)
        disconnect(m_metaAggregator, nullptr, this, nullptr);
    m_metaAggregator = agg;
    if (m_metaAggregator) {
        connect(m_metaAggregator,
                &tankostream::stream::MetaAggregator::metaItemReady,
                this, &StreamDownloadsPage::onMetaItemReady,
                Qt::UniqueConnection);
    }
    // Re-render so the enrichment fetch fires now that the provider exists.
    refreshHistory();
    refreshActive();
}

QWidget* StreamDownloadsPage::makePosterWidget(const QString& imdbId, const QString& title)
{
    auto* pl = new QLabel;
    pl->setObjectName("StreamDownloadsPoster");
    pl->setFixedSize(96, 144);
    pl->setScaledContents(true);
    pl->setAlignment(Qt::AlignCenter);
    pl->setWordWrap(true);
    pl->setStyleSheet(
        "QLabel#StreamDownloadsPoster {"
        "  border-top-left-radius: 12px; border-bottom-left-radius: 12px;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 rgba(255,255,255,0.10), stop:1 rgba(255,255,255,0.03));"
        "  color: rgba(255,255,255,0.45); font-size: 9pt; padding: 6px;"
        "}");

    const QString path = posterCachePath(imdbId);
    QPixmap pm;
    if (QFile::exists(path) && pm.load(path)) {
        pl->setPixmap(pm.scaled(96, 144, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation));
    } else {
        pl->setText(title);  // placeholder until art loads
    }
    return pl;
}

void StreamDownloadsPage::savePosterFrom(const QString& imdbId, const QUrl& posterUrl)
{
    if (posterUrl.isEmpty() || QFile::exists(posterCachePath(imdbId)))
        return;
    if (!m_posterNam)
        m_posterNam = new QNetworkAccessManager(this);
    QNetworkRequest req(posterUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_posterNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll()))
            return;
        const QString path = posterCachePath(imdbId);
        QDir().mkpath(QFileInfo(path).absolutePath());
        pm.save(path, "JPG");
        const QPixmap scaled = pm.scaled(96, 144, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
        for (QHash<QString, DownloadCardRefs>* map : {&m_historyCards, &m_activeCards}) {
            auto it = map->constFind(imdbId);
            if (it != map->constEnd()) {
                if (auto* lbl = qobject_cast<QLabel*>(it->posterWidget)) {
                    lbl->setText(QString());
                    lbl->setPixmap(scaled);
                }
            }
        }
    });
}

void StreamDownloadsPage::onMetaItemReady(const tankostream::addon::MetaItem& item)
{
    const QString imdbId = item.preview.id;
    savePosterFrom(imdbId, item.preview.poster);

    for (QHash<QString, DownloadCardRefs>* map : {&m_historyCards, &m_activeCards}) {
        auto it = map->find(imdbId);
        if (it == map->end())
            continue;
        DownloadCardRefs& refs = it.value();
        if (!item.preview.name.isEmpty() && refs.titleLabel)
            refs.titleLabel->setText(item.preview.name);

        // Movie row (single entry) — enrich to the catalog name.
        if (!item.preview.name.isEmpty()) {
            auto mit = refs.rowTitleByKey.constFind(QStringLiteral("movie"));
            if (mit != refs.rowTitleByKey.constEnd() && mit.value())
                mit.value()->setText(item.preview.name);
        }

        for (const tankostream::addon::Video& v : item.videos) {
            if (!v.seriesInfo.has_value() || v.title.isEmpty())
                continue;
            const int s = v.seriesInfo->season;
            const int ep = v.seriesInfo->episode;
            auto rit = refs.rowTitleByKey.constFind(QStringLiteral("%1:%2").arg(s).arg(ep));
            if (rit != refs.rowTitleByKey.constEnd() && rit.value()) {
                rit.value()->setText(QStringLiteral("S%1E%2")
                                         .arg(s, 2, 10, QLatin1Char('0'))
                                         .arg(ep, 2, 10, QLatin1Char('0'))
                                     + dotSep() + v.title);
            }
        }
    }
}

void StreamDownloadsPage::updateEmptyState()
{
    if (!m_emptyState || !m_activeBody || !m_historyBody
        || !m_activeBodyLayout || !m_historyBodyLayout) {
        return;
    }

    const bool anyActive = !m_activeBody->isHidden() && m_activeBodyLayout->count() > 0;
    const bool anyHistory = !m_historyBody->isHidden() && m_historyBodyLayout->count() > 0;
    m_emptyState->setVisible(!anyActive && !anyHistory);
}

void StreamDownloadsPage::refreshActive()
{
    if (!m_activeBodyLayout)
        return;

    while (auto* item = m_activeBodyLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_activeCards.clear();  // stored handles point at the widgets just torn down

    if (!m_torrentClient) {
        m_activeHeader->setVisible(false);
        m_activeBody->setVisible(false);
        updateEmptyState();
        return;
    }

    const QJsonObject groups = m_torrentClient->streamBulkGroups();
    if (groups.isEmpty()) {
        m_activeHeader->setVisible(false);
        m_activeBody->setVisible(false);
        updateEmptyState();
        return;
    }

    // Group by imdbId. Each group's items[] array carries the per-episode
    // state machine entries. We display one card per show (imdbId), with
    // an aggregated progress label and a per-episode summary count.
    QHash<QString, QList<QJsonObject>> byImdb;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        const QJsonObject group = it.value().toObject();
        const QJsonObject sourceIds = group.value(QStringLiteral("sourceIds")).toObject();
        QString imdbId = group.value(QStringLiteral("imdbId")).toString();
        if (imdbId.isEmpty())
            imdbId = sourceIds.value(QStringLiteral("seriesId")).toString();
        if (imdbId.isEmpty())
            continue;
        byImdb[imdbId].append(group);
    }

    if (byImdb.isEmpty()) {
        m_activeHeader->setVisible(false);
        m_activeBody->setVisible(false);
        updateEmptyState();
        return;
    }

    m_activeHeader->setVisible(true);
    m_activeBody->setVisible(true);

    QStringList imdbsSorted = byImdb.keys();
    std::sort(imdbsSorted.begin(), imdbsSorted.end());
    for (const QString& imdbId : imdbsSorted) {
        const QList<QJsonObject>& showGroups = byImdb[imdbId];

        // Aggregate across this show's season groups into one progress summary.
        QSet<int> seasons;
        int total = 0, done = 0, active = 0, pending = 0, failed = 0;
        for (const QJsonObject& group : showGroups) {
            const int season = group.value(QStringLiteral("season")).toInt(
                group.value(QStringLiteral("sourceIds")).toObject()
                     .value(QStringLiteral("season")).toInt(0));
            if (season > 0) seasons.insert(season);
            const QJsonArray items = group.value(QStringLiteral("items")).toArray();
            for (const auto& v : items) {
                ++total;
                const QString state = v.toObject().value(QStringLiteral("itemState")).toString();
                if (state == QLatin1String("Published") || state == QLatin1String("Completed"))
                    ++done;
                else if (state == QLatin1String("Downloading") || state == QLatin1String("Publishing"))
                    ++active;
                else if (state == QLatin1String("Pending"))
                    ++pending;
                else
                    ++failed;
            }
        }

        const QString showTitle = bestTitleFromGroups(showGroups, imdbId);

        auto* card = new QFrame(m_activeBody);
        card->setObjectName("StreamDownloadsActiveCard");
        card->setStyleSheet(
            "QFrame#StreamDownloadsActiveCard {"
            "  background: rgba(255,255,255,0.038);"
            "  border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 12px;"
            "}");
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        DownloadCardRefs refs;
        refs.card = card;
        refs.posterWidget = makePosterWidget(imdbId, showTitle);
        h->addWidget(refs.posterWidget, 0, Qt::AlignTop);

        auto* right = new QVBoxLayout();
        right->setContentsMargins(16, 13, 16, 13);
        right->setSpacing(6);

        refs.titleLabel = new QLabel(showTitle, card);
        refs.titleLabel->setObjectName("StreamDownloadsShowTitle");
        refs.titleLabel->setStyleSheet(
            "QLabel#StreamDownloadsShowTitle { color: #ededed; font-size: 15px; font-weight: 600; }");
        right->addWidget(refs.titleLabel);

        auto* sub = new QLabel(card);
        QString subText = tr("%1 of %2 downloaded").arg(done).arg(total);
        if (seasons.size() == 1)
            subText = tr("Season %1").arg(*seasons.begin()) + dotSep() + subText;
        sub->setText(subText);
        sub->setStyleSheet("color: rgba(255,255,255,0.55); font-size: 12px;");
        right->addWidget(sub);

        // Grayscale progress bar — fill/empty via layout stretch (responsive).
        auto* track = new QFrame(card);
        track->setFixedHeight(4);
        track->setStyleSheet("background: rgba(255,255,255,0.12); border-radius: 2px;");
        auto* tl = new QHBoxLayout(track);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(0);
        auto* fill = new QFrame(track);
        fill->setStyleSheet("background: rgba(255,255,255,0.55); border-radius: 2px;");
        tl->addWidget(fill, done);
        tl->addStretch(total > done ? total - done : 0);
        right->addWidget(track);

        QStringList parts;
        if (active > 0) parts << tr("%1 downloading").arg(active);
        if (pending > 0) parts << tr("%1 queued").arg(pending);
        if (failed > 0) parts << tr("%1 stuck").arg(failed);
        if (!parts.isEmpty()) {
            auto* state = new QLabel(parts.join(dotSep()), card);
            state->setStyleSheet("color: rgba(255,255,255,0.50); font-size: 11px;");
            right->addWidget(state);
        }

        h->addLayout(right, 1);

        m_activeCards.insert(imdbId, refs);
        m_activeBodyLayout->addWidget(card);

        if (m_metaAggregator)
            m_metaAggregator->fetchMetaItem(imdbId, QStringLiteral("series"));
    }

    updateEmptyState();
}

void StreamDownloadsPage::refreshHistory()
{
    if (!m_historyBodyLayout)
        return;

    while (auto* item = m_historyBodyLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_historyCards.clear();  // stored handles point at the widgets just torn down

    if (!m_streamDownloadIndex) {
        m_historyHeader->setVisible(false);
        m_historyBody->setVisible(false);
        updateEmptyState();
        return;
    }

    const QList<StreamDownloadIndex::Entry> all = m_streamDownloadIndex->all();
    if (all.isEmpty()) {
        m_historyHeader->setVisible(false);
        m_historyBody->setVisible(false);
        updateEmptyState();
        return;
    }

    QHash<QString, QList<StreamDownloadIndex::Entry>> byImdb;
    for (const auto& e : all) {
        if (e.state != StreamDownloadIndex::Entry::Complete)
            continue;
        byImdb[e.imdbId].append(e);
    }

    if (byImdb.isEmpty()) {
        m_historyHeader->setVisible(false);
        m_historyBody->setVisible(false);
        updateEmptyState();
        return;
    }

    m_historyHeader->setVisible(true);
    m_historyBody->setVisible(true);

    QStringList imdbsSorted = byImdb.keys();
    std::sort(imdbsSorted.begin(), imdbsSorted.end(),
              [&byImdb](const QString& a, const QString& b) {
                  qint64 maxA = 0, maxB = 0;
                  for (const auto& e : byImdb[a]) maxA = std::max(maxA, e.addedAt);
                  for (const auto& e : byImdb[b]) maxB = std::max(maxB, e.addedAt);
                  return maxA > maxB;
              });

    for (const QString& imdbId : imdbsSorted) {
        QList<StreamDownloadIndex::Entry> entries = byImdb[imdbId];
        std::sort(entries.begin(), entries.end(),
                  [](const StreamDownloadIndex::Entry& a, const StreamDownloadIndex::Entry& b) {
                      if (a.season != b.season) return a.season < b.season;
                      return a.episode < b.episode;
                  });

        const bool isMovie = entries.first().type == QLatin1String("movie");
        const QString showTitle = bestTitleFromEntries(entries, imdbId);

        auto* card = new QFrame(m_historyBody);
        card->setObjectName("StreamDownloadsHistoryCard");
        card->setStyleSheet(
            "QFrame#StreamDownloadsHistoryCard {"
            "  background: rgba(255,255,255,0.038);"
            "  border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 12px;"
            "}");
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        DownloadCardRefs refs;
        refs.card = card;
        refs.posterWidget = makePosterWidget(imdbId, showTitle);
        h->addWidget(refs.posterWidget, 0, Qt::AlignTop);

        auto* right = new QVBoxLayout();
        right->setContentsMargins(16, 13, 16, 11);
        right->setSpacing(3);

        refs.titleLabel = new QLabel(showTitle, card);
        refs.titleLabel->setObjectName("StreamDownloadsShowTitle");
        refs.titleLabel->setStyleSheet(
            "QLabel#StreamDownloadsShowTitle { color: #ededed; font-size: 15px; font-weight: 600; }");
        right->addWidget(refs.titleLabel);

        auto* sub = new QLabel(card);
        sub->setText(isMovie ? tr("Movie") : tr("%n episode(s)", "", entries.size()));
        sub->setStyleSheet("color: rgba(255,255,255,0.55); font-size: 12px;");
        right->addWidget(sub);

        auto* rows = new QVBoxLayout();
        rows->setContentsMargins(0, 8, 0, 0);
        rows->setSpacing(2);

        for (const auto& e : entries) {
            auto* row = new QPushButton(card);
            row->setObjectName("StreamDownloadsHistoryRow");
            row->setCursor(Qt::PointingHandCursor);
            row->setFlat(true);
            row->setStyleSheet(
                "QPushButton#StreamDownloadsHistoryRow {"
                "  text-align: left; padding: 7px 10px; color: rgba(255,255,255,0.86);"
                "  font-size: 13px; border: none; background: transparent;"
                "}"
                "QPushButton#StreamDownloadsHistoryRow:hover {"
                "  background: rgba(255,255,255,0.065); border-radius: 6px;"
                "}");

            // Title-only. Placeholder = prettified filename; metaItemReady swaps
            // in the real catalog title (rebuilding the SxxExx · title text).
            const QString placeholder = prettifyFilenameTitle(e.canonicalPath);
            if (isMovie) {
                row->setText(placeholder.isEmpty() ? showTitle : placeholder);
                refs.rowTitleByKey.insert(QStringLiteral("movie"), row);
            } else {
                row->setText(QStringLiteral("S%1E%2")
                                 .arg(e.season, 2, 10, QLatin1Char('0'))
                                 .arg(e.episode, 2, 10, QLatin1Char('0'))
                             + dotSep() + placeholder);
                refs.rowTitleByKey.insert(
                    QStringLiteral("%1:%2").arg(e.season).arg(e.episode), row);
            }

            const QString canonicalPath = e.canonicalPath;
            const QString rowImdb = e.imdbId;
            const int rowSeason = e.season;
            const int rowEpisode = e.episode;
            connect(row, &QPushButton::clicked, this,
                    [this, canonicalPath, rowImdb, rowSeason, rowEpisode]() {
                        emit playLocalFileRequested(canonicalPath, rowImdb,
                                                    QFileInfo(canonicalPath).completeBaseName(),
                                                    rowSeason, rowEpisode);
                    });
            rows->addWidget(row);
        }

        right->addLayout(rows);
        h->addLayout(right, 1);

        m_historyCards.insert(imdbId, refs);
        m_historyBodyLayout->addWidget(card);

        if (m_metaAggregator)
            m_metaAggregator->fetchMetaItem(
                imdbId, isMovie ? QStringLiteral("movie") : QStringLiteral("series"));
    }

    updateEmptyState();
}
