#include "StreamDownloadsPage.h"

#include "core/torrent/TorrentClient.h"
#include "core/stream/StreamDownloadIndex.h"

#include <QFrame>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace {

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

        auto* card = new QFrame(m_activeBody);
        card->setObjectName("StreamDownloadsActiveCard");
        card->setStyleSheet(
            "QFrame#StreamDownloadsActiveCard {"
            "  background: rgba(255,255,255,0.04);"
            "  border-radius: 8px;"
            "  padding: 12px;"
            "}");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(6);

        const QString showTitle = bestTitleFromGroups(showGroups, imdbId);
        auto* showLabel = new QLabel(showTitle, card);
        showLabel->setObjectName("StreamDownloadsShowTitle");
        showLabel->setStyleSheet(
            "QLabel#StreamDownloadsShowTitle { color: #eeeeee; font-size: 12pt; font-weight: 600; }");
        cardLayout->addWidget(showLabel);

        if (showTitle != imdbId) {
            auto* imdbLabel = new QLabel(imdbId, card);
            imdbLabel->setObjectName("StreamDownloadsShowMeta");
            imdbLabel->setStyleSheet(
                "QLabel#StreamDownloadsShowMeta { color: rgba(255,255,255,0.42); font-size: 9pt; }");
            cardLayout->addWidget(imdbLabel);
        }

        for (const QJsonObject& group : showGroups) {
            const int season = group.value(QStringLiteral("season")).toInt(
                group.value(QStringLiteral("sourceIds")).toObject()
                     .value(QStringLiteral("season")).toInt(0));
            const QJsonArray items = group.value(QStringLiteral("items")).toArray();

            int total = items.size();
            int done = 0, active = 0, pending = 0, failed = 0;
            for (const auto& v : items) {
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

            auto* groupLabel = new QLabel(card);
            const QString summary = tr("Season %1 - %2 of %3 done")
                                        .arg(season > 0 ? QString::number(season) : QStringLiteral("?"))
                                        .arg(done)
                                        .arg(total);
            QString statusLine = summary;
            if (active > 0) statusLine += tr("  -  %1 active").arg(active);
            if (pending > 0) statusLine += tr("  -  %1 queued").arg(pending);
            if (failed > 0) statusLine += tr("  -  %1 stuck").arg(failed);
            groupLabel->setText(statusLine);
            groupLabel->setStyleSheet(
                "color: rgba(255,255,255,0.75); font-size: 10pt; padding-top: 2px;");
            cardLayout->addWidget(groupLabel);
        }

        m_activeBodyLayout->addWidget(card);
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

        auto* card = new QFrame(m_historyBody);
        card->setObjectName("StreamDownloadsHistoryCard");
        card->setStyleSheet(
            "QFrame#StreamDownloadsHistoryCard {"
            "  background: rgba(255,255,255,0.04);"
            "  border-radius: 8px;"
            "}");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(4);

        const QString showTitle = bestTitleFromEntries(entries, imdbId);
        auto* showLabel = new QLabel(card);
        showLabel->setObjectName("StreamDownloadsShowTitle");
        const QString showHeader = entries.first().type == QLatin1String("movie")
            ? showTitle
            : tr("%1  -  %2 episodes").arg(showTitle).arg(entries.size());
        showLabel->setText(showHeader);
        showLabel->setStyleSheet(
            "QLabel#StreamDownloadsShowTitle { color: #eeeeee; font-size: 12pt; font-weight: 600; }");
        cardLayout->addWidget(showLabel);

        if (showTitle != imdbId) {
            auto* imdbLabel = new QLabel(imdbId, card);
            imdbLabel->setObjectName("StreamDownloadsShowMeta");
            imdbLabel->setStyleSheet(
                "QLabel#StreamDownloadsShowMeta { color: rgba(255,255,255,0.42); font-size: 9pt; }");
            cardLayout->addWidget(imdbLabel);
        }

        for (const auto& e : entries) {
            auto* row = new QPushButton(card);
            row->setObjectName("StreamDownloadsHistoryRow");
            row->setCursor(Qt::PointingHandCursor);
            row->setFlat(true);
            row->setStyleSheet(
                "QPushButton#StreamDownloadsHistoryRow {"
                "  text-align: left; padding: 6px 8px; color: rgba(255,255,255,0.85);"
                "  font-size: 10pt; border: none; background: transparent;"
                "}"
                "QPushButton#StreamDownloadsHistoryRow:hover {"
                "  background: rgba(255,255,255,0.06); border-radius: 4px;"
                "}");
            QString rowLabel;
            if (e.type == QLatin1String("movie")) {
                rowLabel = QFileInfo(e.canonicalPath).fileName();
            } else {
                rowLabel = tr("S%1E%2  -  %3")
                    .arg(e.season, 2, 10, QLatin1Char('0'))
                    .arg(e.episode, 2, 10, QLatin1Char('0'))
                    .arg(QFileInfo(e.canonicalPath).fileName());
            }
            row->setText(rowLabel);

            const QString canonicalPath = e.canonicalPath;
            const QString rowImdb = e.imdbId;
            const int rowSeason = e.season;
            const int rowEpisode = e.episode;
            connect(row, &QPushButton::clicked, this, [this, canonicalPath, rowImdb, rowSeason, rowEpisode]() {
                emit playLocalFileRequested(canonicalPath, rowImdb,
                                            QFileInfo(canonicalPath).completeBaseName(),
                                            rowSeason, rowEpisode);
            });
            cardLayout->addWidget(row);
        }

        m_historyBodyLayout->addWidget(card);
    }

    updateEmptyState();
}
