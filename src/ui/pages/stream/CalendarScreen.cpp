#include "CalendarScreen.h"

#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace tankostream::stream {

namespace {

// Qt::UserRole slots carry: kind tag → meta.imdb → season → episode.
// Kind tag values are "itemKind_bucket" / "itemKind_day" / "itemKind_episode".
constexpr int kRoleKind    = Qt::UserRole;
constexpr int kRoleImdbId  = Qt::UserRole + 1;
constexpr int kRoleSeason  = Qt::UserRole + 2;
constexpr int kRoleEpisode = Qt::UserRole + 3;

}

CalendarScreen::CalendarScreen(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

void CalendarScreen::buildUI()
{
    setObjectName(QStringLiteral("StreamCalendarScreen"));
    setStyleSheet(QStringLiteral(
        "#StreamCalendarScreen { background: transparent; }"
        "#StreamCalendarTitle { color: #e5e7eb; font-size: 14px; font-weight: 600; }"
        "#StreamCalendarStatus { color: #9ca3af; font-size: 11px; }"
        "#StreamCalendarBack, #StreamCalendarRefresh {"
        " color: #d1d5db;"
        " background: rgba(255,255,255,0.07);"
        " border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px;"
        " padding: 4px 10px; }"
        "#StreamCalendarBack:hover, #StreamCalendarRefresh:hover {"
        " border-color: rgba(255,255,255,0.22); }"
        "#StreamCalendarTree {"
        " background: rgba(0,0,0,0.20);"
        " border: 1px solid rgba(255,255,255,0.08);"
        " border-radius: 8px;"
        " color: #d1d5db;"
        " alternate-background-color: rgba(255,255,255,0.03); }"
        "#StreamCalendarTree::item:selected { background: rgba(255,255,255,0.12); }"
        "#StreamCalendarTree QHeaderView::section {"
        " background: rgba(255,255,255,0.05);"
        " border: none;"
        " color: #9ca3af;"
        " font-size: 11px;"
        " padding: 4px; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 8, 16, 12);
    root->setSpacing(8);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_backButton = new QPushButton(tr("Back"), this);
    m_backButton->setObjectName(QStringLiteral("StreamCalendarBack"));
    m_backButton->setCursor(Qt::PointingHandCursor);
    connect(m_backButton, &QPushButton::clicked, this, &CalendarScreen::backRequested);
    header->addWidget(m_backButton);

    m_titleLabel = new QLabel(tr("Calendar"), this);
    m_titleLabel->setObjectName(QStringLiteral("StreamCalendarTitle"));
    header->addWidget(m_titleLabel);
    header->addStretch();

    m_refreshButton = new QPushButton(tr("Refresh"), this);
    m_refreshButton->setObjectName(QStringLiteral("StreamCalendarRefresh"));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    connect(m_refreshButton, &QPushButton::clicked, this, &CalendarScreen::refreshRequested);
    header->addWidget(m_refreshButton);

    root->addLayout(header);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("StreamCalendarStatus"));
    m_statusLabel->setText(QString());
    m_statusLabel->hide();
    root->addWidget(m_statusLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("StreamCalendarTree"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels(
        {tr("Date"), tr("Series"), tr("Episode"), tr("Title")});
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(0, 120);
    m_tree->setColumnWidth(1, 240);
    m_tree->setColumnWidth(2, 80);
    root->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item, int /*column*/) {
            if (!item) return;
            if (item->data(0, kRoleKind).toString() != QLatin1String("itemKind_episode")) {
                return;
            }
            const QString imdbId = item->data(0, kRoleImdbId).toString();
            const int     season = item->data(0, kRoleSeason).toInt();
            const int     episode = item->data(0, kRoleEpisode).toInt();
            if (!imdbId.isEmpty() && season >= 0 && episode >= 0) {
                emit seriesEpisodeActivated(imdbId, season, episode);
            }
        });
}

void CalendarScreen::setLoading(bool loading)
{
    if (loading) {
        m_statusLabel->setText(tr("Loading calendar..."));
        m_statusLabel->show();
    } else if (m_statusLabel->text() == tr("Loading calendar...")) {
        m_statusLabel->clear();
        m_statusLabel->hide();
    }
}

void CalendarScreen::setItems(const QList<CalendarItem>& items)
{
    renderGroups(regroupFromFlat(items));
}

void CalendarScreen::setGroupedItems(const QList<CalendarDayGroup>& groups)
{
    renderGroups(groups);
}

void CalendarScreen::setError(const QString& message)
{
    m_tree->clear();
    m_statusLabel->setText(message);
    m_statusLabel->show();
}

void CalendarScreen::renderGroups(const QList<CalendarDayGroup>& groups)
{
    m_tree->clear();

    if (groups.isEmpty()) {
        m_statusLabel->setText(tr("No upcoming episodes in the next 60 days."));
        m_statusLabel->show();
        return;
    }

    // Fold day groups into their bucket, filter invalid/empty.
    QMap<int, QList<CalendarDayGroup>> byBucket;
    int totalRows = 0;
    for (const CalendarDayGroup& group : groups) {
        if (!group.day.isValid() || group.items.isEmpty()) continue;
        byBucket[bucketOrder(group.bucket)].push_back(group);
        totalRows += group.items.size();
    }

    if (totalRows == 0) {
        m_statusLabel->setText(tr("No upcoming episodes in the next 60 days."));
        m_statusLabel->show();
        return;
    }

    for (auto bucketIt = byBucket.constBegin(); bucketIt != byBucket.constEnd(); ++bucketIt) {
        const QList<CalendarDayGroup>& bucketGroups = bucketIt.value();
        if (bucketGroups.isEmpty()) continue;

        const CalendarBucket bucketKind = bucketGroups.first().bucket;
        auto* bucketItem = new QTreeWidgetItem(m_tree);
        bucketItem->setText(0, bucketTitle(bucketKind));
        bucketItem->setData(0, kRoleKind, QLatin1String("itemKind_bucket"));
        bucketItem->setFirstColumnSpanned(true);
        bucketItem->setExpanded(true);

        QList<CalendarDayGroup> sortedDays = bucketGroups;
        std::sort(sortedDays.begin(), sortedDays.end(),
            [](const CalendarDayGroup& a, const CalendarDayGroup& b) {
                return a.day < b.day;
            });

        for (const CalendarDayGroup& dayGroup : sortedDays) {
            auto* dayItem = new QTreeWidgetItem(bucketItem);
            dayItem->setText(0, formatDay(dayGroup.day));
            dayItem->setData(0, kRoleKind, QLatin1String("itemKind_day"));
            dayItem->setFirstColumnSpanned(true);
            dayItem->setExpanded(true);

            for (const CalendarItem& item : dayGroup.items) {
                auto* row = new QTreeWidgetItem(dayItem);
                row->setText(0, dayGroup.day.toString(QStringLiteral("dd MMM")));
                row->setText(1, item.meta.name);
                row->setText(2, formatEpisodeCode(item.video));
                row->setText(3, item.video.title);

                row->setData(0, kRoleKind, QLatin1String("itemKind_episode"));
                row->setData(0, kRoleImdbId, item.meta.id);
                row->setData(0, kRoleSeason,
                             item.video.seriesInfo.has_value()
                                 ? item.video.seriesInfo->season
                                 : -1);
                row->setData(0, kRoleEpisode,
                             item.video.seriesInfo.has_value()
                                 ? item.video.seriesInfo->episode
                                 : -1);
            }
        }
    }

    m_statusLabel->setText(tr("%n upcoming episode(s)", "", totalRows));
    m_statusLabel->show();
}

QList<CalendarDayGroup> CalendarScreen::regroupFromFlat(const QList<CalendarItem>& items)
{
    QMap<QDate, QList<CalendarItem>> byDay;
    const QDate today = QDate::currentDate();

    for (const CalendarItem& item : items) {
        const QDate day = item.video.released.toUTC().date();
        if (!day.isValid()) continue;
        byDay[day].push_back(item);
    }

    // Week starts Monday — mirror CalendarEngine::classifyBucket.
    const QDate weekStart      = today.addDays(1 - today.dayOfWeek());
    const QDate nextWeekStart  = weekStart.addDays(7);
    const QDate weekAfterStart = weekStart.addDays(14);

    QList<CalendarDayGroup> out;
    out.reserve(byDay.size());
    for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) {
        CalendarDayGroup g;
        g.day   = it.key();
        g.items = it.value();
        if (g.day < nextWeekStart)       g.bucket = CalendarBucket::ThisWeek;
        else if (g.day < weekAfterStart) g.bucket = CalendarBucket::NextWeek;
        else                             g.bucket = CalendarBucket::Later;
        out.push_back(g);
    }
    return out;
}

QString CalendarScreen::bucketTitle(CalendarBucket bucket)
{
    switch (bucket) {
    case CalendarBucket::ThisWeek: return tr("This Week");
    case CalendarBucket::NextWeek: return tr("Next Week");
    case CalendarBucket::Later:    return tr("Later");
    }
    return tr("Later");
}

int CalendarScreen::bucketOrder(CalendarBucket bucket)
{
    switch (bucket) {
    case CalendarBucket::ThisWeek: return 0;
    case CalendarBucket::NextWeek: return 1;
    case CalendarBucket::Later:    return 2;
    }
    return 2;
}

QString CalendarScreen::formatEpisodeCode(const tankostream::addon::Video& video)
{
    if (!video.seriesInfo.has_value()) return QStringLiteral("-");
    const int season  = video.seriesInfo->season;
    const int episode = video.seriesInfo->episode;
    return QStringLiteral("S%1E%2")
        .arg(season,  2, 10, QLatin1Char('0'))
        .arg(episode, 2, 10, QLatin1Char('0'));
}

QString CalendarScreen::formatDay(const QDate& day)
{
    return day.toString(QStringLiteral("ddd, dd MMM yyyy"));
}

}
